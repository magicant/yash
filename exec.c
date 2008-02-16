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
};

static int (*create_pipes(size_t count))[2];
static void close_pipes(PIPES pipes);
static inline int xdup2(int oldfd, int newfd);
static void exec_pipelines(PIPELINE *pipelines);
static void exec_pipelines_and_exit(PIPELINE *pipelines)
	__attribute__((noreturn));
static void exec_processes(PIPELINE *pl, bool background, char *jobname);
static pid_t exec_single(
		PROCESS *p, ssize_t pindex, pid_t pgid, exec_t etype, PIPES pipes);
static bool open_redirections(REDIR *redirs, struct save_redirect **save);
static void undo_redirections(struct save_redirect *save);
static void clear_save_redirect(struct save_redirect *save);

/* 最後に実行したコマンドの終了コード */
int laststatus = 0;

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
		PIPELINE *p = s->s_pipeline;
		if (p) {
			if (!s->s_bg) {
				/* フォアグラウンド */
				exec_pipelines(p);
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
		exec_processes(p, false, NULL);
		if (!p->pl_next_cond == !laststatus)
			break;
		p = p->next;
	}
}

/* 一つの文の各パイプラインを実行し、そのまま終了する。 */
static void exec_pipelines_and_exit(PIPELINE *p)
{
	/* コマンドが一つだけならこのプロセスで直接実行する。 */
	if (p && !p->next && !p->pl_neg && !p->pl_loop) {
		PROCESS *proc = p->pl_proc;
		if (!proc->next) switch (proc->p_type) {
			case PT_NORMAL:
				exec_single(proc, 0, 0, SELF,
						(PIPES) { .p_count = 0, .p_pipes = NULL, });
				assert(false);
			case PT_GROUP:  case PT_SUBSHELL:
				exec_statements_and_exit(proc->p_subcmds);
		}
	}

	exec_pipelines(p);
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
	ps[0].jp_status = JS_RUNNING;
	pgid = ps[0].jp_pid = exec_single(p, pl->pl_loop ? -1 : 0, 0,
			bg ? BACKGROUND : FOREGROUND, pipes);
	ps[0].jp_waitstatus = 0;
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
	BUILTIN *builtin = NULL;

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
			if (!argc) {   /* リダイレクトや変数代入しかない場合 */
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
			} else {   /* 具体的なコマンドがある場合 */
				if (strchr(argv[0], '/')) {
					commandpath = argv[0];
				} else {
					builtin = get_builtin(argv[0]);
					enum biflags flags = (BI_SPECIAL | BI_SEMISPECIAL);
					if (!builtin
							|| (posixly_correct && !(builtin->flags & flags))) {
						commandpath = get_command_fullpath(argv[0], false);
						if (!commandpath) {
							xerror(0, 0, "%s: command not found", argv[0]);
							laststatus = EXIT_NOTFOUND;
							return 0;
						}
					}
					if (builtin) {  /* 組込みコマンドを fork せずに実行 */
						struct save_redirect *saver;
						if (open_redirections(p->p_redirs, &saver)) {
							bool temp = !(builtin->flags & BI_SPECIAL);
							bool export = (builtin->flags & BI_SPECIAL);
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
						if (posixly_correct && (builtin->flags & BI_SPECIAL)
								&& !is_interactive_now)
							exit(EXIT_FAILURE);
						undo_redirections(saver);
						recfree((void **) argv, free);
						return -1;
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

	// TODO FIXME: !expanded でエラー発生時 argv[0] を出力しようとする

	pid_t cpid = fork();
	if (cpid < 0) {  /* fork 失敗 */
		xerror(0, errno, "%s: fork", argv[0]);
		if (expanded) { recfree((void **) argv, free); }
		return -1;
	} else if (cpid) {  /* 親プロセス */
		if (is_interactive_now) {
			if (setpgid(cpid, pgid) < 0 && errno != EACCES && errno != ESRCH)
				xerror(0, errno, "%s: setpgid (parent)", argv[0]);
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
				.rd_value = xstrdup("/dev/null"),
			};
			p->p_redirs = r;
		}
	}
	joblist_reinit();
	is_interactive_now = false;

directexec:
	unset_signals();
	assert(!is_interactive_now);

	/* パイプを繋ぐ */
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
		close_pipes(pipes);  /* 余ったパイプを閉じる */
	}

	if (p->p_type == PT_NORMAL && !expanded) {
		if (!expand_line(p->p_args, &argc, &argv))  /* パラメータの展開 */
			exit(EXIT_FAILURE);
	}
	if (!open_redirections(p->p_redirs, NULL))
		exit(EXIT_FAILURE);
	assign_variables(p->p_assigns, false, true);

	switch (p->p_type) {
		case PT_NORMAL:
			if (!argc)
				exit(EXIT_SUCCESS);
			if (!strchr(argv[0], '/')) {
				if (!builtin && !commandpath)
					builtin = get_builtin(argv[0]);
				if (builtin && (builtin->flags & BI_SPECIAL))
					exit(builtin->main(argc, argv));
				// XXX 関数の実行
				if (builtin &&
						((builtin->flags & BI_SEMISPECIAL) || !posixly_correct))
					exit(builtin->main(argc, argv));

				if (!commandpath) {
					commandpath = get_command_fullpath(argv[0], false);
					if (!commandpath)
						xerror(EXIT_NOTFOUND, 0,
								"%s: command not found", argv[0]);
				}

				if (builtin)
					exit(builtin->main(argc, argv));
			} else {
				commandpath = argv[0];
			}
			execve(commandpath, argv, environ);
			if (errno != ENOEXEC) {
				if (errno == EACCES && is_directory(commandpath))
					errno = EISDIR;
				xerror(EXIT_NOEXEC, errno, "%s", argv[0]);
			}
			selfexec(commandpath, argv);
		case PT_GROUP:  case PT_SUBSHELL:
			exec_statements_and_exit(p->p_subcmds);
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
		if (r->rd_type != RT_HERE && r->rd_type != RT_HERERT) {
			exp = expand_single(r->rd_value, is_interactive);
			if (!exp)
				goto returnfalse;
		} else {
			exp = NULL;
		}

		/* リダイレクトをセーブする */
		if (save) {
			int copyfd = fcntl(r->rd_fd, F_DUPFD, SHELLFD);
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
			case RT_HERE:
			case RT_HERERT:;
				char *content;
				if (strpbrk(r->rd_value, "\"'\\"))
					content = r->rd_herecontent;
				else
					content = unescape_here_document(
							expand_word(r->rd_herecontent, te_none, true));
				
				size_t len = strlen(content);
				if (len <= PIPE_BUF) {
					/* ヒアドキュメントの内容をパイプを使って渡す */
					int pipefd[2];
					if (pipe(pipefd) < 0) {
						if (content != r->rd_herecontent)
							free(content);
						goto onerror;
					}
					ssize_t wr = write(pipefd[1], content, len);
					if (wr < 0) {
						int saveerrno = errno;
						close(pipefd[0]);
						close(pipefd[1]);
						errno = saveerrno;
						if (content != r->rd_herecontent)
							free(content);
						goto onerror;
					}
					/* パイプへの PIPE_BUF バイト以内の書き込みは、必ず全て
					 * 書き込めることが保証されている */
					assert((size_t) wr == len);
					close(pipefd[1]);
					fd = pipefd[0];
				} else {
					/* ヒアドキュメントの内容を一時ファイルを使って渡す */
					errno = 0;
					char tempfile[] = "/tmp/yash-XXXXXX";
					fd = xmkstemp(tempfile);
					if (fd < 0) {
						if (content != r->rd_herecontent)
							free(content);
						goto onerror;
					}
					if (unlink(tempfile) < 0) {
						xerror(0, errno,
								"failed to remove temporary file `%s'",
								tempfile);
						close(fd);
						if (content != r->rd_herecontent)
							free(content);
						goto returnfalse;
					}
					/* 一時ファイルにヒアドキュメントを書き出す */
					size_t off = 0;
					while (off < len) {
						size_t rem = len - off;
						if (rem > SSIZE_MAX)
							rem = SSIZE_MAX;
						ssize_t count = write(fd, content + off, rem);
						if (count < 0) {
							xerror(0, errno, "redirect: %s: "
									"failed to write temporary file",
									r->rd_value);
							break;
						}
						off += count;
					}
					/* ファイルディスクリプタを先頭に戻す */
					if (lseek(fd, 0, SEEK_SET) == (off_t) -1)
						xerror(0, errno, "redirect :%s", r->rd_value);
					/* XXX: ファイルディスクリプタを読み取り専用にすべき? */
				}
				if (content != r->rd_herecontent)
					free(content);
				break;
			default:
				assert(false);
		}
		if (fd != r->rd_fd) {
			if (close(r->rd_fd) < 0 && errno != EBADF)
				xerror(0, errno, "redirect: %s", r->rd_value);
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
	xerror(0, errno, "redirect: %s", r->rd_value);
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
		if (save->sr_copyfd >= 0) {
			if (!FD_ISSET(save->sr_copyfd, &leave)) {
				if (close(save->sr_copyfd) < 0 && errno != EBADF)
					xerror(0, errno, "closing copied file descriptor %d",
							save->sr_copyfd);
			}
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
		close(pipefd[0]);

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
