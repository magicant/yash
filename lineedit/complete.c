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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#if HAVE_GETGRENT
# include <grp.h>
#endif
#if HAVE_GETHOSTENT
# include <netdb.h>
#endif
#if HAVE_GETPWENT
# include <pwd.h>
#endif
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <sys/stat.h>
#include "../builtin.h"
#include "../exec.h"
#include "../expand.h"
#include "../hashtable.h"
#include "../option.h"
#include "../parser.h"
#include "../path.h"
#include "../plist.h"
#include "../redir.h"
#include "../sig.h"
#include "../strbuf.h"
#include "../util.h"
#include "../variable.h"
#include "../xfnmatch.h"
#include "../yash.h"
#include "complete.h"
#include "compparse.h"
#include "display.h"
#include "editing.h"
#include "lineedit.h"
#include "terminfo.h"

#if HAVE_GETPWENT
# ifndef getpwent
extern struct passwd *getpwent(void);
# endif
# ifndef endpwent
extern void endpwent(void);
# endif
#endif
#if HAVE_GETGRENT
# ifndef getgrent
extern struct group *getgrent(void);
# endif
# ifndef endgrent
extern void endgrent(void);
# endif
#endif
#if HAVE_GETHOSTENT
# ifndef gethostent
extern struct hostent *gethostent(void);
# endif
# ifndef endhostent
extern void endhostent(void);
# endif
#endif


static void free_candidate(void *c)
    __attribute__((nonnull));
static void free_context(le_context_T *ctxt);
static void sort_candidates(void);
static int sort_candidates_cmp(const void *cp1, const void *cp2)
    __attribute__((nonnull));
static void print_context_info(const le_context_T *ctxt)
    __attribute__((nonnull));

struct cmdcandgen_T;
static const struct candgen_T *get_candgen(void);
static const struct candgen_T *get_candgen_cmdarg(void);
static const struct cmdcandgen_T *get_cmdcandgen(const wchar_t *cmdname)
    __attribute__((nonnull,pure));
static void autoload_completion(const wchar_t *cmdname)
    __attribute__((nonnull));
static const struct candgen_T *get_candgen_parse_pwords(
	const struct cmdcandgen_T *ccg)
    __attribute__((nonnull));
static const struct candgen_T *get_candgen_default(void);
static const struct candgen_T *get_candgen_option(void);
static const struct candgen_T *get_candgen_operands(
	const struct candgen_T *candgen)
    __attribute__((nonnull));
static void skip_prefix(size_t len);
static int generate_candidates(const struct candgen_T *candgen)
    __attribute__((nonnull));
static void generate_file_candidates(
	le_candgentype_T type, const wchar_t *pattern)
    __attribute__((nonnull));
static void generate_external_command_candidates(
	le_candgentype_T type, le_context_T *context)
    __attribute__((nonnull));
static void generate_keyword_candidates(
	le_candgentype_T type, le_context_T *context)
    __attribute__((nonnull));
static void generate_option_candidates(
	le_candgentype_T type, le_context_T *context)
    __attribute__((nonnull));
static wchar_t *malloc_shortopt(wchar_t shortoptchar)
    __attribute__((malloc,warn_unused_result));
static wchar_t *malloc_shortopt_nh(wchar_t shortoptchar)
    __attribute__((malloc,warn_unused_result));
static void generate_logname_candidates(
        le_candgentype_T type, le_context_T *context)
    __attribute__((nonnull));
static void generate_group_candidates(
        le_candgentype_T type, le_context_T *context)
    __attribute__((nonnull));
static void generate_host_candidates(
        le_candgentype_T type, le_context_T *context)
    __attribute__((nonnull));
static void generate_candidates_from_words(
	void *const *words, le_context_T *context)
    __attribute__((nonnull(2)));

static size_t get_common_prefix_length(void)
    __attribute__((pure));
static void update_main_buffer(bool subst, bool finish);
static bool need_subst(void);
static void substitute_source_word_all(void);
static void quote(xwcsbuf_T *restrict buf,
	const wchar_t *restrict s, le_quote_T quotetype)
    __attribute__((nonnull));

/* The current completion context. */
static le_context_T *ctxt = NULL;

/* A list that contains the current completion candidates.
 * The elements pointed to by `le_candidates.contains[*]' are of type
 * `le_candidate_T'. */
plist_T le_candidates = { .contents = NULL };
/* The index of the currently selected candidate in `le_candidates'.
 * When no candidate is selected, the index is `le_candidates.length'. */
size_t le_selected_candidate_index;

/* The length of the longest common prefix of the current candidates.
 * The value is ((size_t) -1) when not computed. */
static size_t common_prefix_length;


/* Performs command line completion.
 * Existing candidates are deleted, if any, and candidates are computed from
 * the current command line.
 * `lecr' is called after candidate generation. */
void le_complete(le_compresult_T lecr)
{
    if (shopt_le_compdebug) {
	/* If the `le-compdebug' option is set, the command line is temporarily
	 * cleared during completion.
	 * Note that `shopt_le_compdebug' is referenced only here. During the
	 * completion, we check the value of `le_state' to test if the option
	 * is set. The value of `shopt_le_compdebug' might be changed by a
	 * candidate generator code. */
	le_display_finalize();
	le_restore_terminal();
	le_state = LE_STATE_SUSPENDED | LE_STATE_COMPLETING;
	le_compdebug("completion start");
    } else {
	le_state |= LE_STATE_COMPLETING;
	le_allow_terminal_signal(true);
    }

    le_complete_cleanup();
    pl_init(&le_candidates);
    common_prefix_length = (size_t) -1;

    ctxt = le_get_context();
    if (le_state_is_compdebug)
	print_context_info(ctxt);

    generate_candidates(get_candgen());
    sort_candidates();
    le_compdebug("total of %zu candidate(s)", le_candidates.length);

    /* display the results */
    lecr();

    if (le_state_is_compdebug) {
	le_compdebug("completion end");
	le_setupterm(true);
	le_set_terminal();
    } else {
	assert((le_state & (LE_STATE_ACTIVE | LE_STATE_COMPLETING))
			== (LE_STATE_ACTIVE | LE_STATE_COMPLETING));
	le_allow_terminal_signal(false);

	/* the terminal size may have been changed during completion, so we
	 * re-check the terminal state here. */
	le_display_clear(false);
	le_setupterm(true);
    }
    le_state = LE_STATE_ACTIVE;
}

/* An `le_compresult_T' function that does nothing. */
void lecr_nop(void)
{
}

/* An `le_compresult_T' function for `cmd_complete'. */
void lecr_normal(void)
{
    if (le_candidates.length == 0) {
	le_selected_candidate_index = 0;
    } else if (ctxt->substsrc || need_subst()) {
	le_selected_candidate_index = 0;
	substitute_source_word_all();
	le_complete_cleanup();
    } else if (le_candidates.length == 1) {
	le_selected_candidate_index = 0;
	update_main_buffer(false, true);
	le_complete_cleanup();
    } else {
	le_selected_candidate_index = le_candidates.length;
	le_display_make_rawvalues();
	update_main_buffer(false, false);
    }
}

/* An `le_compresult_T' function for `cmd_vi_complete_all'. */
void lecr_substitute_all_candidates(void)
{
    le_selected_candidate_index = 0;
    if (le_candidates.length == 0) {
	lebuf_print_alert(true);
    } else {
	substitute_source_word_all();
    }
    le_complete_cleanup();
}

/* An `le_compresult_T' function for `cmd_vi_complete_max'. */
void lecr_longest_common_prefix(void)
{
    le_selected_candidate_index = 0;
    if (le_candidates.length == 0) {
	lebuf_print_alert(true);
    } else {
	bool subst = ctxt->substsrc || need_subst();
	if (le_candidates.length > 1) {
	    le_selected_candidate_index = le_candidates.length;
	    update_main_buffer(subst, false);
	} else {
	    update_main_buffer(subst, true);
	}
    }
    le_complete_cleanup();
}

/* Increases `le_selected_candidate_index' by `offset', selecting the `offset'th
 * next candidate. If there are no candidates, simply calls `le_complete' to
 * produce candidates. */
void le_complete_select_candidate(int offset)
{
    if (le_candidates.contents == NULL) {
	le_complete(lecr_normal);
	return;
    } else if (le_candidates.length == 0) {
	return;
    }

    assert(le_selected_candidate_index <= le_candidates.length);
    if (offset >= 0) {
	offset %= le_candidates.length + 1;
	le_selected_candidate_index += offset;
	le_selected_candidate_index %= le_candidates.length + 1;
    } else {
	offset = -offset % (le_candidates.length + 1);
	if ((size_t) offset <= le_selected_candidate_index)
	    le_selected_candidate_index -= offset;
	else
	    le_selected_candidate_index += le_candidates.length - offset + 1;
    }
    assert(le_selected_candidate_index <= le_candidates.length);

    update_main_buffer(false, false);
}

/* Selects the first candidate of the `offset'th next column.
 * If there are no candidates, simply calls `le_complete' to produce candidates.
 */
void le_complete_select_column(int offset)
{
    if (le_candidates.contents == NULL) {
	le_complete(lecr_normal);
	return;
    } else if (le_candidates.length == 0) {
	return;
    }

    le_selected_candidate_index = le_display_select_column(offset);
    update_main_buffer(false, false);
}

/* Selects the first candidate of the `offset'th next page.
 * If there are no candidates, simply calls `le_complete' to produce candidates.
 */
void le_complete_select_page(int offset)
{
    if (le_candidates.contents == NULL) {
	le_complete(lecr_normal);
	return;
    } else if (le_candidates.length == 0) {
	return;
    }

    le_selected_candidate_index = le_display_select_page(offset);
    update_main_buffer(false, false);
}

/* If `index' is not positive, performs completion and list candidates.
 * Otherwise, substitutes the source word with the `index'th candidate and
 * cleans up.
 * Returns true iff the source word was successfully substituted. */
bool le_complete_fix_candidate(int index)
{
    if (le_candidates.contents == NULL) {
	le_complete(lecr_nop);
	le_selected_candidate_index = le_candidates.length;
	le_display_make_rawvalues();
    }
    if (le_candidates.length == 0) {
	lebuf_print_alert(true);
	return false;
    }
    if (index <= 0)
	return false;

    unsigned uindex = (unsigned) index - 1;
    if (uindex >= le_candidates.length) {
	lebuf_print_alert(true);
	return false;
    }
    le_selected_candidate_index = uindex;

    bool subst = ctxt->substsrc;
    if (!subst) {
	const le_candidate_T *cand =
	    le_candidates.contents[le_selected_candidate_index];
	subst = !matchwcsprefix(cand->origvalue, ctxt->origsrc);
    }
    update_main_buffer(subst, true);
    le_complete_cleanup();
    return true;
}

/* Clears the current candidates. */
void le_complete_cleanup(void)
{
    le_display_complete_cleanup();
    if (le_candidates.contents != NULL) {
	recfree(pl_toary(&le_candidates), free_candidate);
	le_candidates.contents = NULL;
    }
    free_context(ctxt);
    ctxt = NULL;
}

/* Frees a completion candidate.
 * The argument must point to a `le_candidate_T' value. */
void free_candidate(void *c)
{
    le_candidate_T *cand = c;
    free(cand->origvalue);
    free(cand->rawvalue.raw);
    free(cand->desc);
    free(cand->rawdesc.raw);
    free(cand);
}

/* Frees the specified `le_context_T' data. */
void free_context(le_context_T *ctxt)
{
    if (ctxt != NULL) {
	recfree(ctxt->pwords, free);
	free(ctxt->origsrc);
	free(ctxt->origpattern);
	xfnm_free(ctxt->cpattern);
	free(ctxt);
    }
}

/* Sorts the candidates in the candidate list and remove duplicates. */
void sort_candidates(void)
{
    qsort(le_candidates.contents,
	    le_candidates.length, sizeof *le_candidates.contents,
	    sort_candidates_cmp);

    if (le_candidates.length >= 2) {
	for (size_t i = le_candidates.length - 1; i > 0; i--) {
	    le_candidate_T *cand1 = le_candidates.contents[i];
	    le_candidate_T *cand2 = le_candidates.contents[i - 1];
	    // XXX case-sensitive
	    if (wcscoll(cand1->origvalue, cand2->origvalue) == 0) {
		free_candidate(cand1);
		pl_remove(&le_candidates, i, 1);
	    }
	}
    }
}

int sort_candidates_cmp(const void *cp1, const void *cp2)
{
    const le_candidate_T *cand1 = *(const le_candidate_T **) cp1;
    const le_candidate_T *cand2 = *(const le_candidate_T **) cp2;
    return wcscoll(cand1->origvalue, cand2->origvalue);
    // XXX case-sensitive
}

/* Prints the formatted string to the standard error if the completion debugging
 * option is on.
 * The string is preceded by "[compdebug] " and followed by a newline. */
void le_compdebug(const char *format, ...)
{
    if (!le_state_is_compdebug)
	return;

    fputs("[compdebug] ", stderr);

    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);

    fputc('\n', stderr);
}

/* Prints information on the specified context if the `compdebug' option is
 * enabled. */
void print_context_info(const le_context_T *ctxt)
{
#ifdef NDEBUG
    const char *s;
#else
    const char *s = s;
#endif
    switch (ctxt->quote) {
	case QUOTE_NONE:    s = "none";    break;
	case QUOTE_NORMAL:  s = "normal";  break;
	case QUOTE_SINGLE:  s = "single";  break;
	case QUOTE_DOUBLE:  s = "double";  break;
    }
    le_compdebug("quote type: %s", s);
    switch (ctxt->type & CTXT_MASK) {
	case CTXT_NORMAL:        s = "normal";                   break;
	case CTXT_COMMAND:       s = "command";                  break;
	case CTXT_TILDE:         s = "tilde";                    break;
	case CTXT_VAR:           s = "variable";                 break;
	case CTXT_ARITH:         s = "arithmetic";               break;
	case CTXT_ASSIGN:        s = "assignment";               break;
	case CTXT_REDIR:         s = "redirection";              break;
	case CTXT_REDIR_FD:      s = "redirection (fd)";         break;
	case CTXT_FOR_IN:        s = "\"in\" or \"do\"";         break;
	case CTXT_FOR_DO:        s = "\"do\"";                   break;
	case CTXT_CASE_IN:       s = "\"in\"";                   break;
    }
    le_compdebug("context type: %s%s%s%s", s,
	    ctxt->type & CTXT_EBRACED ? " (in brace expn)" : "",
	    ctxt->type & CTXT_VBRACED ? " (in variable)" : "",
	    ctxt->type & CTXT_QUOTED ? " (quoted)" : "");
    for (int i = 0; i < ctxt->pwordc; i++)
	le_compdebug("preceding word %d: \"%ls\"",
		i + 1, (const wchar_t *) ctxt->pwords[i]);
    le_compdebug("source word: \"%ls\"", ctxt->src);
    le_compdebug(" as pattern: \"%ls\"", ctxt->pattern);
}


/********** Completion Style Determination **********/

#define WMATCH(pattern, s) (xfnm_wmatch(pattern, s).start != (size_t) -1)

/* This structure specifies how to generate completion candidates.
 * The `type' member is bitwise OR of CGT_* values.
 * The `words' member is a pointer to an array of pointers to wide strings that
 * are added as candidates. These strings, after their terminating null
 * character, are immediately followed by a description of the candidate.
 * The `function' member is the name of the function that is called to generate
 * candidates.
 * The `words' and `function' members may be NULL. */
struct candgen_T {
    le_candgentype_T type;
    void **words;
    wchar_t *function;
};

/* This structure specifies the completion style for an option of a command.
 * The `description' member is a description of the option (may be NULL).
 * The `shortoptchar' member is the short-option character.
 * The `longopt' member is the name of this option as a long option, starting
 * with at least one hyphen.
 * Either the `shortoptchar' or `longopt' member (but not both) may be null.
 * The `requiresargument' member is true iff this option requires an argument.
 * The value of the `candgen' member is valid only when `requiresargument' is
 * true. */
struct optcandgen_T {
    struct optcandgen_T *next;
    wchar_t *description;
    wchar_t shortoptchar;
    wchar_t *longopt;
    bool requiresargument;
    struct candgen_T candgen;
};

/* This structure specifies the completion style for a command.
 * The `description' member is a description of the command (may be NULL).
 * The `operands' member is used for completing a operand for the command.
 *
 * The `options' member is a pointer to a linked list of `optcandgen_T'
 * structures specifying the options for the command and the completion styles
 * for their arguments.
 * If the command has no options, `options' is NULL.
 *
 * The `intermixed' member specifies whether options can appear after
 * operands or not. If this member is false, all arguments after the first
 * operand are considered as operands. */
struct cmdcandgen_T {
    wchar_t *description;
    struct candgen_T operands;
    struct optcandgen_T *options;
    bool intermixed;
};

/* A hashtable that maps command names to candidate generation data.
 * A key is a pointer to a freeable wide string containing a command name.
 * A value is a pointer to a freeable `cmdcandgen_T' structure.
 * When the hashtable is not yet initialized, the capacity is zero. */
static hashtable_T candgens = { .capacity = 0 };

/* The completion style returned by `get_candgen'. */
static struct candgen_T tmpcandgen;

/* Determines how to generate candidates under context `ctxt'.
 * The result is valid until the next call to this function or
 * `complete_builtin_set'.
 * This function may change `ctxt->src', `ctxt->pattern'. */
const struct candgen_T *get_candgen(void)
{
    static void *in_do[] = { L"in", L"do", NULL };
    static void *in[] = { L"in", NULL };

    tmpcandgen = (struct candgen_T) {
	.type = 0, .words = NULL, .function = NULL };
    switch (ctxt->type & CTXT_MASK) {
	case CTXT_NORMAL:
	    return get_candgen_cmdarg();
	case CTXT_COMMAND:
	    if (wcschr(ctxt->src, L'/')) {
		tmpcandgen.type |= CGT_DIRECTORY | CGT_EXECUTABLE;
	    } else {
		tmpcandgen.type |= CGT_DIRECTORY | CGT_COMMAND;
		if (ctxt->quote == QUOTE_NORMAL
			&& !wcschr(ctxt->pattern, L'\\'))
		    tmpcandgen.type |= CGT_KEYWORD | CGT_NALIAS;
	    }
	    return &tmpcandgen;
	case CTXT_TILDE:
	    tmpcandgen.type |= CGT_LOGNAME;
	    return &tmpcandgen;
	case CTXT_VAR:
	    tmpcandgen.type |= CGT_VARIABLE;
	    return &tmpcandgen;
	case CTXT_ARITH:
	    tmpcandgen.type |= CGT_SCALAR;
	    return &tmpcandgen;
	case CTXT_ASSIGN:
	    tmpcandgen.type |= CGT_FILE;
	    return &tmpcandgen;
	case CTXT_REDIR:
	    tmpcandgen.type |= CGT_FILE;
	    return &tmpcandgen;
	case CTXT_REDIR_FD:
	    return &tmpcandgen;
	case CTXT_FOR_IN:
	    tmpcandgen.words = in_do;
	    return &tmpcandgen;
	case CTXT_FOR_DO:
	    tmpcandgen.words = in_do + 1;
	    return &tmpcandgen;
	case CTXT_CASE_IN:
	    tmpcandgen.words = in;
	    return &tmpcandgen;
    }
    assert(false);
}

/* Parses the already-entered command words and determines how to complete the
 * current word.
 * This function may change `ctxt->src', `ctxt->pattern'. */
const struct candgen_T *get_candgen_cmdarg(void)
{
    const struct cmdcandgen_T *ccg;
    const struct optcandgen_T *ocg;
    if (candgens.capacity == 0
	    || (ccg = get_cmdcandgen(ctxt->pwords[0])) == NULL) {
	autoload_completion(ctxt->pwords[0]);
	if (candgens.capacity == 0
		|| (ccg = get_cmdcandgen(ctxt->pwords[0])) == NULL)
	    return get_candgen_default();
    }

    const struct candgen_T *result = get_candgen_parse_pwords(ccg);
    if (result != NULL)
	return result;

    /* if the source word is not an option, simply complete as an operand */
    if (ccg->options == NULL || ctxt->src[0] != L'-')
	return get_candgen_operands(&ccg->operands);
    if (ctxt->src[1] == L'\0')
	return get_candgen_option();

    /* parse the source word to find how we should complete an option */
    /* first check for long options */
    const struct optcandgen_T *match = NULL;
    size_t skiplen = 0;
    for (ocg = ccg->options; ocg != NULL; ocg = ocg->next) {
	const wchar_t *ss = ctxt->src, *longopt = ocg->longopt;
	if (longopt == NULL)
	    continue;
	assert(ss[0] == L'-');
	assert(longopt[0] == L'-');

	/* compare `ss' and `longopt' */
	do {
	    ss++, longopt++;
	    if (*ss == L'\0')
		return get_candgen_option();
	} while (*ss == *longopt);

	if (*ss == L'=' && ocg->requiresargument) {
	    if (*longopt == L'\0') {  /* exact match */
		match = ocg, skiplen = ss - ctxt->src + 1;
		break;
	    }
	    if (ctxt->src[1] != L'-')
		continue;  /* single-hyphened option must match exactly */
	    match = ocg, skiplen = ss - ctxt->src + 1;
	}
    }
    if (match != NULL) {
	assert(match->requiresargument);
	assert(skiplen > 0);
	skip_prefix(skiplen);
	return &match->candgen;
    }

    /* next check for short options */
    const wchar_t *ss = ctxt->src;
    if (ss[1] == L'-')
	return get_candgen_option();
    while (*++ss != L'\0') {
	for (ocg = ccg->options; ocg != NULL; ocg = ocg->next) {
	    if (*ss == ocg->shortoptchar) {
		if (ocg->requiresargument) {
		    skip_prefix(ss - ctxt->src + 1);
		    return &ocg->candgen;
		} else {
		    break;
		}
	    }
	}
    }
    skip_prefix(ss - ctxt->src);
    return get_candgen_option();
}

/* Search `candgens' for the specified command.
 * If `cmdname' contains a slash and if the command was not found for the whole
 * pathname, search is done for the last path component. */
const struct cmdcandgen_T *get_cmdcandgen(const wchar_t *cmdname)
{
    const struct cmdcandgen_T *result;
    result = ht_get(&candgens, cmdname).value;
    if (result == NULL) {
	const wchar_t *slash = wcsrchr(cmdname, L'/');
	if (slash != NULL)
	    result = ht_get(&candgens, slash + 1).value;
    }
    return result;
}

/* Executes the file in $YASH_COMPPATH to load completion configuration
 * for the specified command. */
void autoload_completion(const wchar_t *cmdname)
{
    const wchar_t *slash = wcsrchr(cmdname, L'/');
    if (slash != NULL)
	cmdname = slash + 1;

    char *mbscmdname;
    /* If `cmdname' is L"." or L"..", use "_." or "_.." for the filename */
    if (wcscmp(cmdname, L".") == 0)
	mbscmdname = xstrdup("_.");
    else if (wcscmp(cmdname, L"..") == 0)
	mbscmdname = xstrdup("_..");
    else
	mbscmdname = malloc_wcstombs(cmdname);
    if (mbscmdname == NULL)
	return;

    char *path = which(mbscmdname,
	    get_path_array(PA_COMPPATH), is_readable_regular);
    free(mbscmdname);
    if (path == NULL)
	return;

    int fd = move_to_shellfd(open(path, O_RDONLY));
    if (fd >= 0) {
	struct parsestate_T *state = save_parse_state();
	int savelaststatus = laststatus;
	bool save_posixly_correct = posixly_correct;
	posixly_correct = false;
	open_new_environment(false);
	set_positional_parameters((void *[]) { (void *) cmdname, NULL, });
	set_variable(L VAR_IFS, xwcsdup(DEFAULT_IFS), SCOPE_LOCAL, false);

	le_compdebug("autoload: start executing `%s'", path);
	exec_input(fd, path, false, false, false);
	le_compdebug("autoload: finished executing `%s'", path);
	if (laststatus != Exit_SUCCESS)
	    le_compdebug("          with exit status of %d", laststatus);

	close_current_environment();
	laststatus = savelaststatus;
	posixly_correct = save_posixly_correct;
	restore_parse_state(state);
	remove_shellfd(fd);
	xclose(fd);
    }
    free(path);
}

/* Parses `ctxt->pwords'. */
const struct candgen_T *get_candgen_parse_pwords(const struct cmdcandgen_T *ccg)
{
    const struct optcandgen_T *ocg;

    for (int i = 1; i < ctxt->pwordc; i++) {
	const wchar_t *s = ctxt->pwords[i];
	if (s[0] != L'-' || s[1] == L'\0') {
	    if (ccg->intermixed)
		continue;
	    else
		return get_candgen_operands(&ccg->operands);
	} else if (s[1] == L'-' && s[2] == L'\0') {  /* s == L"--" */
	    return get_candgen_operands(&ccg->operands);
	}

	/* first check if the word is a long option */
	const struct optcandgen_T *match = NULL;
	wchar_t matchend = L'\0';
	for (ocg = ccg->options; ocg != NULL; ocg = ocg->next) {
	    const wchar_t *ss = s, *longopt = ocg->longopt;
	    if (longopt == NULL)
		continue;
	    assert(ss[0] == L'-');
	    assert(longopt[0] == L'-');

	    do  /* compare `ss' and `longopt' */
		ss++, longopt++;
	    while (*ss != L'\0' && *ss == *longopt);

	    if (*ss == L'\0' || (*ss == L'=' && ocg->requiresargument)) {
		if (*longopt == L'\0') {  /* exact match */
		    match = ocg, matchend = *ss;
		    break;
		}
		if (s[1] != L'-')
		    continue;  /* single-hyphened option must match exactly */
		if (match == NULL)
		    match = ocg, matchend = *ss;
		else
		    goto next_word;  /* ambiguous match */
	    }
	}
	if (match != NULL) {
	    if (matchend == L'\0' && match->requiresargument) {
		i++;  /* option argument is in the next word */
		if (i == ctxt->pwordc)
		    return &match->candgen;
	    }
	    goto next_word;
	}

	/* next check for short options */
	if (s[1] != L'-') {
	    for (const wchar_t *ss = s + 1; *ss != L'\0'; ss++) {
		for (ocg = ccg->options; ocg != NULL; ocg = ocg->next) {
		    if (*ss == ocg->shortoptchar) {
			if (ocg->requiresargument) {
			    if (*(ss + 1) == L'\0')
				if (++i == ctxt->pwordc)
				    return &ocg->candgen;
			    goto next_word;
			} else {
			    break;
			}
		    }
		}
	    }
	}
next_word:;
    }

    return NULL;
}

const struct candgen_T *get_candgen_default(void)
{
    tmpcandgen.type = CGT_FILE;
    return &tmpcandgen;
}

const struct candgen_T *get_candgen_option(void)
{
    tmpcandgen.type = CGT_OPTION;
    return &tmpcandgen;
}

const struct candgen_T *get_candgen_operands(const struct candgen_T *candgen)
{
    if (!candgen->type
	    && (candgen->words == NULL || candgen->words[0] == NULL)
	    && (candgen->function == NULL))
	return get_candgen_default();
    return candgen;
}

/* Increases `ctxt->src' by `len' and `ctxt->pattern' accordingly. */
void skip_prefix(size_t len)
{
    assert(len <= wcslen(ctxt->src));

    ctxt->src += len;

    while (len > 0) {
	if (ctxt->pattern[0] == L'\\')
	    ctxt->pattern++;
	if (ctxt->pattern[0] != L'\0')
	    ctxt->pattern++;
	len--;
    }

    le_compdebug("new source word: \"%ls\"", ctxt->src);
    le_compdebug("     as pattern: \"%ls\"", ctxt->pattern);
}


/********** Completion Candidate Generation **********/

/* Generates completion candidates according to the specified completion style.
 * The candidate list must have been initialized when this function is called.
 * Returns the exit status of the candidate generator function. */
int generate_candidates(const struct candgen_T *candgen)
{
    generate_file_candidates(candgen->type, ctxt->pattern);
    generate_builtin_candidates(candgen->type, ctxt);
    generate_external_command_candidates(candgen->type, ctxt);
    generate_function_candidates(candgen->type, ctxt);
    generate_keyword_candidates(candgen->type, ctxt);
    generate_option_candidates(candgen->type, ctxt);
#if YASH_ENABLE_ALIAS
    generate_alias_candidates(candgen->type, ctxt);
#endif
    generate_variable_candidates(candgen->type, ctxt);
    generate_job_candidates(candgen->type, ctxt);
    generate_signal_candidates(candgen->type, ctxt);
    generate_logname_candidates(candgen->type, ctxt);
    generate_group_candidates(candgen->type, ctxt);
    generate_host_candidates(candgen->type, ctxt);
    generate_bindkey_candidates(candgen->type, ctxt);
    generate_candidates_from_words(candgen->words, ctxt);
    return generate_candidates_using_function(candgen->function, ctxt);
    /* `generate_candidates_using_function' must be last because the function
     * execution may modify `candgen'. */
}

/* Adds a CT_COMMAND candidate with the specified name to the candidate list.
 * The description for the candidate is looked up from the registered completion
 * style.
 * Argument `cmdname' must be a freeable string, which is used as the candidate
 * value. */
void le_new_command_candidate(wchar_t *cmdname)
{
    wchar_t *desc = NULL;
    if (candgens.capacity != 0) {
	const struct cmdcandgen_T *ccg = get_cmdcandgen(cmdname);
	if (ccg != NULL && ccg->description) {
	    desc = xwcsdup(ccg->description);
	}
    }

    le_new_candidate(CT_COMMAND, cmdname, desc);
}

/* Adds the specified value as a completion candidate to the candidate list.
 * The ignored prefix in `ctxt->origsrc' is prepended to the candidate value.
 * A description for the candidate can be given as `desc', which may be NULL
 * when no description is provided.
 * Arguments `value' and `desc' must be a freeable string, which is used as the
 * candidate value/description.
 * This function must NOT be used for a CT_FILE/CT_OPTION/CT_OPTIONA candidate.
 * If `value' is NULL, this function does nothing (except freeing `desc'). */
void le_new_candidate(le_candtype_T type, wchar_t *value, wchar_t *desc)
{
    if (value == NULL) {
	free(desc);
	return;
    }
    if (desc != NULL && (desc[0] == L'\0' || wcscmp(value, desc) == 0)) {
	/* ignore useless description */
	free(desc);
	desc = NULL;
    }

    le_candidate_T *cand = xmalloc(sizeof *cand);
    cand->type = type;
    cand->value = value;
    cand->rawvalue.raw = NULL;
    cand->rawvalue.width = 0;
    cand->desc = desc;
    cand->rawdesc.raw = NULL;
    cand->rawdesc.width = 0;
    le_add_candidate(cand);
}

/* Adds the specified candidate to the candidate list.
 * The le_candidate_T structure must have been properly initialized, except for
 * the `origvalue' member, which is initialized in this function as the value
 * with the ignored prefixed prepended. */
void le_add_candidate(le_candidate_T *cand)
{
    if (ctxt->origsrc == ctxt->src) {
	cand->origvalue = cand->value;
    } else {
	size_t prefixlen = ctxt->src - ctxt->origsrc;
	xwcsbuf_T buf;
	wb_initwith(&buf, cand->value);
	wb_ninsert_force(&buf, 0, ctxt->origsrc, prefixlen);
	cand->origvalue = wb_towcs(&buf);
	cand->value = cand->origvalue + prefixlen;
    }

    if (le_state_is_compdebug) {
	const char *typestr = NULL;
	switch (cand->type) {
	    case CT_WORD:      typestr = "word";                       break;
	    case CT_FILE:      typestr = "file";                       break;
	    case CT_COMMAND:   typestr = "command";                    break;
	    case CT_ALIAS:     typestr = "alias";                      break;
	    case CT_OPTION:    typestr = "option";                     break;
	    case CT_OPTIONA:   typestr = "argument-requiring option";  break;
	    case CT_VAR:       typestr = "variable";                   break;
	    case CT_JOB:       typestr = "job";                        break;
	    case CT_FD:        typestr = "file descriptor";            break;
	    case CT_SIG:       typestr = "signal";                     break;
	    case CT_LOGNAME:   typestr = "user name";                  break;
	    case CT_GRP:       typestr = "group name";                 break;
	    case CT_HOSTNAME:  typestr = "host name";                  break;
	    case CT_BINDKEY:   typestr = "lineedit command";           break;
	}
	le_compdebug("new %s candidate \"%ls\"", typestr, cand->origvalue);
	if (cand->desc != NULL)
	    le_compdebug("  (desc: %ls)", cand->desc);
    }

    pl_add(&le_candidates, cand);
}

/* Compiles the pattern in the specified context if not yet compiled.
 * Returns true iff successful. */
bool le_compile_cpattern(le_context_T *context)
{
    if (context->cpattern != NULL)
	return true;

    context->cpattern = xfnm_compile(
	    context->pattern, XFNM_HEADONLY | XFNM_TAILONLY);
    if (context->cpattern == NULL) {
	le_compdebug("failed to compile pattern \"%ls\"", context->pattern);
	return false;
    }
    return true;
}

/* Generates file name candidates that match the specified glob pattern.
 * The CGT_FILE, CGT_DIRECTORY, and CGT_EXECUTABLE flags specify what candidate
 * to generate. The other flags are ignored. */
void generate_file_candidates(le_candgentype_T type, const wchar_t *pattern)
{
    if (!(type & (CGT_FILE | CGT_DIRECTORY | CGT_EXECUTABLE)))
	return;

    le_compdebug("adding filenames for pattern \"%ls\"", pattern);

    enum wglbflags flags = WGLB_NOSORT;
    // if (shopt_nocaseglob)   flags |= WGLB_CASEFOLD;  XXX case-sensitive
    if (shopt_dotglob)      flags |= WGLB_PERIOD;
    if (shopt_extendedglob) flags |= WGLB_RECDIR;

    plist_T list;
    wglob(pattern, flags, pl_init(&list));

    for (size_t i = 0; i < list.length; i++) {
	wchar_t *name = list.contents[i];
	char *mbsname = malloc_wcstombs(name);
	struct stat st;
	if (mbsname != NULL &&
		(stat(mbsname, &st) >= 0 || lstat(mbsname, &st) >= 0)) {
	    bool executable = S_ISREG(st.st_mode) && is_executable(mbsname);
	    if ((type & CGT_FILE)
		    || ((type & CGT_DIRECTORY) && S_ISDIR(st.st_mode))
		    || ((type & CGT_EXECUTABLE) && executable)) {
		le_candidate_T *cand = xmalloc(sizeof *cand);
		cand->type = CT_FILE;
		cand->value = list.contents[i];
		cand->rawvalue.raw = NULL;
		cand->rawvalue.width = 0;
		cand->desc = NULL;
		cand->rawdesc.raw = NULL;
		cand->rawdesc.width = 0;
		cand->appendage.filestat.is_executable = executable;
		cand->appendage.filestat.mode = st.st_mode;
		cand->appendage.filestat.nlink = st.st_nlink;
		cand->appendage.filestat.size = st.st_size;
		le_add_candidate(cand);
		name = NULL;
	    }
	}
	free(name);
	free(mbsname);
    }
    pl_destroy(&list);
}

/* Generates candidates that are the names of external commands matching the
 * pattern in the specified context.
 * If CGT_EXECUTABLE is not in `type', this function does nothing. */
void generate_external_command_candidates(
	le_candgentype_T type, le_context_T *context)
{
    if (!(type & CGT_EXTCOMMAND))
	return;

    le_compdebug("adding external command names for pattern \"%ls\"",
	    context->pattern);
    if (!le_compile_cpattern(context))
	return;

    char *const *paths = get_path_array(PA_PATH);
    xstrbuf_T path;

    if (paths == NULL)
	return;
    sb_init(&path);
    for (const char *dirpath; (dirpath = *paths) != NULL; paths++) {
	DIR *dir = opendir(dirpath);
	struct dirent *de;
	size_t dirpathlen;

	if (dir == NULL)
	    continue;
	sb_cat(&path, dirpath);
	if (path.length > 0 && path.contents[path.length - 1] != '/')
	    sb_ccat(&path, '/');
	dirpathlen = path.length;
	while ((de = readdir(dir)) != NULL) {
	    if (xfnm_match(context->cpattern, de->d_name) != 0)
		continue;
	    sb_cat(&path, de->d_name);
	    if (is_executable_regular(path.contents))
		le_new_command_candidate(malloc_mbstowcs(de->d_name));
	    sb_truncate(&path, dirpathlen);
	}
	sb_clear(&path);
	closedir(dir);
    }
    sb_destroy(&path);
}

/* Generates candidates that are keywords matching the pattern in the specified
 * context. */
void generate_keyword_candidates(le_candgentype_T type, le_context_T *context)
{
    if (!(type & CGT_KEYWORD))
	return;

    le_compdebug("adding keywords matching pattern \"%ls\"", context->pattern);
    if (!le_compile_cpattern(context))
	return;

    static const wchar_t *keywords[] = {
	L"case", L"do", L"done", L"elif", L"else", L"esac", L"fi", L"for",
	L"function", L"if", L"then", L"until", L"while", NULL,
	// XXX "select" is not currently supported
    };

    for (const wchar_t **k = keywords; *k != NULL; k++)
	if (WMATCH(context->cpattern, *k))
	    le_new_command_candidate(xwcsdup(*k));
}

/* Generates candidates to complete an option matching the pattern in the
 * specified context. */
void generate_option_candidates(le_candgentype_T type, le_context_T *context)
{
    const wchar_t *src = context->src;
    if (!(type & CGT_OPTION))
	return;

    le_compdebug("adding options matching pattern \"%ls\"", context->pattern);
    if (candgens.capacity == 0)
	return;
    if (!le_compile_cpattern(context))
	return;

    const struct cmdcandgen_T *ccg;
    const struct optcandgen_T *ocg;
    ccg = get_cmdcandgen(context->pwords[0]);
    if (ccg == NULL)
	return;

    bool allowshort = (src[0] != L'-') || (src[1] == L'\0');
    bool allowlong  = (src[0] == L'-');
    for (ocg = ccg->options; ocg != NULL; ocg = ocg->next) {
	bool shortmatch = allowshort && (ocg->shortoptchar != L'\0');
	bool longmatch = allowlong && (ocg->longopt != NULL)
	    && WMATCH(context->cpattern, ocg->longopt);
	if (shortmatch || longmatch) {
	    le_candidate_T *cand = xmalloc(sizeof *cand);
	    cand->type = CT_OPTION;
	    if (shortmatch) {  // XXX user-preference
		if (allowlong)
		    cand->value = malloc_shortopt(ocg->shortoptchar);
		else
		    cand->value = malloc_shortopt_nh(ocg->shortoptchar);
		if (ocg->longopt != NULL)
		    if (ocg->description != NULL)
			cand->desc = malloc_wprintf(L"%ls: %ls",
				ocg->longopt, ocg->description);
		    else
			cand->desc = xwcsdup(ocg->longopt);
		else
		    goto desc;
	    } else {
		assert(ocg->longopt[0] == L'-');
		if (ocg->longopt[1] == L'-' && ocg->requiresargument)
		    cand->type = CT_OPTIONA;
		cand->value = xwcsdup(ocg->longopt);
		if (ocg->shortoptchar != L'\0')
		    if (ocg->description != NULL)
			cand->desc = malloc_wprintf(L"-%lc: %ls",
				(wint_t) ocg->shortoptchar, ocg->description);
		    else
			cand->desc = malloc_shortopt(ocg->shortoptchar);
		else
desc:
		    if (ocg->description != NULL)
			cand->desc = xwcsdup(ocg->description);
		    else
			cand->desc = NULL;
	    }
	    cand->rawvalue.raw = NULL;
	    cand->rawvalue.width = 0;
	    cand->rawdesc.raw = NULL;
	    cand->rawdesc.width = 0;
	    le_add_candidate(cand);
	}
    }
}

/* Returns a newly malloced string L"-x" where `x' is `shortoptchar'. */
wchar_t *malloc_shortopt(wchar_t shortoptchar)
{
    wchar_t *result = xmalloc(3 * sizeof *result);
    result[0] = L'-';
    result[1] = shortoptchar;
    result[2] = L'\0';
    return result;
}

/* Returns a newly malloced string containing `shortoptchar' only. */
wchar_t *malloc_shortopt_nh(wchar_t shortoptchar)
{
    wchar_t *result = xmalloc(2 * sizeof *result);
    result[0] = shortoptchar;
    result[1] = L'\0';
    return result;
}

/* Generates candidates to complete a user name matching the pattern in the
 * specified context. */
void generate_logname_candidates(le_candgentype_T type, le_context_T *context)
{
    if (!(type & CGT_LOGNAME))
	return;

    le_compdebug("adding users matching pattern \"%ls\"", context->pattern);

#if HAVE_GETPWENT
    if (!le_compile_cpattern(context))
	return;

    struct passwd *pwd;
    while ((pwd = getpwent()) != NULL)
	if (xfnm_match(context->cpattern, pwd->pw_name) == 0)
	    le_new_candidate(CT_LOGNAME, malloc_mbstowcs(pwd->pw_name),
# if HAVE_PW_GECOS
		    (pwd->pw_gecos != NULL) ? malloc_mbstowcs(pwd->pw_gecos) :
# endif
		    NULL);
    endpwent();
#else
    le_compdebug("  getpwent not supported on this system");
#endif
}

/* Generates candidates to complete a group name matching the pattern in the
 * specified context. */
void generate_group_candidates(le_candgentype_T type, le_context_T *context)
{
    if (!(type & CGT_GROUP))
	return;

    le_compdebug("adding groups matching pattern \"%ls\"", context->pattern);

#if HAVE_GETGRENT
    if (!le_compile_cpattern(context))
	return;

    struct group *grp;
    while ((grp = getgrent()) != NULL)
	if (xfnm_match(context->cpattern, grp->gr_name) == 0)
	    le_new_candidate(CT_GRP, malloc_mbstowcs(grp->gr_name), NULL);
    endgrent();
#else
    le_compdebug("  getgrent not supported on this system");
#endif
}

/* Generates candidates to complete a host name matching the pattern in the
 * specified context. */
void generate_host_candidates(le_candgentype_T type, le_context_T *context)
{
    if (!(type & CGT_HOSTNAME))
	return;

    le_compdebug("adding hosts matching pattern \"%ls\"", context->pattern);

#if HAVE_GETHOSTENT
    if (!le_compile_cpattern(context))
	return;

    struct hostent *host;
    sethostent(true);
    while ((host = gethostent()) != NULL) {
	if (xfnm_match(context->cpattern, host->h_name) == 0)
	    le_new_candidate(CT_HOSTNAME, malloc_mbstowcs(host->h_name), NULL);
	if (host->h_aliases != NULL)
	    for (char *const *a = host->h_aliases; *a != NULL; a++)
		if (xfnm_match(context->cpattern, *a) == 0)
		    le_new_candidate(CT_HOSTNAME, malloc_mbstowcs(*a), NULL);
    }
    endhostent();
#else
    le_compdebug("  gethostent not supported on this system");
#endif
}

/* Generates candidates from words that match the pattern in the specified
 * context. */
void generate_candidates_from_words(void *const *words, le_context_T *context)
{
    if (words == NULL)
	return;

    le_compdebug("adding predefined words for pattern \"%ls\"",
	    context->pattern);
    if (!le_compile_cpattern(context))
	return;

    for (; *words != NULL; words++) {
	wchar_t *word = *words;
	if (WMATCH(context->cpattern, word)) {
	    const wchar_t *desc = word + wcslen(word) + 1;
	    le_new_candidate(CT_WORD, xwcsdup(word),
		    (desc[0] == L'\0') ? NULL : xwcsdup(desc));
	}
    }
}


/********** Displaying Functions **********/

/* Calculates the length of the longest common prefix (leading substring)
 * for the current candidates.
 * The result includes the ignored prefix in the candidate values.
 * The result is saved in `common_prefix_length'.
 * There must be at least one candidate in `le_candidates'. */
size_t get_common_prefix_length(void)
{
    assert(le_candidates.contents != NULL);
    assert(le_candidates.length > 0);

    if (common_prefix_length != (size_t) -1)
	return common_prefix_length;

    const le_candidate_T *cand = le_candidates.contents[0];
    const wchar_t *value = cand->origvalue;
    size_t cpl = wcslen(value);
    for (size_t i = 1; i < le_candidates.length; i++) {
	cand = le_candidates.contents[i];
	const wchar_t *value2 = cand->origvalue;
	for (size_t j = 0; j < cpl; j++)
	    if (value[j] != value2[j])  // XXX comparison is case-sensitive
		cpl = j;
    }
    common_prefix_length = cpl;

    if (le_state_is_compdebug) {
	wchar_t value[common_prefix_length + 1];
	cand = le_candidates.contents[0];
	wcsncpy(value, cand->origvalue, common_prefix_length);
	value[common_prefix_length] = L'\0';
	le_compdebug("candidate common prefix: \"%ls\"", value);
    }

    return common_prefix_length;
}

/* Inserts the currently selected candidate into the main buffer.
 * The already inserted candidate is replaced if any.
 * When no candidate is selected, sets to the longest common prefix of the
 * candidates. There must be at least one candidate.
 * If `subst' is true, the whole source word is replaced with the candidate
 * value. Otherwise, the source word is appended (in which case the word must
 * have a valid common prefix).
 * If `finish' is true, the word is closed so that the next word can just be
 * entered directly.
 * If either `subst' or `finish' is true, the completion state must be cleaned
 * up after this function. */
void update_main_buffer(bool subst, bool finish)
{
    const le_candidate_T *cand;
    xwcsbuf_T buf;
    size_t srclen;
    size_t substindex;
    le_quote_T quotetype;

    wb_init(&buf);
    if (subst) {
	srclen = 0;
	substindex = ctxt->srcindex;
	quotetype = QUOTE_NORMAL;
    } else {
	srclen = wcslen(ctxt->origsrc);
	substindex = ctxt->origindex;
	quotetype = ctxt->quote;
    }
    if (le_selected_candidate_index >= le_candidates.length) {
	size_t cpl = get_common_prefix_length();
	assert(srclen <= cpl);
	cand = le_candidates.contents[0];

	size_t valuelen = cpl - srclen;
	wchar_t value[valuelen + 1];
	wcsncpy(value, cand->origvalue + srclen, valuelen);
	value[valuelen] = L'\0';
	quote(&buf, value, quotetype);
    } else {
	cand = le_candidates.contents[le_selected_candidate_index];
	assert(srclen <= wcslen(cand->origvalue));
	if (cand->origvalue[0] == L'\0' && quotetype == QUOTE_NORMAL)
	    wb_cat(&buf, L"\"\"");
	else
	    quote(&buf, cand->origvalue + srclen, quotetype);
    }
    assert(le_main_index >= substindex);
    wb_replace_force(&le_main_buffer,
	    substindex, le_main_index - substindex,
	    buf.contents, buf.length);
    le_main_index = substindex + buf.length;
    wb_destroy(&buf);

    if (le_selected_candidate_index >= le_candidates.length)
	return;

    if (cand->type == CT_FILE && S_ISDIR(cand->appendage.filestat.mode)) {
	size_t len = wcslen(cand->value);
	if (len > 0 && cand->value[len - 1] != L'/') {
	    wb_ninsert_force(&le_main_buffer, le_main_index, L"/", 1);
	    le_main_index += 1;
	}
    } else if (cand->type == CT_OPTIONA) {
	if (cand->value[0] == L'-') {
	    wb_ninsert_force(&le_main_buffer, le_main_index, L"=", 1);
	    le_main_index += 1;
	}
    } else {
	switch (quotetype) {
	    case QUOTE_NONE:
	    case QUOTE_NORMAL:
		break;
	    case QUOTE_SINGLE:
		wb_ninsert_force(&le_main_buffer, le_main_index, L"'", 1);
		le_main_index += 1;
		break;
	    case QUOTE_DOUBLE:
		wb_ninsert_force(&le_main_buffer, le_main_index, L"\"", 1);
		le_main_index += 1;
		break;
	}

	if (finish) {
	    if (ctxt->type & CTXT_VBRACED) {
		wb_ninsert_force(&le_main_buffer, le_main_index, L"}", 1);
		le_main_index += 1;
	    } else if (ctxt->type & CTXT_EBRACED) {
		wb_ninsert_force(&le_main_buffer, le_main_index, L",", 1);
		le_main_index += 1;
	    }
	    if (ctxt->type & CTXT_QUOTED) {
		wb_ninsert_force(&le_main_buffer, le_main_index, L"\"", 1);
		le_main_index += 1;
	    }
	    switch (ctxt->type & CTXT_MASK) {
		case CTXT_NORMAL:
		case CTXT_COMMAND:
		case CTXT_VAR:
		case CTXT_ARITH:
		case CTXT_ASSIGN:
		case CTXT_REDIR:
		case CTXT_REDIR_FD:
		case CTXT_FOR_IN:
		case CTXT_FOR_DO:
		case CTXT_CASE_IN:
		    if (ctxt->type & (CTXT_EBRACED | CTXT_VBRACED))
			break;
		    wb_ninsert_force(&le_main_buffer, le_main_index, L" ", 1);
		    le_main_index += 1;
		    break;
		case CTXT_TILDE:
		    wb_ninsert_force(&le_main_buffer, le_main_index, L"/", 1);
		    le_main_index += 1;
		    break;
	    }
	}
    }
}

/* Determines whether the source word should be substituted even if
 * `ctxt->substsrc' is false. */
/* Returns true if there is a candidate that does not begin with
 * `ctxt->origsrc'. */
bool need_subst(void)
{
    for (size_t i = 0; i < le_candidates.length; i++) {
	const le_candidate_T *cand = le_candidates.contents[i];
	if (!matchwcsprefix(cand->origvalue, ctxt->origsrc))
	    return true;
    }
    return false;
}

/* Substitutes the source word in the main buffer with all of the current
 * candidates. `ctxt' must be a valid context. */
void substitute_source_word_all(void)
{
    le_compdebug("substituting source word with candidate(s)");

    /* remove source word */
    wb_remove(&le_main_buffer, ctxt->srcindex, le_main_index - ctxt->srcindex);
    le_main_index = ctxt->srcindex;

    /* insert candidates */
    xwcsbuf_T buf;
    wb_init(&buf);
    for (size_t i = 0; i < le_candidates.length; i++) {
	const le_candidate_T* cand = le_candidates.contents[i];

	quote(wb_clear(&buf), cand->origvalue, QUOTE_NORMAL);
	wb_wccat(&buf, L' ');
	wb_ninsert_force(&le_main_buffer, le_main_index,
		buf.contents, buf.length);
	le_main_index += buf.length;
    }
    wb_destroy(&buf);
}

/* Quotes characters in the specified string that are not treated literally
 * according to `quotetype'.
 * The result is appended to the specified buffer, which must have been
 * initialized by the caller. */
void quote(xwcsbuf_T *restrict buf,
	const wchar_t *restrict s, le_quote_T quotetype)
{
    switch (quotetype) {
	case QUOTE_NONE:
	    wb_cat(buf, s);
	    return;
	case QUOTE_NORMAL:
	    while (*s != L'\0') {
		if (*s == L'\n') {
		    wb_ncat_force(buf, L"'\n'", 3);
		} else {
		    if (wcschr(L"|&;<>()$`\\\"'*?[]#~={}", *s) || iswspace(*s))
			wb_wccat(buf, L'\\');
		    wb_wccat(buf, *s);
		}
		s++;
	    }
	    return;
	case QUOTE_SINGLE:
	    while (*s != L'\0') {
		if (*s != L'\'')
		    wb_wccat(buf, *s);
		else
		    wb_ncat_force(buf, L"'\\''", 4);
		s++;
	    }
	    return;
	case QUOTE_DOUBLE:
	    while (*s != L'\0') {
		if (wcschr(CHARS_ESCAPABLE, *s))
		    wb_wccat(buf, L'\\');
		wb_wccat(buf, *s);
		s++;
	    }
	    return;
    }
    assert(false);
}


/********** Builtins **********/

static int complete_builtin_print(
	const wchar_t *command, wchar_t shortopt, const wchar_t *longopt);
static int print_cmdcandgen(
	const wchar_t *command, wchar_t shortopt, const wchar_t *longopt,
	const struct cmdcandgen_T *ccg)
    __attribute__((nonnull(1,4)));
static struct optcandgen_T *get_optcandgen(const struct cmdcandgen_T *ccg,
	wchar_t shortopt, const wchar_t *longopt)
    __attribute__((nonnull(1),pure));
static int compare_wcs(const void *p1, const void *p2)
    __attribute__((nonnull));
static int print_candgen(
	const wchar_t *command, wchar_t shortoptchar, const wchar_t *longopt,
	const wchar_t *description, bool intermixed,
	const struct candgen_T *candgen)
    __attribute__((nonnull(1)));
static bool quote_print(const wchar_t *word)
    __attribute__((nonnull));
static bool need_quote(const wchar_t *s)
    __attribute__((nonnull));
static void complete_builtin_remove(
	const wchar_t *command, wchar_t shortopt, const wchar_t *longopt);
static void kvfree_cmdcanden(kvpair_T kv);
static void free_cmdcandgen(struct cmdcandgen_T *ccg);
static void free_optcandgen(struct optcandgen_T *ocg);
static void free_candgen_contents(const struct candgen_T *candgen)
    __attribute__((nonnull));
static int complete_builtin_set(
	const wchar_t *command, wchar_t shortopt, const wchar_t *longopt,
	const wchar_t *description, bool intermixed,
	const struct candgen_T *candgen)
    __attribute__((nonnull(1,6)));
static void copy_candgen(
	struct candgen_T *restrict dest, const struct candgen_T *restrict src)
    __attribute__((nonnull));
static bool complete_builtin_change_src(const wchar_t *newsrc)
    __attribute__((nonnull));
static int complete_builtin_delegate(int wordc, void **words)
    __attribute__((nonnull));

/* The "complete" builtin. */
int complete_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"array-variable",       xno_argument,       L'A', },
	{ L"alias",                xno_argument,       L'a', },
	{ L"bindkey",              xno_argument,       L'B', },
	{ L"builtin",              xno_argument,       L'b', },
	{ L"target-command",       xrequired_argument, L'C', },
	{ L"command",              xno_argument,       L'c', },
	{ L"description",          xrequired_argument, L'D', },
	{ L"directory",            xno_argument,       L'd', },
	{ L"executable-file",      xno_argument,       L'E', },
	{ L"external-command",     xno_argument,       L'e', },
	{ L"generator-function",   xrequired_argument, L'F', },
	{ L"file",                 xno_argument,       L'f', },
	{ L"delegate",             xno_argument,       L'G', },
	{ L"group",                xno_argument,       L'g', },
	{ L"hostname",             xno_argument,       L'h', },
	{ L"signal",               xno_argument,       L'I', },
	{ L"running-job",          xno_argument,       L'J', },
	{ L"job",                  xno_argument,       L'j', },
	{ L"keyword",              xno_argument,       L'k', },
	{ L"global-alias",         xno_argument,       L'L', },
	{ L"normal-alias",         xno_argument,       L'N', },
	{ L"function",             xno_argument,       L'n', },
	{ L"target-option",        xrequired_argument, L'O', },
	{ L"print",                xno_argument,       L'P', },
	{ L"remove",               xno_argument,       L'R', },
	{ L"regular-builtin",      xno_argument,       L'r', },
	{ L"semi-special-builtin", xno_argument,       L'S', },
	{ L"special-builtin",      xno_argument,       L's', },
	{ L"username",             xno_argument,       L'u', },
	{ L"scalar-variable",      xno_argument,       L'V', },
	{ L"variable",             xno_argument,       L'v', },
	{ L"word",                 xrequired_argument, L'W', },
	{ L"intermixed",           xno_argument,       L'X', },
	{ L"finished-job",         xno_argument,       L'Y', },
	{ L"stopped-job",          xno_argument,       L'Z', },
#if YASH_ENABLE_HELP
	{ L"help",                 xno_argument,       L'-', },
#endif
	{ NULL, 0, 0, },
    };

    const wchar_t *command = NULL, *longopt = NULL;
    const wchar_t *function = NULL, *description = NULL;
    const wchar_t *newsrc = NULL;
    wchar_t shortopt = L'\0';
    le_candgentype_T cgtype = 0;
    bool intermixed = false;
    bool print = false, remove = false, delegate = false;

    wchar_t opt;
    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(
		    argv, L"C:D:F:O:GPRW:Xabcdfghjkuv", long_options, NULL))) {
	switch (opt) {
	    case L'A':  cgtype |= CGT_ARRAY;       break;
	    case L'a':  cgtype |= CGT_ALIAS;       break;
	    case L'B':  cgtype |= CGT_BINDKEY;     break;
	    case L'b':  cgtype |= CGT_BUILTIN;     break;
	    case L'C':
		if (command != NULL)
		    goto dupopterror;
		command = xoptarg;
		break;
	    case L'c':  cgtype |= CGT_COMMAND;     break;
	    case L'D':
		if (description != NULL)
		    goto dupopterror;
		description = xoptarg;
		break;
	    case L'd':  cgtype |= CGT_DIRECTORY;   break;
	    case L'E':  cgtype |= CGT_EXECUTABLE;  break;
	    case L'e':  cgtype |= CGT_EXTCOMMAND;  break;
	    case L'F':
		if (function != NULL)
		    goto dupopterror;
		function = xoptarg;
		break;
	    case L'f':  cgtype |= CGT_FILE;        break;
	    case L'G':  delegate = true;           break;
	    case L'g':  cgtype |= CGT_GROUP;       break;
	    case L'h':  cgtype |= CGT_HOSTNAME;    break;
	    case L'I':  cgtype |= CGT_SIGNAL;      break;
	    case L'J':  cgtype |= CGT_RUNNING;     break;
	    case L'j':  cgtype |= CGT_JOB;         break;
	    case L'k':  cgtype |= CGT_KEYWORD;     break;
	    case L'L':  cgtype |= CGT_GALIAS;      break;
	    case L'N':  cgtype |= CGT_NALIAS;      break;
	    case L'n':  cgtype |= CGT_FUNCTION;    break;
	    case L'O':
		if (xoptarg[0] == L'\0')
		    goto invalidopterror;
		if (xoptarg[0] == L'-') {
		    if (xoptarg[1] == L'\0'
			    || (xoptarg[1] == L'-' && xoptarg[2] == L'\0'))
			goto invalidopterror;
		    if (longopt != NULL)
			goto manyopterror;
		    longopt = xoptarg;
		} else {
		    if (xoptarg[1] != L'\0')
			goto invalidopterror;
		    if (shortopt != L'\0')
			goto manyopterror;
		    shortopt = xoptarg[0];
		}
		break;
	    case L'P':  print = true;              break;
	    case L'R':  remove = true;             break;
	    case L'r':  cgtype |= CGT_RBUILTIN;    break;
	    case L'S':  cgtype |= CGT_SSBUILTIN;   break;
	    case L's':  cgtype |= CGT_SBUILTIN;    break;
	    case L'u':  cgtype |= CGT_LOGNAME;     break;
	    case L'V':  cgtype |= CGT_SCALAR;      break;
	    case L'v':  cgtype |= CGT_VARIABLE;    break;
	    case L'W':
		if (newsrc != NULL)
		    goto dupopterror;
		newsrc = xoptarg;
		break;
	    case L'X':  intermixed = true;         break;
	    case L'Y':  cgtype |= CGT_DONE;        break;
	    case L'Z':  cgtype |= CGT_STOPPED;     break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		goto print_usage;
invalidopterror:
		xerror(0, Ngt("invalid option specification: -O %ls"), xoptarg);
		return Exit_ERROR;
manyopterror:
		xerror(0, Ngt("too many -O options specified"));
		return Exit_ERROR;
dupopterror:
		xerror(0, Ngt("more than one -%lc option specified"),
			(wint_t) opt);
		return Exit_ERROR;
	}
    }
    if (command == NULL && (shortopt != L'\0' || longopt != NULL)) {
	xerror(0, Ngt("-O option specified without -C option"));
	return Exit_ERROR;
    } else if (ctxt == NULL && command == NULL && !print && !remove) {
	xerror(0, Ngt("not in candidate generator function, "
		    "but -C, -P, -R option not specified"));
	return Exit_ERROR;
    } else if (print + remove + delegate > 1) {
	xerror(0, Ngt("more than one of -P, -R, -G option specified"));
	goto print_usage;
    } else if ((print || remove) && newsrc != NULL) {
	xerror(0, Ngt("-W option cannot be used with -P or -R option"));
	goto print_usage;
    } else if ((print || remove || delegate)
	    && (function != NULL || description != NULL
		|| cgtype != 0 || intermixed)) {
	xerror(0, Ngt("-%lc option cannot be used with style specification"),
		print ? (wint_t) L'P' : remove ? (wint_t) L'R' : (wint_t) L'G');
	goto print_usage;
    } else if (delegate && command != NULL) {
	xerror(0, Ngt("-G option cannot be used with -C option"));
	goto print_usage;
    }

    if (print) {
	return complete_builtin_print(command, shortopt, longopt);
    } else if (remove) {
	complete_builtin_remove(command, shortopt, longopt);
	return Exit_SUCCESS;
    }

    if (newsrc != NULL) {
	if (!complete_builtin_change_src(newsrc))
	    return Exit_FAILURE;
    }
    if (delegate) {
	argc -= xoptind;
	argv += xoptind;
	if (argc == 0) {
	    xerror(0, Ngt("operand not specified"));
	    return Exit_ERROR;
	}
	return complete_builtin_delegate(argc, argv);
    }

    struct candgen_T candgen;
    candgen.type = cgtype;
    if (xoptind < argc) {
	plist_T words;
	pl_init(&words);
	for (void **w = argv + xoptind; *w != NULL; w++) {
	    xwcsbuf_T buf;
	    wb_wccat(wb_cat(wb_init(&buf), *w), L'\0');
	    if (description != NULL)
		wb_cat(&buf, description);
	    pl_add(&words, wb_towcs(&buf));
	}
	candgen.words = pl_toary(&words);
    } else {
	candgen.words = NULL;
    }
    candgen.function = (function != NULL) ? xwcsdup(function) : NULL;

    if (command == NULL) {
	assert(ctxt != NULL);

	int exitstatus = generate_candidates(&candgen);
	free_candgen_contents(&candgen);
	return exitstatus;
    } else {
	return complete_builtin_set(
		command, shortopt, longopt, description, intermixed, &candgen);
    }

print_usage:
    fprintf(stderr,
	gt("Usage:  complete [-C command] [-O option] [-D description] \\\n"
	   "                 [-F function] [-Xabcdfghjkouv] [words...]\n"
	   "        complete -P|-R [-C command] [-O option]\n"
	   "        complete -G words...\n"));
    return Exit_ERROR;
}

/* Prints commands that can be used to restore the current completion settings
 * for the specified command/target.
 * If the command is not specified, all completion settings are printed.
 * Returns Exit_SUCCESS iff successful. Returns Exit_FAILURE if the specified
 * command/option was not found. */
int complete_builtin_print(
	const wchar_t *command, wchar_t shortopt, const wchar_t *longopt)
{
    if (candgens.capacity == 0 || candgens.count == 0)
	return (command == NULL) ? Exit_SUCCESS : Exit_FAILURE;

    if (command != NULL) {
	const struct cmdcandgen_T *ccg = ht_get(&candgens, command).value;
	if (ccg == NULL)
	    return Exit_FAILURE;
	return print_cmdcandgen(command, shortopt, longopt, ccg);
    } else {
	int result = Exit_SUCCESS;
	kvpair_T *kvs = ht_tokvarray(&candgens);
	qsort(kvs, candgens.count, sizeof *kvs, keywcscoll);
	for (size_t i = 0; i < candgens.count; i++) {
	    result = print_cmdcandgen(kvs[i].key, L'\0', NULL, kvs[i].value);
	    if (result != Exit_SUCCESS)
		break;
	}
	free(kvs);
	return result;
    }
}

/* Prints the contents of the specified `cmdcandgen_T' structure
 * in a form interpretable as a command.
 * If `targetoption' is non-NULL, prints the specified option only.
 * Returns Exit_SUCCESS iff successful. */
int print_cmdcandgen(
	const wchar_t *command, wchar_t shortopt, const wchar_t *longopt,
	const struct cmdcandgen_T *ccg)
{
    if (shortopt == L'\0' && longopt == NULL) {
	/* print command */
	int exitstatus = print_candgen(command, L'\0', NULL,
		ccg->description, ccg->intermixed, &ccg->operands);
	if (exitstatus != Exit_SUCCESS)
	    return exitstatus;

	/* print options */
	for (const struct optcandgen_T *ocg = ccg->options;
		ocg != NULL;
		ocg = ocg->next) {
	    exitstatus = print_candgen(command,
		    ocg->shortoptchar, ocg->longopt, ocg->description, false,
		    ocg->requiresargument ? &ocg->candgen : NULL);
	    if (exitstatus != Exit_SUCCESS)
		return exitstatus;
	}
	return Exit_SUCCESS;
    }

    /* find option to print */
    struct optcandgen_T *ocg = get_optcandgen(ccg, shortopt, longopt);
    if (ocg == NULL || ocg == (struct optcandgen_T *) ccg)
	return Exit_FAILURE;
    return print_candgen(command,
	    ocg->shortoptchar, ocg->longopt, ocg->description, false,
	    ocg->requiresargument ? &ocg->candgen : NULL);
}

/* Searches `ccg->options' for the specified options.
 * Either or both of `shortopt' and `longopt' must be non-NULL.
 * If the short and long options conflict, returns `ccg'.
 * If not found, returns NULL. */
struct optcandgen_T *get_optcandgen(const struct cmdcandgen_T *ccg,
	wchar_t shortopt, const wchar_t *longopt)
{
    struct optcandgen_T *ocgs = NULL, *ocgl = NULL;

    assert(shortopt != L'\0' || longopt != NULL);
    if (shortopt != L'\0') {
	for (ocgs = ccg->options; ocgs != NULL; ocgs = ocgs->next)
	    if (shortopt == ocgs->shortoptchar)
		break;
	if (longopt == NULL)
	    return ocgs;
    }
    if (longopt != NULL) {
	for (ocgl = ccg->options; ocgl != NULL; ocgl = ocgl->next)
	    if (ocgl->longopt != NULL && wcscmp(longopt, ocgl->longopt) == 0)
		break;
	if (shortopt == L'\0')
	    return ocgl;
    }

    assert(shortopt != L'\0' && longopt != NULL);
    if (ocgs == NULL) {
	if (ocgl != NULL && ocgl->shortoptchar != L'\0'
			 && ocgl->shortoptchar != shortopt)
	    return (struct optcandgen_T *) ccg;
	return ocgl;
    } else {  /* ocgs != NULL */
	if (ocgl != NULL && ocgs != ocgl)
	    return (struct optcandgen_T *) ccg;
	if (ocgs->longopt != NULL && wcscmp(longopt, ocgs->longopt) != 0)
	    return (struct optcandgen_T *) ccg;
	return ocgs;
    }
}

/* Prints the contents of the specified `candgen_T' structure
 * in a form interpretable as a command.
 * Returns Exit_SUCCESS iff successful. */
int print_candgen(
	const wchar_t *command, wchar_t shortoptchar, const wchar_t *longopt,
	const wchar_t *description, bool intermixed,
	const struct candgen_T *candgen)
{
#define TRY(cond)      do if (!(cond)) goto ioerror; while (0)
#define TRYPRINT(...)  TRY(printf(__VA_ARGS__) >= 0)

    wchar_t shortoptary[2] = { shortoptchar, L'\0' };
    wchar_t *shortopt = (shortoptary[0] != L'\0') ? shortoptary : NULL;

    wchar_t *qcommand = NULL, *qshortopt = NULL, *qlongopt = NULL;
    if (need_quote(command))
	command = qcommand = quote_sq(command);
    if (shortopt != NULL && need_quote(shortopt))
	shortopt = qshortopt = quote_sq(shortopt);
    if (longopt != NULL && need_quote(longopt))
	longopt = qlongopt = quote_sq(longopt);

    TRYPRINT("complete -C %ls", command);

    if (intermixed)
	TRYPRINT(" -X");
    if (shortopt != NULL)
	TRYPRINT(" -O %ls", shortopt);
    if (longopt != NULL)
	TRYPRINT(" -O %ls", longopt);
    if (description != NULL) {
	TRYPRINT(" -D");
	TRY(quote_print(description));
    }

    if (candgen == NULL) {
	TRYPRINT("\n");
	goto finish;
    }

    if (candgen->type & CGT_FILE)
	TRYPRINT(" --file");
    if (candgen->type & CGT_DIRECTORY)
	TRYPRINT(" --directory");
    if (candgen->type & CGT_EXECUTABLE)
	TRYPRINT(" --executable-file");
    if ((candgen->type & CGT_COMMAND) == CGT_COMMAND) {
	TRYPRINT(" --command");
    } else {
	if ((candgen->type & CGT_BUILTIN) == CGT_BUILTIN) {
	    TRYPRINT(" --builtin");
	} else {
	    if (candgen->type & CGT_SBUILTIN)
		TRYPRINT(" --special-builtin");
	    if (candgen->type & CGT_SSBUILTIN)
		TRYPRINT(" --semi-special-builtin");
	    if (candgen->type & CGT_RBUILTIN)
		TRYPRINT(" --regular-builtin");
	}
	if (candgen->type & CGT_EXTCOMMAND)
	    TRYPRINT(" --external-command");
	if (candgen->type & CGT_FUNCTION)
	    TRYPRINT(" --function");
    }
    if (candgen->type & CGT_KEYWORD)
	TRYPRINT(" --keyword");
    if (candgen->type & CGT_OPTION)
	if (false)  /* CGT_OPTION never appears in candgen->type */
	    goto ioerror;
    if ((candgen->type & CGT_ALIAS) == CGT_ALIAS) {
	TRYPRINT(" --alias");
    } else {
	if (candgen->type & CGT_NALIAS)
	    TRYPRINT(" --normal-alias");
	if (candgen->type & CGT_GALIAS)
	    TRYPRINT(" --global-alias");
    }
    if ((candgen->type & CGT_VARIABLE) == CGT_VARIABLE) {
	TRYPRINT(" --variable");
    } else {
	if (candgen->type & CGT_SCALAR)
	    TRYPRINT(" --scalar-variable");
	if (candgen->type & CGT_ARRAY)
	    TRYPRINT(" --array-variable");
    }
    if ((candgen->type & CGT_JOB) == CGT_JOB) {
	TRYPRINT(" --job");
    } else {
	if (candgen->type & CGT_RUNNING)
	    TRYPRINT(" --running-job");
	if (candgen->type & CGT_STOPPED)
	    TRYPRINT(" --stopped-job");
	if (candgen->type & CGT_DONE)
	    TRYPRINT(" --finished-job");
    }
    if (candgen->type & CGT_SIGNAL)
	TRYPRINT(" --signal");
    if (candgen->type & CGT_LOGNAME)
	TRYPRINT(" --username");
    if (candgen->type & CGT_GROUP)
	TRYPRINT(" --group");
    if (candgen->type & CGT_HOSTNAME)
	TRYPRINT(" --hostname");
    if (candgen->type & CGT_BINDKEY)
	TRYPRINT(" --bindkey");

    if (candgen->function != NULL) {
	TRYPRINT(" -F");
	TRY(quote_print(candgen->function));
    }

    TRYPRINT("\n");

    if (candgen->words != NULL) {
	/* sort before print */
	qsort(candgen->words, plcount(candgen->words), sizeof *candgen->words,
		compare_wcs);

	for (void *const *w = candgen->words; *w != NULL; w++) {
	    const wchar_t *word = *w;
	    TRYPRINT("complete -C %ls", command);
	    if (shortopt != NULL)
		TRYPRINT(" -O %ls", shortopt);
	    if (longopt != NULL)
		TRYPRINT(" -O %ls", longopt);
	    description = word + wcslen(word) + 1;
	    if (description[0] != L'\0') {
		TRYPRINT(" -D");
		TRY(quote_print(description));
	    }
	    if (word[0] == L'-')
		TRYPRINT(" --");
	    TRY(quote_print(word));
	    TRYPRINT("\n");
	}
    }

finish:
    free(qcommand);
    free(qshortopt);
    free(qlongopt);
    return Exit_SUCCESS;

ioerror:
    xerror(errno, Ngt("cannot print to standard output"));
    free(qcommand);
    free(qshortopt);
    free(qlongopt);
    return Exit_FAILURE;

#undef TRY
#undef TRYPRINT
}

int compare_wcs(const void *p1, const void *p2)
{
    return wcscoll(*(const wchar_t *const *) p1, *(const wchar_t *const *) p2);
}

/* Prints a space followed by the specified word to the standard output.
 * The word is quoted if necessary.
 * Returns true iff successful. */
bool quote_print(const wchar_t *word)
{
    if (word[0] != L'\0' && !need_quote(word))
	return printf(" %ls", word) >= 0;

    wchar_t *q = quote_sq(word);
    int r = printf(" %ls", q);
    free(q);
    return r >= 0;
}

bool need_quote(const wchar_t *s)
{
    switch (*s) {
	case L'#':  case L'~':
	    return true;
    }
    for (;;) {
	switch (*s) {
	    case L'\0':
		return false;
	    case L' ':  case L'\n': case L'\t':
	    case L';':  case L'&':  case L'|':
	    case L'<':  case L'>':  case L'(':  case L')':
	    case L'$':  case L'`':  case L'*':  case L'?':  case L'[':
	    case L'\\': case L'\'': case L'"':
		return true;
	    default:
		if (iswblank(*s))
		    return true;
		break;
	}
	s++;
    }
}

/* Removes the registered completion style for the specified command/option. */
void complete_builtin_remove(
	const wchar_t *command, wchar_t shortopt, const wchar_t *longopt)
{
    if (candgens.capacity == 0 || candgens.count == 0)
	return;

    if (command == NULL) {
	/* remove everything */
	ht_destroy(ht_clear(&candgens, kvfree_cmdcanden));
	candgens.capacity = 0;
    } else if (shortopt == L'\0' && longopt == NULL) {
	/* remove specified command */
	kvfree_cmdcanden(ht_remove(&candgens, command));
    } else {
	/* remove specified option */
	struct cmdcandgen_T *ccg = ht_get(&candgens, command).value;
	if (ccg != NULL) {
	    struct optcandgen_T **ocgp = &ccg->options;
	    struct optcandgen_T  *ocg  =  ccg->options;
	    for (; ocg != NULL; ocg = *ocgp) {
		if ((shortopt != L'\0' && shortopt == ocg->shortoptchar)
			|| (longopt != NULL && ocg->longopt != NULL
			    && wcscmp(longopt, ocg->longopt) == 0)) {
		    *ocgp = ocg->next;
		    free_optcandgen(ocg);
		} else {
		    ocgp = &ocg->next;
		}
	    }
	}
    }
}

/* Frees the specified pair of pointers to a wide string and a `cmdcandgen_T'
 * object. */
void kvfree_cmdcanden(kvpair_T kv)
{
    free(kv.key);
    free_cmdcandgen(kv.value);
}

/* Frees the specified `cmdcandgen_T' object. */
void free_cmdcandgen(struct cmdcandgen_T *ccg)
{
    if (ccg != NULL) {
	struct optcandgen_T *ocg = ccg->options;
	while (ocg != NULL) {
	    struct optcandgen_T *nextocg = ocg->next;
	    free_optcandgen(ocg);
	    ocg = nextocg;
	}

	free(ccg->description);
	free_candgen_contents(&ccg->operands);
	free(ccg);
    }
}

/* Frees the specified `optcandgen_T' object. */
void free_optcandgen(struct optcandgen_T *ocg)
{
    if (ocg != NULL) {
	free(ocg->description);
	free(ocg->longopt);
	free_candgen_contents(&ocg->candgen);
	free(ocg);
    }
}

/* Frees the contents of the specified `candgen_T' object. */
void free_candgen_contents(const struct candgen_T *candgen)
{
    recfree(candgen->words, free);
    free(candgen->function);
}

/* Sets the completion style for the specified command/option to the specified
 * `candgen_T' object.
 * The contents of `candgen' is freed in this function. */
int complete_builtin_set(
	const wchar_t *command, wchar_t shortopt, const wchar_t *longopt,
	const wchar_t *description, bool intermixed,
	const struct candgen_T *candgen)
{
    if (candgens.capacity == 0)
	ht_init(&candgens, hashwcs, htwcscmp);

    struct cmdcandgen_T *ccg = ht_get(&candgens, command).value;
    if (ccg == NULL) {
	ccg = xmalloc(sizeof *ccg);
	*ccg = (struct cmdcandgen_T) { .intermixed = false };
	ht_set(&candgens, xwcsdup(command), ccg);
    }

    ccg->intermixed |= intermixed;
    if (shortopt == L'\0' && longopt == NULL) {
	/* set style for command operands */
	if (description != NULL && candgen->words == NULL) {
	    free(ccg->description);
	    ccg->description = xwcsdup(description);
	}
	copy_candgen(&ccg->operands, candgen);
	return Exit_SUCCESS;
    }

    /* find option to set style */
    struct optcandgen_T *ocg = get_optcandgen(ccg, shortopt, longopt);
    if (ocg == (struct optcandgen_T *) ccg) {
	xerror(0, Ngt("-C %ls -O %lc -O %ls: specified short and long options "
		    "don't match with ones already registered"),
		command, (wint_t) shortopt, longopt);
	free_candgen_contents(candgen);
	return Exit_FAILURE;
    }

    /* if no option was found, create a new one */
    if (ocg == NULL) {
	ocg = xmalloc(sizeof *ocg);
	*ocg = (struct optcandgen_T) { .next = ccg->options };
	ccg->options = ocg;
    }

    if (description != NULL && candgen->words == NULL) {
	free(ocg->description);
	ocg->description = xwcsdup(description);
    }
    if (ocg->shortoptchar == L'\0')
	ocg->shortoptchar = shortopt;
    if (ocg->longopt == NULL && longopt != NULL)
	ocg->longopt = xwcsdup(longopt);
    if (candgen->type != 0 || candgen->words != NULL
	    || candgen->function != NULL) {
	ocg->requiresargument = true;
	copy_candgen(&ocg->candgen, candgen);
//  } else {
//	free_candgen_contents(candgen);
    }

    return Exit_SUCCESS;
}

/* Moves the contents of `src' to `dest'.
 * The old contents of `dest' is not erased (except for the `function' member):
 * the contents of `src' is added to that of `dest'. */
void copy_candgen(
	struct candgen_T *restrict dest, const struct candgen_T *restrict src)
{
    dest->type |= src->type;
    if (src->words != NULL) {
	if (dest->words == NULL) {
	    dest->words = src->words;
	} else {
	    plist_T words;
	    pl_initwith(&words, dest->words, plcount(dest->words));
	    for (void **sw = src->words; *sw != NULL; sw++) {
		for (void **dw = words.contents; *dw != NULL; dw++) {
		    if (wcscmp(*sw, *dw) == 0) {
			free(*dw);
			*dw = *sw;
			goto next;
		    }
		}
		pl_add(&words, *sw);
next:;
	    }
	    dest->words = pl_toary(&words);
	    free(src->words);
	}
    }
    if (src->function != NULL) {
	free(dest->function);
	dest->function = src->function;
    }
}

/* Changes `ctxt->src' to `newsrc', which should be a postfix of `ctxt->src'. */
bool complete_builtin_change_src(const wchar_t *newsrc)
{
    if (ctxt == NULL) {
	xerror(0, Ngt("-W option cannot be used "
		    "when not in candidate generator function"));
	return false;
    }

    size_t srclen = wcslen(ctxt->src);
    size_t newsrclen = wcslen(newsrc);
    if (srclen < newsrclen ||
	    wcscmp(ctxt->src + srclen - newsrclen, newsrc) != 0) {
	xerror(0, Ngt("the current target word "
		    "does not end with the specified word `%ls'"), newsrc);
	return false;
    }

    skip_prefix(srclen - newsrclen);
    return true;
}

/* Generates candidates using the specified words as `pwords' and `src'. */
int complete_builtin_delegate(int wordc, void **words)
{
    assert(ctxt != NULL);
    assert(wordc > 0);

    void *pwords[wordc];
    memcpy(pwords, words, (wordc - 1) * sizeof *pwords);
    pwords[wordc - 1] = NULL;

    le_context_T *savecontext = ctxt;
    le_context_T newctxt;
    int exitstatus;

    newctxt.quote     = savecontext->quote;
    newctxt.pwordc    = wordc - 1;
    newctxt.pwords    = pwords;
    newctxt.cpattern  = NULL;
    newctxt.srcindex  = savecontext->srcindex;
    newctxt.origindex = savecontext->origindex;
    newctxt.substsrc  = false;

    if ((savecontext->type & CTXT_MASK) == CTXT_NORMAL
	    && newctxt.pwordc == 0) {
	newctxt.type = CTXT_COMMAND | (savecontext->type & ~CTXT_MASK);
    } else {
	newctxt.type = savecontext->type;
    }

    newctxt.origsrc = newctxt.src = words[wordc - 1];
    if (wcscmp(savecontext->origsrc, words[wordc - 1]) == 0) {
	newctxt.pattern = xwcsdup(savecontext->origpattern);
    } else {
	xwcsbuf_T buf;
	wb_initwith(&buf, escape(words[wordc - 1], NULL));
	newctxt.pattern = wb_towcs(wb_wccat(&buf, L'*'));
    }
    newctxt.origpattern = newctxt.pattern;

    if (le_state_is_compdebug) {
	le_compdebug("delegation start (complete -G)");
	print_context_info(&newctxt);
    }

    ctxt = &newctxt;
    exitstatus = generate_candidates(get_candgen());
    ctxt = savecontext;

    free(newctxt.origpattern);
    xfnm_free(newctxt.cpattern);

    le_compdebug("delegation end (complete -G)");
    return exitstatus;
}

#if YASH_ENABLE_HELP
const char complete_help[] = Ngt(
"complete - edit completion style or generate completion candidates\n"
"\tcomplete [-C command] [-O option] [-D description] [-W word] \\\n"
"\t         [-F function] [-Xabcdfghjkuv] [words...]\n"
"\tcomplete -P|-R [-C command] [-O option]\n"
"\tcomplete -G [-W word] words...\n"
"The \"complete\" builtin specifies how to complete command-line words.\n"
"You can specify possible options and operands for each command, and possible\n"
"arguments for each argument-requiring option as well.\n"
"The target command must be specified as the argument to the -C\n"
"(--target-command) option. To register a possible option for that command,\n"
"specify the -O (--target-option) option. If the argument to the -O option is\n"
"a single character, it is considered a short (one-character) option.\n"
"Otherwise, it is considered a long option (but it must start with a hyphen).\n"
"To register more than one option, invoke the \"complete\" builtin for each\n"
"option.\n"
"\n"
"To specify the completion style for operands to the command, use options\n"
"that correspond to the categories of candidates:\n"
" -a --alias                 aliases (global and non-global)\n"
"    --array-variable        array variables\n"
"    --bindkey               command names for line-editing key bindings\n"
" -b --builtin               builtin commands\n"
" -c --command               commands and shell keywords\n"
" -d --directory             directory names\n"
"    --executable-file       executable regular files\n"
"    --external-command      external commands\n"
" -f --file                  files (including directories)\n"
"    --finished-job          finished job names\n"
"    --function              shell functions\n"
"    --global-alias          global aliases\n"
" -g --group                 group names\n"
" -h --hostname              host names\n"
" -j --job                   job names\n"
" -k --keyword               shell keywords\n"
"    --normal-alias          non-global aliases\n"
"    --regular-builtin       regular builtins\n"
"    --running-job           running job names\n"
"    --scalar-variable       normal (non-array) variables\n"
"    --semi-special-builtin  semi-special builtins\n"
"    --signal                signal names\n"
"    --special-builtin       special builtins\n"
"    --stopped-job           stopped job names\n"
" -u --username              user names\n"
" -v --variable              variable names\n"
"In addition to the options above, you can specify a function name as the\n"
"argument to the -F (--generator-function) option. The function is called\n"
"when an operand for the command is completed.\n"
"Moreover, arbitrary words can be given as operands to the \"complete\"\n"
"builtin as possible operands to the completed command.\n"
"If the options and operands described above are specified with the -O\n"
"option, it is considered an argument-requiring option where the argument is\n"
"completed according to the specified style.\n"
"You can attach a description of the target command, option, or candidate\n"
"words as the argument to the -D (--description) option.\n"
"\n"
"If the command's options and operands can be intermixed, specify the -X\n"
"(--intermixed) option. If this option is not specified, the completer\n"
"considers all arguments after the first operand as operands.\n"
"\n"
"If the -P (--print) option is specified, the \"complete\" builtin prints\n"
"the completion settings for the specified command (and option).\n"
"If the -R (--remove) option is specified, the settings for the specified\n"
"command (and option) are removed.\n"
"\n"
"The \"complete\" builtin can be invoked from a candidate generator function\n"
"to generate candidates, dynamically specifying the completion style.\n"
"In this case, the -C and -O option must not be specified.\n"
"If the -G (--delegate) option is specified, candidates are generated as if\n"
"the currently completed command/arguments are <words>.\n"
"In a candidate generator function, the -W (--word) option can be used to\n"
"change the target word to its substring.\n"
);
#endif /* YASH_ENABLE_HELP */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
