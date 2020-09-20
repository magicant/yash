/* Yash: yet another shell */
/* expand.h: word expansion */
/* (C) 2007-2020 magicant */

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

/* characters that can be escaped with a backslash inside double-quotes. */
#define CHARS_ESCAPABLE L"$`\"\\"

/* type of tilde expansion */
typedef enum { TT_NONE, TT_SINGLE, TT_MULTI, } tildetype_T;

/* Category of characters resulting from expansion.
 * A charcategory_T value is bitwise or of one of the origin categories
 * (CC_LITERAL, CC_HARD_EXPANSION, and CC_SOFT_EXPANSION) and optionally any
 * combinations of modifier flags (CC_QUOTED and CC_QUOTATION).
 * The category determines if a character is subject to brace expansion, field
 * splitting, and globbing (pathname expansion). */
typedef enum {
    CC_LITERAL,         /* from the original word */
    CC_HARD_EXPANSION,  /* from tilde expansion or numeric brace expansion */
    CC_SOFT_EXPANSION,  /* from parameter expansion, command substitution or
			   arithmetic expansion */
    CC_ORIGIN_MASK = (1 << 2) - 1,
    CC_QUOTED      = 1 << 2, /* The character is quoted by backslash, single- or
				double-quotes. */
    CC_QUOTATION   = 1 << 3, /* The character is a quotation mark */
} charcategory_T;
/* A character can be both CC_QUOTED and CC_QUOTATION at a time. This may happen
 * in a nested quotation like "\"". */

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
extern wchar_t *expand_single(const struct wordunit_T *arg,
	tildetype_T tilde, _Bool processquotes, _Bool escapeall)
    __attribute__((malloc,warn_unused_result));
extern wchar_t *expand_single_and_unescape(const struct wordunit_T *arg,
	tildetype_T tilde, _Bool processquotes, _Bool escapeall)
    __attribute__((malloc,warn_unused_result));
extern char *expand_single_with_glob(
	const struct wordunit_T *arg, tildetype_T tilde)
    __attribute__((malloc,warn_unused_result));

extern wchar_t *extract_fields(
	const wchar_t *restrict s, const char *restrict split,
	_Bool escaped, const wchar_t *restrict ifs,
	struct plist_T *restrict dest)
    __attribute__((nonnull));

struct xwcsbuf_T;
extern wchar_t *escape(const wchar_t *restrict s, const wchar_t *restrict t)
    __attribute__((nonnull(1),malloc,warn_unused_result));
extern wchar_t *escapefree(
	wchar_t *restrict s, const wchar_t *restrict t)
    __attribute__((nonnull(1),malloc,warn_unused_result));
extern wchar_t *unescape(const wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));
extern wchar_t *unescapefree(wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));
extern wchar_t *quote_as_word(const wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));
extern struct xwcsbuf_T *wb_quote_as_word(
	struct xwcsbuf_T *restrict buf, const wchar_t *restrict s)
    __attribute__((nonnull));
extern wchar_t *unquote(const wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));

extern wchar_t *parse_and_expand_string(
	const wchar_t *s, const char *name, _Bool esc)
    __attribute__((nonnull(1),malloc,warn_unused_result));


#endif /* YASH_EXPAND_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
