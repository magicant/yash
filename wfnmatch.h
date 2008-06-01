/* Yash: yet another shell */
/* wfnmatch.h: fnmatch for wide-character strings */
/* © 2007-2008 magicant */

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


#ifndef WFNMATCH_H
#define WFNMATCH_H

#include <stddef.h>
#include <stdbool.h>

/* wfnmatch の flags 引数の値 */
enum wfnmflags {
    WFNM_NOESCAPE = 1 << 0,
    WFNM_PATHNAME = 1 << 1,
    WFNM_PERIOD   = 1 << 2,
    WFNM_CASEFOLD = 1 << 3,
};

/* wfnmatch の type 引数の値 */
enum wfnmtype { WFNM_WHOLE, WFNM_LONGEST, WFNM_SHORTEST, };

/* マッチしなかった場合・エラーの場合に返す値 */
#define WFNM_NOMATCH ((size_t) -1)
#define WFNM_ERROR   ((size_t) -2)

extern size_t wfnmatch(const wchar_t *pat, const wchar_t *s,
	enum wfnmflags flags, enum wfnmtype type)
    __attribute__((nonnull));
extern size_t wfnmatchl(const wchar_t *pat, const wchar_t *s,
	enum wfnmflags flags, enum wfnmtype type, size_t shortest_match_length)
    __attribute__((nonnull));
extern size_t shortest_match_length(const wchar_t *pat, enum wfnmflags flags)
    __attribute__((nonnull));
static inline bool pattern_is_nonliteral(const wchar_t *pat)
    __attribute__((pure,nonnull));
extern bool pattern_has_special_char(const wchar_t *pat)
    __attribute__((pure,nonnull));

/* パターン内に L'*' や L'?' などの特殊文字があるかどうか調べる。
 * 結果が false ならば、pat にマッチする文字列は pat それ自身のみである。 */
bool pattern_is_nonliteral(const wchar_t *pat)
{
    extern wchar_t *wcspbrk(const wchar_t *, const wchar_t *);
    return wcspbrk(pat, L"*?[\\") != NULL;
}


#endif /* WFNMATCH_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
