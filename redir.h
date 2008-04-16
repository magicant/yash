/* Yash: yet another shell */
/* redir.h: manages file descriptors and provides functions for redirections */
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


#ifndef REDIR_H
#define REDIR_H

#include <stdbool.h>
#include <stdio.h>

typedef struct saveredir_T saveredir_T;

extern int xclose(int fd);
extern int xdup2(int oldfd, int newfd);

extern bool is_stdin_redirected;

extern void init_shellfds(void);
extern void clear_shellfds(void);
extern int copy_as_shellfd(int fd);
extern FILE *reopen_with_shellfd(FILE *f, const char *mode);


#endif /* REDIR_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
