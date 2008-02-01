/* Yash: yet another shell */
/* parser.h: interface to the parser */
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


#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include "lineinput.h"


/* 解析中のソースコードに関する情報を表す */
struct parse_info {
	const char *filename;
	unsigned lineno;
	getline_t *input;
	void *inputinfo;
};
/* filename はソースコードのファイル名。エラー表示で使う。NULL でもよい。
 * lineno は行番号。解析が進むにつれて増える。最初は 0。
 * input は入力関数。 */

typedef struct x_redirect  REDIR;
typedef struct x_process   PROCESS;
typedef struct x_pipeline  PIPELINE;
typedef struct x_statement STATEMENT;

/* リダイレクトを表す */
struct x_redirect {
	REDIR *next;
	enum redirect_type {
		RT_INPUT, RT_OUTPUT, RT_OUTCLOB, RT_APPEND, RT_INOUT,
		RT_DUPIN, RT_DUPOUT, RT_HERE,    RT_HERERT,
	}      rd_type;
	int    rd_fd;           /* リダイレクトするファイルディスクリプタ */
	char  *rd_value;        /* リダイレクトの引数 (常に非 NULL) */
	char  *rd_herecontent;  /* ヒアドキュメントの内容 */
};
/* 例えば '2>&1' では、rd_fd = 2, rd_value = "1" となる。 */
/* rd_herecontent は実際のヒアドキュメントの内容。RT_HERERT では、内容を
 * 読み取った段階で予め行頭のタブを除いておく。 */

/* パイプラインに含まれる一つのプロセス */
struct x_process {
	PROCESS       *next;       /* パイプライン内の次のプロセス */
	enum {
		PT_NORMAL,    /* 普通のコマンド */
		PT_GROUP,     /* 現在のシェルで実行するコマンド群: { ... } */
		PT_SUBSHELL,  /* サブシェルで実行するコマンド群: ( ... ) */
	}              p_type;
	char         **p_assigns;  /* 変数代入 */
	union {
		char     **p_args;     /* コマンド名と引数 */
		STATEMENT *p_subcmds;  /* プロセスに含まれる文の内容 */
	} p_content_;
	REDIR         *p_redirs;   /* プロセスに対するリダイレクト */
};
#define p_args    p_content_.p_args
#define p_subcmds p_content_.p_subcmds
/* p_type が PT_NORMAL のとき、p_args にコマンドの内容 (変数代入やリダイレクトを
 * 除く) が入る。(空白ごとに分けてエイリアスを展開しただけで、
 * それ以上パラメータの展開などは行っていない)
 * p_type が非 PT_NORMAL のときは、プロセスに含まれるサブステートメントが
 * p_subcmds に入る。
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


extern unsigned lineno;

int read_and_parse(
		struct parse_info *restrict info, STATEMENT **restrict result)
	__attribute__((nonnull));
char *skip_with_quote(const char *restrict s, const char *restrict delim)
	__attribute__((nonnull));
char *skip_without_quote(const char *restrict s, const char *restrict delim)
	__attribute__((nonnull));
char *make_statement_name(PIPELINE *p);
char *make_pipeline_name(PIPELINE *p);
void procsfree(PROCESS *processes);
void pipesfree(PIPELINE *pipelines);
void statementsfree(STATEMENT *statements);


#endif /* PARSER_H */
