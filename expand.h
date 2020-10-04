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

/* treatment of quotation marks during expansion */
typedef enum {
    Q_WORD,     /* Single quotations, double quotations, and backslashes are
		   recognized as in the normal word. */
    Q_DQPARAM,  /* The string is treated as the substitution word of a parameter
		   expansion inside a pair of double quotations: Double
		   quotations are recognized, but single quotations are not.
		   Backslashes are recognized only before $, `, ", \ or }. */
    Q_INDQ,     /* The string is treated as if it is inside a pair of double
		   quotations: Single and double quotations are not recognized.
		   Backslashes are recognized only before a $, `, or \. */
    Q_LITERAL,  /* No quotations are recognized. */
} quoting_T;

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

/* type of characters to be backslash-escaped in the expansion results */
typedef enum {
    ES_NONE,         /* No characters are escaped. */
    ES_QUOTED,       /* Quoted characters remain escaped. */
    ES_QUOTED_HARD,  /* Ditto, and characters marked CC_HARD_EXPANSION and
			backslashes are also escaped. */
} escaping_T;
/* ES_QUOTED_HARD is for pathname expansion patterns while ES_QUOTED is for
 * other patterns. With ES_QUOTED_HARD, backslashes that are not quotation
 * marks are escaped to prevent them from being regarded as escaping
 * characters. This does not apply to ES_QUOTED because the pattern is
 * supposed to be matched without quote removal. */

/* result of word expansion */
typedef struct cc_word_T {
    wchar_t *value;  /* word value */
    char *cc;        /* corresponding charcategory_T string */
} cc_word_T;

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
extern struct cc_word_T expand_single_cc(
	const struct wordunit_T *w, tildetype_T tilde, quoting_T quoting)
    __attribute__((warn_unused_result));
extern wchar_t *expand_single(
	const struct wordunit_T *w,
	tildetype_T tilde, quoting_T quoting, escaping_T escaping)
    __attribute__((malloc,warn_unused_result));
extern char *expand_single_with_glob(const struct wordunit_T *arg)
    __attribute__((malloc,warn_unused_result));

extern wchar_t *extract_fields(
	const wchar_t *restrict s, const char *restrict cc,
	const wchar_t *restrict ifs, struct plist_T *restrict dest)
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
extern wchar_t *quote_removal(
	const wchar_t *restrict s, const char *restrict cc, escaping_T escaping)
    __attribute__((nonnull,malloc,warn_unused_result));

extern wchar_t *parse_and_expand_string(
	const wchar_t *s, const char *name, _Bool esc)
    __attribute__((nonnull(1),malloc,warn_unused_result));


#endif /* YASH_EXPAND_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
