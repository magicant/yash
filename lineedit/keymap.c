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
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <wctype.h>
#include "../hashtable.h"
#include "../strbuf.h"
#include "../util.h"
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
    struct {
	/* When count is not specified, `sign' and `abs' is 0.
	 * Otherwise, `sign' is 1 or -1.
	 * When the negative sign is specified but digits are not, `abs' is 0.*/
	int sign;
	unsigned abs;
#define COUNT_ABS_MAX 999999999
    } count;
} state;

/* If true, characters are overwritten rather than inserted. */
static bool overwrite = false;

/* The kill ring */
#define KILL_RING_SIZE 30
static wchar_t *kill_ring[KILL_RING_SIZE];
/* The index of the element to which next killed string is assigned. */
static size_t next_kill_index = 0;
/* The index of the element which was put last. */
static size_t last_put_index = 0;


static inline void reset_count(void);
static inline int get_count(int default_value);
static void exec_motion_command(size_t index, bool inclusive);
static void add_to_kill_ring(const wchar_t *s, size_t n)
    __attribute__((nonnull));

static bool alert_if_first(void);
static bool alert_if_last(void);
static void move_cursor_forward_char(int offset);
static void move_cursor_backward_char(int offset);
static void move_cursor_forward_bigword(int count);
static void move_cursor_backward_bigword(int count);
static void move_cursor_forward_word(int count);
static void move_cursor_backward_word(int count);
static size_t next_bigword_index(const wchar_t *s, size_t i)
    __attribute__((nonnull));
static size_t next_end_of_bigword_index(const wchar_t *s, size_t i)
    __attribute__((nonnull));
static size_t previous_bigword_index(const wchar_t *s, size_t i)
    __attribute__((nonnull));
static size_t next_word_index(const wchar_t *s, size_t i)
    __attribute__((nonnull));
static size_t next_end_of_word_index(const wchar_t *s, size_t i)
    __attribute__((nonnull));
static size_t previous_word_index(const wchar_t *s, size_t i)
    __attribute__((nonnull));
static void kill_chars(bool backward);
static inline bool is_blank_or_punct(wchar_t c)
    __attribute__((pure));


/* Initializes `yle_modes'.
 * Must not be called more than once. */
void yle_keymap_init(void)
{
    trie_T *t;

    yle_modes[YLE_MODE_VI_INSERT].default_command = cmd_self_insert;
    t = trie_create();
    t = trie_setw(t, Key_c_v,       CMDENTRY(cmd_expect_verbatim));
    t = trie_setw(t, Key_backslash, CMDENTRY(cmd_insert_backslash));
    t = trie_setw(t, Key_right,     CMDENTRY(cmd_forward_char));
    t = trie_setw(t, Key_left,      CMDENTRY(cmd_backward_char));
    t = trie_setw(t, Key_home,      CMDENTRY(cmd_beginning_of_line));
    t = trie_setw(t, Key_end,       CMDENTRY(cmd_end_of_line));
    t = trie_setw(t, Key_c_j,       CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_c_m,       CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_interrupt, CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_c_c,       CMDENTRY(cmd_abort_line));
    t = trie_setw(t, Key_eof,       CMDENTRY(cmd_eof_if_empty));
    t = trie_setw(t, Key_c_lb,      CMDENTRY(cmd_setmode_vicommand));
    t = trie_setw(t, Key_c_l,       CMDENTRY(cmd_redraw_all));
    t = trie_setw(t, Key_delete,    CMDENTRY(cmd_delete_char));
    t = trie_setw(t, Key_backspace, CMDENTRY(cmd_backward_delete_char));
    t = trie_setw(t, Key_erase,     CMDENTRY(cmd_backward_delete_char));
    t = trie_setw(t, Key_c_h,       CMDENTRY(cmd_backward_delete_char));
    t = trie_setw(t, Key_c_w,       CMDENTRY(cmd_backward_delete_semiword));
    t = trie_setw(t, Key_kill,      CMDENTRY(cmd_backward_delete_line));
    t = trie_setw(t, Key_c_u,       CMDENTRY(cmd_backward_delete_line));
    //TODO
    yle_modes[YLE_MODE_VI_INSERT].keymap = t;

    yle_modes[YLE_MODE_VI_COMMAND].default_command = cmd_alert;
    t = trie_create();
    t = trie_setw(t, Key_c_lb,      CMDENTRY(cmd_noop));
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
    t = trie_setw(t, L"W",          CMDENTRY(cmd_forward_bigword));
    t = trie_setw(t, L"E",          CMDENTRY(cmd_end_of_bigword));
    t = trie_setw(t, L"B",          CMDENTRY(cmd_backward_bigword));
    t = trie_setw(t, L"w",          CMDENTRY(cmd_forward_word));
    t = trie_setw(t, L"e",          CMDENTRY(cmd_end_of_word));
    t = trie_setw(t, L"b",          CMDENTRY(cmd_backward_word));
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
    t = trie_setw(t, L"P",          CMDENTRY(cmd_put_before));
    t = trie_setw(t, L"p",          CMDENTRY(cmd_put));
    t = trie_setw(t, L"I",          CMDENTRY(cmd_vi_insert_beginning));
    t = trie_setw(t, L"a",          CMDENTRY(cmd_vi_append));
    t = trie_setw(t, L"A",          CMDENTRY(cmd_vi_append_end));
    t = trie_setw(t, L"R",          CMDENTRY(cmd_vi_replace));
    t = trie_setw(t, L"~",          CMDENTRY(cmd_vi_change_case));
    //TODO
    // =
    // \ 
    // *
    // @ char
    // .
    // v
    // |
    // f/F char
    // t/T char
    // ;
    // ,
    // c motion
    // C
    // S
    // r char
    // _
    // X
    // d motion
    // D
    // y motion
    // Y
    // u
    // U
    // k/-
    // j/+
    // g
    // G
    // /
    // ?
    // n
    // N
    yle_modes[YLE_MODE_VI_COMMAND].keymap = t;
}

/* Sets the editing mode to the one specified by `id'. */
void yle_set_mode(yle_mode_id_T id)
{
    assert(id < YLE_MODE_N);
    yle_current_mode = &yle_modes[id];
}

/* Resets the state of keymap before starting editing. */
void yle_keymap_reset(void)
{
    reset_count();
    overwrite = false;
}

/* Invokes the given command. */
void yle_keymap_invoke(yle_command_func_T *cmd, wchar_t arg)
{
    cmd(arg);

    if (yle_current_mode == &yle_modes[YLE_MODE_VI_COMMAND]) {
	if (yle_main_index > 0 && yle_main_index == yle_main_buffer.length) {
	    yle_main_index--;
	}
    }
    yle_display_reposition_cursor();
}

/* Resets `state.count'. */
void reset_count(void)
{
    state.count.sign = 0;
    state.count.abs = 0;
}

/* Returns the count value.
 * If the count is not set, returns the `default_value'. */
int get_count(int default_value)
{
    if (state.count.sign == 0)
	return default_value;
    if (state.count.sign < 0 && state.count.abs == 0)
	return -1;
    return state.count.sign * (int) state.count.abs;
}

/* Applies the current pending editing command to the range between the current
 * cursor index and the given `index'. If no editing command is pending, simply
 * moves the cursor to the `index'. */
/* This function is used for all cursor-moving commands, even when not in the
 * vi mode. */
void exec_motion_command(size_t index, bool inclusive)
{
    assert(index <= yle_main_buffer.length);
    //TODO no editing commands for now
    (void) inclusive;
    {
	yle_main_index = index;
    }
    reset_count();
}

/* Adds the specified string to the kill ring.
 * The maximum number of characters that are added is specified by `n'. */
void add_to_kill_ring(const wchar_t *s, size_t n)
{
    free(kill_ring[next_kill_index]);
    kill_ring[next_kill_index] = xwcsndup(s, n);
    next_kill_index = (next_kill_index + 1) % KILL_RING_SIZE;
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

/* Invoke `cmd_alert' and returns true if the cursor is at the first character.
 */
bool alert_if_first(void)
{
    if (yle_main_index > 0)
	return false;

    cmd_alert(L'\a');
    return true;
}

/* Invoke `cmd_alert' and returns true if the cursor is at the last character.
 */
bool alert_if_last(void)
{
    if (yle_current_mode == &yle_modes[YLE_MODE_VI_COMMAND]) {
	if (yle_main_buffer.length > 0
		&& yle_main_index < yle_main_buffer.length - 1)
	    return false;
    } else {
	if (yle_main_index < yle_main_buffer.length)
	    return false;
    }

    cmd_alert(L'\a');
    return true;
}

/* Inserts one character in the buffer. */
void cmd_self_insert(wchar_t c)
{
    if (c != L'\0') {
	int count = get_count(1);
	size_t old_index = yle_main_index;

	while (--count >= 0)
	    if (overwrite && yle_main_index < yle_main_buffer.length)
		yle_main_buffer.contents[yle_main_index++] = c;
	    else
		wb_ninsert_force(&yle_main_buffer, yle_main_index++, &c, 1);
	yle_display_reprint_buffer(old_index,
		!overwrite && yle_main_index == yle_main_buffer.length);
    } else {
	yle_alert();
    }
    reset_count();
}

/* Sets the `yle_next_verbatim' flag.
 * The next character will be input to the main buffer even if it's a special
 * character. */
void cmd_expect_verbatim(wchar_t c __attribute__((unused)))
{
    yle_next_verbatim = true;
}

/* Inserts the backslash character. */
void cmd_insert_backslash(wchar_t c __attribute__((unused)))
{
    cmd_self_insert(L'\\');
}

/* Adds the specified digit `c' to the accumulating argument. */
/* If `c' is not a digit, does nothing. */
void cmd_digit_argument(wchar_t c)
{
    if (L'0' <= c && c <= L'9') {
	if (state.count.abs > COUNT_ABS_MAX / 10) {
	    cmd_alert(c);  // argument too large
	    return;
	}
	if (state.count.sign == 0)
	    state.count.sign = 1;
	state.count.abs = state.count.abs * 10 + (unsigned) (c - L'0');
    } else if (c == L'-') {
	if (state.count.sign == 0)
	    state.count.sign = -1;
	else
	    state.count.sign = -state.count.sign;
    }
}

/* Moves forward one character (or `count' characters if the count is set). */
/* exclusive motion command */
void cmd_forward_char(wchar_t c __attribute__((unused)))
{
    int count = get_count(1);
    if (count >= 0)
	move_cursor_forward_char(count);
    else
	move_cursor_backward_char(-count);
}

/* Moves backward one character.
 * If the count is set, moves backward `count' characters. */
/* exclusive motion command */
void cmd_backward_char(wchar_t c __attribute__((unused)))
{
    int count = get_count(1);
    if (count >= 0)
	move_cursor_backward_char(count);
    else
	move_cursor_forward_char(-count);
}

/* Moves the cursor forward by `offset', relative to the current position.
 * The `offset' must not be negative. */
void move_cursor_forward_char(int offset)
{
    assert(offset >= 0);
    if (alert_if_last())
	return;

#if COUNT_ABS_MAX > SIZE_MAX
    if (offset > SIZE_MAX)
	offset = SIZE_MAX;
#endif

    size_t new_index;
    if (yle_main_buffer.length - yle_main_index < (size_t) offset)
	new_index = yle_main_buffer.length;
    else
	new_index = yle_main_index + offset;
    exec_motion_command(new_index, false);
}

/* Moves the cursor backward by `offset', relative to the current position.
 * The `offset' must not be negative. */
void move_cursor_backward_char(int offset)
{
    assert(offset >= 0);
    if (alert_if_first())
	return;

    size_t new_index;
#if COUNT_ABS_MAX > SIZE_MAX
    if ((int) yle_main_index <= offset)
#else
    if (yle_main_index <= (size_t) offset)
#endif
	new_index = 0;
    else
	new_index = yle_main_index - offset;
    exec_motion_command(new_index, false);
}

/* Moves forward one bigword (or `count' bigwords if the count is set). */
/* exclusive motion command */
void cmd_forward_bigword(wchar_t c __attribute__((unused)))
{
    int count = get_count(1);
    if (count >= 0)
	move_cursor_forward_bigword(count);
    else
	move_cursor_backward_bigword(-count);
}

/* Moves the cursor to the end of the current bigword (or the next bigword if
 * already at the end). If the count is set, moves to the end of `count'th
 * bigword. */
/* inclusive motion command */
void cmd_end_of_bigword(wchar_t c __attribute__((unused)))
{
    if (alert_if_last())
	return;

    int count = get_count(1);
    size_t new_index = yle_main_index;
    while (--count >= 0 && new_index < yle_main_buffer.length)
	new_index = next_end_of_bigword_index(yle_main_buffer.contents,new_index);
    exec_motion_command(new_index, true);
}

/* Moves backward one bigword (or `count' bigwords if the count is set). */
/* exclusive motion command */
void cmd_backward_bigword(wchar_t c __attribute__((unused)))
{
    int count = get_count(1);
    if (count >= 0)
	move_cursor_backward_bigword(count);
    else
	move_cursor_forward_bigword(-count);
}

/* Moves the cursor forward `count' bigwords, relative to the current position.
 * If `count' is negative, the cursor is not moved. */
void move_cursor_forward_bigword(int count)
{
    if (alert_if_last())
	return;

    size_t new_index = yle_main_index;
    while (--count >= 0 && new_index < yle_main_buffer.length)
	new_index = next_bigword_index(yle_main_buffer.contents, new_index);
    exec_motion_command(new_index, false);
}

/* Moves the cursor backward `count' bigwords, relative to the current position.
 * If `count' is negative, the cursor is not moved. */
void move_cursor_backward_bigword(int count)
{
    if (alert_if_first())
	return;

    size_t new_index = yle_main_index;
    while (--count >= 0 && new_index > 0)
	new_index = previous_bigword_index(yle_main_buffer.contents, new_index);
    exec_motion_command(new_index, false);
}

/* Returns the index of the next bigword in the string `s', counted from the index
 * `i'. The return value is greater than `i' unless `s[i]' is a null character. */
/* A bigword is a sequence of non-blank characters. */
size_t next_bigword_index(const wchar_t *s, size_t i)
{
    while (s[i] != L'\0' && !iswblank(s[i]))
	i++;
    while (s[i] != L'\0' && iswblank(s[i]))
	i++;
    return i;
}

/* Returns the index of the end of a bigword in the string `s', counted from index
 * `i'. If `i' is at the end of a bigword, the end of the next bigword is
 * returned. The return value is greater than `i' unless `s[i]' is a null
 * character. */
size_t next_end_of_bigword_index(const wchar_t *s, size_t i)
{
    const size_t init = i;
start:
    if (s[i] == L'\0')
	return i;
    while (s[i] != L'\0' && iswblank(s[i]))
	i++;
    while (s[i] != L'\0' && !iswblank(s[i]))
	i++;
    i--;
    if (i > init) {
	return i;
    } else {
	i++;
	goto start;
    }
}

/* Returns the index of the previous bigword in the string `s', counted from the
 * index `i'. The return value is less than `i' unless `i' is zero. */
size_t previous_bigword_index(const wchar_t *s, size_t i)
{
    const size_t init = i;
start:
    while (i > 0 && iswblank(s[i]))
	i--;
    while (i > 0 && !iswblank(s[i]))
	i--;
    if (i == 0)
	return i;
    i++;
    if (i < init) {
	return i;
    } else {
	i--;
	goto start;
    }
}

/* Moves forward one word (or `count' words if the count is set). */
/* exclusive motion command */
void cmd_forward_word(wchar_t c __attribute__((unused)))
{
    int count = get_count(1);
    if (count >= 0)
	move_cursor_forward_word(count);
    else
	move_cursor_backward_word(-count);
}

/* Moves the cursor to the end of the current word (or the next word if already at
 * the end). If the count is set, moves to the end of the `count'th word. */
/* inclusive motion command */
void cmd_end_of_word(wchar_t c __attribute__((unused)))
{
    if (alert_if_last())
	return;

    int count = get_count(1);
    size_t new_index = yle_main_index;
    while (--count >= 0 && new_index < yle_main_buffer.length)
	new_index = next_end_of_word_index(yle_main_buffer.contents, new_index);
    exec_motion_command(new_index, true);
}

/* Moves backward one word (or `count' words if the count is set). */
/* exclusive motion command */
void cmd_backward_word(wchar_t c __attribute__((unused)))
{
    int count = get_count(1);
    if (count >= 0)
	move_cursor_backward_word(count);
    else
	move_cursor_forward_word(-count);
}

/* Moves the cursor forward `count' words, relative to the current position.
 * If `count' is negative, the cursor is not moved. */
void move_cursor_forward_word(int count)
{
    if (alert_if_last())
	return;

    size_t new_index = yle_main_index;
    while (--count >= 0 && new_index < yle_main_buffer.length)
	new_index = next_word_index(yle_main_buffer.contents, new_index);
    exec_motion_command(new_index, false);
}

/* Moves the cursor backward `count' words, relative to the current position.
 * If `count' is negative, the cursor is not moved. */
void move_cursor_backward_word(int count)
{
    if (alert_if_first())
	return;

    size_t new_index = yle_main_index;
    while (--count >= 0 && new_index > 0)
	new_index = previous_word_index(yle_main_buffer.contents, new_index);
    exec_motion_command(new_index, false);
}

/* Returns the index of the next word in the string `s', counted from the index
 * `i'. The return value is greater than `i' unless `s[i]' is a null character. */
/* A word is a sequence of alphanumeric characters and underscores, or a sequence
 * of other non-blank characters. */
size_t next_word_index(const wchar_t *s, size_t i)
{
    if (s[i] == L'_' || iswalnum(s[i])) {
	do
	    i++;
	while (s[i] == L'_' || iswalnum(s[i]));
	if (!iswblank(s[i]))
	    return i;
    } else if (!iswblank(s[i])) {
	for (;;) {
	    if (s[i] == L'\0')
		return i;
	    i++;
	    if (s[i] == L'_' || iswalnum(s[i]))
		return i;
	    if (iswblank(s[i]))
		break;
	}
    }
    /* find the first non-blank character */
    while (iswblank(s[++i]));
    return i;
}

/* Returns the index of the end of a word in the string `s', counted from index
 * `i'. If `i' is at the end of a word, the end of the next word is returned.
 * The return value is greater than `i' unless `s[i]' is a null character. */
size_t next_end_of_word_index(const wchar_t *s, size_t i)
{
    const size_t init = i;
start:
    while (iswblank(s[i]))
	i++;
    if (s[i] == L'\0')
	return i;
    if (s[i] == L'_' || iswalnum(s[i])) {
	do
	    i++;
	while (s[i] == L'_' || iswalnum(s[i]));
    } else {
	do
	    i++;
	while (!iswblank(s[i]) && !(s[i] == L'_' || iswalnum(s[i])));
    }
    i--;
    if (i > init) {
	return i;
    } else {
	i++;
	goto start;
    }
}

/* Returns the index of the previous word in the string `s', counted form the
 * index `i'. The return value is less than `i' unless `i' is zero. */
size_t previous_word_index(const wchar_t *s, size_t i)
{
    const size_t init = i;
start:
    while (i > 0 && iswblank(s[i]))
	i--;
    if (s[i] == L'_' || iswalnum(s[i])) {
	do {
	    if (i == 0)
		return 0;
	    i--;
	} while (s[i] == L'_' || iswalnum(s[i]));
    } else {
	do {
	    if (i == 0)
		return 0;
	    i--;
	} while (!iswblank(s[i]) && !(s[i] == L'_' || iswalnum(s[i])));
    }
    i++;
    if (i < init) {
	return i;
    } else {
	i--;
	goto start;
    }
}

/* Moves the cursor to the beginning of line. */
/* exclusive motion command */
void cmd_beginning_of_line(wchar_t c __attribute__((unused)))
{
    exec_motion_command(0, false);
}

/* Moves the cursor to the end of line. */
/* inclusive motion command */
void cmd_end_of_line(wchar_t c __attribute__((unused)))
{
    exec_motion_command(yle_main_buffer.length, true);
}

/* If the count is not set, moves the cursor to the beginning of line.
 * Otherwise, adds the given digit to the count. */
void cmd_bol_or_digit(wchar_t c)
{
    if (state.count.sign == 0)
	cmd_beginning_of_line(c);
    else
	cmd_digit_argument(c);
}

/* Moves the cursor to the first non-blank character. */
/* exclusive motion command */
void cmd_first_nonblank(wchar_t c __attribute__((unused)))
{
    size_t i = 0;

    while (c = yle_main_buffer.contents[i], c != L'\0' && iswblank(c))
	i++;
    exec_motion_command(i, false);
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
 * Otherwise, causes alert. */
void cmd_eof_if_empty(wchar_t c)
{
    if (yle_main_buffer.length == 0) {
	yle_state = YLE_STATE_ERROR;
	reset_count();
    } else {
	cmd_alert(c);
    }
}

/* If the edit line is empty, sets `yle_state' to YLE_STATE_ERROR (return EOF).
 * Otherwise, deletes the character under the cursor. */
void cmd_eof_or_delete(wchar_t c)
{
    if (yle_main_buffer.length == 0) {
	yle_state = YLE_STATE_ERROR;
	reset_count();
    } else {
	cmd_delete_char(c);
    }
}

/* Inserts a hash sign ('#') at the beginning of the line and accepts the line. */
void cmd_accept_with_hash(wchar_t c)
{
    wb_insert(&yle_main_buffer, 0, L"#");
    yle_display_reprint_buffer(0, false);
    cmd_accept_line(c);
}

/* Changes the editing mode to "vi insert". */
void cmd_setmode_viinsert(wchar_t c __attribute__((unused)))
{
    yle_set_mode(YLE_MODE_VI_INSERT);
    reset_count();
    overwrite = false;
}

/* Changes the editing mode to "vi command". */
void cmd_setmode_vicommand(wchar_t c __attribute__((unused)))
{
    if (yle_current_mode == &yle_modes[YLE_MODE_VI_INSERT])
	if (yle_main_index > 0)
	    yle_main_index--;
    yle_set_mode(YLE_MODE_VI_COMMAND);
    reset_count();
    overwrite = false;
}

/* Redraw everything. */
void cmd_redraw_all(wchar_t c __attribute__((unused)))
{
    yle_display_clear();
    yle_display_print_all();
}


/********** Editing Commands **********/

/* Removes the character under the cursor.
 * If the count is set, `count' characters are killed. */
void cmd_delete_char(wchar_t c)
{
    if (state.count.sign == 0) {
	if (yle_main_index < yle_main_buffer.length) {
	    wb_remove(&yle_main_buffer, yle_main_index, 1);
	    yle_display_reprint_buffer(yle_main_index, false);
	} else {
	    yle_alert();
	}
	reset_count();
    } else {
	cmd_kill_char(c);
    }
}

/* Removes the character behind the cursor.
 * If the count is set, `count' characters are killed. */
void cmd_backward_delete_char(wchar_t c)
{
    if (state.count.sign == 0) {
	if (yle_main_index > 0) {
	    wb_remove(&yle_main_buffer, --yle_main_index, 1);
	    yle_display_reprint_buffer(yle_main_index, false);
	} else {
	    yle_alert();
	}
	reset_count();
    } else {
	cmd_backward_kill_char(c);
    }
}

/* Removes the semiword behind the cursor.
 * If the count is set, `count' semiwords are removed. */
/* A "semiword" is a sequence of characters that are not <blank> or <punct>. */
void cmd_backward_delete_semiword(wchar_t c __attribute__((unused)))
{
    size_t bound = yle_main_index;

    for (int count = get_count(1); --count >= 0; ) {
	do {
	    if (bound == 0)
		goto done;
	} while (is_blank_or_punct(yle_main_buffer.contents[--bound]));
	do {
	    if (bound == 0)
		goto done;
	} while (!is_blank_or_punct(yle_main_buffer.contents[--bound]));
    }
    bound++;
done:
    if (bound < yle_main_index) {
	wb_remove(&yle_main_buffer, bound, yle_main_index - bound);
	yle_main_index = bound;
	yle_display_reprint_buffer(yle_main_index, false);
    }
    reset_count();
}

bool is_blank_or_punct(wchar_t c)
{
    return iswblank(c) || iswpunct(c);
}

/* Removes all characters in the edit line. */
void cmd_delete_line(wchar_t c __attribute__((unused)))
{
    wb_clear(&yle_main_buffer);
    yle_main_index = 0;
    yle_display_reprint_buffer(0, false);
    reset_count();
}

/* Removes all characters after the cursor. */
void cmd_forward_delete_line(wchar_t c __attribute__((unused)))
{
    if (yle_main_index < yle_main_buffer.length) {
	wb_remove(&yle_main_buffer, yle_main_index, SIZE_MAX);
	yle_display_reprint_buffer(yle_main_index, false);
    }
    reset_count();
}

/* Removes all characters behind the cursor. */
void cmd_backward_delete_line(wchar_t c __attribute__((unused)))
{
    if (yle_main_index > 0) {
	wb_remove(&yle_main_buffer, 0, yle_main_index);
	yle_main_index = 0;
	yle_display_reprint_buffer(0, false);
    }
    reset_count();
}

/* Kills the character under the cursor.
 * If the count is set, `count' characters are killed. */
void cmd_kill_char(wchar_t c)
{
    assert(yle_main_index <= yle_main_buffer.length);
    if (yle_main_index == yle_main_buffer.length) {
	cmd_alert(c);
	return;
    }

    kill_chars(false);
}

/* Kills the character behind the cursor.
 * If the count is set, `count' characters are killed.
 * If the cursor is at the beginning of the line, the terminal is alerted. */
void cmd_backward_kill_char(wchar_t c)
{
    assert(yle_main_index <= yle_main_buffer.length);
    if (yle_main_index == 0) {
	cmd_alert(c);
	return;
    }

    kill_chars(true);
}

void kill_chars(bool backward)
{
    int n = get_count(1);
    size_t offset;
    if (backward)
	n = -n;
    if (n >= 0) {
#if COUNT_ABS_MAX > SIZE_MAX
	if (n > SIZE_MAX)
	    n = SIZE_MAX;
#endif
	offset = yle_main_index;
    } else {
	n = -n;
#if COUNT_ABS_MAX > SIZE_MAX
	if (n >= (int) yle_main_index)
#else
	if ((size_t) n >= yle_main_index)
#endif
	    n = yle_main_index;
	offset = yle_main_index - n;
    }
    add_to_kill_ring(yle_main_buffer.contents + offset, n);
    wb_remove(&yle_main_buffer, offset, n);
    yle_display_reprint_buffer(offset, false);
    reset_count();
}

/* Inserts the last-killed string before the cursor.
 * If the count is set, inserts `count' times. */
void cmd_put_before(wchar_t c)
{
    last_put_index = (next_kill_index - 1) % KILL_RING_SIZE;

    const wchar_t *s = kill_ring[last_put_index];
    if (s == NULL) {
	cmd_alert(c);
	return;
    } else if (s[0] == L'\0') {
	reset_count();
	return;
    }

    size_t offset = yle_main_buffer.length - yle_main_index;
    size_t old_index = yle_main_index;
    for (int count = get_count(1); --count >= 0; )
	wb_insert(&yle_main_buffer, yle_main_index, s);
    assert(yle_main_buffer.length >= offset + 1);
    yle_main_index = yle_main_buffer.length - offset - 1;
    yle_display_reprint_buffer(old_index, offset == 0);
    reset_count();
}

/* Inserts the last-killed string after the cursor.
 * If the count is set, inserts `count' times. */
void cmd_put(wchar_t c)
{
    if (yle_main_index < yle_main_buffer.length)
	yle_main_index++;
    cmd_put_before(c);
}


/********** Vi-Mode Specific Commands **********/

/* Moves the cursor to the beginning of line and sets the editing mode to
 * "vi insert". */
void cmd_vi_insert_beginning(wchar_t c)
{
    yle_main_index = 0;
    cmd_setmode_viinsert(c);
}

/* Moves the cursor by one character and sets the editing mode to "vi insert". */
void cmd_vi_append(wchar_t c)
{
    if (yle_main_index < yle_main_buffer.length)
	yle_main_index++;
    cmd_setmode_viinsert(c);
}

/* Moves the cursor to the end of line and sets the editing mode to "vi insert".*/
void cmd_vi_append_end(wchar_t c)
{
    yle_main_index = yle_main_buffer.length;
    cmd_setmode_viinsert(c);
}

/* Sets the editing mode to "vi insert", with the `overwrite' flag true. */
void cmd_vi_replace(wchar_t c)
{
    cmd_setmode_viinsert(c);
    overwrite = true;
}

/* Changes the case of the character under the cursor and advances the cursor.
 * If the count is set, `count' characters are changed. */
void cmd_vi_change_case(wchar_t c)
{
    size_t old_index = yle_main_index;

    if (yle_main_index == yle_main_buffer.length) {
	cmd_alert(c);
	return;
    }
    for (int count = get_count(1); --count >= 0; ) {
	wchar_t c = yle_main_buffer.contents[yle_main_index];
	yle_main_buffer.contents[yle_main_index]
	    = (iswlower(c) ? towupper : towlower)(c);
	yle_main_index++;
	if (yle_main_index == yle_main_buffer.length)
	    break;
    }
    yle_display_reprint_buffer(old_index, false);
    reset_count();
}


/* vim: set ts=8 sts=4 sw=4 noet: */
