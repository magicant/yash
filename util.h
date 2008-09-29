/* Yash: yet another shell */
/* util.h: miscellaneous utility functions */
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


#ifndef YASH_UTIL_H
#define YASH_UTIL_H

#include <stddef.h>

#define Size_max ((size_t) -1)  // = SIZE_MAX


/********** General functions **********/

static inline void *xcalloc(size_t nmemb, size_t size)
    __attribute__((malloc,warn_unused_result));
static inline void *xmalloc(size_t size)
    __attribute__((malloc,warn_unused_result));
static inline void *xrealloc(void *ptr, size_t size)
    __attribute__((malloc,warn_unused_result));
extern void alloc_failed(void)
    __attribute__((noreturn));

/* Attempts a `calloc' and abort the program on failure. */
void *xcalloc(size_t nmemb, size_t size)
{
    extern void *calloc(size_t nmemb, size_t size)
	__attribute__((malloc,warn_unused_result));
    void *result = calloc(nmemb, size);
    if (!result)
	alloc_failed();
    return result;
}

/* Attempts a `malloc' and abort the program on failure. */
void *xmalloc(size_t size)
{
    extern void *malloc(size_t size)
	__attribute__((malloc,warn_unused_result));
    void *result = malloc(size);
    if (!result)
	alloc_failed();
    return result;
}

/* Attempts a `realloc' and abort the program on failure. */
void *xrealloc(void *ptr, size_t size)
{
    extern void *realloc(void *ptr, size_t size)
	__attribute__((malloc,warn_unused_result));
    void *result = realloc(ptr, size);
    if (!result)
	alloc_failed();
    return result;
}


/********** String utilities **********/

extern size_t xstrnlen(const char *s, size_t maxlen)
    __attribute__((pure,nonnull));
extern char *xstrndup(const char *s, size_t maxlen)
    __attribute__((malloc,warn_unused_result,nonnull));
static inline char *xstrdup(const char *s)
    __attribute__((malloc,warn_unused_result,nonnull));
extern size_t xwcsnlen(const wchar_t *s, size_t maxlen)
    __attribute__((pure,nonnull));
extern wchar_t *xwcsndup(const wchar_t *s, size_t maxlen)
    __attribute__((malloc,warn_unused_result,nonnull));
static inline wchar_t *xwcsdup(const wchar_t *s)
    __attribute__((malloc,warn_unused_result,nonnull));
static inline void **duparray(void *const *array, void *copy(const void *p))
    __attribute__((malloc,warn_unused_result,nonnull(2)));
extern void **duparrayn(
	void *const *array, size_t count, void *copy(const void *p))
    __attribute__((malloc,warn_unused_result,nonnull(3)));
extern char *joinstrarray(char *const *array, const char *padding)
    __attribute__((malloc,warn_unused_result,nonnull));
extern wchar_t *joinwcsarray(void *const *array, const wchar_t *padding)
    __attribute__((malloc,warn_unused_result,nonnull));
extern char *matchstrprefix(const char *s, const char *prefix)
    __attribute__((pure,nonnull));
extern wchar_t *matchwcsprefix(const wchar_t *s, const wchar_t *prefix)
    __attribute__((pure,nonnull));

extern void *copyaswcs(const void *p)
    __attribute__((malloc,warn_unused_result));

extern void sort_mbs_array(void **array)
    __attribute__((nonnull));

#if HAVE_STRNLEN
extern size_t strnlen(const char *s, size_t maxlen);
#define xstrnlen(s,maxlen) strnlen(s,maxlen)
#endif
#if HAVE_WCSNLEN
extern size_t wcsnlen(const wchar_t *s, size_t maxlen);
#define xwcsnlen(s,maxlen) wcsnlen(s,maxlen)
#endif

/* Returns a newly malloced copy of the specified string.
 * Aborts the program if failed to allocate memory. */
char *xstrdup(const char *s)
{
    return xstrndup(s, Size_max);
}

/* Returns a newly malloced copy of the specified string.
 * Aborts the program if failed to allocate memory. */
wchar_t *xwcsdup(const wchar_t *s)
{
    return xwcsndup(s, Size_max);
}

/* Clones a NULL-terminated array of pointers.
 * Each pointer element is passed to `copy' function and the return value is
 * assigned to the new array element. */
/* `xstrdup' and `copyaswcs' are suitable for `copy'. */
void **duparray(void *const *array, void *copy(const void *p))
{
    return duparrayn(array, Size_max, copy);
}


/* These macros are used to cast the argument properly.
 * We don't need such macros for wide strings. */
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


/********** Arithmetic utilities **********/

/* Whether an integral type `type' is signed. */
#define IS_TYPE_SIGNED(type) ((type) 1 > (type) -1)

/* Maximum number of digits we need to convert a value of an integral type
 * `type' to a decimal string.
 * Inclusion of <limits.h> is required for CHAR_BIT. */
#define INT_STRLEN_BOUND(type) \
    ((sizeof(type) * CHAR_BIT - IS_TYPE_SIGNED(type)) * 31 / 100 \
     + 1 + IS_TYPE_SIGNED(type))


/********** Error utilities **********/

extern const wchar_t *yash_program_invocation_name;
extern const wchar_t *yash_program_invocation_short_name;
extern const wchar_t *current_builtin_name;
extern unsigned yash_error_message_count;
extern void xerror(int errno_, const char *restrict format, ...)
    __attribute__((format(printf,2,3)));


/********** xgetopt **********/

extern wchar_t *xoptarg;
extern int xoptind;
extern wchar_t xoptopt;
extern _Bool xopterr;

struct xoption {
    const wchar_t *name;
    enum { xno_argument = 0, xrequired_argument, xoptional_argument, } has_arg;
    wchar_t val;
};

extern wchar_t xgetopt_long(
	void **restrict argv,
	const wchar_t *restrict optstring,
	const struct xoption *restrict longopts,
	int *restrict longindex)
    __attribute__((nonnull(1,2)));
static inline wchar_t xgetopt(
	void **restrict argv,
	const wchar_t *restrict optstring)
    __attribute__((nonnull(1,2)));

/* `xgetopt_long' without long options. */
wchar_t xgetopt(void **restrict argv, const wchar_t *restrict optstring)
{
    return xgetopt_long(argv, optstring, NULL, NULL);
}


#undef Size_max

#endif /* YASH_UTIL_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
