/* Yash: yet another shell */
/* sig.c: signal handling */
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


#include "common.h"
#include "sig.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <wchar.h>
#include <wctype.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "builtin.h"
#include "exec.h"
#include "expand.h"
#include "job.h"
#include "option.h"
#include "parser.h"
#include "redir.h"
#include "siglist.h"
#include "signum.h"
#include "strbuf.h"
#include "util.h"
#include "yash.h"
#if YASH_ENABLE_LINEEDIT
# include "xfnmatch.h"
# include "lineedit/complete.h"
# include "lineedit/lineedit.h"
#endif


/* About the shell's signal handling:
 *
 * Yash always catches SIGCHLD.
 * When job control is active, SIGTTIN, SIGTTOU, and SIGTSTP are ignored.
 * If the shell is interactive, SIGTERM and SIGQUIT are ignored and SIGINT and
 * SIGWINCH are caught.
 * Trapped signals are also caught.
 *
 * SIGQUIT and SIGINT are ignored in an asynchronous list.
 * SIGTSTP is left ignored in command substitution in a job-control shell.
 *
 * The shell inherits the signal mask from its invoker and commands invoked by
 * the shell also inherit it. (POSIX.1-2008)
 * Signals with the handler installed are almost always blocked to avoid
 * unexpected interruption of system calls. They are unblocked when:
 *  - the shell waits for input
 *  - the shell waits for a child process to finish
 *  - the shell handles traps.
 * Also, SIGINT is unblocked when:
 *  - the shell tries to open a socket
 *  - the shell performs pathname expansion.
 *
 * SIGTTOU is blocked in `put_foreground' and unblocked in `ensure_foreground'.
 * All signals are blocked to avoid race conditions when the shell forks. */


static int parse_signal_number(const wchar_t *number)
    __attribute__((pure));

static int xsigaction(int signum, const struct sigaction *restrict newaction,
	struct sigaction *restrict oldaction);
static void set_special_handler(int signum, void (*handler)(int signum));
static void reset_special_handler(
	int signum, void (*handler)(int signum), bool leave);
static void sig_handler(int signum);
static void handle_sigchld(void);
static void set_trap(int signum, const wchar_t *command);
static bool is_originally_ignored(int signum);
static void banish_phantoms(void);
#if YASH_ENABLE_LINEEDIT
# ifdef SIGWINCH
static inline void handle_sigwinch(void);
# endif
static void sig_new_candidate(
	const le_compopt_T *restrict compopt, int num, xwcsbuf_T *restrict name)
    __attribute__((nonnull));
#endif


/********** Auxiliary Functions **********/

/* Checks if there exists a process with the specified process ID, which must
 * be positive. */
bool process_exists(pid_t pid)
{
    assert(pid > 0);
    return kill(pid, 0) >= 0 || errno != ESRCH;
    /* send a dummy signal to check if the process exists. */
}

/* Returns the name of the signal with the specified number.
 * The returned name doesn't have the "SIG"-prefix.
 * An empty string is returned for an unknown signal number.
 * The returned string is valid until the next call to this function. */
const wchar_t *get_signal_name(int signum)
{
    if (signum == 0)
	return L"EXIT";
#if defined SIGRTMIN && defined SIGRTMAX
    int sigrtmin = SIGRTMIN, sigrtmax = SIGRTMAX;
    if (sigrtmin <= signum && signum <= sigrtmax) {
	static wchar_t *name = NULL;
	if (signum == sigrtmin)
	    return L"RTMIN";
	if (signum == sigrtmax)
	    return L"RTMAX";
	int range = sigrtmax - sigrtmin, diff = signum - sigrtmin;
	free(name);
	if (diff <= range / 2)
	    name = malloc_wprintf(L"RTMIN+%d", diff);
	else
	    name = malloc_wprintf(L"RTMAX-%d", sigrtmax - signum);
	return name;
    }
#endif
    for (const signal_T *s = signals; s->no != 0; s++)
	if (s->no == signum)
	    return s->name;
    return L"";
}

/* Returns the number of the signal whose name is `name'.
 * `name' may have the "SIG"-prefix.
 * If `name' is an integer that is a valid signal number, the number is
 * returned.
 * Returns 0 for "EXIT" and -1 for an unknown name.
 * `name' should be all in uppercase. */
int get_signal_number(const wchar_t *name)
{
    if (iswdigit(name[0]))
	return parse_signal_number(name);

    if (wcscmp(name, L"EXIT") == 0)
	return 0;
    if (wcsncmp(name, L"SIG", 3) == 0)
	name += 3;
    for (const signal_T *s = signals; s->no != 0; s++)
	if (wcscmp(name, s->name) == 0)
	    return s->no;
#if defined SIGRTMIN && defined SIGRTMAX
    if (wcsncmp(name, L"RTMIN", 5) == 0) {
	int sigrtmin = SIGRTMIN;
	name += 5;
	if (name[0] == L'\0') {
	    return sigrtmin;
	} else if (name[0] == L'+') {
	    int num;
	    if (xwcstoi(name, 10, &num)) {
		assert(num >= 0);
		if (SIGRTMAX - sigrtmin >= num)
		    return sigrtmin + num;
	    }
	}
    } else if (wcsncmp(name, L"RTMAX", 5) == 0) {
	int sigrtmax = SIGRTMAX;
	name += 5;
	if (name[0] == L'\0') {
	    return sigrtmax;
	} else if (name[0] == L'-') {
	    int num;
	    if (xwcstoi(name, 10, &num)) {
		assert(num < 0);
		if (SIGRTMIN - sigrtmax <= num)
		    return sigrtmax + num;
	    }
	}
    }
#endif
    return -1;
}

/* Parses the specified string as non-negative integer and returns the integer
 * if it is zero or a valid signal number. Returns -1 otherwise. */
int parse_signal_number(const wchar_t *number)
{
    int signum;

    /* parse `number' */
    if (!xwcstoi(number, 10, &signum) || signum < 0)
	return -1;

    /* check if `signum' is a valid signal */
    if (signum == 0)
	return 0;
#if defined SIGRTMIN && defined SIGRTMAX
    if (SIGRTMIN <= signum && signum <= SIGRTMAX)
	return signum;
#endif
    for (const signal_T *s = signals; s->no != 0; s++)
	if (s->no == signum)
	    return signum;
    return -1;
}

/* Returns the number of the signal whose name is `name'.
 * `name' may have the "SIG"-prefix.
 * If `name' is an integer that is a valid signal number, the number is
 * returned.
 * Returns 0 for "EXIT" and -1 for an unknown name.
 * The given string is converted into uppercase. */
int get_signal_number_toupper(wchar_t *name)
{
    for (wchar_t *n = name; *n != L'\0'; n++)
	*n = towupper(*n);
    return get_signal_number(name);
}


/********** Signal Handler Management */

/* set to true when any trap other than "ignore" is set */
bool any_trap_set = false;

/* flag to indicate a signal is caught. */
static volatile sig_atomic_t any_signal_received = false;

/* the signal for which trap is currently executed */
static int handled_signal = -1;

/* flags to indicate a signal is caught. */
static volatile sig_atomic_t signal_received[MAXSIGIDX];
/* commands to be executed when a signal is trapped (caught). */
static wchar_t *trap_command[MAXSIGIDX];
/* These arrays are indexed by `sigindex'. The index 0 is for the EXIT trap. */

/* `signal_received' and `trap_command' for real-time signals. */
#if defined SIGRTMIN && defined SIGRTMAX
# if RTSIZE == 0
#  error "RTSIZE == 0"
# endif
static volatile sig_atomic_t rtsignal_received[RTSIZE];
static wchar_t *rttrap_command[RTSIZE];
#endif

/* If true, the contents of `trap_command` and `rttrap_command` are all
 * phantom; that is, non-empty trap commands in those arrays should be treated
 * as "default". Phantoms are used to remember previous trap commands after
 * they were actually reset to "default".
 * If false, trap commands are real. */
static bool is_phantom = false;

/* Set of signals that should be blocked in commands invoked by the shell.
 * This is by default the same as the mask the shell inherited from the parent.
 * When a signal's trap is set, the signal is removed from this mask. */
static sigset_t official_sigmask;
/* Set of signals that are known whose handler was "default"/"ignore" when the
 * shell was invoked, respectively. */
static sigset_t originally_defaulted_signals, originally_ignored_signals;
/* Set of signals whose handler has been installed for the purpose of the
 * shell's duty but should be treated as "ignore" in terms of observable
 * behavior of the shell. The handler of these signals must be reset to "ignore"
 * before the shell invokes a command so that the command inherits "ignore" as
 * the handler. */
static sigset_t officially_ignored_signals;
/* Set of signals whose trap is set to other than "ignore".
 * These signals are almost always blocked. */
static sigset_t trapped_signals;
/* Set of signals that are in `original_sigmask' but not in `trapped_signals' */
static sigset_t accept_sigmask;

/* This flag is set to true as well as `signal_received[sigindex(SIGCHLD/INT)]'
 * when SIGCHLD/SIGINT is caught.
 * This flag is used for job control while `signal_received[...]' is for trap
 * handling. */
static volatile sig_atomic_t sigchld_received, sigint_received;
#if YASH_ENABLE_LINEEDIT && defined(SIGWINCH)
/* This flag is set to true as well as `signal_received[sigindex(SIGWINCH)]'
 * when SIGWINCH is caught.
 * This flag is used by line-editing. */
static volatile sig_atomic_t sigwinch_received;
#endif

/* true iff SIGCHLD is handled. */
static bool main_handler_set = false;
/* true iff SIGTTIN, SIGTTOU, and SIGTSTP are ignored. */
static bool job_handlers_set = false;
/* true iff SIGTERM, SIGINT, SIGQUIT and SIGWINCH are ignored/handled. */
static bool interactive_handlers_set = false;

/* Initializes the signal module. */
void init_signal(void)
{
    sigemptyset(&official_sigmask);
    sigemptyset(&originally_defaulted_signals);
    sigemptyset(&originally_ignored_signals);
    sigemptyset(&officially_ignored_signals);
    sigemptyset(&trapped_signals);
    sigprocmask(SIG_SETMASK, NULL, &official_sigmask);
    accept_sigmask = official_sigmask;
}

/* Installs signal handlers used by the shell according to the current settings.
 */
void set_signals(void)
{
    sigset_t block = trapped_signals;

    if (!job_handlers_set && doing_job_control_now) {
	job_handlers_set = true;
	set_special_handler(SIGTTIN, SIG_IGN);
	set_special_handler(SIGTTOU, SIG_IGN);
	set_special_handler(SIGTSTP, SIG_IGN);
    }

    if (!interactive_handlers_set && is_interactive_now) {
	interactive_handlers_set = true;
	sigaddset(&block, SIGINT);
	set_special_handler(SIGINT, sig_handler);
	set_special_handler(SIGTERM, SIG_IGN);
	set_special_handler(SIGQUIT, SIG_IGN);
#if YASH_ENABLE_LINEEDIT && defined(SIGWINCH)
	sigaddset(&block, SIGWINCH);
	set_special_handler(SIGWINCH, sig_handler);
#endif
    }

    if (!main_handler_set) {
	main_handler_set = true;
	sigaddset(&block, SIGCHLD);
	set_special_handler(SIGCHLD, sig_handler);
    }

    sigprocmask(SIG_BLOCK, &block, NULL);
}

/* Restores the original signal handlers for the signals used by the shell.
 * If `leave' is true, the current process is assumed to be about to exec:
 * the handler may be left unchanged if the handler is supposed to be reset
 * during exec. The signal setting for SIGCHLD is restored.
 * If `leave' is false, the setting for SIGCHLD are not restored. */
void restore_signals(bool leave)
{
    if (job_handlers_set) {
	job_handlers_set = false;
	reset_special_handler(SIGTTIN, SIG_IGN, leave);
	reset_special_handler(SIGTTOU, SIG_IGN, leave);
	reset_special_handler(SIGTSTP, SIG_IGN, leave);
    }
    if (interactive_handlers_set) {
	interactive_handlers_set = false;
	reset_special_handler(SIGINT, sig_handler, leave);
	reset_special_handler(SIGTERM, SIG_IGN, leave);
	reset_special_handler(SIGQUIT, SIG_IGN, leave);
#if YASH_ENABLE_LINEEDIT && defined(SIGWINCH)
	reset_special_handler(SIGWINCH, sig_handler, leave);
#endif
    }
    if (main_handler_set) {
	sigset_t ss = official_sigmask;
	if (leave) {
	    main_handler_set = false;
	    reset_special_handler(SIGCHLD, sig_handler, leave);
	} else {
	    sigaddset(&ss, SIGCHLD);
	}
	sigprocmask(SIG_SETMASK, &ss, NULL);
    }
}

/* Re-sets the signal handler for SIGTTIN, SIGTTOU, and SIGTSTP according to the
 * current `doing_job_control_now' and `job_handlers_set'. */
void reset_job_signals(void)
{
    if (doing_job_control_now && !job_handlers_set) {
	job_handlers_set = true;
	set_special_handler(SIGTTIN, SIG_IGN);
	set_special_handler(SIGTTOU, SIG_IGN);
	set_special_handler(SIGTSTP, SIG_IGN);
    } else if (!doing_job_control_now && job_handlers_set) {
	job_handlers_set = false;
	reset_special_handler(SIGTTIN, SIG_IGN, false);
	reset_special_handler(SIGTTOU, SIG_IGN, false);
	reset_special_handler(SIGTSTP, SIG_IGN, false);
    }
}

/* Calls `sigaction' and, if the signal is not in either of
 * `originally_defaulted_signals' and `originally_ignored_signals', adds it to
 * one of them. */
int xsigaction(int signum, const struct sigaction *restrict newaction,
	struct sigaction *restrict oldaction)
{
    struct sigaction oldaction2;
    sigemptyset(&oldaction2.sa_mask);

    if (sigaction(signum, newaction, &oldaction2) < 0)
	return -1;

    if (oldaction != NULL)
	*oldaction = oldaction2;

    if (sigismember(&originally_defaulted_signals, signum) ||
	    sigismember(&originally_ignored_signals, signum))
	return 0;

    if (oldaction2.sa_handler != SIG_IGN)
	sigaddset(&originally_defaulted_signals, signum);
    else
	sigaddset(&originally_ignored_signals, signum);

    return 0;
}

/* Sets the signal handler of signal `signum' to function `handler', which must
 * be either SIG_IGN or `sig_handler'.
 * If the signal is not in either of `originally_defaulted_signals' and
 * `originally_ignored_signals', it is added to one of them.
 * If the previous signal handler was SIG_IGN, the signal is added to
 * `officially_ignored_signals'.
 * If `handler' is SIG_IGN and the trap for the signal is set, the signal
 * handler is not changed. */
/* Note that this function does not block or unblock the specified signal. */
void set_special_handler(int signum, void (*handler)(int signum))
{
    if (!is_phantom && sigismember(&trapped_signals, signum))
	return;  /* The signal handler has already been set. */

    struct sigaction action, oldaction;
    action.sa_flags = 0;
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    sigemptyset(&oldaction.sa_mask);
    if (xsigaction(signum, &action, &oldaction) >= 0)
	if (oldaction.sa_handler == SIG_IGN)
	    sigaddset(&officially_ignored_signals, signum);
}

/* Resets the signal handler for signal `signum' to what external commands
 * should inherit from the shell. The handler that have been passed to
 * `set_special_handler' must be passed as `handler'. If `leave' is true, the
 * current process is assumed to be about to exec: the handler may be left
 * unchanged if the handler is supposed to be reset during exec. */
void reset_special_handler(int signum, void (*handler)(int signum), bool leave)
{
    if (sigismember(&trapped_signals, signum)) {
	assert(!sigismember(&officially_ignored_signals, signum));
	return;
    }

    struct sigaction action;
    if (sigismember(&officially_ignored_signals, signum))
	action.sa_handler = SIG_IGN;
    else
	action.sa_handler = SIG_DFL;

    if (leave && handler != SIG_IGN)
	handler = SIG_DFL;

    if (handler != action.sa_handler) {
	action.sa_flags = 0;
	sigemptyset(&action.sa_mask);
	sigaction(signum, &action, NULL);
    }
}

/* Unblocks SIGINT so that system calls can be interrupted.
 * First, this function must be called with the argument of true and this
 * function unblocks SIGINT. Later, this function must be called with the
 * argument of false and SIGINT is re-blocked.
 * This function is effective only if the shell is interactive. */
void set_interruptible_by_sigint(bool onoff)
{
    if (interactive_handlers_set) {
	sigset_t ss;
	sigemptyset(&ss);
	sigaddset(&ss, SIGINT);
	sigprocmask(onoff ? SIG_UNBLOCK : SIG_BLOCK, &ss, NULL);
    }
}

/* Sets the signal handler for SIGQUIT and SIGINT to SIG_IGN
 * to prevent an asynchronous job from being killed by these signals. */
void ignore_sigquit_and_sigint(void)
{
    struct sigaction action;

    if (!interactive_handlers_set) {
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = SIG_IGN;
	xsigaction(SIGQUIT, &action, NULL);
	xsigaction(SIGINT, &action, NULL);
    }  /* Don't set the handers if interactive because they are reset when
	  `restore_signals' is called later. */
    sigaddset(&officially_ignored_signals, SIGQUIT);
    sigaddset(&officially_ignored_signals, SIGINT);
}

/* Sets the action of SIGTSTP to ignoring the signal
 * to prevent a command substitution process from being stopped by SIGTSTP.
 * `doing_job_control_now' must be true. */
void ignore_sigtstp(void)
{
    assert(doing_job_control_now);
    /* Don't set the hander now because it is reset when `restore_signals' is
     * called later.
    struct sigaction action;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_handler = SIG_IGN;
    xsigaction(SIGTSTP, &action, NULL);
    */
    sigaddset(&officially_ignored_signals, SIGTSTP);
}

/* Sends SIGSTOP to the shell process (and the processes in the same process
 * group).
 * On error, an error message is printed to the standard error. */
void stop_myself(void)
{
    if (kill(0, SIGSTOP) < 0)
	xerror(errno, Ngt("cannot send SIGSTOP signal"));
}

/* the general signal handler */
void sig_handler(int signum)
{
    any_signal_received = true;
#if defined SIGRTMIN && defined SIGRTMAX
    int sigrtmin = SIGRTMIN;
    if (sigrtmin <= signum && signum <= SIGRTMAX) {
	size_t index = signum - sigrtmin;
	if (index < RTSIZE)
	    rtsignal_received[index] = true;
    } else
#endif
    {
	signal_received[sigindex(signum)] = true;

	switch (signum) {
	    case SIGCHLD:   sigchld_received  = true;   break;
	    case SIGINT:    sigint_received   = true;   break;
#if YASH_ENABLE_LINEEDIT && defined(SIGWINCH)
	    case SIGWINCH:  sigwinch_received = true;   break;
#endif
	}
    }
}

/* Accepts currently pending signals and calls `handle_sigchld' and
 * `handle_traps'. */
void handle_signals(void)
{
    sigset_t ss = accept_sigmask, savess;
    sigdelset(&ss, SIGCHLD);
    if (interactive_handlers_set)
	sigdelset(&ss, SIGINT);
    sigemptyset(&savess);
    sigprocmask(SIG_SETMASK, &ss, &savess);
    sigprocmask(SIG_SETMASK, &savess, NULL);

    handle_sigchld();
    handle_traps();
}

/* Waits for SIGCHLD to be caught and call `handle_sigchld'.
 * If SIGCHLD is already caught, this function doesn't wait.
 * If `interruptible' is true, this function can be canceled by SIGINT.
 * If `return_on_trap' is true, this function returns immediately after a trap
 * is handled. Otherwise, traps are not handled.
 * Returns the signal number if interrupted or zero if successful. */
int wait_for_sigchld(bool interruptible, bool return_on_trap)
{
    int result = 0;

    sigset_t ss = accept_sigmask;
    sigdelset(&ss, SIGCHLD);
    if (interruptible)
	sigdelset(&ss, SIGINT);

    for (;;) {
	if (return_on_trap && ((result = handle_traps()) != 0))
	    break;
	if (interruptible && sigint_received)
	    break;
	if (sigchld_received)
	    break;
	if (sigsuspend(&ss) < 0) {
	    if (errno != EINTR) {
		xerror(errno, "sigsuspend");
		break;
	    }
	}
    }

    if (interruptible && sigint_received)
	result = SIGINT;
    handle_sigchld();
    return result;
}

/* Waits for the specified file descriptor to be available for reading.
 * `handle_sigchld' and `handle_sigwinch' are called to handle SIGCHLD and
 * SIGWINCH that are caught while waiting.
 * If `trap' is true, traps are also handled while waiting and the
 * `sigint_received' flag is cleared when this function returns. The return
 * value can be W_INTERRUPTED only if `trap' is true. Any SIGINT received before
 * entering this function is ignored.
 * The maximum time length of wait is specified by `timeout' in milliseconds.
 * If `timeout' is negative, the wait time is unlimited.
 * If the wait is interrupted by a signal, this function will re-wait for the
 * specified timeout, which means that this function may wait for a time length
 * longer than the specified timeout. */
enum wait_for_input_T wait_for_input(int fd, bool trap, int timeout)
{
    sigset_t ss;
    struct timespec to;
    struct timespec *top;

    assert(fd >= 0);
    if (fd >= FD_SETSIZE) {
	xerror(0, Ngt("too many files are opened for yash to handle"));
	return W_ERROR;
    }

    if (trap)
	sigint_received = false;

    ss = accept_sigmask;
    sigdelset(&ss, SIGCHLD);
    if (interactive_handlers_set) {
	sigdelset(&ss, SIGINT);
#if YASH_ENABLE_LINEEDIT && defined SIGWINCH
	sigdelset(&ss, SIGWINCH);
#endif
    }

    if (timeout < 0) {
	top = NULL;
    } else {
	to.tv_sec  = timeout / 1000;
	to.tv_nsec = timeout % 1000 * 1000000;
	top = &to;
    }

    for (;;) {
	handle_sigchld();
	if (trap)
	    handle_traps();
#if YASH_ENABLE_LINEEDIT && defined SIGWINCH
	handle_sigwinch();
#endif
	if (trap && sigint_received) {
	    sigint_received = false;
	    return W_INTERRUPTED;
	}

	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);

	int count = pselect(fd + 1, &fdset, NULL, NULL, top, &ss);

	if (trap && sigint_received) {
	    sigint_received = false;
	    return W_INTERRUPTED;
	}

	if (count >= 0)
	    return FD_ISSET(fd, &fdset) ? W_READY : W_TIMED_OUT;

	if (errno != EINTR) {
	    xerror(errno, "pselect");
	    return W_ERROR;
	}
    }
}

/* Handles SIGCHLD if caught. */
void handle_sigchld(void)
{
    if (sigchld_received) {
        sigchld_received = false;
        do_wait();
    }

    if (any_job_status_has_changed()) {
	/* print job status if the notify option is set */
#if YASH_ENABLE_LINEEDIT
	if (le_state & LE_STATE_ACTIVE) {
	    if (!(le_state & LE_STATE_COMPLETING)) {
		if (shopt_notify || shopt_notifyle) {
		    le_suspend_readline();
		    print_job_status_all();
		    le_resume_readline();
		}
	    }
	} else
#endif
	if (shopt_notify) {
	    sigset_t ss, savess;
	    sigemptyset(&ss);
	    sigaddset(&ss, SIGTTOU);
	    sigemptyset(&savess);
	    sigprocmask(SIG_BLOCK, &ss, &savess);
	    print_job_status_all();
	    sigprocmask(SIG_SETMASK, &savess, NULL);
	}
    }
}

/* Executes the trap handlers for trapped signals if any.
 * There must not be an active job when this function is called.
 * Returns the signal number if any handler was executed, otherwise zero.
 * Note that, if more than one signal is caught, only one of their numbers is
 * returned. */
int handle_traps(void)
{
    /* Signal handler execution is not reentrant because the value of
     * `savelaststatus' would be lost. But the EXIT is the only exception:
     * The EXIT trap may be executed inside another trap. */
    if (!any_trap_set || !any_signal_received || handled_signal >= 0)
	return 0;
#if YASH_ENABLE_LINEEDIT
    /* Don't handle traps during command line completion. Otherwise, the command
     * line would be messed up! */
    if (le_state & LE_STATE_COMPLETING)
	return 0;
#endif

    int signum = 0;
    bool save_sigint_received = sigint_received;
    savelaststatus = laststatus;

    sigint_received = false;

    do {
	/* we reset this before executing signal handlers to avoid race */
	any_signal_received = false;

	for (const signal_T *s = signals; s->no != 0; s++) {
	    size_t i = sigindex(s->no);
	    if (signal_received[i]) {
		signal_received[i] = false;
		wchar_t *command = trap_command[i];
		if (!is_phantom && command != NULL && command[0] != L'\0') {
#if YASH_ENABLE_LINEEDIT
		    le_suspend_readline();
#endif
		    struct execstate_T *execstate = save_execstate();
		    reset_execstate(true);
		    signum = handled_signal = s->no;
		    command = xwcsdup(command);
		    exec_wcs(command, "trap", false);
		    free(command);
		    cancel_return();
		    restore_execstate(execstate);
		    laststatus = savelaststatus;
		}
	    }
	}
#if defined SIGRTMIN && defined SIGRTMAX
	int sigrtmin = SIGRTMIN, range = SIGRTMAX - sigrtmin + 1;
	if (range > RTSIZE)
	    range = RTSIZE;
	for (int i = 0; i < range; i++) {
	    if (rtsignal_received[i]) {
		rtsignal_received[i] = false;
		wchar_t *command = rttrap_command[i];
		if (!is_phantom && command != NULL && command[0] != L'\0') {
#if YASH_ENABLE_LINEEDIT
		    le_suspend_readline();
#endif
		    struct execstate_T *execstate = save_execstate();
		    reset_execstate(true);
		    signum = handled_signal = sigrtmin + i;
		    command = xwcsdup(command);
		    exec_wcs(command, "trap", false);
		    free(command);
		    cancel_return();
		    restore_execstate(execstate);
		    laststatus = savelaststatus;
		}
	    }
	}
#endif  /* defined SIGRTMAX && defined SIGRTMIN */
    } while (any_signal_received);

    sigint_received |= save_sigint_received;
    savelaststatus = -1;
    handled_signal = -1;
#if YASH_ENABLE_LINEEDIT
    if (shopt_notifyle && (le_state & LE_STATE_SUSPENDED))
	print_job_status_all();
    le_resume_readline();
#endif

    return signum;
}

/* Executes the EXIT trap if any.
 * This function calls `reset_execstate' without saving `execstate'. */
void execute_exit_trap(void)
{
    if (is_phantom)
	return;

    wchar_t *command = trap_command[sigindex(0)];
    if (command != NULL) {
	savelaststatus = laststatus;
	command = xwcsdup(command);
	reset_execstate(true);
	exec_wcs(command, "EXIT trap", false);
	free(command);
	savelaststatus = -1;
    }
}

/* Sets trap for the signal `signum' to `command'.
 * If `command' is NULL, the trap is reset to the default.
 * If `command' is an empty string, the trap is set to SIG_IGN.
 * This function may call `get_signal_name'.
 * If `is_phantom` is true, `(rt)trap_command' is not modified.
 * An error message is printed to the standard error on error. */
void set_trap(int signum, const wchar_t *command)
{
    if (signum == SIGKILL || signum == SIGSTOP) {
	xerror(0, Ngt("SIG%ls cannot be trapped"),
		signum == SIGKILL ? L"KILL" : L"STOP");
	return;
    }

    wchar_t **commandp;
    volatile sig_atomic_t *receivedp;
#if defined SIGRTMIN && defined SIGRTMAX
    int sigrtmin = SIGRTMIN;
    if (sigrtmin <= signum && signum <= SIGRTMAX) {
	size_t index = signum - sigrtmin;
	if (index < RTSIZE) {
	    commandp = &rttrap_command[index];
	    receivedp = &rtsignal_received[index];
	} else {
	    xerror(0, Ngt("real-time signal SIG%ls is not supported"),
		    get_signal_name(signum));
	    return;
	}
    } else
#endif
    {
	size_t index = sigindex(signum);
	commandp = &trap_command[index];
	receivedp = &signal_received[index];
    }

    if (!is_interactive && *commandp == NULL && is_originally_ignored(signum)) {
	/* Signals that were ignored on entry to a non-interactive shell cannot
	 * be trapped or reset. (POSIX) */
#if FIXED_SIGNAL_AS_ERROR
	xerror(0, Ngt("SIG%ls cannot be reset"), get_signal_name(signum));
#endif
	return;
    }

    if (!is_phantom) {
	free(*commandp);
	if (command != NULL) {
	    if (command[0] != L'\0')
		any_trap_set = true;
	    *commandp = xwcsdup(command);
	} else {
	    *commandp = NULL;
	}
	*receivedp = false;
    }
    if (signum == 0)
	return;

    struct sigaction action;
    if (command == NULL)
	action.sa_handler = SIG_DFL;
    else if (command[0] == L'\0')
	action.sa_handler = SIG_IGN;
    else
	action.sa_handler = sig_handler;

    if (action.sa_handler == SIG_IGN) {
	sigaddset(&officially_ignored_signals, signum);
    } else {
	sigdelset(&officially_ignored_signals, signum);
    }
    if (action.sa_handler == sig_handler) {
	sigdelset(&official_sigmask, signum);
	sigaddset(&trapped_signals, signum);
	sigdelset(&accept_sigmask, signum);
    } else {
	sigdelset(&trapped_signals, signum);
    }

    switch (signum) {
	case SIGCHLD:
	    /* SIGCHLD's signal handler is always `sig_handler' */
	    return;
	case SIGINT:
#if YASH_ENABLE_LINEEDIT && defined SIGWINCH
	case SIGWINCH:
#endif
	    /* SIGINT and SIGWINCH's signal handler is always `sig_handler'
	     * when interactive */
	    if (interactive_handlers_set)
		return;
	    break;
	case SIGTTIN:
	case SIGTTOU:
	case SIGTSTP:
	    if (job_handlers_set)
		goto default_ignore;
	    break;
	case SIGTERM:
	case SIGQUIT:
	    if (interactive_handlers_set)
		goto default_ignore;
	    break;
default_ignore:
	    if (action.sa_handler == SIG_DFL)
		action.sa_handler = SIG_IGN;
	    break;
    }

    if (action.sa_handler == sig_handler)
	sigprocmask(SIG_BLOCK, &trapped_signals, NULL);

    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (xsigaction(signum, &action, NULL) < 0) {
	int saveerrno = errno;
	xerror(saveerrno, "sigaction(SIG%ls)", get_signal_name(signum));
    }
}

/* Checks if the specified signal was originally ignored when the shell was
 * invoked. Asserts the shell is not interactive. */
bool is_originally_ignored(int signum)
{
    assert(!is_interactive_now);

    if (signum == 0)
	return false;

    if (sigismember(&originally_ignored_signals, signum))
	return true;
    if (sigismember(&originally_defaulted_signals, signum))
	return false;

    xsigaction(signum, NULL, NULL);
    return sigismember(&originally_ignored_signals, signum);
}

/* Clears the EXIT trap. */
void clear_exit_trap(void)
{
    set_trap(0, NULL);
}

/* Change all traps into phantom except that are set to SIG_IGN. */
void phantomize_traps(void)
{
    if (!any_trap_set && !any_signal_received)
	return;

    is_phantom = true;

    {
	size_t index = sigindex(0);
	wchar_t *command = trap_command[index];
	if (command != NULL && command[0] != L'\0')
	    set_trap(0, NULL);
	signal_received[index] = false;
    }
    for (const signal_T *s = signals; s->no != 0; s++) {
	size_t index = sigindex(s->no);
	wchar_t *command = trap_command[index];
	if (command != NULL && command[0] != L'\0')
	    set_trap(s->no, NULL);
	signal_received[index] = false;
    }
#if defined SIGRTMIN && defined SIGRTMAX
    for (int sigrtmin = SIGRTMIN, i = 0; i < RTSIZE; i++) {
	wchar_t *command = rttrap_command[i];
	if (command != NULL && command[0] != L'\0')
	    set_trap(sigrtmin + i, NULL);
	rtsignal_received[i] = false;
    }
#endif
    any_trap_set = false, any_signal_received = false;
}

/* Remove all phantoms from `trap_command` and `rttrap_command`. */
void banish_phantoms(void)
{
    if (!is_phantom)
	return;
    is_phantom = false;

    {
	size_t index = sigindex(0);
	wchar_t *command = trap_command[index];
	if (command != NULL && command[0] != L'\0') {
	    free(command);
	    trap_command[index] = NULL;
	}
    }
    for (const signal_T *s = signals; s->no != 0; s++) {
	size_t index = sigindex(s->no);
	wchar_t *command = trap_command[index];
	if (command != NULL && command[0] != L'\0') {
	    free(command);
	    trap_command[index] = NULL;
	}
    }
#if defined SIGRTMIN && defined SIGRTMAX
    for (int i = 0; i < RTSIZE; i++) {
	wchar_t *command = rttrap_command[i];
	if (command != NULL && command[0] != L'\0') {
	    free(command);
	    rttrap_command[i] = NULL;
	}
    }
#endif
}

/* Tests the `sigint_received' flag. Returns true only if interactive. */
bool is_interrupted(void)
{
    return is_interactive_now && sigint_received;
}

/* Sets `laststatus' to (SIGINT + TERMSIGOFFSET) if the shell has been
 * interrupted. */
void set_laststatus_if_interrupted(void)
{
    if (is_interrupted())
	laststatus = SIGINT + TERMSIGOFFSET;
}

/* Sets the `sigint_received' flag. */
void set_interrupted(void)
{
    sigint_received = true;
}

#if YASH_ENABLE_LINEEDIT

#ifdef SIGWINCH

/* If SIGWINCH has been caught and line-editing is currently active, cause
 * line-editing to redraw the display. */
void handle_sigwinch(void)
{
    if (sigwinch_received)
	le_display_size_changed();
}

#endif /* defined SIGWINCH */

/* Resets the `sigwinch_received' flag. */
void reset_sigwinch(void)
{
#ifdef SIGWINCH
    sigwinch_received = false;
#endif
}

/* Generates completion candidates for signal names matching the pattern. */
/* The prototype of this function is declared in "lineedit/complete.h". */
void generate_signal_candidates(const le_compopt_T *compopt)
{
    if (!(compopt->type & CGT_SIGNAL))
	return;

    le_compdebug("adding signal name candidates");
    if (!le_compile_cpatterns(compopt))
	return;

    bool prefix = matchwcsprefix(compopt->src, L"SIG");
    xwcsbuf_T buf;
    wb_init(&buf);
    for (const signal_T *s = signals; s->no != 0; s++) {
	if (prefix)
	    wb_cat(&buf, L"SIG");
	wb_cat(&buf, s->name);
	sig_new_candidate(compopt, s->no, &buf);
    }
#if defined SIGRTMIN && defined SIGRTMAX
    int sigrtmin = SIGRTMIN, sigrtmax = SIGRTMAX;
    for (int s = sigrtmin; s <= sigrtmax; s++) {
	if (prefix)
	    wb_cat(&buf, L"SIG");
	wb_cat(&buf, L"RTMIN");
	if (s != sigrtmin)
	    wb_wprintf(&buf, L"+%d", s - sigrtmin);
	sig_new_candidate(compopt, s, &buf);

	if (prefix)
	    wb_cat(&buf, L"SIG");
	wb_cat(&buf, L"RTMAX");
	if (s != sigrtmax)
	    wb_wprintf(&buf, L"-%d", sigrtmax - s);
	sig_new_candidate(compopt, s, &buf);
    }
#endif
    wb_destroy(&buf);
}

/* If the pattern in `compopt' matches the specified signal name, adds a new
 * completion candidate with the name and the description for the signal. */
void sig_new_candidate(
	const le_compopt_T *restrict compopt, int num, xwcsbuf_T *restrict name)
{
    if (le_wmatch_comppatterns(compopt, name->contents)) {
	xwcsbuf_T desc;
	wb_init(&desc);
#if HAVE_STRSIGNAL
	char *mbsdesc = strsignal(num);
	if (mbsdesc != NULL)
	    wb_wprintf(&desc, L"%d: %s", num, mbsdesc);
	else
#endif
	    wb_wprintf(&desc, L"%d", num);
	le_new_candidate(CT_SIG, wb_towcs(name), wb_towcs(&desc), compopt);
	wb_init(name);
    } else {
	wb_clear(name);
    }
}

#endif /* YASH_ENABLE_LINEEDIT */


/********** Built-in **********/

static bool print_trap(const wchar_t *signame, const wchar_t *command)
    __attribute__((nonnull(1)));
static bool print_signal(int signum, const wchar_t *name, bool verbose)
    __attribute__((nonnull));
static void signal_job(int signum, const wchar_t *jobname)
    __attribute__((nonnull));

/* Options for the "trap" built-in. */
const struct xgetopt_T trap_options[] = {
    { L'p', L"print", OPTARG_NONE, false, NULL, },
#if YASH_ENABLE_HELP
    { L'-', L"help",  OPTARG_NONE, false, NULL, },
#endif
    { L'\0', NULL, 0, false, NULL, },
};

/* The "trap" built-in. */
int trap_builtin(int argc, void **argv)
{
    bool print = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, trap_options, 0)) != NULL) {
	switch (opt->shortopt) {
	    case L'p':
		print = true;
		break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return special_builtin_error(Exit_ERROR);
	}
    }

    if (xoptind == argc) {
	/* print all traps */
	sigset_t printed;
	sigemptyset(&printed);
	if (!print_trap(L"EXIT", trap_command[sigindex(0)]))
	    return Exit_FAILURE;
	for (const signal_T *s = signals; s->no != 0; s++) {
	    if (!sigismember(&printed, s->no)) {
		sigaddset(&printed, s->no);
		if (!print_trap(s->name, trap_command[sigindex(s->no)]))
		    return special_builtin_error(Exit_FAILURE);
	    }
	}
#if defined SIGRTMIN && defined SIGRTMAX
	int sigrtmin = SIGRTMIN, sigrtmax = SIGRTMAX;
	for (int i = 0; i < RTSIZE; i++) {
	    if (sigrtmin + i > sigrtmax)
		break;
	    if (!print_trap(get_signal_name(sigrtmin + i), rttrap_command[i]))
		return special_builtin_error(Exit_FAILURE);
	}
#endif
    } else if (print) {
	/* print specified traps */
#if defined SIGRTMIN && defined SIGRTMAX
	int sigrtmin = SIGRTMIN, sigrtmax = SIGRTMAX;
#endif
	do {
	    wchar_t *name = ARGV(xoptind);
	    int signum = get_signal_number_toupper(name);
	    if (signum < 0) {
		xerror(0, Ngt("no such signal `%ls'"), name);
		continue;
	    }

#if defined SIGRTMIN && defined SIGRTMAX
	    if (sigrtmin <= signum && signum <= sigrtmax) {
		int index = signum - sigrtmin;
		if (index < RTSIZE)
		    if (!print_trap(name, rttrap_command[index]))
			return special_builtin_error(Exit_FAILURE);
	    } else
#endif
	    {
		if (!print_trap(name, trap_command[sigindex(signum)]))
		    return special_builtin_error(Exit_FAILURE);
	    }
	} while (++xoptind < argc);
    } else {
	const wchar_t *command;

	/* check if the first operand is an integer */
	wchar_t *end;
	errno = 0;
	if (iswdigit(ARGV(xoptind)[0])
		&& (wcstoul(ARGV(xoptind), &end, 10), *end == L'\0')) {
	    command = NULL;
	} else {
	    command = ARGV(xoptind++);
	    if (xoptind == argc)
		return special_builtin_error(insufficient_operands_error(2));

	    if (wcscmp(command, L"-") == 0)
		command = NULL;
	}

	/* set traps */
	banish_phantoms();
	do {
	    wchar_t *name = ARGV(xoptind);
	    int signum = get_signal_number_toupper(name);
	    if (signum < 0) {
		xerror(0, Ngt("no such signal `%ls'"), name);
		continue;
	    }
	    set_trap(signum, command);
	} while (++xoptind < argc);
    }

    return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;
}

/* Prints trap to the standard output in a format that can be used to restore
 * the current signal handler for the specified signal.
 * If the `command' is NULL, this function does nothing.
 * Otherwise, the `command' is properly single-quoted and printed.
 * Returns true iff successful (no error). On error, an error message is printed
 * to the standard error. */
bool print_trap(const wchar_t *signame, const wchar_t *command)
{
    if (command == NULL)
	return true;

    wchar_t *q = quote_as_word(command);
    bool ok = xprintf("trap -- %ls %ls\n", q, signame);
    free(q);
    return ok;
}

#if YASH_ENABLE_HELP
const char trap_help[] = Ngt(
"set or print signal handlers"
);
const char trap_syntax[] = Ngt(
"\ttrap [action signal...]\n"
"\ttrap signal_number [signal...]\n"
"\ttrap -p [signal...]\n"
);
#endif

/* The "kill" built-in, which accepts the following options:
 *  -s sig: specifies the signal to send
 *  -n num: specifies the signal to send by number
 *  -l: prints signal info
 *  -v: prints signal info verbosely */
int kill_builtin(int argc, void **argv)
{
    int signum = SIGTERM;
    bool list = false, verbose = false;

    /* We don't use the xgetopt function to parse options because the kill
     * built-in has non-standard syntax. */
    int optind;
    for (optind = 1; optind < argc; optind++) {
	wchar_t *arg = ARGV(optind);
	if (arg[0] != L'-' || arg[1] == L'\0')
	    break;
	for (size_t i = 1; arg[i] != L'\0'; i++) {
	    switch (arg[i]) {
		case L'n':  case L's':
		/* we don't make any differences between -n and -s options */
		    if (list)
			return mutually_exclusive_option_error(arg[i], L'l');

		    arg = &arg[i + 1];
		    if (arg[0] == L'\0') {
			arg = ARGV(++optind);
			if (arg == NULL) {
			    xerror(0, Ngt("the signal name is not specified"));
			    return Exit_ERROR;
			}
		    }
parse_signal_name:
		    if (posixly_correct && matchwcsprefix(arg, L"SIG")) {
			xerror(0, Ngt("%ls: the signal name must be specified "
				    "without `SIG'"), arg);
			return Exit_ERROR;
		    }
		    signum = get_signal_number_toupper(arg);
		    if (signum < 0 || (signum == 0 && !iswdigit(arg[0]))) {
			xerror(0, Ngt("no such signal `%ls'"), arg);
			return Exit_FAILURE;
		    }
		    optind++;
		    if (optind < argc && wcscmp(ARGV(optind), L"--") == 0)
			optind++;
		    goto main;
		case L'l':
		    list = true;
		    break;
		case L'v':
		    list = verbose = true;
		    break;
		case L'-':
		    if (i == 1) {
			if (arg[2] == L'\0') {  /* `arg' is "--" */
			    optind++;
			    goto main;
			}
#if YASH_ENABLE_HELP
			if (!posixly_correct && matchwcsprefix(L"--help", arg))
			    return print_builtin_help(ARGV(0));
#endif
		    }
		    /* falls thru! */
		default:
		    if (i == 1 && !list) {
			arg = &arg[i];
			goto parse_signal_name;
		    } else {
			xerror(0, Ngt("`%ls' is not a valid option"), arg);
			return Exit_ERROR;
		    }
	    }
	}
    }

main:
    if (list) {
	if (optind == argc) {
	    /* print info of all signals */
	    for (const signal_T *s = signals; s->no != 0; s++)
		if (!print_signal(s->no, s->name, verbose))
		    return Exit_FAILURE;
#if defined SIGRTMIN && defined SIGRTMAX
	    for (int i = SIGRTMIN, max = SIGRTMAX; i <= max; i++)
		if (!print_signal(i, get_signal_name(i), verbose))
		    return Exit_FAILURE;
#endif
	} else {
	    /* print info of the specified signals */
	    do {
		int signum;
		const wchar_t *signame;

		if (xwcstoi(ARGV(optind), 10, &signum) && signum >= 0) {
		    if (signum >= TERMSIGOFFSET)
			signum -= TERMSIGOFFSET;
		    else if (signum >= (TERMSIGOFFSET & 0xFF))
			signum -= (TERMSIGOFFSET & 0xFF);
		} else {
		    signum = get_signal_number_toupper(ARGV(optind));
		}
		signame = get_signal_name(signum);
		if (signum <= 0 || signame[0] == L'\0') {
		    xerror(0, Ngt("no such signal `%ls'"), ARGV(optind));
		    continue;
		}

		if (!print_signal(signum, signame, verbose))
		    return Exit_FAILURE;
	    } while (++optind < argc);
	}
    } else {
	/* send signal */
	if (optind == argc)
	    return insufficient_operands_error(1);

	do {
	    wchar_t *proc = ARGV(optind);
	    if (proc[0] == L'%') {
		signal_job(signum, proc);
	    } else {
		long pid;

		if (!xwcstol(proc, 10, &pid)) {
		    xerror(0, Ngt("`%ls' is not a valid integer"), proc);
		    continue;
		}
		// XXX this cast might not be safe
		if (kill((pid_t) pid, signum) < 0) {
		    xerror(errno, "%ls", proc);
		    continue;
		}
	    }
	} while (++optind < argc);
    }
    return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;
}

/* Prints info about the specified signal.
 * `signum' must be a valid signal number and `name' must be the name of the
 * signal.
 * Returns false iff an IO error occurred. */
bool print_signal(int signum, const wchar_t *name, bool verbose)
{
    if (!verbose) {
	return xprintf("%ls\n", name);
    } else {
#if HAVE_STRSIGNAL
	const char *sigdesc = strsignal(signum);
	if (sigdesc != NULL)
	    return xprintf("%d\t%-10ls %s\n", signum, name, sigdesc);
	else
#endif
	    return xprintf("%d\t%-10ls\n", signum, name);
    }
}

/* Sends the specified signal to the specified job.
 * Returns true iff successful. On error, an error message is printed to the
 * standard error. */
void signal_job(int signum, const wchar_t *jobspec)
{
    pid_t jobpgid = get_job_pgid(jobspec);
    if (jobpgid <= 0)
	return;

    if (kill(-jobpgid, signum) < 0)
	xerror(errno, "%ls", jobspec);
}

#if YASH_ENABLE_HELP
const char kill_help[] = Ngt(
"send a signal to processes"
);
const char kill_syntax[] = Ngt(
"\tkill [-signal|-s signal|-n number] process...\n"
"\tkill -l [-v] [number...]\n"
);
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
