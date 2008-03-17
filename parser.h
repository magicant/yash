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
typedef struct and_or_T {
	struct and_or_T   *next;
	struct pipeline_T *ao_pipelines;  /* リストを構成するパイプラインたち */
	bool               ao_async;      /* この and/or リストを非同期実行するか */
} and_or_T;

/* パイプラインを表す */
typedef struct pipeline_T {
	struct pipeline_T *next;
	struct command_T  *pl_commands;  /* パイプラインを構成するコマンドたち */
	bool               pl_neg, pl_loop, pl_next_cond;
} pipeline_T;
/* pl_neg はパイプラインの終了ステータスを反転するかどうか。
 * pl_loop はパイプラインが環状かどうか。
 * pl_next_cond は next を実行する条件で、'&&' なら true、'||' なら false。 */

/* command_T の種類を表す */
typedef enum {
	CT_SIMPLE,     /* シンプルコマンド */
	CT_GROUP,      /* { } で囲んだコマンドグループ */
	CT_SUBSHELL,   /* ( ) で囲んだサブシェルコマンドグループ */
	CT_IF,         /* if コマンド */
	CT_FOR,        /* for コマンド */
	CT_WHILE,      /* while/until コマンド */
	CT_CASE,       /* case コマンド */
} commandtype_T;

/* パイプラインを構成する一つのコマンドを表す */
typedef struct command_T {
	struct command_T *next;
	commandtype_T     c_type;
	unsigned long     c_lineno;   /* このコマンドの行番号 */
	struct redir_T   *c_redirs;   /* このコマンドで行うリダイレクト */
	union {
		struct {
			struct assign_T *assigns;  /* このコマンドで行う変数代入 */
			void           **words;    /* コマンド名と引数 */
		} simplecontent;
		struct and_or_T     *subcmds;  /* CT_GROUP, CT_SUBSHELL の内容 */
		struct {
			struct and_or_T *ifcond;   /* if の条件 */
			struct and_or_T *ifthen;   /* if で条件が真のとき実行するコマンド */
			struct and_or_T *ifelse;   /* if で条件が偽のとき実行するコマンド */
		} ifcontent;
		struct {
			wchar_t         *forname;  /* for で回す変数名 */
			void           **forwords; /* 代入する word のリスト */
			struct and_or_T *forcmds;  /* for で実行するコマンド */
		} forcontent;
		struct {
			bool             whltype;  /* while なら 1、until なら 0 */
			struct and_or_T *whlcond;  /* while/until の条件 */
			struct and_or_T *whlcmds;  /* while/until で実行するコマンド */
		} whilecontent;
		struct {
			struct wordunit_T *casword;   /* case で検索する word */
			struct caseitem_T *casitems;  /* case の各項目 */
		} casecontent;
	} c_content;
} command_T;
#define c_assigns  c_content.simplecontent.assigns
#define c_words    c_content.simplecontent.words
#define c_subcmds  c_content.subcmds
#define c_ifcond   c_content.ifcontent.ifcond
#define c_ifthen   c_content.ifcontent.ifthen
#define c_ifelse   c_content.ifcontent.ifelse
#define c_forname  c_content.forcontent.forname
#define c_forwords c_content.forcontent.forwords
#define c_forcmds  c_content.forcontent.forcmds
#define c_whltype  c_content.whilecontent.whltype
#define c_whlcond  c_content.whilecontent.whlcond
#define c_whlcmds  c_content.whilecontent.whlcmds
#define c_casword  c_content.casecontent.casword
#define c_casitems c_content.casecontent.casitems
/* c_words, c_forwords は wordunit_T へのポインタの NULL 終端配列へのポインタ */

/* case コマンドの一つの項目を表す */
typedef struct caseitem_T {
	struct caseitem_T *next;
	void             **ci_patterns;  /* 一致するか調べるパタンの配列 */
	struct and_or_T   *ci_commands;  /* 一致したときに実行するコマンド */
} caseitem_T;
/* ci_patterns は wordunit_T へのポインタの NULL 終端配列へのポインタ */

/* wordunit_T の種類を表す */
typedef enum {
	WT_STRING,  /* 文字列部分 (引用符を含む) */
	WT_PARAM,   /* パラメータ展開 */
	WT_CMDSUB,  /* コマンド置換 */
	WT_ARITH,   /* 数式展開 */
} wordunittype_T;

/* 単語展開の対象となる単語の構成要素を表す */
typedef struct wordunit_T {
	struct wordunit_T *next;
	wordunittype_T     wu_type;
	union {
		wchar_t           *string;  /* 文字列部分 */
		struct paramexp_T *param;   /* パラメータ展開の内容 */
		wchar_t           *cmdsub;  /* コマンド置換で実行するコマンド */
		struct wordunit_T *arith;   /* 数式展開の内容 */
	} wu_value;
} wordunit_T;
#define wu_string wu_value.string
#define wu_param  wu_value.param
#define wu_cmdsub wu_value.cmdsub
#define wu_arith  wu_value.arith

/* paramexp_T の種類を表す */
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
} paramexptype_T;
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
typedef struct paramexp_T {
	paramexptype_T pe_type;
	wchar_t *pe_name;
	struct wordunit_T *pe_match, *pe_subst;
} paramexp_T;
/* pe_name は変数名。
 * pe_match は変数の内容とマッチさせる単語で、PT_MATCH, PT_SUBST でつかう。
 * pe_subst は変数の内容を置換する単語で、PT_NONE, PT_MATCH 以外で使う。 */

/* 代入を表す */
typedef struct assign_T {
	struct assign_T *next;
	wchar_t *name;             /* 代入する変数名 */
	struct wordunit_T *value;  /* 代入する値 */
} assign_T;

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
} redirtype_T;

/* リダイレクトを表す */
typedef struct redir_T {
	struct redir_T *next;
	redirtype_T rd_type;
	int rd_fd;  /* リダイレクトするファイル記述子 */
	union {
		struct wordunit_T *filename;
		struct {
			wchar_t *hereend;  /* ヒアドキュメントの終わりを示すトークン */
			struct wordunit_T *herecontent;  /* ヒアドキュメントの内容 */
		} heredoc;
	} rd_value;
} redir_T;
#define rd_filename    rd_value.filename
#define rd_hereend     rd_value.heredoc.hereend
#define rd_herecontent rd_value.heredoc.herecontent
/* 例えば '2>&1' では rd_type = RT_DUPOUT, rd_fd = 2, rd_filename = "1" となる。
 * rd_herecontent はヒアドキュメントの内容。RT_HERERT では、予め行頭のタブは
 * 取り除いてある。rd_hereend に引用符がある場合、ヒアドキュメントの内容の
 * パラメータ展開などは行わないので、rd_herecontent は単一の WT_STRING の
 * word_unit になる。 */


/********** 構文解析ルーチンへのインタフェース宣言 **********/

struct xwcsbuf_T;

/* 入力を読み取ってバッファに追加する関数。
 * 入力は基本的に行単位だが、一行が長い場合などは行の途中までしか読み込まない
 * 場合もある。しかし、一度に (改行を越えて) 複数行を読み込むことはない。
 * inputinfo は、read_and_parse 関数の第 2 引数で与えられたポインタである。
 * エラーのときは、エラーメッセージを出して EOF を返す。
 * 戻り値: 0:   何らかの入力があった。
 *         1:   対話モードで SIGINT を受けた。(buf の内容は不定)
 *         EOF: EOF に達したか、エラーがあった。(buf の内容は不定) */
typedef int inputfunc_T(struct xwcsbuf_T *buf, void *inputinfo);

typedef struct parseinfo_T {
	const char *filename; /* エラー表示で使うファイル名。NULL でも良い。 */
	unsigned long lineno; /* 行番号。最初は 1 にしておく。 */
	inputfunc_T *input;   /* 入力関数 */
	void *inputinfo;      /* 入力関数に渡す情報 */
} parseinfo_T;

extern int read_and_parse(
		parseinfo_T *restrict info, and_or_T **restrict result)
	__attribute__((nonnull));


/********** 構文木を文字列に戻すルーチン **********/

extern wchar_t *commands_to_wcstring(const and_or_T *commands)
	__attribute__((malloc));


/********** 構文木データを解放するルーチン **********/

extern void andorsfree(and_or_T *a);


#endif /* PARSER_H */
