/* Yash: yet another shell */
/* exec.h: command execution */
/* (C) 2007-2020 magicant */

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


#ifndef YASH_EXEC_H
#define YASH_EXEC_H

#include <stddef.h>
#include <sys/types.h>
#include "xgetopt.h"


/* options for `fork_and_reset' */
typedef enum sigtype_T {
    t_quitint    = 1 << 0,
    t_tstp       = 1 << 1,
    t_leave      = 1 << 2,
} sigtype_T;

#define Exit_SUCCESS  0
#define Exit_FAILURE  1
#define Exit_ERROR    2
#define Exit_NOEXEC   126
#define Exit_NOTFOUND 127
#define Exit_SYNERROR (256 + Exit_ERROR)
#define Exit_EXPERROR Exit_ERROR
#define Exit_ASSGNERR Exit_ERROR
#define Exit_REDIRERR Exit_ERROR

extern int laststatus, savelaststatus, exitstatus;
extern pid_t lastasyncpid;
extern _Bool special_builtin_executed;
extern _Bool is_executing_auxiliary;

struct execstate_T;
extern void reset_execstate(_Bool reset_iteration);
extern struct execstate_T *save_execstate(void)
    __attribute__((malloc,warn_unused_result));
extern void restore_execstate(struct execstate_T *save)
    __attribute__((nonnull));
extern void disable_return(void);
extern void cancel_return(void);
extern _Bool need_break(void)
    __attribute__((pure));

struct and_or_T;
struct embedcmd_T;
extern void exec_and_or_lists(const struct and_or_T *a, _Bool finally_exit);
extern struct xwcsbuf_T *get_xtrace_buffer(void);
extern pid_t fork_and_reset(pid_t pgid, _Bool fg, sigtype_T sigtype);
extern wchar_t *exec_command_substitution(const struct embedcmd_T *cmdsub)
    __attribute__((nonnull,malloc,warn_unused_result));
extern int exec_variable_as_commands(
	const wchar_t *varname, const char *codename)
    __attribute__((nonnull));
extern int exec_variable_as_auxiliary(
	const wchar_t *varname, const char *codename)
    __attribute__((nonnull));
#define exec_variable_as_auxiliary_(varname) \
	exec_variable_as_auxiliary(L varname, "$" varname)

#if YASH_ENABLE_LINEEDIT
extern _Bool autoload_completion_function_file(
	const wchar_t *filename, const wchar_t *cmdname)
    __attribute__((nonnull(1)));
extern _Bool call_completion_function(const wchar_t *funcname)
    __attribute__((nonnull));
#endif

extern const struct xgetopt_T iter_options[];

extern int return_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char return_help[], return_syntax[];
#endif
extern const struct xgetopt_T return_options[];

extern int break_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char break_help[], break_syntax[], continue_help[],
	continue_syntax[];
#endif

extern int eval_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char eval_help[], eval_syntax[];
#endif

extern int dot_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char dot_help[], dot_syntax[];
#endif
extern const struct xgetopt_T dot_options[];

extern int exec_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char exec_help[], exec_syntax[];
#endif
extern const struct xgetopt_T exec_options[];

extern int command_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char command_help[], command_syntax[], type_help[], type_syntax[];
#endif
extern const struct xgetopt_T command_options[];

extern int times_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char times_help[], times_syntax[];
#endif


#endif /* YASH_EXEC_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
