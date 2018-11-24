/* Yash: yet another shell */
/* util.c: miscellaneous utility functions */
/* (C) 2007-2012 magicant */

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


#include "common.h"
#include "util.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "exec.h"
#include "option.h"
#include "plist.h"


/********** Memory Utilities **********/

/* This function is called on memory allocation failure and
 * aborts the program after printing an error message. */
void alloc_failed(void)
{
    xerror(ENOMEM, NULL);
    abort();
}


/********** String Utilities **********/

#if !HAVE_STRNLEN
/* Returns min(maxlen, strlen(s)). */
size_t xstrnlen(const char *s, size_t maxlen)
{
    size_t result = 0;
    while (result < maxlen && s[result] != '\0')
	result++;
    return result;
}
#endif

#if !HAVE_STRDUP
/* Returns a newly malloced copy of the specified string.
 * Aborts the program on malloc failure. */
char *xstrdup(const char *s)
{
    size_t len = strlen(s);
    // char *result = xmalloce(len, 1, sizeof (char));
    char *result = xmalloc(add(len, 1));
    result[len] = '\0';
    return memcpy(result, s, len);
}
#endif

#if !HAVE_WCSNLEN
/* Returns min(maxlen, wcslen(s)). */
size_t xwcsnlen(const wchar_t *s, size_t maxlen)
{
    size_t result = 0;
    while (result < maxlen && s[result] != L'\0')
	result++;
    return result;
}
#endif

/* Returns a newly malloced copy of the specified string.
 * The copy is at most `len' characters long.
 * Returns an exact copy if (wcslen(s) <= len).
 * Aborts the program on malloc failure. */
wchar_t *xwcsndup(const wchar_t *s, size_t len)
{
    len = xwcsnlen(s, len);

    wchar_t *result = xmalloce(len, 1, sizeof (wchar_t));
    result[len] = L'\0';
    return wmemcpy(result, s, len);
}

/* Converts the specified multibyte string into an integer value.
 * If successful, stores the integer value in `*resultp', sets `errno' to zero,
 * and returns true. Otherwise, sets `errno' to a non-zero error value and
 * returns false (the value of `*resultp' is undefined).
 * The conversion is considered successful if the string is not empty, the
 * `strtol' function does not set `errno' to non-zero, and there is no remaining
 * character after the value.
 * Spaces at the beginning of the string are ignored. */
bool xstrtoi(const char *s, int base, int *resultp)
{
    long result;
    char *endp;
    if (*s == '\0') {
	errno = EINVAL;
	return false;
    }
    errno = 0;
    result = strtol(s, &endp, base);
    if (errno != 0)
	return false;
    if (*endp != '\0') {
	errno = EINVAL;
	return false;
    }
    if (result < INT_MIN || result > INT_MAX) {
	errno = ERANGE;
	return false;
    }
    *resultp = (int) result;
    return true;
}

/* Converts the specified wide string into an integer value.
 * If successful, stores the integer value in `*resultp', sets `errno' to zero,
 * and returns true. Otherwise, sets `errno' to a non-zero error value and
 * returns false (the value of `*resultp' is undefined).
 * The conversion is considered successful if the string is not empty, the
 * `wcstol' function does not set `errno' to non-zero, and there is no remaining
 * character after the value.
 * Spaces at the beginning of the string are ignored. */
bool xwcstoi(const wchar_t *s, int base, int *resultp)
{
    long result;
    if (!xwcstol(s, base, &result))
	return false;
    if (result < INT_MIN || result > INT_MAX) {
	errno = ERANGE;
	return false;
    }
    *resultp = (int) result;
    return true;
}

/* The `long' version of `xwcstoi'. */
bool xwcstol(const wchar_t *s, int base, long *resultp)
{
    wchar_t *endp;
    if (*s == L'\0') {
	errno = EINVAL;
	return false;
    }
    errno = 0;
    *resultp = wcstol(s, &endp, base);
    if (errno != 0)
	return false;
    if (*endp != L'\0') {
	errno = EINVAL;
	return false;
    }
    return true;
}

/* The `unsigned long' version of `xwcstoi'. */
bool xwcstoul(const wchar_t *s, int base, unsigned long *resultp)
{
    wchar_t *endp;
    if (*s == L'\0') {
	errno = EINVAL;
	return false;
    }
    errno = 0;
    *resultp = wcstoul(s, &endp, base);
    if (errno != 0)
	return false;
    if (*endp != L'\0') {
	errno = EINVAL;
	return false;
    }
    return true;
}

/* If string `s' starts with `prefix', returns a pointer to the byte right after
 * the prefix in `s'. Otherwise, returns NULL.
 * This function does not change the value of `errno'. */
char *matchstrprefix(const char *s, const char *prefix)
{
    while (*prefix != '\0') {
	if (*prefix != *s)
	    return NULL;
	prefix++, s++;
    }
    return (char *) s;
}

/* If string `s' starts with `prefix', returns a pointer to the character right
 * after the prefix in `s'. Otherwise, returns NULL.
 * This function does not change the value of `errno'. */
wchar_t *matchwcsprefix(const wchar_t *s, const wchar_t *prefix)
{
    while (*prefix != L'\0') {
	if (*prefix != *s)
	    return NULL;
	prefix++, s++;
    }
    return (wchar_t *) s;
}

/* Same as `xwcsdup', except that the argument and the return value are of type
 * (void *). */
void *copyaswcs(const void *p)
{
    return xwcsdup(p);
}


/********** Error Utilities **********/

/* The name of the current shell process. This value is the first argument to
 * the main function of the shell. */
const wchar_t *yash_program_invocation_name;
/* The basename of `yash_program_invocation_name'. */
const wchar_t *yash_program_invocation_short_name;
/* The name of the currently executed built-in. */
const wchar_t *current_builtin_name = NULL;
/* The number of calls to the `xerror' function. */
/* This value is reset each time before a built-in is invoked. */
unsigned yash_error_message_count = 0;

/* Prints the specified error message to the standard error.
 * `format' is passed to `gettext' and the result is printed using `vfprintf'.
 * If `errno_' is non-zero, prints `strerror(errno_)' after the formatted
 * string.
 * `format' should not end with '\n' because this function automatically prints
 * a newline lastly. If (format == NULL && errno_ == 0), prints "unknown error".
 */
void xerror(int errno_, const char *restrict format, ...)
{
    yash_error_message_count++;
    fprintf(stderr, "%ls: ",
	    current_builtin_name != NULL
	    ? current_builtin_name
	    : yash_program_invocation_name);
    if (format == NULL && errno_ == 0)
	format = Ngt("unknown error");
    if (format != NULL) {
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, gt(format), ap);
	va_end(ap);
    }
    if (errno_ != 0) {
	fprintf(stderr,
		(format == NULL) ? "%s" : ": %s",
		strerror(errno_));
    }
    fputc('\n', stderr);
}

/* Prints a formatted string like `printf', but if failed to write to the
 * standard output, writes an error message to the standard error.
 * Returns true iff successful. When this function returns, the value of `errno'
 * may not be the one set by the `printf' function. */
/* The `format' string is not passed to `gettext'. */
bool xprintf(const char *restrict format, ...)
{
    va_list ap;
    int result;

    va_start(ap, format);
    result = vprintf(format, ap);
    va_end(ap);

    if (result >= 0) {
	return true;
    } else {
	xerror(errno, Ngt("cannot print to the standard output"));
	return false;
    }
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
