/* Yash: yet another shell */
/* printf.c: the echo/printf built-ins */
/* (C) 2007-2019 magicant */

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


#include "../common.h"
#include "printf.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "../builtin.h"
#include "../exec.h"
#include "../option.h"
#include "../strbuf.h"
#include "../util.h"
#include "../variable.h"

#if HAVE_WCSTOLD && !defined(wcstold)
extern long double wcstold(const wchar_t *restrict s, wchar_t **restrict endp)
    __attribute__((nonnull(1)));
#endif


/* type of format data used in the "printf" built-in */
struct format_T {
    struct format_T *next;
    enum formattype_T {
	FT_NONE, FT_RAW, FT_STRING, FT_CHAR, FT_INT, FT_UINT, FT_FLOAT, FT_ECHO,
    } type;
    union {
	struct {
	    char *value;
	    size_t length;
	} raw;
	char *convspec;
	struct {
	    bool left;
	    unsigned long width, max;
	} echo;
    } value;
};
/* The FT_NONE format type corresponds to the "%%" conversion specification.
 * The FT_RAW format type is used for literal strings that are not conversion
 * specifications. The format types of FT_STRING, FT_CHAR, FT_INT, FT_UINT, and
 * FT_FLOAT are used for various types of conversion specifications (`convspec')
 * that require a value of the corresponding type.
 * The FT_ECHO format type is used for the "b" conversion specification. */
/* FT_STRING  -> wchar_t *
 * FT_CHAR    -> wint_t
 * FT_INT     -> intmax_t
 * FT_UINT    -> uintmax_t
 * FT_FLOAT   -> long double */

enum printf_result_T { PR_OK, PR_OK_END, PR_ERROR, };
static enum printf_result_T echo_parse_escape(const wchar_t *restrict s,
	xstrbuf_T *restrict buf, mbstate_t *restrict st)
    __attribute__((nonnull));
static bool printf_parse_format(
	const wchar_t *format, struct format_T **resultp)
    __attribute__((nonnull));
static struct format_T **printf_parse_percent(
	const wchar_t **formatp, struct format_T **resultp)
    __attribute__((nonnull,warn_unused_result));
static struct format_T *printf_parse_percent_b(xstrbuf_T *convspec)
    __attribute__((nonnull,malloc,warn_unused_result));
static enum printf_result_T printf_printf(
	const struct format_T *format, const wchar_t *arg, xstrbuf_T *buf)
    __attribute__((nonnull(1,3)));
static uintmax_t printf_parse_integer(const wchar_t *arg, bool is_signed);
static enum printf_result_T printf_print_escape(
	const struct format_T *format, const wchar_t *arg, xstrbuf_T *buf)
    __attribute__((nonnull));
static void freeformat(struct format_T *f);


/* The "echo" built-in. */
int echo_builtin(int argc, void **argv)
{
    bool nonewline, escape, noption, eoption;
    const wchar_t *echo_style;

    /* Determine the behavior of "echo" according to $ECHO_STYLE.
     * The possible values for $ECHO_STYLE are:
     *   SYSV, XSI, BSD, GNU, ZSH, DASH, RAW
     * But we only care about the first character of it. */
    nonewline = false;
    echo_style = getvar(L VAR_ECHO_STYLE);
    switch ((echo_style != NULL) ? echo_style[0] : L'\0') {
	case L'S':  case L's':  case L'X':  case L'x':  default:
	    escape = true, noption = false, eoption = false;
	    break;
	case L'B':  case L'b':
	    escape = false, noption = true, eoption = false;
	    break;
	case L'G':  case L'g':
	    escape = false, noption = true, eoption = true;
	    break;
	case L'Z':  case L'z':
	    escape = true, noption = true, eoption = true;
	    break;
	case L'D':  case L'd':
	    escape = true, noption = true, eoption = false;
	    break;
	case L'R':  case L'r':
	    escape = false, noption = false, eoption = false;
	    break;
    }

    /* parse options */
    int index = 1;
    if (eoption) {
	assert(noption);
	for (index = 1; index < argc; index++) {
	    if (ARGV(index)[0] != L'-' || ARGV(index)[1] == L'\0')
		break;
	    if (ARGV(index)[wcsspn(&ARGV(index)[1], L"neE") + 1] != L'\0')
		break;
	    for (const wchar_t *opt = &ARGV(index)[1]; *opt != L'\0'; opt++) {
		switch (*opt) {
		    case L'n':  nonewline = true;   break;
		    case L'e':  escape    = true;   break;
		    case L'E':  escape    = false;  break;
		    default:    assert(false);
		}
	    }
	}
    } else if (noption) {
	if (argc >= 2 && wcscmp(ARGV(index), L"-n") == 0) {
	    nonewline = true;
	    index++;
	}
    }

    /* parse arguments */
    xstrbuf_T buf;
    mbstate_t state;

    sb_init(&buf);
    memset(&state, 0, sizeof state); // initialize as the initial shift state

    if (index < argc) {
	for (;;) {
	    if (escape) {
		switch (echo_parse_escape(ARGV(index), &buf, &state)) {
		    case PR_OK:      break;
		    case PR_OK_END:  nonewline = true;  goto print;
		    case PR_ERROR:   goto error;
		}
	    } else {
		if (sb_wcscat(&buf, ARGV(index), &state) != NULL) {
		    errno = EILSEQ;
		    goto error;
		}
	    }

	    index++;
	    if (index >= argc)
		break;

	    if (!sb_wccat(&buf, L' ', &state))
		goto error;
	}
    }

    if (!nonewline)
	if (!sb_wccat(&buf, L'\n', &state))
	    goto error;

    /* print to the standard output */
print:
    clearerr(stdout);
    fwrite(buf.contents, sizeof *buf.contents, buf.length, stdout);
    if (ferror(stdout))
	goto error;

    if (fflush(stdout) != 0)
	goto error;

    sb_destroy(&buf);
    return Exit_SUCCESS;

error:
    xerror(errno, Ngt("cannot print to the standard output"));
    sb_destroy(&buf);
    return Exit_FAILURE;
}

/* Parses string `s' that may include escape sequences.
 * The result is appended to string buffer `buf'.
 * Shift state `st' is used to convert wide characters into multibyte
 * characters.
 * On error, `errno' is set and PR_ERROR is returned. */
enum printf_result_T echo_parse_escape(const wchar_t *restrict s,
	xstrbuf_T *restrict buf, mbstate_t *restrict st)
{
    while (*s != L'\0') {
	if (*s != L'\\') {
normal:
	    if (!sb_wccat(buf, *s, st))
		return PR_ERROR;
	    s++;
	} else {
	    switch (s[1]) {
		wchar_t c;
		case L'a':   c = L'\a';    goto print_char;
		case L'b':   c = L'\b';    goto print_char;
		case L'c':   return PR_OK_END;
		case L'e':   c = L'\033';  goto print_char;
		case L'f':   c = L'\f';    goto print_char;
		case L'n':   c = L'\n';    goto print_char;
		case L'r':   c = L'\r';    goto print_char;
		case L't':   c = L'\t';    goto print_char;
		case L'v':   c = L'\v';    goto print_char;
		case L'\\':  c = L'\\';    goto print_char;
print_char:
		    if (!sb_wccat(buf, c, st))
			return PR_ERROR;
		    s += 2;
		    break;

		/* At most three digits are recognized in an octal escape
		 * excluding the first zero. */
		case L'0':
		    {
			int value = 0;
			s += 2;
			for (int i = 0;
				i < 3 && L'0' <= *s && *s <= L'7';
				i++, s++)
			    value = value * 8 + (*s - L'0');
			sb_ccat(buf, TO_CHAR(value));
			break;
		    }

		default:
		    goto normal;
	    }
	}
    }
    return PR_OK;
}

#if YASH_ENABLE_HELP
const char echo_help[] = Ngt(
"print arguments"
);
const char echo_syntax[] = Ngt(
"\techo [string...]\n"
);
#endif


/* The "printf" built-in. */
int printf_builtin(int argc, void **argv)
{
    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, help_option, XGETOPT_POSIX)) != NULL) {
	switch (opt->shortopt) {
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return Exit_ERROR;
	}
    }
    if (xoptind == argc)
	return insufficient_operands_error(1);

    /* parse the format string */
    struct format_T *format = NULL;
    if (!printf_parse_format(ARGV(xoptind), &format)) {
	freeformat(format);
	return Exit_FAILURE;
    }
    xoptind++;

    /* format the operands */
    int oldoptind;
    xstrbuf_T buf;
    sb_init(&buf);
    do {
	oldoptind = xoptind;
	for (struct format_T *f = format; f != NULL; f = f->next) {
	    switch (printf_printf(f, ARGV(xoptind), &buf)) {
		case PR_OK:      break;
		case PR_OK_END:  goto print;
		case PR_ERROR:   goto error;
	    }
	}
    } while (xoptind < argc && xoptind != oldoptind);

print:
    freeformat(format);

    /* print the result to the standard output */
    clearerr(stdout);
    fwrite(buf.contents, sizeof *buf.contents, buf.length, stdout);
    if (ferror(stdout))
	goto error;

    if (fflush(stdout) != 0)
	goto error;

    sb_destroy(&buf);
    return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;

error:
    xerror(errno, Ngt("cannot print to the standard output"));
    sb_destroy(&buf);
    return Exit_FAILURE;
}

/* Parses the format for the "printf" built-in.
 * If successful, a pointer to the result is assigned to `*resultp' and true is
 * returned.
 * If unsuccessful, an error message is printed and false is returned. A pointer
 * to a partial result may be assigned to `*resultp'. */
bool printf_parse_format(const wchar_t *format, struct format_T **resultp)
{
#define MAKE_STRING                                  \
    do {                                             \
	if (buf.length > 0) {                        \
	    struct format_T *f = xmalloc(sizeof *f); \
	    sb_wccat(&buf, L'\0', &state);           \
	    f->next = NULL;                          \
	    f->type = FT_RAW;                        \
	    f->value.raw.length = buf.length;        \
	    f->value.raw.value = sb_tostr(&buf);     \
	    *resultp = f;                            \
	    resultp = &f->next;                      \
	} else                                       \
	    sb_destroy(&buf);                        \
    } while (0)

    xstrbuf_T buf;
    mbstate_t state;

    sb_init(&buf);
    memset(&state, 0, sizeof state);
    while (*format != L'\0') {
	switch (*format) {
	case L'%':
	    MAKE_STRING;
	    resultp = printf_parse_percent(&format, resultp);
	    if (resultp == NULL)
		return false;
	    sb_init(&buf);
	    break;
	case L'\\':
	    switch (format[1]) {
		char c;
		case L'a':   c = '\a';  goto put_char;
		case L'b':   c = '\b';  goto put_char;
		case L'f':   c = '\f';  goto put_char;
		case L'n':   c = '\n';  goto put_char;
		case L'r':   c = '\r';  goto put_char;
		case L't':   c = '\t';  goto put_char;
		case L'v':   c = '\v';  goto put_char;
		case L'\\':  c = '\\';  goto put_char;
		case L'\"':  c = '\"';  goto put_char;
		case L'\'':  c = '\'';  goto put_char;
put_char:
		    sb_ccat(&buf, c);
		    format += 2;
		    break;

		/* At most three digits are recognized in an octal escape */
		case L'0':  case L'1':  case L'2':  case L'3':
		case L'4':  case L'5':  case L'6':  case L'7':
		    {
			int value = 0;
			format++;
			for (int i = 0;
				i < 3 && L'0' <= *format && *format <= L'7';
				i++, format++)
			    value = value * 8 + (*format - L'0');
			sb_ccat(&buf, TO_CHAR(value));
			break;
		    }
		default:
		    goto normal;
	    }
	    break;
	default:  normal:
	    {
		if (!sb_wccat(&buf, *format, &state)) {
		    xerror(errno, Ngt("cannot parse the format"));
		    sb_destroy(&buf);
		    return false;
		}
		format++;
		break;
	    }
	}
    }
    MAKE_STRING;
    return true;

#undef MAKE_STRING
}

/* Parses the conversion specification that starts with L'%' pointed to by
 * `*formatp'.
 * If successful, a pointer to the character to parse next is assigned to
 * `*formatp', a pointer to the result is assigned to `*resultp', and the next
 * `resultp' value is returned.
 * If unsuccessful, an error message is printed and NULL is returned. A pointer
 * to a partial result may be assigned to `*resultp'. */
struct format_T **printf_parse_percent(
	const wchar_t **formatp, struct format_T **resultp)
{
    const wchar_t *format = *formatp;
    xstrbuf_T buf;
    bool hashflag = false, zeroflag = false;
    enum formattype_T type;
    struct format_T *result;
#ifdef __STDC_MB_MIGHT_NEQ_WC__
    mbstate_t state;
    memset(&state, 0, sizeof state);
# define BUFCAT(c) sb_wccat(&buf, c, &state)
#else
# define BUFCAT(c) sb_ccat(&buf, (char) (c))
#endif

    assert(*format == L'%');
    format++;
    sb_init(&buf);
    sb_ccat(&buf, '%');

    /* parse flags */
    for (;;) {
	switch (*format) {
	    case L'#':
		hashflag = true;
		goto add_char;
	    case L'0':
		zeroflag = true;
		goto add_char;
	    case L'-':  case L'+':  case L' ':  add_char:
		BUFCAT(*format++);
		break;
	    default:
		goto parse_width;
	}
    }

parse_width:
    while (iswdigit(*format))
	BUFCAT(*format++);

    /* parse precision */
    if (*format == L'.') {
	do {
	    BUFCAT(*format++);
	} while (iswdigit(*format));
    }

    /* parse conversion specifier */
    switch (*format) {
	case L'd':  case L'i':
	    if (hashflag) goto flag_error;
	    type = FT_INT;
	    sb_ccat(&buf, 'j');
	    break;
	case L'u':
	    if (hashflag) goto flag_error;
	    /* falls thru! */
	case L'o':  case L'x':  case L'X':
	    type = FT_UINT;
	    sb_ccat(&buf, 'j');
	    break;
	case L'f':  case L'F': case L'e':  case L'E':  case L'g':  case L'G':
	    type = FT_FLOAT;
	    sb_ccat(&buf, 'L');
	    break;
	case L'c':
	    if (hashflag || zeroflag) goto flag_error;
	    type = FT_CHAR;
	    sb_ccat(&buf, 'l');
	    break;
	case L's':
	    if (hashflag || zeroflag) goto flag_error;
	    type = FT_STRING;
	    sb_ccat(&buf, 'l');
	    break;
	case L'b':
	    if (hashflag || zeroflag) goto flag_error;
	    format++;
	    result = printf_parse_percent_b(&buf);
	    goto end;
	case L'%':
	    if (buf.length != 1) goto flag_error;
	    type = FT_NONE;
	    break;
	case L'\0':
	    xerror(0, Ngt("the conversion specifier is missing"));
	    sb_destroy(&buf);
	    return NULL;
	default:
	    xerror(0, Ngt("`%lc' is not a valid conversion specifier"),
		    (wint_t) *format);
	    sb_destroy(&buf);
	    return NULL;
flag_error:
	    xerror(0, Ngt("invalid flag for conversion specifier `%lc'"),
		    (wint_t) *format);
	    sb_destroy(&buf);
	    return NULL;
    }
    BUFCAT(*format++);

    result = xmalloc(sizeof *result);
    result->next = NULL;
    result->type = type;
    switch (type) {
	case FT_NONE:
	    sb_destroy(&buf);
	    break;
	case FT_RAW:
	case FT_ECHO:
	    assert(false);
	default:
	    result->value.convspec = sb_tostr(&buf);
	    break;
    }

end:
    *formatp = format;
    *resultp = result;
    return &result->next;

#undef BUFCAT
}

/* Parses the conversion specification given in buffer `convspec'.
 * The specification in the buffer must not have the conversion specifier, which
 * is assumed to be 'b'. The buffer is destroyed in this function. */
struct format_T *printf_parse_percent_b(xstrbuf_T *convspec)
{
    size_t index = 0;
    struct format_T *result = xmalloc(sizeof *result);

    result->next = NULL;
    result->type = FT_ECHO;
    result->value.echo.left = false;

    assert(convspec->contents[index] == '%');

    for (;;) {
	index++;
	switch (convspec->contents[index]) {
	    case '#':  case '0':  case '+':  case ' ':
		break;
	    case '-':
		result->value.echo.left = true;
		break;
	    default:
		goto parse_width;
	}
    }

    char *endp;
parse_width:
    result->value.echo.width = strtoul(convspec->contents + index, &endp, 10);
    index = endp - convspec->contents;

    if (convspec->contents[index] == '.') {
	index++;
	result->value.echo.max = strtoul(convspec->contents + index, &endp, 10);
	index = endp - convspec->contents;
    } else {
	result->value.echo.max = ULONG_MAX;
    }

    assert(index == convspec->length);
    sb_destroy(convspec);
    return result;
}

/* Formats the specified string. The result is appended to buffer `buf'.
 * Increases `xoptind' if `arg' is used. Otherwise, `arg' is ignored. */
enum printf_result_T printf_printf(
	const struct format_T *format, const wchar_t *arg, xstrbuf_T *buf)
{
    switch (format->type) {
	case FT_NONE:
	    sb_ccat(buf, '%');
	    return PR_OK;
	case FT_RAW:
	    sb_ncat_force(buf,
		    format->value.raw.value, format->value.raw.length);
	    return PR_OK;
	case FT_STRING:
	    if (arg != NULL)
		xoptind++;
	    else
		arg = L"";
	    if (sb_printf(buf, format->value.convspec, arg) < 0)
		return PR_ERROR;
	    return PR_OK;
	case FT_CHAR:
	    if (arg != NULL && arg[0] != L'\0') {
		xoptind++;
		if (sb_printf(buf, format->value.convspec, (wint_t) arg[0]) < 0)
		    return PR_ERROR;
	    }
	    return PR_OK;
	case FT_INT:
	    if (sb_printf(buf, format->value.convspec,
			printf_parse_integer(arg, true)) < 0)
		return PR_ERROR;
	    return PR_OK;
	case FT_UINT:
	    if (sb_printf(buf, format->value.convspec,
			printf_parse_integer(arg, false)) < 0)
		return PR_ERROR;
	    return PR_OK;
	case FT_FLOAT:
	    {
		long double value;
		wchar_t *end;

		if (arg != NULL)
		    xoptind++;
		else
		    arg = L"0";
		errno = 0;
#if HAVE_WCSTOLD
		if (!posixly_correct)
		    value = wcstold(arg, &end);
		else
#endif
		    value = wcstod(arg, &end);
		if (errno || arg[0] == L'\0' || *end != L'\0')
		    xerror(errno, Ngt("`%ls' is not a valid number"), arg);
		if (sb_printf(buf, format->value.convspec, value) < 0)
		    return PR_ERROR;
		return PR_OK;
	    }
	case FT_ECHO:
	    if (arg != NULL)
		xoptind++;
	    else
		arg = L"";
	    return printf_print_escape(format, arg, buf);
    }
    assert(false);
}

/* Parses the specified string as an integer. */
uintmax_t printf_parse_integer(const wchar_t *arg, bool is_signed)
{
    uintmax_t value;
    wchar_t *end;

    if (arg != NULL)
	xoptind++;
    else
	arg = L"0";
    if (arg[0] == L'"' || arg[0] == L'\'') {
	value = (uintmax_t) arg[1];
    } else {
	errno = 0;
	if (is_signed)
	    value = (uintmax_t) wcstoimax(arg, &end, 0);
	else
	    value = wcstoumax(arg, &end, 0);
	if (errno || arg[0] == L'\0' || *end != L'\0')
	    xerror(errno, Ngt("`%ls' is not a valid integer"), arg);
    }
    return value;
}

/* Prints the specified string that may include escape sequences and formats it
 * in the specified format. */
enum printf_result_T printf_print_escape(
	const struct format_T *format, const wchar_t *s, xstrbuf_T *buf)
{
    xstrbuf_T subbuf;
    mbstate_t state;

    sb_init(&subbuf);
    memset(&state, 0, sizeof state);

    enum printf_result_T result = echo_parse_escape(s, &subbuf, &state);

    if (result == PR_OK)
	sb_wccat(&subbuf, L'\0', &state);

    if (format->value.echo.max < subbuf.length)
	sb_truncate(&subbuf, format->value.echo.max);

    if (format->value.echo.width <= subbuf.length) {
	sb_ncat_force(buf, subbuf.contents, subbuf.length);
    } else {
	size_t increment = format->value.echo.width - subbuf.length;
	if (format->value.echo.left) {
	    sb_ncat_force(buf, subbuf.contents, subbuf.length);
	    sb_ccat_repeat(buf, ' ', increment);
	} else {
	    sb_ccat_repeat(buf, ' ', increment);
	    sb_ncat_force(buf, subbuf.contents, subbuf.length);
	}
    }

    sb_destroy(&subbuf);
    return result;
}

/* Frees the specified format data. */
void freeformat(struct format_T *f)
{
    while (f != NULL) {
	struct format_T *next = f->next;
	switch (f->type) {
	    case FT_NONE:
		break;
	    case FT_RAW:
		free(f->value.raw.value);
		break;
	    case FT_ECHO:
		break;
	    default:
		free(f->value.convspec);
		break;
	}
	free(f);
	f = next;
    }
}

#if YASH_ENABLE_HELP
const char printf_help[] = Ngt(
"print a formatted string"
);
const char printf_syntax[] = Ngt(
"\tprintf format [value...]\n"
);
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
