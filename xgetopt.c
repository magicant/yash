/* Yash: yet another shell */
/* xgetopt.c: command option parser */
/* (C) 2012 magicant */

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
#include "xgetopt.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>
#include <wctype.h>
#include "option.h"
#include "util.h"


static void argshift(void **argv, int from, int to)
    __attribute__((nonnull));
static struct xgetopt_T *no_such_option(
	const wchar_t *s, const struct xgetopt_T *opts)
    __attribute__((nonnull));
static struct xgetopt_T *option_argument_is_missing(
	bool shortopt, const struct xgetopt_T *opt)
    __attribute__((nonnull));
static struct xgetopt_T *sentinel(const struct xgetopt_T *opts)
    __attribute__((nonnull,pure));

wchar_t *xoptarg;
int xoptind = 0;

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
    return no_such_option(ARGV(xoptind), opts);

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
		return option_argument_is_missing(shortopt, opts);
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
	return no_such_option(ARGV(xoptind), opts);

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
	return no_such_option(ARGV(xoptind), opts);
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

ambiguous_long_option:
    xerror(0, Ngt("option `%ls' is ambiguous"), ARGV(xoptind));
#if LIST_AMBIGUOUS_OPTIONS
    fprintf(stderr, "\t--%ls\n", match->longopt);
    for (; opts->shortopt != L'\0'; opts++)
	if (opts->longopt != NULL
		&& wcsncmp(opts->longopt, &arg[2], namelen) == 0)
	    fprintf(stderr, "\t--%ls\n", opts->longopt);
#endif
    return sentinel(opts);
invalid_option_argument:
    xerror(0, Ngt("%ls: the --%ls option does not take an argument"),
	    ARGV(xoptind), opts->longopt);
    return sentinel(opts);
}

/* Reorders array elements. The `argv[from]' is moved to `argv[to]'.
 * Elements `argv[from+1]', `argv[from+2]', ..., `argv[to]' are moved to
 * `argv[from]', `argv[from+1]', ..., `argv[to-1]', respectively.
 * `from' must be equal to or less than `to'. */
void argshift(void **argv, int from, int to)
{
    void *s = argv[from];

    assert(from >= to);
    for (int i = from; i > to; i--)
	argv[i] = argv[i - 1];
    argv[to] = s;
}

/* Prints an error message that says that string `s' is not a valid option.
 * Returns the sentinel value that indicates an error. */
struct xgetopt_T *no_such_option(const wchar_t *s, const struct xgetopt_T *opts)
{
    xerror(0, Ngt("`%ls' is not a valid option"), s);
    return sentinel(opts);
}

/* Prints an error message that says that an option argument is missing.
 * `shortopt' must be true iff the erroneous option is a short option. 
 * `opt' is a pointer to the erroneous option in the array given to the
 * `xgetopt' function.
 * Returns the sentinel value that indicates an error. */
struct xgetopt_T *option_argument_is_missing(
	bool shortopt, const struct xgetopt_T *opt)
{
    if (shortopt)
	xerror(0, Ngt("the -%lc option requires an argument"),
		(wint_t) opt->shortopt);
    else
	xerror(0, Ngt("the --%ls option requires an argument"),
		opt->longopt);
    return sentinel(opt);
}

/* Returns a pointer to the sentinel element of the specified xgetopt_T array.
 * A sentinel element is the first element whose `shortopt' is L'\0', which
 * indicates the end of the array. */
struct xgetopt_T *sentinel(const struct xgetopt_T *opts)
{
    while (opts->shortopt != L'\0')
	opts++;
    return (struct xgetopt_T *) opts;
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
