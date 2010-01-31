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
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sys/stat.h>
#include "../builtin.h"
#include "../hashtable.h"
#include "../option.h"
#include "../path.h"
#include "../plist.h"
#include "../util.h"
#include "../variable.h"
#include "../xfnmatch.h"
#include "complete.h"
#include "display.h"
#include "editing.h"
#include "lineedit.h"
#include "terminfo.h"


static void free_candidate(void *c)
    __attribute__((nonnull));
static void sort_candidates(void);
static int sort_candidates_cmp(const void *cp1, const void *cp2)
    __attribute__((nonnull));

static void generate_candidates(const le_context_T *context)
    __attribute__((nonnull));
static const le_candgen_T *get_candgen(const le_context_T *context)
    __attribute__((nonnull));
static const le_candgen_T *find_optarg_candgen(
	const hashtable_T *options, const wchar_t *s)
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
/* The type of quotation at the current position. */
static le_quote_T quotetype;
/* The length of the expanded source word, not including backslash escapes. */
static size_t expanded_source_word_length;
/* The length of the longest common prefix of the current candidates. */
static size_t common_prefix_length;


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

    le_context_T context = {
	.quote = QUOTE_NORMAL,
	.type = CTXT_NORMAL,
	.srcindex = 0,
    };  // TODO
    context.pwordc = 0;  // TODO
    context.pwords = xmalloc(1 * sizeof *context.pwords);  // TODO
    context.pwords[0] = NULL;  // TODO
    context.pattern = xwcsdup(le_main_buffer.contents);  // TODO
    extern wchar_t *unescape(const wchar_t *);  // TODO
    context.src = unescape(context.pattern);
    if (is_pathname_matching_pattern(context.pattern)) {
	context.substsrc = true;
    } else {
	xwcsbuf_T buf;
	context.substsrc = false;
	context.pattern =
	    wb_towcs(wb_wccat(wb_initwith(&buf, context.pattern), L'*'));
    }  // TODO
    context.cpattern = xfnm_compile(
	    context.pattern, XFNM_HEADONLY | XFNM_TAILONLY);

    insertion_index = le_main_index;
    quotetype = context.quote;
    expanded_source_word_length = wcslen(context.src);

    if (le_state == LE_STATE_SUSPENDED_COMPDEBUG) {
	for (int i = 0; i < context.pwordc; i++)
	    le_compdebug("preceding word %d: \"%ls\"",
		    i + 1, (const wchar_t *) context.pwords[i]);
	le_compdebug("source word: \"%ls\"", context.src);
	le_compdebug(" as pattern: \"%ls\"", context.pattern);
    }

    generate_candidates(&context);
    sort_candidates();
    le_compdebug("total of %zu candidate(s)", le_candidates.length);

    /* display the results */
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
    recfree(context.pwords, free);
    free(context.src);
    free(context.pattern);
    xfnm_free(context.cpattern);

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

/* Generates completion candidates under the specified context.
 * The candidate list must have been initialized when this function is called.*/
void generate_candidates(const le_context_T *context)
{
    const le_candgen_T *candgen = get_candgen(context);

    generate_file_candidates(candgen->type, context->pattern);
    generate_builtin_candidates(candgen->type, context);
    generate_external_command_candidates(candgen->type, context);
    generate_function_candidates(candgen->type, context);
    generate_keyword_candidates(candgen->type, context);
    // TODO: other types
}

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
    tempresult = (le_candgen_T) { .type = 0, .words = NULL, .function = NULL };
    switch (context->type) {
	case CTXT_NORMAL:
	case CTXT_VAR_BRCK_WORD:
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
	case CTXT_VAR_BRCK:
	    tempresult.type |= CGT_VARIABLE;
	    return &tempresult;
	case CTXT_ARITH:
	    tempresult.type |= CGT_SCALAR;
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

/* Adds the specified value as a completion candidate to the candidate list.
 * The value is freed in this function.
 * If the specified value does not match `cgt', it may not be added. */
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
	    case CT_WORD:      typestr = "word";              break;
	    case CT_FILE:      typestr = "file";              break;
	    case CT_COMMAND:   typestr = "command";           break;
	    case CT_ALIAS:     typestr = "alias";             break;
	    case CT_OPTION:    typestr = "option";            break;
	    case CT_VAR:       typestr = "variable";          break;
	    case CT_FUNC:      typestr = "function";          break;
	    case CT_JOB:       typestr = "job";               break;
	    case CT_SHOPT:     typestr = "shell option";      break;
	    case CT_FD:        typestr = "file descriptor";   break;
	    case CT_SIG:       typestr = "signal";            break;
	    case CT_LOGNAME:   typestr = "user name";         break;
	    case CT_HOSTNAME:  typestr = "host name";         break;
	    case CT_BINDKEY:   typestr = "lineedit command";  break;
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
	if (xfnm_wmatch(context->cpattern, *k).start != (size_t) -1)
	    le_add_candidate(type, CT_COMMAND, xwcsdup(*k));
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

    // TODO
    if (cand->type == CT_FILE && S_ISDIR(cand->filestat.mode)) {
	size_t len = wcslen(cand->value);
	if (len > 0 && cand->value[len - 1] != L'/') {
	    wb_ninsert_force(&le_main_buffer, le_main_index, L"/", 1);
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
	if (le_candidates.length == 1) {
	    wb_ninsert_force(&le_main_buffer, le_main_index, L" ", 1);
	    le_main_index += 1;
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
		    if (wcschr(L"|&;<>()$`\\\"'*?[]#~=", *s) || iswspace(*s))
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
		if (wcschr(L"$`\"\\", *s))
		    wb_wccat(buf, L'\\');
		wb_wccat(buf, *s);
		s++;
	    }
	    return;
    }
    assert(false);
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
