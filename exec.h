/* Yash: yet another shell */
/* exec.h: interface to command execution and job control */
/* © 2007 magicant */

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


#ifndef EXEC_H
#define EXEC_H

#include <signal.h>
#include <sys/types.h>
#include "parser.h"


extern bool huponexit;
extern int lastwaitstatus, laststatus;

/* 設定したリダイレクトを後で元に戻すためのデータ */
struct save_redirect {
	struct save_redirect *next;
	int   sr_origfd; /* 元のファイルディスクリプタ */
	int   sr_copyfd; /* 新しくコピーしたファイルディスクリプタ */
	FILE *sr_file;   /* 元のストリーム */
};

enum jstatus { JS_NULL, JS_RUNNING, JS_DONE, JS_STOPPED, };
extern const char * const jstatusstr[];
/* jstatusstr のインデックスとして jstatus の値を使用している */

#define MAX_JOB 10000

/* ジョブの情報 */
typedef struct {
	pid_t  j_pgid;           /* プロセスグループの ID */
	pid_t *j_pids;           /* 子プロセスの ID の配列 */
	enum jstatus j_status;   /* 最後に確認したジョブの状態 */
	bool   j_statuschanged;  /* まだ状態変化を報告していないなら true */
	enum {
		JF_NOHUP = 1
	}      j_flags;
	int    j_exitstatus;     /* ジョブの終了ステータス */
	bool   j_exitcodeneg;    /* ジョブの exitcode を反転するかどうか */
	char  *j_name;           /* 表示用ジョブ名 */
} JOB;
/* j_pids は 0 終端である。終了したプロセスは値が反数になる。 */

extern struct plist joblist;
extern size_t currentjobnumber;

void init_exec(void);
int exitcode_from_status(int status);
JOB *get_job(size_t jobnumber);
unsigned job_count(void);
bool remove_job(size_t jobnumber);
void print_job_status(size_t jobnumber, bool changedonly, bool printpids);
void print_all_job_status(bool changedonly, bool printpids);
int get_jobnumber_from_pid(pid_t pid);
void wait_chld(void);
void wait_for_signal(void);
void send_sighup_to_all_jobs(void);
void exec_statements(STATEMENT *statements);
void exec_statements_and_exit(STATEMENT *statements)
	__attribute__((noreturn));


#endif /* EXEC_H */
