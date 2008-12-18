/* Yash: yet another shell */
/* lineedit.c: command line editing */
/* (C) 2007-2008 magicant */

/* This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.  */


#include "../common.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include "../history.h"
#include "../job.h"
#include "../option.h"
#include "../sig.h"
#include "../strbuf.h"
#include "../util.h"
#include "key.h"
#include "keymap.h"
#include "lineedit.h"
#include "terminfo.h"
#include "trie.h"


static void yle_wprintf(const wchar_t *format, ...)
    __attribute__((nonnull(1)));
static void print_color_seq(const wchar_t **sp)
    __attribute__((nonnull));

static void reader_init(void);
static void reader_finalize(void);
static void read_next(void);
static inline trieget_T make_trieget(const wchar_t *keyseq)
    __attribute__((nonnull,const));


/* True if `yle_setupterm' should be called in the next call to `yle_setup'. */
bool yle_need_term_reset = true;


/* The current cursor position. */
/* 0 <= yle_line < lines, 0 <= yle_column < columns */
int yle_line, yle_column;
/* If false, `yle_line' and `yle_column' are not changed when characters are
 * printed. */
bool yle_counting = true;
/* If true, there are no characters printed after the cursor.
 * In this case, when a character is inserted at the cursor, redraw is not
 * needed after the cursor: we can simply echo the inserted character. */
bool yle_cursor_appendable;
/* If true, some information is printed below the edit line.
 * A typical example of such info is completion candidates.
 * This info must be cleared when editing is finished. */
bool yle_additional_info_printed;

/* The cursor position of the first character of the edit line, just after the
 * prompt. */
int yle_editbase_line, yle_editbase_column;


/* The state of lineedit. */
static enum { MODE_INACTIVE, MODE_ACTIVE, MODE_SUSPENDED, } mode;
/* The state of editing. */
yle_state_T yle_state;

/* The main buffer where the command line is edited. */
xwcsbuf_T yle_main_buffer;
/* The position of the cursor on the command line. */
/* 0 <= yle_main_buffer_index <= yle_main_buffer.length */
size_t yle_main_buffer_index;


/* Initializes line editing.
 * Must be called at least once before call to `yle_setup'.
 * May be called more than once, but does nothing for the second call or later.
 */
void yle_init(void)
{
    static bool initialized = false;

    if (initialized)
	return;
    initialized = true;
    yle_init_keymap();
}

/* Initializes line editing.
 * Must be called before each call to `yle_readline'.
 * Returns true iff successful.
 * If this function returns false, the vi/emacs option is unset and
 * `yle_readline' must not be called. */
bool yle_setup(void)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDERR_FILENO))
	return false;
    if (!yle_need_term_reset)
	return true;

    if (yle_setupterm()) {
	yle_need_term_reset = false;
	return true;
    }
    return false;
}

/* Prints the specified `prompt' and reads one line from stdin.
 * This function can be called only after `yle_setup' succeeded.
 * The `prompt' may contain backslash escapes specified in "input.c".
 * The result is returned as a newly malloced wide string, including the
 * trailing newline. When EOF is encountered or on error, an empty string is
 * returned. NULL is returned when interrupted. */
wchar_t *yle_readline(const wchar_t *prompt)
{
    assert(is_interactive_now);
    assert(!yle_need_term_reset);

    if (mode != MODE_INACTIVE)
	return xwcsdup(L"");

    yle_line = yle_column = 0;
    yle_cursor_appendable = true;

    yle_state = YLE_STATE_EDITING;

    wb_init(&yle_main_buffer);
    yle_main_buffer_index = 0;

    switch (shopt_lineedit) {
	case shopt_vi:
	case shopt_emacs:  // TODO currently, emacs is the same as vi
	    yle_set_mode(YLE_MODE_VI_INSERT);
	    break;
	default:
	    assert(false);
    }

    mode = MODE_ACTIVE;
    yle_set_terminal();
    reader_init();

    yle_print_prompt(prompt);
    fflush(stderr);
    yle_editbase_line = yle_line, yle_editbase_column = yle_column;

    do
	read_next();
    while (yle_state == YLE_STATE_EDITING);

    reader_finalize();
    yle_restore_terminal();
    mode = MODE_INACTIVE;

    if (yle_state == YLE_STATE_INTERRUPTED) {
	wb_destroy(&yle_main_buffer);
	return NULL;
    } else {
	return wb_towcs(&yle_main_buffer);
    }
}

/* Restores the terminal state and clears the whole display temporarily. */
void yle_suspend_readline(void)
{
    if (mode == MODE_ACTIVE) {
	mode = MODE_SUSPENDED;
	//TODO yle_suspend_readline: clear the display
	yle_restore_terminal();
    }
}

/* Resumes line editing suspended by `yle_suspend_readline'. */
void yle_resume_readline(void)
{
    if (mode == MODE_SUSPENDED) {
	mode = MODE_ACTIVE;
	yle_set_terminal();
	//TODO yle_resume_readline: rewrite the display
    }
}


/********** Printing **********/

/* Prints the given wide character to the terminal. */
void yle_print_wc(wchar_t c)
{
    /* Special characters like L'\t' and L'\f' will make yle_{line,column}
     * counted wrong. */
    if (c == L'\n') {
	yle_line++, yle_column = 0;
    } else if (c == L'\r') {
	yle_column = 0;
    } else {
#if HAVE_WCWIDTH
	int width = wcwidth(c);
#else
	int width = (c == L'\0') ? 0 : 1;
#endif
	if (width > 0) {
	    int new_column = yle_column + width;
	    if (new_column < yle_columns)
		yle_column = new_column;
	    else if (new_column == yle_columns)
		yle_line++, yle_column = 0;
	    else
		yle_line++, yle_column = width;
	}
    }
    fprintf(stderr, "%lc", (wint_t) c);
}

/* Prints the given string to the terminal.
 * The first `n' characters are printed at most. */
void yle_print_ws(const wchar_t *s, size_t n)
{
    for (size_t i = 0; i < n && s[n] != L'\0'; i++)
	yle_print_wc(s[n]);
}

/* Formats and prints the given string. */
void yle_wprintf(const wchar_t *format, ...)
{
    va_list args;

    va_start(args, format);

    wchar_t *s = malloc_vwprintf(format, args);
    yle_print_ws(s, SIZE_MAX);
    free(s);

    va_end(args);
}

/* Prints the given prompt, which may contain backslash escapes. */
void yle_print_prompt(const wchar_t *prompt)
{
    /* The backslash escapes are defined in "../input.c". */

    yle_counting = true;

    //assert(yle_line == 0);
    //assert(yle_column == 0);

    const wchar_t *s = prompt;
    while (*s) {
	if (*s != L'\\') {
	    yle_print_wc(*s);
	} else switch (*++s) {
	case L'\0':   yle_print_wc(L'\\');  goto done;
	//case L'\\':   yle_print_wc(L'\\');  break;
	case L'a':    yle_print_wc(L'\a');  break;
	case L'e':    yle_print_wc(L'\033');  break;
	case L'n':    yle_print_wc(L'\n');  break;
	case L'r':    yle_print_wc(L'\r');  break;
	case L'$':    yle_print_wc(geteuid() ? L'$' : L'#');  break;
	default:      yle_print_wc(*s);  break;
	case L'j':    yle_wprintf(L"%zu", job_count());  break;
#if YASH_ENABLE_HISTORY
	case L'!':    yle_wprintf(L"%d", hist_next_number);  break;
#endif
	case L'[':    yle_counting = false;  break;
	case L']':    yle_counting = true;   break;
	case L'f':    print_color_seq(&s);   continue;
	}
	s++;
    }
done:
    yle_counting = true;
}

/* Prints a sequence to change the terminal font.
 * When the function is called, `*sp' must point to the character L'f' after the
 * backslash. When the function returns, `*sp' points to the next character to
 * print. */
void print_color_seq(const wchar_t **sp)
{
    int standout = 0, underline = 0, reverse = 0, blink = 0, dim = 0, bold = 0,
	invisible = 0;
    int fg = -1, bg = -1, op = 0;

    while ((*sp)++, **sp) switch (**sp) {
	case L'k':  fg = 0;  /* black   */  break;
	case L'r':  fg = 1;  /* red     */  break;
	case L'g':  fg = 2;  /* green   */  break;
	case L'y':  fg = 3;  /* yellow  */  break;
	case L'b':  fg = 4;  /* blue    */  break;
	case L'm':  fg = 5;  /* magenta */  break;
	case L'c':  fg = 6;  /* cyan    */  break;
	case L'w':  fg = 7;  /* white   */  break;
	case L'K':  bg = 0;  /* black   */  break;
	case L'R':  bg = 1;  /* red     */  break;
	case L'G':  bg = 2;  /* green   */  break;
	case L'Y':  bg = 3;  /* yellow  */  break;
	case L'B':  bg = 4;  /* blue    */  break;
	case L'M':  bg = 5;  /* magenta */  break;
	case L'C':  bg = 6;  /* cyan    */  break;
	case L'W':  bg = 7;  /* white   */  break;
	case L'd':  op = 1;  break;
	case L's':  standout  = 1;  break;
	case L'u':  underline = 1;  break;
	case L'v':  reverse   = 1;  break;
	case L'n':  blink     = 1;  break;
	case L'i':  dim       = 1;  break;
	case L'o':  bold      = 1;  break;
	case L'x':  invisible = 1;  break;
	default:    goto done;
    }
done:
    if (**sp == L'.')
	(*sp)++;

    yle_print_sgr(standout, underline, reverse, blink, dim, bold, invisible);
    if (op) {  /* restore original color pair */
	yle_print_op();
    }
    if (fg >= 0) { /* set foreground color */
	yle_print_setfg(fg);
    }
    if (bg >= 0) { /* set background color */
	yle_print_setbg(bg);
    }
}


/********** Input Reading **********/

/* The temporary buffer for bytes before conversion to wide characters. */
static xstrbuf_T reader_first_buffer;
/* The conversion state used in reading input. */
static mbstate_t reader_state;
/* The temporary buffer for converted characters. */
static xwcsbuf_T reader_second_buffer;

/* Initializes the state of the reader.
 * Called for each invocation of `yle_readline'. */
void reader_init(void)
{
    sb_init(&reader_first_buffer);
    memset(&reader_state, 0, sizeof reader_state);
    wb_init(&reader_second_buffer);
}

/* Frees memory used by the reader. */
void reader_finalize(void)
{
    sb_destroy(&reader_first_buffer);
    wb_destroy(&reader_second_buffer);
}

/* Reads the next byte from stdin and take all the corresponding actions.
 * May return without doing anything if a signal is caught, etc.
 * The caller must check `yle_state' after return from this function;
 * This function must be called repeatedly while it is `YLE_STATE_EDITING'. */
void read_next(void)
{
    static bool incomplete_wchar = false;

    assert(yle_state == YLE_STATE_EDITING);

    /* wait for and read the next byte */
    block_sigchld_and_sigint();
    wait_for_input(STDIN_FILENO, true);
    unblock_sigchld_and_sigint();

    char c;
    switch (read(STDIN_FILENO, &c, 1)) {
	case 0:
	    return;
	case 1:
	    break;
	case -1:
	    switch (errno) {
		case EAGAIN:
#if EAGAIN != EWOULDBLOCK
		case EWOULDBLOCK:
#endif
		case EINTR:
		    return;
		default:
		    xerror(errno, Ngt("cannot read input"));
		    yle_state = YLE_STATE_ERROR;
		    return;
	    }
	default:
	    assert(false);
    }
    if (yle_meta_bit8 && (c & META_BIT))
	sb_ccat(sb_ccat(&reader_first_buffer, ESCAPE_CHAR), c & ~META_BIT);
    else
	sb_ccat(&reader_first_buffer, c);

    /** process the content in the buffer **/
    if (!incomplete_wchar) {
	while (reader_first_buffer.length > 0) {
	    /* check if `reader_first_buffer' is a special sequence */
	    trieget_T tg;
	    if (reader_first_buffer.contents[0] == yle_interrupt_char)
		tg = make_trieget(Key_interrupt);
	    else if (reader_first_buffer.contents[0] == yle_eof_char)
		tg = make_trieget(Key_eof);
	    else if (reader_first_buffer.contents[0] == yle_kill_char)
		tg = make_trieget(Key_kill);
	    else if (reader_first_buffer.contents[0] == yle_erase_char)
		tg = make_trieget(Key_erase);
	    else if (reader_first_buffer.contents[0] == '\\')
		tg = make_trieget(Key_backslash);
	    else
		tg = trie_get(yle_keycodes,
		    reader_first_buffer.contents, reader_first_buffer.length);
	    switch (tg.type) {
		case TG_NOMATCH:
		    goto process_wide;
		case TG_UNIQUE:
		    sb_remove(&reader_first_buffer, 0, tg.matchlength);
		    wb_cat(&reader_second_buffer, tg.value.keyseq);
		    break;
		case TG_AMBIGUOUS:
		    return;
	    }
	}
    }

    /* convert bytes into wide characters */
process_wide:
    if (reader_first_buffer.length > 0) {
	wchar_t wc;
	size_t n = mbrtowc(&wc, reader_first_buffer.contents,
		reader_first_buffer.length, &reader_state);
	incomplete_wchar = false;
	switch (n) {
	    case 0:            // read null character
		sb_clear(&reader_first_buffer);
		wb_cat(&reader_second_buffer, Key_c_at);
		return;
	    case (size_t) -1:  // conversion error
		yle_alert();
		memset(&reader_state, 0, sizeof reader_state);
		sb_clear(&reader_first_buffer);
		break;
	    case (size_t) -2:  // more bytes needed
		incomplete_wchar = true;
		goto process_wide;
	    default:
		sb_remove(&reader_first_buffer, 0, n);
		wb_wccat(&reader_second_buffer, wc);
		break;
	}
    }

    /* process key mapping for wide characters */
    while (reader_second_buffer.length > 0) {
	trieget_T tg = trie_getw(
		yle_current_mode->keymap, reader_second_buffer.contents);
	switch (tg.type) {
	    case TG_NOMATCH:
		//TODO
		break;
	    case TG_UNIQUE:
		wb_remove(&reader_second_buffer, 0, tg.matchlength);
		//TODO
		break;
	    case TG_AMBIGUOUS:
		return;
	}
    }
}

trieget_T make_trieget(const wchar_t *keyseq)
{
    return (trieget_T) {
	.type = TG_UNIQUE,
	.matchlength = 1,
	.value.keyseq = keyseq,
    };
}


/* vim: set ts=8 sts=4 sw=4 noet: */
