/* Yash: yet another shell */
/* exec.h: command execution */
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


#ifndef EXEC_H
#define EXEC_H

#include <stdbool.h>
#include <sys/types.h>


#define EXIT_NOEXEC   126
#define EXIT_NOTFOUND 127

extern int laststatus;
extern pid_t lastasyncpid;

struct and_or_T;
extern void exec_and_or_lists(const struct and_or_T *a, bool finally_exit);


#endif /* EXEC_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
