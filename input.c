/* Yash: yet another shell */
/* input.c: functions for input of command line */
/* (C) 2007-2011 magicant */

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
#include <wctype.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "exec.h"
#include "expand.h"
#if YASH_ENABLE_HISTORY
# include "history.h"
#endif
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
#if YASH_ENABLE_LINEEDIT
# include "lineedit/display.h"
# include "lineedit/lineedit.h"
#endif


/********** Input Functions **********/

#if YASH_ENABLE_LINEEDIT
static wchar_t *forward_line(wchar_t *linebuffer, xwcsbuf_T *buf)
    __attribute__((nonnull,malloc,warn_unused_result));
#endif
static wchar_t *expand_ps1_posix(wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));

/* An input function that inputs from a wide string.
 * `inputinfo' is a pointer to a `struct input_wcs_info'.
 * Reads one line from `inputinfo->src' and appends it to the buffer `buf'.
 * If more string is available after reading one line, `inputinfo->src' is
 * updated to point to the character to read next. If no more is available,
 * `inputinfo->src' is assigned NULL. */
inputresult_T input_wcs(struct xwcsbuf_T *buf, void *inputinfo)
{
    struct input_wcs_info *info = inputinfo;
    const wchar_t *src = info->src;
    size_t count = 0;

    if (!src)
	return INPUT_EOF;

    while (src[count] != L'\0' && src[count++] != L'\n');

    if (count == 0) {
	info->src = NULL;
	return INPUT_EOF;
    } else {
	wb_ncat(buf, src, count);
	info->src = src + count;
	return INPUT_OK;
    }
}

/* An input function that reads input from a file stream.
 * `inputinfo' is a pointer to a `struct input_file_info'.
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
 *   INPUT_EOF   if reached end of file without reading any characters
 *   INPUT_ERROR if an error occurred before reading any characters */
inputresult_T read_input(
	xwcsbuf_T *buf, struct input_file_info *info, bool trap)
{
    size_t initlen = buf->length;
    bool ok = true;

    if (trap) {
	handle_signals();
	reset_sigint();
    }

    for (;;) {
	if (info->bufpos >= info->bufmax) {
read_input:  /* if there's nothing in the buffer, read the next input */
	    if (!(ok = wait_for_input(info->fd, trap, -1)))
		goto end;

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
	wb_ensuremax(buf, buf->length + 1);
	assert(info->bufpos < info->bufmax);
	size_t convcount = mbrtowc(buf->contents + buf->length,
		info->buf + info->bufpos, info->bufmax - info->bufpos,
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
    ok = false;
end:
    if (initlen != buf->length)
	return INPUT_OK;
    else if (ok)
	return INPUT_EOF;
    else
	return INPUT_ERROR;
}

/* An input function that prints a prompt and reads input.
 * `inputinfo' is a pointer to a `struct input_interactive_info'.
 * `inputinfo->type' must be 1 or 2, specifying the type of the prompt.
 * For example, $PS1 is printed if `inputinfo->type' is 1.
 * If `inputinfo->type' is 1, this function changes it to 2. */
inputresult_T input_interactive(struct xwcsbuf_T *buf, void *inputinfo)
{
#if YASH_ENABLE_LINEEDIT
    /* An input function must not return more than one line at a time.
     * If line editing returns more than one line, this function returns only
     * the first line, saving the rest in this buffer. */
    static wchar_t *linebuffer = NULL;
    if (linebuffer) {
	linebuffer = forward_line(linebuffer, buf);
	return INPUT_OK;
    }
#endif

    struct parsestate_T *state = save_parse_state();
    struct input_interactive_info *info = inputinfo;
    struct promptset_T prompt;

    if (info->prompttype == 1)
	if (!posixly_correct)
	    exec_variable_as_commands(L VAR_PROMPT_COMMAND, VAR_PROMPT_COMMAND);
    prompt = get_prompt(info->prompttype);
    if (do_job_control)
	print_job_status_all();
    if (info->prompttype == 1)
	check_mail();
    restore_parse_state(state);
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

	result = le_readline(prompt, &line);
	if (result != INPUT_ERROR) {
	    free_prompt(prompt);
	    if (result == INPUT_OK) {
		if (info->prompttype == 1)
		    info->prompttype = 2;
#if YASH_ENABLE_HISTORY
		add_history(line);
#endif
		linebuffer = forward_line(line, buf);
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
    if (info->prompttype == 2)
	add_history(buf->contents + oldlen);
#endif
    return result;
}

#if YASH_ENABLE_LINEEDIT

/* Processes the line buffer.
 * If `linebuffer' contains one line, appends it to `buf' and returns NULL.
 * If `linebuffer' contains more than one line, only the first line is appended
 * and the rest is returned.
 * `linebuffer' is freed in this function anyway and the result is a newly
 * malloced buffer. */
wchar_t *forward_line(wchar_t *linebuffer, xwcsbuf_T *buf)
{
    wchar_t *nl = wcschr(linebuffer, L'\n');
    assert(nl != NULL);
    nl++;
    if (*nl == L'\0') {
	wb_catfree(buf, linebuffer);
	return NULL;
    } else {
	wb_ncat_force(buf, linebuffer, nl - linebuffer);
	return wmemmove(linebuffer, nl, wcslen(nl) + 1);
    }
}

#endif /* YASH_ENABLE_LINEEDIT */

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
    wchar_t *const saves = s;
    xwcsbuf_T buf;

    wb_init(&buf);
    while (*s != L'\0') {
	if (*s == L'\\') {
	    wb_wccat(wb_wccat(&buf, L'\\'), L'\\');
	} else if (*s == L'!') {
	    if (*(s + 1) == L'!') {
		wb_wccat(&buf, L'!');
		s++;
	    } else {
		wb_wccat(wb_wccat(&buf, L'\\'), L'!');
	    }
	} else {
	    wb_wccat(&buf, *s);
	}
	s++;
    }

    free(saves);
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
 *   \$    L'#' if the effective uid is 0, L'$' otherwise
 *   \\    a backslash
 *
 * If line-editing is enabled, the followings are also available:
 *   \fX   change color
 *   \[    start of substring not to be counted as printable characters
 *   \]    end of substring not to be counted as printable characters
 * "X" in "\fX" is any number of flags from the following:
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
	    case L'$':   wb_wccat(&buf, geteuid() ? L'$' : L'#');  break;
	    case L'j':   wb_wprintf(&buf, L"%zu", job_count());    break;
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


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
