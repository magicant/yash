/* Yash: yet another shell */
/* exec.h: interface to command execution */
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

#include <signal.h>
#include <stdbool.h>
#include <sys/types.h>
#include "parser.h"


extern int laststatus;

void exec_statements(STATEMENT *statements);
void exec_statements_and_exit(STATEMENT *statements)
	__attribute__((noreturn));
char *exec_and_read(const char *code, bool trimend);


#endif /* EXEC_H */
