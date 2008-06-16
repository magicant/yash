/* Yash: yet another shell */
/* exec.c: command execution */
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
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#if HAVE_PATHS_H
# include <paths.h>
#endif
#include "option.h"
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "wfnmatch.h"
#include "path.h"
#include "input.h"
#include "parser.h"
#include "variable.h"
#include "sig.h"
#include "expand.h"
#include "redir.h"
#include "job.h"
#include "builtin.h"
#include "exec.h"
#include "yash.h"


/* way of command execution */
typedef enum {
    execnormal,  /* normal execution */
    execasync,   /* asynchronous execution */
    execself,    /* execution in the shell's own process */
} exec_T;

/* info about file descriptors of pipes */
typedef struct pipeinfo_T {
    int pi_fromprevfd;      /* reading end of the pipe from the previous process */
    int pi_tonextfds[2];    /* both ends of the pipe to the next process */
    int pi_loopoutfd;       /* writing end of the pipe to the first process */
    /* for each member, -1 is assigned if a pipe is unused */
} pipeinfo_T;
#define PIDX_IN  0   /* index of the reading end of a pipe */
#define PIDX_OUT 1   /* index of the writing end of a pipe */
#define PIPEINFO_INIT { -1, { -1, -1 }, -1 }  /* used to initialize `pipeinfo_T' */

/* info about a simple command to execute */
typedef struct commandinfo_T {
    enum cmdtype_T {        /* type of command */
	externalprogram,
	specialbuiltin,
	semispecialbuiltin,
	regularbuiltin,
	function,
    } type;
    union {
	const char *path;     /* command path (for external program) */
	main_T *builtin;      /* body of built-in */
	command_T *function;  /* body of function */
    } value;
} commandinfo_T;
#define ci_path     value.path
#define ci_builtin  value.builtin
#define ci_function value.function

static inline bool need_break(void)
    __attribute__((pure));

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

static inline void next_pipe(pipeinfo_T *pi, bool next)
    __attribute__((nonnull));
static inline void connect_pipes(pipeinfo_T *pi)
    __attribute__((nonnull));

static void exec_commands(command_T *c, exec_T type, bool looppipe);
static pid_t exec_process(command_T *c, exec_T type, pipeinfo_T *pi, pid_t pgid)
    __attribute__((nonnull));
static pid_t fork_and_reset(pid_t pgid, bool fg);
static void search_command(
	const char *restrict name, const wchar_t *restrict wname,
	commandinfo_T *restrict ci)
    __attribute__((nonnull));
static bool do_assignments_for_command_type(
	const assign_T *asgns, enum cmdtype_T type);
static void exec_nonsimple_command(command_T *c, bool finally_exit)
    __attribute__((nonnull));
static void exec_simple_command(const commandinfo_T *ci,
	int argc, char *argv0, void **argv, bool finally_exit)
    __attribute__((nonnull));
static void print_xtrace(void *const *argv)
    __attribute__((nonnull));
static void exec_fall_back_on_sh(
	int argc, char *const *argv, char *const *env, const char *path)
    __attribute__((nonnull(2,4)));
static void exec_function_body(command_T *body, void **args, bool finally_exit)
    __attribute__((nonnull));
static inline int xexecv(const char *path, char *const *argv)
    __attribute__((nonnull(1)));
static inline int xexecve(
	const char *path, char *const *argv, char *const *envp)
    __attribute__((nonnull(1)));


/* exit status of the last command */
int laststatus = EXIT_SUCCESS;
/* the process ID of the last asynchronous list */
pid_t lastasyncpid;

/* This flag is set to true while the shell is executing the condition of an if-
 * statement, an and-or list, etc. to suppress the effect of "-o errexit" option. */
static bool supresserrexit;

/* state of currently executed loop */
struct execinfo {
    unsigned loopnest;    /* level of nested loops */
    unsigned breakcount;  /* # of loops to break (<= loopnest) */
    enum { ee_none, ee_continue, ee_return
    } exception;          /* exceptional jump to be done */
} execinfo;
/* Note that n continues are equivalent to (n-1) breaks followed by one continue.
 * When `exception' is set to `ee_return', `breakcount' must be 0. */
/* Note that `execinfo' is not reset when a subshell forks. */
#define EXECINFO_INIT { 0, 0, ee_none }  /* used to initialize `execinfo' */

/* Returns true iff we're breaking/continuing/returning now. */
bool need_break(void)
{
    return execinfo.breakcount > 0 || execinfo.exception != ee_none;
}

/* Saves the current `execinfo' and returns it.
 * `execinfo' is re-initialized. */
struct execinfo *save_execinfo(void)
{
    struct execinfo *save = xmalloc(sizeof execinfo);
    *save = execinfo;
    execinfo = (struct execinfo) EXECINFO_INIT;
    return save;
}

/* Restores `execinfo' and frees the saved info. */
void load_execinfo(struct execinfo *save)
{
    execinfo = *save;
    free(save);
}


/* Executes and-or lists.
 * If `finally_exit' is true, the shell exits after execution. */
void exec_and_or_lists(const and_or_T *a, bool finally_exit)
{
    while (a && !need_break()) {
	if (!a->ao_async)
	    exec_pipelines(a->ao_pipelines, finally_exit && !a->next);
	else
	    exec_pipelines_async(a->ao_pipelines);

	a = a->next;
    }
    if (finally_exit)
	exit_shell();
}

/* Executes pipelines. */
void exec_pipelines(const pipeline_T *p, bool finally_exit)
{
    for (bool first = true; p && !need_break(); p = p->next, first = false) {
	if (!first && p->pl_cond == !!laststatus)
	    continue;

	bool savesee = supresserrexit;
	supresserrexit |= p->pl_neg || p->next;

	// TODO exec_pipelines: add a condition that no trap is set
	exec_commands(p->pl_commands,
	    (finally_exit && !p->next && !p->pl_neg) ? execself : execnormal,
	    p->pl_loop);
	if (p->pl_neg)
	    laststatus = !laststatus;

	supresserrexit = savesee;
    }
    if (finally_exit)
	exit_shell();
}

/* Executes pipelines asynchronously. */
void exec_pipelines_async(const pipeline_T *p)
{
    assert(!need_break());

    if (!p->next && !p->pl_neg) {
	exec_commands(p->pl_commands, execasync, p->pl_loop);
    } else {
	bool doingjobcontrol = doing_job_control_now;
	pid_t cpid = fork_and_reset(0, false);
	
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
	    job->j_loop = false;
	    job->j_pcount = 1;

	    set_active_job(job);
	    add_job(false);
	    laststatus = EXIT_SUCCESS;
	    lastasyncpid = cpid;
	} else if (cpid == 0) {
	    /* child process: execute the commands and then exit */
	    if (!doingjobcontrol)
		ignore_sigquit_and_sigint();
	    maybe_redirect_stdin_to_devnull();
	    exec_pipelines(p, true);
	    assert(false);
	}
    }
}

/* Executes an if command */
void exec_if(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_IF);

    for (const ifcommand_T *cmds = c->c_ifcmds; cmds; cmds = cmds->next) {
	if (need_break())
	    goto done;
	if (exec_condition(cmds->ic_condition)) {
	    exec_and_or_lists(cmds->ic_commands, finally_exit);
	    return;
	}
    }
    laststatus = 0;
done:
    if (finally_exit)
	exit_shell();
}

/* Executes the condition of an if/while/until command. */
bool exec_condition(const and_or_T *c)
{
    if (c) {
	bool savesee = supresserrexit;
	supresserrexit = true;
	exec_and_or_lists(c, false);
	supresserrexit = savesee;
	return laststatus == EXIT_SUCCESS;
    } else {
	return true;
    }
}

/* Executes a for command. */
void exec_for(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_FOR);
    assert(!need_break());
    execinfo.loopnest++;

    int count;
    void **words;

    if (c->c_forwords) {
	if (!expand_line(c->c_forwords, &count, &words)) {
	    laststatus = EXIT_EXPERROR;
	    goto finish;
	}
    } else {
	bool concat;
	words = get_variable("@", &concat);
	assert(words != NULL && !concat);
	count = plcount(words);
    }

#define CHECK_LOOP                                   \
    if (execinfo.breakcount > 0) {                   \
	execinfo.breakcount--;                       \
	goto done;                                   \
    } else if (execinfo.exception == ee_continue) {  \
	execinfo.exception = ee_none;                \
	continue;                                    \
    } else if (execinfo.exception != ee_none) {      \
	goto done;                                   \
    } else (void) 0

    int i;
    for (i = 0; i < count; i++) {
	if (!set_variable(c->c_forname, words[i], !posixly_correct, false))
	    goto done;
	exec_and_or_lists(c->c_forcmds, false);
	CHECK_LOOP;
    }

done:
    while (++i < count)
	free(words[i]);
    free(words);
    if (count == 0)
	laststatus = EXIT_SUCCESS;
finish:
    if (finally_exit)
	exit_shell();
    execinfo.loopnest--;
}

/* Executes a while/until command. */
/* The exit status of the while/until command is that of `c_whlcmds' executed
 * last.  If `c_whlcmds' is not executed at all, the status is 0 regardless of
 * `c_whlcond'. */
void exec_while(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_WHILE);
    assert(!need_break());
    execinfo.loopnest++;

    int status = EXIT_SUCCESS;
    while (exec_condition(c->c_whlcond) == c->c_whltype) {
	CHECK_LOOP;
	exec_and_or_lists(c->c_whlcmds, false);
	status = laststatus;
	CHECK_LOOP;
    }

    laststatus = status;
done:
    if (finally_exit)
	exit_shell();
    execinfo.loopnest--;
}
#undef CHECK_LOOP

/* Executes a case command. */
void exec_case(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_CASE);
    assert(!need_break());

    wchar_t *word = expand_single(c->c_casword, tt_single);
    if (!word)
	goto fail;
    word = unescapefree(word);

    for (caseitem_T *ci = c->c_casitems; ci; ci = ci->next) {
	for (void **pats = ci->ci_patterns; *pats; pats++) {
	    wchar_t *pattern = expand_single(*pats, tt_single);
	    if (!pattern)
		goto fail;
	    size_t match = wfnmatch(pattern, word, 0, WFNM_WHOLE);
	    free(pattern);
	    if (match != WFNM_NOMATCH && match != WFNM_ERROR) {
		exec_and_or_lists(ci->ci_commands, finally_exit);
		goto done;
	    }
	}
    }
    laststatus = EXIT_SUCCESS;
done:
    if (finally_exit)
	exit_shell();
    free(word);
    return;

fail:
    laststatus = EXIT_EXPERROR;
    goto done;
}

/* Updates the contents of a `pipeinfo_T' to proceed to the next process
 * execution. `pi->pi_fromprevfd' and `pi->pi_tonextfds[PIDX_OUT]' are closed,
 * `pi->pi_tonextfds[PIDX_IN]' is moved to `pi->pi_fromprevfd', and,
 * if `next' is true, a new pipe is opened in `pi->pi_tonextfds'.
 * if `next' is false, `pi->pi_loopoutfd' is moved to
 * `pi->pi_tonextfds[PIDX_OUT]' and `pi->pi_loopoutfd' and
 * `pi->pi_tonextfds[PIDX_IN]' are assigned -1.
 * Returns true iff successful. */
void next_pipe(pipeinfo_T *pi, bool next)
{
    if (pi->pi_fromprevfd >= 0)
	xclose(pi->pi_fromprevfd);
    if (pi->pi_tonextfds[PIDX_OUT] >= 0)
	xclose(pi->pi_tonextfds[PIDX_OUT]);
    pi->pi_fromprevfd = pi->pi_tonextfds[PIDX_IN];
    if (next) {
	/* If the file descriptors 0 and/or 1 are unused, we open a dummy pipe
	 * so that pipes really used have the file descriptors larger than 1.
	 * If we don't do this, the pipe's file descriptors are overridden when
	 * we connect the pipe to stdin/stdout later. */
	int dummy[2];
	bool usedummy = (fcntl(STDIN_FILENO,  F_GETFD) == -1 && errno == EBADF)
		     || (fcntl(STDOUT_FILENO, F_GETFD) == -1 && errno == EBADF);

	if (usedummy && pipe(dummy) < 0)
	    goto fail;
	if (pipe(pi->pi_tonextfds) < 0)
	    goto fail;
	if (usedummy) {
	    xclose(dummy[PIDX_IN]);
	    xclose(dummy[PIDX_OUT]);
	}
    } else {
	pi->pi_tonextfds[PIDX_IN] = -1;
	pi->pi_tonextfds[PIDX_OUT] = pi->pi_loopoutfd;
	pi->pi_loopoutfd = -1;
    }
    return;

fail:
    pi->pi_tonextfds[PIDX_IN] = pi->pi_tonextfds[PIDX_OUT] = -1;
    xerror(errno, Ngt("cannot open pipe"));
}

/* Connects the pipe(s) and closes the pipes left. */
void connect_pipes(pipeinfo_T *pi)
{
    if (pi->pi_fromprevfd >= 0) {
	xdup2(pi->pi_fromprevfd, STDIN_FILENO);
	xclose(pi->pi_fromprevfd);
    }
    if (pi->pi_tonextfds[PIDX_OUT] >= 0) {
	xdup2(pi->pi_tonextfds[PIDX_OUT], STDOUT_FILENO);
	xclose(pi->pi_tonextfds[PIDX_OUT]);
    }
    if (pi->pi_tonextfds[PIDX_IN] >= 0)
	xclose(pi->pi_tonextfds[PIDX_IN]);
    if (pi->pi_loopoutfd >= 0)
	xclose(pi->pi_loopoutfd);
}

/* Executes commands in a pipeline. */
void exec_commands(command_T *c, exec_T type, bool looppipe)
{
    size_t count;
    pid_t pgid;
    command_T *cc;
    job_T *job;
    process_T *ps;
    pipeinfo_T pinfo = PIPEINFO_INIT;
    commandtype_T lasttype;

    assert(!need_break());

    /* count the number of commands */
    count = 0;
    for (cc = c; cc; cc = cc->next)
	count++;
    assert(count > 0);

    if (looppipe) {  /* open a pipe to connect the both ends */
	int fds[2];
	if (pipe(fds) < 0) {
	    xerror(errno, Ngt("cannot open pipe"));
	} else {
	    pinfo.pi_tonextfds[PIDX_IN] = fds[PIDX_IN];
	    pinfo.pi_loopoutfd = fds[PIDX_OUT];
	}
    }

    job = xmalloc(sizeof *job + count * sizeof *job->j_procs);
    ps = job->j_procs;

    /* execute commands */
    pgid = 0;
    cc = c;
    for (size_t i = 0; i < count; i++) {
	pid_t pid;

	lasttype = cc->c_type;
	next_pipe(&pinfo, i < count - 1);
	pid = exec_process(cc,
		(type == execself && i < count - 1) ? execnormal : type,
		&pinfo,
		pgid);
	ps[i].pr_pid = pid;
	if (pid) {
	    ps[i].pr_status = JS_RUNNING;
	    ps[i].pr_statuscode = 0;
	} else {
	    ps[i].pr_status = JS_DONE;
	    ps[i].pr_statuscode = laststatus;
	}
	ps[i].pr_name = NULL;   /* name is given later */
	if (pgid == 0)
	    pgid = pid;
	cc = cc->next;
    }
    assert(cc == NULL);
    assert(type != execself); /* `exec_process' doesn't return for `execself' */
    assert(pinfo.pi_tonextfds[PIDX_IN] < 0);
    assert(pinfo.pi_loopoutfd < 0);
    if (pinfo.pi_fromprevfd >= 0)
	xclose(pinfo.pi_fromprevfd);           /* close leftover pipes */
    if (pinfo.pi_tonextfds[PIDX_OUT] >= 0)
	xclose(pinfo.pi_tonextfds[PIDX_OUT]);  /* close leftover pipes */

    if (pgid == 0) {  /* no more things to do if didn't fork */
	free(job);
	goto done;
    }

    set_active_job(job);
    job->j_pgid = doing_job_control_now ? pgid : 0;
    job->j_status = JS_RUNNING;
    job->j_statuschanged = true;
    job->j_loop = looppipe;
    job->j_pcount = count;
    if (type == execnormal) {   /* wait for job to finish */
	wait_for_job(ACTIVE_JOBNO, doing_job_control_now);
	laststatus = calc_status_of_job(job);
	if (job->j_status == JS_DONE) {
	    remove_job(ACTIVE_JOBNO);
	    goto finish;
	}
    } else {
	laststatus = EXIT_SUCCESS;
	lastasyncpid = ps[count - 1].pr_pid;
    }

    /* add the new job to the job list */
    cc = c;
    for (size_t i = 0; i < count; i++) {  /* name processes */
	ps[i].pr_name = command_to_wcs(cc);
	cc = cc->next;
    }
    add_job(type == execnormal /* TODO || shopt_curasync */);

finish:
    if (doing_job_control_now)
	make_myself_foreground();
done:
    handle_traps();
    if (shopt_errexit && !supresserrexit && laststatus != EXIT_SUCCESS
	    && lasttype == CT_SIMPLE)
	exit_shell();
}

/* Executes a command.
 * If job control is active, the child process's process group ID is set to
 * `pgid'. If `pgid' is 0, set to the child process's process ID.
 * If a child process forked successfully, its process ID is returned.
 * If the command was executed without forking, `laststatus' is set and
 * 0 is returned.
 * if `type' is `execself', this function never returns. */
pid_t exec_process(command_T *c, exec_T type, pipeinfo_T *pi, pid_t pgid)
{
    bool early_fork;   /* do early fork? */
    bool later_fork;   /* do later fork? */
    bool finally_exit; /* never return? */
    int argc;
    void **argv = NULL;
    char *argv0 = NULL;
    commandinfo_T cmdinfo = cmdinfo;

    current_lineno = c->c_lineno;

    /* early fork if `type' is `execasync' or there is a pipe. */
    early_fork = (type != execself) && (type == execasync
	|| pi->pi_fromprevfd >= 0 || pi->pi_tonextfds[PIDX_OUT] >= 0);
    if (early_fork) {
	bool doingjobcontrol = doing_job_control_now;
	pid_t cpid = fork_and_reset(pgid, type == execnormal);
	if (cpid)
	    return cpid;
	if (!doingjobcontrol && type == execasync)
	    ignore_sigquit_and_sigint();
    }

    switch (c->c_type) {
    case CT_SIMPLE:
	if (!expand_line(c->c_words, &argc, &argv))
	    goto exp_fail;
	if (argc == 0) {
	    later_fork = finally_exit = false;
	    argv0 = NULL;
	} else {
	    argv0 = malloc_wcstombs(argv[0]);
	    if (!argv0)
		argv0 = xstrdup("");
	    search_command(argv0, argv[0], &cmdinfo);

	    /* fork for an external command.
	     * don't fork for a built-in or a function */
	    later_fork = finally_exit = (cmdinfo.type == externalprogram);

	    /* if command isn't found and there's no redirection or assignment,
	     * we're done. */
	    if (cmdinfo.type == externalprogram && !cmdinfo.ci_path
		    && !c->c_redirs && !c->c_assigns) {
		xerror(0, Ngt("%s: no such command or function"), argv0);
		laststatus = EXIT_NOTFOUND;
		recfree(argv, free);
		free(argv0);
		goto done;
	    }
	}
	break;
    case CT_SUBSHELL:
	later_fork = finally_exit = true;
	break;
    default:
	later_fork = finally_exit = false;
	break;
    }
    /* `argc', `argv' and `cmdinfo' are used only for `CT_SIMPLE'. */

    if (early_fork || type == execself)
	later_fork = false, finally_exit = true;

    assert(!(early_fork && later_fork));  /* don't fork twice */
    assert(!(early_fork || later_fork) || finally_exit);  /* exit if fork */
    if (later_fork) {
	pid_t cpid = fork_and_reset(pgid, type == execnormal);
	if (cpid != 0) {
	    recfree(argv, free);
	    free(argv0);
	    return cpid;
	}
    }
    if (finally_exit) {
	if (c->c_type == CT_SIMPLE && argc > 0
		&& cmdinfo.type == externalprogram)
	    reset_all_signals();
	else
	    reset_signals();
    }

    /* connect pipes and close leftovers */
    connect_pipes(pi);

    /* open redirections */
    savefd_T *savefd;
    if (!open_redirections(c->c_redirs, finally_exit ? NULL : &savefd))
	goto redir_fail;
    
    /* redirect stdin to "/dev/null" if non-interactive and asynchronous */
    if (type == execasync && pi->pi_fromprevfd < 0)
	maybe_redirect_stdin_to_devnull();

    /* execute! */
    if (c->c_type == CT_SIMPLE) {
	if (argc == 0) {
	    if (do_assignments(c->c_assigns, false, shopt_allexport))
		laststatus = EXIT_SUCCESS;
	    else
		laststatus = EXIT_ASSGNERR;
	} else {
	    if (do_assignments_for_command_type(c->c_assigns, cmdinfo.type)) {
		exec_simple_command(&cmdinfo, argc, argv0, argv, finally_exit);
	    } else {
		laststatus = EXIT_ASSGNERR;
		if (!is_interactive && cmdinfo.type == specialbuiltin)
		    exit_shell();
	    }
	    clear_temporary_variables();
	}
	recfree(argv, free);
	free(argv0);
    } else {
	exec_nonsimple_command(c, finally_exit);
    }
    if (finally_exit)
	exit_shell();

    // TODO exec: exec_process: leave "exec" command's redirections open
#if 0
    if (c->c_type == CT_SIMPLE && argc > 0 && cmdinfo.type == specialbuiltin
	    && cmdinfo.ci_builtin == exec_builtin && laststatus == EXIT_SUCCESS)
	clear_savefd(savefd);
    else
#endif
	undo_redirections(savefd);
    return 0;

exp_fail:
    laststatus = EXIT_EXPERROR;
done:
    if (early_fork || type == execself)
	exit_shell();
    return 0;
redir_fail:
    laststatus = EXIT_REDIRERR;
    if (finally_exit)
	exit_shell();
    if (posixly_correct && !is_interactive && c->c_type == CT_SIMPLE &&
	    argc > 0 && cmdinfo.type == specialbuiltin)
	exit_shell();
    undo_redirections(savefd);
    if (c->c_type == CT_SIMPLE)
	recfree(argv, free), free(argv0);
    return 0;
}

/* Forks a subshell and does some settings.
 * If job control is active, the child process's process group ID is set to
 * `pgid'. If `pgid' is 0, set to the child process's process ID.
 * If `pgid' is negative or job control is inactive, the process group ID is not
 * changed.
 * If job control is active and `fg' is true, the child process becomes a
 * foreground process.
 * Returns the return value of `fork'. */
pid_t fork_and_reset(pid_t pgid, bool fg)
{
    fflush(NULL);

    pid_t cpid = fork();

    if (cpid < 0) {
	/* fork failure */
	xerror(errno, Ngt("fork: cannot make child process"));
    } else if (cpid > 0) {
	/* parent process */
	if (doing_job_control_now && pgid >= 0)
	    setpgid(cpid, pgid);
    } else {
	/* child process */
	if (doing_job_control_now && pgid >= 0) {
	    setpgid(0, pgid);
	    if (fg)
		make_myself_foreground();
	}
	forget_initial_pgrp();
	remove_all_jobs();
	clear_traps();
	clear_shellfds();
	// TODO fork_and_reset: fix_temporary_variables
	is_interactive_now = false;
    }
    return cpid;
}

/* Makes the process group foreground that this shell belongs to. */
void make_myself_foreground(void)
{
    assert(ttyfd >= 0);
    block_sigttou();
    tcsetpgrp(ttyfd, getpgrp());
    unblock_sigttou();
}

/* Searches and determines the command to execute.
 * The result is assigned to `*ci'.
 * If the command is not found, `ci->type' is `externalprogram' and
 * `ci->ci_path' is NULL.
 * `name' and `wname' must contain the same string value. */
void search_command(
	const char *restrict name, const wchar_t *restrict wname,
	commandinfo_T *restrict ci)
{
    if (wcschr(wname, L'/')) {
	ci->type = externalprogram;
	ci->ci_path = name;
	return;
    }

    /* search builtins and functions. */
    const builtin_T *bi = get_builtin(name);
    command_T *funcbody = get_function(name);
    if (bi && bi->type == BI_SPECIAL) {
	ci->type = specialbuiltin;
	ci->ci_builtin = bi->body;
	return;
    } else if (funcbody) {
	ci->type = function;
	ci->ci_function = funcbody;
	return;
    } else if (bi && bi->type == BI_SEMISPECIAL) {
	ci->type = semispecialbuiltin;
	ci->ci_builtin = bi->body;
	return;
    } else if (bi && !posixly_correct) {
	ci->type = regularbuiltin;
	ci->ci_builtin = bi->body;
	return;
    }

    ci->ci_path = get_command_path(name, false);
    if (bi && ci->ci_path) {
	assert(bi->type == BI_REGULAR);
	ci->type = regularbuiltin;
	ci->ci_builtin = bi->body;
    } else {
	ci->type = externalprogram;
    }
}

/* Calls `do_assignments' with appropriate arguments according to the specified
 * command type. */
bool do_assignments_for_command_type(const assign_T *asgns, enum cmdtype_T type)
{
    bool temporary;
    switch (type) {
	/* For external programs `temporary' should be true in theory, but
	 * actually we don't have to make the variables temporary because
	 * the process forks. */
	case externalprogram:
	case specialbuiltin:
	case function:
	    temporary = false;
	    break;
	case semispecialbuiltin:
	case regularbuiltin:
	    temporary = true;
	    break;
	default:
	    assert(false);
    }
    return do_assignments(asgns, temporary, true);
}

/* Executes a command whose type is not `CT_SIMPLE'.
 * The redirections for the command is not performed in this function. */
void exec_nonsimple_command(command_T *c, bool finally_exit)
{
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
	if (define_function(c->c_funcname, c->c_funcbody))
	    laststatus = EXIT_SUCCESS;
	else
	    laststatus = EXIT_ASSGNERR;
	if (finally_exit)
	    exit_shell();
	break;
    }
    comsfree(c);
}

/* Executes a simple command. */
/* `argv0' is a multibyte version of `argv[0]' */
void exec_simple_command(
	const commandinfo_T *ci, int argc, char *argv0, void **argv,
	bool finally_exit)
{
    assert(argv[argc] == NULL);

    print_xtrace(argv);
    switch (ci->type) {
    case externalprogram:
	assert(finally_exit);
	if (ci->ci_path == NULL) {
	    xerror(0, Ngt("%s: no such command or function"), argv0);
	    exit_shell_with_status(EXIT_NOTFOUND);
	}

	{
	    char *mbsargv[argc + 1];
	    mbsargv[0] = argv0;
	    for (int i = 1; i < argc; i++) {
		mbsargv[i] = malloc_wcstombs(argv[i]);
		if (!mbsargv[i])
		    mbsargv[i] = xstrdup("");
	    }
	    mbsargv[argc] = NULL;

	    xexecv(ci->ci_path, mbsargv);
	    if (errno != ENOEXEC) {
		if (errno == EACCES && is_directory(ci->ci_path))
		    errno = EISDIR;
		if (strcmp(mbsargv[0], ci->ci_path) == 0)
		    xerror(errno, Ngt("cannot execute `%s'"), argv0);
		else
		    xerror(errno, Ngt("cannot execute `%s' (%s)"),
			    argv0, ci->ci_path);
	    } else {
		exec_fall_back_on_sh(argc, mbsargv, environ, ci->ci_path);
	    }
	    for (int i = 1; i < argc; i++)
		free(mbsargv[i]);
	    exit_shell_with_status(EXIT_NOEXEC);
	}
	//break;
    case specialbuiltin:
    case semispecialbuiltin:
    case regularbuiltin:
	laststatus = ci->ci_builtin(argc, argv);
	break;
    case function:
	exec_function_body(ci->ci_function, argv, finally_exit);
	break;
    }
    if (finally_exit)
	exit_shell();
}

/* Prints a trace if "-o xtrace" option is on. */
void print_xtrace(void *const *argv)
{
    if (shopt_xtrace) {
	print_prompt(4);
	for (;;) {
	    fprintf(stderr, "%ls", (wchar_t *) *argv);
	    argv++;
	    if (!*argv)
		break;
	    fputc(' ', stderr);
	}
	fputc('\n', stderr);
    }
}

/* Executes a command script by `exec'ing a shell.
 * `path' is the full path to the script file.
 * Returns iff failed to `exec'. */
void exec_fall_back_on_sh(
	int argc, char *const *argv, char *const *envp, const char *path)
{
    static char *const null[] = { NULL, };

    assert(argv[argc] == NULL);
    if (!envp)
	envp = null;

    char *args[argc + 3];
    size_t index = 0;
    args[index++] = argv[0];
    args[index++] = (char *) "-";
    if (strcmp(path, "--") == 0)
	args[index++] = "./--";
    else
	args[index++] = (char *) path;
    for (int i = 1; i < argc; i++)
	args[index++] = argv[i];
    args[index++] = NULL;
#if HAVE_PROC_FILESYSTEM
    xexecve("/proc/self/exe", args, envp);
#elif defined _PATH_BSHELL
    xexecve(_PATH_BSHELL, args, envp);
#else
    const char *shpath = get_command_path("sh", false);
    if (shpath)
	xexecve(shpath, args, envp);
    else
	errno = ENOENT;
#endif
    xerror(errno, Ngt("cannot invoke new shell to execute `%s'"), argv[0]);
}

/* Executes the specified command as a function.
 * `args' are the arguments to the function, which are wide strings cast to
 * (void *). `args[0]' is the function name and is ignored.
 * `args[1]' is the first argument, `args[2]' the second, and so on. */
void exec_function_body(command_T *body, void **args, bool finally_exit)
{
    savefd_T *savefd;

    assert(args[0] != NULL);
    if (open_redirections(body->c_redirs, &savefd)) {
	open_new_environment();
	set_positional_parameters(args + 1);
	exec_nonsimple_command(body, finally_exit);
	if (execinfo.exception == ee_return) {
	    assert(execinfo.breakcount == 0);
	    execinfo.exception = ee_none;
	}
	close_current_environment();
    }
    undo_redirections(savefd);
}

/* Calls `execv' until it doesn't return EINTR. */
int xexecv(const char *path, char *const *argv)
{
    do
	execv(path, argv);
    while (errno == EINTR);
    return -1;
}

/* Calls `execve' until it doesn't return EINTR. */
int xexecve(const char *path, char *const *argv, char *const *envp)
{
    do
	execve(path, argv, envp);
    while (errno == EINTR);
    return -1;
}

/* Executes a command substitution and returns the string to substitute with.
 * This function block until the command finishes.
 * The returned string is newly malloced and has the trailing newlines removed.
 * NULL is returned on error. */
wchar_t *exec_command_substitution(const wchar_t *code)
{
    int pipefd[2];
    pid_t cpid;
    bool save_is_interactive_now = is_interactive_now;

    if (!code[0])
	return xwcsdup(L"");

    /* open a pipe to receive output from the command */
    if (pipe(pipefd) < 0) {
	xerror(errno, Ngt("cannot open pipe for command substitution"));
	return NULL;
    }

    cpid = fork_and_reset(-1, false);
    if (cpid < 0) {
	/* fork failure */
	xclose(pipefd[PIDX_IN]);
	xclose(pipefd[PIDX_OUT]);
	return NULL;
    } else if (cpid) {
	/* parent process */
	FILE *f;

	xclose(pipefd[PIDX_OUT]);
	f = fdopen(pipefd[PIDX_IN], "r");
	if (!f) {
	    xerror(errno, Ngt("cannot open pipe for command substitution"));
	    xclose(pipefd[PIDX_IN]);
	    return NULL;
	}

	/* make the child process the active job */
	job_T *job = xmalloc(sizeof *job + sizeof *job->j_procs);
	job->j_pgid = 0;
	job->j_status = JS_RUNNING;
	job->j_statuschanged = false;
	job->j_loop = false;
	job->j_pcount = 1;
	job->j_procs[0].pr_pid = cpid;
	job->j_procs[0].pr_status = JS_RUNNING;
	job->j_procs[0].pr_statuscode = 0;
	job->j_procs[0].pr_name = NULL;
	set_active_job(job);

	/* read output from the command */
	xwcsbuf_T buf;
	wb_init(&buf);
	set_nonblocking(pipefd[PIDX_IN]);
	block_sigchld();
	handle_sigchld();
	for (;;) {
	    wint_t c = fgetwc(f);
	    if (c == WEOF) {
		if (feof(f))
		    break;
		assert(ferror(f));
		switch (errno) {
		    case EINTR:
		    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
		    case EWOULDBLOCK:
#endif
			wait_for_input(pipefd[PIDX_IN], false);
			continue;
		}
		break;
	    } else {
		wb_wccat(&buf, c);
	    }
	}
	unblock_sigchld();
	fclose(f);

	/* wait for the child to finish */
	wait_for_job(ACTIVE_JOBNO, false);
	laststatus = calc_status_of_job(job);
	remove_job(ACTIVE_JOBNO);

	/* trim trailing newlines and return */
	while (buf.length > 0 && buf.contents[buf.length - 1] == L'\n')
	    wb_remove(&buf, buf.length - 1, 1);
	return wb_towcs(&buf);
    } else {
	/* child process */

	/* If the child is stopped by SIGTSTP, it can never be resumed and
	 * the shell will be stuck. So we make the child unstoppable
	 * by SIGTSTP. */
	if (save_is_interactive_now)
	    ignore_sigtstp();
	reset_signals();

	xclose(pipefd[PIDX_IN]);
	if (pipefd[PIDX_OUT] != STDOUT_FILENO) {  /* connect the pipe */
	    xdup2(pipefd[PIDX_OUT], STDOUT_FILENO);
	    xclose(pipefd[PIDX_OUT]);
	}

	exec_wcs(code, gt("command substitution"), true);
	assert(false);
    }
}

/* Opens a here-document whose contents is specified by the argument.
 * Returns a newly opened file descriptor if successful, or a negative number
 * on error. */
/* The contents of the here-document is passed through a pipe. */
int open_heredocument(const wordunit_T *contents)
{
    int pipefd[2];
    pid_t cpid;

    /* open a pipe to pass the contents to a process */
    if (pipe(pipefd) < 0) {
	xerror(errno, Ngt("cannot open pipe for here-document"));
	return -1;
    }

    /* if contents is empty, we're done */
    if (!contents)
	goto success;

    cpid = fork_and_reset(-1, false);
    if (cpid < 0) {
	/* fork failure */
	xerror(0, Ngt("cannot redirect to here-document"));
	xclose(pipefd[PIDX_IN]);
	xclose(pipefd[PIDX_OUT]);
	return -1;
    } else if (cpid > 0) {
	/* parent process */
success:
	xclose(pipefd[PIDX_OUT]);
	return pipefd[PIDX_IN];
    } else {
	/* child process */
	xclose(pipefd[PIDX_IN]);
	block_all_but_sigpipe();

	FILE *f = fdopen(pipefd[PIDX_OUT], "w");
	if (!f) {
	    xerror(errno, Ngt("cannot open pipe for here-document"));
	    exit(EXIT_ERROR);
	}

	wchar_t *s = expand_string(contents, true);
	if (!s)
	    exit(EXIT_EXPERROR);
	if (fputws(s, f) < 0) {
#ifndef NDEBUG
	    free(s);
#endif
	    xerror(errno, Ngt("cannot write here-document contents"));
	    exit(EXIT_ERROR);
	}
#ifndef NDEBUG
	free(s);
#endif
	exit(EXIT_SUCCESS);
    }
}


/* vim: set ts=8 sts=4 sw=4 noet: */
