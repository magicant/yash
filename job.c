/* Yash: yet another shell */
/* job.c: job control */
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
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "option.h"
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "sig.h"
#include "redir.h"
#include "job.h"
#include "builtin.h"
#include "exec.h"


static inline job_T *get_job(size_t jobnumber)
    __attribute__((pure));
static inline void free_job(job_T *job);
static void trim_joblist(void);
static void set_current_jobnumber(size_t no);
static size_t find_next_job(size_t numlimit);
static int calc_status(int status)
    __attribute__((const));
static wchar_t *get_job_name(const job_T *job)
    __attribute__((nonnull,warn_unused_result));
static char *get_process_status_string(const process_T *p, bool *needfree)
    __attribute__((nonnull,malloc,warn_unused_result));
static char *get_job_status_string(const job_T *job, bool *needfree)
    __attribute__((nonnull,malloc,warn_unused_result));
static size_t get_jobnumber_from_name(const wchar_t *name)
    __attribute__((nonnull,pure));
static size_t get_jobnumber_from_pid(pid_t pid)
    __attribute__((pure));

static void jobs_builtin_print_job(size_t jobnumber,
	bool verbose, bool changedonly, bool pidonly,
	bool runningonly, bool stoppedonly);
static int continue_job(size_t jobnumber, job_T *job, bool fg)
    __attribute__((nonnull));


/* The list of jobs.
 * `joblist.contents[ACTIVE_JOBNO]' is a special job, which is called "active
 * job": the job that is being executed. */
static plist_T joblist;

/* number of the current/previous jobs. 0 if none. */
static size_t current_jobnumber, previous_jobnumber;

/* Initializes the job list. */
void init_job(void)
{
    static bool initialized = false;
    if (!initialized) {
	initialized = true;
	pl_init(&joblist);
	pl_add(&joblist, NULL);
    }
}

/* Sets the active job. */
void set_active_job(job_T *job)
{
    assert(joblist.contents[ACTIVE_JOBNO] == NULL);
    joblist.contents[ACTIVE_JOBNO] = job;
}

/* Moves the active job into the job list.
 * If `current' is true or there is no current job, the job will be the current
 * job. */
void add_job(bool current)
{
    job_T *job = joblist.contents[ACTIVE_JOBNO];

    assert(job != NULL);
    joblist.contents[ACTIVE_JOBNO] = NULL;

    /* if there is an empty element in the list, use it */
    for (size_t i = 1; i < joblist.length; i++) {
	if (joblist.contents[i] == NULL) {
	    joblist.contents[i] = job;
	    if (current || current_jobnumber == 0)
		set_current_jobnumber(i);
	    else if (previous_jobnumber == 0)
		previous_jobnumber = i;
	    return;
	}
    }

    /* if there is no empty, append at the end of the list */
    pl_add(&joblist, job);
    if (current || current_jobnumber == 0)
	set_current_jobnumber(joblist.length - 1);
    else if (previous_jobnumber == 0)
	previous_jobnumber = joblist.length - 1;
}

/* Returns the job of the specified number or NULL if not found. */
job_T *get_job(size_t jobnumber)
{
    return jobnumber < joblist.length ? joblist.contents[jobnumber] : NULL;
}

/* Removes the job of the specified number.
 * If the job is the current/previous job, the current/previous job is reset
 * (another job is assigned to it). */
void remove_job(size_t jobnumber)
{
    job_T *job = get_job(jobnumber);
    joblist.contents[jobnumber] = NULL;
    free_job(job);
    trim_joblist();

    if (jobnumber == current_jobnumber) {
	current_jobnumber = previous_jobnumber;
	previous_jobnumber = find_next_job(current_jobnumber);
    } else if (jobnumber == previous_jobnumber) {
	previous_jobnumber = find_next_job(current_jobnumber);
    }
}

/* Removes all jobs unconditionally. */
void remove_all_jobs(void)
{
    for (size_t i = 0; i < joblist.length; i++)
	remove_job(i);
    trim_joblist();
    current_jobnumber = previous_jobnumber = 0;
}

/* Frees a job. */
void free_job(job_T *job)
{
    if (job) {
	for (size_t i = 0; i < job->j_pcount; i++)
	    free(job->j_procs[i].pr_name);
	free(job);
    }
}

/* Removes unused elements in `joblist'. */
void trim_joblist(void)
{
    size_t tail = joblist.length;

    while (tail > 0 && joblist.contents[--tail] == NULL);
    tail++;
    if (joblist.maxlength > 20 && joblist.maxlength / 2 > joblist.length)
	pl_setmax(&joblist, tail);
    else
	pl_remove(&joblist, tail, SIZE_MAX);
}

/* Negate the `j_pgid' member of all jobs.
 * All the jobs are no longer taken care of by job control. */
void neglect_all_jobs(void)
{
    for (size_t i = 0; i < joblist.length; i++) {
	job_T *job = joblist.contents[i];
	if (job && job->j_pgid >= 0)
	    job->j_pgid = (job->j_pgid > 0) ? -job->j_pgid : -1;
    }
    current_jobnumber = previous_jobnumber = 0;
}

/* - When the current job changes, the last current job will be the next
 *   previous job.
 *   - The "fg" command changes the current job.
 *   - The `add_job' function may change the current job.
 * - When the current job finishes, the previous job becomes the current job.
 * - Restarting the current or previous job by the "bg" command resets the
 *   current and previous jobs.
 * - The "wait" command doesn't change the current and previous jobs. */

/* Sets the current job number to the specified one and resets the previous job
 * number. If `jobnumber' is 0, the previous job becomes the current job.
 * Otherwise `jobnumber' must be a valid job number. */
void set_current_jobnumber(size_t jobnumber)
{
    assert(jobnumber == 0 || get_job(jobnumber) != NULL);

    previous_jobnumber = current_jobnumber;
    if (jobnumber == 0) {
	jobnumber = previous_jobnumber;
	if (jobnumber == 0 || get_job(jobnumber) == NULL)
	    jobnumber = find_next_job(0);
    }
    current_jobnumber = jobnumber;

    if (previous_jobnumber == 0
	    || previous_jobnumber == current_jobnumber)
	previous_jobnumber = find_next_job(current_jobnumber);
}

/* Returns an arbitrary job number except the specified.
 * The returned number is suitable for the next current/previous jobs.
 * If there is no job to pick out, 0 is returned.
 * Stopped jobs are preferred to running/finished jobs.
 * If there are more than one stopped jobs, larger job number is preferred. */
size_t find_next_job(size_t excl)
{
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

/* Counts the number of jobs in the job list. */
size_t job_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < joblist.length; i++)
	if (joblist.contents[i])
	    count++;
    return count;
}

/* Counts the number of stopped jobs in the job list. */
size_t stopped_job_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < joblist.length; i++) {
	job_T *job = joblist.contents[i];
	if (job && job->j_status == JS_STOPPED)
	    count++;
    }
    return count;
}


/* Updates the info about the jobs in the job list by calling `waitpid'.
 * This function doesn't block. */
void do_wait(void)
{
    pid_t pid;
    int status;
#ifdef WIFCONTINUED
    static int waitopts = WUNTRACED | WCONTINUED | WNOHANG;
#else
    static int waitopts = WUNTRACED | WNOHANG;
#endif

start:
    pid = waitpid(-1, &status, waitopts);
    if (pid < 0) {
	switch (errno) {
	    case EINTR:
		goto start;  /* try again */
	    case ECHILD:
		return;      /* there are no child processes */
	    case EINVAL:
#ifdef WIFCONTINUED
		/* According to the Bash source:
		 *     WCONTINUED may be rejected by waitpid as invalid even
		 *     when defined
		 * -> retry without WCONTINUED. */
		if (waitopts & WCONTINUED) {
		    waitopts = WUNTRACED | WNOHANG;
		    goto start;
		}
#endif
		/* falls thru! */
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
	if ((job = joblist.contents[jobnumber]))
	    for (pnumber = 0; pnumber < job->j_pcount; pnumber++)
		if ((pr = &job->j_procs[pnumber])->pr_pid == pid)
		    goto found;

    /* If `pid' is not found in the job list, we simply ignore it. This may
     * happen on some occasions: e.g. the job is "disown"ed. */
    goto start;

found:
    pr->pr_statuscode = status;
    if (WIFEXITED(status) || WIFSIGNALED(status))
	pr->pr_status = JS_DONE;
    else if (WIFSTOPPED(status))
	pr->pr_status = JS_STOPPED;
#ifdef WIFCONTINUED
    else if (WIFCONTINUED(status))
	pr->pr_status = JS_RUNNING;
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

/* Waits for a job to finish (or stop).
 * `jobnumber' must be a valid job number.
 * If `return_on_stop' is false, waits for the job to finish.
 * Otherwise, waits for the job to finish or stop.
 * If `interruptible' is true, this function can be canceled by SIGINT.
 * This function returns immediately if the job is already finished/stopped or
 * is not a child of this shell process.
 * Returns false iff interrupted. */
bool wait_for_job(size_t jobnumber, bool return_on_stop, bool interruptible)
{
    bool result = true;
    job_T *job = joblist.contents[jobnumber];

    if (job->j_pgid >= 0 && job->j_status != JS_DONE
	    && (!return_on_stop || job->j_status != JS_STOPPED)) {
	block_sigchld_and_sigint();
	do {
	    if (!wait_for_sigchld(interruptible)) {
		result = false;
		break;
	    }
	} while (job->j_status != JS_DONE
		&& (!return_on_stop || job->j_status != JS_STOPPED));
	unblock_sigchld_and_sigint();
    }
    return result;
}

/* Put the specified process group in the foreground.
 * `pgrp' must be a valid process group ID and `doing_job_control_now' must be
 * true. */
void put_foreground(pid_t pgrp)
{
    assert(doing_job_control_now);
    assert(pgrp > 0);
    block_sigttou();
    tcsetpgrp(get_ttyfd(), pgrp);
    unblock_sigttou();
}

/* Computes the exit status from the status code returned by `waitpid'. */
int calc_status(int status)
{
    if (WIFEXITED(status))
	return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
	return WTERMSIG(status) + TERMSIGOFFSET;
    if (WIFSTOPPED(status))
	return WSTOPSIG(status) + TERMSIGOFFSET;
#ifdef WIFCONTINUED
    if (WIFCONTINUED(status))
	return 0;
#endif
    assert(false);
}

/* Computes the exit status of the specified job.
 * The job must be JS_DONE or JS_STOPPED. */
int calc_status_of_job(const job_T *job)
{
    switch (job->j_status) {
    case JS_DONE:
	if (job->j_procs[job->j_pcount - 1].pr_pid)
	    return calc_status(job->j_procs[job->j_pcount - 1].pr_statuscode);
	else
	    return job->j_procs[job->j_pcount - 1].pr_statuscode;
    case JS_STOPPED:
	for (int i = job->j_pcount; --i >= 0; ) {
	    if (job->j_procs[i].pr_status == JS_STOPPED)
		return calc_status(job->j_procs[i].pr_statuscode);
	}
	/* falls thru! */
    default:
	assert(false);
    }
}

/* Returns the name of the specified job.
 * If the job has only one process and is not a loop pipe,
 * `job->j_procs[0].pr_name' is returned.
 * Otherwise, the names of all the process are concatenated and returned, which
 * must be freed by the caller. */
wchar_t *get_job_name(const job_T *job)
{
    if (job->j_pcount == 1 && !job->j_loop)
	return job->j_procs[0].pr_name;

    xwcsbuf_T buf;
    wb_init(&buf);
    if (job->j_loop)
	wb_cat(&buf, L"| ");
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
 * true. */
char *get_process_status_string(const process_T *p, bool *needfree)
{
    int status;

    switch (p->pr_status) {
    case JS_RUNNING:
	*needfree = false;
	return gt("Running");
    case JS_STOPPED:
	*needfree = true;
	return malloc_printf(gt("Stopped(SIG%s)"),
		get_signal_name(WSTOPSIG(p->pr_statuscode)));
    case JS_DONE:
	status = p->pr_statuscode;
	if (p->pr_pid == 0)
	    goto exitstatus;
	if (WIFEXITED(status)) {
	    status = WEXITSTATUS(status);
exitstatus:
	    if (status == EXIT_SUCCESS) {
		*needfree = false;
		return gt("Done");
	    } else {
		*needfree = true;
		return malloc_printf(gt("Done(%d)"), status);
	    }
	} else {
	    assert(WIFSIGNALED(status));
	    *needfree = true;
	    status = WTERMSIG(status);
#ifdef WCOREDUMP
	    if (WCOREDUMP(status)) {
		return malloc_printf(gt("Killed (SIG%s: core dumped)"),
			get_signal_name(status));
	    }
#endif
	    return malloc_printf(gt("Killed (SIG%s)"),
		    get_signal_name(status));
	}
    }
    assert(false);
}

/* Returns a string that describes the status of the specified job
 * such as "Running" and "Stopped(SIGTSTP)".
 * The returned string must be freed by the caller iff `*needfree' is assigned
 * true. */
char *get_job_status_string(const job_T *job, bool *needfree)
{
    switch (job->j_status) {
    case JS_RUNNING:
	*needfree = false;
	return gt("Running");
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

/* Prints the status of job(s).
 * Finished jobs are removed from the job list after the status is printed.
 * If `jobnumber' is PJS_ALL, all the jobs are printed. If the specified job
 * doesn't exist, nothing is printed (it isn't an error).
 * If `changedonly' is true, only jobs whose `j_statuschanged' is true is
 * printed. If `verbose' is true, the status is printed in the process-wise
 * format rather than the usual job-wise format. */
void print_job_status(size_t jobnumber, bool changedonly, bool verbose, FILE *f)
{
    if (jobnumber == PJS_ALL) {
	for (size_t i = 1; i < joblist.length; i++)
	    print_job_status(i, changedonly, verbose, f);
	return;
    }

    job_T *job = get_job(jobnumber);
    if (!job || (changedonly && !job->j_statuschanged))
	return;

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
	fprintf(f, gt("[%zu] %c %-20s %ls\n"),
		jobnumber, current, status, jobname);

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
	char looppipe = job->j_loop ? '|' : ' ';
	wchar_t *jobname = job->j_procs[0].pr_name;

	/* TRANSLATORS: the translated format string can be different 
	 * from the original only in the number of spaces. This is required
	 * for POSIX compliance. */
	fprintf(f, gt("[%zu] %c %5jd %-20s %c %ls\n"),
		jobnumber, current, (intmax_t) pid, status, looppipe, jobname);
	if (needfree)
	    free(status);

	for (size_t i = 1; i < job->j_pcount; i++) {
	    pid = job->j_procs[i].pr_pid;
	    status = get_process_status_string(&job->j_procs[i], &needfree);
	    jobname = job->j_procs[i].pr_name;

	    /* TRANSLATORS: the translated format string can be different 
	     * from the original only in the number of spaces. This is required
	     * for POSIX compliance. */
	    fprintf(f, gt("      %5jd %-20s | %ls\n"),
		    (intmax_t) pid, (posixly_correct ? "" : status), jobname);
	    if (needfree)
		free(status);
	}
    }
    job->j_statuschanged = false;
    if (job->j_status == JS_DONE)
	remove_job(jobnumber);
}

/* Returns the job number from the specified job ID string.
 * If no applicable job is found, zero is returned.
 * If more than one jobs are found, `joblist.length' is returned.
 * When `name' is of the form "%n", the number `n' is returned if it is a valid
 * job number.
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
	unsigned num;
	wchar_t *nameend;
	errno = 0;
	num = wcstoul(name, &nameend, 10);
	if (!errno && !*nameend)
	    return get_job(num) ? num : 0;
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
	if (job) {
	    wchar_t *jobname = get_job_name(job);
	    bool match = (contain ? wcsstr : matchwcsprefix)(jobname, name);
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
size_t get_jobnumber_from_pid(pid_t pid)
{
    size_t jobnumber;
    for (jobnumber = joblist.length; --jobnumber > 0; ) {
	job_T *job = joblist.contents[jobnumber];
	if (job) {
	    for (size_t i = 0; i < job->j_pcount; i++)
		if (job->j_procs[i].pr_pid == pid)
		    goto found;
	}
    }
found:
    return jobnumber;
}


/********** Builtins **********/

/* "jobs" builtin, which accepts the following options:
 * -l: be verbose
 * -n: print the jobs only whose status have changed
 * -p: print the process ID only
 * -r: print running jobs only
 * -s: print stopped jobs only
 * If `posixly_correct' is true, only -l and -p are available. */
int jobs_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"verbose",      xno_argument, L'l', },
	{ L"new",          xno_argument, L'n', },
	{ L"pid-only",     xno_argument, L'p', },
	{ L"running-only", xno_argument, L'r', },
	{ L"stopped-only", xno_argument, L's', },
	{ L"help",         xno_argument, L'-', },
	{ NULL, 0, 0, },
    };

    bool verbose = false, changedonly = false, pidonly = false;
    bool runningonly = false, stoppedonly = false;
    bool err = false;
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv,
		    posixly_correct ? L"lp" : L"lnprs",
		    long_options, NULL))) {
	switch (opt) {
	    case L'l':  verbose     = true;  break;
	    case L'n':  changedonly = true;  break;
	    case L'p':  pidonly     = true;  break;
	    case L'r':  runningonly = true;  break;
	    case L's':  stoppedonly = true;  break;
	    case L'-':
		print_builtin_help(ARGV(0));
		return EXIT_SUCCESS;
	    default:
		fprintf(stderr, gt(posixly_correct
			    ? Ngt("Usage:  jobs [-lp] [job...]\n")
			    : Ngt("Usage:  jobs [-lnprs] [job...]\n")));
		return EXIT_ERROR;
	}
    }

    if (xoptind < argc) {
	do {
	    const wchar_t *jobspec = ARGV(xoptind);
	    if (jobspec[0] == L'%') {
		jobspec++;
	    } else if (posixly_correct) {
		xerror(0, Ngt("%ls: %ls: invalid job specification"),
			ARGV(0), ARGV(xoptind));
		err = true;
		continue;
	    }
	    size_t jobnumber = get_jobnumber_from_name(jobspec);
	    if (jobnumber >= joblist.length) {
		xerror(0, Ngt("%ls: %ls: ambiguous job specification"),
			ARGV(0), ARGV(xoptind));
		err = true;
	    } else if (jobnumber == 0 || joblist.contents[jobnumber] == NULL) {
		xerror(0, Ngt("%ls: %ls: no such job"),
			ARGV(0), ARGV(xoptind));
		err = true;
	    } else {
		jobs_builtin_print_job(jobnumber, verbose, changedonly, pidonly,
			runningonly, stoppedonly);
	    }
	} while (++xoptind < argc);
    } else {
	/* print all jobs */
	for (size_t i = 1; i < joblist.length; i++) {
	    jobs_builtin_print_job(i, verbose, changedonly, pidonly,
		    runningonly, stoppedonly);
	}
    }

    return err ? EXIT_FAILURE1 : EXIT_SUCCESS;
}

/* Prints the job status */
void jobs_builtin_print_job(size_t jobnumber,
	bool verbose, bool changedonly, bool pidonly,
	bool runningonly, bool stoppedonly)
{
    job_T *job = get_job(jobnumber);

    if (job == NULL)
	return;
    if (runningonly && job->j_status != JS_RUNNING)
	return;
    if (stoppedonly && job->j_status != JS_STOPPED)
	return;

    if (pidonly) {
	if (changedonly && !job->j_statuschanged)
	    return;
	printf("%jd\n", imaxabs(job->j_pgid));
    } else {
	print_job_status(jobnumber, changedonly, verbose, stdout);
    }
}

const char jobs_help[] = Ngt(
"jobs - print info about jobs\n"
"\tjobs [-lnprs] [job...]\n"
"Prints the status of jobs in the current shell execution environment.\n"
"If the <job> is specified, the specified job is printed.\n"
"If none is specified, all jobs are printed.\n"
"Available options:\n"
" -l --verbose\n"
"\tprint info for each process in the job, including process ID\n"
" -n --new\n"
"\tprint jobs whose status have changed only\n"
" -p --pid-only\n"
"\tprint process group IDs only\n"
" -r --running-only\n"
"\tprint running jobs only\n"
" -s --stopped-only\n"
"\tprint stopped jobs only\n"
"In POSIXly correct mode, only the -l and -p options are available.\n"
);

/* "fg"/"bg" builtin */
int fg_builtin(int argc, void **argv)
{
    bool fg = wcscmp(argv[0], L"fg") == 0;
    bool err = false;
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"", help_option, NULL))) {
	switch (opt) {
	    case L'-':
		print_builtin_help(ARGV(0));
		return EXIT_SUCCESS;
	    default:
		fprintf(stderr, gt(fg ? Ngt("Usage:  fg [job]\n")
		                      : Ngt("Usage:  bg [job...]\n")));
		return EXIT_ERROR;
	}
    }

    if (!doing_job_control_now) {
	xerror(0, Ngt("%ls: job control disabled"), ARGV(0));
	return EXIT_FAILURE1;
    }

    int status = EXIT_SUCCESS;
    job_T *job;
    if (xoptind < argc) {
	do {
	    const wchar_t *jobspec = ARGV(xoptind);
	    if (jobspec[0] == L'%') {
		jobspec++;
	    } else if (posixly_correct) {
		xerror(0, Ngt("%ls: %ls: invalid job specification"),
			ARGV(0), ARGV(xoptind));
		err = true;
		continue;
	    }
	    size_t jobnumber = get_jobnumber_from_name(jobspec);
	    if (jobnumber >= joblist.length) {
		xerror(0, Ngt("%ls: %ls: ambiguous job specification"),
			ARGV(0), ARGV(xoptind));
		err = true;
	    } else if (jobnumber == 0
		    || (job = joblist.contents[jobnumber]) == NULL
		    || job->j_pgid < 0) {
		xerror(0, Ngt("%ls: %ls: no such job"),
			ARGV(0), ARGV(xoptind));
		err = true;
	    } else if (job->j_pgid == 0) {
		xerror(0, Ngt("%ls: %ls: not job-controlled job"),
			ARGV(0), ARGV(xoptind));
		err = true;
	    } else {
		set_current_jobnumber(jobnumber);
		status = continue_job(jobnumber, job, fg);
	    }
	} while (++xoptind < argc);
    } else {
	if (current_jobnumber == 0 ||
		(job = joblist.contents[current_jobnumber])->j_pgid <= 0) {
	    xerror(0, Ngt("%ls: no current job"), ARGV(0));
	    err = true;
	} else {
	    status = continue_job(current_jobnumber, job, fg);
	}
    }

    return (status != 0) ? status : err ? EXIT_FAILURE1 : EXIT_SUCCESS;
}

/* Continues execution of the specified job. */
int continue_job(size_t jobnumber, job_T *job, bool fg)
{
    wchar_t *name = get_job_name(job);
    if (fg && posixly_correct)
	printf("%ls\n", name);
    else
	printf("[%zu] %ls\n", jobnumber, name);
    if (name != job->j_procs[0].pr_name)
	free(name);

    if (job->j_status != JS_DONE) {
	if (fg)
	    put_foreground(job->j_pgid);
	send_sigcont_to_pgrp(job->j_pgid);
	job->j_status = JS_RUNNING;
    }

    if (fg)
	wait_for_job(jobnumber, true, false);
    int status = (job->j_status == JS_RUNNING) ? 0 : calc_status_of_job(job);
    if (job->j_status == JS_DONE)
	remove_job(jobnumber);
    return status;
}

const char fg_help[] = Ngt(
"fg - run jobs in the foreground\n"
"\tfg [job...]\n"
"Continues execution of the specified jobs in the foreground.\n"
"In POSIXly correct mode, you can specify at most one job. Otherwise, more\n"
"than one jobs can be specified, which are in turn continued.\n"
"If no job is specified, the current job is continued.\n"
);

const char bg_help[] = Ngt(
"bg - run jobs in the background\n"
"\tbg [job...]\n"
"Continues execution of the specified jobs in the background.\n"
"If no job is specified, the current job is continued.\n"
);

/* "wait" builtin */
int wait_builtin(int argc, void **argv)
{
    bool jobcontrol = doing_job_control_now;
    bool err = false;
    int status = EXIT_SUCCESS;
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"", help_option, NULL))) {
	switch (opt) {
	    case L'-':
		print_builtin_help(ARGV(0));
		return EXIT_SUCCESS;
	    default:
		fprintf(stderr, gt("Usage:  wait [job or pid...]\n"));
		return EXIT_ERROR;
	}
    }

    job_T *job;
    if (xoptind < argc) {
	do {
	    const wchar_t *jobspec = ARGV(xoptind);
	    size_t jobnumber;
	    if (jobspec[0] == L'%') {
		jobnumber = get_jobnumber_from_name(jobspec + 1);
	    } else {
		pid_t pid;
		wchar_t *end;
		errno = 0;
		pid = wcstol(jobspec, &end, 10);
		if (errno || *end || pid < 0) {
		    xerror(0, Ngt("%ls: %ls: invalid job specification"),
			    ARGV(0), jobspec);
		    err = true;
		    continue;
		}
		jobnumber = pid ? get_jobnumber_from_pid(pid) : 0;
	    }
	    if (jobnumber >= joblist.length) {
		xerror(0, Ngt("%ls: %ls: ambiguous job specification"),
			ARGV(0), ARGV(xoptind));
		err = true;
	    } else if (jobnumber == 0
		    || (job = joblist.contents[jobnumber]) == NULL
		    || job->j_pgid < 0) {
		status = EXIT_NOTFOUND;
	    } else {
		if (wait_for_job(jobnumber, jobcontrol, jobcontrol)) {
		    status = calc_status_of_job(job);
		} else {
		    status = TERMSIGOFFSET + SIGINT;
		    break;
		}
		if (!jobcontrol && job->j_status == JS_DONE)
		    remove_job(jobnumber);
	    }
	} while (++xoptind < argc);
    } else {
	/* wait for all jobs */
	for (size_t i = 1; i < joblist.length; i++) {
	    job = joblist.contents[i];
	    if (job != NULL && job->j_pgid >= 0) {
		if (!wait_for_job(i, jobcontrol, jobcontrol)) {
		    status = TERMSIGOFFSET + SIGINT;
		    break;
		}
		if (!jobcontrol && job->j_status == JS_DONE)
		    remove_job(i);
	    }
	}
    }

    return status ? status : err ? EXIT_FAILURE1 : EXIT_SUCCESS;
}

const char wait_help[] = Ngt(
"wait - wait for jobs to terminate\n"
"\twait [job or pid...]\n"
"Waits for the specified jobs, or all jobs if none specified, to terminate.\n"
"Jobs can be specified in the usual job specification form such as \"%2\" or\n"
"by the process ID of a process belonging to the job.\n"
);

/* "disown" builtin, which accepts the following option:
 * -a: disown all jobs */
int disown_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"all",  xno_argument, L'a', },
	{ L"help", xno_argument, L'-', },
	{ NULL, 0, 0, },
    };

    bool all = false;
    bool err = false;
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"a", long_options, NULL))) {
	switch (opt) {
	    case L'a':  all = true;  break;
	    case L'-':
		print_builtin_help(ARGV(0));
		return EXIT_SUCCESS;
	    default:
		fprintf(stderr, gt("Usage:  disown [job...]\n"));
		return EXIT_ERROR;
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
		xerror(0, Ngt("%ls: %ls: invalid job specification"),
			ARGV(0), ARGV(xoptind));
		err = true;
		continue;
	    }
	    size_t jobnumber = get_jobnumber_from_name(jobspec);
	    if (jobnumber >= joblist.length) {
		xerror(0, Ngt("%ls: %ls: ambiguous job specification"),
			ARGV(0), ARGV(xoptind));
		err = true;
	    } else if (jobnumber == 0 || joblist.contents[jobnumber] == NULL) {
		xerror(0, Ngt("%ls: %ls: no such job"),
			ARGV(0), ARGV(xoptind));
		err = true;
	    } else {
		remove_job(jobnumber);
	    }
	} while (++xoptind < argc);
    } else {
	if (current_jobnumber == 0 || get_job(current_jobnumber) == NULL) {
	    xerror(0, Ngt("%ls: no current job"), ARGV(0));
	    err = true;
	} else {
	    remove_job(current_jobnumber);
	}
    }

    return err ? EXIT_FAILURE1 : EXIT_SUCCESS;
}

const char disown_help[] = Ngt(
"disown - disown jobs\n"
"\tdisown [-a|job...]\n"
"Removes the specified jobs from the job list.\n"
"The status of the disowned jobs is no longer reported and the jobs can no\n"
"longer be put back in the foreground.\n"
"If the -a (--all) option is specified, all existing jobs are disowned.\n"
"Otherwise, the specified jobs are disowned. If none is specified, the\n"
"current job is disowned.\n"
);


/* vim: set ts=8 sts=4 sw=4 noet: */
