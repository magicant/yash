/* Yash: yet another shell */
/* input.c: functions for input of command line */
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
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "exec.h"
#include "expand.h"
#include "history.h"
#include "input.h"
#include "job.h"
#include "mail.h"
#include "option.h"
#include "parser.h"
#include "sig.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"
#include "yash.h"


/********** Input Functions **********/

static void exec_prompt_command(void);
static wchar_t *expand_ps1_posix(wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));
static wchar_t *expand_ps_yash(wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));

/* An input function that inputs from a multibyte string.
 * `inputinfo' is a pointer to a `struct input_mbs_info'.
 * Reads one line from `inputinfo->src', converts it into a wide string, and
 * appends it to the buffer `buf'. `inputinfo->state' must be a valid shift
 * state.
 * If more string is available after reading one line, `inputinfo->src' is
 * updated to point to the character to read next. If no more is available,
 * `inputinfo->src' is assigned NULL and `inputinfo->state' is a initial shift
 * state. */
int input_mbs(struct xwcsbuf_T *buf, void *inputinfo)
{
    struct input_mbs_info *info = inputinfo;
    size_t initbuflen = buf->length;
    size_t count;

    if (!info->src)
	return EOF;

    while (info->srclen > 0) {
	wb_ensuremax(buf, buf->length + 1);
	count = mbrtowc(buf->contents + buf->length,
		info->src, info->srclen, &info->state);
	switch (count) {
	    case 0:  /* read a null character */
		info->src = NULL;
		info->srclen = 0;
		return (buf->length == initbuflen) ? EOF : 0;
	    default:  /* read a non-null character */
		info->src += count;
		info->srclen -= count;
		if (buf->contents[buf->length++] == '\n') {
		    buf->contents[buf->length] = L'\0';
		    return 0;
		}
		break;
	    case (size_t) -2:  /* bytes are incomplete */
	    case (size_t) -1:  /* invalid bytes */
		goto err;
	}
    }
err:
    xerror(errno, Ngt("cannot convert multibyte characters "
		"into wide characters"));
    return EOF;

    /*
    wb_ensuremax(buf, buf->length + 120);
    count = mbsrtowcs(buf->contents + buf->length, &info->src,
	    buf->maxlength - buf->length + 1, &info->state);
    if (count == (size_t) -1) {
	xerror(errno, Ngt("cannot convert multibyte characters "
		    "into wide characters"));
	return EOF;
    }
    buf->length += count;
    return 0;
    */
}

/* An input function that inputs from a wide string.
 * `inputinfo' is a pointer to a `struct input_wcs_info'.
 * Reads one line from `inputinfo->src' and appends it to the buffer `buf'.
 * If more string is available after reading one line, `inputinfo->src' is
 * updated to point to the character to read next. If no more is available,
 * `inputinfo->src' is assigned NULL. */
int input_wcs(struct xwcsbuf_T *buf, void *inputinfo)
{
    struct input_wcs_info *info = inputinfo;
    const wchar_t *src = info->src;
    size_t count = 0;

    if (!src)
	return EOF;

    while (src[count] != L'\0' && src[count++] != L'\n');

    if (count == 0) {
	info->src = NULL;
	return EOF;
    } else {
	wb_ncat(buf, src, count);
	info->src = src + count;
	return 0;
    }
}

/* An input function that reads input from a file stream.
 * `inputinfo' is a pointer to a `FILE', which must be set to non-blocking.
 * Reads one line with `fgetws' and appends it to the buffer. */
int input_file(struct xwcsbuf_T *buf, void *inputinfo)
{
    FILE *f = inputinfo;
    int fd = fileno(f);
    size_t initlen = buf->length;

start:
    wb_ensuremax(buf, buf->length + 100);
    if (fgetws(buf->contents + buf->length, buf->maxlength - buf->length, f)) {
	size_t len = wcslen(buf->contents + buf->length);
	// `len' may be 0 if a null character is input
	buf->length += len;
	if (len > 0 && buf->contents[buf->length - 1] != L'\n')
	    goto start;
	else
	    goto end;
    } else {
	buf->contents[buf->length] = L'\0';
	if (feof(f)) {
	    clearerr(f);
	    goto end;
	}
	assert(ferror(f));
	switch (errno) {
	    case EINTR:
	    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
	    case EWOULDBLOCK:
#endif
		clearerr(f);
		wait_for_input(fd, true);
		goto start;
	    default:
		xerror(errno, Ngt("cannot read input"));
		goto end;
	}
    }
end:
    return (initlen == buf->length) ? EOF : 0;
}

/* An input function that reads input from the standard input.
 * Bytes are read one by one until a newline is encountered. No more bytes are
 * read after the newline.
 * This function does not use `inputinfo'.
 * The result is appended to the buffer. */
int input_stdin(struct xwcsbuf_T *buf, void *inputinfo __attribute__((unused)))
{
    size_t initlen = buf->length;
    read_line_from_stdin(buf, true);
    return (initlen == buf->length) ? EOF : 0;
}

/* Reads a line of input from the standard input.
 * Bytes are read one by one until a newline is encountered. No more bytes are
 * read after the newline.
 * If `trap' is true, traps are handled while reading.
 * The result is appended to the buffer.
 * Returns true iff successful. */
bool read_line_from_stdin(struct xwcsbuf_T *buf, bool trap)
{
    static bool initialized = false;
    static mbstate_t state;

    bool ok = true;

    if (!initialized) {
	initialized = true;
	memset(&state, 0, sizeof state);  /* initialize the state */
    }

    if (!set_nonblocking(STDIN_FILENO))
	return false;
    while (ok) {
	char c;
	ssize_t n = read(STDIN_FILENO, &c, 1);
	if (n < 0) switch (errno) {
	    case EINTR:
		break;
	    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
	    case EWOULDBLOCK:
#endif
		wait_for_input(STDIN_FILENO, trap);
		break;
	    default:
		xerror(errno, Ngt("cannot read input"));
		ok = false;
		break;
	} else if (n == 0) {
	    goto done;
	} else {
	    wchar_t wc;
	    switch (mbrtowc(&wc, &c, 1, &state)) {
		case 0:
		case 1:
		    wb_wccat(buf, wc);
		    if (wc == L'\n')
			goto done;
		    break;
		case (size_t) -2:
		    break;
		case (size_t) -1:
		    xerror(errno, Ngt("cannot read input"));
		    ok = false;
		    break;
		default:
		    assert(false);
	    }
	}
    }
done:
    unset_nonblocking(STDIN_FILENO);
    return ok;
}

/* An input function that prints a prompt and read input.
 * `inputinfo' is a pointer to a `struct input_readline_info'.
 * `inputinfo->type' must be between 1 and 4 inclusive and specifies the type of
 * the prompt. For example, PS1 is printed if `inputinfo->type' is 1.
 * If `inputinfo->type' is 1, this function changes its value to 2. */
int input_readline(struct xwcsbuf_T *buf, void *inputinfo)
{
    struct parsestate_T *state = save_parse_state();
    struct input_readline_info *info = inputinfo;
    if (do_job_control)
	print_job_status(PJS_ALL, true, false, stderr);
    if (info->type == 1) {
	exec_prompt_command();
	check_mail();
    }

    print_prompt(info->type);
    if (info->type == 1)
	info->type = 2;
    restore_parse_state(state);

    int result;
#if ENABLE_HISTORY
    size_t oldlen = buf->length;
#endif
    if (info->fp == stdin)
	result = input_stdin(buf, NULL);
    else
	result = input_file(buf, info->fp);
#if ENABLE_HISTORY
    if (info->type == 2)
	if (buf->contents[oldlen] != L'\n' && buf->length > oldlen) {
	    wchar_t savechar = buf->contents[buf->length - 1];
	    if (savechar == L'\n')
		buf->contents[buf->length - 1] = L'\0';
	    add_history(buf->contents + oldlen, false);
	    buf->contents[buf->length - 1] = savechar;
	}
#endif
    return result;
}

/* Executes the prompt command.
 * The parse state must be saved beforehand. */
void exec_prompt_command(void)
{
    if (!posixly_correct) {
	const wchar_t *pc = getvar(VAR_PROMPT_COMMAND);
	if (pc) {
	    /* PROMPT_COMMAND is executed as if a trap handler */
	    wchar_t *savepc = xwcsdup(pc);
	    savelaststatus = laststatus;
	    exec_wcs(savepc, VAR_PROMPT_COMMAND, false);
	    laststatus = savelaststatus;
	    savelaststatus = -1;
	    free(savepc);
	}
    }
}

/* Prints a prompt of the specified type.
 * `type' must be between 1 and 4 inclusive.
 * `save_parse_state' must be called before calling this function because this
 * function calls `parse_string'. */
void print_prompt(int type)
{
    const wchar_t *ps;
    switch (type) {
	case 1:   ps = getvar(VAR_PS1);   break;
	case 2:   ps = getvar(VAR_PS2);   break;
	case 3:   ps = getvar(VAR_PS3);   goto just_print;
	case 4:   ps = getvar(VAR_PS4);   break;
	default:  assert(false);
    }
    if (ps == NULL)
	return;

    wchar_t *prompt = parse_and_expand_string(ps, gt("prompt"), false);
    if (prompt == NULL)
	goto just_print;
    if (posixly_correct) {
	if (type == 1)
	    prompt = expand_ps1_posix(prompt);
    } else {
	if (type == 1 || type == 2)
	    prompt = expand_ps_yash(prompt);
    }
    fprintf(stderr, "%ls", prompt);
    fflush(stderr);
    free(prompt);
    return;

just_print:
    if (ps) {
	fprintf(stderr, "%ls", ps);
	fflush(stderr);
    }
}

/* Expands the contents of PS1 variable in the posixly correct way.
 * The argument is `free'd in this function.
 * The return value must be `free'd by the caller. */
/* In this function, "!" is expanded to the next history number and "!!" to "!"
 * if the history feature is enabled. Otherwise, this function simply returns
 * the argument. */
wchar_t *expand_ps1_posix(wchar_t *s)
{
#if ENABLE_HISTORY
    wchar_t *const saves = s;
    xwcsbuf_T buf;
    wb_init(&buf);

    while (*s) {
	if (*s == L'!') {
	    if (*++s == L'!') {
		wb_wccat(&buf, L'!');
	    } else {
		wb_wprintf(&buf, L"%d", hist_next_number);
		continue;
	    }
	} else {
	    wb_wccat(&buf, *s);
	}
	s++;
    }
    free(saves);
    return wb_towcs(&buf);
#else
    return s;
#endif
}

/* Expands the contents of PS1/PS2 in Yash's way.
 * The argument is `free'd in this function.
 * The return value must be `free'd by the caller. */
/* In this function, the following backslash escapes are expanded:
 *   \a    a bell character: L'\a' (L'\07')
 *   \e    an escape code: L'\033'
 *   \fX   change color
 *   \j    the number of jobs
 *   \n    newline: L'\n'
 *   \r    carriage return: L'\r'
 *   \!    next history number
 *   \$    L'#' if the effective uid is 0, L'$' otherwise
 *   \\    a backslash
 *   \[    start of substring not to be counted as printable characters
 *   \]    end of substring not to be counted as printable characters
 *
 * TODO expand_ps_yash: change color sequence
 * "X" in "\fX" is any number of flags from the following:
 *   (foreground color)
 *     k (black)    r (red)        g (green)    y (yellow)
 *     b (blue)     m (magenta)    c (cyan)     w (white)
 *   (background color)
 *     K R G Y B M C W
 *   d (default foreground/background color)
 *   s (standout)
 *   u (underline)
 *   v (reverse)
 *   n (blink)
 *   i (dim)
 *   o (bold)
 *   x (invisible)
 *   . (end: omittable)
 * */
wchar_t *expand_ps_yash(wchar_t *s)
{
    wchar_t *const saves = s;
    xwcsbuf_T buf;
    wb_init(&buf);

    while (*s) {
	if (*s != L'\\') {
	    wb_wccat(&buf, *s);
	} else switch (*++s) {
	case L'\0':   wb_wccat(&buf, L'\\');     goto done;
	//case L'\\':   wb_wccat(&buf, L'\\');     break;
	case L'a':    wb_wccat(&buf, L'\a');     break;
	case L'e':    wb_wccat(&buf, L'\033');   break;
	case L'n':    wb_wccat(&buf, L'\n');     break;
	case L'r':    wb_wccat(&buf, L'\r');     break;
	default:      wb_wccat(&buf, *s);        break;
	case L'$':    wb_wccat(&buf, geteuid() ? L'$' : L'#');  break;
	case L'j':    wb_wprintf(&buf, L"%zu", job_count());  break;
#if ENABLE_HISTORY
	case L'!':    wb_wprintf(&buf, L"%d", hist_next_number); break;
#endif
	case L'[':    // TODO  expand_ps_yash: \[ and \]
	case L']':    break;
	}
	s++;
    }
done:
    free(saves);
    return wb_towcs(&buf);
}

/* Sets O_NONBLOCK flag of the specified file descriptor.
 * If `fd' is negative, does nothing.
 * Returns true if successful, or false otherwise, with `errno' set. */
bool set_nonblocking(int fd)
{
    if (fd >= 0) {
	int flags = fcntl(fd, F_GETFL);
	if (flags < 0)
	    return false;
	if (!(flags & O_NONBLOCK))
	    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
    }
    return true;
}

/* Unsets O_NONBLOCK flag of the specified file descriptor.
 * If `fd' is negative, does nothing.
 * Returns true if successful, or false otherwise, with `errno' set. */
bool unset_nonblocking(int fd)
{
    if (fd >= 0) {
	int flags = fcntl(fd, F_GETFL);
	if (flags < 0)
	    return false;
	if (flags & O_NONBLOCK)
	    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) != -1;
    }
    return true;
}


/* vim: set ts=8 sts=4 sw=4 noet: */
