/* Yash: yet another shell */
/* util.c: miscellaneous utility functions */
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


#include "common.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
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
    exit(EXIT_ERROR);
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

    wchar_t *result = xmalloc((len + 1) * sizeof(wchar_t));
    result[len] = L'\0';
    return wmemcpy(result, s, len);
}

/* Clones a NULL-terminated array of pointers.
 * Each pointer element is passed to `copy' function and the return value is
 * assigned to the new array element. */
/* `xstrdup' and `copyaswcs' are suitable for `copy'. */
void **duparray(void *const *array, void *copy(const void *p))
{
    if (!array)
	return NULL;

    size_t count = 0;
    for (void *const *a = array; *a; a++)
	count++;

    void **result = xmalloc((count + 1) * sizeof *result);
    for (size_t i = 0; i < count; i++)
	result[i] = copy(array[i]);
    result[count] = NULL;
    return result;
}

/* Joins wide-character strings in the specified array whose elements are cast
 * from (wchar_t *) to (void *). If `padding' is non-NULL, the contents of
 * `padding' is padded between each joined element.
 * Returns a newly malloced string. */
wchar_t *joinwcsarray(void *const *array, const wchar_t *padding)
{
    size_t elemcount, ccount = 0;

    /* count the full length of the resulting string */
    for (elemcount = 0; array[elemcount]; elemcount++)
	ccount += wcslen(array[elemcount]);
    if (padding && elemcount > 0)
	ccount += wcslen(padding) * (elemcount - 1);

    /* do copying */
    wchar_t *result = xmalloc((ccount + 1) * sizeof *result);
    wchar_t *s = result;
    for (size_t i = 0; i < elemcount; i++) {
	wchar_t *elem = array[i];
	while (*elem)
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

/* If the string `s' starts with the `prefix', returns a pointer to the
 * byte right after the prefix in `s'. Otherwise returns NULL. */
char *matchstrprefix(const char *s, const char *prefix)
{
    while (*prefix) {
	if (*prefix != *s)
	    return NULL;
	prefix++;
	s++;
    }
    return (char *) s;
}

/* If the string `s' starts with the `prefix', returns a pointer to the
 * character right after the prefix in `s'. Otherwise returns NULL. */
wchar_t *matchwcsprefix(const wchar_t *s, const wchar_t *prefix)
{
    while (*prefix) {
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
    return strcoll(*(char *const *) p1, *(char *const *) p2);
}

/* Sorts a NULL-terminated array of multibyte strings
 * according to the current locale. */
void sort_mbs_array(void **array)
{
    qsort(array, plcount(array), sizeof *array, compare_mbs);
}


/********** Error utilities **********/

const wchar_t *yash_program_invocation_name;
const wchar_t *yash_program_invocation_short_name;
const wchar_t *current_builtin_name;
unsigned yash_error_message_count = 0;

/* Prints an error message to stderr.
 * `format' is passed to `gettext' and the result is printed to stderr using
 * `vfprintf'.  If `errno_' is non-zero, prints `strerror(errno_)' after the
 * formatted string.
 * `format' need not to end with '\n'. If (format == NULL && errno_ == 0),
 * prints "unknown error". */
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
 * Each time this function is called, one option is parsed.
 * If a one-character option is parsed, the character is returned.
 * If a long option is parsed, the corresponding `val' is returned.
 * If an unknown option is encountered, L'?' is returned.
 * If there is no more option to be recognized, L'\0' is returned.
 *
 * `argv' is a pointer to a NULL-terminated array whose elements are pointers
 * to wide strings to be parsed. 
 * Elements in `argv' may be sorted in this function.
 *
 * `optstring' is a string specifying one-character options to be recognized.
 * For example, if `optstring' is L"ad:w", -a, -d and -w are recognized and
 * -d takes an operand. If option character is followed by two colons,
 * the operand is optional. Option characters should be alphanumeric.
 * If `optstring' starts with L'+', options are parsed in posixly correct way.
 * If `optstring' starts with L'*', one-character options may start with L'+'
 * instead of L'-', which is allowed in "sh" and "set" command.
 * L'+' must precede L'*' when the both are specified.
 *
 * `longopts' is a pointer to an array of `struct xoption's specifying long
 * options to be recognized. The last element must be { 0, 0, 0, 0 }.
 * `longopts' may be NULL if no long option is to be recognized.
 * `longopts' is ignored if `posixly_correct' is true.
 *
 * When a long option is recognized and `longindex' is non-NULL,
 * the index of corresponding `struct xoption' in the `longopts' array is
 * assigned to `*longindex'.
 *
 * If `posixly_correct' is true or `optstring' starts with a L'+' flag,
 * all the options must be preceding the operands. `argv' is not arranged.
 * Otherwise options and operands may be mixed. After all the option is parsed,
 * `argv' is arranged so that all the options percede the operands (except when
 * there was a parse error).
 *
 * After this function returned L'\0', it must not be called any more and
 * `argv[xoptind]' points to the first operand.
 * After this function returned L'?', it must not be called any more and
 * `argv[xoptind]' points to the erroneous option.
 * After this function returned something otherwise, `argv[xoptind]' points to
 * the next argument to parse.
 * `argv', `xoptind' and `xoptopt' must not be changed from outside this
 * function during parsing.
 *
 * Here are the meaning of the members of `struct xoption':
 *  name:      Name of the long option (without preceding "--")
 *             that should consist only of alphanumeric characters and hyphens.
 *  has_arg:   One of `xno_argument', `xrequired_argument' and
 *             `xoptional_argument'.
 *             An argument is specified in the form of "--opt=arg".
 *             A required argument may be in the form of "--opt arg" instead.
 *  val:       When a long option is recognized, the corresponding `val' is
 *             returned.
 *
 * Here are the usage of external variables with `xopt'-prefix.
 *  xoptind:  Index of the argument in `argv' to be parsed next.
 *            Must be initialized to 0 before first call to the function.
 *  xoptarg:  When an option with an argument is parsed, `xoptarg' points to
 *            the first character of the argument. If an optional argument is
 *            not given, `xoptarg' is NULL.
 *  xoptopt:  When an unknown one-character option is encountered, the
 *            character is assigned to `xoptopt'.
 *            When a one-character option is parsed properly, L'-' or L'+' is
 *            assigned to `xoptopt' according to the prefix of the option.
 *            When a long option is parsed, L'-' is assigned.
 *  xopterr:  When an unknown option is encountered, if `xopterr' is true,
 *            an error message is printed to stderr.
 *
 * This implementation is not fully compatible with GNU's getopt_long or
 * POSIX's getopt.
 * Some implementation has `argreset' variable, which is assigned 1 before
 * starting new parsing to reset the state. In this implementation, the parse
 * state is reset when 0 is assigned to `xoptind'. */
wchar_t xgetopt_long(
	void **restrict argv,
	const wchar_t *restrict optstring,
	const struct xoption *restrict longopts,
	int *restrict longindex)
{
    int initind;
    wchar_t *arg, *arg2, argchar;
    static int aindex;
    bool optionsfirst = posixly_correct, plusoptions = false;

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

    initind = xoptind;
    while ((arg = argv[xoptind])) {
	if ((arg[0] != L'-' && (!plusoptions || (arg[0] != L'+'))) || !arg[1]) {
	    /* arg is not an option */
	    if (optionsfirst)
		break;
	    xoptind++;
	    continue;
	}

	if (arg[0] == L'-' && arg[1] == L'-') {
	    /* arg starts with "--" */
	    goto tryparselongoption;
	} else {
	    /* start parsing a one-character option */
	    goto tryparseshortoption;
	}
    }
    xoptind = initind;
    return L'\0';

tryparseshortoption:
    argchar = arg[aindex];
    xoptopt = arg[0];
    optstring = wcschr(optstring, argchar);
    if (!optstring) {
	xoptopt = argchar;
	goto nosuchoption;
    }

    /* a valid option is found */
    if (optstring[1] == L':') {
	/* the option takes an argument */
	xoptarg = &arg[aindex + 1];
	aindex = 1;
	if (!*xoptarg && optstring[2] != L':') {
	    /* the argument is split from the option like "-x arg" */
	    xoptarg = argv[xoptind + 1];
	    if (!xoptarg)
		goto argumentmissing;
	    argshift(argv, xoptind, initind);
	    argshift(argv, xoptind + 1, initind + 1);
	    xoptind = initind + 2;
	} else {
	    /* the argument is omitted */
	    argshift(argv, xoptind, initind);
	    xoptind = initind + 1;
	}
    } else {
	/* the option doesn't take an argument */
	if (arg[aindex + 1]) {
	    aindex++;
	    argshift(argv, xoptind, initind);
	    xoptind = initind;
	} else {
	    aindex = 1;
	    argshift(argv, xoptind, initind);
	    xoptind = initind + 1;
	}
    }
    return argchar;

tryparselongoption:
    arg2 = &arg[2];
    if (!arg2[0]) {
	/* arg == "--" */
	argshift(argv, xoptind, initind);
	xoptind = initind + 1;
	return L'\0';
    }
    if (posixly_correct || !longopts)
	goto nosuchoption;

    /* identify the name of the long option */
    int matchindex = -1;
    size_t len = wcscspn(arg2, L"=");
    for (int i = 0; longopts[i].name; i++) {
	if (wcsncmp(longopts[i].name, arg2, len) == 0) {
	    if (longopts[i].name[len]) {
		/* partial match */
		if (matchindex < 0) {
		    /* first match */
		    matchindex = i;
		    continue;
		} else {
		    /* there was other match */
		    arg = arg2;
		    goto ambiguousmatch;
		}
	    } else {
		/* full match */
		matchindex = i;
		break;
	    }
	}
    }
    if (matchindex < 0)
	goto nosuchoption;

    /* a valid long option is found */
    if (longindex)
	*longindex = matchindex;
    xoptopt = L'-';
    if (longopts[matchindex].has_arg) {
	wchar_t *eq = wcschr(arg2, L'=');
	if (!eq) {
	    /* the argument is split from option like "--option argument" */
	    xoptarg = argv[xoptind + 1];
	    if (!xoptarg) {
		if (longopts[matchindex].has_arg == xrequired_argument)
		    goto argumentmissing;
		argshift(argv, xoptind, initind);
		xoptind = initind + 1;
	    } else {
		argshift(argv, xoptind, initind);
		argshift(argv, xoptind + 1, initind + 1);
		xoptind = initind + 2;
	    }
	} else {
	    /* the argument is specified after L'=' like "--option=argument" */
	    xoptarg = eq + 1;
	    argshift(argv, xoptind, initind);
	    xoptind = initind + 1;
	}
    } else {
	/* the option doesn't take an argument */
	argshift(argv, xoptind, initind);
	xoptind = initind + 1;
    }
    return longopts[matchindex].val;

ambiguousmatch:
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
nosuchoption:
    if (xopterr)
	fprintf(stderr, gt("%ls: %ls: invalid option\n"),
		(wchar_t *) argv[0], (wchar_t *) argv[xoptind]);
    return L'?';
argumentmissing:
    if (xopterr)
	fprintf(stderr, gt("%ls: %ls: argument missing\n"),
		(wchar_t *) argv[0], (wchar_t *) argv[xoptind]);
    return L'?';
}


/* vim: set ts=8 sts=4 sw=4 noet: */
