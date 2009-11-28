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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include "../history.h"
#include "../job.h"
#include "../option.h"
#include "../util.h"
#include "display.h"
#include "editing.h"
#include "lineedit.h"
#include "terminfo.h"


/* Characters displayed on the screen by lineedit are divided into three parts:
 * the prompt, the edit line and the info area.
 * The prompt is immediately followed by the edit line (on the same line) but
 * the info area are always on separate lines.
 * The three parts are printed in this order, from upper to lower.
 * When history search is active, the edit line and the info area are replaced
 * by the temporary search result line and the search target line.
 *
 * The `tputwc' function is the main function for displaying. It prints one
 * character and updates the cursor position data in each call.
 * In most terminals, when as many characters are printed as the number of
 * the columns, the cursor temporarily sticks to the end of the line. The cursor
 * moves to the next line immediately before the next character is printed. So
 * we must take care not to move the cursor (by the "cub1" capability, etc.)
 * when the cursor is sticking, or we cannot track the cursor position
 * correctly. To deal with this problem, when the cursor reaches the end of a
 * line, we immediately print the next character (or a dummy character),
 * ensuring the cursor is no longer sticking. Since we print characters in the
 * order of normal text flow, the problem becomes rather simple: All we have to
 * do is to print a dummy character and erase it if we finish printing the text
 * at the end of a line. */


static void clear_to_end_of_screen(void);
static void go_to(int line, int column);
static void go_to_index(size_t i);
static void tputwc(wchar_t c);
static void tputws(const wchar_t *s, size_t n)
    __attribute__((nonnull));
static void twprintf(const wchar_t *format, ...)
    __attribute__((nonnull(1)));
static void fillip_cursor(void);

static void print_prompt(void);
static void print_color_seq(const wchar_t **sp)
    __attribute__((nonnull));

static void update_editline(void);
static void clear_editline(void);
static void print_search(void);


/* True when something is displayed on the screen. */
static bool display_active;
/* The current cursor position. */
/* 0 <= current_line < lines, 0 <= current_column <= columns */
static int current_line, current_column;
/* The maximum value of `current_line' in the current editing. */
static int current_line_max;
/* If false, `current_line' and `current_column' are not changed when characters
 * are printed. */
static bool trace_position = true;
/* Normally, control characters are printed like "^X".
 * If `convert_all_control' is false, '\a', '\n', '\r' are printed in a
 * different way by `tputwc'. */
static bool convert_all_control;
/* If true, some information is printed below the edit line.
 * A typical example of such info is completion candidates.
 * This info must be cleared when editing is finished. */
//static bool additional_info_printed;
/* Updates `current_line_max'. */
#define CHECK_CURRENT_LINE_MAX \
    do if (current_line_max < current_line) \
	   current_line_max = current_line; \
    while (0)

/* String that is printed as the prompt.
 * May contain escape sequences. */
static const wchar_t *promptstring;
/* The position of the first character of the edit line, just after the prompt.
 */
static int editbase_line, editbase_column;

/* The content of the edit line that is currently displayed on the screen. */
static wchar_t *current_editline;
/* Array of cursor positions in the screen.
 * If the nth character in the main buffer is positioned at line `l', column `c'
 * on the screen, then cursor_positions[n] == l * le_columns + c. */
static int *cursor_positions;
/* The line number of the last edit line (or the search buffer). */
static int last_edit_line;


#if !HAVE_WCWIDTH
# undef wcwidth
# define wcwidth(c) (iswprint(c) ? 1 : 0)
#endif


/* Initializes the display module.
 * Must be called after `le_editing_init'. */
void le_display_init(const wchar_t *prompt)
{
    current_line = current_column = current_line_max = 0;
    last_edit_line = 0;
    promptstring = prompt;
}

/* Finalizes the display module.
 * Must be called before `le_editing_finalize'. */
void le_display_finalize(void)
{
    assert(le_search_buffer.contents == NULL);

    le_main_index = le_main_buffer.length;
    le_display_update();

    if (current_column != 0 || current_line == 0)
	le_print_nel(), current_line++, current_column = 0;
    CHECK_CURRENT_LINE_MAX;
    clear_to_end_of_screen();

    free(current_editline);
    current_editline = NULL;
    free(cursor_positions);
    cursor_positions = NULL;
    display_active = false;
}

/* Clears everything printed by lineedit, restoreing the state before lineedit
 * is started. */
void le_display_clear(void)
{
    go_to(0, 0);
    clear_to_end_of_screen();

    free(current_editline);
    current_editline = NULL;
    free(cursor_positions);
    cursor_positions = NULL;
    display_active = false;
}

/* Clears display area below the cursor.
 * The cursor must be at the beginning of a line. */
void clear_to_end_of_screen(void)
{
    assert(current_column == 0);
    if (le_print_ed())  /* if the terminal has "ed" capability, just use it */
	return;

    int saveline = current_line;
    while (current_line < current_line_max) {
	le_print_el();
	le_print_nel();
	current_line++;
    }
    le_print_el();
    go_to(saveline, 0);
}

/* Prints a dummy string, which moves the cursor to the first column of the next
 * line if the cursor is not at the first column.
 * This function does nothing if the "le-promptsp" option is not set or the
 * terminal does not have the "am" (auto-margin) capability. */
void le_display_maybe_promptsp(void)
{
    if (shopt_le_promptsp && le_ti_am) {
	le_print_sgr(1, 0, 0, 0, 0, 0, 0);
	fputc('$', stderr);
	le_print_sgr(0, 0, 0, 0, 0, 0, 0);
	for (int i = le_ti_xenl ? 1 : 2; i < le_columns; i++)
	    fputc(' ', stderr);
	le_print_cr();
	le_print_ed();
    }
}

/* (Re)prints the display appropriately and moves the cursor to the proper
 * position. */
void le_display_update(void)
{
    /* print prompt */
    if (!display_active) {
	assert(current_line == 0 && current_column == 0);
	display_active = true;
	print_prompt();
    }

    /* print edit line */
    if (le_search_buffer.contents == NULL) {
	update_editline();
    } else {
	go_to(editbase_line, editbase_column);
	print_search();
	return;
    }

    /* print info area TODO */

    /* position cursor */
    assert(le_main_index <= le_main_buffer.length);
    go_to_index(le_main_index);
}

/* Moves the cursor to the specified position. */
static void go_to(int line, int column)
{
    // assert(line < le_lines);
    assert(column < le_columns);

    if (line == current_line) {
	if (column == 0)
	    le_print_cr();
	else if (current_column < column)
	    le_print_cuf(column - current_column);
	else if (current_column > column)
	    le_print_cub(current_column - column);
	current_column = column;
	return;
    }

    le_print_cr();
    current_column = 0;
    if (current_line < line)
	le_print_cud(line - current_line);
    else if (current_line > line)
	le_print_cuu(current_line - line);
    current_line = line;
    assert(current_line <= current_line_max);
    if (column > 0) {
	le_print_cuf(column);
	current_column = column;
    }
}

/* Moves the cursor to the character of the specified index in the main buffer.
 * This function relies on `cursor_positions', so `update_editline()' must have
 * been called beforehand. */
void go_to_index(size_t i)
{
    int pos = cursor_positions[i];
    go_to(pos / le_columns, pos % le_columns);
}

/* Counts the width of the specified character as printed by `tputwc'. */
int count_width(wchar_t c)
{
    int width = wcwidth(c);
    if (width > 0)
	return width;
    if (!convert_all_control) switch (c) {
	case L'\a':  case L'\n':  case L'\r':
	    return 0;
    }
    if (c < L'\040')
	return count_width(L'^') + count_width(c + L'\100');
    else
	return 0;
}

/* Counts the width of the first 'n' characters in `s' as printed by `tputws'.*/
int count_width_ws(const wchar_t *s, size_t n)
{
    int count = 0;

    for (size_t i = 0; i < n && s[n] != L'\0'; i++)
	count += count_width(s[n]);
    return count;
}

/* Prints the given wide character to the terminal.
 * If `trace_position' is true, `current_column' and `current_line' are updated
 * to reflect the new cursor position. */
void tputwc(wchar_t c)
{
    if (!trace_position) {
	fprintf(stderr, "%lc", (wint_t) c);
	return;
    }

    int width = wcwidth(c);
    if (width > 0) {
	int new_column = current_column + width;
	if (le_ti_xenl ? new_column <= le_columns : new_column < le_columns) {
	    current_column = new_column;
	} else {
	    /* go to the next line */
	    if (current_column < le_columns)
		le_print_el();
	    if (!le_ti_am)
		le_print_nel();
	    current_line++, current_column = width;
	    CHECK_CURRENT_LINE_MAX;
	}
	fprintf(stderr, "%lc", (wint_t) c);
    } else {
	if (!convert_all_control) switch (c) {
	    case L'\a':
		le_alert();
		return;
	    case L'\n':
		le_print_nel();
		current_line++, current_column = 0;
		CHECK_CURRENT_LINE_MAX;
		return;
	    case L'\r':
		le_print_cr();
		current_column = 0;
		return;
	}
	if (c < L'\040') {  // XXX ascii-compatible encoding assumed
	    tputwc(L'^');
	    tputwc(c + L'\100');
	} else if (c == L'\177') {
	    tputwc(L'^');
	    tputwc(L'?');
	}
    }
}

/* Prints the given string to the terminal.
 * The first `n' characters are printed at most. */
void tputws(const wchar_t *s, size_t n)
{
    for (size_t i = 0; i < n && s[i] != L'\0'; i++)
	tputwc(s[i]);
}

/* Formats and prints the given string. */
void twprintf(const wchar_t *format, ...)
{
    va_list args;

    va_start(args, format);

    wchar_t *s = malloc_vwprintf(format, args);
    tputws(s, SIZE_MAX);
    free(s);

    va_end(args);
}

/* If the cursor is sticking to the end of line, moves it to the next line. */
void fillip_cursor(void)
{
    if (current_column >= le_columns) {
	tputwc(L' ');
	le_print_cr();
	current_column = 0;
	le_print_el();
    }
}

/* Prints the given prompt that may contain backslash escapes. */
void print_prompt(void)
{
    /* The backslash escapes are defined in "../input.c". */

    trace_position = true;
    convert_all_control = false;

    assert(current_line == 0);
    assert(current_column == 0);

    const wchar_t *s = promptstring;
    while (*s) {
	if (*s != L'\\') {
	    tputwc(*s);
	} else switch (*++s) {
	case L'\0':   tputwc(L'\\');  goto done;
	//case L'\\':   tputwc(L'\\');  break;
	case L'a':    tputwc(L'\a');  break;
	case L'e':    tputwc(L'\033');  break;
	case L'n':    tputwc(L'\n');  break;
	case L'r':    tputwc(L'\r');  break;
	case L'$':    tputwc(geteuid() ? L'$' : L'#');  break;
	default:      tputwc(*s);  break;
	case L'j':    twprintf(L"%zu", job_count());  break;
	case L'!':    twprintf(L"%d", hist_next_number);  break;
	case L'[':    trace_position = false;  break;
	case L']':    trace_position = true;   break;
	case L'f':    print_color_seq(&s);   continue;
	}
	s++;
    }
done:
    fillip_cursor();
    CHECK_CURRENT_LINE_MAX;
    editbase_line = current_line, editbase_column = current_column;
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

    le_print_sgr(standout, underline, reverse, blink, dim, bold, invisible);
    if (op)       /* restore original color pair */
	le_print_op();
    if (fg >= 0)  /* set foreground color */
	le_print_setfg(fg);
    if (bg >= 0)  /* set background color */
	le_print_setbg(bg);
}

/* Prints the content of the edit line, updating `cursor_positions' and
 * `last_edit_line'.
 * The cursor is left at an unspecified position. */
void update_editline(void)
{
    size_t index = 0;

    if (current_editline != NULL) {
	/* We only reprint what have been changed from the last update:
	 * Skip unchanged part at the beginning of the line. */
	assert(cursor_positions != NULL);
	while (current_editline[index] != L'\0'
		&& current_editline[index] == le_main_buffer.contents[index])
	    index++;

	/* return if nothing changed */
	if (current_editline[index] == L'\0'
		&& le_main_buffer.contents[index] == L'\0')
	    return;

	go_to_index(index);
	if (current_editline[index] != L'\0')
	    clear_editline();
    } else {
	go_to(editbase_line, editbase_column);
	clear_editline();
    }

    trace_position = convert_all_control = true;
    current_editline = xrealloc(current_editline,
	    sizeof *current_editline * (le_main_buffer.length + 1));
    cursor_positions = xrealloc(cursor_positions,
	    sizeof *cursor_positions * (le_main_buffer.length + 1));
    while (index < le_main_buffer.length) {
	wchar_t c = le_main_buffer.contents[index];
	current_editline[index] = c;
	cursor_positions[index] = current_line * le_columns + current_column;
	tputwc(c);
	index++;
    }
    current_editline[index] = L'\0';
    cursor_positions[index] = current_line * le_columns + current_column;

    fillip_cursor();
    CHECK_CURRENT_LINE_MAX;
    last_edit_line = current_line;
}

/* Clears (part of) the edit line on the screen, from the current cursor
 * position to the end of the edit line.
 * The prompt and the info area are not cleared.
 * When this function is called, the cursor must be positioned within the edit
 * line. When this function returns, the cursor is moved back to that position.
 */
void clear_editline(void)
{
    assert(current_line > editbase_line || current_column >= editbase_column);
    assert(current_line <= current_line_max);

    int save_line = current_line, save_column = current_column;

    le_print_el();
    while (current_line < last_edit_line) {
	le_print_nel();
	le_print_el();
	current_line++;
	assert(current_line <= current_line_max);
    }
    go_to(save_line, save_column);
}

/* Prints the current search result and the search line.
 * When this function is called, the cursor must be just after the prompt.
 * Characters after the prompt are cleared in this function.
 * When this function returns, the cursor is left after the search line. */
void print_search(void)
{
    assert(le_search_buffer.contents != NULL);

    free(current_editline);
    current_editline = NULL;
    free(cursor_positions);
    cursor_positions = NULL;

    if (le_search_result != Histlist)
	twprintf(L"%s", le_search_result->value);
    fillip_cursor();
    if (current_column > 0)
	le_print_el(), le_print_nel(), current_line++, current_column = 0;
    CHECK_CURRENT_LINE_MAX;
    clear_to_end_of_screen();

    const char *text;
    switch (le_search_type) {
	case SEARCH_VI:
	    switch (le_search_direction) {
		case FORWARD:   tputwc(L'?');  break;
		case BACKWARD:  tputwc(L'/');  break;
		default:        assert(false);
	    }
	    break;
	case SEARCH_EMACS:
	    switch (le_search_direction) {
		case FORWARD:   text = "Forward search: ";   break;
		case BACKWARD:  text = "Backward search: ";  break;
		default:        assert(false);
	    }
	    twprintf(L"%s", gt(text));
	    break;
    }
    tputws(le_search_buffer.contents, SIZE_MAX);
    fillip_cursor();
    CHECK_CURRENT_LINE_MAX;
    last_edit_line = current_line;
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
