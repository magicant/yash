/* Yash: yet another shell */
/* terminfo.c: interface to terminfo and termios */
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


#define _XOPEN_SOURCE_EXTENDED 1
#include "../common.h"
#include <assert.h>
#include <curses.h>
#include <stdio.h>
#include <term.h>
#include <termios.h>
#include <unistd.h>
#include "terminfo.h"


/********** TERMINFO **********/


/* Number of lines and columns in the current terminal. */
/* Initialized by `yle_setupterm'. */
int yle_lines, yle_columns;


static int putchar_stderr(int c);


/* Calls `setupterm' and checks if terminfo data is available.
 * Returns true iff successful. */
_Bool yle_setupterm(void)
{
    int err;

    if (setupterm(NULL, STDERR_FILENO, &err) != OK)
	return 0;

    // XXX should we do some more checks?

    yle_lines = tigetnum(TI_lines);
    yle_columns = tigetnum(TI_cols);
    if (yle_lines <= 0 || yle_columns <= 0)
	return 0;

    return 1;
}

/* Prints "sgr" variable. */
void yle_print_sgr(long standout, long underline, long reverse, long blink,
	long dim, long bold, long invisible)
{
    char *v = tigetstr(TI_sgr);
    if (v) {
	v = tparm(v, standout, underline, reverse, blink, dim, bold, invisible,
		0, 0);
	if (v)
	    tputs(v, 1, putchar_stderr);
    }
}

/* Prints "op" variable. */
void yle_print_op(void)
{
    char *v = tigetstr(TI_op);
    if (v)
	tputs(v, 1, putchar_stderr);
}

/* Prints "setf"/"setaf" variable. */
void yle_print_setfg(int color)
{
    char *v = tigetstr(TI_setaf);
    if (!v)
	v = tigetstr(TI_setf);
    if (v) {
	v = tparm(v, color);
	if (v)
	    tputs(v, 1, putchar_stderr);
    }
}

/* Prints "setb"/"setab" variable. */
void yle_print_setbg(int color)
{
    char *v = tigetstr(TI_setab);
    if (!v)
	v = tigetstr(TI_setb);
    if (v) {
	v = tparm(v, color);
	if (v)
	    tputs(v, 1, putchar_stderr);
    }
}

/* Like `putchar', but prints to `stderr'. */
int putchar_stderr(int c)
{
    return fputc(c, stderr);
}


/********** TERMIOS **********/

static struct termios original_terminal_state;

/* Sets the terminal to the "raw" mode.
 * The current state is saved as `original_terminal_state'.
 * `stdin' must be the terminal.
 * Returns true iff successful. */
_Bool yle_set_terminal(void)
{
    struct termios term;

    if (tcgetattr(STDIN_FILENO, &term) != 0)
	return 0;
    original_terminal_state = term;

    /* set attributes */
    term.c_iflag &= ~(IGNBRK | ISTRIP | INLCR | IGNCR | ICRNL);
    term.c_iflag |= BRKINT;
    term.c_oflag &= ~OPOST;
    term.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
    term.c_lflag |= ISIG;
    term.c_cflag &= ~CSIZE;
    term.c_cflag |= CS8;
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &term) != 0)
	goto fail;

    /* check if the attributes are properly set */
    if (tcgetattr(STDIN_FILENO, &term) != 0)
	goto fail;
    if ((term.c_iflag & (IGNBRK | ISTRIP | INLCR | IGNCR | ICRNL))
	    || !(term.c_iflag & BRKINT)
	    ||  (term.c_oflag & OPOST)
	    ||  (term.c_lflag & (ECHO | ECHONL | ICANON | IEXTEN))
	    || !(term.c_lflag & ISIG)
	    ||  ((term.c_cflag & CSIZE) != CS8))
	goto fail;

    return 1;

fail:
    tcsetattr(STDIN_FILENO, TCSADRAIN, &original_terminal_state);
    return 0;
}

/* Restores the terminal to the original state.
 * `stdin' must be the terminal.
 * Returns true iff successful. */
_Bool yle_restore_terminal(void)
{
    return tcsetattr(STDIN_FILENO, TCSADRAIN, &original_terminal_state) == 0;
}


/* vim: set ts=8 sts=4 sw=4 noet: */
