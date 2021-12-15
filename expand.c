/* Yash: yet another shell */
/* expand.c: word expansion */
/* (C) 2007-2021 magicant */

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


#include "common.h"
#include "expand.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include "arith.h"
#include "exec.h"
#include "input.h"
#include "option.h"
#include "parser.h"
#include "path.h"
#include "plist.h"
#include "sig.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"
#include "xfnmatch.h"
#include "yash.h"


/* result of word expansion */
struct expand_four_T {
    plist_T valuelist, cclist;
};
/* `valuelist' is a list of pointers to newly malloced wide strings that contain
 * the expanded word value. `cclist' is a list of pointers to newly malloced
 * single-byte strings whose values are charcategory_T cast to char. `cclist'
 * must have as many strings as `valuelist' and each string in `cclist' must
 * have the same length as the corresponding wide string in `valuelist'. */

static plist_T expand_word(const wordunit_T *w)
    __attribute__((warn_unused_result));
static struct expand_four_T expand_four(const wordunit_T *restrict w,
	tildetype_T tilde, quoting_T quoting, charcategory_T defaultcc)
    __attribute__((warn_unused_result));
static inline void fill_ccbuf(
	const xwcsbuf_T *restrict valuebuf, xstrbuf_T *restrict ccbuf,
	charcategory_T c)
    __attribute__((nonnull));

static wchar_t *expand_tilde(const wchar_t **ss,
	bool hasnextwordunit, tildetype_T tt)
    __attribute__((nonnull,malloc,warn_unused_result));

enum indextype_T { IDX_NONE, IDX_ALL, IDX_CONCAT, IDX_NUMBER, };

static struct expand_four_T expand_param(const paramexp_T *p, bool indq)
    __attribute__((nonnull));
static enum indextype_T parse_indextype(const wchar_t *indexstr)
    __attribute__((nonnull,pure));
static wchar_t *trim_wstring(wchar_t *s, ssize_t startindex, ssize_t endindex)
    __attribute__((nonnull));
static void **trim_array(void **a, ssize_t startindex, ssize_t endindex)
    __attribute__((nonnull));
static void print_subst_as_error(const paramexp_T *p, quoting_T quoting)
    __attribute__((nonnull));
static void match_each(void **restrict slist, const wchar_t *restrict pattern,
	paramexptype_T type)
    __attribute__((nonnull));
static void subst_each(void **restrict slist, const wchar_t *pattern,
	const wchar_t *subst, paramexptype_T type)
    __attribute__((nonnull));
static wchar_t *concatenate_values(void **values, bool escape)
    __attribute__((nonnull,malloc,warn_unused_result));
static void **concatenate_values_into_array(void **values, bool escape)
    __attribute__((nonnull,malloc,warn_unused_result));
static void subst_length_each(void **slist)
    __attribute__((nonnull));
static void merge_expand_four(
	struct expand_four_T *restrict from,
	struct expand_four_T *restrict to,
	xwcsbuf_T *restrict valuebuf, xstrbuf_T *restrict ccbuf)
    __attribute__((nonnull));

/* data used in brace expansion */
struct brace_expand_T {
    const wchar_t *word;  /* the word to expand */
    const char *cc;       /* the corresponding charcategory_T string */
    void *const *graph;   /* see the comment in the `expand_brace` function */
    plist_T *valuelist;   /* the list to add the results (words) */
    plist_T *cclist;      /* the list to add the results (charcategory_T) */
};

static void expand_brace_each(
	void *const *restrict values, void *const *restrict ccs,
	plist_T *restrict valuelist, plist_T *restrict cclist)
    __attribute__((nonnull));
static void expand_brace(
	wchar_t *restrict word, char *restrict cc,
	plist_T *restrict valuelist, plist_T *restrict cclist)
    __attribute__((nonnull));
static void generate_brace_expand_results(
	const struct brace_expand_T *restrict e, size_t ci,
	xwcsbuf_T *restrict valuebuf, xstrbuf_T *restrict ccbuf)
    __attribute__((nonnull));
static bool try_expand_brace_sequence(
	const struct brace_expand_T *restrict e, size_t ci,
	xwcsbuf_T *restrict valuebuf, xstrbuf_T *restrict ccbuf)
    __attribute__((nonnull));
static bool has_leading_zero(const wchar_t *restrict s, bool *restrict sign)
    __attribute__((nonnull));

static void fieldsplit(void **restrict valuelist, void **restrict cclist,
	plist_T *restrict outvaluelist, plist_T *restrict outcclist)
    __attribute__((nonnull));
static bool is_ifs_char(wchar_t c, charcategory_T cc, const wchar_t *ifs)
    __attribute__((nonnull,pure));
static bool is_ifs_whitespace(wchar_t c, charcategory_T cc, const wchar_t *ifs)
    __attribute__((nonnull,pure));
static bool is_non_ifs_char(wchar_t c, charcategory_T cc, const wchar_t *ifs)
    __attribute__((nonnull,pure));
static void add_empty_field(plist_T *dest, const wchar_t *p)
    __attribute__((nonnull));

static inline void add_sq(
	const wchar_t *restrict *ss, xwcsbuf_T *restrict buf, bool escape)
    __attribute__((nonnull));
static inline bool should_escape(charcategory_T cc, escaping_T escaping)
    __attribute__((const));
static wchar_t *quote_removal_free(
	wchar_t *restrict s, char *restrict cc, escaping_T escaping)
    __attribute__((nonnull,malloc,warn_unused_result));

static enum wglobflags_T get_wglobflags(void)
    __attribute__((pure));
static void glob_all(
	struct expand_four_T *restrict e, plist_T *restrict results)
    __attribute__((nonnull));

static void maybe_exit_on_error(void);


/********** Entry Points **********/

/* Expands a command line.
 * `args' is a NULL-terminated array of pointers to `const wordunit_T'
 * to expand.
 * If successful, the number of resulting words is assigned to `*argcp', a
 * pointer to a newly malloced array of the expanded words is assigned to
 * `*argvp', and true is returned. The array is NULL-terminated and its elements
 * are newly malloced wide strings.
 * If unsuccessful, false is returned and the values of `*argcp' and `*argvp'
 * are indeterminate.
 * On error in a non-interactive shell, the shell exits. */
bool expand_line(void *const *restrict args,
    int *restrict argcp, void ***restrict argvp)
{
    plist_T list;
    pl_init(&list);

    for (; *args != NULL; args++) {
	if (!expand_multiple(*args, &list)) {
	    plfree(pl_toary(&list), free);
	    return false;
	}
    }

    *argcp = list.length;
    *argvp = pl_toary(&list);
    return true;
}

/* Expands a word.
 * The results, which are added to `list' as newly-malloced wide strings, may
 * be multiple words.
 * The return value is true iff successful.
 * On error in a non-interactive shell, the shell exits. */
bool expand_multiple(const wordunit_T *w, plist_T *list)
{
    /* four expansions (w -> valuelist) */
    struct expand_four_T expand = expand_four(w, TT_SINGLE, Q_WORD, CC_LITERAL);
    if (expand.valuelist.contents == NULL) {
	maybe_exit_on_error();
	return false;
    }

    /* brace expansion (valuelist -> valuelist2) */
    plist_T valuelist2, cclist2;
    if (shopt_braceexpand) {
	pl_init(&valuelist2);
	pl_init(&cclist2);
	expand_brace_each(expand.valuelist.contents, expand.cclist.contents,
		&valuelist2, &cclist2);
	pl_truncate(&expand.valuelist, 0);
	pl_truncate(&expand.cclist, 0);
    } else {
	valuelist2 = expand.valuelist;
	cclist2 = expand.cclist;
	pl_init(&expand.valuelist);
	pl_init(&expand.cclist);
    }

    /* field splitting (valuelist2 -> valuelist) */
    fieldsplit(pl_toary(&valuelist2), pl_toary(&cclist2),
	    &expand.valuelist, &expand.cclist);
    assert(expand.valuelist.length == expand.cclist.length);

    /* pathname expansion (and quote removal) */
    glob_all(&expand, list);

    return true;
}

/* Expands a word to a single field.
 * If successful, the result is a pair of newly malloced strings.
 * On error, an error message is printed and a NULL pair is returned.
 * On error in a non-interactive shell, the shell exits. */
/* This function first expands the word into (possibly many) fields and then
 * concatenates into one field. */
struct cc_word_T expand_single_cc(
	const wordunit_T *w, tildetype_T tilde, quoting_T quoting)
{
    struct expand_four_T e = expand_four(w, tilde, quoting, CC_LITERAL);

    if (e.valuelist.contents == NULL) {
	maybe_exit_on_error();
	return (struct cc_word_T) { NULL, NULL };
    }

    const wchar_t *ifs = getvar(L VAR_IFS);
    wchar_t separator = ifs != NULL ? ifs[0] : L' ';

    size_t totallength;
    if (e.valuelist.length == 0)
	totallength = 0;
    else {
	totallength = e.valuelist.length - 1;
	for (size_t i = 0; i < e.valuelist.length; i++)
	    totallength = add(totallength, wcslen(e.valuelist.contents[i]));
    }

    xwcsbuf_T valuebuf;
    xstrbuf_T ccbuf;
    wb_initwithmax(&valuebuf, totallength);
    sb_initwithmax(&ccbuf, totallength);
    for (size_t i = 0; i < e.valuelist.length; i++) {
	if (i > 0 && separator != L'\0') {
	    wb_wccat(&valuebuf, separator);
	    sb_ccat(&ccbuf, CC_SOFT_EXPANSION);
	}
	wb_catfree(&valuebuf, e.valuelist.contents[i]);
	sb_ncat_force(
		&ccbuf, e.cclist.contents[i], valuebuf.length - ccbuf.length);
	free(e.cclist.contents[i]);
    }

    pl_destroy(&e.valuelist);
    pl_destroy(&e.cclist);
    return (struct cc_word_T) { wb_towcs(&valuebuf), sb_tostr(&ccbuf) };
}

/* Expands a word to (possibly any number of) fields.
 * If successful, the return value is a plist_T containing newly malloced wide
 * strings. In most cases, the plist_T contains one string. If the word contains
 * "$@", however, it may contain any number of strings.
 * On error, the return value is a plist_T with `contents' being NULL. */
plist_T expand_word(const wordunit_T *w)
{
    /* four expansions */
    struct expand_four_T expand = expand_four(w, TT_NONE, Q_WORD, CC_LITERAL);

    /* quote removal */
    for (size_t i = 0; i < expand.valuelist.length; i++)
	expand.valuelist.contents[i] = quote_removal_free(
	    expand.valuelist.contents[i], expand.cclist.contents[i], ES_NONE);

    pl_destroy(&expand.cclist);

    return expand.valuelist;
}

/* Expands a single word: the four expansions and quote removal.
 * This function doesn't perform brace expansion, field splitting, or globbing.
 * If successful, the resulting word is returned as a newly malloced string.
 * On error, an error message is printed and NULL is returned.
 * On error in a non-interactive shell, the shell exits. */
wchar_t *expand_single(const wordunit_T *w,
	tildetype_T tilde, quoting_T quoting, escaping_T escaping)
{
    cc_word_T e = expand_single_cc(w, tilde, quoting);
    if (e.value == NULL)
	return NULL;
    return quote_removal_free(e.value, e.cc, escaping);
}

/* Expands a single word: the four expansions, pathname expansion, and quote
 * removal.
 * This function doesn't perform brace expansion or field splitting.
 * If the result of pathname expansion is more than one word, this function
 *   - returns the original pattern string if in the POSIXly correct mode
 *   - treats it as an error otherwise.
 * If the "glob" shell option is off, pathname expansion is not performed.
 * The "nullglob" shell option is ignored.
 * If successful, the resulting word is returned as a newly malloced string.
 * On error, an error message is printed and NULL is returned.
 * On error in a non-interactive shell, the shell exits. */
char *expand_single_with_glob(const wordunit_T *arg)
{
    cc_word_T e = expand_single_cc(arg, TT_SINGLE, Q_WORD);
    if (e.value == NULL)
	goto return_null;

    if (!shopt_glob)
	goto quote_removal;

    wchar_t *pattern = quote_removal(e.value, e.cc, ES_QUOTED_HARD);
    if (!is_pathname_matching_pattern(pattern)) {
	free(pattern);
	goto quote_removal;
    }

    plist_T globresults;
    bool ok;

    /* perform pathname expansion */
    pl_init(&globresults);
    set_interruptible_by_sigint(true);
    ok = wglob(pattern, get_wglobflags(), &globresults);
    set_interruptible_by_sigint(false);
    free(pattern);
    if (!ok) {
	plfree(pl_toary(&globresults), free);
	xerror(EINTR, Ngt("redirection"));
	goto return_null;
    }

    /* examine the expansion results */
    wchar_t *wresult;
    if (globresults.length == 1) {
	wresult = globresults.contents[0];
	pl_destroy(&globresults);
    } else {
	plfree(pl_toary(&globresults), free);
	if (!posixly_correct) {
	    wchar_t *word = quote_removal(e.value, e.cc, ES_NONE);
	    xerror(0, Ngt("filename `%ls' matches more than one file"), word);
	    free(word);
	    goto return_null;
	}
quote_removal:
	wresult = quote_removal(e.value, e.cc, ES_NONE);
    }

    char *mbresult = realloc_wcstombs(wresult);
    if (mbresult == NULL)
	xerror(EILSEQ, Ngt("redirection"));
    free(e.value);
    free(e.cc);
    return mbresult;

return_null:
    free(e.value);
    free(e.cc);
    return NULL;
}


/********** Four Expansions **********/

/* Performs the four expansions, i.e., tilde expansion, parameter expansion,
 * command substitution, and arithmetic expansion.
 * If successful, `valuelist' in the return value is the list of the resultant
 * fields, which are newly malloced wide strings, and `cclist' is the list of
 * the corresponding charcategory_T strings, which are also newly malloced.
 * Usually this function produces one or more fields, but it may produce zero
 * fields if "$@" is expanded with no positional parameters.
 * If unsuccessful, `valuelist' and `cclist' are empty and have NULL `contents'.
 */
struct expand_four_T expand_four(const wordunit_T *restrict w,
	tildetype_T tilde, quoting_T quoting, charcategory_T defaultcc)
{
    /* lists to insert the final results into */
    struct expand_four_T e;
    pl_init(&e.valuelist);
    pl_init(&e.cclist);

    /* intermediate value of the currently expanded word */
    xwcsbuf_T valuebuf;
    wb_init(&valuebuf);

    /* charcategory_T string corresponding to `valuebuf' */
    xstrbuf_T ccbuf;
    sb_init(&ccbuf);

    bool indq = false;  /* in a double quote? */
    bool first = true;  /* is the first word unit? */
    const wchar_t *ss;
    wchar_t *s;

    /* When there are no positional parameters, expansion of ${@} contained in
     * double-quotes is very special: The result is no fields. To handle this
     * correctly, we do the following:
     *  1. Remove the surrounding double-quotes if a parameter expansion inside
     *     them happens to expand to no fields.
     *  2. Remove the empty field so that the final result is no fields rather
     *     than one empty field.
     * `removedq' is for step 1 and `removeempty' for step 2. */
    bool removedq = false, removeempty = false;

    for (; w != NULL; w = w->next, first = false) {
	switch (w->wu_type) {
	case WT_STRING:
	    ss = w->wu_string;
	    if (first && tilde != TT_NONE) {
		s = expand_tilde(&ss, w->next, tilde);
		if (s != NULL) {
		    wb_catfree(&valuebuf, s);
		    fill_ccbuf(&valuebuf, &ccbuf,
			    CC_HARD_EXPANSION | (defaultcc & CC_QUOTED));
		}
	    }
	    while (*ss != L'\0') {
		switch (*ss) {
		case L'"':
		    switch (quoting) {
			case Q_WORD:  case Q_DQPARAM:
			    break;
			case Q_INDQ:  case Q_LITERAL:
			    goto default_;
		    }
		    if (!indq) {
			indq = true; /* entering a quotation */
			removedq = false;
		    } else {
			indq = false; /* leaving a quotation */
			if (removedq && e.valuelist.length == 0 &&
				wcscmp(valuebuf.contents, L"\"") == 0 &&
				(ccbuf.contents[0] & CC_QUOTATION)) {
			    /* remove the corresponding opening double-quote */
			    wb_clear(&valuebuf);
			    sb_clear(&ccbuf);
			    removeempty = true;
			    break; /* and ignore the closing double-quote */
			}
		    }
		    wb_wccat(&valuebuf, L'"');
		    sb_ccat(&ccbuf, defaultcc | CC_QUOTATION);
		    break;
		case L'\'':
		    if (quoting != Q_WORD || indq)
			goto default_;

		    wb_wccat(&valuebuf, L'\'');
		    sb_ccat(&ccbuf, defaultcc | CC_QUOTATION);

		    add_sq(&ss, &valuebuf, false);
		    assert(*ss == L'\'');
		    fill_ccbuf(&valuebuf, &ccbuf, defaultcc | CC_QUOTED);

		    wb_wccat(&valuebuf, L'\'');
		    sb_ccat(&ccbuf, defaultcc | CC_QUOTATION);
		    break;
		case L'\\':
		    switch (quoting) {
			case Q_WORD:
			    if (indq && wcschr(CHARS_ESCAPABLE, ss[1]) == NULL)
				goto default_;
			    break;
			case Q_DQPARAM:
			    if (wcschr(CHARS_ESCAPABLE "}", ss[1]) == NULL)
				goto default_;
			    break;
			case Q_INDQ:
			    if (wcschr(L"$`\\", ss[1]) == NULL)
				goto default_;
			    break;
			case Q_LITERAL:
			    goto default_;
		    }

		    wb_wccat(&valuebuf, L'\\');
		    sb_ccat(&ccbuf, defaultcc | CC_QUOTATION);
		    ss++;
		    if (*ss != L'\0') {
			wb_wccat(&valuebuf, *ss++);
			sb_ccat(&ccbuf, defaultcc | CC_QUOTED);
		    }
		    continue;
		case L':':
		    if (indq || tilde != TT_MULTI)
			goto default_;

		    /* perform tilde expansion after a colon */
		    wb_wccat(&valuebuf, L':');
		    sb_ccat(&ccbuf, defaultcc);
		    ss++;
		    s = expand_tilde(&ss, w->next, tilde);
		    if (s != NULL) {
			wb_catfree(&valuebuf, s);
			fill_ccbuf(&valuebuf, &ccbuf, CC_HARD_EXPANSION);
		    }
		    continue;
default_:
		default:
		    wb_wccat(&valuebuf, *ss);
		    sb_ccat(&ccbuf, defaultcc | (indq * CC_QUOTED));
		    break;
		}
		ss++;
	    }
	    break;
	case WT_PARAM:;
	    struct expand_four_T e2 = expand_param(w->wu_param,
		    indq || quoting == Q_LITERAL || (defaultcc & CC_QUOTED));
	    if (e2.valuelist.contents == NULL)
		goto failure;
	    if (e2.valuelist.length == 0) {
		if (indq)
		    removedq = true;
		else
		    removeempty = true;
	    }
	    merge_expand_four(&e2, &e, &valuebuf, &ccbuf);
	    break;
	case WT_CMDSUB:
	    s = exec_command_substitution(&w->wu_cmdsub);
	    goto cat_s;
	case WT_ARITH:
	    s = expand_single(w->wu_arith, TT_NONE, Q_INDQ, ES_NONE);
	    if (s != NULL)
		s = evaluate_arithmetic(s);
cat_s:
	    if (s == NULL)
		goto failure;
	    wb_catfree(&valuebuf, s);
	    fill_ccbuf(&valuebuf, &ccbuf, CC_SOFT_EXPANSION |
		    (indq * CC_QUOTED) | (defaultcc & CC_QUOTED));
	    break;
	}
    }

    /* empty field removal */
    if (removeempty && e.valuelist.length == 0 && valuebuf.length == 0) {
	wb_destroy(&valuebuf);
	sb_destroy(&ccbuf);
    } else {
	pl_add(&e.valuelist, wb_towcs(&valuebuf));
	pl_add(&e.cclist, sb_tostr(&ccbuf));
    }

    return e;

failure:
    sb_destroy(&ccbuf);
    wb_destroy(&valuebuf);
    plfree(pl_toary(&e.cclist), free);
    plfree(pl_toary(&e.valuelist), free);
    e.valuelist.contents = e.cclist.contents = NULL;
    return e;
}

/* Appends to `e->ccbuf' as many `c's as needed to match the length with
 * `e->valuebuf'. */
void fill_ccbuf(const xwcsbuf_T *restrict valuebuf, xstrbuf_T *restrict ccbuf,
	charcategory_T c)
{
    sb_ccat_repeat(ccbuf, c, valuebuf->length - ccbuf->length);
}

/* Performs tilde expansion.
 * `ss' is a pointer to a pointer to the tilde character. The pointer is
 * increased so that it points to the character right after the expanded string.
 * The string pointed by the pointer pointed by `ss' should be contents of a
 * word unit of type WT_STRING. Iff there is a next word unit, `hasnextwordunit'
 * must be true.
 * If `**ss' is not L'~' or expansion fails, this function has no side effects
 * and returns NULL. If successful, `*ss' is incremented and the result is
 * returned as a newly malloced string. */
wchar_t *expand_tilde(const wchar_t **ss, bool hasnextwordunit, tildetype_T tt)
{
    const wchar_t *s = *ss;
    if (*s != L'~')
	return NULL;
    s++;

    const wchar_t *end = wcspbrk(s, tt == TT_SINGLE ? L"/" : L"/:");
    wchar_t *username;
    const wchar_t *home;
    size_t usernamelen;

    if (end != NULL) {
	usernamelen = end - s;
    } else {
	if (hasnextwordunit)
	    return NULL;
	usernamelen = wcslen(s);
    }
    username = xwcsndup(s, usernamelen);
    if (username[0] == L'\0') {
	/* empty user name: use $HOME */
	home = getvar(L VAR_HOME);
	goto finish;
    } else if (wcspbrk(username, L"\"'\\") != 0) {
	/* don't expand if the user name is quoted */
	free(username);
	return NULL;
    }
    if (!posixly_correct) {
	if (username[0] == L'+' && username[1] == L'\0') {
	    home = getvar(L VAR_PWD);
	    goto finish;
	}
	if (username[0] == L'-' && username[1] == L'\0') {
	    home = getvar(L VAR_OLDPWD);
	    goto finish;
	}
#if YASH_ENABLE_DIRSTACK
	if (username[0] == L'+' || username[0] == L'-') {
	    size_t index;
	    if (parse_dirstack_index(username, &index, &home, false)
		    && index != SIZE_MAX) {
		goto finish;
	    }
	}
#endif
    }
    home = get_home_directory(username, false);
finish:
    free(username);
    if (home == NULL)
	return NULL;
    *ss = s + usernamelen;
    return xwcsdup(home);
}

/* Performs parameter expansion.
 * If successful, the return value contains valid lists of pointers to newly
 * malloced strings. Note that the lists may contain no strings.
 * If unsuccessful, the lists have NULL `contents'. */
struct expand_four_T expand_param(const paramexp_T *p, bool indq)
{
    /* parse indices first */
    ssize_t startindex, endindex;
    enum indextype_T indextype;
    if (p->pe_start == NULL) {
	startindex = 0, endindex = SSIZE_MAX, indextype = IDX_NONE;
    } else {
	wchar_t *start = expand_single(p->pe_start, TT_NONE, Q_WORD, ES_NONE);
	if (start == NULL)
	    goto failure1;
	indextype = parse_indextype(start);
	if (indextype != IDX_NONE) {
	    startindex = 0, endindex = SSIZE_MAX;
	    free(start);
	    if (p->pe_end != NULL) {
		xerror(0, Ngt("the parameter index is invalid"));
		goto failure1;
	    }
	} else if (!evaluate_index(start, &startindex)) {
	    goto failure1;
	} else {
	    if (p->pe_end == NULL) {
		endindex = (startindex == -1) ? SSIZE_MAX : startindex;
	    } else {
		wchar_t *end = expand_single(
			p->pe_end, TT_NONE, Q_WORD, ES_NONE);
		if (end == NULL || !evaluate_index(end, &endindex))
		    goto failure1;
	    }
	    if (startindex == 0)
		startindex = SSIZE_MAX;
	    else if (startindex >= 0)
		startindex--;
	}
    }
    /* Here, `startindex' and `endindex' are zero-based. `startindex' is
     * included in the range but `endindex' is not. A negative index will be
     * wrapped around the length. */

    /* get the value of parameter or nested expansion */
    struct get_variable_T v;
    bool unset;   /* parameter is not set? */
    if (p->pe_type & PT_NEST) {
	plist_T plist = expand_word(p->pe_nest);
	if (plist.contents == NULL)
	    goto failure1;
	v.type = (plist.length == 1) ? GV_SCALAR : GV_ARRAY;
	v.count = plist.length;
	v.values = pl_toary(&plist);
	v.freevalues = true;
	unset = false;
    } else {
	v = get_variable(p->pe_name);
	if (v.type == GV_NOTFOUND) {
	    /* if the variable is not set, return empty string */
	    v.type = GV_SCALAR;
	    v.count = 1;
	    v.values = xmallocn(2, sizeof *v.values);
	    v.values[0] = xwcsdup(L"");
	    v.values[1] = NULL;
	    v.freevalues = true;
	    unset = true;
	} else {
	    unset = false;
	}
    }

    /* here, the contents of `v.values' are not escaped by backslashes. */

    /* modify the elements of `v.values' according to the indices */
    void **values;  /* the result */
    bool concat;    /* concatenate array elements? */
    switch (v.type) {
	case GV_SCALAR:
	    assert(v.values != NULL && v.count == 1);
	    save_get_variable_values(&v);
	    if (indextype != IDX_NUMBER) {
		trim_wstring(v.values[0], startindex, endindex);
	    } else {
		size_t len = wcslen(v.values[0]);
		free(v.values[0]);
		v.values[0] = malloc_wprintf(L"%zu", len);
	    }
	    values = v.values, concat = false;
	    break;
	case GV_ARRAY:
	    concat = false;
	    goto treat_array;
	case GV_ARRAY_CONCAT:
	    concat = true;
treat_array:
	    switch (indextype) {
	    case IDX_CONCAT:
		concat = true;
		/* falls thru! */
	    case IDX_NONE:
	    case IDX_ALL:
		if (startindex >= 0) {
#if SIZE_MAX >= SSIZE_MAX
		    if ((size_t) startindex > v.count)
#else
		    if (startindex > (ssize_t) v.count)
#endif
			startindex = v.count;
		} else {
		    startindex += v.count;
		    if (startindex < 0)
			startindex = 0;
		}
		if (endindex < 0)
		    endindex += v.count + 1;
		if (endindex < startindex)
		    endindex = startindex;
#if SSIZE_MAX > SIZE_MAX
		else if (endindex > (ssize_t) SIZE_MAX)
		    endindex = SIZE_MAX;
#endif
		assert(0 <= startindex && startindex <= endindex);
		values = v.freevalues
		    ? trim_array(v.values, startindex, endindex)
		    : plndup(v.values + startindex,
			    endindex - startindex, copyaswcs);
		break;
	    case IDX_NUMBER:
		if (v.freevalues)
		    plfree(v.values, free);
		values = xmallocn(2, sizeof *values);
		values[0] = malloc_wprintf(L"%zu", v.count);
		values[1] = NULL;
		concat = false;
		break;
	    default:
		assert(false);
	    }
	    break;
	default:
	    assert(false);
    }

    /* if `PT_COLON' is true, empty string is treated as unset */
    if (p->pe_type & PT_COLON)
	if (values[0] == NULL ||
		(((wchar_t *) values[0])[0] == L'\0' && values[1] == NULL))
	    unset = true;

    /* PT_PLUS, PT_MINUS, PT_ASSIGN, PT_ERROR */
    quoting_T substq = indq ? Q_DQPARAM : Q_WORD;
    wchar_t *subst;
    switch (p->pe_type & PT_MASK) {
    case PT_PLUS:
	if (!unset)
	    goto subst;
	unset = false;
	break;
    case PT_MINUS:
	if (unset) {
subst:
	    plfree(values, free);
	    return expand_four(p->pe_subst, TT_SINGLE, substq,
		    CC_SOFT_EXPANSION | (indq * CC_QUOTED));
	}
	break;
    case PT_ASSIGN:
	if (unset) {
	    plfree(values, free);
	    if (p->pe_type & PT_NEST) {
		xerror(0,
		    Ngt("a nested parameter expansion cannot be assigned"));
		goto failure1;
	    } else if (!is_name(p->pe_name)) {
		xerror(0, Ngt("cannot assign to parameter `%ls' "
			    "in parameter expansion"),
			p->pe_name);
		goto failure1;
	    } else if ((v.type == GV_ARRAY_CONCAT)
		    || (v.type == GV_ARRAY && startindex + 1 != endindex)) {
                xerror(0, Ngt("the specified index does not support assignment "
			    "in the parameter expansion of array `%ls'"),
			p->pe_name);
		goto failure1;
	    }
	    subst = expand_single(p->pe_subst, TT_SINGLE, substq, ES_NONE);
	    if (subst == NULL)
		goto failure1;
	    if (v.type != GV_ARRAY) {
		assert(v.type == GV_NOTFOUND || v.type == GV_SCALAR);
		if (!set_variable(
			    p->pe_name, xwcsdup(subst), SCOPE_GLOBAL, false)) {
		    free(subst);
		    goto failure1;
		}
	    } else {
		assert(0 <= startindex && (size_t) startindex <= v.count);
		if (!set_array_element(p->pe_name, startindex, xwcsdup(subst))){
		    free(subst);
		    goto failure1;
		}
	    }
	    values = xmallocn(2, sizeof *values);
	    values[0] = subst;
	    values[1] = NULL;
	    unset = false;
	}
	break;
    case PT_ERROR:
	if (unset) {
	    print_subst_as_error(p, substq);
	    goto failure2;
	}
	break;
    }

    if (unset && !shopt_unset) {
	xerror(0, Ngt("parameter `%ls' is not set"), p->pe_name);
	goto failure2;
    }

    /* PT_MATCH, PT_SUBST */
    wchar_t *match;
    switch (p->pe_type & PT_MASK) {
    case PT_MATCH:
	match = expand_single(p->pe_match, TT_SINGLE, Q_WORD, ES_QUOTED);
	if (match == NULL)
	    goto failure2;
	match_each(values, match, p->pe_type);
	free(match);
	break;
    case PT_SUBST:
	match = expand_single(p->pe_match, TT_SINGLE, Q_WORD, ES_QUOTED);
	if (match == NULL)
	    goto failure2;
	subst = expand_single(p->pe_subst, TT_SINGLE, Q_WORD, ES_NONE);
	if (subst == NULL) {
	    free(match);
	    goto failure2;
	}
	subst_each(values, match, subst, p->pe_type);
	free(match);
	free(subst);
	break;
    }

    /* concatenate the elements of `values' */
    if (concat && indq)
	values = concatenate_values_into_array(values, false);

    /* PT_NUMBER */
    if (p->pe_type & PT_NUMBER)
	subst_length_each(values);

    struct expand_four_T e;

    pl_initwith(&e.valuelist, values, plcount(values));
    pl_initwithmax(&e.cclist, e.valuelist.length);

    /* create `e.cclist' contents */
    charcategory_T cc = CC_SOFT_EXPANSION | (indq * CC_QUOTED);
    for (size_t i = 0; i < e.valuelist.length; i++) {
	xwcsbuf_T sbuf;
	wb_initwith(&sbuf, e.valuelist.contents[i]);
	size_t n = sbuf.length;
	xstrbuf_T ccbuf;
	sb_initwithmax(&ccbuf, n + 1);
	sb_ccat_repeat(&ccbuf, cc, n);
	if (indq && e.valuelist.length > 1) {
	    // keep the field from empty field removal by adding a dummy quote
	    wb_wccat(&sbuf, L'"');
	    sb_ccat(&ccbuf, cc | CC_QUOTATION);
	}
	e.valuelist.contents[i] = wb_towcs(&sbuf);
	pl_add(&e.cclist, sb_tostr(&ccbuf));
    }

    return e;

failure2:
    plfree(values, free);
failure1:
    e.valuelist.contents = e.cclist.contents = NULL;
    return e;
}

/* Returns IDX_ALL, IDX_CONCAT, IDX_NUMBER if `indexstr' is L"@", L"*",
 * L"#" respectively. Otherwise returns IDX_NONE. */
enum indextype_T parse_indextype(const wchar_t *indexstr)
{
    if (indexstr[0] != L'\0' && indexstr[1] == L'\0') {
	switch (indexstr[0]) {
	    case L'@':  return IDX_ALL;
	    case L'*':  return IDX_CONCAT;
	    case L'#':  return IDX_NUMBER;
	}
    }
    return IDX_NONE;
}

/* Trims some leading and trailing characters of the wide string.
 * Characters in the range [`startindex', `endindex') remain.
 * Returns the string `s'. */
wchar_t *trim_wstring(wchar_t *s, ssize_t startindex, ssize_t endindex)
{
    if (startindex == 0 && endindex == SSIZE_MAX)
	return s;
    if (startindex < 0 || endindex < 0) {
	ssize_t len = wcslen(s);
	if (startindex < 0) {
	    startindex += len;
	    if (startindex < 0)
		startindex = 0;
	}
	if (endindex < 0) {
	    endindex += len + 1;
	    if (endindex <= startindex)
		goto return_empty;
	}
    }

    assert(startindex >= 0 && endindex >= 0);
    if (startindex >= endindex)
	goto return_empty;
    for (ssize_t i = 0; i < startindex; i++)
	if (s[i] == L'\0')
	    goto return_empty;
    for (ssize_t i = 0; i < endindex - startindex; i++)
	if ((s[i] = s[startindex + i]) == L'\0')
	    return s;
    s[endindex - startindex] = L'\0';
    return s;

return_empty:
    s[0] = L'\0';
    return s;
}

/* Trims some leading and trailing elements of the NULL-terminated array of
 * pointers.
 * Elements in the range [`startindex', `endindex') remain. `startindex' must
 * not be negative and `endindex' must not be less than `startindex'.
 * Removed elements are freed.
 * Returns the array `a'. */
/* `startindex' and/or `endindex' may be >= the length of the array. */
void **trim_array(void **a, ssize_t startindex, ssize_t endindex)
{
    assert(0 <= startindex && startindex <= endindex);
    if (startindex == 0 && endindex == SSIZE_MAX)
	return a;

    ssize_t len = endindex - startindex;
    for (ssize_t i = 0; i < startindex; i++) {
	if (a[i] == NULL) {
	    a[0] = NULL;
	    return a;
	}
	free(a[i]);
    }
    for (ssize_t i = 0; i < len; i++)
	if ((a[i] = a[startindex + i]) == NULL)
	    return a;
    for (ssize_t i = endindex; a[i] != NULL; i++)
	free(a[i]);
    a[len] = NULL;
    return a;
}

/* Expands `p->pe_subst' and prints it as an error message. */
void print_subst_as_error(const paramexp_T *p, quoting_T quoting)
{
    if (p->pe_subst != NULL) {
	wchar_t *subst =
	    expand_single(p->pe_subst, TT_SINGLE, quoting, ES_NONE);
	if (subst != NULL) {
	    if (p->pe_type & PT_NEST)
		xerror(0, "%ls", subst);
	    else
		xerror(0, "%ls: %ls", p->pe_name, subst);
	    free(subst);
	}
    } else {
	/* use the default error message */
	if (p->pe_type & PT_NEST)
	    xerror(0, Ngt("the parameter value is empty"));
	else
	    xerror(0, (p->pe_type & PT_COLON)
		    ? Ngt("parameter `%ls' is not set or has an empty value")
		    : Ngt("parameter `%ls' is not set"),
		    p->pe_name);
    }
}

/* Matches each string in array `slist' to pattern `pattern' and removes the
 * matching part of the string.
 * `slist' is a NULL-terminated array of pointers to `free'able wide strings.
 * `type' must contain at least one of PT_MATCHHEAD, PT_MATCHTAIL and
 * PT_MATCHLONGEST. If both of PT_MATCHHEAD and PT_MATCHTAIL are specified,
 * PT_MATCHLONGEST must be specified too.
 * Elements of `slist' may be modified and/or `realloc'ed in this function. */
void match_each(void **restrict slist, const wchar_t *restrict pattern,
	paramexptype_T type)
{
    xfnmflags_T flags = 0;
    assert(type & (PT_MATCHHEAD | PT_MATCHTAIL | PT_MATCHLONGEST));
    if (type & PT_MATCHHEAD)
	flags |= XFNM_HEADONLY;
    if (type & PT_MATCHTAIL)
	flags |= XFNM_TAILONLY;
    if (!(type & PT_MATCHLONGEST))
	flags |= XFNM_SHORTEST;

    xfnmatch_T *xfnm = xfnm_compile(pattern, flags);
    if (xfnm == NULL)
	return;

    for (size_t i = 0; slist[i] != NULL; i++) {
	wchar_t *s = slist[i];
	xfnmresult_T result = xfnm_wmatch(xfnm, s);
	if (result.start != (size_t) -1) {
	    xwcsbuf_T buf;
	    wb_initwith(&buf, s);
	    wb_remove(&buf, result.start, result.end - result.start);
	    slist[i] = wb_towcs(&buf);
	}
    }
    xfnm_free(xfnm);
}

/* Matches each string in array `slist' to pattern `pattern' and substitutes
 * the matching portions with `subst'.
 * `slist' is a NULL-terminated array of pointers to `free'able wide strings.
 * `type' may contain PT_MATCHHEAD, PT_MATCHTAIL and PT_SUBSTALL.
 * PT_MATCHLONGEST is always assumed to be specified.
 * Elements of `slist' may be modified and/or `realloc'ed in this function. */
void subst_each(void **restrict slist, const wchar_t *pattern,
	const wchar_t *subst, paramexptype_T type)
{
    xfnmflags_T flags = 0;
    if (type & PT_MATCHHEAD)
	flags |= XFNM_HEADONLY;
    if (type & PT_MATCHTAIL)
	flags |= XFNM_TAILONLY;

    xfnmatch_T *xfnm = xfnm_compile(pattern, flags);
    if (xfnm == NULL)
	return;

    for (size_t i = 0; slist[i] != NULL; i++) {
	wchar_t *s = slist[i];
	slist[i] = xfnm_subst(xfnm, s, subst, type & PT_SUBSTALL);
	free(s);
    }
    xfnm_free(xfnm);
}

/* Concatenates the wide strings in the specified array.
 * Array `*values' must be a NULL-terminated array of pointers to wide strings.
 * The strings are concatenated into one, each separated by the first $IFS
 * character. The separators are backslashed if `escape' is true.
 * The array and its element strings are all freed in this function.
 * The return value is a pointer to the newly malloced wide string that is the
 * result of concatenation. */
wchar_t *concatenate_values(void **values, bool escape)
{
    wchar_t *first = values[0];
    if (first != NULL && values[1] == NULL) {
	// no actual concatenation needed
	free(values);
	return first;
    }

    const wchar_t *ifs = getvar(L VAR_IFS);
    wchar_t padding[] = { L'\\', ifs != NULL ? ifs[0] : L' ', L'\0' };
    wchar_t *result = joinwcsarray(values, escape ? padding : &padding[1]);
    plfree(values, free);
    return result;
}

/* Like `concatenate_values', but returns a pointer to a newly malloced
 * NULL-terminated array containing the concatenated string. */
void **concatenate_values_into_array(void **values, bool escape)
{
    if (values[0] != NULL && values[1] == NULL)
	return values;

    void **results = xmallocn(2, sizeof *values);
    results[0] = concatenate_values(values, escape);
    results[1] = NULL;
    return results;
}

/* Substitutes each string in the specified array with a string that contains
 * the number of characters in the original string.
 * `slist' is a NULL-terminated array of pointers to `free'able wide strings.
 * The strings are `realloc'ed and modified in this function. */
void subst_length_each(void **slist)
{
    for (size_t i = 0; slist[i] != NULL; i++) {
	size_t len = wcslen(slist[i]);
	free(slist[i]);
	slist[i] = malloc_wprintf(L"%zu", len);
    }
}

/* Merge a result of `expand_param' into another expand_four_T value.
 * All the values in `from->valuelist' and `from->cclist' except the last one
 * are moved into `to->valuelist' and `to->cclist', respectively, and the last
 * one is left in `valuebuf' and `ccbuf'.
 * `from->valuelist' and `from->cclist' are destroyed in this function. */
void merge_expand_four(
	struct expand_four_T *restrict from,
	struct expand_four_T *restrict to,
	xwcsbuf_T *restrict valuebuf, xstrbuf_T *restrict ccbuf)
{
    assert(from->valuelist.length == from->cclist.length);
    assert(to->valuelist.length == to->cclist.length);
    assert(valuebuf->length == ccbuf->length);

    if (from->valuelist.length > 0) {
	/* add the first element */
	wb_catfree(valuebuf, from->valuelist.contents[0]);
	sb_ncat_force(ccbuf, from->cclist.contents[0],
		valuebuf->length - ccbuf->length);
	free(from->cclist.contents[0]);

	if (from->valuelist.length > 1) {
	    pl_add(&to->valuelist, wb_towcs(valuebuf));
	    pl_add(&to->cclist, sb_tostr(ccbuf));

	    /* add the other elements but last */
	    pl_ncat(&to->valuelist,
		    &from->valuelist.contents[1], from->valuelist.length - 2);
	    pl_ncat(&to->cclist,
		    &from->cclist.contents[1], from->cclist.length - 2);

	    /* add the last element */
	    size_t i = from->valuelist.length - 1;
	    wb_initwith(valuebuf, from->valuelist.contents[i]);
	    sb_initwithmax(ccbuf, valuebuf->length);
	    sb_ncat_force(ccbuf, from->cclist.contents[i], valuebuf->length);
	    free(from->cclist.contents[i]);
	}
    }

    pl_destroy(&from->valuelist);
    pl_destroy(&from->cclist);
}


/********** Brace Expansions **********/

/* Performs brace expansion in each element of the specified array.
 * `values' is an array of pointers to `free'able wide strings to be expanded.
 * `ccs' is an array of pointers to `free'able charcategory_T strings.
 * `values' and `ccs' must contain the same number of elements and be NULL-
 * terminated. Their elements are freed in this function. The arrays themselves
 * are not freed.
 * Newly malloced results are added to `valuelist' and `cclist'. */
void expand_brace_each(
	void *const *restrict values, void *const *restrict ccs,
	plist_T *restrict valuelist, plist_T *restrict cclist)
{
    while (*values != NULL) {
	expand_brace(*values, *ccs, valuelist, cclist);
	values++, ccs++;
    }
}

/* Performs brace expansion in the specified single word.
 * `cc' is the charcategory_T string corresponding to `word'.
 * `word' and `cc' are freed in this function.
 * `Free'able results are added to `valuelist' and `cclist'. */
void expand_brace(
	wchar_t *restrict const word, char *restrict const cc,
	plist_T *restrict valuelist, plist_T *restrict cclist)
{
#define idx(p) ((size_t) ((wchar_t *) (p) - word))

    /* quick check */
    const wchar_t *c;
    if ((c = wcschr(word, L'{')) == NULL || (c = wcschr(c + 1, L'}')) == NULL) {
no_expansion:
	pl_add(valuelist, word);
	pl_add(cclist, cc);
	return;
    }

    /* First, we create a `graph' by scanning all the characters in the `word'.
     * The graph is a list of pointers that has the same length as the word,
     * which means each element in the graph corresponds to the character at the
     * same index in the word. For each L'{' and L',' in the word, the
     * corresponding graph element will be a pointer to the next matching L','
     * or L'}' in the word. For L'}' that has one or more matching L','s, the
     * corresponding element is a pointer to the L'}' itself. For other
     * characters, the graph element will be NULL. */
    /* To find matching L','s and L'}'s correctly, we keep track of nested
     * braces by using a `stack'. Every time we find a new L'{', a pointer to
     * the L'{' is pushed into the stack *twice*. The first one keeps pointing
     * to the L'{' while the second is updated to point to the last found L','
     * during the scan. The two pointers are popped out of the stack when a
     * matching L'}' is found. */
    bool pairfound = false;
    plist_T graph;
    plist_T stack;
    pl_initwithmax(&graph, idx(c) + wcslen(c) /* = wcslen(word) */);
    pl_init(&stack);
    while (word[graph.length] != L'\0') {
	if (cc[graph.length] == CC_LITERAL) {
	    switch (word[graph.length]) {
		case L'{':
		    pl_add(&stack, &word[graph.length]);
		    pl_add(&stack, &word[graph.length]);
		    break;
		case L',':
		    if (stack.length > 0) {
			size_t ci = idx(stack.contents[stack.length - 1]);
			assert(ci < graph.length);
			graph.contents[ci] = &word[graph.length];
			stack.contents[stack.length - 1] = &word[graph.length];
		    }
		    break;
		case L'}':
		    if (stack.length > 0) {
			size_t ci = idx(stack.contents[stack.length - 1]);
			assert(ci < graph.length);
			graph.contents[ci] = &word[graph.length];
			assert(stack.length % 2 == 0);
			pl_truncate(&stack, stack.length - 2);
			pairfound = true;
			if (word[ci] == L',') {
			    pl_add(&graph, &word[graph.length]);
			    continue;
			}
		    }
		    break;
	    }
	}
	pl_add(&graph, NULL);
    }

    /* If no pairs of braces were found, we don't need to expand anything. */
    if (!pairfound) {
	pl_destroy(&stack);
	pl_destroy(&graph);
	goto no_expansion;
    }

    /* After scanning the whole `word', if we have any elements left in the
     * stack, they are L'{'s that have no matching L'}'s. They cannot be
     * expanded, so let's remove them from the graph. */
    while (stack.length > 0) {
	assert(stack.length % 2 == 0);
	size_t ci = idx(stack.contents[stack.length - 2]);
	const wchar_t *cnext;
	while ((cnext = graph.contents[ci]) != NULL) {
	    graph.contents[ci] = NULL;
	    ci = idx(cnext);
	}
	pl_truncate(&stack, stack.length - 2);
    }
    pl_destroy(&stack);

    /* Now start expansion! */
    struct brace_expand_T e = {
	.word = word,
	.cc = cc,
	.graph = graph.contents,
	.valuelist = valuelist,
	.cclist = cclist,
    };
    xwcsbuf_T valuebuf;
    xstrbuf_T ccbuf;
    wb_init(&valuebuf);
    sb_init(&ccbuf);
    generate_brace_expand_results(&e, 0, &valuebuf, &ccbuf);
    pl_destroy(&graph);
    free(word);
    free(cc);
#undef idx
}

/* Generates results of brace expansion.
 * Part of `e->word' that has been processed before calling this function
 * may have been added to `valuebuf' and `ccbuf'.
 * This function modifies `valuebuf' and `ccbuf' in place to construct the
 * results, and finally destroys them.
 * The results are added to `e->valuelist' and `e->cclist'. */
void generate_brace_expand_results(
	const struct brace_expand_T *restrict e, size_t ci,
	xwcsbuf_T *restrict valuebuf, xstrbuf_T *restrict ccbuf)
{
start:
    /* add normal characters up to the next delimiter */
    while (e->word[ci] != L'\0' && e->graph[ci] == NULL) {
normal:
	wb_wccat(valuebuf, e->word[ci]);
	sb_ccat(ccbuf, e->cc[ci]);
	ci++;
    }

    switch (e->word[ci]) {
	case L'\0':
	    /* No more characters: we're done! */
	    pl_add(e->valuelist, wb_towcs(valuebuf));
	    pl_add(e->cclist, sb_tostr(ccbuf));
	    return;
	case L',':
	    /* skip up to next L'}' and go on */
	    do {
		const wchar_t *nextdelimiter = e->graph[ci];
		ci = nextdelimiter - e->word;
	    } while (e->word[ci] == L',');
	    assert(e->word[ci] == L'}');
	    /* falls thru! */
	case L'}':
	    /* skip this L'}' and go on */
	    ci++;
	    goto start;
    }
    assert(e->word[ci] == L'{');

    const wchar_t *nextdelimiter = e->graph[ci];
    if (*nextdelimiter == L'}') {
	/* No commas between the braces: try numeric brace expansion */
	if (try_expand_brace_sequence(e, ci, valuebuf, ccbuf))
	    return;

	/* No numeric brace expansion happened.
	 * Treat this brace as normal and go on. */
	goto normal;
    }
    assert(*nextdelimiter == L',');

    /* Now generate the results (except the last one) */
    while (*nextdelimiter == L',') {
	xwcsbuf_T valuebuf2;
	xstrbuf_T ccbuf2;
	wb_initwithmax(&valuebuf2, valuebuf->maxlength);
	wb_ncat_force(&valuebuf2, valuebuf->contents, valuebuf->length);
	sb_initwithmax(&ccbuf2, ccbuf->maxlength);
	sb_ncat_force(&ccbuf2, ccbuf->contents, ccbuf->length);
	ci++;
	generate_brace_expand_results(e, ci, &valuebuf2, &ccbuf2);
	ci = nextdelimiter - e->word;
	nextdelimiter = e->graph[ci];
    }
    assert(*nextdelimiter == L'}');

    /* Generate the last one */
    ci++;
    goto start; // generate_brace_expand_results(e, ci, valuebuf, ccbuf);
}

/* Tries numeric brace expansion like "{01..05}".
 * `ci' must be the index of the L'{' character in `e->word'.
 * If unsuccessful, this function returns false without any side effects.
 * If successful, the results are added to `e->valuelist' and `e->cclist'.
 * In that case, this function modifies `valuebuf' and `ccbuf' in place to
 * construct the results, and finally destroys them. */
bool try_expand_brace_sequence(
	const struct brace_expand_T *restrict e, size_t ci,
	xwcsbuf_T *restrict valuebuf, xstrbuf_T *restrict ccbuf)
{
    assert(e->word[ci] == L'{');
    ci++;

    size_t starti = ci;

    /* parse the starting point */
    const wchar_t *c = &e->word[ci];
    wchar_t *cp;
    errno = 0;
    long start = wcstol(c, &cp, 10);
    if (c == cp || errno != 0 || cp[0] != L'.' || cp[1] != L'.')
	return false;

    bool sign = false;
    int startlen = has_leading_zero(c, &sign) ? (cp - c) : 0;

    /* parse the ending point */
    c = cp + 2;
    errno = 0;
    long end = wcstol(c, &cp, 10);
    if (c == cp || errno != 0)
	return false;
    int endlen = has_leading_zero(c, &sign) ? (cp - c) : 0;

    /* parse the delta */
    long delta;
    if (cp[0] == L'.') {
	if (cp[1] != L'.')
	    return false;

	c = cp + 2;
	errno = 0;
	delta = wcstol(c, &cp, 10);
	if (delta == 0 || c == cp || errno != 0 || cp[0] != L'}')
	    return false;
    } else if (cp[0] == L'}') {
	if (start <= end)
	    delta = 1;
	else
	    delta = -1;
    } else {
	return false;
    }

    /* validate charcategory_T */
    size_t bracei = cp - e->word;
    if (e->cc[bracei] != CC_LITERAL)
	return false;
    for (ci = starti; ci < bracei; ci++)
	if (e->cc[ci] & CC_QUOTED)
	    return false;

    /* expand the sequence */
    long value = start;
    int len = (startlen > endlen) ? startlen : endlen;
    ci = cp - e->word + 1;
    do {
	xwcsbuf_T valuebuf2;
	xstrbuf_T ccbuf2;
	wb_initwithmax(&valuebuf2, valuebuf->maxlength);
	wb_ncat_force(&valuebuf2, valuebuf->contents, valuebuf->length);
	sb_initwithmax(&ccbuf2, ccbuf->maxlength);
	sb_ncat_force(&ccbuf2, ccbuf->contents, ccbuf->length);

	/* format the number */
	int plen =
	    wb_wprintf(&valuebuf2, sign ? L"%0+*ld" : L"%0*ld", len, value);
	if (plen >= 0)
	    sb_ccat_repeat(&ccbuf2, CC_HARD_EXPANSION, plen);

	/* expand the remaining portion recursively */
	generate_brace_expand_results(e, ci, &valuebuf2, &ccbuf2);

	if (delta >= 0) {
	    if (LONG_MAX - delta < value)
		break;
	} else {
	    if (LONG_MIN - delta > value)
		break;
	}
	value += delta;
    } while (delta >= 0 ? value <= end : value >= end);

    wb_destroy(valuebuf);
    sb_destroy(ccbuf);
    return true;
}

/* Checks if the specified numeral starts with a L'0'.
 * Leading spaces are ignored.
 * If the numeral has a plus sign L'+', true is assigned to `*sign'.
 * If not, false is assigned. */
bool has_leading_zero(const wchar_t *restrict s, bool *restrict sign)
{
    while (iswspace(*s))
	s++;
    if (*s == L'+') {
	*sign = true;
	s++;
    } else if (*s == L'-') {
	s++;
    }
    return *s == L'0';
}


/********** Field Splitting **********/

/* Performs field splitting.
 * `valuelist' is a NULL-terminated array of pointers to wide strings to split.
 * `cclist' is an array of pointers to corresponding charcategory_T strings.
 * `valuelist' and `cclist' are `plfree'ed in this function.
 * The results are added to `outvaluelist' and `outcclist'. */
void fieldsplit(void **restrict const valuelist, void **restrict const cclist,
	plist_T *restrict outvaluelist, plist_T *restrict outcclist)
{
    const wchar_t *ifs = getvar(L VAR_IFS);
    if (ifs == NULL)
	ifs = DEFAULT_IFS;

    plist_T fields;
    pl_init(&fields);

    for (size_t i = 0; valuelist[i] != NULL; i++) {
	wchar_t *s = valuelist[i];
	char *cc = cclist[i];
	extract_fields(s, cc, ifs, &fields);
	assert(fields.length % 2 == 0);

	if (fields.length == 2 && fields.contents[0] == s &&
		*(wchar_t *) fields.contents[1] == L'\0') {
	    /* The result is the same as the original field. */
	    pl_add(outvaluelist, s);
	    pl_add(outcclist, cc);
	} else {
	    /* Produce new fields. */
	    for (size_t j = 0; j < fields.length; j += 2) {
		const wchar_t *start = fields.contents[j];
		const wchar_t *end = fields.contents[j + 1];
		size_t idx = start - s, len = end - start;
		pl_add(outvaluelist, xwcsndup(start, len));
		pl_add(outcclist, memcpy(xmalloc(len), &cc[idx], len));
	    }
	    free(s);
	    free(cc);
	}

	pl_truncate(&fields, 0);
    }
    pl_destroy(&fields);
    free(valuelist);
    free(cclist);
}

/* Extracts fields from a string.
 * `s' is the word to split.
 * `cc` is an array of charcategory_T values corresponding to `s'. It must be at
 * least as long as `wcslen(s)'.
 * `ifs' must not be NULL.
 *
 * The results are appended to `dest'. If n fields are found, 2n pointers are
 * appended to `dest'. The first pointer points to the first character of the
 * first field in `s'. The second to the character past the last character of
 * the first field. The third to the first character of the second field. And so
 * on.
 *
 * The word is split at characters that are contained in `ifs' and whose
 * corresponding character in `cc' is CC_SOFT_EXPANSION. Refer to POSIX for how
 * whitespaces are treated in field splitting.
 *
 * If an IFS non-whitespace delimits an empty field, the field is assumed just
 * before the non-whitespace delimiter. The empty last field is removed if
 * `shopt_emptylastfield' is false.
 *
 * The return value is a pointer to the end of the input string (but before
 * trailing IFS whitespaces). */
/* Split examples (assuming `ifs' = L" -" and `shopt_emptylastfield' is true)
 *   ""                  ->   (nothing)
 *   "  "                ->   (nothing)
 *   " abc 123 "         ->   "abc" "123"
 *   "  abc  123  "      ->   "abc" "123"
 *   "-abc-123-"         ->   "" "abc" "123" ""
 *   " - abc - 123 - "   ->   "" "abc" "123" ""
 *   "abc--123"          ->   "abc" "" "123"
 *   "abc - - 123"       ->   "abc" "" "123"
 */
wchar_t *extract_fields(const wchar_t *restrict s, const char *restrict cc,
	const wchar_t *restrict ifs, plist_T *restrict dest)
{
    size_t index = 0;
    size_t ifswhitestartindex;
    size_t oldlen = dest->length;

    /* true when the currently skipping IFS whitespaces immediately follow a
     * previously split field. */
    bool afterfield = false;

    for (;;) {
	ifswhitestartindex = index;
	while (is_ifs_whitespace(s[index], cc[index], ifs))
	    index++;

	/* extract next field, if any */
	size_t fieldstartindex = index;
	while (is_non_ifs_char(s[index], cc[index], ifs))
	    index++;
	if (index != fieldstartindex) {
	    pl_add(pl_add(dest, &s[fieldstartindex]), &s[index]);
	    afterfield = true;
	    continue;
	}

	/* Now the current char is either null or an IFS non-whitespace. */

	if (!afterfield)
	    add_empty_field(dest, &s[index]);

	if (s[index] == L'\0')
	    break;

	/* skip (only) one IFS non-whitespace */
	assert(is_ifs_char(s[index], cc[index], ifs));
	assert(!is_ifs_whitespace(s[index], cc[index], ifs));
	index++;
	afterfield = false;
    }

    /* remove the empty last field */
    size_t newlen = dest->length;
    assert(newlen - oldlen >= 2);
    if (!shopt_emptylastfield || newlen - oldlen == 2)
	if (dest->contents[newlen - 2] == dest->contents[newlen - 1])
	    pl_truncate(dest, newlen - 2);

    assert(dest->length >= oldlen);
    return (wchar_t *) &s[ifswhitestartindex];
}

/* Returns true if `c' is a non-null, IFS character. */
bool is_ifs_char(wchar_t c, charcategory_T cc, const wchar_t *ifs)
{
    return cc == CC_SOFT_EXPANSION && c != L'\0' && wcschr(ifs, c) != NULL;
}

/* Returns true if `c' is a non-null, IFS-whitespace character. */
bool is_ifs_whitespace(wchar_t c, charcategory_T cc, const wchar_t *ifs)
{
    return is_ifs_char(c, cc, ifs) && iswspace(c);
}

/* Returns true if `c' is a non-null, non-IFS character. */
bool is_non_ifs_char(wchar_t c, charcategory_T cc, const wchar_t *ifs)
{
    return c != L'\0' && !is_ifs_char(c, cc, ifs);
}

void add_empty_field(plist_T *dest, const wchar_t *p)
{
    pl_add(dest, p);
    pl_add(dest, p);
}


/********** Escaping **********/

/* Unquotes the specified single-quoted string and adds it to the specified
 * buffer.
 * `ss' is a pointer to a pointer to the opening quote in the string.
 * `*ss' is incremented so that it points to the closing quote.
 * If there is no closing quote, `*ss' will point to the L'\0' terminator.
 * If `escape' is true, all the characters added are backslashed. */
void add_sq(const wchar_t *restrict *ss, xwcsbuf_T *restrict buf, bool escape)
{
    assert(**ss == L'\'');
    for (;;) {
	(*ss)++;
	switch (**ss) {
	    case L'\0':
	    case L'\'':
		return;
	    default:
		if (escape)
		    wb_wccat(buf, L'\\');
		wb_wccat(buf, **ss);
		break;
	}
    }
}

/* Backslashes characters in `s' that are contained in `t'.
 * Returns a newly-malloced wide string.
 * `t' may be NULL, in which case all the characters are backslashed. */
wchar_t *escape(const wchar_t *restrict s, const wchar_t *restrict t)
{
    xwcsbuf_T buf;
    wb_initwithmax(&buf, mul(wcslen(s), 2));
    for (size_t i = 0; s[i] != L'\0'; i++) {
	if (t == NULL || wcschr(t, s[i]) != NULL)
	    wb_wccat(&buf, L'\\');
	wb_wccat(&buf, s[i]);
    }
    return wb_towcs(&buf);
}

/* Same as `escape', except that the first argument is freed. */
wchar_t *escapefree(wchar_t *restrict s, const wchar_t *restrict t)
{
    if (t != NULL && wcspbrk(s, t) == NULL)
	return s;

    wchar_t *result = escape(s, t);
    free(s);
    return result;
}

/* Removes backslash escapes. The result is a newly malloced string.
 * If there is an unescaped backslash before the null character, the backslash
 * is ignored. */
wchar_t *unescape(const wchar_t *s)
{
    xwcsbuf_T buf;
    wb_initwithmax(&buf, wcslen(s));
    for (size_t i = 0; s[i] != L'\0'; i++) {
	if (s[i] == L'\\') {
	    i++;
	    if (s[i] == L'\0')
		break;
	}
	wb_wccat(&buf, s[i]);
    }
    return wb_towcs(&buf);
}

#if 0
/* Same as `unescape', except that the first argument is freed. */
wchar_t *unescapefree(wchar_t *s)
{
    if (wcschr(s, L'\\') == NULL)
	return s;

    wchar_t *result = unescape(s);
    free(s);
    return result;
}
#endif

/* Quotes the specified string using backslashes and single-quotes. The result
 * is suitable for re-parsing as a shell command word that would expand to the
 * original string. The result is a newly malloced string. */
wchar_t *quote_as_word(const wchar_t *s)
{
    xwcsbuf_T buf;
    wb_initwithmax(&buf, mul(wcslen(s), 2));
    wb_quote_as_word(&buf, s);
    return wb_towcs(&buf);
}

/* Quotes string `s' using backslashes and single-quotes. The result
 * is suitable for re-parsing as a shell command word that would expand to the
 * original string. The result is appended to the given buffer, which must
 * have been initialized before calling this function. */
xwcsbuf_T *wb_quote_as_word(xwcsbuf_T *restrict buf, const wchar_t *restrict s)
{
    if (*s == L'\0') {
	wb_wccat(buf, L'\'');
	wb_wccat(buf, L'\'');
	return buf;
    }

    while (*s != L'\0') {
	if (*s == L'\'') {
	    wb_wccat(buf, L'\\');
	    wb_wccat(buf, L'\'');
	    s++;
	    continue;
	}

	const wchar_t *end = s;
	while (*end == L'.' || *end == L'-' || *end == L'_' || *end == L'/' ||
		iswalnum(*end))
	    end++;
	if (*end == L'\0' || *end == L'\'') {
	    /* No characters have to be quoted until `*end'. */
	    wb_ncat_force(buf, s, end - s);
	    s = end;
	    continue;
	}

	/* Quote characters until the next single-quote or end-of-string. */
	wb_ensuremax(buf, buf->length + (end - s) + 2);
	wb_wccat(buf, L'\'');
	while (*s != L'\0' && *s != L'\'') {
	    wb_wccat(buf, *s);
	    s++;
	}
	wb_wccat(buf, L'\'');
    }

    return buf;
}

/* Removes quotes (', ", \).
 * The result is a newly malloced string if successful.
 * Returns NULL if there is an unclosed quote. */
wchar_t *unquote(const wchar_t *s)
{
    bool indq = false;
    xwcsbuf_T buf;
    wb_initwithmax(&buf, wcslen(s));
    for (;;) {
	switch (*s) {
	case L'\0':
	    if (indq) {
		wb_destroy(&buf);
		return NULL;
	    }
	    return wb_towcs(&buf);
	case L'\'':
	    if (indq)
		goto default_case;
	    add_sq(&s, &buf, false);
	    if (*s == L'\0') {
		wb_destroy(&buf);
		return NULL;
	    }
	    assert(*s == L'\'');
	    break;
	case L'"':
	    indq = !indq;
	    break;
	case L'\\':
	    if (s[1] != L'\0' && (!indq || wcschr(CHARS_ESCAPABLE, s[1]))) {
		wb_wccat(&buf, s[1]);
		s += 2;
		continue;
	    }
	    /* falls thru! */
	default:  default_case:
	    wb_wccat(&buf, *s);
	    break;
	}
	s++;
    }
}

/* Tests if a character should be backslash-escaped. */
bool should_escape(charcategory_T cc, escaping_T escaping)
{
    switch (escaping) {
	case ES_NONE:
	    return false;
	case ES_QUOTED_HARD:
	    if ((cc & CC_ORIGIN_MASK) == CC_HARD_EXPANSION)
		return true;
	    /* falls thru! */
	case ES_QUOTED:
	    return cc & CC_QUOTED;
    }
    assert(false);
}

/* Removes all quotation marks in the input string `s' and optionally add
 * backslash escapes to the originally quoted characters as specified by
 * `escaping'. The result is a newly malloced string. */
wchar_t *quote_removal(
	const wchar_t *restrict s, const char *restrict cc, escaping_T escaping)
{
    xwcsbuf_T result;
    wb_initwithmax(&result, mul(wcslen(s), 2));
    for (size_t i = 0; s[i] != L'\0'; i++) {
	if (cc[i] & CC_QUOTATION)
	    continue;
	if (should_escape(cc[i], escaping))
	    wb_wccat(&result, L'\\');
	wb_wccat(&result, s[i]);
    }
    return wb_towcs(&result);
}

/* Like `quote_removal', but frees the arguments. */
wchar_t *quote_removal_free(
	wchar_t *restrict s, char *restrict cc, escaping_T escaping)
{
    wchar_t *result = quote_removal(s, cc, escaping);
    free(s);
    free(cc);
    return result;
}


/********** Pathname Expansion (Glob) **********/

/* Makes a option value from the current shell settings. */
enum wglobflags_T get_wglobflags(void)
{
    enum wglobflags_T flags = 0;
    if (!shopt_caseglob)    flags |= WGLB_CASEFOLD;
    if (shopt_dotglob)      flags |= WGLB_PERIOD;
    if (shopt_markdirs)     flags |= WGLB_MARK;
    if (shopt_extendedglob) flags |= WGLB_RECDIR;
    return flags;
}

/* Performs pathname expansion.
 * If `shopt_glob' is off or a field is not a pattern, quote removal is
 * performed instead.
 * The input lists and their contents in `e' are freed in this function.
 * The results are added to `results' as newly-malloced wide strings. */
void glob_all(struct expand_four_T *restrict e, plist_T *restrict results)
{
    enum wglobflags_T flags = get_wglobflags();
    bool unblock = false;

    for (size_t i = 0; i < e->valuelist.length; i++) {
	wchar_t *field = e->valuelist.contents[i];
	char *cc = e->cclist.contents[i];
	wchar_t *pattern = quote_removal(field, cc, ES_QUOTED_HARD);
	if (shopt_glob && is_pathname_matching_pattern(pattern)) {
	    if (!unblock) {
		set_interruptible_by_sigint(true);
		unblock = true;
	    }

	    size_t oldlen = results->length;
	    wglob(pattern, flags, results);
	    if (!shopt_nullglob && oldlen == results->length)
		goto quote_removal;
	} else {
	    /* If the pattern doesn't contain characters like L'*' and L'?',
	     * we don't need to glob. */
quote_removal:
	    pl_add(results, quote_removal(field, cc, ES_NONE));
	}
	free(field);
	free(cc);
	free(pattern);
    }
    if (unblock)
	set_interruptible_by_sigint(false);
    pl_destroy(&e->valuelist);
    pl_destroy(&e->cclist);
}


/********** Auxiliary functions **********/

/* Performs parameter expansion, command substitution of the form "$(...)", and
 * arithmetic expansion in the specified string.
 * If `name' is non-NULL, it is printed in error messages on error.
 * If `esc' is true, backslashes preceding $, `, \ are removed. Otherwise,
 * no quotations are removed.
 * Returns a newly malloced string if successful. Otherwise NULL is returned.
 * This function uses the parser, so the parser state must have been saved if
 * this function is called during another parse. */
wchar_t *parse_and_expand_string(const wchar_t *s, const char *name, bool esc)
{
    struct input_wcs_info_T winfo = {
	.src = s,
    };
    parseparam_T info = {
	.print_errmsg = true,
	.enable_verbose = false,
	.enable_alias = true,
	.filename = name,
	.lineno = 1,
	.input = input_wcs,
	.inputinfo = &winfo,
	.interactive = false,
    };
    wordunit_T *word;
    wchar_t *result;

    if (!parse_string(&info, &word))
	return NULL;
    result = expand_single(word, TT_NONE, esc ? Q_INDQ : Q_LITERAL, ES_NONE);
    wordfree(word);
    return result;
}

/* This function is called when an expansion error occurred.
 * The shell exits if it is non-interactive. */
void maybe_exit_on_error(void)
{
    if (shell_initialized && !is_interactive_now)
	exit_shell_with_status(Exit_EXPERROR);
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
