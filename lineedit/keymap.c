/* Yash: yet another shell */
/* keymap.c: mappings from keys to functions */
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


#include "../common.h"
#include <assert.h>
#include <stddef.h>
#include "../strbuf.h"
#include "../util.h"
#include "editing.h"
#include "key.h"
#include "keymap.h"
#include "trie.h"


/* Definition of editing modes. */
le_mode_T le_modes[LE_MODE_N];

/* The current editing mode.
 * Points to one of `le_modes'. */
le_mode_T *le_current_mode;


/* Initializes `le_modes'.
 * Must not be called more than once. */
void le_keymap_init(void)
{
    trie_T *t;

    le_modes[LE_MODE_VI_INSERT].default_command = cmd_self_insert;
    t = trie_create();
    t = trie_setw(t, Key_backslash, CMDENTRY(cmd_self_insert));
    t = trie_setw(t, Key_c_v,       CMDENTRY(cmd_expect_verbatim));
    t = trie_setw(t, Key_right,     CMDENTRY(cmd_forward_char));
    t = trie_setw(t, Key_left,      CMDENTRY(cmd_backward_char));
    t = trie_setw(t, Key_home,      CMDENTRY(cmd_beginning_of_line));
    t = trie_setw(t, Key_end,       CMDENTRY(cmd_end_of_line));
    t = trie_setw(t, L"\n",          CMDENTRY(cmd_accept_line));
    t = trie_setw(t, L"\r",          CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_c_j,       CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_c_m,       CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_interrupt, CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_c_c,       CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_eof,       CMDENTRY(cmd_eof_if_empty));
    t = trie_setw(t, Key_escape,    CMDENTRY(cmd_setmode_vicommand));
    t = trie_setw(t, Key_c_l,       CMDENTRY(cmd_redraw_all));
    t = trie_setw(t, Key_delete,    CMDENTRY(cmd_delete_char));
    t = trie_setw(t, Key_backspace, CMDENTRY(cmd_backward_delete_char));
    t = trie_setw(t, Key_erase,     CMDENTRY(cmd_backward_delete_char));
    t = trie_setw(t, Key_c_h,       CMDENTRY(cmd_backward_delete_char));
    t = trie_setw(t, Key_c_w,       CMDENTRY(cmd_backward_delete_semiword));
    t = trie_setw(t, Key_kill,      CMDENTRY(cmd_backward_delete_line));
    t = trie_setw(t, Key_c_u,       CMDENTRY(cmd_backward_delete_line));
    t = trie_setw(t, Key_down,      CMDENTRY(cmd_next_history_eol));
    t = trie_setw(t, Key_c_n,       CMDENTRY(cmd_next_history_eol));
    t = trie_setw(t, Key_up,        CMDENTRY(cmd_prev_history_eol));
    t = trie_setw(t, Key_c_p,       CMDENTRY(cmd_prev_history_eol));
    le_modes[LE_MODE_VI_INSERT].keymap = t;

    le_modes[LE_MODE_VI_COMMAND].default_command = cmd_alert;
    t = trie_create();
    t = trie_setw(t, Key_escape,    CMDENTRY(cmd_noop));
    t = trie_setw(t, L"1",          CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, L"2",          CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, L"3",          CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, L"4",          CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, L"5",          CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, L"6",          CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, L"7",          CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, L"8",          CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, L"9",          CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, L"l",          CMDENTRY(cmd_forward_char));
    t = trie_setw(t, L" ",          CMDENTRY(cmd_forward_char));
    t = trie_setw(t, Key_right,     CMDENTRY(cmd_forward_char));
    t = trie_setw(t, L"h",          CMDENTRY(cmd_backward_char));
    t = trie_setw(t, Key_left,      CMDENTRY(cmd_backward_char));
    t = trie_setw(t, Key_backspace, CMDENTRY(cmd_backward_char));
    t = trie_setw(t, Key_erase,     CMDENTRY(cmd_backward_char));
    t = trie_setw(t, Key_c_h,       CMDENTRY(cmd_backward_char));
    t = trie_setw(t, L"W",          CMDENTRY(cmd_forward_bigword));
    t = trie_setw(t, L"E",          CMDENTRY(cmd_end_of_bigword));
    t = trie_setw(t, L"B",          CMDENTRY(cmd_backward_bigword));
    t = trie_setw(t, L"w",          CMDENTRY(cmd_forward_viword));
    t = trie_setw(t, L"e",          CMDENTRY(cmd_end_of_viword));
    t = trie_setw(t, L"b",          CMDENTRY(cmd_backward_viword));
    t = trie_setw(t, Key_home,      CMDENTRY(cmd_beginning_of_line));
    t = trie_setw(t, L"$",          CMDENTRY(cmd_end_of_line));
    t = trie_setw(t, Key_end,       CMDENTRY(cmd_end_of_line));
    t = trie_setw(t, L"0",          CMDENTRY(cmd_bol_or_digit));
    t = trie_setw(t, L"^",          CMDENTRY(cmd_first_nonblank));
    t = trie_setw(t, Key_c_j,       CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_c_m,       CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_interrupt, CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_c_c,       CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_eof,       CMDENTRY(cmd_eof_if_empty));
    t = trie_setw(t, L"#",          CMDENTRY(cmd_accept_with_hash));
    t = trie_setw(t, L"i",          CMDENTRY(cmd_setmode_viinsert));
    t = trie_setw(t, Key_insert,    CMDENTRY(cmd_setmode_viinsert));
    t = trie_setw(t, Key_c_l,       CMDENTRY(cmd_redraw_all));
    t = trie_setw(t, L"x",          CMDENTRY(cmd_kill_char));
    t = trie_setw(t, Key_delete,    CMDENTRY(cmd_kill_char));
    t = trie_setw(t, L"X",          CMDENTRY(cmd_backward_kill_char));
    t = trie_setw(t, L"P",          CMDENTRY(cmd_put_before));
    t = trie_setw(t, L"p",          CMDENTRY(cmd_put));
    t = trie_setw(t, L"u",          CMDENTRY(cmd_undo));
    t = trie_setw(t, L"U",          CMDENTRY(cmd_undo_all));
    t = trie_setw(t, Key_c_r,       CMDENTRY(cmd_cancel_undo));
    t = trie_setw(t, L".",          CMDENTRY(cmd_redo));
    t = trie_setw(t, L"|",          CMDENTRY(cmd_vi_column));
    t = trie_setw(t, L"f",          CMDENTRY(cmd_vi_find));
    t = trie_setw(t, L"F",          CMDENTRY(cmd_vi_find_rev));
    t = trie_setw(t, L"t",          CMDENTRY(cmd_vi_till));
    t = trie_setw(t, L"T",          CMDENTRY(cmd_vi_till_rev));
    t = trie_setw(t, L";",          CMDENTRY(cmd_vi_refind));
    t = trie_setw(t, L",",          CMDENTRY(cmd_vi_refind_rev));
    t = trie_setw(t, L"r",          CMDENTRY(cmd_vi_replace_char));
    t = trie_setw(t, L"I",          CMDENTRY(cmd_vi_insert_beginning));
    t = trie_setw(t, L"a",          CMDENTRY(cmd_vi_append));
    t = trie_setw(t, L"A",          CMDENTRY(cmd_vi_append_end));
    t = trie_setw(t, L"R",          CMDENTRY(cmd_vi_replace));
    t = trie_setw(t, L"~",          CMDENTRY(cmd_vi_change_case));
    t = trie_setw(t, L"y",          CMDENTRY(cmd_vi_yank));
    t = trie_setw(t, L"Y",          CMDENTRY(cmd_vi_yank_to_eol));
    t = trie_setw(t, L"d",          CMDENTRY(cmd_vi_delete));
    t = trie_setw(t, L"D",          CMDENTRY(cmd_vi_delete_to_eol));
    t = trie_setw(t, L"c",          CMDENTRY(cmd_vi_change));
    t = trie_setw(t, L"C",          CMDENTRY(cmd_vi_change_to_eol));
    t = trie_setw(t, L"S",          CMDENTRY(cmd_vi_change_all));
    t = trie_setw(t, L"s",          CMDENTRY(cmd_vi_substitute));
    t = trie_setw(t, L"_",          CMDENTRY(cmd_vi_append_last_bigword));
    t = trie_setw(t, L"@",          CMDENTRY(cmd_vi_exec_alias));
    t = trie_setw(t, L"v",          CMDENTRY(cmd_vi_edit_and_accept));
    t = trie_setw(t, L"?",          CMDENTRY(cmd_vi_search_forward));
    t = trie_setw(t, L"/",          CMDENTRY(cmd_vi_search_backward));
    t = trie_setw(t, L"n",          CMDENTRY(cmd_vi_search_again));
    t = trie_setw(t, L"N",          CMDENTRY(cmd_vi_search_again_rev));
    t = trie_setw(t, L"G",          CMDENTRY(cmd_oldest_history));
    t = trie_setw(t, L"g",          CMDENTRY(cmd_return_history));
    t = trie_setw(t, L"j",          CMDENTRY(cmd_next_history));
    t = trie_setw(t, L"+",          CMDENTRY(cmd_next_history));
    t = trie_setw(t, Key_down,      CMDENTRY(cmd_next_history));
    t = trie_setw(t, Key_c_n,       CMDENTRY(cmd_next_history));
    t = trie_setw(t, L"k",          CMDENTRY(cmd_prev_history));
    t = trie_setw(t, L"-",          CMDENTRY(cmd_prev_history));
    t = trie_setw(t, Key_up,        CMDENTRY(cmd_prev_history));
    t = trie_setw(t, Key_c_p,       CMDENTRY(cmd_prev_history));
    //TODO
    // =
    // \ 
    // *
    le_modes[LE_MODE_VI_COMMAND].keymap = t;

    le_modes[LE_MODE_VI_EXPECT].default_command = cmd_expect_char;
    t = trie_create();
    t = trie_setw(t, Key_c_v,       CMDENTRY(cmd_expect_verbatim));
    t = trie_setw(t, Key_interrupt, CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_c_c,       CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_backslash, CMDENTRY(cmd_expect_char));
    t = trie_setw(t, Key_escape,    CMDENTRY(cmd_abort_expect_char));
    le_modes[LE_MODE_VI_EXPECT].keymap = t;

    le_modes[LE_MODE_VI_SEARCH].default_command = cmd_srch_self_insert;
    t = trie_create();
    t = trie_setw(t, Key_c_v,       CMDENTRY(cmd_expect_verbatim));
    t = trie_setw(t, Key_interrupt, CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_c_c,       CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_c_l,       CMDENTRY(cmd_redraw_all));
    t = trie_setw(t, Key_backslash, CMDENTRY(cmd_srch_self_insert));
    t = trie_setw(t, Key_backspace, CMDENTRY(cmd_srch_backward_delete_char));
    t = trie_setw(t, Key_erase,     CMDENTRY(cmd_srch_backward_delete_char));
    t = trie_setw(t, Key_c_h,       CMDENTRY(cmd_srch_backward_delete_char));
    t = trie_setw(t, Key_kill,      CMDENTRY(cmd_srch_backward_delete_line));
    t = trie_setw(t, Key_c_u,       CMDENTRY(cmd_srch_backward_delete_line));
    t = trie_setw(t, Key_c_j,       CMDENTRY(cmd_srch_accept_search));
    t = trie_setw(t, Key_c_m,       CMDENTRY(cmd_srch_accept_search));
    t = trie_setw(t, Key_escape,    CMDENTRY(cmd_srch_abort_search));
    le_modes[LE_MODE_VI_SEARCH].keymap = t;

    le_modes[LE_MODE_EMACS].default_command = cmd_self_insert;
    t = trie_create();
    t = trie_setw(t, Key_backslash, CMDENTRY(cmd_self_insert));
    t = trie_setw(t, Key_c_v,       CMDENTRY(cmd_expect_verbatim));
    t = trie_setw(t, Key_escape "0", CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, Key_escape "1", CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, Key_escape "2", CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, Key_escape "3", CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, Key_escape "4", CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, Key_escape "5", CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, Key_escape "6", CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, Key_escape "7", CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, Key_escape "8", CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, Key_escape "9", CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, Key_escape "-", CMDENTRY(cmd_digit_argument));
    t = trie_setw(t, Key_right,     CMDENTRY(cmd_forward_char));
    t = trie_setw(t, Key_c_f,       CMDENTRY(cmd_forward_char));
    t = trie_setw(t, Key_left,      CMDENTRY(cmd_backward_char));
    t = trie_setw(t, Key_c_b,       CMDENTRY(cmd_backward_char));
    t = trie_setw(t, Key_escape "f", CMDENTRY(cmd_forward_nonword));
    t = trie_setw(t, Key_escape "F", CMDENTRY(cmd_forward_nonword));
    t = trie_setw(t, Key_escape "b", CMDENTRY(cmd_backward_word));
    t = trie_setw(t, Key_escape "B", CMDENTRY(cmd_backward_word));
    t = trie_setw(t, Key_home,      CMDENTRY(cmd_beginning_of_line));
    t = trie_setw(t, Key_c_a,       CMDENTRY(cmd_beginning_of_line));
    t = trie_setw(t, Key_end,       CMDENTRY(cmd_end_of_line));
    t = trie_setw(t, Key_c_e,       CMDENTRY(cmd_end_of_line));
    t = trie_setw(t, L"\n",         CMDENTRY(cmd_accept_line));
    t = trie_setw(t, L"\r",         CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_c_j,       CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_c_m,       CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_interrupt, CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_c_c,       CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_eof,       CMDENTRY(cmd_eof_or_delete));
    t = trie_setw(t, Key_c_l,       CMDENTRY(cmd_redraw_all));
    t = trie_setw(t, Key_delete,    CMDENTRY(cmd_delete_char));
    t = trie_setw(t, Key_backspace, CMDENTRY(cmd_backward_delete_char));
    t = trie_setw(t, Key_erase,     CMDENTRY(cmd_backward_delete_char));
    t = trie_setw(t, Key_c_h,       CMDENTRY(cmd_backward_delete_char));
    t = trie_setw(t, Key_c_k,       CMDENTRY(cmd_forward_kill_line));
    t = trie_setw(t, Key_kill,      CMDENTRY(cmd_backward_kill_line));
    t = trie_setw(t, Key_c_u,       CMDENTRY(cmd_backward_kill_line));
    t = trie_setw(t, Key_c_y,       CMDENTRY(cmd_put_left));
    t = trie_setw(t, Key_escape "y", CMDENTRY(cmd_put_pop));
    t = trie_setw(t, Key_escape "Y", CMDENTRY(cmd_put_pop));
    // TODO emacs cmd_backward_kill_word, cmd_backward_kill_semiword
    //t = trie_setw(t, Key_c_w,       CMDENTRY(cmd_backward_kill_word));
    t = trie_setw(t, Key_c_ul,      CMDENTRY(cmd_undo));
    t = trie_setw(t, Key_c_x Key_c_u, CMDENTRY(cmd_undo));
    t = trie_setw(t, Key_c_x Key_kill, CMDENTRY(cmd_undo));
    t = trie_setw(t, Key_escape "r", CMDENTRY(cmd_undo_all));
    t = trie_setw(t, Key_escape "R", CMDENTRY(cmd_undo_all));
    t = trie_setw(t, Key_escape "<", CMDENTRY(cmd_oldest_history_eol));
    t = trie_setw(t, Key_escape ">", CMDENTRY(cmd_return_history_eol));
    t = trie_setw(t, Key_c_n,       CMDENTRY(cmd_next_history_eol));
    t = trie_setw(t, Key_c_p,       CMDENTRY(cmd_prev_history_eol));
    // TODO emacs keybinds: command search
    le_modes[LE_MODE_EMACS].keymap = t;
}

/* Sets the editing mode to the one specified by `id'. */
void le_set_mode(le_mode_id_T id)
{
    assert(id < LE_MODE_N);
    le_current_mode = &le_modes[id];
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
