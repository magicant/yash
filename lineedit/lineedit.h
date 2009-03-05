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
    YLE_STATE_EDITING,     // editing is on-going
    YLE_STATE_DONE,        // `yle_readline' should return (successful)
    YLE_STATE_ERROR,       // `yle_readline' should return (unsuccessful)
    YLE_STATE_INTERRUPTED, // `yle_readline' should return (interrupted)
} yle_state_T;


extern _Bool yle_need_term_reset;

extern yle_state_T yle_state;

extern _Bool yle_setup(void);

extern wchar_t *yle_readline(const wchar_t *prompt)
    __attribute__((nonnull,malloc,warn_unused_result));
extern void yle_suspend_readline(void);
extern void yle_resume_readline(void);
#if YASH_ENABLE_LINEEDIT
# define YLE_SUSPEND_READLINE() yle_suspend_readline()
# define YLE_RESUME_READLINE()  yle_resume_readline()
#else
# define YLE_SUSPEND_READLINE() ((void) 0)
# define YLE_RESUME_READLINE()  ((void) 0)
#endif

extern _Bool yle_next_verbatim;


#endif /* YASH_LINEEDIT_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
