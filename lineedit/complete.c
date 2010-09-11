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
# ifndef setpwent
extern void setpwent(void);
# endif
# ifndef getpwent
extern struct passwd *getpwent(void);
# endif
# ifndef endpwent
extern void endpwent(void);
# endif
#endif
#if HAVE_GETGRENT
# if 0 /* avoid conflict on BSD */
extern void setgrent(void);
# endif
# ifndef getgrent
extern struct group *getgrent(void);
# endif
# ifndef endgrent
extern void endgrent(void);
# endif
#endif
#if HAVE_GETHOSTENT
# ifndef sethostent
extern void sethostent(int);
# endif
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

struct candgen_T;
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

    // FIXME generate_candidates(get_candgen());
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
	subst = !matchwcsprefix(cand->origvalue, ctxt->src);
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
	free(ctxt->src);
	free(ctxt->pattern);
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
    const wchar_t *v1 = cand1->origvalue;
    const wchar_t *v2 = cand2->origvalue;

    /* sort candidates that start with hyphens in a special order so that short
     * options come before long options */
    if (v1[0] == L'-' || v2[0] == L'-') {
	while (*v1 == L'-' && *v2 == L'-')
	    v1++, v2++;
	if (*v1 == L'-')
	    return 1;
	if (*v2 == L'-')
	    return -1;
    }

    return wcscoll(v1, v2);
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
 * The `arg' member specifies if this option requires an argument.
 * The `candgen' member is valid only when `arg' is not OPTARG_NONE. */
struct optcandgen_T {
    struct optcandgen_T *next;
    wchar_t *description;
    wchar_t shortoptchar;
    wchar_t *longopt;
    enum optarg_T arg;
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
 * with the ignored prefix prepended. */
void le_add_candidate(le_candidate_T *cand)
{
#if 0
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
#endif // TODO FIXME

    if (le_state_is_compdebug) {
	const char *typestr = NULL;
	switch (cand->type) {
	    case CT_WORD:      typestr = "word";                       break;
	    case CT_FILE:      typestr = "file";                       break;
	    case CT_COMMAND:   typestr = "command";                    break;
	    case CT_ALIAS:     typestr = "alias";                      break;
	    case CT_OPTION:    typestr = "option";                     break;
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
	    if (xfnm_match(NULL /* FIXME */, de->d_name) != 0)
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
	if (WMATCH(NULL /* FIXME */, *k))
	    le_new_command_candidate(xwcsdup(*k));
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
    setpwent();
    while ((pwd = getpwent()) != NULL)
	if (xfnm_match(NULL /* FIXME */, pwd->pw_name) == 0)
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
    setgrent();
    while ((grp = getgrent()) != NULL)
	if (xfnm_match(NULL /* FIXME */, grp->gr_name) == 0)
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
	if (xfnm_match(NULL /* FIXME */, host->h_name) == 0)
	    le_new_candidate(CT_HOSTNAME, malloc_mbstowcs(host->h_name), NULL);
	if (host->h_aliases != NULL)
	    for (char *const *a = host->h_aliases; *a != NULL; a++)
		if (xfnm_match(NULL /* FIXME */, *a) == 0)
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
	if (WMATCH(NULL /* FIXME */, word)) {
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
	srclen = wcslen(ctxt->src);
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
#if 0
    } else if (cand->type == CT_OPTIONA) {
	if (cand->value[0] == L'-') {
	    wb_ninsert_force(&le_main_buffer, le_main_index, L"=", 1);
	    le_main_index += 1;
	}
#endif // FIXME
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
	if (!matchwcsprefix(cand->origvalue, ctxt->src))
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

static int complete_builtin_delegate(int wordc, void **words)
    __attribute__((nonnull));

/* The "complete" builtin. */
int complete_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"optional-argument",    OPTARG_NONE,     L'A', },
	{ L"alias",                OPTARG_NONE,     L'a', },
	{ L"bindkey",              OPTARG_NONE,     L'B', },
	{ L"builtin",              OPTARG_NONE,     L'b', },
	{ L"target-command",       OPTARG_REQUIRED, L'C', },
	{ L"command",              OPTARG_NONE,     L'c', },
	{ L"description",          OPTARG_REQUIRED, L'D', },
	{ L"directory",            OPTARG_NONE,     L'd', },
	{ L"executable-file",      OPTARG_NONE,     L'E', },
	{ L"external-command",     OPTARG_NONE,     L'e', },
	{ L"generator-function",   OPTARG_REQUIRED, L'F', },
	{ L"file",                 OPTARG_NONE,     L'f', },
	{ L"delegate",             OPTARG_NONE,     L'G', },
	{ L"group",                OPTARG_NONE,     L'g', },
	{ L"hostname",             OPTARG_NONE,     L'h', },
	{ L"signal",               OPTARG_NONE,     L'I', },
	{ L"running-job",          OPTARG_NONE,     L'J', },
	{ L"job",                  OPTARG_NONE,     L'j', },
	{ L"keyword",              OPTARG_NONE,     L'k', },
	{ L"global-alias",         OPTARG_NONE,     L'L', },
	{ L"normal-alias",         OPTARG_NONE,     L'N', },
	{ L"function",             OPTARG_NONE,     L'n', },
	{ L"target-option",        OPTARG_REQUIRED, L'O', },
	{ L"print",                OPTARG_NONE,     L'P', },
	{ L"remove",               OPTARG_NONE,     L'R', },
	{ L"regular-builtin",      OPTARG_NONE,     L'r', },
	{ L"semi-special-builtin", OPTARG_NONE,     L'S', },
	{ L"special-builtin",      OPTARG_NONE,     L's', },
	{ L"username",             OPTARG_NONE,     L'u', },
	{ L"scalar-variable",      OPTARG_NONE,     L'V', },
	{ L"variable",             OPTARG_NONE,     L'v', },
	{ L"word",                 OPTARG_REQUIRED, L'W', },
	{ L"intermixed",           OPTARG_NONE,     L'X', },
	{ L"finished-job",         OPTARG_NONE,     L'Y', },
	{ L"array-variable",       OPTARG_NONE,     L'y', },
	{ L"stopped-job",          OPTARG_NONE,     L'Z', },
#if YASH_ENABLE_HELP
	{ L"help",                 OPTARG_NONE,     L'-', },
#endif
	{ NULL, 0, 0, },
    };

    const wchar_t *command = NULL, *longopt = NULL;
    const wchar_t *function = NULL, *description = NULL;
    const wchar_t *newsrc = NULL;
    wchar_t shortopt = L'\0';
    le_candgentype_T cgtype = 0;
    bool intermixed = false, argopt = false;
    bool print = false, remove = false, delegate = false;

    wchar_t opt;
    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(
		    argv, L"AC:D:F:O:GPRW:Xabcdfghjkuv", long_options, NULL))) {
	switch (opt) {
	    case L'A':  argopt = true;             break;
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
	    case L'y':  cgtype |= CGT_ARRAY;       break;
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
		|| cgtype != 0 || intermixed || argopt)) {
	xerror(0, Ngt("-%lc option cannot be used with style specification"),
		print ? (wint_t) L'P' : remove ? (wint_t) L'R' : (wint_t) L'G');
	goto print_usage;
    } else if (argopt && (shortopt == L'\0' && longopt == NULL)) {
	xerror(0, Ngt("-A option specified without -O option"));
	return Exit_ERROR;
    } else if (delegate && command != NULL) {
	xerror(0, Ngt("-G option cannot be used with -C option"));
	goto print_usage;
    }

    // FIXME
    if (print) {
	// return complete_builtin_print(command, shortopt, longopt);
	return Exit_ERROR;
    } else if (remove) {
	return Exit_SUCCESS;
    }

    if (newsrc != NULL) {
	return Exit_ERROR;
//	if (!complete_builtin_change_src(newsrc))
//	    return Exit_FAILURE;
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
	return exitstatus;
    } else {
	return 0;
//	return complete_builtin_set(command, shortopt, longopt, description,
//		intermixed, argopt, &candgen);
    }

print_usage:
    fprintf(stderr,
	gt("Usage:  complete [-C command] [-O option] [-D description] \\\n"
	   "                 [-F function] [-AXabcdfghjkouv] [words...]\n"
	   "        complete -P|-R [-C command] [-O option]\n"
	   "        complete -G words...\n"));
    return Exit_ERROR;
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
    newctxt.srcindex  = savecontext->srcindex;
    newctxt.origindex = savecontext->origindex;
    newctxt.substsrc  = false;

#if 0
    if ((savecontext->type & CTXT_MASK) == CTXT_NORMAL
	    && newctxt.pwordc == 0) {
	newctxt.type = CTXT_COMMAND | (savecontext->type & ~CTXT_MASK);
    } else {
	newctxt.type = savecontext->type;
    }

    newctxt.src = newctxt.src = words[wordc - 1];
    if (wcscmp(savecontext->origsrc, words[wordc - 1]) == 0) {
	newctxt.pattern = xwcsdup(savecontext->origpattern);
    } else {
	xwcsbuf_T buf;
	wb_initwith(&buf, escape(words[wordc - 1], NULL));
	newctxt.pattern = wb_towcs(wb_wccat(&buf, L'*'));
    }
    newctxt.origpattern = newctxt.pattern;
#endif // FIXME

    if (le_state_is_compdebug) {
	le_compdebug("delegation start (complete -G)");
	print_context_info(&newctxt);
    }

    ctxt = &newctxt;
    exitstatus = 0; // FIXME generate_candidates(get_candgen());
    ctxt = savecontext;

    free(newctxt.pattern);

    le_compdebug("delegation end (complete -G)");
    return exitstatus;
}

#if YASH_ENABLE_HELP
const char complete_help[] = Ngt(
"complete - edit completion style or generate completion candidates\n"
"\tcomplete [-C command] [-O option] [-D description] [-W word] \\\n"
"\t         [-F function] [-AXabcdfghjkuv] [words...]\n"
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
"option, the option specified by the -O option is assumed to take an\n"
"argument, which is completed according to the specified style.\n"
"If the -A (--optional-argument) option is also specified, the option\n"
"argument is considered optional.\n"
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
