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
typedef struct yle_mode_T {
    yle_command_func_T *default_command;
    struct trienode_T /* trie_T */ *keymap;
} yle_mode_T;

/* mode indeces */
typedef enum yle_mode_id_T {
    YLE_MODE_VI_INSERT,
    YLE_MODE_VI_COMMAND,
    //TODO currently, no emacs mode
    YLE_MODE_N,  // number of modes
} yle_mode_id_T;


extern const yle_mode_T *yle_current_mode;

extern void yle_keymap_init(void);
extern void yle_set_mode(yle_mode_id_T id);
extern void yle_keymap_reset(void);


#define CMDENTRY(cmdfunc_) \
    ((trievalue_T) { .cmdfunc = (cmdfunc_) })


/********** Basic Commands **********/

extern yle_command_func_T
    cmd_noop,
    cmd_alert,
    cmd_self_insert,
    cmd_insert_backslash,
    cmd_accept_line,
    cmd_abort_line,
    cmd_setmode_viinsert,
    cmd_setmode_vicommand;


#endif /* YASH_KEYMAP_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
