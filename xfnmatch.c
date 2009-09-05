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
 *  XFNM_HEADONLY:  only match at the beginning of string
 *  XFNM_TAILONLY:  only match at the end of string
 *  XFNM_PERIOD:    don't match with a string that starts with a period
 *  XFNM_CASEFOLD:  ignore case while matching
 */

#define XFNM_HEADTAIL (XFNM_HEADONLY | XFNM_TAILONLY)

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


/* Checks if there is L'*' or L'?' or a bracket expression in a pattern.
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

/* Skips a bracket expression starting with L'[' and returns a pointer to the
 * closing L']'. If no corresponding L']' is found, returns `pat'. Backslash
 * escaping is recognized in the bracket expression. */
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

/* Checks if there is L'*' or L'?' or a bracket expression in a pattern.
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
	if (!is_matching_pattern(buf))
	    return false;
	pat = p + 1;
    }
    return is_matching_pattern(pat);
}

/* Compiles a pattern.
 * The flags are logical OR of the followings:
 *  XFNM_SHORTEST:  do shortest match
 *  XFNM_HEADONLY:  only match at the beginning of string
 *  XFNM_TAILONLY:  only match at the end of string
 *  XFNM_PERIOD:    force explicit match for a period at the beginning
 *  XFNM_CASEFOLD:  ignore case while matching
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

/* Converts a pathname matching pattern into a regex pattern.
 * The result is appended to `buf', which must be in the initial shift state
 * when the function is called and is terminated in the initial shift state when
 * the function returns. */
/* A trailing backslash, escaping the terminating null character, is ignored.
 * This is useful for pathname expansion as the pattern "f*o\/b?r" for example
 * is separated into "f*o\" and "b?r" and each matched for filenames. */
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

/* Converts a bracket pattern of pathname matching pattern into that of regex.
 * Backslash escaping is recognized in the original pattern.
 * `*pat' must be the opening bracket '['.
 * If a bracket pattern is successfully converted, a pointer to the closing
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
    buf->contents[buf->length = savelength] = '\0';
	// sb_remove(buf, savelength, SIZE_MAX);
    *state = savestate;
    sb_wccat(buf, L'\\', state);
    sb_wccat(buf, L'[', state);
    return savepat;
}

/* Converts a collating symbol, equivalence class or character class pattern.
 * `*pat' must be the opening bracket '[' that is supposed to be the beginning
 * of one of those patterns.
 * If a pattern is successfully converted, a pointer to the corresponding
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

xfnmresult_T xfnm_match(const xfnmatch_T *restrict xfnm, const char *restrict s)
{
    // TODO
    (void) xfnm, (void) s;
    return (xfnmresult_T) { 0, 0 };
}

xfnmresult_T xfnm_wmatch(
	const xfnmatch_T *restrict xfnm, const wchar_t *restrict s)
{
    // TODO
    (void) xfnm, (void) s;
    return (xfnmresult_T) { 0, 0 };
}

wchar_t *xfnm_subst(const xfnmatch_T *restrict xfnm, const char *restrict s,
	const wchar_t *restrict repl, bool substall)
{
    //TODO
    (void) xfnm, (void) s, (void) repl, (void) substall;
    return NULL;
}

/* Frees a compiled pattern. */
void xfnm_free(xfnmatch_T *xfnm)
{
    if (xfnm != NULL) {
	regfree(&xfnm->regex);
	free(xfnm);
    }
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
