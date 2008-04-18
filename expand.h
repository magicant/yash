/* Yash: yet another shell */
/* expand.h: word expansion */
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


#ifndef EXPAND_H
#define EXPAND_H

#include <stdbool.h>
#include <stddef.h>

/* チルダ展開の種類 */
typedef enum { tt_none, tt_single, tt_multi, } tildetype_T;

extern bool expand_line(
	void *const *restrict args,
	int *restrict argcp,
	char ***restrict argvp)
    __attribute__((nonnull));
extern wchar_t *expand_single(const wordunit_T *arg, tildetype_T tilde)
    __attribute__((malloc,warn_unused_result));
extern char *expand_single_with_glob(const wordunit_T *arg, tildetype_T tilde)
    __attribute__((malloc,warn_unused_result));
extern wchar_t *expand_string(const wordunit_T *w, bool esc)
    __attribute__((malloc,warn_unused_result));

extern wchar_t *escape(const wchar_t *restrict s, const wchar_t *restrict t)
    __attribute__((nonnull(1),malloc,warn_unused_result));
static inline wchar_t *escapefree(
	wchar_t *restrict s, const wchar_t *restrict t)
    __attribute__((nonnull(1),malloc,warn_unused_result));
extern wchar_t *unescape(const wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));
static inline wchar_t *unescapefree(wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));
extern wchar_t *unquote(const wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));


/* escape と同じだが、第 1 引数を free する。
 * s の文字のうち、t に含まれる文字をバックスラッシュエスケープして返す。 */
static inline wchar_t *escapefree(
	wchar_t *restrict s, const wchar_t *restrict t)
{
    void free(void *);

    wchar_t *wcspbrk(const wchar_t *ws1, const wchar_t *ws2);
    if (t && !wcspbrk(s, t))
	return s;

    wchar_t *result = escape(s, t);
    free(s);
    return result;
}

/* unescape と同じだが、引数を free する。 */
wchar_t *unescapefree(wchar_t *s)
{
    void free(void *);

    wchar_t *wcschr(const wchar_t *ws, wchar_t wc);
    if (!wcschr(s, L'\\'))
	return s;

    wchar_t *result = unescape(s);
    free(s);
    return result;
}


#endif /* EXPAND_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
