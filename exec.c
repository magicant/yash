/* Yash: yet another shell */
/* exec.c: command execution */
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
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/times.h>
#include <unistd.h>
#include <wchar.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#if HAVE_PATHS_H
# include <paths.h>
#endif
#if YASH_ENABLE_ALIAS
# include "alias.h"
#endif
#include "builtin.h"
#include "exec.h"
#include "expand.h"
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
# include "lineedit/lineedit.h"
#endif


/* way of command execution */
typedef enum {
    execnormal,  /* normal execution */
    execasync,   /* asynchronous execution */
    execself,    /* execution in the shell's own process */
} exec_T;

/* info about file descriptors of pipes */
typedef struct pipeinfo_T {
    int pi_fromprevfd;   /* reading end of the pipe from the previous process */
    int pi_tonextfds[2]; /* both ends of the pipe to the next process */
    /* -1 is assigned to unused members. */
} pipeinfo_T;
#define PIPEINFO_INIT { -1, { -1, -1 }, }

/* values used to specify the way of command search. */
enum srchcmdtype_T {
    sct_external = 1 << 0,  /* search external commands */
    sct_builtin  = 1 << 1,  /* search builtins */
    sct_function = 1 << 2,  /* search functions */
    sct_defpath  = 1 << 3,  /* search the default PATH */
    sct_rbpath   = 1 << 4,  /* return path of regular builtin */
    sct_noslash  = 1 << 5,  /* don't check if name contains slash */
};

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

static void exec_commands(command_T *c, exec_T type);
static pid_t exec_process(
	command_T *restrict c, exec_T type, pipeinfo_T *restrict pi, pid_t pgid)
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
static void print_xtrace(void *const *argv);
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
static struct execinfo {
    unsigned loopnest;    /* level of nested loops */
    unsigned breakcount;  /* # of loops to break (<= loopnest) */
    enum { ee_none, ee_continue, ee_return
    } exception;          /* exceptional jump to be done */
} execinfo;
/* Note that n continues are equivalent to (n-1) breaks followed by one
 * continue. When `exception' is set to `ee_return', `breakcount' must be 0. */
/* Note that `execinfo' is not reset when a subshell forks. */
#define INIT_EXECINFO { 0, 0, ee_none }  /* used to initialize `execinfo' */

/* state of currently executed iteration. */
static struct iterinfo {
    bool iterating;
    enum { ie_none, ie_continue, ie_break } exception;
} iterinfo;

/* This flag is set when a special builtin is executed as such. */
bool special_builtin_executed;

/* This flag is set while the "exec" builtin is executed. */
static bool exec_builtin_executed = false;

/* the last assignment. */
static assign_T *last_assign;

/* a buffer for xtrace. */
static xwcsbuf_T xtrace_buffer;


/* Resets `execinfo' to the initial state. */
void reset_execinfo(void)
{
    execinfo = (struct execinfo) {
	.loopnest = 0,
	.breakcount = 0,
	.exception = ee_none,
    };
}

/* Saves the current `execinfo' and returns it.
 * `execinfo' is re-initialized. */
struct execinfo *save_execinfo(void)
{
    struct execinfo *save = xmalloc(sizeof execinfo);
    *save = execinfo;
    reset_execinfo();
    return save;
}

/* Restores `execinfo' to `save' and frees `save'. */
void load_execinfo(struct execinfo *save)
{
    execinfo = *save;
    free(save);
}

/* Returns true iff `parse_and_exec' should quit execution. */
inline bool return_pending(void)
{
    return execinfo.exception != ee_none || iterinfo.exception != ie_none;
}

/* Returns true iff we're breaking/continuing/returning now. */
bool need_break(void)
{
    return execinfo.breakcount > 0 || return_pending() || is_interrupted();
}


/* Executes the and-or lists.
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

/* Executes the pipelines. */
void exec_pipelines(const pipeline_T *p, bool finally_exit)
{
    for (bool first = true; p && !need_break(); p = p->next, first = false) {
	if (!first && p->pl_cond == !!laststatus)
	    continue;

	bool savesee = supresserrexit;
	supresserrexit |= p->pl_neg || p->next;

	bool self = finally_exit && !doing_job_control_now
	    && !p->next && !p->pl_neg && !any_trap_set;
	exec_commands(p->pl_commands, self ? execself : execnormal);
	if (p->pl_neg)
	    laststatus = !laststatus;

	supresserrexit = savesee;
    }
    if (finally_exit)
	exit_shell();
}

/* Executes the pipelines asynchronously. */
void exec_pipelines_async(const pipeline_T *p)
{
    if (!p->next && !p->pl_neg) {
	exec_commands(p->pl_commands, execasync);
    } else {
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
}

/* Executes the if command */
void exec_if(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_IF);

    for (const ifcommand_T *cmds = c->c_ifcmds; cmds; cmds = cmds->next) {
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
    if (c) {
	bool savesee = supresserrexit;
	supresserrexit = true;
	exec_and_or_lists(c, false);
	supresserrexit = savesee;
	return laststatus == Exit_SUCCESS;
    } else {
	return true;
    }
}

/* Executes the for command. */
void exec_for(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_FOR);
    execinfo.loopnest++;

    int count;
    void **words;

    if (c->c_forwords) {
	/* expand the words between "in" and "do" of the for command. */
	if (!expand_line(c->c_forwords, &count, &words)) {
	    laststatus = Exit_EXPERROR;
	    goto finish;
	}
    } else {
	/* no "in" keyword in the for command: use the positional parameters */
	struct get_variable v = get_variable(L"@");
	assert(v.type == GV_ARRAY && v.values != NULL);
	words = duparray(v.values, copyaswcs);
	count = (int) v.count;
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
    } else if (is_interrupted()) {                   \
	goto done;                                   \
    } else (void) 0

    int i;
    for (i = 0; i < count; i++) {
	if (!set_variable(c->c_forname, words[i],
		    posixly_correct ? SCOPE_GLOBAL : SCOPE_LOCAL, false))
	    goto done;
	exec_and_or_lists(c->c_forcmds, finally_exit && i + 1 == count);

	if (!c->c_forcmds)
	    handle_signals();
	CHECK_LOOP;
    }

done:
    while (++i < count)  /* free unused words */
	free(words[i]);
    free(words);
    if (count == 0 && c->c_forcmds)
	laststatus = Exit_SUCCESS;
finish:
    execinfo.loopnest--;
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
    execinfo.loopnest++;

    int status = Exit_SUCCESS;
    for (;;) {
	bool cond = exec_condition(c->c_whlcond);

	if (!c->c_whlcond)
	    handle_signals();
	CHECK_LOOP;

	if (cond != c->c_whltype)
	    break;
	if (c->c_whlcmds) {
	    exec_and_or_lists(c->c_whlcmds, false);
	    status = laststatus;
	}

	if (!c->c_whlcmds)
	    handle_signals();
	CHECK_LOOP;
    }

    laststatus = status;
done:
    execinfo.loopnest--;
    if (finally_exit)
	exit_shell();
}
#undef CHECK_LOOP

/* Executes the case command. */
void exec_case(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_CASE);

    wchar_t *word = expand_single(c->c_casword, tt_single);
    if (!word)
	goto fail;
    word = unescapefree(word);

    for (caseitem_T *ci = c->c_casitems; ci; ci = ci->next) {
	for (void **pats = ci->ci_patterns; *pats; pats++) {
	    wchar_t *pattern = expand_single(*pats, tt_single);
	    if (!pattern)
		goto fail;

	    xfnmatch_T *xfnm = xfnm_compile(
		    pattern, XFNM_HEADONLY | XFNM_TAILONLY);
	    free(pattern);
	    if (!xfnm)
		continue;

	    bool match = (xfnm_wmatch(xfnm, word).start != (size_t) -1);
	    xfnm_free(xfnm);
	    if (match) {
		if (ci->ci_commands) {
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
    if (finally_exit)
	exit_shell();
    free(word);
    return;

fail:
    laststatus = Exit_EXPERROR;
    goto done;
}

/* Updates the contents of the `pipeinfo_T' to proceed to the next process
 * execution. `pi->pi_fromprevfd' and `pi->pi_tonextfds[PIDX_OUT]' are closed,
 * `pi->pi_tonextfds[PIDX_IN]' is moved to `pi->pi_fromprevfd', and,
 * if `next' is true, a new pipe is opened in `pi->pi_tonextfds' or,
 * if `next' is false, `pi->pi_tonextfds' is assigned -1.
 * Returns true iff successful. */
void next_pipe(pipeinfo_T *pi, bool next)
{
    if (pi->pi_fromprevfd >= 0)
	xclose(pi->pi_fromprevfd);
    if (pi->pi_tonextfds[PIDX_OUT] >= 0)
	xclose(pi->pi_tonextfds[PIDX_OUT]);
    pi->pi_fromprevfd = pi->pi_tonextfds[PIDX_IN];
    if (next) {
	if (pipe(pi->pi_tonextfds) < 0)
	    goto fail;

	/* The pipe's FDs must not be 0 or 1, or they may be overridden by each
	 * other when we connect the pipe later. If they are 0 or 1, move them
	 * to bigger numbers. */
	int origin  = pi->pi_tonextfds[PIDX_IN];
	int origout = pi->pi_tonextfds[PIDX_OUT];
	if (origin < 2 || origout < 2) {
	    if (origin < 2)
		pi->pi_tonextfds[PIDX_IN] = dup(origin);
	    if (origout < 2)
		pi->pi_tonextfds[PIDX_OUT] = dup(origout);
	    if (origin < 2)
		xclose(origin);
	    if (origout < 2)
		xclose(origout);
	    if (pi->pi_tonextfds[PIDX_IN] < 0) {
		xclose(pi->pi_tonextfds[PIDX_OUT]);
		goto fail;
	    }
	    if (pi->pi_tonextfds[PIDX_OUT] < 0) {
		xclose(pi->pi_tonextfds[PIDX_IN]);
		goto fail;
	    }
	}
    } else {
	pi->pi_tonextfds[PIDX_IN] = pi->pi_tonextfds[PIDX_OUT] = -1;
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
    for (cc = c; cc; cc = cc->next)
	count++;
    assert(count > 0);

    job = xmalloc(sizeof *job + count * sizeof *job->j_procs);
    ps = job->j_procs;

    /* execute the commands */
    pgid = 0, cc = c, pp = ps;
    do {
	pid_t pid;

	lasttype = cc->c_type;
	next_pipe(&pinfo, cc->next != NULL);
	pid = exec_process(cc,
		(type == execself && cc->next != NULL) ? execnormal : type,
		&pinfo,
		pgid);
	pp->pr_pid = pid;
	if (pid) {
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
    assert(type != execself); /* `exec_process' doesn't return for `execself' */
    assert(pinfo.pi_tonextfds[PIDX_IN] < 0);
    assert(pinfo.pi_tonextfds[PIDX_OUT] < 0);
    if (pinfo.pi_fromprevfd >= 0)
	xclose(pinfo.pi_fromprevfd);           /* close leftover pipes */

    if (pgid == 0) {
	/* nothing more to do if we didn't fork */
	free(job);
    } else {
	job->j_pgid = doing_job_control_now ? pgid : 0;
	job->j_status = JS_RUNNING;
	job->j_statuschanged = true;
	job->j_nonotify = false;
	job->j_pcount = count;
	set_active_job(job);
	if (type == execnormal) {
	    wait_for_job(ACTIVE_JOBNO, doing_job_control_now, false, false);
	    if (doing_job_control_now)
		put_foreground(shell_pgid);
	    laststatus = calc_status_of_job(job);
	} else {
	    assert(type == execasync);
	    laststatus = Exit_SUCCESS;
	    lastasyncpid = ps[count - 1].pr_pid;
	}
	if (job->j_status != JS_DONE) {
	    for (cc = c, pp = ps; cc != NULL; cc = cc->next, pp++)
		pp->pr_name = command_to_wcs(cc, false);
	    add_job(type == execnormal || shopt_curasync);
	} else {
	    notify_signaled_job(ACTIVE_JOBNO);
	    remove_job(ACTIVE_JOBNO);
	}
    }

    handle_signals();
    if (shopt_errexit && !supresserrexit && laststatus != Exit_SUCCESS
	    && lasttype == CT_SIMPLE)
	exit_shell_with_status(laststatus);
}

/* Executes the command.
 * If job control is active, the child process's process group ID is set to
 * `pgid'. If `pgid' is 0, set to the child process's process ID.
 * If a child process forked successfully, its process ID is returned.
 * If the command was executed without forking, `laststatus' is set and
 * 0 is returned.
 * if `type' is `execself', this function never returns. */
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

    /* do early fork if `type' is `execasync', the command type is subshell,
     * or there is a pipe. */
    early_fork = (type != execself)
	&& (type == execasync || c->c_type == CT_SUBSHELL
		|| pi->pi_fromprevfd >= 0 || pi->pi_tonextfds[PIDX_OUT] >= 0);
    finally_exit = (type == execself);
    if (early_fork) {
	sigtype_T sigtype = (type == execasync) ? t_quitint : 0;
	cpid = fork_and_reset(pgid, type == execnormal, sigtype);
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
	    recfree(argv, free);
	    goto done;
	}
	if (argc == 0) {
	    argv0 = NULL;
	} else {
	    argv0 = malloc_wcstombs(argv[0]);
	    if (!argv0)
		argv0 = xstrdup("");
	}
    }
    /* `argc' and `argv' are used only for `CT_SIMPLE'. */

    lastcmdsubstatus = Exit_SUCCESS;

    /* connect pipes and close leftovers */
    connect_pipes(pi);

    /* open redirections */
    savefd_T *savefd = NULL;
    if (!open_redirections(c->c_redirs, finally_exit ? NULL : &savefd)) {
	/* On redirection error, the command is not executed. */
	laststatus = Exit_REDIRERR;
	if (posixly_correct && !is_interactive_now && c->c_type == CT_SIMPLE &&
		argc > 0 && is_special_builtin(argv0))
	    finally_exit = true;
	goto done2;
    }
    
    if (type == execasync && pi->pi_fromprevfd < 0)
	maybe_redirect_stdin_to_devnull();

    if (c->c_type != CT_SIMPLE) {
	exec_nonsimple_command(c, finally_exit);
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

    /* First, we check if the command is a special builtin or a function
     * and determine whether we have to open a temporary environment. */
    search_command(argv0, argv[0], &cmdinfo, sct_builtin | sct_function);
    special_builtin_executed = (cmdinfo.type == specialbuiltin);
    temp = c->c_assigns && assignment_is_temporary(cmdinfo.type);
    if (temp)
	open_new_environment(true);

    /* perform the assignments */
    if (!do_assignments(c->c_assigns, temp, true)) {
	/* On assignment error, the command is not executed. */
	print_xtrace(NULL);
	laststatus = Exit_ASSGNERR;
	if (posixly_correct && !is_interactive &&
		cmdinfo.type == specialbuiltin)
	    finally_exit = true;
	goto done3;
    }

    /* find command path */
    if (cmdinfo.type == externalprogram) {
	if (posixly_correct) {
	    search_command(argv0, argv[0], &cmdinfo,
		    sct_external | sct_builtin);
	} else {
	    if (wcschr(argv[0], L'/')) {
		cmdinfo.ci_path = argv0;
		if (!is_executable_regular(argv0))
		    if (command_not_found_handler(argv))
			goto done3;
	    } else {
		search_command(argv0, argv[0], &cmdinfo,
			sct_external | sct_builtin | sct_noslash);
		if (cmdinfo.type == externalprogram && cmdinfo.ci_path == NULL)
		    if (command_not_found_handler(argv))
			goto done3;
	    }
	    /* If the command was not found and the shell is not in the POSIXly
	     * correct mode, the not-found handler is called. */
	}
    }

    /* create a child process to execute the external command */
    if (cmdinfo.type == externalprogram && !finally_exit) {
	cpid = fork_and_reset(pgid, type == execnormal, t_leave);
	if (cpid != 0)
	    goto done3;
	finally_exit = true;
    }

    /* execute! */
    exec_simple_command(&cmdinfo, argc, argv0, argv, finally_exit);

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
	recfree(argv, free), free(argv0);
done1:
    undo_redirections(savefd);
done:
    if (finally_exit)
	exit_shell();
    if (cpid < 0) {
	laststatus = Exit_NOEXEC;
	cpid = 0;
    }
    return cpid;
}

/* Forks a subshell and does some settings.
 * If job control is active, the child process's process group ID is set to
 * `pgid'. If `pgid' is 0, set to the child process's process ID.
 * If `pgid' is negative or job control is inactive, the process group ID is not
 * changed.
 * If job control is active and `fg' is true, the child process becomes a
 * foreground process.
 * `sigtype' specifies the settings of signals in the child.
 * `sigtype' is a bitwise OR of the followings:
 *   t_quitint: SIGQUIT & SIGINT are ignored if the parent's job control is off
 *   t_tstp: SIGTSTP is ignored if the parent is job-controlling
 *   t_leave: Don't clear traps and shellfds. Restore the signal mask for
 *          SIGCHLD. This option must be used iff the shell is going to `exec'
 *          to an extenal program.
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
	    xerror(errno, Ngt("fork: cannot make child process"));
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
	restore_signals(sigtype & t_leave);  /* signal mask is restored here */
	clear_shellfds(sigtype & t_leave);
	if (!(sigtype & t_leave)) {
	    clear_traps();
	    neglect_all_jobs();
	}
	is_interactive_now = false;
    }
    return cpid;
}

/* Searches and determines the command to execute.
 * The result is assigned to `*ci'.
 * If the command is not found, `ci->type' is set to `externalprogram' and
 * `ci->ci_path' is set to NULL.
 * `name' and `wname' must contain the same string value. */
/* When the result type `ci->type' is `regularbuiltin', the result value is
 * either a function pointer to the builtin (if the `sct_rbpath' flag is not
 * specified in `type') or the path of the corresponding external command
 * (otherwise). */
/* In the POSIXly correct mode, you need the sct_external flag to find a regular
 * builtin. */
void search_command(
	const char *restrict name, const wchar_t *restrict wname,
	commandinfo_T *restrict ci, enum srchcmdtype_T type)
{
    const builtin_T *bi;
    command_T *funcbody;

    if (!(type & sct_noslash) && wcschr(wname, L'/')) {
	if (!(type & sct_external))
	    goto notfound;
	ci->type = externalprogram;
	ci->ci_path = name;
	return;
    }

    /* search builtins and functions. */
    bi = (type & sct_builtin) ? get_builtin(name) : NULL;
    if (bi && bi->type == BI_SPECIAL) {
	ci->type = specialbuiltin;
	ci->ci_builtin = bi->body;
	return;
    }
    funcbody = (type & sct_function) ? get_function(wname) : NULL;
    if (funcbody) {
	ci->type = function;
	ci->ci_function = funcbody;
	return;
    } else if (bi && bi->type == BI_SEMISPECIAL) {
	ci->type = semispecialbuiltin;
	ci->ci_builtin = bi->body;
	return;
    } else if (bi && !posixly_correct && !(type & sct_rbpath)) {
	ci->type = regularbuiltin;
	ci->ci_builtin = bi->body;
	return;
    }

    if (!(type & (sct_external | sct_rbpath)))
	goto notfound;

    ci->ci_path = (type & sct_defpath)
	? get_command_path_default(name) : get_command_path(name, false);
    if (bi && (ci->ci_path || !posixly_correct)) {
	assert(bi->type == BI_REGULAR);
	ci->type = regularbuiltin;
	if (!(type & sct_rbpath))
	    ci->ci_builtin = bi->body;
    } else {
	ci->type = externalprogram;
	if (!(type & sct_external))
	    ci->ci_path = NULL;
    }
    return;

notfound:
    ci->type = externalprogram;
    ci->ci_path = NULL;
    return;
}

/* Returns true iff the specified command is a special builtin. */
bool is_special_builtin(const char *cmdname)
{
    const builtin_T *bi = get_builtin(cmdname);
    return bi && bi->type == BI_SPECIAL;
}

/* Determines whether the assignments should be temporary according to `type'.*/
bool assignment_is_temporary(enum cmdtype_T type)
{
    switch (type) {
	case specialbuiltin:
	case function:
	    return false;
	case semispecialbuiltin:
	case regularbuiltin:
	case externalprogram:
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
	if (define_function(c->c_funcname, c->c_funcbody))
	    laststatus = Exit_SUCCESS;
	else
	    laststatus = Exit_ASSGNERR;
	if (finally_exit)
	    exit_shell();
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

    print_xtrace(argv);
    switch (ci->type) {
    case externalprogram:
	if (ci->ci_path == NULL) {
	    xerror(0, Ngt("%s: no such command or function"), argv0);
	    laststatus = Exit_NOTFOUND;
	} else {
	    assert(finally_exit);

	    char *mbsargv[argc + 1];
	    mbsargv[0] = argv0;
	    for (int i = 1; i < argc; i++) {
		mbsargv[i] = malloc_wcstombs(argv[i]);
		if (!mbsargv[i])
		    mbsargv[i] = xstrdup("");
	    }
	    mbsargv[argc] = NULL;

	    restore_signals(true);
	    xexecv(ci->ci_path, mbsargv);
	    int errno_ = errno;
	    if (errno_ != ENOEXEC) {
		if (errno_ == EACCES && is_directory(ci->ci_path))
		    errno_ = EISDIR;
		if (strcmp(mbsargv[0], ci->ci_path) == 0)
		    xerror(errno_, Ngt("cannot execute `%s'"), argv0);
		else
		    xerror(errno_, Ngt("cannot execute `%s' (%s)"),
			    argv0, ci->ci_path);
	    } else if (errno_ != ENOENT) {
		exec_fall_back_on_sh(argc, mbsargv, environ, ci->ci_path);
	    }
	    for (int i = 1; i < argc; i++)
		free(mbsargv[i]);
	    laststatus = (errno_ == ENOENT) ? Exit_NOTFOUND : Exit_NOEXEC;
	    set_signals();
	}
	break;
    case specialbuiltin:
    case semispecialbuiltin:
    case regularbuiltin:
	yash_error_message_count = 0;
	current_builtin_name = argv[0];  // XXX not reentrant
	laststatus = ci->ci_builtin(argc, argv);
	current_builtin_name = NULL;
	break;
    case function:
	exec_function_body(ci->ci_function, argv, finally_exit);
	break;
    }
    if (finally_exit)
	exit_shell();
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
	    && le_state != LE_STATE_ACTIVE
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
    if (xtrace_buffer.contents) {
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
    static char *const null[] = { NULL, };

    assert(argv[argc] == NULL);
    if (!envp)
	envp = null;

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
    if (shpath)
	xexecve(shpath, args, envp);
    else
	errno = ENOENT;
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
	open_new_environment(false);
	set_positional_parameters(args + 1);
	exec_nonsimple_command(body, finally_exit);
	if (execinfo.exception == ee_return)
	    execinfo.exception = ee_none;
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
	    : cmdsub->value.unparsed[0] == L'\0')  /* command is empty? */
	return xwcsdup(L"");

    /* open a pipe to receive output from the command */
    if (pipe(pipefd) < 0) {
	xerror(errno, Ngt("cannot open pipe for command substitution"));
	return NULL;
    }

    /* If the child is stopped by SIGTSTP, it can never be resumed and
     * the shell will be stuck. So we make the child unstoppable by SIGTSTP.
     * The `t_tstp' flag designates this behavior. */
    cpid = fork_and_reset(-1, false, t_tstp);
    if (cpid < 0) {
	/* fork failure */
	xclose(pipefd[PIDX_IN]);
	xclose(pipefd[PIDX_OUT]);
	lastcmdsubstatus = Exit_NOEXEC;
	return NULL;
    } else if (cpid) {
	/* parent process */
	FILE *f;

	xclose(pipefd[PIDX_OUT]);
	f = fdopen(pipefd[PIDX_IN], "r");
	if (!f) {
	    xerror(errno, Ngt("cannot open pipe for command substitution"));
	    xclose(pipefd[PIDX_IN]);
	    lastcmdsubstatus = Exit_NOEXEC;
	    return NULL;
	}

	/* read output from the command */
	xwcsbuf_T buf;
	wb_init(&buf);
	for (;;) {
	    wint_t c = fgetwc(f);
	    if (c == WEOF) {
		if (feof(f) || errno != EINTR)
		    break;
	    } else {
		wb_wccat(&buf, c);
	    }
	}
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
	xclose(pipefd[PIDX_IN]);
	if (pipefd[PIDX_OUT] != STDOUT_FILENO) {  /* connect the pipe */
	    if (xdup2(pipefd[PIDX_OUT], STDOUT_FILENO) < 0)
		exit(Exit_NOEXEC);
	    xclose(pipefd[PIDX_OUT]);
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
    struct iterinfo saveiterinfo = iterinfo;

    for (void *const *c = commands; *c; c++) {
	iterinfo.iterating = true;
	iterinfo.exception = ie_none;
	exec_wcs(*c, codename, false);
	commandstatus = laststatus;
	laststatus = savelaststatus;
	if (iterinfo.exception == ie_break)
	    break;
    }
    iterinfo = saveiterinfo;
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
    struct get_variable gv = get_variable(varname);
    void **array;

    switch (gv.type) {
	case GV_NOTFOUND:
	    return -1;
	case GV_SCALAR:
	    array = gv.values;
	    break;
	case GV_ARRAY:
	    /* copy the array values in case they are unset during execution */
	    array = duparrayn(gv.values, gv.count, copyaswcs);
	    break;
	case GV_ARRAY_CONCAT:
	    /* should execute the concatenated value, but is not supported now*/
	    return -1;
	default:
	    assert(false);
    }

    int result = exec_iteration(array, codename);
    recfree(array, free);
    return result;
}


/********** Builtins **********/

static int exec_builtin_2(int argc, void **argv, const wchar_t *as, bool clear)
    __attribute__((nonnull(2)));
static int command_builtin_execute(
	int argc, void **argv, enum srchcmdtype_T type)
    __attribute__((nonnull));
static bool print_command_info(const wchar_t *commandname,
	enum srchcmdtype_T type, bool keywordalias, bool humanfriendly)
    __attribute__((nonnull));
static void print_command_absolute_path(
	const char *name, const char *path, bool humanfriendly)
    __attribute__((nonnull));

/* Options for the "break", "continue" and "eval" builtins. */
static const struct xoption iter_options[] = {
    { L"iteration", xno_argument, L'i', },
#if YASH_ENABLE_HELP
    { L"help",      xno_argument, L'-', },
#endif
    { NULL, 0, 0, },
};

/* The "return" builtin */
int return_builtin(int argc, void **argv)
{
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"", help_option, NULL))) {
	switch (opt) {
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:  print_usage:
		fprintf(stderr, gt("Usage:  %ls [n]\n"), ARGV(0));
		SPECIAL_BI_ERROR;
		return Exit_ERROR;
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
    execinfo.exception = ee_return;
    return status;
}

#if YASH_ENABLE_HELP
const char return_help[] = Ngt(
"return - return from function\n"
"\treturn [n]\n"
"Exits the currently executing function or script file with the exit status\n"
"of <n>. If <n> is not specified, it defaults to the exit status of the last\n"
"executed command. <n> should be between 0 and 255 inclusive.\n"
);
#endif

/* The "break"/"continue" builtin, which accepts the following option:
 *  -i: iterative execution */
int break_builtin(int argc, void **argv)
{
    wchar_t opt;
    bool iter = false;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"i", iter_options, NULL))) {
	switch (opt) {
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
	if (!iterinfo.iterating) {
	    xerror(0, Ngt("not in iteration"));
	    return Exit_ERROR;
	}
	if (wcscmp(ARGV(0), L"break") == 0) {
	    iterinfo.exception = ie_break;
	} else {
	    assert(wcscmp(ARGV(0), L"continue") == 0);
	    iterinfo.exception = ie_continue;
	}
	execinfo.breakcount = execinfo.loopnest;
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
		xerror(0, Ngt("%u: not a positive integer"), 0u);
		SPECIAL_BI_ERROR;
		return Exit_ERROR;
	    } else if (countl > UINT_MAX) {
		count = UINT_MAX;
	    } else {
		count = (unsigned) countl;
	    }
	}
	assert(count > 0);
	if (execinfo.loopnest == 0) {
	    xerror(0, Ngt("not in loop"));
	    SPECIAL_BI_ERROR;
	    return Exit_ERROR;
	}
	if (count > execinfo.loopnest)
	    count = execinfo.loopnest;
	if (wcscmp(ARGV(0), L"break") == 0) {
	    execinfo.breakcount = count;
	} else {
	    assert(wcscmp(ARGV(0), L"continue") == 0);
	    execinfo.breakcount = count - 1;
	    execinfo.exception = ee_continue;
	}
	return Exit_SUCCESS;
    }
}

#if YASH_ENABLE_HELP

const char break_help[] = Ngt(
"break - exit loop\n"
"\tbreak [n]\n"
"\tbreak -i\n"
"The first form exits the currently executing for/while/until loop.\n"
"If <n> is specified, exits the <n>th outer loop.\n"
"The second form, with the -i (--iteration) option, exits the current\n"
"iterative execution.\n"
);

const char continue_help[] = Ngt(
"continue - continue loop\n"
"\tcontinue [n]\n"
"\tcontinue -i\n"
"The first form ends the current iteration of a for/while/until loop and\n"
"resumes the next iteration. If <n> is specified, ends the iteration of the\n"
"<n>th outer loop.\n"
"The second form, with the -i (--iteration) option, ends the execution of\n"
"commands and resumes the next iteration of the current iterative execution.\n"
);

#endif /* YASH_ENABLE_HELP */

/* The "eval" builtin, which accepts the following option:
 *  -i: iterative execution */
int eval_builtin(int argc __attribute__((unused)), void **argv)
{
    bool iter = false;
    wchar_t opt;
    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv,
		    posixly_correct ? L"+" : L"+i", iter_options, NULL))) {
	switch (opt) {
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
	return exec_iteration(argv + xoptind, "eval");
    } else {
	wchar_t *args = joinwcsarray(argv + xoptind, L" ");
	exec_wcs(args, "eval", false);
	free(args);
	return laststatus;
    }
}

#if YASH_ENABLE_HELP
const char eval_help[] = Ngt(
"eval - evaluate arguments as command\n"
"\teval [-i] [arg...]\n"
"Parses and executes the specified <arg>s as commands in the current shell\n"
"environment. Without the -i (--iteration) option, all the <arg>s are joined\n"
"with a space inserted between each <arg> and the whole resultant string is\n"
"parsed at a time. With the -i option, <arg>s are parsed and executed one by\n"
"one (iterative execution).\n"
"In POSIXly correct mode, the -i option cannot be used.\n"
);
#endif

/* The "." builtin, which accepts the following option:
 *  -A: disable aliases */
int dot_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"no-alias", xno_argument, L'A', },
#if YASH_ENABLE_HELP
	{ L"help",     xno_argument, L'-', },
#endif
	{ NULL, 0, 0, },
    };

    bool enable_alias = true;

    wchar_t opt;
    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv,
		    posixly_correct ? L"+" : L"+A",
		    long_options, NULL))) {
	switch (opt) {
	    case L'A':
		enable_alias = false;
		break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:  print_usage:
		fprintf(stderr, gt(posixly_correct
			    ? Ngt("Usage:  . file\n")
			    : Ngt("Usage:  . [-A] file [arg...]\n")));
		SPECIAL_BI_ERROR;
		return Exit_ERROR;
	}
    }

    const wchar_t *filename = ARGV(xoptind++);
    if (!filename)
	goto print_usage;

    bool has_args = xoptind < argc;
    if (has_args && posixly_correct)
	goto print_usage;

    char *mbsfilename = malloc_wcstombs(filename);
    if (!mbsfilename) {
	xerror(EILSEQ, Ngt("unexpected error"));
	return Exit_ERROR;
    }

    char *path;
    if (!wcschr(filename, L'/')) {
	path = which(mbsfilename, get_path_array(PA_PATH), is_readable_regular);
	if (!path) {
	    if (!posixly_correct) {
		path = mbsfilename;
	    } else {
		xerror(0, Ngt("%s: not found in $PATH"), mbsfilename);
		free(mbsfilename);
		if (!is_interactive)
		    exit_shell_with_status(Exit_FAILURE);
		return Exit_FAILURE;
	    }
	}
    } else {
	path = mbsfilename;
    }

    if (has_args) {
	open_new_environment(false);
	set_positional_parameters(argv + xoptind);
    }

    FILE *f = fopen(path, "r");
    if (path != mbsfilename)
	free(path);
    f = reopen_with_shellfd(f, "r", true);
    if (!f) {
	xerror(errno, Ngt("cannot open `%s'"), mbsfilename);
	free(mbsfilename);
	if (!is_interactive)
	    exit_shell_with_status(Exit_FAILURE);
	return Exit_FAILURE;
    }

    exec_input(f, mbsfilename, false, enable_alias, false);
    fclose(f);
    free(mbsfilename);

    if (has_args) {
	close_current_environment();
    }

    return laststatus;
}

#if YASH_ENABLE_HELP
const char dot_help[] = Ngt(
"dot - read file and execute commands\n"
"\t. [-A] file [arg...]\n"
"Reads the specified <file> and executes commands in it.\n"
"If <arg>s are specified, they are used as the positional parameters.\n"
"Otherwise, the positional parameters are not changed.\n"
"If <file> does not contain any slashes, the shell searches $PATH for a\n"
"readable shell script file whose name is <file>. To ensure that the file in\n"
"the current working directory is used, start <file> with \"./\".\n"
"If the -A (--no-alias) option is specified, alias substitution is not\n"
"performed during processing the file.\n"
"In POSIXly correct mode, the -A option cannot be used and <arg>s must not be\n"
"given.\n"
);
#endif

/* The "exec" builtin, which accepts the following options:
 *  -a name: give <name> as argv[0] to the command
 *  -c: don't pass environment variables to the command
 *  -f: suppress error when we have stopped jobs */
int exec_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"as",    xrequired_argument, L'a', },
	{ L"clear", xno_argument,       L'c', },
	{ L"force", xno_argument,       L'f', },
#if YASH_ENABLE_HELP
	{ L"help",  xno_argument,       L'-', },
#endif
	{ NULL, 0, 0, },
    };

    wchar_t opt;
    const wchar_t *as = NULL;
    bool clear = false, force = false;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv,
		    posixly_correct ? L"+" : L"+a:cf",
		    long_options, NULL))) {
	switch (opt) {
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
		    ngt("You have %zu stopped job(s)!",
			"You have a stopped job!",
			"You have %zu stopped jobs!",
			sjc),
		    sjc);
	    fprintf(stderr, gt("  Use `-f' option to exec anyway.\n"));
	    return Exit_FAILURE;
	}
    }

    return exec_builtin_2(argc - xoptind, argv + xoptind, as, clear);
}

/* Implements the "exec" builtin.
 * argc, argv: the operands (not arguments) of the "exec" builtin.
 * as: value of the -a option or NULL
 * clear: true iff the -c option is specified */
int exec_builtin_2(int argc, void **argv, const wchar_t *as, bool clear)
{
    int err;
    char *freelater = NULL;
    char *args[argc + 1];
    for (int i = 0; i < argc; i++) {
	args[i] = malloc_wcstombs(ARGV(i));
	if (!args[i]) {
	    xerror(EILSEQ, Ngt("cannot convert wide characters into "
			"multibyte characters: replaced with empty string"));
	    args[i] = xstrdup("");
	}
    }
    args[argc] = NULL;

    const char *commandpath;
    if (wcschr(ARGV(0), L'/')) {
	commandpath = args[0];
    } else {
	commandpath = get_command_path(args[0], false);
	if (!commandpath) {
	    xerror(0, Ngt("%s: no such command"), args[0]);
	    err = Exit_NOTFOUND;
	    goto err;
	}
    }

    char **envs;
    if (clear) {
	/* use the environment that contains only the variables assigned by the
	 * assignment for this `exec' builtin. */
	plist_T list;
	
	pl_init(&list);
	for (assign_T *assign = last_assign; assign; assign = assign->next) {
	    const wchar_t *val = getvar(assign->a_name);
	    pl_add(&list, malloc_printf(
			"%ls=%ls", assign->a_name, val ? val : L""));
	}
	envs = (char **) pl_toary(&list);
    } else {
	envs = environ;
    }

    if (as) {
	freelater = args[0];
	args[0] = malloc_wcstombs(as);
	if (!args[0]) {
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
	    xerror(errno, Ngt("cannot execute `%s'"), args[0]);
	else
	    xerror(errno, Ngt("cannot execute `%s' (%s)"),
		    args[0], commandpath);
    } else {
	exec_fall_back_on_sh(argc, args, envs, commandpath);
    }
    if (posixly_correct || !is_interactive_now)
	exit(Exit_NOEXEC);
    set_signals();
    err = Exit_NOEXEC;

    if (envs != environ)
	recfree((void **) envs, free);

err:
    for (int i = 0; i < argc; i++)
	free(args[i]);
    free(freelater);
    if (posixly_correct || !is_interactive_now)
	exit(err);
    return err;
}

#if YASH_ENABLE_HELP
const char exec_help[] = Ngt(
"exec - execute command in the shell process\n"
"\texec [-cf] [-a name] [command [args...]]\n"
"Replaces the shell process with the specified command. The shell process is\n"
"`changed' into the new command's process. No child process is created.\n"
"When an interactive shell has stopped jobs, the -f (--force) option is\n"
"required to really do exec.\n"
"If the -c (--clear) option is specified, the command is executed only with\n"
"the environment variables assigned for this command.\n"
"If the -a <name> (--as=<name>) option is specified, <name> is passed to the\n"
"command instead of <command> as the zeroth argument.\n"
"If no <command> is given, the shell does nothing. As a special result,\n"
"the effects of redirections associated with the \"exec\" command remain\n"
"after the command.\n"
"In the POSIXly correct mode, none of these options are available and the -f\n"
"option is always assumed.\n"
);
#endif

/* The "command"/"type" builtin, which accepts the following options:
 *  -b: search builtins
 *  -B: search external commands
 *  -p: use the default path to find the command
 *  -v: print info about the command
 *  -V: print info about the command in a human-friendly format */
int command_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"builtin-only",     xno_argument, L'b', },
	{ L"external-only",    xno_argument, L'B', },
	{ L"standard-path",    xno_argument, L'p', },
	{ L"identify",         xno_argument, L'v', },
	{ L"verbose-identify", xno_argument, L'V', },
#if YASH_ENABLE_HELP
	{ L"help",             xno_argument, L'-', },
#endif
	{ NULL, 0, 0, },
    };

    bool argv0istype = wcscmp(ARGV(0), L"type") == 0;
    bool printinfo = argv0istype, humanfriendly = argv0istype;
    bool builtin = false, external = false, defpath = false;

    wchar_t opt;
    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv,
		    posixly_correct ? L"+pvV" : L"+bBpvV",
		    long_options, NULL))) {
	switch (opt) {
	    case L'b':  builtin   = true;  break;
	    case L'B':  external  = true;  break;
	    case L'p':  defpath   = true;  break;
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

    if (xoptind == argc) {
	if (posixly_correct)
	    goto print_usage;
	else
	    return Exit_SUCCESS;
    } else if (posixly_correct && (builtin || external)) {
	goto print_usage;
    }

    enum srchcmdtype_T type = 0;
    if (builtin)
	type |= sct_builtin;
    if (external)
	type |= sct_external;
    if (!builtin && !external)
	type |= sct_builtin | sct_external;
    if (defpath)
	type |= sct_defpath;

    if (!printinfo) {
	return command_builtin_execute(
		argc - xoptind, argv + xoptind, type);
    } else {
	bool keywordalias = false;
	if (!builtin && !external)
	    type |= sct_function, keywordalias = true;

	bool ok = true;
	clearerr(stdout);
	for (int i = xoptind; i < argc; i++) {
	    ok &= print_command_info(
		    ARGV(i), type, keywordalias, humanfriendly);
	    if (ferror(stdout)) {
		xerror(0, Ngt("cannot print to standard output"));
		return Exit_FAILURE;
	    }
	}
	return ok ? Exit_SUCCESS : Exit_FAILURE;
    }

print_usage:
    if (argv0istype)
	fprintf(stderr, gt("Usage:  type command...\n"));
    else if (posixly_correct)
	fprintf(stderr, gt("Usage:  command [-p] command [arg...]\n"
			   "        command -v|-V [-p] command...\n"));
    else
	fprintf(stderr, gt("Usage:  command [-bBp] command [arg...]\n"
			   "        command -v|-V [-bBp] command...\n"));
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

    if (!argv0)
	argv0 = xstrdup("");
    search_command(argv0, argv[0], &ci, type);
    if (ci.type == externalprogram) {
	pid_t cpid = fork_and_reset(0, true, t_leave);
	if (cpid < 0) {
	    free(argv0);
	    return Exit_NOEXEC;
	} else if (cpid > 0) {
	    wchar_t **namep = wait_for_child(
		    cpid,
		    doing_job_control_now ? cpid : 0,
		    doing_job_control_now);
	    if (namep)
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
 * If the command is not found, returns false.
 * The shell keywords and aliases are printed only when `keywordalias' is true.
 */
bool print_command_info(
	const wchar_t *commandname, enum srchcmdtype_T type,
	bool keywordalias, bool humanfriendly)
{
    bool found;

    if (keywordalias && is_keyword(commandname)) {
	printf(humanfriendly ? gt("%ls: shell keyword\n") : "%ls\n",
		commandname);
	return true;
    }
#if YASH_ENABLE_ALIAS
    found = keywordalias && print_alias_if_defined(commandname, humanfriendly);
    if (found && !humanfriendly)
	return true;
#else
    found = false;
#endif

    char *name = malloc_wcstombs(commandname);
    if (!name)
	return false;

    commandinfo_T ci;
    search_command(name, commandname, &ci, type | sct_rbpath);
    switch (ci.type) {
	case externalprogram:
	    if (ci.ci_path && is_executable_regular(ci.ci_path)) {
		print_command_absolute_path(name, ci.ci_path, humanfriendly);
		goto ok;
	    } else if (found) {
		goto ok;
	    } else {
		if (humanfriendly)
		    xerror(0, gt("%s: no such command or function"), name);
		goto error;
	    }
	case specialbuiltin:
	    if (humanfriendly)
		printf(gt("%s: special builtin\n"), name);
	    else
		puts(name);
	    goto ok;
	case semispecialbuiltin:
	    if (humanfriendly)
		printf(gt("%s: semi-special builtin\n"), name);
	    else
		puts(name);
	    goto ok;
	case regularbuiltin:
	    if (ci.ci_path) {
		if (humanfriendly)
		    printf(gt("%s: regular builtin at %s\n"), name, ci.ci_path);
		else
		    puts(ci.ci_path);
	    } else {
		if (humanfriendly)
		    printf(gt("%s: regular builtin (not found in $PATH)\n"),
			    name);
		else
		    puts(name);
	    }
	    goto ok;
	case function:
	    if (humanfriendly)
		printf(gt("%s: function\n"), name);
	    else
		puts(name);
	    goto ok;
    }

ok:
    free(name);
    return true;
error:
    free(name);
    return false;
}

/* Prints the absolute path of the specified command. */
void print_command_absolute_path(
	const char *name, const char *path, bool humanfriendly)
{
    if (path[0] == L'/') {
	/* the path is already absolute */
	if (humanfriendly)
	    printf(gt("%s: external command at %s\n"), name, path);
	else
	    puts(path);
	return;
    }

    const wchar_t *wpwd = getvar(L VAR_PWD);
    char *pwd = NULL;
    if (wpwd) {
	pwd = malloc_wcstombs(wpwd);
	if (!is_same_file(pwd, ".")) {
	    free(pwd);
	    pwd = NULL;
	}
    }
    if (!pwd) {
	pwd = xgetcwd();
	if (!pwd)
	    pwd = xstrdup(".");  /* last resort */
    }

    if (humanfriendly)
	printf(gt("%s: external command at %s/%s\n"), name, pwd, path);
    else
	printf("%s/%s\n", pwd, path);
    free(pwd);
    return;
}

#if YASH_ENABLE_HELP

const char command_help[] = Ngt(
"command - execute or identify command\n"
"\tcommand [-bBp] command [argument...]\n"
"\tcommand -v|-V [-bBp] command...\n"
"Executes or identifies the specified command.\n"
"Without the -v or -V option, <command> is executed with given <argument>s if\n"
"any. <command> is treated as a builtin or external command, but not a\n"
"function or an alias. If the -p (--standard-path) option is given, the\n"
"system's default PATH is searched for the command instead of the current\n"
"$PATH. If the -b (--builtin-only) option (but not -B) is given, <command> is\n"
"treated as a builtin only: the external command is ignored. The -B\n"
"(--external-only) option does the opposite: an external command is executed\n"
"ignoring the builtin.\n"
"With the -v (--identify) option, identifies <command>. If the command is\n"
"found in $PATH, its full path is printed. If it is a builtin or a function,\n"
"the command name is simply printed. If it is an alias, it is printed in the\n"
"form like \"alias ll='ls -l'\". If the command is not found, nothing is\n"
"printed and the exit status is non-zero.\n"
"With the -V (--verbose-identify) option, the command is identified in the\n"
"same way but the result is printed verbosely in a human-readable form.\n"
"In the POSIXly correct mode, the -b and -B options are not available.\n"
);

const char type_help[] = Ngt(
"type - identify command\n"
"\ttype command...\n"
"Prints the type of <command>s. Same as \"command -V <command>...\".\n"
);

#endif /* YASH_ENABLE_HELP */

/* The "times" builtin */
int times_builtin(int argc __attribute__((unused)), void **argv)
{
    wchar_t opt;
    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"", help_option, NULL))) {
	switch (opt) {
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:  print_usage:
		fprintf(stderr, gt("Usage:  times\n"));
		SPECIAL_BI_ERROR;
		return Exit_ERROR;
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
	xerror(errno, Ngt("cannot get time data"));
	return Exit_FAILURE;
    }
    format_time(tms.tms_utime, sum, sus);
    format_time(tms.tms_stime, ssm, sss);
    format_time(tms.tms_cutime, cum, cus);
    format_time(tms.tms_cstime, csm, css);
    if (printf("%jdm%fs %jdm%fs\n%jdm%fs %jdm%fs\n",
	    sum, sus, ssm, sss, cum, cus, csm, css) >= 0) {
	return Exit_SUCCESS;
    } else {
	xerror(errno, Ngt("cannot print to standard output"));
	return Exit_FAILURE;
    }
#undef format_time
}

#if YASH_ENABLE_HELP
const char times_help[] = Ngt(
"times - print process times\n"
"\ttimes\n"
"Prints the accumulated user and system times consumed by the shell process\n"
"and all of its child processes.\n"
"The first output line is for the shell process, and the second the child\n"
"processes. For each line, the user time is printed, followed by the system\n"
"time.\n"
);
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
