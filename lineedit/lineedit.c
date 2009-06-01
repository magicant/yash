/* Yash: yet another shell */
/* lineedit.c: command line editing */
/* (C) 2007-2009 magicant */

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
#include "../option.h"
#include "../sig.h"
#include "../strbuf.h"
#include "../util.h"
#include "display.h"
#include "editing.h"
#include "key.h"
#include "keymap.h"
#include "lineedit.h"
#include "terminfo.h"
#include "trie.h"


static void reader_init(void);
static void reader_finalize(void);
static void read_next(void);
static char pop_prebuffer(void);
static inline bool has_meta_bit(char c)
    __attribute__((pure));
static inline trieget_T make_trieget(const wchar_t *keyseq)
    __attribute__((nonnull,const));
static void append_to_second_buffer(wchar_t wc);
static inline wchar_t wb_get_char(const xwcsbuf_T *buf)
    __attribute__((nonnull,pure));


/* The state of lineedit. */
enum le_state_T le_state;
/* The state of editing. */
enum le_editstate_T le_editstate;


/* Initializes line editing.
 * Must be called before each call to `le_readline'.
 * Returns true iff successful, in which case `le_readline' must be called
 * afterward. */
bool le_setup(void)
{
    static bool initialized = false;
    if (!initialized) {
	initialized = true;
	le_keymap_init();
    }

    return isatty(STDIN_FILENO) && isatty(STDERR_FILENO)
	&& le_setupterm()
	&& le_set_terminal();
}

/* Prints the specified `prompt' and reads one line from stdin.
 * This function can be called after and only after `le_setup' succeeded.
 * The `prompt' may contain backslash escapes specified in "input.c".
 * The result is returned as a newly malloced wide string, including the
 * trailing newline. When EOF is encountered or on error, an empty string is
 * returned. NULL is returned when interrupted. */
wchar_t *le_readline(const wchar_t *prompt)
{
    wchar_t *resultline;

    assert(is_interactive_now);
    assert(le_state == LE_STATE_INACTIVE);

    le_state = LE_STATE_ACTIVE;
    le_editing_init();
    le_display_init(prompt);
    reader_init();
    le_editstate = LE_EDITSTATE_EDITING;

    do
	read_next();
    while (le_editstate == LE_EDITSTATE_EDITING);

    reader_finalize();
    le_display_finalize();
    resultline = le_editing_finalize();
    fflush(stderr);
    le_restore_terminal();
    le_state = LE_STATE_INACTIVE;

    switch (le_editstate) {
	case LE_EDITSTATE_EDITING:
	    assert(false);
	case LE_EDITSTATE_DONE:
	    break;
	case LE_EDITSTATE_ERROR:
	    resultline[0] = L'\0';
	    break;
	case LE_EDITSTATE_INTERRUPTED:
	    free(resultline);
	    resultline = NULL;
	    break;
    }
    return resultline;
}

/* Restores the terminal state and clears the whole display temporarily. */
void le_suspend_readline(void)
{
    if (le_state == LE_STATE_ACTIVE) {
	le_state = LE_STATE_SUSPENDED;
	le_display_clear();
	fflush(stderr);
	le_restore_terminal();
    }
}

/* Resumes line editing suspended by `le_suspend_readline'. */
void le_resume_readline(void)
{
    if (le_state == LE_STATE_SUSPENDED) {
	le_state = LE_STATE_ACTIVE;
	le_setupterm();
	le_set_terminal();
	le_display_print_all(true);
	le_display_reposition_cursor();
	fflush(stderr);
    }
}

/* Re-retrieves the terminfo and reprints everything. */
/* When the display size is changed, this function is called. */
void le_display_size_changed(void)
{
    if (le_state == LE_STATE_ACTIVE) {
	le_display_clear();
	le_setupterm();
	le_display_print_all(false);
	le_display_reposition_cursor();
	fflush(stderr);
    }
}


/********** Input Reading **********/

/* Temporary buffer for bytes that are treated as input. */
static xstrbuf_T reader_prebuffer;
/* Temporary buffer for bytes before conversion to wide characters. */
static xstrbuf_T reader_first_buffer;
/* Conversion state used in reading input. */
static mbstate_t reader_state;
/* Temporary buffer for converted characters. */
static xwcsbuf_T reader_second_buffer;
/* If true, next input will be inserted directly to the main buffer. */
bool le_next_verbatim;

/* Initializes the state of the reader.
 * Called for each invocation of `le_readline'. */
void reader_init(void)
{
    sb_init(&reader_first_buffer);
    memset(&reader_state, 0, sizeof reader_state);
    wb_init(&reader_second_buffer);
    le_next_verbatim = false;
}

/* Frees memory used by the reader. */
void reader_finalize(void)
{
    sb_destroy(&reader_first_buffer);
    wb_destroy(&reader_second_buffer);
}

/* Reads the next byte from stdin and take all the corresponding actions.
 * May return without doing anything if a signal is caught, etc.
 * The caller must check `le_state' after return from this function;
 * This function must be called repeatedly while it is `LE_STATE_EDITING'. */
void read_next(void)
{
    static bool incomplete_wchar = false;

    assert(le_editstate == LE_EDITSTATE_EDITING);

    char c = pop_prebuffer();
    if (c)
	goto direct_first_buffer;

    /* wait for and read the next byte */
    fflush(stderr);
    wait_for_input(STDIN_FILENO, true);
    switch (read(STDIN_FILENO, &c, 1)) {
	case 0:
	    le_editstate = LE_EDITSTATE_ERROR;
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
		    le_editstate = LE_EDITSTATE_ERROR;
		    return;
	    }
	default:
	    assert(false);
    }
    if (has_meta_bit(c))
	sb_ccat(sb_ccat(&reader_first_buffer, ESCAPE_CHAR), c & ~META_BIT);
    else
direct_first_buffer:
	sb_ccat(&reader_first_buffer, c);

    if (incomplete_wchar || le_next_verbatim)
	goto process_wide;

    /** process the content in the buffer **/
    while (reader_first_buffer.length > 0) {
	/* check if `reader_first_buffer' is a special sequence */
	trieget_T tg;
	if (reader_first_buffer.contents[0] == le_interrupt_char)
	    tg = make_trieget(Key_interrupt);
	else if (reader_first_buffer.contents[0] == le_eof_char)
	    tg = make_trieget(Key_eof);
	else if (reader_first_buffer.contents[0] == le_kill_char)
	    tg = make_trieget(Key_kill);
	else if (reader_first_buffer.contents[0] == le_erase_char)
	    tg = make_trieget(Key_erase);
	else if (reader_first_buffer.contents[0] == '\\')
	    tg = make_trieget(Key_backslash);
	else
	    tg = trie_get(le_keycodes,
		reader_first_buffer.contents, reader_first_buffer.length);
	switch (tg.type) {
	    case TG_NOMATCH:
		goto process_wide;
	    case TG_UNIQUE:
		sb_remove(&reader_first_buffer, 0, tg.matchlength);
		wb_cat(&reader_second_buffer, tg.value.keyseq);
		break;
	    case TG_AMBIGUOUS:
		goto process_keymap;
	}
    }

    /* convert bytes into wide characters */
process_wide:
    while (reader_first_buffer.length > 0) {
	wchar_t wc;
	size_t n = mbrtowc(&wc, reader_first_buffer.contents,
		reader_first_buffer.length, &reader_state);
	incomplete_wchar = false;
	switch (n) {
	    case 0:            // read null character
		sb_clear(&reader_first_buffer);
		append_to_second_buffer(L'\0');
		break;
	    case (size_t) -1:  // conversion error
		le_alert();
		memset(&reader_state, 0, sizeof reader_state);
		sb_clear(&reader_first_buffer);
		le_next_verbatim = false;
		goto process_keymap;
	    case (size_t) -2:  // more bytes needed
		incomplete_wchar = true;
		sb_clear(&reader_first_buffer);
		goto process_keymap;
	    default:
		sb_remove(&reader_first_buffer, 0, n);
		append_to_second_buffer(wc);
		break;
	}
    }

    /* process key mapping for wide characters */
process_keymap:
    while (reader_second_buffer.length > 0) {
	trieget_T tg = trie_getw(
		le_current_mode->keymap, reader_second_buffer.contents);
	switch (tg.type) {
	    case TG_NOMATCH:
		le_invoke_command(le_current_mode->default_command,
			wb_get_char(&reader_second_buffer));
		wb_clear(&reader_second_buffer);
		break;
	    case TG_UNIQUE:
		le_invoke_command(tg.value.cmdfunc,
			wb_get_char(&reader_second_buffer));
		wb_remove(&reader_second_buffer, 0, tg.matchlength);
		break;
	    case TG_AMBIGUOUS:
		return;
	}
	if (le_editstate != LE_EDITSTATE_EDITING)
	    break;
    }
}

/* Appends `s' to the prebuffer. The string `s' is freed in this function. */
void append_to_prebuffer(char *s)
{
    if (reader_prebuffer.contents == NULL)
	sb_initwith(&reader_prebuffer, s);
    else
	sb_catfree(&reader_prebuffer, s);
}

/* Removes and returns the next character in the prebuffer if available, or '\0'
 * otherwise. */
char pop_prebuffer(void)
{
    if (reader_prebuffer.contents) {
	char result = reader_prebuffer.contents[0];
	sb_remove(&reader_prebuffer, 0, 1);
	if (reader_prebuffer.length == 0) {
	    sb_destroy(&reader_prebuffer);
	    reader_prebuffer.contents = NULL;
	}
	return result;
    }
    return '\0';
}

bool has_meta_bit(char c)
{
    switch (shopt_le_convmeta) {
	case shopt_yes:
	    break;
	case shopt_no:
	    return false;
	case shopt_auto:
	    if (le_meta_bit8)
		break;
	    else
		return false;
    }
    return c & META_BIT;
}

trieget_T make_trieget(const wchar_t *keyseq)
{
    return (trieget_T) {
	.type = TG_UNIQUE,
	.matchlength = 1,
	.value.keyseq = keyseq,
    };
}

void append_to_second_buffer(wchar_t wc)
{
    if (le_next_verbatim) {
	le_invoke_command(le_current_mode->default_command, wc);
    } else if (wc != L'\0') {
	wb_wccat(&reader_second_buffer, wc);
    }
    le_next_verbatim = false;
}

wchar_t wb_get_char(const xwcsbuf_T *buf)
{
    assert(buf->length > 0);
    return buf->length > 1 ? L'\0' : buf->contents[0];
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
