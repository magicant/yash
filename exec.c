/* Yash: yet another shell */
/* exec.c: command execution and job control */
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


#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#include "yash.h"
#include "util.h"
#include "sig.h"
#include "expand.h"
#include "exec.h"
#include "path.h"
#include "builtin.h"
#include "variable.h"
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

/* 設定したリダイレクトを後で元に戻すためのデータ */
struct save_redirect {
	struct save_redirect *next;
	int   sr_origfd; /* 元のファイルディスクリプタ */
	int   sr_copyfd; /* 新しくコピーしたファイルディスクリプタ */
	FILE *sr_file;   /* 元のストリーム */
};

static void joblist_reinit(void);
static int add_job(void);
static int (*create_pipes(size_t count))[2];
static void close_pipes(PIPES pipes);
static inline int xdup2(int oldfd, int newfd);
static void exec_pipelines(PIPELINE *pipelines);
static void exec_pipelines_and_exit(PIPELINE *pipelines)
	__attribute__((noreturn));
static void exec_processes(PROCESS *processes, const char *jobname,
		bool neg_status, bool pipe_loop, bool background);
static pid_t exec_single(
		PROCESS *p, ssize_t pindex, pid_t pgid, exec_t etype, PIPES pipes);
static bool open_redirections(REDIR *redirs, struct save_redirect **save);
static void undo_redirections(struct save_redirect *save);
static void clear_save_redirect(struct save_redirect *save);

/* 最後に実行したコマンドの終了コード */
int laststatus = 0;

/* true なら終了時に未了のジョブに SIGHUP を送る */
bool huponexit = false;

/* ジョブのリスト。リストの要素は JOB へのポインタ。
 * リストの中の「空き」は、NULL ポインタによって示す。 */
/* これらの値はシェルの起動時に初期化される */
/* ジョブ番号が 0 のジョブはアクティブジョブ (形式的にはまだジョブリストにない
 * ジョブ) である。 */
struct plist joblist;

/* カレントジョブのジョブ番号 (joblist 内でのインデックス)
 * この値が存在しないジョブのインデックス (0 を含む) となっているとき、
 * カレントジョブは存在しない。 */
size_t currentjobnumber = 0;

/* 一時的な作業用の子プロセスの情報。普段は .jp_pid = 0。 */
static struct jproc temp_chld = { .jp_pid = 0, };

/* 最後に起動したバックグラウンドジョブのプロセス ID。
 * ジョブがパイプラインになっている場合は、パイプ内の最後のプロセスの ID。
 * この値は特殊パラメータ $! の値となる。 */
pid_t last_bg_pid;

/* enum jstatus の文字列表現 */
const char *const jstatusstr[] = {
	"Running", "Done", "Stopped",
};

/* 実行環境を初期化する */
void init_exec(void)
{
	pl_init(&joblist);
	pl_append(&joblist, NULL);
}

/* joblist を再初期化する */
static void joblist_reinit(void)
{
	for (size_t i = 0; i < joblist.length; i++)
		remove_job(i);
	/*
	pl_clear(&joblist);
	pl_append(&joblist, NULL);
	*/
}

/* waitpid が返す status から終了コードを得る。
 * 戻り値: status がプロセスの終了を表していない場合は -1。 */
int exitcode_from_status(int status)
{
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		return WTERMSIG(status) + 384;
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

/* 現在のジョブリスト内の実行中のジョブの個数を数える(アクティブジョブを除く) */
unsigned running_job_count(void)
{
	unsigned result = 0;
	JOB *job;

	assert(joblist.length > 0);
	for (size_t index = joblist.length; --index > 0; )
		if ((job = joblist.contents[index]) && job->j_status == JS_RUNNING)
			result++;
	return result;
}

/* 現在のジョブリスト内の停止中のジョブの個数を数える(アクティブジョブを除く) */
unsigned stopped_job_count(void)
{
	unsigned result = 0;
	JOB *job;

	assert(joblist.length > 0);
	for (size_t index = joblist.length; --index > 0; )
		if ((job = joblist.contents[index]) && job->j_status == JS_STOPPED)
			result++;
	return result;
}

/* 現在のジョブリスト内の未終了ジョブの数を数える (アクティブジョブを除く) */
unsigned undone_job_count(void)
{
	unsigned result = 0;
	JOB *job;

	assert(joblist.length > 0);
	for (size_t index = joblist.length; --index > 0; )
		if ((job = joblist.contents[index]) && job->j_status != JS_DONE)
			result++;
	return result;
}

/* アクティブジョブ (ジョブ番号 0 のジョブ) をジョブリストに移動し、
 * そのジョブをカレントジョブにする。
 * 対話的シェルでは print_job_status を行う。
 * 戻り値: 成功したら新しいジョブ番号 (>0)、失敗したら -1。*/
static int add_job(void)
{
	size_t jobnumber;
	JOB *job;

	if (!(job = get_job(0)))
		return -1;

	/* リストの空いているインデックスを探す */
	for (jobnumber = 1; jobnumber < joblist.length; jobnumber++)
		if (!joblist.contents[jobnumber])
			break;
	if (jobnumber == joblist.length) {  /* 空きがなかったらリストを大きくする */
		if (joblist.length == MAX_JOB) {
			xerror(0, 0, "too many jobs");
			return -1;
		} else {
			pl_append(&joblist, job);
		}
	} else {  /* 空きがあったらそこに入れる */
		assert(!joblist.contents[jobnumber]);
		joblist.contents[jobnumber] = job;
	}
	joblist.contents[0] = NULL;
	currentjobnumber = jobnumber;
	if (is_interactive_now)
		print_job_status(jobnumber, true, false);
	return jobnumber;
}

/* 指定した番号のジョブを削除する。
 * 戻り値: true なら削除成功。false はもともとジョブがなかった場合。 */
bool remove_job(size_t jobnumber)
{
	JOB *job = get_job(jobnumber);
	if (job) {
		joblist.contents[jobnumber] = NULL;
		free(job->j_procv);
		free(job->j_name);
		free(job);
		return true;
	} else {
		return false;
	}
}

/* 既に終了したジョブを削除する。(アクティブジョブを除く) */
void remove_exited_jobs(void)
{
	JOB *job;

	assert(joblist.length > 0);
	for (size_t index = joblist.length; --index > 0; )
		if ((job = joblist.contents[index]) && job->j_status == JS_DONE)
			remove_job(index);
}

/* ジョブの状態を表示する。
 * 終了したジョブ (j_status == JS_DONE) は、削除する。
 * jobnumber:   ジョブ番号。joblist のインデックス。
 *              使用されていないジョブ番号を指定した場合、何もしない。
 * changedonly: true なら変化があった場合だけ表示する。
 * printpids:   true なら PGID のみならず各子プロセスの PID も表示する */
void print_job_status(size_t jobnumber, bool changedonly, bool printpids)
{
	JOB *job;

	if (!(job = get_job(jobnumber)))
		return;

	if (!changedonly || job->j_statuschanged) {
		int estatus = job->j_waitstatus;
		if (job->j_status == JS_DONE) {
			if (WIFEXITED(estatus) && WEXITSTATUS(estatus))
				fprintf(stderr, "[%zu]%c %5d  Exit %-3d    %s\n",
						jobnumber, currentjobnumber == jobnumber ? '+' : ' ',
						(int) job->j_pgid, WEXITSTATUS(estatus),
						job->j_name ? job->j_name : "<< unknown job >>");
			else if (WIFSIGNALED(job->j_waitstatus))
				fprintf(stderr, "[%zu]%c %5d  %-8s    %s\n",
						jobnumber, currentjobnumber == jobnumber ? '+' : ' ',
						(int) job->j_pgid, xstrsignal(WTERMSIG(estatus)),
						job->j_name ? job->j_name : "<< unknown job >>");
			else
				goto normal;
		} else {
			bool iscurrent, isbg;
normal:
			iscurrent = (currentjobnumber == jobnumber);
			isbg = (job->j_status == JS_RUNNING);
			fprintf(stderr, "[%zu]%c %5d  %-8s    %s%s\n",
					jobnumber,
					iscurrent ? '+' : ' ',
					(int) job->j_pgid, jstatusstr[job->j_status],
					job->j_name ? job->j_name : "<< unknown job >>",
					isbg ? " &" : "");
		}
		if (printpids) {
			struct jproc *ps = job->j_procv;
			for (size_t i = 0; i < job->j_procc; i++)
				fprintf(stderr, "\t* %5d  %-8s\n",
						ps[i].jp_pid, jstatusstr[ps[i].jp_status]);
		}
		job->j_statuschanged = false;
	}
	if (job->j_status == JS_DONE)
		remove_job(jobnumber);
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
 * 戻り値: ジョブ番号 (>= 0) または -1 (見付からなかった場合)。 */
int get_jobnumber_from_pid(pid_t pid)
{
	JOB *job;

	for (size_t i = 0; i < joblist.length; i++)
		if ((job = joblist.contents[i]))
			for (size_t j = 0; j < job->j_procc; j++)
				if (job->j_procv[j].jp_pid == pid)
					return i;
	return -1;
}

/* 全ての子プロセスを対象に wait する。
 * Wait するだけでジョブの状態変化の報告はしない。
 * この関数はブロックしない。 */
/* この関数は内部で SIGCHLD をブロックする。 */
void wait_chld(void)
{
	int waitpidopt;
	int status;
	pid_t pid;
	sigset_t newset, oldset;

	sigemptyset(&newset);
	sigaddset(&newset, SIGCHLD);
	sigemptyset(&oldset);
	if (sigprocmask(SIG_BLOCK, &newset, &oldset) < 0)
		xerror(0, errno, "sigprocmask before wait");

start:
	waitpidopt = WUNTRACED | WCONTINUED | WNOHANG;
	pid = waitpid(-1 /* any child process */, &status, waitpidopt);
	if (pid <= 0) {
		if (pid < 0) {
			if (errno == EINTR)
				goto start;
			if (errno != ECHILD)
				xerror(0, errno, "waitpid");
		}
		if (sigprocmask(SIG_SETMASK, &oldset, NULL) < 0)
			xerror(0, errno, "sigprocmask after wait");
		return;
	}

	size_t jobnumber;
	JOB *job;
	size_t proci;
	struct jproc *proc;
	enum jstatus oldstatus;

	/* jobnumber と、JOB の j_pids 内の PID の位置を探す */
	for (jobnumber = 0; jobnumber < joblist.length; jobnumber++)
		if ((job = joblist.contents[jobnumber]))
			for (proci = 0; proci < job->j_procc; proci++)
				if ((*(proc = job->j_procv + proci)).jp_pid == pid)
					goto pfound;
	if (temp_chld.jp_pid == pid) {
		job = NULL;
		proci = 0;
		proc = &temp_chld;
		goto pfound;
	}
	/* 未知のプロセス番号を受け取ったとき (これはジョブを disown したとき等に
	 * 起こりうる) は、単に無視して start に戻る */
	goto start;

pfound:
	if (WIFEXITED(status) || WIFSIGNALED(status)) {
		proc->jp_status = JS_DONE;
	} else if (WIFSTOPPED(status)) {
		proc->jp_status = JS_STOPPED;
	} else if (WIFCONTINUED(status)) {
		proc->jp_status = JS_RUNNING;
	}
	proc->jp_waitstatus = status;
	if (proc == &temp_chld)
		goto start;

	if (proci + 1 == job->j_procc)
		job->j_waitstatus = status;

	bool anyrunning = false, anystopped = false;
	/* ジョブの全プロセスが停止・終了したかどうか調べる */
	for (size_t i = 0; i < job->j_procc; i++) {
		switch (job->j_procv[i].jp_status) {
			case JS_RUNNING:
				anyrunning = true;
				continue;
			case JS_STOPPED:
				anystopped = true;
				continue;
			case JS_DONE:
				continue;
		}
	}
	oldstatus = job->j_status;
	job->j_status = anyrunning ? JS_RUNNING : anystopped ? JS_STOPPED : JS_DONE;
	if (job->j_status != oldstatus)
		job->j_statuschanged = true;

	goto start;
}

/* Nohup を設定していない全てのジョブに SIGHUP を送る。
 * 停止しているジョブには SIGCONT も送る。 */
void send_sighup_to_all_jobs(void)
{
	if (is_interactive_now) {
		if (temp_chld.jp_pid) {
			kill(temp_chld.jp_pid, SIGHUP);
			if (temp_chld.jp_status == JS_STOPPED)
				kill(temp_chld.jp_pid, SIGCONT);
		}
		for (size_t i = 0; i < joblist.length; i++) {
			JOB *job = joblist.contents[i];

			if (job && !(job->j_flags & JF_NOHUP)) {
				kill(-job->j_pgid, SIGHUP);
				for (size_t j = 0; j < job->j_procc; j++) {
					if (job->j_procv[i].jp_status == JS_STOPPED) {
						kill(-job->j_pgid, SIGCONT);
						break;
					}
				}
			}
		}
	} else {
		kill(0, SIGHUP);
	}
}

/* count 組のパイプを作り、その (新しく malloc した) 配列へのポインタを返す。
 * count が 0 なら何もせずに NULL を返す。
 * エラー時も NULL を返す。 */
static int (*create_pipes(size_t count))[2]
{
	size_t i, j;
	int (*pipes)[2];
	int dummypipe[2];

	if (!count)
		return NULL;

	/* ファイルディスクリプタ 0 番または 1 番が未使用の場合は、ダミーのパイプを
	 * 開くことで実際のパイプのファイルディスクリプタを 2 以上にする。
	 * こうしないと、後でパイプを標準入出力に繋ぎ変える時に他のパイプを
	 * 上書きしてしまう。 */
	bool usedummy = (fcntl(STDIN_FILENO,  F_GETFD) == -1 && errno == EBADF)
	             || (fcntl(STDOUT_FILENO, F_GETFD) == -1 && errno == EBADF);
	if (usedummy) {
		if (pipe(dummypipe) < 0) {
			xerror(0, errno, "pipe");
			return NULL;
		}
	}

	pipes = xmalloc(count * sizeof(int[2]));
	for (i = 0; i < count; i++) {
		if (pipe(pipes[i]) < 0) {
			xerror(0, errno, "pipe");
			goto failed;
		}
	}
	if (usedummy) { close(dummypipe[0]); close(dummypipe[1]); }
	return pipes;

failed:
	for (j = 0; j < i; j++) {
		if (close(pipes[i][0]) < 0)
			xerror(0, errno, "pipe close");
		if (close(pipes[i][1]) < 0)
			xerror(0, errno, "pipe close");
	}
	if (usedummy) { close(dummypipe[0]); close(dummypipe[1]); }
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
			xerror(0, errno, "pipe close");
		if (close(pipes.p_pipes[i][1]) < 0)
			xerror(0, errno, "pipe close");
	}
	free(pipes.p_pipes);
}

/* dup2 を確実に行う。(dup2 が EINTR を返したら、やり直す) */
static inline int xdup2(int oldfd, int newfd)
{
	int result;

	while ((result = dup2(oldfd, newfd)) < 0 && errno == EINTR);
	return result;
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
					.p_assigns = NULL,
					.p_subcmds = s,
					.p_redirs = NULL,
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
	while (p) {
		char *name = make_pipeline_name(p->pl_proc, p->pl_neg, p->pl_loop);
		exec_processes(p->pl_proc, name, p->pl_neg, p->pl_loop, false);
		free(name);
		if (!p->pl_next_cond == !laststatus)
			break;
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
	struct jproc *ps;
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

	ps = xmalloc(pcount * sizeof *ps);
	ps[0].jp_status = JS_RUNNING;
	pgid = ps[0].jp_pid = exec_single(p, loop ? -1 : 0, 0,
			bg ? BACKGROUND : FOREGROUND, pipes);
	ps[0].jp_waitstatus = 0;
	if (pgid >= 0)
		for (size_t i = 1; i < pcount; i++)
			ps[i] = (struct jproc) {
				.jp_pid = exec_single(p = p->next, i, pgid,
						bg ? BACKGROUND : FOREGROUND, pipes),
				.jp_status = JS_RUNNING,
			};
	else
		laststatus = EXIT_FAILURE;
	close_pipes(pipes);
	if (pgid > 0) {
		JOB *job = xmalloc(sizeof *job);
		assert(!joblist.contents[0]);
		joblist.contents[0] = job;
		*job = (JOB) {
			.j_pgid = pgid,
			.j_status = JS_RUNNING,
			.j_statuschanged = true,
			.j_procc = pcount,
			.j_procv = ps,
			.j_flags = 0,
			.j_name = xstrdup(name),
		};
		if (!bg) {
			do {
				wait_for_signal();
			} while (job->j_status == JS_RUNNING ||
					(!is_interactive_now && job->j_status == JS_STOPPED));
			if (WIFSTOPPED(job->j_waitstatus)) {
				laststatus = TERMSIGOFFSET + SIGTSTP;
				fflush(stdout);
				fputs("\n", stderr);
				fflush(stderr);
			} else {
				laststatus = exitcode_from_status(job->j_waitstatus);
				if (WIFSIGNALED(job->j_waitstatus)) {
					int sig = WTERMSIG(job->j_waitstatus);
					if (is_interactive_now && sig != SIGINT && sig != SIGPIPE)
						fprintf(stderr, "%s\n", xstrsignal(sig));
				}
			}
			if (neg)
				laststatus = !laststatus;
			if (job->j_status == JS_DONE)
				remove_job(0);
			else
				add_job();
		} else {
			laststatus = EXIT_SUCCESS;
			last_bg_pid = ps[pcount - 1].jp_pid;
			add_job();
		}
	} else {
		free(ps);
	}
	assert(!joblist.contents[0]);
}

/* 一つのコマンドを実行する。内部で処理できる組込みコマンドでなければ
 * fork/exec し、リダイレクトなどを設定する。
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
/* fork しないで 0 を返すことがあるのは、etype == FOREGROUND で
 * pipes.p_count == 0 の場合に限る。
 * 0 を返す場合、laststatus に値が入る。 */
static pid_t exec_single(
		PROCESS *p, ssize_t pindex, pid_t pgid, exec_t etype, PIPES pipes)
{
	bool expanded = false;
	int argc;
	char **argv;
	const char *commandpath = NULL;

	if (etype == SELF)
		goto directexec;
	
	/* fork せずに実行できる場合を処理する */
	if (etype == FOREGROUND && pipes.p_count == 0) {
		if (p->p_type == PT_NORMAL) {
			expanded = true;
			if (!expand_line(p->p_args, &argc, &argv)) {
				recfree((void **) argv, free);
				return -1;
			}
			if (!argc) {
				bool ok = true;
				if (ok && p->p_assigns) {
					ok &= assign_variables(p->p_assigns, false, false);
				}
				if (ok && p->p_redirs) {
					struct save_redirect *saver;
					ok &= open_redirections(p->p_redirs, &saver);
					undo_redirections(saver);
				}
				laststatus = ok ? EXIT_SUCCESS : EXIT_FAILURE;
				return 0;
			} else {
				BUILTIN *builtin = get_builtin(argv[0]);
				if (builtin) {  /* 組込みコマンドを fork せずに実行 */
					struct save_redirect *saver;
					if (open_redirections(p->p_redirs, &saver)) {
						bool temp = !builtin->is_special;
						bool export = builtin->is_special;
						if (assign_variables(p->p_assigns, temp, export)){
							laststatus = builtin->main(argc, argv);
							if (temp)
								unset_temporary(NULL);
							if (builtin->main == builtin_exec
									&& laststatus == EXIT_SUCCESS)
								clear_save_redirect(saver);
							else
								undo_redirections(saver);
							recfree((void **) argv, free);
							return 0;
						}
						unset_temporary(NULL);
					}
					if (posixly_correct &&
							builtin->is_special && !is_interactive_now)
						exit(EXIT_FAILURE);
					undo_redirections(saver);
					recfree((void **) argv, free);
					return -1;
				} else {
					commandpath = strchr(argv[0], '/')
						? argv[0] : get_command_fullpath(argv[0], false);
					if (!commandpath) {
						xerror(0, 0, "%s: command not found", argv[0]);
						laststatus = EXIT_NOTFOUND;
						return 0;
					}
				}
			}
		} else if (p->p_type == PT_GROUP && !p->p_redirs) {
			exec_statements(p->p_subcmds);
			return 0;
		}
	}

	/* fork する前にバッファを空にする
	 * (親と子で同じものを二回書き出すのを防ぐため) */
	fflush(NULL);

	pid_t cpid = fork();
	if (cpid < 0) {  /* fork 失敗 */
		xerror(0, errno, "%s: fork", argv[0]);
		if (expanded) { recfree((void **) argv, free); }
		return -1;
	} else if (cpid) {  /* 親プロセス */
		if (is_interactive_now) {
			if (setpgid(cpid, pgid) < 0 && errno != EACCES && errno != ESRCH)
				xerror(0, errno, "%s: setpgid (parent)", argv[0]);
			/* if (etype == FOREGROUND
					&& tcsetpgrp(STDIN_FILENO, pgid ? pgid : cpid) < 0
					&& errno != EPERM)
				error(0, errno, "%s: tcsetpgrp (parent)", argv[0]); */
		}
		if (expanded) { recfree((void **) argv, free); }
		return cpid;
	}
	
	/* 子プロセス */
	forget_orig_pgrp();
	if (is_interactive_now) {
		if (setpgid(0, pgid) < 0)
			xerror(0, errno, "%s: setpgid (child)", argv[0]);
		if (etype == FOREGROUND
				&& tcsetpgrp(STDIN_FILENO, pgid ? pgid : getpid()) < 0)
			xerror(0, errno, "%s: tcsetpgrp (child)", argv[0]);
	} else if (!is_interactive) {
		if (etype == BACKGROUND && pindex <= 0) {
			REDIR *r = xmalloc(sizeof *r);
			*r = (REDIR) {
				.next = p->p_redirs,
				.rd_type = RT_INOUT,
				.rd_fd = STDIN_FILENO,
				.rd_file = xstrdup("/dev/null"),
			};
			p->p_redirs = r;
		}
	}
	joblist_reinit();
	is_interactive_now = false;

directexec:
	unset_signals();
	assert(!is_interactive_now);
	if (pipes.p_count > 0) {
		if (pindex) {
			size_t index = ((pindex >= 0) ? (size_t)pindex : pipes.p_count) - 1;
			if (xdup2(pipes.p_pipes[index][0], STDIN_FILENO) < 0)
				xerror(0, errno, "%s: cannot connect pipe to stdin", argv[0]);
		}
		if (pindex < (ssize_t) pipes.p_count) {
			size_t index = (pindex >= 0) ? (size_t) pindex : 0;
			if (xdup2(pipes.p_pipes[index][1], STDOUT_FILENO) < 0)
				xerror(0, errno, "%s: cannot connect pipe to stdout", argv[0]);
		}
		close_pipes(pipes);
	}

	if (p->p_type == PT_NORMAL && !expanded) {
		if (!expand_line(p->p_args, &argc, &argv))
			exit(EXIT_FAILURE);
	}
	if (!open_redirections(p->p_redirs, NULL))
		exit(EXIT_FAILURE);
	assign_variables(p->p_assigns, false, true);

	switch (p->p_type) {
		BUILTIN *builtin;
		case PT_NORMAL:
			if (!argc)
				exit(EXIT_SUCCESS);

			builtin = get_builtin(argv[0]);
			if (builtin)  /* 組込みコマンドを実行 */
				exit(builtin->main(argc, argv));

			if (!commandpath)
				commandpath = strchr(argv[0], '/')
					? argv[0] : get_command_fullpath(argv[0], false);
			if (!commandpath)
				xerror(EXIT_NOTFOUND, 0, "%s: command not found", argv[0]);
			execvp(commandpath, argv);
			xerror(EXIT_NOEXEC, errno, "%s", argv[0]);
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
		exp = is_interactive
			? expand_single(r->rd_file)
			: unescape(expand_word(r->rd_file, false));
		if (!exp)
			goto returnfalse;

		/* リダイレクトをセーブする */
		if (save) {
			int copyfd = dup(r->rd_fd);
			if (copyfd < 0 && errno != EBADF) {
				xerror(0, errno, "redirect: cannot save file descriptor %d",
						r->rd_fd);
			} else if (copyfd >= 0 && fcntl(copyfd, F_SETFD, FD_CLOEXEC) == -1){
				xerror(0, errno, "redirect: fcntl(%d,SETFD,CLOEXEC)", r->rd_fd);
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
			case RT_DUPIN:
			case RT_DUPOUT:
				if (strcmp(exp, "-") == 0) {
					/* r->rd_fd を閉じる */
					if (close(r->rd_fd) < 0 && errno != EBADF)
						xerror(0, errno,
								"redirect: error on closing file descriptor %d",
								r->rd_fd);
					goto next;
				} else {
					char *end;
					errno = 0;
					fd = strtol(exp, &end, 10);
					if (errno) goto onerror;
					if (exp[0] == '\0' || end[0] != '\0') {
						xerror(0, 0, "%s: redirect syntax error", exp);
						goto returnfalse;
					}
					if (fd < 0) {
						errno = ERANGE;
						goto onerror;
					}
					if (posixly_correct) {
						int flags = fcntl(fd, F_GETFL);
						if (flags >= 0) {
							if (r->rd_type == RT_DUPIN
									&& (flags & O_ACCMODE) == O_WRONLY) {
								xerror(0, 0, "%d<&%d: file descriptor "
										"not writable", r->rd_fd, fd);
								goto returnfalse;
							}
							if (r->rd_type == RT_DUPOUT
									&& (flags & O_ACCMODE) == O_RDONLY) {
								xerror(0, 0, "%d>&%d: file descriptor "
										"not readable", r->rd_fd, fd);
								goto returnfalse;
							}
						}
					}
					if (fd != r->rd_fd) {
						if (close(r->rd_fd) < 0)
							if (errno != EBADF)
								goto onerror;
						if (xdup2(fd, r->rd_fd) < 0)
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
			if (xdup2(fd, r->rd_fd) < 0)
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
	xerror(0, errno, "redirect: %s", r->rd_file);
returnfalse:
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
			xerror(0, errno, "closing file descriptor %d", save->sr_origfd);
		if (save->sr_copyfd >= 0) {
			if (xdup2(save->sr_copyfd, save->sr_origfd) < 0)
				xerror(0, errno, "can't restore file descriptor %d from %d",
						save->sr_origfd, save->sr_copyfd);
			if (close(save->sr_copyfd) < 0)
				xerror(0, errno, "closing copied file descriptor %d",
						save->sr_copyfd);
		}
		free(save);
		save = next;
	}
}

/* セーブしたリダイレクトを削除し、元に戻せないようにする。
 * さらに引数リスト save を free する。 */
static void clear_save_redirect(struct save_redirect *save)
{
	fd_set leave;  /* 残しておく FD の集合 */
	FD_ZERO(&leave);
	while (save) {
		if (!FD_ISSET(save->sr_copyfd, &leave)) {
			if (close(save->sr_copyfd) < 0 && errno != EBADF)
				xerror(0, errno, "closing copied file descriptor %d",
						save->sr_copyfd);
		}
		/* leave には「生きている」FD のみを入れられる。 */
		if (fcntl(save->sr_origfd, F_GETFD) >= 0)
			FD_SET(save->sr_origfd, &leave);

		struct save_redirect *next = save->next;
		free(save);
		save = next;
	}
}

/* 指定したコマンドを実行し、その標準出力の内容を返す。
 * 出力結果の末尾にある改行は削除する。
 * この関数はコマンドの実行が終わるまで返らない。
 * code:    実行するコマンド
 * trimend: true なら戻り値の末尾の改行を削除してから返す。
 * 戻り値:  新しく malloc した、statements の実行結果。
 *          エラーや、statements が中止された場合は NULL。 */
char *exec_and_read(const char *code, bool trimend)
{
	int pipefd[2];
	pid_t cpid;

	if (!code || !code[0])
		return xstrdup("");

	/* コマンドの出力を受け取るためのパイプを開く */
	if (pipe(pipefd) < 0) {
		xerror(0, errno, "can't open pipe for command substitution");
		return NULL;
	}
	fflush(stdout);

	assert(!temp_chld.jp_pid);
	cpid = fork();
	if (cpid < 0) {  /* fork 失敗 */
		xerror(0, errno, "command substitution: fork");
		close(pipefd[0]);
		close(pipefd[1]);
		return NULL;
	} else if (cpid) {  /* 親プロセス */
		sigset_t newset, oldset;
		fd_set fds;
		char *buf;
		size_t len, max;
		ssize_t count;

		close(pipefd[1]);

		sigfillset(&newset);
		sigemptyset(&oldset);
		if (sigprocmask(SIG_BLOCK, &newset, &oldset) < 0) {
			xerror(0, errno, "sigprocmask");
			return NULL;
		}

		sigint_received = false;
		temp_chld = (struct jproc) { .jp_pid = cpid, .jp_status = JS_RUNNING, };
		len = 0;
		max = 100;
		buf = xmalloc(max + 1);

		for (;;) {
			handle_signals();
			FD_ZERO(&fds);
			FD_SET(pipefd[0], &fds);
			if (pselect(pipefd[0] + 1, &fds, NULL, NULL, NULL, &oldset) < 0) {
				if (errno == EINTR) {
					continue;
				} else {
					xerror(0, errno, "command substitution");
					break;
				}
			}
			if (FD_ISSET(pipefd[0], &fds)) {
				count = read(pipefd[0], buf + len, max - len);
				if (count < 0) {
					if (errno == EINTR) {
						continue;
					} else {
						xerror(0, errno, "command substitution");
						break;
					}
				} else if (count == 0) {
					break;
				}
				len += count;
				if (len + 30 >= max) {
					max *= 2;
					buf = xrealloc(buf, max + 1);
				}
			}
		}
		if (sigprocmask(SIG_SETMASK, &oldset, NULL) < 0)
			xerror(0, errno, "command substitution");
		close(pipefd[0]);
		while (temp_chld.jp_status != JS_DONE)
			wait_for_signal();
		temp_chld.jp_pid = 0;
		laststatus = exitcode_from_status(temp_chld.jp_waitstatus);
		if (WIFSIGNALED(temp_chld.jp_waitstatus)
				&& WTERMSIG(temp_chld.jp_waitstatus) == SIGINT) {
			free(buf);
			return NULL;
		}

		if (trimend)
			while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
				len--;
		if (len + 30 < max)
			buf = xrealloc(buf, len + 1);
		buf[len] = '\0';

		return buf;
	} else {  /* 子プロセス */
		sigset_t ss;

		if (is_interactive_now) {
			/* 子プロセスが SIGTSTP で停止してしまうと、親プロセスである
			 * 対話的なシェルに制御が戻らなくなってしまう。よって、SIGTSTP を
			 * 受け取っても停止しないようにブロックしておく。 */
			sigemptyset(&ss);
			sigaddset(&ss, SIGTSTP);
			sigprocmask(SIG_BLOCK, &ss, NULL);
		}

		forget_orig_pgrp();
		joblist_reinit();
		is_interactive_now = false;

		close(pipefd[0]);
		if (pipefd[1] != STDOUT_FILENO) {
			/* ↑ この条件が成り立たないことは普通考えられないが…… */
			if (close(STDOUT_FILENO) < 0)
				xerror(0, errno, "command substitution");
			if (xdup2(pipefd[1], STDOUT_FILENO) < 0)
				xerror(2, errno, "command substitution");
			if (close(pipefd[1]) < 0)
				xerror(0, errno, "command substitution");
		}
		exec_source_and_exit(code, "command substitution");
	}
}
