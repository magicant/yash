/* Yash: yet another shell */
/* editing.h: main editing module */
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


#ifndef YASH_EDITING_H
#define YASH_EDITING_H

#include <stddef.h>
#include "../strbuf.h"
#include "key.h"


extern xwcsbuf_T yle_main_buffer;
extern size_t yle_main_index;

extern void yle_editing_init(void);
extern wchar_t *yle_editing_finalize(void)
    __attribute__((malloc,warn_unused_result));
extern void yle_invoke_command(yle_command_func_T *cmd, wchar_t arg)
    __attribute__((nonnull));


/********** Commands **********/

/* basic commands */
extern yle_command_func_T
    cmd_noop,
    cmd_alert,
    cmd_self_insert,
    cmd_expect_verbatim,
    cmd_insert_backslash,
    cmd_digit_argument,
    cmd_forward_char,
    cmd_backward_char,
    cmd_forward_bigword,
    cmd_end_of_bigword,
    cmd_backward_bigword,
    cmd_forward_word,
    cmd_end_of_word,
    cmd_backward_word,
    cmd_beginning_of_line,
    cmd_end_of_line,
    cmd_bol_or_digit,
    cmd_first_nonblank,
    cmd_accept_line,
    cmd_abort_line,
    cmd_eof_if_empty,
    cmd_eof_or_delete,
    cmd_accept_with_hash,
    cmd_setmode_viinsert,
    cmd_setmode_vicommand,
    cmd_expect_char,
    cmd_abort_expect_char,
    cmd_redraw_all;

/* editing commands */
extern yle_command_func_T
    cmd_delete_char,
    cmd_backward_delete_char,
    cmd_backward_delete_semiword,
    cmd_delete_line,
    cmd_forward_delete_line,
    cmd_backward_delete_line,
    cmd_kill_char,
    cmd_backward_kill_char,
    cmd_put_before,
    cmd_put,
    cmd_put_left,
    cmd_put_pop,
    cmd_undo,
    cmd_undo_all,
    cmd_cancel_undo,
    cmd_cancel_undo_all,
    cmd_redo;

/* vi-mode specific commands */
extern yle_command_func_T
    cmd_vi_column,
    cmd_vi_find,
    cmd_vi_find_rev,
    cmd_vi_till,
    cmd_vi_till_rev,
    cmd_vi_refind,
    cmd_vi_refind_rev,
    cmd_vi_replace_char,
    cmd_vi_insert_beginning,
    cmd_vi_append,
    cmd_vi_append_end,
    cmd_vi_replace,
    cmd_vi_change_case,
    cmd_vi_yank,
    cmd_vi_yank_to_eol,
    cmd_vi_delete,
    cmd_vi_delete_to_eol,
    cmd_vi_change,
    cmd_vi_change_to_eol,
    cmd_vi_change_all,
    cmd_vi_yank_and_change,
    cmd_vi_yank_and_change_to_eol,
    cmd_vi_yank_and_change_all,
    cmd_vi_substitute;


#endif /* YASH_EDITING_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
