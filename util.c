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
#include <wctype.h>
#include "exec.h"
#include "option.h"
#include "plist.h"


/********** General functions **********/

/* Computes `xmalloc(mainsize + count * elemsize)', but aborts the program if
 * the size is too large. `elemsize' must not be zero. */
size_t addmul(size_t mainsize, size_t count, size_t elemsize)
{
    assert(elemsize > 0);

    size_t arraysize = count * elemsize;
    size_t totalsize = mainsize + arraysize;
    if (arraysize / elemsize != count || totalsize < mainsize)
	alloc_failed();
    return totalsize;
}

/* This function is called on memory allocation failure and
 * aborts the program after printing an error message. */
void alloc_failed(void)
{
    xerror(ENOMEM, NULL);
    abort();
}


/********** String utilities **********/

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

/* Returns a newly malloced copy of the specified string.
 * The copy is at most `len' bytes long.
 * Returns an exact copy if (strlen(s) <= len).
 * Aborts the program on malloc failure. */
char *xstrndup(const char *s, size_t len)
{
    len = xstrnlen(s, len);

    char *result = xmalloc(len + 1);
    result[len] = '\0';
    return memcpy(result, s, len);
}

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

    wchar_t *result = xmallocn(len + 1, sizeof (wchar_t));
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


/********** xgetopt **********/

static void argshift(void **argv, int from, int to);

wchar_t *xoptarg;
int xoptind = 0;

/* Moves `argv[from]' to `argv[to]'. */
void argshift(void **argv, int from, int to /* <= from */)
{
    void *s = argv[from];

    assert(from >= to);
    for (int i = from; i > to; i--)
	argv[i] = argv[i - 1];
    argv[to] = s;
}

/* Parses options for a command.
 *
 * When starting parsing of a new set of arguments, firstly `xoptind' must be
 * set to 0. Then, this function must be repeatedly called with the same
 * arguments. One option is parsed for each call.
 *
 * `argv' is a pointer to a NULL-terminated array whose elements are pointers to
 * wide strings that are parsed. The array elements are reordered while parsing
 * so that all the options come before the operands unless doing the POSIX
 * parsing.
 * `opts' is a pointer to an array of xgetopt_T structures that specify options
 * accepted by the parser. The array must terminated by a xgetopt_T structure
 * whose `shortopt' member is L'\0'.
 * `opt' is bitwise OR of zero or more xgetoptopt_T flags that specify the way
 * parsing is done:
 *    XGETOPT_POSIX: Do the POSIX parsing, that is, parse options before the
 *                   first operand only.
 *    XGETOPT_DIGIT: Negative numbers like "-1" are not treated as options.
 * In the POSIXly correct mode, XGETOPT_POSIX is always assumed and
 * XGETOPT_DIGIT is ignored.
 *
 * When a valid option is parsed, this function returns a pointer to the
 * xgetopt_T structure (in the `opts' array) corresponding to the parsed option.
 * Moreover, if the option takes an argument, `xoptarg' is assigned a pointer to
 * the argument; Otherwise, `xoptarg' is set to NULL.
 *
 * When an option is parsed but it is not in the `opts' array or a required
 * option argument is missing, this function prints an error message and returns
 * a pointer to the last xgetopt_T structure in `opts', whose `shortopt' member
 * is L'\0'.
 *
 * When there is no more option, a NULL pointer is returned and `xoptind' is set
 * to the index of the first operand in the `argv' array. (If there are no
 * operands, `xoptind' is the index of the terminating NULL pointer in `argv'.)
 *
 * The xgetopt_T structure contains the following members:
 *    shortopt: A character representing a single-character option.
 *              If `shortopt' is L'-', it is not parsed as a single-char option.
 *    longopt:  A pointer to a string representing a long option (not including
 *              the preceding "--"). If `longopt' is a NULL pointer, it is not
 *              parsed as a long option.
 *    optarg:   One of the optarg_T values, which specify if the option takes an
 *              argument.
 *    posix:    In the POSIXly correct mode, this flag must be true for the
 *              option to be accepted.
 *    ptr:      Not used in the `xgetopt' function. */
struct xgetopt_T *xgetopt(
	void **restrict argv,
	const struct xgetopt_T *restrict opts,
	enum xgetoptopt_T opt)
{
    static int secondindex;

    bool shortopt;

    if (posixly_correct)
	opt = XGETOPT_POSIX;

    if (xoptind == 0)  /* reset the state */
	xoptind = secondindex = 1;

    int saveoptind = xoptind;
    const wchar_t *arg;
    while ((arg = ARGV(xoptind)) != NULL) {
	if (arg[0] == L'-') {
	    if (arg[1] == L'-')  /* `arg' starts with "--" */
		goto parse_long_option;
	    if (arg[1] != L'\0' && !((opt & XGETOPT_DIGIT) && iswdigit(arg[1])))
		goto parse_short_option;
	}

	/* `arg' is not a option */
	if (opt & XGETOPT_POSIX)
	    break;
	xoptind++;
    }

    /* no more options! */
    xoptind = saveoptind;
    return NULL;

parse_short_option:
    shortopt = true;
    assert((size_t) secondindex < wcslen(arg));
    if (arg[secondindex] != L'-')
	for (; opts->shortopt != L'\0'; opts++)
	    if (opts->posix || !posixly_correct)
		if (opts->shortopt == arg[secondindex])
		    goto short_option_found;
    goto no_such_option;

short_option_found:
    if (opts->optarg != OPTARG_NONE) {
	/* the option takes an argument */
	xoptarg = (wchar_t *) &arg[secondindex + 1];
	secondindex = 1;
	if (*xoptarg == L'\0' && opts->optarg == OPTARG_REQUIRED) {
	    /* the option argument is split from the option like "-x arg" */
split_option_argument:
	    xoptarg = ARGV(xoptind + 1);
	    if (xoptarg == NULL)
		goto argument_missing;
	    argshift(argv, xoptind, saveoptind);
	    argshift(argv, xoptind + 1, saveoptind + 1);
	    xoptind = saveoptind + 2;
	} else {
	    /* the option argument is in the same string like "-xarg" */
	    goto next_arg;
	}
    } else {
	/* the option doesn't take an argument */
	if (arg[secondindex + 1] != L'\0') {
	    /* we have a next option in the same argument string */
	    secondindex++;
	    argshift(argv, xoptind, saveoptind);
	    xoptind = saveoptind;
	} else {
	    /* no options are remaining in this argument string */
	    secondindex = 1;
next_arg:
	    argshift(argv, xoptind, saveoptind);
	    xoptind = saveoptind + 1;
	}
    }
    return (struct xgetopt_T *) opts;

parse_long_option:
    shortopt = false;
    if (arg[2] == L'\0') {  /* `arg' is "--" */
	argshift(argv, xoptind, saveoptind);
	xoptind = saveoptind + 1;
	return NULL;
    }
    if (posixly_correct)
	goto no_such_option;

    /* identify the long option */
    const struct xgetopt_T *match = NULL;
    size_t namelen = wcscspn(&arg[2], L"=");
    for (; opts->shortopt != L'\0'; opts++) {
	if (!opts->posix && posixly_correct)
	    continue;
	if (opts->longopt != NULL
		&& wcsncmp(opts->longopt, &arg[2], namelen) == 0) {
	    if (opts->longopt[namelen] == L'\0') {
		/* exact match */
		match = opts;
		break;
	    } else {
		/* partial match: `&arg[2]' starts with `opts->longopt' */
		if (match == NULL)
		    /* first partial match */
		    match = opts;
		else
		    /* more than one partial match */
		    goto ambiguous_long_option;
	    }
	}
    }
    if (match == NULL)
	goto no_such_option;
    opts = match;

    /* a long option was identified */
    const wchar_t *eq = &arg[2 + namelen];
    if (opts->optarg != OPTARG_NONE) {
	/* the option takes an argument */
	if (*eq == L'\0') {
	    /* no option argument in `arg' */
	    switch (opts->optarg) {
		case OPTARG_OPTIONAL:
		    /* the optional argument is not given */
		    goto next_arg;
		case OPTARG_REQUIRED:
		    /* the argument is split from the option
		     * like "--option argument" */
		    goto split_option_argument;
		default:
		    assert(false);
	    }
	} else {
	    /* the argument is specified after L'=' like "--option=argument" */
	    xoptarg = (wchar_t *) &eq[1];
	    goto next_arg;
	}
    } else {
	/* the option doesn't take an argument */
	if (*eq != L'\0')
	    goto invalid_option_argument;
	goto next_arg;
    }
    assert(false);

no_such_option:
    xerror(0, Ngt("`%ls' is not a valid option"), (wchar_t *) argv[xoptind]);
    goto error;
argument_missing:
    if (shortopt)
	xerror(0, Ngt("the -%lc option requires an argument"),
		(wint_t) opts->shortopt);
    else
	xerror(0, Ngt("the --%ls option requires an argument"),
		opts->longopt);
    goto error;
ambiguous_long_option:
    xerror(0, Ngt("option `%ls' is ambiguous"), ARGV(xoptind));
#if LIST_AMBIGUOUS_OPTIONS
    fprintf(stderr, "\t--%ls\n", match->longopt);
    for (; opts->shortopt != L'\0'; opts++)
	if (opts->longopt != NULL
		&& wcsncmp(opts->longopt, &arg[2], namelen) == 0)
	    fprintf(stderr, "\t--%ls\n", opts->longopt);
#endif
    goto error;
invalid_option_argument:
    xerror(0, Ngt("%ls: the --%ls option does not take an argument"),
	    ARGV(xoptind), opts->longopt);
    goto error;
error:
    while (opts->shortopt != L'\0')
	opts++;
    return (struct xgetopt_T *) opts;
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
