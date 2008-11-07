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
#include <curses.h>
#include <stdbool.h>
#include <term.h>
#include <unistd.h>
#include "../util.h"
#include "lineedit.h"


/* True if `setupterm' should be called in the next call to `yle_readline'. */
bool yle_need_term_reset = true;


/* Prints the specified `prompt' and reads one line from stdin.
 * Both `stdin' and `stderr' must be connected to a terminal.
 * The `prompt' may contain backslash escapes specified in "input.c".
 * The result is returned as a newly malloced wide string, including the
 * trailing newline. When EOF is encountered, an empty string is returned.
 * NULL is returned on error. */
wchar_t *yle_readline(const wchar_t *prompt)
{
    if (yle_need_term_reset) {
	int err;
	yle_need_term_reset = false;
	if (setupterm(NULL, STDERR_FILENO, &err) != OK) {
	    xerror(0, Ngt("cannot setup terminal"));
	    return NULL;
	}
    }
    (void) prompt;
    return NULL;
}


/* vim: set ts=8 sts=4 sw=4 noet: */
