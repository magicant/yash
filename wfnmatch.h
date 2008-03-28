/* Yash: yet another shell */
/* strmatch.h: fnmatch for wide-character strings */
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


#ifndef STRMATCH_H
#define STRMATCH_H

#include <stddef.h>

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

__attribute__((nonnull))
size_t wfnmatch(const wchar_t *pat, const wchar_t *str,
	enum wfnmflags flags, enum wfnmtype type);


#endif /* STRMATCH_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
