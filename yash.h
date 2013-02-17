/* Yash: yet another shell */
/* yash.h: basic functions of the shell and miscellanies */
/* (C) 2007-2013 magicant */

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

#include <stddef.h>
#include <sys/types.h>
#include "xgetopt.h"


extern pid_t shell_pid, shell_pgid;

extern _Bool shell_initialized;

static inline void exit_shell(void)
    __attribute__((noreturn));
extern void exit_shell_with_status(int status)
    __attribute__((noreturn));


extern struct input_file_info_T *stdin_input_file_info;

extern void exec_wcs(const wchar_t *code, const char *name, _Bool finally_exit)
    __attribute__((nonnull(1)));

typedef enum exec_input_options_T {
    XIO_INTERACTIVE  = 1 << 0,
    XIO_SUBST_ALIAS  = 1 << 1,
    XIO_FINALLY_EXIT = 1 << 2,
} exec_input_options_T;

extern void exec_input(int fd, const char *name, exec_input_options_T options);


extern _Bool nextforceexit;

extern const struct xgetopt_T force_help_options[];

extern int exit_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char exit_help[], exit_syntax[];
#endif

extern int suspend_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char suspend_help[], suspend_syntax[];
#endif


/* Exits the shell with the last exit status.
 * This function executes the EXIT trap.
 * This function never returns.
 * This function is reentrant and exits immediately if reentered. */
void exit_shell(void)
{
    exit_shell_with_status(-1);
}


#endif /* YASH_YASH_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
