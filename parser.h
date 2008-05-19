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
    bool               pl_neg, pl_loop, pl_cond;
} pipeline_T;
/* pl_neg はパイプラインの終了ステータスを反転するかどうか。
 * pl_loop はパイプラインが環状かどうか。
 * pl_cond はパイプラインを実行する条件で、'&&' なら true、'||' なら false。
 * ただし and/or リスト内の最初のパイプラインの pl_cond は不定。 */

/* command_T の種類を表す */
typedef enum {
    CT_SIMPLE,     /* シンプルコマンド */
    CT_GROUP,      /* { } で囲んだコマンドグループ */
    CT_SUBSHELL,   /* ( ) で囲んだサブシェルコマンドグループ */
    CT_IF,         /* if コマンド */
    CT_FOR,        /* for コマンド */
    CT_WHILE,      /* while/until コマンド */
    CT_CASE,       /* case コマンド */
    CT_FUNCDEF,    /* 関数定義 */
} commandtype_T;

/* パイプラインを構成する一つのコマンドを表す */
typedef struct command_T {
    struct command_T *next;
    unsigned          refcount;   /* 参照カウント */
    commandtype_T     c_type;
    unsigned long     c_lineno;   /* このコマンドの行番号 */
    struct redir_T   *c_redirs;   /* このコマンドで行うリダイレクト */
    union {
	struct {
	    struct assign_T *assigns;  /* このコマンドで行う変数代入 */
	    void           **words;    /* コマンド名と引数 */
	} simplecontent;
	struct and_or_T     *subcmds;  /* CT_GROUP, CT_SUBSHELL の内容 */
	struct ifcommand_T  *ifcmds;   /* if の内容 */
	struct {
	    char            *forname;  /* for で回す変数名 */
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
	struct {
	    char             *funcname;  /* 定義する関数の名前 */
	    struct command_T *funcbody;  /* 定義する関数の中身 */
	} funcdef;
    } c_content;
} command_T;
#define c_assigns  c_content.simplecontent.assigns
#define c_words    c_content.simplecontent.words
#define c_subcmds  c_content.subcmds
#define c_ifcmds   c_content.ifcmds
#define c_forname  c_content.forcontent.forname
#define c_forwords c_content.forcontent.forwords
#define c_forcmds  c_content.forcontent.forcmds
#define c_whltype  c_content.whilecontent.whltype
#define c_whlcond  c_content.whilecontent.whlcond
#define c_whlcmds  c_content.whilecontent.whlcmds
#define c_casword  c_content.casecontent.casword
#define c_casitems c_content.casecontent.casitems
#define c_funcname c_content.funcdef.funcname
#define c_funcbody c_content.funcdef.funcbody
/* c_words, c_forwords は void * にキャストした wordunit_T * の
 * NULL 終端配列へのポインタ */
/* c_forwords が NULL のとき、for に in 節はない。
 * c_forwords[0] が NULL のとき、for の in 節は空。 */

/* if コマンドの条件とその条件が成り立つとき実行するコマンドを表す */
typedef struct ifcommand_T {
    struct ifcommand_T *next;
    struct and_or_T    *ic_condition;  /* 条件 */
    struct and_or_T    *ic_commands;   /* 実行するコマンド */
} ifcommand_T;
/* else は next と ic_condition が NULL の ifcommand_T で表す。 */

/* case コマンドの一つの項目を表す */
typedef struct caseitem_T {
    struct caseitem_T *next;
    void             **ci_patterns;  /* 一致するか調べるパタンの配列 */
    struct and_or_T   *ci_commands;  /* 一致したときに実行するコマンド */
} caseitem_T;
/* ci_patterns は void * にキャストした wordunit_T * の
 * NULL 終端配列へのポインタ */

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
/* 数式展開では、$(( )) 内を数式として解釈する前にパラメータ展開を行う。
 * よって、wu_arith の型は wordunit_T * である。 */

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
    PT_NEST         = 1 << 9,  /* パラメータ展開の入れ子 */
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
 * PT_NONE でのみ有効。(${#n})
 * ${${VAR#foo}%bar} のような入れ子では、PT_NEST フラグが立つ。
 * PT_SUBST, PT_NEST は POSIX 規格にはない。 */

/* パラメータ展開を表す */
typedef struct paramexp_T {
    paramexptype_T pe_type;
    union {
	char              *name;
	struct wordunit_T *nest;
    } pe_value;
    struct wordunit_T *pe_match, *pe_subst;
} paramexp_T;
#define pe_name pe_value.name
#define pe_nest pe_value.nest
/* pe_name は変数名。
 * pe_nest は変数名の代わりに与えられる入れ子の展開要素。
 * pe_match は変数の内容とマッチさせる単語で、PT_MATCH, PT_SUBST で使う。
 * pe_subst は変数の内容を置換する単語で、PT_NONE, PT_MATCH 以外で使う。 */
/* pe_match, pe_subst が NULL のとき、それは空文字列を表す。 */

/* 代入を表す */
typedef struct assign_T {
    struct assign_T *next;
    char *name;                /* 代入する変数名 */
    struct wordunit_T *value;  /* 代入する値 */
} assign_T;
/* value が NULL のとき、それは空文字列を表す。 */

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
 * word_unit になる。いずれにしても rd_herecontent は esc が true の
 * expand_string で展開する。 */


/********** 構文解析ルーチンへのインタフェース宣言 **********/

struct xwcsbuf_T;

/* 入力を読み取ってバッファに追加する関数。
 * 入力は基本的に行単位で、入力元の現在位置から次の改行文字までを読み込む。
 * 読み込んだ改行文字もバッファに追加する。ただし改行がないまま入力の最後に
 * 到達すると改行文字はもちろんバッファに追加しない。
 * 一度に (改行を越えて) 複数行を読み込むことはない。
 * inputinfo は、read_and_parse 関数の第 2 引数で与えられたポインタである。
 * エラーのときは、エラーメッセージを出して EOF を返す。
 * 戻り値: 0:   何らかの入力があった。
 *         1:   対話モードで SIGINT を受けた。
 *         EOF: EOF に達したか、エラーがあった。
 * 戻り値が 0 でなければ、buf の内容は変化してはならない。
 * この関数は非 0 を返した後もさらに呼ばれることがある。 */
typedef int inputfunc_T(struct xwcsbuf_T *buf, void *inputinfo);

typedef struct parseinfo_T {
    bool print_errmsg;    /* エラーメッセージを出力するかどうか */
    const char *filename; /* エラー表示で使うファイル名。NULL でも良い。 */
    unsigned long lineno; /* 行番号。最初は 1 にしておく。 */
    inputfunc_T *input;   /* 入力関数 */
    void *inputinfo;      /* 入力関数に渡す情報 */
    bool intrinput;       /* 入力が対話的かどうか */
    bool inputisatty;     /* 入力が対話的端末かどうか */
    int lastinputresult;  /* 最後の input の戻り値。最初は 0 にしておく。 */
} parseinfo_T;
/* intrinput が true ならば、input は input_readline で inputinfo は
 * struct input_readline_info へのポインタである。
 * intrinput が true でも入力元が本当に端末であるとは限らない。 */
/* intrinput が true で、かつ入力元に関して isatty も true なら inputisatty
 * が true になる。 */

extern int read_and_parse(
	parseinfo_T *restrict info, and_or_T **restrict result)
    __attribute__((nonnull));


/********** 構文木を文字列に戻すルーチン **********/

extern wchar_t *pipelines_to_wcs(const pipeline_T *pipelines)
    __attribute__((malloc,warn_unused_result));
extern wchar_t *command_to_wcs(const command_T *command)
    __attribute__((malloc,warn_unused_result));


/********** 構文木データを複製・解放するルーチン **********/

extern void andorsfree(and_or_T *a);
static inline command_T *comsdup(command_T *c);
extern void comsfree(command_T *c);


/* 指定したコマンドを (仮想的に) 複製する。 */
command_T *comsdup(command_T *c)
{
    c->refcount++;
    return c;
}


#endif /* PARSER_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
