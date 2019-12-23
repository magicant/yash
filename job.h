/* Yash: yet another shell */
/* job.h: job control */
/* (C) 2007-2017 magicant */

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


#ifndef YASH_JOB_H
#define YASH_JOB_H

#include <stddef.h>
#include <sys/types.h>
#include "xgetopt.h"


/* status of job/process */
typedef enum jobstatus_T {
    JS_RUNNING, JS_STOPPED, JS_DONE,
} jobstatus_T;

/* info about a process in a job */
typedef struct process_T {
    pid_t        pr_pid;          /* process ID */
    jobstatus_T  pr_status;
    int          pr_statuscode;
    wchar_t     *pr_name;         /* process name made from command line */
} process_T;
/* If `pr_pid' is 0, the process was finished without `fork'ing from the shell.
 * In this case, `pr_status' is JS_DONE and `pr_statuscode' is the exit status.
 * If `pr_pid' is a positive number, it's the process ID. In this case,
 * `pr_statuscode' is the status code returned by `waitpid'. */

/* info about a job */
typedef struct job_T {
    pid_t       j_pgid;          /* process group ID */
    jobstatus_T j_status;
    _Bool       j_statuschanged; /* job's status not yet reported? */
    _Bool       j_legacy;        /* not a true child of the shell? */
    _Bool       j_nonotify;      /* suppress printing job status? */
    size_t      j_pcount;        /* # of processes in `j_procs' */
    process_T   j_procs[];       /* info about processes */
} job_T;
/* When job control is off, `j_pgid' is 0 since the job shares the process group
 * ID with the shell.
 * In subshells, the `j_legacy' flag is set to indicate that the job is not
 * a direct child of the current shell process. */


/* job number of the active job */
#define ACTIVE_JOBNO 0

/* When a process is stopped/terminated by a signal, this value is added to the
 * signal number to make the value of the exit status.
 * 128 in bash/zsh/dash/pdksh/mksh/posh, 256 in ksh. */
#ifndef TERMSIGOFFSET
#define TERMSIGOFFSET 384
#endif

extern void init_job(void);

extern void set_active_job(job_T *job)
    __attribute__((nonnull));
extern void add_job(_Bool current);
extern void remove_job(size_t jobnumber);
extern void remove_job_nofitying_signal(size_t jobnumber);
extern void remove_all_jobs(void);
extern void neglect_all_jobs(void);
extern size_t job_count(void)
    __attribute__((pure));
extern size_t stopped_job_count(void)
    __attribute__((pure));

extern void do_wait(void);
extern int wait_for_job(size_t jobnumber, _Bool return_on_stop,
	_Bool interruptible, _Bool return_on_trap);
extern wchar_t **wait_for_child(pid_t cpid, pid_t cpgid, _Bool return_on_stop);
extern pid_t get_job_pgid(const wchar_t *jobname)
    __attribute__((pure));

extern void put_foreground(pid_t pgrp);
extern void ensure_foreground(void);

extern int calc_status_of_job(const job_T *job)
    __attribute__((pure,nonnull));

extern _Bool any_job_status_has_changed(void)
    __attribute__((pure));
extern void print_job_status_all(void);
extern void notify_signaled_job(size_t jobnumber);

extern int jobs_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char jobs_help[], jobs_syntax[];
#endif
extern const struct xgetopt_T jobs_options[];

extern int fg_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char fg_help[], fg_syntax[], bg_help[], bg_syntax[];
#endif

extern int wait_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char wait_help[], wait_syntax[];
#endif

extern int disown_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char disown_help[], disown_syntax[];
#endif


#endif /* YASH_JOB_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
