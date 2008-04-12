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
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#ifdef HAVE_GETTEXT
# include <libintl.h>
#endif
#include "option.h"
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "path.h"
#include "variable.h"
#include "parser.h"
#include "sig.h"
#include "expand.h"
#include "redir.h"
#include "job.h"
#include "exec.h"
#include "yash.h"


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

/* TODO exec.c: main_T: should be moved to builtin.h */
typedef int main_T(int argc, char **argv);

/* 実行するコマンドの情報 */
typedef struct commandinfo_T {
    enum {                  /* コマンドの種類 */
	externalprogram,      /* 外部コマンド */
	builtin,              /* 組込みコマンド */
	function,             /* 関数 */
    } type;
    union {
	const char *path;           /* コマンドのパス (externalprogram 用) */
	main_T *builtin;            /* 組込みコマンドの本体 */
	const command_T *function;  /* 関数の本体 */
    } value;
} commandinfo_T;
#define ci_path     value.path
#define ci_builtin  value.builtin
#define ci_function value.function

static void exec_pipelines(const pipeline_T *p, bool finally_exit);
static void exec_pipelines_async(const pipeline_T *p)
    __attribute__((nonnull));
static void exec_if(const command_T *c, bool finally_exit)
    __attribute__((nonnull));
static void exec_for(const command_T *c, bool finally_exit)
    __attribute__((nonnull));
static void exec_while(const command_T *c, bool finally_exit)
    __attribute__((nonnull));
static void exec_case(const command_T *c, bool finally_exit)
    __attribute__((nonnull));
static inline void next_pipe(pipeinfo_T *pi, bool next)
    __attribute__((nonnull));
static void exec_commands(const command_T *c, exec_T type, bool looppipe);
static pid_t exec_process(
	const command_T *c, exec_T type, pipeinfo_T *pi, pid_t pgid)
    __attribute__((nonnull));
static pid_t fork_and_reset(pid_t pgid, bool fg);
static bool search_command(
	const char *restrict name, commandinfo_T *restrict ci);
static void exec_nonsimple_command(const command_T *c, bool finally_exit)
    __attribute__((nonnull));
static void exec_simple_command(const commandinfo_T *ci,
	int argc, char *const *argv, bool finally_exit)
    __attribute__((nonnull));


/* 最後に実行したコマンドの終了ステータス */
int laststatus = EXIT_SUCCESS;
/* 最後に起動した非同期実行リストのプロセス ID */
pid_t lastasyncpid;


/* コマンドを実行する。
 * finally_exit: true なら実行後そのまま終了する。 */
void exec_and_or_lists(const and_or_T *a, bool finally_exit)
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
void exec_pipelines(const pipeline_T *p, bool finally_exit)
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

/* 一つ以上のパイプラインを非同期的に実行する。 */
void exec_pipelines_async(const pipeline_T *p)
{
    if (!p->next && !p->pl_neg) {
	exec_commands(p->pl_commands, execasync, p->pl_loop);
    } else {
	pid_t cpid = fork_and_reset(0, false);
	
	if (cpid > 0) {
	    /* 親プロセス: 新しいジョブを登録する */
	    job_T *job = xmalloc(sizeof *job + sizeof *job->j_procs);
	    process_T *ps = job->j_procs;

	    ps->pr_pid = cpid;
	    ps->pr_status = JS_RUNNING;
	    ps->pr_statuscode = 0;
	    ps->pr_name = pipelines_to_wcs(p);

	    job->j_pgid = do_job_control ? cpid : 0;
	    job->j_status = JS_RUNNING;
	    job->j_statuschanged = true;
	    job->j_loop = false;
	    job->j_pcount = 1;

	    set_active_job(job);
	    add_job();
	    laststatus = EXIT_SUCCESS;
	    lastasyncpid = cpid;
	} else if (cpid == 0) {
	    /* 子プロセス: コマンドを実行して終了 */
	    block_sigquit_and_sigint();
	    //if (!is_stdin_redirected)
	    //	(void) 0;
		// TODO exec.c: exec_pipelines_async: stdin を /dev/null に
	    exec_pipelines(p, true);
	    assert(false);
	}
    }
}

/* if コマンドを実行する */
void exec_if(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_IF);

    for (const ifcommand_T *cmds = c->c_ifcmds; cmds; cmds = cmds->next) {
	if (cmds->ic_condition) {
	    exec_and_or_lists(cmds->ic_condition, false);
	    if (laststatus != EXIT_SUCCESS)
		continue;
	}
	exec_and_or_lists(cmds->ic_commands, finally_exit);
	return;
    }
    laststatus = 0;
    if (finally_exit)
	exit(laststatus);
}

/* for コマンドを実行する */
void exec_for(const command_T *c, bool finally_exit)
{
    // TODO exec.c: exec_for: 未実装
    (void) c;
    laststatus = 0;
    if (finally_exit)
	exit(laststatus);
}

/* while コマンドを実行する */
void exec_while(const command_T *c, bool finally_exit)
{
    // TODO exec.c: exec_while: 未実装
    (void) c;
    laststatus = 0;
    if (finally_exit)
	exit(laststatus);
}

/* case コマンドを実行する */
void exec_case(const command_T *c, bool finally_exit)
{
    // TODO exec.c: exec_case: 未実装
    (void) c;
    laststatus = 0;
    if (finally_exit)
	exit(laststatus);
}

/* exec_commands 関数で使うサブルーチン */
/* 次のプロセスの処理のために pipeinfo_T の内容を更新する。
 * pi->pi_fromprevfd, pi->pi_tonextfds[PIDX_OUT] を閉じ、
 * pi->pi_tonextfds[PIDX_IN] を pi->pi_fromprevfd に移し、
 * next が true なら新しいパイプを開いて pi->pi_tonextfds に入れる。
 * next が false なら pi->pi_loopoutfd を pi->pi_tonextfds[PIDX_OUT] に移し、
 * pi->pi_loopoutfd と pi->pi_tonextfds[PIDX_IN] を -1 にする。
 * 成功すると true、エラーがあると false を返す。 */
inline void next_pipe(pipeinfo_T *pi, bool next)
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
    xerror(errno, Ngt("cannot open pipe"));
}

/* 一つのパイプラインを構成する各コマンドを実行する
 * c:        実行する一つ以上のコマンド
 * type:     実行のしかた
 * looppipe: パイプをループ状にするかどうか */
void exec_commands(const command_T *c, exec_T type, bool looppipe)
{
    size_t count;
    pid_t pgid;
    const command_T *cc;
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
	    xerror(errno, Ngt("cannot open pipe"));
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

	next_pipe(&pinfo, i < count - 1);
	pid = exec_process(cc,
		(type == execself && i < count - 1) ? execnormal : type,
		&pinfo,
		pgid);
	if (pid < 0)
	    goto fail;
	ps[i].pr_pid = pid;
	if (pid) {
	    ps[i].pr_status = JS_RUNNING;
	    ps[i].pr_statuscode = 0;
	} else {
	    ps[i].pr_status = JS_DONE;
	    ps[i].pr_statuscode = laststatus;
	}
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
    job->j_pgid = do_job_control ? pgid : 0;
    job->j_status = JS_RUNNING;
    job->j_statuschanged = true;
    job->j_loop = looppipe;
    job->j_pcount = count;
    if (type == execnormal) {   /* ジョブの終了を待つ */
	wait_for_job(ACTIVE_JOBNO, do_job_control);
	laststatus = calc_status_of_job(job);
	handle_traps();
	if (job->j_status == JS_DONE) {
	    remove_job(ACTIVE_JOBNO);
	    return;
	}
    } else {
	laststatus = EXIT_SUCCESS;
	lastasyncpid = ps[count - 1].pr_pid;
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
 * 0 を返すとき、exec_process 内で laststatus を設定する。 */
// TODO exec.c: exec_process: 本当に -1 を返す場合があるか考え直す
pid_t exec_process(const command_T *c, exec_T type, pipeinfo_T *pi, pid_t pgid)
{
    bool need_fork;    /* fork するかどうか */
    bool finally_exit; /* true なら最後に exit してこの関数から返らない */
    int argc;
    char **argv = NULL;
    commandinfo_T cmdinfo;

    current_lineno = c->c_lineno;

    switch (c->c_type) {
    case CT_SIMPLE:
	if (!expand_line(c->c_words, &argc, &argv)) {
	    laststatus = 1;
	    goto done;
	}
	if (argc == 0) {
	    need_fork = finally_exit = false;
#ifndef NDEBUG  /* GCC の警告を黙らせる小細工 */
	    cmdinfo.type = externalprogram;
#endif
	} else {
	    if (!search_command(argv[0], &cmdinfo)) {
		xerror(0, Ngt("%s: command not found"), argv[0]);
		laststatus = EXIT_NOTFOUND;
		recfree((void **) argv, free);
		goto done;
	    }
	    /* 外部コマンドは fork し、組込みコマンドや関数は fork しない。 */
	    need_fork = finally_exit = (cmdinfo.type == externalprogram);
	}
	break;
    case CT_SUBSHELL:
	need_fork = finally_exit = true;
	break;
    default:
	need_fork = finally_exit = false;
	break;
    }
    /* argc, argv, cmdinfo は CT_SIMPLE でしか使わない */

    if (type == execself) {
	/* execself は絶対に fork しないで自分で実行して終了 */
	need_fork = false;  finally_exit = true;
	assert(!is_interactive_now);
    } else if (type == execasync
	    || pi->pi_fromprevfd >= 0 || pi->pi_tonextfds[PIDX_OUT] >= 0) {
	/* 非同期実行またはパイプを繋ぐ場合は fork */
	need_fork = finally_exit = true;
    }

    assert(!need_fork || finally_exit);  /* fork すれば子プロセスは必ず exit */
    if (need_fork) {
	pid_t cpid = fork_and_reset(pgid, type == execnormal);
	if (cpid != 0) {
	    recfree((void **) argv, free);
	    return cpid;
	}
    }
    if (finally_exit) {
	if (!do_job_control && type == execasync)
	    block_sigquit_and_sigint();
	reset_signals();
    }

    /* パイプを繋ぎ、余ったパイプを閉じる */
    if (pi->pi_fromprevfd >= 0) {
	xdup2(pi->pi_fromprevfd, STDIN_FILENO);
	xclose(pi->pi_fromprevfd);
    }
    if (pi->pi_tonextfds[PIDX_OUT] >= 0) {
	xdup2(pi->pi_tonextfds[PIDX_OUT], STDOUT_FILENO);
	xclose(pi->pi_tonextfds[PIDX_OUT]);
    }
    if (pi->pi_tonextfds[PIDX_IN] >= 0)
	xclose(pi->pi_tonextfds[PIDX_IN]);
    if (pi->pi_loopoutfd >= 0)
	xclose(pi->pi_loopoutfd);

    /* リダイレクトを開く */
    // TODO exec.c: exec_process: redirect
    
    /* 非対話的シェルで非同期実行する場合は stdin を /dev/null にリダイレクト */
    if (type == execasync && !is_interactive && pi->pi_fromprevfd < 0
	    /* TODO exec.c:  && !is_stdin_redirected */) {
    }

    /* 変数を代入する */
    // TODO exec.c: exec_process: assignment

    /* コマンドを実行する */
    if (c->c_type == CT_SIMPLE) {
	if (argc != 0)
	    exec_simple_command(&cmdinfo, argc, argv, finally_exit);
	recfree((void **) argv, free);
    } else {
	exec_nonsimple_command(c, finally_exit);
    }
    if (finally_exit)
	exit(laststatus);

    // TODO exec.c: exec_process: 変数の一時代入を削除
    // TODO exec.c: exec_process: セーブしたリダイレクトを戻す
    return 0;

done:
    if (type == execself)
	exit(laststatus);
    return 0;
}

/* fork して、いろいろ必要な設定を行う。
 * pgid:   ジョブ制御有効時、fork 後に子プロセスに設定するプロセスグループ ID。
 *         子プロセスのプロセス ID をプロセスグループ ID にする場合は 0。
 *         ジョブ制御が有効でもプロセスグループを変えない場合は負数。
 * fg:     プロセスグループ設定後、子プロセスをフォアグラウンドにするかどうか。
 * 戻り値: fork の戻り値 */
pid_t fork_and_reset(pid_t pgid, bool fg)
{
    fflush(NULL);

    pid_t cpid = fork();

    if (cpid < 0) {
	/* fork 失敗 */
	xerror(errno, Ngt("fork: cannot make child process"));
    } else if (cpid > 0) {
	/* 親プロセス */
	if (do_job_control && pgid >= 0)
	    setpgid(cpid, pgid);
    } else {
	/* 子プロセス */
	if (do_job_control && pgid >= 0) {
	    setpgid(0, pgid);
	    if (fg)
		(void) 0; // TODO exec: fork_and_reset: tcsetpgrp
	}
	// TODO exec: fork_and_reset: forget_original_pgrp
	remove_all_jobs();
	clear_traps();
	// TODO exec: fork_and_reset: clear_shellfds
	do_job_control = is_interactive_now = false;
    }
    return cpid;
}

/* コマンドの種類を特定し、コマンドのありかを探す。
 * コマンドが見付かれば結果を *ci に入れて true を返す。
 * 見付からなければ false を返し、*ci は不定 */
bool search_command(const char *restrict name, commandinfo_T *restrict ci)
{
    wchar_t *wname = malloc_mbstowcs(name);
    bool slash = wname && wcschr(wname, L'/');
    free(wname);
    if (slash) {  /* name にスラッシュが入っていれば name をそのまま返す */
	ci->type = externalprogram;
	ci->ci_path = name;
	return true;
    }

    // TODO exec.c: find_command: 暫定実装
    ci->type = externalprogram;
    ci->ci_path = get_command_path(name, false);
    return ci->ci_path != NULL;
}

/* CT_SIMPLE でない一つのコマンドを実行する。
 * c->c_redirs は実行しない */
void exec_nonsimple_command(const command_T *c, bool finally_exit)
{
    switch (c->c_type) {
    case CT_SIMPLE:
	assert(false);
    case CT_SUBSHELL:
    case CT_GROUP:
	exec_and_or_lists(c->c_subcmds, finally_exit);
	break;
    case CT_IF:
	exec_if(c, finally_exit);
	break;
    case CT_FOR:
	exec_for(c, finally_exit);
	break;
    case CT_WHILE:
	exec_while(c, finally_exit);
	break;
    case CT_CASE:
	exec_case(c, finally_exit);
	break;
    }
}

/* 単純コマンドを実行する
 * ci:   実行するコマンドの情報
 * argv: コマンド名と引数。マルチバイト文字列へのポインタの配列へのポインタ。 */
void exec_simple_command(
	const commandinfo_T *ci, int argc, char *const *argv, bool finally_exit)
{
    assert(argv[argc] == NULL);

    switch (ci->type) {
    case externalprogram:
	execv(ci->ci_path, (char **) argv);
	if (errno != ENOEXEC) {
	    if (errno == EACCES && is_directory(ci->ci_path))
		errno = EISDIR;
	    if (strcmp(argv[0], ci->ci_path) == 0)
		xerror(errno, Ngt("cannot execute `%s'"),
			(char *) argv[0]);
	    else
		xerror(errno, Ngt("cannot execute `%s' (%s)"),
			(char *) argv[0], ci->ci_path);
	    exit(EXIT_NOEXEC);
	}
	// TODO exec.c: exec_simple_command: コマンドをシェルスクリプトとして実行
	break;
    case builtin:
	laststatus = ci->ci_builtin(argc, (char **) argv);
	break;
    case function:
	// TODO exec.c: exec_simple_command: 関数のリダイレクト
	exec_nonsimple_command(ci->ci_function, finally_exit);
	break;
    }
    if (finally_exit)
	exit(laststatus);
}

/* コマンド置換を実行する。
 * 指定したコードを実行し、その標準出力を返す。
 * この関数はコマンドの実行が終わるまで返らない。
 * 戻り値: 新しく malloc したコマンドの出力した文字列。末尾の改行は除いてある。
 *         エラーや、中止した場合は NULL。 */
wchar_t *exec_command_substitution(const wchar_t *code)
{
    int pipefd[2];
    pid_t cpid;

    if (!code[0])
	return xwcsdup(L"");

    /* コマンドの出力を受け取るためのパイプを開く */
    if (pipe(pipefd) < 0) {
	xerror(errno, Ngt("cannot open pipe for command substitution"));
	return NULL;
    }

    cpid = fork_and_reset(-1, false);
    if (cpid < 0) {
	/* fork 失敗 */
	xclose(pipefd[PIDX_IN]);
	xclose(pipefd[PIDX_OUT]);
	return NULL;
    } else if (cpid) {
	/* 親プロセス */
	FILE *f;

	xclose(pipefd[PIDX_OUT]);
	f = fdopen(pipefd[PIDX_IN], "r");
	if (!f) {
	    xerror(errno, Ngt("cannot open pipe for command substitution"));
	    xclose(pipefd[PIDX_IN]);
	    return NULL;
	}

	/* 子プロセスの出力を読む */
	xwcsbuf_T buf;
	wb_init(&buf);
	for (;;) {
	    wint_t c = fgetwc(f);
	    if (c == WEOF) {
		if (feof(f) || errno != EINTR)
		    break;
	    } else {
		wb_wccat(&buf, c);
	    }
	}
	fclose(f);

	/* 子プロセスの終了を待つ */
	job_T *job = xmalloc(sizeof *job + sizeof *job->j_procs);
	job->j_pgid = 0;
	job->j_status = JS_RUNNING;
	job->j_statuschanged = false;
	job->j_loop = false;
	job->j_pcount = 1;
	job->j_procs[0].pr_pid = cpid;
	job->j_procs[0].pr_status = JS_RUNNING;
	job->j_procs[0].pr_statuscode = 0;
	job->j_procs[0].pr_name = NULL;
	set_active_job(job);
	wait_for_job(ACTIVE_JOBNO, false);
	laststatus = calc_status_of_job(job);
	remove_job(ACTIVE_JOBNO);

	/* 末尾の改行を削って返す */
	while (buf.length > 0 && buf.contents[buf.length - 1] == L'\n')
	    wb_remove(&buf, buf.length - 1, 1);
	return wb_towcs(&buf);
    } else {
	/* 子プロセス */

	/* 子プロセスが SIGTSTP で停止してしまうと、親プロセスである
	 * 対話的なシェルに制御が戻らなくなってしまう。よって、SIGTSTP を
	 * 受け取っても停止しないようにブロックしておく。 */
	if (is_interactive_now)
	    block_sigtstp();
	reset_signals();

	xclose(pipefd[PIDX_IN]);
	if (pipefd[PIDX_OUT] != STDOUT_FILENO) {  /* パイプを繋ぐ */
	    xdup2(pipefd[PIDX_OUT], STDOUT_FILENO);
	    xclose(pipefd[PIDX_OUT]);
	}

	exec_wcs(code, gt("command substitution"), true);
	assert(false);
    }
}


/* vim: set ts=8 sts=4 sw=4 noet: */
