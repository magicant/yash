/* Yash: yet another shell */
/* expand.c: word expansion */
/* (C) 2007-2020 magicant */

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


/* characters that have special meanings in brace expansion, quote removal, and
 * globbing. When an unquoted expansion includes these characters, they are
 * backslashed to protect from unexpected side effects in succeeding expansion
 * steps. */
#define CHARS_ESCAPED L"\\\"\'{,}"

/* data passed between expansion functions */
struct expand_four_T {
    plist_T valuelist, cclist;
    xwcsbuf_T valuebuf;
    xstrbuf_T ccbuf;
    bool zeroword;
};
/* If expansion yields multiple fields, all the fields are added to `valuelist'
 * except that the last field remains in `valuebuf'. Character categories
 * (charcategory_T) corresponding to the characters in `valuelist' and
 * `valuebuf' are cast to char and added to `cclist' and `ccbuf' accordingly. */
/* When "$@" appears during expansion and there is no positional parameter, the
 * `zeroword' flag is set so that the quoted empty word can be removed later. */

static plist_T expand_word(const wordunit_T *w,
	tildetype_T tilde, quoting_T quoting, escaping_T escaping)
    __attribute__((warn_unused_result));
static plist_T expand_four_and_remove_quotes(
	const wordunit_T *restrict w,
	tildetype_T tilde, bool processquotes, bool escapeall);
static bool expand_four(const wordunit_T *restrict w,
	tildetype_T tilde, bool processquotes, bool escapeall,
	charcategory_T defaultcc, struct expand_four_T *restrict e)
    __attribute__((nonnull(6)));
static bool expand_4444(const wordunit_T *restrict w, tildetype_T tilde,
	quoting_T quoting, charcategory_T defaultcc,
	struct expand_four_T *restrict e)
    __attribute__((nonnull(5)));
static void fill_ccbuf(struct expand_four_T *e, charcategory_T c)
    __attribute__((nonnull));
static void append_value(wchar_t *restrict s, const wchar_t *t,
	charcategory_T defaultcc, struct expand_four_T *e)
    __attribute__((nonnull(1,4)));

static wchar_t *expand_tilde(const wchar_t **ss,
	bool hasnextwordunit, tildetype_T tt)
    __attribute__((nonnull,malloc,warn_unused_result));

enum indextype_T { IDX_NONE, IDX_ALL, IDX_CONCAT, IDX_NUMBER, };

static bool expand_param(const paramexp_T *restrict p, bool indq,
	struct expand_four_T *restrict e)
    __attribute__((nonnull));
static enum indextype_T parse_indextype(const wchar_t *indexstr)
    __attribute__((nonnull,pure));
static wchar_t *trim_wstring(wchar_t *s, ssize_t startindex, ssize_t endindex)
    __attribute__((nonnull));
static void **trim_array(void **a, ssize_t startindex, ssize_t endindex)
    __attribute__((nonnull));
static void print_subst_as_error(const paramexp_T *p)
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

static void expand_brace_each(
	void *const *restrict values, void *const *restrict ccs,
	plist_T *restrict valuelist, plist_T *restrict cclist)
    __attribute__((nonnull));
static void expand_brace(
	wchar_t *restrict word, char *restrict cc,
	plist_T *restrict valuelist, plist_T *restrict cclist)
    __attribute__((nonnull));
static bool try_expand_brace_sequence(
	wchar_t *word, char *restrict cc, wchar_t *startc,
	plist_T *restrict valuelist, plist_T *restrict cclist)
    __attribute__((nonnull));
static bool has_leading_zero(const wchar_t *restrict s, bool *restrict sign)
    __attribute__((nonnull));

static void fieldsplit_all(
	void **restrict valuelist, void **restrict cclist,
	plist_T *restrict outvaluelist, plist_T *restrict outcclist)
    __attribute__((nonnull));
static void fieldsplit(wchar_t *restrict s, char *restrict cc,
	const wchar_t *restrict ifs,
	plist_T *restrict outvaluelist, plist_T *restrict outcclist)
    __attribute__((nonnull));
static size_t skip_ifs(const wchar_t *s, const char *cc,
	bool escaped, const wchar_t *ifs)
    __attribute__((nonnull,pure));
static size_t skip_ifs_whitespaces(const wchar_t *s, const char *cc,
	bool escaped, const wchar_t *ifs)
    __attribute__((nonnull,pure));
static size_t skip_field(const wchar_t *s, const char *cc,
	bool escaped, const wchar_t *ifs)
    __attribute__((nonnull,pure));
static void add_empty_field(plist_T *dest, const wchar_t *p)
    __attribute__((nonnull));

static inline void add_sq(
	const wchar_t *restrict *ss, xwcsbuf_T *restrict buf, bool escape)
    __attribute__((nonnull));
static wchar_t *escaped_wcspbrk(const wchar_t *s, const wchar_t *accept)
    __attribute__((nonnull));
static wchar_t *escaped_remove(const wchar_t *s, const wchar_t *reject)
    __attribute__((nonnull,malloc,warn_unused_result));
static inline wchar_t *escaped_remove_free(wchar_t *s, const wchar_t *reject)
    __attribute__((nonnull,malloc,warn_unused_result));
static inline bool should_escape(char c, charcategory_T cc, escaping_T escaping)
    __attribute__((const));
static wchar_t *quote_removal(
	const wchar_t *restrict s, const char *restrict cc, escaping_T escaping)
    __attribute__((nonnull,malloc,warn_unused_result));
static wchar_t *quote_removal_free(
	wchar_t *restrict s, char *restrict cc, escaping_T escaping)
    __attribute__((nonnull,malloc,warn_unused_result));

static void glob_all(void **restrict patterns, plist_T *restrict list)
    __attribute__((nonnull));
static enum wglobflags_T get_wglobflags(void)
    __attribute__((pure));

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
    struct expand_four_T expand;
    pl_init(&expand.valuelist);
    pl_init(&expand.cclist);
    wb_init(&expand.valuebuf);
    sb_init(&expand.ccbuf);
    expand.zeroword = false;

    /* four expansions (w -> valuelist) */
    if (!expand_four(w, TT_SINGLE, true, false, CC_LITERAL, &expand)) {
	plfree(pl_toary(&expand.valuelist), free);
	plfree(pl_toary(&expand.cclist), free);
	wb_destroy(&expand.valuebuf);
	sb_destroy(&expand.ccbuf);
	maybe_exit_on_error();
	return false;
    }
    assert(expand.valuebuf.length == expand.ccbuf.length);
    pl_add(&expand.valuelist, wb_towcs(&expand.valuebuf));
    pl_add(&expand.cclist, sb_tostr(&expand.ccbuf));

    /* brace expansion (valuelist -> valuelist2) */
    plist_T valuelist2, cclist2;
    if (shopt_braceexpand) {
	pl_init(&valuelist2);
	pl_init(&cclist2);
	expand_brace_each(expand.valuelist.contents, expand.cclist.contents,
		&valuelist2, &cclist2);
	pl_clear(&expand.valuelist, 0);
	pl_clear(&expand.cclist, 0);
    } else {
	valuelist2 = expand.valuelist;
	cclist2 = expand.cclist;
	pl_init(&expand.valuelist);
	pl_init(&expand.cclist);
    }

    /* field splitting (valuelist2 -> valuelist) */
    fieldsplit_all(pl_toary(&valuelist2), pl_toary(&cclist2),
	    &expand.valuelist, &expand.cclist);
    assert(expand.valuelist.length == expand.cclist.length);

    /* empty field removal */
    if (expand.valuelist.length == 1) {
	const wchar_t *field = expand.valuelist.contents[0];
	const char *cc = expand.cclist.contents[0];
	if (field[0] == L'\0' ||
		(expand.zeroword && wcscmp(field, L"\"\"") == 0 &&
		(cc[0] & cc[1] & CC_QUOTATION))) {
	    pl_clear(&expand.valuelist, free);
	    pl_clear(&expand.cclist, free);
	}
    }

    // TODO use cclist in the following steps
    plfree(pl_toary(&expand.cclist), free);

    /* quote removal */
    for (size_t i = 0; i < expand.valuelist.length; i++)
	expand.valuelist.contents[i] =
	    escaped_remove_free(expand.valuelist.contents[i], L"\"\'");

    /* globbing (valuelist -> list) */
    if (shopt_glob) {
	glob_all(pl_toary(&expand.valuelist), list);
    } else {
	for (size_t i = 0; i < expand.valuelist.length; i++)
	    pl_add(list, unescapefree(expand.valuelist.contents[i]));
	pl_destroy(&expand.valuelist);
    }

    return true;
}

/* Expands a word to (possibly any number of) fields.
 * If successful, the return value is a plist_T containing newly malloced wide
 * strings. In most cases, the plist_T contains one string. If the word contains
 * "$@", however, it may contain any number of strings.
 * On error, the return value is a plist_T with `contents' being NULL. */
plist_T expand_word(const wordunit_T *w,
	tildetype_T tilde, quoting_T quoting, escaping_T escaping)
{
    struct expand_four_T expand;

    pl_init(&expand.valuelist);
    pl_init(&expand.cclist);
    wb_init(&expand.valuebuf);
    sb_init(&expand.ccbuf);
    expand.zeroword = false;

    if (!expand_4444(w, tilde, quoting, CC_LITERAL, &expand)) {
	plfree(pl_toary(&expand.valuelist), free);
	plfree(pl_toary(&expand.cclist), free);
	wb_destroy(&expand.valuebuf);
	sb_destroy(&expand.ccbuf);

	expand.valuelist.contents = NULL;
	return expand.valuelist;
    }
    assert(expand.valuelist.length == expand.cclist.length);
    assert(expand.valuebuf.length == expand.ccbuf.length);
    pl_add(&expand.valuelist, wb_towcs(&expand.valuebuf));
    pl_add(&expand.cclist, sb_tostr(&expand.ccbuf));

    /* empty field removal */
    if (expand.valuelist.length == 1) {
	const wchar_t *field = expand.valuelist.contents[0];
	const char *cc = expand.cclist.contents[0];
	if (expand.zeroword && wcscmp(field, L"\"\"") == 0 &&
		(cc[0] & cc[1] & CC_QUOTATION)) {
	    pl_clear(&expand.valuelist, free);
	    pl_clear(&expand.cclist, free);
	}
    }

    /* quote removal */
    for (size_t i = 0; i < expand.valuelist.length; i++)
	expand.valuelist.contents[i] = quote_removal_free(
	    expand.valuelist.contents[i], expand.cclist.contents[i], escaping);

    pl_destroy(&expand.cclist);

    return expand.valuelist;
}

/* Expands a single word: the four expansions and quote removal.
 * This function doesn't perform brace expansion, field splitting, globbing and
 * unescaping.
 * If `processquotes' is true, single- and double-quotations are recognized as
 * quotes. Otherwise, they are treated like backslashed characters.
 * If `escapeall' is true, the expanded words are all backslashed as if the
 * entire expansion is quoted.
 * If `processquotes' and `escapeall' are false, only backslashes not preceding
 * any of $, `, \ are self-backslashed.
 * If successful, the resulting word is returned as a newly malloced string
 * that may include backslash escapes.
 * On error, an error message is printed and NULL is returned.
 * On error in a non-interactive shell, the shell exits. */
wchar_t *expand_single(const wordunit_T *arg,
	tildetype_T tilde, bool processquotes, bool escapeall)
{
    plist_T list =
	expand_four_and_remove_quotes(arg, tilde, processquotes, escapeall);
    if (list.contents == NULL) {
	maybe_exit_on_error();
	return NULL;
    }

    return concatenate_values(pl_toary(&list), true);
}

/* Expands a single word: the four expansions and quote removal.
 * This function doesn't perform brace expansion, field splitting, or globbing.
 * If successful, the resulting word is returned as a newly malloced string.
 * On error, an error message is printed and NULL is returned.
 * On error in a non-interactive shell, the shell exits. */
wchar_t *expand_111111(const wordunit_T *w,
	tildetype_T tilde, quoting_T quoting, escaping_T escaping)
{
    plist_T list = expand_word(w, tilde, quoting, escaping);
    if (list.contents == NULL) {
	maybe_exit_on_error();
	return NULL;
    }

    return concatenate_values(pl_toary(&list), escaping != ES_NONE);
}

/* Like `expand_single', but the result is unescaped (if successful). */
wchar_t *expand_single_and_unescape(const wordunit_T *arg,
	tildetype_T tilde, bool processquotes, bool escapeall)
{
    wchar_t *result = expand_single(arg, tilde, processquotes, escapeall);
    return result == NULL ? NULL : unescapefree(result);
}

/* Expands a single word: the four expansions, glob, quote removal and unescape.
 * This function doesn't perform brace expansion and field splitting.
 * If the result of glob is more than one word,
 *   - returns the pre-glob pattern string if in the POSIXly correct mode
 *   - treats as an error otherwise.
 * If the "glob" shell option is off, glob is not performed.
 * The "nullglob" shell option is ignored.
 * If successful, the resulting word is returned as a newly malloced string.
 * On error, an error message is printed and NULL is returned.
 * On error in a non-interactive shell, the shell exits. */
char *expand_single_with_glob(const wordunit_T *arg, tildetype_T tilde)
{
    wchar_t *exp = expand_single(arg, tilde, true, false);
    char *result;

    if (exp == NULL)
	return NULL;

    /* glob */
    if (shopt_glob && is_pathname_matching_pattern(exp)) {
	plist_T list;
	bool ok;

	pl_init(&list);
	set_interruptible_by_sigint(true);
	ok = wglob(exp, get_wglobflags(), &list);
	set_interruptible_by_sigint(false);
	if (!ok) {
	    free(exp);
	    plfree(pl_toary(&list), free);
	    xerror(EINTR, Ngt("redirection"));
	    result = NULL;
	} else if (list.length == 1) {
	    free(exp);
	    result = realloc_wcstombs(list.contents[0]);
	    if (result == NULL)
		xerror(EILSEQ, Ngt("redirection"));
	    pl_destroy(&list);
	} else {
	    plfree(pl_toary(&list), free);
	    if (posixly_correct) {
		goto noglob;
	    } else {
		exp = unescapefree(exp);
		xerror(0, Ngt("filename `%ls' matches more than one file"),
			exp);
		free(exp);
		result = NULL;
	    }
	}
    } else {
noglob:
	result = realloc_wcstombs(unescapefree(exp));
	if (result == NULL)
	    xerror(EILSEQ, Ngt("redirection"));
    }
    return result;
}


/********** Four Expansions **********/

// TODO Replace this function with expand_word
/* Performs the four expansions in the specified single word.
 * `w' is the word in which expansions occur.
 * `tilde' is type of tilde expansion that is performed.
 * If `processquotes' is true, single- and double-quotations are recognized as
 * quotes. Otherwise, they are treated like backslashed characters.
 * If `escapeall' is true, the expanded words are all backslashed as if the
 * entire expansion is quoted.
 * If `processquotes' and `escapeall' are false, only backslashes not preceding
 * any of $, `, \ are self-backslashed.
 * If successful, the return value is a plist_T containing newly malloced wide
 * strings. In most cases, the plist_T contains one string. If the word contains
 * "$@", however, it may contain any number of strings.
 * Single- or double-quoted characters are unquoted and backslashed.
 * On error, the return value is a plist_T with `contents' being NULL. */
plist_T expand_four_and_remove_quotes(
	const wordunit_T *restrict w,
	tildetype_T tilde, bool processquotes, bool escapeall)
{
    struct expand_four_T expand;

    pl_init(&expand.valuelist);
    pl_init(&expand.cclist);
    wb_init(&expand.valuebuf);
    sb_init(&expand.ccbuf);
    expand.zeroword = false;

    if (!expand_four(w, tilde, processquotes, escapeall, CC_LITERAL, &expand)) {
	plfree(pl_toary(&expand.valuelist), free);
	plfree(pl_toary(&expand.cclist), free);
	wb_destroy(&expand.valuebuf);
	sb_destroy(&expand.ccbuf);

	expand.valuelist.contents = NULL;
	return expand.valuelist;
    }
    assert(expand.valuelist.length == expand.cclist.length);
    assert(expand.valuebuf.length == expand.ccbuf.length);
    pl_add(&expand.valuelist, wb_towcs(&expand.valuebuf));
    pl_add(&expand.cclist, sb_tostr(&expand.ccbuf));

    /* empty field removal */
    if (expand.valuelist.length == 1) {
	const wchar_t *field = expand.valuelist.contents[0];
	const char *cc = expand.cclist.contents[0];
	if (expand.zeroword && wcscmp(field, L"\"\"") == 0 &&
		(cc[0] & cc[1] & CC_QUOTATION)) {
	    pl_clear(&expand.valuelist, free);
	    pl_clear(&expand.cclist, free);
	}
    }

    plfree(pl_toary(&expand.cclist), free);

    /* quote removal */
    for (size_t i = 0; i < expand.valuelist.length; i++)
	expand.valuelist.contents[i] =
	    escaped_remove_free(expand.valuelist.contents[i], L"\"\'");

    return expand.valuelist;
}

/* Performs the four expansions in the specified single word.
 * The four expansions are tilde expansion, parameter expansion, command
 * substitution, and arithmetic expansion.
 * `w' is the word in which expansions occur.
 * `tilde' specifies the type of tilde expansion that is performed.
 * If `processquotes' is true, single- and double-quotations are recognized as
 * quotes. Otherwise, they are treated like backslashed characters.
 * If `escapeall' is true, the expanded words are all backslashed as if the
 * entire expansion is quoted.
 * If `processquotes' and `escapeall' are false, only backslashes not preceding
 * any of $, `, \ are self-backslashed.
 * `rec' must be true iff this expansion is part of another expansion.
 * `e->valuebuf' must be initialized before calling this function and is used to
 * expand the current word. If `w' expands to multiple words, the last word is
 * put in `e->valuebuf' and the others are inserted to `e->valuelist'.
 * Single- and double-quotations remain in the resulting word. In addition,
 * characters inside those quotations are backslashed.
 * The return value is true iff successful. */
bool expand_four(const wordunit_T *restrict w,
	tildetype_T tilde, bool processquotes, bool escapeall,
	charcategory_T defaultcc, struct expand_four_T *restrict e)
{
    bool ok = true;
    bool indq = false;  /* in a double quote? */
    bool first = true;  /* is the first word unit? */
    const wchar_t *ss;
    wchar_t *s;

    for (; w != NULL; w = w->next, first = false) {
	switch (w->wu_type) {
	case WT_STRING:
	    ss = w->wu_string;
	    if (first && tilde != TT_NONE) {
		s = expand_tilde(&ss, w->next, tilde);
		if (s != NULL)
		    append_value(s, NULL, CC_HARD_EXPANSION, e);
	    }
	    while (*ss != L'\0') {
		switch (*ss) {
		case L'"':
		    if (!processquotes)
			goto escape;
		    indq = !indq;
		    wb_wccat(&e->valuebuf, L'"');
		    sb_ccat(&e->ccbuf, defaultcc | CC_QUOTATION);
		    break;
		case L'\'':
		    if (!processquotes || indq)
			goto escape;

		    wb_wccat(&e->valuebuf, L'\'');
		    sb_ccat(&e->ccbuf, defaultcc | CC_QUOTATION);

		    add_sq(&ss, &e->valuebuf, true);
		    fill_ccbuf(e, defaultcc | CC_QUOTED);

		    wb_wccat(&e->valuebuf, L'\'');
		    sb_ccat(&e->ccbuf, defaultcc | CC_QUOTATION);
		    break;
		case L'\\':
		    if (!processquotes) {
			if (!escapeall) {
			    wchar_t c = ss[1];
			    if (c == L'$' || c == L'`' || c == L'\\')
				ss++;
			}
			goto escape;
		    }

		    if (indq && wcschr(CHARS_ESCAPABLE, ss[1]) == NULL) {
			goto escape;
		    } else {
			wb_wccat(&e->valuebuf, L'\\');
			sb_ccat(&e->ccbuf,
				defaultcc | CC_QUOTATION | (indq * CC_QUOTED));
			if (*++ss != L'\0') {
			    wb_wccat(&e->valuebuf, *ss++);
			    sb_ccat(&e->ccbuf, defaultcc | CC_QUOTED);
			}
			continue;
		    }
		case L':':
		    if (!indq && tilde == TT_MULTI) {
			/* perform tilde expansion after a colon */
			wb_wccat(&e->valuebuf, L':');
			sb_ccat(&e->ccbuf, defaultcc);
			ss++;
			s = expand_tilde(&ss, w->next, tilde);
			if (s != NULL)
			    append_value(s, NULL, CC_HARD_EXPANSION, e);
			continue;
		    }
		    /* falls thru! */
		default:
		    if (indq || escapeall) { // FIXME don't escape here
escape:
			wb_wccat(&e->valuebuf, L'\\');
			sb_ccat(&e->ccbuf, defaultcc | CC_QUOTATION);
		    }
		    wb_wccat(&e->valuebuf, *ss);
		    sb_ccat(&e->ccbuf, defaultcc | (indq * CC_QUOTED));
		    break;
		}
		ss++;
	    }
	    break;
	case WT_PARAM:
	    if (!expand_param(w->wu_param,
			indq || escapeall || (defaultcc & CC_QUOTED), e))
		ok = false;
	    break;
	case WT_CMDSUB:
	    s = exec_command_substitution(&w->wu_cmdsub);
	    goto cat_s;
	case WT_ARITH:
	    s = expand_single_and_unescape(w->wu_arith, TT_NONE, true, false);
	    if (s != NULL)
		s = evaluate_arithmetic(s);
cat_s:
	    if (s != NULL) {
		append_value(s, (indq || escapeall) ? NULL : CHARS_ESCAPED,
			CC_SOFT_EXPANSION |
			(indq * CC_QUOTED) | (defaultcc & CC_QUOTED),
			e);
	    } else {
		ok = false;
	    }
	    break;
	}
    }

    return ok;
}

/* Performs the four expansions in the specified single word.
 * The four expansions are tilde expansion, parameter expansion, command
 * substitution, and arithmetic expansion.
 * The lists and buffers in `e' must have been initialized before calling this
 * function. If the expansion yields a single field, the result is appended to
 * `e->valuebuf'. If more than one field result, all but the last field are
 * appended to `e->valuelist' as newly malloced wide strings and the last field
 * remains in `e->valuebuf'. The corresponding charcategory_T strings are added
 * to `e->cclist' and `e->ccbuf', having the same count and length as
 * `e->valuelist' and `e->valuebuf'.
 * The return value is true iff successful. */
bool expand_4444(const wordunit_T *restrict w, tildetype_T tilde,
	quoting_T quoting, charcategory_T defaultcc,
	struct expand_four_T *restrict e)
{
    bool ok = true;
    bool indq = false;  /* in a double quote? */
    bool first = true;  /* is the first word unit? */
    const wchar_t *ss;
    wchar_t *s;

    for (; w != NULL; w = w->next, first = false) {
	switch (w->wu_type) {
	case WT_STRING:
	    ss = w->wu_string;
	    if (first && tilde != TT_NONE) {
		s = expand_tilde(&ss, w->next, tilde);
		if (s != NULL) {
		    wb_catfree(&e->valuebuf, s);
		    fill_ccbuf(e, CC_HARD_EXPANSION | (defaultcc & CC_QUOTED));
		}
	    }
	    while (*ss != L'\0') {
		switch (*ss) {
		case L'"':
		    if (quoting != Q_WORD)
			goto default_;
		    indq = !indq;
		    wb_wccat(&e->valuebuf, L'"');
		    sb_ccat(&e->ccbuf, defaultcc | CC_QUOTATION);
		    break;
		case L'\'':
		    if (quoting != Q_WORD || indq)
			goto default_;

		    wb_wccat(&e->valuebuf, L'\'');
		    sb_ccat(&e->ccbuf, defaultcc | CC_QUOTATION);

		    add_sq(&ss, &e->valuebuf, false);
		    fill_ccbuf(e, defaultcc | CC_QUOTED);

		    wb_wccat(&e->valuebuf, L'\'');
		    sb_ccat(&e->ccbuf, defaultcc | CC_QUOTATION);
		    break;
		case L'\\':
		    switch (quoting) {
			case Q_WORD:
			    if (indq && wcschr(CHARS_ESCAPABLE, ss[1]) == NULL)
				goto default_;
			    break;
			case Q_INDQ:
			    if (wcschr(L"$`\\", ss[1]) == NULL)
				goto default_;
			    break;
			case Q_LITERAL:
			    goto default_;
		    }

		    wb_wccat(&e->valuebuf, L'\\');
		    sb_ccat(&e->ccbuf, defaultcc | CC_QUOTATION);
		    ss++;
		    if (*ss != L'\0') {
			wb_wccat(&e->valuebuf, *ss);
			sb_ccat(&e->ccbuf, defaultcc | CC_QUOTED);
		    }
		    break;
		case L':':
		    if (indq || tilde != TT_MULTI)
			goto default_;

		    /* perform tilde expansion after a colon */
		    wb_wccat(&e->valuebuf, L':');
		    sb_ccat(&e->ccbuf, defaultcc);
		    ss++;
		    s = expand_tilde(&ss, w->next, tilde);
		    if (s != NULL) {
			wb_catfree(&e->valuebuf, s);
			fill_ccbuf(e, CC_HARD_EXPANSION);
		    }
		    continue;
default_:
		default:
		    wb_wccat(&e->valuebuf, *ss);
		    sb_ccat(&e->ccbuf, defaultcc | (indq * CC_QUOTED));
		    break;
		}
		ss++;
	    }
	    break;
	case WT_PARAM:
	    if (!expand_param(w->wu_param,
			indq || quoting == Q_LITERAL || (defaultcc & CC_QUOTED),
			e))
		ok = false;
	    break;
	case WT_CMDSUB:
	    s = exec_command_substitution(&w->wu_cmdsub);
	    goto cat_s;
	case WT_ARITH:
	    s = expand_single_and_unescape(w->wu_arith, TT_NONE, true, false);
	    if (s != NULL)
		s = evaluate_arithmetic(s);
cat_s:
	    if (s != NULL) {
		wb_catfree(&e->valuebuf, s);
		fill_ccbuf(e, CC_SOFT_EXPANSION |
			(indq * CC_QUOTED) | (defaultcc & CC_QUOTED));
	    } else {
		ok = false;
	    }
	    break;
	}
    }

    return ok;
}

/* Appends to `e->ccbuf' as many `c's as needed to match the length with
 * `e->valuebuf'. */
void fill_ccbuf(struct expand_four_T *e, charcategory_T c)
{
    sb_ccat_repeat(&e->ccbuf, c, e->valuebuf.length - e->ccbuf.length);
}

/* Appends `s' to the buffer of `e'.
 * Characters that are contained in `t' are backslash-escaped. If `t' is NULL,
 * all characters are escaped.
 * `s' is freed in this function. */
void append_value(wchar_t *restrict const s, const wchar_t *t,
	charcategory_T defaultcc, struct expand_four_T *e)
{
    for (size_t i = 0; s[i] != L'\0'; i++) {
	if (t == NULL || wcschr(t, s[i]) != NULL) {
	    wb_wccat(&e->valuebuf, L'\\');
	    sb_ccat(&e->ccbuf, defaultcc | CC_QUOTATION);
	}
	wb_wccat(&e->valuebuf, s[i]);
	sb_ccat(&e->ccbuf, defaultcc);
    }
    free(s);
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
 * The result is put in `e'.
 * Returns true iff successful. */
bool expand_param(const paramexp_T *restrict p, bool indq,
	struct expand_four_T *restrict e)
{
    /* parse indices first */
    ssize_t startindex, endindex;
    enum indextype_T indextype;
    if (p->pe_start == NULL) {
	startindex = 0, endindex = SSIZE_MAX, indextype = IDX_NONE;
    } else {
	wchar_t *start = expand_single_and_unescape(
		p->pe_start, TT_NONE, true, false);
	if (start == NULL)
	    return false;
	indextype = parse_indextype(start);
	if (indextype != IDX_NONE) {
	    startindex = 0, endindex = SSIZE_MAX;
	    free(start);
	    if (p->pe_end != NULL) {
		xerror(0, Ngt("the parameter index is invalid"));
		return false;
	    }
	} else if (!evaluate_index(start, &startindex)) {
	    return false;
	} else {
	    if (p->pe_end == NULL) {
		endindex = (startindex == -1) ? SSIZE_MAX : startindex;
	    } else {
		wchar_t *end = expand_single_and_unescape(
			p->pe_end, TT_NONE, true, false);
		if (end == NULL || !evaluate_index(end, &endindex))
		    return false;
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
	plist_T plist = expand_word(p->pe_nest, TT_NONE, Q_WORD, ES_NONE);
	if (plist.contents == NULL)
	    return false;
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
	    return expand_4444(p->pe_subst, TT_SINGLE, Q_WORD,
		    CC_SOFT_EXPANSION | (indq * CC_QUOTED), e);
	}
	break;
    case PT_ASSIGN:
	if (unset) {
	    plfree(values, free);
	    if (p->pe_type & PT_NEST) {
		xerror(0,
		    Ngt("a nested parameter expansion cannot be assigned"));
		return false;
	    } else if (!is_name(p->pe_name)) {
		xerror(0, Ngt("cannot assign to parameter `%ls' "
			    "in parameter expansion"),
			p->pe_name);
		return false;
	    } else if ((v.type == GV_ARRAY_CONCAT)
		    || (v.type == GV_ARRAY && startindex + 1 != endindex)) {
                xerror(0, Ngt("the specified index does not support assignment "
			    "in the parameter expansion of array `%ls'"),
			p->pe_name);
		return false;
	    }
	    subst = expand_single_and_unescape(
		    p->pe_subst, TT_SINGLE, true, false);
	    if (subst == NULL)
		return false;
	    if (v.type != GV_ARRAY) {
		assert(v.type == GV_NOTFOUND || v.type == GV_SCALAR);
		if (!set_variable(
			    p->pe_name, xwcsdup(subst), SCOPE_GLOBAL, false)) {
		    free(subst);
		    return false;
		}
	    } else {
		assert(0 <= startindex && (size_t) startindex <= v.count);
		if (!set_array_element(p->pe_name, startindex, xwcsdup(subst))){
		    free(subst);
		    return false;
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
	    plfree(values, free);
	    print_subst_as_error(p);
	    return false;
	}
	break;
    }

    if (unset && !shopt_unset) {
	plfree(values, free);
	xerror(0, Ngt("parameter `%ls' is not set"), p->pe_name);
	return false;
    }

    /* PT_MATCH, PT_SUBST */
    wchar_t *match;
    switch (p->pe_type & PT_MASK) {
    case PT_MATCH:
	match = expand_single(p->pe_match, TT_SINGLE, true, false);
	if (match == NULL) {
	    plfree(values, free);
	    return false;
	}
	match_each(values, match, p->pe_type);
	free(match);
	break;
    case PT_SUBST:
	match = expand_single(p->pe_match, TT_SINGLE, true, false);
	subst = expand_single_and_unescape(p->pe_subst, TT_SINGLE, true, false);
	if (match == NULL || subst == NULL) {
	    free(match);
	    free(subst);
	    plfree(values, free);
	    return false;
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

    /* add the elements of `values' to `e->valuelist' */
    if (values[0] == NULL) {
	if (indq)
	    e->zeroword = true;
    } else {
	charcategory_T cc = CC_SOFT_EXPANSION | (indq * CC_QUOTED);

	/* add the first element */
	append_value(values[0], indq ? NULL : CHARS_ESCAPED, cc, e);

	/* add the other elements */
	for (size_t i = 1; values[i] != NULL; i++) {
	    pl_add(&e->valuelist, wb_towcs(&e->valuebuf));
	    pl_add(&e->cclist, sb_tostr(&e->ccbuf));

	    wb_init(&e->valuebuf);
	    sb_init(&e->ccbuf);

	    append_value(values[i], indq ? NULL : CHARS_ESCAPED, cc, e);
	}
    }
    free(values);

    return true;
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
void print_subst_as_error(const paramexp_T *p)
{
    if (p->pe_subst != NULL) {
	wchar_t *subst = expand_single_and_unescape(
		p->pe_subst, TT_SINGLE, true, false);
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

    size_t ci = 0;

start:

    /* find '{' */
    do {
	wchar_t *c = wcschr(&word[ci], L'{');
	if (c == NULL) {
	    /* no L'{', no expansion */
	    pl_add(valuelist, word);
	    pl_add(cclist, cc);
	    return;
	}
	ci = idx(c);
    } while (cc[ci++] != CC_LITERAL);

    if (try_expand_brace_sequence(word, cc, &word[ci], valuelist, cclist)) {
	return;
    }

    plist_T splitpoints;
    unsigned nest;

    /* collect pointers to characters where the word is split */
    /* The pointers point to the character just after L'{', L',' or L'}'. */
    pl_init(&splitpoints);
    pl_add(&splitpoints, &word[ci]);
    nest = 0;
    for (; word[ci] != L'\0'; ci++) {
	if (cc[ci] != CC_LITERAL)
	    continue;
	switch (word[ci]) {
	    case L'{':
		nest++;
		break;
	    case L',':
		if (nest == 0)
		    pl_add(&splitpoints, &word[ci + 1]);
		break;
	    case L'}':
		if (nest > 0) {
		    nest--;
		    break;
		} else if (splitpoints.length == 1) {
		    /* no comma between { and } */
		    goto restart;
		} else {
		    pl_add(&splitpoints, &word[ci + 1]);
		    goto done;
		}
	}
    }
restart:
    /* if there is no L',' or L'}' corresponding to L'{',
     * find the next L'{' and try again */
    ci = idx(splitpoints.contents[0]);
    pl_destroy(&splitpoints);
    goto start;

done:;
    size_t lastelemindex = splitpoints.length - 1;
    size_t headlen = idx(splitpoints.contents[0]) - 1;
    size_t taillen = wcslen(splitpoints.contents[lastelemindex]);
    for (size_t i = 0; i < lastelemindex; i++) {
	xwcsbuf_T buf;
	xstrbuf_T cbuf;
	wb_init(&buf);
	sb_init(&cbuf);

	wb_ncat_force(&buf, word, headlen);
	sb_ncat_force(&cbuf, cc, headlen);

	size_t len = (wchar_t *) splitpoints.contents[i + 1] -
	             (wchar_t *) splitpoints.contents[i    ] - 1;
	ci = idx(splitpoints.contents[i]);
	wb_ncat_force(&buf, &word[ci], len);
	sb_ncat_force(&cbuf, &cc[ci], len);

	ci = idx(splitpoints.contents[lastelemindex]);
	wb_ncat_force(&buf, &word[ci], taillen);
	sb_ncat_force(&cbuf, &cc[ci], taillen);
	assert(buf.length == cbuf.length);

	/* expand the remaining portion recursively */
	expand_brace(wb_towcs(&buf), sb_tostr(&cbuf), valuelist, cclist);
    }
    pl_destroy(&splitpoints);
    free(word);
    free(cc);
}

/* Tries numeric brace expansion like "{01..05}".
 * If unsuccessful, this function returns false without any side effects.
 * If successful, `word' and `cc' are freed and the full expansion results are
 * added to `valuelist' and `cclist'.
 * `startc' is a pointer to the character right after L'{' in `word'.
 */
bool try_expand_brace_sequence(
	wchar_t *const word, char *restrict const cc, wchar_t *const startc,
	plist_T *restrict valuelist, plist_T *restrict cclist)
{
    long start, end, delta, value;
    wchar_t *dotp, *dotbracep, *bracep, *c;
    int startlen, endlen, len, wordlen;
    bool sign = false;

    assert(startc[-1] == L'{');
    c = startc;

    /* parse the starting point */
    dotp = wcschr(c, L'.');
    if (dotp == NULL || c == dotp || dotp[1] != L'.')
	return false;
    startlen = has_leading_zero(c, &sign) ? (dotp - c) : 0;
    errno = 0;
    start = wcstol(c, &c, 10);
    if (errno != 0 || c != dotp)
	return false;

    c = dotp + 2;

    /* parse the ending point */
    dotbracep = wcspbrk(c, L".}");
    if (dotbracep == NULL || c == dotbracep ||
	    (dotbracep[0] == L'.' && dotbracep[1] != L'.'))
	return false;
    endlen = has_leading_zero(c, &sign) ? (dotbracep - c) : 0;
    errno = 0;
    end = wcstol(c, &c, 10);
    if (errno != 0 || c != dotbracep)
	return false;

    /* parse the delta */
    if (dotbracep[0] == L'.') {
	assert(dotbracep[1] == L'.');
	c = dotbracep + 2;
	bracep = wcschr(c, L'}');
	if (bracep == NULL || c == bracep)
	    return false;
	errno = 0;
	delta = wcstol(c, &c, 10);
	if (delta == 0 || errno != 0 || c != bracep)
	    return false;
    } else {
	assert(dotbracep[0] == L'}');
	bracep = dotbracep;
	if (start <= end)
	    delta = 1;
	else
	    delta = -1;
    }

    /* validate charcategory_T */
    if (cc[idx(bracep)] != CC_LITERAL)
	return false;
    for (size_t ci = idx(startc); ci < idx(bracep); ci++)
	if (cc[ci] & CC_QUOTED)
	    return false;

    /* expand the sequence */
    value = start;
    len = (startlen > endlen) ? startlen : endlen;
    wordlen = idx(bracep + 1) + wcslen(bracep + 1); // = wcslen(word);
    do {
	xwcsbuf_T buf;
	xstrbuf_T cbuf;
	wb_init(&buf);
	sb_init(&cbuf);

	size_t slen = idx(startc - 1);
	wb_ncat_force(&buf, word, slen);
	sb_ncat_force(&cbuf, cc, slen);

	int plen = wb_wprintf(&buf, sign ? L"%0+*ld" : L"%0*ld", len, value);
	if (plen >= 0)
	    sb_ccat_repeat(&cbuf, CC_HARD_EXPANSION, plen);

	slen = idx(bracep + 1);
	wb_ncat_force(&buf, bracep + 1, wordlen - slen);
	sb_ncat_force(&cbuf, cc + slen, wordlen - slen);
	assert(buf.length == cbuf.length);

	/* expand the remaining portion recursively */
	expand_brace(wb_towcs(&buf), sb_tostr(&cbuf), valuelist, cclist);

	if (delta >= 0) {
	    if (LONG_MAX - delta < value)
		break;
	} else {
	    if (LONG_MIN - delta > value)
		break;
	}
	value += delta;
    } while (delta >= 0 ? value <= end : value >= end);
    free(word);
    free(cc);
    return true;
#undef idx
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
void fieldsplit_all(
	void **restrict const valuelist, void **restrict const cclist,
	plist_T *restrict outvaluelist, plist_T *restrict outcclist)
{
    const wchar_t *ifs = getvar(L VAR_IFS);
    if (ifs == NULL)
	ifs = DEFAULT_IFS;

    for (size_t i = 0; valuelist[i] != NULL; i++)
	fieldsplit(valuelist[i], cclist[i], ifs, outvaluelist, outcclist);
    free(valuelist);
    free(cclist);
}

/* Performs field splitting.
 * `s' is the word to split and freed in this function.
 * `cc' is the charcategory_T string corresponding to `s' and also freed
 * `ifs' must not be NULL.
 * The results are added to `outvaluelist' and `outcclist' as newly-malloced
 * strings. */
void fieldsplit(wchar_t *restrict s, char *restrict cc,
	const wchar_t *restrict ifs,
	plist_T *restrict outvaluelist, plist_T *restrict outcclist)
{
    plist_T fields;

    pl_init(&fields);
    extract_fields(s, cc, true, ifs, &fields);
    assert(fields.length % 2 == 0);

    for (size_t i = 0; i < fields.length; i += 2) {
	const wchar_t *start = fields.contents[i], *end = fields.contents[i+1];
	size_t idx = start - s, len = end - start;
	pl_add(outvaluelist, xwcsndup(start, len));
	pl_add(outcclist, memcpy(xmalloc(len), &cc[idx], len));
    }

    pl_destroy(&fields);
    free(s);
    free(cc);
}

/* Extracts fields from a string.
 * `s' is the word to split.
 * `cc` is an array of charcategory_T values corresponding to `s'. It must be at
 * least as long as `wcslen(s)'.
 * If `escaped' is true, backslashes in `s' are treated as escapes. But
 * backslashes do not prevent splitting.
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
 *   ""                  ->   ""
 *   "  "                ->   ""
 *   " abc 123 "         ->   "abc" "123"
 *   "  abc  123  "      ->   "abc" "123"
 *   "-abc-123-"         ->   "" "abc" "123" ""
 *   " - abc - 123 - "   ->   "" "abc" "123" ""
 *   "abc--123"          ->   "abc" "" "123"
 *   "abc - - 123"       ->   "abc" "" "123"
 */
wchar_t *extract_fields(const wchar_t *restrict s, const char *restrict cc,
	bool escaped, const wchar_t *restrict ifs, plist_T *restrict dest)
{
    size_t index = 0;
    size_t ifswhitestartindex;
    size_t oldlen = dest->length;

    /* true when the currently skipping IFS whitespaces immediately follow a
     * previously split field. */
    bool afterfield = false;

    for (;;) {
	ifswhitestartindex = index;
	index += skip_ifs_whitespaces(&s[index], &cc[index], escaped, ifs);

	/* extract next field, if any */
	size_t fieldstartindex = index;
	index += skip_field(&s[index], &cc[index], escaped, ifs);
	if (index != fieldstartindex) {
	    pl_add(pl_add(dest, &s[fieldstartindex]), &s[index]);
	    afterfield = true;
	    continue;
	}

	/* Now the current char is either null or a IFS non-whitespace. */

	if (!afterfield)
	    add_empty_field(dest, &s[index]);

	/* skip (only one) IFS non-whitespace */
	size_t ifsstartindex = index;
	index += skip_ifs(&s[index], &cc[index], escaped, ifs);
	if (index != ifsstartindex) {
	    afterfield = false;
	    continue;
	}

	/* Now the current char is null. We're done. */
	break;
    }

    /* remove the empty last field */
    size_t newlen = dest->length;
    if (!shopt_emptylastfield && newlen - oldlen >= 2 * 2 &&
	    dest->contents[newlen - 2] == dest->contents[newlen - 1])
	pl_remove(dest, newlen - 2, 2);

    assert(dest->length - oldlen >= 2);
    return (wchar_t *) &s[ifswhitestartindex];
}

/* If `*s' is a (possibly escaped if `escaped') IFS character, returns the
 * number of characters to skip it. Otherwise returns zero. */
size_t skip_ifs(const wchar_t *s, const char *cc,
	bool escaped, const wchar_t *ifs)
{
    size_t i = 0;
    if (escaped && s[i] == L'\\')
	i++;
    if (s[i] == L'\0')
	return 0;
    if (cc[i] == CC_SOFT_EXPANSION && wcschr(ifs, s[i]) != NULL)
	return i + 1;
    else
	return 0;
}

/* Returns the length of IFS whitespace sequence starting at `*s'. */
size_t skip_ifs_whitespaces(const wchar_t *s, const char *cc,
	bool escaped, const wchar_t *ifs)
{
    size_t total = 0;
    for (;;) {
	size_t current = skip_ifs(&s[total], &cc[total], escaped, ifs);
	if (current == 0 || !iswspace(s[total + current - 1]))
	    return total;
	total += current;
    }
}

/* Returns the length of a field starting at `*s'. */
size_t skip_field(const wchar_t *s, const char *cc,
	bool escaped, const wchar_t *ifs)
{
    size_t index = 0;
    for (;;) {
	size_t saveindex = index;
	if (escaped && s[index] == L'\\')
	    index++;
	if (s[index] == L'\0')
	    return saveindex;
	if (cc[index] == CC_SOFT_EXPANSION && wcschr(ifs, s[index]) != NULL)
	    return saveindex;
	index++;
    }
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
 * If `escape' is true, all the characters added are backslashed. */
void add_sq(const wchar_t *restrict *ss, xwcsbuf_T *restrict buf, bool escape)
{
    assert(**ss == L'\'');
    for (;;) {
	(*ss)++;
	switch (**ss) {
	    case L'\0':
		assert(false);
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
    wb_init(&buf);
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
    if (t != NULL && wcspbrk(s, t) == NULL) {
	return s;
    } else {
	wchar_t *result = escape(s, t);
	free(s);
	return result;
    }
}

/* Removes backslash escapes. The result is a newly malloced string.
 * If there is an unescaped backslash before the null character, the backslash
 * is ignored. */
wchar_t *unescape(const wchar_t *s)
{
    xwcsbuf_T buf;
    wb_init(&buf);
    for (size_t i = 0; s[i] != L'\0'; i++) {
	if (s[i] == L'\\') {
	    if (s[i + 1] == L'\0')
		break;
	    else
		i++;
	}
	wb_wccat(&buf, s[i]);
    }
    return wb_towcs(&buf);
}

/* Same as `unescape', except that the first argument is freed. */
wchar_t *unescapefree(wchar_t *s)
{
    if (wcschr(s, L'\\') == NULL) {
	return s;
    } else {
	wchar_t *result = unescape(s);
	free(s);
	return result;
    }
}

/* Quotes the specified string using backslashes and single-quotes. The result
 * is suitable for re-parsing as a shell command word that would expand to the
 * original string. The result is a newly malloced string. */
wchar_t *quote_as_word(const wchar_t *s)
{
    xwcsbuf_T buf;
    wb_init(&buf);
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

/* Removes quotes (', ", \). The result is a newly malloced string. */
wchar_t *unquote(const wchar_t *s)
{
    bool indq = false;
    xwcsbuf_T buf;
    wb_init(&buf);
    for (;;) {
	switch (*s) {
	case L'\0':
	    return wb_towcs(&buf);
	case L'\'':
	    if (indq)
		goto default_case;
	    add_sq(&s, &buf, false);
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

/* Like `wcspbrk', but ignores backslashed characters in `s'. */
wchar_t *escaped_wcspbrk(const wchar_t *s, const wchar_t *accept)
{
    for (; *s != L'\0'; s++) {
	if (*s == L'\\') {
	    s++;
	    if (*s == L'\0')
		break;
	    continue;
	}
	if (wcschr(accept, *s) != NULL)
	    return (wchar_t *) s;
    }
    return NULL;
}

/* Removes characters in `reject' from `s'.
 * Backslash escapes in `s' are recognized. Escapes and escaped characters are
 * kept in the result.
 * The result is a newly malloced string. */
wchar_t *escaped_remove(const wchar_t *s, const wchar_t *reject)
{
    xwcsbuf_T result;
    wb_init(&result);
    for (;;) {
	const wchar_t *rejectchar = escaped_wcspbrk(s, reject);
	if (rejectchar == NULL)
	    break;
	wb_ncat_force(&result, s, rejectchar - s);
	s = rejectchar + 1;
    }
    wb_cat(&result, s);
    return wb_towcs(&result);
}

/* Like `escaped_remove', but frees `s' before returning the result. */
wchar_t *escaped_remove_free(wchar_t *s, const wchar_t *reject)
{
    wchar_t *result = escaped_remove(s, reject);
    free(s);
    return result;
}

/* Tests if a character should be backslash-escaped. */
bool should_escape(char c, charcategory_T cc, escaping_T escaping)
{
    switch (escaping) {
	case ES_NONE:
	    return false;
	case ES_QUOTED_HARD:
	    if ((cc & CC_ORIGIN_MASK) == CC_HARD_EXPANSION)
		return true;
	    /* falls thru! */
	case ES_QUOTED:
	    return c == L'\\' || (cc & CC_QUOTED);
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
    wb_init(&result);
    for (size_t i = 0; s[i] != L'\0'; i++) {
	if (cc[i] & CC_QUOTATION)
	    continue;
	if (should_escape(s[i], cc[i], escaping))
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


/********** File Name Expansion (Glob) **********/

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

/* Performs file name expansion to the specified patterns.
 * `patterns' is a NULL-terminated array of pointers to `free'able wide strings
 * cast to (void *). `patterns' is `plfree'd in this function.
 * The results are added to `list' as newly-malloced wide strings. */
void glob_all(void **restrict patterns, plist_T *restrict list)
{
    enum wglobflags_T flags = get_wglobflags();
    bool unblock = false;

    for (size_t i = 0; patterns[i] != NULL; i++) {
	wchar_t *pat = patterns[i];
	if (is_pathname_matching_pattern(pat)) {
	    if (!unblock) {
		set_interruptible_by_sigint(true);
		unblock = true;
	    }

	    size_t oldlen = list->length;
	    wglob(pat, flags, list);
	    if (!shopt_nullglob && oldlen == list->length)
		goto addpattern;
	    free(pat);
	} else {
	    /* If the pattern doesn't contain characters like L'*' and L'?',
	     * we don't need to glob. */
addpattern:
	    pl_add(list, unescapefree(pat));
	}
    }
    if (unblock)
	set_interruptible_by_sigint(false);
    free(patterns);
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
    result = expand_single_and_unescape(word, TT_NONE, false, !esc);
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
