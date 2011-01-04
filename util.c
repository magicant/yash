/* Yash: yet another shell */
/* util.c: miscellaneous utility functions */
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


#include "common.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
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
 *  has_arg:   One of `OPTARG_NONE', `OPTARG_REQUIRED' and `OPTARG_OPTIONAL'.
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

    /* a long option was identified */
    if (longindex)
	*longindex = matchindex;
    xoptopt = L'-';
    wchar_t *eq = wcschr(arg2, L'=');
    if (longopts[matchindex].arg != OPTARG_NONE) {
	if (eq == NULL) {
	    if (longopts[matchindex].arg == OPTARG_OPTIONAL)
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
	if (eq == NULL) {
shift1l:
	    argshift(argv, xoptind, initind);
	    xoptind = initind + 1;
	} else {
	    goto invalid_option_argument;
	}
    }
    return longopts[matchindex].val;

ambiguous_match:
    if (xopterr) {
	xerror(0, Ngt("option `%ls' is ambiguous"), (wchar_t *) argv[xoptind]);
#if 1
	for (int i = 0; longopts[i].name; i++)
	    if (matchwcsprefix(longopts[i].name, arg))
		fprintf(stderr, "\t--%ls\n", longopts[i].name);
#endif
    }
    return L'?';
no_such_option:
    if (xopterr)
	xerror(0, Ngt("`%ls' is not a valid option"),
		(wchar_t *) argv[xoptind]);
    return L'?';
argument_missing:
    if (xopterr)  // TODO: should be revised for better error message
	xerror(0, Ngt("%ls: argument missing"), (wchar_t *) argv[xoptind]);
    return L'?';
invalid_option_argument:
    if (xopterr)
	xerror(0, Ngt("%ls: this option does not take an argument"),
		(wchar_t *) argv[xoptind]);
    return L'?';
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
 *    arg:      One of the optarg_T values, which specify if the option takes an
 *              argument. */
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
    if (opt & XGETOPT_POSIX)
	goto no_such_option;

    /* identify the long option */
    const struct xgetopt_T *match = NULL;
    size_t namelen = wcscspn(&arg[2], L"=");
    for (; opts->shortopt != L'\0'; opts++) {
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
#if 1
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
