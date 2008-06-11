/* Yash: yet another shell */
/* sig.c: signal handling */
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


#include "common.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/select.h>
#include "option.h"
#include "util.h"
#include "sig.h"
#include "job.h"
#include "signum.h"


/* About the shell's signal handling:
 *
 * Yash always catches SIGCHLD, SIGHUP and SIGQUIT.
 * When job control is active, SIGTTOU and SIGTSTP are also caught.
 * If the shell is interactive, SIGINT and SIGTERM are also caught.
 * Other signals are caught if a trap for the signals are set.
 *
 * SIGQUIT and SIGINT are blocked in an asynchronous list.
 * SIGTSTP is blocked in a command substitution in an interactive shell.
 * SIGTTOU is blocked during `tcsetpgrp'
 * all signals other than SIGPIPE is blocked when writing the contents of a
 * here-document to a pipe.
 * SIGCHLD and SIGHUP is blocked to avoid a race condition
 *  - while reading the output of a command substitution, and
 *  - in `wait_for_job'. */


static void sig_handler(int signum);

/* signal number and name */
typedef struct signal_T {
    int no;
    const char *name;
} signal_T;

/* list of signals */
static const signal_T signals[] = {
    /* defined by POSIX.1-1990 */
    { SIGHUP,  "HUP",  }, { SIGINT,  "INT",  }, { SIGQUIT, "QUIT", },
    { SIGILL,  "ILL",  }, { SIGABRT, "ABRT", }, { SIGFPE,  "FPE",  },
    { SIGKILL, "KILL", }, { SIGSEGV, "SEGV", }, { SIGPIPE, "PIPE", },
    { SIGALRM, "ALRM", }, { SIGTERM, "TERM", }, { SIGUSR1, "USR1", },
    { SIGUSR2, "USR2", }, { SIGCHLD, "CHLD", }, { SIGCONT, "CONT", },
    { SIGSTOP, "STOP", }, { SIGTSTP, "TSTP", }, { SIGTTIN, "TTIN", },
    { SIGTTOU, "TTOU", },
    /* defined by SUSv2 & POSIX.1-2001 (SUSv3) */
    { SIGBUS,  "BUS",  }, { SIGPROF, "PROF", }, { SIGSYS,  "SYS",  },
    { SIGTRAP, "TRAP", }, { SIGURG,  "URG",  }, { SIGXCPU, "XCPU", },
    { SIGXFSZ, "XFSZ", },
#ifdef SIGPOLL
    { SIGPOLL, "POLL", },
#endif
#ifdef SIGVTALRM
    { SIGVTALRM, "VTALRM", },
#endif
    /* non-standardized signals */
#ifdef SIGIOT
    { SIGIOT, "IOT", },
#endif
#ifdef SIGEMT
    { SIGEMT, "EMT", },
#endif
#ifdef SIGSTKFLT
    { SIGSTKFLT, "STKFLT", },
#endif
#ifdef SIGIO
    { SIGIO, "IO", },
#endif
#ifdef SIGCLD
    { SIGCLD, "CLD", },
#endif
#ifdef SIGPWR
    { SIGPWR, "PWR", },
#endif
#ifdef SIGINFO
    { SIGINFO, "INFO", },
#endif
#ifdef SIGLOST
    { SIGLOST, "LOST", },
#endif
#ifdef SIGMSG
    { SIGMSG, "MSG", },
#endif
#ifdef SIGWINCH
    { SIGWINCH, "WINCH", },
#endif
#ifdef SIGDANGER
    { SIGDANGER, "DANGER", },
#endif
#ifdef SIGMIGRATE
    { SIGMIGRATE, "MIGRATE", },
#endif
#ifdef SIGPRE
    { SIGPRE, "PRE", },
#endif
#ifdef SIGVIRT
    { SIGVIRT, "VIRT", },
#endif
#ifdef SIGKAP
    { SIGKAP, "KAP", },
#endif
#ifdef SIGGRANT
    { SIGGRANT, "GRANT", },
#endif
#ifdef SIGRETRACT
    { SIGRETRACT, "RETRACT", },
#endif
#ifdef SIGSOUND
    { SIGSOUND, "SOUND", },
#endif
#ifdef SIGUNUSED
    { SIGUNUSED, "UNUSED", },
#endif
    { 0, NULL, },
    /* end of array: any signal number is non-zero (C99 7.14) */
};

/* Returns the name of the signal with the specified number.
 * The returned name doesn't have a "SIG"-prefix.
 * "?" is returned for an unknown signal number. */
const char *get_signal_name(int signum)
{
    for (const signal_T *s = signals; s->no; s++)
	if (s->no == signum)
	    return s->name;
    return "?";
}


/* flags to indicate a signal is caught. */
static volatile sig_atomic_t signal_received[MAXSIGIDX];
/* commands to be executed when a signal is trapped (caught). */
static char *trap_command[MAXSIGIDX];
/* These arrays are indexed by `sigindex'. The index 0 is for the EXIT trap. */

/* `signal_received' and `trap_command' for real-time signals. */
static volatile sig_atomic_t rtsignal_received[RTSIZE];
static char *rttrap_command[RTSIZE];

/* This flag is set to true as well as `signal_received[sigindex(SIGCHLD)]'
 * when SIGCHLD is caught.
 * This flag is used for job control while `signal_received[...]' is for trap
 * handling. */
static volatile sig_atomic_t sigchld_received;

/* Initial actions for SIGCHLD/HUP/QUIT/TTOU/TSTP/TERM/INT.
 * These actions are restored before calling `execve'. */
static struct sigaction
    initsigchldaction, initsighupaction, initsigquitaction,
    initsigttouaction, initsigtstpaction,
    initsigtermaction, initsigintaction;

/* true iff `initsig{chld,hup,quit}action' are valid. */
static bool initialized = false;
/* true iff `initsig{ttou,tstp}action' are valid. */
static bool job_initialized = false;
/* true iff `initsig{int,term}action' are valid. */
static bool interactive_initialized = false;

/* Initializes signal handling */
void init_signal(void)
{
    struct sigaction action;
    sigset_t ss;

    if (!initialized) {
	initialized = true;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = sig_handler;
	if (sigaction(SIGCHLD, &action, &initsigchldaction) < 0)
	    xerror(errno, "sigaction(SIGCHLD)");
	if (sigaction(SIGHUP, &action, &initsighupaction) < 0)
	    xerror(errno, "sigaction(SIGHUP)");
	if (sigaction(SIGQUIT, &action, &initsigquitaction) < 0)
	    xerror(errno, "sigaction(SIGQUIT)");

	sigemptyset(&ss);
	if (sigprocmask(SIG_SETMASK, &ss, NULL) < 0)
	    xerror(errno, "sigprocmask(SETMASK, nothing)");
    }
}

/* Initializes actions for SIGTTOU/TSTP/INT/TERM according to
 * `do_job_control' and `is_interactive_now'. */
void set_signals(void)
{
    struct sigaction action;

    if (do_job_control && !job_initialized) {
	job_initialized = true;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = sig_handler;
	if (sigaction(SIGTTOU, &action, &initsigttouaction) < 0)
	    xerror(errno, "sigaction(SIGTTOU)");
	if (sigaction(SIGTSTP, &action, &initsigtstpaction) < 0)
	    xerror(errno, "sigaction(SIGTSTP)");
    }
    if (is_interactive_now && !interactive_initialized) {
	interactive_initialized = true;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = sig_handler;
	if (sigaction(SIGINT, &action, &initsigintaction) < 0)
	    xerror(errno, "sigaction(SIGINT)");
	if (sigaction(SIGTERM, &action, &initsigtermaction) < 0)
	    xerror(errno, "sigaction(SIGTERM)");
    }
}

/* Restores the initial signal actions for all signals. */
void reset_all_signals(void)
{
    if (initialized) {
	initialized = false;
	if (sigaction(SIGCHLD, &initsigchldaction, NULL) < 0)
	    xerror(errno, "sigaction(SIGCHLD)");
	if (sigaction(SIGHUP, &initsighupaction, NULL) < 0)
	    xerror(errno, "sigaction(SIGHUP)");
	if (sigaction(SIGQUIT, &initsigquitaction, NULL) < 0)
	    xerror(errno, "sigaction(SIGQUIT)");
    }
    reset_signals();
}

/* Restores the initial signal actions for SIGTTOU/TSTP/INT/TERM. */
void reset_signals(void)
{
    if (job_initialized) {
	job_initialized = false;
	if (sigaction(SIGTTOU, &initsigttouaction, NULL) < 0)
	    xerror(errno, "sigaction(SIGTTOU)");
	if (sigaction(SIGTSTP, &initsigtstpaction, NULL) < 0)
	    xerror(errno, "sigaction(SIGTSTP)");
    }
    if (interactive_initialized) {
	interactive_initialized = false;
	if (sigaction(SIGINT, &initsigintaction, NULL) < 0)
	    xerror(errno, "sigaction(SIGINT)");
	if (sigaction(SIGTERM, &initsigtermaction, NULL) < 0)
	    xerror(errno, "sigaction(SIGTERM)");
    }
}

/* Blocks SIGQUIT and SIGINT. */
void block_sigquit_and_sigint(void)
{
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGQUIT);
    sigaddset(&ss, SIGINT);
    if (sigprocmask(SIG_BLOCK, &ss, NULL) < 0)
	xerror(errno, "sigprocmask(BLOCK, QUIT|INT)");
}

/* Blocks SIGTSTP. */
void block_sigtstp(void)
{
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGTSTP);
    if (sigprocmask(SIG_BLOCK, &ss, NULL) < 0)
	xerror(errno, "sigprocmask(BLOCK, TSTP)");
}

/* Blocks SIGTTOU. */
void block_sigttou(void)
{
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGTTOU);
    if (sigprocmask(SIG_BLOCK, &ss, NULL) < 0)
	xerror(errno, "sigprocmask(BLOCK, TTOU)");
}

/* Unblocks SIGTTOU. */
void unblock_sigttou(void)
{
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGTTOU);
    if (sigprocmask(SIG_UNBLOCK, &ss, NULL) < 0)
	xerror(errno, "sigprocmask(UNBLOCK, TTOU)");
}

/* Blocks all signals other than SIGPIPE and set SIGPIPE's action to SIG_DFL. */
void block_all_but_sigpipe(void)
{
    sigset_t ss;
    sigfillset(&ss);
    sigdelset(&ss, SIGPIPE);
    if (sigprocmask(SIG_SETMASK, &ss, NULL) < 0)
	xerror(errno, "sigprocmask(SETMASK, all but PIPE)");

    struct sigaction action;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_handler = SIG_DFL;
    if (sigaction(SIGPIPE, &action, NULL) < 0)
	xerror(errno, "sigaction(SIGPIPE)");
}

/* general signal handler */
void sig_handler(int signum)
{
#if defined SIGRTMIN && defined SIGRTMAX
    if (SIGRTMIN <= signum && signum <= SIGRTMAX) {
	size_t index = signum - SIGRTMIN;
	if (index < RTSIZE)
	    rtsignal_received[index] = true;
    } else
#endif
    {
	size_t index = sigindex(signum);
	assert(index < MAXSIGIDX);
	signal_received[index] = true;

	if (signum == SIGCHLD)
	    sigchld_received = true;
    }
}

/* Blocks SIGCHLD and SIGHUP. */
void block_sigchld_and_sighup(void)
{
    sigset_t ss;

    sigemptyset(&ss);
    sigaddset(&ss, SIGCHLD);
    sigaddset(&ss, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &ss, NULL) < 0)
	xerror(errno, "sigprocmask(BLOCK, CHLD|HUP)");
}

/* Unblocks SIGCHLD and SIGHUP. */
void unblock_sigchld_and_sighup(void)
{
    sigset_t ss;

    sigemptyset(&ss);
    sigaddset(&ss, SIGCHLD);
    sigaddset(&ss, SIGHUP);
    if (sigprocmask(SIG_UNBLOCK, &ss, NULL) < 0)
	xerror(errno, "sigprocmask(UNBLOCK, CHLD|HUP)");
}

/* Waits for SIGCHLD to be caught and call `handle_sigchld_and_sighup'.
 * This function must be called with SIGCHLD/HUP blocked.
 * If SIGCHLD is already caught, this function doesn't wait. */
void wait_for_sigchld(void)
{
    sigset_t ss;

    sigemptyset(&ss);
    while (!sigchld_received && !signal_received[sigindex(SIGHUP)]) {
	if (sigsuspend(&ss) < 0 && errno != EINTR) {
	    xerror(errno, "sigsuspend");
	    break;
	}
    }

    handle_sigchld_and_sighup();
}

/* Waits for the specified file descriptor to be available for reading.
 * If SIGCHLD or SIGHUP is caught while waiting, `handle_sigchld_and_sighup'
 * is called. SIGCHLD and SIGHUP must be blocked when this function is called.
 * If `trap' is true, traps are also handled while waiting. */
void wait_for_input(int fd, bool trap)
{
    sigset_t ss;

    sigemptyset(&ss);
    for (;;) {
	handle_sigchld_and_sighup();
	if (trap)
	    handle_traps();

	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);
	if (pselect(fd + 1, &fdset, NULL, NULL, NULL, &ss) >= 0) {
	    if (FD_ISSET(fd, &fdset))
		return;
	} else {
	    if (errno != EINTR) {
		xerror(errno, "pselect");
		return;
	    }
	}
    }
}

/* Handles SIGCHLD and SIGHUP if caught. */
void handle_sigchld_and_sighup(void)
{
    if (sigchld_received) {
        sigchld_received = false;
        do_wait();
	if (shopt_notify) {
	    print_job_status(PJS_ALL, true, false, stderr);
	    // XXX redisplay prompt if lineedit is active
	}
    }
    // TODO sig.c: handling of SIGHUP
}

/* Executes trap commands for trapped signals if any.
 * There must not be an active job when this function is called. */
void handle_traps(void)
{
    (void) trap_command;
    (void) rttrap_command;
    // TODO sig.c: handle_traps: unimplemented
}

/* Clears all traps except that are set to SIG_IGN. */
void clear_traps(void)
{
    // TODO sig.c: clear_traps: unimplemented
}


/* vim: set ts=8 sts=4 sw=4 noet: */
