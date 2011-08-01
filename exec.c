/* Yash: yet another shell */
/* exec.c: command execution */
/* (C) 2007-2011 magicant */

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
#if YASH_ENABLE_ALIAS
# include "alias.h"
#endif
#include "builtin.h"
#include "exec.h"
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
    CT_SEMISPECIALBUILTIN,
    CT_REGULARBUILTIN,
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
    unsigned breakloopnest; /* target of break/continue */
    exception_T exception;  /* exceptional jump to be done */
    bool noreturn;          /* true when the "return" built-in is not allowed */
    bool iterating;         /* true when iterative execution is ongoing */
} execstate_T;

static void exec_pipelines(const pipeline_T *p, bool finally_exit);
static void exec_pipelines_async(const pipeline_T *p)
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

static void exec_commands(command_T *c, exec_T type);
static inline void next_pipe(pipeinfo_T *pi, bool next)
    __attribute__((nonnull));
static pid_t exec_process(
	command_T *restrict c, exec_T type, pipeinfo_T *restrict pi, pid_t pgid)
    __attribute__((nonnull));
static inline void connect_pipes(pipeinfo_T *pi)
    __attribute__((nonnull));
static void search_command(
	const char *restrict name, const wchar_t *restrict wname,
	commandinfo_T *restrict ci, enum srchcmdtype_T type)
    __attribute__((nonnull));
static inline bool is_special_builtin(const char *cmdname)
    __attribute__((nonnull,pure));
static inline bool assignment_is_temporary(enum cmdtype_T type)
    __attribute__((const));
static bool command_not_found_handler(void *const *argv)
    __attribute__((nonnull));
static void exec_nonsimple_command(command_T *c, bool finally_exit)
    __attribute__((nonnull));
static void exec_simple_command(const commandinfo_T *ci,
	int argc, char *argv0, void **argv, bool finally_exit)
    __attribute__((nonnull));
static void exec_external_program(
	const char *path, int argc, char *argv0, void **argv)
    __attribute__((nonnull));
static void print_xtrace(void *const *argv);
static void exec_fall_back_on_sh(
	int argc, char *const *argv, char *const *env, const char *path)
    __attribute__((nonnull(2,3,4)));
static void exec_function_body(
	command_T *body, void *const *args, bool finally_exit)
    __attribute__((nonnull));
static inline int xexecve(
	const char *path, char *const *argv, char *const *envp)
    __attribute__((nonnull(1)));

static int exec_iteration(void *const *commands, const char *codename)
    __attribute__((nonnull));


/* exit status of the last command */
int laststatus = Exit_SUCCESS;
/* exit status of the command preceding the currently executed trap action */
int savelaststatus = -1;  // -1 if not in a trap handler
/* exit status of the last command substitution */
static int lastcmdsubstatus;
/* the process ID of the last asynchronous list */
pid_t lastasyncpid;

/* This flag is set to true while the shell is executing the condition of an if-
 * statement, an and-or list, etc. to suppress the effect of the "errexit"
 * option. */
static bool supresserrexit = false;

/* state of currently executed loop */
static execstate_T execstate;
/* Note that `execstate' is not reset when a subshell forks. */

/* This flag is set when a special built-in is executed as such. */
bool special_builtin_executed;

/* This flag is set while the "exec" built-in is executed. */
static bool exec_builtin_executed = false;

/* the last assignment. */
static const assign_T *last_assign;

/* a buffer for xtrace. */
static xwcsbuf_T xtrace_buffer = { .contents = NULL };


/* Resets `execstate' to the initial state. */
void reset_execstate(void)
{
    execstate = (struct execstate_T) {
	.loopnest = 0,
	.breakloopnest = UINT_MAX,
	.exception = E_NONE,
	.noreturn = false,
	.iterating = false,
    };
}

/* Saves the current `execstate' and returns it.
 * `execstate' is re-initialized. */
struct execstate_T *save_execstate(void)
{
    struct execstate_T *save = xmalloc(sizeof execstate);
    *save = execstate;
    reset_execstate();
    return save;
}

/* Restores `execstate' to `save' and frees `save'. */
void restore_execstate(struct execstate_T *save)
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
    if (execstate.exception == E_RETURN)
	execstate.exception = E_NONE;
}

/* Returns true iff we're breaking/continuing/returning now. */
bool need_break(void)
{
    return execstate.breakloopnest < execstate.loopnest
	|| execstate.exception != E_NONE
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
    for (bool first = true;
	    p != NULL && !need_break();
	    p = p->next, first = false) {
	if (!first && p->pl_cond == (laststatus != Exit_SUCCESS))
	    continue;

	bool savesee = supresserrexit;
	supresserrexit |= p->pl_neg || p->next != NULL;

	bool self = finally_exit && !doing_job_control_now
	    && !p->next && !p->pl_neg && !any_trap_set;
	exec_commands(p->pl_commands, self ? E_SELF : E_NORMAL);
	if (p->pl_neg) {
	    if (laststatus == Exit_SUCCESS)
		laststatus = Exit_FAILURE;
	    else
		laststatus = Exit_SUCCESS;
	}

	supresserrexit = savesee;
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
	job_T *job = xmalloc(sizeof *job + sizeof *job->j_procs);
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

    bool savesee = supresserrexit;
    supresserrexit = true;
    exec_and_or_lists(c, false);
    supresserrexit = savesee;
    return laststatus == Exit_SUCCESS;
}

/* Executes the for command. */
void exec_for(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_FOR);
    execstate.loopnest++;
    execstate.breakloopnest = UINT_MAX;

    int count;
    void **words;

    if (c->c_forwords != NULL) {
	/* expand the words between "in" and "do" of the for command. */
	if (!expand_line(c->c_forwords, &count, &words)) {
	    laststatus = Exit_EXPERROR;
	    goto finish;
	}
    } else {
	/* no "in" keyword in the for command: use the positional parameters */
	struct get_variable_T v = get_variable(L"@");
	assert(v.type == GV_ARRAY && v.values != NULL);
	words = pldup(v.values, copyaswcs);
	count = (int) v.count;
    }

#define CHECK_LOOP                                      \
    if (execstate.breakloopnest < execstate.loopnest) { \
	goto done;                                      \
    } else if (execstate.exception == E_CONTINUE) {     \
	execstate.exception = E_NONE;                   \
	continue;                                       \
    } else if (execstate.exception != E_NONE) {         \
	goto done;                                      \
    } else if (is_interrupted()) {                      \
	goto done;                                      \
    } else (void) 0

    int i;
    for (i = 0; i < count; i++) {
	if (!set_variable(c->c_forname, words[i],
		    posixly_correct ? SCOPE_GLOBAL : SCOPE_LOCAL, false))
	    goto done;
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
    execstate.breakloopnest = UINT_MAX;

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

    wchar_t *word = expand_single(c->c_casword, tt_single);
    if (word == NULL)
	goto fail;
    word = unescapefree(word);

    for (const caseitem_T *ci = c->c_casitems; ci != NULL; ci = ci->next) {
	for (void **pats = ci->ci_patterns; *pats != NULL; pats++) {
	    wchar_t *pattern = expand_single(*pats, tt_single);
	    if (pattern == NULL)
		goto fail;

	    xfnmatch_T *xfnm = xfnm_compile(
		    pattern, XFNM_HEADONLY | XFNM_TAILONLY);
	    free(pattern);
	    if (xfnm == NULL)
		continue;

	    bool match = (xfnm_wmatch(xfnm, word).start != (size_t) -1);
	    xfnm_free(xfnm);
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
    goto done;
}

/* Executes the function definition. */
void exec_funcdef(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_FUNCDEF);

    wchar_t *funcname = expand_single(c->c_funcname, tt_single);
    if (funcname != NULL) {
	funcname = unescapefree(funcname);

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

/* Executes the commands in a pipeline. */
void exec_commands(command_T *c, exec_T type)
{
    size_t count;
    pid_t pgid;
    command_T *cc;
    job_T *job;
    process_T *ps, *pp;
    pipeinfo_T pinfo = PIPEINFO_INIT;
    commandtype_T lasttype;

    /* count the number of the commands */
    count = 0;
    for (cc = c; cc != NULL; cc = cc->next)
	count++;
    assert(count > 0);

    job = xmallocs(sizeof *job, count, sizeof *job->j_procs);
    ps = job->j_procs;

    /* execute the commands */
    pgid = 0, cc = c, pp = ps;
    do {
	pid_t pid;

	lasttype = cc->c_type;
	next_pipe(&pinfo, cc->next != NULL);
	pid = exec_process(cc,
		(type == E_SELF && cc->next != NULL) ? E_NORMAL : type,
		&pinfo,
		pgid);
	pp->pr_pid = pid;
	if (pid != 0) {
	    pp->pr_status = JS_RUNNING;
	    pp->pr_statuscode = 0;
	} else {
	    pp->pr_status = JS_DONE;
	    pp->pr_statuscode = laststatus;
	}
	pp->pr_name = NULL;   /* name is given later */
	if (pgid == 0)
	    pgid = pid;
	cc = cc->next, pp++;
    } while (cc != NULL);
    assert(type != E_SELF); /* `exec_process' doesn't return for E_SELF */
    assert(pinfo.pi_tonextfds[PIPE_IN] < 0);
    assert(pinfo.pi_tonextfds[PIPE_OUT] < 0);
    if (pinfo.pi_fromprevfd >= 0)
	xclose(pinfo.pi_fromprevfd);           /* close leftover pipe */

    if (pgid == 0) {
	/* nothing more to do if we didn't fork */
	free(job);
    } else {
	job->j_pgid = doing_job_control_now ? pgid : 0;
	job->j_status = JS_RUNNING;
	job->j_statuschanged = true;
	job->j_legacy = false;
	job->j_nonotify = false;
	job->j_pcount = count;
	set_active_job(job);
	if (type == E_NORMAL) {
	    wait_for_job(ACTIVE_JOBNO, doing_job_control_now, false, false);
	    if (doing_job_control_now)
		put_foreground(shell_pgid);
	    laststatus = calc_status_of_job(job);
	} else {
	    assert(type == E_ASYNC);
	    laststatus = Exit_SUCCESS;
	    lastasyncpid = ps[count - 1].pr_pid;
	}
	if (job->j_status != JS_DONE) {
	    for (cc = c, pp = ps; cc != NULL; cc = cc->next, pp++)
		pp->pr_name = command_to_wcs(cc, false);
	    add_job(type == E_NORMAL || shopt_curasync);
	} else {
	    notify_signaled_job(ACTIVE_JOBNO);
	    remove_job(ACTIVE_JOBNO);
	}
    }

    handle_signals();

    if (shopt_errexit && !supresserrexit
	    && laststatus != Exit_SUCCESS && lasttype == CT_SIMPLE
#if YASH_ENABLE_LINEEDIT
	    && !(le_state & LE_STATE_COMPLETING)
#endif
	    )
	exit_shell_with_status(laststatus);
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

/* Executes the command.
 * If job control is active, the child process's process group ID is set to
 * `pgid'. If `pgid' is 0, the child process's process ID is used as the process
 * group ID.
 * If the child process forked successfully, its process ID is returned.
 * If the command was executed without forking, `laststatus' is set to the exit
 * status of the command and 0 is returned.
 * if `type' is E_SELF, this function never returns. */
pid_t exec_process(
	command_T *restrict c, exec_T type, pipeinfo_T *restrict pi, pid_t pgid)
{
    bool early_fork;   /* do early fork? */
    bool finally_exit; /* never return? */
    int argc;
    void **argv = NULL;
    char *argv0 = NULL;
    pid_t cpid = 0;

    current_lineno = c->c_lineno;

    /* fork first if `type' is E_ASYNC, the command type is subshell,
     * or there is a pipe. */
    early_fork = (type != E_SELF)
	&& (type == E_ASYNC || c->c_type == CT_SUBSHELL
		|| pi->pi_fromprevfd >= 0 || pi->pi_tonextfds[PIPE_OUT] >= 0);
    finally_exit = (type == E_SELF);
    if (early_fork) {
	sigtype_T sigtype = (type == E_ASYNC) ? t_quitint : 0;
	cpid = fork_and_reset(pgid, type == E_NORMAL, sigtype);
	if (cpid != 0)
	    goto done;
	finally_exit = true;
    }

    if (c->c_type == CT_SIMPLE) {
	if (!expand_line(c->c_words, &argc, &argv)) {
	    laststatus = Exit_EXPERROR;
	    goto done;
	}
	if (is_interrupted()) {
	    plfree(argv, free);
	    goto done;
	}
	if (argc == 0) {
	    argv0 = NULL;
	} else {
	    argv0 = malloc_wcstombs(argv[0]);
	    if (argv0 == NULL)
		argv0 = xstrdup("");
	}
    }
    /* `argc' and `argv' are used only for `CT_SIMPLE'. */

    lastcmdsubstatus = Exit_SUCCESS;

    /* connect pipes and close leftovers */
    connect_pipes(pi);

    /* open redirections */
    savefd_T *savefd;
    if (!open_redirections(c->c_redirs, &savefd)) {
	/* On redirection error, the command is not executed. */
	laststatus = Exit_REDIRERR;
	if (posixly_correct && !is_interactive_now && c->c_type == CT_SIMPLE &&
		argc > 0 && is_special_builtin(argv0))
	    finally_exit = true;
	goto done2;
    }
    
    if (type == E_ASYNC && pi->pi_fromprevfd < 0)
	maybe_redirect_stdin_to_devnull();

    if (c->c_type != CT_SIMPLE) {
	exec_nonsimple_command(c, finally_exit && savefd == NULL);
	goto done1;
    }

    last_assign = c->c_assigns;
    if (argc == 0) {
	/* if there is no command word, just perform assignments */
	if (do_assignments(c->c_assigns, false, shopt_allexport))
	    laststatus = lastcmdsubstatus;
	else
	    laststatus = Exit_ASSGNERR;
	print_xtrace(NULL);
	goto done2;
    }

    commandinfo_T cmdinfo;
    bool temp;

    /* check if the command is a special built-in or a function and determine
     * whether we have to open a temporary environment. */
    search_command(argv0, argv[0], &cmdinfo, SCT_BUILTIN | SCT_FUNCTION);
    special_builtin_executed = (cmdinfo.type == CT_SPECIALBUILTIN);
    temp = c->c_assigns != NULL && assignment_is_temporary(cmdinfo.type);
    if (temp)
	open_new_environment(true);

    /* perform the assignments */
    if (!do_assignments(c->c_assigns, temp, true)) {
	/* On assignment error, the command is not executed. */
	print_xtrace(NULL);
	laststatus = Exit_ASSGNERR;
	if (posixly_correct && !is_interactive &&
		cmdinfo.type == CT_SPECIALBUILTIN)
	    finally_exit = true;
	goto done3;
    }
    print_xtrace(argv);

    /* find command path */
    if (cmdinfo.type == CT_NONE) {
	search_command(argv0, argv[0], &cmdinfo,
		SCT_EXTERNAL | SCT_BUILTIN | SCT_CHECK);
	if (cmdinfo.type == CT_NONE) {
	    if (!posixly_correct && command_not_found_handler(argv))
		goto done3;
	    if (wcschr(argv[0], L'/') != NULL) {
		cmdinfo.type = CT_EXTERNALPROGRAM;
		cmdinfo.ci_path = argv0;
	    }
	}
    }

    /* create a child process to execute the external command */
    if (cmdinfo.type == CT_EXTERNALPROGRAM && !finally_exit) {
	cpid = fork_and_reset(pgid, type == E_NORMAL, t_leave);
	if (cpid != 0)
	    goto done3;
	finally_exit = true;
    }

    /* execute! */
    bool finally_exit2;
    switch (cmdinfo.type) {
	case CT_EXTERNALPROGRAM:
	    finally_exit2 = true;
	    break;
	case CT_FUNCTION:
	    finally_exit2 = (finally_exit && /* !temp && */ savefd == NULL);
	    break;
	default:
	    finally_exit2 = false;
	    break;
    }
    exec_simple_command(&cmdinfo, argc, argv0, argv, finally_exit2);

    /* Redirections are not undone after a successful "exec" command:
     * remove the saved data of file descriptors. */
    if (exec_builtin_executed && laststatus == Exit_SUCCESS) {
	clear_savefd(savefd);
	savefd = NULL;
    }
    exec_builtin_executed = false;

done3:
    if (temp)
	close_current_environment();
done2:
    if (c->c_type == CT_SIMPLE)
	plfree(argv, free), free(argv0);
done1:
    undo_redirections(savefd);
done:
    if (cpid < 0) {
	laststatus = Exit_NOEXEC;
	cpid = 0;
    }
    if (finally_exit)
	exit_shell();
    return cpid;
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
	bool save_doing_job_control_now = doing_job_control_now;
	if (save_doing_job_control_now && pgid >= 0) {
	    setpgid(0, pgid);
	    if (pgid == 0)
		pgid = getpgrp();
	    shell_pgid = pgid;
	    if (fg)
		put_foreground(pgid);
	}
	if (sigtype & t_quitint)
	    if (posixly_correct || !save_doing_job_control_now)
		ignore_sigquit_and_sigint();
	if (sigtype & t_tstp)
	    if (save_doing_job_control_now)
		ignore_sigtstp();
	if (!(sigtype & t_leave)) {
	    clear_traps();
	    neglect_all_jobs();
#if YASH_ENABLE_HISTORY
	    close_history_file();
#endif
	    reset_execstate();
	}
	restore_signals(sigtype & t_leave);  /* signal mask is restored here */
	clear_shellfds(sigtype & t_leave);
	is_interactive_now = false;
    }
    return cpid;
}

/* Searches for a command.
 * The result is assigned to `*ci'.
 * `name' and `wname' must contain the same string value.
 * If the SCT_ALL flag is not set:
 *   *  a function whose name contains a slash cannot be found
 *   *  a regular built-in cannot be found in the POSIXly correct mode if the
 *      SCT_EXTERNAL flag is not set either.
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
	if (bi->type == BI_SEMISPECIAL) {
	    ci->type = CT_SEMISPECIALBUILTIN;
	    ci->ci_builtin = bi->body;
	    return;
	} else if (!posixly_correct) {
	    goto regular_builtin;
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
regular_builtin:
		    assert(bi->type == BI_REGULAR);
		    ci->type = CT_REGULARBUILTIN;
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

/* Determines whether the assignments should be temporary according to `type'.*/
bool assignment_is_temporary(enum cmdtype_T type)
{
    switch (type) {
	case CT_SPECIALBUILTIN:
	case CT_FUNCTION:
	    return false;
	case CT_NONE:
	case CT_SEMISPECIALBUILTIN:
	case CT_REGULARBUILTIN:
	case CT_EXTERNALPROGRAM:
	    return true;
	default:
	    assert(false);
    }
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

    result = exec_variable_as_commands(
	    L VAR_COMMAND_NOT_FOUND_HANDLER, VAR_COMMAND_NOT_FOUND_HANDLER);
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

/* Executes the specified command whose type is not `CT_SIMPLE'.
 * The redirections for the command is not performed in this function.
 * For CT_SUBSHELL, this function must be called in an already-forked subshell.
 */
void exec_nonsimple_command(command_T *c, bool finally_exit)
{
    /* increment the reference count of `c' to prevent `c' from being freed
     * during execution. */
    c = comsdup(c);

    switch (c->c_type) {
    case CT_SIMPLE:
	assert(false);
    case CT_SUBSHELL:
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
    case CT_FUNCDEF:
	exec_funcdef(c, finally_exit);
	break;
    }

    comsfree(c);
}

/* Executes the simple command. */
/* `argv0' is the multibyte version of `argv[0]' */
void exec_simple_command(
	const commandinfo_T *ci, int argc, char *argv0, void **argv,
	bool finally_exit)
{
    assert(plcount(argv) == (size_t) argc);

    switch (ci->type) {
    case CT_NONE:
	xerror(0, Ngt("no such command `%s'"), argv0);
	laststatus = Exit_NOTFOUND;
	break;
    case CT_EXTERNALPROGRAM:
	assert(finally_exit);
	exec_external_program(ci->ci_path, argc, argv0, argv);
	break;
    case CT_SPECIALBUILTIN:
    case CT_SEMISPECIALBUILTIN:
    case CT_REGULARBUILTIN:
	yash_error_message_count = 0;

	const wchar_t *savecbn = current_builtin_name;
	current_builtin_name = argv[0];

	laststatus = ci->ci_builtin(argc, argv);

	current_builtin_name = savecbn;
	break;
    case CT_FUNCTION:
	exec_function_body(ci->ci_function, &argv[1], finally_exit);
	break;
    }
    if (finally_exit)
	exit_shell();
}

/* Executes the external program.
 *  path:  path to the program to be executed
 *  argc:  number of strings in `argv'
 *  argv0: multibyte version of `argv[0]'
 *  argv:  pointer to an array of pointers to wide strings that are passed to
 *         the program */
void exec_external_program(const char *path, int argc, char *argv0, void **argv)
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

    xexecve(path, mbsargv, environ);
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
	exec_fall_back_on_sh(argc, mbsargv, environ, path);
    }
    laststatus = (saveerrno == ENOENT) ? Exit_NOTFOUND : Exit_NOEXEC;

    set_signals();

    for (int i = 1; i < argc; i++)
	free(mbsargv[i]);
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
    bool tracevars = xtrace_buffer.contents != NULL
		  && xtrace_buffer.length > 0;

    if (shopt_xtrace && (tracevars || argv != NULL)
#if YASH_ENABLE_LINEEDIT
	    && !(le_state & LE_STATE_ACTIVE)
#endif
	    ) {
	bool first = true;

	struct promptset_T prompt = get_prompt(4);
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
		fprintf(stderr, "%ls", (wchar_t *) *a);
		first = false;
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
 * (void *). */
void exec_function_body(command_T *body, void *const *args, bool finally_exit)
{
    bool save_noreturn;
    savefd_T *savefd;

    save_noreturn = execstate.noreturn;
    execstate.noreturn = false;

    if (open_redirections(body->c_redirs, &savefd)) {
	open_new_environment(false);
	set_positional_parameters(args);
	exec_nonsimple_command(body, finally_exit);
	if (execstate.exception == E_RETURN)
	    execstate.exception = E_NONE;
	close_current_environment();
    }
    undo_redirections(savefd);

    execstate.noreturn = save_noreturn;
}

/* Calls `execve' until it doesn't return EINTR. */
int xexecve(const char *path, char *const *argv, char *const *envp)
{
    do
	execve(path, argv, envp);
    while (errno == EINTR);
    return -1;
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
	switch (execstate.exception) {
	    case E_BREAK_ITERATION:
		execstate.exception = E_NONE;
		/* falls thru! */
	    default:
		goto done;
	    case E_CONTINUE_ITERATION:
		execstate.exception = E_NONE;
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
    void **array;

    switch (gv.type) {
	case GV_NOTFOUND:
	    return -1;
	case GV_SCALAR:
	    array = gv.values;
	    break;
	case GV_ARRAY:
	    /* copy the array values in case they are unset during execution */
	    array = plndup(gv.values, gv.count, copyaswcs);
	    break;
	case GV_ARRAY_CONCAT:
	    /* should execute the concatenated value, but is not supported now*/
	    return -1;
	default:
	    assert(false);
    }

    int result = exec_iteration(array, codename);
    plfree(array, free);
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

    struct parsestate_T *parsestate = save_parse_state();
    struct execstate_T *execstate = save_execstate();
    int savelaststatus = laststatus;
    bool saveposix = posixly_correct;
    posixly_correct = false;
    open_new_environment(false);
    set_positional_parameters((void *[]) { (void *) cmdname, NULL });

    le_compdebug("executing file \"%s\" (autoload)", path);
    exec_input(fd, mbsfilename, false, false, false);
    le_compdebug("finished executing file \"%s\"", path);

    close_current_environment();
    posixly_correct = saveposix;
    laststatus = savelaststatus;
    restore_execstate(execstate);
    restore_parse_state(parsestate);
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

    int savelaststatus = laststatus;
    bool saveposix = posixly_correct;
    bool save_noreturn = execstate.noreturn;
    posixly_correct = false;
    execstate.noreturn = false;

    le_compdebug("executing completion function \"%ls\"", funcname);

    /* don't use `exec_function_body': We have to prepare some special local
     * variables. */
    savefd_T *savefd;
    open_new_environment(false);
    set_positional_parameters((void *[]) { NULL });
    set_completion_variables();
    if (open_redirections(func->c_redirs, &savefd)) {
	exec_nonsimple_command(func, false);
	if (execstate.exception == E_RETURN)
	    execstate.exception = E_NONE;
    }
    undo_redirections(savefd);
    close_current_environment();

    le_compdebug("finished executing completion function \"%ls\"", funcname);
    le_compdebug("  with the exit status of %d", laststatus);

    execstate.noreturn = save_noreturn;
    posixly_correct = saveposix;
    laststatus = savelaststatus;

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

/* Options for the "break", "continue" and "eval" built-ins. */
static const struct xgetopt_T iter_options[] = {
    { L'i', L"iteration", OPTARG_NONE, false, NULL, },
#if YASH_ENABLE_HELP
    { L'-', L"help",      OPTARG_NONE, false, NULL, },
#endif
    { L'\0', NULL, 0, false, NULL, },
};

/* The "return" built-in, which accepts the following option:
 *  -n: don't return from a function. */
int return_builtin(int argc, void **argv)
{
    static const struct xgetopt_T options[] = {
	{ L'n', L"no-return", OPTARG_NONE, false, NULL, },
#if YASH_ENABLE_HELP
	{ L'-', L"help",      OPTARG_NONE, false, NULL, },
#endif
	{ L'\0', NULL, 0, false, NULL, },
    };

    bool noreturn = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, options, 0)) != NULL) {
	switch (opt->shortopt) {
	    case L'n':
		noreturn = true;
		break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		goto print_usage;
	}
    }
    if (argc - xoptind > 1)
	goto print_usage;

    int status;
    const wchar_t *statusstr = ARGV(xoptind);
    if (statusstr != NULL) {
	if (!xwcstoi(statusstr, 10, &status) || status < 0) {
	    xerror(0, Ngt("`%ls' is not a valid integer"), statusstr);
	    status = Exit_ERROR;
	    SPECIAL_BI_ERROR;
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
	execstate.exception = E_RETURN;
    }
    return status;

print_usage:
    fprintf(stderr,
	    gt(posixly_correct ? Ngt("Usage:  %ls [n]\n")
		: Ngt("Usage:  %ls [-n] [n]\n")),
	    ARGV(0));
    SPECIAL_BI_ERROR;
    return Exit_ERROR;
}

#if YASH_ENABLE_HELP
const char *return_help[] = { Ngt(
"return - return from a function or script\n"
), Ngt(
"\treturn [-n] [n]\n"
), Ngt(
"The return built-in exits the currently executing function or script file\n"
"with the exit status of <n>. If <n> is not specified, it defaults to the\n"
"exit status of the last executed command. <n> should be between 0 and 255\n"
"inclusive.\n"
), Ngt(
"If the -n (--no-return) option is specified, this built-in does not return\n"
"from a function or script; simply returns the specified exit status.\n"
"The -n option cannot be used in the POSIXly correct mode.\n"
), NULL };
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
	    default:  print_usage:
		fprintf(stderr,
			gt("Usage:  %ls [n]\n        %ls -i\n"),
			ARGV(0), ARGV(0));
		SPECIAL_BI_ERROR;
		return Exit_ERROR;
	}
    }
    if (argc - xoptind > (iter ? 0 : 1))
	goto print_usage;

    if (iter) {
	/* break/continue iteration */
	if (!execstate.iterating) {
	    xerror(0, Ngt("not in an iteration"));
	    return Exit_ERROR;
	}
	if (wcscmp(ARGV(0), L"break") == 0) {
	    execstate.exception = E_BREAK_ITERATION;
	} else {
	    assert(wcscmp(ARGV(0), L"continue") == 0);
	    execstate.exception = E_CONTINUE_ITERATION;
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
		SPECIAL_BI_ERROR;
		return Exit_ERROR;
	    } else if (countl == 0) {
		xerror(0, Ngt("%u is not a positive integer"), 0u);
		SPECIAL_BI_ERROR;
		return Exit_ERROR;
	    } else if (countl > UINT_MAX) {
		count = UINT_MAX;
	    } else {
		count = (unsigned) countl;
	    }
	}
	assert(count > 0);
	if (execstate.loopnest == 0) {
	    xerror(0, Ngt("not in a loop"));
	    SPECIAL_BI_ERROR;
	    return Exit_ERROR;
	}
	if (count > execstate.loopnest)
	    count = execstate.loopnest;
	if (wcscmp(ARGV(0), L"break") == 0) {
	    execstate.breakloopnest = execstate.loopnest - count;
	} else {
	    assert(wcscmp(ARGV(0), L"continue") == 0);
	    execstate.breakloopnest = execstate.loopnest - count + 1;
	    execstate.exception = E_CONTINUE;
	}
	return Exit_SUCCESS;
    }
}

#if YASH_ENABLE_HELP

const char *break_help[] = { Ngt(
"break - exit a loop\n"
), Ngt(
"\tbreak [n]\n"
"\tbreak -i\n"
), Ngt(
"The first form exits the currently executing for/while/until loop.\n"
"If <n> is specified, exits the <n>th outer loop.\n"
), Ngt(
"The second form, with the -i (--iteration) option, exits the current\n"
"iterative execution.\n"
), NULL };

const char *continue_help[] = { Ngt(
"continue - continue a loop\n"
), Ngt(
"\tcontinue [n]\n"
"\tcontinue -i\n"
), Ngt(
"The first form ends the current iteration of a for/while/until loop and\n"
"resumes the next iteration. If <n> is specified, ends the iteration of the\n"
"<n>th outer loop.\n"
), Ngt(
"The second form, with the -i (--iteration) option, ends the execution of\n"
"commands and resumes the next iteration of the current iterative execution.\n"
), NULL };

#endif /* YASH_ENABLE_HELP */

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
		fprintf(stderr, gt(posixly_correct
			    ? Ngt("Usage:  eval [arg...]\n")
			    : Ngt("Usage:  eval [-i] [arg...]\n")));
		SPECIAL_BI_ERROR;
		return Exit_ERROR;
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
const char *eval_help[] = { Ngt(
"eval - evaluate arguments as a command\n"
), Ngt(
"\teval [-i] [arg...]\n"
), Ngt(
"The eval built-in parses and executes the specified <arg>s as commands in\n"
"the current shell environment. Without the -i (--iteration) option, all the\n"
"<arg>s are joined with a space inserted between each <arg> and the whole\n"
"resultant string is parsed at a time. With the -i option, <arg>s are parsed\n"
"and executed one by one (iterative execution).\n"
"In the POSIXly correct mode, the -i option cannot be used.\n"
), NULL };
#endif

/* The "." built-in, which accepts the following option:
 *  -A: disable aliases
 *  -L: autoload */
int dot_builtin(int argc, void **argv)
{
    static const struct xgetopt_T options[] = {
	{ L'A', L"no-alias", OPTARG_NONE, false, NULL, },
	{ L'L', L"autoload", OPTARG_NONE, false, NULL, },
#if YASH_ENABLE_HELP
	{ L'-', L"help",     OPTARG_NONE, false, NULL, },
#endif
	{ L'\0', NULL, 0, false, NULL, },
    };

    bool enable_alias = true, autoload = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, options, XGETOPT_POSIX)) != NULL) {
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
	    default:  print_usage:
		fprintf(stderr, gt(posixly_correct
			    ? Ngt("Usage:  . file\n")
			    : Ngt("Usage:  . [-AL] file [arg...]\n")));
		SPECIAL_BI_ERROR;
		return Exit_ERROR;
	}
    }

    const wchar_t *filename = ARGV(xoptind++);
    if (filename == NULL)
	goto print_usage;

    bool has_args = xoptind < argc;
    if (has_args && posixly_correct)
	goto print_usage;

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

    if (has_args) {
	open_new_environment(false);
	set_positional_parameters(&argv[xoptind]);
    }

    int fd = move_to_shellfd(open(path, O_RDONLY));
    if (path != mbsfilename)
	free(path);
    if (fd < 0) {
	xerror(errno, Ngt("cannot open file `%s'"), mbsfilename);
	goto error;
    }

    exec_input(fd, mbsfilename, false, enable_alias, false);
    remove_shellfd(fd);
    xclose(fd);
    free(mbsfilename);

    if (has_args) {
	close_current_environment();
    }

    return laststatus;

error:
    free(mbsfilename);
    if (!is_interactive)
	exit_shell_with_status(Exit_FAILURE);
    return Exit_FAILURE;
}

#if YASH_ENABLE_HELP
const char *dot_help[] = { Ngt(
"dot - read a file and execute commands\n"
), Ngt(
"\t. [-AL] file [arg...]\n"
), Ngt(
"The dot built-in reads the specified <file> and executes commands in it.\n"
"If <arg>s are specified, they are used as the positional parameters.\n"
"Otherwise, the positional parameters are not changed.\n"
"If <file> does not contain any slashes, the shell searches $PATH for a\n"
"readable shell script file whose name is <file>. To ensure that the file in\n"
"the current working directory is used, start <file> with `./'.\n"
), Ngt(
"If the -L (--autoload) option is specified, the shell searches\n"
"$YASH_LOADPATH instead of $PATH, regardless of whether <file> contains\n"
"slashes.\n"
), Ngt(
"If the -A (--no-alias) option is specified, alias substitution is not\n"
"performed while processing the file.\n"
), Ngt(
"In the POSIXly correct mode, options cannot be used and <arg>s must not be\n"
"given.\n"
), NULL };
#endif

/* The "exec" built-in, which accepts the following options:
 *  -a name: give <name> as argv[0] to the command
 *  -c: don't pass environment variables to the command
 *  -f: suppress error when we have stopped jobs */
int exec_builtin(int argc, void **argv)
{
    static const struct xgetopt_T options[] = {
	{ L'a', L"as",    OPTARG_REQUIRED, false, NULL, },
	{ L'c', L"clear", OPTARG_NONE,     false, NULL, },
	{ L'f', L"force", OPTARG_NONE,     false, NULL, },
#if YASH_ENABLE_HELP
	{ L'-', L"help",  OPTARG_NONE,     false, NULL, },
#endif
	{ L'\0', NULL, 0, false, NULL, },
    };

    const wchar_t *as = NULL;
    bool clear = false, force = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, options, XGETOPT_POSIX)) != NULL) {
	switch (opt->shortopt) {
	    case L'a':  as = xoptarg;  break;
	    case L'c':  clear = true;  break;
	    case L'f':  force = true;  break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		fprintf(stderr, gt(posixly_correct
		    ? Ngt("Usage:  exec [command [arg...]]\n")
		    : Ngt("Usage:  exec [-cf] [-a name] [command [arg...]]\n")));
		SPECIAL_BI_ERROR;
		return Exit_ERROR;
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
    char *freelater = NULL;
    char *args[argc + 1];
    for (int i = 0; i < argc; i++) {
	args[i] = malloc_wcstombs(ARGV(i));
	if (args[i] == NULL) {
	    xerror(EILSEQ, Ngt("cannot convert wide characters into "
			"multibyte characters: replaced with empty string"));
	    args[i] = xstrdup("");
	}
    }
    args[argc] = NULL;

    const char *commandpath;
    if (wcschr(ARGV(0), L'/') != NULL) {
	commandpath = args[0];
    } else {
	commandpath = get_command_path(args[0], false);
	if (commandpath == NULL) {
	    xerror(0, Ngt("no such command `%s'"), args[0]);
	    err = Exit_NOTFOUND;
	    goto err;
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
	    if (value != NULL)
		pl_add(&list, malloc_printf("%ls=%s", assign->a_name, value));
	}
	envs = (char **) pl_toary(&list);
    } else {
	envs = environ;
    }

    if (as != NULL) {
	freelater = args[0];
	args[0] = malloc_wcstombs(as);
	if (args[0] == NULL) {
	    xerror(EILSEQ, Ngt("cannot convert wide characters into "
			"multibyte characters: replaced with empty string"));
	    args[0] = xstrdup("");
	}
    }

    restore_signals(true);
    xexecve(commandpath, args, envs);
    if (errno != ENOEXEC) {
	if (errno == EACCES && is_directory(commandpath))
	    errno = EISDIR;
	if (strcmp(args[0], commandpath) == 0)
	    xerror(errno, Ngt("cannot execute command `%s'"), args[0]);
	else
	    xerror(errno, Ngt("cannot execute command `%s' (%s)"),
		    args[0], commandpath);
    } else {
	exec_fall_back_on_sh(argc, args, envs, commandpath);
    }
    if (posixly_correct || !is_interactive_now)
	exit(Exit_NOEXEC);
    set_signals();
    err = Exit_NOEXEC;

    if (clear)
	plfree((void **) envs, free);

err:
    for (int i = 0; i < argc; i++)
	free(args[i]);
    free(freelater);
    if (posixly_correct || !is_interactive_now)
	exit(err);
    return err;
}

#if YASH_ENABLE_HELP
const char *exec_help[] = { Ngt(
"exec - execute a command in the shell process\n"
), Ngt(
"\texec [-cf] [-a name] [command [args...]]\n"
), Ngt(
"The exec built-in replaces the shell process with the specified command.\n"
"The shell process is `changed' into the new command's process. No child\n"
"process is created. When an interactive shell has stopped jobs, the -f\n"
"(--force) option is required to perform exec.\n"
), Ngt(
"If the -c (--clear) option is specified, the command is executed only with\n"
"the environment variables assigned for this command.\n"
), Ngt(
"If the -a <name> (--as=<name>) option is specified, <name> is passed to the\n"
"command instead of <command> as the zeroth argument.\n"
), Ngt(
"If no <command> is given, the shell does nothing. As a special result,\n"
"the effects of redirections associated with the exec built-in remain\n"
"after the command.\n"
), Ngt(
"In the POSIXly correct mode, none of these options are available and the -f\n"
"option is always assumed.\n"
), NULL };
#endif

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
    static const struct xgetopt_T options[] = {
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

    bool argv0istype = wcscmp(ARGV(0), L"type") == 0;
    bool printinfo = argv0istype, humanfriendly = argv0istype;
    enum srchcmdtype_T type = 0;
    bool aliases = false, keywords = false, defpath = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, options, XGETOPT_POSIX)) != NULL) {
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
		goto print_usage;
	}
    }

    if (!printinfo) {
	if (aliases || keywords)
	    goto print_usage;
	if (xoptind == argc) {
	    if (posixly_correct)
		goto print_usage;
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
	if (!argv0istype && posixly_correct && argc - xoptind != 1)
	    goto print_usage;
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

print_usage:
    if (argv0istype)
	fprintf(stderr, gt("Usage:  type command...\n"));
    else if (posixly_correct)
	fprintf(stderr, gt("Usage:  command [-p] command [arg...]\n"
			   "        command -v|-V [-p] command\n"));
    else
	fprintf(stderr, gt("Usage:  command [-befp] command [arg...]\n"
			   "        command -v|-V [-abefkp] command...\n"));
    return Exit_ERROR;
}

/* Executes the specified simple command.
 * `argc' must be positive.
 * Returns the exit status of the command. */
int command_builtin_execute(int argc, void **argv, enum srchcmdtype_T type)
{
    char *argv0 = malloc_wcstombs(argv[0]);
    commandinfo_T ci;
    bool finally_exit = false;

    if (argv0 == NULL) {
	xerror(EILSEQ, NULL);
	return Exit_NOTFOUND;
    }

    search_command(argv0, argv[0], &ci, type);
    if (ci.type == CT_EXTERNALPROGRAM) {
	pid_t cpid = fork_and_reset(0, true, t_leave);
	if (cpid < 0) {
	    free(argv0);
	    return Exit_NOEXEC;
	} else if (cpid > 0) {
	    wchar_t **namep = wait_for_child(
		    cpid,
		    doing_job_control_now ? cpid : 0,
		    doing_job_control_now);
	    if (namep != NULL)
		*namep = joinwcsarray(argv, L" ");
	    free(argv0);
	    return laststatus;
	}
	finally_exit = true;
    }
    exec_simple_command(&ci, argc, argv0, argv, finally_exit);
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

#if YASH_ENABLE_ALIAS
    if (aliases) {
	if (print_alias_if_defined(commandname, humanfriendly)) {
	    return true;
	} else {
	    if (ferror(stdout))
		return false;
	}
    }
#else
    (void) aliases;
#endif

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
	case CT_SEMISPECIALBUILTIN:
	    msgfmt = humanfriendly ? gt("%s: a semi-special built-in\n")
		                   : "%s\n";
	    xprintf(msgfmt, name);
	    break;
	case CT_REGULARBUILTIN:;
	    const char *cmdpath;
	    if (type & SCT_STDPATH)
		cmdpath = get_command_path_default(name);
	    else
		cmdpath = get_command_path(name, false);
	    if (humanfriendly) {
		msgfmt = (cmdpath == NULL)
		    ? Ngt("%s: a regular built-in (not found in $PATH)\n")
		    : Ngt("%s: a regular built-in at %s\n");
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
	if (humanfriendly)
	    xprintf(gt("%s: an external command at %s\n"), name, path);
	else
	    xprintf("%s\n", path);
	return;
    }

    const wchar_t *wpwd = getvar(L VAR_PWD);
    char *pwd = NULL;
    if (wpwd != NULL) {
	pwd = malloc_wcstombs(wpwd);
	if (pwd != NULL && !is_same_file(pwd, ".")) {
	    free(pwd);
	    pwd = NULL;
	}
    }
    if (pwd == NULL) {
	pwd = xgetcwd();
	if (pwd == NULL)
	    pwd = xstrdup(".");  /* last resort */
    }

    if (humanfriendly)
	xprintf(gt("%s: an external command at %s/%s\n"), name, pwd, path);
    else
	xprintf("%s/%s\n", pwd, path);
    free(pwd);
}

#if YASH_ENABLE_HELP

const char *command_help[] = { Ngt(
"command - execute or identify a command\n"
), Ngt(
"\tcommand [-befp] command [argument...]\n"
"\tcommand -v|-V [-abefkp] command...\n"
), Ngt(
"The command built-in executes or identifies the specified command.\n"
), Ngt(
"Without the -v or -V option, <command> is executed with <argument>s given.\n"
"<command> is treated as a built-in or external command or a function\n"
"according to the options specified. If the -p (--standard-path) option is\n"
"given, the system's default PATH is searched for the command instead of the\n"
"current $PATH.\n"
), Ngt(
"With the -v (--identify) option, <command> is identified. If the command is\n"
"found in $PATH, its full path is printed. If it is a built-in or a function,\n"
"the command name is simply printed. If it is an alias, it is printed in the\n"
"form like \"alias ll='ls -l'\". If the command is not found, nothing is\n"
"printed and the exit status is non-zero.\n"
), Ngt(
"With the -V (--verbose-identify) option, the command is identified in the\n"
"same way but the result is printed verbosely in a human-readable form.\n"
), Ngt(
"The following options specify the command type:\n"
"  -a --alias\n"
"  -b --builtin-command\n"
"  -e --external-command\n"
"  -f --function\n"
"  -k --keyword\n"
"When none of these are specified, they default to -be (without -v or -V) or\n"
"-abefk (with -v or -V)\n"
), NULL };

const char *type_help[] = { Ngt(
"type - identify a command\n"
), Ngt(
"\ttype command...\n"
), Ngt(
"The type built-in prints the type of <command>s.\n"
"Same as `command -V <command>...'.\n"
), NULL };

#endif /* YASH_ENABLE_HELP */

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
		goto print_usage;
	}
    }

    if (xoptind < argc)
	goto print_usage;

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
	return Exit_FAILURE;
    }
    format_time(tms.tms_utime, sum, sus);
    format_time(tms.tms_stime, ssm, sss);
    format_time(tms.tms_cutime, cum, cus);
    format_time(tms.tms_cstime, csm, css);
#undef format_time

    xprintf("%jdm%fs %jdm%fs\n%jdm%fs %jdm%fs\n",
	    sum, sus, ssm, sss, cum, cus, csm, css);
    return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;

print_usage:
    fprintf(stderr, gt("Usage:  times\n"));
    SPECIAL_BI_ERROR;
    return Exit_ERROR;
}

#if YASH_ENABLE_HELP
const char *times_help[] = { Ngt(
"times - print process times\n"
), Ngt(
"\ttimes\n"
), Ngt(
"The times built-in prints the accumulated user and system times consumed by\n"
"the shell process and all of its child processes.\n"
"The first output line is for the shell process, and the second the child\n"
"processes. For each line, the user time is printed followed by the system\n"
"time.\n"
), NULL };
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
