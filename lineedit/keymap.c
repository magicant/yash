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

#define Set(key, cmd) \
    (t = trie_setw(t, key, (trievalue_T) { .cmdfunc = (cmd) }))

    le_modes[LE_MODE_VI_INSERT].default_command = cmd_self_insert;
    t = trie_create();
    Set(Key_backslash, cmd_self_insert);
    Set(Key_c_v,       cmd_expect_verbatim);
    Set(Key_right,     cmd_forward_char);
    Set(Key_left,      cmd_backward_char);
    Set(Key_home,      cmd_beginning_of_line);
    Set(Key_end,       cmd_end_of_line);
    Set(L"\n",         cmd_accept_line);
    Set(L"\r",         cmd_accept_line);
    Set(Key_c_j,       cmd_accept_line);
    Set(Key_c_m,       cmd_accept_line);
    Set(Key_interrupt, cmd_abort_line);
    Set(Key_c_c,       cmd_abort_line);
    Set(Key_eof,       cmd_eof_if_empty);
    Set(Key_escape,    cmd_setmode_vicommand);
    Set(L"\f",         cmd_redraw_all);
    Set(Key_c_l,       cmd_redraw_all);
    Set(Key_delete,    cmd_delete_char);
    Set(Key_backspace, cmd_backward_delete_char);
    Set(Key_erase,     cmd_backward_delete_char);
    Set(Key_c_h,       cmd_backward_delete_char);
    Set(Key_c_w,       cmd_backward_delete_semiword);
    Set(Key_kill,      cmd_backward_delete_line);
    Set(Key_c_u,       cmd_backward_delete_line);
    Set(Key_down,      cmd_next_history_eol);
    Set(Key_c_n,       cmd_next_history_eol);
    Set(Key_up,        cmd_prev_history_eol);
    Set(Key_c_p,       cmd_prev_history_eol);
    le_modes[LE_MODE_VI_INSERT].keymap = t;

    le_modes[LE_MODE_VI_COMMAND].default_command = cmd_alert;
    t = trie_create();
    Set(Key_escape,    cmd_noop);
    Set(L"1",          cmd_digit_argument);
    Set(L"2",          cmd_digit_argument);
    Set(L"3",          cmd_digit_argument);
    Set(L"4",          cmd_digit_argument);
    Set(L"5",          cmd_digit_argument);
    Set(L"6",          cmd_digit_argument);
    Set(L"7",          cmd_digit_argument);
    Set(L"8",          cmd_digit_argument);
    Set(L"9",          cmd_digit_argument);
    Set(L"0",          cmd_bol_or_digit);
    Set(Key_c_j,       cmd_accept_line);
    Set(Key_c_m,       cmd_accept_line);
    Set(Key_interrupt, cmd_abort_line);
    Set(Key_c_c,       cmd_abort_line);
    Set(Key_eof,       cmd_eof_if_empty);
    Set(L"#",          cmd_accept_with_hash);
    Set(L"i",          cmd_setmode_viinsert);
    Set(Key_insert,    cmd_setmode_viinsert);
    Set(Key_c_l,       cmd_redraw_all);
    Set(L"l",          cmd_forward_char);
    Set(L" ",          cmd_forward_char);
    Set(Key_right,     cmd_forward_char);
    Set(L"h",          cmd_backward_char);
    Set(Key_left,      cmd_backward_char);
    Set(Key_backspace, cmd_backward_char);
    Set(Key_erase,     cmd_backward_char);
    Set(Key_c_h,       cmd_backward_char);
    Set(L"W",          cmd_forward_bigword);
    Set(L"E",          cmd_end_of_bigword);
    Set(L"B",          cmd_backward_bigword);
    Set(L"w",          cmd_forward_viword);
    Set(L"e",          cmd_end_of_viword);
    Set(L"b",          cmd_backward_viword);
    Set(Key_home,      cmd_beginning_of_line);
    Set(L"$",          cmd_end_of_line);
    Set(Key_end,       cmd_end_of_line);
    Set(L"^",          cmd_first_nonblank);
    Set(L"x",          cmd_kill_char);
    Set(Key_delete,    cmd_kill_char);
    Set(L"X",          cmd_backward_kill_char);
    Set(L"P",          cmd_put_before);
    Set(L"p",          cmd_put);
    Set(L"u",          cmd_undo);
    Set(L"U",          cmd_undo_all);
    Set(Key_c_r,       cmd_cancel_undo);
    Set(L".",          cmd_redo);
    Set(L"|",          cmd_vi_column);
    Set(L"f",          cmd_vi_find);
    Set(L"F",          cmd_vi_find_rev);
    Set(L"t",          cmd_vi_till);
    Set(L"T",          cmd_vi_till_rev);
    Set(L";",          cmd_vi_refind);
    Set(L",",          cmd_vi_refind_rev);
    Set(L"r",          cmd_vi_replace_char);
    Set(L"I",          cmd_vi_insert_beginning);
    Set(L"a",          cmd_vi_append);
    Set(L"A",          cmd_vi_append_end);
    Set(L"R",          cmd_vi_replace);
    Set(L"~",          cmd_vi_change_case);
    Set(L"y",          cmd_vi_yank);
    Set(L"Y",          cmd_vi_yank_to_eol);
    Set(L"d",          cmd_vi_delete);
    Set(L"D",          cmd_vi_delete_to_eol);
    Set(L"c",          cmd_vi_change);
    Set(L"C",          cmd_vi_change_to_eol);
    Set(L"S",          cmd_vi_change_all);
    Set(L"s",          cmd_vi_substitute);
    Set(L"_",          cmd_vi_append_last_bigword);
    Set(L"@",          cmd_vi_exec_alias);
    Set(L"v",          cmd_vi_edit_and_accept);
    Set(L"?",          cmd_vi_search_forward);
    Set(L"/",          cmd_vi_search_backward);
    Set(L"n",          cmd_search_again);
    Set(L"N",          cmd_search_again_rev);
    Set(L"G",          cmd_oldest_history);
    Set(L"g",          cmd_return_history);
    Set(L"j",          cmd_next_history);
    Set(L"+",          cmd_next_history);
    Set(Key_down,      cmd_next_history);
    Set(Key_c_n,       cmd_next_history);
    Set(L"k",          cmd_prev_history);
    Set(L"-",          cmd_prev_history);
    Set(Key_up,        cmd_prev_history);
    Set(Key_c_p,       cmd_prev_history);
    //TODO
    // =
    // \ 
    // *
    le_modes[LE_MODE_VI_COMMAND].keymap = t;

    le_modes[LE_MODE_VI_EXPECT].default_command = cmd_expect_char;
    t = trie_create();
    Set(Key_c_v,       cmd_expect_verbatim);
    Set(Key_interrupt, cmd_abort_line);
    Set(Key_c_c,       cmd_abort_line);
    Set(Key_backslash, cmd_expect_char);
    Set(Key_escape,    cmd_abort_expect_char);
    le_modes[LE_MODE_VI_EXPECT].keymap = t;

    le_modes[LE_MODE_VI_SEARCH].default_command = cmd_srch_self_insert;
    t = trie_create();
    Set(Key_c_v,       cmd_expect_verbatim);
    Set(Key_interrupt, cmd_abort_line);
    Set(Key_c_c,       cmd_abort_line);
    Set(Key_c_l,       cmd_redraw_all);
    Set(Key_backslash, cmd_srch_self_insert);
    Set(Key_backspace, cmd_srch_backward_delete_char);
    Set(Key_erase,     cmd_srch_backward_delete_char);
    Set(Key_c_h,       cmd_srch_backward_delete_char);
    Set(Key_kill,      cmd_srch_backward_delete_line);
    Set(Key_c_u,       cmd_srch_backward_delete_line);
    Set(Key_c_j,       cmd_srch_accept_search);
    Set(Key_c_m,       cmd_srch_accept_search);
    Set(Key_escape,    cmd_srch_abort_search);
    le_modes[LE_MODE_VI_SEARCH].keymap = t;

    le_modes[LE_MODE_EMACS].default_command = cmd_self_insert;
    t = trie_create();
    Set(Key_backslash,      cmd_self_insert);
    Set(Key_escape Key_c_i, cmd_insert_tab);
    Set(Key_c_q,            cmd_expect_verbatim);
    Set(Key_c_v,            cmd_expect_verbatim);
    Set(Key_escape "0",     cmd_digit_argument);
    Set(Key_escape "1",     cmd_digit_argument);
    Set(Key_escape "2",     cmd_digit_argument);
    Set(Key_escape "3",     cmd_digit_argument);
    Set(Key_escape "4",     cmd_digit_argument);
    Set(Key_escape "5",     cmd_digit_argument);
    Set(Key_escape "6",     cmd_digit_argument);
    Set(Key_escape "7",     cmd_digit_argument);
    Set(Key_escape "8",     cmd_digit_argument);
    Set(Key_escape "9",     cmd_digit_argument);
    Set(Key_escape "-",     cmd_digit_argument);
    Set(L"\n",              cmd_accept_line);
    Set(L"\r",              cmd_accept_line);
    Set(Key_c_j,            cmd_accept_line);
    Set(Key_c_m,            cmd_accept_line);
    Set(Key_interrupt,      cmd_abort_line);
    Set(Key_c_c,            cmd_abort_line);
    Set(Key_eof,            cmd_eof_or_delete);
    Set(Key_escape "#",     cmd_accept_with_hash);
    Set(Key_c_l,            cmd_redraw_all);
    Set(Key_right,          cmd_forward_char);
    Set(Key_c_f,            cmd_forward_char);
    Set(Key_left,           cmd_backward_char);
    Set(Key_c_b,            cmd_backward_char);
    Set(Key_escape "f",     cmd_forward_emacsword);
    Set(Key_escape "F",     cmd_forward_emacsword);
    Set(Key_escape "b",     cmd_backward_emacsword);
    Set(Key_escape "B",     cmd_backward_emacsword);
    Set(Key_home,           cmd_beginning_of_line);
    Set(Key_c_a,            cmd_beginning_of_line);
    Set(Key_end,            cmd_end_of_line);
    Set(Key_c_e,            cmd_end_of_line);
    Set(Key_delete,         cmd_delete_char);
    Set(Key_backspace,      cmd_backward_delete_char);
    Set(Key_erase,          cmd_backward_delete_char);
    Set(Key_c_h,            cmd_backward_delete_char);
    Set(Key_escape "d",     cmd_kill_emacsword);
    Set(Key_escape "D",     cmd_kill_emacsword);
    Set(Key_escape Key_backspace, cmd_backward_kill_emacsword);
    Set(Key_escape Key_erase, cmd_backward_kill_emacsword);
    Set(Key_c_h,            cmd_backward_kill_emacsword);
    Set(Key_c_k,            cmd_forward_kill_line);
    Set(Key_kill,           cmd_backward_kill_line);
    Set(Key_c_u,            cmd_backward_kill_line);
    Set(Key_c_y,            cmd_put_left);
    Set(Key_escape "y",     cmd_put_pop);
    Set(Key_escape "Y",     cmd_put_pop);
    Set(Key_c_w,            cmd_backward_kill_bigword);
    Set(Key_c_ul,           cmd_undo);
    Set(Key_c_x Key_c_u,    cmd_undo);
    Set(Key_c_x Key_kill,   cmd_undo);
    Set(Key_escape Key_c_r, cmd_undo_all);
    Set(Key_escape "r",     cmd_undo_all);
    Set(Key_escape "R",     cmd_undo_all);
    Set(Key_escape "<",     cmd_oldest_history_eol);
    Set(Key_escape ">",     cmd_return_history_eol);
    Set(Key_down,           cmd_next_history_eol);
    Set(Key_c_n,            cmd_next_history_eol);
    Set(Key_up,             cmd_prev_history_eol);
    Set(Key_c_p,            cmd_prev_history_eol);
    Set(Key_c_s,            cmd_emacs_search_forward);
    Set(Key_c_r,            cmd_emacs_search_backward);
    // TODO emacs keybinds: command search
    // TODO emacs keybinds: find_char
    // TODO emacs keybinds: other commands
    le_modes[LE_MODE_EMACS].keymap = t;

#undef Set
}

/* Sets the editing mode to the one specified by `id'. */
void le_set_mode(le_mode_id_T id)
{
    assert(id < LE_MODE_N);
    le_current_mode = &le_modes[id];
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
