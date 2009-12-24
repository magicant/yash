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
#include "../input.h"


enum le_state_T {
    LE_STATE_INACTIVE,
    LE_STATE_ACTIVE,
    LE_STATE_SUSPENDED,
};
enum le_editstate_T {
    LE_EDITSTATE_EDITING,     // editing is on-going
    LE_EDITSTATE_DONE,        // `le_readline' should return (successful)
    LE_EDITSTATE_ERROR,       // `le_readline' should return (unsuccessful)
    LE_EDITSTATE_INTERRUPTED, // `le_readline' should return (interrupted)
};


extern enum le_state_T le_state;
extern enum le_editstate_T le_editstate;

extern inputresult_T le_readline(
	struct promptset_T prompt, wchar_t **resultp)
    __attribute__((nonnull(2),malloc,warn_unused_result));
extern void le_suspend_readline(void);
extern void le_resume_readline(void);
extern void le_display_size_changed(void);

extern _Bool le_next_verbatim;

extern void append_to_prebuffer(char *s)
    __attribute__((nonnull));


#endif /* YASH_LINEEDIT_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
