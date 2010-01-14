/* Yash: yet another shell */
/* complete.c: command line completion */
/* (C) 2007-2010 magicant */

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
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../option.h"
#include "../util.h"
#include "complete.h"
#include "display.h"
#include "editing.h"
#include "lineedit.h"
#include "terminfo.h"


static void free_candidate(void *c)
    __attribute__((nonnull));
static void calculate_common_prefix_length(void);
static void set_candidate(void);
static void finish_word(void);
static void compdebug(const char *format, ...)
    __attribute__((nonnull,format(printf,1,2)));

/* A list that contains the current completion candidates.
 * The elements pointed to by `le_candidates.contains[*]' are of type
 * `le_candidate_T'. */
plist_T le_candidates = { .contents = NULL };
/* The index of the currently selected candidate in `le_candidates'.
 * When no candidate is selected, the index is `le_candidates.length'. */
size_t le_selected_candidate_index;

/* The index of the cursor before completion started. */
static size_t insertion_index;
/* The length of the expanded source word. */
static size_t expanded_source_word_length;
/* The length of the longest common prefix of the current candidates. */
static size_t common_prefix_length;


/* Performs command line completion.
 * Existing candidates are deleted, if any, and candidates are computed from
 * the current command line. */
void le_complete(void)
{
    if (shopt_le_compdebug) {
	le_display_finalize();
	le_restore_terminal();
	le_state = LE_STATE_SUSPENDED;
	compdebug("completion start");
    }

    le_complete_cleanup();
    pl_init(&le_candidates);

    insertion_index = le_main_index;
    expanded_source_word_length = 0;  // TODO

    //TODO
    // this is test implementation
    for (int i = 0; i < 97; i++) {
	le_candidate_T *cand = xmalloc(sizeof *cand);
	cand->value = malloc_wprintf(L"cand%d", i + 5);
	cand->rawvalue = NULL;
	cand->width = 0;
	pl_add(&le_candidates, cand);
    }
    compdebug("got %zu candidate(s)", le_candidates.length);

    if (le_candidates.length == 0) {
	le_selected_candidate_index = 0;
    } else if (le_candidates.length == 1) {
	le_selected_candidate_index = 0;
	set_candidate();
	finish_word();
	le_complete_cleanup();
    } else {
	calculate_common_prefix_length();
	if (shopt_le_compdebug) {
	    const le_candidate_T *cand = le_candidates.contents[0];
	    wchar_t *common_prefix
		= xwcsndup(cand->value, common_prefix_length);
	    compdebug("candidate common prefix: \"%ls\"", common_prefix);
	    free(common_prefix);
	}
	le_selected_candidate_index = le_candidates.length;
	set_candidate();
    }

    if (shopt_le_compdebug) {
	compdebug("completion end");
	le_setupterm(false);
	le_set_terminal();
	le_state = LE_STATE_ACTIVE;
    }
}

/* Increases `le_selected_candidate_index' by `offset', selecting the `offset'th
 * next candidate. If there are no candidates, simply calls `le_complete' to
 * produce candidates. */
void le_complete_select(int offset)
{
    if (le_candidates.contents == NULL) {
	le_complete();
	return;
    } else if (le_candidates.length == 0) {
	return;
    }

    assert(le_selected_candidate_index <= le_candidates.length);
    offset %= (int) le_candidates.length + 1;
    if (offset < 0)
	offset += (int) le_candidates.length + 1;
    assert(offset >= 0);
    le_selected_candidate_index += offset;
    le_selected_candidate_index %= le_candidates.length + 1;

    set_candidate();
}

/* Clears the current candidates. */
void le_complete_cleanup(void)
{
    le_display_complete_cleanup();
    if (le_candidates.contents != NULL) {
	recfree(pl_toary(&le_candidates), free_candidate);
	le_candidates.contents = NULL;
    }
}

/* Frees a completion candidate.
 * The argument must point to a `le_candidate_T' value. */
void free_candidate(void *c)
{
    le_candidate_T *cand = c;
    free(cand->value);
    free(cand->rawvalue);
    free(cand);
}

/* Calculates the value of `common_prefix_length' for the current candidates.
 * There must be at least one candidate in `le_candidates'. */
void calculate_common_prefix_length(void)
{
    assert(le_candidates.contents != NULL);
    assert(le_candidates.length > 0);

    const le_candidate_T *cand = le_candidates.contents[0];
    const wchar_t *value = cand->value;
    size_t cpl = wcslen(value);
    for (size_t i = 1; i < le_candidates.length; i++) {
	const le_candidate_T *cand2 = le_candidates.contents[i];
	const wchar_t *value2 = cand2->value;
	for (size_t j = 0; j < cpl; j++)
	    if (value[j] != value2[j])  // XXX comparison is case-sensitive
		cpl = j;
    }
    common_prefix_length = cpl;
}

/* Sets the contents of the main buffer to the currently selected candidate.
 * When no candidate is selected, sets to the longest common prefix of the
 * candidates. There must be at least one candidate. */
void set_candidate(void)
{
    const le_candidate_T *cand;
    const wchar_t *value;
    size_t length;

    if (le_selected_candidate_index >= le_candidates.length) {
	cand = le_candidates.contents[0];
	value = cand->value + expanded_source_word_length;
	length = common_prefix_length - expanded_source_word_length;
    } else {
	cand = le_candidates.contents[le_selected_candidate_index];
	value = cand->value + expanded_source_word_length;
	length = wcslen(value);
    }

    wb_replace_force(&le_main_buffer,
	    insertion_index, le_main_index - insertion_index, value, length);
    le_main_index = insertion_index + length;
}

/* Appends a space, slash, or something that should come after the completed
 * word. Must be called only when there is exactly one candidate. */
void finish_word(void)
{
    //TODO
    const wchar_t *finisher = L" ";
    size_t length = wcslen(finisher);

    wb_ninsert_force(&le_main_buffer, le_main_index, finisher, length);
    le_main_index += length;
}

/* Prints the formatted string to the standard error.
 * The string is preceded by "[compdebug] " and followed by a newline. */
void compdebug(const char *format, ...)
{
    if (!shopt_le_compdebug)
	return;

    fputs("[compdebug] ", stderr);

    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);

    fputc('\n', stderr);
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
