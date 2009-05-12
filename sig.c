/* Yash: yet another shell */
/* sig.c: signal handling */
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


#include "common.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
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
#include "sig.h"
#include "siglist.h"
#include "signum.h"
#include "strbuf.h"
#include "util.h"
#include "yash.h"
#if YASH_ENABLE_LINEEDIT
# include "lineedit/lineedit.h"
#endif


/* About the shell's signal handling:
 *
 * Yash always catches SIGCHLD.
 * When job control is active, SIGTTOU and SIGTSTP are ignored.
 * If the shell is interactive, SIGINT, SIGTERM and SIGQUIT are also ignored.
 * Other signals are caught if a trap for the signals are set.
 *
 * SIGQUIT and SIGINT are ignored in an asynchronous list.
 * SIGTSTP is ignored in a command substitution in an interactive shell.
 * SIGTTOU is blocked during `tcsetpgrp'.
 * SIGCHLD and SIGINT are blocked to avoid a race condition
 *  - while reading the output of a command substitution, or
 *  - in `wait_for_job'. */


static void set_special_handler(int signum, void (*handler)(int signum))
    __attribute__((nonnull));
static void reset_special_handler(int signum);
static inline bool is_ignored(int signum)
    __attribute__((pure));
static void sig_handler(int signum);
static bool set_trap(int signum, const wchar_t *command);


/* Checks if there exists a process with the specified process ID, which should
 * be positive. */
bool process_exists(pid_t pid)
{
    return kill(pid, 0) >= 0 || errno != ESRCH;
}

/* Returns the name of the signal with the specified number.
 * The returned name doesn't have a "SIG"-prefix.
 * "?" is returned for an unknown signal number.
 * The returned string is valid until the next call to this function. */
const char *get_signal_name(int signum)
{
    if (signum == 0)
	return "EXIT";
#if defined SIGRTMIN && defined SIGRTMAX
    int sigrtmin = SIGRTMIN, sigrtmax = SIGRTMAX;
    if (sigrtmin <= signum && signum <= sigrtmax) {
	static char *name = NULL;
	if (signum == sigrtmin)
	    return "RTMIN";
	if (signum == sigrtmax)
	    return "RTMAX";
	int range = sigrtmax - sigrtmin, diff = signum - sigrtmin;
	free(name);
	if (diff <= range / 2)
	    name = malloc_printf("RTMIN+%d", diff);
	else
	    name = malloc_printf("RTMAX-%d", sigrtmax - signum);
	return name;
    }
#endif
    for (const signal_T *s = signals; s->no; s++)
	if (s->no == signum)
	    return s->name;
    return "?";
}

/* Returns the number of a signal whose name is `name'.
 * Returns 0 for "EXIT" and -1 for an unknown name.
 * `name' should be all in uppercase. */
int get_signal_number(const char *name)
{
    if (isdigit(name[0])) {  /* parse a decimal integer */
	long num;
	char *end;
	errno = 0;
	num = strtol(name, &end, 10);
	if (errno || *end != '\0' || num < 0 || num > INT_MAX)
	    return -1;

	/* check if `num' is a valid signal */
	int signum = (int) num;
	if (signum == 0)
	    return 0;
#if defined SIGRTMIN && defined SIGRTMAX
	if (SIGRTMIN <= signum && signum <= SIGRTMAX)
	    return signum;
#endif
	for (const signal_T *s = signals; s->no; s++)
	    if (s->no == signum)
		return signum;
	return -1;
    }

    if (strcmp(name, "EXIT") == 0)
	return 0;
    if (strncmp(name, "SIG", 3) == 0)
	name += 3;
    for (const signal_T *s = signals; s->no; s++)
	if (strcmp(name, s->name) == 0)
	    return s->no;
#if defined SIGRTMIN && defined SIGRTMAX
    if (strncmp(name, "RTMIN", 5) == 0) {
	int sigrtmin = SIGRTMIN;
	name += 5;
	if (name[0] == '\0') {
	    return sigrtmin;
	} else if (name[0] == '+') {
	    long num;
	    char *end;
	    errno = 0;
	    num = strtol(name, &end, 10);
	    if (errno || *end != '\0' || num < 0 || sigrtmin + num >= SIGRTMAX)
		return -1;
	    return sigrtmin + (int) num;
	}
    } else if (strncmp(name, "RTMAX", 5) == 0) {
	int sigrtmax = SIGRTMAX;
	name += 5;
	if (name[0] == '\0') {
	    return sigrtmax;
	} else if (name[0] == '-') {
	    long num;
	    char *end;
	    errno = 0;
	    num = strtol(name, &end, 10);
	    if (errno || *end != '\0' || num > 0 || sigrtmax + num < SIGRTMIN)
		return -1;
	    return sigrtmax + (int) num;
	}
    }
#endif
    return -1;
}

/* Returns the number of a signal whose name is `name'.
 * Returns 0 for "EXIT" and -1 for an unknown name.
 * The given string is converted into uppercase. */
int get_signal_number_w(wchar_t *name)
{
    for (wchar_t *n = name; *n; n++)
	*n = towupper(*n);
    char *mname = malloc_wcstombs(name);
    if (!mname)
	return -1;
    int result = get_signal_number(mname);
    free(mname);
    return result;
}


/* set to true when any trap other than ignore is set */
bool any_trap_set = false;

/* flag to indicate a signal is caught. */
static volatile sig_atomic_t any_signal_received = false;

/* the signal for which trap is currently executed */
static int handled_signal = -1;
/* set to true when the EXIT trap is executed */
static bool exit_handled = false;

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

/* The signal mask the shell inherited on invocation.
 * This mask must be inherited by commands the shell invokes.
 * When a signal's trap is set, the signal is removed from this mask. */
static sigset_t blocked_signals;
/* Set of signals whose handler was SIG_IGN when the shell is invoked but
 * currently is substituted with the shell's handler.
 * The handler of these signals must be reset to SIG_IGN before the shell
 * invokes another command so that the command inherits SIG_IGN as the handler.
 * A signal is added to this set also when its trap handler is set to "ignore."
 */
static sigset_t ignored_signals;

/* This flag is set to true as well as `signal_received[sigindex(SIGCHLD/INT)]'
 * when SIGCHLD/SIGINT is caught.
 * This flag is used for job control while `signal_received[...]' is for trap
 * handling. */
static volatile sig_atomic_t sigchld_received, sigint_received;

/* true iff SIGCHLD is handled. */
static bool initialized = false;
/* true iff SIGTTOU/TSTP are ignored. */
static bool job_initialized = false;
/* true iff SIGTERM/INT/QUIT are ignored. */
static bool interactive_initialized = false;

/* Initializes signal handling.
 * May be called more than once. */
void init_signal(void)
{
    if (!initialized) {
	sigset_t ss;

	initialized = true;
	assert(!job_initialized);
	assert(!interactive_initialized);

	sigemptyset(&ignored_signals);

	sigemptyset(&blocked_signals);
	sigemptyset(&ss);
	sigaddset(&ss, SIGCHLD);
#if YASH_ENABLE_LINEEDIT && defined(SIGWINCH)
	sigaddset(&ss, SIGWINCH);
#endif
	if (sigprocmask(SIG_UNBLOCK, &ss, &blocked_signals) < 0)
	    xerror(errno, "sigprocmask");
	set_special_handler(SIGCHLD, sig_handler);
    }
}

/* Sets signal actions for SIGTTOU/TSTP/INT/TERM according to
 * `doing_job_control_now' and `is_interactive_now'. */
void set_signals(void)
{
    if (doing_job_control_now)
	set_job_signals();
    else
	restore_job_signals();
    if (is_interactive_now)
	set_interactive_signals();
    else
	restore_interactive_signals();
}

/* Sets signal actions for SIGTTOU/TSTP if `job_initialized' is false. */
void set_job_signals(void)
{
    if (!job_initialized) {
	job_initialized = true;
	set_special_handler(SIGTTOU, SIG_IGN);
	set_special_handler(SIGTSTP, SIG_IGN);
	/* don't have to unblock these signals because they are ignored anyway*/
    }
}

/* Sets signal actions for SIGINT/TERM if `interactive_initialized' is false. */
void set_interactive_signals(void)
{
    if (!interactive_initialized) {
	interactive_initialized = true;
	set_special_handler(SIGINT, SIG_IGN);
	set_special_handler(SIGTERM, SIG_IGN);
	set_special_handler(SIGQUIT, SIG_IGN);
	/* don't have to unblock these signals because they are ignored anyway*/
    }
}

/* Restores the initial signal actions and the signal mask for all signals. */
/* This function is called before calling the `exec' function. */
void restore_all_signals(void)
{
    restore_job_signals();
    restore_interactive_signals();
    if (initialized) {
	initialized = false;
	reset_special_handler(SIGCHLD);

	if (sigprocmask(SIG_SETMASK, &blocked_signals, NULL) < 0)
	    xerror(errno, "sigprocmask");
    }
}

/* Restores the initial signal actions for SIGTTOU/TSTP. */
void restore_job_signals(void)
{
    if (job_initialized) {
	job_initialized = false;
	reset_special_handler(SIGTTOU);
	reset_special_handler(SIGTSTP);
    }
}

/* Restores the initial signal actions for SIGINT/TERM/QUIT. */
void restore_interactive_signals(void)
{
    if (interactive_initialized) {
	interactive_initialized = false;
	reset_special_handler(SIGINT);
	reset_special_handler(SIGTERM);
	reset_special_handler(SIGQUIT);
    }
}

/* Sets the signal handler of `signum' to `handler'.
 * If the current handler is SIG_IGN, `signum' is added to `ignored_signals'. */
/* Note that this function does not unblock the specified signal. */
void set_special_handler(int signum, void (*handler)(int signum))
{
    if (!trap_command[sigindex(signum)]) {
	struct sigaction action, oldaction;
	action.sa_flags = 0;
	action.sa_handler = handler;
	sigemptyset(&action.sa_mask);
	sigemptyset(&oldaction.sa_mask);
	if (sigaction(signum, &action, &oldaction) >= 0) {
	    if (oldaction.sa_handler == SIG_IGN)
		sigaddset(&ignored_signals, signum);
	} else {
	    int saveerrno = errno;
	    xerror(saveerrno, "sigaction(SIG%s)", get_signal_name(signum));
	}
    }
}

/* Resets the signal handler of `signum' to what it should be. */
void reset_special_handler(int signum)
{
    struct sigaction action;
    action.sa_flags = 0;
    if (sigismember(&ignored_signals, signum))
	action.sa_handler = SIG_IGN;
    else if (trap_command[sigindex(signum)])
	return;
    else
	action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    if (sigaction(signum, &action, NULL) < 0) {
	int saveerrno = errno;
	xerror(saveerrno, "sigaction(SIG%s)", get_signal_name(signum));
    }
}

/* Checks if the specified signal is ignored. */
bool is_ignored(int signum)
{
    struct sigaction action;
    sigemptyset(&action.sa_mask);
    return sigaction(signum, NULL, &action) >= 0
	&& action.sa_handler == SIG_IGN;
}

/* Sets SIGINT to be caught if the shell is interactive (`onoff' = 1).
 * Restores SIGINT otherwise (`onoff' = 0).
 * The signal action for SIGINT set by this function must be restored before
 * any other signal handling. */
void set_interruptible_by_sigint(bool onoff)
{
    if (interactive_initialized) {
	const wchar_t *siginttrap = trap_command[sigindex(SIGINT)];
	if (!siginttrap || !siginttrap[0]) {
	    struct sigaction action;
	    sigemptyset(&action.sa_mask);
	    action.sa_flags = 0;
	    action.sa_handler = onoff ? sig_handler : SIG_IGN;
	    if (sigaction(SIGINT, &action, NULL) < 0)
		xerror(errno, "sigaction(SIG%s)", "INT");
	    if (onoff && sigismember(&blocked_signals, SIGINT)) {
		/* assert(sigisemptyset(&action.sa_mask)); */
		sigaddset(&action.sa_mask, SIGINT);
		if (sigprocmask(SIG_UNBLOCK, &action.sa_mask, NULL) < 0)
		    xerror(errno, "sigprocmask");
	    }
	} else {
	    /* If trap for SIGINT is set, SIGINT is already set to be caught. */
	}
    }
}

/* Sets the action of SIGQUIT and SIGINT to ignoring the signals
 * to prevent an asynchronous job from being killed by these signals. */
void ignore_sigquit_and_sigint(void)
{
    struct sigaction action;

    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGQUIT, &action, NULL) < 0)
	xerror(errno, "sigaction(SIG%s)", "QUIT");
    if (sigaction(SIGINT, &action, NULL) < 0)
	xerror(errno, "sigaction(SIG%s)", "INT");
    sigaddset(&ignored_signals, SIGQUIT);
    sigaddset(&ignored_signals, SIGINT);
}

/* Sets the action of SIGTSTP to ignoring the signal
 * to prevent a command substitution process from being stopped by SIGTSTP. */
void ignore_sigtstp(void)
{
    struct sigaction action;

    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGTSTP, &action, NULL) < 0)
	xerror(errno, "sigaction(SIG%s)", "TSTP");
    sigaddset(&ignored_signals, SIGTSTP);
}

/* Sends SIGSTOP to the shell process.
 * Returns true iff successful. `errno' is set on failure. */
bool send_sigstop_to_myself(void)
{
    return kill(0, SIGSTOP) == 0;
}

/* general signal handler */
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

	if (signum == SIGCHLD)
	    sigchld_received = true;
	else if (signum == SIGINT)
	    sigint_received = true;
    }
}

/* Waits for SIGCHLD to be caught and call `handle_sigchld'.
 * If SIGCHLD is already caught, this function doesn't wait.
 * If `interruptible' is true, this function can be canceled by SIGINT.
 * If `return_on_trap' is true, this function returns false immediately after
 * trap actions are performed. Otherwise, traps are not handled.
 * Returns the signal number if interrupted, or zero if successful. */
int wait_for_sigchld(bool interruptible, bool return_on_trap)
{
    int result = 0;
    if (sigchld_received)
	goto finish;

    sigset_t ss, savess;
    sigfillset(&ss);
    sigemptyset(&savess);
    if (sigprocmask(SIG_BLOCK, &ss, &savess) < 0)
	xerror(errno, "sigprocmask");

    struct sigaction action, saveaction;
    if (interruptible) {
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = sig_handler;
	sigemptyset(&saveaction.sa_mask);
	sigint_received = false;
	if (sigaction(SIGINT, &action, &saveaction) < 0) {
	    xerror(errno, "sigaction(SIG%s)", "INT");
	    interruptible = false;
	}
    }

    while (!sigchld_received) {
	if (return_on_trap && have_unhandled_traps())
	    break;
	if (sigsuspend(&savess) < 0) {
	    if (errno == EINTR) {
		if (interruptible && sigint_received)
		    break;
	    } else {
		xerror(errno, "sigsuspend");
		break;
	    }
	}
    }

    if (interruptible)
	if (sigaction(SIGINT, &saveaction, NULL) < 0)
	    xerror(errno, "sigaction(SIG%s)", "INT");
    if (sigprocmask(SIG_SETMASK, &savess, NULL) < 0)
	xerror(errno, "sigprocmask");
finish:
    if (return_on_trap)
	result = handle_traps();
    if (interruptible && sigint_received)
	result = SIGINT;
    handle_sigchld();
    return result;
}

/* Waits for the specified file descriptor to be available for reading.
 * If SIGCHLD is caught while waiting, `handle_sigchld' is called.
 * If `trap' is true, traps are also handled while waiting. */
void wait_for_input(int fd, bool trap)
{
    sigset_t ss, savess;

start:
    sigfillset(&ss);
    sigemptyset(&savess);
    if (sigprocmask(SIG_BLOCK, &ss, &savess) < 0)
	xerror(errno, "sigprocmask");

    for (;;) {
	handle_sigchld();
	if (trap && have_unhandled_traps()) {
	    if (sigprocmask(SIG_SETMASK, &savess, NULL) < 0)
		xerror(errno, "sigprocmask");
	    handle_traps();
	    goto start;
	}

	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);
	if (pselect(fd + 1, &fdset, NULL, NULL, NULL, &savess) >= 0) {
	    if (FD_ISSET(fd, &fdset))
		break;
	} else {
	    if (errno != EINTR) {
		xerror(errno, "pselect");
		break;
	    }
	}
    }

    if (sigprocmask(SIG_SETMASK, &savess, NULL) < 0)
	xerror(errno, "sigprocmask");
}

/* Handles SIGCHLD if caught. */
void handle_sigchld(void)
{
    if (sigchld_received) {
        sigchld_received = false;
        do_wait();
#if YASH_ENABLE_LINEEDIT
	if (shopt_notify || (shopt_notifyle && le_state == LE_STATE_ACTIVE)) {
	    le_suspend_readline();
	    print_job_status_all(true, false, stderr);
	    le_resume_readline();
	}
#else
	if (shopt_notify) {
	    print_job_status_all(true, false, stderr);
	}
#endif
    }
}

/* Checks if there are unhandled traps (signals have been caught but the
 * associated trap commands have not been executed). */
bool have_unhandled_traps(void)
{
    if (!any_trap_set || !any_signal_received)
	return false;

    for (const signal_T *s = signals; s->no; s++) {
	size_t i = sigindex(s->no);
	wchar_t *command = trap_command[i];
	if (signal_received[i] && command && command[0])
	    return true;
    }
#if defined SIGRTMIN && defined SIGRTMAX
    int sigrtmin = SIGRTMIN, range = SIGRTMAX - sigrtmin + 1;
    if (range > RTSIZE)
	range = RTSIZE;
    for (int i = 0; i < range; i++) {
	wchar_t *command = rttrap_command[i];
	if (rtsignal_received[i] && command && command[0])
	    return true;
    }
#endif
    return false;
}

/* Executes trap commands for trapped signals if any.
 * There must not be an active job when this function is called.
 * Returns the signal number if any handler is executed, zero otherwise.
 * Note that, if more than one signal is caught, only one of their numbers is
 * returned. */
int handle_traps(void)
{
    /* Signal handler execution is not reentrant because the value of
     * `savelaststatus' would be lost. But the EXIT is the only exception:
     * The EXIT trap may be executed inside another trap. */
    if (!any_trap_set || !any_signal_received || handled_signal >= 0)
	return false;

#if YASH_ENABLE_LINEEDIT
    le_suspend_readline();
#endif

    int signum = 0;
    struct parsestate_T *state = NULL;
    savelaststatus = laststatus;

exec_handlers:
    /* we reset this before executing signal commands to avoid race */
    any_signal_received = false;

    for (const signal_T *s = signals; s->no; s++) {
	size_t i = sigindex(s->no);
	if (signal_received[i]) {
	    signal_received[i] = false;
	    wchar_t *command = trap_command[i];
	    if (command && command[0]) {
		if (!state)
		    state = save_parse_state();
		signum = handled_signal = s->no;
		exec_wcs(command, "trap", false);
		laststatus = savelaststatus;
		if (command != trap_command[i])
		    free(command);
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
	    if (command && command[0]) {
		if (!state)
		    state = save_parse_state();
		signum = handled_signal = sigrtmin + i;
		exec_wcs(command, "trap", false);
		laststatus = savelaststatus;
		if (command != rttrap_command[i])
		    free(command);
	    }
	}
    }
#endif

#if YASH_ENABLE_LINEEDIT
    if (shopt_notifyle && le_state == LE_STATE_SUSPENDED)
	print_job_status_all(true, false, stderr);
    le_resume_readline();
#endif

    if (any_signal_received)
	goto exec_handlers;
    savelaststatus = -1;
    handled_signal = -1;
    if (state)
	restore_parse_state(state);
    return signum;
}

/* Executes the EXIT trap if any. */
void execute_exit_trap(void)
{
    wchar_t *command = trap_command[sigindex(0)];
    if (command) {
	/* don't have to save state because no commands are executed any more */
	// struct parsestate_T *state = save_parse_state();
	assert(!exit_handled);
	exit_handled = true;
	savelaststatus = laststatus;
	exec_wcs(command, "EXIT trap", false);
	savelaststatus = -1;
	// restore_parse_state(state);
#ifndef NDEBUG
	if (command != trap_command[sigindex(0)])
	    free(command);
#endif
    }
}

/* Sets trap for the signal `signum' to `command'.
 * If `command' is NULL, the trap is reset to the default.
 * If `command' is an empty string, the trap is set to SIG_IGN.
 * This function may call `get_signal_name'.
 * Returns true iff successful. */
bool set_trap(int signum, const wchar_t *command)
{
    if (signum == SIGKILL || signum == SIGSTOP) {
	xerror(0, Ngt("SIG%s: cannot be trapped"),
		signum == SIGKILL ? "KILL" : "STOP");
	return false;
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
	    xerror(0, Ngt("SIG%s: unsupported real-time signal"),
		    get_signal_name(signum));
	    return false;
	}
    } else
#endif
    {
	size_t index = sigindex(signum);
	commandp = &trap_command[index];
	receivedp = &signal_received[index];
    }

    void (*oldhandler)(int);
    if (*commandp == NULL)
	oldhandler = SIG_DFL;
    else if ((*commandp)[0] == L'\0')
	oldhandler = SIG_IGN;
    else
	oldhandler = sig_handler;
    if (!is_interactive && oldhandler == SIG_DFL && is_ignored(signum)) {
	/* Signals that were ignored on entry to a non-interactive shell cannot
	 * be trapped or reset. (POSIX) */
#if FIXED_SIGNAL_AS_ERROR
	xerror(0, Ngt("SIG%s: cannot be reset"),
		get_signal_name(signum));
	return false;
#else
	return true;
#endif
    }

    /* If `*commandp' is currently executed, we must not free it. */
    if (signum != handled_signal && (signum != 0 || !exit_handled))
	free(*commandp);
    if (command) {
	if (command[0] != L'\0')
	    any_trap_set = true;
	*commandp = xwcsdup(command);
    } else {
	*commandp = NULL;
    }
    *receivedp = false;
    if (signum == 0)
	return true;

    struct sigaction action;
    if (command == NULL)
	action.sa_handler = SIG_DFL;
    else if (command[0] == L'\0')
	action.sa_handler = SIG_IGN;
    else
	action.sa_handler = sig_handler;

    if (action.sa_handler == oldhandler)
	return true;

    if (action.sa_handler == SIG_IGN)
	sigaddset(&ignored_signals, signum);
    else
	sigdelset(&ignored_signals, signum);
    switch (signum) {
	case SIGCHLD:
	    /* SIGCHLD's signal handler is always `sig_handler' */
	    return true;
	case SIGTTOU:
	    if (job_initialized)
		goto nodefault;
	    break;
	case SIGTSTP:
	    if (job_initialized)
		goto nodefault;
	    break;
	case SIGINT:
	    if (interactive_initialized)
		goto nodefault;
	    break;
	case SIGTERM:
	    if (interactive_initialized)
		goto nodefault;
	    break;
	case SIGQUIT:
	    if (interactive_initialized)
		goto nodefault;
	    break;
nodefault:
	    if (action.sa_handler == SIG_DFL)
		action.sa_handler = SIG_IGN;
	    break;
    }

    if (sigismember(&blocked_signals, signum)) {
	sigemptyset(&action.sa_mask);
	sigaddset(&action.sa_mask, signum);
	if (sigprocmask(SIG_UNBLOCK, &action.sa_mask, NULL) < 0) {
	    xerror(errno, "sigprocmask");
	} else {
	    sigdelset(&blocked_signals, signum);
	}
    }

    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(signum, &action, NULL) >= 0) {
	return true;
    } else {
	int saveerrno = errno;
	xerror(saveerrno, "sigaction(SIG%s)", get_signal_name(signum));
	return false;
    }
}

/* Clears all traps except that are set to SIG_IGN. */
void clear_traps(void)
{
    if (!any_trap_set && !any_signal_received)
	return;

    {
	size_t index = sigindex(0);
	wchar_t *command = trap_command[index];
	if (command && command[0])
	    set_trap(0, NULL);
	signal_received[index] = false;
    }
    for (const signal_T *s = signals; s->no; s++) {
	size_t index = sigindex(s->no);
	wchar_t *command = trap_command[index];
	if (command && command[0])
	    set_trap(s->no, NULL);
	signal_received[index] = false;
    }
#if defined SIGRTMIN && defined SIGRTMAX
    for (int sigrtmin = SIGRTMIN, i = 0; i < RTSIZE; i++) {
	wchar_t *command = rttrap_command[i];
	if (command && command[0])
	    set_trap(sigrtmin + i, NULL);
	rtsignal_received[i] = false;
    }
#endif
    any_trap_set = false, any_signal_received = false;
}


/********** Builtin **********/

static void print_trap(const char *signame, const wchar_t *command)
    __attribute__((nonnull(1)));
static bool print_signal(int signum, const char *name, bool verbose);
static bool signal_job(int signum, const wchar_t *jobname)
    __attribute__((nonnull));

/* The "trap" builtin */
int trap_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"print", xno_argument, L'p', },
	{ L"help",  xno_argument, L'-', },
	{ NULL, 0, 0, },
    };

    wchar_t opt;
    bool print = false;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv,
		    posixly_correct ? L"" : L"p",
		    long_options, NULL))) {
	switch (opt) {
	    case L'p':
		print = true;
		break;
	    case L'-':
		print_builtin_help(ARGV(0));
		return Exit_SUCCESS;
	    default:
		goto print_usage;
	}
    }

    if (xoptind == argc) {
	/* print all traps */
	sigset_t ss;
	sigemptyset(&ss);
	print_trap("EXIT", trap_command[sigindex(0)]);
	for (const signal_T *s = signals; s->no; s++) {
	    if (!sigismember(&ss, s->no)) {
		sigaddset(&ss, s->no);
		print_trap(s->name, trap_command[sigindex(s->no)]);
	    }
	}
#if defined SIGRTMIN && defined SIGRTMAX
	int sigrtmin = SIGRTMIN, sigrtmax = SIGRTMAX;
	for (int i = 0; i < RTSIZE; i++) {
	    if (sigrtmin + i > sigrtmax)
		break;
	    print_trap(get_signal_name(sigrtmin + i), rttrap_command[i]);
	}
#endif
	return Exit_SUCCESS;
    } else if (print) {
	/* print specified traps */
#if defined SIGRTMIN && defined SIGRTMAX
	int sigrtmin = SIGRTMIN, sigrtmax = SIGRTMAX;
#endif
	do {
	    wchar_t *wname = ARGV(xoptind);
	    for (wchar_t *w = wname; *w; w++)
		*w = towupper(*w);
	    char *name = malloc_wcstombs(wname);
	    if (!name)
		name = "";
	    int signum = get_signal_number(name);
	    if (signum < 0) {
		xerror(0, Ngt("%ls: no such signal"), wname);
	    } else {
#if defined SIGRTMIN && defined SIGRTMAX
		if (sigrtmin <= signum && signum <= sigrtmax) {
		    int index = signum - sigrtmin;
		    if (index < RTSIZE)
			print_trap(get_signal_name(signum),
				rttrap_command[index]);
		} else
#endif
		{
		    print_trap(name, trap_command[sigindex(signum)]);
		}
	    }
	    free(name);
	} while (++xoptind < argc);
	return Exit_SUCCESS;
    }

    bool err = false;
    const wchar_t *command;

    /* check if the first operand is an integer */
    wchar_t *end;
    errno = 0;
    wcstoul(ARGV(xoptind), &end, 10);
    if (ARGV(xoptind)[0] != L'\0' && *end == L'\0') {
	command = NULL;
	goto set_traps;
    } else {
	command = ARGV(xoptind++);
	if (xoptind == argc)
	    goto print_usage;
	if (wcscmp(command, L"-") == 0)
	    command = NULL;
    }

set_traps:
    do {
	wchar_t *name = ARGV(xoptind);
	int signum = get_signal_number_w(name);
	if (signum < 0) {
	    xerror(0, Ngt("%ls: no such signal"), name);
	} else {
	    err |= !set_trap(signum, command);
	}
    } while (++xoptind < argc);
    return err ? Exit_FAILURE : Exit_SUCCESS;

print_usage:
    fprintf(stderr,
	    Ngt("Usage:  trap [action signal...]\n"
		"        trap signum [signal...]\n"
		"        trap -p [signal...]\n"));
    SPECIAL_BI_ERROR;
    return Exit_ERROR;
}

/* Prints a trap command to stdout that can be used to restore the current
 * signal handler for the specified signal.
 * If the `command' is NULL, this function does nothing.
 * Otherwise, the `command' is properly single-quoted and printed. */
void print_trap(const char *signame, const wchar_t *command)
{
    if (command) {
	wchar_t *q = quote_sq(command);
	printf("trap -- %ls %s\n", q, signame);
	free(q);
    }
}

const char trap_help[] = Ngt(
"trap - set signal handler\n"
"\ttrap [action signal...]\n"
"\ttrap signum [signal...]\n"
"\ttrap -p [signal...]\n"
"Sets the signal handler of the specified <signal>s to <action>.\n"
"When the shell receives the signal, the corresponding action is executed as\n"
"if by the \"eval\" command.\n"
"If <action> is an empty string, no actions are taken and the signal is\n"
"silently ignored when the signal is received.\n"
"If <action> is \"-\", the signal handler is reset to the default.\n"
"If the first operand is a non-negative integer, the operand is considered as\n"
"a signal specification and <action> is assumed to be \"-\".\n"
"If the -p (--print) option is specified, the actions for the specified\n"
"<signal>s are printed.\n"
"Without any operands, all signal handlers currently set are printed.\n"
);

/* The "kill" builtin, which accepts the following options:
 * -s SIG: specifies the signal to send
 * -n num: specifies the signal to send by number
 * -l: prints signal info
 * -v: prints signal info verbosely */
int kill_builtin(int argc, void **argv)
{
    if (!posixly_correct && argc == 2 && wcscmp(ARGV(1), L"--help") == 0) {
	print_builtin_help(ARGV(0));
	return Exit_SUCCESS;
    }

    wchar_t opt;
    int signum = SIGTERM;
    bool list = false, verbose = false;
    bool err = false;

    xoptind = 0, xopterr = false;
    while ((opt = xgetopt_long(argv,
		    posixly_correct ? L"ls:" : L"+ln:s:v",
		    NULL, NULL))) {
	switch (opt) {
	    /* we don't make any differences between -n and -s options */
	    case L'n':  case L's':  parse_signal:
		if (list)
		    goto print_usage;
		if (posixly_correct && wcsncmp(xoptarg, L"SIG", 3) == 0) {
		    xerror(0, Ngt("%ls: signal name must be specified "
				"without `SIG'"), xoptarg);
		    return Exit_ERROR;
		}
		signum = get_signal_number_w(xoptarg);
		if (signum < 0 || (signum == 0 && !iswdigit(xoptarg[0]))) {
		    xerror(0, Ngt("%ls: no such signal"), xoptarg);
		    return Exit_FAILURE;
		}
		goto no_more_options;
	    case L'l':
		list = true;
		break;
	    case L'v':
		list = verbose = true;
		break;
	    default:
		if (ARGV(xoptind)[0] == L'-' && ARGV(xoptind)[1] == xoptopt) {
		    if (list)
			goto no_more_options;
		    xoptarg = &ARGV(xoptind++)[1];
		    goto parse_signal;
		}
		goto print_usage;
	    no_more_options:
		if (xoptind < argc && wcscmp(ARGV(xoptind), L"--") == 0)
		    xoptind++;
		goto main;
	}
    }

main:
    if (list) {
	/* print signal info */
	if (xoptind == argc) {
	    for (const signal_T *s = signals; s->no; s++)
		print_signal(s->no, s->name, verbose);
#if defined SIGRTMIN && defined SIGRTMAX
	    for (int i = SIGRTMIN, max = SIGRTMAX; i <= max; i++)
		print_signal(i, NULL, verbose);
#endif
	} else {
	    do {
		long signum;
		wchar_t *end;

		errno = 0;
		signum = wcstol(ARGV(xoptind), &end, 10);
		if (errno || *end || signum < 0 || signum > INT_MAX) {
		    xerror(0, Ngt("`%ls' is not a valid integer"),
			    ARGV(xoptind));
		    err = true;
		    continue;
		}
		if (signum >= TERMSIGOFFSET)
		    signum -= TERMSIGOFFSET;
		else if (signum >= (TERMSIGOFFSET & 0xFF))
		    signum -= (TERMSIGOFFSET & 0xFF);
		if (signum <= 0 || !print_signal((int) signum, NULL, verbose))
		    xerror(0, Ngt("%ls: no such signal"), ARGV(xoptind));
	    } while (++xoptind < argc);
	}
    } else {
	/* send signal */
	if (xoptind == argc)
	    goto print_usage;
	do {
	    wchar_t *proc = ARGV(xoptind);
	    if (proc[0] == L'%') {
		if (!signal_job(signum, proc))
		    err = true;
	    } else {
		long pid;
		wchar_t *end;

		errno = 0;
		pid = wcstol(proc, &end, 10);
		if (errno || *end) {
		    xerror(0, Ngt("`%ls' is not a valid integer"), proc);
		    err = true;
		    continue;
		}
		// XXX this cast might not be safe
		if (kill((pid_t) pid, signum) < 0) {
		    xerror(errno, "%ls", proc);
		    err = true;
		    continue;
		}
	    }
	} while (++xoptind < argc);
    }
    return err ? Exit_FAILURE : Exit_SUCCESS;

print_usage:
    if (posixly_correct)
	fprintf(stderr, Ngt(
		    "Usage:  kill [-s signal] process...\n"
		    "        kill -l [number...]\n"));
    else
	fprintf(stderr, Ngt(
		    "Usage:  kill [-s signal | -n signum] process...\n"
		    "        kill -l [-v] [number...]\n"));
    return Exit_ERROR;
}

/* Prints info about the specified signal.
 * If `name' is non-NULL, it must be a valid signal name of `signum'.
 * If `name' is NULL and `signum' is not a valid signal number, it is an error.
 * `get_signal_name' may be called in this function.
 * Returns true iff successful. */
bool print_signal(int signum, const char *name, bool verbose)
{
    if (!name) {
	name = get_signal_name(signum);
	if (strcmp(name, "?") == 0)
	    return false;
    }
    if (!verbose) {
	puts(name);
    } else {
#if HAVE_STRSIGNAL
	const char *sigdesc = strsignal(signum);
	if (sigdesc) {
	    printf("%d\t%-10s %s\n", signum, name, sigdesc);
	} else
#endif
	{
	    printf("%d\t%-10s\n", signum, name);
	}
    }
    return true;
}

/* Sends the specified signal to the specified job.
 * Returns true iff successful. */
bool signal_job(int signum, const wchar_t *jobspec)
{
    switch (send_signal_to_job(signum, jobspec + 1)) {
	case 0:  /* success */
	    return true;
	case 1:  /* no such job */
	    xerror(0, Ngt("%ls: no such job"), jobspec);
	    return false;
	case 2:  /* ambiguous job specification */
	    xerror(0, Ngt("%ls: ambiguous job specification"), jobspec);
	    return false;
	case 3:  /* not job-controlled */
	    xerror(0, Ngt("%ls: not job-controlled job"), jobspec);
	    return false;
	case 4:  /* `kill' failed */
	    xerror(errno, "%ls", jobspec);
	    return false;
	default:
	    assert(false);
    }
}

const char kill_help[] = Ngt(
"kill - send a signal to processes\n"
"\tkill [-signal|-s signal|-n number] process...\n"
"\tkill -l [-v] [number...]\n"
"The first form sends a signal to the specified processes. The signal to send\n"
"can be specified by the name or by the number, which defaults to SIGTERM if\n"
"not specified. The processes can be specified by the process ID or in the\n"
"job specification format like \"%1\".\n"
"If the process ID is negative, the signal is sent to the process group.\n"
"The second form prints info about signals. For each <number> given, the name\n"
"of the corresponding signal is printed. The <number> must be a valid signal\n"
"number or the exit status of a process kill by a signal. If no <number>s are\n"
"given, a list of available signals is printed.\n"
"With the -v option, verbose info is printed.\n"
);


/* vim: set ts=8 sts=4 sw=4 noet: */
