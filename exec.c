/* Yash: yet another shell */
/* exec.c: command execution */
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
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wchar.h>
#include "util.h"
#include "parser.h"
#include "redir.h"
#include "job.h"
#include "exec.h"


/* コマンドの実行のしかたを表す */
typedef enum {
	execnormal,  /* 普通に実行 */
	execasync,   /* 非同期的に実行 */
	execself,    /* 自分自身のプロセスで実行 */
} exec_T;

/* パイプのファイルディスクリプタの情報 */
typedef struct pipeinfo_T {
	int pi_fromprevfd;      /* 前のプロセスとのパイプ */
	int pi_tonextfds[2];    /* 次のプロセスとのパイプ */
	int pi_loopoutfd;       /* ループパイプの書き込み側 */
} pipeinfo_T;
/* 使用しない部分は無効な値として -1 を入れる。 */
#define PIDX_IN  0   /* パイプの読み込み側の FD のインデックス */
#define PIDX_OUT 1   /* パイプの書き込み側の FD のインデックス */
#define PIPEINFO_INIT { -1, { -1, -1 }, -1 }  /* pipeinfo_T の初期化用 */

static void exec_pipelines(pipeline_T *p, bool finally_exit);
static void exec_pipelines_async(pipeline_T *p);
static inline void next_pipe(pipeinfo_T *pi, bool next)
	__attribute__((nonnull));
static void exec_commands(command_T *c, exec_T type, bool looppipe);
static pid_t exec_process(command_T *c, exec_T type, pipeinfo_T *pi, pid_t pgid)
	__attribute__((nonnull));


/* 最後に実行したコマンドの終了ステータス */
int laststatus = EXIT_SUCCESS;


/* コマンドを実行する。
 * finally_exit: true なら実行後そのまま終了する。 */
void exec_and_or_lists(and_or_T *a, bool finally_exit)
{
	while (a) {
		if (!a->ao_async)
			exec_pipelines(a->ao_pipelines, finally_exit && !a->next);
		else
			exec_pipelines_async(a->ao_pipelines);

		a = a->next;
	}
	if (finally_exit)
		exit(laststatus);
}

/* パイプラインたちを実行する。 */
static void exec_pipelines(pipeline_T *p, bool finally_exit)
{
	while (p) {
		exec_commands(p->pl_commands,
			(finally_exit && !p->next && !p->pl_neg) ? execself : execnormal,
			p->pl_loop);
		if (p->pl_neg)
			laststatus = !laststatus;

		if (p->pl_next_cond == !!laststatus)
			break;
		p = p->next;
	}
	if (finally_exit)
		exit(laststatus);
}

/* パイプラインたちを非同期的に実行する。 */
static void exec_pipelines_async(pipeline_T *p)
{
	(void) p;
	xerror(0,0,"%s: NOT IMPLEMENTED", __func__);
	// TODO exec.c: exec_pipelines_async
}

/* exec_commands 関数で使うサブルーチン */
/* 次のプロセスの処理のために pipeinfo_T の内容を更新する。
 * pi->pi_fromprevfd, pi->pi_tonextfds[PIDX_OUT] を閉じ、
 * pi->pi_tonextfds[PIDX_IN] を pi->pi_fromprevfd に移し、
 * next が true なら新しいパイプを開いて pi->pi_tonextfds に入れる。
 * next が false なら pi->pi_loopoutfd を pi->pi_tonextfds[PIDX_OUT] に移し、
 * pi->pi_loopoutfd と pi->pi_tonextfds[PIDX_IN] を -1 にする。
 * 成功すると true、エラーがあると false を返す。 */
static inline void next_pipe(pipeinfo_T *pi, bool next)
{
	if (pi->pi_fromprevfd >= 0)
		xclose(pi->pi_fromprevfd);
	if (pi->pi_tonextfds[PIDX_OUT] >= 0)
		xclose(pi->pi_tonextfds[PIDX_OUT]);
	pi->pi_fromprevfd = pi->pi_tonextfds[PIDX_IN];
	if (next) {
		/* ファイルディスクリプタ 0 または 1 が未使用の場合は、ダミーのパイプを
		 * 開くことで実際のパイプのファイルディスクリプタを 2 以上にする。
		 * こうしないと、後でパイプを標準入出力に繋ぎ変える時に他のパイプを
		 * 上書きしてしまう。 */
		int dummy[2];
		bool usedummy = (fcntl(STDIN_FILENO,  F_GETFD) == -1 && errno == EBADF)
		             || (fcntl(STDOUT_FILENO, F_GETFD) == -1 && errno == EBADF);

		if (usedummy && pipe(dummy) < 0)
			goto fail;
		if (pipe(pi->pi_tonextfds) < 0)
			goto fail;
		if (usedummy) {
			xclose(dummy[PIDX_IN]);
			xclose(dummy[PIDX_OUT]);
		}
	} else {
		pi->pi_tonextfds[PIDX_IN] = -1;
		pi->pi_tonextfds[PIDX_OUT] = pi->pi_loopoutfd;
		pi->pi_loopoutfd = -1;
	}
	return;

fail:
	pi->pi_tonextfds[PIDX_IN] = pi->pi_tonextfds[PIDX_OUT] = -1;
	xerror(0, errno, Ngt("cannot open pipe"));
}

/* 一つのパイプラインを構成する各コマンドを実行する
 * c:        実行する一つ以上のコマンド
 * type:     実行のしかた
 * looppipe: パイプをループ状にするかどうか */
static void exec_commands(command_T *c, exec_T type, bool looppipe)
{
	size_t count;
	pid_t pgid;
	command_T *cc;
	job_T *job;
	process_T *ps;
	pipeinfo_T pinfo = PIPEINFO_INIT;

	/* コマンドの数を数える */
	count = 0;
	for (cc = c; cc; cc = cc->next)
		count++;
	assert(count > 0);

	if (looppipe) {  /* 最初と最後を繋ぐパイプを用意する */
		int fds[2];
		if (pipe(fds) < 0) {
			xerror(0, errno, Ngt("cannot open pipe"));
		} else {
			pinfo.pi_tonextfds[PIDX_IN] = fds[PIDX_IN];
			pinfo.pi_loopoutfd = fds[PIDX_OUT];
		}
	}

	job = xmalloc(sizeof *job + count * sizeof *job->j_procs);
	ps = job->j_procs;

	/* 各コマンドを実行 */
	pgid = 0;
	cc = c;
	for (size_t i = 0; i < count; i++) {
		pid_t pid;

		next_pipe(&pinfo, i == count - 1);
		pid = exec_process(cc,
				(type == execself && i < count - 1) ? execnormal : type,
				&pinfo, pgid);
		if (pid < 0)
			goto fail;
		ps[i].pr_pid = pid;
		ps[i].pr_status = JS_RUNNING;
		ps[i].pr_waitstatus = 0;
		ps[i].pr_name = NULL;   /* 最初は名無し: 名前は後で付ける */
		if (pgid == 0)
			pgid = pid;
		cc = cc->next;
	}
	assert(cc == NULL);
	assert(type != execself);  /* execself なら exec_process は返らない */
	assert(pinfo.pi_tonextfds[PIDX_IN] < 0);
	assert(pinfo.pi_loopoutfd < 0);
	if (pinfo.pi_fromprevfd >= 0)
		xclose(pinfo.pi_fromprevfd);           /* 残ったパイプを閉じる */
	if (pinfo.pi_tonextfds[PIDX_OUT] >= 0)
		xclose(pinfo.pi_tonextfds[PIDX_OUT]);  /* 残ったパイプを閉じる */

	if (pgid == 0) {  /* fork しなかったらもうやることはない */
		free(job);
		return;
	}

	set_active_job(job);
	if (type == execnormal) {   /* ジョブの終了を待つ */
		job->j_pgid = do_job_control ? pgid : 0;
		job->j_status = JS_RUNNING;
		job->j_statuschanged = true;
		job->j_loop = looppipe;
		job->j_pcount = count;
		wait_for_job(ACTIVE_JOBNO, do_job_control);
		if (job->j_status == JS_DONE) {
			remove_job(ACTIVE_JOBNO);
			return;
		}
	}

	/* バックグラウンドジョブをジョブリストに追加する */
	cc = c;
	for (size_t i = 0; i < count; i++) {  /* 各プロセスの名前を設定 */
		ps[i].pr_name = command_to_wcs(cc);
		cc = cc->next;
	}
	add_job();
	return;

fail:
	laststatus = -2;
	if (pinfo.pi_fromprevfd >= 0)
		xclose(pinfo.pi_fromprevfd);
	if (pinfo.pi_tonextfds[PIDX_IN] >= 0)
		xclose(pinfo.pi_tonextfds[PIDX_IN]);
	if (pinfo.pi_tonextfds[PIDX_OUT] >= 0)
		xclose(pinfo.pi_tonextfds[PIDX_OUT]);
	if (pinfo.pi_loopoutfd >= 0)
		xclose(pinfo.pi_loopoutfd);
	free(job);
}

/* 一つのコマンドを実行する。
 * c:      実行するコマンド
 * type:   実行のしかた
 * pi:     繋ぐパイプのファイルディスクリプタ情報
 * pgid:   ジョブ制御有効時、fork 後に子プロセスに設定するプロセスグループ ID。
 *         子プロセスのプロセス ID をプロセスグループ ID にする場合は 0。
 * 戻り値: fork した場合、子プロセスのプロセス ID。fork せずにコマンドを実行
 *         できた場合は 0。重大なエラーが生じたら -1。
 * 0 を返すとき、exec_process 内で laststatus を設定する。
 * パイプを一方でも繋いだら、必ず fork する。この時 type == execself は不可。 */
static pid_t exec_process(command_T *c, exec_T type, pipeinfo_T *pi, pid_t pgid)
{
	wchar_t *name = command_to_wcs(c);
	fprintf(stderr, "DEBUG(%s:%d) <%ls>\n", __func__, __LINE__, name);
	free(name);
	(void) type, (void) pi, (void) pgid;
	// TODO exec.c: exec_process
	return -1;
}
