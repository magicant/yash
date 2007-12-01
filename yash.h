/* Yash: yet another shell */
/* © 2007 magicant */

/* This software can be redistributed and/or modified under the terms of
 * GNU General Public License, version 2 or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRENTY. */


#ifndef YASH_H
#define YASH_H

#include <stdarg.h>
#include <stdbool.h>
#include <sys/types.h>


/* -- Misc -- */

#define YASH_VERSION   "1.0"
#define YASH_COPYRIGHT "Copyright (C) 2007 magicant"

#define ENV_USER     "USER"
#define ENV_LOGNAME  "LOGNAME"
#define ENV_HOME     "HOME"
#define ENV_LANG     "LANG"
#define ENV_PATH     "PATH"
#define ENV_PWD      "PWD"
#define ENV_SPWD     "SPWD"
#define ENV_OLDPWD   "OLDPWD"
#define ENV_SHLVL    "SHLVL"
#define ENV_HOSTNAME "HOSTNAME"

#define EXIT_NOEXEC   126
#define EXIT_NOTFOUND 127


/* -- main (yash.c) -- */

extern bool is_loginshell;
extern bool is_interactive;

void setsigaction(void);
void resetsigaction(void);

void init_interactive(void);
void finalize_interactive(void);

void exec_file(const char *path, bool suppresserror);
void exec_file_exp(const char *path, bool suppresserror);

void yash_exit(int exitcode)
	__attribute__ ((noreturn));


/* -- utilities -- */

void *xcalloc(size_t nmemb, size_t size)
	__attribute__ ((malloc));
void *xmalloc(size_t size)
	__attribute__ ((malloc));
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s)
	__attribute__ ((malloc));
char *xstrndup(const char *s, size_t len)
	__attribute__ ((malloc));
char **straryclone(char **ary)
	__attribute__ ((malloc));
char *skipblanks(const char *s);
char *skipspaces(const char *s);
char *skipwhites(const char *s);
char *stripspaces(char *s);
char *strjoin(int argc, char *const *argv, const char *padding);
char *read_all(int fd);

#define MAX(X,Y) \
	({ typeof(X) _X = (X); typeof(Y) _Y = (Y); _X > _Y ? _X : _Y; })
#define MIN(X,Y) \
	({ typeof(X) _X = (X); typeof(Y) _Y = (Y); _X < _Y ? _X : _Y; })

#define STRBUF_INITSIZE 16
struct strbuf {
	char *contents;
	size_t length;
	size_t maxlength;
};

void strbuf_init(struct strbuf *buf);
void strbuf_destroy(struct strbuf *buf);
char *strbuf_tostr(struct strbuf *buf);
void strbuf_setmax(struct strbuf *buf, size_t newmax);
void strbuf_trim(struct strbuf *buf);
void strbuf_clear(struct strbuf *buf);
void strbuf_ninsert(struct strbuf *buf, size_t i, const char *s, size_t n);
void strbuf_insert(struct strbuf *buf, size_t i, const char *s);
void strbuf_nappend(struct strbuf *buf, const char *s, size_t n);
void strbuf_append(struct strbuf *buf, const char *s);
int strbuf_vprintf(struct strbuf *buf, const char *format, va_list ap)
	__attribute__ ((format (printf, 2, 0)));
int strbuf_printf(struct strbuf *buf, const char *format, ...)
	__attribute__ ((format (printf, 2, 3)));


/* -- readline/history -- */

extern char *history_filename;
extern int history_filesize;
extern int history_histsize;
extern char *readline_prompt1;
extern char *prompt_command;

void initialize_readline(void);
void finalize_readline(void);
int yash_readline(int ptype, char **result);


/* -- パーサ -- */

struct _redirect;
struct _process;
struct _pipeline;
struct _statement;
typedef struct _redirect  REDIR;
typedef struct _process   PROCESS;
typedef struct _pipeline  PIPELINE;
typedef struct _statement STATEMENT;

/* リダイレクトを表す */
struct _redirect {
	REDIR *next;
	int    rd_fd;      /* リダイレクトするファイルディスクリプタ */
	int    rd_destfd;  /* リダイレクト先のファイルディスクリプタ */
	char  *rd_file;    /* リダイレクト先のファイル名 (NULL なら閉じる) */
	int    rd_flags;   /* リダイレクト先のファイルの開き方 (open の第 2 引数) */
};
/* rd_destfd が負のときのみ rd_{file|flags} が使用される。
 * 例えば '2>&1' では、rd_fd = 2, rd_destfd = 1 となる。 */

/* パイプラインに含まれる一つのプロセス */
struct _process {
	PROCESS   *next;       /* パイプライン内の次のプロセス */
	enum {
		PT_NORMAL,    /* 普通のコマンド */
		PT_GROUP,     /* 現在のシェルで実行するコマンド群: { ... } */
		PT_SUBSHELL,  /* サブシェルで実行するコマンド群: ( ... ) */
	}          p_type;
	char      *p_body;
	STATEMENT *p_subcmds;  /* プロセスに含まれる文の内容 */
};
/* p_type が非 PT_NORMAL のとき、プロセスに含まれるサブステートメントが
 * p_subcmds に入る。p_body はコマンドの内容である。
 * p_subcmds が非 NULL のとき、p_body にはリダイレクト関連の記述が入る。 */

/* 一つのパイプライン */
struct _pipeline {
	PIPELINE *next;     /* コマンドリスト内の次のパイプライン */
	PROCESS  *pl_proc;  /* パイプラインの最初のプロセス */
	bool      pl_neg;   /* パイプラインの終了ステータスを反転するかどうか */
	bool      pl_loop;  /* パイプラインが環状かどうか */
	bool      pl_next_cond;
};
/* pl_next_cond は next を実行する条件。
 * true は '&&' を、false は '||' を意味する */

/* 一つの文 */
struct _statement {
	STATEMENT *next;        /* ソース内の次の文 */
	PIPELINE  *s_pipeline;  /* 文の中の最初のパイプライン */
	bool       s_bg;        /* バックグラウンドかどうか */
	char      *s_name;      /* 画面表示用のコマンド名 */
};

STATEMENT *parse_all(const char *src, bool *more);
char **expand_pipe(const char *pipesrc, REDIR **redirs);
void redirsfree(REDIR *redirs);
void procsfree(PROCESS *processes);
void pipesfree(PIPELINE *pipelines);
void statementsfree(STATEMENT *statements);


/* -- path -- */

char *which(const char *name, const char *path);
bool isexecutable(const char *path);
char *expand_tilde(const char *path);
char *skip_homedir(const char *path);
char *collapse_homedir(const char *path);


/* -- 実行 (exec) -- */

extern bool huponexit;
extern int laststatus;

enum jstatus { JS_NULL, JS_RUNNING, JS_DONE, JS_STOPPED, };
extern const char * const jstatusstr[];
/* jstatusstr のインデックスとして jstatus の値を使用している */

#define MAX_JOB 256

typedef struct {
	pid_t j_pgid;           /* プロセスグループの ID */
	pid_t *j_pids;          /* 子プロセスの ID の配列 */
	enum jstatus j_status;  /* 最後に確認したジョブの状態 */
	bool j_statuschanged;    /* まだ状態変化を報告していないなら true */
	enum {
		JF_NOHUP = 1
	} j_flags;
	int j_exitstatus;       /* ジョブの終了ステータス */
	char *j_name;           /* 表示用ジョブ名 */
} JOB;
/* j_pgid が 0 の JOB は、JOB そのものが無効な値 (null) であることを示す。 */
/* j_pids は 0 終端である。終了したプロセスは値が反数になる。 */

extern JOB *joblist;
extern size_t joblistlen;
extern size_t currentjobnumber;

extern volatile bool cancel_wait;

int exitcode_from_status(int status);
unsigned job_count(void);
void print_job_status(size_t jobnumber, bool changedonly, bool printpids);
void print_all_job_status(bool changedonly, bool printpids);
int get_jobnumber_from_pid(pid_t pid);
void wait_all(int blockedjob);
void send_sighup_to_all_jobs(void);
void sig_hup(int signal);
void sig_chld(int signal);
void exec_statements(STATEMENT *statements);


/* -- 組込みコマンド -- */

typedef int (*cbody)(int argc, char *const *argv);

typedef struct {
	const char *b_name;  /* コマンド名 */
	cbody b_body;        /* コマンド本体 */
} BUILTIN;

extern const BUILTIN const builtins[];

BUILTIN assoc_builtin(const char *name);

#define OPT_HISTSIZE      "histsize"
#define OPT_HISTFILE      "histfile"
#define OPT_HISTFILESIZE  "histfilesize"
#define OPT_PS1           "ps1"
#define OPT_PS2           "ps2"
#define OPT_PROMPTCOMMAND "promptcommand"
#define OPT_HUPONEXIT     "huponexit"


/* -- エイリアス -- */

/* エイリアスのエントリ。
 * ALIAS データは連鎖リストとして定義される */
/* エイリアスの name と value の領域は一度に malloc される:
 * エイリアスを削除するときは name のみを free し、value を free してはならない
 */
typedef struct alias_t {
	const char *name, *value;
	struct alias_t *next;
} ALIAS;

int set_alias(const char *name, const char *value);
int remove_alias(const char *name);
void remove_all_aliases(void);
const char *get_alias(const char *name);
const ALIAS *get_all_aliases(void);
int for_all_aliases(int (*func)(const char *name, const char *value));
char *expand_alias(const char *s);


#endif  /* YASH_H */
