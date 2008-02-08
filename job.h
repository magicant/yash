/* Yash: yet another shell */
/* job.h: interface to job control */
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


extern bool huponexit;

/* プロセスがシグナルによって終了した場合は、シグナル番号に TERMSIGOFFSET を
 * 加えたものがそのプロセスの終了コードになる。
 * bash/zsh では 128、ksh では 256。 */
#define TERMSIGOFFSET 384

enum jstatus { JS_RUNNING, JS_DONE, JS_STOPPED, };
extern const char *const jstatusstr[];
/* jstatusstr のインデックスとして jstatus の値を使用している */

#define MAX_JOB 10000

/* ジョブの情報 */
typedef struct {
	pid_t  j_pgid;           /* プロセスグループの ID */
	enum jstatus j_status;   /* 最後に確認したジョブの状態 */
	bool   j_statuschanged;  /* まだ状態変化を報告していないなら true */
	size_t j_procc;          /* 子プロセスの数 */
	struct jproc {
		pid_t        jp_pid;
		enum jstatus jp_status;
		int          jp_waitstatus;
	}     *j_procv;          /* 各子プロセスの情報の配列へのポインタ */
	enum {
		JF_NOHUP = 1
	}      j_flags;
	int    j_waitstatus;     /* ジョブの終了ステータス */
	char  *j_name;           /* 表示用ジョブ名 */
} JOB;

extern struct plist joblist;
extern size_t currentjobnumber;
extern struct jproc temp_chld;

extern pid_t last_bg_pid;

void joblist_reinit(void);
void init_jobcontrol(void);
void finalize_jobcontrol(void);
int exitcode_from_status(int status);
JOB *get_job(size_t jobnumber);
unsigned job_count(void);
unsigned running_job_count(void);
unsigned stopped_job_count(void);
unsigned undone_job_count(void);
int add_job(void);
bool remove_job(size_t jobnumber);
void remove_exited_jobs(void);
void print_job_status(size_t jobnumber, bool changedonly, bool printpids);
void print_all_job_status(bool changedonly, bool printpids);
int get_jobnumber_from_pid(pid_t pid);
void wait_chld(void);
void send_sighup_to_all_jobs(void);


#endif /* JOB_H */
