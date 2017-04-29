/* Yash: yet another shell */
/* editing.h: main editing module */
/* (C) 2007-2017 magicant */

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


extern xwcsbuf_T le_main_buffer;
extern size_t le_main_length, le_main_index;

enum le_search_direction_T { FORWARD, BACKWARD, };
/* FORWARD:  find the oldest candidate from the ones newer than the current
 * BACKWARD: find the newest candidate from the ones older than the current */
enum le_search_type_T { SEARCH_PREFIX, SEARCH_VI, SEARCH_EMACS, };

extern enum le_search_direction_T le_search_direction;
extern enum le_search_type_T le_search_type;
extern xwcsbuf_T le_search_buffer;
extern const struct histlink_T *le_search_result;

extern void le_editing_init(void);
extern wchar_t *le_editing_finalize(void)
    __attribute__((malloc,warn_unused_result));
extern void le_invoke_command(le_command_func_T *cmd, wchar_t arg)
    __attribute__((nonnull));


/********** Commands **********/

// This header file is parsed by the Makefile script to create the "commands.in"
// file. For every line that ends with "/*C*/", the first word that starts with
// "cmd_" is interpreted as a command name.

/* basic commands */
extern le_command_func_T
    cmd_noop, /*C*/
    cmd_alert, /*C*/
    cmd_self_insert, /*C*/
    cmd_insert_tab, /*C*/
    cmd_expect_verbatim, /*C*/
    cmd_digit_argument, /*C*/
    cmd_bol_or_digit, /*C*/
    cmd_accept_line, /*C*/
    cmd_abort_line, /*C*/
    cmd_eof, /*C*/
    cmd_eof_if_empty, /*C*/
    cmd_eof_or_delete, /*C*/
    cmd_accept_with_hash, /*C*/
    cmd_accept_prediction, /*C*/
    cmd_setmode_viinsert, /*C*/
    cmd_setmode_vicommand, /*C*/
    cmd_setmode_emacs, /*C*/
    cmd_expect_char, /*C*/
    cmd_abort_expect_char, /*C*/
    cmd_redraw_all, /*C*/
    cmd_clear_and_redraw_all; /*C*/

/* motion commands */
extern le_command_func_T
    cmd_forward_char, /*C*/
    cmd_backward_char, /*C*/
    cmd_forward_bigword, /*C*/
    cmd_end_of_bigword, /*C*/
    cmd_backward_bigword, /*C*/
    cmd_forward_semiword, /*C*/
    cmd_end_of_semiword, /*C*/
    cmd_backward_semiword, /*C*/
    cmd_forward_viword, /*C*/
    cmd_end_of_viword, /*C*/
    cmd_backward_viword, /*C*/
    cmd_forward_emacsword, /*C*/
    cmd_backward_emacsword, /*C*/
    cmd_beginning_of_line, /*C*/
    cmd_end_of_line, /*C*/
    cmd_go_to_column, /*C*/
    cmd_first_nonblank, /*C*/
    cmd_find_char, /*C*/
    cmd_find_char_rev, /*C*/
    cmd_till_char, /*C*/
    cmd_till_char_rev, /*C*/
    cmd_refind_char, /*C*/
    cmd_refind_char_rev; /*C*/

/* editing commands */
extern le_command_func_T
    cmd_delete_char, /*C*/
    cmd_delete_bigword, /*C*/
    cmd_delete_semiword, /*C*/
    cmd_delete_viword, /*C*/
    cmd_delete_emacsword, /*C*/
    cmd_backward_delete_char, /*C*/
    cmd_backward_delete_bigword, /*C*/
    cmd_backward_delete_semiword, /*C*/
    cmd_backward_delete_viword, /*C*/
    cmd_backward_delete_emacsword, /*C*/
    cmd_delete_line, /*C*/
    cmd_forward_delete_line, /*C*/
    cmd_backward_delete_line, /*C*/
    cmd_kill_char, /*C*/
    cmd_kill_bigword, /*C*/
    cmd_kill_semiword, /*C*/
    cmd_kill_viword, /*C*/
    cmd_kill_emacsword, /*C*/
    cmd_backward_kill_char, /*C*/
    cmd_backward_kill_bigword, /*C*/
    cmd_backward_kill_semiword, /*C*/
    cmd_backward_kill_viword, /*C*/
    cmd_backward_kill_emacsword, /*C*/
    cmd_kill_line, /*C*/
    cmd_forward_kill_line, /*C*/
    cmd_backward_kill_line, /*C*/
    cmd_put_before, /*C*/
    cmd_put, /*C*/
    cmd_put_left, /*C*/
    cmd_put_pop, /*C*/
    cmd_undo, /*C*/
    cmd_undo_all, /*C*/
    cmd_cancel_undo, /*C*/
    cmd_cancel_undo_all, /*C*/
    cmd_redo; /*C*/

/* completion commands */
extern le_command_func_T
    cmd_complete, /*C*/
    cmd_complete_next_candidate, /*C*/
    cmd_complete_prev_candidate, /*C*/
    cmd_complete_next_column, /*C*/
    cmd_complete_prev_column, /*C*/
    cmd_complete_next_page, /*C*/
    cmd_complete_prev_page, /*C*/
    cmd_complete_list, /*C*/
    cmd_complete_all, /*C*/
    cmd_complete_max, /*C*/
    cmd_complete_max_then_list, /*C*/
    cmd_complete_max_then_next_candidate, /*C*/
    cmd_complete_max_then_prev_candidate, /*C*/
    cmd_clear_candidates; /*C*/

/* vi-mode specific commands */
extern le_command_func_T
    cmd_vi_replace_char, /*C*/
    cmd_vi_insert_beginning, /*C*/
    cmd_vi_append, /*C*/
    cmd_vi_append_to_eol, /*C*/
    cmd_vi_replace, /*C*/
    cmd_vi_switch_case, /*C*/
    cmd_vi_switch_case_char, /*C*/
    cmd_vi_yank, /*C*/
    cmd_vi_yank_to_eol, /*C*/
    cmd_vi_delete, /*C*/
#define cmd_vi_delete_to_eol cmd_forward_kill_line /*C*/
    cmd_vi_change, /*C*/
    cmd_vi_change_to_eol, /*C*/
    cmd_vi_change_line, /*C*/
    cmd_vi_yank_and_change, /*C*/
    cmd_vi_yank_and_change_to_eol, /*C*/
    cmd_vi_yank_and_change_line, /*C*/
    cmd_vi_substitute, /*C*/
    cmd_vi_append_last_bigword, /*C*/
    cmd_vi_exec_alias, /*C*/
    cmd_vi_edit_and_accept, /*C*/
    cmd_vi_complete_list, /*C*/
    cmd_vi_complete_all, /*C*/
    cmd_vi_complete_max, /*C*/
    cmd_vi_search_forward, /*C*/
    cmd_vi_search_backward; /*C*/

/* emacs-mode specific commands */
extern le_command_func_T
    cmd_emacs_transpose_chars, /*C*/
    cmd_emacs_transpose_words, /*C*/
    cmd_emacs_upcase_word, /*C*/
    cmd_emacs_downcase_word, /*C*/
    cmd_emacs_capitalize_word, /*C*/
    cmd_emacs_delete_horizontal_space, /*C*/
    cmd_emacs_just_one_space, /*C*/
    cmd_emacs_search_forward, /*C*/
    cmd_emacs_search_backward; /*C*/

/* history-related commands */
extern le_command_func_T
    cmd_oldest_history, /*C*/
    cmd_newest_history, /*C*/
    cmd_return_history, /*C*/
    cmd_oldest_history_bol, /*C*/
    cmd_newest_history_bol, /*C*/
    cmd_return_history_bol, /*C*/
    cmd_oldest_history_eol, /*C*/
    cmd_newest_history_eol, /*C*/
    cmd_return_history_eol, /*C*/
    cmd_next_history, /*C*/
    cmd_prev_history, /*C*/
    cmd_next_history_bol, /*C*/
    cmd_prev_history_bol, /*C*/
    cmd_next_history_eol, /*C*/
    cmd_prev_history_eol, /*C*/
    /* command history search */
    cmd_srch_self_insert, /*C*/
    cmd_srch_backward_delete_char, /*C*/
    cmd_srch_backward_delete_line, /*C*/
    cmd_srch_continue_forward, /*C*/
    cmd_srch_continue_backward, /*C*/
    cmd_srch_accept_search, /*C*/
    cmd_srch_abort_search, /*C*/
    cmd_search_again, /*C*/
    cmd_search_again_rev, /*C*/
    cmd_search_again_forward, /*C*/
    cmd_search_again_backward, /*C*/
    cmd_beginning_search_forward, /*C*/
    cmd_beginning_search_backward; /*C*/


#endif /* YASH_EDITING_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
