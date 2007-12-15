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
#include <stdio.h>
#include <sys/types.h>


#define UNUSED __attribute__((__unused__))


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

extern char *prompt_command;

void setsigaction(void);
void resetsigaction(void);

int exec_file(const char *path, bool suppresserror);
int exec_file_exp(const char *path, bool suppresserror);
int exec_source(const char *name, const char *code);
void exec_source_and_exit(const char *name, const char *code)
	__attribute__((noreturn));

void init_interactive(void);
void finalize_interactive(void);

void yash_exit(int exitcode)
	__attribute__((noreturn));


/* -- utilities -- */

void *xcalloc(size_t nmemb, size_t size)
	__attribute__((malloc));
void *xmalloc(size_t size)
	__attribute__((malloc));
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s)
	__attribute__((malloc));
char *xstrndup(const char *s, size_t len)
	__attribute__((malloc));
char **straryclone(char **ary)
	__attribute__((malloc));
void recfree(void **ary);
char *skipblanks(const char *s);
char *skipspaces(const char *s);
char *skipwhites(const char *s);
char *strchug(char *s);
char *strchomp(char *s);
char *strjoin(int argc, char *const *argv, const char *padding);

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
void strbuf_cappend(struct strbuf *buf, char c);
void strbuf_replace(struct strbuf *buf, size_t i, size_t n, const char *s);
int strbuf_vprintf(struct strbuf *buf, const char *format, va_list ap)
	__attribute__((format (printf, 2, 0)));
int strbuf_printf(struct strbuf *buf, const char *format, ...)
	__attribute__((format (printf, 2, 3)));

#define PLIST_INITSIZE 8
struct plist {
	void **contents;
	size_t length;
	size_t maxlength;
};
void plist_init(struct plist *list);
void plist_destroy(struct plist *list);
void **plist_toary(struct plist *list);
void plist_setmax(struct plist *list, size_t newmax);
void plist_trim(struct plist *list);
void plist_clear(struct plist *list);
void plist_insert(struct plist *list, size_t i, void *e);
void plist_append(struct plist *list, void *e);

#define HASHT_INITSIZE 5
struct hasht {
	size_t   capacity;
	size_t   count;
	ssize_t *indices;
	ssize_t  nullindex;
	ssize_t  tailindex;
	struct hash_entry {
		ssize_t   next;
		unsigned  hash;
		char     *key;
		void     *value;
	}       *entries;
};
void ht_init(struct hasht *ht);
void ht_destroy(struct hasht *ht);
void ht_ensurecap(struct hasht *ht, size_t newcap);
void ht_trim(struct hasht *ht);
void ht_clear(struct hasht *ht);
void *ht_get(struct hasht *ht, const char *key);
void *ht_set(struct hasht *ht, const char *key, void *value);
void *ht_remove(struct hasht *ht, const char *key);
int ht_each(struct hasht *ht, int (*func)(const char *key, void *value));


/* -- readline/history -- */

/* ソースを一行読み込んで返す関数。
 * ptype:  現在の状況。新しい文を読み始めるときは 1、文の途中で続きを読むときは
 *         2。(この引数はプロンプトの種類を変えたりするのに使う)
 * 戻り値: 読み込んだソース。末尾の改行はない。EOF に達したときは NULL。 */
typedef char *getline_t(int ptype);

extern char *history_filename;
extern int history_filesize;
extern int history_histsize;
extern char *readline_prompt1;
extern char *readline_prompt2;

void initialize_readline(void);
void finalize_readline(void);
getline_t yash_readline;


/* -- パーサ -- */

typedef struct _redirect  REDIR;
typedef struct _process   PROCESS;
typedef struct _pipeline  PIPELINE;
typedef struct _statement STATEMENT;

/* リダイレクトを表す */
struct _redirect {
	REDIR *next;
	enum redirect_type {
		RT_INPUT, RT_OUTPUT, RT_APPEND, RT_INOUT, RT_DUP,
	}      rd_type;
	int    rd_fd;      /* リダイレクトするファイルディスクリプタ */
	char  *rd_file;    /* リダイレクト先のファイル名 (常に非 NULL) */
};
/* rd_destfd が負のときのみ rd_{file|flags} が使用される。
 * 例えば '2>&1' では、rd_fd = 2, rd_destfd = 1 となる。 */

/* パイプラインに含まれる一つのプロセス */
struct _process {
	PROCESS    *next;       /* パイプライン内の次のプロセス */
	enum {
		PT_NORMAL,    /* 普通のコマンド */
		PT_GROUP,     /* 現在のシェルで実行するコマンド群: { ... } */
		PT_SUBSHELL,  /* サブシェルで実行するコマンド群: ( ... ) */
		PT_X_PIPE,    /* サブシェル内部で仕様する特殊な値 */
	}           p_type;
	char      **p_args;
	STATEMENT  *p_subcmds;  /* プロセスに含まれる文の内容 */
	REDIR      *p_redirs;   /* プロセスに対するリダイレクト */
};
/* p_type が非 PT_NORMAL のとき、プロセスに含まれるサブステートメントが
 * p_subcmds に入る。p_args はリダイレクトを除くコマンドの内容である。
 * (空白ごとに分けてエイリアスを展開しただけで、
 * それ以上パラメータの展開などは行っていない)
 * p_redirs はリダイレクトのデータを表す。(こちらも展開していない) */

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
};

int read_and_parse(const char *filename, getline_t *input, STATEMENT **result);
unsigned get_line_number(void);
void set_line_number(unsigned num);
char *make_statement_name(PIPELINE *p);
char *make_pipeline_name(PROCESS *processes, bool neg, bool loop);
void redirsfree(REDIR *redirs);
void procsfree(PROCESS *processes);
void pipesfree(PIPELINE *pipelines);
void statementsfree(STATEMENT *statements);


/* -- コマンドライン展開 (expand) -- */

bool expand_line(char **args, int *argc, char ***argv);
char *expand_single(const char *arg);
void escape_sq(const char *s, struct strbuf *buf);
void escape_dq(const char *s, struct strbuf *buf);


/* -- path -- */

char *which(const char *name, const char *path);
bool isexecutable(const char *path);
char *expand_tilde(const char *path);
char *skip_homedir(const char *path);
char *collapse_homedir(const char *path);


/* -- 実行 (exec) -- */

extern bool huponexit;
extern int laststatus;

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

extern volatile bool cancel_wait;

void init_exec(void);
int exitcode_from_status(int status);
JOB *get_job(size_t jobnumber);
unsigned job_count(void);
bool remove_job(size_t jobnumber);
void print_job_status(size_t jobnumber, bool changedonly, bool printpids);
void print_all_job_status(bool changedonly, bool printpids);
int get_jobnumber_from_pid(pid_t pid);
void wait_all(int blockedjob);
void send_sighup_to_all_jobs(void);
void sig_hup(int signal);
void sig_chld(int signal);
void exec_statements(STATEMENT *statements);
void exec_statements_and_exit(STATEMENT *statements)
	__attribute__((noreturn));


/* -- 組込みコマンド -- */

typedef int cbody(int argc, char *const *argv);

void init_builtin(void);
cbody *get_builtin(const char *name);

int builtin_exec(int argc, char *const *argv);

#define OPT_HISTSIZE      "histsize"
#define OPT_HISTFILE      "histfile"
#define OPT_HISTFILESIZE  "histfilesize"
#define OPT_PS1           "ps1"
#define OPT_PS2           "ps2"
#define OPT_PROMPTCOMMAND "promptcommand"
#define OPT_HUPONEXIT     "huponexit"


/* -- エイリアス -- */

/* エイリアスのエントリ。 */
/* inhibit_len はエイリアスを展開する際に使う。 */
typedef struct alias {
	char   *value;
	bool    global;
	size_t  valid_len;
} ALIAS;

void init_alias(void);
void set_alias(const char *name, const char *value, bool global);
int remove_alias(const char *name);
void remove_all_aliases(void);
ALIAS *get_alias(const char *name);
int for_all_aliases(int (*func)(const char *name, ALIAS *alias));
void alias_reset(void);
void expand_alias(struct strbuf *buf, size_t i, bool global);


#endif  /* YASH_H */
