/* Yash: yet another shell */
/* display.c: display control */
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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include "../history.h"
#include "../job.h"
#include "display.h"
#include "lineedit.h"
#include "terminfo.h"


static void go_to(int line, int column);
static void tputwc(wchar_t c);
static void tputws(const wchar_t *s, size_t n)
    __attribute__((nonnull));

static void twprintf(const wchar_t *format, ...)
    __attribute__((nonnull(1)));
static void print_prompt(void);
static void print_color_seq(const wchar_t **sp)
    __attribute__((nonnull));


/* The current cursor position. */
/* 0 <= current_line < lines, 0 <= current_column <= columns */
static int current_line, current_column;
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

/* The main buffer where the command line is edited. */
static xwcsbuf_T main_buffer;
/* The position of the cursor on the command line. */
/* 0 <= main_buffer_index <= main_buffer.length */
static size_t main_buffer_index;

/* String that is printed as the prompt.
 * May contain escape sequences. */
static const wchar_t *promptstring;
/* The cursor position of the first character of the edit line, just after the
 * prompt. */
static int yle_editbase_line, yle_editbase_column;


/* Initializes the display module. */
void yle_display_init(const wchar_t *prompt)
{
    current_line = current_column = 0;

    wb_init(&main_buffer);
    main_buffer_index = 0;

    promptstring = prompt;
    yle_display_print_all();
}

/* Finalizes the display module.
 * Returns the content of the main buffer, which must be freed by the caller. */
wchar_t *yle_display_finalize(void)
{
    //TODO move cursor to last char of buffer
    yle_print_nel();
    yle_print_ed();
    return wb_towcs(&main_buffer);
}

/* Clears everything printed by lineedit, restoreing the state before lineedit
 * is started. */
void yle_display_clear(void)
{
    go_to(0, 0);
    yle_print_ed();
}

/* Prints everything and moves the cursor to the proper position.
 * This function assumes that the cursor is at the origin and the screen is
 * cleared. */
void yle_display_print_all(void)
{
    print_prompt();
    //TODO redraw properly
    //additional_info_printed = XXX;
}


/* Moves the cursor to the specified position. */
static void go_to(int line, int column)
{
    yle_print_cr();
    current_column = 0;
    if (current_line < line) {
	yle_print_cud(line - current_line);
    } else if (current_line > line) {
	yle_print_cuu(current_line - line);
    }
    current_line = line;
    if (column > 0) {
	yle_print_cuf(column);
	current_column = column;
    }
}

/* Counts the width of the character `c' as printed by `fputwc'. */
int count_width(wchar_t c)
{
#if HAVE_WCWIDTH
    int width = wcwidth(c);
    if (width > 0)
	return width;
#else
    if (iswprint(c))
	return 1;
#endif
    if (!convert_all_control) switch (c) {
	case L'\a':  case L'\n':  case L'\r':
	    return 0;
    }
    if (c < L'\040') {
	return count_width(L'^') + count_width(c + L'\100');
    } else {
	return 0;
    }
}

/* Counts the width of the first 'n' characters in `s' as printed by `fputws'.*/
int count_width_ws(const wchar_t *s, size_t n)
{
    int count = 0;

    for (size_t i = 0; i < n && s[n] != L'\0'; i++)
	count += count_width(s[n]);
    return count;
}

/* Prints the given wide character to the terminal. */
void tputwc(wchar_t c)
{
#if HAVE_WCWIDTH
    int width = wcwidth(c);
#else
    int width = iswprint(c) ? 1 : 0;
#endif
    if (width > 0) {
	int new_column = current_column + width;
	if (new_column <= yle_columns) {
	    current_column = new_column;
	} else {
	    yle_print_nel_if_no_auto_margin();
	    current_line++, current_column = width;
	}
	fprintf(stderr, "%lc", (wint_t) c);
    } else {
	if (!convert_all_control) switch (c) {
	    case L'\a':
		yle_alert();
		return;
	    case L'\n':
		yle_print_nel();
		current_line++, current_column = 0;
		return;
	    case L'\r':
		yle_print_cr();
		current_column = 0;
		return;
	}
	if (c < L'\040') {  // XXX ascii-compatible coding assumed
	    tputwc(L'^');
	    tputwc(c + L'\100');
	}
    }
}

/* Prints the given string to the terminal.
 * The first `n' characters are printed at most. */
void tputws(const wchar_t *s, size_t n)
{
    for (size_t i = 0; i < n && s[n] != L'\0'; i++)
	tputwc(s[n]);
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

/* Prints the given prompt, which may contain backslash escapes. */
void print_prompt(void)
{
    /* The backslash escapes are defined in "../input.c". */

    trace_position = true;

    //assert(current_line == 0);
    //assert(current_column == 0);

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
#if YASH_ENABLE_HISTORY
	case L'!':    twprintf(L"%d", hist_next_number);  break;
#endif
	case L'[':    trace_position = false;  break;
	case L']':    trace_position = true;   break;
	case L'f':    print_color_seq(&s);   continue;
	}
	s++;
    }
done:
    trace_position = true;
    yle_editbase_line = current_line, yle_editbase_column = current_column;
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


/* vim: set ts=8 sts=4 sw=4 noet: */
