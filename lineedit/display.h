/* Yash: yet another shell */
/* display.h: display control */
/* (C) 2007-2011 magicant */

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
#include "../input.h"
#include "../strbuf.h"
#include "lineedit.h"


typedef struct le_pos_T {
    int line, column;
} le_pos_T;

extern struct lebuf_T {
    xstrbuf_T buf;
    le_pos_T pos;
    int maxcolumn;
} lebuf;

extern void lebuf_init(le_pos_T p);
extern int lebuf_putchar(int c);
extern void lebuf_update_position(int width);
extern void lebuf_putwchar_raw(wchar_t c);
extern void lebuf_putwchar(wchar_t c, _Bool convert_cntrl);
extern void lebuf_putws(const wchar_t *s, _Bool convert_cntrl)
    __attribute__((nonnull));
extern void lebuf_print_prompt(const wchar_t *s)
    __attribute__((nonnull));
extern _Bool lebuf_putwchar_trunc(wchar_t c);
extern void lebuf_putws_trunc(const wchar_t *s)
    __attribute__((nonnull));

extern void le_display_init(struct promptset_T prompt);
extern void le_display_finalize(void);
extern void le_display_clear(_Bool clear);
extern void le_display_flush(void);
extern void le_display_update(_Bool cursor);
extern void le_display_make_rawvalues(void);
extern void le_display_complete_cleanup(void);
extern void le_display_select_column(int offset);
extern void le_display_select_page(int offset);

extern _Bool le_try_print_prompt(const wchar_t *s)
    __attribute__((nonnull));


#endif /* YASH_DISPLAY_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
