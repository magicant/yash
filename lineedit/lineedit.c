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
#include <curses.h>
#include <stdbool.h>
#include <term.h>
#include <unistd.h>
#include "../option.h"
#include "../util.h"
#include "lineedit.h"


/* True if `setupterm' should be called in the next call to `yle_init'. */
bool yle_need_term_reset = true;


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

    int err;
    if (setupterm(NULL, STDERR_FILENO, &err) == OK) {
	// XXX should we do some more checks?
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

    (void) prompt;
    return xwcsdup(L"");
}


/* vim: set ts=8 sts=4 sw=4 noet: */
