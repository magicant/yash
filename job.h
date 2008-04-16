/* Yash: yet another shell */
/* job.h: job control */
/* © 2007-2008 magicant */

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


#ifndef JOB_H
#define JOB_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>


/* ジョブ／プロセスの状態 */
typedef enum jobstatus_T {
    JS_RUNNING, JS_STOPPED, JS_DONE,
} jobstatus_T;

/* ジョブを構成するプロセスの情報 */
typedef struct process_T {
    pid_t             pr_pid;          /* プロセス ID */
    enum jobstatus_T  pr_status;       /* プロセスの状態 */
    int               pr_statuscode;
    wchar_t          *pr_name;         /* 表示用のプロセス名 */
} process_T;
/* pr_pid が 0 のとき、そのプロセスは fork せずに実行できたことを示す。このとき
 * そのコマンドの終了コードが pr_statuscode に入る。
 * pr_pid が正数のとき、それはプロセス ID である。このときの pr_statuscode は
 * waitpid が返した status の値である。 */

/* ジョブの情報 */
typedef struct job_T {
    pid_t             j_pgid;          /* プロセスグループ ID */
    enum jobstatus_T  j_status;        /* ジョブの状態 */
    bool              j_statuschanged; /* 状態変化を報告していないなら true */
    bool              j_loop;          /* ループパイプかどうか */
    size_t            j_pcount;        /* j_procs の要素数 */
    struct process_T  j_procs[];       /* 各プロセスの情報 */
} job_T;
/* ジョブ制御無効時、ジョブのプロセスグループ ID はシェルと同じになるので、
 * j_pgid は 0 となる。 */


/* アクティブジョブのジョブ番号 */
#define ACTIVE_JOBNO 0

/* プロセスがシグナルによって終了した場合は、シグナル番号に TERMSIGOFFSET を
 * 加えた値がそのプロセスの終了ステータスになる。
 * bash/zsh/dash ではこの値は 128、ksh では 256 である。 */
#define TERMSIGOFFSET 384

extern void init_job(void);

extern void set_active_job(job_T *job)
    __attribute__((nonnull));
extern void add_job(void);
extern void remove_job(size_t jobnumber);
extern void remove_all_jobs(void);

extern void do_wait(void);
extern void wait_for_job(size_t jobnumber, bool return_on_stop);

extern int calc_status_of_job(const job_T *job)
    __attribute__((pure,nonnull));


#endif /* JOB_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
