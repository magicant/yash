/* Yash: yet another shell */
/* terminfo.h: interface to terminfo and termios */
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


#ifndef YASH_TERMINFO_H
#define YASH_TERMINFO_H


extern int le_lines, le_columns;
extern _Bool le_ti_am, le_ti_xenl;
extern _Bool le_meta_bit8;
extern struct trienode_T /* trie_T */ *le_keycodes;

extern _Bool le_setupterm(void);

extern void le_print_cr(void);
extern void le_print_nel(void);
extern void le_print_cub(long count);
extern void le_print_cuf(long count);
extern void le_print_cud(long count);
extern void le_print_cuu(long count);
extern void le_print_el(void);
extern _Bool le_print_ed(void);
extern void le_print_sgr(long standout, long underline, long reverse,
	long blink, long dim, long bold, long invisible);
extern void le_print_op(void);
extern void le_print_setfg(int color);
extern void le_print_setbg(int color);
extern void le_alert(void);


extern char le_eof_char, le_kill_char, le_interrupt_char, le_erase_char;

extern _Bool le_set_terminal(void);
extern _Bool le_restore_terminal(void);


#endif /* YASH_TERMINFO_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
