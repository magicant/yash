/* Yash: yet another shell */
/* display.c: display control */
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
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include "../history.h"
#include "../job.h"
#include "../option.h"
#include "../strbuf.h"
#include "../util.h"
#include "complete.h"
#include "display.h"
#include "editing.h"
#include "terminfo.h"


/* Characters displayed on the screen by lineedit are divided into three parts:
 * the prompt, the edit line and the candidate area.
 * The prompt is immediately followed by the edit line (on the same line) but
 * the candidate area are always on separate lines.
 * The three parts are printed in this order, from upper to lower.
 * When history search is active, the edit line is replaced by the temporary
 * search result line and the search target line.
 *
 * We have to track the cursor position each time we print a character on the
 * screen, so that we can correctly reposition the cursor later. To do this, we
 * use a buffer accompanied by a position data (`lebuf'). The `lebuf_putwchar'
 * function appends a wide character and updates the position data accordingly.
 *
 * In most terminals, when as many characters are printed as the number of
 * the columns, the cursor temporarily sticks to the end of the line. The cursor
 * moves to the next line immediately before the next character is printed. So
 * we must take care not to move the cursor (by the "cub1" capability, etc.)
 * when the cursor is sticking, or we cannot track the cursor position
 * correctly. To deal with this problem, if we finish printing a text at the end
 * of a line, we print a dummy space character and erase it to ensure the cursor
 * is no longer sticking. */


#if !HAVE_WCWIDTH
# undef wcwidth
# define wcwidth(c) (iswprint(c) ? 1 : 0)
#endif


/********** The Print Buffer **********/

static void lebuf_wprintf(bool convert_cntrl, const wchar_t *format, ...)
    __attribute__((nonnull(2)));
static const wchar_t *print_color_seq(const wchar_t *s)
    __attribute__((nonnull));

/* The print buffer. */
struct lebuf_T lebuf;

/* Initializes the print buffer with the specified position data. */
void lebuf_init(le_pos_T p)
{
    lebuf.pos = p;
    sb_init(&lebuf.buf);
}

/* Appends the specified byte to the print buffer without updating the position
 * data. Returns the appended character `c'. */
/* The signature of this function is intentionally aligned to that of the
 * `putchar' function. */
int lebuf_putchar(int c)
{
    sb_ccat(&lebuf.buf, TO_CHAR(c));
    return c;
}

/* Updates the position data of the print buffer as if a character with the
 * specified width has been printed.
 * The specified width must be no less than zero. */
void lebuf_update_position(int width)
{
    assert(width >= 0);

    int new_column = lebuf.pos.column + width;
    if (new_column <= le_columns)
	if (le_ti_xenl || new_column < le_columns)
	    lebuf.pos.column = new_column;
	else
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
 * The buffer and `le_columns' must have been initialized.
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
    while (*s != L'\0') {
	lebuf_putwchar(*s, convert_cntrl);
	s++;
    }
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
	    case L'$':   lebuf_putwchar(geteuid() ? L'$' : L'#', false);  break;
	    case L'j':   lebuf_wprintf(false, L"%zu", job_count());       break;
	    case L'!':   lebuf_wprintf(false, L"%u",  hist_next_number);  break;
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


/********** Displaying **********/

static void clean_up(void);
static void clear_to_end_of_screen(void);
static void clear_editline(void);
static void maybe_print_promptsp(void);
static void update_editline(void);
static void update_styler(void);
static void update_right_prompt(void);
static void print_search(void);
static void go_to(le_pos_T p);
static void go_to_index(size_t index);
static void go_to_after_editline(void);
static void fillip_cursor(void);

static le_compcol_T *fit_candidates(
	le_compcand_T *cand, int cand_per_col, int maxwidth)
    __attribute__((nonnull,malloc,warn_unused_result));
static le_comppage_T *divide_cands_pages(le_compcand_T *cand, int cand_per_col)
    __attribute__((nonnull,malloc,warn_unused_result));
static void print_candidates_all(void);


/* True when the prompt is displayed on the screen. */
/* The print buffer, `rprompt', and `sprompt' are valid iff `display_active' is
 * true. */
static bool display_active = false;
/* The current cursor position. */
static le_pos_T currentp;
/* The maximum line count. */
static int line_max;

/* The set of strings printed as the prompt, right prompt, after prompt.
 * May contain escape sequences. */
static struct promptset_T prompt;

/* The position of the first character of the edit line, just after the prompt.
 */
static le_pos_T editbasep;
/* The content of the edit line that is currently displayed on the screen. */
static wchar_t *current_editline = NULL;
/* An array of cursor positions of each character in the edit line.
 * If the nth character of `current_editline' is positioned at line `l', column
 * `c', then cursor_positions[n] == l * le_columns + c. */
static int *cursor_positions = NULL;
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

static struct {
    char *value;
    size_t length;  /* number of bytes in `value' */
} sprompt;


/* Initializes the display module.
 * Must be called after `le_editing_init'. */
void le_display_init(struct promptset_T prompt_)
{
    prompt = prompt_;

    currentp.line = currentp.column = 0;
}

/* Finalizes the display module.
 * Must be called before `le_editing_finalize'. */
void le_display_finalize(void)
{
    assert(le_search_buffer.contents == NULL);

    le_main_index = le_main_buffer.length;
    le_display_update(false);

    lebuf_print_sgr0();
    go_to_after_editline();
    clean_up();
}

/* Clears prompt, edit line and info area on the screen. */
void le_display_clear(void)
{
    if (display_active) {
	lebuf_init(currentp);
	lebuf_print_sgr0();
	go_to((le_pos_T) { 0, 0 });
	clean_up();
    }
}

void clean_up(void)
{
    assert(display_active);

    clear_to_end_of_screen();

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
    currentp = lebuf.pos;
    fwrite(lebuf.buf.contents, 1, lebuf.buf.length, stderr);
    fflush(stderr);
    sb_destroy(&lebuf.buf);
}

/* Clears the screen area below the cursor.
 * The cursor must be at the beginning of a line. */
void clear_to_end_of_screen(void)
{
    assert(lebuf.pos.column == 0);
    if (!le_ti_msgr)
	lebuf_print_sgr0(), styler_active = false;

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

/* Clears (part of) the edit line on the screen: from the current cursor
 * position to the end of the edit line.
 * The prompt and the info area are not cleared.
 * When this function is called, the cursor must be positioned within the edit
 * line. When this function returns, the cursor is moved back to that position.
 */
void clear_editline(void)
{
    assert(lebuf.pos.line > editbasep.line
	    || (lebuf.pos.line == editbasep.line
		&& lebuf.pos.column >= editbasep.column));
    assert(lebuf.pos.line <= last_edit_line);

    le_pos_T save_pos = lebuf.pos;

    if (!le_ti_msgr)
	lebuf_print_sgr0(), styler_active = false;
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
 * The output is sent to the print buffer. */
void le_display_update(bool cursor)
{
    if (!display_active) {
	display_active = true;
	last_edit_line = line_max = 0;

	/* prepare the right prompt */
	lebuf_init((le_pos_T) { 0, 0 });
	lebuf_print_sgr0();
	lebuf_print_prompt(prompt.right);
	if (lebuf.pos.line != 0) {  /* right prompt must be one line */
	    sb_truncate(&lebuf.buf, 0);
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
	    sb_truncate(&lebuf.buf, 0);
	    /* lebuf.pos.line = lebuf.pos.column = 0; */
	}
	sprompt.value = lebuf.buf.contents;
	sprompt.length = lebuf.buf.length;
	styler_active = false;

	/* print main prompt */
	lebuf_init((le_pos_T) { 0, 0 });
	maybe_print_promptsp();
	lebuf_print_prompt(prompt.main);
	editbasep = lebuf.pos;
    } else {
	lebuf_init(currentp);
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

    // TODO: print info area

    /* set cursor position */
    assert(le_main_index <= le_main_buffer.length);
    if (cursor)
	go_to_index(le_main_index);
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
	int count = le_columns - (le_ti_xenl ? 2 : 1);
	while (--count >= 0)
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

	while (current_editline[index] != L'\0'
		&& current_editline[index] == le_main_buffer.contents[index])
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
	go_to(editbasep);
	clear_editline();
    }

    update_styler();

    current_editline = xrealloc(current_editline,
	    sizeof *current_editline * (le_main_buffer.length + 1));
    cursor_positions = xrealloc(cursor_positions,
	    sizeof *cursor_positions * (le_main_buffer.length + 1));
    while (index < le_main_buffer.length) {
	wchar_t c = le_main_buffer.contents[index];
	current_editline[index] = c;
	cursor_positions[index]
	    = lebuf.pos.line * le_columns + lebuf.pos.column;
	lebuf_putwchar(c, true);
	index++;
    }
    assert(index == le_main_buffer.length);
    current_editline[index] = L'\0';
    cursor_positions[index] = lebuf.pos.line * le_columns + lebuf.pos.column;

    fillip_cursor();

    last_edit_line =
	(lebuf.pos.line >= rprompt_line) ? lebuf.pos.line : rprompt_line;

    /* clear the right prompt if the edit line reaches it. */
    if (rprompt_line == lebuf.pos.line
	    && lebuf.pos.column > le_columns - rprompt.width - 2) {
	lebuf_print_el();
	rprompt_line = -1;
    } else if (rprompt_line < lebuf.pos.line) {
	rprompt_line = -1;
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

/* Prints the right prompt if there is enough room in the edit line or if the
 * "le-alwaysrp" option is set.
 * The edit line must have been printed when this function is called. */
void update_right_prompt(void)
{
    if (rprompt_line >= 0)
	return;
    if (rprompt.width == 0)
	return;
    if (le_columns - rprompt.width - 2 < 0)
	return;
    int c = cursor_positions[le_main_buffer.length] % le_columns;
    bool has_enough_room = (c <= le_columns - rprompt.width - 2);
    if (!has_enough_room && !shopt_le_alwaysrp)
	return;

    go_to_index(le_main_buffer.length);
    if (!has_enough_room)
	lebuf_print_nel();
    lebuf_print_cuf(le_columns - rprompt.width - lebuf.pos.column - 1);
    sb_ncat_force(&lebuf.buf, rprompt.value, rprompt.length);
    lebuf.pos.column += rprompt.width;
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

    go_to(editbasep);
    clear_editline();

    update_styler();

    if (le_search_result != Histlist)
	lebuf_wprintf(true, L"%s", le_search_result->value);
    if (!le_ti_msgr)
	lebuf_print_sgr0(), styler_active = false;
    lebuf_print_nel();
    clear_to_end_of_screen();

    update_styler();

    const char *text;
    switch (le_search_type) {
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
 * The target column must be less than `le_columns'. */
void go_to(le_pos_T p)
{
    if (line_max < lebuf.pos.line)
	line_max = lebuf.pos.line;

    assert(p.line <= line_max);
    assert(p.column < le_columns);

    if (p.line == lebuf.pos.line) {
	if (lebuf.pos.column == p.column)
	    return;
	if (!le_ti_msgr)
	    lebuf_print_sgr0(), styler_active = false;
	if (lebuf.pos.column < p.column)
	    lebuf_print_cuf(p.column - lebuf.pos.column);
	else
	    lebuf_print_cub(lebuf.pos.column - p.column);
	return;
    }

    if (!le_ti_msgr)
	lebuf_print_sgr0(), styler_active = false;
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
    go_to((le_pos_T) { .line = p / le_columns, .column = p % le_columns });
}

/* Moves the cursor to the beginning of the line below `last_edit_line'.
 * The edit line (and possibly the right prompt) must have been printed. */
void go_to_after_editline(void)
{
    if (rprompt_line >= 0) {
	go_to((le_pos_T) { rprompt_line, le_columns - 1 });
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
    if (lebuf.pos.column >= le_columns) {
	lebuf_putwchar(L' ',  false);
	lebuf_putwchar(L'\r', false);
	lebuf_print_el();
	if (rprompt_line == lebuf.pos.line)
	    rprompt_line = -1;
    }
}


/* Arranges the specified list of completion candidates to fit to the screen.
 * A newly-malloced page list of newly-malloced columns is returned.
 * The columns contain the candidates specified by the argument list.
 * Note that the argument list is re-linked in this function.
 * If there are too few lines available on the screen, this function simply
 * returns NULL without modifying the argument list. */
le_comppage_T *le_arrange_candidates(le_compcand_T *firstcand)
{
    int maxrow = le_lines - last_edit_line - 1;
    if (maxrow <= 1)
	return NULL;

    /* First, we check if the candidates fit into one page. */
    for (int cand_per_col = 1; cand_per_col < maxrow; cand_per_col++) {
	le_compcol_T *cols = fit_candidates(
		firstcand, cand_per_col, le_columns - 1);

	if (cols != NULL) {
	    le_comppage_T *page = xmalloc(sizeof *page);
	    page->prev = page->next = NULL;
	    page->firstcol = cols;
	    return page;
	}
    }

    /* divide the candidate list into pages */
    return divide_cands_pages(firstcand, maxrow - 1);
}

/* Tries to fit the specified list of completion candidates into one page.
 * `cand_per_col' specifies the number of candidates in a column.
 * `maxwidth' specifies the width of the page. If `maxwidth' is negative, it is
 * considered unlimited and this function always succeeds.
 * If the candidates successfully fit into one page (that is, the total width of
 * columns does not exceed `maxwidth'), a newly-malloced list of columns is
 * returned. Each column contains part of the candidate list, which is split in
 * this function.
 * If all the candidates do not fit into one page, the candidate list is not
 * modified and NULL is returned. */
le_compcol_T *fit_candidates(
	le_compcand_T *cand, int cand_per_col, int maxwidth)
{
    int totalwidth = 0;
    le_compcol_T *firstcol = NULL, *lastcol = NULL;

    assert(cand->prev == NULL);
    do {
	le_compcol_T *col = xmalloc(sizeof *col);

	if (firstcol == NULL)
	    firstcol = col;
	col->prev = lastcol;
	col->next = NULL;
	col->firstcand = cand;
	col->width = 0;
	lastcol->next = col;

	for (int i = 0; i < cand_per_col; i++) {
	    assert(cand->width + 2 < le_columns);
	    if (col->width < cand->width)
		col->width = cand->width;
	    cand = cand->next;
	    if (cand == NULL)
		break;
	}

	totalwidth += col->width + 2;
	lastcol = col;
    } while (cand != NULL && (maxwidth < 0 || totalwidth <= maxwidth));

    if (maxwidth < 0 || totalwidth <= maxwidth) {
	/* OK, all the candidates fit into one page! Now we split the candidate
	 * list and return the columns. */
	for (le_compcol_T *col = firstcol->next; col != NULL; col = col->next) {
	    col->firstcand->prev->next = NULL;
	    col->firstcand->prev = NULL;
	}
	return firstcol;
    } else {
	/* Hum, all the candidates don't fit into one page... */
	le_free_compcols(firstcol, false);
	return NULL;
    }
}

/* Divides the specified list of completion candidates into columns and divides
 * the columns into pages to fit to the screen.
 * Each column contains `cand_per_col' candidates.
 * The candidate list contained in each column is part of the original candidate
 * list, which is split in this function. */
le_comppage_T *divide_cands_pages(le_compcand_T *cand, int cand_per_col)
{
    le_compcol_T *col = fit_candidates(cand, cand_per_col, -1);
    le_comppage_T *firstpage = NULL, *lastpage = NULL;

    for (;;) {
	le_comppage_T *page = xmalloc(sizeof *page);
	int pagewidth = col->width + 2;

	if (firstpage == NULL)
	    firstpage = page;
	page->prev = lastpage;
	page->next = NULL;
	page->firstcol = col;
	lastpage->next = page;

	for (;;) {
	    col = col->next;
	    if (col == NULL)
		return firstpage;
	    if (pagewidth + col->width + 2 >= le_columns)
		break;
	    pagewidth += col->width + 2;
	}

	col->prev->next = NULL;
	col->prev = NULL;
	lastpage = page;
    }
}

/* Prints the whole candidate area.
 * The cursor may be anywhere when this function is called.
 * The cursor is left at an unspecified position when this function returns. */
void print_candidates_all(void)
{
    //TODO
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
