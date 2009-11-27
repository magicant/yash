/* Yash: yet another shell */
/* xfnmatch.c: regex matching wrapper as a replacement for fnmatch */
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


#include "common.h"
#include <assert.h>
#include <regex.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "strbuf.h"
#include "util.h"
#include "xfnmatch.h"


struct xfnmatch_T {
    xfnmflags_T flags;
    regex_t regex;
};
/* The flags are logical OR of the followings:
 *  XFNM_SHORTEST:  do shortest match
 *  XFNM_HEADONLY:  only match at the beginning of the string
 *  XFNM_TAILONLY:  only match at the end of the string
 *  XFNM_PERIOD:    don't match with a string that starts with a period
 *  XFNM_CASEFOLD:  ignore case while matching
 * When XFNM_SHORTEST is specified, either (but not both) of XFNM_HEADONLY and
 * XFNM_TAILONLY must be also specified. When XFNM_PERIOD is specified,
 * XFNM_HEADONLY must be also specified. */

#define XFNM_HEADTAIL (XFNM_HEADONLY | XFNM_TAILONLY)
#define MISMATCH ((xfnmresult_T) { (size_t) -1, (size_t) -1, })

static wchar_t *skip_bracket(const wchar_t *pat)
    __attribute__((nonnull));
static void encode_pattern(const wchar_t *restrict pat, xstrbuf_T *restrict buf)
    __attribute__((nonnull));
static const wchar_t *encode_pattern_bracket(const wchar_t *restrict pat,
	xstrbuf_T *restrict buf, mbstate_t *restrict state)
    __attribute__((nonnull));
static const wchar_t *encode_pattern_bracket2(const wchar_t *restrict pat,
	xstrbuf_T *restrict buf, mbstate_t *restrict state)
    __attribute__((nonnull));
static void append_as_collating_symbol(wchar_t c,
	xstrbuf_T *restrict buf, mbstate_t *restrict state)
    __attribute__((nonnull));
static xfnmresult_T wmatch_headtail(
	const regex_t *restrict regex, const wchar_t *restrict s)
    __attribute__((nonnull));
static xfnmresult_T wmatch_shortest_head(
	const regex_t *restrict regex, const wchar_t *restrict s)
    __attribute__((nonnull));
static xfnmresult_T wmatch_shortest_tail(
	const regex_t *restrict regex, const wchar_t *restrict s)
    __attribute__((nonnull));
static xfnmresult_T wmatch_longest(
	const regex_t *restrict regex, const wchar_t *restrict s)
    __attribute__((nonnull));


/* Checks if there is L'*' or L'?' or a bracket expression in the pattern.
 * If the result is false, the pattern is not a filename expansion pattern. */
/* This function treats L'/' as an ordinary character. */
bool is_matching_pattern(const wchar_t *pat)
{
    for (;;) {
	switch (*pat) {
	    case L'\0':
		return false;
	    case L'\\':
		pat++;
		if (!*pat)
		    return false;
		pat++;
		break;
	    case L'*':  case L'?':
		return true;
	    case L'[':
		if (skip_bracket(pat) != pat)
		    return true;
		/* falls thru! */
	    default:
		pat++;
		break;
	}
    }
}

/* Skips the bracket expression that starts with L'[' pointed to by `pat' and
 * returns a pointer to the closing L']'. If no corresponding L']' is found,
 * returns `pat'. Backslash escapes are recognized in the bracket expression. */
wchar_t *skip_bracket(const wchar_t *pat)
{
    const wchar_t *savepat = pat;
    assert(*pat == L'[');
    pat++;

    for (;;) {
	switch (*pat) {
	    case L'\0':
		goto fail;
	    case L'[':
		pat++;
		const wchar_t *p;
		switch (*pat) {
		    case L'.':  p = wcsstr(pat + 1, L".]");  break;
		    case L':':  p = wcsstr(pat + 1, L":]");  break;
		    case L'=':  p = wcsstr(pat + 1, L"=]");  break;
		    default:    continue;
		}
		if (!p)
		    goto fail;
		pat = p + 2;
		continue;
	    case L']':
		if (pat > savepat + 1)
		    return (wchar_t *) pat;
		break;
	    case L'\\':
		pat++;
		if (*pat == L'\0')
		    goto fail;
		break;
	}
	pat++;
    }

fail:
    return (wchar_t *) savepat;
}

/* Checks if there is L'*' or L'?' or a bracket expression in the pattern.
 * If the result is false, the pattern is not a filename expansion pattern.
 * If the pattern contains slashes, components separated by the slashes are each
 * checked by the `is_matching_pattern' function. */
bool is_pathname_matching_pattern(const wchar_t *pat)
{
    const wchar_t *p;

    while ((p = wcschr(pat, L'/'))) {
	wchar_t buf[p - pat + 1];
	wmemcpy(buf, pat, p - pat);
	buf[p - pat] = L'\0';
	if (is_matching_pattern(buf))
	    return true;
	pat = p + 1;
    }
    return is_matching_pattern(pat);
}

/* Compiles the specified pattern.
 * The flags are logical OR of the followings:
 *  XFNM_SHORTEST:  do shortest match
 *  XFNM_HEADONLY:  only match at the beginning of string
 *  XFNM_TAILONLY:  only match at the end of string
 *  XFNM_PERIOD:    force explicit match for a period at the beginning
 *  XFNM_CASEFOLD:  ignore case while matching
 * When XFNM_SHORTEST is specified, either (but not both) of XFNM_HEADONLY and
 * XFNM_TAILONLY must be also specified. When XFNM_PERIOD is specified,
 * XFNM_HEADONLY must be also specified.
 * Returns NULL on failure. */
xfnmatch_T *xfnm_compile(const wchar_t *pat, xfnmflags_T flags)
{
    xstrbuf_T buf;

    sb_init(&buf);
    if (flags & XFNM_HEADONLY)
	sb_ccat(&buf, '^');
    encode_pattern(pat, &buf);
    if (flags & XFNM_TAILONLY)
	sb_ccat(&buf, '$');

    if (flags & XFNM_PERIOD)
	if (pat[0] == L'.' || pat[0] == L'\\')
	    flags &= ~XFNM_PERIOD;

    xfnmatch_T *xfnm = xmalloc(sizeof *xfnm);
    xfnm->flags = flags;

    int regexflags = 0;
    if (flags & XFNM_CASEFOLD)
	regexflags |= REG_ICASE;
    if ((flags & XFNM_HEADTAIL) == XFNM_HEADTAIL)
	regexflags |= REG_NOSUB;

    int err = regcomp(&xfnm->regex, buf.contents, regexflags);

    sb_destroy(&buf);
    if (err) {
	free(xfnm);
	xfnm = NULL;
    }
    return xfnm;
}

/* Converts the specified pathname matching pattern into a regex pattern.
 * The result is appended to `buf', which must be in the initial shift state.
 * When this function returns, the buffer is reset to the initial shift state.*/
/* A trailing backslash, escaping the terminating null character, is ignored.
 * This is useful for pathname expansion since, for example, the pattern
 * "f*o\/b?r" is separated into "f*o\" and "b?r", one of which has a trailing
 * backslash that should be ignored. */
void encode_pattern(const wchar_t *restrict pat, xstrbuf_T *restrict buf)
{
    mbstate_t state;

    memset(&state, 0, sizeof state);  /* initial shift state */
    for (;;) {
	switch (*pat) {
	    case L'?':
		sb_wccat(buf, L'.', &state);
		break;
	    case L'*':
		sb_wccat(buf, L'.', &state);
		sb_wccat(buf, L'*', &state);
		break;
	    case L'[':
		pat = encode_pattern_bracket(pat, buf, &state);
		break;
	    case L'\\':
		switch (*++pat) {
		    case L'.':  case L'*':  case L'[':
		    case L'^':  case L'$':  case L'\\':
			goto escape;
		    default:
			goto ordinary;
		}
	    case L'.':  case L'^':  case L'$':  escape:
		sb_wccat(buf, L'\\', &state);
		/* falls thru */
	    default:  ordinary:
		sb_wccat(buf, *pat, &state);
		if (*pat == L'\0')
		    return;
		break;
	}
	pat++;
    }
}

/* Converts the specified bracket pattern of pathname matching pattern into that
 * of regex.
 * Backslash escapes are recognized in the original pattern.
 * Pointer `pat' must point to the opening bracket '['.
 * If the bracket pattern was successfully converted, a pointer to the closing
 * bracket ']' is returned. Otherwise, an escaped bracket character '\[' is
 * appended to `buf' and `pat' is returned. */
const wchar_t *encode_pattern_bracket(const wchar_t *restrict pat,
	xstrbuf_T *restrict buf, mbstate_t *restrict state)
{
    const wchar_t *savepat = pat;
    size_t savelength = buf->length;
    mbstate_t savestate = *state;

    assert(*pat == L'[');
    sb_wccat(buf, L'[', state);
    pat++;
    if (*pat == L'!' || *pat == L'^') {
	sb_wccat(buf, L'^', state);
	pat++;
    }
    if (*pat == L']') {
	sb_wccat(buf, L']', state);
	pat++;
    }
    for (;;) {
	switch (*pat) {
	    case L'\0':
		goto fail;
	    case L'[':
		pat = encode_pattern_bracket2(pat, buf, state);
		if (pat == NULL)
		    goto fail;
		break;
	    case L'\\':
		pat++;
		switch (*pat) {
		    case L'\0':
			goto fail;
		    case L'[':  case L'^':  case L'-':  case L']':
			append_as_collating_symbol(*pat, buf, state);
			break;
		    default:
			sb_wccat(buf, *pat, state);
			break;
		}
		break;
	    default:
		sb_wccat(buf, *pat, state);
		if (*pat == L']')
		    return pat;
		break;
	}
	pat++;
    }

fail:
    sb_truncate(buf, savelength);
    *state = savestate;
    sb_wccat(buf, L'\\', state);
    sb_wccat(buf, L'[', state);
    return savepat;
}

/* Converts the collating symbol, equivalence class, or character class pattern
 * that starts with the opening bracket '[' pointed to by `pat'.
 * If the pattern is successfully converted, a pointer to the corresponding
 * closing bracket ']' is returned. If `*pat' is not followed by any of '.', ':'
 * and '=', then a single bracket character is appended to `buf' and `pat' is
 * returned. If no corresponding closing bracket is found, NULL is returned. */
const wchar_t *encode_pattern_bracket2(const wchar_t *restrict pat,
	xstrbuf_T *restrict buf, mbstate_t *restrict state)
{
    const wchar_t *savepat = pat;
    const wchar_t *p;

    assert(*pat == L'[');
    switch (*(pat + 1)) {
	case L'.':  p = wcsstr(pat + 2, L".]");  break;
	case L':':  p = wcsstr(pat + 2, L":]");  break;
	case L'=':  p = wcsstr(pat + 2, L"=]");  break;
	default:    goto not_a_pattern;
    }
    if (!p)
	return NULL;
    for (;;) {
	if (*pat == L'\\')
	    pat++;
	sb_wccat(buf, *pat, state);
	if (pat > p)
	    break;
	pat++;
    }
    assert(*pat == L']');
    return pat;

not_a_pattern:
    sb_wccat(buf, L'[', state);
    return savepat;
}

void append_as_collating_symbol(wchar_t c,
	xstrbuf_T *restrict buf, mbstate_t *restrict state)
{
    sb_wccat(buf, L'[', state);
    sb_wccat(buf, L'.', state);
    sb_wccat(buf, c,    state);
    sb_wccat(buf, L'.', state);
    sb_wccat(buf, L']', state);
}

/* Performs matching on string `s' using pre-compiled pattern `xfnm'.
 * Returns zero on successful match. On mismatch, an error number that was
 * returned by `regexec' is returned.
 * This function does not support the XFNM_SHORTEST flag. The given pattern must
 * have been compiled without the XFNM_SHORTEST flag. */
int xfnm_match(const xfnmatch_T *restrict xfnm, const char *restrict s)
{
    assert(!(xfnm->flags & XFNM_SHORTEST));

    if (xfnm->flags & XFNM_PERIOD) {
	assert(xfnm->flags & XFNM_HEADONLY);
	if (s[0] == '.')
	    return REG_NOMATCH;
    }

    return regexec(&xfnm->regex, s, 0, NULL, 0);
}

/* Performs matching on string `s' using pre-compiled pattern `xfnm'.
 * On match, returns an xfnmresult_T structure which shows the character offsets
 * of the matched range in `s'. On mismatch, returns { -1, -1 }.
 * If the pattern was compiled with both XFNM_HEADONLY and XFNM_TAILONLY flags
 * specified, only the `start' member of the returned structure is meaningful
 * and the `end' member's value is unspecified. */
xfnmresult_T xfnm_wmatch(
	const xfnmatch_T *restrict xfnm, const wchar_t *restrict s)
{
    xfnmflags_T flags = xfnm->flags;
    if (flags & XFNM_PERIOD) {
	assert(flags & XFNM_HEADONLY);
	if (s[0] == L'.')
	    return MISMATCH;
    }
    if ((flags & XFNM_HEADTAIL) == XFNM_HEADTAIL) {
	return wmatch_headtail(&xfnm->regex, s);
    }
    if (flags & XFNM_SHORTEST) {
	if (flags & XFNM_HEADONLY) {
	    assert(!(flags & XFNM_TAILONLY));
	    return wmatch_shortest_head(&xfnm->regex, s);
	} else {
	    assert(flags & XFNM_TAILONLY);
	    return wmatch_shortest_tail(&xfnm->regex, s);
	}
    } else {
	return wmatch_longest(&xfnm->regex, s);
    }
}

xfnmresult_T wmatch_headtail(
	const regex_t *restrict regex, const wchar_t *restrict s)
{
    char *mbs = malloc_wcstombs(s);
    if (mbs == NULL)
	return MISMATCH;
    int r = regexec(regex, mbs, 0, NULL, 0);
    free(mbs);
    return (r == 0) ? ((xfnmresult_T) { .start = 0 }) : MISMATCH;
}

xfnmresult_T wmatch_shortest_head(
	const regex_t *restrict regex, const wchar_t *restrict s)
{
    xstrbuf_T buf;
    mbstate_t state;
    size_t i;

    sb_init(&buf);
    memset(&state, 0, sizeof state);  /* initial shift state */
    i = 0;
    for (;;) {
	if (regexec(regex, buf.contents, 0, NULL, 0) == 0) {
	    /* successful match */
	    break;
	}
	if (s[i] == L'\0') {
	    /* mismatch */
	    i = (size_t) -1;
	    break;
	}
	if (!sb_wccat(&buf, s[i], &state)) {
	    /* error */
	    i = (size_t) -1;
	    break;
	}
	i++;
    }
    sb_destroy(&buf);
    return (xfnmresult_T) {
	.start = (i == (size_t) -1) ? -1 : 0,
	.end = i,
    };
}

xfnmresult_T wmatch_shortest_tail(
	const regex_t *restrict regex, const wchar_t *restrict s)
{
    size_t i = 0;
    xfnmresult_T result = MISMATCH;

    for (;;) {
	xfnmresult_T newresult = wmatch_longest(regex, s + i);
	if (newresult.start == (size_t) -1)
	    break;
	newresult.start += i;
	newresult.end += i;
	result = newresult;
	if (s[newresult.start] == L'\0')
	    break;
	i = newresult.start + 1;
    }
    return result;
}

xfnmresult_T wmatch_longest(
	const regex_t *restrict regex, const wchar_t *restrict s)
{
    regmatch_t match[1];
    char *mbs = malloc_wcstombs(s);
    int r = regexec(regex, mbs, 1, match, 0);

    if (r != 0) {
	free(mbs);
	return MISMATCH;
    }

    /* Now convert the byte offsets into the character offsets */

    const char *mbs2;
    mbstate_t state;
    xfnmresult_T result;

    memset(&state, 0, sizeof state);  /* initial shift state */
    mbs[match[0].rm_eo] = '\0';
    mbs2 = mbs;
    result.end = mbsrtowcs(NULL, &mbs2, 0, &state);
    memset(&state, 0, sizeof state);
    mbs[match[0].rm_so] = '\0';
    mbs2 = mbs;
    result.start = mbsrtowcs(NULL, &mbs2, 0, &state);

    if (result.start == (size_t) -1 || result.end == (size_t) -1)
	result = MISMATCH;
    free(mbs);
    return result;
}

/* Substitutes part of string `s' that matches pre-compiled pattern `xfnm'
 * with string `repl'. If `substall' is true, all matching substrings in `s' are
 * substituted. Otherwise, only the first match is substituted. The resulting
 * string is returned as a newly-malloced string. */
wchar_t *xfnm_subst(const xfnmatch_T *restrict xfnm, const wchar_t *restrict s,
	const wchar_t *restrict repl, bool substall)
{
    xfnmflags_T flags = xfnm->flags;

    if ((flags & XFNM_HEADTAIL) == XFNM_HEADTAIL) {
	xfnmresult_T result = wmatch_headtail(&xfnm->regex, s);
	return xwcsdup((result.start != (size_t) -1) ? repl : s);
    }
    if (flags & XFNM_HEADONLY)
	substall = false;

    xwcsbuf_T buf;
    size_t i = 0;

    wb_init(&buf);
    do {
	xfnmresult_T result = xfnm_wmatch(xfnm, s + i);
	if (result.start == (size_t) -1 || result.start >= result.end)
	    break;
	wb_ncat(&buf, s + i, result.start);
	wb_cat(&buf, repl);
	i += result.end;
    } while (substall);
    return wb_towcs(wb_cat(&buf, s + i));
}

/* Frees the specified compiled pattern. */
void xfnm_free(xfnmatch_T *xfnm)
{
    if (xfnm != NULL) {
	regfree(&xfnm->regex);
	free(xfnm);
    }
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
