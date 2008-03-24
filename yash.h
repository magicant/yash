/* Yash: yet another shell */
/* yash.h: basic functions of the shell and miscellanies */
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


#ifndef YASH_H
#define YASH_H

#include <stdbool.h>
#include <sys/types.h>


#define EXIT_NOEXEC   126
#define EXIT_NOTFOUND 127


extern bool posixly_correct;
extern bool is_login_shell, is_interactive, is_interactive_now;
extern const char *command_name;
extern pid_t shell_pid;

__attribute__((nonnull(1)))
extern bool exec_mbs(const char *code, const char *name, bool finally_exit);


#endif /* YASH_H */
