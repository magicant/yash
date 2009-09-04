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
#include <wchar.h>
#include "strbuf.h"
#include "xfnmatch.h"


static wchar_t *skip_bracket(const wchar_t *pat, bool pathname)
    __attribute__((nonnull));


/* Skips a bracket expression starting with L'[' and returns a pointer to the
 * closing L']'. If no corresponding L']' is found, returns `pat'. */
wchar_t *skip_bracket(const wchar_t *pat, bool pathname)
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
		    case L'.':
			p = wcsstr(pat + 1, L".]");
			break;
		    case L':':
			p = wcsstr(pat + 1, L":]");
			break;
		    case L'=':
			p = wcsstr(pat + 1, L"=]");
			break;
		    default:
			continue;
		}
		if (!p)
		    goto fail;
		pat = p + 2;
		continue;
	    case L']':
		if (pat > savepat + 1)
		    return (wchar_t *) pat + 1;
		break;
	    case L'/':
		/* L'/' is not allowed inside a bracket expr if PATHNAME. */
		if (pathname)
		    goto fail;
		break;
	    default:
		break;
	}
	pat++;
    }

fail:
    return (wchar_t *) savepat;
}

/* Checks if there is L'*' or L'?' or a bracket expression in a pattern.
 * The WFNM_NOESCAPE flag is assumed not to be specified.
 * If the result is false, the pattern is not a filename expansion pattern. */
bool pattern_has_special_char(const wchar_t *pat, bool pathname)
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
		if (skip_bracket(pat, pathname) != pat)
		    return true;
		/* falls thru! */
	    default:
		pat++;
		break;
	}
    }
}

/* Compiles a pattern.
 * The flag is logical OR of the followings:
 *  XFNM_SHORTEST:  do shortest match
 *  XFNM_HEADONLY:  only match at the beginning of string
 *  XFNM_TAILONLY:  only match at the end of string
 *  XFNM_NOESCAPE:  treat `pat' as a literal string
 *  XFNM_PERIOD:    force explicit match for a period at the beginning
 *  XFNM_CASEFOLD:  ignore case while matching
 */
xfnmatch_T *xfnm_compile(const wchar_t *pat, xfnmflags_T flag)
{
    // TODO
    (void) pat, (void) flag;
    return NULL;
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
    //TODO
    (void) xfnm;
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
