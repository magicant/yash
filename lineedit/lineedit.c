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
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wchar.h>
#include "../history.h"
#include "../job.h"
#include "../option.h"
#include "../strbuf.h"
#include "../util.h"
#include "lineedit.h"
#include "terminfo.h"


static void yle_wprintf(const wchar_t *format, ...)
    __attribute__((nonnull(1)));
static void print_color_seq(const wchar_t **sp)
    __attribute__((nonnull));


/* True if `yle_setupterm' should be called in the next call to `yle_init'. */
bool yle_need_term_reset = true;


/* The current cursor position. */
/* 0 <= yle_line < lines, 0 <= yle_column < columns */
int yle_line, yle_column;
/* If false, `yle_line' and `yle_column' are not changed when characters are
 * printed. */
bool yle_counting = true;


/* Initializes line editing.
 * Must be called before each call to `yle_readline'.
 * Returns true iff successful.
 * If this function returns false, the vi/emacs option is unset and
 * `yle_readline' must not be called. */
bool yle_init(void)
{
    if (!yle_need_term_reset)
	return true;
    if (!isatty(STDIN_FILENO) || !isatty(STDERR_FILENO))
	goto fail;

    if (yle_setupterm()) {
	yle_need_term_reset = false;
	return true;
    }

fail:
    shopt_lineedit = shopt_nolineedit;
    return false;
}

/* Prints the specified `prompt' and reads one line from stdin.
 * This function can be called only after `yle_init' succeeded.
 * The `prompt' may contain backslash escapes specified in "input.c".
 * The result is returned as a newly malloced wide string, including the
 * trailing newline. When EOF is encountered or on error, an empty string is
 * returned. NULL is returned when SIGINT is caught. */
wchar_t *yle_readline(const wchar_t *prompt)
{
    assert(!yle_need_term_reset);

    yle_line = yle_column = 0;

    yle_set_terminal();

    yle_print_prompt(prompt);
    //TODO

    yle_restore_terminal();

    return xwcsdup(L"");
}

/* Prints the given wide character to the terminal. */
void yle_print_wc(wchar_t c)
{
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


/* vim: set ts=8 sts=4 sw=4 noet: */
