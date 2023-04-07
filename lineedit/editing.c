/* Yash: yet another shell */
/* editing.c: main editing module */
/* (C) 2007-2023 magicant */

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
#include "editing.h"
#include <assert.h>
#include <errno.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include "../alias.h"
#include "../exec.h"
#include "../expand.h"
#include "../history.h"
#include "../job.h"
#include "../option.h"
#include "../path.h"
#include "../plist.h"
#include "../redir.h"
#include "../strbuf.h"
#include "../util.h"
#include "../xfnmatch.h"
#include "../yash.h"
#include "complete.h"
#include "display.h"
#include "keymap.h"
#include "lineedit.h"
#include "terminfo.h"
#include "trie.h"


/* The type of pairs of a command and an argument. */
struct le_command_T {
    le_command_func_T *func;
    wchar_t arg;
};


/* The main buffer where the command line is edited.
 * The contents of the buffer is divided into two parts: The first part is the
 * main command line text that is input and edited by the user. The second is
 * automatically appended after the first as a result of the prediction
 * feature. As the user edits the first part, the prediction feature updates
 * the second. When the user moves the cursor to somewhere in the second part,
 * the text up to the cursor then becomes the first. */
xwcsbuf_T le_main_buffer;
/* The position that divides the main buffer into two parts as described just
 * above. If `le_main_length > le_main_buffer.length', the second part is
 * assumed empty. */
size_t le_main_length;
/* The position of the cursor on the command line.
 * le_main_index <= le_main_buffer.length */
size_t le_main_index;

/* The history entry that is being edited in the main buffer now.
 * When we're editing no history entry, `main_history_entry' is `Histlist'. */
static const histlink_T *main_history_entry;
/* The original value of `main_history_entry', converted into a wide string. */
static wchar_t *main_history_value;

/* The direction of currently performed command history search. */
enum le_search_direction_T le_search_direction;
/* The type of currently performed command history search. */
enum le_search_type_T le_search_type;
/* Supplementary buffer used in command history search.
 * When search is not being performed, `le_search_buffer.contents' is NULL. */
xwcsbuf_T le_search_buffer;
/* The search result for the current value of `le_search_buffer'.
 * If there is no match, `le_search_result' is `Histlist'. */
const histlink_T *le_search_result;
/* The search string and the direction of the last search. */
static struct {
    enum le_search_direction_T direction;
    enum le_search_type_T type;
    wchar_t *value;
} last_search;

/* The last executed command and the currently executing command. */
static struct le_command_T last_command, current_command;

/* The type of motion expecting commands. */
enum motion_expect_command_T {
    MEC_UPPERCASE  = 1 << 0,  /* convert the text to upper case */
    MEC_LOWERCASE  = 1 << 1,  /* convert the text to lower case */
    MEC_SWITCHCASE = MEC_UPPERCASE | MEC_LOWERCASE,  /* switch case of text */
    MEC_CASEMASK   = MEC_SWITCHCASE,

    MEC_TOSTART    = 1 << 2,  /* move cursor to the beginning of the region */
    MEC_TOEND      = 1 << 3,  /* move cursor to the end of the region */
    MEC_MOVE       = MEC_TOSTART | MEC_TOEND,  /* move cursor to motion end */
    MEC_CURSORMASK = MEC_MOVE,
    /* If none of MEC_TOSTART, MEC_TOEND, and MEC_MOVE is specified, the cursor
     * is not moved unless MEC_DELETE is specified. */

    MEC_COPY       = 1 << 4,  /* copy the text to the kill ring */
    MEC_DELETE     = 1 << 5,  /* delete the text */
    MEC_INSERT     = 1 << 6,  /* go to insert mode */
    MEC_KILL       = MEC_COPY    | MEC_DELETE,
    MEC_CHANGE     = MEC_DELETE  | MEC_INSERT,
    MEC_COPYCHANGE = MEC_KILL    | MEC_INSERT,
};

/* The state in which a command is executed. */
struct state_T {
    struct {
	/* When count is not specified, `sign' and `abs' are 0.
	 * Otherwise, `sign' is 1 or -1.
	 * When the negative sign is specified but digits are not, `abs' is 0.*/
	int sign;
	unsigned abs;
	int multiplier;
    } count;
    enum motion_expect_command_T pending_command_motion;
    le_command_func_T *pending_command_char;
};
#define COUNT_ABS_MAX 999999999

/* The current state. */
static struct state_T state;

/* The last executed editing command and the then state.
 * Valid iff `.command.func' is non-null. */
static struct {
    struct le_command_T command;
    struct state_T state;
} last_edit_command;

/* The last executed find/till command. */
/* `last_find_command' is valid iff `.func' is non-null. */
static struct le_command_T last_find_command;

/* The editing mode before the mode is changed to LE_MODE_CHAR_EXPECT/SEARCH.
 * When the char-expecting/search command finishes, the mode is restored to
 * this mode. */
static le_mode_id_T savemode;

/* When starting the overwrite mode, the then `le_main_buffer' contents and
 * `le_main_length' are saved in this structure. The values are kept so that the
 * original contents can be restored when the user hits backspace. When the user
 * leaves the overwrite mode, `contents' is freed and set to NULL. */
static struct {
    wchar_t *contents;
    size_t length;
} overwrite_save_buffer;

/* History of the edit line between editing commands. */
static plist_T undo_history;
/* Index of the current state in the history.
 * If the current state is the newest, the index is `undo_history.length'. */
static size_t undo_index;
/* The history entry that is saved in the undo history. */
static const histlink_T *undo_history_entry;
/* The index that is to be the value of the `index' member of the next undo
 * history entry. */
static size_t undo_save_index;
/* Structure of history entries */
struct undo_history {
    size_t index;        /* index of the cursor */
    wchar_t contents[];  /* contents of the edit line */
    // `contents' is a copy of `le_main_buffer.contents' up to `le_main_length'.
};

#define KILL_RING_SIZE 32  /* must be power of 2 */
/* The kill ring */
static wchar_t *kill_ring[KILL_RING_SIZE];
/* The index of the element to which next killed string is assigned. */
static size_t next_kill_index = 0;
/* The index of the last put element. */
static size_t last_put_elem = 0;  /* < KILL_RING_SIZE */
/* The position and length of the last put string. */
static size_t last_put_range_start, last_put_range_length;

/* Set to true if the next completion command should restart completion from
 * scratch. */
static bool reset_completion;
/* The next value of `reset_completion'. */
static bool next_reset_completion;

/* Probability distribution tree for command prediction. */
static trie_T *prediction_tree = NULL;


static void reset_state(void);
static void reset_count(void);
static int get_count(int default_value)
    __attribute__((pure));
static size_t active_length(void)
    __attribute__((pure));
static void save_current_edit_command(void);
static void save_current_find_command(void);
static void save_undo_history(void);
static void maybe_save_undo_history(void);
static void exec_motion_command(size_t new_index, bool inclusive);
static void set_motion_expect_command(enum motion_expect_command_T cmd);
static void exec_motion_expect_command(
	enum motion_expect_command_T cmd, le_command_func_T motion);
static void exec_motion_expect_command_line(enum motion_expect_command_T cmd);
static void exec_motion_expect_command_all(void);
static void add_to_kill_ring(const wchar_t *s, size_t n)
    __attribute__((nonnull));
static void set_char_expect_command(le_command_func_T cmd)
    __attribute__((nonnull));
static void set_overwriting(bool overwriting);
static inline bool is_overwriting(void)
    __attribute__((pure));
static void restore_overwritten_buffer_contents(
	size_t start_index, size_t end_index);
static void set_search_mode(le_mode_id_T mode, enum le_search_direction_T dir);
static void to_upper_case(wchar_t *s, size_t n)
    __attribute__((nonnull));
static void to_lower_case(wchar_t *s, size_t n)
    __attribute__((nonnull));
static void switch_case(wchar_t *s, size_t n)
    __attribute__((nonnull));

static void set_mode(le_mode_id_T newmode, bool overwrite);
static void redraw_all(bool clear);

static bool alert_if_first(void);
static bool alert_if_last(void);
static void move_cursor_forward_char(int offset);
static void move_cursor_backward_char(int offset);
static void move_cursor_forward_bigword(int count);
static void move_cursor_backward_bigword(int count);
static size_t next_bigword_index(const wchar_t *s, size_t i)
    __attribute__((nonnull));
static size_t next_end_of_bigword_index(
	const wchar_t *s, size_t i, bool progress)
    __attribute__((nonnull));
static size_t previous_bigword_index(const wchar_t *s, size_t i)
    __attribute__((nonnull));
static void move_cursor_forward_semiword(int count);
static void move_cursor_backward_semiword(int count);
static size_t next_semiword_index(const wchar_t *s, size_t i)
    __attribute__((nonnull));
static size_t next_end_of_semiword_index(
	const wchar_t *s, size_t i, bool progress)
    __attribute__((nonnull));
static size_t previous_semiword_index(const wchar_t *s, size_t i)
    __attribute__((nonnull));
static void move_cursor_forward_viword(int count);
static inline bool need_cw_treatment(void)
    __attribute__((pure));
static void move_cursor_backward_viword(int count);
static size_t next_viword_index(const wchar_t *s, size_t i)
    __attribute__((nonnull));
static size_t next_end_of_viword_index(
	const wchar_t *s, size_t i, bool progress)
    __attribute__((nonnull));
static size_t previous_viword_index(const wchar_t *s, size_t i)
    __attribute__((nonnull));
static void move_cursor_forward_emacsword(int count);
static void move_cursor_backward_emacsword(int count);
static size_t next_emacsword_index(const wchar_t *s, size_t i)
    __attribute__((nonnull));
static size_t previous_emacsword_index(const wchar_t *s, size_t i)
    __attribute__((nonnull));
static void find_char(wchar_t c);
static void find_char_rev(wchar_t c);
static void till_char(wchar_t c);
static void till_char_rev(wchar_t c);
static void exec_find(wchar_t c, int count, bool till);
static size_t find_nth_occurence(wchar_t c, int n);

static void put_killed_string(bool after_cursor, bool cursor_on_last_char);
static void insert_killed_string(
	bool after_cursor, bool cursor_on_last_char, size_t index);
static void cancel_undo(int offset);

static void check_reset_completion(void);

static void create_prediction_tree(void);
static size_t count_matching_previous_commands(const histentry_T *e1)
    __attribute__((nonnull,pure));
static void clear_prediction(void);
static void update_buffer_with_prediction(void);

static void vi_replace_char(wchar_t c);
static void vi_exec_alias(wchar_t c);
struct xwcsrange { const wchar_t *start, *end; };
static struct xwcsrange get_next_bigword(const wchar_t *s)
    __attribute__((nonnull));
static struct xwcsrange get_prev_bigword(
	const wchar_t *beginning, const wchar_t *s)
    __attribute__((nonnull));

static void replace_horizontal_space(bool deleteafter, const wchar_t *s)
    __attribute__((nonnull));

static void go_to_history_absolute(
	const histlink_T *l, enum le_search_type_T curpos)
    __attribute__((nonnull));
static void go_to_history_relative(int offset, enum le_search_type_T curpos);
static void go_to_history(const histlink_T *l, enum le_search_type_T curpos)
    __attribute__((nonnull));

static bool need_update_last_search_value(void)
    __attribute__((pure));
static void update_search(void);
static void perform_search(const wchar_t *pattern,
	enum le_search_direction_T dir, enum le_search_type_T type)
    __attribute__((nonnull));
static void search_again(enum le_search_direction_T dir);
static void beginning_search(enum le_search_direction_T dir);
static inline bool beginning_search_check_go_to_history(const wchar_t *prefix)
    __attribute__((nonnull,pure));

#define ALERT_AND_RETURN_IF_PENDING                     \
    do if (state.pending_command_motion != MEC_MOVE)    \
	{ cmd_alert(L'\0'); return; }                   \
    while (0)


/* Initializes the editing module before starting editing. */
void le_editing_init(void)
{
    wb_init(&le_main_buffer);
    le_main_length = le_main_index = 0;
    main_history_entry = Histlist;
    main_history_value = xwcsdup(L"");

    switch (shopt_lineedit) {
	case SHOPT_VI:     le_set_mode(LE_MODE_VI_INSERT);  break;
	case SHOPT_EMACS:  le_set_mode(LE_MODE_EMACS);      break;
	default:           assert(false);
    }

    last_command.func = 0;
    last_command.arg = L'\0';

    start_using_history();
    pl_init(&undo_history);
    undo_index = 0;
    undo_save_index = le_main_index;
    undo_history_entry = Histlist;
    save_undo_history();

    reset_completion = true;

    reset_state();
    set_overwriting(false);

    if (shopt_le_predict) {
	create_prediction_tree();
	update_buffer_with_prediction();
    }
}

/* Finalizes the editing module when editing is finished.
 * Returns the content of the main buffer, which must be freed by the caller. */
wchar_t *le_editing_finalize(void)
{
    assert(le_search_buffer.contents == NULL);

    plfree(pl_toary(&undo_history), free);

    le_complete_cleanup();

    end_using_history();
    free(main_history_value);

    clear_prediction();
    trie_destroy(prediction_tree), prediction_tree = NULL;
    wb_wccat(&le_main_buffer, L'\n');
    return wb_towcs(&le_main_buffer);
}

/* Invokes the specified command. */
void le_invoke_command(le_command_func_T *cmd, wchar_t arg)
{
    current_command.func = cmd;
    current_command.arg = arg;

    next_reset_completion = true;

    cmd(arg);

    last_command = current_command;

    reset_completion |= next_reset_completion;

    if (le_main_length < le_main_index)
	le_main_length = le_main_index;
    switch (le_editstate) {
	case LE_EDITSTATE_EDITING:
	    if (shopt_le_predict)
		update_buffer_with_prediction();
	    break;
	case LE_EDITSTATE_DONE:
	case LE_EDITSTATE_ERROR:
	    clear_prediction();
	    break;
	case LE_EDITSTATE_INTERRUPTED:
	    break;
    }

    if (LE_CURRENT_MODE == LE_MODE_VI_COMMAND)
	if (le_main_index > 0 && le_main_index == le_main_buffer.length)
	    le_main_index--;
}

/* Resets `state'. */
void reset_state(void)
{
    reset_count();
    state.pending_command_motion = MEC_MOVE;
    state.pending_command_char = 0;
}

/* Resets `state.count'. */
void reset_count(void)
{
    state.count.sign = 0;
    state.count.abs = 0;
    state.count.multiplier = 1;
}

/* Returns the count value.
 * If the count is not set, returns the `default_value'. */
int get_count(int default_value)
{
    long long result;

    if (state.count.sign == 0)
	result = (long long) default_value * state.count.multiplier;
    else if (state.count.sign < 0 && state.count.abs == 0)
	result = (long long) -state.count.multiplier;
    else
	result = (long long) state.count.abs * state.count.sign *
	    state.count.multiplier;

    if (result < -COUNT_ABS_MAX)
	result = -COUNT_ABS_MAX;
    else if (result > COUNT_ABS_MAX)
	result = COUNT_ABS_MAX;
    return result;
}

/* Returns the length of the first part of `le_main_buffer'. */
size_t active_length(void)
{
    if (le_main_length > le_main_buffer.length)
	return le_main_buffer.length;
    if (le_main_length < le_main_index)
	return le_main_index;
    return le_main_length;
}

/* Saves the currently executing command and the current state in
 * `last_edit_command' if we are not redoing and the mode is not "vi insert". */
void save_current_edit_command(void)
{
    if (current_command.func != cmd_redo
	    && LE_CURRENT_MODE != LE_MODE_VI_INSERT) {
	last_edit_command.command = current_command;
	last_edit_command.state = state;
    }
}

/* Saves the currently executing command and the current state in
 * `last_find_command' if we are not redoing/refinding. */
void save_current_find_command(void)
{
    if (current_command.func != cmd_refind_char
	    && current_command.func != cmd_refind_char_rev
	    && current_command.func != cmd_redo)
	last_find_command = current_command;
}

/* Saves the current contents of the edit line to the undo history.
 * History entries at the current `undo_index' and newer are removed before
 * saving the current. If `undo_history_entry' is different from
 * `main_history_entry', all undo history entries are removed. */
void save_undo_history(void)
{
    for (size_t i = undo_index; i < undo_history.length; i++)
	free(undo_history.contents[i]);
    pl_truncate(&undo_history, undo_index);

    // No need to check for overflow in `len + 1' here. Should overflow occur,
    // the buffer would not have been allocated successfully.
    size_t len = active_length();
    struct undo_history *e = xmallocs(sizeof *e, len + 1, sizeof *e->contents);
    e->index = le_main_index;
    wcsncpy(e->contents, le_main_buffer.contents, len);
    e->contents[len] = L'\0';
    pl_add(&undo_history, e);
    assert(undo_index == undo_history.length - 1);
    undo_history_entry = main_history_entry;
}

/* Calls `save_undo_history' if the current contents of the edit line is not
 * saved. */
void maybe_save_undo_history(void)
{
    assert(undo_index <= undo_history.length);

    size_t save_undo_save_index = undo_save_index;
    undo_save_index = le_main_index;

    size_t len = active_length();
    if (undo_history_entry == main_history_entry) {
	if (undo_index < undo_history.length) {
	    struct undo_history *h = undo_history.contents[undo_index];
	    if (wcsncmp(le_main_buffer.contents, h->contents, len) == 0 &&
		    h->contents[len] == L'\0') {
		/* The contents of the main buffer is the same as saved in the
		 * history. Just save the index. */
		h->index = le_main_index;
		return;
	    }
	    undo_index++;
	}
    } else {
	if (wcsncmp(le_main_buffer.contents, main_history_value, len) == 0 &&
		main_history_value[len] == L'\0')
	    return;

	/* The contents of the buffer has been changed from the value of the
	 * history entry, but it's not yet saved in the undo history. We first
	 * save the original history value and then save the current buffer
	 * contents. */
	struct undo_history *h;
	pl_clear(&undo_history, free);
	h = xmallocs(sizeof *h,
		add(wcslen(main_history_value), 1), sizeof *h->contents);
	assert(save_undo_save_index <= wcslen(main_history_value));
	h->index = save_undo_save_index;
	wcscpy(h->contents, main_history_value);
	pl_add(&undo_history, h);
	undo_index = 1;
    }
    save_undo_history();
}

/* Applies the currently pending editing command to the range between the
 * current cursor index and the specified index. If no editing command is
 * pending, simply moves the cursor to the specified index. */
/* This function is used for all cursor-moving commands, even when not in the
 * vi mode. */
void exec_motion_command(size_t new_index, bool inclusive)
{
    assert(le_main_index <= le_main_buffer.length);
    assert(new_index <= le_main_buffer.length);

    size_t old_index = le_main_index;
    size_t start_index, end_index;
    if (old_index <= new_index)
	start_index = old_index, end_index = new_index;
    else
	start_index = new_index, end_index = old_index;
    if (inclusive && end_index < le_main_buffer.length)
	end_index++;

    enum motion_expect_command_T mec = state.pending_command_motion;

    /* don't save undo history when repeating backspace */
    bool repeated_backspace = (mec & MEC_DELETE)
	&& (new_index + 1 == old_index)
	&& current_command.func == cmd_backward_delete_char
	&& last_command.func == cmd_backward_delete_char;
    if (!repeated_backspace)
	maybe_save_undo_history();

    if (mec & MEC_COPY) {
	add_to_kill_ring(&le_main_buffer.contents[start_index],
		end_index - start_index);
    }
    if (mec & MEC_CASEMASK) {
	void (*case_func)(wchar_t *, size_t);
	INIT(case_func, 0);
	switch (mec & MEC_CASEMASK) {
	    case MEC_UPPERCASE:
		case_func = to_upper_case;
		break;
	    case MEC_LOWERCASE:
		case_func = to_lower_case;
		break;
	    case MEC_SWITCHCASE:
		case_func = switch_case;
		break;
	}
	case_func(&le_main_buffer.contents[start_index],
		end_index - start_index);
	if (le_main_length < end_index)
	    le_main_length = end_index;
    }
    switch (mec & MEC_CURSORMASK) {
	case MEC_TOSTART:  le_main_index = start_index;  break;
	case MEC_TOEND:    le_main_index = end_index;    break;
	case MEC_MOVE:     le_main_index = new_index;    break;
    }
    if (mec & MEC_DELETE) {
	save_current_edit_command();
	clear_prediction();
	if (!is_overwriting() || old_index <= new_index)
	    wb_remove(&le_main_buffer, start_index, end_index - start_index);
	else
	    restore_overwritten_buffer_contents(start_index, end_index);
	le_main_index = start_index;
    }
    if (mec & MEC_INSERT) {
	le_set_mode(LE_MODE_VI_INSERT);
	set_overwriting(false);
    }
    reset_state();
}

/* Sets the specified motion expecting command as pending.
 * If the command is already pending, the command is executed on the whole
 * line. */
void set_motion_expect_command(enum motion_expect_command_T cmd)
{
    if (state.pending_command_motion == MEC_MOVE) {
	state.count.multiplier = get_count(1);
	state.count.sign = 0;
	state.count.abs = 0;
	state.pending_command_motion = cmd;
    } else {
	if (state.pending_command_motion == cmd)
	    exec_motion_expect_command_all();
	else
	    cmd_alert(L'\0');
    }
}

/* Executes the specified motion expecting command with the specified motion
 * command. */
void exec_motion_expect_command(
	enum motion_expect_command_T cmd, le_command_func_T motion)
{
    if (current_command.func != cmd_redo)
	ALERT_AND_RETURN_IF_PENDING;
    state.pending_command_motion = cmd;
    motion(L'\0');
}

/* Executes the specified motion expecting command from the beginning to the end
 * of the line. */
void exec_motion_expect_command_line(enum motion_expect_command_T cmd)
{
    if (current_command.func != cmd_redo)
	ALERT_AND_RETURN_IF_PENDING;
    state.pending_command_motion = cmd;
    exec_motion_expect_command_all();
}

/* Executes the currently pending motion expecting command from the beginning to
 * the end of the line. */
void exec_motion_expect_command_all(void)
{
    size_t save_index = le_main_index;
    enum motion_expect_command_T save_pending = state.pending_command_motion;

    le_main_index = 0;
    cmd_end_of_line(L'\0');
    if (!(save_pending & (MEC_DELETE | MEC_CURSORMASK)))
	le_main_index = save_index;
}

/* Adds the specified string to the kill ring.
 * The maximum number of characters that are added is specified by `n'. */
void add_to_kill_ring(const wchar_t *s, size_t n)
{
    if (n > 0 && s[0] != L'\0') {
	free(kill_ring[next_kill_index]);
	kill_ring[next_kill_index] = xwcsndup(s, n);
	next_kill_index = (next_kill_index + 1) % KILL_RING_SIZE;
    }
}

/* Sets the editing mode to "char expect" and the pending command to `cmd'.
 * The current editing mode is saved in `savemode'. */
void set_char_expect_command(le_command_func_T cmd)
{
    savemode = LE_CURRENT_MODE;
    le_set_mode(LE_MODE_CHAR_EXPECT);
    state.pending_command_char = cmd;
}

/* Enables or disables the overwrite mode. */
void set_overwriting(bool overwrite)
{
    free(overwrite_save_buffer.contents);
    if (overwrite) {
	size_t len = active_length();
	overwrite_save_buffer.contents = xwcsndup(le_main_buffer.contents, len);
	overwrite_save_buffer.length = len;
    } else {
	overwrite_save_buffer.contents = NULL;
    }
}

/* Returns true iff the overwrite mode is active. */
bool is_overwriting(void)
{
    return overwrite_save_buffer.contents != NULL;
}

/* Restores the main buffer contents that were overwritten in the current
 * overwrite mode. When called, `le_main_length >= le_main_buffer.length' must
 * hold. The caller must adjust `le_main_index' because this function may remove
 * some characters from `le_main_buffer'. */
void restore_overwritten_buffer_contents(size_t start_index, size_t end_index)
{
    size_t mid_index;
    if (overwrite_save_buffer.length < start_index)
	mid_index = start_index;
    else if (overwrite_save_buffer.length > end_index)
	mid_index = end_index;
    else
	mid_index = overwrite_save_buffer.length;

    /* Restore contents from `start_index' to `mid_index' */
    wmemcpy(&le_main_buffer.contents[start_index],
	    &overwrite_save_buffer.contents[start_index],
	    mid_index - start_index);

    /* Contents from `mid_index' to `end_index' were actually not overwritten
     * but appended, so they should be removed. */
    wb_remove(&le_main_buffer, mid_index, end_index - mid_index);
}

/* Starts command history search by setting the editing mode to `mode' with
 * the specified direction `dir'. `mode' must be either LE_MODE_VI_SEARCH or
 * LE_MODE_EMACS_SEARCH.
 * The current editing mode is saved in `savemode'. */
void set_search_mode(le_mode_id_T mode, enum le_search_direction_T dir)
{
    le_complete_cleanup();

    savemode = LE_CURRENT_MODE;
    le_set_mode(mode);
    le_search_direction = dir;
    switch (mode) {
	case LE_MODE_VI_SEARCH:     le_search_type = SEARCH_VI;     break;
	case LE_MODE_EMACS_SEARCH:  le_search_type = SEARCH_EMACS;  break;
	default:                    assert(false);
    }
    wb_init(&le_search_buffer);
    update_search();
}

/* Converts the first `n' characters of string `s' to upper case.
 * The string must be at least `n' characters long. */
void to_upper_case(wchar_t *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
	s[i] = towupper(s[i]);
}

/* Converts the first `n' characters of string `s' to lower case.
 * The string must be at least `n' characters long. */
void to_lower_case(wchar_t *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
	s[i] = towlower(s[i]);
}

/* Switches case of the first `n' characters of string `s'.
 * The string must be at least `n' characters long. */
void switch_case(wchar_t *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
	wchar_t c = s[i];
	s[i] = iswlower(c) ? towupper(c) : towlower(c);
    }
}


/********** Basic Commands **********/

/* Does nothing. */
void cmd_noop(wchar_t c __attribute__((unused)))
{
    next_reset_completion = false;
    reset_state();
}

/* Alerts. */
void cmd_alert(wchar_t c __attribute__((unused)))
{
    lebuf_print_alert(true);
    reset_state();
}

/* Inserts the character argument into the buffer.
 * If the count is set, inserts `count' times.
 * If `is_overwriting()' is true, overwrites the character instead of inserting.
 */
void cmd_self_insert(wchar_t c)
{
    ALERT_AND_RETURN_IF_PENDING;
    if (c == L'\0') {
	cmd_alert(L'\0');
	return;
    }
    clear_prediction();

    int count = get_count(1);
    while (--count >= 0)
	if (is_overwriting() && le_main_index < le_main_buffer.length)
	    le_main_buffer.contents[le_main_index++] = c;
	else
	    wb_ninsert_force(&le_main_buffer, le_main_index++, &c, 1);
    reset_state();
}

/* Inserts the tab character. */
void cmd_insert_tab(wchar_t c __attribute__((unused)))
{
    cmd_self_insert(L'\t');
}

/* Sets the `le_next_verbatim' flag.
 * The next character will be input to the main buffer even if it's a special
 * character. */
void cmd_expect_verbatim(wchar_t c __attribute__((unused)))
{
    le_next_verbatim = true;
}

/* Adds the specified digit to the accumulating argument. */
/* If `c' is not a digit or a hyphen, does nothing. */
void cmd_digit_argument(wchar_t c)
{
    if (L'0' <= c && c <= L'9') {
	if (state.count.abs > COUNT_ABS_MAX / 10) {
	    cmd_alert(L'\0');  // argument too large
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

    next_reset_completion = false;
}

/* If the count is not set, moves the cursor to the beginning of the line.
 * Otherwise, adds the given digit to the count. */
void cmd_bol_or_digit(wchar_t c)
{
    if (state.count.sign == 0)
	cmd_beginning_of_line(c);
    else
	cmd_digit_argument(c);
}

/* Accepts the current line.
 * `le_editstate' is set to LE_EDITSTATE_DONE to induce line-editing to
 * terminate.
 * If history search is currently active, the search result is accepted. If the
 * search was failing, the line is not accepted. */
void cmd_accept_line(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;

    if (le_search_buffer.contents == NULL) {
	le_editstate = LE_EDITSTATE_DONE;
	reset_state();
    } else {
	if (le_search_result != Histlist)
	    le_editstate = LE_EDITSTATE_DONE;
	cmd_srch_accept_search(L'\0');
    }
}

/* Aborts the current line.
 * `le_editstate' is set to LE_EDITSTATE_INTERRUPTED to induce line-editing to
 * terminate. */
void cmd_abort_line(wchar_t c __attribute__((unused)))
{
    cmd_srch_abort_search(L'\0');
    le_editstate = LE_EDITSTATE_INTERRUPTED;
    reset_state();
}

/* Sets `le_editstate' to LE_EDITSTATE_ERROR.
 * The `le_readline' function will return INPUT_EOF. */
void cmd_eof(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;

    cmd_srch_abort_search(L'\0');
    le_editstate = LE_EDITSTATE_ERROR;
    reset_state();
}

/* If the edit line is empty, sets `le_editstate' to LE_EDITSTATE_ERROR (return
 * EOF). Otherwise, alerts. */
void cmd_eof_if_empty(wchar_t c __attribute__((unused)))
{
    if (active_length() == 0)
	cmd_eof(L'\0');
    else
	cmd_alert(L'\0');
}

/* If the edit line is empty, sets `le_editstate' to LE_EDITSTATE_ERROR (return
 * EOF). Otherwise, deletes the character under the cursor. */
void cmd_eof_or_delete(wchar_t c __attribute__((unused)))
{
    if (active_length() == 0)
	cmd_eof(L'\0');
    else
	cmd_delete_char(L'\0');
}

/* Inserts a hash sign ('#') at the beginning of the line and accepts the line.
 * If any count is set and the line already begins with a hash sign, the hash
 * sign is removed rather than added. The line is accepted anyway. */
void cmd_accept_with_hash(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    clear_prediction();

    if (state.count.sign == 0 || le_main_buffer.contents[0] != L'#')
	wb_insert(&le_main_buffer, 0, L"#");
    else
	wb_remove(&le_main_buffer, 0, 1);
    le_main_index = 0;
    cmd_accept_line(L'\0');
}

/* Accept the current line including the prediction. */
void cmd_accept_prediction(wchar_t c)
{
    cmd_accept_line(c);
    if (le_editstate == LE_EDITSTATE_DONE)
	le_main_length = SIZE_MAX;
}

/* Changes the editing mode to "vi insert". */
void cmd_setmode_viinsert(wchar_t c __attribute__((unused)))
{
    set_mode(LE_MODE_VI_INSERT, false);
}

/* Changes the editing mode to "vi command". */
void cmd_setmode_vicommand(wchar_t c __attribute__((unused)))
{
    set_mode(LE_MODE_VI_COMMAND, false);
}

/* Changes the editing mode to "emacs". */
void cmd_setmode_emacs(wchar_t c __attribute__((unused)))
{
    set_mode(LE_MODE_EMACS, false);
}

/* Changes the editing mode to the specified one. */
void set_mode(le_mode_id_T newmode, bool overwrite)
{
    ALERT_AND_RETURN_IF_PENDING;
    maybe_save_undo_history();

    if (LE_CURRENT_MODE == LE_MODE_VI_INSERT && newmode == LE_MODE_VI_COMMAND)
	if (le_main_index > 0)
	    le_main_index--;

    le_set_mode(newmode);
    set_overwriting(overwrite);
    reset_state();
}

/* Executes the currently pending char-expecting command. */
void cmd_expect_char(wchar_t c)
{
    if (!state.pending_command_char) {
	cmd_alert(L'\0');
	return;
    }

    current_command.func = state.pending_command_char;
    current_command.arg = c;
    state.pending_command_char(c);
}

/* Cancels the currently pending char-expecting command. */
void cmd_abort_expect_char(wchar_t c __attribute__((unused)))
{
    if (!state.pending_command_char) {
	cmd_alert(L'\0');
	return;
    }

    le_set_mode(savemode);
    reset_state();
}

/* Redraws everything.
 * If the count is set, clears the screen before redrawing. */
void cmd_redraw_all(wchar_t c __attribute__((unused)))
{
    redraw_all(state.count.sign != 0);
    reset_state();
}

/* Clears the screen and redraws everything at the top of the screen.
 * If the count is set, skips clearing the screen and only redraws. */
void cmd_clear_and_redraw_all(wchar_t c __attribute__((unused)))
{
    redraw_all(state.count.sign == 0);
    reset_state();
}

void redraw_all(bool clear)
{
    next_reset_completion = false;

    le_display_clear(clear);
    le_restore_terminal();
    le_setupterm(false);
    le_set_terminal();
}


/********** Motion Commands **********/

/* Invokes `cmd_alert' and returns true if the cursor is on the first character.
 */
bool alert_if_first(void)
{
    if (le_main_index > 0)
	return false;

    cmd_alert(L'\0');
    return true;
}

/* Invokes `cmd_alert' and returns true if the cursor is on the last character.
 */
bool alert_if_last(void)
{
    if (LE_CURRENT_MODE == LE_MODE_VI_COMMAND) {
	if (state.pending_command_motion != MEC_MOVE)
	    return false;
	if (le_main_buffer.length > 0
		&& le_main_index < le_main_buffer.length - 1)
	    return false;
    } else {
	if (le_main_index < le_main_buffer.length)
	    return false;
    }

    cmd_alert(L'\0');
    return true;
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

/* Moves backward one character (or `count' characters if the count is set). */
/* exclusive motion command */
void cmd_backward_char(wchar_t c __attribute__((unused)))
{
    int count = get_count(1);
    if (count >= 0)
	move_cursor_backward_char(count);
    else
	move_cursor_forward_char(-count);
}

/* Moves the cursor forward by `offset'. The `offset' must not be negative. */
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
    if (le_main_buffer.length - le_main_index < (size_t) offset)
	new_index = le_main_buffer.length;
    else
	new_index = le_main_index + offset;
    exec_motion_command(new_index, false);
}

/* Moves the cursor backward by `offset'. The `offset' must not be negative. */
void move_cursor_backward_char(int offset)
{
    assert(offset >= 0);
    if (alert_if_first())
	return;

    size_t new_index;
#if COUNT_ABS_MAX > SIZE_MAX
    if ((int) le_main_index <= offset)
#else
    if (le_main_index <= (size_t) offset)
#endif
	new_index = 0;
    else
	new_index = le_main_index - offset;
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
    size_t new_index = le_main_index;
    while (--count >= 0 && new_index < le_main_buffer.length)
	new_index = next_end_of_bigword_index(
		le_main_buffer.contents, new_index, true);
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

/* Moves the cursor forward `count' bigwords.
 * If `count' is negative, the cursor is not moved. */
void move_cursor_forward_bigword(int count)
{
    if (alert_if_last())
	return;

    size_t new_index = le_main_index;
    if (!need_cw_treatment()) {
	while (count-- > 0 && new_index < le_main_buffer.length)
	    new_index = next_bigword_index(le_main_buffer.contents, new_index);
	exec_motion_command(new_index, false);
    } else {
	while (count > 1 && new_index < le_main_buffer.length) {
	    new_index = next_bigword_index(le_main_buffer.contents, new_index);
	    count--;
	}
	if (count > 0 && new_index < le_main_buffer.length) {
	    new_index = next_end_of_bigword_index(
		    le_main_buffer.contents, new_index, false);
	}
	exec_motion_command(new_index, true);
    }
}

/* Moves the cursor backward `count' bigwords.
 * If `count' is negative, the cursor is not moved. */
void move_cursor_backward_bigword(int count)
{
    if (alert_if_first())
	return;

    size_t new_index = le_main_index;
    while (count-- > 0 && new_index > 0)
	new_index = previous_bigword_index(le_main_buffer.contents, new_index);
    exec_motion_command(new_index, false);
}

/* Returns the index of the next bigword in string `s', counted from index `i'.
 * The return value is greater than `i' unless `s[i]' is a null character. */
/* A bigword is a sequence of non-blank characters. */
size_t next_bigword_index(const wchar_t *s, size_t i)
{
    while (s[i] != L'\0' && !iswblank(s[i]))
	i++;
    while (s[i] != L'\0' && iswblank(s[i]))
	i++;
    return i;
}

/* Returns the index of the end of the current bigword in string `s', counted
 * from index `i'. If `i' is at the end of the bigword and `progress' is true,
 * the end of the next bigword is returned.
 * The return value is greater than `i' unless `s[i]' is a null character. */
size_t next_end_of_bigword_index(const wchar_t *s, size_t i, bool progress)
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
    if (i > init || !progress) {
	return i;
    } else {
	i++;
	goto start;
    }
}

/* Returns the index of the previous bigword in string `s', counted from index
 * `i'. The return value is less than `i' unless `i' is zero. */
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

/* Moves forward one semiword (or `count' semiwords if the count is set). */
/* exclusive motion command */
void cmd_forward_semiword(wchar_t c __attribute__((unused)))
{
    int count = get_count(1);
    if (count >= 0)
	move_cursor_forward_semiword(count);
    else
	move_cursor_backward_semiword(-count);
}

/* Moves the cursor to the end of the current semiword (or the next semiword if
 * already at the end). If the count is set, moves to the end of the `count'th
 * semiword. */
/* inclusive motion command */
void cmd_end_of_semiword(wchar_t c __attribute__((unused)))
{
    if (alert_if_last())
	return;

    int count = get_count(1);
    size_t new_index = le_main_index;
    while (--count >= 0 && new_index < le_main_buffer.length)
	new_index = next_end_of_semiword_index(
		le_main_buffer.contents, new_index, true);
    exec_motion_command(new_index, true);
}

/* Moves backward one semiword (or `count' semiwords if the count is set). */
/* exclusive motion command */
void cmd_backward_semiword(wchar_t c __attribute__((unused)))
{
    int count = get_count(1);
    if (count >= 0)
	move_cursor_backward_semiword(count);
    else
	move_cursor_forward_semiword(-count);
}

/* Moves the cursor forward `count' semiwords.
 * If `count' is negative, the cursor is not moved. */
void move_cursor_forward_semiword(int count)
{
    if (alert_if_last())
	return;

    size_t new_index = le_main_index;
    if (!need_cw_treatment()) {
	while (count-- > 0 && new_index < le_main_buffer.length)
	    new_index = next_semiword_index(le_main_buffer.contents, new_index);
	exec_motion_command(new_index, false);
    } else {
	while (count > 1 && new_index < le_main_buffer.length) {
	    new_index = next_semiword_index(le_main_buffer.contents, new_index);
	    count--;
	}
	if (count > 0 && new_index < le_main_buffer.length) {
	    new_index = next_end_of_semiword_index(
		    le_main_buffer.contents, new_index, false);
	}
	exec_motion_command(new_index, true);
    }
}

/* Moves the cursor backward `count' semiwords.
 * If `count' is negative, the cursor is not moved. */
void move_cursor_backward_semiword(int count)
{
    if (alert_if_first())
	return;

    size_t new_index = le_main_index;
    while (count-- > 0 && new_index > 0)
	new_index = previous_semiword_index(le_main_buffer.contents, new_index);
    exec_motion_command(new_index, false);
}

/* Returns the index of the next semiword in string `s', counted from index `i'.
 * The return value is greater than `i' unless `s[i]' is a null character. */
/* A "semiword" is a sequence of characters that are not <blank> or <punct>. */
size_t next_semiword_index(const wchar_t *s, size_t i)
{
    while (s[i] != L'\0' && !iswblank(s[i]) && !iswpunct(s[i]))
	i++;
    while (s[i] != L'\0' && (iswblank(s[i]) || iswpunct(s[i])))
	i++;
    return i;
}

/* Returns the index of the end of the current semiword in string `s', counted
 * from index `i'. If `i' is at the end of the semiword and `progress' is true,
 * the end of the next semiword is returned.
 * The return value is greater than `i' unless `s[i]' is a null character. */
size_t next_end_of_semiword_index(const wchar_t *s, size_t i, bool progress)
{
    const size_t init = i;
start:
    if (s[i] == L'\0')
	return i;
    while (s[i] != L'\0' && (iswblank(s[i]) || iswpunct(s[i])))
	i++;
    while (s[i] != L'\0' && !iswblank(s[i]) && !iswpunct(s[i]))
	i++;
    i--;
    if (i > init || !progress) {
	return i;
    } else {
	i++;
	goto start;
    }
}

/* Returns the index of the previous semiword in string `s', counted from index
 * `i'. The return value is less than `i' unless `i' is zero. */
size_t previous_semiword_index(const wchar_t *s, size_t i)
{
    const size_t init = i;
start:
    while (i > 0 && (iswblank(s[i]) || iswpunct(s[i])))
	i--;
    while (i > 0 && !iswblank(s[i]) && !iswpunct(s[i]))
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

/* Moves forward one viword (or `count' viwords if the count is set). */
/* exclusive motion command */
void cmd_forward_viword(wchar_t c __attribute__((unused)))
{
    int count = get_count(1);
    if (count >= 0)
	move_cursor_forward_viword(count);
    else
	move_cursor_backward_viword(-count);
}

/* Moves the cursor to the end of the current viword (or the next viword if
 * already at the end). If the count is set, moves to the end of the `count'th
 * viword. */
/* inclusive motion command */
void cmd_end_of_viword(wchar_t c __attribute__((unused)))
{
    if (alert_if_last())
	return;

    int count = get_count(1);
    size_t new_index = le_main_index;
    while (--count >= 0 && new_index < le_main_buffer.length)
	new_index = next_end_of_viword_index(
		le_main_buffer.contents, new_index, true);
    exec_motion_command(new_index, true);
}

/* Moves backward one viword (or `count' viwords if the count is set). */
/* exclusive motion command */
void cmd_backward_viword(wchar_t c __attribute__((unused)))
{
    int count = get_count(1);
    if (count >= 0)
	move_cursor_backward_viword(count);
    else
	move_cursor_forward_viword(-count);
}

/* Moves the cursor forward `count' viwords.
 * If `count' is negative, the cursor is not moved. */
void move_cursor_forward_viword(int count)
{
    if (alert_if_last())
	return;

    size_t new_index = le_main_index;
    if (!need_cw_treatment()) {
	while (count-- > 0 && new_index < le_main_buffer.length)
	    new_index = next_viword_index(le_main_buffer.contents, new_index);
	exec_motion_command(new_index, false);
    } else {
	while (count > 1 && new_index < le_main_buffer.length) {
	    new_index = next_viword_index(le_main_buffer.contents, new_index);
	    count--;
	}
	if (count > 0 && new_index < le_main_buffer.length) {
	    new_index = next_end_of_viword_index(
		    le_main_buffer.contents, new_index, false);
	}
	exec_motion_command(new_index, true);
    }
}

/* Checks if we need a special treatment for the "cw" and "cW" commands. */
bool need_cw_treatment(void)
{
    return (state.pending_command_motion & MEC_INSERT)
	&& !iswblank(le_main_buffer.contents[le_main_index]);
}

/* Moves the cursor backward `count' viwords.
 * If `count' is negative, the cursor is not moved. */
void move_cursor_backward_viword(int count)
{
    if (alert_if_first())
	return;

    size_t new_index = le_main_index;
    while (count-- > 0 && new_index > 0)
	new_index = previous_viword_index(le_main_buffer.contents, new_index);
    exec_motion_command(new_index, false);
}

/* Returns the index of the next viword in string `s', counted from index `i'.
 * The return value is greater than `i' unless `s[i]' is a null character. */
/* A viword is a sequence either of alphanumeric characters and underscores or
 * of other non-blank characters. */
size_t next_viword_index(const wchar_t *s, size_t i)
{
    if (s[i] == L'_' || iswalnum(s[i])) {
	do
	    i++;
	while (s[i] == L'_' || iswalnum(s[i]));
	if (!iswblank(s[i]))
	    return i;
    } else {
	while (!iswblank(s[i])) {
	    if (s[i] == L'\0')
		return i;
	    i++;
	    if (s[i] == L'_' || iswalnum(s[i]))
		return i;
	}
    }

    do
	i++;
    while (iswblank(s[i]));

    return i;
}

/* Returns the index of the end of the current viword in string `s', counted
 * from index `i'.
 * If `progress' is true:
 *   If `i' is at the end of the viword, the end of the next viword is returned.
 *   The return value is greater than `i' unless `s[i]' is a null character.
 * If `progress' is false:
 *   If `i' is at the end of the viword, `i' is returned. */
size_t next_end_of_viword_index(const wchar_t *s, size_t i, bool progress)
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
	while (s[i] != L'\0' && s[i] != L'_'
		&& !iswblank(s[i]) && !iswalnum(s[i]));
    }
    i--;
    if (i > init || !progress) {
	return i;
    } else {
	i++;
	goto start;
    }
}

/* Returns the index of the previous viword in string `s', counted form index
 * `i'. The return value is less than `i' unless `i' is zero. */
size_t previous_viword_index(const wchar_t *s, size_t i)
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
	} while (s[i] != L'_' && !iswblank(s[i]) && !iswalnum(s[i]));
    }
    i++;
    if (i < init) {
	return i;
    } else {
	i--;
	goto start;
    }
}

/* Moves to the next emacsword (or the `count'th emacsword if the count is set).
 */
/* exclusive motion command */
void cmd_forward_emacsword(wchar_t c __attribute__((unused)))
{
    int count = get_count(1);
    if (count >= 0)
	move_cursor_forward_emacsword(count);
    else
	move_cursor_backward_emacsword(-count);
}

/* Moves backward one emacsword (or `count' emacswords if the count is set). */
/* exclusive motion command */
void cmd_backward_emacsword(wchar_t c __attribute__((unused)))
{
    int count = get_count(1);
    if (count >= 0)
	move_cursor_backward_emacsword(count);
    else
	move_cursor_forward_emacsword(-count);
}

/* Moves the cursor to the `count'th emacsword.
 * If `count' is negative, the cursor is not moved. */
void move_cursor_forward_emacsword(int count)
{
    size_t new_index = le_main_index;
    while (count-- > 0 && new_index < le_main_buffer.length)
	new_index = next_emacsword_index(le_main_buffer.contents, new_index);
    exec_motion_command(new_index, false);
}

/* Moves the cursor backward `count'th emacsword.
 * If `count' is negative, the cursor is not moved. */
void move_cursor_backward_emacsword(int count)
{
    size_t new_index = le_main_index;
    while (count-- > 0 && new_index > 0)
	new_index = previous_emacsword_index(
		le_main_buffer.contents, new_index);
    exec_motion_command(new_index, false);
}

/* Returns the index of the next emacsword in string `s', counted from index
 * `i'. The return value is greater than `i' unless `s[i]' is a null character.
 */
/* An emacsword is a sequence of non-alphanumeric characters. */
size_t next_emacsword_index(const wchar_t *s, size_t i)
{
    while (s[i] != L'\0' && !iswalnum(s[i]))
	i++;
    while (s[i] != L'\0' && iswalnum(s[i]))
	i++;
    return i;
}

/* Returns the index of the previous emacsword in string `s', counted from
 * index `i'. The return value is less than `i' unless `i' is zero. */
/* An emacsword is a sequence of alphanumeric characters. */
size_t previous_emacsword_index(const wchar_t *s, size_t i)
{
    const size_t init = i;
start:
    while (i > 0 && !iswalnum(s[i]))
	i--;
    while (i > 0 && iswalnum(s[i]))
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

/* Moves the cursor to the beginning of the line. */
/* exclusive motion command */
void cmd_beginning_of_line(wchar_t c __attribute__((unused)))
{
    exec_motion_command(0, false);
}

/* Moves the cursor to the end of the line. */
/* inclusive motion command */
void cmd_end_of_line(wchar_t c __attribute__((unused)))
{
    exec_motion_command(le_main_buffer.length, true);
}

/* Moves the cursor to the `count'th character in the edit line.
 * If the count is not set, moves to the beginning of the line. If the count is
 * negative, moves to the `le_main_buffer.length + count'th character. */
/* exclusive motion command */
void cmd_go_to_column(wchar_t c __attribute__((unused)))
{
    int index = get_count(0);

    if (index >= 0) {
	if (index > 0)
	    index--;
#if COUNT_ABS_MAX > SIZE_MAX
	if (index > (int) le_main_buffer.length)
#else
	if ((size_t) index > le_main_buffer.length)
#endif
	    index = le_main_buffer.length;
    } else {
#if COUNT_ABS_MAX > SIZE_MAX
	if (-index > (int) le_main_buffer.length)
#else
	if ((size_t) -index > le_main_buffer.length)
#endif
	    index = 0;
	else
	    index = (int) le_main_buffer.length + index;
    }
    exec_motion_command((size_t) index, false);
}

/* Moves the cursor to the first non-blank character. */
/* exclusive motion command */
void cmd_first_nonblank(wchar_t c __attribute__((unused)))
{
    size_t i = 0;

    while (c = le_main_buffer.contents[i], c != L'\0' && iswblank(c))
	i++;
    exec_motion_command(i, false);
}

/* Sets the editing mode to "char expect" and the pending command to
 * `find_char'. */
void cmd_find_char(wchar_t c __attribute__((unused)))
{
    maybe_save_undo_history();
    set_char_expect_command(find_char);
}

/* Moves the cursor to the `count'th occurrence of `c' after the current
 * position. */
/* inclusive motion command */
void find_char(wchar_t c)
{
    exec_find(c, get_count(1), false);
}

/* Sets the editing mode to "char expect" and the pending command to
 * `find_char_rev'. */
void cmd_find_char_rev(wchar_t c __attribute__((unused)))
{
    maybe_save_undo_history();
    set_char_expect_command(find_char_rev);
}

/* Moves the cursor to the `count'th occurrence of `c' before the current
 * position. */
/* exclusive motion command */
void find_char_rev(wchar_t c)
{
    exec_find(c, -get_count(1), false);
}

/* Sets the editing mode to "char expect" and the pending command to
 * `till_char'. */
void cmd_till_char(wchar_t c __attribute__((unused)))
{
    maybe_save_undo_history();
    set_char_expect_command(till_char);
}

/* Moves the cursor to the character just before `count'th occurrence of `c'
 * after the current position. */
/* inclusive motion command */
void till_char(wchar_t c)
{
    exec_find(c, get_count(1), true);
}

/* Sets the editing mode to "char expect" and the pending command to
 * `till_char_rev'. */
void cmd_till_char_rev(wchar_t c __attribute__((unused)))
{
    maybe_save_undo_history();
    set_char_expect_command(till_char_rev);
}

/* Moves the cursor to the character just after `count'th occurrence of `c'
 * before the current position. */
/* exclusive motion command */
void till_char_rev(wchar_t c)
{
    exec_find(c, -get_count(1), true);
}

/* Executes the find/till command. */
void exec_find(wchar_t c, int count, bool till)
{
    le_set_mode(savemode);
    save_current_find_command();

    size_t new_index = find_nth_occurence(c, count);
    if (new_index == SIZE_MAX)
	goto error;
    if (till) {
	if (new_index >= le_main_index) {
	    if (new_index == 0)
		goto error;
	    new_index--;
	} else {
	    if (new_index == le_main_buffer.length)
		goto error;
	    new_index++;
	}
    }
    exec_motion_command(new_index, new_index >= le_main_index);
    return;

error:
    cmd_alert(L'\0');
    return;
}

/* Finds the position of the `n'th occurrence of `c' in the edit line from the
 * current position. Returns `SIZE_MAX' on failure (no such occurrence). */
size_t find_nth_occurence(wchar_t c, int n)
{
    size_t i = le_main_index;

    if (n == 0) {
	return i;
    } else if (c == L'\0') {
	return SIZE_MAX;  /* no such occurrence */
    } else if (n >= 0) {
	while (n > 0 && i < le_main_buffer.length) {
	    i++;
	    if (le_main_buffer.contents[i] == c)
		n--;
	}
    } else {
	while (n < 0 && i > 0) {
	    i--;
	    if (le_main_buffer.contents[i] == c)
		n++;
	}
    }
    if (n != 0)
	return SIZE_MAX;  /* no such occurrence */
    else
	return i;
}

/* Redoes the last find/till command. */
void cmd_refind_char(wchar_t c __attribute__((unused)))
{
    if (!last_find_command.func) {
	cmd_alert(L'\0');
	return;
    }

    last_find_command.func(last_find_command.arg);
}

/* Redoes the last find/till command in the reverse direction. */
void cmd_refind_char_rev(wchar_t c __attribute__((unused)))
{
    if (!last_find_command.func) {
	cmd_alert(L'\0');
	return;
    }

    if (state.count.sign == 0)
	state.count.sign = -1, state.count.abs = 1;
    else if (state.count.sign >= 0)
	state.count.sign = -1;
    else
	state.count.sign = 1;
    last_find_command.func(last_find_command.arg);
}


/********** Editing Commands **********/

/* Removes the character under the cursor.
 * If the count is set, `count' characters are killed. */
void cmd_delete_char(wchar_t c __attribute__((unused)))
{
    enum motion_expect_command_T cmd;

    cmd = (state.count.sign == 0) ? MEC_DELETE : MEC_KILL;
    exec_motion_expect_command(cmd, cmd_forward_char);
}

/* Removes the bigword after the cursor.
 * If the count is set, `count' bigwords are killed.
 * If the cursor is at the end of the line, the terminal is alerted. */
void cmd_delete_bigword(wchar_t c __attribute__((unused)))
{
    enum motion_expect_command_T cmd;

    cmd = (state.count.sign == 0) ? MEC_DELETE : MEC_KILL;
    exec_motion_expect_command(cmd, cmd_forward_bigword);
}

/* Removes the semiword after the cursor.
 * If the count is set, `count' semiwords are killed.
 * If the cursor is at the end of the line, the terminal is alerted. */
void cmd_delete_semiword(wchar_t c __attribute__((unused)))
{
    enum motion_expect_command_T cmd;

    cmd = (state.count.sign == 0) ? MEC_DELETE : MEC_KILL;
    exec_motion_expect_command(cmd, cmd_forward_semiword);
}

/* Removes the viword after the cursor.
 * If the count is set, `count' viwords are killed.
 * If the cursor is at the end of the line, the terminal is alerted. */
void cmd_delete_viword(wchar_t c __attribute__((unused)))
{
    enum motion_expect_command_T cmd;

    cmd = (state.count.sign == 0) ? MEC_DELETE : MEC_KILL;
    exec_motion_expect_command(cmd, cmd_forward_viword);
}

/* Removes the emacsword after the cursor.
 * If the count is set, `count' emacswords are killed.
 * If the cursor is at the end of the line, the terminal is alerted. */
void cmd_delete_emacsword(wchar_t c __attribute__((unused)))
{
    enum motion_expect_command_T cmd;

    cmd = (state.count.sign == 0) ? MEC_DELETE : MEC_KILL;
    exec_motion_expect_command(cmd, cmd_forward_emacsword);
}

/* Removes the character behind the cursor.
 * If the count is set, `count' characters are killed. */
void cmd_backward_delete_char(wchar_t c __attribute__((unused)))
{
    enum motion_expect_command_T cmd;

    cmd = (state.count.sign == 0) ? MEC_DELETE : MEC_KILL;
    exec_motion_expect_command(cmd, cmd_backward_char);
}

/* Removes the bigword behind the cursor.
 * If the count is set, `count' bigwords are killed.
 * If the cursor is at the beginning of the line, the terminal is alerted. */
void cmd_backward_delete_bigword(wchar_t c __attribute__((unused)))
{
    enum motion_expect_command_T cmd;

    cmd = (state.count.sign == 0) ? MEC_DELETE : MEC_KILL;
    exec_motion_expect_command(cmd, cmd_backward_bigword);
}

/* Removes the semiword behind the cursor.
 * If the count is set, `count' semiwords are killed.
 * If the cursor is at the beginning of the line, the terminal is alerted. */
void cmd_backward_delete_semiword(wchar_t c __attribute__((unused)))
{
    enum motion_expect_command_T cmd;

    cmd = (state.count.sign == 0) ? MEC_DELETE : MEC_KILL;
    exec_motion_expect_command(cmd, cmd_backward_semiword);
}

/* Removes the viword behind the cursor.
 * If the count is set, `count' viwords are killed.
 * If the cursor is at the beginning of the line, the terminal is alerted. */
void cmd_backward_delete_viword(wchar_t c __attribute__((unused)))
{
    enum motion_expect_command_T cmd;

    cmd = (state.count.sign == 0) ? MEC_DELETE : MEC_KILL;
    exec_motion_expect_command(cmd, cmd_backward_viword);
}

/* Removes the emacsword behind the cursor.
 * If the count is set, `count' emacswords are killed.
 * If the cursor is at the beginning of the line, the terminal is alerted. */
void cmd_backward_delete_emacsword(wchar_t c __attribute__((unused)))
{
    enum motion_expect_command_T cmd;

    cmd = (state.count.sign == 0) ? MEC_DELETE : MEC_KILL;
    exec_motion_expect_command(cmd, cmd_backward_emacsword);
}

/* Removes all characters in the edit line. */
void cmd_delete_line(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command_line(MEC_DELETE);
}

/* Removes all characters after the cursor. */
void cmd_forward_delete_line(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_DELETE, cmd_end_of_line);
}

/* Removes all characters behind the cursor. */
void cmd_backward_delete_line(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_DELETE, cmd_beginning_of_line);
}

/* Kills the character under the cursor.
 * If the count is set, `count' characters are killed. */
void cmd_kill_char(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_KILL, cmd_forward_char);
}

/* Kills the bigword after the cursor.
 * If the count is set, `count' bigwords are killed.
 * If the cursor is at the end of the line, the terminal is alerted. */
void cmd_kill_bigword(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_KILL, cmd_forward_bigword);
}

/* Kills the semiword after the cursor.
 * If the count is set, `count' semiwords are killed.
 * If the cursor is at the end of the line, the terminal is alerted. */
void cmd_kill_semiword(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_KILL, cmd_forward_semiword);
}

/* Kills the viword after the cursor.
 * If the count is set, `count' viwords are killed.
 * If the cursor is at the end of the line, the terminal is alerted. */
void cmd_kill_viword(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_KILL, cmd_forward_viword);
}

/* Kills the emacsword after the cursor.
 * If the count is set, `count' emacswords are killed.
 * If the cursor is at the end of the line, the terminal is alerted. */
void cmd_kill_emacsword(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_KILL, cmd_forward_emacsword);
}

/* Kills the character behind the cursor.
 * If the count is set, `count' characters are killed.
 * If the cursor is at the beginning of the line, the terminal is alerted. */
void cmd_backward_kill_char(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_KILL, cmd_backward_char);
}

/* Kills the bigword behind the cursor.
 * If the count is set, `count' bigwords are killed.
 * If the cursor is at the beginning of the line, the terminal is alerted. */
void cmd_backward_kill_bigword(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_KILL, cmd_backward_bigword);
}

/* Kills the semiword behind the cursor.
 * If the count is set, `count' semiwords are killed.
 * If the cursor is at the beginning of the line, the terminal is alerted. */
void cmd_backward_kill_semiword(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_KILL, cmd_backward_semiword);
}

/* Kills the viword behind the cursor.
 * If the count is set, `count' viwords are killed.
 * If the cursor is at the beginning of the line, the terminal is alerted. */
void cmd_backward_kill_viword(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_KILL, cmd_backward_viword);
}

/* Kills the emacsword behind the cursor.
 * If the count is set, `count' emacswords are killed.
 * If the cursor is at the beginning of the line, the terminal is alerted. */
void cmd_backward_kill_emacsword(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_KILL, cmd_backward_emacsword);
}

/* Kills all characters in the edit line. */
void cmd_kill_line(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command_line(MEC_KILL);
}

/* Kills all characters after the cursor. */
void cmd_forward_kill_line(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_KILL, cmd_end_of_line);
}

/* Kills all characters before the cursor. */
void cmd_backward_kill_line(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_KILL, cmd_beginning_of_line);
}

/* Inserts the last-killed string before the cursor.
 * If the count is set, inserts `count' times.
 * The cursor is left on the last character inserted. */
void cmd_put_before(wchar_t c __attribute__((unused)))
{
    put_killed_string(false, true);
}

/* Inserts the last-killed string after the cursor.
 * If the count is set, inserts `count' times.
 * The cursor is left on the last character inserted. */
void cmd_put(wchar_t c __attribute__((unused)))
{
    put_killed_string(true, true);
}

/* Inserts the last-killed string before the cursor.
 * If the count is set, inserts `count' times.
 * The cursor is left after the inserted string. */
void cmd_put_left(wchar_t c __attribute__((unused)))
{
    put_killed_string(false, false);
}

/* Inserts the last-killed text at the current cursor position (`count' times).
 * If `after_cursor' is true, the text is inserted after the current cursor
 * position. Otherwise, before the current position.
 * If `cursor_on_last_char' is true, the cursor is left on the last character
 * inserted. Otherwise, the cursor is left after the inserted text. */
void put_killed_string(bool after_cursor, bool cursor_on_last_char)
{
    ALERT_AND_RETURN_IF_PENDING;
    save_current_edit_command();
    maybe_save_undo_history();

    size_t index = (next_kill_index - 1) % KILL_RING_SIZE;
    if (kill_ring[index] == NULL) {
	cmd_alert(L'\0');
	return;
    }

    insert_killed_string(after_cursor, cursor_on_last_char, index);
}

/* Inserts the killed text at the current cursor position (`count' times).
 * If `after_cursor' is true, the text is inserted after the current cursor
 * position. Otherwise, before the current position.
 * If `cursor_on_last_char' is true, the cursor is left on the last character
 * inserted. Otherwise, the cursor is left after the inserted text.
 * `index' specifies the text in the kill ring to be inserted. If the text
 * does not exist at the specified index in the kill ring, this function does
 * nothing. */
void insert_killed_string(
	bool after_cursor, bool cursor_on_last_char, size_t index)
{
    clear_prediction();

    const wchar_t *s = kill_ring[index];
    if (s == NULL)
	return;

    last_put_elem = index;
    if (after_cursor && le_main_index < le_main_buffer.length)
	le_main_index++;

    size_t offset = le_main_buffer.length - le_main_index;
    for (int count = get_count(1); --count >= 0; )
	wb_insert(&le_main_buffer, le_main_index, s);
    assert(le_main_buffer.length >= offset + 1);

    last_put_range_start = le_main_index;
    le_main_index = le_main_buffer.length - offset;
    last_put_range_length = le_main_index - last_put_range_start;
    if (cursor_on_last_char)
	le_main_index--;

    reset_state();
}

/* Replaces the string just inserted by `cmd_put_left' with the previously
 * killed string. */
void cmd_put_pop(wchar_t c __attribute__((unused)))
{
    static bool last_success = false;

    ALERT_AND_RETURN_IF_PENDING;
    if ((last_command.func != cmd_put_left
	    && last_command.func != cmd_put
	    && last_command.func != cmd_put_before
	    && (last_command.func != cmd_put_pop || !last_success))
	    || kill_ring[last_put_elem] == NULL) {
	last_success = false;
	cmd_alert(L'\0');
	return;
    }
    last_success = true;
    save_current_edit_command();
    maybe_save_undo_history();
    clear_prediction();

    size_t index = last_put_elem;
    do
	index = (index - 1) % KILL_RING_SIZE;
    while (kill_ring[index] == NULL);

    /* Remove the just inserted text. */
    assert(last_put_range_start <= le_main_buffer.length);
    wb_remove(&le_main_buffer, last_put_range_start, last_put_range_length);
    le_main_index = last_put_range_start;

    insert_killed_string(false, false, index);
}

/* Undoes the last editing command. */
void cmd_undo(wchar_t c __attribute__((unused)))
{
    cancel_undo(-get_count(1));
}

/* Undoes all changes to the edit line. */
void cmd_undo_all(wchar_t c __attribute__((unused)))
{
    cancel_undo(-COUNT_ABS_MAX);
}

/* Cancels the last undo. */
void cmd_cancel_undo(wchar_t c __attribute__((unused)))
{
    cancel_undo(get_count(1));
}

/* Cancels all previous undo. */
void cmd_cancel_undo_all(wchar_t c __attribute__((unused)))
{
    cancel_undo(COUNT_ABS_MAX);
}

/* Performs "undo"/"cancel undo".
 * `undo_index' is increased by `offset' and the contents of the history entry
 * of the new index is set to the edit line.
 * `offset' must be between `-COUNT_ABS_MAX' and `COUNT_ABS_MAX'. */
void cancel_undo(int offset)
{
    maybe_save_undo_history();
    clear_prediction();

    if (undo_history_entry != main_history_entry)
	goto error;
    if (offset < 0) {
	if (undo_index == 0)
	    goto error;
#if COUNT_ABS_MAX > SIZE_MAX
	if (-offset > (int) undo_index)
#else
	if ((size_t) -offset > undo_index)
#endif
	    undo_index = 0;
	else
	    undo_index += offset;
    } else {
	if (undo_index + 1 >= undo_history.length)
	    goto error;
#if COUNT_ABS_MAX > SIZE_MAX
	if (offset >= (int) (undo_history.length - undo_index))
#else
	if ((size_t) offset >= undo_history.length - undo_index)
#endif
	    undo_index = undo_history.length - 1;
	else
	    undo_index += offset;
    }

    const struct undo_history *entry = undo_history.contents[undo_index];
    wb_replace(&le_main_buffer, 0, SIZE_MAX, entry->contents, SIZE_MAX);
    assert(entry->index <= le_main_buffer.length);
    le_main_index = entry->index;

    reset_state();
    return;

error:
    cmd_alert(L'\0');
    return;
}

/* Redoes the last editing command. */
/* XXX: currently vi's "i" command cannot be redone. */
void cmd_redo(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    if (!last_edit_command.command.func) {
	cmd_alert(L'\0');
	return;
    }

    if (state.count.sign != 0)
	last_edit_command.state.count = state.count;
    state = last_edit_command.state;
    last_edit_command.command.func(last_edit_command.command.arg);
}


/********** Completion Commands **********/

/* Performs command line completion. */
void cmd_complete(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    clear_prediction();
    check_reset_completion();

    le_complete(lecr_normal);

    reset_state();
}

/* Selects the next completion candidate.
 * If the count is set, selects the `count'th next candidate. */
void cmd_complete_next_candidate(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    clear_prediction();
    check_reset_completion();

    le_complete_select_candidate(get_count(1));

    reset_state();
}

/* Selects the previous completion candidate.
 * If the count is set, selects the `count'th previous candidate. */
void cmd_complete_prev_candidate(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    clear_prediction();
    check_reset_completion();

    le_complete_select_candidate(-get_count(1));

    reset_state();
}

/* Selects the first candidate in the next column.
 * If the count is set, selects that of the `count'th next column. */
void cmd_complete_next_column(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    clear_prediction();
    check_reset_completion();

    le_complete_select_column(get_count(1));

    reset_state();
}

/* Selects the first candidate in the previous column.
 * If the count is set, selects that of the `count'th previous column. */
void cmd_complete_prev_column(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    clear_prediction();
    check_reset_completion();

    le_complete_select_column(-get_count(1));

    reset_state();
}

/* Selects the first candidate in the next page.
 * If the count is set, selects that of the `count'th next page. */
void cmd_complete_next_page(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    clear_prediction();
    check_reset_completion();

    le_complete_select_page(get_count(1));

    reset_state();
}

/* Selects the first candidate in the previous page.
 * If the count is set, selects that of the `count'th previous page. */
void cmd_complete_prev_page(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    clear_prediction();
    check_reset_completion();

    le_complete_select_page(-get_count(1));

    reset_state();
}

/* Performs command line completion and
 *   * if the count is not set, list all the candidates without changing the
 *     main buffer.
 *   * if the count is set, complete the `count'th candidate. */
void cmd_complete_list(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    maybe_save_undo_history();
    clear_prediction();
    le_complete_cleanup();
    /* leave `next_reset_completion' to be true because the results of this
     * command cannot be used by succeeding completion commands. */
    // next_reset_completion = false;

    le_complete_fix_candidate(get_count(0));

    reset_state();
}

/* Performs command line completion and replaces the current word with all of
 * the generated candidates. */
void cmd_complete_all(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    clear_prediction();
    check_reset_completion();

    le_complete(lecr_substitute_all_candidates);

    reset_state();
}

/* Performs command line completion and replaces the current word with the
 * longest common prefix of the candidates. */
void cmd_complete_max(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    clear_prediction();
    check_reset_completion();

    le_complete(lecr_longest_common_prefix);

    reset_state();
}

/* Like `cmd_complete_max' for a first key stroke, then like `cmd_complete'. */
void cmd_complete_max_then_list(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    clear_prediction();
    check_reset_completion();

    le_complete(last_command.func != cmd_complete_max_then_list
	    ? lecr_longest_common_prefix : lecr_normal);

    reset_state();
}

/* Like `cmd_complete_max' for a first key stroke, then like
 * `cmd_complete_next_candidate'. */
void cmd_complete_max_then_next_candidate(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    clear_prediction();
    check_reset_completion();

    if (last_command.func != cmd_complete_max_then_next_candidate)
	le_complete(lecr_longest_common_prefix);
    else
	le_complete_select_candidate(get_count(1));

    reset_state();
}

/* Like `cmd_complete_max' for a first key stroke, then like
 * `cmd_complete_prev_candidate'. */
void cmd_complete_max_then_prev_candidate(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    clear_prediction();
    check_reset_completion();

    if (last_command.func != cmd_complete_max_then_prev_candidate)
	le_complete(lecr_longest_common_prefix);
    else
	le_complete_select_candidate(-get_count(1));

    reset_state();
}

/* Clears the current candidates. */
void cmd_clear_candidates(wchar_t c __attribute__((unused)))
{
    le_complete_cleanup();
}

void check_reset_completion(void)
{
    if (reset_completion) {
	maybe_save_undo_history();
	le_complete_cleanup();
	reset_completion = false;
    }
    next_reset_completion = false;
}


/********** Prediction Commands **********/

#ifndef MAX_PREDICTION_SAMPLE
#define MAX_PREDICTION_SAMPLE 10000
#endif /* ifndef MAX_PREDICTION_SAMPLE */

/* Create a probability distribution tree for command prediction based on the
 * current history. The result is set to `prediction_tree'. */
void create_prediction_tree(void)
{
    trie_T *t = trie_create();
#define N 4
    size_t hits[N] = {0};
    for (const histlink_T *l = Histlist; (l = l->prev) != Histlist; ) {
	const histentry_T *e = (const histentry_T *) l;
	size_t k = count_matching_previous_commands(e);
	assert(k < N);
	for (size_t i = 0; i <= k; i++)
	    hits[i]++;
	if (hits[0] >= MAX_PREDICTION_SAMPLE)
	    break;

	wchar_t *cmd = malloc_mbstowcs(e->value);
	if (cmd == NULL)
	    continue;
	t = trie_add_probability(t, cmd, 1.0 / (hits[k] + 1));
	free(cmd);
    }

    prediction_tree = t;
}

// Counts N-1 at most
size_t count_matching_previous_commands(const histentry_T *e)
{
    size_t count = 0;
    const histlink_T *l1 = &e->link, *l2 = Histlist;
    while ((l1 = l1->prev) != Histlist) {
	l2 = l2->prev;

	const histentry_T *e1 = (const histentry_T *) l1;
	const histentry_T *e2 = (const histentry_T *) l2;
	if (strcmp(e1->value, e2->value) != 0)
	    break;
	count++;
	if (count >= N - 1)
	    break;
    }
    return count;
}
#undef N

/* Clears the second part of `le_main_buffer'.
 * Commands that modify the buffer usually need to call this function. However,
 * if a command affects or is affected by the second part, the command might
 * need to update `le_main_length' in a more sophisticated way rather than
 * calling this function. */
void clear_prediction(void)
{
    if (le_main_length < le_main_buffer.length)
	wb_truncate(&le_main_buffer, le_main_length);
    le_main_length = SIZE_MAX;
}

/* Removes any existing prediction and, if the cursor is at the end of line,
 * appends a new prediction to the main buffer. */
void update_buffer_with_prediction(void)
{
    clear_prediction();

    if (!shopt_le_predictempty && active_length() == 0)
	return;

    if (le_main_index < active_length())
	return;

    le_main_length = le_main_buffer.length;

    wchar_t *suffix = trie_probable_key(
	    prediction_tree, le_main_buffer.contents);
    wb_catfree(&le_main_buffer, suffix);
}


/********** Vi-Mode Specific Commands **********/

/* Sets the editing mode to "vi expect" and the pending command to
 * `vi_replace_char'. */
void cmd_vi_replace_char(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    set_char_expect_command(vi_replace_char);
}

/* Replaces the character under the cursor with `c'.
 * If the count is set, the `count' characters are replaced. */
void vi_replace_char(wchar_t c)
{
    save_current_edit_command();
    le_set_mode(savemode);

    if (c != L'\0') {
	int count = get_count(1);

	if (count > 0 && le_main_index < le_main_buffer.length) {
	    do {
		le_main_buffer.contents[le_main_index] = c;
		count--, le_main_index++;
	    } while (count > 0 && le_main_index < le_main_buffer.length);
	    if (le_main_length < le_main_index)
		le_main_length = le_main_index;
	    clear_prediction();
	    le_main_index--;
	}
	reset_state();
    } else {
	cmd_alert(L'\0');
    }
}

/* Moves the cursor to the beginning of the line and sets the editing mode to
 * "vi insert". */
void cmd_vi_insert_beginning(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command_line(MEC_INSERT | MEC_TOSTART);
}

/* Moves the cursor forward one character and sets the editing mode to "vi
 * insert". */
void cmd_vi_append(wchar_t c __attribute__((unused)))
{
    reset_count();
    exec_motion_expect_command(MEC_INSERT | MEC_MOVE, cmd_forward_char);
}

/* Moves the cursor to the end of the line and sets the editing mode to "vi
 * insert".*/
void cmd_vi_append_to_eol(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command_line(MEC_INSERT | MEC_TOEND);
}

/* Sets the editing mode to "vi insert" and starts the overwrite mode. */
void cmd_vi_replace(wchar_t c __attribute__((unused)))
{
    set_mode(LE_MODE_VI_INSERT, true);
}

/* Sets the pending command to MEC_SWITCHCASE.
 * The count multiplier is set to the current count.
 * If the pending command is already set to MEC_SWITCHCASE, the whole line is
 * switch-cased. */
void cmd_vi_switch_case(wchar_t c __attribute__((unused)))
{
    set_motion_expect_command(MEC_SWITCHCASE | MEC_TOSTART);
}

/* Switches the case of the character under the cursor and advances the cursor.
 * If the count is set, `count' characters are changed. */
void cmd_vi_switch_case_char(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_SWITCHCASE | MEC_TOEND, cmd_forward_char);
}

/* Sets the pending command to MEC_COPY.
 * The count multiplier is set to the current count.
 * If the pending command is already set to MEC_COPY, the whole line is copied
 * to the kill ring. */
void cmd_vi_yank(wchar_t c __attribute__((unused)))
{
    set_motion_expect_command(MEC_COPY);
}

/* Copies the content of the edit line from the current position to the end. */
void cmd_vi_yank_to_eol(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_COPY, cmd_end_of_line);
}

/* Sets the pending command to MEC_KILL.
 * The count multiplier is set to the current count. 
 * If the pending command is already set to MEC_KILL, the whole line is moved
 * to the kill ring. */
void cmd_vi_delete(wchar_t c __attribute__((unused)))
{
    set_motion_expect_command(MEC_KILL);
}

/* Deletes the content of the edit line from the current position to the end and
 * put it in the kill ring. */
/* cmd_vi_delete_to_eol is the same as cmd_forward_kill_line.
void cmd_vi_delete_to_eol(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_KILL, cmd_end_of_line);
}
*/

/* Sets the pending command to MEC_CHANGE.
 * The count multiplier is set to the current count. 
 * If the pending command is already set to MEC_CHANGE, the whole line is
 * deleted and the editing mode is set to "vi insert". */
void cmd_vi_change(wchar_t c __attribute__((unused)))
{
    set_motion_expect_command(MEC_CHANGE);
}

/* Deletes the content of the edit line from the current position to the end and
 * sets the editing mode to "vi insert". */
void cmd_vi_change_to_eol(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_CHANGE, cmd_end_of_line);
}

/* Deletes all the content of the edit line and sets the editing mode to "vi
 * insert". */
void cmd_vi_change_line(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command_line(MEC_CHANGE);
}

/* Sets the pending command to `MEC_COPYCHANGE'.
 * The count multiplier is set to the current count. 
 * If the pending command is already set to `MEC_COPYCHANGE', the whole line is
 * moved to the kill ring and the editing mode is set to "vi insert". */
void cmd_vi_yank_and_change(wchar_t c __attribute__((unused)))
{
    set_motion_expect_command(MEC_COPYCHANGE);
}

/* Deletes the content of the edit line from the current position to the end,
 * put it in the kill ring, and sets the editing mode to "vi insert". */
void cmd_vi_yank_and_change_to_eol(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_COPYCHANGE, cmd_end_of_line);
}

/* Deletes all the content of the edit line, put it in the kill ring, and sets
 * the editing mode to "vi insert". */
void cmd_vi_yank_and_change_line(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command_line(MEC_COPYCHANGE);
}

/* Kills the character under the cursor and sets the editing mode to
 * "vi insert". If the count is set, `count' characters are killed. */
void cmd_vi_substitute(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(MEC_COPYCHANGE, cmd_forward_char);
}

/* Appends a space followed by the last bigword from the newest history entry.
 * If the count is specified, the `count'th word is appended.
 * The mode is changed to vi-insert. */
void cmd_vi_append_last_bigword(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    save_current_edit_command();
    maybe_save_undo_history();

    wchar_t *lastcmd = NULL;
    int count = get_count(-1);
    if (count == 0 || histlist.count == 0)
	goto fail;

    struct xwcsrange range;
    lastcmd = malloc_mbstowcs(ashistentry(histlist.Newest)->value);
    if (lastcmd == NULL)
	goto fail;
    if (count >= 0) {
	/* find the count'th word */
	range.start = range.end = lastcmd;
	do {
	    struct xwcsrange r = get_next_bigword(range.end);
	    if (r.start == r.end)
		break;
	    range = r;
	} while (--count > 0 && *range.end != L'\0');
    } else {
	/* find the count'th last word */
	range.start = range.end = lastcmd + wcslen(lastcmd);
	do {
	    struct xwcsrange r = get_prev_bigword(lastcmd, range.start);
	    if (r.start == r.end)
		break;
	    range = r;
	} while (++count < 0 && lastcmd < range.start);
    }
    assert(range.start <= range.end);
    if (range.start == range.end)
	goto fail;

    clear_prediction();
    if (le_main_index < le_main_buffer.length)
	le_main_index++;
    size_t len = range.end - range.start;
    wb_ninsert_force(&le_main_buffer, le_main_index, L" ", 1);
    le_main_index += 1;
    wb_ninsert_force(&le_main_buffer, le_main_index, range.start, len);
    le_main_index += len;
    free(lastcmd);
    cmd_setmode_viinsert(L'\0');
    return;

fail:
    free(lastcmd);
    cmd_alert(L'\0');
    return;
}

struct xwcsrange get_next_bigword(const wchar_t *s)
{
    struct xwcsrange result;
    while (iswblank(*s))
	s++;
    result.start = s;
    while (*s != L'\0' && !iswblank(*s))
	s++;
    result.end = s;
    return result;
    /* result.start == result.end if no bigword found */
}

struct xwcsrange get_prev_bigword(const wchar_t *beginning, const wchar_t *s)
{
    struct xwcsrange result;
    assert(beginning <= s);
    do {
	if (beginning == s) {
	    result.start = result.end = s;
	    return result;
	}
    } while (iswblank(*--s));
    result.end = &s[1];
    do {
	if (beginning == s) {
	    result.start = s;
	    return result;
	}
    } while (!iswblank(*--s));
    result.start = &s[1];
    return result;
    /* result.start == result.end if no bigword found */
}

/* Sets the editing mode to "vi expect" and the pending command to
 * `vi_exec_alias'. */
void cmd_vi_exec_alias(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    set_char_expect_command(vi_exec_alias);
}

/* Appends the value of the alias `_c' to the pre-buffer so that the alias value
 * is interpreted as commands, where `c' in the alias name is the argument of
 * this command. */
void vi_exec_alias(wchar_t c)
{
    le_set_mode(savemode);
    state.pending_command_char = 0;

    wchar_t aliasname[3] = { L'_', c, L'\0', };
    const wchar_t *aliasvalue = get_alias_value(aliasname);
    if (aliasvalue != NULL) {
	char *mbaliasvalue = malloc_wcstombs(aliasvalue);
	if (mbaliasvalue != NULL) {
	    le_append_to_prebuffer(mbaliasvalue);
	    return;
	}
    }
    cmd_alert(L'\0');
}

/* Invokes an external command to edit the current line and accepts the result.
 * If the count is set, goes to the `count'th history entry and edit it.
 * If the editor returns a non-zero status, the line is not accepted. */
/* cf. history.c:fc_edit_and_exec_entries */
void cmd_vi_edit_and_accept(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;

    char *tempfile;
    int fd;
    FILE *f;
    pid_t cpid;
    int savelaststatus;

    if (state.count.sign != 0) {
	int num = get_count(0);
	if (num < 0)
	    goto error0;
	const histlink_T *l = get_history_entry((unsigned) num);
	if (l == Histlist)
	    goto error0;
	go_to_history(l, SEARCH_VI);
    }
    clear_prediction();
    le_complete_cleanup();
    le_suspend_readline();

    fd = create_temporary_file(&tempfile, ".sh", S_IRUSR | S_IWUSR);
    if (fd < 0) {
	xerror(errno, Ngt("cannot create a temporary file to edit history"));
	goto error1;
    }
    f = fdopen(fd, "w");
    if (f == NULL) {
	xerror(errno, Ngt("cannot open temporary file `%s'"), tempfile);
	xclose(fd);
	goto error2;
    }

    savelaststatus = laststatus;
    cpid = fork_and_reset(0, true, 0);
    if (cpid < 0) { // fork failed
	xerror(0, Ngt("cannot invoke the editor to edit history"));
	fclose(f);
	if (unlink(tempfile) < 0)
	    xerror(errno, Ngt("failed to remove temporary file `%s'"),
		    tempfile);
error2:
	free(tempfile);
error1:
	le_resume_readline();
error0:
	cmd_alert(L'\0');
    } else if (cpid > 0) {  // parent process
	fclose(f);

	wchar_t **namep = wait_for_child(cpid,
		doing_job_control_now ? cpid : 0,
		doing_job_control_now);
	if (namep)
	    *namep = malloc_wprintf(L"vi %s", tempfile);
	if (laststatus != Exit_SUCCESS)
	    goto end;

	f = fopen(tempfile, "r");
	if (f == NULL) {
	    cmd_alert(L'\0');
	} else {
	    wint_t c;

	    wb_clear(&le_main_buffer);
	    while ((c = fgetwc(f)) != WEOF)
		wb_wccat(&le_main_buffer, (wchar_t) c);
	    fclose(f);

	    /* remove trailing newline */
	    while (le_main_buffer.length > 0 &&
		    le_main_buffer.contents[le_main_buffer.length - 1] == L'\n')
		wb_remove(&le_main_buffer, le_main_buffer.length - 1, 1);

	    le_main_index = le_main_buffer.length;

	    le_editstate = LE_EDITSTATE_DONE;
end:
	    reset_state();
	}

	laststatus = savelaststatus;
	unlink(tempfile);
	free(tempfile);
	if (shopt_notify || shopt_notifyle)
	    print_job_status_all();
	le_resume_readline();
    } else {  // child process
	fwprintf(f, L"%ls\n", le_main_buffer.contents);
	fclose(f);

	wchar_t *command = malloc_wprintf(L"vi %s", tempfile);
	free(tempfile);
	exec_wcs(command, gt("lineedit"), true);
#ifndef NDEBUG
	free(command);
#endif
	assert(false);
    }
}

/* Performs command line completion and
 *   * if the count is not set, list all the candidates without changing the
 *     main buffer.
 *   * if the count is set, complete the `count'th candidate and set the mode
 *     to vi-insert. */
void cmd_vi_complete_list(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    maybe_save_undo_history();
    clear_prediction();
    le_complete_cleanup();
    /* leave `next_reset_completion' to be true because the results of this
     * command cannot be used by succeeding completion commands. */
    // next_reset_completion = false;

    size_t oldindex = le_main_index;
    if (le_main_index < le_main_buffer.length)
	le_main_index++;

    if (le_complete_fix_candidate(get_count(0))) {
	cmd_setmode_viinsert(L'\0');
    } else {
	le_main_index = oldindex;
	reset_state();
    }
}

/* Performs command line completion and replaces the current word with all of
 * the generated candidates.
 * The mode is changed to vi-insert. */
void cmd_vi_complete_all(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    clear_prediction();
    check_reset_completion();

    if (le_main_index < le_main_buffer.length)
	le_main_index++;
    le_complete(lecr_substitute_all_candidates);

    cmd_setmode_viinsert(L'\0');
}

/* Performs command line completion and replaces the current word with the
 * longest common prefix of the candidates.
 * The mode is changed to vi-insert. */
void cmd_vi_complete_max(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    clear_prediction();
    check_reset_completion();

    if (le_main_index < le_main_buffer.length)
	le_main_index++;
    le_complete(lecr_longest_common_prefix);

    cmd_setmode_viinsert(L'\0');
}

/* Starts vi-like command history search in the forward direction. */
void cmd_vi_search_forward(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    set_search_mode(LE_MODE_VI_SEARCH, FORWARD);
}

/* Starts vi-like command history search in the backward direction. */
void cmd_vi_search_backward(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    set_search_mode(LE_MODE_VI_SEARCH, BACKWARD);
}


/********** Emacs-Mode Specific Commands **********/

/* Moves the character before the cursor to the right by `count' characters. */
void cmd_emacs_transpose_chars(wchar_t c __attribute__((unused)))
{
    //ALERT_AND_RETURN_IF_PENDING;
    if (state.pending_command_motion != MEC_MOVE || le_main_index == 0)
	goto error;
    maybe_save_undo_history();
    if (state.count.sign == 0
	    && le_main_index == le_main_buffer.length && le_main_index >= 2)
	le_main_index--;

    int count = get_count(1);
    size_t index;

    if (count >= 0) {
#if COUNT_ABS_MAX > SIZE_MAX
	if (count <= (int) (le_main_buffer.length - le_main_index))
#else
	if ((size_t) count <= le_main_buffer.length - le_main_index)
#endif
	    index = le_main_index + (size_t) count;
	else {
	    le_main_index = le_main_buffer.length;
	    goto error;
	}
    } else {
#if COUNT_ABS_MAX > SIZE_MAX
	if (-count < (int) le_main_index)
#else
	if ((size_t) -count < le_main_index)
#endif
	    index = le_main_index + (size_t) count;
	else {
	    le_main_index = 0;
	    goto error;
	}
    }

    size_t old_index = le_main_index;

    assert(le_main_index > 0);
    assert(0 < index && index <= le_main_buffer.length);
    c = le_main_buffer.contents[old_index - 1];
    wb_remove(&le_main_buffer, old_index - 1, 1);
    wb_ninsert(&le_main_buffer, index - 1, &c, 1);
    le_main_index = index;

    if (le_main_length < old_index)
	le_main_length = old_index;
    if (le_main_length < index)
	le_main_length = index;

    reset_state();
    return;

error:
    cmd_alert(L'\0');
}

/* Interchanges the word before the cursor and the `count' words after the
 * cursor. */
void cmd_emacs_transpose_words(wchar_t c __attribute__((unused)))
{
    if (state.pending_command_motion != MEC_MOVE || le_main_index == 0)
	goto error;
    maybe_save_undo_history();

    int count = get_count(1);
    size_t w1start, w1end, w2start, w2end, new_index;
    xwcsbuf_T buf;

    if (count == 0)
	goto end;

    w1start = previous_emacsword_index(le_main_buffer.contents, le_main_index);
    w1end = next_emacsword_index(le_main_buffer.contents, w1start);
    if (count >= 0) {
	w2end = next_emacsword_index(le_main_buffer.contents, w1end);
	w2start = previous_emacsword_index(le_main_buffer.contents, w2end);
	while (--count > 0) {
	    if (w2end == le_main_buffer.length)
		goto error;
	    w2end = next_emacsword_index(le_main_buffer.contents, w2end);
	}
	new_index = w2end;
    } else {
	w2start = w1start, w2end = w1end;
	w1start = previous_emacsword_index(le_main_buffer.contents, w2start);
	w1end = next_emacsword_index(le_main_buffer.contents, w1start);
	while (++count < 0) {
	    if (w1start == 0)
		goto error;
	    w1start = previous_emacsword_index(
		    le_main_buffer.contents, w1start);
	}
	new_index = w1start + (w2end - w2start);
    }
    if (w1end >= w2start)
	goto error;
    wb_initwithmax(&buf, w2end - w1start);
    wb_ncat_force(&buf, &le_main_buffer.contents[w2start], w2end - w2start);
    wb_ncat_force(&buf, &le_main_buffer.contents[w1end], w2start - w1end);
    wb_ncat_force(&buf, &le_main_buffer.contents[w1start], w1end - w1start);
    assert(buf.length == w2end - w1start);
    wb_replace_force(&le_main_buffer, w1start, buf.length,
	    buf.contents, buf.length);
    wb_destroy(&buf);
    le_main_index = new_index;
    if (le_main_length < w2end)
	le_main_length = w2end;
end:
    reset_state();
    return;

error:
    cmd_alert(L'\0');
}

/* Converts the word after the cursor to lower case.
 * If the count is set, `count' words are converted.
 * The cursor is left after the last converted word. */
void cmd_emacs_downcase_word(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(
	    MEC_LOWERCASE | MEC_TOEND, cmd_forward_emacsword);
}

/* Converts `count' words after the cursor to upper case.
 * If the count is set, `count' words are converted.
 * The cursor is left after the last converted word. */
void cmd_emacs_upcase_word(wchar_t c __attribute__((unused)))
{
    exec_motion_expect_command(
	    MEC_UPPERCASE | MEC_TOEND, cmd_forward_emacsword);
}

/* Capitalizes the word after the cursor.
 * If the count is set, `count' words are capitalized.
 * The cursor is left after the last capitalized word. */
void cmd_emacs_capitalize_word(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    maybe_save_undo_history();

    int count = get_count(1);

    if (count > 0) {
	wchar_t *s = &le_main_buffer.contents[le_main_index];
	do {
	    while (*s != L'\0' && !iswalnum(*s))
		s++;
	    *s = towupper(*s);
	    s++;
	    while (*s != L'\0' && iswalnum(*s))
		s++;
	} while (*s != L'\0' && --count > 0);
	le_main_index = s - le_main_buffer.contents;
    } else if (count < 0) {
	size_t index = le_main_index;
	do {
	    index = previous_emacsword_index(le_main_buffer.contents, index);
	    le_main_buffer.contents[index] =
		towupper(le_main_buffer.contents[index]);
	} while (index > 0 && ++count < 0);
    }

    if (le_main_length < le_main_index)
	le_main_length = le_main_index;

    reset_state();
}

/* Deletes blank characters around the cursor.
 * If the count is set, only blanks before the cursor are deleted. */
void cmd_emacs_delete_horizontal_space(wchar_t c __attribute__((unused)))
{
    replace_horizontal_space(state.count.sign == 0, L"");
}

/* Replaces blank characters around the cursor with a space.
 * The cursor is left after the space.
 * A space is inserted even if there are no blanks around the cursor.
 * If the count is specified, blanks are replaced with `count' spaces. */
void cmd_emacs_just_one_space(wchar_t c __attribute__((unused)))
{
    int count = get_count(1);
    if (count < 0)
	count = 0;
    else if (count > 1000)
	count = 1000;

    wchar_t s[count + 1];
    wmemset(s, L' ', count);
    s[count] = L'\0';

    replace_horizontal_space(true, s);
}

/* Replaces blank characters around the cursor with the specified string.
 * If `deleteafter' is true, blanks after the cursor are replaced as well as
 * blanks before the cursor. If `deleteafter' is false, only blanks before the
 * cursor are replaced.
 * The cursor is left after the replacement. */
void replace_horizontal_space(bool deleteafter, const wchar_t *s)
{
    ALERT_AND_RETURN_IF_PENDING;
    maybe_save_undo_history();

    size_t start_index = le_main_index;
    while (start_index > 0
	    && iswblank(le_main_buffer.contents[start_index - 1]))
	start_index--;

    size_t end_index = le_main_index;
    if (deleteafter)
	while (end_index < le_main_buffer.length
		&& iswblank(le_main_buffer.contents[end_index]))
	    end_index++;

    if (le_main_length < end_index)
	le_main_length = end_index;

    size_t slen = wcslen(s);
    wb_replace_force(&le_main_buffer, start_index, end_index - start_index,
	    s, slen);

    le_main_index = start_index + slen;
    le_main_length += le_main_index - end_index;

    reset_state();
}

/* Starts emacs-like command history search in the forward direction. */
void cmd_emacs_search_forward(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    set_search_mode(LE_MODE_EMACS_SEARCH, FORWARD);
}

/* Starts emacs-like command history search in the backward direction. */
void cmd_emacs_search_backward(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    set_search_mode(LE_MODE_EMACS_SEARCH, BACKWARD);
}


/********** History-Related Commands **********/

/* Goes to the oldest history entry.
 * If the count is specified, goes to the history entry whose number is count.
 * If the specified entry is not found, the terminal is alerted.
 * The cursor position is not changed. */
void cmd_oldest_history(wchar_t c __attribute__((unused)))
{
    go_to_history_absolute(histlist.Oldest, SEARCH_PREFIX);
}

/* Goes to the newest history entry.
 * If the count is specified, goes to the history entry whose number is count.
 * If the specified entry is not found, the terminal is alerted.
 * The cursor position is not changed. */
void cmd_newest_history(wchar_t c __attribute__((unused)))
{
    go_to_history_absolute(histlist.Newest, SEARCH_PREFIX);
}

/* Goes to the newest history entry.
 * If the count is specified, goes to the history entry whose number is count.
 * If the specified entry is not found, the terminal is alerted.
 * The cursor position is not changed. */
void cmd_return_history(wchar_t c __attribute__((unused)))
{
    go_to_history_absolute(Histlist, SEARCH_PREFIX);
}

/* Goes to the oldest history entry.
 * If the count is specified, goes to the history entry whose number is count.
 * If the specified entry is not found, the terminal is alerted.
 * The cursor is put at the beginning of line. */
void cmd_oldest_history_bol(wchar_t c __attribute__((unused)))
{
    go_to_history_absolute(histlist.Oldest, SEARCH_VI);
}

/* Goes to the newest history entry.
 * If the count is specified, goes to the history entry whose number is count.
 * If the specified entry is not found, the terminal is alerted.
 * The cursor is put at the beginning of line. */
void cmd_newest_history_bol(wchar_t c __attribute__((unused)))
{
    go_to_history_absolute(histlist.Newest, SEARCH_VI);
}

/* Goes to the newest history entry.
 * If the count is specified, goes to the history entry whose number is count.
 * If the specified entry is not found, the terminal is alerted.
 * The cursor is put at the beginning of line. */
void cmd_return_history_bol(wchar_t c __attribute__((unused)))
{
    go_to_history_absolute(Histlist, SEARCH_VI);
}

/* Goes to the oldest history entry.
 * If the count is specified, goes to the history entry whose number is count.
 * If the specified entry is not found, the terminal is alerted.
 * The cursor is put at the end of line. */
void cmd_oldest_history_eol(wchar_t c __attribute__((unused)))
{
    go_to_history_absolute(histlist.Oldest, SEARCH_EMACS);
}

/* Goes to the newest history entry.
 * If the count is specified, goes to the history entry whose number is count.
 * If the specified entry is not found, the terminal is alerted.
 * The cursor is put at the end of line. */
void cmd_newest_history_eol(wchar_t c __attribute__((unused)))
{
    go_to_history_absolute(histlist.Newest, SEARCH_EMACS);
}

/* Goes to the newest history entry.
 * If the count is specified, goes to the history entry whose number is count.
 * If the specified entry is not found, the terminal is alerted.
 * The cursor is put at the end of line. */
void cmd_return_history_eol(wchar_t c __attribute__((unused)))
{
    go_to_history_absolute(Histlist, SEARCH_EMACS);
}

/* Goes to the specified history entry.
 * If the count is specified, goes to the history entry whose number is count.
 * If the specified entry is not found, the terminal is alerted.
 * See `go_to_history' for the meaning of `curpos'. */
void go_to_history_absolute(const histlink_T *l, enum le_search_type_T curpos)
{
    ALERT_AND_RETURN_IF_PENDING;

    if (state.count.sign == 0) {
	if (histlist.count == 0)
	    goto alert;
    } else {
	int num = get_count(0);
	if (num <= 0)
	    goto alert;
	l = get_history_entry((unsigned) num);
	if (l == Histlist)
	    goto alert;
    }
    go_to_history(l, curpos);
    reset_state();
    return;

alert:
    cmd_alert(L'\0');
    return;
}

/* Goes to the `count'th next history entry.
 * The cursor position is not changed. */
void cmd_next_history(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    go_to_history_relative(get_count(1), SEARCH_PREFIX);
}

/* Goes to the `count'th previous history entry.
 * The cursor position is not changed. */
void cmd_prev_history(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    go_to_history_relative(-get_count(1), SEARCH_PREFIX);
}

/* Goes to the `count'th next history entry.
 * The cursor is put at the beginning of line. */
void cmd_next_history_bol(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    go_to_history_relative(get_count(1), SEARCH_VI);
}

/* Goes to the `count'th previous history entry.
 * The cursor is put at the beginning of line. */
void cmd_prev_history_bol(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    go_to_history_relative(-get_count(1), SEARCH_VI);
}

/* Goes to the `count'th next history entry.
 * The cursor is put at the end of line. */
void cmd_next_history_eol(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    go_to_history_relative(get_count(1), SEARCH_EMACS);
}

/* Goes to the `count'th previous history entry.
 * The cursor is put at the end of line. */
void cmd_prev_history_eol(wchar_t c __attribute__((unused)))
{
    ALERT_AND_RETURN_IF_PENDING;
    go_to_history_relative(-get_count(1), SEARCH_EMACS);
}

/* Goes to the `offset'th next history entry.
 * See `go_to_history' for the meaning of `curpos'. */
void go_to_history_relative(int offset, enum le_search_type_T curpos)
{
    const histlink_T *l = main_history_entry;
    if (offset > 0) {
	do {
	    if (l == Histlist)
		goto alert;
	    l = l->next;
	} while (--offset > 0);
    } else if (offset < 0) {
	do {
	    l = l->prev;
	    if (l == Histlist)
		goto alert;
	} while (++offset < 0);
    }
    go_to_history(l, curpos);
    reset_state();
    return;

alert:
    cmd_alert(L'\0');
    return;
}

/* Sets the value of the specified history entry to the main buffer.
 * The value of `curpos' specifies where the cursor is left:
 *  SEARCH_PREFIX: the current position (unless it exceeds the buffer length)
 *  SEARCH_VI:     the beginning of the buffer
 *  SEARCH_EMACS:  the end of the buffer */
void go_to_history(const histlink_T *l, enum le_search_type_T curpos)
{
    maybe_save_undo_history();
    clear_prediction();

    free(main_history_value);
    wb_clear(&le_main_buffer);
    if (l == undo_history_entry && undo_index < undo_history.length) {
	struct undo_history *h = undo_history.contents[undo_index];
	wb_cat(&le_main_buffer, h->contents);
	assert(h->index <= le_main_buffer.length);
	le_main_index = h->index;
    } else {
	if (l != Histlist)
	    wb_mbscat(&le_main_buffer, ashistentry(l)->value);
	switch (curpos) {
	    case SEARCH_PREFIX:
		if (le_main_index > le_main_buffer.length)
		    le_main_index = le_main_buffer.length;
		break;
	    case SEARCH_VI:
		le_main_index = 0;
		break;
	    case SEARCH_EMACS:
		le_main_index = le_main_buffer.length;
		break;
	}
    }
    main_history_entry = l;
    main_history_value = xwcsdup(le_main_buffer.contents);
    undo_save_index = le_main_index;
}

/***** History Search Commands *****/

/* Appends the argument character to the search buffer. */
void cmd_srch_self_insert(wchar_t c)
{
    if (le_search_buffer.contents == NULL || c == L'\0') {
	cmd_alert(L'\0');
	return;
    }

    wb_wccat(&le_search_buffer, c);
    update_search();
}

/* Removes the last character from the search buffer.
 * If there are no characters in the buffer, calls `cmd_srch_abort_search' (for
 * vi-like search) or `cmd_alert' (for emacs-like search). */
void cmd_srch_backward_delete_char(wchar_t c __attribute__((unused)))
{
    if (le_search_buffer.contents == NULL) {
	cmd_alert(L'\0');
	return;
    }

    if (le_search_buffer.length == 0) {
	switch (le_search_type) {
	    case SEARCH_VI:
		cmd_srch_abort_search(L'\0');
		return;
	    case SEARCH_EMACS:
		cmd_alert(L'\0');
		return;
	    case SEARCH_PREFIX:
		assert(false);
	}
    }

    wb_remove(&le_search_buffer, le_search_buffer.length - 1, 1);
    update_search();
}

/* Removes all characters from the search buffer. */
void cmd_srch_backward_delete_line(wchar_t c __attribute__((unused)))
{
    if (le_search_buffer.contents == NULL) {
	cmd_alert(L'\0');
	return;
    }

    wb_clear(&le_search_buffer);
    update_search();
}

/* Settles the current search result and continues the search with the current
 * pattern in the forward direction. This is like `cmd_search_again_forward'
 * but this command can be used during the search. */
void cmd_srch_continue_forward(wchar_t c __attribute__((unused)))
{
    if (le_search_buffer.contents == NULL) {
	cmd_alert(L'\0');
	return;
    }

    le_search_direction = FORWARD;
    if (le_search_result != Histlist)
	go_to_history(le_search_result, le_search_type);
    update_search();
}

/* Settles the current search result and continues the search with the current
 * pattern in the backward direction. This is like `cmd_search_again_backward'
 * but this command can be used during the search. */
void cmd_srch_continue_backward(wchar_t c __attribute__((unused)))
{
    if (le_search_buffer.contents == NULL) {
	cmd_alert(L'\0');
	return;
    }

    le_search_direction = BACKWARD;
    if (le_search_result != Histlist)
	go_to_history(le_search_result, le_search_type);
    update_search();
}

/* Finishes the history search and accepts the current result candidate.
 * If no search is being performed, does nothing. */
void cmd_srch_accept_search(wchar_t c __attribute__((unused)))
{
    if (le_search_buffer.contents == NULL)
	return;

    last_search.direction = le_search_direction;
    last_search.type = le_search_type;
    if (need_update_last_search_value()) {
	free(last_search.value);
	last_search.value = wb_towcs(&le_search_buffer);
    } else {
	wb_destroy(&le_search_buffer);
    }
    le_search_buffer.contents = NULL;
    le_set_mode(savemode);
    if (le_search_result == Histlist) {
	cmd_alert(L'\0');
    } else {
	go_to_history(le_search_result, le_search_type);
    }
    reset_state();
}

/* Checks if we should update `last_search.value' to the current value of
 * `le_search_buffer'. */
bool need_update_last_search_value(void)
{
    switch (le_search_type) {
	case SEARCH_PREFIX:
	    break;
	case SEARCH_VI:
	    if (le_search_buffer.contents[0] == L'\0')
		return false;
	    if (le_search_buffer.contents[0] == L'^'
		    && le_search_buffer.contents[1] == L'\0')
		return false;
	    return true;
	case SEARCH_EMACS:
	    return le_search_buffer.contents[0] != L'\0';
    }
    assert(false);
}

/* Aborts the history search.
 * If no search is being performed, does nothing. */
void cmd_srch_abort_search(wchar_t c __attribute__((unused)))
{
    if (le_search_buffer.contents == NULL)
	return;

    wb_destroy(&le_search_buffer);
    le_search_buffer.contents = NULL;
    le_set_mode(savemode);
    reset_state();
}

/* Re-calculates the search result candidate. */
void update_search(void)
{
    const wchar_t *pattern = le_search_buffer.contents;
    if (pattern[0] == L'\0') {
	switch (le_search_type) {
	    case SEARCH_PREFIX:
		break;
	    case SEARCH_VI:
		pattern = last_search.value;
		if (pattern == NULL) {
		    le_search_result = Histlist;
		    goto done;
		}
		break;
	    case SEARCH_EMACS:
		le_search_result = Histlist;
		goto done;
	}
    }

    perform_search(pattern, le_search_direction, le_search_type);
done:
    reset_state();
}

/* Performs history search with the given parameters and updates the result
 * candidate. */
void perform_search(const wchar_t *pattern,
	enum le_search_direction_T dir, enum le_search_type_T type)
{
    const histlink_T *l = main_history_entry;
    xfnmatch_T *xfnm;

    if (dir == FORWARD && l == Histlist)
	goto done;

    switch (type) {
	case SEARCH_PREFIX: {
	    wchar_t *p = escape(pattern, NULL);
	    xfnm = xfnm_compile(p, XFNM_HEADONLY);
	    free(p);
	    break;
	}
	case SEARCH_VI: {
	    xfnmflags_T flags = 0;
	    if (pattern[0] == L'^') {
		flags |= XFNM_HEADONLY;
		pattern++;
		if (pattern[0] == L'\0') {
		    l = Histlist;
		    goto done;
		}
	    }
	    xfnm = xfnm_compile(pattern, flags);
	    break;
	}
	case SEARCH_EMACS: {
	    wchar_t *p = escape(pattern, NULL);
	    xfnm = xfnm_compile(p, 0);
	    free(p);
	    break;
	}
	default:
	    assert(false);
    }
    if (xfnm == NULL) {
	l = Histlist;
	goto done;
    }

    for (;;) {
	switch (dir) {
	    case FORWARD:   l = l->next;  break;
	    case BACKWARD:  l = l->prev;  break;
	}
	if (l == Histlist)
	    break;
	if (xfnm_match(xfnm, ashistentry(l)->value) == 0)
	    break;
    }
    xfnm_free(xfnm);
done:
    le_search_result = l;
}

/* Redoes the last search. */
void cmd_search_again(wchar_t c __attribute__((unused)))
{
    search_again(last_search.direction);
}

/* Redoes the last search in the reverse direction. */
void cmd_search_again_rev(wchar_t c __attribute__((unused)))
{
    switch (last_search.direction) {
	case FORWARD:   search_again(BACKWARD);  break;
	case BACKWARD:  search_again(FORWARD);   break;
    }
}

/* Redoes the last search in the forward direction. */
void cmd_search_again_forward(wchar_t c __attribute__((unused)))
{
    search_again(FORWARD);
}

/* Redoes the last search in the backward direction. */
void cmd_search_again_backward(wchar_t c __attribute__((unused)))
{
    search_again(BACKWARD);
}

/* Performs command search for the last search pattern in the specified
 * direction. If the count is set, re-search `count' times. If the count is
 * negative, search in the opposite direction. */
void search_again(enum le_search_direction_T dir)
{
    ALERT_AND_RETURN_IF_PENDING;

    if (last_search.value == NULL) {
	cmd_alert(L'\0');
	return;
    }

    int count = get_count(1);
    if (count < 0) {
	count = -count;
	switch (dir) {
	    case FORWARD:  dir = BACKWARD; break;
	    case BACKWARD: dir = FORWARD;  break;
	}
    }

    while (--count >= 0) {
	perform_search(last_search.value, dir, last_search.type);
	if (le_search_result == Histlist) {
	    cmd_alert(L'\0');
	    break;
	} else {
	    go_to_history(le_search_result, last_search.type);
	}
    }
    reset_state();
}

/* Searches the history in the forward direction for an entry that has a common
 * prefix with the current buffer contents. The "prefix" is the first
 * `le_main_index' characters of the buffer. */
void cmd_beginning_search_forward(wchar_t c __attribute__((unused)))
{
    beginning_search(FORWARD);
}

/* Searches the history in the backward direction for an entry that has a common
 * prefix with the current buffer contents. The "prefix" is the first
 * `le_main_index' characters of the buffer. */
void cmd_beginning_search_backward(wchar_t c __attribute__((unused)))
{
    beginning_search(BACKWARD);
}

/* Searches the history in the specified direction for an entry that has a
 * common prefix with the current buffer contents. The "prefix" is the first
 * `le_main_index' characters of the buffer. */
void beginning_search(enum le_search_direction_T dir)
{
    ALERT_AND_RETURN_IF_PENDING;

    int count = get_count(1);
    if (count < 0) {
	count = -count;
	switch (dir) {
	    case FORWARD:  dir = BACKWARD; break;
	    case BACKWARD: dir = FORWARD;  break;
	}
    }

    wchar_t *prefix = xwcsndup(le_main_buffer.contents, le_main_index);
    while (--count >= 0) {
	perform_search(prefix, dir, SEARCH_PREFIX);
	if (le_search_result == Histlist) {
	    if (dir == FORWARD && beginning_search_check_go_to_history(prefix))
		go_to_history(le_search_result, SEARCH_PREFIX);
	    else
		cmd_alert(L'\0');
	    break;
	} else {
	    go_to_history(le_search_result, SEARCH_PREFIX);
	}
    }
    free(prefix);
    reset_state();
}

bool beginning_search_check_go_to_history(const wchar_t *prefix)
{
    if (le_search_result == undo_history_entry
	    && undo_index < undo_history.length) {
	const struct undo_history *h = undo_history.contents[undo_index];
	return matchwcsprefix(h->contents, prefix) != NULL;
    } else {
	return false;
    }
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
