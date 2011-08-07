/* Yash: yet another shell */
/* expand.h: word expansion */
/* (C) 2007-2010 magicant */

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


#ifndef YASH_EXPAND_H
#define YASH_EXPAND_H

#include <stddef.h>


#define DEFAULT_IFS L" \t\n"

/* characters that have special meanings in brace expansion and glob. */
#define CHARS_ESCAPED L"\\{,}"

/* characters that can be escaped with a backslash inside double-quotes. */
#define CHARS_ESCAPABLE L"$`\"\\"

/* type of tilde expansion */
typedef enum { TT_NONE, TT_SINGLE, TT_MULTI, } tildetype_T;

struct wordunit_T;
struct plist_T;
extern _Bool expand_line(
	void *const *restrict args,
	int *restrict argcp,
	void ***restrict argvp)
    __attribute__((nonnull));
extern _Bool expand_multiple(
	const struct wordunit_T *restrict w, struct plist_T *restrict list)
    __attribute__((nonnull(2)));
extern wchar_t *expand_single(const struct wordunit_T *arg, tildetype_T tilde)
    __attribute__((malloc,warn_unused_result));
extern char *expand_single_with_glob(
	const struct wordunit_T *arg, tildetype_T tilde)
    __attribute__((malloc,warn_unused_result));
extern wchar_t *expand_string(const struct wordunit_T *w, _Bool esc)
    __attribute__((malloc,warn_unused_result));

extern wchar_t *split_next_field(
	const wchar_t **sp, const wchar_t *ifs, _Bool noescape)
    __attribute__((nonnull(1),malloc,warn_unused_result));
extern void trim_trailing_spaces(
	wchar_t *restrict s, const wchar_t *restrict ifs)
    __attribute__((nonnull(1)));

extern wchar_t *escape(const wchar_t *restrict s, const wchar_t *restrict t)
    __attribute__((nonnull(1),malloc,warn_unused_result));
extern wchar_t *escapefree(
	wchar_t *restrict s, const wchar_t *restrict t)
    __attribute__((nonnull(1),malloc,warn_unused_result));
extern wchar_t *unescape(const wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));
extern wchar_t *unescapefree(wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));
extern wchar_t *quote_sq(const wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));
extern wchar_t *unquote(const wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));

extern wchar_t *parse_and_expand_string(
	const wchar_t *s, const char *name, _Bool esc)
    __attribute__((nonnull(1),malloc,warn_unused_result));


#endif /* YASH_EXPAND_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
