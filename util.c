/* Yash: yet another shell */
/* util.c: miscellaneous utility functions */
/* (C) 2007-2009 magicant */

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
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "exec.h"
#include "option.h"
#include "plist.h"
#include "util.h"


/********** General functions **********/

/* This function is called on memory allocation failure and
 * aborts the program after printing an error message. */
void alloc_failed(void)
{
    xerror(ENOMEM, NULL);
    exit(Exit_ERROR);
}


/********** String utilities **********/

#if !HAVE_STRNLEN
/* Returns min(maxlen, strlen(s)). */
size_t xstrnlen(const char *s, size_t maxlen)
{
    size_t result = 0;
    while (result < maxlen && s[result]) result++;
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
    while (result < maxlen && s[result]) result++;
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

    wchar_t *result = xmalloc((len + 1) * sizeof (wchar_t));
    result[len] = L'\0';
    return wmemcpy(result, s, len);
}

/* Converts the specified multi-byte string into an integer value.
 * The conversion result is stored in `*resultp'.
 * Returns true iff successful. On error, the value of `*resultp' is undefined.
 * The conversion is considered successful if the string is not empry, `errno'
 * is zero and there is no remaining character after the value.
 * `errno' is non-zero iff this function returns false.
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
 * The conversion result is stored in `*resultp'.
 * Returns true iff successful. On error, the value of `*resultp' is undefined.
 * The conversion is considered successful if the string is not empry, `errno'
 * is zero and there is no remaining character after the value.
 * `errno' is non-zero iff this function returns false.
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

/* Clones the specified NULL-terminated array of pointers.
 * Each pointer element is passed to function `copy' and the return value is
 * assigned to the new array element.
 * If the array contains more than `count' elements, only the first `count'
 * elements are copied. If the array contains elements fewer than `count', the
 * whole array is copied.
 * If `array' is NULL, simply returns NULL. */
/* `xstrdup' and `copyaswcs' are suitable for `copy'. */
void **duparrayn(void *const *array, size_t count, void *copy(const void *p))
{
    if (!array)
	return NULL;

    size_t realcount = 0;
    while (array[realcount] != NULL && realcount < count)
	realcount++;

    void **result = xmalloc((realcount + 1) * sizeof *result);
    for (size_t i = 0; i < realcount; i++)
	result[i] = copy(array[i]);
    result[realcount] = NULL;
    return result;
}

/* Joins the wide-character strings in the specified NULL-terminated array. The
 * array elements are considered pointers to wide strings.
 * If `padding' is non-NULL, `padding' is padded between each joined element.
 * Returns a newly malloced string. */
wchar_t *joinwcsarray(void *const *array, const wchar_t *padding)
{
    size_t elemcount, ccount = 0;

    /* count the full length of the resulting string */
    for (elemcount = 0; array[elemcount] != NULL; elemcount++)
	ccount += wcslen(array[elemcount]);
    if (padding != NULL && elemcount > 0)
	ccount += wcslen(padding) * (elemcount - 1);

    /* do copying */
    wchar_t *result = xmalloc((ccount + 1) * sizeof *result);
    wchar_t *s = result;
    for (size_t i = 0; i < elemcount; i++) {
	wchar_t *elem = array[i];
	while (*elem != L'\0')
	    *s++ = *elem++;
	if (i + 1 < elemcount) {
	    const wchar_t *pad = padding;
	    while (*pad)
		*s++ = *pad++;
	}
    }
    *s = L'\0';

    return result;
}

/* If string `s' starts with `prefix', returns a pointer to the byte right after
 * the prefix in `s'. Otherwise, returns NULL.
 * This function does not change the value of `errno'. */
char *matchstrprefix(const char *s, const char *prefix)
{
    while (*prefix != '\0') {
	if (*prefix != *s)
	    return NULL;
	prefix++;
	s++;
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
	prefix++;
	s++;
    }
    return (wchar_t *) s;
}

/* Same as `xwcsdup', except that the argument and the return value are cast to
 * (void *). */
void *copyaswcs(const void *p)
{
    return xwcsdup(p);
}

/* Comparison function used in `sort_mbs_array'. */
static int compare_mbs(const void *p1, const void *p2)
{
    return strcoll(*(const char *const *) p1, *(const char *const *) p2);
}

/* Sorts the specified NULL-terminated array of multibyte strings
 * according to the current locale. */
void sort_mbs_array(void **array)
{
    qsort(array, plcount(array), sizeof *array, compare_mbs);
}


/********** Error Utilities **********/

/* The name of the current shell process. This value is the first argument to
 * the main function of the shell. */
const wchar_t *yash_program_invocation_name;
/* The basename of `yash_program_invocation_name'. */
const wchar_t *yash_program_invocation_short_name;
/* The name of the currently executed builtin. */
const wchar_t *current_builtin_name = NULL;
/* The number of calls to the `xerror' function. */
/* This value is reset each time before a builtin is invoked. */
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
    va_list ap;

    yash_error_message_count++;
    fprintf(stderr, "%ls: ",
	    current_builtin_name
	    ? current_builtin_name
	    : yash_program_invocation_name);
    if (format) {
	va_start(ap, format);
	vfprintf(stderr, gt(format), ap);
	va_end(ap);
    }
    if (errno_) {
	if (format)
	    fputs(": ", stderr);
	fputs(strerror(errno_), stderr);
    }
    if (format || errno_) {
	fputc('\n', stderr);
    } else {
	fputs(gt("unknown error\n"), stderr);
    }
    fflush(stderr);
}


/********** xgetopt **********/

static void argshift(void **argv, int from, int to);

wchar_t *xoptarg;
int xoptind = 0;
wchar_t xoptopt;
bool xopterr = true;

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
 * This function is called repeatedly with the same arguments. Each time this
 * function is called, one option is parsed.
 * If a one-character option is parsed, the character is returned.
 * If a long option is parsed, the corresponding `val' is returned.
 * If an unknown option is encountered, L'?' is returned.
 * If there is no more option to be recognized, L'\0' is returned.
 *
 * `argv' is a pointer to a NULL-terminated array whose elements are pointers
 * to wide strings to be parsed. 
 * Elements in `argv' may be arranged in this function unless parsing is done
 * polixly correctly.
 *
 * `optstring' is a string that specifies the one-character options to be
 * recognized. If an option character is followed by a colon, the option is
 * considered to take an argument. For example, if `optstring' is L"ad:w", then
 * -a, -d and -w are recognized and -d takes an argument.
 * If option character is followed by two colons, the argument is optional.
 * Option characters should be alphanumeric.
 * If `optstring' starts with L'+', options are parsed in the POSIXly correct
 * way.
 * If `optstring' starts with L'*', one-character options may start with L'+'
 * instead of L'-', which is allowed in the "set" command.
 * If `optstring' starts with L'-', negative integers like "-1" are not treated
 * as options: if the hyphen is followed by a digit, it is treated as an
 * argument. This is not effective in the POSIXly correct mode.
 * If more than one of L'+', L'*' and L'-' are used at once, they must be
 * specified in this order.
 *
 * `longopts' is a pointer to an array of `struct xoption's that specify long
 * options to be recognized. The last element must be { 0, 0, 0, 0 }.
 * `longopts' may be NULL if no long option is to be recognized.
 * `longopts' is ignored in the POSIXly correct mode.
 *
 * When a long option is recognized and `longindex' is non-NULL, the index of
 * the corresponding "xoption" structure in array `longopts' is assigned to
 * `*longindex'.
 *
 * If the shell is in the POSIXly correct mode or `optstring' starts with the
 * L'+' flag, all options must precede operands, i.e., any argument after the
 * first operand is considered to be an operand. In this case, `argv' is not
 * arranged.
 * Otherwise, options and operands may be intermingled. After all the option was
 * parsed, `argv' is arranged so that all the options precede the operands
 * (unless there is a parse error).
 *
 * When all the options were parsed, this function returns L'\0' and `xoptind'
 * indicates the first operand in array `argv' and then this function must not
 * be called any more. (If there is no operand, `xoptind' indicates the
 * terminating NULL element of array `argv'.)
 * When a parse error occurred, this function returns L'?' and `xoptind'
 * indicates the erroneous option in array `argv' and the this function must not
 * be called any more.
 * `argv', `xoptind' and `xoptopt' must not be changed from outside this
 * function until parsing is done.
 *
 * An "xoption" structure contains the following members:
 *  name:      Name of the long option (without preceding "--"),
 *             which should consist only of alphanumeric characters and hyphens.
 *  has_arg:   One of `xno_argument', `xrequired_argument' and
 *             `xoptional_argument'.
 *             An option's argument is specified in the form of "--opt=arg".
 *             A required argument may be in the form of "--opt arg" instead.
 *  val:       When a long option is recognized, `val' is returned by this
 *             function.
 *
 * The usage of external variables with `xopt'-prefix is as follows:
 *  xoptind:  Index of the argument in `argv' to be parsed next.
 *            Must be initialized to 0 before starting parsing a new arguments.
 *  xoptarg:  When an option with an argument is parsed, `xoptarg' points to
 *            the first character of the argument. If the argument is optional
 *            and not given, `xoptarg' is NULL.
 *  xoptopt:  When an unknown one-character option is encountered, the
 *            character is assigned to `xoptopt'.
 *            When a one-character option is parsed properly, the prefix of the
 *            option (L'-' or L'+') is assigned.
 *            When a long option is parsed, L'-' is assigned.
 *  xopterr:  When an unknown option is encountered, if `xopterr' is true,
 *            an error message is printed to the standard error.
 */
/* This implementation is not fully compatible with GNU's getopt_long or
 * POSIX's getopt.
 * Some implementation has the `argreset' variable, which is assigned 1 before
 * starting new parsing to reset the state. In this implementation, the parse
 * state is reset when 0 is assigned to `xoptind'. */
wchar_t xgetopt_long(
	void **restrict argv,
	const wchar_t *restrict optstring,
	const struct xoption *restrict longopts,
	int *restrict longindex)
{
    static int aindex;
    bool optionsfirst = posixly_correct;
    bool plusoptions = false;
    bool ignorenegativenumbers = false;

    if (xoptind == 0) {  /* reset the state */
	aindex = 1;
	xoptind = 1;
    }
    if (*optstring == L'+') {
	optionsfirst = true;
	optstring++;
    }
    if (*optstring == L'*') {
	plusoptions = true;
	optstring++;
    }
    if (*optstring == L'-') {
	ignorenegativenumbers = !posixly_correct;
	optstring++;
    }

    int initind = xoptind;
    wchar_t *arg;
    while ((arg = argv[xoptind])) {
	if ((arg[0] != L'-' && (!plusoptions || (arg[0] != L'+')))
		|| arg[1] == L'\0'
		|| (ignorenegativenumbers && iswdigit(arg[1]))) {
	    /* arg is not an option */
	    if (optionsfirst)
		break;
	    xoptind++;
	    continue;
	}

	if (arg[0] == L'-' && arg[1] == L'-') {
	    /* arg starts with "--" */
	    goto try_parse_long_option;
	} else {
	    /* start parsing a one-character option */
	    goto try_parse_short_option;
	}
    }

    /* nothing more to parse! */
    xoptind = initind;
    return L'\0';

    wchar_t argchar;
try_parse_short_option:
    argchar = arg[aindex];
    xoptopt = arg[0];
    optstring = wcschr(optstring, argchar);
    if (!optstring) {
	xoptopt = argchar;
	goto no_such_option;
    }

    /* a valid option was found */
    if (optstring[1] == L':') {
	/* the option takes an argument */
	xoptarg = &arg[aindex + 1];
	aindex = 1;
	if (*xoptarg == L'\0' && optstring[2] != L':') {
	    /* the option argument is split from the option like "-x arg" */
	    xoptarg = argv[xoptind + 1];
	    if (!xoptarg)
		goto argument_missing;
	    argshift(argv, xoptind, initind);
	    argshift(argv, xoptind + 1, initind + 1);
	    xoptind = initind + 2;
	} else {
	    /* the option argument is in the same string like "-xarg" */
	    goto shift1s;
	}
    } else {
	/* the option doesn't take an argument */
	if (arg[aindex + 1] != L'\0') {
	    /* we have a next option in the same argument string */
	    aindex++;
	    argshift(argv, xoptind, initind);
	    xoptind = initind;
	} else {
	    /* no options are remaining in this argument string */
	    aindex = 1;
shift1s:
	    argshift(argv, xoptind, initind);
	    xoptind = initind + 1;
	}
    }
    return argchar;

    wchar_t *arg2;
try_parse_long_option:
    arg2 = &arg[2];
    if (arg2[0] == L'\0') {
	/* arg == "--" */
	argshift(argv, xoptind, initind);
	xoptind = initind + 1;
	return L'\0';
    }
    if (posixly_correct || !longopts)
	goto no_such_option;

    /* identify the name of the long option */
    int matchindex = -1;
    size_t len = wcscspn(arg2, L"=");
    for (int i = 0; longopts[i].name; i++) {
	if (wcsncmp(longopts[i].name, arg2, len) == 0) {
	    if (longopts[i].name[len] != L'\0') {
		/* partial match */
		if (matchindex < 0) {
		    /* first match */
		    matchindex = i;
		    continue;
		} else {
		    /* there was other match */
		    arg = arg2;
		    goto ambiguous_match;
		}
	    } else {
		/* full match */
		matchindex = i;
		break;
	    }
	}
    }
    if (matchindex < 0)
	goto no_such_option;

    /* a valid long option is found */
    if (longindex)
	*longindex = matchindex;
    xoptopt = L'-';
    if (longopts[matchindex].has_arg != xno_argument) {
	wchar_t *eq = wcschr(arg2, L'=');
	if (eq == NULL) {
	    if (longopts[matchindex].has_arg == xoptional_argument)
		goto shift1l;  /* the optional argument is not given */

	    /* the argument is split from option like "--option argument" */
	    xoptarg = argv[xoptind + 1];
	    if (xoptarg == NULL) {
		goto argument_missing;
	    } else {
		argshift(argv, xoptind, initind);
		argshift(argv, xoptind + 1, initind + 1);
		xoptind = initind + 2;
	    }
	} else {
	    /* the argument is specified after L'=' like "--option=argument" */
	    xoptarg = &eq[1];
	    goto shift1l;
	}
    } else {
	/* the option doesn't take an argument */
shift1l:
	argshift(argv, xoptind, initind);
	xoptind = initind + 1;
    }
    return longopts[matchindex].val;

ambiguous_match:
    if (xopterr) {
	fprintf(stderr, gt("%ls: --%ls: ambiguous option\n"),
		(wchar_t *) argv[0], arg);
#if 0
	for (int i = 0; longopts[i].name; i++)
	    if (matchstrprefix(longopts[i].name, arg))
		fprintf(stderr, "\t--%s\n", longopts[i].name);
#endif
    }
    return L'?';
no_such_option:
    if (xopterr)
	fprintf(stderr, gt("%ls: %ls: invalid option\n"),
		(wchar_t *) argv[0], (wchar_t *) argv[xoptind]);
    return L'?';
argument_missing:
    if (xopterr)
	fprintf(stderr, gt("%ls: %ls: argument missing\n"),
		(wchar_t *) argv[0], (wchar_t *) argv[xoptind]);
    return L'?';
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
