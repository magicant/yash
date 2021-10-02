/* Yash: yet another shell */
/* util.h: miscellaneous utility functions */
/* (C) 2007-2021 magicant */

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


/********** Miscellaneous Functions **********/

static inline int xunsetenv(const char *name)
    __attribute__((nonnull));

/* Removes the environment variable of the specified name.
 * This function wraps the `unsetenv' function, which has an incompatible
 * prototype on some old environments. */
int xunsetenv(const char *name)
{
#if UNSETENV_RETURNS_INT
    return unsetenv(name);
#else
    unsetenv(name);
    return 0;
#endif
}


/********** Memory Functions **********/

static inline size_t add(size_t a, size_t b)
    __attribute__((pure));
static inline size_t mul(size_t a, size_t b)
    __attribute__((pure));

static inline void *xcalloc(size_t nmemb, size_t size)
    __attribute__((malloc,warn_unused_result));
static inline void *xmalloc(size_t size)
    __attribute__((malloc,warn_unused_result));
static inline void *xmallocn(size_t count, size_t elemsize)
    __attribute__((malloc,warn_unused_result));
static inline void *xmalloce(size_t count1, size_t count2, size_t elemsize)
    __attribute__((malloc,warn_unused_result));
static inline void *xmallocs(size_t mainsize, size_t count, size_t elemsize)
    __attribute__((malloc,warn_unused_result));
static inline void *xrealloc(void *ptr, size_t size)
    __attribute__((malloc,warn_unused_result));
static inline void *xreallocn(void *ptr, size_t count, size_t elemsize)
    __attribute__((malloc,warn_unused_result));
static inline void *xrealloce(void *ptr,
	size_t count1, size_t count2, size_t elemsize)
    __attribute__((malloc,warn_unused_result));
static inline void *xreallocs(void *ptr,
	size_t mainsize, size_t count, size_t elemsize)
    __attribute__((malloc,warn_unused_result));
extern void alloc_failed(void)
    __attribute__((noreturn));

/* Computes `a + b', but aborts the program by ENOMEM if the result overflows.
 */
size_t add(size_t a, size_t b)
{
    size_t sum = a + b;
    if (sum < a)
	alloc_failed();
    return sum;
}

/* Computes `a * b', but aborts the program by ENOMEM if the result overflows.
 */
size_t mul(size_t a, size_t b)
{
    size_t product = a * b;
    if (b != 0 && product / b != a)
	alloc_failed();
    return product;
}

/* Attempts `calloc' and aborts the program on failure. */
void *xcalloc(size_t nmemb, size_t size)
{
    void *result = calloc(nmemb, size);
    if (result == NULL && nmemb > 0 && size > 0)
	alloc_failed();
    return result;
}

/* Attempts `malloc' and aborts the program on failure. */
void *xmalloc(size_t size)
{
    void *result = malloc(size);
    if (result == NULL && size > 0)
	alloc_failed();
    return result;
}

/* Like `xmalloc(count * elemsize)', but aborts the program if the size is too
 * large. */
void *xmallocn(size_t count, size_t elemsize)
{
    return xmalloc(mul(count, elemsize));
}

/* Like `xmalloc((count1 + count2) * elemsize)', but aborts the program if the
 * size is too large. */
void *xmalloce(size_t count1, size_t count2, size_t elemsize)
{
    return xmallocn(add(count1, count2), elemsize);
}

/* Like `xmalloc(mainsize + count * elemsize)', but aborts the program if the
 * size is too large. */
void *xmallocs(size_t mainsize, size_t count, size_t elemsize)
{
    return xmalloc(add(mainsize, mul(count, elemsize)));
}

/* Attempts `realloc' and aborts the program on failure. */
void *xrealloc(void *ptr, size_t size)
{
    /* The behavior of `realloc(non_null_pointer, 0)' is unreliable. Some
     * implementations free the previously allocated region and return NULL.
     * Some reallocate an empty region and return a pointer to it. The latter
     * may fail to allocate the new region, leaving the previous region intact
     * and returning NULL. That means, when `realloc(non_null_pointer, 0)'
     * returned NULL, we cannot tell if the previous region has been freed or
     * not. */
    if (size == 0) {
	free(ptr);
	return NULL;
    }

    void *result = realloc(ptr, size);
    if (result == NULL)
	alloc_failed();
    return result;
}

/* Like `xrealloc(ptr, count * elemsize)', but aborts the program if the size is
 * too large. */
void *xreallocn(void *ptr, size_t count, size_t elemsize)
{
    return xrealloc(ptr, mul(count, elemsize));
}

/* Like `xrealloc(ptr, (count1 + count2) * elemsize)', but aborts the program if
 * the size is too large. */
void *xrealloce(void *ptr, size_t count1, size_t count2, size_t elemsize)
{
    return xreallocn(ptr, add(count1, count2), elemsize);
}

/* Like `xrealloc(ptr, mainsize + count * elemsize)', but aborts the program if
 * the size is too large. */
void *xreallocs(void *ptr, size_t mainsize, size_t count, size_t elemsize)
{
    return xrealloc(ptr, add(mainsize, mul(count, elemsize)));
}


/********** String Utilities **********/

#if !HAVE_STRNLEN
extern size_t xstrnlen(const char *s, size_t maxlen)
    __attribute__((pure,nonnull));
#endif
#if !HAVE_STRDUP
extern char *xstrdup(const char *s)
    __attribute__((malloc,warn_unused_result,nonnull));
#endif
#if !HAVE_WCSNLEN
extern size_t xwcsnlen(const wchar_t *s, size_t maxlen)
    __attribute__((pure,nonnull));
#endif
extern wchar_t *xwcsndup(const wchar_t *s, size_t maxlen)
    __attribute__((malloc,warn_unused_result,nonnull));
#if !HAVE_WCSDUP
static inline wchar_t *xwcsdup(const wchar_t *s)
    __attribute__((malloc,warn_unused_result,nonnull));
#endif
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
# define xstrnlen strnlen
#endif

#if HAVE_STRDUP
# ifndef strdup
extern char *strdup(const char *s);
# endif
# define xstrdup strdup
#endif

#if HAVE_WCSNLEN
# ifndef wcsnlen
extern size_t wcsnlen(const wchar_t *s, size_t maxlen);
# endif
# define xwcsnlen wcsnlen
#endif

#if HAVE_WCSDUP
# ifndef wcsdup
extern wchar_t *wcsdup(const wchar_t *s);
# endif
# define xwcsdup wcsdup
#else
/* Returns a newly malloced copy of the specified string.
 * Aborts the program if failed to allocate memory. */
wchar_t *xwcsdup(const wchar_t *s)
{
    return xwcsndup(s, Size_max);
}
#endif


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


#undef Size_max

#endif /* YASH_UTIL_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
