/* Yash: yet another shell */
/* job.c: job control */
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
#include "job.h"
#include <assert.h>
#include <errno.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include "builtin.h"
#include "exec.h"
#include "option.h"
#include "plist.h"
#include "redir.h"
#include "sig.h"
#include "strbuf.h"
#include "util.h"
#include "yash.h"
#if YASH_ENABLE_LINEEDIT
# include "xfnmatch.h"
# include "lineedit/complete.h"
# ifndef FG_DONT_SAVE_TERMINAL
#  include "lineedit/terminfo.h"
# endif
#endif


static inline job_T *get_job(size_t jobnumber)
    __attribute__((pure));
static inline void free_job(job_T *job);
static void trim_joblist(void);
static void set_current_jobnumber(size_t jobnumber);
static size_t find_next_job(size_t numlimit);
static void apply_curstop(void);
static int calc_status(int status)
    __attribute__((const));
static inline int calc_status_of_process(const process_T *p)
    __attribute__((nonnull,pure));
static wchar_t *get_job_name(const job_T *job)
    __attribute__((nonnull,warn_unused_result));
static char *get_process_status_string(const process_T *p, bool *needfree)
    __attribute__((nonnull,malloc,warn_unused_result));
static char *get_job_status_string(const job_T *job, bool *needfree)
    __attribute__((nonnull,malloc,warn_unused_result));
static int print_job_status(size_t jobnumber,
	bool changedonly, bool verbose, bool remove_done, FILE *f)
    __attribute__((nonnull));
static size_t get_jobnumber_from_name(const wchar_t *name)
    __attribute__((nonnull,pure));
static size_t get_jobnumber_from_pid(long pid)
    __attribute__((pure));

static bool jobs_builtin_print_job(size_t jobnumber,
	bool verbose, bool changedonly, bool pgidonly,
	bool runningonly, bool stoppedonly);
static int continue_job(size_t jobnumber, job_T *job, bool fg)
    __attribute__((nonnull));
static int wait_for_job_by_jobspec(const wchar_t *jobspec)
    __attribute__((nonnull));
static bool wait_builtin_has_job(bool jobcontrol);


/* The list of jobs.
 * `joblist.contents[ACTIVE_JOBNO]' is a special job that is called "active
 * job", the job that is currently being executed.
 * The list length is always non-zero. */
static plist_T joblist;

/* number of the current/previous jobs. 0 if none. */
static size_t current_jobnumber, previous_jobnumber;

/* Initializes the job list. */
void init_job(void)
{
    assert(joblist.contents == NULL);
    pl_init(&joblist);
    pl_add(&joblist, NULL);
}

/* Sets the active job. */
void set_active_job(job_T *job)
{
    assert(ACTIVE_JOBNO < joblist.length);
    assert(joblist.contents[ACTIVE_JOBNO] == NULL);
    joblist.contents[ACTIVE_JOBNO] = job;
}

/* Moves the active job into the job list.
 * If the newly added job is stopped, it becomes the current job.
 * If `current' is true or there is no current job, the newly added job becomes
 * the current job if there is no stopped job. */
void add_job(bool current)
{
    job_T *job = joblist.contents[ACTIVE_JOBNO];
    size_t jobnumber;

    assert(job != NULL);
    joblist.contents[ACTIVE_JOBNO] = NULL;

    /* if there is an empty element in the list, use it */
    for (jobnumber = 1; jobnumber < joblist.length; jobnumber++) {
	if (joblist.contents[jobnumber] == NULL) {
	    joblist.contents[jobnumber] = job;
	    goto set_current;
	}
    }

    /* if there is no empty, append at the end of the list */
    pl_add(&joblist, job);

set_current:
    assert(joblist.contents[jobnumber] == job);
    if (job->j_status == JS_STOPPED || current)
	set_current_jobnumber(jobnumber);
    else
	set_current_jobnumber(current_jobnumber);
}

/* Returns the job of the specified number or NULL if not found. */
job_T *get_job(size_t jobnumber)
{
    return (jobnumber < joblist.length) ? joblist.contents[jobnumber] : NULL;
}

/* Removes the job of the specified number.
 * If the job is the current/previous job, the current/previous job is reset
 * (another job is assigned to it). */
void remove_job(size_t jobnumber)
{
    free_job(get_job(jobnumber));
    joblist.contents[jobnumber] = NULL;
    trim_joblist();
    set_current_jobnumber(current_jobnumber);
}

/* Removes all jobs unconditionally. */
void remove_all_jobs(void)
{
    for (size_t i = 0; i < joblist.length; i++) {
	free_job(joblist.contents[i]);
	joblist.contents[i] = NULL;
    }
    trim_joblist();
    current_jobnumber = previous_jobnumber = 0;
}

/* Frees the specified job. */
void free_job(job_T *job)
{
    if (job != NULL) {
	for (size_t i = 0; i < job->j_pcount; i++)
	    free(job->j_procs[i].pr_name);
	free(job);
    }
}

/* Shrink the job list, removing unused elements. */
void trim_joblist(void)
{
    if (joblist.maxlength > 20 && joblist.maxlength / 2 > joblist.length) {
	pl_setmax(&joblist, joblist.length * 2);
    } else {
	size_t tail = joblist.length;

	while (tail > 1 && joblist.contents[tail - 1] == NULL)
	    tail--;
	assert(tail > 0);
	pl_truncate(&joblist, tail);
    }
}

/* Sets the `j_legacy' flags of all jobs.
 * All the jobs will be no longer job-controlled. */
void neglect_all_jobs(void)
{
    for (size_t i = 0; i < joblist.length; i++) {
	job_T *job = joblist.contents[i];
	if (job != NULL)
	    job->j_legacy = true;
    }
    current_jobnumber = previous_jobnumber = 0;
}

/* Current/previous job selection discipline:
 *
 * - When there is one or more stopped jobs, the current job must be one of
 *   them.
 * - When there are more than one stopped job, the previous job must be one of
 *   them but the current one.
 * - The current job becomes the previous job when another job becomes the
 *   current.
 *
 * - When a foreground job is stopped, it becomes the current job.
 * - When an asynchronous command is executed and the "curasync" option is set,
 *   it becomes the current job.
 * - When a job is continued by the "bg" command and the "curbg" option is set,
 *   it becomes the current job.
 * - When a job is stopped and the "curstop" option is set, it becomes the
 *   current job.
 *
 * - The "wait" command doesn't change the current and previous jobs. */

/* Sets the current job number to the specified one and resets the previous job
 * number. If the specified job number is not used, a job is arbitrarily chosen
 * for the current. If there is one or more stopped jobs and the one specified
 * by the argument is not stopped, the current job is not changed. */
/* This function must be called whenever a job is added to or removed from the
 * job list or any job's status has been changed. */
void set_current_jobnumber(size_t jobnumber)
{
    size_t stopcount = stopped_job_count();
    const job_T *newcurrent = get_job(jobnumber);

    if (newcurrent == NULL
	    || (stopcount > 0 && newcurrent->j_status != JS_STOPPED)) {
	jobnumber = current_jobnumber;
	newcurrent = get_job(jobnumber);
	if (newcurrent == NULL
		|| (stopcount > 0 && newcurrent->j_status != JS_STOPPED)) {
	    jobnumber = previous_jobnumber;
	    newcurrent = get_job(jobnumber);
	    if (newcurrent == NULL
		    || (stopcount > 0 && newcurrent->j_status != JS_STOPPED))
		jobnumber = find_next_job(ACTIVE_JOBNO);
	}
    }

    if (jobnumber != current_jobnumber) {
	size_t oldcurrentnum = current_jobnumber;
	current_jobnumber = jobnumber;
	jobnumber = oldcurrentnum;
    } else {
	jobnumber = previous_jobnumber;
    }

    const job_T *newprevious = get_job(jobnumber);

    if (newprevious == NULL || jobnumber == current_jobnumber
	    || (stopcount > 1 && newprevious->j_status != JS_STOPPED)) {
	jobnumber = previous_jobnumber;
	newprevious = get_job(jobnumber);
	if (newprevious == NULL || jobnumber == current_jobnumber
		|| (stopcount > 1 && newprevious->j_status != JS_STOPPED))
	    jobnumber = find_next_job(current_jobnumber);
    }
    previous_jobnumber = jobnumber;
}

/* Returns an arbitrary job number except the specified.
 * The returned number is suitable for the current/previous jobs.
 * If there is no job to pick out, 0 is returned.
 * Stopped jobs are preferred to running/finished jobs.
 * If there are more than one stopped jobs, the previous job is preferred. */
size_t find_next_job(size_t excl)
{
    if (previous_jobnumber != excl) {
	job_T *job = get_job(previous_jobnumber);
	if (job != NULL && job->j_status == JS_STOPPED)
	    return previous_jobnumber;
    }
    size_t jobnumber = joblist.length;
    while (--jobnumber > 0) {
	if (jobnumber != excl) {
	    job_T *job = get_job(jobnumber);
	    if (job != NULL && job->j_status == JS_STOPPED)
		return jobnumber;
	}
    }
    jobnumber = joblist.length;
    while (--jobnumber > 0) {
	if (jobnumber != excl) {
	    job_T *job = get_job(jobnumber);
	    if (job != NULL)
		return jobnumber;
	}
    }
    return 0;
}

/* If the "curstop" option is set and there is a job which has been stopped and
 * whose `j_statuschanged' flag is set, make it the current job. */
void apply_curstop(void)
{
    if (shopt_curstop) {
	for (size_t i = 0; i < joblist.length; i++) {
	    job_T *job = joblist.contents[i];
	    if (job != NULL)
		if (job->j_status == JS_STOPPED && job->j_statuschanged)
		    set_current_jobnumber(i);
	}
    }
    set_current_jobnumber(current_jobnumber);
}

/* Counts the number of jobs in the job list. */
size_t job_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < joblist.length; i++) {
	const job_T *job = joblist.contents[i];
	if (job == NULL)
	    continue;
	if (job->j_status == JS_DONE && !job->j_statuschanged)
	    continue; // Ignore finished jobs that have already been reported.
	count++;
    }
    return count;
}

/* Counts the number of stopped jobs in the job list. */
size_t stopped_job_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < joblist.length; i++) {
	job_T *job = joblist.contents[i];
	if (job != NULL && job->j_status == JS_STOPPED)
	    count++;
    }
    return count;
}


/* Updates the info about the jobs in the job list.
 * This function doesn't block. */
void do_wait(void)
{
    pid_t pid;
    int status;
#if HAVE_WCONTINUED
    static int waitpidoption = WUNTRACED | WCONTINUED | WNOHANG;
#else
    const int waitpidoption = WUNTRACED | WNOHANG;
#endif

start:
    pid = waitpid(-1, &status, waitpidoption);
    if (pid < 0) {
	switch (errno) {
	    case EINTR:
		goto start;  /* try again */
	    case ECHILD:
		return;      /* there are no child processes */
#if HAVE_WCONTINUED
	    /* Even when the WCONTINUED flag is defined in the <sys/wait.h>
	     * header, the OS kernel may not support it. We try again without
	     * the flag if it is rejected. */
	    case EINVAL:
		if (waitpidoption & WCONTINUED) {
		    waitpidoption &= ~WCONTINUED;
		    goto start;
		}
#endif
		; /* falls thru! */
	    default:
		xerror(errno, "waitpid");
		return;
	}
    } else if (pid == 0) {
	/* no more jobs to be updated */
	return;
    }

    size_t jobnumber, pnumber;
    job_T *job;
    process_T *pr;

    /* determine `jobnumber', `job' and `pr' from `pid' */
    for (jobnumber = 0; jobnumber < joblist.length; jobnumber++)
	if ((job = joblist.contents[jobnumber]) != NULL)
	    for (pnumber = 0; pnumber < job->j_pcount; pnumber++)
		if ((pr = &job->j_procs[pnumber])->pr_pid == pid &&
			pr->pr_status != JS_DONE)
		    goto found;

    /* If `pid' was not found in the job list, we simply ignore it. This may
     * happen on some occasions: e.g. the job has been "disown"ed. */
    goto start;

found:
    pr->pr_statuscode = status;
    if (WIFEXITED(status) || WIFSIGNALED(status))
	pr->pr_status = JS_DONE;
    if (WIFSTOPPED(status))
	pr->pr_status = JS_STOPPED;
#ifdef HAVE_WCONTINUED
    if (WIFCONTINUED(status))
	pr->pr_status = JS_RUNNING;
    /* On FreeBSD, when WIFCONTINUED is true, WIFSIGNALED is also true. We must
     * be careful about the order of these checks. */
#endif

    /* decide the job status from the process status:
     * - JS_RUNNING if any of the processes is running.
     * - JS_STOPPED if no processes are running but some are stopped.
     * - JS_DONE if all the processes are finished. */
    jobstatus_T oldstatus = job->j_status;
    bool anyrunning = false, anystopped = false;
    /* check if there are running/stopped processes */
    for (size_t i = 0; i < job->j_pcount; i++) {
	switch (job->j_procs[i].pr_status) {
	    case JS_RUNNING:  anyrunning = true;  goto out_of_loop;
	    case JS_STOPPED:  anystopped = true;  break;
	    default:                              break;
	}
    }
out_of_loop:
    job->j_status = anyrunning ? JS_RUNNING : anystopped ? JS_STOPPED : JS_DONE;
    if (job->j_status != oldstatus)
	job->j_statuschanged = true;

    goto start;
}

/* Waits for the specified job to finish (or stop).
 * `jobnumber' must be a valid job number.
 * If `return_on_stop' is false, waits for the job to finish.
 * Otherwise, waits for the job to finish or stop.
 * If `interruptible' is true, this function can be canceled by SIGINT.
 * If `return_on_trap' is true, this function returns false immediately after
 * trap actions are performed. Otherwise, traps are not handled.
 * This function returns immediately if the job is already finished/stopped or
 * is not a child of this shell process.
 * Returns the signal number if interrupted, or zero if successful. */
/* In most cases, you should call `put_foreground' to bring the shell back to
 * foreground after calling `wait_for_job' if `doing_job_control_now' is true.
 */
int wait_for_job(size_t jobnumber, bool return_on_stop,
	bool interruptible, bool return_on_trap)
{
    int signum = 0;
    job_T *job = joblist.contents[jobnumber];

    if (!job->j_legacy) {
	bool savenonotify = job->j_nonotify;
	job->j_nonotify = true;
	for (;;) {
	    if (job->j_status == JS_DONE)
		break;
	    if (return_on_stop && job->j_status == JS_STOPPED)
		break;
	    signum = wait_for_sigchld(interruptible, return_on_trap);
	    if (signum != 0)
		break;
	}
	job->j_nonotify = savenonotify;
    }
    return signum;
}

/* Waits for the specified child process to finish (or stop).
 * `cpid' is the process ID of the child process to wait for. This must not be
 * in the job list.
 * `cpgid' is the process group ID of the child. If the child's PGID is the same
 * as that of the parent, `cpgid' must be 0.
 * If `return_on_stop' is false, waits for the job to finish.
 * Otherwise, waits for the job to finish or stop.
 * Traps are not handled in this function.
 * There must be no active job when this function is called.
 * If `return_on_stop' is true and the child is stopped, this function returns
 * a pointer to a pointer to a wide string. The caller must assign a pointer to
 * a newly malloced wide string to the variable the return value points to.
 * This string is used as the name of the new stopped job.
 * If the child exited, this function returns NULL.
 * The exit status is assigned to `laststatus' in any case. */
wchar_t **wait_for_child(pid_t cpid, pid_t cpgid, bool return_on_stop)
{
    job_T *job = xmalloc(add(sizeof *job, sizeof *job->j_procs));
    job->j_pgid = cpgid;
    job->j_status = JS_RUNNING;
    job->j_statuschanged = false;
    job->j_legacy = false;
    job->j_nonotify = false;
    job->j_pcount = 1;
    job->j_procs[0].pr_pid = cpid;
    job->j_procs[0].pr_status = JS_RUNNING;
    job->j_procs[0].pr_statuscode = 0;
    job->j_procs[0].pr_name = NULL;
    set_active_job(job);
    wait_for_job(ACTIVE_JOBNO, return_on_stop, false, false);
    if (doing_job_control_now)
	put_foreground(shell_pgid);
    laststatus = calc_status_of_job(job);
    if (job->j_status == JS_DONE) {
	notify_signaled_job(ACTIVE_JOBNO);
	remove_job(ACTIVE_JOBNO);
	return NULL;
    } else {
	add_job(true);
	return &job->j_procs[0].pr_name;
    }
}

/* Returns the process group ID of the specified job.
 * If no valid job is found, an error message is printed and -1 is returned.
 * `jobname' may have a preceding '%' sign. */
pid_t get_job_pgid(const wchar_t *jobname)
{
    size_t jobnumber = get_jobnumber_from_name(
	    (jobname[0] == L'%') ? &jobname[1] : jobname);
    const job_T *job;

    if (jobnumber >= joblist.length) {
	xerror(0, Ngt("job specification `%ls' is ambiguous"), jobname);
	return -1;
    } else if (jobnumber == 0
	    || (job = joblist.contents[jobnumber]) == NULL
	    || job->j_legacy) {
	xerror(0, Ngt("no such job `%ls'"), jobname);
	return -1;
    } else if (job->j_pgid == 0) {
	xerror(0, Ngt("`%ls' is not a job-controlled job"), jobname);
	return -1;
    } else {
	return job->j_pgid;
    }
}

/* Puts the specified process group in the foreground.
 * `pgrp' must be a valid process group ID and `doing_job_control_now' must be
 * true. */
void put_foreground(pid_t pgrp)
{
    sigset_t blockss, savess;

    assert(doing_job_control_now);
    assert(pgrp > 0);

    sigemptyset(&blockss);
    sigaddset(&blockss, SIGTTOU);
    sigemptyset(&savess);
    sigprocmask(SIG_BLOCK, &blockss, &savess);

    tcsetpgrp(ttyfd, pgrp);

    sigprocmask(SIG_SETMASK, &savess, NULL);
}

/* Ensures the current shell process is in the foreground.
 * The shell process is stopped by SIGTTOU until it is put in the foreground.
 * This function requires `doing_job_control_now' to be true. */
/* This function prevents the job-control shell from mangling the terminal while
 * another shell is using it. */
void ensure_foreground(void)
{
    /* This function calls `tcsetpgrp' with the default SIGTTOU handler. If the
     * shell is in the background, it will receive SIGTTOU and get stopped until
     * it is continued in the foreground. */

    struct sigaction dflsa, savesa;
    sigset_t blockss, savess;

    assert(doing_job_control_now);
    assert(shell_pgid > 0);

    dflsa.sa_handler = SIG_DFL;
    dflsa.sa_flags = 0;
    sigemptyset(&dflsa.sa_mask);
    sigemptyset(&savesa.sa_mask);
    sigaction(SIGTTOU, &dflsa, &savesa);

    sigemptyset(&blockss);
    sigaddset(&blockss, SIGTTOU);
    sigemptyset(&savess);
    sigprocmask(SIG_UNBLOCK, &blockss, &savess);

    tcsetpgrp(ttyfd, shell_pgid);

    sigprocmask(SIG_SETMASK, &savess, NULL);
    sigaction(SIGTTOU, &savesa, NULL);
}

/* Computes the exit status from the status code returned by `waitpid'. */
int calc_status(int status)
{
    if (WIFEXITED(status))
	return WEXITSTATUS(status);
#ifdef WIFCONTINUED
    if (WIFCONTINUED(status))
	return Exit_SUCCESS;
    /* On FreeBSD, when WIFCONTINUED is true, WIFSIGNALED is also true. We must
     * be careful about the order of these checks. */
#endif
    if (WIFSIGNALED(status))
	return WTERMSIG(status) + TERMSIGOFFSET;
    if (WIFSTOPPED(status))
	return WSTOPSIG(status) + TERMSIGOFFSET;
    assert(false);
}

/* Computes the exit status of the specified process.
 * The process state must be JS_DONE or JS_STOPPED. */
int calc_status_of_process(const process_T *p)
{
    int s = p->pr_statuscode;
    return (p->pr_pid == 0) ? s : calc_status(s);
}

/* Computes the exit status of the specified job.
 * The job state must be JS_DONE or JS_STOPPED. */
int calc_status_of_job(const job_T *job)
{
    switch (job->j_status) {
    case JS_DONE:
	if (!shopt_pipefail)
	    return calc_status_of_process(&job->j_procs[job->j_pcount - 1]);
	for (size_t i = job->j_pcount; i-- > 0; ) {
	    int status = calc_status_of_process(&job->j_procs[i]);
	    if (status != Exit_SUCCESS)
		return status;
	}
	return Exit_SUCCESS;
    case JS_STOPPED:
	for (size_t i = job->j_pcount; i-- > 0; ) {
	    if (job->j_procs[i].pr_status == JS_STOPPED)
		return calc_status(job->j_procs[i].pr_statuscode);
	}
	/* falls thru! */
    default:
	assert(false);
    }
}

/* Returns the name of the specified job.
 * If the job has only one process, `job->j_procs[0].pr_name' is returned.
 * Otherwise, the names of all the process are concatenated and returned, which
 * must be freed by the caller. */
wchar_t *get_job_name(const job_T *job)
{
    if (job->j_pcount == 1)
	return job->j_procs[0].pr_name;

    xwcsbuf_T buf;
    wb_init(&buf);
    for (size_t i = 0; i < job->j_pcount; i++) {
	if (i > 0)
	    wb_cat(&buf, L" | ");
	wb_cat(&buf, job->j_procs[i].pr_name);
    }
    return wb_towcs(&buf);
}

/* Returns a string that describes the status of the specified process
 * such as "Running" and "Stopped(SIGTSTP)".
 * The returned string must be freed by the caller iff `*needfree' is assigned
 * true, otherwise it must not be modified or freed. */
char *get_process_status_string(const process_T *p, bool *needfree)
{
    int status, sig;

    switch (p->pr_status) {
    case JS_RUNNING:
	*needfree = false;
	return (char *) gt("Running");
    case JS_STOPPED:
	*needfree = true;
	return malloc_printf(gt("Stopped(SIG%ls)"),
		get_signal_name(WSTOPSIG(p->pr_statuscode)));
    case JS_DONE:
	status = p->pr_statuscode;
	if (p->pr_pid == 0)
	    goto exitstatus;
	if (WIFEXITED(status)) {
	    status = WEXITSTATUS(status);
exitstatus:
	    if (status == Exit_SUCCESS) {
		*needfree = false;
		return (char *) gt("Done");
	    } else {
		*needfree = true;
		return malloc_printf(gt("Done(%d)"), status);
	    }
	} else {
	    assert(WIFSIGNALED(status));
	    *needfree = true;
	    sig = WTERMSIG(status);
#ifdef WCOREDUMP
	    if (WCOREDUMP(sig))
		return malloc_printf(gt("Killed (SIG%ls: core dumped)"),
			get_signal_name(sig));
#endif
	    return malloc_printf(gt("Killed (SIG%ls)"), get_signal_name(sig));
	}
    }
    assert(false);
}

/* Returns a string that describes the status of the specified job
 * such as "Running" and "Stopped(SIGTSTP)".
 * The returned string must be freed by the caller iff `*needfree' is assigned
 * true, otherwise it must not be modified or freed. */
char *get_job_status_string(const job_T *job, bool *needfree)
{
    switch (job->j_status) {
    case JS_RUNNING:
	*needfree = false;
	return (char *) gt("Running");
    case JS_STOPPED:
	/* find a stopped process */
	for (size_t i = job->j_pcount; ; )
	    if (job->j_procs[--i].pr_status == JS_STOPPED)
		return get_process_status_string(&job->j_procs[i], needfree);
	assert(false);
    case JS_DONE:
	return get_process_status_string(
		&job->j_procs[job->j_pcount - 1], needfree);
    }
    assert(false);
}

/* Returns true iff there is any job whose status has been changed but not yet
 * reported. If this function returns false, `print_job_status_all' is nop. */
bool any_job_status_has_changed(void)
{
    for (size_t i = 1; i < joblist.length; i++) {
	const job_T *job = get_job(i);
	if (job != NULL && !job->j_nonotify && job->j_statuschanged)
	    return true;
    }
    return false;
}

/* Prints the status of the specified job.
 * If `remove_done' is true, finished jobs are removed from the job list after
 * the status is printed.
 * If the specified job doesn't exist, nothing is printed (it isn't an error).
 * If `changedonly' is true, the job is printed only if the `j_statuschanged'
 * flag is true.
 * If `verbose' is true, the status is printed in the process-wise format rather
 * than the usual job-wise format.
 * Returns zero if successful. Returns errno if `fprintf' failed. */
int print_job_status(size_t jobnumber,
	bool changedonly, bool verbose, bool remove_done, FILE *f)
{
    int result = 0;

    job_T *job = get_job(jobnumber);
    if (job == NULL || job->j_nonotify)
	return result;
    if (changedonly && !job->j_statuschanged)
	return result;

    char current;
    if      (jobnumber == current_jobnumber)  current = '+';
    else if (jobnumber == previous_jobnumber) current = '-';
    else                                      current = ' ';

    if (!verbose) {
	bool needfree;
	char *status = get_job_status_string(job, &needfree);
	wchar_t *jobname = get_job_name(job);

	/* TRANSLATORS: the translated format string can be different 
	 * from the original only in the number of spaces. This is required
	 * for POSIX compliance. */
	result = fprintf(f, gt("[%zu] %c %-20s %ls\n"),
		jobnumber, current, status, jobname);
	result = (result >= 0) ? 0 : errno;

	if (needfree)
	    free(status);
	if (jobname != job->j_procs[0].pr_name)
	    free(jobname);
    } else {
	bool needfree;
	pid_t pid = job->j_procs[0].pr_pid;
	char *status = get_process_status_string(
		&job->j_procs[posixly_correct ? job->j_pcount - 1 : 0],
		&needfree);
	wchar_t *jobname = job->j_procs[0].pr_name;

	/* TRANSLATORS: the translated format string can be different 
	 * from the original only in the number of spaces. This is required
	 * for POSIX compliance. */
	result = fprintf(f, gt("[%zu] %c %5jd %-20s   %ls\n"),
		jobnumber, current, (intmax_t) pid, status, jobname);
	result = (result >= 0) ? 0 : errno;
	if (needfree)
	    free(status);

	for (size_t i = 1; result == 0 && i < job->j_pcount; i++) {
	    pid = job->j_procs[i].pr_pid;
	    status = get_process_status_string(&job->j_procs[i], &needfree);
	    jobname = job->j_procs[i].pr_name;

	    /* TRANSLATORS: the translated format string can be different 
	     * from the original only in the number of spaces. This is required
	     * for POSIX compliance. */
	    result = fprintf(f, gt("      %5jd %-20s | %ls\n"),
		    (intmax_t) pid, (posixly_correct ? "" : status), jobname);
	    result = (result >= 0) ? 0 : errno;
	    if (needfree)
		free(status);
	}
    }
    job->j_statuschanged = false;
    if (remove_done && job->j_status == JS_DONE)
	remove_job(jobnumber);

    return result;
}

/* Prints the status of jobs which have been changed but not reported. */
void print_job_status_all(void)
{
    apply_curstop();
    for (size_t i = 1; i < joblist.length; i++)
	print_job_status(i, true, false, false, stderr);
}

/* If the shell is interactive and the specified job has been killed by a
 * signal other than SIGPIPE, prints a notification to the standard error.
 * If the signal is SIGINT, only a single newline is printed to the standard
 * error and the shell is flagged as interrupted. */
void notify_signaled_job(size_t jobnumber)
{
    if (!is_interactive_now)
	return;

    job_T *job = get_job(jobnumber);
    if (job == NULL || job->j_status != JS_DONE)
	return;

    process_T *p = &job->j_procs[job->j_pcount - 1];
    assert(p->pr_status == JS_DONE);
    if (p->pr_pid == 0 || !WIFSIGNALED(p->pr_statuscode))
	return;

    int sig = WTERMSIG(p->pr_statuscode);
    switch (sig) {
	case SIGINT:
	    fputc('\n', stderr);
	    set_interrupted();
	    break;
	case SIGPIPE:
	    break;
	default:
#if HAVE_STRSIGNAL
	    fprintf(stderr, gt("The process was killed by SIG%ls: %s\n"),
		    get_signal_name(sig), strsignal(sig));
#else
	    fprintf(stderr, gt("The process was killed by SIG%ls\n"),
		    get_signal_name(sig));
#endif
	    break;
    }
}

/* Returns the job number from the specified job ID string.
 * If no applicable job is found, zero is returned.
 * If more than one jobs are found, `joblist.length' is returned.
 * When `name' is a number, the number is returned if it is a valid job number.
 * The string must not have the preceding '%'. */
/* "", "%", "+"  -> the current job
 * "-"           -> the previous job
 * "n" (integer) -> the job #n
 * "xxx"         -> the job whose name starts with "xxx"
 * "?xxx"        -> the job whose name contains "xxx" */
size_t get_jobnumber_from_name(const wchar_t *name)
{
    if (name[0] == L'\0' || wcscmp(name, L"%") == 0 || wcscmp(name, L"+") == 0)
	return current_jobnumber;
    if (wcscmp(name, L"-") == 0)
	return previous_jobnumber;

    if (iswdigit(name[0])) {
	unsigned long num;
	if (xwcstoul(name, 10, &num))
	    return (num <= SIZE_MAX && get_job(num) != NULL) ? num : 0;
    }

    bool contain;
    size_t n = 0;
    if (name[0] == L'?') {
	contain = true;
	name++;
    } else {
	contain = false;
    }
    for (size_t i = 1; i < joblist.length; i++) {
	job_T *job = joblist.contents[i];
	if (job != NULL) {
	    wchar_t *jobname = get_job_name(job);
	    bool match = contain
		? wcsstr(jobname, name) != NULL
		: matchwcsprefix(jobname, name) != NULL;
	    if (jobname != job->j_procs[0].pr_name)
		free(jobname);
	    if (match) {
		if (n != 0)
		    return joblist.length;  /* more than one found */
		else
		    n = i;
	    }
	}
    }
    return n;
}

/* Returns the number of job that contains a process whose process ID is `pid'.
 * If not found, 0 is returned. */
size_t get_jobnumber_from_pid(long pid)
{
    size_t jobnumber;
    if (pid == 0)
	return 0;
    for (jobnumber = joblist.length; --jobnumber > 0; ) {
	job_T *job = joblist.contents[jobnumber];
	if (job != NULL) {
	    for (size_t i = 0; i < job->j_pcount; i++)
		if (job->j_procs[i].pr_pid == pid)
		    goto found;
	}
    }
found:
    return jobnumber;
}

#if YASH_ENABLE_LINEEDIT

/* Generates completion candidates for job names matching the pattern. */
/* The prototype of this function is declared in "lineedit/complete.h". */
void generate_job_candidates(const le_compopt_T *compopt)
{
    if (!(compopt->type & CGT_JOB))
	return;

    le_compdebug("adding job candidates");
    if (!le_compile_cpatterns(compopt))
	return;

    for (size_t i = 1; i < joblist.length; i++) {
	const job_T *job = joblist.contents[i];
	if (job == NULL)
	    continue;
	switch (job->j_status) {
	    case JS_RUNNING:
		if (!(compopt->type & CGT_RUNNING))
		    continue;
		break;
	    case JS_STOPPED:
		if (!(compopt->type & CGT_STOPPED))
		    continue;
		break;
	    case JS_DONE:
		if (!(compopt->type & CGT_DONE))
		    continue;
		break;
	}

	wchar_t *jobname = get_job_name(job);
	if (le_wmatch_comppatterns(compopt, jobname))
	    le_new_candidate(CT_JOB, xwcsdup(jobname),
		    malloc_wprintf(L"%%%zu", i), compopt);

	if (jobname != job->j_procs[0].pr_name)
	    free(jobname);
    }
}

#endif /* YASH_ENABLE_LINEEDIT */


/********** Built-ins **********/

/* Options for the "jobs" built-in. */
const struct xgetopt_T jobs_options[] = {
    { L'l', L"verbose",      OPTARG_NONE, true,  NULL, },
    { L'n', L"new",          OPTARG_NONE, false, NULL, },
    { L'p', L"pgid-only",    OPTARG_NONE, true,  NULL, },
    { L'r', L"running-only", OPTARG_NONE, false, NULL, },
    { L's', L"stopped-only", OPTARG_NONE, false, NULL, },
#if YASH_ENABLE_HELP
    { L'-', L"help",         OPTARG_NONE, false, NULL, },
#endif
    { L'\0', NULL, 0, false, NULL, },
};

/* The "jobs" built-in, which accepts the following options:
 *  -l: be verbose
 *  -n: print the jobs only whose status have changed
 *  -p: print the process ID only
 *  -r: print running jobs only
 *  -s: print stopped jobs only
 * In the POSIXly correct mode, only -l and -p are available. */
int jobs_builtin(int argc, void **argv)
{
    bool verbose = false, changedonly = false, pgidonly = false;
    bool runningonly = false, stoppedonly = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, jobs_options, 0)) != NULL) {
	switch (opt->shortopt) {
	    case L'l':  verbose     = true;  break;
	    case L'n':  changedonly = true;  break;
	    case L'p':  pgidonly    = true;  break;
	    case L'r':  runningonly = true;  break;
	    case L's':  stoppedonly = true;  break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return Exit_ERROR;
	}
    }

    nextforceexit = true;
    apply_curstop();

    if (xoptind < argc) {
	/* print the specified jobs */
	do {
	    const wchar_t *jobspec = ARGV(xoptind);
	    if (jobspec[0] == L'%') {
		jobspec++;
	    } else if (posixly_correct) {
		xerror(0, Ngt("`%ls' is not a valid job specification"),
			ARGV(xoptind));
		continue;
	    }

	    size_t jobnumber = get_jobnumber_from_name(jobspec);
	    if (jobnumber >= joblist.length) {
		xerror(0, Ngt("job specification `%ls' is ambiguous"),
			ARGV(xoptind));
	    } else if (jobnumber == 0 || joblist.contents[jobnumber] == NULL) {
		xerror(0, Ngt("no such job `%ls'"), ARGV(xoptind));
	    } else {
		if (!jobs_builtin_print_job(jobnumber, verbose,
			changedonly, pgidonly, runningonly, stoppedonly))
		    return Exit_FAILURE;
	    }
	} while (++xoptind < argc);
    } else {
	/* print all jobs */
	for (size_t i = 1; i < joblist.length; i++) {
	    if (!jobs_builtin_print_job(i, verbose, changedonly, pgidonly,
		    runningonly, stoppedonly))
		return Exit_FAILURE;
	}
    }

    return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;
}

/* Prints the job status.
 * On an I/O error, an error message is printed to the standard error and false
 * is returned. */
bool jobs_builtin_print_job(size_t jobnumber,
	bool verbose, bool changedonly, bool pgidonly,
	bool runningonly, bool stoppedonly)
{
    job_T *job = get_job(jobnumber);

    if (job == NULL)
	return true;
    if (runningonly && job->j_status != JS_RUNNING)
	return true;
    if (stoppedonly && job->j_status != JS_STOPPED)
	return true;

    int err;
    if (pgidonly) {
	if (changedonly && !job->j_statuschanged)
	    return true;
	int result = printf("%jd\n", (intmax_t) job->j_pgid);
	err = (result >= 0) ? 0 : errno;
    } else {
	err = print_job_status(jobnumber, changedonly, verbose, true, stdout);
    }
    if (err != 0) {
	xerror(err, Ngt("cannot print to the standard output"));
	return false;
    } else {
	return true;
    }
}

#if YASH_ENABLE_HELP
const char jobs_help[] = Ngt(
"print info about jobs"
);
const char jobs_syntax[] = Ngt(
"\tjobs [-lnprs] [job...]\n"
);
#endif

/* The "fg"/"bg" built-in */
int fg_builtin(int argc, void **argv)
{
    bool fg = (wcscmp(argv[0], L"fg") == 0);

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, help_option, 0)) != NULL) {
	switch (opt->shortopt) {
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return Exit_ERROR;
	}
    }

    if (fg && posixly_correct && !validate_operand_count(argc - xoptind, 0, 1))
	return Exit_ERROR;

    if (!doing_job_control_now) {
	xerror(0, Ngt("job control is disabled"));
	return Exit_FAILURE;
    }

    int status = Exit_SUCCESS;
    job_T *job;

    if (xoptind < argc) {
	do {
	    const wchar_t *jobspec = ARGV(xoptind);
	    if (jobspec[0] == L'%') {
		jobspec++;
	    } else if (posixly_correct) {
		xerror(0, Ngt("`%ls' is not a valid job specification"),
			ARGV(xoptind));
		continue;
	    }

	    size_t jobnumber = get_jobnumber_from_name(jobspec);
	    if (jobnumber >= joblist.length) {
		xerror(0, Ngt("job specification `%ls' is ambiguous"),
			ARGV(xoptind));
	    } else if (jobnumber == 0
		    || (job = joblist.contents[jobnumber]) == NULL
		    || job->j_legacy) {
		xerror(0, Ngt("no such job `%ls'"), ARGV(xoptind));
	    } else if (job->j_pgid == 0) {
		xerror(0, Ngt("`%ls' is not a job-controlled job"),
			ARGV(xoptind));
	    } else {
		status = continue_job(jobnumber, job, fg);
	    }
	} while (++xoptind < argc);
    } else {
	if (current_jobnumber == 0 ||
		(job = joblist.contents[current_jobnumber])->j_legacy) {
	    xerror(0, Ngt("there is no current job"));
	} else if (job->j_pgid == 0) {
	    xerror(0, Ngt("the current job is not a job-controlled job"));
	} else {
	    status = continue_job(current_jobnumber, job, fg);
	}
    }

    if (status != 0)
	return status;
    if (yash_error_message_count != 0)
	return Exit_FAILURE;
    return Exit_SUCCESS;
}

/* Continues execution of the specified job.
 * Returns the exit code of the continued job or 0 if it is still running. */
int continue_job(size_t jobnumber, job_T *job, bool fg)
{
    assert(job->j_pgid > 0);
    assert(!job->j_legacy);

    wchar_t *name = get_job_name(job);
    if (fg && posixly_correct)
	xprintf("%ls\n", name);
    else
	xprintf("[%zu] %ls\n", jobnumber, name);
    if (name != job->j_procs[0].pr_name)
	free(name);

#if YASH_ENABLE_LINEEDIT && !defined(FG_DONT_SAVE_TERMINAL)
    bool termsave = fg && le_save_terminal();  /* see below */
#endif

    if (job->j_status != JS_DONE) {
	if (fg)
	    put_foreground(job->j_pgid);
	if (kill(-job->j_pgid, SIGCONT) >= 0)
	    job->j_status = JS_RUNNING;
    } else {
	if (!fg)
	    xerror(0, Ngt("job %%%zu has already terminated"), jobnumber);
    }

    int status;
    if (fg) {
	wait_for_job(jobnumber, true, false, false);
	put_foreground(shell_pgid);
#if YASH_ENABLE_LINEEDIT && !defined(FG_DONT_SAVE_TERMINAL)
	if (termsave)
	    le_restore_terminal();
#endif
	switch (job->j_status) {
	    case JS_STOPPED:
		status = calc_status_of_job(job);
		set_current_jobnumber(jobnumber);
		break;
	    case JS_DONE:
		status = calc_status_of_job(job);
		notify_signaled_job(jobnumber);
		remove_job(jobnumber);
		break;
	    default:
		assert(false);
	}
    } else {
	set_current_jobnumber(shopt_curbg ? jobnumber : current_jobnumber);
	status = (job->j_status == JS_RUNNING) ? Exit_SUCCESS : Exit_FAILURE;
    }
    return status;

    /* We save the terminal state before continuing a job in the foreground and
     * restore the state after the job has finished. This is because some
     * programs leave the terminal in the wrong state if they were at first
     * invoked in the background.
     * Programs that change the terminal state generally save the state before
     * changing it and restore it when they are finished. But, if they are
     * invoked in the background, they may save the state that is being used by
     * another program (typically the shell's line-editing), so the state that
     * they restore is not the normal state.
     * We try to work around this problem by saving and restoring the terminal
     * state for the continued programs. */
}

#if YASH_ENABLE_HELP

const char fg_help[] = Ngt(
"run jobs in the foreground"
);
const char fg_syntax[] = Ngt(
"\tfg [job...]\n"
);

const char bg_help[] = Ngt(
"run jobs in the background"
);
const char bg_syntax[] = Ngt(
"\tbg [job...]\n"
);

#endif /* YASH_ENABLE_HELP */

/* The "wait" built-in */
int wait_builtin(int argc, void **argv)
{
    bool jobcontrol = doing_job_control_now;
    int status = Exit_SUCCESS;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, help_option, 0)) != NULL) {
	switch (opt->shortopt) {
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return Exit_ERROR;
	}
    }

    if (xoptind < argc) {
	/* wait for the specified jobs */
	for (; xoptind < argc; xoptind++) {
	    int jobstatus = wait_for_job_by_jobspec(ARGV(xoptind));
	    if (jobstatus < 0) {
		status = -jobstatus;
		break;
	    }
	    status = jobstatus;
	}
    } else {
	/* wait for all jobs */
	while (wait_builtin_has_job(jobcontrol)) {
	    status = wait_for_sigchld(jobcontrol, true);
	    if (status) {
		assert(TERMSIGOFFSET >= 128);
		status += TERMSIGOFFSET;
		break;
	    }
	}
    }

    if (yash_error_message_count != 0)
	return Exit_FAILURE;
    return status;
}

/* Finds a job specified by the argument and waits for it.
 * Returns a negated exit status if interrupted. */
int wait_for_job_by_jobspec(const wchar_t *jobspec)
{
    size_t jobnumber;
    if (jobspec[0] == L'%') {
	jobnumber = get_jobnumber_from_name(&jobspec[1]);
    } else {
	long pid;
	if (!xwcstol(jobspec, 10, &pid) || pid < 0) {
	    xerror(0, Ngt("`%ls' is not a valid job specification"), jobspec);
	    return Exit_FAILURE;
	}
	jobnumber = get_jobnumber_from_pid(pid);
    }
    if (jobnumber >= joblist.length) {
	xerror(0, Ngt("job specification `%ls' is ambiguous"), jobspec);
	return Exit_FAILURE;
    }

    job_T *job;
    if (jobnumber == 0
	    || (job = joblist.contents[jobnumber]) == NULL
	    || job->j_legacy)
	return Exit_NOTFOUND;

    int signal = wait_for_job(jobnumber,
	    doing_job_control_now, doing_job_control_now, true);
    if (signal != 0) {
	assert(TERMSIGOFFSET >= 128);
	return -(signal + TERMSIGOFFSET);
    }

    int status = calc_status_of_job(job);
    if (job->j_status != JS_RUNNING) {
	if (doing_job_control_now && is_interactive_now && !posixly_correct)
	    print_job_status(jobnumber, false, false, true, stdout);
	else if (job->j_status == JS_DONE)
	    remove_job(jobnumber);
    }
    return status;
}

/* Checks if the shell has any job to wait for. */
bool wait_builtin_has_job(bool jobcontrol)
{
    /* print/remove already-finished jobs */
    for (size_t i = 1; i < joblist.length; i++) {
	job_T *job = joblist.contents[i];
	if (jobcontrol && is_interactive_now && !posixly_correct)
	    print_job_status(i, true, false, false, stdout);
	if (job != NULL && (job->j_legacy || job->j_status == JS_DONE))
	    remove_job(i);
    }

    /* see if we have jobs to wait for. */
    for (size_t i = 1; i < joblist.length; i++) {
	job_T *job = joblist.contents[i];
	if (job != NULL && (!jobcontrol || job->j_status == JS_RUNNING))
	    return true;
    }
    return false;
}

#if YASH_ENABLE_HELP
const char wait_help[] = Ngt(
"wait for jobs to terminate"
);
const char wait_syntax[] = Ngt(
"\twait [job or process_id...]\n"
);
#endif

/* The "disown" built-in, which accepts the following option:
 *  -a: disown all jobs */
int disown_builtin(int argc, void **argv)
{
    bool all = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, all_help_options, 0)) != NULL) {
	switch (opt->shortopt) {
	    case L'a':
		all = true;
		break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return Exit_ERROR;
	}
    }

    if (all) {
	remove_all_jobs();
    } else if (xoptind < argc) {
	do {
	    const wchar_t *jobspec = ARGV(xoptind);
	    if (jobspec[0] == L'%') {
		jobspec++;
	    } else if (posixly_correct) {
		xerror(0, Ngt("`%ls' is not a valid job specification"),
			ARGV(xoptind));
		continue;
	    }
	    size_t jobnumber = get_jobnumber_from_name(jobspec);
	    if (jobnumber >= joblist.length) {
		xerror(0, Ngt("job specification `%ls' is ambiguous"),
			ARGV(xoptind));
	    } else if (jobnumber == 0 || joblist.contents[jobnumber] == NULL) {
		xerror(0, Ngt("no such job `%ls'"), ARGV(xoptind));
	    } else {
		remove_job(jobnumber);
	    }
	} while (++xoptind < argc);
    } else {
	if (current_jobnumber == 0 || get_job(current_jobnumber) == NULL)
	    xerror(0, Ngt("there is no current job"));
	else
	    remove_job(current_jobnumber);
    }

    return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;
}

#if YASH_ENABLE_HELP
const char disown_help[] = Ngt(
"disown jobs"
);
const char disown_syntax[] = Ngt(
"\tdisown [job...]\n"
"\tdisown -a\n"
);
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
