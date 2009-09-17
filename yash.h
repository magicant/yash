/* Yash: yet another shell */
/* yash.h: basic functions of the shell and miscellanies */
/* (C) 2007-2009 magicant */

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


#ifndef YASH_YASH_H
#define YASH_YASH_H

#include <stdio.h>
#include <sys/types.h>


extern pid_t shell_pid, shell_pgid;

static inline void exit_shell(void)
    __attribute__((noreturn));
extern void exit_shell_with_status(int status)
    __attribute__((noreturn));


struct parseinfo_T;

extern void exec_mbs(const char *code, const char *name, _Bool finally_exit)
    __attribute__((nonnull(1)));
extern void exec_wcs(const wchar_t *code, const char *name, _Bool finally_exit)
    __attribute__((nonnull(1)));
extern void exec_input(FILE *f, const char *name,
	_Bool intrinput, _Bool finally_exit)
    __attribute__((nonnull(1)));
extern void parse_and_exec(struct parseinfo_T *pinfo, _Bool finally_exit)
    __attribute__((nonnull(1)));


extern _Bool nextforceexit;

extern int exit_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern int suspend_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern const char exit_help[], suspend_help[];


/* Exits the shell with the current exit status.
 * This function executes EXIT trap.
 * This function never returns.
 * This function is reentrant and exits immediately if reentered. */
void exit_shell(void)
{
    exit_shell_with_status(-1);
}


#endif /* YASH_YASH_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
