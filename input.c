/* Yash: yet another shell */
/* input.c: functions for input of command line */
/* (C) 2007-2017 magicant */

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
#include "input.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include "exec.h"
#include "expand.h"
#if YASH_ENABLE_HISTORY
# include "history.h"
#endif
#include "job.h"
#include "mail.h"
#include "option.h"
#include "parser.h"
#include "sig.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"
#include "yash.h"
#if YASH_ENABLE_LINEEDIT
# include "lineedit/display.h"
# include "lineedit/lineedit.h"
#endif


static wchar_t *expand_ps1_posix(wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));
static inline wchar_t get_euid_marker(void)
    __attribute__((pure));

/* An input function that inputs from a wide string.
 * `inputinfo' must be a pointer to a `struct input_wcs_info_T'.
 * Reads the next line from `inputinfo->src' and appends it to buffer `buf'.
 * `inputinfo->src' is assigned NULL if there are no more lines remaining. */
inputresult_T input_wcs(struct xwcsbuf_T *buf, void *inputinfo)
{
    struct input_wcs_info_T *info = inputinfo;
    const wchar_t *src = info->src;

    if (src == NULL)
	return INPUT_EOF;

    const wchar_t *newlinep = wcschr(src, L'\n');
    if (newlinep != NULL) {
	const wchar_t *nextlinep = &newlinep[1];
	wb_ncat_force(buf, src, nextlinep - src);
	info->src = nextlinep;
	return INPUT_OK;
    }

    bool isempty = (src[0] == L'\0');
    wb_cat(buf, src);
    info->src = NULL;
    return isempty ? INPUT_EOF : INPUT_OK;
}

/* An input function that reads input from a file stream.
 * `inputinfo' is a pointer to a `struct input_file_info_T'.
 * Reads one line from `inputinfo->fd' and appends it to the buffer. */
inputresult_T input_file(struct xwcsbuf_T *buf, void *inputinfo)
{
    return read_input(buf, inputinfo, true);
}

/* Reads one line from file descriptor `info->fd' and appends it to `buf'.
 * If `trap' is true, traps are handled while reading and the `sigint_received'
 * flag is cleared when this function returns.
 * Returns:
 *   INPUT_OK    if at least one character was appended
 *   INPUT_EOF   if reached the end of file without reading any characters
 *   INPUT_ERROR if an error occurred before reading any characters */
inputresult_T read_input(
	xwcsbuf_T *buf, struct input_file_info_T *info, bool trap)
{
    size_t initlen = buf->length;
    inputresult_T status = INPUT_EOF;

    for (;;) {
	if (info->bufpos >= info->bufmax) {
read_input:  /* if there's nothing in the buffer, read the next input */
	    switch (wait_for_input(info->fd, trap, -1)) {
		case W_READY:
		    break;
		case W_TIMED_OUT:
		    assert(false);
		case W_INTERRUPTED:
		    // Ignore interruption and continue reading, because:
		    //  1) POSIX does not require to do so, and
		    //  2) the buffer for canonical-mode editing cannot be
		    //     controlled from the shell.
		    goto read_input;
		case W_ERROR:
		    status = INPUT_ERROR;
		    goto end;
	    }

	    ssize_t readcount = read(info->fd, info->buf, info->bufsize);
	    if (readcount < 0) switch (errno) {
		case EINTR:
		case EAGAIN:
#if EAGAIN != EWOULDBLOCK
		case EWOULDBLOCK:
#endif
		    goto read_input;  /* try again */
		default:
		    goto error;
	    } else if (readcount == 0) {
		goto end;
	    }
	    info->bufpos = 0;
	    info->bufmax = readcount;
	}

	/* convert bytes in `info->buf' into a wide character and
	 * append it to `buf' */
	wb_ensuremax(buf, add(buf->length, 1));
	assert(info->bufpos < info->bufmax);
	size_t convcount = mbrtowc(&buf->contents[buf->length],
		&info->buf[info->bufpos], info->bufmax - info->bufpos,
		&info->state);
	switch (convcount) {
	    case 0:            /* read null character */
		goto end;
	    case (size_t) -1:  /* not a valid character */
		goto error;
	    case (size_t) -2:  /* needs more input */
		goto read_input;
	    default:
		info->bufpos += convcount;
		buf->contents[++buf->length] = L'\0';
		if (buf->contents[buf->length - 1] == L'\n')
		    goto end;
		break;
	}
    }

error:
    xerror(errno, Ngt("cannot read input"));
    status = INPUT_ERROR;
end:
    if (initlen != buf->length)
	return INPUT_OK;
    else
	return status;
}

/* An input function that prints a prompt and reads input.
 * `inputinfo' is a pointer to a `struct input_interactive_info'.
 * `inputinfo->type' must be either 1 or 2, which specifies the prompt type.
 * For example, $PS1 is printed if `inputinfo->type' is 1.
 * If `inputinfo->type' is 1, this function changes it to 2 after a successful
 * read.
 * The line is added to the history. */
inputresult_T input_interactive(struct xwcsbuf_T *buf, void *inputinfo)
{
    struct input_interactive_info_T *info = inputinfo;
    struct promptset_T prompt;

    if (info->prompttype == 1) {
	if (!posixly_correct)
	    exec_variable_as_auxiliary_(VAR_PROMPT_COMMAND);
	check_mail();
    }
    prompt = get_prompt(info->prompttype);
    if (do_job_control)
	print_job_status_all();
    /* Note: no commands must be executed between `print_job_status_all' here
     * and `le_readline', or the "notifyle" option won't work. More precisely,
     * `handle_sigchld' must not be called from any other function until it is
     * called from `wait_for_input' during the line-editing. */

#if YASH_ENABLE_LINEEDIT
    /* read a line using line editing */
    if (info->fileinfo->fd == STDIN_FILENO
	    && shopt_lineedit != SHOPT_NOLINEEDIT) {
	wchar_t *line;
	inputresult_T result;

	result = le_readline(prompt, true, &line);
	if (result != INPUT_ERROR) {
	    free_prompt(prompt);
	    if (result == INPUT_OK) {
		if (info->prompttype == 1)
		    info->prompttype = 2;
#if YASH_ENABLE_HISTORY
		add_history(line);
#endif
		wb_catfree(buf, line);
	    }
	    return result;
	}
    }
#endif /* YASH_ENABLE_LINEEDIT */

    /* read a line without line editing */
    print_prompt(prompt.main);
    print_prompt(prompt.styler);
    if (info->prompttype == 1)
	info->prompttype = 2;

    int result;
#if YASH_ENABLE_HISTORY
    size_t oldlen = buf->length;
#endif
    result = input_file(buf, info->fileinfo);

    print_prompt(PROMPT_RESET);
    free_prompt(prompt);

#if YASH_ENABLE_HISTORY
    add_history(buf->contents + oldlen);
#endif
    return result;
}

/* Returns the prompt string, possibly containing backslash escapes.
 * `type' must be 1, 2 or 4.
 * This function never fails: A newly malloced string is always returned.
 * `save_parse_state' must be called before calling this function because this
 * function calls `parse_string'. */
struct promptset_T get_prompt(int type)
{
    struct promptset_T result;
    wchar_t name[5] = { L'P', L'S', L'\0', L'\0', L'\0' };

    switch (type) {
	case 1:   name[2] = L'1';  break;
	case 2:   name[2] = L'2';  break;
	case 4:   name[2] = L'4';  break;
	default:  assert(false);
    }

    const wchar_t *ps = getvar(name);
    if (ps == NULL)
	ps = L"";

    wchar_t *prompt = parse_and_expand_string(ps, gt("prompt"), false);
    if (prompt == NULL)
	prompt = xwcsdup(L"");
    if (posixly_correct) {
	if (type == 1)
	    result.main = expand_ps1_posix(prompt);
	else
	    result.main = escapefree(prompt, L"\\");

	result.right  = xwcsdup(L"");
	result.styler = xwcsdup(L"");
    } else {
	result.main = prompt;

	name[3] = L'R';
	ps = getvar(name);
	if (ps == NULL)
	    ps = L"";
	prompt = parse_and_expand_string(ps, gt("prompt"), false);
	if (prompt == NULL)
	    prompt = xwcsdup(L"");
	result.right = prompt;

	name[3] = L'S';
	ps = getvar(name);
	if (ps == NULL)
	    ps = L"";
	prompt = parse_and_expand_string(ps, gt("prompt"), false);
	if (prompt == NULL)
	    prompt = xwcsdup(L"");
	result.styler = prompt;
    }

    return result;
}

/* Expands the contents of the PS1 variable in the posixly correct way.
 * The argument is `free'd in this function.
 * The return value must be `free'd by the caller. */
/* In this function, "!" is replaced with "\!", "!!" with "!", and "\" with
 * "\\". */
wchar_t *expand_ps1_posix(wchar_t *s)
{
    xwcsbuf_T buf;

    wb_init(&buf);
    for (size_t i = 0; s[i] != L'\0'; i++) {
	if (s[i] == L'\\') {
	    wb_wccat(&buf, L'\\');
	    wb_wccat(&buf, L'\\');
	} else if (s[i] == L'!') {
	    if (s[i + 1] == L'!') {
		wb_wccat(&buf, L'!');
		i++;
	    } else {
		wb_wccat(&buf, L'\\');
		wb_wccat(&buf, L'!');
	    }
	} else {
	    wb_wccat(&buf, s[i]);
	}
    }

    free(s);
    return wb_towcs(&buf);
}

/* Prints the specified prompt string to the standard error.
 * If the argument is NULL, the "sgr0" terminfo capability is printed, which
 * resets the terminal's font style.
 *
 * Escape sequences are handled:
 *   \a    a bell character: L'\a' (L'\07')
 *   \e    an escape code: L'\033'
 *   \j    the number of jobs
 *   \n    newline: L'\n'
 *   \r    carriage return: L'\r'
 *   \!    next history number
 *   \$    L'#' if the effective user ID is 0, L'$' otherwise
 *   \\    a backslash
 *
 * If line-editing is enabled, the followings are also available:
 *   \fX   change color
 *   \[    start of substring not to be counted as printable characters
 *   \]    end of substring not to be counted as printable characters
 * "X" in "\fX" can be any number of flags from the following:
 *   (foreground color)
 *     k (black)    r (red)        g (green)    y (yellow)
 *     b (blue)     m (magenta)    c (cyan)     w (white)
 *   (background color)
 *     K R G Y B M C W
 *   t (brighter color: used just after a color flag above)
 *   d (default foreground/background color)
 *   D (default foreground/background color and style)
 *   s (standout)
 *   u (underline)
 *   v (reverse)
 *   n (blink)
 *   i (dim)
 *   o (bold)
 *   x (invisible)
 *   . (end: omittable)
 * These are ignored in this function. */
void print_prompt(const wchar_t *s)
{
#if YASH_ENABLE_LINEEDIT
    if (le_try_print_prompt(s))
	return;
#endif

    xwcsbuf_T buf;

    wb_init(&buf);
    while (*s != L'\0') {
	if (*s != L'\\') {
	    wb_wccat(&buf, *s);
	} else switch (*++s) {
	    default:     wb_wccat(&buf, *s);       break;
	    case L'\0':  wb_wccat(&buf, L'\\');    goto done;
//	    case L'\\':  wb_wccat(&buf, L'\\');    break;
	    case L'a':   wb_wccat(&buf, L'\a');    break;
	    case L'e':   wb_wccat(&buf, L'\033');  break;
	    case L'n':   wb_wccat(&buf, L'\n');    break;
	    case L'r':   wb_wccat(&buf, L'\r');    break;
	    case L'$':   wb_wccat(&buf, get_euid_marker());      break;
	    case L'j':   wb_wprintf(&buf, L"%zu", job_count());  break;
#if YASH_ENABLE_HISTORY
	    case L'!':   wb_wprintf(&buf, L"%u", next_history_number());  break;
#endif
	    case L'[':
	    case L']':
		break;
	    case L'f':
		while (iswalnum(*++s));
		if (*s == L'.')
		    s++;
		continue;
	}
	s++;
    }
done:
    fprintf(stderr, "%ls", buf.contents);
    fflush(stderr);
    wb_destroy(&buf);
}

wchar_t get_euid_marker(void)
{
    return geteuid() == 0 ? L'#' : L'$';
}

/* Unsets O_NONBLOCK flag of the specified file descriptor.
 * If `fd' is negative, does nothing.
 * Returns true if successful. On error, `errno' is set and false is returned.*/
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


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
