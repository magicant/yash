/* Yash: yet another shell */
/* display.h: display control */
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


#ifndef YASH_DISPLAY_H
#define YASH_DISPLAY_H

#include <stddef.h>
#include "../strbuf.h"


extern xwcsbuf_T yle_main_buffer;
extern size_t yle_main_index;

extern void yle_display_init(const wchar_t *prompt)
    __attribute__((nonnull));
extern wchar_t *yle_display_finalize(void);
extern void yle_display_clear(void);
extern void yle_display_print_all(void);
extern void yle_display_reprint_buffer(size_t index, _Bool noclear);
extern void yle_display_reposition_cursor(void);


#endif /* YASH_DISPLAY_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
