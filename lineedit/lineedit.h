/* Yash: yet another shell */
/* lineedit.h: command line editing */
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


#ifndef YASH_LINEEDIT_H
#define YASH_LINEEDIT_H

#include <stddef.h>


typedef enum {
    LE_STATE_EDITING,     // editing is on-going
    LE_STATE_DONE,        // `le_readline' should return (successful)
    LE_STATE_ERROR,       // `le_readline' should return (unsuccessful)
    LE_STATE_INTERRUPTED, // `le_readline' should return (interrupted)
} le_state_T;


extern le_state_T le_state;

extern _Bool le_setup(void);

extern wchar_t *le_readline(const wchar_t *prompt)
    __attribute__((nonnull,malloc,warn_unused_result));
extern void le_suspend_readline(void);
extern void le_resume_readline(void);
#if YASH_ENABLE_LINEEDIT
# define LE_SUSPEND_READLINE() le_suspend_readline()
# define LE_RESUME_READLINE()  le_resume_readline()
#else
# define LE_SUSPEND_READLINE() ((void) 0)
# define LE_RESUME_READLINE()  ((void) 0)
#endif

extern _Bool le_next_verbatim;

void append_to_prebuffer(char *s)
    __attribute__((nonnull));


#endif /* YASH_LINEEDIT_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
