/* Yash: yet another shell */
/* util.h: miscellaneous utility functions */
/* (C) 2007-2011 magicant */

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

#include <stdlib.h>

#define Size_max ((size_t) -1)  // = SIZE_MAX


/********** General Functions **********/

static inline void *xcalloc(size_t nmemb, size_t size)
    __attribute__((malloc,warn_unused_result));
static inline void *xmalloc(size_t size)
    __attribute__((malloc,warn_unused_result));
static inline void *xmallocn(size_t count, size_t elemsize)
    __attribute__((malloc,warn_unused_result));
static inline void *xrealloc(void *ptr, size_t size)
    __attribute__((malloc,warn_unused_result));
static inline void *xreallocn(void *ptr, size_t count, size_t elemsize)
    __attribute__((malloc,warn_unused_result));
extern void alloc_failed(void)
    __attribute__((noreturn));

/* Attempts `calloc' and abort the program on failure. */
void *xcalloc(size_t nmemb, size_t size)
{
    void *result = calloc(nmemb, size);
    if (result == NULL)
	alloc_failed();
    return result;
}

/* Attempts `malloc' and abort the program on failure. */
void *xmalloc(size_t size)
{
    void *result = malloc(size);
    if (result == NULL)
	alloc_failed();
    return result;
}

/* Like `xmalloc(count * elemsize)', but aborts the program if the size is too
 * large. `elemsize' must not be zero. */
void *xmallocn(size_t count, size_t elemsize)
{
    size_t size = count * elemsize;
    if (size / elemsize != count)
	alloc_failed();
    return xmalloc(size);
}

/* Attempts `realloc' and abort the program on failure. */
void *xrealloc(void *ptr, size_t size)
{
    void *result = realloc(ptr, size);
    if (result == NULL)
	alloc_failed();
    return result;
}

/* Like `xrealloc(ptr, count * elemsize)', but aborts the program if the size is
 * too large. `elemsize' must not be zero. */
void *xreallocn(void *ptr, size_t count, size_t elemsize)
{
    size_t size = count * elemsize;
    if (size / elemsize != count)
	alloc_failed();
    return xrealloc(ptr, size);
}


/********** String Utilities **********/

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
extern _Bool xstrtoi(const char *s, int base, int *resultp)
    __attribute__((warn_unused_result,nonnull));
extern _Bool xwcstoi(const wchar_t *s, int base, int *resultp)
    __attribute__((warn_unused_result,nonnull));
extern _Bool xwcstol(const wchar_t *s, int base, long *resultp)
    __attribute__((warn_unused_result,nonnull));
extern _Bool xwcstoul(const wchar_t *s, int base, unsigned long *resultp)
    __attribute__((warn_unused_result,nonnull));
extern char *matchstrprefix(const char *s, const char *prefix)
    __attribute__((pure,nonnull));
extern wchar_t *matchwcsprefix(const wchar_t *s, const wchar_t *prefix)
    __attribute__((pure,nonnull));
extern void *copyaswcs(const void *p)
    __attribute__((malloc,warn_unused_result,nonnull));

#if HAVE_STRNLEN
# ifndef strnlen
extern size_t strnlen(const char *s, size_t maxlen);
# endif
# define xstrnlen(s,maxlen) strnlen(s,maxlen)
#endif
#if HAVE_WCSNLEN
# ifndef wcsnlen
extern size_t wcsnlen(const wchar_t *s, size_t maxlen);
# endif
# define xwcsnlen(s,maxlen) wcsnlen(s,maxlen)
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


/* These macros are used to cast the argument properly.
 * We don't need such macros for wide characters. */
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


/* Casts scalar to char safely. */
#define TO_CHAR(value) \
    ((union { char c; unsigned char uc; }) { .uc = (unsigned char) (value), }.c)


/********** Error Utilities **********/

extern const wchar_t *yash_program_invocation_name;
extern const wchar_t *yash_program_invocation_short_name;
extern const wchar_t *current_builtin_name;
extern unsigned yash_error_message_count;
extern void xerror(int errno_, const char *restrict format, ...)
    __attribute__((format(printf,2,3)));

extern _Bool xprintf(const char *restrict format, ...)
    __attribute__((format(printf,1,2)));


/********** xgetopt **********/

extern wchar_t *xoptarg;
extern int xoptind;

enum xgetoptopt_T {
    XGETOPT_POSIX = 1 << 0,
    XGETOPT_DIGIT = 1 << 1,
};
enum optarg_T {
    OPTARG_NONE, OPTARG_REQUIRED, OPTARG_OPTIONAL,
};
struct xgetopt_T {
    wchar_t shortopt;
    const wchar_t *longopt;
    enum optarg_T optarg;
    _Bool posix;
    void *ptr;
};

extern struct xgetopt_T *xgetopt(
	void **restrict argv,
	const struct xgetopt_T *restrict opts,
	enum xgetoptopt_T opt)
    __attribute__((nonnull));


#undef Size_max

#endif /* YASH_UTIL_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
