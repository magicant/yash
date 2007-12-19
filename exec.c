/* Yash: yet another shell */
/* exec.c: command execution and job control */
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


#define _GNU_SOURCE
#include <error.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "yash.h"
#include "util.h"
#include "expand.h"
#include "exec.h"
#include "path.h"
#include "builtin.h"
#include <assert.h>


/* パイプの集合 */
typedef struct {
	size_t p_count;     /* パイプの fd ペアの数 */
	int (*p_pipes)[2];  /* パイプの fd ペアの配列へのポインタ */
} PIPES;

/* コマンドをどのように実行するか */
typedef enum {
	FOREGROUND,         /* フォアグラウンドで実行 */
	BACKGROUND,         /* バックグラウンドで実行 */
	SELF,               /* このシェル自身を exec して実行 */
} exec_t;

void init_exec(void);
static void joblist_reinit(void);
int exitcode_from_status(int status);
JOB *get_job(size_t jobnumber);
unsigned job_count(void);
static int add_job(void);
bool remove_job(size_t jobnumber);
void print_job_status(size_t jobnumber, bool changedonly, bool printpids);
void print_all_job_status(bool changedonly, bool printpids);
int get_jobnumber_from_pid(pid_t pid);
void wait_all(int blockedjob);
void send_sighup_to_all_jobs(void);
void sig_hup(int signal);
void sig_chld(int signal);
static int (*create_pipes(size_t count))[2];
static void close_pipes(PIPES pipes);
void exec_statements(STATEMENT *statements);
void exec_statements_and_exit(STATEMENT *statements)
	__attribute__((noreturn));
static void exec_pipelines(PIPELINE *pipelines);
static void exec_pipelines_and_exit(PIPELINE *pipelines)
	__attribute__((noreturn));
static void exec_processes(PROCESS *processes, const char *jobname,
		bool neg_status, bool pipe_loop, bool background);
static pid_t exec_single(
		PROCESS *p, ssize_t pindex, pid_t pgid, exec_t etype, PIPES pipes);
static bool open_redirections(REDIR *redirs, struct save_redirect **save);
static void undo_redirections(struct save_redirect *save);
static void savesfree(struct save_redirect *save);

/* 最後に実行したコマンドの終了コード */
int laststatus = 0;

/* true なら終了時に未了のジョブに SIGHUP を送る */
bool huponexit = false;

/* wait_all 中のシグナルハンドラでこれを非 0 にすると wait を止める */
volatile bool cancel_wait = false;

/* ジョブのリスト。リストの要素は JOB へのポインタ。
 * リストの中の「空き」は、NULL ポインタによって示す。 */
/* これらの値はシェルの起動時に初期化される */
/* ジョブ番号が 0 のジョブはアクティブジョブ (論理的にはまだジョブリストにない
 * ジョブ) である。 */
struct plist joblist;

/* カレントジョブのジョブ番号 (joblist 内でのインデックス)
 * この値が存在しないジョブのインデックス (0 を含む) となっているとき、
 * カレントジョブは存在しない。 */
size_t currentjobnumber = 0;

/* enum jstatus の文字列表現 */
const char * const jstatusstr[] = {
	"NULL", "Running", "Done", "Stopped",
};

/* 実行環境を初期化する */
void init_exec(void)
{
	plist_init(&joblist);
	plist_append(&joblist, NULL);
}

/* joblist を再初期化する */
static void joblist_reinit(void)
{
	for (size_t i = 0; i < joblist.length; i++)
		remove_job(i);
	/*
	plist_clear(&joblist);
	plist_append(&joblist, NULL);
	*/
}

/* waitpid が返す status から終了コードを得る。
 * 戻り値: status がプロセスの終了を表していない場合は -1。 */
int exitcode_from_status(int status)
{
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		return WTERMSIG(status) + 128;
	else
		return -1;
}

/* 指定したジョブ番号のジョブを取得する。ジョブがなければ NULL を返す。 */
JOB *get_job(size_t jobnumber)
{
	return jobnumber < joblist.length ? joblist.contents[jobnumber] : NULL;
}

/* 現在のジョブリスト内のジョブの個数を数える (アクティブジョブを除く) */
unsigned job_count(void)
{
	unsigned result = 0;

	assert(joblist.length > 0);
	for (size_t index = joblist.length; --index > 0; )
		if (joblist.contents[index])
			result++;
	return result;
}

/* アクティブジョブ (ジョブ番号 0 のジョブ) をジョブリストに移動し、
 * そのジョブをカレントジョブにする。
 * 戻り値: 成功したら新しいジョブ番号 (>0)、失敗したら -1。*/
/* この関数は内部で SIGHUP/SIGCHLD をブロックする */
static int add_job(void)
{
	size_t jobnumber;
	JOB *job;
	sigset_t sigset, oldsigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGHUP);
	sigaddset(&sigset, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &sigset, &oldsigset) < 0)
		error(EXIT_FAILURE, errno, "sigprocmask");

	if (!(job = get_job(0))) {
		jobnumber = -1;
		goto end;
	}
	assert(job->j_status > JS_NULL && job->j_status <= JS_STOPPED);

	/* リストの空いているインデックスを探す */
	for (jobnumber = 1; jobnumber < joblist.length; jobnumber++)
		if (!joblist.contents[jobnumber])
			break;
	if (jobnumber == joblist.length) {  /* 空きがなかったらリストを大きくする */
		if (joblist.length == MAX_JOB) {
			error(0, 0, "too many jobs");
			jobnumber = -1;
			goto end;
		} else {
			plist_append(&joblist, job);
		}
	} else {  /* 空きがあったらそこに入れる */
		assert(!joblist.contents[jobnumber]);
		joblist.contents[jobnumber] = job;
	}
	joblist.contents[0] = NULL;
	currentjobnumber = jobnumber;

end:
	if (sigprocmask(SIG_SETMASK, &oldsigset, NULL) < 0)
		error(EXIT_FAILURE, errno, "sigprocmask");
	return jobnumber;
}

/* 指定した番号のジョブを削除する。
 * 戻り値: true なら削除成功。false はもともとジョブがなかった場合。 */
bool remove_job(size_t jobnumber)
{
	JOB *job = get_job(jobnumber);
	if (job) {
		joblist.contents[jobnumber] = NULL;
		free(job->j_pids);
		free(job->j_name);
		free(job);
		return true;
	} else {
		return false;
	}
}

/* ジョブの状態を表示する。
 * 終了したジョブ (j_status == JS_DONE) は、削除する。
 * jobnumber:   ジョブ番号。joblist のインデックス。
 *              使用されていないジョブ番号を指定した場合、何もしない。
 * changedonly: true なら変化があった場合だけ表示する。
 * printpids:   true なら PGID のみならず各子プロセスの PID も表示する */
/* この関数は内部で SIGHUP/SIGCHLD をブロックする */
void print_job_status(size_t jobnumber, bool changedonly, bool printpids)
{
	JOB *job;
	sigset_t sigset, oldsigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGHUP);
	sigaddset(&sigset, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &sigset, &oldsigset) < 0)
		error(EXIT_FAILURE, errno, "sigprocmask");

	if (!(job = get_job(jobnumber)))
		goto end;

	if (!changedonly || job->j_statuschanged) {
		int estatus = job->j_exitstatus;
		if (job->j_status == JS_DONE) {
			if (WIFEXITED(estatus) && WEXITSTATUS(estatus))
				printf("[%zu]%c %5d  Exit %-3d    %s\n",
						jobnumber, currentjobnumber == jobnumber ? '+' : ' ',
						(int) job->j_pgid, WEXITSTATUS(estatus),
						job->j_name ? : "<< unknown job >>");
			else if (WIFSIGNALED(job->j_exitstatus))
				printf("[%zu]%c %5d  %-8s    %s\n",
						jobnumber, currentjobnumber == jobnumber ? '+' : ' ',
						(int) job->j_pgid, strsignal(WTERMSIG(estatus)),
						job->j_name ? : "<< unknown job >>");
			else
				goto normal;
		} else {
			bool iscurrent, isbg;
normal:
			iscurrent = (currentjobnumber == jobnumber);
			isbg = (job->j_status == JS_RUNNING);
			printf("[%zu]%c %5d  %-8s    %s%s\n",
					jobnumber,
					iscurrent ? '+' : ' ',
					(int) job->j_pgid, jstatusstr[job->j_status],
					job->j_name ? : "<< unknown job >>",
					isbg ? " &" : "");
		}
		if (printpids) {
			pid_t *pidp = job->j_pids;
			printf("\tPID: %ld", (long) *pidp);
			while (*++pidp)
				printf(", %ld", (long) *pidp);
			printf("\n");
		}
		job->j_statuschanged = false;
	}
	if (job->j_status == JS_DONE)
		remove_job(jobnumber);

end:
	if (sigprocmask(SIG_SETMASK, &oldsigset, NULL) < 0)
		error(EXIT_FAILURE, errno, "sigprocmask");
}

/* (アクティブジョブを除く) 全てのジョブの状態を画面に出力する。
 * changedonly: true なら変化があったものだけ表示する。
 * printpids:   true なら PGID のみならず各子プロセスの PID も表示する */
void print_all_job_status(bool changedonly, bool printpids)
{
	for (size_t i = 1; i < joblist.length; i++)
		print_job_status(i, changedonly, printpids);
}

/* 子プロセスの ID からジョブ番号を得る。
 * 戻り値: ジョブ番号 (>= 0) または -1 (見付からなかった場合)。
 *         子プロセスが既に終了している場合、見付からない。 */
int get_jobnumber_from_pid(pid_t pid)
{
	JOB *job;

	for (unsigned i = 0; i < joblist.length; i++)
		if ((job = get_job(i)))
			for (pid_t *pids = job->j_pids; *pids; pids++)
				if (*pids == pid)
					return i;
	return -1;
}

/* 全ての子プロセスを対象に wait する。
 * Wait するだけでジョブの状態変化の報告はしない。
 * blockedjob: 0 以上ならその番号のジョブが終了または停止するまでブロックする。
 *         -1 なら全てのジョブが終了または停止するまでブロックする。
 *         -2 以下ならブロックしない。その番号のジョブがない時もブロックしない。
 * ただし、対話的シェルでは終了または停止するまでブロックするが、
 * 非対話的シェルでは終了するまでブロックする。
 * blockedjob で指定したジョブが停止した場合、対話的シェルなら改行を出力する。
 * blockedjob で指定したジョブが終了した場合、j_statuschanged は false になり、
 * 終了の原因が INT/PIPE 以外のシグナルならそれを報告する。
 * アクティブジョブが停止した場合、対話的シェルなら add_job を行う。
 * アクティブジョブが終了した場合、アクティブジョブを削除する。
 * wait 中のシグナルハンドラ実行後 (waitpid が EINTR を返した後)、
 * cancel_wait の値が true なら (値を false に戻して) 直ちに返る。 */
/* この関数は内部で SIGCHLD をブロックする。
 * この関数をシグナルハンドラから呼出すときは必ず blockedjob を -2 にすること。
 * */
void wait_all(int blockedjob)
{
	int waitpidopt;
	int status;
	pid_t pid;
	sigset_t sigset, oldsigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &sigset, &oldsigset) < 0)
		error(EXIT_FAILURE, errno, "sigprocmask before wait");
	
	if (blockedjob >= 0 && !get_job(blockedjob))
		blockedjob = -2;
start:
	if (cancel_wait) {
		cancel_wait = false;
		goto end;
	}

	/* 全ジョブが終了したかどうか調べる。 */
	if (blockedjob == -1) {
		for (size_t i = 0; i < joblist.length; i++) {
			JOB *job;
			if ((job = joblist.contents[i])) {
				switch (job->j_status) {
					case JS_STOPPED:
						if (!is_interactive)
							goto startwaiting;
						/* falls thru! */
					case JS_DONE:
						break;
					default:
						goto startwaiting;
				}
			}
		}
		blockedjob = -2;
	}

startwaiting:
	waitpidopt = WSTOPPED | WCONTINUED | (blockedjob >= -1 ? 0 : WNOHANG);
	pid = waitpid(-1 /* any child process */, &status, waitpidopt);
	if (pid < 0) {
		if (errno == EINTR)
			goto start;
		if (errno != ECHILD)
			error(0, errno, "waitpid");
		goto end;
	} else if (pid == 0) {  /* non-blocking && no status change */
		goto end;
	}

	size_t jobnumber;
	JOB *job;
	pid_t *pidp;
	enum jstatus oldstatus;

	/* jobnumber と、JOB の j_pids 内の PID の位置を探す */
	for (jobnumber = 0; jobnumber < joblist.length; jobnumber++)
		if ((job = joblist.contents[jobnumber]))
			for (pidp = job->j_pids; *pidp; pidp++)
				if (pid == *pidp)
					goto outofloop;
	/*error(0, 0, "unexpected waitpid result: %ld", (long) pid);*/
	goto start;

outofloop:
	oldstatus = job->j_status;
	if (WIFEXITED(status) || WIFSIGNALED(status)) {
		*pidp = -pid;
		if (!pidp[1])  /* この子プロセスがパイプの最後のプロセスの場合 */
			job->j_exitstatus = status;
	} else if (WIFSTOPPED(status)) {
		job->j_status = JS_STOPPED;
	} else if (WIFCONTINUED(status)) {
		job->j_status = JS_RUNNING;
	}

	/* ジョブの全プロセスが終了したかどうか調べる */
	for (pidp = job->j_pids; *pidp; pidp++)
		if (*pidp > 0)
			goto stillrunning;
	job->j_status = JS_DONE;
stillrunning:
	if (job->j_status != oldstatus)
		job->j_statuschanged = true;

	/* ブロック対象のジョブが変化した場合 */
	if ((int) jobnumber == blockedjob) {
		switch (job->j_status) {
			case JS_NULL:
			default:
				assert(false);
			case JS_RUNNING:
				break;
			case JS_STOPPED:
				if (is_interactive) {
					printf("\n");
					blockedjob = -2;
				}
				break;
			case JS_DONE:
				job->j_statuschanged = false;
				if (WIFSIGNALED(status)) {
					int signal = WTERMSIG(status);
					if (signal != SIGINT && signal != SIGPIPE)
						psignal(signal, NULL);
				}
				blockedjob = -2;
				break;
		}
	}

	/* アクティブジョブが変化した場合 */
	if (jobnumber == 0) {
		switch (job->j_status) {
			default:
			case JS_NULL:
				assert(false);
			case JS_RUNNING:
				break;
			case JS_DONE:
				laststatus = exitcode_from_status(job->j_exitstatus);
				if (job->j_exitcodeneg)
					laststatus = !laststatus;
				remove_job(jobnumber);
				break;
			case JS_STOPPED:
				if (is_interactive) {
					laststatus = exitcode_from_status(job->j_exitstatus);
					add_job();
				}
				break;
		}
	}

	goto start;

end:
	if (sigprocmask(SIG_SETMASK, &oldsigset, NULL) < 0)
		error(EXIT_FAILURE, errno, "sigprocmask after wait");
}

/* Nohup を設定していない全てのジョブに SIGHUP を送る。
 * 停止しているジョブには SIGCONT も送る。 */
void send_sighup_to_all_jobs(void)
{
	size_t i;

	if (is_interactive) {
		for (i = 0; i < joblist.length; i++) {
			JOB *job = joblist.contents[i];

			if (job && !(job->j_flags & JF_NOHUP)) {
				killpg(job->j_pgid, SIGHUP);
				if (job->j_status == JS_STOPPED)
					killpg(job->j_pgid, SIGCONT);
			}
		}
	} else {
		killpg(0, SIGHUP);
	}
}

/* SIGHUP シグナルのハンドラ。
 * 全てのジョブに SIGHUP を送ってから自分にもう一度 SIGHUP を送って自滅する。
 * (このハンドラは一度シグナルを受け取ると無効になるようになっている) */
void sig_hup(int signal __UNUSED__)
{
	send_sighup_to_all_jobs();
	finalize_readline();
	raise(SIGHUP);
}

/* count 個のパイプのペアを作り、その配列へのポインタを返す。
 * count が 0 なら何もせずに NULL を返す。
 * エラー時も NULL を返す。 */
static int (*create_pipes(size_t count))[2]
{
	size_t i, j;
	int (*pipes)[2];

	if (!count)
		return NULL;
	pipes = xmalloc(count * sizeof(int[2]));
	for (i = 0; i < count; i++) {
		if (pipe(pipes[i]) < 0) {
			error(0, errno, "pipe");
			goto failed;
		}
	}
	return pipes;

failed:
	for (j = 0; j < i; j++) {
		if (close(pipes[i][0]) < 0)
			error(0, errno, "pipe close");
		if (close(pipes[i][1]) < 0)
			error(0, errno, "pipe close");
	}
	free(pipes);
	return NULL;
}

/* 引数の配列に含まれるパイプを閉じ、配列を解放する。
 * pipes: パイプのペアの配列。NULL なら何もしない。
 * count: パイプのペアの個数 */
static void close_pipes(PIPES pipes)
{
	size_t i;

	if (!pipes.p_pipes)
		return;
	for (i = 0; i < pipes.p_count; i++) {
		if (close(pipes.p_pipes[i][0]) < 0)
			error(0, errno, "pipe close");
		if (close(pipes.p_pipes[i][1]) < 0)
			error(0, errno, "pipe close");
	}
	free(pipes.p_pipes);
}

/* コマンド入力全体を受け取って、全コマンドを実行する。 */
void exec_statements(STATEMENT *s)
{
	while (s) {
		if (!s->s_bg) {
			exec_pipelines(s->s_pipeline);
		} else {
			PIPELINE *p = s->s_pipeline;
			if (p && !p->next) {
				char *name = make_pipeline_name(
						p->pl_proc, p->pl_neg, p->pl_loop);
				exec_processes(p->pl_proc, name, p->pl_neg, p->pl_loop, true);
				free(name);
			} else {
				PROCESS proc = {
					.next = NULL,
					.p_type = PT_X_PIPE,
					.p_args = NULL,
					.p_subcmds = s,
				};
				char *name = make_statement_name(p);
				exec_processes(&proc, name, false, false, true);
				free(name);
			}
		}
		s = s->next;
	}
}

/* コマンド入力全体を受け取って、全コマンドを実行し、そのまま終了する。 */
void exec_statements_and_exit(STATEMENT *s)
{
	if (s && !s->next && !s->s_bg)
		exec_pipelines_and_exit(s->s_pipeline);

	exec_statements(s);
	exit(laststatus);
}

/* 一つの文の各パイプラインを実行する。 */
static void exec_pipelines(PIPELINE *p)
{
	if (!p) return;

	bool nextcond = true;
	laststatus = 0;
	while (p && (nextcond == !laststatus)) {
		char *name = make_pipeline_name(p->pl_proc, p->pl_neg, p->pl_loop);
		exec_processes(p->pl_proc, name, p->pl_neg, p->pl_loop, false);
		free(name);
		nextcond = p->pl_next_cond;
		p = p->next;
	}
}

/* 一つの文の各パイプラインを実行し、そのまま終了する。 */
static void exec_pipelines_and_exit(PIPELINE *p)
{
	if (p && !p->next && !p->pl_neg && !p->pl_loop) {
		PROCESS *proc = p->pl_proc;
		if (!proc->next) switch (proc->p_type) {
			case PT_NORMAL:
				exec_single(proc, 0, 0, SELF,
						(PIPES) { .p_count = 0, .p_pipes = NULL, });
				assert(false);
			case PT_GROUP:  case PT_SUBSHELL:
				exec_statements_and_exit(proc->p_subcmds);
			case PT_X_PIPE:
				exec_pipelines_and_exit(proc->p_subcmds->s_pipeline);
		}
	}

	exec_pipelines(p);
	exit(laststatus);
}

/* 一つのパイプラインを実行し、wait する。
 * name はパイプラインのジョブ名。
 * neg ならばパイプラインの終了コードを反転。
 * loop ならば環状のパイプラインを作成。
 * bg ならばバックグラウンドでジョブを実行。
 * すなわち wait せずに新しいジョブを追加して戻る。
 * bg でなければフォアグラウンドでジョブを実行。
 * すなわち wait し、停止した場合のみ新しいジョブを追加して戻る。 */
static void exec_processes(
		PROCESS *p, const char *name, bool neg, bool loop, bool bg)
{
	size_t pcount;
	pid_t pgid;
	pid_t *pids;
	PIPES pipes;

	/* パイプライン内のプロセス数を数える */
	pcount = 0;
	for (PROCESS *pp = p; pp; pcount++, pp = pp->next);

	/* 必要な数のパイプを作成する */
	pipes.p_count = loop ? pcount : pcount - 1;
	pipes.p_pipes = create_pipes(pipes.p_count);
	if (pipes.p_count > 0 && !pipes.p_pipes) {
		laststatus = 2;
		return;
	}

	pids = xcalloc(pcount + 1, sizeof(pid_t));
	pgid = pids[0] = exec_single(p, loop ? -1 : 0, 0, bg, pipes);
	if (pgid >= 0)
		for (size_t i = 1; i < pcount; i++)
			pids[i] = exec_single(p = p->next, i, pgid, bg, pipes);
	else
		laststatus = EXIT_FAILURE;
	close_pipes(pipes);
	if (pgid > 0) {
		JOB *job = joblist.contents[0];
		assert(job);
		job->j_pids = pids;
		job->j_status = JS_RUNNING;
		job->j_statuschanged = true;
		job->j_flags = 0;
		job->j_exitstatus = 0;
		job->j_exitcodeneg = neg;
		job->j_name = xstrdup(name);
		if (!bg) {
			wait_all(0);
		} else {
			add_job();
		}
	}
}

/* 一つのコマンドを実行する。内部で処理できる組込みコマンドでなければ
 * fork/exec し、リダイレクトなどを設定する。
 * pgid = 0 で fork した場合、親プロセスでは新しくアクティブジョブを
 * joblist.contents[0] に作成し、j_pgid メンバの値を設定する。
 * p:       実行するコマンド
 * pindex:  パイプ全体における子プロセスのインデックス。
 *          環状パイプを作る場合は 0 の代わりに -1。
 * pgid:    子プロセスに設定するプロセスグループ ID。
 *          子プロセスのプロセス ID をそのままプロセスグループ ID にする時は 0。
 *          サブシェルではプロセスグループを設定しないので pgid は無視。
 * etype:   このプロセスの実行方式。
 *          FOREGROUND では、組込みコマンドなどは fork しないことがある。
 *          SELF は、非対話状態でのみ使える。
 * pipes:   パイプの配列。
 * 戻り値:  子プロセスの PID。fork/exec しなかった場合は 0。エラーなら -1。 */
static pid_t exec_single(
		PROCESS *p, ssize_t pindex, pid_t pgid, exec_t etype, PIPES pipes)
{
	bool expanded = false;
	int argc;
	char **argv;
	sigset_t sigset, oldsigset;

	if (etype == SELF)
		goto directexec;
	
	if (etype == FOREGROUND && pipes.p_count == 0) {
		expanded = true;
		if (!expand_line(p->p_args, &argc, &argv)) {
			recfree((void **) argv);
			return -1;
		}
		if (p->p_type == PT_NORMAL && argc == 0)
			return 0;  // XXX リダイレクトがあるなら特殊操作
		if (p->p_type != PT_NORMAL && argc > 0) {
			error(0, 0, "redirect syntax error");
			return -1;
		}
		if (p->p_type == PT_NORMAL) {
			cbody *body = get_builtin(argv[0]);
			if (body) {  /* fork せずに組込みコマンドを実行 */
				struct save_redirect *saver;
				if (open_redirections(p->p_redirs, &saver))
					laststatus = body(argc, argv);
				else
					laststatus = EXIT_FAILURE;
				if (body != builtin_exec || laststatus != EXIT_SUCCESS)
					undo_redirections(saver);
				else
					savesfree(saver);
				recfree((void **) argv);
				return 0;
			}
		} else if (p->p_type == PT_GROUP && !p->p_redirs) {
			recfree((void **) argv);
			exec_statements(p->p_subcmds);
			return 0;
		}
	}

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGHUP);
	if (sigprocmask(SIG_BLOCK, &sigset, &oldsigset) < 0)
		error(EXIT_FAILURE, errno, "sigprocmask before fork");

	pid_t cpid = fork();
	if (cpid < 0) {  /* fork 失敗 */
		error(0, errno, "%s: fork", argv[0]);
		if (expanded) { recfree((void **) argv); }
		return -1;
	} else if (cpid) {  /* 親プロセス */
		if (is_interactive) {
			if (setpgid(cpid, pgid) < 0 && errno != EACCES && errno != ESRCH)
				error(0, errno, "%s: setpgid (parent)", argv[0]);
			/* if (etype == FOREGROUND
					&& tcsetpgrp(STDIN_FILENO, pgid ? pgid : cpid) < 0
					&& errno != EPERM)
				error(0, errno, "%s: tcsetpgrp (parent)", argv[0]); */
		}
		if (!pgid) {
			/* SIGHUP をブロックしている内にアクティブジョブに pgid を設定する*/
			JOB *job = xmalloc(sizeof *job);
			*job = (JOB) { .j_pgid = cpid, };
			assert(!joblist.contents[0]);
			joblist.contents[0] = job;
		}
		if (sigprocmask(SIG_SETMASK, &oldsigset, NULL) < 0)
			error(0, errno, "sigprocmask (parent) after fork");
		if (expanded) { recfree((void **) argv); }
		return cpid;
	}
	
	/* 子プロセス */
	if (is_interactive) {
		if (setpgid(0, pgid) < 0)
			error(0, errno, "%s: setpgid (child)", argv[0]);
		if (etype == FOREGROUND
				&& tcsetpgrp(STDIN_FILENO, pgid ? pgid : getpid()) < 0)
			error(0, errno, "%s: tcsetpgrp (child)", argv[0]);
	}
	joblist_reinit();
	is_loginshell = is_interactive = false;
	if (sigprocmask(SIG_SETMASK, &oldsigset, NULL) < 0)
		error(0, errno, "sigprocmask (child) after fork");

directexec:
	assert(!is_interactive);
	if (pipes.p_count > 0) {
		if (pindex) {
			size_t index = ((pindex >= 0) ? (size_t)pindex : pipes.p_count) - 1;
			if (dup2(pipes.p_pipes[index][0], STDIN_FILENO) < 0)
				error(0, errno, "%s: cannot connect pipe to stdin", argv[0]);
		}
		if (pindex < (ssize_t) pipes.p_count) {
			size_t index = (pindex >= 0) ? (size_t) pindex : 0;
			if (dup2(pipes.p_pipes[index][1], STDOUT_FILENO) < 0)
				error(0, errno, "%s: cannot connect pipe to stdout", argv[0]);
		}
		close_pipes(pipes);
	}
	resetsigaction();

	if (!expanded) {
		if (!expand_line(p->p_args, &argc, &argv))
			exit(EXIT_FAILURE);
		if (p->p_type == PT_NORMAL && argc == 0)
			exit(EXIT_SUCCESS);  // XXX リダイレクトがあるなら特殊操作
		if (p->p_type != PT_NORMAL && *argv) {
			error(EXIT_FAILURE, 0, "redirection syntax error");
		}
	}
	if (!open_redirections(p->p_redirs, NULL))
		exit(EXIT_FAILURE);

	switch (p->p_type) {
		cbody *body;
		case PT_NORMAL:
			body = get_builtin(argv[0]);
			if (body)  /* 組込みコマンドを実行 */
				exit(body(argc, argv));

			char *command = which(argv[0],
					strchr(argv[0], '/') ? "." : getenv(ENV_PATH));
			if (!command)
				error(EXIT_NOTFOUND, 0, "%s: command not found", argv[0]);
			execve(command, argv, environ);
			/* execvp(argv[0], argv); */
			error(EXIT_NOEXEC, errno, "%s", argv[0]);
		case PT_GROUP:  case PT_SUBSHELL:
			exec_statements_and_exit(p->p_subcmds);
		case PT_X_PIPE:
			exec_pipelines_and_exit(p->p_subcmds->s_pipeline);
	}
	assert(false);  /* ここには来ないはず */
}

/* リダイレクトを開く。
 * 各 r の rd_file に対する各種展開もここで行う。
 * save:   非 NULL なら、元の FD をセーブしつつリダイレクトを処理し、*save
 *         にセーブデータへのポインタが入る。
 * 戻り値: OK なら true、エラーがあれば false。
 * エラーがあっても *save にはそれまでにセーブした FD のデータが入る。 */
static bool open_redirections(REDIR *r, struct save_redirect **save)
{
	struct save_redirect *s = NULL;
	char *exp;

	while (r) {
		int fd, flags;

		/* rd_file を展開する */
		exp = expand_single(r->rd_file);
		if (!exp) {
			error(0, 0, "redirect: invalid word expansion: %s", r->rd_file);
			goto next;
		}

		/* リダイレクトをセーブする */
		if (save) {
			int copyfd = dup(r->rd_fd);
			if (copyfd < 0 && errno != EBADF) {
				error(0, errno, "redirect: can't save file descriptor %d",
						r->rd_fd);
			} else if (copyfd >= 0 && fcntl(copyfd, F_SETFD, FD_CLOEXEC) < 0) {
				error(0, errno, "redirect: fcntl(%d,SETFD,CLOEXEC)", r->rd_fd);
				close(r->rd_fd);
			} else {
				struct save_redirect *ss = xmalloc(sizeof *ss);
				ss->next = s;
				ss->sr_origfd = r->rd_fd;
				ss->sr_copyfd = copyfd;
				switch (r->rd_fd) {
					case STDIN_FILENO:   ss->sr_file = stdin;  break;
					case STDOUT_FILENO:  ss->sr_file = stdout; break;
					case STDERR_FILENO:  ss->sr_file = stderr; break;
					default:             ss->sr_file = NULL;   break;
				}
				if (ss->sr_file)
					fflush(ss->sr_file);
				s = ss;
			}
		}

		/* 実際に rd_type に応じてリダイレクトを行う */
		switch (r->rd_type) {
			case RT_INPUT:
				flags = O_RDONLY;
				break;
			case RT_OUTPUT:
				flags = O_WRONLY | O_CREAT | O_TRUNC;
				break;
			case RT_APPEND:
				flags = O_WRONLY | O_CREAT | O_APPEND;
				break;
			case RT_INOUT:
				flags = O_RDWR | O_CREAT;
				break;
			case RT_DUP:
				if (strcmp(exp, "-") == 0) {
					/* r->rd_fd を閉じる */
					if (close(r->rd_fd) < 0 && errno != EBADF)
						error(0, errno,
								"redirect: error on closing file descriptor %d",
								r->rd_fd);
					goto next;
				} else {
					char *end;
					errno = 0;
					fd = strtol(exp, &end, 10);
					if (errno) goto onerror;
					if (exp[0] == '\0' || end[0] != '\0') {
						error(0, 0, "redirect syntax error");
						free(exp);
						if (save) *save = s;
						return false;
					}
					if (fd < 0) {
						errno = ERANGE;
						goto onerror;
					}
					if (fd != r->rd_fd) {
						if (close(r->rd_fd) < 0)
							if (errno != EBADF)
								goto onerror;
						if (dup2(fd, r->rd_fd) < 0)
							goto onerror;
					}
				}
				goto next;
			default:
				assert(false);
		}
		fd = open(exp, flags, 0666);
		if (fd < 0) goto onerror;
		if (fd != r->rd_fd) {
			if (close(r->rd_fd) < 0)
				if (errno != EBADF)
					goto onerror;
			if (dup2(fd, r->rd_fd) < 0)
				goto onerror;
			if (close(fd) < 0)
				goto onerror;
		}

next:
		free(exp);
		r = r->next;
	}
	if (save) *save = s;
	return true;

onerror:
	error(0, errno, "redirect: %s", r->rd_file);
	free(exp);
	if (save) *save = s;
	return false;
}

/* セーブしたリダイレクトを元に戻し、さらに引数リスト save を free する。 */
static void undo_redirections(struct save_redirect *save)
{
	while (save) {
		struct save_redirect *next = save->next;
		if (save->sr_file)
			fflush(save->sr_file);
		if (close(save->sr_origfd) < 0 && errno != EBADF)
			error(0, errno, "closing file descriptor %d",
					save->sr_origfd);
		if (save->sr_copyfd >= 0) {
			if (dup2(save->sr_copyfd, save->sr_origfd) < 0)
				error(0, errno, "can't restore file descriptor %d from %d",
						save->sr_origfd, save->sr_copyfd);
			if (close(save->sr_copyfd) < 0)
				error(0, errno, "closing copied file descriptor %d",
						save->sr_copyfd);
		}
		free(save);
		save = next;
	}
}

/* save_redirect のリストを解放する */
static void savesfree(struct save_redirect *save)
{
	while (save) {
		struct save_redirect *next = save->next;
		free(save);
		save = next;
	}
}
