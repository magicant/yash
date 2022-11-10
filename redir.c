/* Yash: yet another shell */
/* redir.c: manages file descriptors and provides functions for redirections */
/* (C) 2007-2022 magicant */

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
#include "redir.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <limits.h>
#if YASH_ENABLE_SOCKET
# include <netdb.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#if YASH_ENABLE_SOCKET
# include <sys/socket.h>
#endif
#include <sys/stat.h>
#include <unistd.h>
#include "exec.h"
#include "expand.h"
#include "input.h"
#include "option.h"
#include "parser.h"
#include "path.h"
#include "sig.h"
#include "strbuf.h"
#include "util.h"
#include "yash.h"


/********** Utilities **********/

/* Closes the specified file descriptor surely.
 * If `close' returns EINTR, tries again.
 * If `close' returns EBADF, it is considered successful and silently ignored.
 * If `close' returns an error other than EINTR/EBADF, an error message is
 * printed. */
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
    if (oldfd != newfd)
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

/* Repeatedly calls `write' until all `data' is written.
 * Returns true iff successful. On error, false is returned with `errno' set. */
/* Note that this function returns a Boolean value, not `ssize_t'. */
bool write_all(int fd, const void *data, size_t size)
{
    while (size > 0) {
	ssize_t s = write(fd, data, size);
	if (s < 0)
	    return false;
	data = (const char *) data + s;
	size -= s;
    }
    return true;
}


/********** Shell FDs **********/

static void reset_shellfdmin(void);


/* Set of file descriptors used by the shell.
 * These file descriptors cannot be used by the user. */
static fd_set shellfds;
/* The minimum file descriptor that can be used for shell FD. */
static int shellfdmin;
/* The maximum file descriptor in `shellfds'.
 * `shellfdmax' is -1 when `shellfds' is empty. */
static int shellfdmax = -1;

#ifndef SHELLFDMINMAX
#define SHELLFDMINMAX 100  /* maximum for `shellfdmin' */
#endif
#if SHELLFDMINMAX < 10
#error SHELLFDMINMAX too little
#endif

/* File descriptor associated with the controlling terminal */
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
    assert(shellfdmax == -1);  // shellfdmax = -1;
}

/* Recomputes `shellfdmin'. */
void reset_shellfdmin(void)
{
    errno = 0;
    shellfdmin = sysconf(_SC_OPEN_MAX);
    if (shellfdmin < 0) {
	if (errno != 0)
	    shellfdmin = 10;
	else
	    shellfdmin = SHELLFDMINMAX;
    } else {
	if (shellfdmin > FD_SETSIZE)
	    shellfdmin = FD_SETSIZE;
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
	do
	    shellfdmax--;
	while (shellfdmax >= 0 && !FD_ISSET(shellfdmax, &shellfds));
    }
}
/* The argument to `FD_CLR' must be a valid (open) file descriptor. This is why
 * `remove_shellfd' must be called before closing the file descriptor. */

/* Checks if the specified file descriptor is in `shellfds'. */
bool is_shellfd(int fd)
{
    return fd >= FD_SETSIZE || (fd >= 0 && FD_ISSET(fd, &shellfds));
}

/* Clears `shellfds'.
 * If `leavefds' is false, the file descriptors in `shellfds' are closed. */
void clear_shellfds(bool leavefds)
{
    if (!leavefds) {
	for (int fd = 0; fd <= shellfdmax; fd++)
	    if (FD_ISSET(fd, &shellfds))
		xclose(fd);
	FD_ZERO(&shellfds);
	shellfdmax = -1;
    }
    ttyfd = -1;
}

/* Duplicates the specified file descriptor as a new shell FD.
 * The new FD is added to `shellfds'.
 * On error, `errno' is set and -1 is returned. */
int copy_as_shellfd(int fd)
{
    int newfd;

#ifdef F_DUPFD_CLOEXEC
    /* Even if the F_DUPFD_CLOEXEC flag is defined in the <fcntl.h> header, the
     * OS kernel may not support it. We fall back on the normal F_DUPFD-F_SETFD
     * sequence if the F_DUPFD_CLOEXEC flag is rejected. */
    static bool dupfd_cloexec_ok = true;
    if (dupfd_cloexec_ok) {
	newfd = fcntl(fd, F_DUPFD_CLOEXEC, shellfdmin);
	if (newfd >= 0 || errno != EINVAL)
	    goto finish;
	dupfd_cloexec_ok = false;
    }
#endif

    newfd = fcntl(fd, F_DUPFD, shellfdmin);
    if (newfd >= 0)
	fcntl(newfd, F_SETFD, FD_CLOEXEC);
#ifdef F_DUPFD_CLOEXEC
finish:
#endif
    if (newfd >= 0)
	add_shellfd(newfd);
    return newfd;
}

/* Moves the specified file descriptor (FD) to a shell FD.
 * The original FD is closed (whether successful or not).
 * If `fd' is negative, this function simply returns `fd' (without changing
 * `errno'). If `fd' is non-negative and the FD cannot be copied, `errno' is set
 * to indicate the error. */
int move_to_shellfd(int fd)
{
    if (fd < 0)
	return fd;

    int newfd = copy_as_shellfd(fd);
    int saveerrno = errno;
    xclose(fd);
    errno = saveerrno;
    return newfd;
}

/* Opens `ttyfd'.
 * On failure, an error message is printed and `do_job_control' is set to false.
 */
void open_ttyfd(void)
{
    if (ttyfd < 0) {
	ttyfd = move_to_shellfd(open("/dev/tty", O_RDWR));
	if (ttyfd < 0) {
	    xerror(errno, Ngt("cannot open file `%s'"), "/dev/tty");
	    xerror(0, Ngt("disabling job control"));
	    do_job_control = false;
	}
    }
}


/********** Redirections **********/

/* info used to undo redirection */
struct savefd_T {
    struct savefd_T *next;
    int  sf_origfd;            /* original file descriptor */
    int  sf_copyfd;            /* copied file descriptor */
};

static char *expand_redir_filename(const struct wordunit_T *filename)
    __attribute__((malloc,warn_unused_result));
static void save_fd(int oldfd, savefd_T **save)
    __attribute__((nonnull));
static int open_file(const char *path, int oflag)
    __attribute__((nonnull));
#if YASH_ENABLE_SOCKET
static int open_socket(const char *hostandport, int socktype)
    __attribute__((nonnull));
#endif
static int parse_and_check_dup(char *num, redirtype_T type)
    __attribute__((nonnull));
static int parse_and_exec_pipe(int outputfd, char *num, savefd_T **save)
    __attribute__((nonnull));
static int open_heredocument(const struct wordunit_T *content);
static int open_herestring(char *s, bool appendnewline)
    __attribute__((nonnull));
static int open_process_redirection(const embedcmd_T *command, redirtype_T type)
    __attribute__((nonnull));


/* Opens redirections.
 * The original FDs are saved and a pointer to the restoration info is assigned
 * to `*save' (whether successful or not).
 * Returns true iff successful. */
bool open_redirections(const redir_T *r, savefd_T **save)
{
    *save = NULL;

    while (r != NULL) {
	if (r->rd_fd < 0) {
	    xerror(0, Ngt("redirection: invalid file descriptor"));
	    return false;
	} else if (is_shellfd(r->rd_fd)) {
	    xerror(0, Ngt("redirection: file descriptor %d is unavailable"),
		    r->rd_fd);
	    return false;
	}

	/* expand rd_filename */
	char *INIT(filename, "");
	switch (r->rd_type) {
	    case RT_INPUT:  case RT_OUTPUT:  case RT_CLOBBER:  case RT_APPEND:
	    case RT_INOUT:  case RT_DUPIN:   case RT_DUPOUT:   case RT_PIPE:
	    case RT_HERESTR:
		filename = expand_redir_filename(r->rd_filename);
		if (filename == NULL)
		    return false;
		break;
	    default:
		break;
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
	    if (!shopt_clobber) {
		flags = O_WRONLY | O_CREAT | O_EXCL;
	    } else {
		/* falls thru! */
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
	    fd = open_file(filename, flags);
	    if (fd < 0) {
		xerror(errno, Ngt("redirection: cannot open file `%s'"),
			filename);
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
	case RT_PIPE:
	    keepopen = false;
	    fd = parse_and_exec_pipe(r->rd_fd, filename, save);
	    if (fd < 0)
		return false;
	    break;
	case RT_HERE:
	case RT_HERERT:
	    keepopen = false;
	    fd = open_heredocument(r->rd_herecontent);
	    if (fd < 0)
		return false;
	    break;
	case RT_HERESTR:
	    keepopen = false;
	    fd = open_herestring(filename, true);
	    if (fd < 0)
		return false;
	    break;
	case RT_PROCIN:
	case RT_PROCOUT:
	    keepopen = false;
	    fd = open_process_redirection(&r->rd_command, r->rd_type);
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

	r = r->next;
    }
    return true;
}

/* Expands the filename for redirection.
 * Returns a newly malloced string or NULL. */
char *expand_redir_filename(const struct wordunit_T *filename)
{
    if (is_interactive) {
	return expand_single_with_glob(filename);
    } else {
	wchar_t *result = expand_single(filename, TT_SINGLE, Q_WORD, ES_NONE);
	if (result == NULL)
	    return NULL;
	char *mbsresult = realloc_wcstombs(result);
	if (mbsresult == NULL)
	    xerror(EILSEQ, Ngt("redirection"));
	return mbsresult;
    }
}

/* Saves the specified file descriptor if `save' is non-NULL. */
void save_fd(int fd, savefd_T **save)
{
    assert(fd >= 0);

    int copyfd = copy_as_shellfd(fd);
    if (copyfd < 0 && errno != EBADF) {
	xerror(errno, Ngt("cannot save file descriptor %d"), fd);
	return;
    }
    /* If file descriptor `fd' is not open, `copy_as_shellfd' returns -1 with
     * the EBADF errno value. */

    savefd_T *s = xmalloc(sizeof *s);
    s->next = *save;
    s->sf_origfd = fd;
    s->sf_copyfd = copyfd;
    *save = s;
}

/* Opens the redirected file.
 * `path' and `oflag' are the first and second arguments to the `open' function.
 * If `oflag' contains the O_EXCL flag, this function may retry without the flag
 * to allow opening an existing non-regular file.
 * If socket redirection is enabled and `path' begins with "/dev/tcp/" or
 * "/dev/udp/", a socket is opened.
 * Returns a new file descriptor if successful. Otherwise, `errno' is set and
 * -1 is returned. */
int open_file(const char *path, int oflag)
{
    const mode_t mode =
	S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

    int fd = open(path, oflag, mode);

    // Support the no-clobber mode.
    if ((oflag & O_EXCL) && fd < 0 && errno == EEXIST) {
	fd = open(path, oflag & ~(O_CREAT | O_EXCL | O_TRUNC), mode);
	if (fd < 0) {
	    if (errno == ENOENT) {
		// There are two possibilities now: One is that a file existed
		// on the first open call and had been removed before the
		// second. In this case, we might be able to create another if
		// we start over. The other is that there is a symbolic link
		// pointing to nothing, in which case retrying would only lead
		// to the same result. Since there is no reliable way to tell
		// the situations apart atomically, we give up and return the
		// initial error.
		errno = EEXIST;
	    }
	} else {
	    struct stat st;
	    if (fstat(fd, &st) >= 0 && S_ISREG(st.st_mode)) {
		// We opened the FD without the O_CREAT flag, so this regular
		// file was created by somebody else. Failure.
		xclose(fd);
		fd = -1;
		errno = EEXIST;
	    }
	}
    }

    // Support socket redirection.
#if YASH_ENABLE_SOCKET
    if (fd < 0) {
	const char *hostandport = matchstrprefix(path, "/dev/tcp/");
	if (hostandport != NULL)
	    fd = open_socket(hostandport, SOCK_STREAM);
    }
    if (fd < 0) {
	const char *hostandport = matchstrprefix(path, "/dev/udp/");
	if (hostandport != NULL)
	    fd = open_socket(hostandport, SOCK_DGRAM);
    }
#endif /* YASH_ENABLE_SOCKET */

    return fd;
}

#if YASH_ENABLE_SOCKET

/* Opens a socket.
 * `hostandport' is the name and the port of the host to connect, concatenated
 * with a slash. `socktype' specifies the type of the socket, which should be
 * SOCK_STREAM for TCP or SOCK_DGRAM for UDP.
 * On failure, returns -1 with `errno' unchanged. */
int open_socket(const char *hostandport, int socktype)
{
    struct addrinfo hints, *ai;
    int err, saveerrno;
    char *hostname, *port;
    int fd;

    saveerrno = errno;

    /* decompose `hostandport' into `hostname' and `port' */
    {
	wchar_t *whostandport;
	const wchar_t *wport;

	whostandport = malloc_mbstowcs(hostandport);
	if (whostandport == NULL) {
	    errno = saveerrno;
	    return -1;
	}
	wport = wcschr(whostandport, L'/');
	if (wport != NULL) {
	    hostname = malloc_wcsntombs(whostandport, wport - whostandport);
	    port = malloc_wcstombs(wport + 1); // XXX error ignored
	} else {
	    hostname = xstrdup(hostandport);
	    port = NULL;
	}
	free(whostandport);
    }

    set_interruptible_by_sigint(true);

    hints.ai_flags = 0;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    hints.ai_protocol = 0;
    hints.ai_addrlen = 0;
    hints.ai_addr = NULL;
    hints.ai_canonname = NULL;
    hints.ai_next = NULL;
    err = getaddrinfo(hostname, port, &hints, &ai);
    free(hostname);
    free(port);
    if (err != 0) {
	xerror(0, Ngt("socket redirection: "
		    "cannot resolve the address of `%s': %s"),
		hostandport, gai_strerror(err));
	set_interruptible_by_sigint(false);
	errno = saveerrno;
	return -1;
    }

    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd >= 0 && connect(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
	xclose(fd);
	fd = -1;
    }
    saveerrno = errno;
    freeaddrinfo(ai);
    set_interruptible_by_sigint(false);
    errno = saveerrno;
    return fd;
}

#endif /* YASH_ENABLE_SOCKET */

/* Parses the argument to an RT_DUPIN/RT_DUPOUT redirection.
 * `num' is the argument to parse, which is expected to be "-" or a non-negative
 * integer. `num' is freed in this function.
 * If `num' is a non-negative integer, the value is returned.
 * If `num' is "-", -1 is returned.
 * Otherwise, a negative value other than -1 is returned.
 * `type' must be either RT_DUPIN or RT_DUPOUT. */
int parse_and_check_dup(char *const num, redirtype_T type)
{
    int fd;

    if (strcmp(num, "-") == 0) {
	fd = -1;
	goto end;
    }

    if (!xisxdigit(num[0]))
	errno = EINVAL;
    else if (xstrtoi(num, 10, &fd))
	if (fd < 0)
	    errno = ERANGE;
    if (errno != 0) {
	xerror(errno, Ngt("redirection: %s"), num);
	fd = -2;
	goto end;
    }

    if (is_shellfd(fd)) {
	xerror(0, Ngt("redirection: file descriptor %d is unavailable"), fd);
	fd = -2;
	goto end;
    }

    if (posixly_correct) {
	/* check the read/write permission */
	int flags = fcntl(fd, F_GETFL);
	if (flags < 0) {
	    xerror(errno, Ngt("redirection: %s"), num);
	    fd = -2;
	} else if (type == RT_DUPIN) {
	    switch (flags & O_ACCMODE) {
		case O_RDONLY:  case O_RDWR:
		    /* ok */
		    break;
		default:
		    xerror(0, Ngt("redirection: "
				"file descriptor %d is not readable"), fd);
		    fd = -2;
		    break;
	    }
	} else {
	    assert(type == RT_DUPOUT);
	    switch (flags & O_ACCMODE) {
		case O_WRONLY:  case O_RDWR:
		    /* ok */
		    break;
		default:
		    xerror(0, Ngt("redirection: "
				"file descriptor %d is not writable"), fd);
		    fd = -2;
		    break;
	    }
	}
    }

end:
    free(num);
    return fd;
}

/* Parses the argument to an RT_PIPE redirection and opens a pipe.
 * `outputfd' is the file descriptor of the output side of the pipe.
 * `num' is the argument to parse, which is expected to be a non-negative
 * integer that is the file descriptor of the input side of the pipe.
 * `num' is freed in this function.
 * The input side FD is saved in this function.
 * If successful, the actual file descriptor of the output side of the pipe is
 * returned, which may differ from `outputfd'. Otherwise, -1 is returned. */
int parse_and_exec_pipe(int outputfd, char *num, savefd_T **save)
{
    int fd, inputfd;
    int pipefd[2];

    assert(outputfd >= 0);

    if (!xisxdigit(num[0])) {
	errno = EINVAL;
    } else {
	if (xstrtoi(num, 10, &inputfd) && inputfd < 0)
	    errno = ERANGE;
    }
    if (errno != 0) {
	xerror(errno, Ngt("redirection: %s"), num);
	fd = -1;
    } else if (outputfd == inputfd) {
	xerror(0, Ngt("redirection: %d>>|%d: "
		    "the input and output file descriptors are same"),
		outputfd, inputfd);
	fd = -1;
    } else if (is_shellfd(inputfd)) {
	xerror(0, Ngt("redirection: file descriptor %d is unavailable"),
		inputfd);
	fd = -1;
    } else {
	/* ok, save inputfd and open the pipe */
	save_fd(inputfd, save);
	if (pipe(pipefd) < 0)
	    goto error;

	/* move the output side from what is to be the input side. */
	if (pipefd[PIPE_OUT] == inputfd) {
	    int newfd = dup(pipefd[PIPE_OUT]);
	    if (newfd < 0)
		goto error2;
	    xclose(pipefd[PIPE_OUT]);
	    pipefd[PIPE_OUT] = newfd;
	}

	/* move the input side to where it should be. */
	if (pipefd[PIPE_IN] != inputfd) {
	    if (xdup2(pipefd[PIPE_IN], inputfd) < 0)
		goto error2;
	    xclose(pipefd[PIPE_IN]);
	    // pipefd[PIPE_IN] = inputfd;
	}

	/* The output side is not moved in this function. */
	fd = pipefd[PIPE_OUT];
    }
end:
    free(num);
    return fd;

error2:;
    int saveerrno = errno;
    xclose(pipefd[PIPE_IN]);
    xclose(pipefd[PIPE_OUT]);
    errno = saveerrno;
error:
    xerror(errno, Ngt("redirection: %d>>|%d"), outputfd, inputfd);
    fd = -1;
    goto end;
}

/* Opens a here-document whose contents is specified by the argument.
 * Returns a newly opened file descriptor if successful, or -1 on error. */
/* The contents of the here-document is passed either through a pipe or a
 * temporary file. */
int open_heredocument(const wordunit_T *contents)
{
    wchar_t *wcontents = expand_single(contents, TT_NONE, Q_INDQ, ES_NONE);
    if (wcontents == NULL)
	return -1;

    char *mcontents = realloc_wcstombs(wcontents);
    if (mcontents == NULL) {
	xerror(EILSEQ, Ngt("cannot write the here-document contents "
		    "to the temporary file"));
	return -1;
    }

    return open_herestring(mcontents, false);
}

/* Opens a here-string whose contents is specified by the argument.
 * If `appendnewline' is true, a newline is appended to the value of `s'.
 * Returns a newly opened file descriptor if successful, or -1 on error.
 * `s' is freed in this function. */
/* The contents of the here-document is passed either through a pipe or a
 * temporary file. */
int open_herestring(char *s, bool appendnewline)
{
    int fd;

    /* if contents is empty */
    if (s[0] == '\0' && !appendnewline) {
	fd = open("/dev/null", O_RDONLY);
	if (fd >= 0) {
	    free(s);
	    return fd;
	}
    }

    size_t len = strlen(s);
    if (appendnewline)
	s[len++] = '\n';

#ifdef PIPE_BUF
    /* use a pipe if the contents is short enough */
    if (len <= PIPE_BUF) {
	int pipefd[2];

	if (pipe(pipefd) >= 0) {
	    /* It is guaranteed that all the contents is written to the pipe
	     * at once, so we don't have to use `write_all' here. */
	    if (write(pipefd[PIPE_OUT], s, len) < 0)
		xerror(errno, Ngt("cannot write the here-document contents "
			    "to the temporary file"));
	    xclose(pipefd[PIPE_OUT]);
	    free(s);
	    return pipefd[PIPE_IN];
	}
    }
#endif /* defined(PIPE_BUF) */

    char *tempfile;
    fd = create_temporary_file(&tempfile, "", 0);
    if (fd < 0) {
	xerror(errno,
		Ngt("cannot create a temporary file for the here-document"));
	free(s);
	return -1;
    }
    if (unlink(tempfile) < 0)
	xerror(errno, Ngt("failed to remove temporary file `%s'"), tempfile);
    free(tempfile);
    if (!write_all(fd, s, len))
	xerror(errno, Ngt("cannot write the here-document contents "
		    "to the temporary file"));
    free(s);
    if (lseek(fd, 0, SEEK_SET) != 0)
	xerror(errno,
		Ngt("cannot seek the temporary file for the here-document"));
    return fd;
}

/* Opens process redirection and returns the file descriptor.
 * `type' must be RT_PROCIN or RT_PROCOUT.
 * The return value is -1 if failed. */
int open_process_redirection(const embedcmd_T *command, redirtype_T type)
{
    int pipefd[2];
    pid_t cpid;

    assert(type == RT_PROCIN || type == RT_PROCOUT);
    if (pipe(pipefd) < 0) {
	xerror(errno, Ngt("redirection: cannot open a pipe "
		    "for the process redirection"));
	return -1;
    }
    cpid = fork_and_reset(-1, false, 0);
    if (cpid < 0) {
	/* fork failure */
	xclose(pipefd[PIPE_IN]);
	xclose(pipefd[PIPE_OUT]);
	return -1;
    } else if (cpid) {
	/* parent process */
	if (type == RT_PROCIN) {
	    xclose(pipefd[PIPE_OUT]);
	    return pipefd[PIPE_IN];
	} else {
	    xclose(pipefd[PIPE_IN]);
	    return pipefd[PIPE_OUT];
	}
    } else {
	/* child process */
	if (type == RT_PROCIN) {
	    xclose(pipefd[PIPE_IN]);
	    if (pipefd[PIPE_OUT] != STDOUT_FILENO) {
		if (xdup2(pipefd[PIPE_OUT], STDOUT_FILENO) < 0)
		    exit(Exit_NOEXEC);
		xclose(pipefd[PIPE_OUT]);
	    }
	} else {
	    xclose(pipefd[PIPE_OUT]);
	    if (pipefd[PIPE_IN] != STDIN_FILENO) {
		if (xdup2(pipefd[PIPE_IN], STDIN_FILENO) < 0)
		    exit(Exit_NOEXEC);
		xclose(pipefd[PIPE_IN]);
	    }
	}
	if (command->is_preparsed)
	    exec_and_or_lists(command->value.preparsed, true);
	else
	    exec_wcs(command->value.unparsed, gt("process redirection"), true);
	assert(false);
    }
}

/* Restores the saved file descriptor and frees `save'. */
void undo_redirections(savefd_T *save)
{
    while (save != NULL) {
	if (save->sf_copyfd >= 0) {
	    remove_shellfd(save->sf_copyfd);
	    xdup2(save->sf_copyfd, save->sf_origfd);
	    xclose(save->sf_copyfd);
	} else {
	    xclose(save->sf_origfd);
	}

	savefd_T *next = save->next;
	free(save);
	save = next;
    }
}

/* Frees the FD-saving info without restoring FD.
 * The copied FDs are closed. */
void clear_savefd(savefd_T *save)
{
    while (save != NULL) {
	if (save->sf_copyfd >= 0) {
	    remove_shellfd(save->sf_copyfd);
	    xclose(save->sf_copyfd);
	}

	savefd_T *next = save->next;
	free(save);
	save = next;
    }
}

/* Redirects the standard input to "/dev/null" if job control is off and the
 * standard input is not yet redirected. */
void maybe_redirect_stdin_to_devnull(void)
{
    if (do_job_control)
	return;

    if (xclose(STDIN_FILENO) < 0)
	return;

    int fd = open("/dev/null", O_RDONLY);
    if (fd < 0) {
	//xerror(errno, Ngt("cannot redirect the standard input to /dev/null"));
    } else {
	assert(fd == STDIN_FILENO);
    }
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
