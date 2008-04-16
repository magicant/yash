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
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>
#include "util.h"
#include "redir.h"


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
		    Ngt("error in copying file descriptor %d to %d"),
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
struct saveredir_T {
    struct saveredir_T *next;
    int   sr_origfd;            /* 元のファイルディスクリプタ */
    int   sr_copyfd;            /* コピー先のファイルディスクリプタ */
    FILE *sr_file;              /* 元のストリーム */
    bool  sr_stdin_redirected;  /* 元の is_stdin_redirected */
};


/* vim: set ts=8 sts=4 sw=4 noet: */
