/* Yash: yet another shell */
/* parser.h: interface to the parser */
/* © 2007 magicant */

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


#ifndef PARSER_H
#define PARSER_H

#include "readline.h"


typedef struct x_redirect  REDIR;
typedef struct x_process   PROCESS;
typedef struct x_pipeline  PIPELINE;
typedef struct x_statement STATEMENT;

/* リダイレクトを表す */
struct x_redirect {
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
struct x_process {
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
struct x_pipeline {
	PIPELINE *next;     /* コマンドリスト内の次のパイプライン */
	PROCESS  *pl_proc;  /* パイプラインの最初のプロセス */
	bool      pl_neg;   /* パイプラインの終了ステータスを反転するかどうか */
	bool      pl_loop;  /* パイプラインが環状かどうか */
	bool      pl_next_cond;
};
/* pl_next_cond は next を実行する条件。
 * true は '&&' を、false は '||' を意味する */

/* 一つの文 */
struct x_statement {
	STATEMENT *next;        /* ソース内の次の文 */
	PIPELINE  *s_pipeline;  /* 文の中の最初のパイプライン */
	bool       s_bg;        /* バックグラウンドかどうか */
};

//STATEMENT *parse_string(const char **src, char end, const char *filename)
//	__attribute__((nonnull(1)));
int read_and_parse(getline_t *input, const char *filename, STATEMENT **result)
	__attribute__((nonnull(1,3)));
unsigned get_line_number(void);
void set_line_number(unsigned num);
char *skip_with_quote(const char *s, const char *delim)
	__attribute__((nonnull));
char *skip_without_quote(const char *s, const char *delim)
	__attribute__((nonnull));
char *make_statement_name(PIPELINE *p);
char *make_pipeline_name(PROCESS *processes, bool neg, bool loop);
void redirsfree(REDIR *redirs);
void procsfree(PROCESS *processes);
void pipesfree(PIPELINE *pipelines);
void statementsfree(STATEMENT *statements);


#endif /* PARSER_H */
