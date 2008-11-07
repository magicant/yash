/* Yash: yet another shell */
/* lineedit.h: command line editing */
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


#ifndef YASH_LINEEDIT_H
#define YASH_LINEEDIT_H

#include <stddef.h>


extern _Bool yle_need_term_reset;

extern _Bool yle_init(void);

extern wchar_t *yle_readline(const wchar_t *prompt)
    __attribute__((malloc,warn_unused_result));


#endif /* YASH_LINEEDIT_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
