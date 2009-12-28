/* Yash: yet another shell */
/* complete.c: command line completion */
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
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include "../option.h"
#include "complete.h"
#include "display.h"
#include "terminfo.h"


static void compdebug(const char *format, ...)
    __attribute__((nonnull,format(printf,1,2)));


/* Performs command line completion. */
void le_complete(void)
{
    if (shopt_le_compdebug) {
	le_display_clear();
	le_restore_terminal();
	compdebug("completion start");
    }

    //TODO

    if (shopt_le_compdebug) {
	compdebug("completion end");
	le_setupterm(false);
	le_set_terminal();
    }
}

/* Prints the formatted string to the standard error.
 * The string is preceded by "[compdebug] " and followed by a newline. */
void compdebug(const char *format, ...)
{
    if (!shopt_le_compdebug)
	return;

    fputs("[compdebug] ", stderr);

    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);

    fputc('\n', stderr);
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
