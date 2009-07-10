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


extern xwcsbuf_T le_main_buffer;
extern size_t le_main_index;

enum le_search_direction { FORWARD, BACKWARD, };
/* FORWARD:  find the oldest candidate from the ones newer than the current
 * BACKWARD: find the newest candidate from the ones older than the current */
enum le_search_type { SEARCH_VI, SEARCH_EMACS, };

extern enum le_search_direction le_search_direction;
extern enum le_search_type le_search_type;
extern xwcsbuf_T le_search_buffer;
extern const struct histentry_T *le_search_result;

extern void le_editing_init(void);
extern wchar_t *le_editing_finalize(void)
    __attribute__((malloc,warn_unused_result));
extern void le_invoke_command(le_command_func_T *cmd, wchar_t arg)
    __attribute__((nonnull));


/********** Commands **********/

/* basic commands */
extern le_command_func_T
    cmd_noop,
    cmd_alert,
    cmd_self_insert,
    cmd_insert_tab,
    cmd_expect_verbatim,
    cmd_digit_argument,
    cmd_bol_or_digit,
    cmd_accept_line,
    cmd_abort_line,
    cmd_eof_if_empty,
    cmd_eof_or_delete,
    cmd_accept_with_hash,
    cmd_setmode_viinsert,
    cmd_setmode_vicommand,
    cmd_setmode_emacs,
    cmd_expect_char,
    cmd_abort_expect_char,
    cmd_redraw_all,
    cmd_clear_and_redraw_all;

/* motion commands */
extern le_command_func_T
    cmd_forward_char,
    cmd_backward_char,
    cmd_forward_bigword,
    cmd_end_of_bigword,
    cmd_backward_bigword,
    cmd_forward_semiword,
    cmd_end_of_semiword,
    cmd_backward_semiword,
    cmd_forward_viword,
    cmd_end_of_viword,
    cmd_backward_viword,
    cmd_forward_emacsword,
    cmd_backward_emacsword,
    cmd_beginning_of_line,
    cmd_end_of_line,
    cmd_go_to_column,
    cmd_first_nonblank,
    cmd_find_char,
    cmd_find_char_rev,
    cmd_till_char,
    cmd_till_char_rev,
    cmd_refind_char,
    cmd_refind_char_rev;

/* editing commands */
extern le_command_func_T
    cmd_delete_char,
    cmd_delete_bigword,
    cmd_delete_semiword,
    cmd_delete_viword,
    cmd_delete_emacsword,
    cmd_backward_delete_char,
    cmd_backward_delete_bigword,
    cmd_backward_delete_semiword,
    cmd_backward_delete_viword,
    cmd_backward_delete_emacsword,
    cmd_delete_line,
    cmd_forward_delete_line,
    cmd_backward_delete_line,
    cmd_kill_char,
    cmd_kill_bigword,
    cmd_kill_semiword,
    cmd_kill_viword,
    cmd_kill_emacsword,
    cmd_backward_kill_char,
    cmd_backward_kill_bigword,
    cmd_backward_kill_semiword,
    cmd_backward_kill_viword,
    cmd_backward_kill_emacsword,
    cmd_kill_line,
    cmd_forward_kill_line,
    cmd_backward_kill_line,
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
extern le_command_func_T
    cmd_vi_replace_char,
    cmd_vi_insert_beginning,
    cmd_vi_append,
    cmd_vi_append_end,
    cmd_vi_replace,
    cmd_vi_switch_case_char,
    cmd_vi_yank,
    cmd_vi_yank_to_eol,
    cmd_vi_delete,
#define cmd_vi_delete_to_eol cmd_forward_kill_line
    cmd_vi_change,
    cmd_vi_change_to_eol,
    cmd_vi_change_all,
    cmd_vi_yank_and_change,
    cmd_vi_yank_and_change_to_eol,
    cmd_vi_yank_and_change_all,
    cmd_vi_substitute,
    cmd_vi_append_last_bigword,
    cmd_vi_exec_alias,
    cmd_vi_edit_and_accept,
    cmd_vi_search_forward,
    cmd_vi_search_backward;

/* emacs-mode specific commands */
extern le_command_func_T
    cmd_emacs_transpose_chars,
    cmd_emacs_transpose_words,
    cmd_emacs_upcase_word,
    cmd_emacs_downcase_word,
    cmd_emacs_capitalize_word,
    cmd_emacs_delete_horizontal_space,
    cmd_emacs_just_one_space,
    cmd_emacs_search_forward,
    cmd_emacs_search_backward;

/* history-related commands */
extern le_command_func_T
    cmd_oldest_history,
    cmd_newest_history,
    cmd_return_history,
    cmd_oldest_history_eol,
    cmd_newest_history_eol,
    cmd_return_history_eol,
    cmd_next_history,
    cmd_prev_history,
    cmd_next_history_eol,
    cmd_prev_history_eol,
    /* command history search */
    cmd_srch_self_insert,
    cmd_srch_backward_delete_char,
    cmd_srch_backward_delete_line,
    cmd_srch_continue_forward,
    cmd_srch_continue_backward,
    cmd_srch_accept_search,
    cmd_srch_abort_search,
    cmd_search_again,
    cmd_search_again_rev,
    cmd_search_again_forward,
    cmd_search_again_backward;


#endif /* YASH_EDITING_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
