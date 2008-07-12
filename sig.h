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


#ifndef SIG_H
#define SIG_H

#include <stdbool.h>


extern const char *get_signal_name(int signum)
    __attribute__((const));

extern void init_signal(void);
extern void set_signals(void);
extern void restore_all_signals(void);
extern void restore_job_signals(void);
extern void restore_interactive_signals(void);
extern void ignore_sigquit_and_sigint(void);
extern void ignore_sigtstp(void);
extern void block_sigttou(void);
extern void unblock_sigttou(void);
extern void send_sigcont_to_pgrp(pid_t pgrp);
extern void block_all_but_sigpipe(void);

extern void block_sigchld_and_sigint(void);
extern void unblock_sigchld_and_sigint(void);
extern bool wait_for_sigchld(bool interruptible);
extern void wait_for_input(int fd, bool trap);

extern void handle_sigchld(void);
extern void handle_traps(void);
extern void clear_traps(void);

#if HAVE_STRSIGNAL
extern char *strsignal(int signum);
#endif


#endif /* SIG_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
