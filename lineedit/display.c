/* Yash: yet another shell */
/* display.c: display control */
/* (C) 2007-2012 magicant */

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
#include "display.h"
#include <assert.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include <sys/stat.h>
#include "../history.h"
#include "../job.h"
#include "../option.h"
#include "../plist.h"
#include "../strbuf.h"
#include "../util.h"
#include "complete.h"
#include "editing.h"
#include "terminfo.h"


/* Characters displayed on the screen by line-editing are divided into three
 * parts: the prompt, the edit line and the candidate area.
 * The prompt is immediately followed by the edit line (on the same line) but
 * the candidate area are always on separate lines.
 * The three parts are printed in this order, from upper to lower.
 * When history search is active, the edit line is replaced by the temporary
 * search result line and the search target line.
 *
 * We have to track the cursor position each time we print a character on the
 * screen, so that we can correctly reposition the cursor later. To do this, we
 * use a buffer (`lebuf') accompanied by a position data. The `lebuf_putwchar'
 * function appends a wide character to the buffer and updates the position data
 * accordingly.
 *
 * In most terminals, when as many characters are printed as the number of
 * the columns, the cursor temporarily sticks to the end of the line. The cursor
 * moves to the next line immediately before the next character is printed. So
 * we must take care not to move the cursor (by the "cub1" capability, etc.)
 * when the cursor is sticking, or we cannot track the cursor position
 * correctly. To deal with this problem, if we finish printing a text at the end
 * of a line, we print a dummy space character and erase it to ensure the cursor
 * is no longer sticking. */


#if HAVE_WCWIDTH
# ifndef wcwidth
extern int wcwidth(wchar_t c);
# endif
#else
# undef wcwidth
# define wcwidth(c) (iswprint(c) ? 1 : 0)
#endif


/********** The Print Buffer **********/

static void lebuf_init_with_max(le_pos_T p, int maxcolumn);
static void lebuf_wprintf(bool convert_cntrl, const wchar_t *format, ...)
    __attribute__((nonnull(2)));
static const wchar_t *print_color_seq(const wchar_t *s)
    __attribute__((nonnull));
static bool lebuf_putchar1_trunc(int c);

/* The print buffer. */
struct lebuf_T lebuf;

/* Initializes the print buffer with the specified position data. */
void lebuf_init(le_pos_T p)
{
    lebuf_init_with_max(p, le_columns);
}

/* Initializes the print buffer with the specified position data.
 * The number of columns in a line is specified by `maxcolumn'.
 * If `maxcolumn' is negative, it is considered as infinite. */
void lebuf_init_with_max(le_pos_T p, int maxcolumn)
{
    lebuf.pos = p;
    lebuf.maxcolumn = maxcolumn;
    sb_init(&lebuf.buf);
}

/* Appends the specified byte to the print buffer without updating the position
 * data. Returns the appended character `c'. */
/* The signature of this function is so defined as to match that of the
 * `putchar' function. */
int lebuf_putchar(int c)
{
    sb_ccat(&lebuf.buf, TO_CHAR(c));
    return c;
}

/* Updates the position data of the print buffer as if a character with the
 * specified width has been printed.
 * The specified width must not be less than zero. */
void lebuf_update_position(int width)
{
    assert(width >= 0);

    int new_column = lebuf.pos.column + width;
    if (lebuf.maxcolumn < 0
	    || new_column < lebuf.maxcolumn
	    || (le_ti_xenl && new_column == lebuf.maxcolumn))
	lebuf.pos.column = new_column;
    else if (new_column == lebuf.maxcolumn)
	lebuf.pos.line++, lebuf.pos.column = 0;
    else
	lebuf.pos.line++, lebuf.pos.column = width;
}

/* Appends the specified wide character to the print buffer without updating the
 * position data. */
void lebuf_putwchar_raw(wchar_t c)
{
    sb_printf(&lebuf.buf, "%lc", (wint_t) c);
}

/* Appends the specified wide character to the print buffer and updates the
 * position data accordingly.
 * If `convert_cntrl' is true, a non-printable character is converted to the
 * '^'-prefixed form or the bracketed form. */
void lebuf_putwchar(wchar_t c, bool convert_cntrl)
{
    int width = wcwidth(c);
    if (width > 0) {
	/* printable character */
	lebuf_update_position(width);
	lebuf_putwchar_raw(c);
    } else {
	/* non-printable character */
	if (!convert_cntrl) {
	    switch (c) {
		case L'\a':
		    lebuf_print_alert(false);
		    return;
		case L'\n':
		    lebuf_print_nel();
		    return;
		case L'\r':
		    lebuf_print_cr();
		    return;
		default:
		    lebuf_putwchar_raw(c);
		    return;
	    }
	} else {
	    if (c < L'\040') {
		lebuf_putwchar(L'^',        false);
		lebuf_putwchar(c + L'\100', true);
	    } else if (c == L'\177') {
		lebuf_putwchar(L'^',        false);
		lebuf_putwchar(L'?',        false);
	    } else {
		lebuf_wprintf(false, L"<%jX>", (uintmax_t) c);
	    }
	}
    }
}

/* Appends the specified string to the print buffer. */
void lebuf_putws(const wchar_t *s, bool convert_cntrl)
{
    for (size_t i = 0; s[i] != L'\0'; i++)
	lebuf_putwchar(s[i], convert_cntrl);
}

/* Appends the formatted string to the print buffer. */
void lebuf_wprintf(bool convert_cntrl, const wchar_t *format, ...)
{
    va_list args;
    wchar_t *s;

    va_start(args, format);
    s = malloc_vwprintf(format, args);
    va_end(args);

    lebuf_putws(s, convert_cntrl);
    free(s);
}

/* Prints the specified prompt string to the print buffer.
 * Escape sequences, which are defined in "../input.c", are handled in this
 * function. */
void lebuf_print_prompt(const wchar_t *s)
{
    le_pos_T save_pos = lebuf.pos;

    while (*s != L'\0') {
	if (*s != L'\\') {
	    lebuf_putwchar(*s, false);
	} else switch (*++s) {
	    default:     lebuf_putwchar(*s,      false);  break;
	    case L'\0':  lebuf_putwchar(L'\\',   false);  goto done;
//	    case L'\\':  lebuf_putwchar(L'\\',   false);  break;
	    case L'a':   lebuf_putwchar(L'\a',   false);  break;
	    case L'e':   lebuf_putwchar(L'\033', false);  break;
	    case L'n':   lebuf_putwchar(L'\n',   false);  break;
	    case L'r':   lebuf_putwchar(L'\r',   false);  break;
	    case L'$':
		lebuf_putwchar(geteuid() != 0 ? L'$' : L'#', false);
		break;
	    case L'j':
		lebuf_wprintf(false, L"%zu", job_count());
		break;
	    case L'!':
		lebuf_wprintf(false, L"%u", next_history_number());
		break;
	    case L'[':   save_pos = lebuf.pos;    break;
	    case L']':   lebuf.pos = save_pos;    break;
	    case L'f':   s = print_color_seq(s);  continue;
	}
	s++;
    }
done:;
}

/* Prints a sequence to change the terminal font.
 * When this function is called, `*s' must be L'f' after the backslash.
 * This function returns a pointer to the character just after the escape
 * sequence. */
const wchar_t *print_color_seq(const wchar_t *s)
{
    assert(s[-1] == L'\\');
    assert(s[ 0] == L'f');

#define SETFG(color) \
	lebuf_print_setfg(LE_COLOR_##color + (s[1] == L't' ? 8 : 0))
#define SETBG(color) \
	lebuf_print_setbg(LE_COLOR_##color + (s[1] == L't' ? 8 : 0))

    for (;;) switch (*++s) {
	case L'k':  SETFG(BLACK);    break;
	case L'r':  SETFG(RED);      break;
	case L'g':  SETFG(GREEN);    break;
	case L'y':  SETFG(YELLOW);   break;
	case L'b':  SETFG(BLUE);     break;
	case L'm':  SETFG(MAGENTA);  break;
	case L'c':  SETFG(CYAN);     break;
	case L'w':  SETFG(WHITE);    break;
	case L'K':  SETBG(BLACK);    break;
	case L'R':  SETBG(RED);      break;
	case L'G':  SETBG(GREEN);    break;
	case L'Y':  SETBG(YELLOW);   break;
	case L'B':  SETBG(BLUE);     break;
	case L'M':  SETBG(MAGENTA);  break;
	case L'C':  SETBG(CYAN);     break;
	case L'W':  SETBG(WHITE);    break;
	case L'd':  lebuf_print_op();     break;
	case L'D':  lebuf_print_sgr0();   break;
	case L's':  lebuf_print_smso();   break;
	case L'u':  lebuf_print_smul();   break;
	case L'v':  lebuf_print_rev();    break;
	case L'n':  lebuf_print_blink();  break;
	case L'i':  lebuf_print_dim();    break;
	case L'o':  lebuf_print_bold();   break;
	case L'x':  lebuf_print_invis();  break;
	default:    if (!iswalnum(*s)) goto done;
    }
done:
    if (*s == L'.')
	s++;
    return s;

#undef SETFG
#undef SETBG
}

/* Like `lebuf_putwchar', but prints only if there are enough column to the
 * right of the current position.
 * Returns false iff there is not enough column.
 * Returns true for a non-printable character. */
bool lebuf_putwchar_trunc(wchar_t c)
{
    int width = wcwidth(c);
    if (width <= 0)
	return true;

    int new_column = lebuf.pos.column + width;
    if (0 <= lebuf.maxcolumn && lebuf.maxcolumn <= new_column)
	return false;
    lebuf.pos.column = new_column;
    lebuf_putwchar_raw(c);
    return true;
}

/* Like `lebuf_putws', but stops printing when the cursor reaches the end of
 * the line. Non-printable characters are ignored. */
void lebuf_putws_trunc(const wchar_t *s)
{
    while (*s != L'\0' && lebuf_putwchar_trunc(*s))
	s++;
}

/* Like `lebuf_putwchar_trunc', but prints a byte instead of a wide character.
 * The width of the character byte is assumed to be 1. */
bool lebuf_putchar1_trunc(int c)
{
    if (0 < lebuf.maxcolumn && lebuf.maxcolumn <= lebuf.pos.column + 1)
	return false;

    lebuf.pos.column += 1;
    sb_ccat(&lebuf.buf, TO_CHAR(c));
    return true;
}


/********** Displaying **********/

typedef struct candpage_T candpage_T;
typedef struct candcol_T candcol_T;

static void finish(void);
static void clear_to_end_of_screen(void);
static void clear_editline(void);
static void maybe_print_promptsp(void);
static void update_editline(void);
static bool current_display_is_uptodate(size_t index)
    __attribute__((pure));
static void check_cand_overwritten(void);
static void update_styler(void);
static void reset_style_before_moving(void);
static void update_right_prompt(void);
static void print_search(void);
static void go_to(le_pos_T p);
static void go_to_index(size_t index);
static void go_to_after_editline(void);
static void fillip_cursor(void);

static void print_candidate_rawvalue(const le_candidate_T *cand)
    __attribute__((nonnull));
static void update_candidates(void);
static void make_pages_and_columns(void);
static bool arrange_candidates(size_t cand_per_col, int totalwidthlimit);
static void divide_candidates_pages(size_t cand_per_col);
static void free_candpage(void *candpage)
    __attribute__((nonnull));
static void free_candcol(void *candcol)
    __attribute__((nonnull));
static void print_candidates_all(void);
static void update_highlighted_candidate(void);
static void print_candidate(const le_candidate_T *cand, const candcol_T *col,
	bool highlight, bool printdesc)
    __attribute__((nonnull));
static void print_candidate_count(size_t pageindex);
static void print_candidate_count_0(void);
static size_t col_of_cand(size_t candindex);
static int col_of_cand_cmp(const void *candindexp, const void *colp)
    __attribute__((nonnull));
static size_t page_of_col(size_t colindex);
static int page_of_col_cmp(const void *colindexp, const void *pagep)
    __attribute__((nonnull));
static size_t select_list_item(size_t index, int offset, size_t listsize)
    __attribute__((const));


/* True when the prompt is displayed on the screen. */
/* The print buffer, `rprompt', and `sprompt' are valid iff `display_active' is
 * true. */
static bool display_active = false;
/* The current cursor position. */
static le_pos_T current_position;
/* The maximum number of lines that have been displayed. */
static int line_max;

/* The set of strings printed as the prompt, right prompt, after prompt.
 * May contain escape sequences. */
static struct promptset_T prompt;

/* The position of the first character of the edit line, just after the prompt.
 */
static le_pos_T editbasepos;
/* The content of the edit line that is currently displayed on the screen. */
static wchar_t *current_editline = NULL;
/* An array of cursor positions of each character in the edit line.
 * If the nth character of `current_editline' is positioned at line `l', column
 * `c', then cursor_positions[n] == l * le_columns + c. */
static int *cursor_positions = NULL;
/* The current index in the edit line that divides the line into two (cf.
 * `le_main_buffer'). */
static size_t current_length = 0;
/* The line number of the last edit line (or the search buffer). */
static int last_edit_line;
/* True when the terminal's current font setting is the one set by the styler
 * prompt. */
static bool styler_active;

/* The line on which the right prompt is displayed.
 * The value is -1 when the right prompt is not displayed. */
static int rprompt_line;
/* The value of the right prompt where escape sequences have been processed. */
static struct {
    char *value;
    size_t length;  /* number of bytes in `value' */
    int width;      /* width of right prompt on screen */
} rprompt;

/* The value of the styler prompt where escape sequences have been processed. */
static struct {
    char *value;
    size_t length;  /* number of bytes in `value' */
} sprompt;

/* The type of completion candidate pages. */
struct candpage_T {
    size_t colindex;  /* index of the first column in this page */
    size_t colcount;  /* number of the columns in this page */
};
/* The type of completion candidate columns. */
struct candcol_T {
    size_t candindex;  /* index of the first candidate in this column */
    size_t candcount;  /* number of the candidates in this column */
    int valuewidth;    /* max width of the candidate values */
    int descwidth;     /* max width of the candidate descriptions */
    int width;         /* total width of the whole column. */
};

/* A list of completion candidate pages.
 * The elements pointed to by `candpages.contents[*]' are of type `candpage_T'.
 */
static plist_T candpages = { .contents = NULL };
/* A list of completion candidate columns.
 * The elements pointed to by `candcols.contents[*]' are of type `candcol_T'.
 * `candcols' is active iff `candpages' is active. */
static plist_T candcols = { .contents = NULL };
/* The index of the candidate that is currently highlighted.
 * An invalid index indicates that no candidate is highlighted. */
static size_t candhighlight;
#define NOHIGHLIGHT ((size_t) -1)
/* The number of the first line on which the candidate area is displayed.
 * When the candidate area is inactive, this value is negative. */
/* When the candidate area is overwritten by the edit line or the right prompt,
 * this value is updated to the first line number of the remaining area. */
static int candbaseline;
/* When the candidate area is overwritten by the edit line or the right prompt,
 * this flag is set. */
static bool candoverwritten;


/* Initializes the display module. */
void le_display_init(struct promptset_T prompt_)
{
    prompt = prompt_;
}

/* Updates the prompt and the edit line, clears the candidate area, and leave
 * the cursor at a blank line.
 * Must be called before `le_editing_finalize'. */
void le_display_finalize(void)
{
    assert(le_search_buffer.contents == NULL);

    le_display_update(false);

    lebuf_print_sgr0();
    go_to_after_editline();
    finish();
}

/* Clears prompt, edit line and candidate area on the screen.
 * If `clear' is true, also prints the "clear" capability. */
void le_display_clear(bool clear)
{
    if (display_active) {
	lebuf_init(current_position);
	lebuf_print_sgr0();
	if (clear)
	    lebuf_print_clear();
	else
	    go_to((le_pos_T) { 0, 0 });
	finish();
    }
}

/* Deactivates the display.
 * Can be called only when `display_active' is true.
 * This function clears the completion candidates, flushes the `lebuf' buffer,
 * and frees data that are no longer in use. */
void finish(void)
{
    assert(display_active);

    clear_to_end_of_screen(), candbaseline = -1;
    le_display_complete_cleanup();

    free(current_editline), current_editline = NULL;
    free(cursor_positions), cursor_positions = NULL;
    free(rprompt.value);
    free(sprompt.value);

    le_display_flush();
    display_active = false;
}

/* Flushes the contents of the print buffer to the standard error and destroys
 * the buffer. */
void le_display_flush(void)
{
    current_position = lebuf.pos;
    fwrite(lebuf.buf.contents, 1, lebuf.buf.length, stderr);
    fflush(stderr);
    sb_destroy(&lebuf.buf);
}

/* Clears the screen area below the cursor.
 * The cursor must be at the beginning of a line. */
void clear_to_end_of_screen(void)
{
    assert(lebuf.pos.column == 0);

    reset_style_before_moving();

    if (lebuf_print_ed()) /* if the terminal has "ed" capability, just use it */
	return;

    int saveline = lebuf.pos.line;
    for (;;) {
	lebuf_print_el();
	if (lebuf.pos.line >= line_max)
	    break;
	lebuf_print_nel();
    }
    go_to((le_pos_T) { saveline, 0 });
}

/* Clears (part of) the edit line on the screen from the current cursor
 * position to the end of the edit line.
 * The prompt and the candidate area are not cleared.
 * When this function is called, the cursor must be positioned within the edit
 * line. When this function returns, the cursor is moved back to that position.
 */
void clear_editline(void)
{
    assert(lebuf.pos.line > editbasepos.line ||
	    (lebuf.pos.line == editbasepos.line &&
	     lebuf.pos.column >= editbasepos.column));

    le_pos_T save_pos = lebuf.pos;

    reset_style_before_moving();
    for (;;) {
	lebuf_print_el();
	if (lebuf.pos.line >= last_edit_line)
	    break;
	lebuf_print_nel();
    }
    rprompt_line = -1;

    go_to(save_pos);
}

/* (Re)prints the display appropriately and, if `cursor' is true, moves the
 * cursor to the proper position.
 * The print buffer must not have been initialized.
 * The output is sent to the print buffer. The buffer needs to be flushed for
 * its contents to be actually displayed. */
void le_display_update(bool cursor)
{
    if (display_active) {
	lebuf_init(current_position);
    } else {
	display_active = true;
	last_edit_line = line_max = 0;
	candhighlight = NOHIGHLIGHT, candbaseline = -1, candoverwritten = false;

	/* prepare the right prompt */
	lebuf_init((le_pos_T) { 0, 0 });
	lebuf_print_sgr0();
	lebuf_print_prompt(prompt.right);
	if (lebuf.pos.line != 0) {  /* right prompt must be one line */
	    sb_clear(&lebuf.buf);
	    /* lebuf.pos.line = */ lebuf.pos.column = 0;
	}
	rprompt.value = lebuf.buf.contents;
	rprompt.length = lebuf.buf.length;
	rprompt.width = lebuf.pos.column;
	rprompt_line = -1;

	/* prepare the styler prompt */
	lebuf_init((le_pos_T) { 0, 0 });
	lebuf_print_sgr0();
	lebuf_print_prompt(prompt.styler);
	if (lebuf.pos.line != 0 || lebuf.pos.column != 0) {
	    /* styler prompt must have no width */
	    sb_clear(&lebuf.buf);
	    /* lebuf.pos.line = lebuf.pos.column = 0; */
	}
	sprompt.value = lebuf.buf.contents;
	sprompt.length = lebuf.buf.length;
	styler_active = false;

	/* print main prompt */
	lebuf_init((le_pos_T) { 0, 0 });
	maybe_print_promptsp();
	lebuf_print_prompt(prompt.main);
	fillip_cursor();
	editbasepos = lebuf.pos;
    }

    if (le_search_buffer.contents == NULL) {
	/* print edit line */
	update_editline();
    } else {
	/* print search line */
	print_search();
	return;
    }

    /* print right prompt */
    update_right_prompt();

    /* print completion candidates */
    update_candidates();

    /* set cursor position */
    assert(le_main_index <= le_main_buffer.length);
    if (cursor) {
	go_to_index(le_main_index);
	update_styler();
    }
}

/* Prints a dummy string that moves the cursor to the first column of the next
 * line if the cursor is not at the first column.
 * This function does nothing if the "le-promptsp" option is not set. */
void maybe_print_promptsp(void)
{
    if (shopt_le_promptsp) {
	lebuf_print_smso();
	lebuf_putchar('$');
	lebuf_print_sgr0();
	for (int count = le_columns - (le_ti_xenl ? 1 : 2); --count >= 0; )
	    lebuf_putchar(' ');
	lebuf_print_cr();
	lebuf_print_ed();
    }
}

/* Prints the content of the edit line.
 * The cursor may be anywhere when this function is called.
 * The cursor is left at an unspecified position when this function returns. */
void update_editline(void)
{
    size_t index = 0;

    if (current_editline != NULL) {
	/* We only reprint what have been changed from the last update:
	 * skip the unchanged part at the beginning of the line. */
	assert(cursor_positions != NULL);

	while (current_editline[index] != L'\0' &&
		current_display_is_uptodate(index))
	    index++;

	/* return if nothing has changed */
	if (current_editline[index] == L'\0'
		&& le_main_buffer.contents[index] == L'\0')
	    return;

	go_to_index(index);
	if (current_editline[index] != L'\0')
	    clear_editline();
    } else {
	/* print the whole edit line */
	go_to(editbasepos);
	clear_editline();
    }

    update_styler();

    // No need to check for overflow in `le_main_buffer.length + 1' here. Should
    // overflow occur, the buffer would not have been allocated successfully.
    current_editline = xreallocn(current_editline,
	    le_main_buffer.length + 1, sizeof *current_editline);
    cursor_positions = xreallocn(cursor_positions,
	    le_main_buffer.length + 1, sizeof *cursor_positions);
    for (;;) {
	wchar_t c = le_main_buffer.contents[index];
	current_editline[index] = c;
	cursor_positions[index]
	    = lebuf.pos.line * lebuf.maxcolumn + lebuf.pos.column;
	if (index == le_main_buffer.length)
	    break;
	if (styler_active && index >= le_main_length) {
	    lebuf_print_sgr0(), styler_active = false;
	    lebuf_print_prompt(prompt.predict);
	}
	lebuf_putwchar(c, true);
	index++;
    }
    lebuf_print_sgr0(), styler_active = false;

    current_length = le_main_length;

    fillip_cursor();

    last_edit_line =
	(lebuf.pos.line >= rprompt_line) ? lebuf.pos.line : rprompt_line;

    /* clear the right prompt if the edit line reaches it. */
    if (rprompt_line == lebuf.pos.line
	    && lebuf.pos.column > lebuf.maxcolumn - rprompt.width - 2) {
	lebuf_print_el();
	rprompt_line = -1;
    } else if (rprompt_line < lebuf.pos.line) {
	rprompt_line = -1;
    }

    /* clear the remaining of the current line if we're overwriting the
     * candidate area. */
    check_cand_overwritten();
}

bool current_display_is_uptodate(size_t index)
{
    if (current_editline[index] != le_main_buffer.contents[index])
	return false;
    if ((index == current_length || index == le_main_length) &&
	    current_length != le_main_length)
	return false;
    return true;
}

/* Sets the `candoverwritten' flag and clears to the end of line if the current
 * position is in the candidate area. */
void check_cand_overwritten(void)
{
    if (0 <= candbaseline && candbaseline <= lebuf.pos.line) {
	lebuf_print_el();
	candbaseline = lebuf.pos.line + 1;
	candoverwritten = true;
    }
}

/* If the styler prompt is inactive, prints it. */
void update_styler(void)
{
    if (!styler_active) {
	sb_ncat_force(&lebuf.buf, sprompt.value, sprompt.length);
	styler_active = true;
    }
}

/* If the "msgr" capability is unavailable, prints the "sgr0" capability. */
/* Cursor-moving capabilities cannot be used in the standout mode unless the
 * "msgr" capability is available. We need to reset text style using the "sgr0"
 * capability before moving cursor. */
void reset_style_before_moving(void)
{
    if (!le_ti_msgr) {
	lebuf_print_sgr0();
	styler_active = false;
    }
}

/* Prints the right prompt if there is enough room in the edit line or if the
 * "le-alwaysrp" option is set.
 * The edit line must have been printed when this function is called. */
void update_right_prompt(void)
{
    int trim = (int)shopt_le_trimright;
    if (rprompt_line >= 0)
	return;
    if (rprompt.width == 0)
	return;
    if (lebuf.maxcolumn - rprompt.width - 2 + trim < 0)
	return;
    int c = cursor_positions[le_main_buffer.length] % lebuf.maxcolumn;
    bool has_enough_room = (c <= lebuf.maxcolumn - rprompt.width - 2 + trim);
    if (!has_enough_room && !shopt_le_alwaysrp)
	return;

    go_to_index(le_main_buffer.length);
    if (!has_enough_room) {
	lebuf_print_nel();
	check_cand_overwritten();
    }
    lebuf_print_cuf(lebuf.maxcolumn - rprompt.width - lebuf.pos.column - 1 + trim);
    sb_ncat_force(&lebuf.buf, rprompt.value, rprompt.length);
    lebuf.pos.column += rprompt.width - trim;
    last_edit_line = rprompt_line = lebuf.pos.line;
    styler_active = false;
}

/* Prints the current search result and the search line.
 * The cursor may be anywhere when this function is called.
 * Characters after the prompt are cleared in this function.
 * When this function returns, the cursor is left after the search line. */
void print_search(void)
{
    assert(le_search_buffer.contents != NULL);

    free(current_editline), current_editline = NULL;
    free(cursor_positions), cursor_positions = NULL;

    go_to(editbasepos);
    clear_editline();

    update_styler();

    if (le_search_result != Histlist)
	lebuf_wprintf(true, L"%s", ashistentry(le_search_result)->value);
    reset_style_before_moving();
    lebuf_print_nel();
    clear_to_end_of_screen(), candbaseline = -1;

    update_styler();

    switch (le_search_type) {
	const char *text;
	case SEARCH_PREFIX:
	    assert(false);
	case SEARCH_VI:
	    switch (le_search_direction) {
		case FORWARD:   lebuf_putwchar(L'?', false);  break;
		case BACKWARD:  lebuf_putwchar(L'/', false);  break;
		default:        assert(false);
	    }
	    break;
	case SEARCH_EMACS:
	    switch (le_search_direction) {
		case FORWARD:   text = "Forward search: ";   break;
		case BACKWARD:  text = "Backward search: ";  break;
		default:        assert(false);
	    }
	    lebuf_wprintf(false, L"%s", gt(text));
	    break;
    }
    lebuf_putws(le_search_buffer.contents, true);

    fillip_cursor();
    last_edit_line = lebuf.pos.line;
}

/* Moves the cursor to the specified position.
 * The target column must be less than `lebuf.maxcolumn'. */
void go_to(le_pos_T p)
{
    if (line_max < lebuf.pos.line)
	line_max = lebuf.pos.line;

    assert(p.line <= line_max);
    assert(p.column < lebuf.maxcolumn);

    if (p.line == lebuf.pos.line) {
	if (lebuf.pos.column == p.column)
	    return;
	reset_style_before_moving();
	if (lebuf.pos.column < p.column)
	    lebuf_print_cuf(p.column - lebuf.pos.column);
	else
	    lebuf_print_cub(lebuf.pos.column - p.column);
	return;
    }

    reset_style_before_moving();
    lebuf_print_cr();
    if (lebuf.pos.line < p.line)
	lebuf_print_cud(p.line - lebuf.pos.line);
    else if (lebuf.pos.line > p.line)
	lebuf_print_cuu(lebuf.pos.line - p.line);
    if (p.column > 0)
	lebuf_print_cuf(p.column);
}

/* Moves the cursor to the character of the specified index in the main buffer.
 * This function relies on `cursor_positions', so `update_editline()' must have
 * been called beforehand. */
void go_to_index(size_t index)
{
    int p = cursor_positions[index];
    go_to((le_pos_T) { .line   = p / lebuf.maxcolumn,
                       .column = p % lebuf.maxcolumn });
}

/* Moves the cursor to the beginning of the line below `last_edit_line'.
 * The edit line (and possibly the right prompt) must have been printed. */
void go_to_after_editline(void)
{
    if (rprompt_line >= 0) {
	go_to((le_pos_T) { rprompt_line, lebuf.maxcolumn - 1 });
	lebuf_print_nel();
    } else {
	go_to_index(le_main_buffer.length);
	if (lebuf.pos.column != 0 || lebuf.pos.line == 0)
	    lebuf_print_nel();
    }
}

/* If the cursor is sticking to the end of line, moves it to the next line. */
void fillip_cursor(void)
{
    if (0 <= lebuf.maxcolumn && lebuf.maxcolumn <= lebuf.pos.column) {
	lebuf_putwchar(L' ',  false);
	lebuf_putwchar(L'\r', false);
	lebuf_print_el();
	if (rprompt_line == lebuf.pos.line)
	    rprompt_line = -1;
    }
}


/* Sets the `raw' and `width' members of candidates in `le_candidates'.
 * This function uses the print buffer to calculate the widths. */
void le_display_make_rawvalues(void)
{
    assert(le_candidates.contents != NULL);

    for (size_t i = 0; i < le_candidates.length; i++) {
	le_candidate_T *cand = le_candidates.contents[i];

	assert(cand->rawvalue.raw == NULL);
	lebuf_init_with_max((le_pos_T) { 0, 0 }, -1);

	print_candidate_rawvalue(cand);

	cand->rawvalue.raw = sb_tostr(&lebuf.buf);
	cand->rawvalue.width = lebuf.pos.column;

	assert(cand->rawdesc.raw == NULL);
	if (cand->desc != NULL) {
	    lebuf_init_with_max((le_pos_T) { 0, 0 }, -1);

	    lebuf_putws_trunc(cand->desc);

	    cand->rawdesc.raw = sb_tostr(&lebuf.buf);
	    cand->rawdesc.width = lebuf.pos.column;
	}
    }
}

/* Prints the "raw value" of the specified candidate to the print buffer.
 * The output is truncated when the cursor reaches the end of the line. */
void print_candidate_rawvalue(const le_candidate_T *cand)
{
    const wchar_t *s = cand->value;

    if (cand->type == CT_FILE) {
	/* skip directory components for a file candidate */
	for (;;) {
	    const wchar_t *ss = wcschr(s, L'/');
	    if (ss == NULL || *++ss == L'\0')
		break;
	    s = ss;
	}
    } else if (cand->type == CT_OPTION) {
	/* prepend a hyphen if none */
	if (cand->value[0] != L'-')
	    lebuf_putwchar_trunc(L'-');
    }

    lebuf_putws_trunc(s);
}

/* Updates the candidate area.
 * The edit line (and the right prompt if any) must have been printed before
 * calling this function.
 * The cursor may be anywhere when this function is called.
 * The cursor is left at an unspecified position when this function returns. */
void update_candidates(void)
{
    if (le_candidates.contents != NULL || candbaseline >= 0) {
	if (candoverwritten)
	    le_display_complete_cleanup();
	if (candpages.contents == NULL) {
	    make_pages_and_columns();
	    print_candidates_all();
	} else if (le_candidates.contents == NULL
		|| candbaseline < 0 || candoverwritten) {
	    print_candidates_all();
	} else {
	    update_highlighted_candidate();
	}
    }
}

/* Arranges the current candidates in `le_candidates' to fit to the screen,
 * making pages and columns of candidates.
 * The edit line (and the right prompt if any) must have been printed before
 * calling this function.
 * The results are assigned to `candpages' and `candcols', which must not be
 * initialized when this function is called.
 * If there are too few lines or columns available to show the candidates on
 * the screen or if there are no candidates, this function does nothing. */
void make_pages_and_columns(void)
{
    assert(candpages.contents == NULL);
    assert(candcols.contents == NULL);

    if (le_candidates.contents == NULL || le_candidates.length == 0)
	return;

    int maxrowi = le_lines - last_edit_line - 1;
    if (maxrowi < 2 || le_columns < 4)
	return;  /* we need at least 2 lines & 4 columns */

    pl_init(&candpages);
    pl_init(&candcols);

#if INT_MAX > SIZE_MAX
    size_t maxrow = (maxrowi > SIZE_MAX) ? SIZE_MAX : (size_t) maxrowi;
#else
    size_t maxrow = (size_t) maxrowi;
#endif

    /* first check if the candidates fit into one page */
    for (size_t cand_per_col = 1; cand_per_col <= maxrow; cand_per_col++) {
	if (arrange_candidates(cand_per_col, le_columns)) {
	    candpage_T *page = xmalloc(sizeof *page);
	    page->colindex = 0;
	    page->colcount = candcols.length;
	    pl_add(&candpages, page);
	    return;
	}
    }

    /* divide the candidate list into pages */
    divide_candidates_pages(maxrow - 1);
}

/* Tries to arrange the current candidates in `le_candidates' into columns that
 * have `cand_per_col' candidates each.
 * `cand_per_col' must be positive.
 * Candidate list `le_candidates' must not be empty.
 * If `totalwidthlimit' is non-negative and if the total width of the resulting
 * columns is >= `totalwidthlimit', then this function fails.
 * On success, the resulting columns are added to `candcols', which must have
 * been initialized as an empty list before calling this function, and true is
 * returned. On failure, `candcols' is not modified and false is returned.
 * This function never modifies `candpages'. */
bool arrange_candidates(size_t cand_per_col, int totalwidthlimit)
{
    int totalwidth = 0;
    size_t candindex = 0;

    assert(cand_per_col > 0);
    assert(candcols.contents[0] == NULL);
    do {
	candcol_T *col = xmalloc(sizeof *col);
	col->candindex = candindex;
	if (le_candidates.length - candindex < cand_per_col)
	    col->candcount = le_candidates.length - candindex;
	else
	    col->candcount = cand_per_col;
	col->valuewidth = col->descwidth = col->width = 0;
	
	for (size_t nextcandindex = candindex + col->candcount;
		candindex < nextcandindex;
		candindex++) {
	    le_candidate_T *cand = le_candidates.contents[candindex];

	    if (col->valuewidth < cand->rawvalue.width)
		col->valuewidth = cand->rawvalue.width;
	    if (col->descwidth < cand->rawdesc.width)
		col->descwidth = cand->rawdesc.width;
	}

	col->valuewidth += 2;
	if (col->descwidth > 0)
	    col->descwidth += 4;
	col->width = col->valuewidth + col->descwidth;
	totalwidth += col->width;
	pl_add(&candcols, col);

	if (totalwidthlimit >= 0 && totalwidth >= totalwidthlimit) {
	    pl_clear(&candcols, free_candcol);
	    return false;
	}
    } while (candindex < le_candidates.length);

    return true;
}

/* Divides the current candidates in `le_candidates' into columns that have
 * `cand_per_col' candidates each and divides the columns into pages that each
 * fit into the screen.
 * Candidate list `le_candidates' must not be empty.
 * The resulting columns and pages are added to `candcols' and `candpages',
 * which must have been initialized as empty lists before calling this function.
 */
void divide_candidates_pages(size_t cand_per_col)
{
    /* divide candidates into columns */
    bool ok = arrange_candidates(cand_per_col, -1);
    assert(ok);

    /* divide columns into pages */
    size_t colindex = 0;
    assert(candpages.contents[0] == NULL);
    do {
	candpage_T *page = xmalloc(sizeof *page);
	int pagewidth;
	candcol_T *col;

	page->colindex = colindex;
	col = candcols.contents[colindex];
	pagewidth = col->width;
	while ((col = candcols.contents[++colindex]) != NULL &&
		pagewidth + col->width < le_columns)
	    pagewidth += col->width;
	page->colcount = colindex - page->colindex;
	/* Each page contains at least one column: The page width may not
	 * necessarily be less than the screen width. */

	pl_add(&candpages, page);
    } while (colindex < candcols.length);  /* col != NULL */
}

/* Clears data used for displaying the candidate area. */
/* Must be called when the current candidates are updated. */
void le_display_complete_cleanup(void)
{
    if (candpages.contents != NULL) {
	plfree(pl_toary(&candpages), free_candpage);
	candpages.contents = NULL;

	assert(candcols.contents != NULL);
	plfree(pl_toary(&candcols), free_candcol);
	candcols.contents = NULL;
    }
    candhighlight = NOHIGHLIGHT;
}

/* Frees a completion candidate page.
 * The argument must point to a `candpage_T' value. */
void free_candpage(void *candpage)
{
    candpage_T *p = candpage;
    free(p);
}

/* Frees a completion candidate column.
 * The argument must point to a `candcol_T' value. */
void free_candcol(void *candcol)
{
    candcol_T *c = candcol;
    free(c);
}

/* Prints the whole candidate area.
 * The edit line (and the right prompt, if any) must have been printed and
 * `make_pages_and_columns' must have been called before calling this function.
 * The cursor may be anywhere when this function is called.
 * The cursor is left at an unspecified position when this function returns.
 * If the candidate list is inactive, this function does nothing but clearing
 * the candidate area. If there are no candidates, an error message is printed.
 */
void print_candidates_all(void)
{
    lebuf_print_sgr0(), styler_active = false;
    go_to_after_editline();
    clear_to_end_of_screen();
    assert(lebuf.pos.column == 0);
    candoverwritten = false;

    if (le_candidates.contents == NULL)
	return;
    if (le_candidates.length == 0) {
	candbaseline = lebuf.pos.line;
	print_candidate_count_0();
	return;
    }
    if (candpages.contents == NULL)
	return;

    candbaseline = lebuf.pos.line;

    size_t pageindex = le_selected_candidate_index < le_candidates.length
	? page_of_col(col_of_cand(le_selected_candidate_index))
	: 0;
    const candpage_T *page = candpages.contents[pageindex];
    const candcol_T *firstcol = candcols.contents[page->colindex];

    for (size_t rowi = 0, rowcount = firstcol->candcount; ; ) {
	int scrcol = 0;

	for (size_t coli = 0; coli < page->colcount; coli++) {
	    const candcol_T *col = candcols.contents[page->colindex + coli];
	    if (rowi >= col->candcount)
		break;

	    while (lebuf.pos.column < scrcol)
		lebuf_putchar1_trunc(' ');

	    size_t candindex = col->candindex + rowi;
	    print_candidate(le_candidates.contents[candindex], col,
		    le_selected_candidate_index == candindex, true);

	    scrcol += col->width;
	}

	rowi++;
	if (rowi >= rowcount)
	    break;
	lebuf_print_nel();
    }

    if (candpages.length > 1) {  /* print status line */
	lebuf_print_nel();
	print_candidate_count(pageindex);
    }

    candhighlight = le_selected_candidate_index;
}

/* Reprints the highlighted candidate.
 * The previously highlighted candidate is reprinted in the normal style and
 * the currently selected candidate is highlighted.
 * Before calling this function, the candidate area must have been printed by
 * `print_candidates_all'.
 * The cursor may be anywhere when this function is called and is left at an
 * unspecified position when this function returns. */
void update_highlighted_candidate(void)
{
    assert(candbaseline >= 0);
    if (le_candidates.length == 0)
	return;
    if (candhighlight == le_selected_candidate_index)
	return;

    size_t oldpageindex, oldcolindex, newpageindex, newcolindex;

    if (candhighlight < le_candidates.length) {
	oldcolindex = col_of_cand(candhighlight);
	oldpageindex = page_of_col(oldcolindex);
    } else {
	oldcolindex = 0;
	oldpageindex = 0;
    }
    if (le_selected_candidate_index < le_candidates.length) {
	newcolindex = col_of_cand(le_selected_candidate_index);
	newpageindex = page_of_col(newcolindex);
    } else {
	newcolindex = 0;
	newpageindex = 0;
    }
    if (oldpageindex != newpageindex) {
	print_candidates_all();
	return;
    }

    lebuf_print_sgr0(), styler_active = false;

    const candpage_T *page = candpages.contents[newpageindex];
    const candcol_T *col;
    int column;
    size_t rowindex;

    /* de-highlight the previous selected candidate */
    if (candhighlight < le_candidates.length) {
	column = 0;
	for (size_t i = page->colindex; i < oldcolindex; i++) {
	    col = candcols.contents[i];
	    column += col->width;
	}
	col = candcols.contents[oldcolindex];
	rowindex = candhighlight - col->candindex;
	go_to((le_pos_T) { .line = candbaseline + (int) rowindex,
			   .column = column });
	print_candidate(
		le_candidates.contents[candhighlight], col, false, false);
    }

    /* highlight the current selected candidate */
    candhighlight = le_selected_candidate_index;
    if (candhighlight < le_candidates.length) {
	column = 0;
	for (size_t i = page->colindex; i < newcolindex; i++) {
	    col = candcols.contents[i];
	    column += col->width;
	}
	col = candcols.contents[newcolindex];
	rowindex = candhighlight - col->candindex;
	go_to((le_pos_T) { .line = candbaseline + (int) rowindex,
			   .column = column });
	print_candidate(
		le_candidates.contents[candhighlight], col, true, false);
    }

    /* print status line */
    if (candpages.length > 1) {
	col = candcols.contents[page->colindex];
	go_to((le_pos_T) { .line = candbaseline + col->candcount,
	                   .column = 0 });
	lebuf_print_el();
	print_candidate_count(newpageindex);
    }
}

/* Prints the specified candidate at the current cursor position.
 * The candidate is highlighted iff `highlight' is true.
 * Iff `printdesc' is true, the candidate's description is printed.
 * The cursor is left just after the printed candidate. */
void print_candidate(const le_candidate_T *cand, const candcol_T *col,
	bool highlight, bool printdesc)
{
    int line = lebuf.pos.line;

    /* print value */
    if (true /* cand->value != NULL */) {
	int base = lebuf.pos.column;
	if (highlight)
	    lebuf_print_bold();
	lebuf_putchar1_trunc(highlight ? '[' : ' ');
	if (lebuf.pos.column + cand->rawvalue.width < lebuf.maxcolumn) {
	    lebuf.pos.column += cand->rawvalue.width;
	    sb_cat(&lebuf.buf, cand->rawvalue.raw);
	} else {
	    print_candidate_rawvalue(cand);
	}
	while (lebuf.pos.column + 2 < lebuf.maxcolumn
		&& lebuf.pos.column - base < col->valuewidth - 1)
	    lebuf_putchar1_trunc(' ');
	if (highlight)
	    lebuf_print_sgr0(), lebuf_print_bold();
	lebuf_putchar1_trunc(highlight ? ']' : ' ');
	if (highlight)
	    lebuf_print_sgr0();
    }

    /* print description */
    if (printdesc && cand->desc != NULL) {
	lebuf_putchar1_trunc(' ');
	lebuf_putchar1_trunc('(');
	if (lebuf.pos.column + cand->rawdesc.width < lebuf.maxcolumn) {
	    lebuf.pos.column += cand->rawdesc.width;
	    sb_cat(&lebuf.buf, cand->rawdesc.raw);
	} else {
	    lebuf_putws_trunc(cand->desc);
	}
	lebuf_putchar1_trunc(')');
    }

    assert(lebuf.pos.line == line);
}

/* Prints the number of the currently selected candidate, the total number of
 * the candidates, the number of the current page, and the total number of the
 * pages at the current position.
 * The index of the page to which the selected candidate belong must be given
 * as `pageindex'. */
void print_candidate_count(size_t pageindex)
{
    size_t sindex = (le_selected_candidate_index < le_candidates.length)
	? le_selected_candidate_index + 1
	: 0;
    char *s1 = malloc_printf(gt("Candidate %zu of %zu; Page %zu of %zu"),
	    sindex, le_candidates.length, pageindex + 1, candpages.length);

    if (s1 != NULL) {
	wchar_t *s2 = realloc_mbstowcs(s1);

	if (s2 != NULL) {
	    lebuf_putws_trunc(s2);
	    free(s2);
	}
    }
}

/* Prints an error message that tells there are no candidates. */
void print_candidate_count_0(void)
{
    wchar_t *s = malloc_mbstowcs(gt("No candidates"));

    if (s != NULL) {
	lebuf_putws_trunc(s);
	free(s);
    }
}

/* Returns the index of the column to which the candidate of index `candindex'
 * belongs. Column list `candcols' must not be empty. */
size_t col_of_cand(size_t candindex)
{
    assert(candcols.length > 0);

    void **colp = bsearch(&candindex,
	    candcols.contents, candcols.length, sizeof *candcols.contents,
	    col_of_cand_cmp);
    assert(colp != NULL);
    return colp - candcols.contents;
}

int col_of_cand_cmp(const void *candindexp, const void *colp)
{
    size_t candindex = *(const size_t *) candindexp;
    const candcol_T *col = *(void **) colp;
    if (candindex < col->candindex)
	return -1;
    else if (candindex < col->candindex + col->candcount)
	return 0;
    else
	return 1;
}

/* Returns the index of the page to which the column of index `colindex'
 * belongs. Page list `candpages' must not be empty. */
size_t page_of_col(size_t colindex)
{
    assert(candpages.length > 0);

    void **pagep = bsearch(&colindex,
	    candpages.contents, candpages.length, sizeof *candpages.contents,
	    page_of_col_cmp);
    assert(pagep != NULL);
    return pagep - candpages.contents;
}

int page_of_col_cmp(const void *colindexp, const void *pagep)
{
    size_t colindex = *(const size_t *) colindexp;
    const candpage_T *page = *(void **) pagep;
    if (colindex < page->colindex)
	return -1;
    else if (colindex < page->colindex + page->colcount)
	return 0;
    else
	return 1;
}

/* Sets `le_selected_candidate_index' to the index of the first candidate of
 * the `offset'th next column, counted from the column containing the currently
 * selected candidate.
 * If `candcols' is empty, `le_selected_candidate_index' is left unchanged. */
void le_display_select_column(int offset)
{
    if (candcols.contents == NULL)
	return;

    size_t colindex = le_selected_candidate_index < le_candidates.length
	? col_of_cand(le_selected_candidate_index)
	: 0;

    colindex = select_list_item(colindex, offset, candcols.length);

    const candcol_T *col = candcols.contents[colindex];
    le_selected_candidate_index = col->candindex;
}

/* Sets `le_selected_candidate_index' to the index of the first candidate of
 * the `offset'th next page, counted from the page containing the currently
 * selected candidate.
 * If `candpages' is empty, `le_selected_candidate_index' is left unchanged. */
void le_display_select_page(int offset)
{
    if (candpages.contents == NULL)
	return;

    size_t pageindex = le_selected_candidate_index < le_candidates.length
	? page_of_col(col_of_cand(le_selected_candidate_index))
	: 0;

    pageindex = select_list_item(pageindex, offset, candpages.length);

    const candpage_T *page = candpages.contents[pageindex];
    const candcol_T *col = candcols.contents[page->colindex];
    le_selected_candidate_index = col->candindex;
}

/* Computes `(index + offset) mod listsize'. */
size_t select_list_item(size_t index, int offset, size_t listsize)
{
    if (offset >= 0) {
	offset %= listsize;
	index += offset;
	index %= listsize;
    } else {
	offset = -offset % listsize;
	if ((size_t) offset <= index)
	    index -= offset;
	else
	    index += listsize - offset;
    }
    assert(index < listsize);
    return index;
}


/********** Utility **********/

/* If the standard error is a terminal, prints the specified prompt to the
 * terminal and returns true. */
bool le_try_print_prompt(const wchar_t *s)
{
    if (isatty(STDERR_FILENO) && le_setupterm(true)) {
	lebuf_init((le_pos_T) { 0, 0 });
	lebuf_print_prompt(s);
	le_display_flush();
	return true;
    } else {
	return false;
    }
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
