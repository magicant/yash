/* Yash: yet another shell */
/* xgetopt.c: command option parser */
/* (C) 2012-2013 magicant */

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
#if LIST_AMBIGUOUS_OPTIONS
# include <stdio.h>
#endif
#include <wchar.h>
#include <wctype.h>
#include "option.h"
#include "util.h"


static struct xgetopt_T *search_argv(void);
static struct xgetopt_T *parse_short_option(void);
static struct xgetopt_T *found_short_option(
	const struct xgetopt_T *restrict opt)
    __attribute__((nonnull));
static struct xgetopt_T *parse_long_option(void);
static struct xgetopt_T *found_long_option(const wchar_t *eq,
	const struct xgetopt_T *restrict opt)
    __attribute__((nonnull));
static struct xgetopt_T *finish_parsing_current_argument(
	const struct xgetopt_T *restrict opt)
    __attribute__((nonnull));
static struct xgetopt_T *parse_separate_option_argument(bool shortopt,
	const struct xgetopt_T *restrict opt)
    __attribute__((nonnull));
static void argshift(void);
static struct xgetopt_T *done(void);
static struct xgetopt_T *no_such_option(const wchar_t *s)
    __attribute__((nonnull));
static struct xgetopt_T *ambiguous_long_option(const wchar_t *s, size_t namelen)
    __attribute__((nonnull));
static struct xgetopt_T *option_argument_is_missing(
	bool shortopt, const struct xgetopt_T *opt)
    __attribute__((nonnull));
static struct xgetopt_T *unwanted_long_option_argument(
	const wchar_t *s, const struct xgetopt_T *opt)
    __attribute__((nonnull));
static struct xgetopt_T *sentinel(void)
    __attribute__((pure));

/* When an option that takes an argument was parsed, a pointer to the argument
 * is assigned to this variable. */
wchar_t *xoptarg;

/* A pointer to the NULL-terminated array of pointers to wide strings that are
 * being parsed. */
static void **argv;

/* The index to array `argv' to which the next found option should be moved.
 * In other words, `xoptind - 1' is the index of the last parsed option string
 * (or its argument). */
int xoptind = 0;

/* The index of the element in array `argv' that should be parsed next. */
static int argvindex;

/* The index of the character in the currently parsed argument string that
 * should be parsed next. */
static int charindex;

/* An array of xgetopt_T's that specifies the set of options accepted by the
 * parser. */
static const struct xgetopt_T *opts;

/* A set of option flags for the current parsing. */
static enum xgetoptopt_T getoptopt;

/* Parses options for a command.
 *
 * When starting parsing of a new set of arguments, firstly `xoptind' must be
 * set to 0. Then, this function must be repeatedly called with the same
 * arguments. One option is parsed for each call.
 *
 * `argv_' is a pointer to a NULL-terminated array whose elements are pointers
 * to wide strings that are parsed. The array elements are reordered while
 * parsing so that all the options come before the operands unless doing the
 * POSIX parsing.
 * `opts_' is a pointer to an array of xgetopt_T structures that specify options
 * accepted by the parser. The array must terminated by a xgetopt_T structure
 * whose `shortopt' member is L'\0'.
 * `getoptopt_' is bitwise OR of zero or more xgetoptopt_T flags that specify
 * the way parsing is done:
 *    XGETOPT_POSIX: Do the POSIX parsing, that is, parse options before the
 *                   first operand only.
 *    XGETOPT_DIGIT: Negative numbers like "-1" are not treated as options.
 * In the POSIXly correct mode, XGETOPT_POSIX is always assumed and
 * XGETOPT_DIGIT is ignored.
 *
 * When a valid option is parsed, this function returns a pointer to the
 * xgetopt_T structure (in the `opts_' array) corresponding to the parsed
 * option.
 * Moreover, if the option takes an argument, `xoptarg' is assigned a pointer to
 * the argument; Otherwise, `xoptarg' is set to NULL.
 *
 * When an option is parsed but it is not in the `opts_' array or a required
 * option argument is missing, this function prints an error message and returns
 * a pointer to the last xgetopt_T structure in `opts_', whose `shortopt' member
 * is L'\0'.
 *
 * When there is no more option, a NULL pointer is returned and `xoptind' is set
 * to the index of the first operand in the `argv_' array. (If there are no
 * operands, `xoptind' is the index of the terminating NULL pointer in `argv_'.)
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
	void **restrict argv_,
	const struct xgetopt_T *restrict opts_,
	enum xgetoptopt_T getoptopt_)
{
    if (xoptind == 0) {
	argv = argv_;
	opts = opts_;
	getoptopt = getoptopt_;
	xoptind = argvindex = charindex = 1;  /* reset the state */
    }

    /* sanity check */
    assert(argv_ == argv);
    assert(opts_ == opts);
    assert(getoptopt_ == getoptopt);
    assert(xoptind <= argvindex);

    return search_argv();
}

/* Searches `argv' to find a candidate of an option. */
struct xgetopt_T *search_argv(void)
{
    enum xgetoptopt_T opt = posixly_correct ? XGETOPT_POSIX : getoptopt;

    const wchar_t *arg;
    for (; (arg = argv[argvindex]) != NULL; argvindex++) {
	if (arg[0] == L'-') {
	    if (arg[1] == L'-')  /* `arg' starts with "--" */
		return parse_long_option();
	    if (arg[1] != L'\0')
		if (!(opt & XGETOPT_DIGIT) || !iswdigit(arg[1]))
		    return parse_short_option();
	}

	/* `arg' is not a option */
	if (opt & XGETOPT_POSIX)
	    break;
    }

    /* no more options! */
    return done();
}

/* Parses `argv[argvindex]' as single-character options. */
struct xgetopt_T *parse_short_option(void)
{
    const wchar_t *arg = argv[argvindex];

    assert((size_t) charindex < wcslen(arg));

    /* A hyphen should not be regarded as an option because the hyphen character
     * is used as a sentinel in the xgetopt_T structure to indicate that a
     * single-character option is not available. */
    if (arg[charindex] == L'-')
	return no_such_option(arg);

    for (const struct xgetopt_T *opt = opts; opt->shortopt != L'\0'; opt++)
	if (opt->posix || !posixly_correct)
	    if (opt->shortopt == arg[charindex])
		return found_short_option(opt);

    return no_such_option(arg);
}

/* This function is called when a single-character option was found.
 * `opt' is a pointer to the option in the xgetopt_T array, which is returned by
 * this function unless an option argument is expected but missing. */
struct xgetopt_T *found_short_option(const struct xgetopt_T *restrict opt)
{
    const wchar_t *arg = argv[argvindex];

    if (opt->optarg == OPTARG_NONE) {
	/* the option doesn't take an argument */
	if (arg[charindex + 1] != L'\0') {
	    /* we have a next option in the same argument string */
	    charindex++;
	    return (struct xgetopt_T *) opt;
	} else {
	    /* no options are remaining in this argument string */
	    return finish_parsing_current_argument(opt);
	}
    } else {
	/* the option takes an argument */
	xoptarg = (wchar_t *) &arg[charindex + 1];
	if (*xoptarg == L'\0' && opt->optarg == OPTARG_REQUIRED) {
	    /* the option argument is split from the option like "-x arg" */
	    return parse_separate_option_argument(true, opt);
	} else {
	    /* the option argument is in the same string like "-xarg" */
	    return finish_parsing_current_argument(opt);
	}
    }
}

/* Parses `argv[argvindex]' as a long option. */
struct xgetopt_T *parse_long_option(void)
{
    const wchar_t *arg = argv[argvindex];

    if (arg[2] == L'\0') {  /* `arg' is "--" */
	argshift();
	return done();
    }

    if (posixly_correct)
	return no_such_option(arg);

    /* identify the long option */
    const struct xgetopt_T *match = NULL;
    size_t namelen = wcscspn(&arg[2], L"=");
    for (const struct xgetopt_T *opt = opts; opt->shortopt != L'\0'; opt++) {
	if (!opt->posix && posixly_correct)
	    continue;
	if (opt->longopt == NULL)
	    continue;
	if (wcsncmp(opt->longopt, &arg[2], namelen) != 0)
	    continue;

	if (opt->longopt[namelen] == L'\0') {
	    /* exact match */
	    match = opt;
	    break;
	}

	/* partial match: `&arg[2]' starts with `opt->longopt' */
	if (match == NULL)
	    /* first partial match */
	    match = opt;
	else
	    /* more than one partial match */
	    return ambiguous_long_option(arg, namelen);
    }
    if (match == NULL)
	return no_such_option(arg);

    return found_long_option(&arg[namelen + 2], match);
}

/* This function is called when a long option was found.
 * `eq' is a pointer to the first L'=' in `argv[argvindex]' (or the terminating
 * null character if there is no L'=').
 * `opt' is a pointer to the option in the xgetopt_T array, which is returned by
 * this function unless an option argument is expected but missing. */
struct xgetopt_T *found_long_option(const wchar_t *eq,
	const struct xgetopt_T *restrict opt)
{
    if (opt->optarg == OPTARG_NONE) {
	/* the option doesn't take an argument */
	if (*eq != L'\0')
	    return unwanted_long_option_argument(argv[argvindex], opt);
	return finish_parsing_current_argument(opt);
    }

    /* the option takes an argument */
    if (*eq != L'\0') {
	/* the argument is specified after L'=' like "--option=argument" */
	xoptarg = (wchar_t *) &eq[1];
	return finish_parsing_current_argument(opt);
    }

    /* no option argument in `argv[argvindex]' */
    switch (opt->optarg) {
	case OPTARG_OPTIONAL:
	    /* the optional argument is not given */
	    return finish_parsing_current_argument(opt);
	case OPTARG_REQUIRED:
	    /* the argument is split from the option like "--option argument" */
	    return parse_separate_option_argument(false, opt);
	case OPTARG_NONE:
	    assert(false);
    }
    assert(false);
}

/* This function is called when an option was parsed in an argument string and
 * the argument has no more options to be parsed.
 * Before calling this function, `argvindex' must be set to the index of the
 * argument in `argv' that was just parsed. This function moves
 * `argv[argvindex]' to `argv[xoptind]' using the `argshift' function and then
 * increments `argvindex' and `xoptind'.
 * `charindex' is reset to 1.
 * `opt' is the option that was parsed, which is returned by this function. */
struct xgetopt_T *finish_parsing_current_argument(
	const struct xgetopt_T *restrict opt)
{
    argshift();
    charindex = 1;
    return (struct xgetopt_T *) opt;
}

/* This function is called when an option that takes an argument was found and
 * the argument is separate from the option string.
 * `shortopt' must be true iff the option is a single-character option. 
 * `xoptarg' is set to `argv[argvindex+1]', which is supposed to be the option
 * argument. `argv[argvindex]' and `argv[argvindex+1]' are moved to
 * `argv[xoptind]' and `argv[xoptind+1]', respectively, and then `argvindex' and `xoptind' are each incremented by 2.
 * `charindex' is reset to 1.
 * `opt' is the option that was parsed, which is returned by this function
 * unless the argument is missing. */
struct xgetopt_T *parse_separate_option_argument(bool shortopt,
	const struct xgetopt_T *restrict opt)
{
    xoptarg = argv[argvindex + 1];
    if (xoptarg == NULL)
	return option_argument_is_missing(shortopt, opt);
    argshift();
    argshift();
    charindex = 1;
    return (struct xgetopt_T *) opt;
}

/* Reorders array elements. The `argv[argvindex]' is moved to `argv[xoptind]'.
 * Elements `argv[argvindex+1]', `argv[argvindex+2]', ..., `argv[xoptind]' are
 * moved to `argv[argvindex]', `argv[argvindex+1]', ..., `argv[xoptind-1]',
 * respectively. After reordering, `argvindex' and `xoptind' are each
 * incremented by 1. */
void argshift(void)
{
    void *s = argv[argvindex];

    assert(argvindex >= xoptind);
    for (int i = argvindex; i > xoptind; i--)
	argv[i] = argv[i - 1];
    argv[xoptind] = s;

    argvindex++, xoptind++;
}

/* This function is called when there is no more option to be parsed.
 * Returns NULL. */
struct xgetopt_T *done(void)
{
#ifndef NDEBUG
    argv = NULL;
    opts = NULL;
#endif
    return NULL;
}

/* Prints an error message that says that string `s' is not a valid option.
 * Returns the sentinel value that indicates an error. */
struct xgetopt_T *no_such_option(const wchar_t *s)
{
    xerror(0, Ngt("`%ls' is not a valid option"), s);
    return sentinel();
}

/* Prints an error message that says that string `s' is an ambiguous option.
 * `namelen' is the length of the long option name in `s', not including the
 * beginning "--", up to (but not including) L'=' or the end of the string.
 * Returns the sentinel value that indicates an error. */
struct xgetopt_T *ambiguous_long_option(const wchar_t *s, size_t namelen)
{
    xerror(0, Ngt("option `%ls' is ambiguous"), s);
#if LIST_AMBIGUOUS_OPTIONS
    for (const struct xgetopt_T *opt = opts; opt->shortopt != L'\0'; opt++)
	if (opt->longopt != NULL
		&& wcsncmp(opt->longopt, &s[2], namelen) == 0)
	    fprintf(stderr, "\t--%ls\n", opt->longopt);
#else
    (void) namelen;
#endif
    return sentinel();
}

/* Prints an error message that says that an option argument is missing.
 * `shortopt' must be true iff the erroneous option is a single-character
 * option. 
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
    return sentinel();
}

/* Prints an error message that says that a long option that does not take an
 * argument was specified with an argument.
 * `s' is a pointer to the erroneous argument string.
 * `opt' is a pointer to the erroneous option in the array given to the
 * `xgetopt' function.
 * Returns the sentinel value that indicates an error. */
struct xgetopt_T *unwanted_long_option_argument(
	const wchar_t *s, const struct xgetopt_T *opt)
{
    xerror(0, Ngt("%ls: the --%ls option does not take an argument"),
	    s, opt->longopt);
    return sentinel();
}

/* Returns a pointer to the sentinel element of the specified xgetopt_T array.
 * A sentinel element is the first element whose `shortopt' is L'\0', which
 * indicates the end of the array. */
struct xgetopt_T *sentinel(void)
{
    const struct xgetopt_T *opt = opts;

    while (opt->shortopt != L'\0')
	opt++;
    return (struct xgetopt_T *) opt;
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
