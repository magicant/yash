/* Yash: yet another shell */
/* parser.h: syntax parser */
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


/* 制限: 識別子 c_* が重複するため、parser.h と termios.h を同時に
 *       インクルードしてはならない */


#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include <stddef.h>


/********** 解析したソースコードの構文木を構成する構造体の宣言 **********/

/* 基本的に、構文木の各要素は連鎖リスト形式になっている。
 * 構造体の next メンバが次の構文要素を表す。 */

/* and/or リストを表す */
typedef struct and_or_t {
	struct and_or_t   *next;
	struct pipeline_t *ao_pipelines;  /* リストを構成するパイプラインたち */
	bool               ao_async;      /* この and/or リストを非同期実行するか */
	unsigned long      ao_lineno;     /* このコマンドの行番号 */
} and_or_t;

/* パイプラインを表す */
typedef struct pipeline_t {
	struct pipeline_t *next;
	struct command_t  *pl_commands;  /* パイプラインを構成するコマンドたち */
	bool               pl_neg, pl_loop, pl_next_cond;
} pipeline_t;
/* pl_neg はパイプラインの終了ステータスを反転するかどうか。
 * pl_loop はパイプラインが環状かどうか。
 * pl_next_cond は next を実行する条件で、'&&' なら true、'||' なら false。 */

/* command_t の種類を表す */
typedef enum {
	CT_SIMPLE,     /* シンプルコマンド */
	CT_GROUP,      /* { } で囲んだコマンドグループ */
	CT_SUBSHELL,   /* ( ) で囲んだサブシェルコマンドグループ */
} commandtype_t;

/* パイプラインを構成する一つのコマンドを表す */
typedef struct command_t {
	struct command_t *next;
	commandtype_t     c_type;
	struct assign_t  *c_assigns;  /* このコマンドで行う変数代入 */
	struct redir_t   *c_redirs;   /* このコマンドで行うリダイレクト */
	union {
		void           **words;   /* コマンド名と引数 */
		struct and_or_t *subcmds; /* このコマンドに含まれるサブコマンド */
	} c_content;
} command_t;
#define c_words   c_content.words
#define c_subcmds c_content.subcmds
/* c_words は、wordunit_t へのポインタの NULL 終端配列へのポインタ */

/* wordunit_t の種類を表す */
typedef enum {
	WT_STRING,  /* 文字列部分 (引用符を含む) */
	WT_PARAM,   /* パラメータ展開 */
	WT_CMDSUB,  /* コマンド置換 */
	//WT_ARITH,   /* 数式展開 */
} wordunittype_t;

/* 単語展開の対象となる単語の構成要素を表す */
typedef struct wordunit_t {
	struct wordunit_t *next;
	wordunittype_t     wu_type;
	union {
		wchar_t           *string;  /* 文字列部分 */
		struct paramexp_t *param;   /* パラメータ展開の内容 */
		wchar_t           *cmdsub;  /* コマンド置換で実行するコマンド */
		//struct arithexp_t *arith;   /* 数式展開の内容 */
	} wu_value;
} wordunit_t;
#define wu_string wu_value.string
#define wu_param  wu_value.param
#define wu_cmdsub wu_value.cmdsub
#define wu_arith  wu_value.arith

/* paramexp_t の種類を表す */
typedef enum {
	PT_NONE,                   /* 通常 */
	PT_MINUS,                  /* ${name-subst} */
	PT_PLUS,                   /* ${name+subst} */
	PT_ASSIGN,                 /* ${name=subst} */
	PT_ERROR,                  /* ${name?subst} */
	PT_MATCH,                  /* ${name#match}, ${name%match} */
	PT_SUBST,                  /* ${name/match/subst} */
	PT_NUMBER       = 1 << 3,  /* ${#name} */
	PT_COLON        = 1 << 4,  /* ${name:-subst}, ${name:+subst}, etc. */
	PT_MATCHHEAD    = 1 << 5,  /* 先頭のみにマッチ */
	PT_MATCHTAIL    = 1 << 6,  /* 末尾のみにマッチ */
	PT_MATCHLONGEST = 1 << 7,  /* できるだけ長くマッチ */
	PT_SUBSTALL     = 1 << 8,  /* マッチしたもの全て置換 */
} paramexptype_t;
#define PT_MASK ((1 << 3) - 1)
/*            type   COLON  MATCHH MATCHT MATCHL SUBSTA
 * ${n-s}     MINUS   no
 * ${n+s}     PLUS    no
 * ${n=s}     ASSIGN  no
 * ${n?s}     ERROR   no
 * ${n:-s}    MINUS   yes
 * ${n:+s}    PLUS    yes
 * ${n:=s}    ASSIGN  yes
 * ${n:?s}    ERROR   yes
 * ${n#m}     MATCH   no     yes    no    no     no
 * ${n##m}    MATCH   no     yes    no    yes    no
 * ${n%m}     MATCH   no     no     yes   no     no
 * ${n%%m}    MATCH   no     no     yes   yes    no
 * ${n/m/s}   SUBST   no     no     no    yes    no
 * ${n/#m/s}  SUBST   no     yes    no    yes    no
 * ${n/%m/s}  SUBST   no     no     yes   yes    no
 * ${n//m/s}  SUBST   no     no     no    yes    yes
 * ${n:/m/s}  SUBST   yes    yes    yes
 *
 * PT_NUMBER は、変数の内容ではなく変数の内容の文字数に置換するフラグで、
 * PT_NONE でのみ有効。 */

/* パラメータ展開を表す */
typedef struct paramexp_t {
	paramexptype_t pe_type;
	wchar_t *pe_name;
	struct wordunit_t *pe_match, *pe_subst;
} paramexp_t;
/* pe_name は変数名。
 * pe_match は変数の内容とマッチさせる単語で、PT_MATCH, PT_SUBST でつかう。
 * pe_subst は変数の内容を置換する単語で、PT_NONE, PT_MATCH 以外で使う。 */

/* 代入を表す */
typedef struct assign_t {
	struct assign_t *next;
	wchar_t *name;             /* 代入する変数名 */
	struct wordunit_t *value;  /* 代入する値 */
} assign_t;

/* リダイレクトの種類を表す */
typedef enum {
	RT_INPUT,    /* <file */
	RT_OUTPUT,   /* >file */
	RT_CLOBBER,  /* >|file */
	RT_APPEND,   /* >>file */
	RT_INOUT,    /* <>file */
	RT_DUPIN,    /* <&fd */
	RT_DUPOUT,   /* >&fd */
	RT_HERE,     /* <<END */
	RT_HERERT,   /* <<-END */
} redirtype_t;

/* リダイレクトを表す */
typedef struct redir_t {
	struct redir_t *next;
	redirtype_t rd_type;
	int rd_fd;  /* リダイレクトするファイル記述子 */
	union {
		struct wordunit_t *filename;
		struct {
			wchar_t *hereend;  /* ヒアドキュメントの終わりを示すトークン */
			struct wordunit_t *herecontent;  /* ヒアドキュメントの内容 */
		} heredoc;
	} rd_value;
} redir_t;
#define rd_filename    rd_value.filename
#define rd_hereend     rd_value.heredoc.hereend
#define rd_herecontent rd_value.heredoc.herecontent
/* 例えば '2>&1' では rd_type = RT_DUPOUT, rd_fd = 2, rd_filename = "1" となる。
 * rd_herecontent はヒアドキュメントの内容。RT_HERERT では、予め行頭のタブは
 * 取り除いてある。rd_hereend に引用符がある場合、ヒアドキュメントの内容の
 * パラメータ展開などは行わないので、rd_herecontent は単一の WT_STRING の
 * word_unit になる。 */


/********** 構文解析ルーチンへのインタフェース宣言 **********/

struct xwcsbuf_t;

/* 入力を読み取ってバッファに追加する関数。
 * 入力は基本的に行単位だが、一行が長い場合などは行の途中までしか読み込まない
 * 場合もある。しかし、一度に (改行を越えて) 複数行を読み込むことはない。
 * inputinfo は、read_and_parse 関数の第 2 引数で与えられたポインタである。
 * 戻り値: 0:   何らかの入力があった。
 *         1:   対話モードで SIGINT を受けた。(buf の内容は不定)
 *         EOF: EOF に達したか、エラーがあった。(buf の内容は不定) */
typedef int inputfunc_t(struct xwcsbuf_t *buf, void *inputinfo);

typedef struct parseinfo_t {
	const char *filename; /* エラー表示で使うファイル名。NULL でも良い。 */
	unsigned long lineno; /* 行番号。最初は 0 にしておく。 */
	inputfunc_t *input;   /* 入力関数 */
	void *inputinfo;      /* 入力関数に渡す情報 */
} parseinfo_t;

extern int read_and_parse(
		parseinfo_t *restrict info, and_or_t **restrict result)
	__attribute__((nonnull));


/********** 構文木を文字列に戻すルーチン **********/

extern wchar_t *commands_to_wcstring(const and_or_t *commands)
	__attribute__((malloc));


/********** 構文木データを解放するルーチン **********/

extern void andorsfree(and_or_t *a);


#endif /* PARSER_H */
