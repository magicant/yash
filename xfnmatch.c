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
#include <stdbool.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "strbuf.h"
#include "xfnmatch.h"

#define NOESCAPE (!!(flags & WFNM_NOESCAPE))
#define PATHNAME (!!(flags & WFNM_PATHNAME))
#define PERIOD   (!!(flags & WFNM_PERIOD))
#define CASEFOLD (!!(flags & WFNM_CASEFOLD))

#define FOLD(c) (CASEFOLD ? towlower(c) : (wint_t) (c))

static wchar_t *skip_bracket(const wchar_t *pat, enum wfnmflags flags)
    __attribute__((nonnull));
static size_t wfnmatchn(const wchar_t *p, const wchar_t *s, size_t lendiff,
	enum wfnmflags flags, enum wfnmtype type)
    __attribute__((nonnull));
static size_t match_bracket(
	const wchar_t **pp, const wchar_t *s, enum wfnmflags flags)
    __attribute__((nonnull));
static wchar_t *check_char_class(const wchar_t *p, wint_t c, bool *match)
    __attribute__((nonnull));


/* Does a pattern matching on a wide string.
 * pat:    a pattern
 * s:      a string to be matched.
 * flags:  a bitwise OR of the following flags:
 *         WFNM_NOESCAPE: don't treat backslashes as escape characters.
 *         WFNM_PATHNAME: L"*" and L"?" don't match to L'/'
 *         WFNM_PERIOD:   L"*" and L"?" don't match to L'.' at the head.
 *         WFNM_CASEFOLD: case insensitive matching
 * type:   one of:
 *         WFNM_WHOLE:    match the whole string only
 *         WFNM_LONGEST:  match as long as possible
 *         WFNM_SHORTEST: match as short as possible
 * Returns the number of characters that matched if successful or
 * WFNM_NOMATCH if the string doesn't match or WFNM_ERROR on error.
 * WFNM_ERROR means that the pattern is invalid, though WFNM_ERROR is not
 * always returned when the pattern is invalid. */
size_t wfnmatch(const wchar_t *pat, const wchar_t *s,
	enum wfnmflags flags, enum wfnmtype type)
{
    return wfnmatchl(pat, s, flags, type, shortest_match_length(pat, flags));
}

/* Does a pattern matching on a wide string.
 * pat:    a pattern
 * s:      a string to be matched.
 * flags:  a bitwise OR of the following flags:
 *         WFNM_NOESCAPE: don't treat backslashes as escape characters.
 *         WFNM_PATHNAME: L"*" and L"?" don't match to L'/'
 *         WFNM_PERIOD:   L"*" and L"?" don't match to L'.' at the head.
 *         WFNM_CASEFOLD: case insensitive matching
 * type:   one of:
 *         WFNM_WHOLE:    match the whole string only
 *         WFNM_LONGEST:  match as long as possible
 *         WFNM_SHORTEST: match as short as possible
 * minlen: the value of `shortest_match_length(pat, flags)'
 * Returns the number of characters that matched if successful or
 * WFNM_NOMATCH if the string doesn't match or WFNM_ERROR on error.
 * WFNM_ERROR means that the pattern is invalid, though WFNM_ERROR is not
 * always returned when the pattern is invalid. */
size_t wfnmatchl(const wchar_t *pat, const wchar_t *s,
	enum wfnmflags flags, enum wfnmtype type, size_t minlen)
{
    if (!pat[0])
	return s[0] ? WFNM_NOMATCH : 0;

    /* check a period at the head */
    if (PERIOD && FOLD(s[0]) == L'.')
	if (pat[0] != L'.' && (pat[0] != L'\\' || pat[1] != L'.'))
	    return WFNM_NOMATCH;

    size_t slen = wcslen(s);
    if (slen < minlen)
	return WFNM_NOMATCH;

    if (type == WFNM_WHOLE) {
	/* check one character at the tail */
	size_t patlen = wcslen(pat);
	if (patlen > 0 && slen > 0) {
	    switch (pat[patlen-1]) {
		case L']':  case L'?':  case L'*':  case L'\\':
		    break;
		default:
		    if (FOLD(pat[patlen-1]) != FOLD(s[slen-1]))
			return WFNM_NOMATCH;
	    }
	}
    }

    return wfnmatchn(pat, s, slen - minlen, flags, type);
}

/* Counts the minimum number of characters that `pat' matches. */
size_t shortest_match_length(const wchar_t *pat, enum wfnmflags flags)
{
    size_t count = 0;

    for (;;) {
	const wchar_t *p;
	switch (*pat) {
	    case L'\0':  eop:
		return count;
	    case L'*':
		break;
	    case L'[':
		p = skip_bracket(pat, flags);
		if (pat != p) {
		    count++;
		    pat = p;
		    continue;
		}
		goto default_case;
	    case L'\\':
		if (!NOESCAPE) {
		    pat++;
		    if (*pat == L'\0')
			goto eop;
		}
		/* falls thru! */
	    default:  case L'?':  default_case:
		count++;
		break;
	}
	pat++;
    }
}

/* Skips a bracket expression starting with L'[' and returns a pointer to the
 * closing L']'. If no corresponding L']' is found, returns `pat'. */
wchar_t *skip_bracket(const wchar_t *pat, enum wfnmflags flags)
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
		if (PATHNAME)
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

/* Do matching.
 * `lendiff' is (wcslen(s) - shortest_match_length(p, flags)).
 * See `wfnmatch' for the other arguments. */
size_t wfnmatchn(const wchar_t *p, const wchar_t *s, size_t lendiff,
	enum wfnmflags flags, enum wfnmtype type)
{
    const wchar_t *const saves = s;
    wint_t sc;

    for (;;) {
	switch (*p) {
	case L'\0':  eop:
	    if (type == WFNM_WHOLE && *s != L'\0')
		return WFNM_NOMATCH;
	    else
		return s - saves;
	case L'*':
	    do p++; while (*p == L'*');  /* skip until non-L'*' */
	    if (*p == L'\0') {
		/* if the pattern ends with L'*', we're done */
		switch (type) {
		case WFNM_WHOLE:
		case WFNM_LONGEST:  return (s - saves) + wcslen(s);
		case WFNM_SHORTEST: return (s - saves);
		}
	    }
	    /* try matching the remaining portion of the pattern, increasing
	     * the number of character that match L'*' until successful */
	    size_t submatchlen = WFNM_NOMATCH;
	    do {
		size_t r = wfnmatchn(p, s, lendiff, flags, type);
		if (r == WFNM_ERROR)
		    return WFNM_ERROR;
		if (r != WFNM_NOMATCH) {
		    if (type == WFNM_SHORTEST)
			return r + (s - saves);
		    else
			submatchlen = r + (s - saves);
		}
		if (PATHNAME && FOLD(*s) == L'/')
		    break;
		s++;
	    } while (lendiff-- != 0);  /* loop until `lendiff' is negative */
	    return submatchlen;
	case L'?':
	    if ((PATHNAME && FOLD(*s) == L'/') || (*s == L'\0'))
		return WFNM_NOMATCH;
	    break;
	case L'[':
	    {
		size_t r = match_bracket(&p, s, flags);
		if (r == WFNM_ERROR || r == WFNM_NOMATCH) return r;
		s += r;
		if (r)
		    continue;
		else
		    goto default_case;
	    }
	case L'\\':
	    if (!NOESCAPE) {
		p++;
		if (*p == L'\0')
		    goto eop;
	    }
	    /* falls thru! */
	default:  default_case:
	    sc = FOLD(*s);
	    if (FOLD(*p) != sc)
		return WFNM_NOMATCH;
	    if (PATHNAME && PERIOD) {
		/* if PATHNAME && PERIOD, check L"/." at a time */
		if (sc == L'/' && FOLD(*(s+1)) == L'.') {
		    if (*(p+1) != L'.') return WFNM_NOMATCH;
		    p += 2;  s += 2;
		    continue;
		}
	    }
	    break;
	}
	p++; s++;
    }
}

/* Parses a bracket expression and checks if it matches.
 * If it matches, `*pp' is advanced to the character right after the match.
 * If not, `*pp' is unchanged.
 * See `wfnmatch' for the meaning of the return value. */
size_t match_bracket(
	const wchar_t **pp, const wchar_t *s, enum wfnmflags flags)
{
    const wchar_t *p = *pp;
    wint_t sc = *s, fsc = FOLD(sc);
    bool matched = false;

    assert(*p == L'[');
    p++;

    /* check for negation.
     * If not PATHNAME, L'^' following L'[' inverts the matching result.
     * If PATHNAME, L'!' is to be used instead of L'^', where the meaning of
     * L'^' is not specified by POSIX. We treat L'^' as same as L'!' in this
     * case. */
    bool neg = (*p == L'^') || (PATHNAME && *p == L'!');
    if (neg)
	p++;

    /* We don't support collating symbols like L"[.ch.]" or equivalent classes
     * like L"[=a=]". These pattern simply matches to the first character of it.
     * For example, L"[.ch.]" is equivalent to L"c". */

    for (bool initial = true; ; initial = false) {
	wint_t rangestart, rangeend;
	switch (*p) {
	case L'\0':
	    return WFNM_ERROR;
	case L']':
	    if (initial)
		goto default_case;
	    *pp = p + 1;
	    return (matched != neg) ? 1 : WFNM_NOMATCH;
	case L'[':
	    switch (*(p+1)) {
		case L'.':
		    rangestart = *(p+2);
		    p = wcsstr(p+2, L".]");
		    if (!p)
			return WFNM_ERROR;
		    p += 2;
		    break;
		case L'=':
		    rangestart = *(p+2);
		    p = wcsstr(p+2, L"=]");
		    if (!p)
			return WFNM_ERROR;
		    p += 2;
		    break;
		case L':':
		    p = check_char_class(p+2, sc, &matched);
		    if (!p)
			return WFNM_ERROR;
		    continue;
		default:
		    goto default_case;
	    }
	    break;
	case L'/':
	    if (PATHNAME)
		return 0;
	    /* falls thru! */
	default:  default_case:
	    rangestart = *p;
	    p++;
	    break;
	}
	if (*p == L'-' && *(p+1) != L']') {
	    switch (*(p+1)) {
	    case L'\0':
		return WFNM_ERROR;
	    case L'[':
		switch (*(p+2)) {
		    case L'.':
			rangeend = *(p+3);
			p = wcsstr(p+3, L".]");
			if (!p)
			    return WFNM_ERROR;
			p += 2;
			break;
		    case L'=':
			rangeend = *(p+3);
			p = wcsstr(p+3, L"=]");
			if (!p)
			    return WFNM_ERROR;
			p += 2;
			break;
		    case L':':
			return WFNM_ERROR;
		    default:
			goto default_case2;
		}
		break;
	    case L'/':
		if (PATHNAME)
		    return 0;
		/* falls thru! */
	    default:  default_case2:
		rangeend = *(p+1);
		p += 2;
		break;
	    }
	} else {
	    rangeend = rangestart;
	}
	if (rangestart)
	    if (FOLD(rangestart) <= fsc && fsc <= FOLD(rangeend))
		matched = true;
    }
}

/* Checks if a character class matches or not.
 * `p' is a pointer to the character right after L"[:".
 * `c' is a character to be matched.
 * The result is assigned to `*match'.
 * Returns the character right after L":]" in the pattern, or NULL if there is
 * no L":]" in the pattern. */
wchar_t *check_char_class(const wchar_t *p, wint_t c, bool *match)
{
    const wchar_t *end = wcsstr(p, L":]");
    if (!end)
	return NULL;

    char *classname = malloc_wcsntombs(p, end - p);
    if (!classname)
	return NULL;

    wctype_t type = wctype(classname);
    free(classname);
    if (iswctype(c, type))
	*match = true;
    return (wchar_t *) end + 2;
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
		if (skip_bracket(pat, pathname ? WFNM_PATHNAME : 0) != pat)
		    return true;
		/* falls thru! */
	    default:
		pat++;
		break;
	}
    }
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
