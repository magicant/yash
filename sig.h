/* Yash: yet another shell */
/* sig.h: signal handling */
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


#ifndef YASH_SIG_H
#define YASH_SIG_H

#include <stddef.h>


extern const char *get_signal_name(int signum)
    __attribute__((const));
extern int get_signal_number(const char *name)
    __attribute__((nonnull,pure));
extern int get_signal_number_w(wchar_t *name)
    __attribute__((nonnull));

extern _Bool any_trap_set;

extern void init_fixed_trap_set(void);
extern void init_signal(void);
extern void set_signals(void);
extern void set_job_signals(void);
extern void set_interactive_signals(void);
extern void restore_all_signals(void);
extern void restore_job_signals(void);
extern void restore_interactive_signals(void);
extern void set_interruptible_by_sigint(_Bool onoff);
extern void ignore_sigquit_and_sigint(void);
extern void ignore_sigtstp(void);
extern void block_sigttou(void);
extern void unblock_sigttou(void);
extern void reset_sigpipe(void);
extern _Bool send_sigstop_to_myself(void);

extern void block_sigchld_and_sigint(void);
extern void unblock_sigchld_and_sigint(void);
extern _Bool wait_for_sigchld(_Bool interruptible, _Bool return_on_trap);
extern void wait_for_input(int fd, _Bool trap);

extern void handle_sigchld(void);
extern _Bool handle_traps(void);
extern void execute_exit_trap(void);
extern void clear_traps(void);

extern int trap_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern int kill_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern const char trap_help[], kill_help[];

#if HAVE_STRSIGNAL
extern char *strsignal(int signum);
#endif


#endif /* YASH_SIG_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
