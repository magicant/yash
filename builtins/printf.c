/* Yash: yet another shell */
/* printf.c: echo/printf builtins */
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


#include "../common.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "../builtin.h"
#include "../exec.h"
#include "../option.h"
#include "../strbuf.h"
#include "../util.h"
#include "../variable.h"
#include "printf.h"


#define VAR_ECHO_STYLE "ECHO_STYLE"

/* type of format data used in the "printf" builtin */
struct format_T {
    struct format_T *next;
    enum formattype_T {
	ft_none, ft_raw, ft_string, ft_char, ft_int, ft_uint, ft_float, ft_echo,
    } type;
    char *convspec;
};
/* One format_T structure represents either a fragment of literal string or a
 * conversion specification. For a string fragment, `type' member is `ft_raw'
 * and `convspec' member is the literal value. For a conversion specification,
 * `convspec' is a string that starts with "%". */
/* ft_string  -> wchar_t *
 * ft_char    -> wint_t
 * ft_int     -> intmax_t
 * ft_uint    -> uintmax_t
 * ft_float   -> long double
 * `ft_echo' corresponds to "%b". */

static bool echo_print_escape(const wchar_t *s, xstrbuf_T *buf)
    __attribute__((nonnull));
static bool printf_parse_format(
	const wchar_t *format, struct format_T **resultp)
    __attribute__((nonnull));
static struct format_T **printf_parse_percent(
	const wchar_t **formatp, struct format_T **resultp)
    __attribute__((nonnull));
static bool printf_printf(const struct format_T *format, const wchar_t *arg)
    __attribute__((nonnull(1)));
static uintmax_t printf_parse_integer(const wchar_t *arg, bool is_signed);
static void freeformat(struct format_T *f);


/* The "echo" builtin */
int echo_builtin(int argc, void **argv)
{
    bool nonewline, escape, noption, eoption;
    const wchar_t *echo_style;
    int index;

    /* Determine the behavior of "echo" according to $ECHO_STYLE.
     * The possible values for $ECHO_STYLE are:
     *   SYSV, XSI, BSD, GNU, ZSH, DASH, RAW
     * But we only care about the first character of it. */
    nonewline = false;
    echo_style = getvar(L VAR_ECHO_STYLE);
    switch (echo_style ? echo_style[0] : L'\0') {
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
    index = 1;
    if (eoption) {
	assert(noption);
	for (index = 1; index < argc; index++) {
	    if (ARGV(index)[0] != L'-')
		break;
	    if (ARGV(index)[wcsspn(ARGV(index) + 1, L"neE") + 1] != L'\0')
		break;
	    for (const wchar_t *opt = ARGV(index) + 1; *opt; opt++) {
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

    /* print arguments */
    clearerr(stdout);
    if (index < argc) {
	for (;;) {
	    if (escape) {
		xstrbuf_T buf;

		sb_init(&buf);
		nonewline |= echo_print_escape(ARGV(index), &buf);
		fputs(buf.contents, stdout);
		sb_destroy(&buf);
		if (nonewline)
		    break;
	    } else {
		printf("%ls", ARGV(index));
	    }

	    index++;
	    if (index >= argc)
		break;

	    fputc(' ', stdout);
	}
    }

    /* print trailing newline and flush the buffer */
    if (!nonewline)
	/* printing a newline automatically flushes the buffer */
	fputc('\n', stdout);
    else
	fflush(stdout);

    if (ferror(stdout)) {
	xerror(errno, Ngt("write error"));
	return Exit_FAILURE;
    }
    return Exit_SUCCESS;
}

/* Prints a string recognizing escape sequences.
 * Returns true iff "\c" sequence is found. */
bool echo_print_escape(const wchar_t *s, xstrbuf_T *buf)
{
    mbstate_t state;

    memset(&state, 0, sizeof state);  // initialize as a initial state
    while (*s) {
	if (*s != L'\\') {
normal:
	    sb_wccat(buf, *s, &state);
	    s++;
	} else {
	    switch (*(s + 1)) {
		char c;
		case L'a':   c = '\a';  goto print_char;
		case L'b':   c = '\b';  goto print_char;
		case L'c':   return true;
		case L'f':   c = '\f';  goto print_char;
		case L'n':   c = '\n';  goto print_char;
		case L'r':   c = '\r';  goto print_char;
		case L't':   c = '\t';  goto print_char;
		case L'v':   c = '\v';  goto print_char;
		case L'\\':  c = '\\';  goto print_char;
print_char:
		    sb_ccat(buf, c);
		    s += 2;
		    break;

		/* At most three digits are recognized in an octal escape
		 * except for the first zero. */
		case L'0':
		    s++;
		    /* falls thru! */
		case L'1':  case L'2':  case L'3':  case L'4':
		case L'5':  case L'6':  case L'7':
		    s++;
		    {
			int value = 0;
			if (L'0' <= *s && *s <= L'7') {
			    value = value * 8 + (*s - L'0');
			    s++;
			    if (L'0' <= *s && *s <= L'7') {
				value = value * 8 + (*s - L'0');
				s++;
				if (L'0' <= *s && *s <= L'7') {
				    value = value * 8 + (*s - L'0');
				    s++;
				}
			    }
			}
			sb_ccat(buf, TO_CHAR(value));
			break;
		    }

		default:
		    goto normal;
	    }
	}
    }
    sb_wcscat(buf, NULL, &state);
    return false;
}

const char echo_help[] = Ngt(
"echo - prints arguments\n"
"\techo [string...]\n"
"Prints <string>s, followed by a newline. The <string>s are each separated by\n"
"a space. This command does not recognize any options except described below.\n"
"\n"
"The behavior of \"echo\" depends on the value of $ECHO_STYLE variable:\n"
"  \"SYSV\" or \"XSI\" (default)\n"
"    always recognize escape sequences, but not any options.\n"
"  \"BSD\"\n"
"    recognize the -n option only.\n"
"  \"GNU\"\n"
"    recognize the -n, -e, -E options. Escape sequences are recognized if and\n"
"    only if enabled by the -e option.\n"
"  \"ZSH\"\n"
"    recognize the -n, -e, -E options. Escape sequences are recognized if and\n"
"    only if not disabled by the -E option.\n"
"  \"DASH\"\n"
"    recognize the -n option and escape sequences. Escape sequences cannot be\n"
"    disabled.\n"
"  \"RAW\"\n"
"    do not recognize any options nor escape sequences.\n"
"\n"
"Options:\n"
"  -n     do not print the last newline.\n"
"  -e     enable escape sequences.\n"
"  -E     disable escape sequences.\n"
"\n"
"Escape sequences:\n"
"  \\a     alert (bell)\n"
"  \\b     backspace\n"
"  \\c     print nothing more\n"
"  \\f     form feed\n"
"  \\n     newline\n"
"  \\r     carriage return\n"
"  \\t     tab\n"
"  \\v     vertical tab\n"
"  \\\\     backslash\n"
"  \\0nnn  8-bit value of the octal number <nnn>.\n"
);


static bool printf_error;

/* The "printf" builtin */
int printf_builtin(int argc, void **argv)
{
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"+", help_option, NULL))) {
	switch (opt) {
	    case L'-':
		print_builtin_help(ARGV(0));
		return Exit_SUCCESS;
	    default:  print_usage:
		fprintf(stderr, gt("Usage:  printf format [value...]\n"));
		return Exit_ERROR;
	}
    }
    if (xoptind == argc)
	goto print_usage;

    struct format_T *format = NULL;
    printf_error = false;
    clearerr(stdout);
    if (printf_parse_format(ARGV(xoptind), &format)) {
	int oldoptind;
	struct format_T *f;

	xoptind++;
	do {
	    oldoptind = xoptind;
	    f = format;
	    while (f) {
		if (printf_printf(f, ARGV(xoptind)))
		    goto end;
		f = f->next;
	    }
	} while (xoptind < argc && xoptind != oldoptind);
end:
	fflush(stdout);
	if (ferror(stdout)) {
	    xerror(errno, Ngt("write error"));
	    printf_error = true;
	}
    } else {
	printf_error = true;
    }
    freeformat(format);

    return printf_error ? Exit_FAILURE : Exit_SUCCESS;
}

/* Parses the format for the "printf" builtin.
 * A pointer to the result is assigned to `*resultp', which must be initialized
 * as NULL.
 * Returns true iff successful. If the format is invalid, false is returned,
 * in which case an error message is printed and a pointer to partial result is
 * assigned to `*resultp'. */
bool printf_parse_format(const wchar_t *format, struct format_T **resultp)
{
#define MAKE_STRING                                  \
    do {                                             \
	if (buf.length > 0) {                        \
	    struct format_T *f = xmalloc(sizeof *f); \
	    sb_wcscat(&buf, NULL, &state);           \
	    f->next = NULL;                          \
	    f->type = ft_raw;                        \
	    f->convspec = sb_tostr(&buf);            \
	    *resultp = f;                            \
	    resultp = &f->next;                      \
	} else                                       \
	    sb_destroy(&buf);                        \
    } while (0)

    xstrbuf_T buf;
    mbstate_t state;

    sb_init(&buf);
    memset(&state, 0, sizeof state);
    while (*format) {
	switch (*format) {
	case L'%':
	    MAKE_STRING;
	    resultp = printf_parse_percent(&format, resultp);
	    if (!resultp)
		return false;
	    sb_init(&buf);
	    break;
	case L'\\':
	    switch (*(format + 1)) {
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
		    format++;
		    {
			int value = 0;
			if (L'0' <= *format && *format <= L'7') {
			    value = value * 8 + (*format - L'0');
			    format++;
			    if (L'0' <= *format && *format <= L'7') {
				value = value * 8 + (*format - L'0');
				format++;
				if (L'0' <= *format && *format <= L'7') {
				    value = value * 8 + (*format - L'0');
				    format++;
				}
			    }
			}
			sb_ccat(&buf, (unsigned char) value);
			break;
		    }
		default:
		    goto normal;
	    }
	    break;
	default:  normal:
	    {
		if (!sb_wccat(&buf, *format, &state)) {
		    xerror(EILSEQ, Ngt("unexpected error"));
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

/* Parses a conversion specification starting with "%".
 * At first `*formatp' must point to L'%' and it is updated to point to the
 * next character to parse.
 * A pointer to the result is assigned to `*resultp', which must be initialized
 * as NULL.
 * Returns new `resultp' value iff successful. If the format is invalid, NULL is
 * returned, in which case an error message is printed and a pointer to partial
 * result is assigned to `*resultp'. */
struct format_T **printf_parse_percent(
	const wchar_t **formatp, struct format_T **resultp)
{
    /* A conversion specification consists of characters only from the basic
     * character set, so we can directly cast wchar_t into char. */

    const wchar_t *format = *formatp;
    xstrbuf_T buf;
    bool hashflag = false, zeroflag = false;
    enum formattype_T type;

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
		sb_ccat(&buf, (char) *format++);
		break;
	    default:
		goto parse_width;
	}
    }

parse_width:
    while (iswdigit(*format))
	sb_ccat(&buf, (char) *format++);

    /* parse precision */
    if (*format == L'.') {
	do {
	    sb_ccat(&buf, (char) *format++);
	} while (iswdigit(*format));
    }

    /* parse conversion specifier */
    switch (*format) {
	case L'd':  case L'i':
	    if (hashflag) goto flag_error;
	    type = ft_int;
	    sb_ccat(&buf, 'j');
	    break;
	case L'u':
	    if (hashflag) goto flag_error;
	    /* falls thru! */
	case L'o':  case L'x':  case L'X':
	    type = ft_uint;
	    sb_ccat(&buf, 'j');
	    break;
	case L'f':  case L'F': case L'e':  case L'E':  case L'g':  case L'G':
	    type = ft_float;
	    sb_ccat(&buf, 'L');
	    break;
	case L'c':
	    if (hashflag || zeroflag) goto flag_error;
	    type = ft_char;
	    sb_ccat(&buf, 'l');
	    break;
	case L's':
	    if (hashflag || zeroflag) goto flag_error;
	    type = ft_string;
	    sb_ccat(&buf, 'l');
	    break;
	case L'b':
	    if (hashflag || zeroflag) goto flag_error;
	    type = ft_echo;
	    sb_ccat(&buf, 's');
	    format++;
	    goto skip;
	case L'%':
	    if (buf.length != 1) goto conv_error;
	    type = ft_none;
	    break;
	case L'\0':
	    xerror(0, Ngt("conversion specifier missing"));
	    sb_destroy(&buf);
	    return NULL;
	default:  conv_error:
	    xerror(0, Ngt("`%lc': invalid conversion specifier"),
		    (wint_t) *format);
	    sb_destroy(&buf);
	    return NULL;
flag_error:
	    xerror(0, Ngt("invalid flag for conversion specifier `%lc'"),
		    (wint_t) *format);
	    sb_destroy(&buf);
	    return NULL;
    }
    sb_ccat(&buf, (char) *format++);

skip:;
    struct format_T *f = xmalloc(sizeof *f);
    f->next = NULL;
    f->type = type;
    f->convspec = sb_tostr(&buf);

    *formatp = format;
    *resultp = f;
    resultp = &f->next;
    return resultp;
}

/* Formats a string and prints it.
 * Increases `xoptind' if `arg' is used. Otherwise `arg' is ignored.
 * Returns true iff no more characters should be written. */
bool printf_printf(const struct format_T *format, const wchar_t *arg)
{
    switch (format->type) {
	case ft_none:
	    assert(strcmp(format->convspec, "%%") == 0);
	    putchar('%');
	    return false;
	case ft_raw:
	    assert(format->convspec[0] != '%');
	    printf("%s", format->convspec);
	    return false;
	case ft_string:
	    if (arg)
		xoptind++;
	    else
		arg = L"";
	    printf(format->convspec, arg);
	    return false;
	case ft_char:
	    if (arg) {
		printf(format->convspec, (wint_t) arg[0]);
		xoptind++;
	    }
	    return false;
	case ft_int:
	    printf(format->convspec, printf_parse_integer(arg, true));
	    return false;
	case ft_uint:
	    printf(format->convspec, printf_parse_integer(arg, false));
	    return false;
	case ft_float:
	    {
		long double value;
		wchar_t *end;

		if (arg)
		    xoptind++;
		else
		    arg = L"0";
		errno = 0;
		if (posixly_correct)
		    value = wcstod(arg, &end);
		else
		    value = wcstold(arg, &end);
		if (errno || !arg[0] || *end) {
		    xerror(errno, Ngt("`%ls' is not a valid number"), arg);
		    printf_error = true;
		}
		printf(format->convspec, value);
		return false;
	    }
	case ft_echo:
	    {
		xstrbuf_T buf;
		bool stop;

		if (arg)
		    xoptind++;
		else
		    arg = L"";
		sb_init(&buf);
		stop = echo_print_escape(arg, &buf);
		printf(format->convspec, buf.contents);
		sb_destroy(&buf);
		return stop;
	    }
	default:
	    assert(false);
    }
}

/* Parses an integer value. */
uintmax_t printf_parse_integer(const wchar_t *arg, bool is_signed)
{
    uintmax_t value;
    wchar_t *end = L"";

    if (arg)
	xoptind++;
    else
	arg = L"0";
    errno = 0;
    if (arg[0] == L'"' || arg[0] == L'\'')
	value = (uintmax_t) arg[1];
    else
	if (is_signed)
	    value = (uintmax_t) wcstoimax(arg, &end, 0);
	else
	    value = wcstoumax(arg, &end, 0);
    if (errno || !arg[0] || *end) {
	xerror(errno, Ngt("`%ls' is not a valid integer"), arg);
	printf_error = true;
    }
    return value;
}

/* Frees format data. */
void freeformat(struct format_T *f)
{
    while (f) {
	struct format_T *next = f->next;
	free(f->convspec);
	free(f);
	f = next;
    }
}

const char printf_help[] = Ngt(
"printf - print a formatted string\n"
"\tprintf format [argument...]\n"
"Prints a string formatted by <format>.\n"
"This command is very similar to the C programming language's \"printf\"\n"
"function. Each conversion specification starting with `%' in <format> is\n"
"substituted with the value of the corresponding <argument>. Escape sequences\n"
"starting with `\\' are also recognized.\n"
"If there are more <argument>s than specified in <format>, formatted strings\n"
"are repeatedly printed until all <argument>s are consumed. If there are\n"
"insufficient <argument>s, empty strings or zero values are assumed for the\n"
"missing arguments.\n"
"\n"
"Conversion specifications:\n"
"  %d    signed decimal integer\n"
"  %i    signed decimal integer\n"
"  %u    unsigned decimal integer\n"
"  %o    unsigned octal integer\n"
"  %x    unsigned hexadecimal integer (lowercase)\n"
"  %X    unsigned hexadecimal integer (uppercase)\n"
"  %f    floating point number (lowercase)\n"
"  %F    floating point number (uppercase)\n"
"  %e    floating point number with exponent (lowercase)\n"
"  %E    floating point number with exponent (uppercase)\n"
"  %g    %f or %e, automatically selected\n"
"  %G    %F or %E, automatically selected\n"
"  %c    first character of string\n"
"  %s    string\n"
"  %b    string (escape sequences recognized as \"echo\")\n"
"  %%    %\n"
"\n"
"Flags, field width, and precision can be specified in this order between\n"
"`%' and the conversion specifier. For example, `%08.3f' is a specifier for a\n"
"floating point number with the `0' flag, the field width of 8, and the\n"
"precision of 3.\n"
"\n"
"Escape sequences:\n"
"  \\a     alert (bell)\n"
"  \\b     backspace\n"
"  \\f     form feed\n"
"  \\n     newline\n"
"  \\r     carriage return\n"
"  \\t     tab\n"
"  \\v     vertical tab\n"
"  \\\\     backslash\n"
"  \\\"     double-quote\n"
"  \\'     single-quote\n"
"  \\nnn   8-bit value of the octal number <nnn>.\n"
);


/* vim: set ts=8 sts=4 sw=4 noet: */
