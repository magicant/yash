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
#include "../path.h"
#include "../plist.h"
#include "../util.h"
#include "../variable.h"
#include "../xfnmatch.h"
#include "complete.h"
#include "compparse.h"
#include "display.h"
#include "editing.h"
#include "lineedit.h"
#include "terminfo.h"


static void free_candidate(void *c)
    __attribute__((nonnull));
static void sort_candidates(void);
static int sort_candidates_cmp(const void *cp1, const void *cp2)
    __attribute__((nonnull));

static const le_candgen_T *get_candgen(const le_context_T *context)
    __attribute__((nonnull));
static const le_candgen_T *find_optarg_candgen(
	const hashtable_T *options, const wchar_t *s)
    __attribute__((nonnull));
static void generate_candidates(const le_candgen_T *candgen)
    __attribute__((nonnull));
static void generate_file_candidates(
	le_candgentype_T type, const wchar_t *pattern)
    __attribute__((nonnull));
static void generate_external_command_candidates(
	le_candgentype_T type, const le_context_T *context)
    __attribute__((nonnull));
static void list_long_options(
	const hashtable_T *options, const wchar_t *s, plist_T *list)
    __attribute__((nonnull));
static void generate_keyword_candidates(
	le_candgentype_T type, const le_context_T *context)
    __attribute__((nonnull));
static void generate_option_candidates(
	le_candgentype_T type, const le_context_T *context)
    __attribute__((nonnull));
static void generate_logname_candidates(
        le_candgentype_T type, const le_context_T *context)
    __attribute__((nonnull));
static void generate_group_candidates(
        le_candgentype_T type, const le_context_T *context)
    __attribute__((nonnull));
static void generate_host_candidates(
        le_candgentype_T type, const le_context_T *context)
    __attribute__((nonnull));
static void generate_candidates_from_words(
	void *const *words, const le_context_T *context)
    __attribute__((nonnull(2)));

static void calculate_common_prefix_length(void);
static void update_main_buffer(void);
static bool need_subst(const le_context_T *context)
    __attribute__((nonnull));
static void substitute_source_word(const le_context_T *context)
    __attribute__((nonnull));
static void quote(xwcsbuf_T *buf, const wchar_t *s, le_quote_T quotetype)
    __attribute__((nonnull));

/* A list that contains the current completion candidates.
 * The elements pointed to by `le_candidates.contains[*]' are of type
 * `le_candidate_T'. */
plist_T le_candidates = { .contents = NULL };
/* The index of the currently selected candidate in `le_candidates'.
 * When no candidate is selected, the index is `le_candidates.length'. */
size_t le_selected_candidate_index;

/* The index of the cursor before completion started. */
static size_t insertion_index;
/* The type of context at the current position. */
static le_contexttype_T ctxttype;
/* The type of quotation at the current position. */
static le_quote_T quotetype;
/* The length of the expanded source word, not including backslash escapes. */
static size_t expanded_source_word_length;
/* The length of the longest common prefix of the current candidates. */
static size_t common_prefix_length;

/* The context in which completion is being performed. */
static const le_context_T *ctxt = NULL;


/* Performs command line completion.
 * Existing candidates are deleted, if any, and candidates are computed from
 * the current command line. */
void le_complete(void)
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
	le_state = LE_STATE_SUSPENDED_COMPDEBUG;
	le_compdebug("completion start");
    }

    le_complete_cleanup();
    pl_init(&le_candidates);

    le_context_T context;
    if (!le_get_context(&context))
	goto display;

    insertion_index = le_main_index;
    ctxttype = context.type;
    quotetype = context.quote;
    expanded_source_word_length = wcslen(context.src);

    ctxt = &context;
    generate_candidates(get_candgen(ctxt));
    ctxt = NULL;
    sort_candidates();
    le_compdebug("total of %zu candidate(s)", le_candidates.length);

display:  /* display the results */
    if (le_candidates.length == 0) {
	le_selected_candidate_index = 0;
    } else if (context.substsrc || need_subst(&context)) {
	le_selected_candidate_index = 0;
	substitute_source_word(&context);
	le_complete_cleanup();
    } else if (le_candidates.length == 1) {
	le_selected_candidate_index = 0;
	update_main_buffer();
	le_complete_cleanup();
    } else {
	calculate_common_prefix_length();
	if (le_state == LE_STATE_SUSPENDED_COMPDEBUG) {
	    const le_candidate_T *cand = le_candidates.contents[0];
	    wchar_t *common_prefix
		= xwcsndup(cand->value, common_prefix_length);
	    le_compdebug("candidate common prefix: \"%ls\"", common_prefix);
	    free(common_prefix);
	}
	le_selected_candidate_index = le_candidates.length;
	update_main_buffer();
    }
    le_free_context(&context);

    if (le_state == LE_STATE_SUSPENDED_COMPDEBUG) {
	le_compdebug("completion end");
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

    update_main_buffer();
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
	    if (wcscoll(cand1->value, cand2->value) == 0) {// XXX case-sensitive
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
    return wcscoll(cand1->value, cand2->value); // XXX case-sensitive
}

/* Prints the formatted string to the standard error if the completion debugging
 * option is on.
 * The string is preceded by "[compdebug] " and followed by a newline. */
void le_compdebug(const char *format, ...)
{
    if (le_state != LE_STATE_SUSPENDED_COMPDEBUG)
	return;

    fputs("[compdebug] ", stderr);

    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);

    fputc('\n', stderr);
}


/********** Completion Candidate Generation **********/

#define WMATCH(pattern, s) (xfnm_wmatch(pattern, s).start != (size_t) -1)

/* The type of data that specifies how to generate candidates for a command's
 * arguments. The `operands' member is used for completing a operand.
 *
 * The `options' member is a hashtable that contains data for completing an
 * option. The hashtable's keys are pointers to freeable strings containing
 * option names and values are pointers to freeable `le_candgen_T' structures.
 * For a single-character option, the key is the single-character string
 * containing the option character. For a long option, the key string is the
 * option name prefixed by a hyphen.
 * For an option that does not take an argument, the corresponding value is
 * NULL. For an option that takes an argument, the value is a non-NULL pointer
 * to an `le_candgen_T' structure.
 * When the `options' hashtable contains no keys, the hashtable may be
 * uninitialized, in which case the `options.capacity' member must be zero.
 *
 * The `option_after_operand' member specifies whether options can appear after
 * operands or not. If this member is false, all arguments after the first
 * operand are considered as operands. */
struct cmdcandgen_T {
    le_candgen_T operands;
    hashtable_T options;
    bool option_after_operand;
};

/* A hashtable that maps command names to candidate generation data.
 * A key is a pointer to a freeable wide string containing a command name.
 * A value is a pointer to a freeable `cmdcandgen_T' structure.
 * When the hashtable is not yet initialized, the capacity is zero. */
static hashtable_T candgens = { .capacity = 0 };

/* Determines how to generate candidates.
 * The context in which the completion is performed is specified by `context'.
 * The result is valid until the next call to this function or `set_candgen'. */
const le_candgen_T *get_candgen(const le_context_T *context)
{
    static le_candgen_T tempresult;

    // TODO test
    // TODO don't add CGT_KEYWORD/CGT_ALIAS when quoted
    tempresult = (le_candgen_T) { .type = 0, .words = NULL, .function = NULL };
    switch (context->type) {
	case CTXT_NORMAL:
	case CTXT_VAR_BRC_WORD:
	    if (context->pwordc == 0) {
		if (wcschr(context->src, L'/')) {
		    tempresult.type |= CGT_DIRECTORY | CGT_EXECUTABLE;
		} else {
		    tempresult.type |= CGT_DIRECTORY | CGT_KEYWORD
			| CGT_COMMAND | CGT_ALIAS;
		}
		return &tempresult;
	    } else {
		goto normal;
	    }
	case CTXT_TILDE:
	    tempresult.type |= CGT_LOGNAME;
	    return &tempresult;
	case CTXT_VAR:
	case CTXT_VAR_BRC:
	    tempresult.type |= CGT_VARIABLE;
	    return &tempresult;
	case CTXT_ARITH:
	    tempresult.type |= CGT_SCALAR;
	    return &tempresult;
	case CTXT_ASSIGN:
	    tempresult.type |= CGT_FILE;
	    return &tempresult;
	case CTXT_REDIR:
	    tempresult.type |= CGT_FILE | CGT_GALIAS;
	    return &tempresult;
	case CTXT_REDIR_FD:
	    tempresult.type |= CGT_GALIAS;
	    return &tempresult;
    }
    assert(false);

    const struct cmdcandgen_T *ccg;
normal:
    if (candgens.capacity == 0)
	goto return_default;
    ccg = ht_get(&candgens, context->pwords[0]).value;
    if (ccg == NULL)
	goto return_default;

    /* parse the current command arguments */
    for (int i = 1; i < context->pwordc; i++) {
	const wchar_t *s = context->pwords[i];
	if (s[0] != L'-') {
	    if (ccg->option_after_operand)
		continue;
	    else
		return &ccg->operands;
	}
	if (s[1] == L'-' && s[2] == L'\0')  /* s == L"--" */
	    return &ccg->operands;

	const le_candgen_T *candgen = find_optarg_candgen(&ccg->options, s);
	if (candgen == NULL)
	    continue;
	if (++i == context->pwordc)
	    return candgen;
    }
    if (context->src[0] == L'-') {
	tempresult.type = CGT_OPTION;
	return &tempresult;
    } else {
	return &ccg->operands;
    }

return_default:
    tempresult.type = CGT_FILE | CGT_GALIAS;
    return &tempresult;
}

/* Searches `options' for an option whose argument should be given as the next
 * command-line argument. The current argument word that starts with an hyphen
 * must be given as `s'. */
const le_candgen_T *find_optarg_candgen(
	const hashtable_T *options, const wchar_t *s)
{
    // TODO test

    assert(s[0] == L'-');

    kvpair_T kv = ht_get(options, s);
    if (kv.key != NULL)
	return kv.value;

    if (s[1] == L'-') {
	plist_T list;
	list_long_options(options, s, pl_init(&list));
	if (list.length == 1 && !wcschr(s, L'=')) {
	    s = list.contents[0];
	    pl_destroy(&list);
	    return ht_get(options, s).value;
	}
	pl_destroy(&list);
    } else {
	wchar_t opt[2];
	opt[1] = L'\0';
	while ((opt[0] = *++s) != L'\0') {
	    kv = ht_get(options, opt);
	    if (kv.value != NULL) {
		if (*++s != L'\0')
		    break;
		else
		    return kv.value;
	    }
	}
    }
    return NULL;
}

/* Generates completion candidates according to the specified generation policy.
 * The candidate list must have been initialized when this function is called.*/
void generate_candidates(const le_candgen_T *candgen)
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
    generate_shopt_candidates(candgen->type, ctxt);
    generate_signal_candidates(candgen->type, ctxt);
    generate_logname_candidates(candgen->type, ctxt);
    generate_group_candidates(candgen->type, ctxt);
    generate_host_candidates(candgen->type, ctxt);
    generate_bindkey_candidates(candgen->type, ctxt);
    generate_candidates_from_words(candgen->words, ctxt);
    generate_candidates_using_function(candgen->function, ctxt);
    /* `generate_candidates_using_function' must be last because the function
     * execution may modify `candgen'. */
}

/* Adds the specified value as a completion candidate to the candidate list.
 * The value is freed in this function.
 * If the specified `type' does not match `cgt', it may not be added. */
void le_add_candidate(le_candgentype_T cgt, le_candtype_T type, wchar_t *value)
{
    le_candidate_T *cand = xmalloc(sizeof *cand);
    cand->type = type;
    cand->value = value;
    cand->rawvalue = NULL;
    cand->width = 0;
    if (type == CT_FILE) {
	char *filename = malloc_wcstombs(value);
	struct stat st;

	if (filename == NULL || stat(filename, &st) < 0) {
	    cand->filestat.is_executable = false;
	    cand->filestat.mode = 0;
	    cand->filestat.nlink = 0;
	    cand->filestat.size = 0;
	} else {
	    cand->filestat.mode = st.st_mode;
	    cand->filestat.nlink = st.st_nlink;
	    cand->filestat.size = st.st_size;
	    cand->filestat.is_executable =
		S_ISREG(st.st_mode) && is_executable(filename);
	}
	free(filename);

	if (cgt & CGT_FILE)
	    goto ok;
	if ((cgt & CGT_DIRECTORY) && S_ISDIR(cand->filestat.mode))
	    goto ok;
	if ((cgt & CGT_EXECUTABLE) && cand->filestat.is_executable)
	    goto ok;
	goto fail;
    }

ok:
    if (le_state == LE_STATE_SUSPENDED_COMPDEBUG) {
	const char *typestr = NULL;
	switch (type) {
	    case CT_WORD:      typestr = "word";                       break;
	    case CT_FILE:      typestr = "file";                       break;
	    case CT_COMMAND:   typestr = "command";                    break;
	    case CT_ALIAS:     typestr = "alias";                      break;
	    case CT_OPTION:    typestr = "option";                     break;
	    case CT_OPTIONA:   typestr = "argument-requiring option";  break;
	    case CT_VAR:       typestr = "variable";                   break;
	    case CT_FUNC:      typestr = "function";                   break;
	    case CT_JOB:       typestr = "job";                        break;
	    case CT_SHOPT:     typestr = "shell option";               break;
	    case CT_FD:        typestr = "file descriptor";            break;
	    case CT_SIG:       typestr = "signal";                     break;
	    case CT_LOGNAME:   typestr = "user name";                  break;
	    case CT_GRP:       typestr = "group name";                 break;
	    case CT_HOSTNAME:  typestr = "host name";                  break;
	    case CT_BINDKEY:   typestr = "lineedit command";           break;
	}
	le_compdebug("new %s candidate \"%ls\"", typestr, value);
    }

    pl_add(&le_candidates, cand);
    return;

fail:
    free_candidate(cand);
}

/* Generates file name candidates that match the specified glob pattern.
 * The CGT_FILE, CGT_DIRECTORY, and CGT_EXECUTABLE flags specify what candidate
 * to generate. The other flags are ignored. */
void generate_file_candidates(le_candgentype_T type, const wchar_t *pattern)
{
    if (!(type & (CGT_FILE | CGT_DIRECTORY | CGT_EXECUTABLE)))
	return;

    le_compdebug("adding filenames for pattern \"%ls\"", pattern);

    plist_T list;
    enum wglbflags flags = WGLB_NOSORT;
    // if (shopt_nocaseglob)   flags |= WGLB_CASEFOLD;  XXX case-sensitive
    if (shopt_dotglob)      flags |= WGLB_PERIOD;
    if (shopt_extendedglob) flags |= WGLB_RECDIR;
    wglob(pattern, flags, pl_init(&list));

    for (size_t i = 0; i < list.length; i++)
	le_add_candidate(type, CT_FILE, list.contents[i]);
    pl_destroy(&list);
}

/* Generates candidates that are the names of external commands matching the
 * pattern in the specified context.
 * If CGT_EXECUTABLE is not in `type', this function does nothing. */
void generate_external_command_candidates(
	le_candgentype_T type, const le_context_T *context)
{
    if (!(type & CGT_EXTCOMMAND))
	return;

    le_compdebug("adding external command names for pattern \"%ls\"",
	    context->pattern);

    char *const *paths = get_path_array(PA_PATH);
    const char *dirpath;
    xstrbuf_T path;

    if (paths == NULL)
	return;
    sb_init(&path);
    while ((dirpath = *paths) != NULL) {
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
	    if (is_executable_regular(path.contents)) {
		wchar_t *wname = malloc_mbstowcs(de->d_name);
		if (wname != NULL)
		    le_add_candidate(type, CT_COMMAND, wname);
	    }
	    sb_truncate(&path, dirpathlen);
	}
	sb_clear(&path);
	closedir(dir);

	paths++;
    }
    sb_destroy(&path);
}

/* Adds pointers that point to option names that starts with `s' to the
 * specified list. The added pointers point to wide strings, which must not be
 * modified or freed. */
void list_long_options(
    const hashtable_T *options, const wchar_t *s, plist_T *list)
{
    // TODO test

    const wchar_t *name;
    size_t i = 0;

    assert(s[0] == L'-');
    while ((name = ht_next(options, &i).key) != NULL) {
	if (matchwcsprefix(name, s))
	    pl_add(list, name);
    }
}

/* Generates candidates that are keywords matching the pattern in the specified
 * context. */
void generate_keyword_candidates(
	le_candgentype_T type, const le_context_T *context)
{
    if (!(type & CGT_KEYWORD))
	return;

    le_compdebug("adding keywords matching pattern \"%ls\"", context->pattern);

    static const wchar_t *keywords[] = {
	L"case", L"do", L"done", L"elif", L"else", L"esac", L"fi", L"for",
	L"if", L"then", L"until", L"while", NULL,
	// XXX "function" and "select" is not currently supported
    };

    for (const wchar_t **k = keywords; *k != NULL; k++)
	if (WMATCH(context->cpattern, *k))
	    le_add_candidate(type, CT_COMMAND, xwcsdup(*k));
}

/* Generates candidates to complete an option matching the pattern in the
 * specified context. */
void generate_option_candidates(
	le_candgentype_T type, const le_context_T *context)
{
    const wchar_t *src = context->src;
    if (!(type & CGT_OPTION) || src[0] != L'-')
	return;

    le_compdebug("adding options matching pattern \"%ls\"", context->pattern);
    if (candgens.capacity == 0)
	return;

    const struct cmdcandgen_T *ccg =
	ht_get(&candgens, context->pwords[0]).value;
    if (ccg == NULL)
	return;

    bool islong = (src[1] == L'-');
    size_t i = 0;
    kvpair_T kv;
    while ((kv = ht_next(&ccg->options, &i)).key != NULL) {
	const wchar_t *optname = kv.key;
	const le_candgen_T *arggen = kv.value;
	le_candtype_T ct = (arggen == NULL) ? CT_OPTION : CT_OPTIONA;

	if (optname[0] != L'-') {
	    if (!islong)
		le_add_candidate(type, ct,
			malloc_wprintf(L"%ls%lc", src, optname[0]));
	} else {
	    if ((islong == (optname[1] == L'-'))
		    && WMATCH(context->cpattern, optname))
		le_add_candidate(type, ct, xwcsdup(optname));
	}
    }
}

/* Generates candidates to complete a user name matching the pattern in the
 * specified context. */
void generate_logname_candidates(
        le_candgentype_T type, const le_context_T *context)
{
    if (!(type & CGT_LOGNAME))
	return;

    le_compdebug("adding users matching pattern \"%ls\"", context->pattern);

#if HAVE_GETPWENT
    struct passwd *pwd;
    while ((pwd = getpwent()) != NULL)
	if (xfnm_match(context->cpattern, pwd->pw_name) == 0)
	    le_add_candidate(type, CT_LOGNAME, malloc_mbstowcs(pwd->pw_name));
    endpwent();
#else
    le_compdebug("  getpwent not supported on this system");
#endif
}

/* Generates candidates to complete a group name matching the pattern in the
 * specified context. */
void generate_group_candidates(
        le_candgentype_T type, const le_context_T *context)
{
    if (!(type & CGT_GROUP))
	return;

    le_compdebug("adding groups matching pattern \"%ls\"", context->pattern);

#if HAVE_GETGRENT
    struct group *grp;
    while ((grp = getgrent()) != NULL)
	if (xfnm_match(context->cpattern, grp->gr_name) == 0)
	    le_add_candidate(type, CT_GRP, malloc_mbstowcs(grp->gr_name));
    endgrent();
#else
    le_compdebug("  getgrent not supported on this system");
#endif
}

/* Generates candidates to complete a host name matching the pattern in the
 * specified context. */
void generate_host_candidates(
        le_candgentype_T type, const le_context_T *context)
{
    if (!(type & CGT_HOSTNAME))
	return;

    le_compdebug("adding hosts matching pattern \"%ls\"", context->pattern);

#if HAVE_GETHOSTENT
    struct hostent *host;
    sethostent(1);
    while ((host = gethostent()) != NULL) {
	if (xfnm_match(context->cpattern, host->h_name) == 0)
	    le_add_candidate(type, CT_HOSTNAME, malloc_mbstowcs(host->h_name));
	if (host->h_aliases != NULL) {
	    for (char *const *a = host->h_aliases; *a != NULL; a++) {
		if (xfnm_match(context->cpattern, *a) == 0)
		    le_add_candidate(type, CT_HOSTNAME, malloc_mbstowcs(*a));
	    }
	}
    }
    endhostent();
#else
    le_compdebug("  gethostent not supported on this system");
#endif
}

/* Generates candidates from words that match the pattern in the specified
 * context. */
void generate_candidates_from_words(
	void *const *words, const le_context_T *context)
{
    if (words == NULL)
	return;

    le_compdebug("adding predefined words for pattern \"%ls\"",
	    context->pattern);

    for (; *words != NULL; words++)
	if (xfnm_wmatch(context->cpattern, *words).start != (size_t) -1)
	    le_add_candidate(0, CT_WORD, xwcsdup(*words));
}


/********** Displaying Functions **********/

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
void update_main_buffer(void)
{
    const le_candidate_T *cand;
    wchar_t *value;
    xwcsbuf_T buf;

    wb_init(&buf);
    if (le_selected_candidate_index >= le_candidates.length) {
	cand = le_candidates.contents[0];
	value = xwcsndup(cand->value + expanded_source_word_length,
		common_prefix_length - expanded_source_word_length);
	quote(&buf, value, quotetype);
	free(value);
    } else {
	cand = le_candidates.contents[le_selected_candidate_index];
	quote(&buf, cand->value + expanded_source_word_length, quotetype);
    }
    wb_replace_force(&le_main_buffer,
	    insertion_index, le_main_index - insertion_index,
	    buf.contents, buf.length);
    le_main_index = insertion_index + buf.length;
    wb_destroy(&buf);

    if (le_selected_candidate_index >= le_candidates.length)
	return;

    if (cand->type == CT_FILE && S_ISDIR(cand->filestat.mode)) {
	size_t len = wcslen(cand->value);
	if (len > 0 && cand->value[len - 1] != L'/') {
	    wb_ninsert_force(&le_main_buffer, le_main_index, L"/", 1);
	    le_main_index += 1;
	}
    } else if (cand->type == CT_OPTIONA) {
	wb_ninsert_force(&le_main_buffer, le_main_index, L"=", 1);
	le_main_index += 1;
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

	if (le_candidates.length == 1) {
	    switch (ctxttype) {
		case CTXT_NORMAL:
		case CTXT_ARITH:
		case CTXT_ASSIGN:
		case CTXT_REDIR:
		case CTXT_REDIR_FD:
		    wb_ninsert_force(&le_main_buffer, le_main_index, L" ", 1);
		    le_main_index += 1;
		    break;
		case CTXT_TILDE:
		    wb_ninsert_force(&le_main_buffer, le_main_index, L"/", 1);
		    le_main_index += 1;
		    break;
		case CTXT_VAR:
		    break;
		case CTXT_VAR_BRC:
		case CTXT_VAR_BRC_WORD:
		    wb_ninsert_force(&le_main_buffer, le_main_index, L"}", 1);
		    le_main_index += 1;
		    break;
	    }
	}
    }
}

/* Determines whether the source word should be substituted even if
 * `context->substsrc' is false. */
/* Returns true if there is a candidate that does not begin with
 * `context->args[argc - 1]'. */
bool need_subst(const le_context_T *context)
{
    for (size_t i = 0; i < le_candidates.length; i++) {
	const le_candidate_T *cand = le_candidates.contents[i];
	if (!matchwcsprefix(cand->value, context->src))
	    return true;
    }
    return false;
}

/* Substitutes the source word in the main buffer with all of the current
 * candidates. */
void substitute_source_word(const le_context_T *context)
{
    le_compdebug("substituting source word with candidate(s)");

    /* remove source word */
    wb_remove(&le_main_buffer, context->srcindex,
	    le_main_index - context->srcindex);
    le_main_index = context->srcindex;

    /* insert candidates */
    xwcsbuf_T buf;
    wb_init(&buf);
    for (size_t i = 0; i < le_candidates.length; i++) {
	const le_candidate_T* cand = le_candidates.contents[i];

	quote(wb_clear(&buf), cand->value, QUOTE_NORMAL);
	wb_ninsert_force(&le_main_buffer, le_main_index,
		buf.contents, buf.length);
	le_main_index += buf.length;

	wb_ninsert_force(&le_main_buffer, le_main_index, L" ", 1);
	le_main_index += 1;
    }
    wb_destroy(&buf);
}

/* Quotes characters in the specified string that are not treated literally
 * according to `quotetype'.
 * The result is appended to the specified buffer, which must have been
 * initialized by the caller. */
void quote(xwcsbuf_T *buf, const wchar_t *s, le_quote_T quotetype)
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
	const wchar_t *targetcommand, const wchar_t *targetoption);
static int print_candgen(
	const wchar_t *targetcommand, const wchar_t *targetoption,
	const le_candgen_T *candgen, bool intermix)
    __attribute__((nonnull(1)));
static bool quote_print(const wchar_t *word)
    __attribute__((nonnull));
static bool need_quote(const wchar_t *s)
    __attribute__((nonnull));
static void complete_builtin_remove(
	const wchar_t *targetcommand, const wchar_t *targetoption);
static void free_cmdcandgen(struct cmdcandgen_T *ccg);
static void free_candgen(le_candgen_T *candgen);
static void free_candgen_contents(const le_candgen_T *candgen)
    __attribute__((nonnull));
static int complete_builtin_set(
	const wchar_t *targetcommand, const wchar_t *targetoption,
	const le_candgen_T *candgen, bool intermix)
    __attribute__((nonnull(1,3)));
static void clone_candgen(
	le_candgen_T *restrict copy, const le_candgen_T *restrict orig)
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
	{ L"finished-job",         xno_argument,       L'D', },
	{ L"directory",            xno_argument,       L'd', },
	{ L"executable-file",      xno_argument,       L'E', },
	{ L"external-command",     xno_argument,       L'e', },
	{ L"generator-function",   xrequired_argument, L'F', },
	{ L"file",                 xno_argument,       L'f', },
	{ L"global-alias",         xno_argument,       L'G', },
	{ L"group",                xno_argument,       L'g', },
	{ L"hostname",             xno_argument,       L'h', },
	{ L"signal",               xno_argument,       L'I', },
	{ L"running-job",          xno_argument,       L'J', },
	{ L"job",                  xno_argument,       L'j', },
	{ L"keyword",              xno_argument,       L'k', },
	{ L"normal-alias",         xno_argument,       L'N', },
	{ L"function",             xno_argument,       L'n', },
	{ L"target-option",        xrequired_argument, L'O', },
	{ L"shell-option",         xno_argument,       L'o', },
	{ L"print",                xno_argument,       L'P', },
	{ L"remove",               xno_argument,       L'R', },
	{ L"regular-builtin",      xno_argument,       L'r', },
	{ L"semi-special-builtin", xno_argument,       L'S', },
	{ L"special-builtin",      xno_argument,       L's', },
	{ L"username",             xno_argument,       L'u', },
	{ L"scalar-variable",      xno_argument,       L'V', },
	{ L"variable",             xno_argument,       L'v', },
	{ L"intermix",             xno_argument,       L'X', },
	{ L"stopped-job",          xno_argument,       L'Z', },
#if YASH_ENABLE_HELP
	{ L"help",                 xno_argument,       L'-', },
#endif
	{ NULL, 0, 0, },
    };

    const wchar_t *targetcommand = NULL, *targetoption = NULL;
    bool intermix = false;
    bool print = false, remove = false;
    le_candgen_T candgen = { .type = 0, .words = NULL, .function = NULL };

    wchar_t opt;
    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(
		    argv, L"C:F:O:PRXabcdfghjkouv", long_options, NULL))) {
	switch (opt) {
	    case L'A':  candgen.type |= CGT_ARRAY;       break;
	    case L'a':  candgen.type |= CGT_ALIAS;       break;
	    case L'B':  candgen.type |= CGT_BINDKEY;     break;
	    case L'b':  candgen.type |= CGT_BUILTIN;     break;
	    case L'C':
		if (targetcommand != NULL)
		    goto dupopterror;
		targetcommand = xoptarg;
		break;
	    case L'c':  candgen.type |= CGT_COMMAND;     break;
	    case L'D':  candgen.type |= CGT_DONE;        break;
	    case L'd':  candgen.type |= CGT_DIRECTORY;   break;
	    case L'E':  candgen.type |= CGT_EXECUTABLE;  break;
	    case L'e':  candgen.type |= CGT_EXTCOMMAND;  break;
	    case L'F':
		if (candgen.function != NULL)
		    goto dupopterror;
		candgen.function = xoptarg;
		break;
	    case L'f':  candgen.type |= CGT_FILE;        break;
	    case L'G':  candgen.type |= CGT_GALIAS;      break;
	    case L'g':  candgen.type |= CGT_GROUP;       break;
	    case L'h':  candgen.type |= CGT_HOSTNAME;    break;
	    case L'I':  candgen.type |= CGT_SIGNAL;      break;
	    case L'J':  candgen.type |= CGT_RUNNING;     break;
	    case L'j':  candgen.type |= CGT_JOB;         break;
	    case L'k':  candgen.type |= CGT_KEYWORD;     break;
	    case L'N':  candgen.type |= CGT_NALIAS;      break;
	    case L'n':  candgen.type |= CGT_FUNCTION;    break;
	    case L'O':
		if (targetoption != NULL)
		    goto dupopterror;
		targetoption = xoptarg;
		break;
	    case L'o':  candgen.type |= CGT_SHOPT;       break;
	    case L'P':  print = true;                    break;
	    case L'R':  remove = true;                   break;
	    case L'r':  candgen.type |= CGT_RBUILTIN;    break;
	    case L'S':  candgen.type |= CGT_SSBUILTIN;   break;
	    case L's':  candgen.type |= CGT_SBUILTIN;    break;
	    case L'u':  candgen.type |= CGT_LOGNAME;     break;
	    case L'V':  candgen.type |= CGT_SCALAR;      break;
	    case L'v':  candgen.type |= CGT_VARIABLE;    break;
	    case L'X':  intermix = true;                 break;
	    case L'Z':  candgen.type |= CGT_STOPPED;     break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		fprintf(stderr,
		    gt("Usage:  complete [-C command] [-O option] [-PX]\n"
		       "                 [-abcdfghjkouv] [-F function] "
		                         "[words...]\n"));
		return Exit_ERROR;
dupopterror:
		xerror(0, Ngt("more than one -C/-O/-F option specified"));
		return Exit_ERROR;
	}
    }
    if (targetoption != NULL && targetcommand == NULL) {
	xerror(0, Ngt("-O option specified without -C option"));
	return Exit_ERROR;
    } else if (print && remove) {
	xerror(0, Ngt("-P option cannot be used with -R option"));
	return Exit_ERROR;
    }
    if (xoptind < argc)
	candgen.words = argv + xoptind;

    if (print) {
	return complete_builtin_print(targetcommand, targetoption);
    } else if (remove) {
	complete_builtin_remove(targetcommand, targetoption);
	return Exit_SUCCESS;
    } else if (targetcommand == NULL) {
	if (ctxt == NULL) {
	    xerror(0, Ngt("not in candidate generator function"));
	    return Exit_FAILURE;
	}
	generate_candidates(&candgen);
	return Exit_SUCCESS;
    } else {
	return complete_builtin_set(
		targetcommand, targetoption, &candgen, intermix);
    }
}

/* Prints commands that can be used to restore the current completion settings
 * for the specified command/target.
 * If the command is not specified, all completion settings are printed. */
int complete_builtin_print(
	const wchar_t *targetcommand, const wchar_t *targetoption)
{
    if (candgens.capacity == 0 || candgens.count == 0)
	return Exit_SUCCESS;

    const struct cmdcandgen_T *ccg;
    const le_candgen_T *candgen;

    if (targetcommand != NULL) {
	ccg = ht_get(&candgens, targetcommand).value;
	if (ccg == NULL)
	    return Exit_FAILURE;
	if (targetoption == NULL)
	    candgen = &ccg->operands;
	else
	    candgen = ht_get(&ccg->options, targetoption).value;
	return print_candgen(targetcommand, targetoption,
		candgen, ccg->option_after_operand);
    } else {
	kvpair_T *kvs = ht_tokvarray(&candgens);
	qsort(kvs, candgens.count, sizeof *kvs, keywcscoll);
	for (size_t i = 0; i < candgens.count; i++) {
	    ccg = kvs[i].value;
	    if (ccg != NULL) {
		print_candgen(kvs[i].key, NULL,
			&ccg->operands, ccg->option_after_operand);
		if (yash_error_message_count != 0)
		    break;

		if (ccg->options.capacity != 0 && ccg->options.count > 0) {
		    kvpair_T *kvs2 = ht_tokvarray(&ccg->options);
		    qsort(kvs2, ccg->options.count, sizeof *kvs2, keywcscoll);
		    for (size_t j = 0; j < ccg->options.count; j++) {
			print_candgen(kvs[i].key, kvs2[j].key,
				kvs2[j].value, false);
			if (yash_error_message_count != 0)
			    break;
		    }
		    free(kvs2);
		    if (yash_error_message_count != 0)
			break;
		}
	    }
	}
	free(kvs);
	return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;
    }
}

/* Prints the contents of the specified candidate generation specification
 * in a form interpretable as a command. */
int print_candgen(
	const wchar_t *targetcommand, const wchar_t *targetoption,
	const le_candgen_T *candgen, bool intermix)
{
    if (fputs("complete -C", stdout) == EOF || !quote_print(targetcommand))
	goto ioerror;

    if (targetoption != NULL) {
	if (fputs(" -O", stdout) == EOF || !quote_print(targetoption))
	    goto ioerror;
    } else if (intermix) {
	if (fputs(" -X", stdout) == EOF)
	    goto ioerror;
    }

    if (candgen == NULL)
	goto finish;

    if (candgen->type & CGT_FILE)
	if (fputs(" --file", stdout) == EOF)
	    goto ioerror;
    if (candgen->type & CGT_DIRECTORY)
	if (fputs(" --directory", stdout) == EOF)
	    goto ioerror;
    if (candgen->type & CGT_EXECUTABLE)
	if (fputs(" --executable-file", stdout) == EOF)
	    goto ioerror;
    if ((candgen->type & CGT_COMMAND) == CGT_COMMAND) {
	if (fputs(" --command", stdout) == EOF)
	    goto ioerror;
    } else {
	if ((candgen->type & CGT_BUILTIN) == CGT_BUILTIN) {
	    if (fputs(" --builtin", stdout) == EOF)
		goto ioerror;
	} else {
	    if (candgen->type & CGT_SBUILTIN)
		if (fputs(" --special-builtin", stdout) == EOF)
		    goto ioerror;
	    if (candgen->type & CGT_SSBUILTIN)
		if (fputs(" --semi-special-builtin", stdout) == EOF)
		    goto ioerror;
	    if (candgen->type & CGT_RBUILTIN)
		if (fputs(" --regular-builtin", stdout) == EOF)
		    goto ioerror;
	}
	if (candgen->type & CGT_EXTCOMMAND)
	    if (fputs(" --external-command", stdout) == EOF)
		goto ioerror;
	if (candgen->type & CGT_FUNCTION)
	    if (fputs(" --function", stdout) == EOF)
		goto ioerror;
    }
    if (candgen->type & CGT_KEYWORD)
	if (fputs(" --keyword", stdout) == EOF)
	    goto ioerror;
    if (candgen->type & CGT_OPTION)
	if (false)
	    goto ioerror;
    if ((candgen->type & CGT_ALIAS) == CGT_ALIAS) {
	if (fputs(" --alias", stdout) == EOF)
	    goto ioerror;
    } else {
	if (candgen->type & CGT_NALIAS)
	    if (fputs(" --normal-alias", stdout) == EOF)
		goto ioerror;
	if (candgen->type & CGT_GALIAS)
	    if (fputs(" --global-alias", stdout) == EOF)
		goto ioerror;
    }
    if ((candgen->type & CGT_VARIABLE) == CGT_VARIABLE) {
	if (fputs(" --variable", stdout) == EOF)
	    goto ioerror;
    } else {
	if (candgen->type & CGT_SCALAR)
	    if (fputs(" --scalar-variable", stdout) == EOF)
		goto ioerror;
	if (candgen->type & CGT_ARRAY)
	    if (fputs(" --array-variable", stdout) == EOF)
		goto ioerror;
    }
    if ((candgen->type & CGT_JOB) == CGT_JOB) {
	if (fputs(" --job", stdout) == EOF)
	    goto ioerror;
    } else {
	if (candgen->type & CGT_RUNNING)
	    if (fputs(" --running-job", stdout) == EOF)
		goto ioerror;
	if (candgen->type & CGT_STOPPED)
	    if (fputs(" --stopped-job", stdout) == EOF)
		goto ioerror;
	if (candgen->type & CGT_DONE)
	    if (fputs(" --finished-job", stdout) == EOF)
		goto ioerror;
    }
    if (candgen->type & CGT_SHOPT)
	if (fputs(" --shell-option", stdout) == EOF)
	    goto ioerror;
    if (candgen->type & CGT_SIGNAL)
	if (fputs(" --signal", stdout) == EOF)
	    goto ioerror;
    if (candgen->type & CGT_LOGNAME)
	if (fputs(" --username", stdout) == EOF)
	    goto ioerror;
    if (candgen->type & CGT_GROUP)
	if (fputs(" --group", stdout) == EOF)
	    goto ioerror;
    if (candgen->type & CGT_HOSTNAME)
	if (fputs(" --hostname", stdout) == EOF)
	    goto ioerror;
    if (candgen->type & CGT_BINDKEY)
	if (fputs(" --bindkey", stdout) == EOF)
	    goto ioerror;

    if (candgen->function != NULL)
	if (fputs(" -F", stdout) == EOF || !quote_print(candgen->function))
	    goto ioerror;

    if (candgen->words != NULL) {
	if (fputs(" --", stdout) == EOF)
	    goto ioerror;
	for (void *const *restrict w = candgen->words; *w != NULL; w++)
	    if (!quote_print(*w))
		goto ioerror;
    }

finish:
    if (putchar('\n') < 0)
	goto ioerror;
    return Exit_SUCCESS;

ioerror:
    xerror(errno, Ngt("cannot print to standard output"));
    return Exit_FAILURE;
}

/* Prints a space followed by the specified word to the standard output.
 * Returns true iff successful. */
bool quote_print(const wchar_t *word)
{
    if (!need_quote(word))
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

/* Removes the candidate generation policy for the specified command (and
 * option). */
void complete_builtin_remove(
	const wchar_t *targetcommand, const wchar_t *targetoption)
{
    if (candgens.capacity == 0)
	return;

    if (targetcommand == NULL) {
	/* remove everything */
	size_t i = 0;
	kvpair_T kv;
	while ((kv = ht_next(&candgens, &i)).key != NULL) {
	    free(kv.key);
	    free_cmdcandgen(kv.value);
	}
	ht_destroy(&candgens);
	candgens.capacity = 0;
    } else if (targetoption == NULL) {
	/* remove specified command */
	kvpair_T kv = ht_remove(&candgens, targetcommand);
	free(kv.key);
	free_cmdcandgen(kv.value);
    } else {
	/* remove specified option */
	struct cmdcandgen_T *ccg = ht_get(&candgens, targetcommand).value;
	if (ccg != NULL && ccg->options.capacity != 0) {
	    kvpair_T kv = ht_remove(&ccg->options, targetoption);
	    free(kv.key);
	    free_candgen(kv.value);
	}
    }
}

/* Frees the specified `cmdcandgen_T' object. */
void free_cmdcandgen(struct cmdcandgen_T *ccg)
{
    if (ccg != NULL) {
	if (ccg->options.capacity != 0) {
	    size_t i = 0;
	    kvpair_T kv;
	    while ((kv = ht_next(&ccg->options, &i)).key != NULL) {
		free(kv.key);
		free_candgen(kv.value);
	    }
	    ht_destroy(&ccg->options);
	}
	free_candgen_contents(&ccg->operands);
	free(ccg);
    }
}

/* Frees the specified `le_candgen_T' object. */
void free_candgen(le_candgen_T *candgen)
{
    if (candgen != NULL) {
	free_candgen_contents(candgen);
	free(candgen);
    }
}

/* Frees the contents of the specified `le_candgen_T' object. */
void free_candgen_contents(const le_candgen_T *candgen)
{
    recfree(candgen->words, free);
    free(candgen->function);
}

/* Sets the candidate generation policy for the specified command/option to the
 * specified `le_candgen_T' object. */
int complete_builtin_set(
	const wchar_t *targetcommand, const wchar_t *targetoption,
	const le_candgen_T *candgen, bool intermix)
{
    if (candgens.capacity == 0)
	ht_init(&candgens, hashwcs, htwcscmp);

    struct cmdcandgen_T *ccg = ht_get(&candgens, targetcommand).value;
    if (ccg == NULL) {
	ccg = xmalloc(sizeof *ccg);
	ccg->operands = (le_candgen_T) {
	    .type = CGT_FILE | CGT_GALIAS, .words = NULL, .function = NULL, };
	ccg->options = (hashtable_T) { .capacity = 0 };
	ccg->option_after_operand = false;
	ht_set(&candgens, xwcsdup(targetcommand), ccg);
    }
    if (targetoption == NULL) {
	ccg->option_after_operand = intermix;
	free_candgen_contents(&ccg->operands);
	clone_candgen(&ccg->operands, candgen);
    } else {
	if (ccg->options.capacity == 0)
	    ht_init(&ccg->options, hashwcs, htwcscmp);

	le_candgen_T *copy;
	if (candgen->type || candgen->words || candgen->function) {
	    copy = xmalloc(sizeof *copy);
	    clone_candgen(copy, candgen);
	} else {
	    copy = NULL;
	}

	kvpair_T kv = ht_set(&ccg->options, xwcsdup(targetoption), copy);
	free(kv.key);
	free_candgen(kv.value);
    }
    return Exit_SUCCESS;
}

/* Copies the contents of `orig' to `copy'. */
void clone_candgen(
	le_candgen_T *restrict copy, const le_candgen_T *restrict orig)
{
    copy->type = orig->type;
    if (orig->words != NULL)
	copy->words = duparray(orig->words, copyaswcs);
    else
	copy->words = NULL;
    if (orig->function != NULL)
	copy->function = xwcsdup(orig->function);
    else
	copy->function = NULL;
}

#if YASH_ENABLE_HELP
const char complete_help[] = Ngt(
"complete - set completion policy\n"
"\tcomplete [-C command] [-O option] [-PX] [options...] [words...]\n"
"The \"complete\" builtin specifies how to complete a command argument.\n"
"The command name for which the settings are made is specified by the\n"
"<command> argument of the -C (--target-command) option. The following\n"
"options can be used to specify what kind of candidates should be generated\n"
"to complete <command>'s argument.\n"
" -a --alias\n"
"\taliases (global and non-global)\n"
" --array-variable\n"
"\tarray variables\n"
" --bindkey\n"
"\tcommand names for line-editing key bindings\n"
" -b --builtin\n"
"\tbuiltin commands\n"
" -c --command\n"
"\tcommands and shell keywords\n"
" -d --directory\n"
"\tdirectories\n"
" --executable-file\n"
"\texecutable regular files\n"
" --external-command\n"
"\texternal commands\n"
" -f --file\n"
"\tfiles (including directories)\n"
" --finished-job\n"
"\tfinished job names\n"
" --function\n"
"\tfunctions\n"
" --global-alias\n"
"\tglobal aliases\n"
" -g --group\n"
"\tgroup names\n"
" -h --hostname\n"
"\thost names\n"
" -j --job\n"
"\tjob names\n"
" -k --keyword\n"
"\tshell keywords, such as \"if\", \"for\", and \"while\"\n"
" --normal-alias\n"
"\tnon-global aliases\n"
" --regular-builtin\n"
"\tregular builtins\n"
" --running-job\n"
"\trunning job names\n"
" --scalar-variable\n"
"\tnormal (non-array) variables\n"
" --semi-special-builtin\n"
"\tsemi-special builtins\n"
" -o --shell-option\n"
"\tshell options, such as \"errexit\", \"xtrace\", and \"noclobber\"\n"
" --signal\n"
"\tsignals, such as \"SIGINT\" and \"SIGKILL\"\n"
" --special-builtin\n"
"\tspecial builtins\n"
" --stopped-job\n"
"\tstopped job names\n"
" -u --username\n"
"\tuser names\n"
" -v --variable\n"
"\tvariables\n"
"In addition to the options above, you can specify the -F\n"
"(--generator-function) option and operands to the \"complete\" builtin to\n"
"generate arbitrary candidates.\n"
"The -F (--generator-function) option requires an argument that specifies the\n"
"name of a function that is executed to generate candidates.\n"
"Operands to the \"complete\" builtin are added to candidates every time\n"
"completion for <command> is performed.\n"
"\n"
"To complete options, the -O (--target-option) option can be used. The\n"
"argument to the -O option specifies the name of the option to complete.\n"
"The option name may be either a single character or a string that starts\n"
"with a hyphen. Multiple single-character options may be combined in to one.\n"
"If the option to complete requires an argument, you can specify how to\n"
"complete the argument by the options and operands described above.\n"
"\n"
"If <command>'s options and operands can be intermixed, specify the -X\n"
"(--intermix) option. If this option is not specified, the completer\n"
"considers all arguments after the first operand as operands.\n"
"\n"
"If the -P (--print) option is specified, the completion settings for the\n"
"specified <command> (and <option>) are printed.\n"
"If the -R (--remove) option is specified, the settings for the specified\n"
"<command> (and <option>) are removed.\n"
"\n"
"The \"complete\" builtin can be invoked from candidate generator functions,\n"
"which have been specified by the -F option in earlier calls, to add\n"
"candidates for the ongoing completion. In this case, the \"complete\"\n"
"builtin must be invoked without the -C or -O option.\n"
);
#endif /* YASH_ENABLE_HELP */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
