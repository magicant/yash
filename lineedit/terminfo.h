/* Yash: yet another shell */
/* terminfo.h: interface to terminfo and termios */
/* (C) 2007-2010 magicant */

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


extern _Bool le_need_term_update;

extern int le_lines, le_columns, le_colors;
extern int le_ti_xmc;
extern _Bool le_ti_am, le_ti_xenl, le_ti_msgr;
extern _Bool le_meta_bit8;
extern struct trienode_T /* trie_T */ *le_keycodes;

extern _Bool le_setupterm(_Bool bypass);

enum le_color {
    LE_COLOR_BLACK   = 0,
    LE_COLOR_RED     = 1,
    LE_COLOR_GREEN   = 2,
    LE_COLOR_YELLOW  = 3,
    LE_COLOR_BLUE    = 4,
    LE_COLOR_MAGENTA = 5,
    LE_COLOR_CYAN    = 6,
    LE_COLOR_WHITE   = 7,
};

extern void lebuf_print_cr(void);
extern void lebuf_print_nel(void);
extern void lebuf_print_cub(long count);
extern void lebuf_print_cuf(long count);
extern void lebuf_print_cud(long count);
extern void lebuf_print_cuu(long count);
extern _Bool lebuf_print_el(void);
extern _Bool lebuf_print_ed(void);
extern _Bool lebuf_print_clear(void);
extern _Bool lebuf_print_op(void);
extern void lebuf_print_setfg(long color);
extern void lebuf_print_setbg(long color);
extern _Bool lebuf_print_sgr0(void);
extern _Bool lebuf_print_smso(void);
extern _Bool lebuf_print_smul(void);
extern _Bool lebuf_print_rev(void);
extern _Bool lebuf_print_blink(void);
extern _Bool lebuf_print_dim(void);
extern _Bool lebuf_print_bold(void);
extern _Bool lebuf_print_invis(void);
extern void lebuf_print_alert(_Bool direct_stderr);


extern int le_eof_char, le_kill_char, le_interrupt_char, le_erase_char;

extern _Bool le_set_terminal(void);
extern _Bool le_save_terminal(void);
extern _Bool le_restore_terminal(void);
extern _Bool le_allow_terminal_signal(_Bool allow);


#endif /* YASH_TERMINFO_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
