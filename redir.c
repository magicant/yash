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
#include <errno.h>
#include <unistd.h>
#include "util.h"
#include "redir.h"


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
	    xerror(0, errno, Ngt("error in closing file descriptor %d"), fd);
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
	    xerror(0, errno,
		    Ngt("error in copying file descriptor %d to %d"),
		    oldfd, newfd);
	    return -1;
	}
    }
    return newfd;
}


/* vim: set ts=8 sts=4 sw=4 noet: */
