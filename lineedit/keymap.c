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
#include "../hashtable.h"
#include "../strbuf.h"
#include "display.h"
#include "key.h"
#include "keymap.h"
#include "lineedit.h"
#include "terminfo.h"
#include "trie.h"


/* Definition of editing modes. */
static yle_mode_T yle_modes[YLE_MODE_N];

/* The current editing mode.
 * Points to one of `yle_modes'. */
const yle_mode_T *yle_current_mode;

/* The keymap state. */
static struct {
    int count;
} state;


static inline void reset_count(void);
static inline int get_count(int default_value);


/* Initializes `yle_modes'.
 * Must not be called more than once. */
void yle_keymap_init(void)
{
    trie_T *t;

    yle_modes[YLE_MODE_VI_INSERT].default_command = cmd_self_insert;
    t = trie_create();
    t = trie_setw(t, Key_backslash, CMDENTRY(cmd_insert_backslash));
    t = trie_setw(t, Key_c_lb, CMDENTRY(cmd_setmode_vicommand));
    t = trie_setw(t, Key_c_j, CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_c_m, CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_interrupt, CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_c_c, CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_eof, CMDENTRY(cmd_eof_or_delete));
    t = trie_setw(t, Key_backspace, CMDENTRY(cmd_backward_delete_char));
    t = trie_setw(t, Key_erase, CMDENTRY(cmd_backward_delete_char));
    t = trie_setw(t, Key_c_h, CMDENTRY(cmd_backward_delete_char));
    //TODO
    yle_modes[YLE_MODE_VI_INSERT].keymap = t;

    yle_modes[YLE_MODE_VI_COMMAND].default_command = cmd_alert;
    t = trie_create();
    t = trie_setw(t, Key_c_lb, CMDENTRY(cmd_noop));
    t = trie_setw(t, L"i", CMDENTRY(cmd_setmode_viinsert));
    t = trie_setw(t, Key_c_j, CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_c_m, CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_interrupt, CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_c_c, CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_eof, CMDENTRY(cmd_eof_or_delete));
    //TODO
    yle_modes[YLE_MODE_VI_COMMAND].keymap = t;
}

/* Sets the editing mode to the one specified by `id'. */
void yle_set_mode(yle_mode_id_T id)
{
    assert(id < YLE_MODE_N);
    yle_current_mode = &yle_modes[id];
}

/* Resets the state of keymap. */
void yle_keymap_reset(void)
{
    reset_count();
}

/* Invokes the given command. */
void yle_keymap_invoke(yle_command_func_T *cmd, wchar_t arg)
{
    cmd(arg);

    if (yle_current_mode == &yle_modes[YLE_MODE_VI_COMMAND]) {
	if (yle_main_index > 0 && yle_main_index == yle_main_buffer.length) {
	    yle_main_index--;
	    yle_display_reposition_cursor();
	}
    }
}

/* Resets `state.count' */
void reset_count(void)
{
    state.count = -1;
}

/* Returns the count value.
 * If the count is not set, returns the `default_value'. */
int get_count(int default_value)
{
    return state.count < 0 ? default_value : state.count;
}


/********** Basic Commands **********/

/* Does nothing. */
void cmd_noop(wchar_t c __attribute__((unused)))
{
    reset_count();
}

/* Same as `cmd_noop', but causes alert. */
void cmd_alert(wchar_t c __attribute__((unused)))
{
    yle_alert();
    reset_count();
}

/* Inserts one character in the buffer. */
void cmd_self_insert(wchar_t c)
{
    if (c != L'\0') {
	int count = get_count(1);

	while (--count >= 0)
	    wb_ninsert_force(&yle_main_buffer, yle_main_index++, &c, 1);
	yle_display_reprint_buffer();
    }
    reset_count();
}

/* Inserts the backslash character. */
void cmd_insert_backslash(wchar_t c __attribute__((unused)))
{
    int count = get_count(1);

    while (--count >= 0)
	wb_ninsert_force(&yle_main_buffer, yle_main_index++, L"\\", 1);
    yle_display_reprint_buffer();
    reset_count();
}

/* Removes the character behind the cursor.
 * If the count is set, `count' characters are killed. */
void cmd_backward_delete_char(wchar_t c __attribute__((unused)))
{
    if (state.count < 0) {
	if (yle_main_index > 0) {
	    wb_remove(&yle_main_buffer, --yle_main_index, 1);
	    yle_display_reprint_buffer();
	}
    } else {
	// TODO cmd_backward_delete_char: kill characters
	reset_count();
    }
}

/* Accepts the current line.
 * `yle_state' is set to YLE_STATE_DONE and `yle_readline' returns. */
void cmd_accept_line(wchar_t c __attribute__((unused)))
{
    yle_state = YLE_STATE_DONE;
    reset_count();
}

/* Aborts the current line.
 * `yle_state' is set to YLE_STATE_INTERRUPTED and `yle_readline' returns. */
void cmd_abort_line(wchar_t c __attribute__((unused)))
{
    yle_state = YLE_STATE_INTERRUPTED;
    reset_count();
}

/* If the edit line is empty, sets `yle_state' to YLE_STATE_ERROR (return EOF).
 * Otherwise, deletes the character under the cursor. */
void cmd_eof_or_delete(wchar_t c __attribute__((unused)))
{
    if (yle_main_buffer.length == 0) {
	yle_state = YLE_STATE_ERROR;
    } else {
	wb_remove(&yle_main_buffer, yle_main_index, get_count(1));
	if (yle_main_index > yle_main_buffer.length)
	    yle_main_index = yle_main_buffer.length;
	yle_display_reprint_buffer();
    }
    reset_count();
}

/* Changes the editing mode to "vi insert". */
void cmd_setmode_viinsert(wchar_t c __attribute__((unused)))
{
    yle_set_mode(YLE_MODE_VI_INSERT);
    reset_count();
}

/* Changes the editing mode to "vi command". */
void cmd_setmode_vicommand(wchar_t c __attribute__((unused)))
{
    yle_set_mode(YLE_MODE_VI_COMMAND);
    reset_count();
}


/* vim: set ts=8 sts=4 sw=4 noet: */
