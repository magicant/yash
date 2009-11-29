/* Yash: yet another shell */
/* lineedit.c: command line editing */
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
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include "../option.h"
#include "../sig.h"
#include "../strbuf.h"
#include "../util.h"
#include "../variable.h"
#include "display.h"
#include "editing.h"
#include "key.h"
#include "keymap.h"
#include "lineedit.h"
#include "terminfo.h"
#include "trie.h"


static void reader_init(void);
static void reader_finalize(void);
static void read_next(void);
static int get_read_timeout(void)
    __attribute__((pure));
static char pop_prebuffer(void);
static inline bool has_meta_bit(char c)
    __attribute__((pure));
static inline trieget_T make_trieget(const wchar_t *keyseq)
    __attribute__((nonnull,const));
static void append_to_second_buffer(wchar_t wc);


/* The state of lineedit. */
enum le_state_T le_state;
/* The state of editing. */
enum le_editstate_T le_editstate;


/* Initializes line-editing.
 * Must be called before each call to `le_readline'.
 * Returns true iff successful, in which case `le_readline' must be called
 * afterward. */
bool le_setup(void)
{
    le_keymap_init();
    return isatty(STDIN_FILENO) && isatty(STDERR_FILENO)
	&& le_setupterm(true)
	&& le_set_terminal();
}

/* Prints the specified `prompt' and reads one line from the standard input.
 * This function can be called after and only after `le_setup' succeeded.
 * The `prompt' may contain backslash escapes specified in "input.c".
 * The result is returned as a newly malloced wide string, including the
 * trailing newline. When EOF is encountered or on error, an empty string is
 * returned. NULL is returned when interrupted. */
wchar_t *le_readline(const wchar_t *prompt)
{
    wchar_t *resultline;

    assert(is_interactive_now);
    assert(le_state == LE_STATE_INACTIVE);

    le_state = LE_STATE_ACTIVE;
    le_editing_init();
    le_display_init(prompt);
    le_display_maybe_promptsp();
    reader_init();
    le_editstate = LE_EDITSTATE_EDITING;

    do
	read_next();
    while (le_editstate == LE_EDITSTATE_EDITING);

    reader_finalize();
    le_display_finalize();
    resultline = le_editing_finalize();
    le_restore_terminal();
    le_state = LE_STATE_INACTIVE;

    switch (le_editstate) {
	case LE_EDITSTATE_EDITING:
	    assert(false);
	case LE_EDITSTATE_DONE:
	    break;
	case LE_EDITSTATE_ERROR:
	    resultline[0] = L'\0';
	    break;
	case LE_EDITSTATE_INTERRUPTED:
	    free(resultline);
	    resultline = NULL;
	    break;
    }
    return resultline;
}

/* Clears the edit line and restores the terminal state.
 * Does nothing if line-editing is not active. */
void le_suspend_readline(void)
{
    if (le_state == LE_STATE_ACTIVE) {
	le_state = LE_STATE_SUSPENDED;
	le_display_clear();
	le_restore_terminal();
    }
}

/* Resumes line-editing suspended by `le_suspend_readline'.
 * Does nothing if the line-editing is not suspended. */
void le_resume_readline(void)
{
    if (le_state == LE_STATE_SUSPENDED) {
	le_state = LE_STATE_ACTIVE;
	le_setupterm(true);
	le_set_terminal();
	le_display_maybe_promptsp();
	le_display_update();
	fflush(stderr);
    }
}

/* Re-retrieves the terminfo and reprints everything. */
/* When the display size is changed, this function is called. */
void le_display_size_changed(void)
{
    if (le_state == LE_STATE_ACTIVE) {
	le_display_clear();
	le_setupterm(true);
	le_display_update();
	fflush(stderr);
    }
}


/********** Input Reading **********/

/* Temporary buffer that contains bytes that are treated as input. */
static xstrbuf_T reader_prebuffer;
/* Temporary buffer that contains input bytes. */
static xstrbuf_T reader_first_buffer;
/* Conversion state used in reading input. */
static mbstate_t reader_state;
/* Temporary buffer that contains input converted into wide characters. */
static xwcsbuf_T reader_second_buffer;
/* If true, next input will be inserted directly to the main buffer. */
bool le_next_verbatim;

/* Initializes the state of the reader. */
void reader_init(void)
{
    sb_init(&reader_first_buffer);
    memset(&reader_state, 0, sizeof reader_state);
    wb_init(&reader_second_buffer);
    le_next_verbatim = false;
}

/* Frees memory used by the reader. */
void reader_finalize(void)
{
    sb_destroy(&reader_first_buffer);
    wb_destroy(&reader_second_buffer);
}

/* Reads the next byte from the standard input and take all the corresponding
 * actions.
 * May return without doing anything if a signal is caught, etc.
 * The caller must check `le_state' after this function returned. This function
 * should be called repeatedly while `le_state' is LE_STATE_EDITING. */
void read_next(void)
{
    static bool incomplete_wchar = false;
    static bool keycode_ambiguous = false;

    assert(le_editstate == LE_EDITSTATE_EDITING);

    bool timeout = false;
    char c = pop_prebuffer();
    if (c)
	goto direct_first_buffer;

    /* wait for and read the next byte */
    le_display_update();
    fflush(stderr);
    timeout = !wait_for_input(
	    STDIN_FILENO, true, keycode_ambiguous ? get_read_timeout() : -1);
    if (!timeout) {
	switch (read(STDIN_FILENO, &c, 1)) {
	    case 0:
		incomplete_wchar = keycode_ambiguous = false;
		le_editstate = LE_EDITSTATE_ERROR;
		return;
	    case 1:
		break;
	    case -1:
		switch (errno) {
		    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
		    case EWOULDBLOCK:
#endif
		    case EINTR:
			return;
		    default:
			xerror(errno, Ngt("cannot read input"));
			incomplete_wchar = keycode_ambiguous = false;
			le_editstate = LE_EDITSTATE_ERROR;
			return;
		}
	    default:
		assert(false);
	}
	if (has_meta_bit(c))
	    sb_ccat(sb_ccat(&reader_first_buffer, ESCAPE_CHAR), c & ~META_BIT);
	else
direct_first_buffer:
	    sb_ccat(&reader_first_buffer, c);
    }

    if (incomplete_wchar || le_next_verbatim)
	goto process_wide;

    /* process the content in the first buffer */
    keycode_ambiguous = false;
    while (reader_first_buffer.length > 0) {
	/* check if `reader_first_buffer' is a special sequence */
	int firstchar = (unsigned char) reader_first_buffer.contents[0];
	trieget_T tg;
	if (firstchar == le_interrupt_char)
	    tg = make_trieget(Key_interrupt);
	else if (firstchar == le_eof_char)
	    tg = make_trieget(Key_eof);
	else if (firstchar == le_kill_char)
	    tg = make_trieget(Key_kill);
	else if (firstchar == le_erase_char)
	    tg = make_trieget(Key_erase);
	else
	    tg = trie_get(le_keycodes,
		reader_first_buffer.contents, reader_first_buffer.length);
	switch (tg.type) {
	    case TG_NOMATCH:
		goto process_wide;
	    case TG_AMBIGUOUS:
		if (timeout) {
	    case TG_EXACTMATCH:
		    sb_remove(&reader_first_buffer, 0, tg.matchlength);
		    wb_cat(&reader_second_buffer, tg.value.keyseq);
		    continue;
		} else {
		    keycode_ambiguous = true;
		}
		/* falls thru! */
	    case TG_PREFIXMATCH:
		goto process_keymap;
	}
    }

    /* convert bytes in the first buffer into wide characters and append to the
     * second buffer */
process_wide:
    while (reader_first_buffer.length > 0) {
	wchar_t wc;
	size_t n = mbrtowc(&wc, reader_first_buffer.contents,
		reader_first_buffer.length, &reader_state);
	incomplete_wchar = false;
	switch (n) {
	    case 0:            // read null character
		sb_clear(&reader_first_buffer);
		append_to_second_buffer(L'\0');
		break;
	    case (size_t) -1:  // conversion error
		le_alert();
		memset(&reader_state, 0, sizeof reader_state);
		sb_clear(&reader_first_buffer);
		le_next_verbatim = false;
		goto process_keymap;
	    case (size_t) -2:  // more bytes needed
		incomplete_wchar = true;
		sb_clear(&reader_first_buffer);
		goto process_keymap;
	    default:
		sb_remove(&reader_first_buffer, 0, n);
		append_to_second_buffer(wc);
		break;
	}
    }

    /* process key mapping for wide characters in the second buffer */
process_keymap:
    while (reader_second_buffer.length > 0) {
	register wchar_t c;
	trieget_T tg = trie_getw(
		le_current_mode->keymap, reader_second_buffer.contents);
	switch (tg.type) {
	    case TG_NOMATCH:
		assert(reader_second_buffer.length > 0);
		if (reader_second_buffer.length > 1)
		    c = L'\0';
		else
		    c = reader_second_buffer.contents[0];
		le_invoke_command(le_current_mode->default_command, c);
		wb_clear(&reader_second_buffer);
		break;
	    case TG_EXACTMATCH:
		assert(tg.matchlength > 0);
		c = reader_second_buffer.contents[tg.matchlength - 1];
		le_invoke_command(tg.value.cmdfunc, c);
		wb_remove(&reader_second_buffer, 0, tg.matchlength);
		break;
	    case TG_PREFIXMATCH:
	    case TG_AMBIGUOUS:
		return;
	/* For an unmatched control sequence, `cmd_self_insert' should not
	 * receive any part of the sequence as argument (because the sequence
	 * should not be inserted in the main buffer), so the input is passed
	 * to the default command iff the input is a usual character.
	 * For a matched control sequence, the last character of the sequence
	 * is passed to the command so that commands like `cmd_digit_argument'
	 * work. */
	}
	if (le_editstate != LE_EDITSTATE_EDITING)
	    break;
    }
}

/* Returns a timeout value to be passed to the `wait_for_input' function.
 * The value is taken from the $YASH_LE_TIMEOUT variable. */
int get_read_timeout(void)
{
#ifndef LE_TIMEOUT_DEFAULT
#define LE_TIMEOUT_DEFAULT 100
#endif

    const wchar_t *v = getvar(L VAR_YASH_LE_TIMEOUT);
    if (v != NULL) {
	int i;
	if (xwcstoi(v, 0, &i))
	    return i;
    }
    return LE_TIMEOUT_DEFAULT;
}

/* Appends `s' to the prebuffer. String `s' is freed in this function. */
void append_to_prebuffer(char *s)
{
    if (reader_prebuffer.contents == NULL)
	sb_initwith(&reader_prebuffer, s);
    else
	sb_catfree(&reader_prebuffer, s);
}

/* Removes and returns the next character in the prebuffer if available;
 * otherwise, returns '\0'. */
char pop_prebuffer(void)
{
    if (reader_prebuffer.contents) {
	char result = reader_prebuffer.contents[0];
	sb_remove(&reader_prebuffer, 0, 1);
	if (reader_prebuffer.length == 0) {
	    sb_destroy(&reader_prebuffer);
	    reader_prebuffer.contents = NULL;
	}
	return result;
    }
    return '\0';
}

/* Tests if the specified character has the meta flag set. */
bool has_meta_bit(char c)
{
    switch (shopt_le_convmeta) {
	case shopt_yes:
	    break;
	case shopt_no:
	    return false;
	case shopt_auto:
	    if (le_meta_bit8)
		break;
	    else
		return false;
    }
    return (c & META_BIT) != 0;
}

trieget_T make_trieget(const wchar_t *keyseq)
{
    return (trieget_T) {
	.type = TG_EXACTMATCH,
	.matchlength = 1,
	.value.keyseq = keyseq,
    };
}

/* Appends the specified character to the second buffer.
 * If `le_next_verbatim' is true, the character is directly processed by the
 * default command. */
void append_to_second_buffer(wchar_t wc)
{
    if (le_next_verbatim) {
	le_next_verbatim = false;
	le_invoke_command(le_current_mode->default_command, wc);
    } else {
	switch (wc) {
	    case L'\0':
		break;
	    case L'\\':
		wb_cat(&reader_second_buffer, Key_backslash);
		break;
	    default:
		wb_wccat(&reader_second_buffer, wc);
		break;
	}
    }
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
