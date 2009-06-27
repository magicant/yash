/* Yash: yet another shell */
/* keymap.h: mappings from keys to functions */
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


#ifndef YASH_KEYMAP_H
#define YASH_KEYMAP_H

#include <stddef.h>
#include "key.h"


/* This structure describes an editing mode. */
typedef struct le_mode_T {
    le_command_func_T *default_command;
    struct trienode_T /* trie_T */ *keymap;
} le_mode_T;

/* mode indeces */
typedef enum le_mode_id_T {
    LE_MODE_VI_INSERT,
    LE_MODE_VI_COMMAND,
    LE_MODE_VI_EXPECT,
    LE_MODE_VI_SEARCH,
    LE_MODE_EMACS,
    LE_MODE_N,  // number of modes
} le_mode_id_T;


extern le_mode_T le_modes[LE_MODE_N];
extern le_mode_T *le_current_mode;

extern void le_keymap_init(void);
extern const le_mode_T *le_get_mode(le_mode_id_T id)
    __attribute__((const));
extern void le_set_mode(le_mode_id_T id);


#define CMDENTRY(cmdfunc_) \
    ((trievalue_T) { .cmdfunc = (cmdfunc_) })


#endif /* YASH_KEYMAP_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
