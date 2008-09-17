/* Yash: yet another shell */
/* redir.c: manages file descriptors and provides functions for redirections */
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
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>
#include "exec.h"
#include "expand.h"
#include "input.h"
#include "option.h"
#include "parser.h"
#include "path.h"
#include "redir.h"
#include "util.h"


/********** Utilities **********/

/* Closes a file descriptor surely.
 * If `close' returns EINTR, tries again.
 * If `close' returns EBADF, it is considered successful and silently ignored.
 * If `close' returns an error other than EINTR/EBADF, a message is printed. */
int xclose(int fd)
{
    while (close(fd) < 0) {
	switch (errno) {
	case EINTR:
	    continue;
	case EBADF:
	    return 0;
	default:
	    xerror(errno, Ngt("error in closing file descriptor %d"), fd);
	    return -1;
	}
    }
    return 0;
}

/* Performs `dup2' surely.
 * If `dup2' returns EINTR, tries again.
 * If `dup2' returns an error other than EINTR, an error message is printed.
 * `xclose' is called before `dup2'. */
int xdup2(int oldfd, int newfd)
{
    xclose(newfd);
    while (dup2(oldfd, newfd) < 0) {
	switch (errno) {
	case EINTR:
	    continue;
	default:
	    xerror(errno,
		    Ngt("cannot copy file descriptor %d to %d"),
		    oldfd, newfd);
	    return -1;
	}
    }
    return newfd;
}


/********** Shell FD **********/

static void reset_shellfdmin(void);


/* true iff stdin is redirected */
static bool is_stdin_redirected = false;

/* set of file descriptors used by the shell.
 * These file descriptors cannot be used by the user. */
static fd_set shellfds;
/* the minimum file descriptor that can be used for shell FD. */
static int shellfdmin;
/* the maximum file descriptor in `shellfds'.
 * `shellfdmax' is -1 if `shellfds' is empty. */
static int shellfdmax;

#ifndef SHELLFDMINMAX
#define SHELLFDMINMAX 100  /* maximum for `shellfdmin' */
#endif

/* file descriptor associated with the controlling terminal */
int ttyfd = -1;


/* Initializes shell FDs. */
void init_shellfds(void)
{
#ifndef NDEBUG
    static bool initialized = false;
    assert(!initialized);
    initialized = true;
#endif

    FD_ZERO(&shellfds);
    reset_shellfdmin();
    shellfdmax = -1;
}

/* Recomputes `shellfdmin'. */
void reset_shellfdmin(void)
{
    errno = 0;
    shellfdmin = sysconf(_SC_OPEN_MAX);
    if (shellfdmin == -1) {
	if (errno)
	    shellfdmin = 10;
	else
	    shellfdmin = SHELLFDMINMAX;
    } else {
	shellfdmin /= 2;
	if (shellfdmin > SHELLFDMINMAX)
	    shellfdmin = SHELLFDMINMAX;
	else if (shellfdmin < 10)
	    shellfdmin = 10;
    }
}

/* Adds the specified file descriptor (>= `shellfdmin') to `shellfds'. */
void add_shellfd(int fd)
{
    assert(fd >= shellfdmin);
    if (fd < FD_SETSIZE)
	FD_SET(fd, &shellfds);
    if (shellfdmax < fd)
	shellfdmax = fd;
}

/* Removes the specified file descriptor from `shellfds'.
 * Must be called BEFORE `xclose(fd)'. */
void remove_shellfd(int fd)
{
    if (0 <= fd && fd < FD_SETSIZE)
	FD_CLR(fd, &shellfds);
    if (fd == shellfdmax) {
	shellfdmax = fd - 1;
	while (shellfdmax >= 0 && !FD_ISSET(shellfdmax, &shellfds))
	    shellfdmax--;
    }
}
/* The argument to `FD_CLR' must be a valid (open) file descriptor. This is why
 * `remove_shellfd' must be called before closing the file descriptor. */

/* Checks if the specified file descriptor is in `shellfds'. */
bool is_shellfd(int fd)
{
    return fd >= FD_SETSIZE || (fd >= 0 && FD_ISSET(fd, &shellfds));
}

/* Closes all file descriptors in `shellfds' and empties it.
 * If `leavefds' is true, the file descriptors are actually not closed. */
void clear_shellfds(bool leavefds)
{
    if (!leavefds)
	for (int fd = 0; fd <= shellfdmax; fd++)
	    if (FD_ISSET(fd, &shellfds))
		xclose(fd);
    FD_ZERO(&shellfds);
    shellfdmax = -1;
    ttyfd = -1;
}

/* Duplicates the specified file descriptor as a new shell FD.
 * The new FD is added to `shellfds'.
 * On error, `errno' is set and -1 is returned. */
int copy_as_shellfd(int fd)
{
    int newfd = fcntl(fd, F_DUPFD, shellfdmin);
    if (newfd >= 0) {
	fcntl(newfd, F_SETFD, FD_CLOEXEC);
	add_shellfd(newfd);
    }
    return newfd;
}

/* Duplicates the underlining file descriptor of the specified stream.
 * The original stream is closed whether successful or not.
 * A new stream is open with the new FD using `fdopen' and returned.
 * The new stream's underlining file descriptor is registered as a shell FD and
 * set to non-blocking.
 * If NULL is given, this function just returns NULL without doing anything. */
FILE *reopen_with_shellfd(FILE *f, const char *mode)
{
    if (!f)
	return NULL;

    int newfd = copy_as_shellfd(fileno(f));
    fclose(f);
    if (newfd < 0 || !set_nonblocking(newfd))
	return NULL;
    else
	return fdopen(newfd, mode);
}

/* Opens `ttyfd' */
void open_ttyfd(void)
{
    if (ttyfd < 0) {
	int fd = open("/dev/tty", O_RDWR);
	if (fd >= 0) {
	    ttyfd = copy_as_shellfd(fd);
	    xclose(fd);
	}
	if (ttyfd < 0) {
	    xerror(errno, Ngt("cannot open `%s'"), "/dev/tty");
	    xerror(0, Ngt("job control disabled"));
	    do_job_control = false;
	}
    }
}


/********** Redirections **********/

/* info used to undo redirection */
struct savefd_T {
    struct savefd_T *next;
    int   sf_origfd;            /* original file descriptor */
    int   sf_copyfd;            /* copied file descriptor */
    bool  sf_stdin_redirected;  /* original `is_stdin_redirected' */
};

static void save_fd(int oldfd, savefd_T **save);
static int parse_and_check_dup(char *num, redirtype_T type)
    __attribute__((nonnull));


/* Opens a redirection.
 * If `save' is non-NULL, the original FD is saved and a pointer to the info is
 * assigned to `*save' (whether successful or not).
 * Returns true iff successful. */
bool open_redirections(const redir_T *r, savefd_T **save)
{
    if (save)
	*save = NULL;

    while (r) {
	if (r->rd_fd < 0 || is_shellfd(r->rd_fd)) {
	    xerror(0, Ngt("redirection: file descriptor %d unavailable"),
		    r->rd_fd);
	    return false;
	}

	/* expand rd_filename */
	char *filename;
	if (r->rd_type != RT_HERE && r->rd_type != RT_HERERT) {
	    filename = expand_single_with_glob(r->rd_filename, tt_single);
	    if (!filename)
		return false;
	} else {
	    filename = NULL;
	}

	/* save original FD */
	save_fd(r->rd_fd, save);

	/* now, open redirection */
	int fd;
	int flags;
	bool keepopen;
	switch (r->rd_type) {
	case RT_INPUT:
	    flags = O_RDONLY;
	    goto openwithflags;
	case RT_OUTPUT:
	    if (shopt_noclobber && !is_irregular_file(filename)) {
		flags = O_WRONLY | O_CREAT | O_EXCL;
	    } else {
	case RT_CLOBBER:
		flags = O_WRONLY | O_CREAT | O_TRUNC;
	    }
	    goto openwithflags;
	case RT_APPEND:
	    flags = O_WRONLY | O_CREAT | O_APPEND;
	    goto openwithflags;
	case RT_INOUT:
	    flags = O_RDWR | O_CREAT;
	    goto openwithflags;
openwithflags:
	    keepopen = false;
	    fd = open(filename, flags,
		    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	    if (fd < 0) {
		xerror(errno, Ngt("redirection: cannot open `%s'"), filename);
		free(filename);
		return false;
	    }
	    free(filename);
	    break;
	case RT_DUPIN:
	case RT_DUPOUT:
	    keepopen = true;
	    fd = parse_and_check_dup(filename, r->rd_type);
	    if (fd < -1)
		return false;
	    break;
	case RT_HERE:
	case RT_HERERT:
	    keepopen = false;
	    fd = open_heredocument(r->rd_herecontent);
	    if (fd < 0)
		return false;
	    break;
	default:
	    assert(false);
	}

	/* move the new FD to `r->rd_fd' */
	if (fd != r->rd_fd) {
	    if (fd >= 0) {
		if (xdup2(fd, r->rd_fd) < 0)
		    return false;
		if (!keepopen)
		    xclose(fd);
	    } else {
		xclose(r->rd_fd);
	    }
	}

	if (r->rd_fd == STDIN_FILENO)
	    is_stdin_redirected = true;

	r = r->next;
    }
    return true;
}

/* Saves the specified file descriptor if `save' is non-NULL. */
void save_fd(int fd, savefd_T **save)
{
    assert(0 <= fd);
    if (!save)
	return;

    int copyfd = copy_as_shellfd(fd);
    if (copyfd < 0 && errno != EBADF) {
	xerror(errno, Ngt("cannot save file descriptor %d"), fd);
	return;
    }

    savefd_T *s = xmalloc(sizeof *s);
    s->next = *save;
    s->sf_origfd = fd;
    s->sf_copyfd = copyfd;
    s->sf_stdin_redirected = is_stdin_redirected;
    /* note: if `fd' is formerly unused, `sf_copyfd' is -1. */
    *save = s;
}

/* Parses the argument to `RT_DUPIN'/`RT_DUPOUT'.
 * `num' is the argument to parse, which is expected to be "-" or numeric.
 * `num' is freed in this function.
 * If `num' is a positive integer, the value is returned.
 * If `num' is "-", -1 is returned.
 * Otherwise, a negative value other than -1 is returned. */
int parse_and_check_dup(char *const num, redirtype_T type)
{
    int fd = fd;
    if (strcmp(num, "-") == 0) {
	fd = -1;
    } else {
	char *end;
	errno = 0;
	long lfd = strtol(num, &end, 10);
	if (!isxdigit(num[0]) || *end != '\0')
	    errno = EINVAL;
	else if (lfd < 0)
	    errno = ERANGE;
	else
	    fd = (int) lfd;
	if (errno) {
	    xerror(errno, Ngt("redirection: %s"), num);
	    fd = -2;
	} else {
	    if (posixly_correct) {
		/* check the read/write permission */
		int flags = fcntl(fd, F_GETFL);
		if (flags < 0) {
		    xerror(errno, Ngt("redirection: %d"), fd);
		    fd = -2;
		} else {
		    if (type == RT_DUPIN && (flags & O_ACCMODE) == O_WRONLY) {
			xerror(0, Ngt("redirection: %d: not readable"), fd);
			fd = -2;
		    }
		    if (type == RT_DUPOUT && (flags & O_ACCMODE) == O_RDONLY) {
			xerror(0, Ngt("redirection: %d: not writable"), fd);
			fd = -2;
		    }
		}
	    }
	}
    }
    free(num);
    return fd;
}

/* Restores the saved file descriptor and frees `save'. */
void undo_redirections(savefd_T *save)
{
    while (save) {
	if (save->sf_copyfd >= 0) {
	    remove_shellfd(save->sf_copyfd);
	    xdup2(save->sf_copyfd, save->sf_origfd);
	    xclose(save->sf_copyfd);
	} else {
	    xclose(save->sf_origfd);
	}
	is_stdin_redirected = save->sf_stdin_redirected;

	savefd_T *next = save->next;
	free(save);
	save = next;
    }
}

/* Frees the FD-saving info without restoring FD.
 * The copied FDs are closed. */
void clear_savefd(savefd_T *save)
{
    while (save) {
	if (save->sf_copyfd >= 0) {
	    remove_shellfd(save->sf_copyfd);
	    xclose(save->sf_copyfd);
	}

	savefd_T *next = save->next;
	free(save);
	save = next;
    }
}

/* Redirects stdin to "/dev/null" if job control is off and stdin is not yet
 * redirected.
 * If `posixly_correct' is true, the condition is slightly different:
 * "if non-interactive" rather than "if job control is off". */
void maybe_redirect_stdin_to_devnull(void)
{
    int fd;

    if ((posixly_correct ? is_interactive : do_job_control)
	    || is_stdin_redirected)
	return;

    xclose(STDIN_FILENO);
    fd = open("/dev/null", O_RDONLY);
    if (fd > 0) {
	xdup2(fd, STDIN_FILENO);
	xclose(fd);
    }
    is_stdin_redirected = true;
}


/* vim: set ts=8 sts=4 sw=4 noet: */
