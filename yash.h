/* Yash: yet another shell */
/* © 2007 magicant */

/* This software can be redistributed and/or modified under the terms of
 * GNU General Public License, version 2 or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRENTY. */


#ifndef YASH_H
#define YASH_H

#include <stdbool.h>
#include <sys/types.h>
#include <wordexp.h>


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

void yash_exit(int exitcode);


/* -- utilities -- */

void *xcalloc(size_t nmemb, size_t size);
void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t len);
char **straryclone(char **ary);
char *skipwhites(const char *s);
char *stripwhites(char *s);
char *strjoin(int argc, char *const *argv, const char *padding);

#define MAX(X,Y) \
	({ typeof(X) _X = (X); typeof(Y) _Y = (Y); _X > _Y ? _X : _Y; })
#define MIN(X,Y) \
	({ typeof(X) _X = (X); typeof(Y) _Y = (Y); _X < _Y ? _X : _Y; })


/* -- 一回のプロンプト入力の解析結果 -- */

/* リダイレクトを表す */
typedef struct {
	int rd_flags;   /* リダイレクトするファイルの開き方 (open の第 2 引数) */
	int rd_fd;      /* リダイレクトするファイルディスクリプタ */
	char *rd_file;  /* リダイレクト先のファイル名 (NULL なら閉じる) */
} REDIR;

/* リダイレクトを含めたコマンド */
typedef struct _single_command {
	enum {        /* このコマンドの種類 */
		CT_END,   /* コマンドの終わり (フォアグラウンド) (';') */
		CT_BG,    /* コマンドの終わり (バックグラウンド) ('&') */
		CT_PIPED, /* このコマンドを次のコマンドにパイプする ('|') */
		CT_AND,   /* このコマンドの結果が 0 なら次のを実行する ('&&') */
		CT_OR,    /* このコマンドの結果が非 0 なら次のを実行する ('||') */
	} c_type;
	int c_argc;
	struct _single_command *c_subcmds;  /* サブシェルで実行するコマンドの配列 */
	char **c_argv;        /* コマンドライン引数の解析結果 */
	size_t c_redircnt;    /* c_redir にふくまれる REDIR の数 */
	REDIR *c_redir;       /* リダイレクトのリスト */
	char *c_name;         /* 表示用のコマンド名 (NULL かもしれない) */
} SCMD;
/* c_subcmds と c_argv の一方が NULL でもう一方が非 NULL である。
 * c_subcmds はコマンドラインで括弧 ( ) で示される、サブシェルで実行する一連の
 * コマンドの列である。この配列は NULL 終端とは限らない。
 * c_argv は実行する一つのコマンドとその引数である。この配列は NULL 終端である。
 * c_argc は c_subcmds/c_argv に含まれる要素の個数を示す。 */
/* コマンド全体は SCMD のリストとして表される。
 * c_type に CT_PIPED を持つ SCMD は次の SCMD とパイプされる。
 * 各 SCMD のリダイレクトはパイプを繋げた後 行う。 */


/* -- readline/history -- */

extern char *history_filename;
extern int history_filesize;
extern int history_histsize;
extern char *readline_prompt1;
extern char *prompt_command;

void initialize_readline(void);
void finalize_readline(void);
int yash_readline(char **result);


/* -- パーサ -- */

ssize_t parse_line(const char *line, SCMD **result);
void redirsfree(REDIR *redirs, size_t count);
void scmdsfree(SCMD *scmds, size_t count);


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

enum {   /* print_[all_]job_status でつかうオプション */
	PJS_ALL,     /* 全部表示する */
	PJS_CHONLY,  /* 変化があったもののみ表示する */
	PJS_SILENT,  /* 何も表示しない */
};

int exitcode_from_status(int status);
unsigned job_count(void);
void print_job_status(size_t jobnumber, bool changedonly, bool printpids);
void print_all_job_status(bool changedonly, bool printpids);
int get_jobnumber_from_pid(pid_t pid);
void wait_all(int blockedjob);
void send_sighup_to_all_jobs(void);
void sig_hup(int signal);
void sig_chld(int signal);
void exec_list(SCMD *scmds, size_t count);


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
