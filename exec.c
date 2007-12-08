/* Yash: yet another shell */
/* © 2007 magicant */

/* This software can be redistributed and/or modified under the terms of
 * GNU General Public License, version 2 or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRENTY. */


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
#include <assert.h>


/* パイプの集合 */
typedef struct {
	size_t p_count;     /* パイプのペアの数 */
	int (*p_pipes)[2];  /* パイプのペアの配列へのポインタ */
} PIPES;

int exitcode_from_status(int status);
unsigned job_count(void);
static int add_job(void);
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
static void exec_pipelines(PIPELINE *pipelines);
static void exec_processes(PROCESS *processes, bool neg_status, bool pipe_loop);
//static size_t exec_pipe(SCMD *scmds);
//static pid_t exec_single(
		//SCMD *scmd, pid_t pgid, bool fg, PIPES pipes, ssize_t pindex);
//static void get_ready_and_exec(SCMD *scmd, PIPES pipes, ssize_t pindex);
static int open_redirections(REDIR *redirs);
//static void builtin_exec(SCMD *scmd);

/* 最後に実行したコマンドの終了コード */
int laststatus = 0;

/* true なら終了時に未了のジョブに SIGHUP を送る */
bool huponexit = false;

/* wait_all 中のシグナルハンドラでこれを非 0 にすると wait を止める */
volatile bool cancel_wait = false;

/* ジョブのリストとその大きさ (要素数)。
 * リストの中の「空き」は、JOB の j_pgid を 0 にすることで示される。 */
/* これらの値はシェルの起動時に初期化される */
/* ジョブ番号が 0 のジョブはアクティブジョブ (論理的にはまだジョブリストにない
 * ジョブ) である。 */
JOB *joblist;
size_t joblistlen;

/* カレントジョブのジョブ番号 (joblist 内でのインデックス)
 * この値が無効なインデックス (0 を含む) となっているとき、
 * カレントジョブは存在しない。 */
size_t currentjobnumber = 0;

/* アクティブジョブの SCMD (の配列) へのポインタ。 */
//static SCMD *activejobscmd = NULL;

/* enum jstatus の文字列表現 */
const char * const jstatusstr[] = {
	"NULL", "Running", "Done", "Stopped",
};

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

/* 現在のジョブリスト内のジョブの個数を数える (アクティブジョブを除く) */
unsigned job_count(void)
{
	unsigned result = 0;

	for (size_t index = joblistlen; --index > 0; )
		if (joblist[index].j_pgid)
			result++;
	return result;
}

/* アクティブジョブ (ジョブ番号 0 のジョブ) をジョブリストに移動し、
 * カレントジョブをそのジョブに変更する。
 * 戻り値: 成功したら新しいジョブ番号 (>0)、失敗したら -1。*/
/* この関数は内部で SIGHUP/SIGCHLD をブロックする */
static int add_job(void)
{
	const size_t LENINC = 8;
	size_t jobnumber;
	sigset_t sigset, oldsigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGHUP);
	sigaddset(&sigset, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &sigset, &oldsigset) < 0)
		error(EXIT_FAILURE, errno, "sigprocmask");

	if (!joblist[0].j_pgid) {
		jobnumber = -1;
		goto end;
	}
	assert(joblist[0].j_status > JS_NULL && joblist[0].j_status <= JS_STOPPED);

	/* リストの空いているインデックスを探す */
	for (jobnumber = 1; jobnumber < joblistlen; jobnumber++)
		if (joblist[jobnumber].j_pgid == 0)
			break;
	if (jobnumber == joblistlen) {  /* 空きがなかったらリストを大きくする */
		if (joblistlen == MAX_JOB) {
			error(0, 0, "too many jobs");
			jobnumber = -1;
			goto end;
		} else {
			JOB *newlist = xrealloc(joblist,
					(joblistlen + LENINC) * sizeof(JOB));
			memset(newlist + joblistlen, 0, LENINC * sizeof(JOB));
			joblist = newlist;
			joblistlen += LENINC;
		}
	}
	assert(joblist[jobnumber].j_pgid == 0);
//	if (activejobscmd)
//		joblist[0].j_name = make_job_name(activejobscmd);
	joblist[jobnumber] = joblist[0];
	memset(&joblist[0], 0, sizeof joblist[0]);
//	activejobscmd = NULL;
	currentjobnumber = jobnumber;

end:
	if (sigprocmask(SIG_SETMASK, &oldsigset, NULL) < 0)
		error(EXIT_FAILURE, errno, "sigprocmask");
	return jobnumber;
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
	JOB *job = joblist + jobnumber;
	sigset_t sigset, oldsigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGHUP);
	sigaddset(&sigset, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &sigset, &oldsigset) < 0)
		error(EXIT_FAILURE, errno, "sigprocmask");

	if (jobnumber < 1 || jobnumber >= joblistlen || !job->j_pgid)
		goto end;

	if (!changedonly || job->j_statuschanged) {
		int estatus = job->j_exitstatus;
		if (job->j_status == JS_DONE) {
			if (WIFEXITED(estatus) && WEXITSTATUS(estatus))
				printf("[%zu]%c %5d  Exit:%-3d    %s\n",
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
normal:
			printf("[%zu]%c %5d  %-8s    %s\n",
					jobnumber, currentjobnumber == jobnumber ? '+' : ' ',
					(int) job->j_pgid, jstatusstr[job->j_status],
					job->j_name ? : "<< unknown job >>");
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
	if (job->j_status == JS_DONE) {
		free(job->j_pids);
		free(job->j_name);
		memset(job, 0, sizeof *job);
	}

end:
	if (sigprocmask(SIG_SETMASK, &oldsigset, NULL) < 0)
		error(EXIT_FAILURE, errno, "sigprocmask");
}

/* (アクティブジョブを除く) 全てのジョブの状態を画面に出力する。
 * changedonly: true なら変化があったものだけ表示する。
 * printpids:   true なら PGID のみならず各子プロセスの PID も表示する */
void print_all_job_status(bool changedonly, bool printpids)
{
	for (size_t i = 1; i < joblistlen; i++)
		print_job_status(i, changedonly, printpids);
}

/* 子プロセスの ID からジョブ番号を得る。
 * 戻り値: ジョブ番号 (> 0) または -1 (見付からなかった場合)。
 *         子プロセスが既に終了している場合、見付からない。 */
int get_jobnumber_from_pid(pid_t pid)
{
	for (unsigned i = 0; i < joblistlen; i++)
		if (joblist[i].j_pgid)
			for (pid_t *pids = joblist[i].j_pids; *pids; pids++)
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
/* この関数は内部で SIGHUP/SIGCHLD をブロックする。
 * この関数をシグナルハンドラから呼出すときは必ず blockedjob を -2 にすること。
 * */
void wait_all(int blockedjob)
{
	int waitpidopt;
	int status;
	pid_t pid;
	sigset_t sigset, oldsigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGHUP);
	sigaddset(&sigset, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &sigset, &oldsigset) < 0)
		error(EXIT_FAILURE, errno, "sigprocmask before wait");
	
	if (blockedjob >= (int) joblistlen
			|| (blockedjob >= 0 && !joblist[blockedjob].j_pgid))
		blockedjob = -2;
start:
	if (cancel_wait) {
		cancel_wait = false;
		goto end;
	}

	/* 全ジョブが終了したかどうか調べる。 */
	if (blockedjob == -1) {
		for (size_t i = 0; i < joblistlen; i++) {
			if (joblist[i].j_pgid) {
				switch (joblist[i].j_status) {
					case JS_STOPPED:  case JS_DONE:
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

	ssize_t jobnumber;
	JOB *job;
	pid_t *pidp;
	enum jstatus oldstatus;

	/* jobnumber と、JOB の j_pids 内の PID の位置を探す */
	for (jobnumber = 0; jobnumber < (ssize_t) joblistlen; jobnumber++)
		if (joblist[jobnumber].j_pgid)
			for (pidp = joblist[jobnumber].j_pids; *pidp; pidp++)
				if (pid == *pidp)
					goto outofloop;
	/*error(0, 0, "unexpected waitpid result: %ld", (long) pid);*/
	goto start;

outofloop:
	assert(jobnumber < (ssize_t) joblistlen);
	job = joblist + jobnumber;
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
	if (jobnumber == blockedjob) {
		switch (job->j_status) {
			case JS_NULL:
			default:
				assert(0);
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

	if (jobnumber == 0 && job->j_pgid) {
		switch (job->j_status) {
			default:
			case JS_NULL:
				assert(0);
			case JS_RUNNING:
				break;
			case JS_DONE:
				laststatus = exitcode_from_status(job->j_exitstatus);
				if (job->j_exitcodeneg)
					laststatus = !laststatus;
				free(job->j_pids);
				free(job->j_name);
				memset(job, 0, sizeof *job);
				break;
			case JS_STOPPED:
				if (is_interactive)
					add_job();
				break;
		}
	}

	goto start;

end:
	if (sigprocmask(SIG_SETMASK, &oldsigset, NULL) < 0)
		error(EXIT_FAILURE, errno, "sigprocmask after wait");
}

/* 全てのジョブに SIGHUP を送る。
 * 停止しているジョブには SIGCONT も送る。 */
void send_sighup_to_all_jobs(void)
{
	size_t i;

	if (is_interactive) {
		for (i = 0; i < joblistlen; i++) {
			JOB *job = joblist + i;

			if (job->j_pgid && !(job->j_flags & JF_NOHUP)) {
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
void sig_hup(int signal)
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
		printf("%s%s\n", s->s_name, s->s_bg ? " &" : "");
		if (!s->s_bg)
			exec_pipelines(s->s_pipeline);
		else
			error(0, 0, "background not supported");  // TODO
		s = s->next;
	}
}

/* 一つの文を実行する。 */
static void exec_pipelines(PIPELINE *p)
{
	if (!p) return;

	bool nextcond = true;
	laststatus = 0;
	while (p && (nextcond == !laststatus)) {
		exec_processes(p->pl_proc, p->pl_neg, p->pl_loop);
		nextcond = p->pl_next_cond;
		p = p->next;
	}
}

/* 一つのパイプラインを実行し、終了または停止を待つ。
 * neg ならばパイプラインの終了コードを反転。
 * loop ならば環状のパイプラインを作成。 */
static void exec_processes(PROCESS *p, bool neg, bool loop)
{
	//TODO exec_processes
}

/* コマンド入力全体を受け取って、コマンドを実行する。 */
//void exec_list(SCMD *scmds, size_t count)
//{
//	size_t i = 0;
//	int skip = 0;
//
//	while (i < count) {
//		if (scmds[i].c_argc == 0) {
//			i++;
//			continue;
//		}
//		if (i) {
//			switch (scmds[i - 1].c_type) {
//				case CT_AND:
//					skip = !!laststatus;
//					break;
//				case CT_OR:
//					skip = !laststatus;
//					break;
//				default:
//					skip = 0;
//					break;
//			}
//		}
//		if (skip) {
//			while (scmds[i++].c_type == CT_PIPED);
//		} else {
//			i += exec_pipe(scmds + i);
//		}
//	}
//}

/* 一つのパイプライン全体を実行する。
 * パイプラインの種類が CT_END ならば、子プロセスを wait する。
 * 戻り値: 実行した SCMD の個数。 */
//static size_t exec_pipe(SCMD *scmds)
//{
//	volatile JOB *job = &joblist[0];
//	size_t count = 0;
//	pid_t pgid;
//	bool fg;
//	PIPES pipes;
//
//	while (scmds[count++].c_type == CT_PIPED);
//	switch (scmds[count - 1].c_type) {
//		case CT_END:  case CT_AND:  case CT_OR:
//			fg = 1;
//			break;
//		default:
//			fg = 0;
//			break;
//	}
//	pipes.p_count = count - 1;
//	pipes.p_pipes = create_pipes(pipes.p_count);
//	if (pipes.p_count && !pipes.p_pipes)
//		goto end;
//	job->j_pids = xcalloc(count + 1, sizeof(pid_t));
//	assert(!job->j_pgid);
//	/* job->j_pgid はこの最初の exec_single の中で設定される (fork した場合) */
//	pgid = job->j_pids[0] =
//		exec_single(scmds, 0, fg, pipes, scmds[pipes.p_count].c_argc ? 0 : -1);
//	if (pgid >= 0)
//		for (int i = 1; i < count; i++)
//			job->j_pids[i] = exec_single(scmds + i, pgid, fg, pipes, i);
//	close_pipes(pipes);
//	if (pgid > 0) {
//		job->j_status = JS_RUNNING;
//		job->j_statuschanged = true;
//		job->j_flags = 0;
//		job->j_exitstatus = 0;
//		job->j_name = NULL;
//		activejobscmd = scmds;
//		laststatus = 0;
//		if (fg) {
//			wait_all(0);
//		} else {
//			add_job();
//		}
//	}
//end:
//	return count;
//}

/* 一つのコマンドを実行する。内部で処理できる組込みコマンドでなければ
 * fork/exec し、リダイレクトなどを設定する。
 * scmd:    実行するコマンド
 * pgid:    子プロセスに設定するプロセスグループ ID。
 *          子プロセスのプロセス ID をそのままプロセスグループ ID にする時は 0。
 *          サブシェルではプロセスグループを設定しないので pgid は無視。
 * fg:      フォアグラウンドジョブにするかどうか。
 * pipes:   パイプの配列。
 * pindex:  パイプ全体における子プロセスのインデックス。
 *          環状パイプを作る場合は 0 の代わりに -1。
 * 戻り値:  子プロセスの PID。fork/exec しなかった場合は 0。エラーなら -1。 */
//static pid_t exec_single(
//		SCMD *scmd, pid_t pgid, bool fg, PIPES pipes, ssize_t pindex)
//{
//	cbody body;
//	pid_t cpid;
//	sigset_t sigset, oldsigset;
//
//	if (!scmd->c_argc)
//		return 0;
//	if (scmd->c_argv && strcmp(scmd->c_argv[0], "exec") == 0) {
//		/* exec 組込みコマンドを実行 */
//		if (pipes.p_count) {
//			error(0, 0, "exec command cannot be piped");
//			return -1;
//		}
//		builtin_exec(scmd);
//		return -1;  /* ここに来たということはエラーである */
//	}
//	if (fg && scmd->c_argv && !pipes.p_count && !scmd->c_redircnt) {
//		body = assoc_builtin(scmd->c_argv[0]).b_body;
//		if (body) {  /* 組込みコマンドを実行 */
//			laststatus = body(scmd->c_argc, scmd->c_argv);
//			return 0;
//		}
//	}
//
//	sigemptyset(&sigset);
//	sigaddset(&sigset, SIGHUP);
//	if (sigprocmask(SIG_BLOCK, &sigset, &oldsigset) < 0)
//		error(EXIT_FAILURE, errno, "sigprocmask before fork");
//	cpid = fork();
//	if (cpid < 0) {  /* fork 失敗 */
//		error(0, errno, "%s: fork", scmd->c_argv[0]);
//		return -1;
//	} else if (cpid) {  /* 親プロセスの処理 */
//		if (is_interactive) {
//			if (setpgid(cpid, pgid) < 0 && errno != EACCES && errno != ESRCH)
//				error(0, errno, "%s: setpgid (parent)", scmd->c_argv[0]);
//			/* if (fg && tcsetpgrp(STDIN_FILENO, pgid ? pgid : cpid) < 0
//					&& errno != EPERM)
//				error(0, errno, "%s: tcsetpgrp (parent)", scmd->c_argv[0]); */
//		}
//		if (!pgid)  /* シグナルをブロックしている間に PGID を設定する */
//			joblist[0].j_pgid = cpid;
//		if (sigprocmask(SIG_SETMASK, &oldsigset, NULL) < 0)
//			error(0, errno, "sigprocmask (parent)");
//		return cpid;
//	} else {  /* 子プロセスの処理 */
//		if (is_interactive) {
//			if (setpgid(0, pgid) < 0)
//				error(0, errno, "%s: setpgid (child)", scmd->c_argv[0]);
//			if (fg && tcsetpgrp(STDIN_FILENO, pgid ? pgid : getpid()) < 0)
//				error(0, errno, "%s: tcsetpgrp (child)", scmd->c_argv[0]);
//		}
//		is_loginshell = is_interactive = false;
//		if (sigprocmask(SIG_SETMASK, &oldsigset, NULL) < 0)
//			error(0, errno, "sigprocmask (child)");
//		get_ready_and_exec(scmd, pipes, pindex);
//	}
//	assert(0);  /* ここには来ないはず */
//	return 0;
//}

/* 子プロセスでパイプやリダイレクトを処理し、exec する。
 * scmd:    実行するコマンド
 * pipes:   パイプの配列。
 * pindex:  パイプ全体における子プロセスのインデックス。
 *          環状パイプを作る場合は 0 の代わりに -1。 */
//static void get_ready_and_exec(SCMD *scmd, PIPES pipes, ssize_t pindex)
//{
//	char **argv = scmd->c_argv;
//
//	if (pipes.p_count) {
//		if (pindex) {
//			size_t index = ((pindex >= 0) ? (size_t)pindex : pipes.p_count) - 1;
//			if (dup2(pipes.p_pipes[index][0], STDIN_FILENO) < 0)
//				error(0, errno, "%s: cannot connect pipe to stdin", argv[0]);
//		}
//		if (pindex < (ssize_t) pipes.p_count) {
//			size_t index = (pindex >= 0) ? (size_t) pindex : 0;
//			if (dup2(pipes.p_pipes[index][1], STDOUT_FILENO) < 0)
//				error(0, errno, "%s: cannot connect pipe to stdout", argv[0]);
//		}
//		close_pipes(pipes);
//	}
//	resetsigaction();
//	if (open_redirections(scmd->c_redir, scmd->c_redircnt) < 0)
//		exit(EXIT_FAILURE);
//
//	if (scmd->c_subcmds) {
//		memset(joblist, 0, joblistlen * sizeof(JOB));
//		laststatus = 0;
//		exec_list(scmd->c_subcmds, scmd->c_argc);
//		exit(laststatus);
//	} else {
//		cbody body = assoc_builtin(argv[0]).b_body;
//		if (body)  /* 組込みコマンドを実行 */
//			exit(body(scmd->c_argc, argv));
//
//		char *command = which(argv[0], getenv(ENV_PATH));
//		if (!command)
//			error(EXIT_NOTFOUND, 0, "%s: command not found", argv[0]);
//		execve(command, argv, environ);
//		/* execvp(argv[0], argv); */
//		error(EXIT_NOEXEC, errno, "%s", argv[0]);
//	}
//	assert(0);  /* ここには来ないはず */
//	return;
//}

/* リダイレクトを開く。
 * 戻り値: OK なら 0、エラーなら -1。 */
static int open_redirections(REDIR *r)
{//XXX : open_redirections not tested
	while (r) {
		int fd;

		if (r->rd_destfd >= 0) {
			fd = r->rd_destfd;
		} else if (!r->rd_file) {
			if (close(r->rd_fd) < 0) {
				error(0, errno,
						"redirection: closing file descriptor %d", r->rd_fd);
				return -1;
			}
			continue;
		} else {
			fd = open(r->rd_file, r->rd_flags, 0666);
			if (fd < 0)
				goto onerror;
		}
		if (fd != r->rd_fd) {
			if (close(r->rd_fd) < 0)
				if (errno != EBADF)
					goto onerror;
			if (dup2(fd, r->rd_fd) < 0)
				goto onerror;
			if (r->rd_destfd < 0 && close(fd) < 0)
				goto onerror;
		}

		r = r->next;
	}
	return 0;

onerror:
	error(0, errno, "redirection: %s", r->rd_file);
	return -1;
}

/* exec 組込みコマンド
 * この関数が返るのはエラーが起きた時だけである。
 * -c:       環境変数なしで execve する。
 * -f:       未了のジョブがあっても execve する。
 * -l:       ログインコマンドとして新しいコマンドを execve する。
 * -a name:  execve するとき argv[0] として name を渡す。 */
//static void builtin_exec(SCMD *scmd)
//{
//	int opt;
//	bool clearenv = false, forceexec = false, login = false;
//	char **argv = scmd->c_argv;
//	char *argv0 = NULL;
//
//	optind = 0;
//	opterr = 1;
//	while ((opt = getopt(scmd->c_argc, argv, "+cfla:")) >= 0) {
//		switch (opt) {
//			case 'c':
//				clearenv = true;
//				break;
//			case 'f':
//				forceexec = true;
//				break;
//			case 'l':
//				login = true;
//				break;
//			case 'a':
//				argv0 = optarg;
//				break;
//			default:
//				printf("Usage:  exec [-cfl] [-a name] command [args...]\n");
//				laststatus = EXIT_FAILURE;
//				return;
//		}
//	}
//
//	if (!forceexec) {
//		wait_all(-2 /* non-blocking */);
//		print_all_job_status(true /* changed only */, false /* not verbose */);
//		if (job_count()) {
//			error(0, 0, "There are undone jobs!"
//					"  Use `-f' option to exec anyway.");
//			laststatus = EXIT_FAILURE;
//			return;
//		}
//	}
//	if (open_redirections(scmd->c_redir, scmd->c_redircnt) < 0) {
//		laststatus = EXIT_FAILURE;
//		return;
//	}
//	if (scmd->c_argc <= optind) {
//		laststatus = EXIT_SUCCESS;
//		return;
//	}
//
//	char *oldargv0 = argv[optind];
//	char *newargv0;
//	char *command = which(oldargv0, getenv(ENV_PATH));
//	if (!command) {
//		error(0, 0, "%s: command not found", argv[0]);
//		goto error;
//	}
//	newargv0 = argv0 ? : oldargv0;
//	if (login) {
//		char *newnewargv0 = xmalloc(strlen(newargv0) + 2);
//		newnewargv0[0] = '-';
//		strcpy(newnewargv0 + 1, newargv0);
//		newargv0 = newnewargv0;
//	} else {
//		newargv0 = xstrdup(newargv0);
//	}
//	argv[optind] = newargv0;
//
//	finalize_interactive();
//	resetsigaction();
//	execve(command, argv + optind, clearenv ? NULL : environ);
//	setsigaction();
//	init_interactive();
//
//	error(0, errno, "%s", argv[0]);
//	argv[optind] = oldargv0;
//	free(newargv0);
//	free(command);
//error:
//	laststatus = EXIT_NOEXEC;
//}
