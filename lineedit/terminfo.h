/* Yash: yet another shell */
/* terminfo.h: interface to terminfo */
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


#ifndef YASH_TERMINFO_H
#define YASH_TERMINFO_H


#define TI_cols    "cols"
#define TI_lines   "lines"
#define TI_op      "op"
#define TI_setab   "setab"
#define TI_setaf   "setaf"
#define TI_setb    "setb"
#define TI_setf    "setf"
#define TI_sgr     "sgr"


extern int yle_lines, yle_columns;


extern _Bool yle_setupterm(void);

extern void yle_print_sgr(long standout, long underline, long reverse,
	long blink, long dim, long bold, long invisible);
extern void yle_print_op(void);
extern void yle_print_setfg(int color);
extern void yle_print_setbg(int color);


#endif /* YASH_TERMINFO_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
