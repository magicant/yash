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
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "yash.h"
#include "util.h"
#include "sig.h"
#include "parser.h"
#include "expand.h"
#include "exec.h"
#include "job.h"
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
	bool  sr_stdin_redirected;  /* is_stdin_redirected の値 */
};

static int (*create_pipes(size_t count))[2];
static void close_pipes(PIPES pipes);
static inline int xdup2(int oldfd, int newfd);
static void exec_pipelines(PIPELINE *pipelines, bool finally_exit);
static void exec_processes(PIPELINE *pl, bool background, char *jobname);
static pid_t exec_single(
		PROCESS *p, ssize_t pindex, pid_t pgid, exec_t etype, PIPES pipes);
static bool open_redirections(REDIR *redirs, struct save_redirect **save);
static void save_redirect(struct save_redirect **save, int fd);
static int parse_dupfd(const char *exp, enum redirect_type type);
static int open_heredocument(bool expand, const char *content);
static void undo_redirections(struct save_redirect *save);
static void clear_save_redirect(struct save_redirect *save);

/* 最後に実行したコマンドの終了コード */
int laststatus = 0;

/* シェルがセーブ等に使用中のファイルディスクリプタの集合。
 * これらのファイルディスクリプタはユーザが使うことはできない。 */
static fd_set shellfds;
/* シェルが自由に使えるファイルディスクリプタの最小値。 */
int shellfdmin;
/* shellfds 内のファイルディスクリプタの最大値。 */
static int shellfdmax = -1;
/* stdin がリダイレクトされているかどうか */
static bool is_stdin_redirected = false;

/* exec モジュールを初期化する */
void init_exec(void)
{
	FD_ZERO(&shellfds);
	reset_shellfdmin();
}

/* fd (>= shellfdmin) を shellfds に追加する。 */
void add_shellfd(int fd)
{
	assert(fd >= shellfdmin);
	if (fd < FD_SETSIZE)
		FD_SET(fd, &shellfds);
	if (shellfdmax < fd)
		shellfdmax = fd;
}

/* fd を shellfds から削除する */
void remove_shellfd(int fd)
{
	if (0 <= fd && fd < FD_SETSIZE)
		FD_CLR(fd, &shellfds);
	if (fd == shellfdmax) {
		shellfdmax = fd - 1;
		while (shellfdmax >= 0 && !FD_ISSET(shellfdmax, &shellfds))
			shellfdmax--;
	}
}

/* fd が shellfds に入っているかどうか */
bool is_shellfd(int fd)
{
	return fd >= FD_SETSIZE || (fd >= 0 && FD_ISSET(fd, &shellfds));
}

/* shellfds 内のファイルディスクリプタを全て閉じる。
 * reset_after_fork の中で呼ばれる。 */
void clear_shellfds(void)
{
	for (int i = 0; i <= shellfdmax; i++)
		if (FD_ISSET(i, &shellfds))
			xclose(i);
	FD_ZERO(&shellfds);
	shellfdmax = -1;
}

/* shellfdmin を(再)計算する */
void reset_shellfdmin(void)
{
	errno = 0;
	shellfdmin = sysconf(_SC_OPEN_MAX);
	if (shellfdmin == -1) {
		if (errno)
			shellfdmin = 10;
		else
			shellfdmin = SHELLFDMINMAX;
	} else {
		shellfdmin /= 2;
		if (shellfdmin > SHELLFDMINMAX)
			shellfdmin = SHELLFDMINMAX;
		else if (shellfdmin < 10)
			shellfdmin = 10;
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
		xclose(pipes.p_pipes[i][0]);
		xclose(pipes.p_pipes[i][1]);
	}
	free(pipes.p_pipes);
}

/* dup2 を確実に行う。(dup2 が EINTR を返したら、やり直す)
 * エラーが出たら、メッセージを出力する。 */
static inline int xdup2(int oldfd, int newfd)
{
	while (dup2(oldfd, newfd) < 0) {
		switch (errno) {
			case EINTR:
				continue;
			default:
				xerror(0, errno,
						"cannot copy file descriptor %d to %d", oldfd, newfd);
				return -1;
		}
	}
	return newfd;
}

/* コマンド入力全体を受け取って、全コマンドを実行する。
 * finally_exit: true なら実行後そのまま終了する。 */
void exec_statements(STATEMENT *s, bool finally_exit)
{
	while (s) {
		PIPELINE *p = s->s_pipeline;
		if (p) {
			if (!s->s_bg) {
				/* フォアグラウンド */
				exec_pipelines(p, !s->next && finally_exit);
			} else {
				/* バックグラウンド */
				if (!p->next) {
					/* パイプが一つしかない場合 */
					exec_processes(p, true, NULL);
				} else {
					/* パイプが複数ある場合 (&& か || がある場合) */
					STATEMENT s2 = {
						.next = NULL,
						.s_pipeline = p,
						.s_bg = false,
					};
					PROCESS proc = {
						.next = NULL,
						.p_type = PT_GROUP,
						.p_assigns = NULL,
						.p_subcmds = &s2,
						.p_redirs = NULL,
					};
					PIPELINE pipe = {
						.next = NULL,
						.pl_proc = &proc,
						.pl_neg = false,
						.pl_loop = false,
					};
					char *name = make_statement_name(p);
					exec_processes(&pipe, true, name);
				}
			}
		}
		s = s->next;
	}
	if (finally_exit)
		exit(laststatus);
}

/* 一つの文の各パイプラインを実行する。
 * finally_exit: true なら実行後そのまま終了する。 */
static void exec_pipelines(PIPELINE *p, bool finally_exit)
{
	while (p) {
		if (finally_exit && !p->next && !p->pl_neg && !p->pl_loop) {
			/* 最後なのでこのプロセスで直接実行する。 */
			PROCESS *proc = p->pl_proc;
			if (!proc->next) {
				exec_single(proc, 0, 0, SELF,
						(PIPES) { .p_count = 0, .p_pipes = NULL, });
				assert(false);
			}
		}

		exec_processes(p, false, NULL);
		if (!p->pl_next_cond == !laststatus)
			break;
		p = p->next;
	}
	if (finally_exit)
		exit(laststatus);
}

/* 一つのパイプラインを実行し、wait する。
 * pl:      実行するパイプライン
 * jobname: パイプラインのジョブ名。この関数内で free する。NULL を指定すると
 *          この関数内でジョブ名を作成する。
 * bg ならばバックグラウンドでジョブを実行。
 * すなわち wait せずに新しいジョブを追加して戻る。
 * bg でなければフォアグラウンドでジョブを実行。
 * すなわち wait し、停止した場合のみ新しいジョブを追加して戻る。 */
static void exec_processes(PIPELINE *pl, bool bg, char *jobname)
{
	size_t pcount;
	pid_t pgid;
	struct jproc *ps;
	PIPES pipes;
	PROCESS *p = pl->pl_proc;

	/* パイプライン内のプロセス数を数える */
	pcount = 0;
	for (PROCESS *pp = p; pp; pcount++, pp = pp->next);

	/* 必要な数のパイプを作成する */
	pipes.p_count = pl->pl_loop ? pcount : pcount - 1;
	pipes.p_pipes = create_pipes(pipes.p_count);
	if (pipes.p_count > 0 && !pipes.p_pipes) {
		laststatus = 2;
		free(jobname);
		return;
	}

	ps = xmalloc(pcount * sizeof *ps);

	/* パイプの最初のコマンドを実行 */
	ps[0].jp_status = JS_RUNNING;
	pgid = ps[0].jp_pid = exec_single(p, pl->pl_loop ? -1 : 0, 0,
			bg ? BACKGROUND : FOREGROUND, pipes);
	ps[0].jp_waitstatus = 0;

	/* パイプの二つ目以降のコマンドを実行 */
	if (pgid >= 0) {
		for (size_t i = 1; i < pcount; i++) {
			p = p->next;
			ps[i] = (struct jproc) {
				.jp_pid = exec_single(p, i, pgid,
						bg ? BACKGROUND : FOREGROUND, pipes),
				.jp_status = JS_RUNNING,
			};
		}
	} else {
		laststatus = EXIT_FAILURE;
	}

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
			.j_name = NULL,
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
			if (pl->pl_neg)
				laststatus = !laststatus;
			if (job->j_status == JS_DONE) {
				remove_job(0);
			} else {
				job->j_name = jobname ? jobname : make_pipeline_name(pl);
				add_job();
			}
		} else {
			laststatus = EXIT_SUCCESS;
			last_bg_pid = ps[pcount - 1].jp_pid;
			job->j_name = jobname ? jobname : make_pipeline_name(pl);
			add_job();
		}
	} else {
		free(ps);
		free(jobname);
	}
	assert(!joblist.contents[0]);
}

/* 一つのコマンドを実行する。リダイレクト・変数代入などを行い、必要に応じて
 * fork/exec する。
 * p:       実行するコマンド
 * pindex:  パイプ全体における子プロセスのインデックス。
 *          環状パイプを作る場合は 0 の代わりに -1。
 * pgid:    子プロセスに設定するプロセスグループ ID。
 *          子プロセスのプロセス ID をそのままプロセスグループ ID にする時は 0。
 *          fork しない場合はプロセスグループを設定しないので pgid は無視。
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
	bool need_fork, finally_exit;
	int argc;
	char **argv = NULL;
	const char *commandname = "?", *commandpath = NULL;
	BUILTIN *builtin = NULL;

	switch (p->p_type) {
	case PT_NORMAL:
		if (!expand_line(p->p_args, &argc, &argv)) {  /* 単語展開 */
			recfree((void **) argv, free);
			return -1;
		}
		if (!argc) {
			need_fork = finally_exit = false;
		} else {
			/* コマンド検索 */
			if (strchr(argv[0], '/')) {
				commandpath = argv[0];
			} else {
				enum biflags flags = BI_SPECIAL | BI_SEMISPECIAL;
				builtin = get_builtin(argv[0]);
				if (!builtin || (posixly_correct && !(builtin->flags & flags))){
					commandpath = get_command_fullpath(argv[0], false);
					if (!commandpath) {
						xerror(0, 0, "%s: command not found", argv[0]);
						laststatus = EXIT_NOTFOUND;
						return 0;
					}
				}
			}
			need_fork = finally_exit = !builtin;
			commandname = argv[0];
		}
		break;
	case PT_SUBSHELL:
		need_fork = finally_exit = true;
		break;
	case PT_GROUP:
		need_fork = finally_exit = false;
		break;
	default:
		assert(false);
	}

	if (etype == SELF) {
		need_fork = false;  finally_exit = true;
		assert(!is_interactive_now);
	} else if (etype == BACKGROUND || pipes.p_count || p->p_type==PT_SUBSHELL) {
		/* 非同期リスト・パイプ内またはサブシェルでは常に fork */
		need_fork = finally_exit = true;
	}

	assert(!need_fork || finally_exit);
	if (need_fork) {
		pid_t cpid = fork();

		if (cpid) {
			if (cpid < 0) {  /* fork 失敗 */
				xerror(0, errno, "%s: fork", commandname);
			} else {  /* 親プロセス */
				if (is_interactive_now) {
					if (setpgid(cpid, pgid) < 0
							&& errno != EACCES && errno != ESRCH)
						xerror(0, errno, "%s: setpgid (parent)", commandname);
				}
			}
			recfree((void **) argv, free);
			return cpid;
		}

		/* 子プロセス */
		if (is_interactive_now) {
			if (setpgid(0, pgid) < 0)
				xerror(0, errno, "%s: setpgid (child)", commandname);
			if (etype == FOREGROUND && ttyfd >= 0
					&& tcsetpgrp(ttyfd, pgid ? pgid : getpid()) < 0)
				xerror(0, errno, "%s: tcsetpgrp (child)", commandname);
		}
		reset_after_fork();
	}

	if (finally_exit)
		unset_signals();

	/* パイプを繋ぐ */
	if (pipes.p_count > 0) {
		if (pindex) {
			size_t index = ((pindex >= 0) ? (size_t)pindex : pipes.p_count) - 1;
			xdup2(pipes.p_pipes[index][0], STDIN_FILENO);
		}
		if (pindex < (ssize_t) pipes.p_count) {
			size_t index = (pindex >= 0) ? (size_t) pindex : 0;
			xdup2(pipes.p_pipes[index][1], STDOUT_FILENO);
		}
		close_pipes(pipes);  /* 余ったパイプを閉じる */
	}

	/* リダイレクトを開く */
	struct save_redirect *saveredir;
	if (!open_redirections(p->p_redirs, finally_exit ? NULL : &saveredir)) {
		if (finally_exit)
			exit(EXIT_FAILURE);
undo_redir_and_fail:
		if (posixly_correct && !is_interactive_now
				&& builtin && (builtin->flags & BI_SPECIAL))
			exit(EXIT_FAILURE);
		undo_redirections(saveredir);
		recfree((void **) argv, free);
		return -1;
	}

	/* 非対話の非同期リストなら stdin を /dev/null にリダイレクトする */
	if (etype == BACKGROUND && !is_interactive
			&& pindex <= 0 && !is_stdin_redirected) {
		xclose(STDIN_FILENO);
		int fd = open("/dev/null", O_RDONLY);
		if (fd < 0)
			xerror(0, errno, "cannot redirect stdin to /dev/null");
		if (fd != STDIN_FILENO) {
			xdup2(fd, STDIN_FILENO);
			xclose(fd);
		}
	}

	/* 変数を代入する */
	bool tempassign = builtin && !(builtin->flags & BI_SPECIAL);
	bool export = argc && !tempassign;
	if (p->p_assigns) {
		if (!assign_variables(p->p_assigns, tempassign, export)) {
			if (finally_exit)
				exit(EXIT_FAILURE);
			unset_temporary(NULL);
			goto undo_redir_and_fail;
		}
	}

	/* コマンドを実行する */
	switch (p->p_type) {
	case PT_NORMAL:
		// XXX 関数の実行
		if (builtin) {
			laststatus = builtin->main(argc, argv);
		} else if (commandpath) {
			execve(commandpath, argv, environ);
			if (errno != ENOEXEC) {
				if (errno == EACCES && is_directory(commandpath))
					errno = EISDIR;
				xerror(EXIT_NOEXEC, errno, "%s", commandname);
			}
			selfexec(commandpath, argv);
		}
		break;

	case PT_SUBSHELL:  case PT_GROUP:
		exec_statements(p->p_subcmds, finally_exit);
		break;
	}
	if (finally_exit)
		exit(laststatus);

	fflush(NULL);
	if (tempassign)
		unset_temporary(NULL);
	if (builtin && builtin->main == builtin_exec && laststatus == EXIT_SUCCESS)
		clear_save_redirect(saveredir);
	else
		undo_redirections(saveredir);
	recfree((void **) argv, free);
	return 0;
}

/* リダイレクトを開く。
 * 各 r の rd_file に対する各種展開もここで行う。
 * save:   非 NULL なら、元の FD をセーブしつつリダイレクトを処理し、*save
 *         にセーブデータへのポインタが入る。
 * 戻り値: OK なら true、エラーがあれば false。
 * エラーがあっても *save にはそれまでにセーブした FD のデータが入る。 */
static bool open_redirections(REDIR *r, struct save_redirect **save)
{
	char *exp;

	if (save)
		*save = NULL;

	while (r) {
		int fd, flags;

		if (r->rd_fd < 0 || is_shellfd(r->rd_fd)) {
			xerror(0, 0, "redirect: file descriptor %d unavailable", r->rd_fd);
			exp = NULL;
			goto returnfalse;
		}

		/* rd_file を展開する */
		if (r->rd_type != RT_HERE && r->rd_type != RT_HERERT) {
			exp = expand_single(r->rd_value, is_interactive);
			if (!exp)
				goto returnfalse;
		} else {
			exp = NULL;
		}

		/* リダイレクトをセーブする */
		save_redirect(save, r->rd_fd);

		/* 実際に rd_type に応じてリダイレクトを行う */
		switch (r->rd_type) {
			case RT_INPUT:
				flags = O_RDONLY;
				goto openwithflags;
			case RT_OUTPUT:
				if (false) {  // TODO noclobber
					flags = O_WRONLY | O_CREAT | O_EXCL;
					goto openwithflags;
				}
				/* falls thru! */
			case RT_OUTCLOB:
				flags = O_WRONLY | O_CREAT | O_TRUNC;
				goto openwithflags;
			case RT_APPEND:
				flags = O_WRONLY | O_CREAT | O_APPEND;
				goto openwithflags;
			case RT_INOUT:
				flags = O_RDWR | O_CREAT;
openwithflags:
				fd = open(exp, flags, S_IRUSR | S_IWUSR | S_IRGRP
						| S_IWGRP | S_IROTH | S_IWOTH);
				if (fd < 0)
					goto onerror;
				break;
			case RT_DUPIN:
			case RT_DUPOUT:
				if (strcmp(exp, "-") == 0) {
					fd = -1;
				} else {
					fd = parse_dupfd(exp, r->rd_type);
					switch (fd) {
						case -1:  goto onerror;
						case -2:  goto returnfalse;
					}
				}
				break;
			case RT_HERE:
			case RT_HERERT:
				fd = open_heredocument(
						!strpbrk(r->rd_value, "\"'\\"), r->rd_herecontent);
				if (fd < 0)
					goto onerror;
				break;
			default:
				assert(false);
		}

		/* fd が目的のファイルディスクリプタとは異なる場合は移動する。 */
		if (fd != r->rd_fd) {
			xclose(r->rd_fd);
			if (fd >= 0) {
				if (xdup2(fd, r->rd_fd) < 0)
					goto returnfalse;
				if (r->rd_type != RT_DUPIN && r->rd_type != RT_DUPOUT)
					if (xclose(fd) < 0)
						goto returnfalse;
			}
		}

		if (r->rd_fd == STDIN_FILENO)
			is_stdin_redirected = true;

		free(exp);
		r = r->next;
	}
	return true;

onerror:
	xerror(0, errno, "redirect: %s", r->rd_value);
returnfalse:
	free(exp);
	return false;
}

/* リダイレクトをセーブする
 * save: セーブ情報の保存先へのポインタ (NULL ならセーブしない)
 * fd:   セーブする元のファイルディスクリプタ */
static void save_redirect(struct save_redirect **save, int fd)
{
	assert(0 <= fd && fd < FD_SETSIZE);
	if (save) {
		int copyfd = fcntl(fd, F_DUPFD, shellfdmin);
		if (copyfd < 0 && errno != EBADF) {
			xerror(0, errno, "redirect: cannot save file descriptor %d", fd);
		} else if (copyfd >= 0 && fcntl(copyfd, F_SETFD, FD_CLOEXEC) == -1){
			xerror(0, errno, "redirect: cannot save file descriptor %d", fd);
			xclose(copyfd);
		} else {
			struct save_redirect *ss = xmalloc(sizeof *ss);
			ss->next = *save;
			ss->sr_origfd = fd;
			ss->sr_copyfd = copyfd;
			ss->sr_stdin_redirected = is_stdin_redirected;
			/* 註: オリジナルが存在しなければ copyfd == -1 */
			if (0 <= copyfd)
				add_shellfd(copyfd);
			switch (fd) {
				case STDIN_FILENO:   ss->sr_file = stdin;  break;
				case STDOUT_FILENO:  ss->sr_file = stdout; break;
				case STDERR_FILENO:  ss->sr_file = stderr; break;
				default:             ss->sr_file = NULL;   break;
			}
			if (ss->sr_file)
				fflush(ss->sr_file);
			*save = ss;

			if (ttyfd >= 0 && ttyfd == fd)
				ttyfd = copyfd;
		}
	}
}

/* DUPIN/DUPOUT のリダイレクトの値を解析する。
 * exp が有効なファイルディスクリプタを表す文字列で、type に適合するなら
 * その番号を返す。エラー時は -1 or -2 を返す。-1 なら呼び出し元で errno に
 * 対応するエラーを出力すべし。-2 ならエラーはこの関数内で出力済。
 * この関数は exp が有効な数字でなければエラーとする。 exp が "-" かどうかは
 * 予め判定しておくこと。 */
static int parse_dupfd(const char *exp, enum redirect_type type)
{
	char *end;
	int fd;

	assert(type == RT_DUPIN || type == RT_DUPOUT);
	errno = 0;
	fd = strtol(exp, &end, 10);
	if (errno)
		return -1;
	if (exp[0] == '\0' || end[0] != '\0') {
		xerror(0, 0, "%s: redirect syntax error", exp);
		return -2;
	}
	if (fd < 0) {
		errno = ERANGE;
		return -1;
	}
	if (posixly_correct) {
		int flags = fcntl(fd, F_GETFL);
		if (flags == -1) {
			return -1;
		} else {
			if (type == RT_DUPIN && (flags & O_ACCMODE) == O_WRONLY) {
				xerror(0, 0, "redirect: %d: file descriptor not readable", fd);
				return -2;
			}
			if (type == RT_DUPOUT && (flags & O_ACCMODE) == O_RDONLY) {
				xerror(0, 0, "redirect: %d: file descriptor not writable", fd);
				return -2;
			}
		}
	}
	assert(fd >= 0);
	return fd;
}

/* ヒアドキュメントの内容を一時ファイルに書き出し、そのファイルへの
 * ファイルディスクリプタを返す。一時ファイルは削除 (unlink) 済である。
 * 返すファイルディスクリプタは、ファイルの先頭から読み込み可能になっている。
 * 失敗すると負数を返す。 */
static int open_heredocument(bool expand, const char *content)
{
	char *expandedcontent = NULL;

	/* 一時ファイルを作る */
	errno = 0;
	char tempfile[] = XMKSTEMP_NAME;
	int fd = xmkstemp(tempfile);
	if (fd < 0)
		return -1;
	if (unlink(tempfile) < 0) {
		xclose(fd);
		return -1;
	}

	/* 一時ファイルにヒアドキュメントを書き出す */
	if (expand)
		content = expandedcontent =
			unescape_here_document(expand_word(content, te_none, true));
	size_t len = strlen(content), off = 0;
	while (off < len) {
		size_t rem = len - off;
		if (rem > SSIZE_MAX)
			rem = SSIZE_MAX;
		ssize_t count = write(fd, content + off, rem);
		if (count < 0) {
			free(expandedcontent);
			return -1;
		}
		off += count;
	}
	free(expandedcontent);

	/* ファイルディスクリプタを先頭に戻す */
	if (lseek(fd, 0, SEEK_SET) == (off_t) -1)
		return -1;
	/* XXX: ファイルディスクリプタを読み取り専用にすべき? */

	return fd;
}

/* セーブしたリダイレクトを元に戻し、さらに引数リスト save を free する。 */
static void undo_redirections(struct save_redirect *save)
{
	while (save) {
		if (save->sr_file)
			fflush(save->sr_file);
		xclose(save->sr_origfd);
		if (save->sr_copyfd >= 0) {
			remove_shellfd(save->sr_copyfd);
			xdup2(save->sr_copyfd, save->sr_origfd);
			xclose(save->sr_copyfd);
		}
		if (save->sr_origfd == STDIN_FILENO)
			is_stdin_redirected = save->sr_stdin_redirected;

		if (ttyfd >= 0 && ttyfd == save->sr_copyfd)
			ttyfd = save->sr_origfd;

		struct save_redirect *next = save->next;
		free(save);
		save = next;
	}
}

/* セーブしたリダイレクトを削除し、元に戻せないようにする。
 * さらに引数リスト save を free する。 */
static void clear_save_redirect(struct save_redirect *save)
{
	while (save) {
		if (save->sr_copyfd >= 0) {
			if (ttyfd != save->sr_copyfd) {
				remove_shellfd(save->sr_copyfd);
				xclose(save->sr_copyfd);
			}
		}

		struct save_redirect *next = save->next;
		free(save);
		save = next;
	}
}

/* fork 後の子プロセスで必要な再設定を行う。 */
void reset_after_fork(void)
{
	forget_orig_pgrp();
	joblist_reinit();
	clear_shellfds();
	is_interactive_now = false;
}

/* 指定したコマンドを実行し、その標準出力の内容を返す。
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
		xclose(pipefd[0]);
		xclose(pipefd[1]);
		return NULL;
	} else if (cpid) {  /* 親プロセス */
		sigset_t newset, oldset;
		fd_set fds;
		char *buf;
		size_t len, max;
		ssize_t count;

		xclose(pipefd[1]);

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

		/* 子プロセスからの出力を受け取る */
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
		xclose(pipefd[0]);

		/* 子プロセスの終了を待つ */
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
			while (len > 0 && (buf[len - 1] == '\n'))
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

		reset_after_fork();

		xclose(pipefd[0]);
		if (pipefd[1] != STDOUT_FILENO) {
			/* ↑ この条件が成り立たないことは普通考えられないが…… */
			xclose(STDOUT_FILENO);
			xdup2(pipefd[1], STDOUT_FILENO);
			xclose(pipefd[1]);
		}
		exec_source_and_exit(code, "command substitution");
	}
}
