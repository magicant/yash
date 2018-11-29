/* Yash: yet another shell */
/* xfnmatch.h: regex matching wrapper as a replacement for fnmatch */
/* (C) 2007-2018 magicant */

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


#ifndef YASH_XFNMATCH_H
#define YASH_XFNMATCH_H

#include <stddef.h>

typedef struct xfnmatch_T xfnmatch_T;
typedef enum {
    XFNM_SHORTEST = 1 << 0,
    XFNM_HEADONLY = 1 << 1,
    XFNM_TAILONLY = 1 << 2,
    XFNM_PERIOD   = 1 << 3,
    XFNM_CASEFOLD = 1 << 4,
    XFNM_compiled = 1 << 5,
    XFNM_headstar = 1 << 6,
    XFNM_tailstar = 1 << 7,
} xfnmflags_T;
typedef struct {
    size_t start, end;
} xfnmresult_T;

extern _Bool is_matching_pattern(const wchar_t *pat)
    __attribute__((pure,nonnull));
extern _Bool is_pathname_matching_pattern(const wchar_t *pat)
    __attribute__((pure,nonnull));

extern xfnmatch_T *xfnm_compile(const wchar_t *pat, xfnmflags_T flags)
    __attribute__((malloc,warn_unused_result,nonnull));
extern int xfnm_match(
	const xfnmatch_T *restrict xfnm, const char *restrict s)
    __attribute__((nonnull));
extern xfnmresult_T xfnm_wmatch(
	const xfnmatch_T *restrict xfnm, const wchar_t *restrict s)
    __attribute__((nonnull));
extern wchar_t *xfnm_subst(
	const xfnmatch_T *restrict xfnm, const wchar_t *restrict s,
	const wchar_t *restrict repl, _Bool substall)
    __attribute__((malloc,warn_unused_result,nonnull));
extern void xfnm_free(xfnmatch_T *xfnm);

extern _Bool match_pattern(const wchar_t *s, const wchar_t *pattern)
    __attribute__((nonnull));
#if YASH_ENABLE_TEST
extern _Bool match_regex(const wchar_t *s, const wchar_t *regex)
    __attribute__((nonnull));
#endif


#endif /* YASH_XFNMATCH_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
