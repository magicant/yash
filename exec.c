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
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#if HAVE_PATHS_H
# include <paths.h>
#endif
#include "option.h"
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "wfnmatch.h"
#include "path.h"
#include "input.h"
#include "parser.h"
#include "variable.h"
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
    enum cmdtype_T {        /* コマンドの種類 */
	externalprogram,      /* 外部コマンド */
	specialbuiltin,       /* 特殊組込みコマンド */
	semispecialbuiltin,   /* 準特殊組込みコマンド */
	regularbuiltin,       /* 普通の組込みコマンド */
	function,             /* 関数 */
    } type;
    union {
	const char *path;     /* コマンドのパス (externalprogram 用) */
	main_T *builtin;      /* 組込みコマンドの本体 */
	command_T *function;  /* 関数の本体 */
    } value;
} commandinfo_T;
#define ci_path     value.path
#define ci_builtin  value.builtin
#define ci_function value.function

static inline bool need_break(void)
    __attribute__((pure));

static void exec_pipelines(const pipeline_T *p, bool finally_exit);
static void exec_pipelines_async(const pipeline_T *p)
    __attribute__((nonnull));
static void exec_if(const command_T *c, bool finally_exit)
    __attribute__((nonnull));
static inline bool exec_condition(const and_or_T *c);
static void exec_for(const command_T *c, bool finally_exit)
    __attribute__((nonnull));
static void exec_while(const command_T *c, bool finally_exit)
    __attribute__((nonnull));
static void exec_case(const command_T *c, bool finally_exit)
    __attribute__((nonnull));

static inline void next_pipe(pipeinfo_T *pi, bool next)
    __attribute__((nonnull));
static inline void connect_pipes(pipeinfo_T *pi)
    __attribute__((nonnull));

static void exec_commands(command_T *c, exec_T type, bool looppipe);
static pid_t exec_process(command_T *c, exec_T type, pipeinfo_T *pi, pid_t pgid)
    __attribute__((nonnull));
static pid_t fork_and_reset(pid_t pgid, bool fg);
static void search_command(
	const char *restrict name, commandinfo_T *restrict ci);
static bool do_assignments_for_command_type(
	const assign_T *asgns, enum cmdtype_T type);
static void exec_nonsimple_command(command_T *c, bool finally_exit)
    __attribute__((nonnull));
static void exec_simple_command(const commandinfo_T *ci,
	int argc, char *const *argv, bool finally_exit)
    __attribute__((nonnull));
static void print_xtrace(char *const *argv)
    __attribute__((nonnull));
static void exec_fall_back_on_sh(
	int argc, char *const *argv, char *const *env, const char *path)
    __attribute__((nonnull(2,4)));
static void exec_function_body(command_T *body, char **args, bool finally_exit)
    __attribute__((nonnull));
static inline int xexecv(const char *path, char *const *argv)
    __attribute__((nonnull(1)));
static inline int xexecve(
	const char *path, char *const *argv, char *const *envp)
    __attribute__((nonnull(1)));


/* 最後に実行したコマンドの終了ステータス */
int laststatus = EXIT_SUCCESS;
/* 最後に起動した非同期実行リストのプロセス ID */
pid_t lastasyncpid;

/* if 文の条件文や and-or リスト内のコマンドを実行する際、
 * -o errexit オプションの効果を抑制するためにこの変数が true になる。 */
static bool supresserrexit;

/* 現在実行中のループと break/continue/return の動作状況 */
struct execinfo {
    unsigned loopnest;    /* 現在実行中のループの深さ */
    unsigned breakcount;  /* break すべきループの数 (<= loopnest) */
    enum { ee_none, ee_continue, ee_return
    } exception;          /* 行うべき例外的処理 */
} execinfo;
/* "continue n" は (n-1) 回の break と 1 回の continue として扱う。
 * exception が ee_return なら breakcount は 0 である。 */
/* ループの内部でサブシェルを作った場合でも execinfo はリセットしないので注意 */
#define EXECINFO_INIT { 0, 0, ee_none }  /* used to initialize `execinfo' */

/* 現在 break/continue/return すべき状態なら true を返す */
bool need_break(void)
{
    return execinfo.breakcount > 0 || execinfo.exception != ee_none;
}

/* 現在の execinfo をセーブし、戻り値として返す。
 * execinfo は初期状態に戻る。 */
struct execinfo *save_execinfo(void)
{
    struct execinfo *save = xmalloc(sizeof execinfo);
    *save = execinfo;
    execinfo = (struct execinfo) EXECINFO_INIT;
    return save;
}

/* execinfo をセーブしておいたものに戻す。引数は関数内で解放する。 */
void load_execinfo(struct execinfo *save)
{
    execinfo = *save;
    free(save);
}


/* コマンドを実行する。
 * finally_exit: true なら実行後そのまま終了する。 */
void exec_and_or_lists(const and_or_T *a, bool finally_exit)
{
    while (a && !need_break()) {
	if (!a->ao_async)
	    exec_pipelines(a->ao_pipelines, finally_exit && !a->next);
	else
	    exec_pipelines_async(a->ao_pipelines);

	a = a->next;
    }
    if (finally_exit)
	exit_shell();
}

/* パイプラインたちを実行する。 */
void exec_pipelines(const pipeline_T *p, bool finally_exit)
{
    for (bool first = true; p && !need_break(); p = p->next, first = false) {
	if (!first && p->pl_cond == !!laststatus)
	    continue;

	bool savesee = supresserrexit;
	supresserrexit |= p->pl_neg || p->next;

	exec_commands(p->pl_commands,
	    (finally_exit && !p->next && !p->pl_neg) ? execself : execnormal,
	    p->pl_loop);
	if (p->pl_neg)
	    laststatus = !laststatus;

	supresserrexit = savesee;
    }
    if (finally_exit)
	exit_shell();
}

/* 一つ以上のパイプラインを非同期的に実行する。 */
void exec_pipelines_async(const pipeline_T *p)
{
    assert(!need_break());

    if (!p->next && !p->pl_neg) {
	exec_commands(p->pl_commands, execasync, p->pl_loop);
    } else {
	bool doingjobcontrol = doing_job_control_now;
	pid_t cpid = fork_and_reset(0, false);
	
	if (cpid > 0) {
	    /* 親プロセス: 新しいジョブを登録する */
	    job_T *job = xmalloc(sizeof *job + sizeof *job->j_procs);
	    process_T *ps = job->j_procs;

	    ps->pr_pid = cpid;
	    ps->pr_status = JS_RUNNING;
	    ps->pr_statuscode = 0;
	    ps->pr_name = pipelines_to_wcs(p);

	    job->j_pgid = doing_job_control_now ? cpid : 0;
	    job->j_status = JS_RUNNING;
	    job->j_statuschanged = true;
	    job->j_loop = false;
	    job->j_pcount = 1;

	    set_active_job(job);
	    add_job(false);
	    laststatus = EXIT_SUCCESS;
	    lastasyncpid = cpid;
	} else if (cpid == 0) {
	    /* 子プロセス: コマンドを実行して終了 */
	    if (!doingjobcontrol)
		block_sigquit_and_sigint();
	    maybe_redirect_stdin_to_devnull();
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
	if (need_break())
	    goto done;
	if (exec_condition(cmds->ic_condition)) {
	    exec_and_or_lists(cmds->ic_commands, finally_exit);
	    return;
	}
    }
    laststatus = 0;
done:
    if (finally_exit)
	exit_shell();
}

/* if/while/until コマンドの条件を実行する */
bool exec_condition(const and_or_T *c)
{
    if (c) {
	bool savesee = supresserrexit;
	supresserrexit = true;
	exec_and_or_lists(c, false);
	supresserrexit = savesee;
	return laststatus == EXIT_SUCCESS;
    } else {
	return true;
    }
}

/* for コマンドを実行する */
void exec_for(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_FOR);
    assert(!need_break());
    execinfo.loopnest++;

    int count;
    void **words;

    if (c->c_forwords) {
	char **mbswords;
	if (!expand_line(c->c_forwords, &count, &mbswords)) {
	    laststatus = EXIT_EXPERROR;
	    goto finish;
	}
	words = (void **) mbswords;
	for (int i = 0; i < count; i++) {
	    words[i] = realloc_mbstowcs(words[i]);
	    if (!words[i])
		words[i] = xwcsdup(L"");
	}
    } else {
	bool concat;
	words = get_variable("@", &concat);
	assert(words != NULL && !concat);
	count = plcount(words);
    }

#define CHECK_LOOP                                   \
    if (execinfo.breakcount > 0) {                   \
	execinfo.breakcount--;                       \
	goto done;                                   \
    } else if (execinfo.exception == ee_continue) {  \
	execinfo.exception = ee_none;                \
	continue;                                    \
    } else if (execinfo.exception != ee_none) {      \
	goto done;                                   \
    } else (void) 0

    int i;
    for (i = 0; i < count; i++) {
	if (!set_variable(c->c_forname, words[i], !posixly_correct, false))
	    goto done;
	exec_and_or_lists(c->c_forcmds, false);
	CHECK_LOOP;
    }

done:
    while (++i < count)
	free(words[i]);
    free(words);
    if (count == 0)
	laststatus = EXIT_SUCCESS;
finish:
    if (finally_exit)
	exit_shell();
    execinfo.loopnest--;
}

/* while コマンドを実行する */
/* while コマンドの終了ステータスは、最後に実行した c_whlcmds の終了ステータス
 * である。一度も c_whlcmds を実行しなければ、c_whlcond の結果にかかわらず
 * 終了ステータスは 0 である。 */
void exec_while(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_WHILE);
    assert(!need_break());
    execinfo.loopnest++;

    int status = EXIT_SUCCESS;
    while (exec_condition(c->c_whlcond) == c->c_whltype) {
	CHECK_LOOP;
	exec_and_or_lists(c->c_whlcmds, false);
	status = laststatus;
	CHECK_LOOP;
    }

    laststatus = status;
done:
    if (finally_exit)
	exit_shell();
    execinfo.loopnest--;
}
#undef CHECK_LOOP

/* case コマンドを実行する */
void exec_case(const command_T *c, bool finally_exit)
{
    assert(c->c_type == CT_CASE);
    assert(!need_break());

    wchar_t *word = expand_single(c->c_casword, tt_single);
    if (!word)
	goto fail;
    word = unescapefree(word);

    for (caseitem_T *ci = c->c_casitems; ci; ci = ci->next) {
	for (void **pats = ci->ci_patterns; *pats; pats++) {
	    wchar_t *pattern = expand_single(*pats, tt_single);
	    if (!pattern)
		goto fail;
	    size_t match = wfnmatch(pattern, word, 0, WFNM_WHOLE);
	    free(pattern);
	    if (match != WFNM_NOMATCH && match != WFNM_ERROR) {
		exec_and_or_lists(ci->ci_commands, finally_exit);
		goto done;
	    }
	}
    }
    laststatus = EXIT_SUCCESS;
done:
    if (finally_exit)
	exit_shell();
    free(word);
    return;

fail:
    laststatus = EXIT_EXPERROR;
    goto done;
}

/* exec_commands 関数で使うサブルーチン */
/* 次のプロセスの処理のために pipeinfo_T の内容を更新する。
 * pi->pi_fromprevfd, pi->pi_tonextfds[PIDX_OUT] を閉じ、
 * pi->pi_tonextfds[PIDX_IN] を pi->pi_fromprevfd に移し、
 * next が true なら新しいパイプを開いて pi->pi_tonextfds に入れる。
 * next が false なら pi->pi_loopoutfd を pi->pi_tonextfds[PIDX_OUT] に移し、
 * pi->pi_loopoutfd と pi->pi_tonextfds[PIDX_IN] を -1 にする。
 * 成功すると true、エラーがあると false を返す。 */
void next_pipe(pipeinfo_T *pi, bool next)
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

/* パイプを繋ぎ、余ったパイプを閉じる */
void connect_pipes(pipeinfo_T *pi)
{
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
}

/* 一つのパイプラインを構成する各コマンドを実行する
 * c:        実行する一つ以上のコマンド
 * type:     実行のしかた
 * looppipe: パイプをループ状にするかどうか */
void exec_commands(command_T *c, exec_T type, bool looppipe)
{
    size_t count;
    pid_t pgid;
    command_T *cc;
    job_T *job;
    process_T *ps;
    pipeinfo_T pinfo = PIPEINFO_INIT;
    commandtype_T lasttype;

    assert(!need_break());

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

	lasttype = cc->c_type;
	next_pipe(&pinfo, i < count - 1);
	pid = exec_process(cc,
		(type == execself && i < count - 1) ? execnormal : type,
		&pinfo,
		pgid);
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
	goto done;
    }

    set_active_job(job);
    job->j_pgid = doing_job_control_now ? pgid : 0;
    job->j_status = JS_RUNNING;
    job->j_statuschanged = true;
    job->j_loop = looppipe;
    job->j_pcount = count;
    if (type == execnormal) {   /* ジョブの終了を待つ */
	wait_for_job(ACTIVE_JOBNO, doing_job_control_now);
	laststatus = calc_status_of_job(job);
	if (job->j_status == JS_DONE) {
	    remove_job(ACTIVE_JOBNO);
	    goto finish;
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
    add_job(type == execnormal /* TODO || shopt_curasync */);

finish:
    if (doing_job_control_now)
	make_myself_foreground();
done:
    handle_traps();
    if (shopt_errexit && !supresserrexit && laststatus != EXIT_SUCCESS
	    && lasttype == CT_SIMPLE)
	exit_shell();
}

/* 一つのコマンドを実行する。
 * c:      実行するコマンド
 * type:   実行のしかた
 * pi:     繋ぐパイプのファイルディスクリプタ情報
 * pgid:   ジョブ制御有効時、fork 後に子プロセスに設定するプロセスグループ ID。
 *         子プロセスのプロセス ID をプロセスグループ ID にする場合は 0。
 * 戻り値: fork した場合、子プロセスのプロセス ID。fork せずにコマンドを実行
 *         できた場合は 0。
 * 0 を返すとき、この関数内でコマンドの終了コードを laststatus に設定する。
 * type == execself ならこの関数は返らない。 */
pid_t exec_process(command_T *c, exec_T type, pipeinfo_T *pi, pid_t pgid)
{
    bool early_fork;   /* まず最初に fork するかどうか */
    bool later_fork;   /* コマンドライン展開の後に fork するかどうか */
    bool finally_exit; /* true なら最後に exit してこの関数から返らない */
    int argc;
    char **argv = NULL;
    commandinfo_T cmdinfo = cmdinfo;  /* 自己代入で GCC の警告を防ぐ */

    current_lineno = c->c_lineno;

    /* 非同期実行であるかまたはパイプを繋ぐ場合は、先に fork。 */
    early_fork = (type != execself) && (type == execasync
	|| pi->pi_fromprevfd >= 0 || pi->pi_tonextfds[PIDX_OUT] >= 0);
    if (early_fork) {
	bool doingjobcontrol = doing_job_control_now;
	pid_t cpid = fork_and_reset(pgid, type == execnormal);
	if (cpid)
	    return cpid;
	if (!doingjobcontrol && type == execasync)
	    block_sigquit_and_sigint();
    }

    switch (c->c_type) {
    case CT_SIMPLE:
	if (!expand_line(c->c_words, &argc, &argv))
	    goto exp_fail;
	if (argc == 0) {
	    later_fork = finally_exit = false;
	} else {
	    search_command(argv[0], &cmdinfo);

	    /* 外部コマンドは fork し、組込みコマンドや関数は fork しない。 */
	    later_fork = finally_exit = (cmdinfo.type == externalprogram);

	    /* コマンドが見付からず、リダイレクトや代入もなければこれで終わり */
	    if (cmdinfo.type == externalprogram && !cmdinfo.ci_path
		    && !c->c_redirs && !c->c_assigns) {
		xerror(0, Ngt("%s: no such command or function"), argv[0]);
		laststatus = EXIT_NOTFOUND;
		recfree((void **) argv, free);
		goto done;
	    }
	}
	break;
    case CT_SUBSHELL:
	later_fork = finally_exit = true;
	break;
    default:
	later_fork = finally_exit = false;
	break;
    }
    /* argc, argv, cmdinfo は CT_SIMPLE でしか使わない */

    if (early_fork || type == execself)
	later_fork = false, finally_exit = true;

    assert(!(early_fork && later_fork));  /* fork は 2 回はしない */
    assert(!(early_fork || later_fork) || finally_exit);  /* fork すれば exit */
    if (later_fork) {
	pid_t cpid = fork_and_reset(pgid, type == execnormal);
	if (cpid != 0) {
	    recfree((void **) argv, free);
	    return cpid;
	}
    }
    if (finally_exit) {
	if (c->c_type == CT_SIMPLE && cmdinfo.type == externalprogram)
	    reset_all_signals();
	else
	    reset_signals();
    }

    /* パイプを繋ぎ、余ったパイプを閉じる */
    connect_pipes(pi);

    /* リダイレクトを開く */
    savefd_T *savefd;
    if (!open_redirections(c->c_redirs, finally_exit ? NULL : &savefd))
	goto redir_fail;
    
    /* 非対話的シェルで非同期実行する場合は stdin を /dev/null にリダイレクト */
    if (type == execasync && pi->pi_fromprevfd < 0)
	maybe_redirect_stdin_to_devnull();

    /* コマンドを実行する */
    if (c->c_type == CT_SIMPLE) {
	if (argc == 0) {
	    if (do_assignments(c->c_assigns, false, shopt_allexport))
		laststatus = EXIT_SUCCESS;
	    else
		laststatus = EXIT_ASSGNERR;
	} else {
	    if (do_assignments_for_command_type(c->c_assigns, cmdinfo.type)) {
		exec_simple_command(&cmdinfo, argc, argv, finally_exit);
	    } else {
		laststatus = EXIT_ASSGNERR;
		if (!is_interactive && cmdinfo.type == specialbuiltin)
		    exit_shell();
	    }
	    clear_temporary_variables();
	}
	recfree((void **) argv, free);
    } else {
	exec_nonsimple_command(c, finally_exit);
    }
    if (finally_exit)
	exit_shell();

    // TODO exec: exec_process: exec コマンドのリダイレクトは残す
#if 0
    if (c->c_type == CT_SIMPLE && cmdinfo.type == specialbuiltin
	    && cmdinfo.ci_builtin == exec_builtin && laststatus == EXIT_SUCCESS)
	clear_savefd(savefd);
    else
#endif
	undo_redirections(savefd);
    return 0;

exp_fail:
    laststatus = EXIT_EXPERROR;
done:
    if (early_fork || type == execself)
	exit_shell();
    return 0;
redir_fail:
    laststatus = EXIT_REDIRERR;
    if (finally_exit)
	exit_shell();
    if (posixly_correct && !is_interactive
	    && c->c_type == CT_SIMPLE && cmdinfo.type == specialbuiltin)
	exit_shell();
    undo_redirections(savefd);
    if (c->c_type == CT_SIMPLE)
	recfree((void **) argv, free);
    return 0;
}

/* サブシェルを fork して、いろいろ必要な設定を行う。
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
	if (doing_job_control_now && pgid >= 0)
	    setpgid(cpid, pgid);
    } else {
	/* 子プロセス */
	if (doing_job_control_now && pgid >= 0) {
	    setpgid(0, pgid);
	    if (fg)
		make_myself_foreground();
	}
	forget_initial_pgrp();
	remove_all_jobs();
	clear_traps();
	clear_shellfds();
	is_interactive_now = false;
    }
    return cpid;
}

/* 自分自身のプロセスグループをフォアグラウンドにする */
void make_myself_foreground(void)
{
    assert(ttyfd >= 0);
    block_sigttou();
    tcsetpgrp(ttyfd, getpgrp());
    unblock_sigttou();
}

/* コマンドの種類を特定し、コマンドのありかを探す。
 * コマンドが見付からなければ ci->type = externalprogram で
 * ci->ci_path = NULL とする。 */
void search_command(const char *restrict name, commandinfo_T *restrict ci)
{
    /* 関数を探す: 関数名に '/' が含まれないことを仮定している */
    command_T *funcbody = get_function(name);
    if (funcbody) {
	assert(strchr(name, '/') == NULL);
	ci->type = function;
	ci->ci_function = funcbody;
	return;
    }

    wchar_t *wname = malloc_mbstowcs(name);
    bool slash = wname && wcschr(wname, L'/');
    free(wname);
    if (slash) {  /* name にスラッシュが入っていれば name をそのまま返す */
	ci->type = externalprogram;
	ci->ci_path = name;
	return;
    }

    // TODO exec.c: find_command: 組込みコマンド
    ci->type = externalprogram;
    ci->ci_path = get_command_path(name, false);
}

/* 指定した commandtype_T に従って do_assignments を呼び出す */
bool do_assignments_for_command_type(const assign_T *asgns, enum cmdtype_T type)
{
    bool temporary;
    switch (type) {
	/* externalprogram は本来 temporary = true だが、fork しているので
	 * 一時的にする必要はない */
	case externalprogram:
	case specialbuiltin:
	case function:
	    temporary = false;
	    break;
	case semispecialbuiltin:
	case regularbuiltin:
	    temporary = true;
	    break;
	default:
	    assert(false);
    }
    return do_assignments(asgns, temporary, true);
}

/* CT_SIMPLE でない一つのコマンドを実行する。
 * c->c_redirs は実行しない */
void exec_nonsimple_command(command_T *c, bool finally_exit)
{
    c = comsdup(c);
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
    case CT_FUNCDEF:
	if (define_function(c->c_funcname, c->c_funcbody))
	    laststatus = EXIT_SUCCESS;
	else
	    laststatus = EXIT_ASSGNERR;
	if (finally_exit)
	    exit_shell();
	break;
    }
    comsfree(c);
}

/* 単純コマンドを実行する
 * ci:   実行するコマンドの情報
 * argv: コマンド名と引数。マルチバイト文字列へのポインタの配列へのポインタ。 */
void exec_simple_command(
	const commandinfo_T *ci, int argc, char *const *argv, bool finally_exit)
{
    assert(argv[argc] == NULL);

    print_xtrace(argv);
    switch (ci->type) {
    case externalprogram:
	assert(finally_exit);
	if (ci->ci_path == NULL) {
	    xerror(0, Ngt("%s: no such command or function"), argv[0]);
	    exit_shell_with_status(EXIT_NOTFOUND);
	}
	xexecv(ci->ci_path, (char **) argv);
	if (errno != ENOEXEC) {
	    if (errno == EACCES && is_directory(ci->ci_path))
		errno = EISDIR;
	    if (strcmp(argv[0], ci->ci_path) == 0)
		xerror(errno, Ngt("cannot execute `%s'"),
			argv[0]);
	    else
		xerror(errno, Ngt("cannot execute `%s' (%s)"),
			argv[0], ci->ci_path);
	} else {
	    exec_fall_back_on_sh(argc, argv, environ, ci->ci_path);
	}
	exit_shell_with_status(EXIT_NOEXEC);
	//break;
    case specialbuiltin:
    case semispecialbuiltin:
    case regularbuiltin:
	laststatus = ci->ci_builtin(argc, (char **) argv);
	break;
    case function:
	exec_function_body(ci->ci_function, (char **) argv, finally_exit);
	break;
    }
    if (finally_exit)
	exit_shell();
}

/* shopt_xtrace が true ならトレースを出力する */
void print_xtrace(char *const *argv)
{
    if (shopt_xtrace) {
	print_prompt(4);
	for (;;) {
	    fputs(*argv, stderr);
	    argv++;
	    if (!*argv)
		break;
	    fputs(" ", stderr);
	}
	fputs("\n", stderr);
    }
}

/* 指定した引数で sh を exec する。
 * exec する sh には path と argv の最初以外を渡す。 */
void exec_fall_back_on_sh(
	int argc, char *const *argv, char *const *envp, const char *path)
{
    assert(argv[argc] == NULL);
    if (!envp)
	envp = (char *[]) { NULL };

    char *args[argc + 3];
    size_t index = 0;
    args[index++] = argv[0];
    args[index++] = (char *) "-";
    if (strcmp(path, "--") == 0)
	args[index++] = "./--";
    else
	args[index++] = (char *) path;
    for (int i = 1; i < argc; i++)
	args[index++] = argv[i];
    args[index++] = NULL;
#if HAVE_PROC_FILESYSTEM
    xexecve("/proc/self/exe", args, envp);
#elif defined _PATH_BSHELL
    xexecve(_PATH_BSHELL, args, envp);
#else
    const char *shpath = get_command_path("sh", false);
    if (shpath)
	xexecve(shpath, args, envp);
    else
	errno = ENOENT;
#endif
    xerror(errno, Ngt("cannot invoke new shell to execute `%s'"), argv[0]);
}

/* 指定したコマンドを関数の本体として実行する
 * args: 関数の引数。args[1] が $1 に args[2] が $2 になる。 */
void exec_function_body(command_T *body, char **args, bool finally_exit)
{
    savefd_T *savefd;

    assert(args[0] != NULL);
    if (open_redirections(body->c_redirs, &savefd)) {
	open_new_environment();
	set_positional_parameters(args + 1);
	exec_nonsimple_command(body, finally_exit);
	if (execinfo.exception == ee_return) {
	    assert(execinfo.breakcount == 0);
	    execinfo.exception = ee_none;
	}
	close_current_environment();
    }
    undo_redirections(savefd);
}

/* execv を行う。execv が EINTR を返したら、やり直す。 */
int xexecv(const char *path, char *const *argv)
{
    do
	execv(path, argv);
    while (errno == EINTR);
    return -1;
}

/* execve を行う。execve が EINTR を返したら、やり直す。 */
int xexecve(const char *path, char *const *argv, char *const *envp)
{
    do
	execve(path, argv, envp);
    while (errno == EINTR);
    return -1;
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
    bool save_is_interactive_now = is_interactive_now;

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

	/* 子プロセスをアクティブジョブにする */
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

	/* 子プロセスの出力を読む */
	xwcsbuf_T buf;
	wb_init(&buf);
	set_nonblocking(pipefd[PIDX_IN]);
	block_sigchld_and_sighup();
	handle_sigchld_and_sighup();
	for (;;) {
	    wint_t c = fgetwc(f);
	    if (c == WEOF) {
		if (feof(f))
		    break;
		assert(ferror(f));
		switch (errno) {
		    case EINTR:
		    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
		    case EWOULDBLOCK:
#endif
			wait_for_input(pipefd[PIDX_IN], false);
			continue;
		}
		break;
	    } else {
		wb_wccat(&buf, c);
	    }
	}
	unblock_sigchld_and_sighup();
	fclose(f);

	/* 子プロセスの終了を待つ */
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
	if (save_is_interactive_now)
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

/* 引数を内容とするヒアドキュメントを開く。
 * 成功ならファイルディスクリプタ、エラーなら負数を返す。 */
/* ヒアドキュメントの内容は fork した子プロセスからパイプ経由で渡す */
int open_heredocument(const wordunit_T *contents)
{
    int pipefd[2];
    pid_t cpid;

    /* ヒアドキュメントの内容を渡すためのパイプを開く */
    if (pipe(pipefd) < 0) {
	xerror(errno, Ngt("cannot open pipe for here-document"));
	return -1;
    }

    /* contents が空文字列なら fork するまでもない */
    if (!contents)
	goto success;

    cpid = fork_and_reset(-1, false);
    if (cpid < 0) {
	/* fork 失敗 */
	xerror(0, Ngt("cannot redirect to here-document"));
	xclose(pipefd[PIDX_IN]);
	xclose(pipefd[PIDX_OUT]);
	return -1;
    } else if (cpid > 0) {
	/* 親プロセス */
success:
	xclose(pipefd[PIDX_OUT]);
	return pipefd[PIDX_IN];
    } else {
	/* 子プロセス */
	xclose(pipefd[PIDX_IN]);
	block_all_but_sigpipe();

	FILE *f = fdopen(pipefd[PIDX_OUT], "w");
	if (!f) {
	    xerror(errno, Ngt("cannot open pipe for here-document"));
	    exit(EXIT_ERROR);
	}

	wchar_t *s = expand_string(contents, true);
	if (!s)
	    exit(EXIT_EXPERROR);
	if (fputws(s, f) < 0) {
#ifndef NDEBUG
	    free(s);
#endif
	    xerror(errno, Ngt("cannot write here-document contents"));
	    exit(EXIT_ERROR);
	}
#ifndef NDEBUG
	free(s);
#endif
	exit(EXIT_SUCCESS);
    }
}


/* vim: set ts=8 sts=4 sw=4 noet: */
