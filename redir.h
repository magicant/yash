/* Yash: yet another shell */
/* redir.h: manages file descriptors and provides functions for redirections */
/* (C) 2007-2023 magicant */

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


#ifndef YASH_REDIR_H
#define YASH_REDIR_H

#include <stddef.h>


extern int xclose(int fd);
extern int xdup2(int oldfd, int newfd);
extern _Bool write_all(int fd, const void *data, size_t size)
    __attribute__((nonnull));

extern int ttyfd;

extern void init_shellfds(void);
extern void add_shellfd(int fd);
extern void remove_shellfd(int fd);
extern _Bool is_shellfd(int fd)
    __attribute__((pure));
extern void clear_shellfds(_Bool leavefds);
extern int copy_as_shellfd(int fd);
extern int move_to_shellfd(int fd);
extern void open_ttyfd(void);

typedef struct savefd_T savefd_T;
struct redir_T;

extern _Bool open_redirections(const struct redir_T *r, savefd_T **save)
    __attribute__((nonnull(2)));
extern void undo_redirections(savefd_T *save);
extern void clear_savefd(savefd_T *save);
extern void maybe_redirect_stdin_to_devnull(void);

#define PIPE_IN  0   /* index of the reading end of a pipe */
#define PIPE_OUT 1   /* index of the writing end of a pipe */


#endif /* YASH_REDIR_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
