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

static void execute_completion_function(void);
static bool autoload_completion_function(void);
static bool call_standard_completion_function(void);

static void simple_completion(le_candgentype_T type);
static void generate_candidates(const le_compopt_T *compopt)
    __attribute__((nonnull));
static void generate_file_candidates(const le_compopt_T *compopt)
    __attribute__((nonnull));
static void generate_external_command_candidates(const le_compopt_T *compopt)
    __attribute__((nonnull));
static void generate_keyword_candidates(const le_compopt_T *compopt)
    __attribute__((nonnull));
static void generate_logname_candidates(const le_compopt_T *compopt)
    __attribute__((nonnull));
static void generate_group_candidates(const le_compopt_T *compopt)
    __attribute__((nonnull));
static void generate_host_candidates(const le_compopt_T *compopt)
    __attribute__((nonnull));
static void generate_candidates_from_words(
	le_candtype_T type, void *const *words, const wchar_t *description,
	const le_compopt_T *compopt)
    __attribute__((nonnull));
static void word_completion(size_t count, ...)
    __attribute__((nonnull));

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

    execute_completion_function();
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


/********** Completion Function Execution **********/

/* default completion function name */
#define DEFAULT_COMPFUNC "completion//default"

/* Loads and executes completion function to generate candidates. */
void execute_completion_function(void)
{
    switch (ctxt->type & CTXT_MASK) {
	case CTXT_NORMAL:
	    if (!call_standard_completion_function()) {
		if (autoload_completion_function())
		    call_standard_completion_function();
		else if (!call_completion_function(L"" DEFAULT_COMPFUNC))
		    simple_completion(CGT_FILE);
	    }
	    break;
	case CTXT_COMMAND:
	    if (!call_completion_function(L"" DEFAULT_COMPFUNC)) {
		le_candgentype_T type;
		if (wcschr(ctxt->src, L'/')) {
		    type = CGT_DIRECTORY | CGT_EXECUTABLE;
		} else {
		    type = CGT_DIRECTORY | CGT_COMMAND;
		    if (ctxt->quote == QUOTE_NORMAL
			    && !wcschr(ctxt->pattern, L'\\'))
			type |= CGT_KEYWORD | CGT_NALIAS;
		}
		simple_completion(type);
	    }
	    break;
	case CTXT_TILDE:
	    simple_completion(CGT_LOGNAME);
	    break;
	case CTXT_VAR:
	    simple_completion(CGT_VARIABLE);
	    break;
	case CTXT_ARITH:
	    simple_completion(CGT_SCALAR);
	    break;
	case CTXT_ASSIGN:
	case CTXT_REDIR:
	    simple_completion(CGT_FILE);
	    break;
	case CTXT_REDIR_FD:
	    break;
	case CTXT_FOR_IN:
	    word_completion(2, L"in", L"do");
	    break;
	case CTXT_FOR_DO:
	    word_completion(1, L"do");
	    break;
	case CTXT_CASE_IN:
	    word_completion(1, L"in");
	    break;
    }

}

/* Define the completion function by autoloading "completion/<ctxt->pwords[0]>".
 * Returns true if a file is autoloaded. */
bool autoload_completion_function(void)
{
    return false; //FIXME
}

/* Calls standard completion function to generate candidates.
 * If a function whose name is "completion/<ctxt->pwords[0]>" has been defined,
 * that function is called with no arguments.
 * Returns false if the function has not been defined. */
bool call_standard_completion_function(void)
{
    const wchar_t *cmdname = ctxt->pwords[0];
    if (cmdname == NULL)
	/* We're completing the command name so there's no standard function */
	return false;

    xwcsbuf_T funcname;
    wb_init(&funcname);
    wb_cat(&funcname, L"completion/");
    wb_cat(&funcname, cmdname);

    bool ok = call_completion_function(funcname.contents);
    if (!ok) {
	/* try the function name without the directory components */
	cmdname = wcsrchr(cmdname, L'/');
	if (cmdname != NULL && *++cmdname != L'\0') {
	    wb_clear(&funcname);
	    wb_cat(&funcname, L"completion/");
	    wb_cat(&funcname, cmdname);
	    ok = call_completion_function(funcname.contents);
	}
    }

    wb_destroy(&funcname);

    return ok;
}

/* Sets special local variables $WORDS and $TARGETWORD in the current variable
 * environment. */
void set_completion_variables(void)
{
    set_array(L VAR_WORDS, ctxt->pwordc, duparray(ctxt->pwords, copyaswcs),
	    SCOPE_LOCAL);
    set_variable(L VAR_TARGETWORD, xwcsdup(ctxt->src),
	    SCOPE_LOCAL, false);
}


/********** Completion Candidate Generation **********/

#define WMATCH(pattern, s) (xfnm_wmatch(pattern, s).start != (size_t) -1)

/* Perform completion for the specified candidate type(s). */
void simple_completion(le_candgentype_T type)
{
    le_compopt_T compopt = {
	.ctxt = ctxt,
	.type = type,
	.src = ctxt->src,
	.pattern = ctxt->pattern,
	.cpattern = NULL,
	.suffix = NULL,
	.terminate = true,
    };

    generate_candidates(&compopt);
}

/* Calls all candidate generation functions.
 * `compopt->cpattern' is freed in this function but is NOT set to NULL. */
void generate_candidates(const le_compopt_T *compopt)
{
    generate_file_candidates(compopt);
    generate_builtin_candidates(compopt);
    generate_external_command_candidates(compopt);
    generate_function_candidates(compopt);
    generate_keyword_candidates(compopt);
#if YASH_ENABLE_ALIAS
    generate_alias_candidates(compopt);
#endif
    generate_variable_candidates(compopt);
    generate_job_candidates(compopt);
    generate_signal_candidates(compopt);
    generate_logname_candidates(compopt);
    generate_group_candidates(compopt);
    generate_host_candidates(compopt);
    generate_bindkey_candidates(compopt);
    xfnm_free(compopt->cpattern);
}

/* Adds the specified value as a completion candidate to the candidate list.
 * The ignored prefix in `ctxt->origsrc' is prepended to the candidate value.
 * A description for the candidate can be given as `desc', which may be NULL
 * when no description is provided.
 * Arguments `value' and `desc' must be a freeable string, which is used as the
 * candidate value/description.
 * This function must NOT be used for a CT_FILE candidate.
 * If `value' is NULL, this function does nothing (except freeing `desc'). */
void le_new_candidate(le_candtype_T type, wchar_t *value, wchar_t *desc,
	const le_compopt_T *compopt)
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
    le_add_candidate(cand, compopt);
}

/* Adds the specified candidate to the candidate list.
 * The le_candidate_T structure must have been properly initialized, except for
 * the `origvalue' and `terminate' members, which are initialized in this
 * function.
 * This function treats the prefix and suffix specified in the "complete"
 * built-in invocation. */
void le_add_candidate(le_candidate_T *cand, const le_compopt_T *compopt)
{
    bool isdir = (cand->type == CT_FILE) &&
	S_ISDIR(cand->appendage.filestat.mode);

    xwcsbuf_T buf;
    wb_initwith(&buf, cand->value);

    /* prepend prefix */
    const wchar_t *origsrc = compopt->ctxt->src;
    size_t prefixlength = compopt->src - origsrc;
    if (prefixlength != 0)
	wb_ninsert_force(&buf, 0, origsrc, prefixlength);

    /* append suffix */
    if (compopt->suffix != NULL)
	wb_cat(&buf, compopt->suffix);
    else if (isdir && buf.length > 0 && buf.contents[buf.length - 1] != L'/')
	wb_wccat(&buf, L'/');

    cand->origvalue = wb_towcs(&buf);
    cand->value = cand->origvalue + prefixlength;
    cand->terminate = compopt->terminate &&
	(cand->type != CT_FILE || !S_ISDIR(cand->appendage.filestat.mode));

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
	    case CT_SIG:       typestr = "signal";                     break;
	    case CT_LOGNAME:   typestr = "user name";                  break;
	    case CT_GRP:       typestr = "group name";                 break;
	    case CT_HOSTNAME:  typestr = "host name";                  break;
	    case CT_BINDKEY:   typestr = "lineedit command";           break;
	}
	le_compdebug("new %s candidate \"%ls\"", typestr, cand->origvalue);
	if (cand->desc != NULL)
	    le_compdebug("  (desc: %ls)", cand->desc);
	if (!cand->terminate)
	    le_compdebug("  (no termination)");
    }

    pl_add(&le_candidates, cand);
}

/* Compiles the pattern `compopt->pattern' into `compopt->cpattern'
 * if not yet compiled. Returns true iff successful. */
bool le_compile_cpattern(const le_compopt_T *compopt)
{
    if (compopt->cpattern != NULL)
	return true;

    le_compopt_T *co = (le_compopt_T *) compopt;
    co->cpattern = xfnm_compile(
	    co->pattern, XFNM_HEADONLY | XFNM_TAILONLY);
    if (co->cpattern != NULL)
	return true;

    le_compdebug("failed to compile pattern \"%ls\"", co->pattern);
    return false;
}

/* Generates file name candidates.
 * The CGT_FILE, CGT_DIRECTORY, and CGT_EXECUTABLE flags specify what candidate
 * to generate. The other flags are ignored. */
void generate_file_candidates(const le_compopt_T *compopt)
{
    if (!(compopt->type & (CGT_FILE | CGT_DIRECTORY | CGT_EXECUTABLE)))
	return;

    le_compdebug("adding filenames for pattern \"%ls\"", compopt->pattern);

    enum wglbflags flags = WGLB_NOSORT;
    // if (shopt_nocaseglob)   flags |= WGLB_CASEFOLD;  XXX case-sensitive
    if (shopt_dotglob)      flags |= WGLB_PERIOD;
    if (shopt_extendedglob) flags |= WGLB_RECDIR;

    plist_T list;
    wglob(compopt->pattern, flags, pl_init(&list));

    for (size_t i = 0; i < list.length; i++) {
	wchar_t *name = list.contents[i];
	char *mbsname = malloc_wcstombs(name);
	struct stat st;
	if (mbsname != NULL &&
		(stat(mbsname, &st) >= 0 || lstat(mbsname, &st) >= 0)) {
	    bool executable = S_ISREG(st.st_mode) && is_executable(mbsname);
	    if ((compopt->type & CGT_FILE)
		    || ((compopt->type & CGT_DIRECTORY) && S_ISDIR(st.st_mode))
		    || ((compopt->type & CGT_EXECUTABLE) && executable)) {
		le_candidate_T *cand = xmalloc(sizeof *cand);
		cand->type = CT_FILE;
		cand->value = name;
		cand->rawvalue.raw = NULL;
		cand->rawvalue.width = 0;
		cand->desc = NULL;
		cand->rawdesc.raw = NULL;
		cand->rawdesc.width = 0;
		cand->appendage.filestat.is_executable = executable;
		cand->appendage.filestat.mode = st.st_mode;
		cand->appendage.filestat.nlink = st.st_nlink;
		cand->appendage.filestat.size = st.st_size;
		le_add_candidate(cand, compopt);
		name = NULL;
	    }
	}
	free(name);
	free(mbsname);
    }
    pl_destroy(&list);
}

/* Generates candidates that are the names of external commands matching the
 * pattern.
 * If CGT_EXTCOMMAND is not in `type', this function does nothing. */
void generate_external_command_candidates(const le_compopt_T *compopt)
{
    if (!(compopt->type & CGT_EXTCOMMAND))
	return;

    le_compdebug("adding external command names for pattern \"%ls\"",
	    compopt->pattern);
    if (!le_compile_cpattern(compopt))
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
	    if (xfnm_match(compopt->cpattern, de->d_name) != 0)
		continue;
	    sb_cat(&path, de->d_name);
	    if (is_executable_regular(path.contents))
		le_new_candidate(CT_COMMAND,
			malloc_mbstowcs(de->d_name), NULL, compopt);
	    sb_truncate(&path, dirpathlen);
	}
	sb_clear(&path);
	closedir(dir);
    }
    sb_destroy(&path);
}

/* Generates candidates that are keywords matching the pattern. */
void generate_keyword_candidates(const le_compopt_T *compopt)
{
    if (!(compopt->type & CGT_KEYWORD))
	return;

    le_compdebug("adding keywords matching pattern \"%ls\"", compopt->pattern);
    if (!le_compile_cpattern(compopt))
	return;

    static const wchar_t *keywords[] = {
	L"case", L"do", L"done", L"elif", L"else", L"esac", L"fi", L"for",
	L"function", L"if", L"then", L"until", L"while", NULL,
	// XXX "select" is not currently supported
    };

    for (const wchar_t **k = keywords; *k != NULL; k++)
	if (WMATCH(compopt->cpattern, *k))
	    le_new_candidate(CT_COMMAND, xwcsdup(*k), NULL, compopt);
}

/* Generates candidates to complete a user name matching the pattern. */
void generate_logname_candidates(const le_compopt_T *compopt)
{
    if (!(compopt->type & CGT_LOGNAME))
	return;

    le_compdebug("adding users matching pattern \"%ls\"", compopt->pattern);

#if HAVE_GETPWENT
    if (!le_compile_cpattern(compopt))
	return;

    struct passwd *pwd;
    setpwent();
    while ((pwd = getpwent()) != NULL)
	if (xfnm_match(compopt->cpattern, pwd->pw_name) == 0)
	    le_new_candidate(CT_LOGNAME, malloc_mbstowcs(pwd->pw_name),
# if HAVE_PW_GECOS
		    (pwd->pw_gecos != NULL) ? malloc_mbstowcs(pwd->pw_gecos) :
# endif
		    NULL,
		    compopt);
    endpwent();
#else
    le_compdebug("  getpwent not supported on this system");
#endif
}

/* Generates candidates to complete a group name matching the pattern. */
void generate_group_candidates(const le_compopt_T *compopt)
{
    if (!(compopt->type & CGT_GROUP))
	return;

    le_compdebug("adding groups matching pattern \"%ls\"", compopt->pattern);

#if HAVE_GETGRENT
    if (!le_compile_cpattern(compopt))
	return;

    struct group *grp;
    setgrent();
    while ((grp = getgrent()) != NULL)
	if (xfnm_match(compopt->cpattern, grp->gr_name) == 0)
	    le_new_candidate(
		    CT_GRP, malloc_mbstowcs(grp->gr_name), NULL, compopt);
    endgrent();
#else
    le_compdebug("  getgrent not supported on this system");
#endif
}

/* Generates candidates to complete a host name matching the pattern. */
void generate_host_candidates(const le_compopt_T *compopt)
{
    if (!(compopt->type & CGT_HOSTNAME))
	return;

    le_compdebug("adding hosts matching pattern \"%ls\"", compopt->pattern);

#if HAVE_GETHOSTENT
    if (!le_compile_cpattern(compopt))
	return;

    struct hostent *host;
    sethostent(true);
    while ((host = gethostent()) != NULL) {
	if (xfnm_match(compopt->cpattern, host->h_name) == 0)
	    le_new_candidate(
		    CT_HOSTNAME, malloc_mbstowcs(host->h_name), NULL, compopt);
	if (host->h_aliases != NULL)
	    for (char *const *a = host->h_aliases; *a != NULL; a++)
		if (xfnm_match(compopt->cpattern, *a) == 0)
		    le_new_candidate(
			    CT_HOSTNAME, malloc_mbstowcs(*a), NULL, compopt);
    }
    endhostent();
#else
    le_compdebug("  gethostent not supported on this system");
#endif
}

/* Generates candidates from words that match the pattern. */
void generate_candidates_from_words(
	le_candtype_T type, void *const *words, const wchar_t *description,
	const le_compopt_T *compopt)
{
    if (words[0] == NULL)
	return;

    le_compdebug("adding words matching pattern \"%ls\"", compopt->pattern);
    if (!le_compile_cpattern(compopt))
	return;

    for (; *words != NULL; words++) {
	wchar_t *word = *words;
	if (WMATCH(compopt->cpattern, word))
	    le_new_candidate(type, xwcsdup(word),
		    (description == NULL) ? NULL : xwcsdup(description),
		    compopt);
    }
}

/* Generates candidates with the specified word values.
 * The words must be given as (const wchar_t *).
 * The first argument `count' is the number of the words. */
void word_completion(size_t count, ...)
{
    va_list ap;
    le_compopt_T compopt = {
	.ctxt = ctxt,
	.type = 0,
	.src = ctxt->src,
	.pattern = ctxt->pattern,
	.cpattern = NULL,
	.suffix = NULL,
	.terminate = true,
    };

    va_start(ap, count);
    for (size_t i = 0; i < count; i++) {
	const wchar_t *word = va_arg(ap, const wchar_t *);
	if (matchwcsprefix(word, compopt.src))
	    le_new_candidate(CT_WORD, xwcsdup(word), NULL, &compopt);
    }
    va_end(ap);
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
 * If `finish' is true and if the completed candidate's `terminate' member is
 * true, the word is closed so that the next word can just be entered directly.
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

    if (!cand->terminate)
	return;

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

/* The "complete" builtin. */
int complete_builtin(int argc __attribute__((unused)), void **argv)
{
    static const struct xoption long_options[] = {
	{ L"array-variable",       OPTARG_NONE,     L'A', },
	{ L"alias",                OPTARG_NONE,     L'a', },
	{ L"bindkey",              OPTARG_NONE,     L'B', },
	{ L"builtin",              OPTARG_NONE,     L'b', },
	{ L"command",              OPTARG_NONE,     L'c', },
	{ L"description",          OPTARG_REQUIRED, L'D', },
	{ L"directory",            OPTARG_NONE,     L'd', },
	{ L"executable-file",      OPTARG_NONE,     L'E', },
	{ L"external-command",     OPTARG_NONE,     L'e', },
	{ L"file",                 OPTARG_NONE,     L'f', },
	{ L"group",                OPTARG_NONE,     L'g', },
	{ L"hostname",             OPTARG_NONE,     L'h', },
	{ L"signal",               OPTARG_NONE,     L'I', },
	{ L"running-job",          OPTARG_NONE,     L'J', },
	{ L"job",                  OPTARG_NONE,     L'j', },
	{ L"keyword",              OPTARG_NONE,     L'k', },
	{ L"global-alias",         OPTARG_NONE,     L'L', },
	{ L"normal-alias",         OPTARG_NONE,     L'N', },
	{ L"function",             OPTARG_NONE,     L'n', },
	{ L"option",               OPTARG_NONE,     L'O', },
	{ L"prefix",               OPTARG_REQUIRED, L'P', },
	{ L"semi-special-builtin", OPTARG_NONE,     L'q', },
	{ L"regular-builtin",      OPTARG_NONE,     L'r', },
	{ L"suffix",               OPTARG_REQUIRED, L'S', },
	{ L"special-builtin",      OPTARG_NONE,     L's', },
	{ L"no-termination",       OPTARG_NONE,     L'T', },
	{ L"username",             OPTARG_NONE,     L'u', },
	{ L"scalar-variable",      OPTARG_NONE,     L'V', },
	{ L"variable",             OPTARG_NONE,     L'v', },
	{ L"finished-job",         OPTARG_NONE,     L'Y', },
	{ L"stopped-job",          OPTARG_NONE,     L'Z', },
#if YASH_ENABLE_HELP
	{ L"help",                 OPTARG_NONE,     L'-', },
#endif
	{ NULL, 0, 0, },
    };

    const wchar_t *prefix = NULL, *suffix = NULL;
    const wchar_t *description = NULL;
    le_candgentype_T cgtype = 0;
    le_candtype_T candtype = CT_WORD;
    bool terminate = true;

    wchar_t opt;
    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(
		    argv, L"D:OP:S:Tabcdfghjkuv", long_options, NULL))) {
	switch (opt) {
	    case L'A':  cgtype |= CGT_ARRAY;       break;
	    case L'a':  cgtype |= CGT_ALIAS;       break;
	    case L'B':  cgtype |= CGT_BINDKEY;     break;
	    case L'b':  cgtype |= CGT_BUILTIN;     break;
	    case L'c':  cgtype |= CGT_COMMAND;     break;
	    case L'D':
		if (description != NULL)
		    goto dupopterror;
		description = xoptarg;
		break;
	    case L'd':  cgtype |= CGT_DIRECTORY;   break;
	    case L'E':  cgtype |= CGT_EXECUTABLE;  break;
	    case L'e':  cgtype |= CGT_EXTCOMMAND;  break;
	    case L'f':  cgtype |= CGT_FILE;        break;
	    case L'g':  cgtype |= CGT_GROUP;       break;
	    case L'h':  cgtype |= CGT_HOSTNAME;    break;
	    case L'I':  cgtype |= CGT_SIGNAL;      break;
	    case L'J':  cgtype |= CGT_RUNNING;     break;
	    case L'j':  cgtype |= CGT_JOB;         break;
	    case L'k':  cgtype |= CGT_KEYWORD;     break;
	    case L'L':  cgtype |= CGT_GALIAS;      break;
	    case L'N':  cgtype |= CGT_NALIAS;      break;
	    case L'n':  cgtype |= CGT_FUNCTION;    break;
	    case L'O':  candtype = CT_OPTION;      break;
	    case L'P':
		if (prefix != NULL)
		    goto dupopterror;
		prefix = xoptarg;
		break;
	    case L'q':  cgtype |= CGT_SSBUILTIN;   break;
	    case L'r':  cgtype |= CGT_RBUILTIN;    break;
	    case L'S':
		if (suffix != NULL)
		    goto dupopterror;
		suffix = xoptarg;
		break;
	    case L's':  cgtype |= CGT_SBUILTIN;    break;
	    case L'T':  terminate = false;         break;
	    case L'u':  cgtype |= CGT_LOGNAME;     break;
	    case L'V':  cgtype |= CGT_SCALAR;      break;
	    case L'v':  cgtype |= CGT_VARIABLE;    break;
	    case L'Y':  cgtype |= CGT_DONE;        break;
	    case L'Z':  cgtype |= CGT_STOPPED;     break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		fprintf(stderr,
		    gt("Usage:  complete [-T] [-P prefix] [-S suffix] \\"
			"        [-abcdfghjkuv] [[-O] [-D desc] words...]\n"));
		return Exit_ERROR;
dupopterror:
		xerror(0, Ngt("more than one -%lc option specified"),
			(wint_t) opt);
		return Exit_ERROR;
	}
    }

    if (ctxt == NULL) {
	xerror(0, Ngt("can only be used during command line completion"));
	return Exit_ERROR;
    }

    void *const *words = argv + xoptind;

    /* treat `prefix' */
    const wchar_t *src = ctxt->src, *pattern = ctxt->pattern;
    if (prefix != NULL) {
	src = matchwcsprefix(src, prefix);
	if (src == NULL) {
	    xerror(0, Ngt("the specified prefix `%ls' doesn't match "
			"the target word `%ls'"), prefix, ctxt->src);
	    return Exit_ERROR;
	}
	while (*prefix != L'\0') {
	    if (*pattern == L'\\')
		pattern++;
	    assert(*pattern != L'\0');
	    pattern++;
	    prefix++;
	}
    }

    le_compopt_T compopt = {
	.ctxt = ctxt,
	.type = cgtype,
	.src = src,
	.pattern = pattern,
	.cpattern = NULL,
	.suffix = suffix,
	.terminate = terminate,
    };

    size_t oldcount = le_candidates.length;
    generate_candidates_from_words(candtype, words, description, &compopt);
    generate_candidates(&compopt);
    size_t newcount = le_candidates.length;

    return oldcount != newcount ? Exit_SUCCESS : Exit_FAILURE;
}

#if YASH_ENABLE_HELP
const char complete_help[] = Ngt(
"complete - generate completion candidates\n"
"\tcomplete [-T] [-P prefix] [-S suffix] \\\n"
"\t         [-abcdfghjkuv] [[-O] [-D description] words...]\n"
"The \"complete\" built-in is called from completion functions and generates\n"
"completion candidates according to the given arguments.\n"
"\n"
"The following options specify types of candidates to generate:\n"
" -a --alias                 aliases (global and non-global)\n"
"    --array-variable        array variables\n"
"    --bindkey               command names for line-editing key bindings\n"
" -b --builtin               built-in commands\n"
" -c --command               commands and shell keywords\n"
" -d --directory             directories\n"
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
"    --regular-builtin       regular built-ins\n"
"    --running-job           running job names\n"
"    --scalar-variable       normal (non-array) variables\n"
"    --semi-special-builtin  semi-special built-ins\n"
"    --signal                signal names\n"
"    --special-builtin       special built-ins\n"
"    --stopped-job           stopped job names\n"
" -u --username              user names\n"
" -v --variable              variable names\n"
"Any operands to the \"complete\" built-in are treated as candidates.\n"
"The following options can be used to add attributes to candidates generated\n"
"in this way:\n"
" -O  --option\n"
"\tThe candidates are treated as command line options.\n"
" -D ...  --description=...\n"
"\tGive a description of the candidates. The description is shown\n"
"\tbeside the candidates in the candidate list.\n"
"\n"
"The followed options can be used for candidate generation both by options\n"
"and by operands.\n"
" -T  --no-termination\n"
"\tWhen the command line word is completed using a candidate that is\n"
"\tgenerated with this option, a space is not appended to the completed\n"
"\tword.\n"
" -P ...  --prefix=...\n"
"\tIn pattern matching during the candidate generation, the prefix of\n"
"\t$TARGETWORD specified by this option is ignored.\n"
" -S ...  --suffix=...\n"
"\tAfter candidates are generated as usual, the suffix specified by\n"
"\tthis option is added to each candidate generated.\n"
);
#endif /* YASH_ENABLE_HELP */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
