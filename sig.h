/* Yash: yet another shell */
/* sig.h: signal handling */
/* (C) 2007-2016 magicant */

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


#ifndef YASH_SIG_H
#define YASH_SIG_H

#include <stddef.h>
#include <sys/types.h>
#include "xgetopt.h"


extern _Bool process_exists(pid_t pid);
extern const wchar_t *get_signal_name(int signum)
    __attribute__((const));
extern int get_signal_number(const wchar_t *name)
    __attribute__((nonnull,pure));
extern int get_signal_number_toupper(wchar_t *name)
    __attribute__((nonnull));

extern _Bool any_trap_set;

extern void init_signal(void);
extern void set_signals(void);
extern void restore_signals(_Bool leave);
extern void reset_job_signals(void);
extern void set_interruptible_by_sigint(_Bool onoff);
extern void ignore_sigquit_and_sigint(void);
extern void ignore_sigtstp(void);
extern void stop_myself(void);

extern void handle_signals(void);
extern int wait_for_sigchld(_Bool interruptible, _Bool return_on_trap);

enum wait_for_input_T {
    W_READY, W_TIMED_OUT, W_INTERRUPTED, W_ERROR,
};

extern enum wait_for_input_T wait_for_input(int fd, _Bool trap, int timeout);

extern int handle_traps(void);
extern void execute_exit_trap(void);
extern void clear_exit_trap(void);
extern void phantomize_traps(void);
extern _Bool is_interrupted(void);
extern void set_laststatus_if_interrupted(void);
extern void set_interrupted(void);
#if YASH_ENABLE_LINEEDIT
extern void reset_sigwinch(void);
#endif

extern int trap_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char trap_help[], trap_syntax[];
#endif
extern const struct xgetopt_T trap_options[];

extern int kill_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char kill_help[], kill_syntax[];
#endif

#if HAVE_STRSIGNAL && !defined(strsignal)
extern char *strsignal(int signum);
#endif


#endif /* YASH_SIG_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
