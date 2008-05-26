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
#include <stdio.h>
#include <sys/types.h>


extern pid_t shell_pid;

extern void set_own_pgrp(void);
extern void reset_own_pgrp(void);
extern void forget_initial_pgrp(void);

static inline void exit_shell(void)
    __attribute__((noreturn));
extern void exit_shell_with_status(int status)
    __attribute__((noreturn));


struct parseinfo_T;

extern bool exec_mbs(const char *code, const char *name, bool finally_exit)
    __attribute__((nonnull(1)));
extern bool exec_wcs(const wchar_t *code, const char *name, bool finally_exit)
    __attribute__((nonnull(1)));
extern bool exec_input(FILE *f, const char *name,
	bool intrinput, bool finally_exit)
    __attribute__((nonnull(1)));
extern bool parse_and_exec(struct parseinfo_T *pinfo, bool finally_exit)
    __attribute__((nonnull(1)));


/* シェルを終了し、終了ステータスとして laststatus を返す。 */
/* この関数は EXIT トラップを実行し、reset_own_pgrp を呼び出す。 */
/* この関数は返らない。 */
/* この関数は再入可能であり、再入すると直ちに終了する。 */
void exit_shell(void)
{
    exit_shell_with_status(-1);
}


#endif /* YASH_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
