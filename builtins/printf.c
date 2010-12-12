/* Yash: yet another shell */
/* printf.c: the echo/printf builtins */
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


#include "../common.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
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

#if HAVE_WCSTOLD && !defined(wcstold)
extern long double wcstold(const wchar_t *restrict s, wchar_t **restrict endp)
    __attribute__((nonnull(1)));
#endif

/* type of format data used in the "printf" builtin */
struct format_T {
    struct format_T *next;
    enum formattype_T {
	ft_none, ft_raw, ft_string, ft_char, ft_int, ft_uint, ft_float, ft_echo,
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
/* The `ft_none' format type corresponds to the "%%" conversion specification.
 * The `ft_raw' format type is used for literal strings that are not conversion
 * specifications. The format types of `ft_string', `ft_char', `ft_int',
 * `ft_uint', and `ft_float' are used for various types of conversion
 * specifications (`convspec') that require a value of the corresponding type.
 * The `ft_echo' format type is used for the "b" conversion specification. */
/* ft_string  -> wchar_t *
 * ft_char    -> wint_t
 * ft_int     -> intmax_t
 * ft_uint    -> uintmax_t
 * ft_float   -> long double */

static bool echo_print_escape(const wchar_t *s, xstrbuf_T *buf)
    __attribute__((nonnull));
static bool printf_parse_format(
	const wchar_t *format, struct format_T **resultp)
    __attribute__((nonnull));
static struct format_T **printf_parse_percent(
	const wchar_t **formatp, struct format_T **resultp)
    __attribute__((nonnull,warn_unused_result));
static struct format_T *printf_parse_percent_b(xstrbuf_T *convspec)
    __attribute__((nonnull,malloc,warn_unused_result));
static bool printf_printf(const struct format_T *format, const wchar_t *arg)
    __attribute__((nonnull(1)));
static inline bool printf_print_raw(const struct format_T *format)
    __attribute__((nonnull));
static uintmax_t printf_parse_integer(const wchar_t *arg, bool is_signed);
static bool printf_print_escape(
	const struct format_T *format, const wchar_t *arg, xstrbuf_T *buf)
    __attribute__((nonnull));
static void freeformat(struct format_T *f);


/* The "echo" builtin */
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
    int index = 1;
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
		bool ok, end;
		size_t count;

		sb_init(&buf);
		nonewline |= end = echo_print_escape(ARGV(index), &buf);
		count = fwrite(buf.contents, sizeof (char), buf.length, stdout);
		ok = (count == buf.length);
		sb_destroy(&buf);
		if (!ok)
		    goto ioerror;
		if (end)
		    break;
	    } else {
		if (printf("%ls", ARGV(index)) < 0)
		    goto ioerror;
	    }

	    index++;
	    if (index >= argc)
		break;

	    if (fputc(' ', stdout) < 0)
		goto ioerror;
	}
    }

    /* print trailing newline and flush the buffer
     * (printing a newline automatically flushes the buffer) */
    if ((nonewline ? fflush(stdout) : putchar('\n')) < 0)
	goto ioerror;

    return Exit_SUCCESS;

ioerror:
    xerror(errno, Ngt("cannot print to standard output"));
    return Exit_FAILURE;
}

/* Prints the specified string that may include escape sequences.
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
		    {
			int value = 0;
			s += 2;
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
    sb_wccat(buf, L'\0', &state);
    return false;
}

#if YASH_ENABLE_HELP
const char echo_help[] = Ngt(
"echo - print arguments\n"
"\techo [string...]\n"
"The echo built-in prints <string>s followed by a newline to the standard\n"
"output. The <string>s are each separated by a space. This built-in does not\n"
"recognize any options except described below.\n"
"\n"
"The behavior of the echo built-in depends on the value of $ECHO_STYLE:\n"
"  `SYSV' or `XSI' (default)\n"
"    always recognize escape sequences but not any options.\n"
"  `BSD'\n"
"    recognize the -n option only.\n"
"  `GNU'\n"
"    recognize the -n, -e, -E options. Escape sequences are recognized if\n"
"    enabled by the -e option.\n"
"  `ZSH'\n"
"    recognize the -n, -e, -E options. Escape sequences are recognized unless\n"
"    disabled by the -E option.\n"
"  `DASH'\n"
"    recognize the -n option and escape sequences. Escape sequences cannot be\n"
"    disabled.\n"
"  `RAW'\n"
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
#endif /* YASH_ENABLE_HELP */


/* The "printf" builtin */
int printf_builtin(int argc, void **argv)
{
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"+", help_option, NULL))) {
	switch (opt) {
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:  print_usage:
		fprintf(stderr, gt("Usage:  printf format [value...]\n"));
		return Exit_ERROR;
	}
    }
    if (xoptind == argc)
	goto print_usage;

    struct format_T *format = NULL;
    if (!printf_parse_format(ARGV(xoptind), &format)) {
	freeformat(format);
	return Exit_FAILURE;
    }
    xoptind++;

    int oldoptind;
    do {
	oldoptind = xoptind;
	struct format_T *f = format;
	while (f) {
	    if (printf_printf(f, ARGV(xoptind)))
		goto end;
	    f = f->next;
	}
    } while (xoptind < argc && xoptind != oldoptind);

end:
    if (fflush(stdout) < 0)
	xerror(errno, Ngt("cannot print to standard output"));
    freeformat(format);

    return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;
}

/* Parses the format for the "printf" builtin.
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
	    f->type = ft_raw;                        \
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
	    format++;
	    result = printf_parse_percent_b(&buf);
	    goto end;
	case L'%':
	    if (buf.length != 1) goto conv_error;
	    type = ft_none;
	    break;
	case L'\0':
	    xerror(0, Ngt("the conversion specifier is missing"));
	    sb_destroy(&buf);
	    return NULL;
	default:  conv_error:
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
	case ft_none:
	    sb_destroy(&buf);
	    break;
	case ft_raw:
	case ft_echo:
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
    result->type = ft_echo;
    result->value.echo.left = false;

    assert(convspec->contents[index] == '%');
    index++;

    for (;; index++) {
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

/* Formats the specified string and prints it.
 * Increases `xoptind' if `arg' is used. Otherwise, `arg' is ignored.
 * Returns true iff no more characters should be written. */
bool printf_printf(const struct format_T *format, const wchar_t *arg)
{
    switch (format->type) {
	case ft_none:
	    if (putchar('%') < 0)
		goto ioerror;
	    return false;
	case ft_raw:
	    if (!printf_print_raw(format))
		goto ioerror;
	    return false;
	case ft_string:
	    if (arg != NULL)
		xoptind++;
	    else
		arg = L"";
	    if (printf(format->value.convspec, arg) < 0)
		goto ioerror;
	    return false;
	case ft_char:
	    if (arg) {
		xoptind++;
		if (printf(format->value.convspec, (wint_t) arg[0]) < 0)
		    goto ioerror;
	    }
	    return false;
	case ft_int:
	    if (printf(format->value.convspec,
			printf_parse_integer(arg, true)) < 0)
		goto ioerror;
	    return false;
	case ft_uint:
	    if (printf(format->value.convspec,
			printf_parse_integer(arg, false)) < 0)
		goto ioerror;
	    return false;
	case ft_float:
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
		if (printf(format->value.convspec, value) < 0)
		    goto ioerror;
		return false;
	    }
	case ft_echo:
	    {
		xstrbuf_T buf;
		bool ok, end;
		size_t count;

		if (arg != NULL)
		    xoptind++;
		else
		    arg = L"";
		sb_init(&buf);
		end = printf_print_escape(format, arg, &buf);
		count = fwrite(buf.contents, sizeof (char), buf.length, stdout);
		ok = (count == buf.length);
		sb_destroy(&buf);
		if (!ok)
		    goto ioerror;
		return end;
	    }
	default:
	    assert(false);
    }

ioerror:
    xerror(errno, Ngt("cannot print to standard output"));
    return true;
}

bool printf_print_raw(const struct format_T *format)
{
    size_t count = fwrite(format->value.raw.value, sizeof (char),
	    format->value.raw.length, stdout);
    return count == format->value.raw.length;
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
 * in the specified format.
 * Returns true iff "\c" sequence is found. */
bool printf_print_escape(
	const struct format_T *format, const wchar_t *s, xstrbuf_T *buf)
{
    bool end = echo_print_escape(s, buf);

    if (format->value.echo.max < buf->length) {
	sb_truncate(buf, format->value.echo.max);
    }
    if (buf->length < format->value.echo.width) {
	size_t increment = format->value.echo.width - buf->length;
	if (format->value.echo.left) {
	    sb_ccat_repeat(buf, ' ', increment);
	} else {
	    sb_ensuremax(buf, buf->length + increment);
	    memmove(buf->contents + increment, buf->contents, buf->length + 1);
	    buf->length += increment;
	    memset(buf->contents, ' ', increment);
	}
    }
    return end;
}

/* Frees the specified format data. */
void freeformat(struct format_T *f)
{
    while (f) {
	struct format_T *next = f->next;
	switch (f->type) {
	    case ft_none:
		break;
	    case ft_raw:
		free(f->value.raw.value);
		break;
	    case ft_echo:
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
"printf - print a formatted string\n"
"\tprintf format [argument...]\n"
"The printf built-in prints a string formatted by <format>.\n"
"This built-in is very similar to the C programming language's `printf'\n"
"function. Each conversion specification starting with `%' in <format> is\n"
"substituted with the value of the corresponding <argument>. Escape sequences\n"
"starting with `\\' are also recognized.\n"
"If there are more <argument>s than specified in <format>, formatted strings\n"
"are repeatedly printed until all <argument>s are consumed. If there are\n"
"insufficient <argument>s, an empty string or a value of zero is assumed for\n"
"the missing arguments.\n"
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
"  %b    string (escape sequences are recognized like by the echo built-in)\n"
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
#endif /* YASH_ENABLE_HELP */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
