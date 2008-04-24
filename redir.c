/* Yash: yet another shell */
/* redir.c: manages file descriptors and provides functions for redirections */
/* © 2007-2008 magicant */

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
#include <sys/select.h>
#include <sys/stat.h>
#include "option.h"
#include "util.h"
#include "parser.h"
#include "expand.h"
#include "redir.h"
#include "exec.h"


/********** ユーティリティ **********/

/* ファイルディスクリプタを確実に閉じる。
 * close が EINTR を返したら、もう一度やり直す。
 * close が EBADF を返したら、エラーとはみなさず 0 を返す。
 * close が EINTR/EBADF 以外のエラーを返したら、エラーメッセージを出す。 */
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

/* dup2 を確実に行う。(dup2 が EINTR を返したら、やり直す)
 * dup2 が EINTR 以外のエラーを返したら、エラーメッセージを出す。
 * この関数は事前に xclose(newfd) を行う。 */
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


/********** shellfd **********/

static void reset_shellfdmin(void);
static void add_shellfd(int fd);
static void remove_shellfd(int fd);
static bool is_shellfd(int fd)
    __attribute__((pure));


/* stdin がリダイレクトされているかどうか */
bool is_stdin_redirected = false;

/* シェルがセーブ等に使用中のファイルディスクリプタの集合。
 * これらのファイルディスクリプタはユーザが使うことはできない。 */
static fd_set shellfds;
/* シェルがセーブ等に使用可能なファイルディスクリプタの最小値 */
static int shellfdmin;
/* shellfds 内のファイルディスクリプタの最大値。
 * shellfds が空集合なら -1。 */
static int shellfdmax;

#ifndef SHELLFDMINMAX
#define SHELLFDMINMAX 100  /* shellfdmin の最大値 */
#endif

/* 制御端末のファイルディスクリプタ */
int ttyfd = -1;


/* redir モジュールを初期化する */
void init_shellfds(void)
{
    static bool initialized = false;
    if (initialized)
	return;
    initialized = true;

    FD_ZERO(&shellfds);
    reset_shellfdmin();
    shellfdmax = -1;
}

/* shellfdmin を(再)計算する */
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

/* fd (>= shellfdmin) を shellfds に追加する */
void add_shellfd(int fd)
{
    assert(fd >= shellfdmin);
    if (fd < FD_SETSIZE)
	FD_SET(fd, &shellfds);
    if (shellfdmax < fd)
	shellfdmax = fd;
}

/* fd を shellfds から削除する */
/* remove_shellfd(fd) は xclose(fd) より先に行うこと */
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

/* fd が shellfds に入っているかどうか調べる */
bool is_shellfd(int fd)
{
    return fd >= FD_SETSIZE || (fd >= 0 && FD_ISSET(fd, &shellfds));
}

/* shellfds 内のファイルディスクリプタを全て閉じる */
void clear_shellfds(void)
{
    for (int fd = 0; fd <= shellfdmax; fd++)
	if (FD_ISSET(fd, &shellfds))
	    xclose(fd);
    FD_ZERO(&shellfds);
    shellfdmax = -1;
    ttyfd = -1;
}

/* 指定したファイルディスクリプタを複製する。
 * 戻り値: 複製した (新しく shellfds に加わった) ファイルディスクリプタ。
 * エラーなら errno を設定して -1 を返す。 */
int copy_as_shellfd(int fd)
{
    int newfd = fcntl(fd, F_DUPFD, shellfdmin);
    if (newfd >= 0) {
	fcntl(newfd, F_SETFD, FD_CLOEXEC);
	add_shellfd(newfd);
    }
    return newfd;
}

/* ストリームの元になっているファイルディスクリプタを copy_as_shellfd して
 * fdopen する。元のストリームはエラーの有無にかかわらず閉じる。
 * 引数が NULL なら何もしない。エラーの時も NULL を返す。 */
FILE *reopen_with_shellfd(FILE *f, const char *mode)
{
    if (!f)
	return NULL;

    int newfd = copy_as_shellfd(fileno(f));
    fclose(f);
    if (newfd < 0)
	return NULL;
    else
	return fdopen(newfd, mode);
}

/* ttyfd を開く */
void open_ttyfd(void)
{
    if (ttyfd < 0) {
	int fd = open("/dev/tty", O_RDWR);
	if (fd >= 0) {
	    ttyfd = copy_as_shellfd(fd);
	    if (ttyfd < 0)
		goto onerror;
	    xclose(fd);
	} else {
	    goto onerror;
	}
    }
    return;

onerror:
    xerror(errno, Ngt("cannot open `%s'"), "/dev/tty");
}


/********** リダイレクト **********/

/* リダイレクトを後で元に戻すための情報 */
struct savefd_T {
    struct savefd_T *next;
    int   sf_origfd;            /* 元のファイルディスクリプタ */
    int   sf_copyfd;            /* コピー先のファイルディスクリプタ */
    bool  sf_stdin_redirected;  /* 元の is_stdin_redirected */
};

static void save_fd(int oldfd, savefd_T **save);
static int parse_and_check_dup(char *num, redirtype_T type)
    __attribute__((nonnull));


/* リダイレクトを開く。
 * save が非 NULL なら、元のファイルディスクリプタをセーブした情報へのポインタを
 * *save に入れる。
 * 戻り値: エラーがなければ true。
 * エラーがあっても、それまでのリダイレクトをセーブした情報が *save に入る */
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

	/* rd_filename を展開する */
	char *filename;
	if (r->rd_type != RT_HERE && r->rd_type != RT_HERERT) {
	    filename = expand_single_with_glob(r->rd_filename, tt_single);
	    if (!filename)
		return false;
	} else {
	    filename = NULL;
	}

	/* リダイレクトをセーブする */
	save_fd(r->rd_fd, save);

	/* 実際にリダイレクトを開く */
	int fd;
	int flags;
	bool keepopen;
	switch (r->rd_type) {
	case RT_INPUT:
	    flags = O_RDONLY;
	    goto openwithflags;
	case RT_OUTPUT:
	    if (false) {  // TODO redir: open_redirections: noclobber option
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

	/* 開いたファイルディスクリプタを r->rd_fd に移動する */
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

/* ファイルディスクリプタをセーブする。
 * fd:   セーブするファイルディスクリプタ
 * save: セーブ情報の保存先へのポインタ。NULL なら何もしない。 */
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
    /* 註: fd が未使用だった場合は sf_copyfd は -1 になる */
    *save = s;
}

/* RT_DUPIN/RT_DUPOUT の対象を解析する。
 * num:    解析の対象となる文字列。"-" または数字であることが期待される。
 *         num はこの関数内で free する。
 * 戻り値: 成功すれば、複製すべきファイルディスクリプタ。num が "-" だった場合は
 *         -1。エラーの場合は -1 未満。 */
int parse_and_check_dup(char *const num, redirtype_T type)
{
    int fd;
    if (strcmp(num, "-") == 0) {
	fd = -1;
    } else {
	char *end;
	errno = 0;
	fd = strtol(num, &end, 10);
	if (*num == '\0' || *end != '\0')
	    errno = EINVAL;
	else if (fd < 0)
	    errno = ERANGE;
	if (errno) {
	    xerror(errno, Ngt("redirection: %s"), num);
	    fd = -2;
	} else {
	    if (posixly_correct) {
		/* ファイルディスクリプタの読み書き権限をチェック */
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

/* セーブしたファイルディスクリプタを元に戻し、save を free する。 */
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

/* セーブしたファイルディスクリプタの情報を削除し、元に戻せないようにする。
 * 引数は関数内で free する。 */
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

/* 標準入力を /dev/null にリダイレクトする。 */
void redirect_stdin_to_devnull(void)
{
    xclose(STDIN_FILENO);

    int fd = open("/dev/null", O_RDONLY);
    if (fd > 0) {
	xdup2(fd, STDIN_FILENO);
	xclose(fd);
    }
    is_stdin_redirected = true;
}


/* vim: set ts=8 sts=4 sw=4 noet: */
