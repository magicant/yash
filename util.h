/* Yash: yet another shell */
/* util.h: miscellaneous utility functions */
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


#ifndef UTIL_H
#define UTIL_H

#ifdef NINLINE
# define NO_UTIL_INLINE
#endif

#include <stdbool.h>
#include <stddef.h>


/********** General functions **********/

extern void *xcalloc(size_t nmemb, size_t size)
	__attribute__((malloc,warn_unused_result));
extern void *xmalloc(size_t size)
	__attribute__((malloc,warn_unused_result));
extern void *xrealloc(void *ptr, size_t size)
	__attribute__((malloc,warn_unused_result));


/********** String utilities **********/

extern size_t xstrnlen(const char *s, size_t maxlen)
	__attribute__((nonnull));
extern char *xstrndup(const char *s, size_t maxlen)
	__attribute__((malloc,warn_unused_result,nonnull));
extern char *xstrdup(const char *s)
	__attribute__((malloc,warn_unused_result,nonnull));
extern size_t xwcsnlen(const wchar_t *s, size_t maxlen)
	__attribute__((nonnull));
extern wchar_t *xwcsndup(const wchar_t *s, size_t maxlen)
	__attribute__((malloc,warn_unused_result,nonnull));
extern wchar_t *xwcsdup(const wchar_t *s)
	__attribute__((malloc,warn_unused_result,nonnull));
#ifndef NO_UTIL_INLINE
# ifdef HAVE_STRNLEN
#  include <string.h>
#  define xstrnlen(s, maxlen) strnlen(s, maxlen)
# endif
# ifdef HAVE_WCSNLEN
#  include <wchar.h>
#  define xwcsnlen(s, maxlen) wcsnlen(s, maxlen)
# endif
# include <stdint.h>
# define xstrdup(s) xstrndup(s, SIZE_MAX)
# define xwcsdup(s) xwcsndup(s, SIZE_MAX)
#endif

extern char *matchstrprefix(const char *s, const char *prefix)
	__attribute__((nonnull));

/* 引数を正しくキャストするためのマクロ */
#define xisalnum(c)  (isalnum((unsigned char) (c)))
#define xisalpha(c)  (isalpha((unsigned char) (c)))
#define xisblank(c)  (isblank((unsigned char) (c)))
#define xiscntrl(c)  (iscntrl((unsigned char) (c)))
#define xisdigit(c)  (isdigit((unsigned char) (c)))
#define xisgraph(c)  (isgraph((unsigned char) (c)))
#define xislower(c)  (islower((unsigned char) (c)))
#define xisprint(c)  (isprint((unsigned char) (c)))
#define xispunct(c)  (ispunct((unsigned char) (c)))
#define xisspace(c)  (isspace((unsigned char) (c)))
#define xisupper(c)  (isupper((unsigned char) (c)))
#define xisxdigit(c) (isxdigit((unsigned char) (c)))
#define xtoupper(c)  (toupper((unsigned char) (c)))
#define xtolower(c)  (tolower((unsigned char) (c)))
/* wchar_t ではこのようなマクロは必要ない */


/********** Arithmetic utilities **********/

/* 整数型 type が符号付きかどうか */
#define IS_TYPE_SIGNED(type) ((type) 1 > (type) -1)

/* 整数型 type を文字列に変換した場合の最大長。
 * CHAR_BIT の為に limits.h が必要。 */
#define INT_STRLEN_BOUND(type) \
	((sizeof(type) * CHAR_BIT - IS_TYPE_SIGNED(type)) * 31 / 100 \
	 + 1 + IS_TYPE_SIGNED(type))


/********** Error utilities **********/

extern const char *yash_program_invocation_name;
extern const char *yash_program_invocation_short_name;
extern unsigned yash_error_message_count;
extern void xerror(int status, int errno_, const char *restrict format, ...)
	__attribute__((format(printf,3,4)));


/********** xgetopt **********/

extern char *xoptarg;
extern int xoptind, xoptopt;
extern bool xopterr;

struct xoption {
	const char *name;
	enum { xno_argument = 0, xrequired_argument, xoptional_argument, } has_arg;
	int *flag;
	int val;
};

extern int xgetopt_long(
		char **restrict argv,
		const char *restrict optstring,
		const struct xoption *restrict longopts,
		int *restrict longindex)
	__attribute__((nonnull(1,2)));
extern int xgetopt(
		char **restrict argv,
		const char *restrict optstring)
	__attribute__((nonnull(1,2)));
#ifndef NO_UTIL_INLINE
# define xgetopt(argv, optstring) xgetopt_long(argv, optstring, NULL, NULL)
#endif


#endif /* UTIL_H */
