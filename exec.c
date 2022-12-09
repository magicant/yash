/* Yash: yet another shell */
/* exec.c: command execution */
/* (C) 2007-2022 magicant */

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
#include "exec.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <limits.h>
#include <math.h>
#if HAVE_PATHS_H
# include <paths.h>
#endif
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/times.h>
#include <unistd.h>
#include <wchar.h>
#include "alias.h"
#include "builtin.h"
#include "expand.h"
#if YASH_ENABLE_HISTORY
# include "history.h"
#endif
#include "input.h"
#include "job.h"
#include "option.h"
#include "parser.h"
#include "path.h"
#include "plist.h"
#include "redir.h"
#include "sig.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"
#include "xfnmatch.h"
#include "yash.h"
#if YASH_ENABLE_DOUBLE_BRACKET
# include "builtins/test.h"
#endif
#if YASH_ENABLE_LINEEDIT
# include "lineedit/complete.h"
# include "lineedit/lineedit.h"
#endif


/* type of command execution */
typedef enum {
    E_NORMAL,  /* normal execution */
    E_ASYNC,   /* asynchronous execution */
    E_SELF,    /* execution in the shell's own process */
} exec_T;

/* info about file descriptors of pipes */
typedef struct pipeinfo_T {
    int pi_fromprevfd;   /* reading end of the pipe from the previous process */
    int pi_tonextfds[2]; /* both ends of the pipe to the next process */
    /* -1 is assigned to unused members. */
} pipeinfo_T;
#define PIPEINFO_INIT { -1, { -1, -1 }, }

/* values used to specify the behavior of command search. */
typedef enum srchcmdtype_T {
    SCT_EXTERNAL = 1 << 0,  /* search for an external command */
    SCT_BUILTIN  = 1 << 1,  /* search for a built-in */
    SCT_FUNCTION = 1 << 2,  /* search for a function */
    SCT_ALL      = 1 << 3,  /* search all */
    SCT_STDPATH  = 1 << 4,  /* search the standard PATH */
    SCT_CHECK    = 1 << 5,  /* check command existence */
} srchcmdtype_T;

typedef enum cmdtype_T {
    CT_NONE,
    CT_EXTERNALPROGRAM,
    CT_SPECIALBUILTIN,
    CT_MANDATORYBUILTIN,
    CT_ELECTIVEBUILTIN,
    CT_EXTENSIONBUILTIN,
    CT_SUBSTITUTIVEBUILTIN,
    CT_FUNCTION,
} cmdtype_T;

/* info about a simple command to execute */
typedef struct commandinfo_T {
    cmdtype_T type;           /* type of command */
    union {
	const char *path;     /* command path (for external program) */
	main_T *builtin;      /* body of built-in */
	command_T *function;  /* body of function */
    } value;
} commandinfo_T;
#define ci_path     value.path
#define ci_builtin  value.builtin
#define ci_function value.function

/* result of `fork_and_wait' */
typedef struct fork_and_wait_T {
    pid_t cpid;       /* child process ID */
    wchar_t **namep;  /* where to place the job name */
} fork_and_wait_T;

typedef enum exception_T {
    E_NONE,
    E_CONTINUE,
    E_RETURN,
    E_BREAK_ITERATION,
    E_CONTINUE_ITERATION,
} exception_T;

/* state of currently executed loop */
typedef struct execstate_T {
    unsigned loopnest;      /* level of nested loops */
    unsigned breakloopnest; /* target level of "break" */
    bool noreturn;          /* true when the "return" built-in is not allowed */
    bool iterating;         /* true when iterative execution is ongoing */
} execstate_T;

static void exec_pipelines(const pipeline_T *p, bool finally_exit);
static void exec_pipelines_async(const pipeline_T *p)
    __attribute__((nonnull));

static void exec_commands(command_T *cs, exec_T type)
    __attribute__((nonnull));
static inline size_t number_of_commands_in_pipeline(const command_T *c)
    __attribute__((nonnull,pure,warn_unused_result));
static void apply_errexit_errreturn(const command_T *c);
static bool is_errexit_condition(void)
    __attribute__((pure));
static bool is_errreturn_condition(void)
    __attribute__((pure));
static bool is_err_condition_for(const command_T *c)
    __attribute__((pure));
static inline void next_pipe(pipeinfo_T *pi, bool next)
    __attribute__((nonnull));
static inline void connect_pipes(pipeinfo_T *pi)
    __attribute__((nonnull));

static void exec_one_command(command_T *c, bool finally_exit)
    __attribute__((nonnull));
static void exec_simple_command(const command_T *c, bool finally_exit)
    __attribute__((nonnull));
static bool exec_simple_command_without_words(const command_T *c)
    __attribute__((nonnull,warn_unused_result));
static bool exec_simple_command_with_words(
	const command_T *c, int argc, void **argv, bool finally_exit)
    __attribute__((nonnull,warn_unused_result));
static void print_xtrace(void *const *argv);
static void search_command(
	const char *restrict name, const wchar_t *restrict wname,
	commandinfo_T *restrict ci, enum srchcmdtype_T type)
    __attribute__((nonnull));
static inline bool is_special_builtin(const char *cmdname)
    __attribute__((nonnull,pure));
static bool command_not_found_handler(void *const *argv)
    __attribute__((nonnull));
static wchar_t **invoke_simple_command(const commandinfo_T *ci,
	int argc, char *argv0, void **argv, bool finally_exit)
    __attribute__((nonnull,warn_unused_result));
static void exec_external_program(
	const char *path, int argc, char *argv0, void **argv, char **envs)
    __attribute__((nonnull));
static inline int xexecve(
	const char *path, char *const *argv, char *const *envp)
    __attribute__((nonnull(1)));
static void exec_fall_back_on_sh(
	int argc, char *const *argv, char *const *env, const char *path)
    __attribute__((nonnull(2,3,4)));
static void exec_function_body(
	command_T *body, void *const *args, bool finally_exit, bool complete)
    __attribute__((nonnull));

static void exec_nonsimple_command(command_T *c, bool finally_exit)
    __attribute__((nonnull));
static void exec_if(const command_T *c, bool finally_exit)
    __attribute__((nonnull));
static inline bool exec_condition(const and_or_T *c);
static void exec_for(const command_T *c, bool finally_exit)
    __attribute__((nonnull));
static void exec_while(const command_T *c, bool finally_exit)
    __attribute__((nonnull));
static void exec_case(const command_T *c, bool finally_exit)
    __attribute__((nonnull));
static void exec_funcdef(const command_T *c, bool finally_exit)
    __attribute__((nonnull));

static fork_and_wait_T fork_and_wait(sigtype_T sigtype)
    __attribute__((warn_unused_result));
static void become_child(sigtype_T sigtype);

static int exec_iteration(void *const *commands, const char *codename)
    __attribute__((nonnull));


/* exit status of the last command */
int laststatus = Exit_SUCCESS;
/* exit status of the command preceding the currently executed trap action */
int savelaststatus = -1;  // -1 if not in a trap handler
/* exit status of the last command substitution */
static int lastcmdsubstatus;
/* exit status of the command that immediately preceded the EXIT trap. */
int exitstatus = -1;  // -1 if not executing the EXIT trap
/* the process ID of the last asynchronous list */
pid_t lastasyncpid;

/* This flag is set to true while the shell is executing the condition of an if-
 * statement, an and-or list, etc. to suppress the effect of the "errexit" and
 * "errreturn" options. */
static bool suppresserrexit = false, suppresserrreturn = false;

/* state of currently executed loop */
static execstate_T execstate;
/* exceptional jump to be done (other than "break") */
static exception_T exception;

/* This flag is set when a special built-in is executed as such. */
bool special_builtin_executed;

/* This flag is set while the "exec" built-in is executed. */
static bool exec_builtin_executed = false;

/* True while executing auxiliary commands such as $PROMPT_COMMAND and
 * $COMMAND_NOT_FOUND_HANDLER. */
bool is_executing_auxiliary = false;

/* the last assignment. */
static const assign_T *last_assign;

/* a buffer for xtrace.
 * When assignments are performed while executing a simple command, the trace
 * is appended to this buffer. Each trace of an assignment must be prefixed
 * with a space to separate it with the previous one. The first space will be
 * trimmed when the buffer is flushed to the standard error. */
static xwcsbuf_T xtrace_buffer = { .contents = NULL };


/* Resets `execstate' to the initial state. */
void reset_execstate(bool reset_iteration)
{
    execstate.loopnest = 0;
    execstate.breakloopnest = 0;
    execstate.noreturn = false;
    if (reset_iteration)
	execstate.iterating = false;
}

/* Saves the current `execstate' and returns it.
 * You typically call `reset_execstate' after calling this function. */
execstate_T *save_execstate(void)
{
    execstate_T *save = xmalloc(sizeof execstate);
    *save = execstate;
    return save;
}

/* Restores `execstate' to `save' and frees `save'. */
void restore_execstate(execstate_T *save)
{
    execstate = *save;
    free(save);
}

/* Disables the "return" built-in in the current `execstate'. */
void disable_return(void)
{
    execstate.noreturn = true;
}

/* If we're returning, clear the flag. */
void cancel_return(void)
{
    if (exception == E_RETURN)
	exception = E_NONE;
}

/* Returns true iff we're breaking/continuing/returning now. */
bool need_break(void)
{
    return execstate.breakloopnest < execstate.loopnest
	|| exception != E_NONE
	|| is_interrupted();
}


/* Executes the and-or lists.
 * If `finally_exit' is true, the shell exits after execution. */
void exec_and_or_lists(const and_or_T *a, bool finally_exit)
{
    while (a != NULL && !need_break()) {
	if (!a->ao_async)
	    exec_pipelines(a->ao_pipelines, finally_exit && !a->next);
	else
	    exec_pipelines_async(a->ao_pipelines);

	a = a->next;
    }
    if (finally_exit)
	exit_shell();
}

/* Executes the pipelines. */
void exec_pipelines(const pipeline_T *p, bool finally_exit)
{
    for (bool first = true; p != NULL; p = p->next, first = false) {
	if (!first && p->pl_cond == (laststatus != Exit_SUCCESS))
	    continue;

	bool savesee = suppresserrexit, saveser = suppresserrreturn;
	bool suppress = p->pl_neg || p->next != NULL;
	suppresserrexit |= suppress;
	suppresserrreturn |= suppress;

	bool self = finally_exit && !p->next && !p->pl_neg;
	exec_commands(p->pl_commands, self ? E_SELF : E_NORMAL);

	suppresserrexit = savesee, suppresserrreturn = saveser;

	if (need_break())
	    break;

	if (p->pl_neg) {
	    if (laststatus == Exit_SUCCESS)
		laststatus = Exit_FAILURE;
	    else
		laststatus = Exit_SUCCESS;
	}
    }
    if (finally_exit)
	exit_shell();
}

/* Executes the pipelines asynchronously. */
void exec_pipelines_async(const pipeline_T *p)
{
    if (p->next == NULL && !p->pl_neg) {
	exec_commands(p->pl_commands, E_ASYNC);
	return;
    }

    pid_t cpid = fork_and_reset(0, false, t_quitint);
    
    if (cpid > 0) {
	/* parent process: add a new job */
	job_T *job = xmalloc(add(sizeof *job, sizeof *job->j_procs));
	process_T *ps = job->j_procs;

	ps->pr_pid = cpid;
	ps->pr_status = JS_RUNNING;
	ps->pr_statuscode = 0;
	ps->pr_name = pipelines_to_wcs(p);

	job->j_pgid = doing_job_control_now ? cpid : 0;
	job->j_status = JS_RUNNING;
	job->j_statuschanged = true;
	job->j_legacy = false;
	job->j_nonotify = false;
	job->j_pcount = 1;

	set_active_job(job);
	add_job(shopt_curasync);
	laststatus = Exit_SUCCESS;
	lastasyncpid = cpid;
    } else if (cpid == 0) {
	/* child process: execute the commands and then exit */
	maybe_redirect_stdin_to_devnull();
	exec_pipelines(p, true);
	assert(false);
    } else {
	/* fork failure */
	laststatus = Exit_NOEXEC;
    }
}

/* Executes the commands in a pipeline. */
void exec_commands(command_T *const cs, exec_T type)
{
    size_t count = number_of_commands_in_pipeline(cs);
    assert(count > 0);

    bool short_circuit =
	type == E_SELF && !doing_job_control_now && !any_trap_set &&
	(count == 1 || !shopt_pipefail);

    if (count == 1 && type != E_ASYNC) {
	exec_one_command(cs, /* finally_exit = */ short_circuit);
	goto done;
    }

    /* fork a child process for each command in the pipeline */
    pid_t pgid = 0;
    pipeinfo_T pipe = PIPEINFO_INIT;
    job_T *job = xmallocs(sizeof *job, count, sizeof *job->j_procs);
    command_T *c;
    process_T *p;
    int forkstatus = Exit_SUCCESS;
    for (c = cs, p = job->j_procs; c != NULL; c = c->next, p++) {
	bool is_last = c->next == NULL;
	next_pipe(&pipe, !is_last);

	if (is_last && short_circuit)
	    goto exec_one_command; /* skip forking */

	sigtype_T sigtype = (type == E_ASYNC) ? t_quitint : 0;
	pid_t pid = fork_and_reset(pgid, type == E_NORMAL, sigtype);
	if (pid == 0) {
exec_one_command: /* child process */
	    free(job);
	    connect_pipes(&pipe);
	    if (type == E_ASYNC && pipe.pi_fromprevfd < 0)
		maybe_redirect_stdin_to_devnull();
	    exec_one_command(c, true);
	    assert(false);
	} else if (pid >= 0) {
	    /* parent process: fork succeeded */
	    if (pgid == 0)
		pgid = pid;
	    p->pr_pid = pid;
	    p->pr_status = JS_RUNNING;
	    // p->pr_statuscode = ?; // The process is still running.
	    p->pr_name = NULL; // The actual name is given later.
	} else {
	    /* parent process: fork failed */
	    p->pr_pid = 0;
	    p->pr_status = JS_DONE;
	    p->pr_statuscode = forkstatus = Exit_NOEXEC;
	    p->pr_name = NULL;
	}
    }

    assert(pipe.pi_tonextfds[PIPE_IN] < 0);
    assert(pipe.pi_tonextfds[PIPE_OUT] < 0);
    if (pipe.pi_fromprevfd >= 0)
	xclose(pipe.pi_fromprevfd); /* close the leftover pipe */

    /* establish the job and wait for it */
    job->j_pgid = doing_job_control_now ? pgid : 0;
    job->j_status = JS_RUNNING;
    job->j_statuschanged = true;
    job->j_legacy = false;
    job->j_nonotify = false;
    job->j_pcount = count;
    set_active_job(job);
    if (type != E_ASYNC) {
	wait_for_job(ACTIVE_JOBNO, doing_job_control_now, false, false);
	if (doing_job_control_now)
	    put_foreground(shell_pgid);
	laststatus = calc_status_of_job(job);
    } else {
	laststatus = forkstatus;
	lastasyncpid = job->j_procs[count - 1].pr_pid;
    }

    if (job->j_status == JS_DONE) {
	notify_signaled_job(ACTIVE_JOBNO);
	remove_job(ACTIVE_JOBNO);
    } else {
	/* name the job processes */
	for (c = cs, p = job->j_procs; c != NULL; c = c->next, p++)
	    p->pr_name = command_to_wcs(c, false);

	/* remember the suspended job */
	add_job(type == E_NORMAL || shopt_curasync);
    }

done:
    handle_signals();

    apply_errexit_errreturn(cs);

    if (type == E_SELF)
	exit_shell();
}

size_t number_of_commands_in_pipeline(const command_T *c)
{
    size_t count = 1;
    while ((c = c->next) != NULL)
	count++;
    return count;
}

/* Tests the current condition for "errexit" and "errreturn" and then performs
 * exit or return if applicable. */
void apply_errexit_errreturn(const command_T *c)
{
    if (is_errexit_condition() && is_err_condition_for(c))
	exit_shell_with_status(laststatus);
    if (is_errreturn_condition() && is_err_condition_for(c))
	exception = E_RETURN;
}

/* Returns true if the shell should exit because of the `errexit' option. */
bool is_errexit_condition(void)
{
    if (!shopt_errexit || suppresserrexit)
	return false;
    if (laststatus == Exit_SUCCESS)
	return false;

#if YASH_ENABLE_LINEEDIT
    if (le_state & LE_STATE_COMPLETING)
	return false;
#endif

    return true;
}

/* Returns true if the shell should return because of the `errreturn' option. */
bool is_errreturn_condition(void)
{
    if (!shopt_errreturn || suppresserrreturn || execstate.noreturn)
	return false;
    return laststatus != Exit_SUCCESS;
}

/* Returns true if "errexit" and "errreturn" should be applied to the given
 * command. */
bool is_err_condition_for(const command_T *c)
{
    if (c == NULL)
	return true;

    /* If this is a multi-command pipeline, the commands are executed in
     * subshells. Otherwise, we need to check the type of the command. */
    if (c->next != NULL)
	return true;

    switch (c->c_type) {
	case CT_SIMPLE:
	case CT_SUBSHELL:
#if YASH_ENABLE_DOUBLE_BRACKET
	case CT_BRACKET:
#endif
	case CT_FUNCDEF:
	    return true;
	case CT_GROUP:
	case CT_IF:
	case CT_FOR:
	case CT_WHILE:
	case CT_CASE:
	    return false;
    }

    assert(false);
}

/* Updates the contents of the `pipeinfo_T' to proceed to the next process
 * execution. `pi->pi_fromprevfd' and `pi->pi_tonextfds[PIPE_OUT]' are closed,
 * `pi->pi_tonextfds[PIPE_IN]' is moved to `pi->pi_fromprevfd', and,
 * if `next' is true, a new pipe is opened in `pi->pi_tonextfds'; otherwise,
 * `pi->pi_tonextfds' is assigned -1.
 * Returns true iff successful. */
void next_pipe(pipeinfo_T *pi, bool next)
{
    if (pi->pi_fromprevfd >= 0)
	xclose(pi->pi_fromprevfd);
    if (pi->pi_tonextfds[PIPE_OUT] >= 0)
	xclose(pi->pi_tonextfds[PIPE_OUT]);
    pi->pi_fromprevfd = pi->pi_tonextfds[PIPE_IN];
    if (next) {
	if (pipe(pi->pi_tonextfds) < 0)
	    goto fail;

	/* The pipe's FDs must not be 0 or 1, or they may be overridden by each
	 * other when we move the pipe to the standard input/output later. So,
	 * if they are 0 or 1, we move them to bigger numbers. */
	int origin  = pi->pi_tonextfds[PIPE_IN];
	int origout = pi->pi_tonextfds[PIPE_OUT];
	if (origin < 2 || origout < 2) {
	    if (origin < 2)
		pi->pi_tonextfds[PIPE_IN] = dup(origin);
	    if (origout < 2)
		pi->pi_tonextfds[PIPE_OUT] = dup(origout);
	    if (origin < 2)
		xclose(origin);
	    if (origout < 2)
		xclose(origout);
	    if (pi->pi_tonextfds[PIPE_IN] < 0) {
		xclose(pi->pi_tonextfds[PIPE_OUT]);
		goto fail;
	    }
	    if (pi->pi_tonextfds[PIPE_OUT] < 0) {
		xclose(pi->pi_tonextfds[PIPE_IN]);
		goto fail;
	    }
	}
    } else {
	pi->pi_tonextfds[PIPE_IN] = pi->pi_tonextfds[PIPE_OUT] = -1;
    }
    return;

fail:
    pi->pi_tonextfds[PIPE_IN] = pi->pi_tonextfds[PIPE_OUT] = -1;
    xerror(errno, Ngt("cannot open a pipe"));
}

/* Connects the pipe(s) and closes the pipes left. */
void connect_pipes(pipeinfo_T *pi)
{
    if (pi->pi_fromprevfd >= 0) {
	xdup2(pi->pi_fromprevfd, STDIN_FILENO);
	xclose(pi->pi_fromprevfd);
    }
    if (pi->pi_tonextfds[PIPE_OUT] >= 0) {
	xdup2(pi->pi_tonextfds[PIPE_OUT], STDOUT_FILENO);
	xclose(pi->pi_tonextfds[PIPE_OUT]);
    }
    if (pi->pi_tonextfds[PIPE_IN] >= 0)
	xclose(pi->pi_tonextfds[PIPE_IN]);
}

/* Executes the command. */
void exec_one_command(command_T *c, bool finally_exit)
{
    /* prevent the command data from being freed in case the command is part of
     * a function that is unset during execution. */
    c = comsdup(c);

    update_lineno(c->c_lineno);

    if (c->c_type == CT_SIMPLE) {
	exec_simple_command(c, finally_exit);
    } else {
	savefd_T *savefd;
	if (open_redirections(c->c_redirs, &savefd)) {
	    exec_nonsimple_command(c, finally_exit && savefd == NULL);
	    undo_redirections(savefd);
	} else {
	    undo_redirections(savefd);
	    laststatus = Exit_REDIRERR;
	    apply_errexit_errreturn(NULL);
	}
    }

    comsfree(c);

    if (finally_exit)
	exit_shell();
}

/* Executes the simple command. */
void exec_simple_command(const command_T *c, bool finally_exit)
{
    lastcmdsubstatus = Exit_SUCCESS;

    /* expand the command words */
    int argc;
    void **argv;
    if (!expand_line(c->c_words, &argc, &argv)) {
	laststatus = Exit_EXPERROR;
	goto done;
    }
    if (is_interrupted())
	goto done1;

    /* execute the remaining part */
    if (argc == 0)
	finally_exit |= exec_simple_command_without_words(c);
    else
	finally_exit |=
	    exec_simple_command_with_words(c, argc, argv, finally_exit);

    /* cleanup */
done1:
    plfree(argv, free);
done:
    if (finally_exit)
	exit_shell();
}

/* Executes the simple command that has no expanded words.
 * Returns true if the shell should exit. */
bool exec_simple_command_without_words(const command_T *c)
{
    /* perform assignments */
    bool ok = do_assignments(c->c_assigns, false, false);
    print_xtrace(NULL);
    last_assign = c->c_assigns;
    if (!ok) {
	laststatus = Exit_ASSGNERR;
	return !is_interactive_now;
    }

    /* done? */
    if (c->c_redirs == NULL) {
	laststatus = lastcmdsubstatus;
	return false;
    }

    /* create a subshell to perform redirections in */
    fork_and_wait_T faw = fork_and_wait(0);
    if (faw.cpid != 0) {
	/* parent process */
	if (faw.namep != NULL)
	    *faw.namep = command_to_wcs(c, false);
	return false;
    }

    /* open redirections in subshell */
    savefd_T *savefd;
    ok = open_redirections(c->c_redirs, &savefd);
    undo_redirections(savefd);
    exit_shell_with_status(ok ? lastcmdsubstatus : Exit_REDIRERR);
}

/* Executes the simple command that has one or more expanded words.
 * `argv' must be a NULL-terminated array of pointers to wide strings that are
 * the results of the word expansion on the simple command being executed.
 * `argc' must be the number of words in `argv', which must be at least 1.
 * If `finally_exit' is true, the shell process may be replaced by the command
 * process. However, this function still may return in some cases.
 * Returns true if the shell should exit. */
bool exec_simple_command_with_words(
	const command_T *c, int argc, void **argv, bool finally_exit)
{
    assert(argc > 0);

    char *argv0 = malloc_wcstombs(argv[0]);
    if (argv0 == NULL)
	argv0 = xstrdup("");

    /* open redirections */
    savefd_T *savefd;
    if (!open_redirections(c->c_redirs, &savefd)) {
	/* On redirection error, the command is not executed. */
	laststatus = Exit_REDIRERR;
	if (posixly_correct && !is_interactive_now && is_special_builtin(argv0))
	    finally_exit = true;
	goto done;
    }

    last_assign = c->c_assigns;

    /* check if the command is a special built-in or function */
    commandinfo_T cmdinfo;
    search_command(argv0, argv[0], &cmdinfo, SCT_BUILTIN | SCT_FUNCTION);
    special_builtin_executed = (cmdinfo.type == CT_SPECIALBUILTIN);

    /* open a temporary variable environment */
    bool temp = c->c_assigns != NULL && !special_builtin_executed;
    if (temp)
	open_new_environment(true);

    /* perform the assignments */
    if (!do_assignments(c->c_assigns, temp, true)) {
	/* On assignment error, the command is not executed. */
	print_xtrace(NULL);
	laststatus = Exit_ASSGNERR;
	if (!is_interactive_now)
	    finally_exit = true;
	goto done1;
    }
    print_xtrace(argv);

    /* find command path */
    if (cmdinfo.type == CT_NONE) {
	search_command(argv0, argv[0], &cmdinfo,
		SCT_EXTERNAL | SCT_BUILTIN | SCT_CHECK);
	if (cmdinfo.type == CT_NONE) {
	    if (!posixly_correct && command_not_found_handler(argv))
		goto done1;
	    if (wcschr(argv[0], L'/') != NULL) {
		cmdinfo.type = CT_EXTERNALPROGRAM;
		cmdinfo.ci_path = argv0;
	    }
	}
    }

    /* execute! */
    wchar_t **namep = invoke_simple_command(&cmdinfo, argc, argv0, argv,
	    finally_exit && /* !temp && */ savefd == NULL);
    if (namep != NULL)
	*namep = command_to_wcs(c, false);

    /* Redirections are not undone after a successful "exec" command:
     * remove the saved data of file descriptors. */
    if (exec_builtin_executed && laststatus == Exit_SUCCESS) {
	clear_savefd(savefd);
	savefd = NULL;
    }
    exec_builtin_executed = false;

    /* cleanup */
done1:
    if (temp)
	close_current_environment();
done:
    undo_redirections(savefd);
    free(argv0);

    return finally_exit;
}

/* Returns a pointer to the xtrace buffer.
 * The buffer is initialized if not. */
xwcsbuf_T *get_xtrace_buffer(void)
{
    if (xtrace_buffer.contents == NULL)
	wb_init(&xtrace_buffer);
    return &xtrace_buffer;
}

/* Prints a trace if the "xtrace" option is on. */
void print_xtrace(void *const *argv)
{
    static bool expanding_ps4 = false;

    bool tracevars = xtrace_buffer.contents != NULL
		  && xtrace_buffer.length > 0;

    if (shopt_xtrace && !expanding_ps4
	    && (!is_executing_auxiliary || shopt_traceall)
	    && (tracevars || argv != NULL)
#if YASH_ENABLE_LINEEDIT
	    && !(le_state & LE_STATE_ACTIVE)
#endif
	    ) {
	bool first = true;

	// Disallow recursion in case $PS4 contains a command substitution
	// that may trigger another xtrace, which would be an infinite loop
	expanding_ps4 = true;
	struct promptset_T prompt = get_prompt(4);
	expanding_ps4 = false;

	print_prompt(prompt.main);
	print_prompt(prompt.styler);

	if (tracevars) {
	    fprintf(stderr, "%ls", xtrace_buffer.contents + 1);
	    first = false;
	}
	if (argv != NULL) {
	    for (void *const *a = argv; *a != NULL; a++) {
		if (!first)
		    fputc(' ', stderr);
		first = false;

		wchar_t *quoted = quote_as_word(*a);
		fprintf(stderr, "%ls", quoted);
		free(quoted);
	    }
	}
	fputc('\n', stderr);

	print_prompt(PROMPT_RESET);
	free_prompt(prompt);
    }
    if (xtrace_buffer.contents != NULL) {
	wb_destroy(&xtrace_buffer);
	xtrace_buffer.contents = NULL;
    }
}

/* Searches for a command.
 * The result is assigned to `*ci'.
 * `name' and `wname' must contain the same string value.
 * If the SCT_ALL flag is not set:
 *   *  a function whose name contains a slash cannot be found
 *   *  a substitutive built-in cannot be found if the SCT_EXTERNAL flag is not
 *      set either.
 * If the SCT_EXTERNAL flag is set, the SCT_CHECK flag is not set, and `name'
 * contains a slash, the external command of the given `name' is always found.
 */
void search_command(
	const char *restrict name, const wchar_t *restrict wname,
	commandinfo_T *restrict ci, enum srchcmdtype_T type)
{
    bool slash = wcschr(wname, L'/') != NULL;

    const builtin_T *bi;
    if (!slash && (type & SCT_BUILTIN))
	bi = get_builtin(name);
    else
	bi = NULL;
    if (bi != NULL && bi->type == BI_SPECIAL) {
	ci->type = CT_SPECIALBUILTIN;
	ci->ci_builtin = bi->body;
	return;
    }

    if ((type & SCT_FUNCTION) && (!slash || (type & SCT_ALL))) {
	command_T *funcbody = get_function(wname);
	if (funcbody != NULL) {
	    ci->type = CT_FUNCTION;
	    ci->ci_function = funcbody;
	    return;
	}
    }

    if (bi != NULL) {
	switch (bi->type) {
	    case BI_MANDATORY:
		ci->type = CT_MANDATORYBUILTIN;
		ci->ci_builtin = bi->body;
		return;
	    case BI_ELECTIVE:
		ci->type = CT_ELECTIVEBUILTIN;
		ci->ci_builtin = bi->body;
		return;
	    case BI_EXTENSION:
		if (posixly_correct) {
		    bi = NULL;
		    break;
		} else {
		    ci->type = CT_EXTENSIONBUILTIN;
		    ci->ci_builtin = bi->body;
		    return;
		}
	    case BI_SPECIAL:
		assert(false);
	    case BI_SUBSTITUTIVE:
		break;
	}
    }

    if (slash) {
	if (type & SCT_EXTERNAL) {
	    if (!(type & SCT_CHECK) || is_executable_regular(name)) {
		ci->type = CT_EXTERNALPROGRAM;
		ci->ci_path = name;
		return;
	    }
	}
    } else {
	if ((type & SCT_EXTERNAL) || (bi != NULL && (type & SCT_ALL))) {
	    const char *cmdpath;
	    if (type & SCT_STDPATH)
		cmdpath = get_command_path_default(name);
	    else
		cmdpath = get_command_path(name, false);
	    if (cmdpath != NULL) {
		if (bi != NULL) {
		    assert(bi->type == BI_SUBSTITUTIVE);
		    ci->type = CT_SUBSTITUTIVEBUILTIN;
		    ci->ci_builtin = bi->body;
		} else {
		    ci->type = CT_EXTERNALPROGRAM;
		    ci->ci_path = cmdpath;
		}
		return;
	    }
	}
    }

    /* command not found... */
    ci->type = CT_NONE;
    ci->ci_path = NULL;
    return;
}

/* Returns true iff the specified command is a special built-in. */
bool is_special_builtin(const char *cmdname)
{
    const builtin_T *bi = get_builtin(cmdname);
    return bi != NULL && bi->type == BI_SPECIAL;
}

/* Executes $COMMAND_NOT_FOUND_HANDLER if any.
 * `argv' is set to the positional parameters of the environment in which the
 * handler is executed.
 * Returns true iff the hander was executed and $HANDLED was set non-empty. */
bool command_not_found_handler(void *const *argv)
{
    static bool handling = false;
    if (handling)
	return false;  /* don't allow reentrance */
    handling = true;

    bool handled;
    int result;

    open_new_environment(false);
    set_positional_parameters(argv);
    set_variable(L VAR_HANDLED, xwcsdup(L""), SCOPE_LOCAL, false);

    result = exec_variable_as_auxiliary_(VAR_COMMAND_NOT_FOUND_HANDLER);
    if (result >= 0) {
	const wchar_t *handledv = getvar(L VAR_HANDLED);
	handled = (handledv != NULL && handledv[0] != L'\0');
    } else {
	handled = false;
    }

    close_current_environment();

    handling = false;
    if (handled) {
	laststatus = result;
	return true;
    } else {
	return false;
    }
}

/* Invokes the simple command. */
/* `argv0' is the multibyte version of `argv[0]' */
wchar_t **invoke_simple_command(
	const commandinfo_T *ci, int argc, char *argv0, void **argv,
	bool finally_exit)
{
    assert(plcount(argv) == (size_t) argc);

    fork_and_wait_T faw = { 0, NULL };

    switch (ci->type) {
    case CT_NONE:
	xerror(0, Ngt("no such command `%s'"), argv0);
	laststatus = Exit_NOTFOUND;
	break;
    case CT_EXTERNALPROGRAM:
	if (!finally_exit) {
	    faw = fork_and_wait(t_leave);
	    if (faw.cpid != 0)
		break;
	    finally_exit = true;
	}
	exec_external_program(ci->ci_path, argc, argv0, argv, environ);
	break;
    case CT_ELECTIVEBUILTIN:
	if (posixly_correct) {
	    xerror(0, Ngt("%ls: non-portable built-in is not supported "
			"in the POSIXly-correct mode"),
		    (const wchar_t *) argv[0]);
	    laststatus = Exit_NOTFOUND;
	    break;
	}
	/* falls thru! */
    case CT_SPECIALBUILTIN:
    case CT_MANDATORYBUILTIN:
    case CT_EXTENSIONBUILTIN:
    case CT_SUBSTITUTIVEBUILTIN:
	yash_error_message_count = 0;

	const wchar_t *savecbn = current_builtin_name;
	current_builtin_name = argv[0];

	laststatus = ci->ci_builtin(argc, argv);

	current_builtin_name = savecbn;
	break;
    case CT_FUNCTION:
	exec_function_body(ci->ci_function, &argv[1], finally_exit, false);
	break;
    }
    if (finally_exit)
	exit_shell();
    return faw.namep;
}

/* Executes the external program.
 *  path:  path to the program to be executed
 *  argc:  number of strings in `argv'
 *  argv0: multibyte version of `argv[0]'
 *  argv:  pointer to an array of pointers to wide strings that are passed to
 *         the program */
void exec_external_program(
	const char *path, int argc, char *argv0, void **argv, char **envs)
{
    char *mbsargv[argc + 1];
    mbsargv[0] = argv0;
    for (int i = 1; i < argc; i++) {
	mbsargv[i] = malloc_wcstombs(argv[i]);
	if (mbsargv[i] == NULL)
	    mbsargv[i] = xstrdup("");
    }
    mbsargv[argc] = NULL;

    restore_signals(true);

    xexecve(path, mbsargv, envs);
    int saveerrno = errno;
    if (saveerrno != ENOEXEC) {
	if (saveerrno == EACCES && is_directory(path))
	    saveerrno = EISDIR;
	xerror(saveerrno,
		strcmp(mbsargv[0], path) == 0
		    ? Ngt("cannot execute command `%s'")
		    : Ngt("cannot execute command `%s' (%s)"),
		argv0, path);
    } else if (saveerrno != ENOENT) {
	exec_fall_back_on_sh(argc, mbsargv, envs, path);
    }
    laststatus = (saveerrno == ENOENT) ? Exit_NOTFOUND : Exit_NOEXEC;

    set_signals();

    for (int i = 1; i < argc; i++)
	free(mbsargv[i]);
}

/* Calls `execve' until it doesn't return EINTR. */
int xexecve(const char *path, char *const *argv, char *const *envp)
{
    do
	execve(path, argv, envp);
    while (errno == EINTR);
    return -1;
}

/* Executes the specified command as a shell script by `exec'ing a shell.
 * `path' is the full path to the script file.
 * Returns iff failed to `exec'. */
void exec_fall_back_on_sh(
	int argc, char *const *argv, char *const *envp, const char *path)
{
    assert(argv[argc] == NULL);

    char *args[argc + 3];
    size_t index = 0;
    args[index++] = "sh";
    args[index++] = (char *) "-";
    if (strcmp(path, "--") == 0)
	args[index++] = "./--";
    else
	args[index++] = (char *) path;
    for (int i = 1; i < argc; i++)
	args[index++] = argv[i];
    args[index] = NULL;
#if HAVE_PROC_SELF_EXE
    xexecve("/proc/self/exe", args, envp);
#elif HAVE_PROC_CURPROC_FILE
    xexecve("/proc/curproc/file", args, envp);
#elif HAVE_PROC_OBJECT_AOUT
    char *objpath = malloc_printf(
	    "/proc/%jd/object/a.out", (intmax_t) getpid());
    xexecve(objpath, args, envp);
    free(objpath);
#elif HAVE_PATHS_H && defined _PATH_BSHELL
    xexecve(_PATH_BSHELL, args, envp);
#endif
    const char *shpath = get_command_path("sh", false);
    if (shpath != NULL)
	xexecve(shpath, args, envp);
    else
	errno = ENOENT;
    xerror(errno, Ngt("cannot invoke a new shell to execute script `%s'"),
	    argv[0]);
}

/* Executes the specified command as a function.
 * `args' are the arguments to the function, which are wide strings cast to
 * (void *).
 * If `complete' is true, `set_completion_variables' will be called after a new
 * variable environment was opened before the function body is executed. */
void exec_function_body(
	command_T *body, void *const *args, bool finally_exit, bool complete)
{
    execstate_T *saveexecstate = save_execstate();
    reset_execstate(false);

    bool saveser = suppresserrreturn;
    suppresserrreturn = false;

    open_new_environment(false);
    set_positional_parameters(args);
#if YASH_ENABLE_LINEEDIT
    if (complete)
	set_completion_variables();
#else
    (void) complete;
#endif
    exec_commands(body, finally_exit ? E_SELF : E_NORMAL);
    close_current_environment();

    cancel_return();
    suppresserrreturn = saveser;
    restore_execstate(saveexecstate);
}

/* Executes the specified command whose type is not `CT_SIMPLE'.
 * The redirections for the command is not performed in this function. */
void exec_nonsimple_command(command_T *c, bool finally_exit)
{
    switch (c->c_type) {
    case CT_SIMPLE:
	assert(false);
    case CT_SUBSHELL:
	if (finally_exit) {
	    /* This is the last command to execute in the current shell, hence
	     * no need to make a new child. */
	    become_child(0);
	} else {
	    /* make a child process to execute the command */
	    fork_and_wait_T faw = fork_and_wait(0);
	    if (faw.cpid != 0) {
		if (faw.namep != NULL)
		    *faw.namep = command_to_wcs(c, false);
		break;
	    }
	    finally_exit = true;
	}
	// falls thru!
    case CT_GROUP:
	exec_and_or_lists(c->c_subcmds, finally_exit);
	break;
    case CT_IF:
	exec_if(c, finally_exit);
	break;
    case CT_FOR:
	exec_for(c, finally_exit);
	break;
    case CT_WHILE:
	exec_while(c, finally_exit);
	break;
    case CT_CASE:
	exec_case(c, finally_exit);
	break;
#if YASH_ENABLE_DOUBLE_BRACKET
    case CT_BRACKET:
	laststatus = exec_double_bracket(c);
	if (finally_exit)
	    exit_shell();
	break;
#endif /* YASH_ENABLE_DOUBLE_BRACKET */
    case CT_FUNCDEF:
	exec_funcdef(c, finally_exit);
	break;
    }
}

/* Executes the if command */
void exec_if(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_IF);

    for (const ifcommand_T *cmds = c->c_ifcmds;
	    cmds != NULL;
	    cmds = cmds->next) {
	if (need_break())
	    goto done;
	if (exec_condition(cmds->ic_condition)) {
	    exec_and_or_lists(cmds->ic_commands, finally_exit);
	    assert(!finally_exit);
	    return;
	}
    }
    laststatus = Exit_SUCCESS;
done:
    if (finally_exit)
	exit_shell();
}

/* Executes the condition of an if/while/until command. */
bool exec_condition(const and_or_T *c)
{
    if (c == NULL)
	return true;

    bool savesee = suppresserrexit, saveser = suppresserrreturn;
    suppresserrexit = suppresserrreturn = true;
    exec_and_or_lists(c, false);
    suppresserrexit = savesee, suppresserrreturn = saveser;
    return laststatus == Exit_SUCCESS;
}

/* Executes the for command. */
void exec_for(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_FOR);
    execstate.loopnest++;
    execstate.breakloopnest = execstate.loopnest;

    int count;
    void **words;

    if (c->c_forwords != NULL) {
	/* expand the words between "in" and "do" of the for command. */
	if (!expand_line(c->c_forwords, &count, &words)) {
	    laststatus = Exit_EXPERROR;
	    apply_errexit_errreturn(NULL);
	    goto finish;
	}
    } else {
	/* no "in" keyword in the for command: use the positional parameters */
	struct get_variable_T v = get_variable(L"@");
	assert(v.type == GV_ARRAY && v.values != NULL);
	save_get_variable_values(&v);
	count = (int) v.count;
	words = v.values;
    }

#define CHECK_LOOP                                      \
    if (execstate.breakloopnest < execstate.loopnest) { \
	goto done;                                      \
    } else if (exception == E_CONTINUE) {               \
	exception = E_NONE;                             \
	continue;                                       \
    } else if (exception != E_NONE) {                   \
	goto done;                                      \
    } else if (is_interrupted()) {                      \
	goto done;                                      \
    } else (void) 0

    int i;
    for (i = 0; i < count; i++) {
	if (!set_variable(c->c_forname, words[i],
		    shopt_forlocal && !posixly_correct ?
			SCOPE_LOCAL : SCOPE_GLOBAL,
		    false)) {
	    laststatus = Exit_ASSGNERR;
	    apply_errexit_errreturn(NULL);
	    if (!is_interactive_now)
		finally_exit = true;
	    goto done;
	}
	exec_and_or_lists(c->c_forcmds, finally_exit && i + 1 == count);

	if (c->c_forcmds == NULL)
	    handle_signals();
	CHECK_LOOP;
    }

done:
    while (++i < count)  /* free unused words */
	free(words[i]);
    free(words);
    if (count == 0 && c->c_forcmds != NULL)
	laststatus = Exit_SUCCESS;
finish:
    execstate.loopnest--;
    if (finally_exit)
	exit_shell();
}

/* Executes the while/until command. */
/* The exit status of a while/until command is that of `c_whlcmds' executed
 * last.  If `c_whlcmds' is not executed at all, the status is 0 regardless of
 * `c_whlcond'. */
void exec_while(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_WHILE);
    execstate.loopnest++;
    execstate.breakloopnest = execstate.loopnest;

    int status = Exit_SUCCESS;
    for (;;) {
	bool cond = exec_condition(c->c_whlcond);

	if (c->c_whlcond == NULL)
	    handle_signals();
	CHECK_LOOP;

	if (cond != c->c_whltype)
	    break;
	if (c->c_whlcmds != NULL) {
	    exec_and_or_lists(c->c_whlcmds, false);
	    status = laststatus;
	}

	if (c->c_whlcmds == NULL)
	    handle_signals();
	CHECK_LOOP;
    }

    laststatus = status;
done:
    execstate.loopnest--;
    if (finally_exit)
	exit_shell();
}
#undef CHECK_LOOP

/* Executes the case command. */
void exec_case(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_CASE);

    wchar_t *word = expand_single(c->c_casword, TT_SINGLE, Q_WORD, ES_NONE);
    if (word == NULL)
	goto fail;

    for (const caseitem_T *ci = c->c_casitems; ci != NULL; ci = ci->next) {
	for (void **pats = ci->ci_patterns; *pats != NULL; pats++) {
	    wchar_t *pattern =
		expand_single(*pats, TT_SINGLE, Q_WORD, ES_QUOTED);
	    if (pattern == NULL)
		goto fail;

	    bool match = match_pattern(word, pattern);
	    free(pattern);
	    if (match) {
		if (ci->ci_commands != NULL) {
		    exec_and_or_lists(ci->ci_commands, finally_exit);
		    goto done;
		} else {
		    goto success;
		}
	    }
	}
    }
success:
    laststatus = Exit_SUCCESS;
done:
    free(word);
    if (finally_exit)
	exit_shell();
    return;

fail:
    laststatus = Exit_EXPERROR;
    apply_errexit_errreturn(NULL);
    goto done;
}

/* Executes the function definition. */
void exec_funcdef(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_FUNCDEF);

    wchar_t *funcname =
	expand_single(c->c_funcname, TT_SINGLE, Q_WORD, ES_NONE);
    if (funcname != NULL) {
	if (define_function(funcname, c->c_funcbody))
	    laststatus = Exit_SUCCESS;
	else
	    laststatus = Exit_ASSGNERR;
	free(funcname);
    } else {
	laststatus = Exit_EXPERROR;
    }

    if (finally_exit)
	exit_shell();
}

/* Forks a new child process and wait for it to finish.
 * `sigtype' is passed to `fork_and_reset'.
 * In the parent process, this function updates `laststatus' to the exit status
 * of the child.
 * In the child process, this function does not wait for anything.
 * Returns a pair of the return value of `fork' and a pointer to a pointer to
 * a wide string. If the pointer is non-null, the caller must assign it a
 * pointer to a newly malloced wide string that is the job name of the child. */
fork_and_wait_T fork_and_wait(sigtype_T sigtype)
{
    fork_and_wait_T result;
    result.cpid = fork_and_reset(0, true, sigtype);
    if (result.cpid < 0) {
	/* Fork failed. */
	laststatus = Exit_NOEXEC;
	result.namep = NULL;
    } if (result.cpid > 0) {
	/* parent process */
	result.namep = wait_for_child(
		result.cpid,
		doing_job_control_now ? result.cpid : 0,
		doing_job_control_now);
    } else {
	/* child process */
	result.namep = NULL;
    }
    return result;
}

/* Forks a subshell and does some settings.
 * If job control is active, the child process's process group ID is set to
 * `pgid'. If `pgid' is 0, the child process's process ID is used as the process
 * group ID..
 * If `pgid' is negative or job control is inactive, the process group ID is not
 * changed.
 * If job control is active and `fg' is true, the child process becomes a
 * foreground process.
 * `sigtype' specifies the settings of signals in the child.
 * `sigtype' is a bitwise OR of the followings:
 *   t_quitint: SIGQUIT & SIGINT are ignored if the parent's job control is off
 *   t_tstp: SIGTSTP is ignored if the parent is job-controlling
 *   t_leave: Don't clear traps and shell FDs. Restore the signal mask for
 *          SIGCHLD. Don't reset `execstate'. This option must be used iff the
 *          shell is going to `exec' to an external program.
 * Returns the return value of `fork'. */
pid_t fork_and_reset(pid_t pgid, bool fg, sigtype_T sigtype)
{
    sigset_t savemask;
    if (sigtype & (t_quitint | t_tstp)) {
	/* block all signals to prevent the race condition */
	sigset_t all;
	sigfillset(&all);
	sigemptyset(&savemask);
	sigprocmask(SIG_BLOCK, &all, &savemask);
    }

    pid_t cpid = fork();

    if (cpid != 0) {
	if (cpid < 0) {
	    /* fork failure */
	    xerror(errno, Ngt("cannot make a child process"));
	} else {
	    /* parent process */
	    if (doing_job_control_now && pgid >= 0)
		setpgid(cpid, pgid);
	}
	if (sigtype & (t_quitint | t_tstp))
	    sigprocmask(SIG_SETMASK, &savemask, NULL);
    } else {
	/* child process */
	if (doing_job_control_now && pgid >= 0) {
	    setpgid(0, pgid);
	    if (pgid == 0)
		pgid = getpgrp();
	    shell_pgid = pgid;
	    if (fg)
		put_foreground(pgid);
	}
	become_child(sigtype);  /* signal mask is restored here */
    }
    return cpid;
}

/* Resets traps, signal handlers, etc. for the current process to become a
 * subshell. See `fork_and_reset' for the meaning of the `sigtype' parameter. */
void become_child(sigtype_T sigtype)
{
    if (sigtype & t_leave) {
	clear_exit_trap();
    } else {
	phantomize_traps();
	neglect_all_jobs();
#if YASH_ENABLE_HISTORY
	close_history_file();
#endif
	reset_execstate(true);
    }

    if (sigtype & t_quitint)
	if (!doing_job_control_now)
	    ignore_sigquit_and_sigint();
    if (sigtype & t_tstp)
	if (doing_job_control_now)
	    ignore_sigtstp();

    restore_signals(sigtype & t_leave);  /* signal mask is restored here */
    clear_shellfds(sigtype & t_leave);
    is_interactive_now = false;
    suppresserrreturn = false;
    exitstatus = -1;
}

/* Executes the command substitution and returns the string to substitute with.
 * This function blocks until the command finishes.
 * The return value is a newly-malloced string without a trailing newline.
 * NULL is returned on error. */
wchar_t *exec_command_substitution(const embedcmd_T *cmdsub)
{
    int pipefd[2];
    pid_t cpid;

    if (cmdsub->is_preparsed
	    ? cmdsub->value.preparsed == NULL
	    : cmdsub->value.unparsed[0] == L'\0')  /* empty command */
	return xwcsdup(L"");

    /* open a pipe to receive output from the command */
    if (pipe(pipefd) < 0) {
	xerror(errno, Ngt("cannot open a pipe for the command substitution"));
	return NULL;
    }

    /* If the child is stopped by SIGTSTP, it can never be resumed and
     * the shell will be stuck. So we specify the `t_tstp' flag to prevent the
     * child from being stopped by SIGTSTP. */
    cpid = fork_and_reset(-1, false, t_tstp);
    if (cpid < 0) {
	/* fork failure */
	xclose(pipefd[PIPE_IN]);
	xclose(pipefd[PIPE_OUT]);
	lastcmdsubstatus = Exit_NOEXEC;
	return NULL;
    } else if (cpid > 0) {
	/* parent process */
	FILE *f;

	xclose(pipefd[PIPE_OUT]);
	f = fdopen(pipefd[PIPE_IN], "r");
	if (f == NULL) {
	    xerror(errno,
		    Ngt("cannot open a pipe for the command substitution"));
	    xclose(pipefd[PIPE_IN]);
	    lastcmdsubstatus = Exit_NOEXEC;
	    return NULL;
	}

	/* read output from the command */
	xwcsbuf_T buf;
	wint_t c;
	wb_init(&buf);
	while ((c = fgetwc(f)) != WEOF)
	    wb_wccat(&buf, c);
	fclose(f);

	/* wait for the child to finish */
	int savelaststatus = laststatus;
	wait_for_child(cpid, 0, false);
	lastcmdsubstatus = laststatus;
	laststatus = savelaststatus;

	/* trim trailing newlines and return */
	size_t len = buf.length;
	while (len > 0 && buf.contents[len - 1] == L'\n')
	    len--;
	return wb_towcs(wb_truncate(&buf, len));
    } else {
	/* child process */
	xclose(pipefd[PIPE_IN]);
	if (pipefd[PIPE_OUT] != STDOUT_FILENO) {  /* connect the pipe */
	    if (xdup2(pipefd[PIPE_OUT], STDOUT_FILENO) < 0)
		exit(Exit_NOEXEC);
	    xclose(pipefd[PIPE_OUT]);
	}

	if (cmdsub->is_preparsed)
	    exec_and_or_lists(cmdsub->value.preparsed, true);
	else
	    exec_wcs(cmdsub->value.unparsed, gt("command substitution"), true);
	assert(false);
    }
}

/* Executes the value of the specified variable.
 * The variable value is parsed as commands.
 * If the `varname' names an array, every element of the array is executed (but
 * if the iteration is interrupted by the "break -i" command, the remaining
 * elements are not executed).
 * `codename' is passed to `exec_wcs' as the command name.
 * Returns the exit status of the executed command (or zero if none executed, or
 * -1 if the variable is unset).
 * When this function returns, `laststatus' is restored to the original value.*/
int exec_variable_as_commands(const wchar_t *varname, const char *codename)
{
    struct get_variable_T gv = get_variable(varname);

    switch (gv.type) {
	case GV_NOTFOUND:
	    return -1;
	case GV_SCALAR:
	case GV_ARRAY:
	    break;
	case GV_ARRAY_CONCAT:
	    /* should execute the concatenated value, but is not supported now*/
	    return -1;
    }

    /* copy the array values in case they are unset during execution */
    save_get_variable_values(&gv);

    /* prevent "break" and "continue" */
    execstate_T *saveexecstate = save_execstate();
    // reset_execstate(false); /* don't reset `execstate.noreturn' */
    execstate.loopnest = 0;

    int result = exec_iteration(gv.values, codename);

    restore_execstate(saveexecstate);

    plfree(gv.values, free);
    return result;
}

/* Executes commands in the given array of wide strings (iterative execution).
 * The strings are parsed and executed one by one.
 * If the iteration is interrupted by the "break -i" command, the remaining
 * elements are not executed.
 * `codename' is passed to `exec_wcs' as the command name.
 * Returns the exit status of the executed command (or zero if none executed).
 * When this function returns, `laststatus' is restored to the original value.*/
int exec_iteration(void *const *commands, const char *codename)
{
    int savelaststatus = laststatus, commandstatus = Exit_SUCCESS;
    bool saveiterating = execstate.iterating;
    execstate.iterating = true;

    for (void *const *command = commands; *command != NULL; command++) {
	exec_wcs(*command, codename, false);
	commandstatus = laststatus;
	laststatus = savelaststatus;
	switch (exception) {
	    case E_BREAK_ITERATION:
		exception = E_NONE;
		/* falls thru! */
	    default:
		goto done;
	    case E_CONTINUE_ITERATION:
		exception = E_NONE;
		/* falls thru! */
	    case E_NONE:
		break;
	}
	if (execstate.breakloopnest < execstate.loopnest || is_interrupted())
	    goto done;
    }
done:
    execstate.iterating = saveiterating;
    return commandstatus;
}

/* Calls `exec_variable_as_commands' with `is_executing_auxiliary' set to true.
 */
int exec_variable_as_auxiliary(const wchar_t *varname, const char *codename)
{
    bool save_executing_auxiliary = is_executing_auxiliary;
    is_executing_auxiliary = true;
    int result = exec_variable_as_commands(varname, codename);
    is_executing_auxiliary = save_executing_auxiliary;
    return result;
}

#if YASH_ENABLE_LINEEDIT

/* Autoloads the specified file to load a completion function definition.
 * String `cmdname', which may be NULL, is used as the only positional parameter
 * during script execution.
 * Returns true iff a file was autoloaded. */
bool autoload_completion_function_file(
	const wchar_t *filename, const wchar_t *cmdname)
{
    char *mbsfilename = malloc_wcstombs(filename);
    if (mbsfilename == NULL)
	return false;

    char *path = which(mbsfilename, get_path_array(PA_LOADPATH),
	    is_readable_regular);
    if (path == NULL) {
	le_compdebug("file \"%s\" was not found in $YASH_LOADPATH",
		mbsfilename);
	free(mbsfilename);
	return false;
    }

    int fd = move_to_shellfd(open(path, O_RDONLY));
    if (fd < 0) {
	le_compdebug("cannot open file \"%s\"", path);
	free(path);
	return false;
    }

    execstate_T *saveexecstate = save_execstate();
    int savelaststatus = laststatus;
    bool saveposix = posixly_correct;
    reset_execstate(true);
    posixly_correct = false;
    open_new_environment(false);
    set_positional_parameters((void *[]) { (void *) cmdname, NULL });

    le_compdebug("executing file \"%s\" (autoload)", path);
    exec_input(fd, mbsfilename, 0);
    le_compdebug("finished executing file \"%s\"", path);

    close_current_environment();
    posixly_correct = saveposix;
    laststatus = savelaststatus;
    cancel_return();
    restore_execstate(saveexecstate);
    remove_shellfd(fd);
    xclose(fd);
    free(path);
    free(mbsfilename);
    return true;
}

/* Calls the function whose name is `funcname' as a completion function.
 * Returns false if no such function has been defined. */
bool call_completion_function(const wchar_t *funcname)
{
    command_T *func = get_function(funcname);
    if (func == NULL) {
	le_compdebug("completion function \"%ls\" is not defined", funcname);
	return false;
    }

    execstate_T *saveexecstate = save_execstate();
    int savelaststatus = laststatus;
    bool saveposix = posixly_correct, saveerrreturn = shopt_errreturn;
    reset_execstate(true);
    posixly_correct = false, shopt_errreturn = false;

    le_compdebug("executing completion function \"%ls\"", funcname);

    exec_function_body(func, (void *[]) { NULL }, false, true);

    le_compdebug("finished executing completion function \"%ls\"", funcname);
    le_compdebug("  with the exit status of %d", laststatus);

    posixly_correct = saveposix, shopt_errreturn = saveerrreturn;
    laststatus = savelaststatus;
    cancel_return();
    restore_execstate(saveexecstate);

    return true;
}

#endif /* YASH_ENABLE_LINEEDIT */


/********** Built-ins **********/

static int exec_builtin_2(int argc, void **argv, const wchar_t *as, bool clear)
    __attribute__((nonnull(2)));
static int command_builtin_execute(
	int argc, void **argv, enum srchcmdtype_T type)
    __attribute__((nonnull));
static bool print_command_info(const wchar_t *commandname,
	enum srchcmdtype_T type, bool alias, bool keyword, bool humanfriendly)
    __attribute__((nonnull));
static void print_command_absolute_path(
	const char *name, const char *path, bool humanfriendly)
    __attribute__((nonnull));
static void print_command_path(
	const char *name, const char *path, bool humanfriendly)
    __attribute__((nonnull));

/* Options for the "break", "continue" and "eval" built-ins. */
const struct xgetopt_T iter_options[] = {
    { L'i', L"iteration", OPTARG_NONE, false, NULL, },
#if YASH_ENABLE_HELP
    { L'-', L"help",      OPTARG_NONE, false, NULL, },
#endif
    { L'\0', NULL, 0, false, NULL, },
};

/* Options for the "return" built-in. */
const struct xgetopt_T return_options[] = {
    { L'n', L"no-return", OPTARG_NONE, false, NULL, },
#if YASH_ENABLE_HELP
    { L'-', L"help",      OPTARG_NONE, false, NULL, },
#endif
    { L'\0', NULL, 0, false, NULL, },
};

/* The "return" built-in, which accepts the following option:
 *  -n: don't return from a function. */
int return_builtin(int argc, void **argv)
{
    bool noreturn = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, return_options, 0)) != NULL) {
	switch (opt->shortopt) {
	    case L'n':
		noreturn = true;
		break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return special_builtin_error(Exit_ERROR);
	}
    }

    if (!validate_operand_count(argc - xoptind, 0, 1))
	return special_builtin_error(Exit_ERROR);

    int status;
    const wchar_t *statusstr = ARGV(xoptind);
    if (statusstr != NULL) {
	if (!xwcstoi(statusstr, 10, &status) || status < 0) {
	    xerror(0, Ngt("`%ls' is not a valid integer"), statusstr);
	    status = Exit_ERROR;
	    special_builtin_error(status);
	    /* return anyway */
	}
    } else {
	status = (savelaststatus >= 0) ? savelaststatus : laststatus;
    }
    if (!noreturn) {
	if (execstate.noreturn && is_interactive_now) {
	    xerror(0, Ngt("cannot be used in the interactive mode"));
	    return Exit_FAILURE;
	}
	exception = E_RETURN;
    }
    return status;
}

#if YASH_ENABLE_HELP
const char return_help[] = Ngt(
"return from a function or script"
);
const char return_syntax[] = Ngt(
"\treturn [-n] [exit_status]\n"
);
#endif

/* The "break"/"continue" built-in, which accepts the following option:
 *  -i: iterative execution */
int break_builtin(int argc, void **argv)
{
    bool iter = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, iter_options, 0)) != NULL) {
	switch (opt->shortopt) {
	    case L'i':
		iter = true;
		break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return special_builtin_error(Exit_ERROR);
	}
    }

    if (!validate_operand_count(argc - xoptind, 0, iter ? 0 : 1))
	return special_builtin_error(Exit_ERROR);

    if (iter) {
	/* break/continue iteration */
	if (!execstate.iterating) {
	    xerror(0, Ngt("not in an iteration"));
	    return Exit_ERROR;
	}
	if (wcscmp(ARGV(0), L"break") == 0) {
	    exception = E_BREAK_ITERATION;
	} else {
	    assert(wcscmp(ARGV(0), L"continue") == 0);
	    exception = E_CONTINUE_ITERATION;
	}
	return laststatus;
    } else {
	unsigned count;
	const wchar_t *countstr = ARGV(xoptind);
	if (countstr == NULL) {
	    count = 1;
	} else {
	    unsigned long countl;
	    if (!xwcstoul(countstr, 0, &countl)) {
		xerror(0, Ngt("`%ls' is not a valid integer"), countstr);
		return special_builtin_error(Exit_ERROR);
	    } else if (countl == 0) {
		xerror(0, Ngt("%u is not a positive integer"), 0u);
		return special_builtin_error(Exit_ERROR);
	    } else if (countl > UINT_MAX) {
		count = UINT_MAX;
	    } else {
		count = (unsigned) countl;
	    }
	}
	assert(count > 0);
	if (execstate.loopnest == 0) {
	    xerror(0, Ngt("not in a loop"));
	    return special_builtin_error(Exit_ERROR);
	}
	if (count > execstate.loopnest)
	    count = execstate.loopnest;
	if (wcscmp(ARGV(0), L"break") == 0) {
	    execstate.breakloopnest = execstate.loopnest - count;
	} else {
	    assert(wcscmp(ARGV(0), L"continue") == 0);
	    execstate.breakloopnest = execstate.loopnest - count + 1;
	    exception = E_CONTINUE;
	}
	return Exit_SUCCESS;
    }
}

#if YASH_ENABLE_HELP

const char break_help[] = Ngt(
"exit a loop"
);
const char break_syntax[] = Ngt(
"\tbreak [count]\n"
"\tbreak -i\n"
);

const char continue_help[] = Ngt(
"continue a loop"
);
const char continue_syntax[] = Ngt(
"\tcontinue [count]\n"
"\tcontinue -i\n"
);

#endif

/* The "eval" built-in, which accepts the following option:
 *  -i: iterative execution */
int eval_builtin(int argc __attribute__((unused)), void **argv)
{
    bool iter = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, iter_options, XGETOPT_POSIX)) != NULL) {
	switch (opt->shortopt) {
	    case L'i':
		iter = true;
		break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return special_builtin_error(Exit_ERROR);
	}
    }

    if (iter) {
	return exec_iteration(&argv[xoptind], "eval");
    } else {
	wchar_t *args = joinwcsarray(&argv[xoptind], L" ");
	exec_wcs(args, "eval", false);
	free(args);
	return laststatus;
    }
}

#if YASH_ENABLE_HELP
const char eval_help[] = Ngt(
"evaluate arguments as a command"
);
const char eval_syntax[] = Ngt(
"\teval [-i] [argument...]\n"
);
#endif

/* Options for the "." built-in. */
const struct xgetopt_T dot_options[] = {
    { L'A', L"no-alias", OPTARG_NONE, false, NULL, },
    { L'L', L"autoload", OPTARG_NONE, false, NULL, },
#if YASH_ENABLE_HELP
    { L'-', L"help",     OPTARG_NONE, false, NULL, },
#endif
    { L'\0', NULL, 0, false, NULL, },
};

/* The "." built-in, which accepts the following option:
 *  -A: disable aliases
 *  -L: autoload */
int dot_builtin(int argc, void **argv)
{
    bool enable_alias = true, autoload = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, dot_options, XGETOPT_POSIX)) != NULL) {
	switch (opt->shortopt) {
	    case L'A':
		enable_alias = false;
		break;
	    case L'L':
		autoload = true;
		break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return special_builtin_error(Exit_ERROR);
	}
    }

    const wchar_t *filename = ARGV(xoptind++);
    if (filename == NULL)
	return special_builtin_error(insufficient_operands_error(1));

    bool has_args = xoptind < argc;
    if (has_args && posixly_correct)
	return special_builtin_error(too_many_operands_error(1));

    char *mbsfilename = malloc_wcstombs(filename);
    if (mbsfilename == NULL) {
	xerror(EILSEQ, Ngt("unexpected error"));
	return Exit_ERROR;
    }

    char *path;
    if (autoload) {
	path = which(mbsfilename, get_path_array(PA_LOADPATH),
		is_readable_regular);
	if (path == NULL) {
	    xerror(0, Ngt("file `%s' was not found in $YASH_LOADPATH"),
		    mbsfilename);
	    goto error;
	}
    } else if (wcschr(filename, L'/') == NULL) {
	path = which(mbsfilename, get_path_array(PA_PATH), is_readable_regular);
	if (path == NULL) {
	    if (!posixly_correct) {
		path = mbsfilename;
	    } else {
		xerror(0, Ngt("file `%s' was not found in $PATH"), mbsfilename);
		goto error;
	    }
	}
    } else {
	path = mbsfilename;
    }

    int fd = move_to_shellfd(open(path, O_RDONLY));
    if (path != mbsfilename)
	free(path);
    if (fd < 0) {
	xerror(errno, Ngt("cannot open file `%s'"), mbsfilename);
	goto error;
    }

    if (has_args) {
	open_new_environment(false);
	set_positional_parameters(&argv[xoptind]);
    }

    execstate_T *saveexecstate = save_execstate();
    reset_execstate(false);

    bool saveser = suppresserrreturn;
    suppresserrreturn = false;

    exec_input(fd, mbsfilename, enable_alias ? XIO_SUBST_ALIAS : 0);

    cancel_return();
    suppresserrreturn = saveser;
    restore_execstate(saveexecstate);
    remove_shellfd(fd);
    xclose(fd);
    free(mbsfilename);

    if (has_args) {
	close_current_environment();
    }

    return laststatus;

error:
    free(mbsfilename);
    if (special_builtin_executed && !is_interactive_now)
	exit_shell_with_status(Exit_FAILURE);
    return Exit_FAILURE;
}

#if YASH_ENABLE_HELP
const char dot_help[] = Ngt(
"read a file and execute commands"
);
const char dot_syntax[] = Ngt(
"\t. [-AL] file [argument...]\n"
);
#endif

/* Options for the "exec" built-in. */
const struct xgetopt_T exec_options[] = {
    { L'a', L"as",    OPTARG_REQUIRED, false, NULL, },
    { L'c', L"clear", OPTARG_NONE,     false, NULL, },
    { L'f', L"force", OPTARG_NONE,     false, NULL, },
#if YASH_ENABLE_HELP
    { L'-', L"help",  OPTARG_NONE,     false, NULL, },
#endif
    { L'\0', NULL, 0, false, NULL, },
};

/* The "exec" built-in, which accepts the following options:
 *  -a name: give <name> as argv[0] to the command
 *  -c: don't pass environment variables to the command
 *  -f: suppress error when we have stopped jobs */
int exec_builtin(int argc, void **argv)
{
    const wchar_t *as = NULL;
    bool clear = false, force = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, exec_options, XGETOPT_POSIX)) != NULL) {
	switch (opt->shortopt) {
	    case L'a':  as = xoptarg;  break;
	    case L'c':  clear = true;  break;
	    case L'f':  force = true;  break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return special_builtin_error(Exit_ERROR);
	}
    }

    exec_builtin_executed = true;

    if (xoptind == argc)
	return Exit_SUCCESS;
    if (!posixly_correct && is_interactive_now && !force) {
	size_t sjc = stopped_job_count();
	if (sjc > 0) {
	    fprintf(stderr,
		    ngt("You have a stopped job!",
			"You have %zu stopped jobs!",
			sjc),
		    sjc);
	    fprintf(stderr, gt("  Use the -f option to exec anyway.\n"));
	    return Exit_FAILURE;
	}
    }

    return exec_builtin_2(argc - xoptind, &argv[xoptind], as, clear);
}

/* The main part of the "exec" built-in.
 * argc, argv: the operands (not arguments) of the "exec" built-in.
 * as: value of the -a option or NULL
 * clear: true iff the -c option is specified */
int exec_builtin_2(int argc, void **argv, const wchar_t *as, bool clear)
{
    int err;

    const wchar_t *saveargv0 = ARGV(0);
    char *mbssaveargv0 = malloc_wcstombs(saveargv0);
    if (mbssaveargv0 == NULL) {
	xerror(EILSEQ, Ngt("unexpected error"));
	err = Exit_NOEXEC;
	goto error1;
    }

    char *mbsargv0;
    if (as != NULL) {
	mbsargv0 = malloc_wcstombs(as);
	if (mbsargv0 == NULL) {
	    xerror(EILSEQ, Ngt("unexpected error"));
	    err = Exit_NOEXEC;
	    goto error2;
	}
	argv[0] = (void *) as;
    } else {
	mbsargv0 = mbssaveargv0;
    }

    const char *commandpath;
    if (wcschr(saveargv0, L'/') != NULL) {
	commandpath = mbssaveargv0;
    } else {
	commandpath = get_command_path(mbssaveargv0, false);
	if (commandpath == NULL) {
	    xerror(0, Ngt("no such command `%s'"), mbssaveargv0);
	    err = Exit_NOTFOUND;
	    goto error3;
	}
    }

    char **envs;
    if (clear) {
	/* use the environment that contains only the variables assigned by the
	 * assignment for this `exec' built-in. */
	plist_T list;
	
	pl_init(&list);
	for (const assign_T *assign = last_assign;
		assign != NULL;
		assign = assign->next) {
	    char *value = get_exported_value(assign->a_name);
	    if (value != NULL) {
		pl_add(&list, malloc_printf("%ls=%s", assign->a_name, value));
		free(value);
	    }
	}
	envs = (char **) pl_toary(&list);
    } else {
	envs = environ;
    }

    exec_external_program(commandpath, argc, mbsargv0, argv, envs);
    err = laststatus;

    if (clear)
	plfree((void **) envs, free);
error3:
    if (as != NULL) {
	free(mbsargv0);
	argv[0] = (void *) saveargv0;
    }
error2:
    free(mbssaveargv0);
error1:
    if (posixly_correct || !is_interactive_now)
	exit(err);
    return err;
}

#if YASH_ENABLE_HELP
const char exec_help[] = Ngt(
"replace the shell process with an external command"
);
const char exec_syntax[] = Ngt(
"\texec [-cf] [-a name] [command [argument...]]\n"
);
#endif

/* Options for the "command" built-in. */
const struct xgetopt_T command_options[] = {
    { L'a', L"alias",            OPTARG_NONE, false, NULL, },
    { L'b', L"builtin-command",  OPTARG_NONE, false, NULL, },
    { L'e', L"external-command", OPTARG_NONE, false, NULL, },
    { L'f', L"function",         OPTARG_NONE, false, NULL, },
    { L'k', L"keyword",          OPTARG_NONE, false, NULL, },
    { L'p', L"standard-path",    OPTARG_NONE, true,  NULL, },
    { L'v', L"identify",         OPTARG_NONE, true,  NULL, },
    { L'V', L"verbose-identify", OPTARG_NONE, true,  NULL, },
#if YASH_ENABLE_HELP
    { L'-', L"help",             OPTARG_NONE, false, NULL, },
#endif
    { L'\0', NULL, 0, false, NULL, },
};

/* The "command"/"type" built-in, which accepts the following options:
 *  -a: search aliases
 *  -b: search built-ins
 *  -e: search external commands
 *  -f: search functions
 *  -k: search keywords
 *  -p: use the default path to find the command
 *  -v: print info about the command
 *  -V: print info about the command in a human-friendly format */
int command_builtin(int argc, void **argv)
{
    bool argv0istype = wcscmp(ARGV(0), L"type") == 0;
    bool printinfo = argv0istype, humanfriendly = argv0istype;
    enum srchcmdtype_T type = 0;
    bool aliases = false, keywords = false, defpath = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, command_options, XGETOPT_POSIX)) != NULL) {
	switch (opt->shortopt) {
	    case L'a':  aliases = true;        break;
	    case L'b':  type |= SCT_BUILTIN;   break;
	    case L'e':  type |= SCT_EXTERNAL;  break;
	    case L'f':  type |= SCT_FUNCTION;  break;
	    case L'k':  keywords = true;       break;
	    case L'p':  defpath = true;        break;
	    case L'v':  printinfo = true;  humanfriendly = false;  break;
	    case L'V':  printinfo = true;  humanfriendly = true;   break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return Exit_ERROR;
	}
    }

    if (!printinfo) {
	if (aliases || keywords) {
	    xerror(0,
		Ngt("the -a or -k option must be used with the -v option"));
	    return Exit_ERROR;
	}

	if (xoptind == argc) {
	    if (posixly_correct)
		return insufficient_operands_error(1);
	    else
		return Exit_SUCCESS;
	}

	if (type == 0)
	    type = SCT_EXTERNAL | SCT_BUILTIN;
	else
	    type |= SCT_ALL;
	if (defpath)
	    type |= SCT_STDPATH;
	return command_builtin_execute(
		argc - xoptind, &argv[xoptind], type);
    } else {
	if (posixly_correct && !validate_operand_count(argc - xoptind, 1,
		    argv0istype ? SIZE_MAX : 1))
	    return Exit_ERROR;

	if (type == 0 && !aliases && !keywords) {
	    type = SCT_EXTERNAL | SCT_BUILTIN | SCT_FUNCTION;
	    aliases = keywords = true;
	} else {
	    type |= SCT_ALL;
	}
	if (defpath)
	    type |= SCT_STDPATH;
	type |= SCT_CHECK;

	bool ok = true;
	clearerr(stdout);
	for (int i = xoptind; i < argc; i++) {
	    ok &= print_command_info(
		    ARGV(i), type, aliases, keywords, humanfriendly);
	    if (ferror(stdout))
		break;
	}
	return ok && yash_error_message_count == 0
	    ? Exit_SUCCESS : Exit_FAILURE;
    }
}

/* Executes the specified simple command.
 * `argc' must be positive.
 * Returns the exit status of the command. */
int command_builtin_execute(int argc, void **argv, enum srchcmdtype_T type)
{
    char *argv0 = malloc_wcstombs(argv[0]);
    commandinfo_T ci;

    if (argv0 == NULL) {
	xerror(EILSEQ, NULL);
	return Exit_NOTFOUND;
    }

    search_command(argv0, argv[0], &ci, type);

    wchar_t **namep = invoke_simple_command(&ci, argc, argv0, argv, false);
    if (namep != NULL)
	*namep = joinwcsarray(argv, L" ");

    free(argv0);
    return laststatus;
}

/* Prints info about the specified command.
 * If the command is not found, returns false. */
bool print_command_info(
	const wchar_t *commandname, enum srchcmdtype_T type,
	bool aliases, bool keywords, bool humanfriendly)
{
    const char *msgfmt;
    char *name = NULL;

    if (keywords && is_keyword(commandname)) {
	msgfmt = humanfriendly ? gt("%ls: a shell keyword\n") : "%ls\n";
	xprintf(msgfmt, commandname);
	return true;
    }

    if (aliases) {
	if (print_alias_if_defined(commandname, humanfriendly)) {
	    return true;
	} else {
	    if (ferror(stdout))
		return false;
	}
    }

    name = malloc_wcstombs(commandname);
    if (name == NULL)
	return false;

    commandinfo_T ci;
    search_command(name, commandname, &ci, type);
    switch (ci.type) {
	case CT_NONE:
	    if (humanfriendly)
		xerror(0, Ngt("no such command `%s'"), name);
	    break;
	case CT_EXTERNALPROGRAM:
	    print_command_absolute_path(name, ci.ci_path, humanfriendly);
	    break;
	case CT_SPECIALBUILTIN:
	    msgfmt = humanfriendly ? gt("%s: a special built-in\n") : "%s\n";
	    xprintf(msgfmt, name);
	    break;
	case CT_MANDATORYBUILTIN:
	    msgfmt = humanfriendly ? gt("%s: a mandatory built-in\n") : "%s\n";
	    xprintf(msgfmt, name);
	    break;
	case CT_ELECTIVEBUILTIN:
	    msgfmt = humanfriendly ? gt("%s: an elective built-in\n") : "%s\n";
	    xprintf(msgfmt, name);
	    break;
	case CT_EXTENSIONBUILTIN:
	    msgfmt = humanfriendly ? gt("%s: an extension built-in\n") : "%s\n";
	    xprintf(msgfmt, name);
	    break;
	case CT_SUBSTITUTIVEBUILTIN:;
	    const char *cmdpath;
	    if (type & SCT_STDPATH)
		cmdpath = get_command_path_default(name);
	    else
		cmdpath = get_command_path(name, false);
	    if (humanfriendly) {
		msgfmt = (cmdpath == NULL)
		    ? Ngt("%s: a substitutive built-in (not found in $PATH)\n")
		    : Ngt("%s: a substitutive built-in for %s\n");
		xprintf(gt(msgfmt), name, cmdpath);
	    } else {
		xprintf("%s\n", (cmdpath == NULL) ? name : cmdpath);
	    }
	    break;
	case CT_FUNCTION:
	    msgfmt = humanfriendly ? gt("%s: a function\n") : "%s\n";
	    xprintf(msgfmt, name);
	    break;
    }

    free(name);
    return ci.type != CT_NONE;
}

/* Prints the absolute path of the specified command. */
void print_command_absolute_path(
	const char *name, const char *path, bool humanfriendly)
{
    if (path[0] == '/') {
	/* the path is already absolute */
	print_command_path(name, path, humanfriendly);
	return;
    }

    xstrbuf_T abspath;
    sb_init(&abspath);

    const wchar_t *wpwd = getvar(L VAR_PWD);
    if (wpwd != NULL) {
	int r = sb_printf(&abspath, "%ls", wpwd);
	if (r < 0 || !is_same_file(abspath.contents, ".")) {
	    sb_truncate(&abspath, 0);
	}
    }
    if (abspath.length == 0) {
	char *pwd = xgetcwd();
	if (pwd != NULL)
	    sb_catfree(&abspath, pwd);
	else
	    sb_ccat(&abspath, '.');  /* last resort */
    }

    if (abspath.length == 0 || abspath.contents[abspath.length - 1] != '/')
	sb_ccat(&abspath, '/');

    sb_cat(&abspath, path);

    print_command_path(name, abspath.contents, humanfriendly);
    sb_destroy(&abspath);
}

void print_command_path(
	const char *name, const char *path, bool humanfriendly)
{
    if (humanfriendly)
	xprintf(gt("%s: an external command at %s\n"), name, path);
    else
	xprintf("%s\n", path);
}

#if YASH_ENABLE_HELP

const char command_help[] = Ngt(
"execute or identify a command"
);
const char command_syntax[] = Ngt(
"\tcommand [-befp] command [argument...]\n"
"\tcommand -v|-V [-abefkp] command...\n"
);

const char type_help[] = Ngt(
"identify a command"
);
const char type_syntax[] = Ngt(
"\ttype command...\n"
);

#endif

/* The "times" built-in. */
int times_builtin(int argc __attribute__((unused)), void **argv)
{
    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, help_option, 0)) != NULL) {
	switch (opt->shortopt) {
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return special_builtin_error(Exit_ERROR);
	}
    }

    if (xoptind < argc)
	return special_builtin_error(too_many_operands_error(0));

    double clock;
    struct tms tms;
    intmax_t sum, ssm, cum, csm;
    double sus, sss, cus, css;
#define format_time(time, min, sec) \
    do {                                   \
	double tsec = (time) / clock;      \
	double m = trunc(tsec / 60.0);     \
	(min) = (intmax_t) m;              \
	(sec) = tsec - m * 60.0;           \
    } while (0)

    clock = sysconf(_SC_CLK_TCK);
    if (times(&tms) == (clock_t) -1) {
	xerror(errno, Ngt("cannot get the time data"));
	return special_builtin_error(Exit_FAILURE);
    }
    format_time(tms.tms_utime, sum, sus);
    format_time(tms.tms_stime, ssm, sss);
    format_time(tms.tms_cutime, cum, cus);
    format_time(tms.tms_cstime, csm, css);
#undef format_time

    xprintf("%jdm%fs %jdm%fs\n%jdm%fs %jdm%fs\n",
	    sum, sus, ssm, sss, cum, cus, csm, css);
    return (yash_error_message_count == 0) ?
	    Exit_SUCCESS : special_builtin_error(Exit_FAILURE);
}

#if YASH_ENABLE_HELP
const char times_help[] = Ngt(
"print CPU time usage"
);
const char times_syntax[] = Ngt(
"\ttimes\n"
);
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
