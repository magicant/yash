/* Yash: yet another shell */
/* exec.h: command execution */
/* (C) 2007-2008 magicant */

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


#define EXIT_FAILURE1 1
#define EXIT_ERROR    2
#define EXIT_NOEXEC   126
#define EXIT_NOTFOUND 127
#define EXIT_SYNERROR (256 + EXIT_ERROR)
#define EXIT_EXPERROR EXIT_ERROR
#define EXIT_ASSGNERR EXIT_ERROR
#define EXIT_REDIRERR EXIT_ERROR

extern int laststatus;
extern pid_t lastasyncpid;

struct execinfo;
extern void reset_execinfo(void);
extern struct execinfo *save_execinfo(void)
    __attribute__((malloc,warn_unused_result));
extern void load_execinfo(struct execinfo *save)
    __attribute__((nonnull));
extern bool return_pending(void)
    __attribute__((pure));

struct and_or_T;
struct wordunit_T;
extern void exec_and_or_lists(const struct and_or_T *a, bool finally_exit);
extern wchar_t *exec_command_substitution(const wchar_t *code)
    __attribute__((nonnull,malloc,warn_unused_result));
extern int open_heredocument(const struct wordunit_T *content);

extern int return_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern int break_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern int eval_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern int dot_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern int exec_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern const char return_help[], break_help[], continue_help[],
       eval_help[], dot_help[], exec_help[];


#endif /* EXEC_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
