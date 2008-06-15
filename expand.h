/* Yash: yet another shell */
/* expand.h: word expansion */
/* (C) 2007-2008 magicant */

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

/* type of tilde expansion */
typedef enum { tt_none, tt_single, tt_multi, } tildetype_T;

extern bool expand_line(
	void *const *restrict args,
	int *restrict argcp,
	void ***restrict argvp)
    __attribute__((nonnull));
extern wchar_t *expand_single(const wordunit_T *arg, tildetype_T tilde)
    __attribute__((malloc,warn_unused_result));
extern char *expand_single_with_glob(const wordunit_T *arg, tildetype_T tilde)
    __attribute__((malloc,warn_unused_result));
extern wchar_t *expand_string(const wordunit_T *w, bool esc)
    __attribute__((malloc,warn_unused_result));

extern wchar_t *escape(const wchar_t *restrict s, const wchar_t *restrict t)
    __attribute__((nonnull(1),malloc,warn_unused_result));
extern wchar_t *escapefree(
	wchar_t *restrict s, const wchar_t *restrict t)
    __attribute__((nonnull(1),malloc,warn_unused_result));
extern wchar_t *unescape(const wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));
extern wchar_t *unescapefree(wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));
extern wchar_t *unquote(const wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));

extern wchar_t *parse_and_expand_string(const wchar_t *s, const char *name)
    __attribute__((nonnull(1),malloc,warn_unused_result));


#endif /* EXPAND_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
