/* Yash: yet another shell */
/* parser.c: syntax parser */
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
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "option.h"
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "lineinput.h"
#include "parser.h"
#include "expand.h"


/********** 解析した構文木データを解放するルーチン **********/

static void pipesfree(pipeline_T *p);
static void ifcmdsfree(ifcommand_T *i);
static void caseitemsfree(caseitem_T *i);
static void wordfree_vp(void *w);
static void paramfree(paramexp_T *p);
static void assignsfree(assign_T *a);
static void redirsfree(redir_T *r);

void andorsfree(and_or_T *a)
{
    while (a) {
	pipesfree(a->ao_pipelines);

	and_or_T *next = a->next;
	free(a);
	a = next;
    }
}

void pipesfree(pipeline_T *p)
{
    while (p) {
	comsfree(p->pl_commands);

	pipeline_T *next = p->next;
	free(p);
	p = next;
    }
}

void comsfree(command_T *c)
{
    while (c && --(c->refcount) == 0) {
	redirsfree(c->c_redirs);
	switch (c->c_type) {
	    case CT_SIMPLE:
		assignsfree(c->c_assigns);
		recfree(c->c_words, wordfree_vp);
		break;
	    case CT_GROUP:
	    case CT_SUBSHELL:
		andorsfree(c->c_subcmds);
		break;
	    case CT_IF:
		ifcmdsfree(c->c_ifcmds);
		break;
	    case CT_FOR:
		free(c->c_forname);
		recfree(c->c_forwords, wordfree_vp);
		andorsfree(c->c_forcmds);
		break;
	    case CT_WHILE:
		andorsfree(c->c_whlcond);
		andorsfree(c->c_whlcmds);
		break;
	    case CT_CASE:
		wordfree(c->c_casword);
		caseitemsfree(c->c_casitems);
		break;
	    case CT_FUNCDEF:
		free(c->c_funcname);
		comsfree(c->c_funcbody);
		break;
	}

	command_T *next = c->next;
	free(c);
	c = next;
    }
}

void ifcmdsfree(ifcommand_T *i)
{
    while (i) {
	andorsfree(i->ic_condition);
	andorsfree(i->ic_commands);

	ifcommand_T *next = i->next;
	free(i);
	i = next;
    }
}

void caseitemsfree(caseitem_T *i)
{
    while (i) {
	recfree(i->ci_patterns, wordfree_vp);
	andorsfree(i->ci_commands);

	caseitem_T *next = i->next;
	free(i);
	i = next;
    }
}

void wordfree(wordunit_T *w)
{
    while (w) {
	switch (w->wu_type) {
	    case WT_STRING:
		free(w->wu_string);
		break;
	    case WT_PARAM:
		paramfree(w->wu_param);
		break;
	    case WT_CMDSUB:
		free(w->wu_cmdsub);
		break;
	    case WT_ARITH:
		wordfree(w->wu_arith);
		break;
	}

	wordunit_T *next = w->next;
	free(w);
	w = next;
    }
}

void wordfree_vp(void *w)
{
    wordfree((wordunit_T *) w);
}

void paramfree(paramexp_T *p)
{
    if (p) {
	if (p->pe_type & PT_NEST)
	    wordfree(p->pe_nest);
	else
	    free(p->pe_name);
	wordfree(p->pe_match);
	wordfree(p->pe_subst);
	free(p);
    }
}

void assignsfree(assign_T *a)
{
    while (a) {
	free(a->name);
	wordfree(a->value);

	assign_T *next = a->next;
	free(a);
	a = next;
    }
}

void redirsfree(redir_T *r)
{
    while (r) {
	switch (r->rd_type) {
	    case RT_INPUT:  case RT_OUTPUT:  case RT_CLOBBER:  case RT_APPEND:
	    case RT_INOUT:  case RT_DUPIN:   case RT_DUPOUT:
		wordfree(r->rd_filename);
		break;
	    case RT_HERE:  case RT_HERERT:
		free(r->rd_hereend);
		wordfree(r->rd_herecontent);
		break;
	}

	redir_T *next = r->next;
	free(r);
	r = next;
    }
}


/********** 構文解析に関する補助ルーチン **********/

static inline bool is_name_char(wchar_t c)
    __attribute__((const));
static wchar_t *skip_name(const wchar_t *s)
    __attribute__((pure,nonnull));


/* 引数が名前構成文字であるかどうか調べる */
bool is_name_char(wchar_t c)
{
    switch (c) {
    case L'0':  case L'1':  case L'2':  case L'3':  case L'4':
    case L'5':  case L'6':  case L'7':  case L'8':  case L'9':
    case L'a':  case L'b':  case L'c':  case L'd':  case L'e':  case L'f':
    case L'g':  case L'h':  case L'i':  case L'j':  case L'k':  case L'l':
    case L'm':  case L'n':  case L'o':  case L'p':  case L'q':  case L'r':
    case L's':  case L't':  case L'u':  case L'v':  case L'w':  case L'x':
    case L'y':  case L'z':
    case L'A':  case L'B':  case L'C':  case L'D':  case L'E':  case L'F':
    case L'G':  case L'H':  case L'I':  case L'J':  case L'K':  case L'L':
    case L'M':  case L'N':  case L'O':  case L'P':  case L'Q':  case L'R':
    case L'S':  case L'T':  case L'U':  case L'V':  case L'W':  case L'X':
    case L'Y':  case L'Z':  case L'_':
	return true;
    default:
	return false;
    }
}

/* s の先頭にある、識別子として正しい部分を飛ばして、残りの部分へのポインタを
 * 返す。 */
wchar_t *skip_name(const wchar_t *s)
{
    if (!iswdigit(*s))
	while (is_name_char(*s))
	    s++;
    return (wchar_t *) s;
}


/********** 構文解析ルーチン **********/

/* 解析を中断する際に途中経過を保存するための構造体 */
struct parsestate_T {
    parseinfo_T *cinfo;
    bool cerror;
    struct xwcsbuf_T cbuf;
    size_t cindex;
    struct plist_T pending_heredocs;
};

typedef enum { noalias, globalonly, anyalias, } aliastype_T;

static void serror(const char *restrict format, ...)
    __attribute__((nonnull(1),format(printf,1,2)));
static inline int read_more_input(void);
static inline void line_continuation(size_t index);
static void ensure_buffer(size_t n);
static void skip_blanks_and_comment(void);
static bool skip_to_next_token(void);
static void next_line(void);
static bool is_token_delimiter_char(wchar_t c)
    __attribute__((pure));
static bool is_command_delimiter_char(wchar_t c)
    __attribute__((const));
static bool is_slash_or_closing_brace(wchar_t c)
    __attribute__((const));
static bool is_closing_brace(wchar_t c)
    __attribute__((const));
static bool is_token_at(const wchar_t *token, size_t index)
    __attribute__((pure,nonnull));
static const wchar_t *check_opening_token_at(size_t index);
static const wchar_t *check_closing_token_at(size_t index);
static and_or_T *parse_command_list(void)
    __attribute__((malloc,warn_unused_result));
static and_or_T *parse_compound_list(void)
    __attribute__((malloc,warn_unused_result));
static and_or_T *parse_and_or_list(void)
    __attribute__((malloc,warn_unused_result));
static pipeline_T *parse_pipelines_in_and_or(void)
    __attribute__((malloc,warn_unused_result));
static pipeline_T *parse_pipeline(void)
    __attribute__((malloc,warn_unused_result));
static command_T *parse_commands_in_pipeline(void)
    __attribute__((malloc,warn_unused_result));
static command_T *parse_command(void)
    __attribute__((malloc,warn_unused_result));
static redir_T **parse_assignments_and_redirects(command_T *c)
    __attribute__((malloc,warn_unused_result,nonnull));
static void **parse_words_and_redirects(redir_T **redirlastp, bool first)
    __attribute__((malloc,warn_unused_result,nonnull));
static void parse_redirect_list(redir_T **lastp)
    __attribute__((nonnull));
static assign_T *tryparse_assignment(void)
    __attribute__((malloc,warn_unused_result));
static redir_T *tryparse_redirect(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *parse_word_to(aliastype_T type, bool testfunc(wchar_t c))
    __attribute__((malloc,warn_unused_result,nonnull));
static inline wordunit_T *parse_word(aliastype_T type)
    __attribute__((malloc,warn_unused_result));
static void skip_to_next_single_quote(void);
static wordunit_T *parse_special_word_unit(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *tryparse_paramexp_raw(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *parse_paramexp_in_brase(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *parse_cmdsubst_in_paren(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *parse_cmdsubst_in_backquote(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *tryparse_arith(void)
    __attribute__((malloc,warn_unused_result));
static wchar_t *parse_word_as_wcs(void)
    __attribute__((malloc,warn_unused_result));
static command_T *parse_compound_command(const wchar_t *command)
    __attribute__((malloc,warn_unused_result,nonnull));
static command_T *parse_group(commandtype_T type)
    __attribute__((malloc,warn_unused_result));
static command_T *parse_if(void)
    __attribute__((malloc,warn_unused_result));
static command_T *parse_for(void)
    __attribute__((malloc,warn_unused_result));
static command_T *parse_while(bool whltype)
    __attribute__((malloc,warn_unused_result));
static command_T *parse_case(void)
    __attribute__((malloc,warn_unused_result));
static caseitem_T *parse_case_list(void)
    __attribute__((malloc,warn_unused_result));
static void **parse_case_patterns(void)
    __attribute__((malloc,warn_unused_result));
static command_T *tryparse_function(void)
    __attribute__((malloc,warn_unused_result));
static void read_heredoc_contents(redir_T *redir);
static void read_heredoc_contents_without_expand(redir_T *r);
static void read_heredoc_contents_with_expand(redir_T *r);
static bool is_end_of_heredoc_contents(
	const wchar_t *eoc, size_t eoclen, bool skiptab)
    __attribute__((nonnull));
static wordunit_T *parse_string_to(bool backquote, bool stoponnewline)
    __attribute__((malloc,warn_unused_result));
static const char *get_errmsg_unexpected_token(const wchar_t *token)
    __attribute__((nonnull));
static void print_errmsg_token_missing(const wchar_t *token, size_t index)
    __attribute__((nonnull));

#define QUOTES L"\"'\\"


/* 解析中のソースのデータ */
static parseinfo_T *cinfo;
/* 現在の解析でエラーが発生しているなら true */
static bool cerror;
/* 解析するソースを取り込むバッファ */
static xwcsbuf_T cbuf;
/* 現在解析している箇所の cbuf におけるインデックス */
static size_t cindex;
/* 読み込みを控えているヒアドキュメントのリスト */
static plist_T pending_heredocs;


/* 解析の途中経過を構造体に保存して返す。
 * 本来 read_and_parse や parse_string は再入できないが、この関数で経過を保存
 * すれば再入できる。この関数を呼んだ後は必ず restore_parse_state で経過を
 * 元に戻すこと。 */
struct parsestate_T *save_parse_state(void)
{
    struct parsestate_T *result = xmalloc(sizeof *result);
    *result = (struct parsestate_T) {
	.cinfo = cinfo,
	.cerror = cerror,
	.cbuf = cbuf,
	.cindex = cindex,
	.pending_heredocs = pending_heredocs,
    };
    return result;
}

/* 指定した解析の途中経過を復元する。
 * *state はこの関数内で解放する。 */
void restore_parse_state(struct parsestate_T *state)
{
    cinfo = state->cinfo;
    cerror = state->cerror;
    cbuf = state->cbuf;
    cindex = state->cindex;
    pending_heredocs = state->pending_heredocs;
    free(state);
}


/* 以下の解析関数は、エラーが発生しても NULL を返すとは限らない。
 * エラーが発生したかどうかは cerror によって判断する。cerror は serror を
 * 呼び出すと true になる。 */
/* parse_ で始まる各関数は、自分が解析すべき部分の後の次のトークンの最初の文字
 * まで cindex を進めてから返る。(入力が EOF でない限り) このとき
 * cbuf.contents[cindex] は非 null である。 */


/* 少なくとも一行の入力を読み取り、解析する。
 * info: 解析情報へのポインタ。全ての値を初期化しておくこと。
 * result: 成功したら *result に解析結果が入る。
 *         コマンドがなければ *result は NULL になる。
 * 戻り値: 成功したら *result に結果を入れて 0 を返す。
 *         構文エラーならエラーメッセージを出して 1 を返す。
 *         入力が最後に達したら EOF を返す。
 *         入力エラーならエラーメッセージを出して EOF を返す。
 * この関数は原則として再入不可能である。 */
int read_and_parse(parseinfo_T *restrict info, and_or_T **restrict result)
{
    cinfo = info;
    cerror = false;
    cindex = 0;
    wb_init(&cbuf);

    if (info->intrinput)
	((struct input_readline_info *) info->inputinfo)->type = 1;

    cinfo->lastinputresult = 0;
    read_more_input();
    if (cinfo->lastinputresult == EOF) {
	wb_destroy(&cbuf);
	return EOF;
    } else if (cinfo->lastinputresult == 1) {
	wb_destroy(&cbuf);
	*result = NULL;
	return 0;
    }
    pl_init(&pending_heredocs);

    and_or_T *r = parse_command_list();

    wb_destroy(&cbuf);
    pl_destroy(&pending_heredocs);

    if (cinfo->lastinputresult == 1) {
	andorsfree(r);
	*result = NULL;
	return 0;
    } else if (cerror) {
	andorsfree(r);
	return 1;
    } else {
	assert(cindex == cbuf.length);
	*result = r;
	return 0;
    }
}

/* 構文エラーメッセージを stderr に出力する。
 * format はこの関数内で gettext に通す。format の最後に改行は不要。 */
void serror(const char *restrict format, ...)
{
    va_list ap;

    if (cinfo->print_errmsg) {
	/* TRANSLATORS: "%s:%lu:" at the beginning should be left intact
	 * because many tools can recognize this "filename:lineno:" format. */
	fprintf(stderr, gt("%s:%lu: syntax error: "),
		cinfo->filename ? cinfo->filename
				: yash_program_invocation_name,
		cinfo->lineno);
	va_start(ap, format);
	vfprintf(stderr, gt(format), ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
    }
    cerror = true;
}

/* 更なる入力を読み込む
 * 戻り値: 0:   何らかの入力があった。
 *         1:   対話モードで SIGINT を受けた。
 *         EOF: EOF に達したか、エラーがあった。
 * 対話的端末からの入力時、cerror が true なら 1 を返す。 */
int read_more_input(void)
{
    if (cerror && cinfo->intrinput)
	return 1;
    if (cinfo->lastinputresult == 0) {
	size_t savelength = cbuf.length;
	cinfo->lastinputresult = cinfo->input(&cbuf, cinfo->inputinfo);
	if (cinfo->enable_verbose && shopt_verbose)
	    fprintf(stderr, "%ls", cbuf.contents + savelength);
    }
    return cinfo->lastinputresult;
}

/* 指定したインデックスにある行連結を削除し、行番号を更新し、次行を読み込む。 */
void line_continuation(size_t index)
{
    assert(cbuf.contents[index] == L'\\' && cbuf.contents[index + 1] == L'\n');
    wb_remove(&cbuf, index, 2);
    cinfo->lineno++;
    read_more_input();
}

/* 現在位置がナル文字なら次行を読み込む。
 * 現在位置から n 文字以内に行連結があれば次行を読み込んで行連結を行う。
 * ただし、n 文字以内に他の引用符があればそこで処理を止める。 */
/* n はできるだけ小さくすること。 */
void ensure_buffer(size_t n)
{
    size_t index = cindex;
    if (cbuf.contents[cindex] == L'\0')
	read_more_input();
    while (index - cindex < n) {
	switch (cbuf.contents[index]) {
	case L'\0':  case L'\'':  case L'"':
	    return;
	case L'\\':
	    if (cbuf.contents[index + 1] != L'\n')
		return;
	    assert(cbuf.contents[index + 2] == L'\0');
	    line_continuation(index);
	    if (cinfo->lastinputresult != 0)
		return;
	    /* falls thru! */
	default:
	    index++;
	    break;
	}
    }
}

/* cindex を増やして blank・コメント・行連結を飛ばす。必要に応じて
 * read_more_input する。cindex は次の非 blank 文字を指す。コメントを飛ばした
 * 場合はコメントの直後の文字 (普通は改行文字) を指す。行連結は、飛ばすと
 * いうよりは "\\\n" をバッファから削除する。 */
void skip_blanks_and_comment(void)
{
    if (cbuf.contents[cindex] == L'\0')
	if (read_more_input() != 0)
	    return;

start:
    /* blank を飛ばす */
    while (iswblank(cbuf.contents[cindex]))
	cindex++;

    /* コメントを飛ばす */
    if (cbuf.contents[cindex] == L'#') {
	do {
	    cindex++;
	} while (cbuf.contents[cindex] != L'\n'
		&& cbuf.contents[cindex] != L'\0');
    }

    /* 行連結を削除する */
    if (cbuf.contents[cindex] == L'\\' && cbuf.contents[cindex + 1] == L'\n') {
	line_continuation(cindex);
	goto start;
    }
}

/* 次のトークンに達するまで cindex を増やして blank 文字・改行文字・コメントを
 * 飛ばす。必要に応じて read_more_input する。
 * 戻り値: 一つ以上の NEWLINE トークンを飛ばしたかどうか */
bool skip_to_next_token(void)
{
    bool newline = false;

    skip_blanks_and_comment();
    while (cinfo->lastinputresult == 0 && cbuf.contents[cindex] == L'\n') {
	newline = true;
	next_line();
	skip_blanks_and_comment();
    }
    return newline;
}

/* 現在位置が改行文字のとき、NEWLINE トークンとして解釈して次の行に進む。
 * ヒアドキュメントの読み込みを控えているならそれも行う。 */
void next_line(void)
{
    assert(cbuf.contents[cindex] == L'\n');
    cindex++;
    cinfo->lineno++;
    assert(cbuf.contents[cindex] == L'\0');

    for (size_t i = 0; i < pending_heredocs.length; i++)
	read_heredoc_contents(pending_heredocs.contents[i]);
    pl_clear(&pending_heredocs);
}

/* 指定した文字がトークン区切り文字かどうかを調べる */
bool is_token_delimiter_char(wchar_t c)
{
    return iswblank(c) || c == L'\0' || c == L'\n'
	|| c == L';' || c == L'&' || c == L'|'
	|| c == L'<' || c == L'>' || c == L'(' || c == L')';
}

/* 指定した文字が単純コマンドを区切る文字かどうかを調べる */
bool is_command_delimiter_char(wchar_t c)
{
    return c == L'\0' || c == L'\n'
	|| c == L';' || c == L'&' || c == L'|' || c == L'(' || c == L')';
}

bool is_slash_or_closing_brace(wchar_t c)
{
    return c == L'/' || c == L'}' || c == L'\0';
}

bool is_closing_brace(wchar_t c)
{
    return c == L'}' || c == L'\0';
}

/* cbuf の指定したインデックスにトークン token があるか調べる。
 * token: 調べるトークン
 * index: cbuf へのインデックス (cbuf.length 以下) */
/* token には、他の演算子トークンの真の部分文字列であるような文字列を
 * 指定してはならない。 */
/* 予め ensure_buffer(wcslen(token)) を行っておくこと。 */
bool is_token_at(const wchar_t *token, size_t index)
{
    wchar_t *c = matchwcsprefix(cbuf.contents + index, token);
    return c && is_token_delimiter_char(*c);
}

/* cbuf の指定したインデックスに ( や { や if などの「開く」トークンがあるか
 * 調べる。あれば、そのトークンを示す文字列定数を返す。なければ NULL を返す。 */
const wchar_t *check_opening_token_at(size_t index)
{
    if (true || index + 6 >= cindex)
	ensure_buffer(6);
    if (cbuf.contents[index] == L'(') return L"(";
    if (is_token_at(L"{",     index)) return L"{";
    if (is_token_at(L"if",    index)) return L"if";
    if (is_token_at(L"for",   index)) return L"for";
    if (is_token_at(L"while", index)) return L"while";
    if (is_token_at(L"until", index)) return L"until";
    if (is_token_at(L"case",  index)) return L"case";
    return NULL;
}

/* cbuf の指定したインデックスに ) や } や fi などの「閉じる」トークンがあるか
 * 調べる。あれば、そのトークンを示す文字列定数を返す。なければ NULL を返す。
 * 「閉じる」トークンというのは、and/or リストを終わらせる印になるものである。*/
const wchar_t *check_closing_token_at(size_t index)
{
    ensure_buffer(5);
    if (cbuf.contents[index] == L')') return L")";
    if (cbuf.contents[index] == L';' && cbuf.contents[index + 1] == L';')
				      return L";;";
    if (is_token_at(L"}",    index))  return L"}";
    if (is_token_at(L"then", index))  return L"then";
    if (is_token_at(L"else", index))  return L"else";
    if (is_token_at(L"elif", index))  return L"elif";
    if (is_token_at(L"fi",   index))  return L"fi";
    if (is_token_at(L"do",   index))  return L"do";
    if (is_token_at(L"done", index))  return L"done";
    if (is_token_at(L"esac", index))  return L"esac";
    return NULL;
}

/* 区切りの良いところまで解析を進める。すなわち、少くとも一行の入力を読み取り、
 * 複合コマンドなどの内部でない改行の直後まで解析する。
 * この関数を呼ぶ前に skip_blanks_and_comment をする必要はない。 */
and_or_T *parse_command_list(void)
{
    and_or_T *first = NULL, **lastp = &first;
    bool separator = true;
    /* 二つ目以降のトークンを解析するにはセパレータ ('&', ';', または一つ以上の
     * 改行) が必要となる。 */

    while (!cerror) {
	skip_blanks_and_comment();
	if (cbuf.contents[cindex] == L'\0') {
	    break;
	} else if (cbuf.contents[cindex] == L'\n') {
	    next_line();
	    break;
	} else if (!separator) {
	    serror(Ngt("`;' or `&' missing"));
	    break;
	}

	and_or_T *ao = parse_and_or_list();
	if (ao) {
	    *lastp = ao;
	    lastp = &ao->next;
	}

	separator = false;
	ensure_buffer(2);
	if (cbuf.contents[cindex] == L'&'
		|| (cbuf.contents[cindex] == L';'
		    && cbuf.contents[cindex + 1] != L';')) {
	    cindex++;
	    separator = true;
	}
    }
    return first;
}

/* 「閉じる」トークンが現れるまでコマンドを解析する。
 * この関数を呼ぶ前に skip_to_next_token をする必要はない。 */
and_or_T *parse_compound_list(void)
{
    and_or_T *first = NULL, **lastp = &first;
    bool savecerror = cerror;
    bool separator = true;
    /* 二つ目以降のトークンを解析するにはセパレータ ('&', ';', または一つ以上の
     * 改行) が必要となる。 */

    if (!cinfo->intrinput)
	cerror = false;
    while (!cerror) {
	separator |= skip_to_next_token();
	if (!separator
		|| cbuf.contents[cindex] == L'\0'
		|| check_closing_token_at(cindex))
	    break;

	and_or_T *ao = parse_and_or_list();
	if (ao) {
	    *lastp = ao;
	    lastp = &ao->next;
	}

	separator = false;
	ensure_buffer(2);
	if (cbuf.contents[cindex] == L'&'
		|| (cbuf.contents[cindex] == L';'
		    && cbuf.contents[cindex + 1] != L';')) {
	    cindex++;
	    separator = true;
	}
    }
    cerror |= savecerror;
    return first;
}

/* 一つの and/or リストを解析する。
 * 末尾の '&' または ';' は解析結果には反映するが、cindex は '&'/';' の後では
 * なく '&'/';' そのもののインデックスを指した状態で返る。 */
and_or_T *parse_and_or_list(void)
{
    and_or_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->ao_pipelines = parse_pipelines_in_and_or();
    result->ao_async = (cbuf.contents[cindex] == L'&');
    return result;
}

/* and/or リスト内の全てのパイプラインを解析する。 */
pipeline_T *parse_pipelines_in_and_or(void)
{
    pipeline_T *first = NULL, **lastp = &first;
    bool cond = cond;  /* GCC の警告を黙らせるために自己代入する */

    for (;;) {
	pipeline_T *p = parse_pipeline();
	if (p) {
	    p->pl_cond = cond;
	    *lastp = p;
	    lastp = &p->next;
	}

	ensure_buffer(2);
	if (cbuf.contents[cindex] == L'&'
		&& cbuf.contents[cindex+1] == L'&') {
	    cond = true;
	} else if (cbuf.contents[cindex] == L'|'
		&& cbuf.contents[cindex+1] == L'|') {
	    cond = false;
	} else {
	    break;
	}
	cindex += 2;
	skip_to_next_token();
    }
    return first;
}

/* 一つのパイプライン ('|' で繋がった一つ以上のコマンド) を解析する。 */
pipeline_T *parse_pipeline(void)
{
    pipeline_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->pl_cond = false;

    ensure_buffer(2);
    if (is_token_at(L"!", cindex)) {
	result->pl_neg = true;
	cindex++;
	skip_blanks_and_comment();
    } else {
	result->pl_neg = false;
    }

    ensure_buffer(2);
    if (!posixly_correct && cbuf.contents[cindex    ] == L'|'
			 && cbuf.contents[cindex + 1] != L'|') {
	result->pl_loop = true;
	cindex++;
	skip_blanks_and_comment();
    } else {
	result->pl_loop = false;
    }

    result->pl_commands = parse_commands_in_pipeline();
    return result;
}

/* パイプラインの本体 (間を '|' で繋いた一つ以上のコマンド) を解析する。 */
command_T *parse_commands_in_pipeline(void)
{
    command_T *first = NULL, **lastp = &first;

    for (;;) {
	command_T *c = parse_command();
	if (c) {
	    *lastp = c;
	    lastp = &c->next;
	}

	ensure_buffer(2);
	if (cbuf.contents[cindex] == L'|' && cbuf.contents[cindex+1] != L'|') {
	    cindex++;
	    skip_to_next_token();
	} else {
	    break;
	}
    }
    return first;
}

/* 一つのコマンドを解析する。 */
command_T *parse_command(void)
{
    /* Note: check_closing_token_at は ensure_buffer(5) を含む */
    const wchar_t *t = check_closing_token_at(cindex);
    if (t) {
	serror(get_errmsg_unexpected_token(t), t);
	return NULL;
    } else if (is_token_at(L"!", cindex)) {
	serror(get_errmsg_unexpected_token(L"!"), L"!");
	return NULL;
    } else if (is_token_at(L"in", cindex)) {
	serror(get_errmsg_unexpected_token(L"in"), L"in");
	return NULL;
    } else if (cbuf.contents[cindex] == L'(') {
	return parse_compound_command(L"(");
    } else if (is_command_delimiter_char(cbuf.contents[cindex])) {
	if (cbuf.contents[cindex] == L'\0' || cbuf.contents[cindex] == L'\n')
	    serror(Ngt("command missing at end of input"));
	else
	    serror(Ngt("command missing before `%lc'"),
		    (wint_t) cbuf.contents[cindex]);
	return NULL;
    }

    t = check_opening_token_at(cindex);
    if (t)
	return parse_compound_command(t);

    unsigned long lineno = cinfo->lineno;
    command_T *result = tryparse_function();
    if (result)
	return result;

    /* 普通の単純なコマンドを解析 */
    result = xmalloc(sizeof *result);
    redir_T **redirlastp;

    result->next = NULL;
    result->refcount = 1;
    result->c_lineno = lineno;
    result->c_type = CT_SIMPLE;
    redirlastp = parse_assignments_and_redirects(result);
    result->c_words = parse_words_and_redirects(redirlastp, true);

    ensure_buffer(1);
    if (cbuf.contents[cindex] == L'(')
	serror(Ngt("invalid use of `%lc'"), (wint_t) cbuf.contents[cindex]);
    return result;
}

/* 変数代入とリダイレクトを解析する。
 * 結果を c->c_assigns と c->c_redirs に代入し、新しい redirlastp を返す。 */
redir_T **parse_assignments_and_redirects(command_T *c)
{
    assign_T **assgnlastp = &c->c_assigns;
    redir_T **redirlastp = &c->c_redirs;
    assign_T *assgn;
    redir_T *redir;

    c->c_assigns = NULL;
    c->c_redirs = NULL;
    while (ensure_buffer(1),
	    !is_command_delimiter_char(cbuf.contents[cindex])) {
	if ((redir = tryparse_redirect())) {
	    *redirlastp = redir;
	    redirlastp = &redir->next;
	} else if ((assgn = tryparse_assignment())) {
	    *assgnlastp = assgn;
	    assgnlastp = &assgn->next;
	} else {
	    break;
	}
    }
    return redirlastp;
}

/* ワードとリダイレクトを解析する。
 * リダイレクトの解析結果は *redirlastp に入る。
 * ワードの解析結果は wordunit_T * を void * に変換したものの配列への
 * ポインタとして返す。
 * *redirlastp は予め NULL を代入しておくこと。
 * first が true なら最初のワードは全ての種類のエイリアスを展開する。 */
void **parse_words_and_redirects(redir_T **redirlastp, bool first)
{
    plist_T wordlist;
    redir_T *redir;
    wordunit_T *word;

    pl_init(&wordlist);
    while (ensure_buffer(1),
	    !is_command_delimiter_char(cbuf.contents[cindex])) {
	if ((redir = tryparse_redirect())) {
	    *redirlastp = redir;
	    redirlastp = &redir->next;
	} else if ((word = parse_word(first ? anyalias : globalonly))) {
	    pl_add(&wordlist, word);
	    first = false;
	} else {
	    break;
	}
    }
    return pl_toary(&wordlist);
}

/* 0 個以上のリダイレクトを解析する。解析結果は *redirlastp に入る。
 * *redirlastp は予め NULL を代入しておくこと。 */
void parse_redirect_list(redir_T **lastp)
{
    redir_T *redir;

    while ((redir = tryparse_redirect())) {
	*lastp = redir;
	lastp = &redir->next;
    }
}

/* 現在位置に変数代入があれば、それを解析して結果を返す。
 * なければ、何もせず NULL を返す。 */
assign_T *tryparse_assignment(void)
{
    size_t namelen;

    do
	namelen = skip_name(cbuf.contents + cindex) - (cbuf.contents + cindex);
    while (cbuf.contents[cindex + namelen] == L'\0' && read_more_input() == 0);
    if (namelen == 0 || cbuf.contents[cindex + namelen] != L'=')
	return NULL;

    assign_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->name = malloc_wcsntombs(cbuf.contents + cindex, namelen);
    assert(result->name != NULL);
    cindex += namelen + 1;

    ensure_buffer(1);
    if (is_token_delimiter_char(cbuf.contents[cindex])) {
	/* '=' の後は空文字列 */
	result->value = NULL;
    } else {
	result->value = parse_word(noalias);
    }
    return result;
}

/* 現在位置にリダイレクトがあれば、それを解析して結果を返す。
 * なければ、何もせず NULL を返す。 */
redir_T *tryparse_redirect(void)
{
    int fd;

    ensure_buffer(2);
    if (iswdigit(cbuf.contents[cindex])) {
	wchar_t *endptr;

reparse:
	errno = 0;
	fd = wcstol(cbuf.contents + cindex, &endptr, 10);
	if (errno)
	    fd = -1;  /* 無効な値 */
	if (endptr[0] == L'\\' && endptr[1] == L'\n') {
	    line_continuation(endptr - cbuf.contents);
	    goto reparse;
	} else if (*endptr != L'<' && *endptr != L'>') {
	    return NULL;
	}
	cindex = endptr - cbuf.contents;
    } else if (cbuf.contents[cindex] == L'<') {
	fd = STDIN_FILENO;
    } else if (cbuf.contents[cindex] == L'>') {
	fd = STDOUT_FILENO;
    } else {
	return NULL;
    }

    redir_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->rd_fd = fd;
    ensure_buffer(3);
    assert(cbuf.contents[cindex] == L'<' || cbuf.contents[cindex] == L'>');
    if (cbuf.contents[cindex] == L'<') {
	switch (cbuf.contents[cindex + 1]) {
	case L'<':
	    if (cbuf.contents[cindex + 2] == L'-') {
		result->rd_type = RT_HERERT; cindex += 3;
	    } else {
		result->rd_type = RT_HERE;   cindex += 2;
	    }
	    break;
	case L'>':  result->rd_type = RT_INOUT;  cindex += 2;  break;
	case L'&':  result->rd_type = RT_DUPIN;  cindex += 2;  break;
	default:    result->rd_type = RT_INPUT;  cindex += 1;  break;
	}
    } else {
	switch (cbuf.contents[cindex + 1]) {
	case L'>':  result->rd_type = RT_APPEND;  cindex += 2;  break;
	case L'|':  result->rd_type = RT_CLOBBER; cindex += 2;  break;
	case L'&':  result->rd_type = RT_DUPOUT;  cindex += 2;  break;
	default:    result->rd_type = RT_OUTPUT;  cindex += 1;  break;
	}
    }
    skip_blanks_and_comment();
    if (is_command_delimiter_char(cbuf.contents[cindex])) {
	serror(Ngt("redirect target not specified"));
	free(result);
	return NULL;
    }
    if (result->rd_type != RT_HERE && result->rd_type != RT_HERERT) {
	result->rd_filename = parse_word(globalonly);
    } else {
	size_t index = cindex;
	wchar_t *endofheredoc = parse_word_as_wcs();
	assert(index != cindex);
	result->rd_hereend = endofheredoc;
	result->rd_herecontent = NULL;
	pl_add(&pending_heredocs, result);
    }
    return result;
}

wordunit_T *parse_word(aliastype_T type)
{
    return parse_word_to(type, is_token_delimiter_char);
}

/* 現在位置のエイリアスを展開し、ワードを解析する。
 * type: 展開するエイリアスの種類
 * testfunc: ワード区切り文字かどうかを判別する関数
 *       解析は、エスケープされていなくて testfunc が false を返す文字まで進む。
 *       testfunc は、L'\0' に対して true を返さねばならない。
 * ワードがなくてもエラーを出さない。 */
wordunit_T *parse_word_to(aliastype_T type, bool testfunc(wchar_t c))
{
    wordunit_T *first = NULL, **lastp = &first, *wu;
    bool indq = false;  /* 二重引用符 " の中かどうか */
    size_t startindex = cindex;

    // TODO parser.c: parse_word: エイリアス展開
    (void) type;

/* startindex から cindex の手前までの文字列を *lastp に追加する。 */
#define MAKE_WORDUNIT_STRING                                                 \
    do {                                                                     \
        if (startindex != cindex) {                                          \
            wordunit_T *w = xmalloc(sizeof *w);                              \
            w->next = NULL;                                                  \
            w->wu_type = WT_STRING;                                          \
            w->wu_string = xwcsndup(                                         \
                    cbuf.contents + startindex, cindex - startindex);        \
            *lastp = w;                                                      \
            lastp = &w->next;                                                \
        }                                                                    \
    } while (0)

    while (indq ? cbuf.contents[cindex] != L'\0'
		: !testfunc(cbuf.contents[cindex])) {

	switch (cbuf.contents[cindex]) {
	case L'\\':
	    if (cbuf.contents[cindex + 1] == L'\n') {
		line_continuation(cindex);
		continue;
	    } else if (cbuf.contents[cindex + 1] != L'\0') {
		cindex += 2;
		continue;
	    }
	    break;
	case L'\n':
	    cinfo->lineno++;
	    read_more_input();
	    break;
	case L'$':
	case L'`':
	    MAKE_WORDUNIT_STRING;
	    wu = parse_special_word_unit();
	    startindex = cindex;
	    if (wu) {
		*lastp = wu;
		lastp = &wu->next;
		continue;
	    } else if (cbuf.contents[cindex] == L'\0') {
		continue;
	    }
	    break;
	case L'\'':
	    if (!indq) {
		cindex++;
		skip_to_next_single_quote();
	    }
	    break;
	case L'"':
	    indq = !indq;
	    /* falls thru! */
	default:
	    break;
	}
	cindex++;
    }
    MAKE_WORDUNIT_STRING;

    if (indq)
	serror(Ngt("double-quote not closed"));

    skip_blanks_and_comment();
    return first;
}

/* 次の単一引用符 ' が現れるまで cindex を増やして飛ばす。
 * cindex は見付かった ' を指した状態で返る。
 * よって cindex が最初から ' を指している場合は何もしない。
 * ' が見付からずに EOF に達したらエラーを出す。 */
void skip_to_next_single_quote(void)
{
    for (;;) {
	switch (cbuf.contents[cindex]) {
	case L'\'':
	    return;
	case L'\0':
	    if (read_more_input() != 0) {
		serror(Ngt("single-quote not closed"));
		return;
	    }
	    break;
	case L'\n':
	    cinfo->lineno++;
	    /* falls thru! */
	default:
	    break;
	}
	cindex++;
    }
}

/* '$' または '`' で始まるパラメータ展開やコマンド置換を解析する。
 * cindex は '$' か '`' を指した状態で呼び出され、解析した部分の直後の文字を
 * 指した状態で返る。'$' の後にパラメータ展開などがなければ、cindex は最初の
 * まま返る。さもなくば、cindex は少なくとも 1 以上増える。 */
wordunit_T *parse_special_word_unit(void)
{
    switch (cbuf.contents[cindex++]) {
    case L'$':
	ensure_buffer(2);
	switch (cbuf.contents[cindex]) {
	case L'{':
	    return parse_paramexp_in_brase();
	case L'(':
	    if (cbuf.contents[cindex + 1] == L'(') {
		wordunit_T *wu = tryparse_arith();
		if (wu)
		    return wu;
	    }
	    return parse_cmdsubst_in_paren();
	default:
	    return tryparse_paramexp_raw();
	}
    case L'`':
	return parse_cmdsubst_in_backquote();
    default:
	assert(false);
    }
}

/* { } で囲んでいないパラメータを解析する。
 * cindex は '$' の直後の文字を指した状態で呼ばれ、変数名部分の直後の文字を
 * 指した状態で返る。正しいパラメータがなければ、cindex を元より 1 少なくして
 * NULL を返す。 */
wordunit_T *tryparse_paramexp_raw(void)
{
    paramexp_T *pe;
    size_t namelen;  /* 変数名の長さ */

    ensure_buffer(1);
    switch (cbuf.contents[cindex]) {
	case L'@':  case L'*':  case L'#':  case L'?':
	case L'-':  case L'$':  case L'!':
	    namelen = 1;
	    goto success;
    }
    if (!is_name_char(cbuf.contents[cindex]))
	goto fail;

findnamelen:
    namelen = skip_name(cbuf.contents + cindex) - (cbuf.contents + cindex);
    if (cbuf.contents[cindex + namelen] == L'\\' &&
	    cbuf.contents[cindex + namelen + 1] == L'\n') {
	line_continuation(cindex + namelen);
	goto findnamelen;
    }
    if (namelen == 0) {
	assert(L'0' <= cbuf.contents[cindex] && cbuf.contents[cindex] <= L'9');
	namelen++;
    }

success:
    pe = xmalloc(sizeof *pe);
    pe->pe_type = PT_NONE;
    pe->pe_name = malloc_wcsntombs(cbuf.contents + cindex, namelen);
    assert(pe->pe_name != NULL);
    pe->pe_match = pe->pe_subst = NULL;

    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_PARAM;
    result->wu_param = pe;
    cindex += namelen;
    return result;

fail:
    cindex--;
    return NULL;
}

/* "${" で始まるパラメータ展開を解析する。
 * cindex は '{' を指した状態で呼ばれ、対応する '}' の直後の文字を
 * 指した状態で返る。 */
wordunit_T *parse_paramexp_in_brase(void)
{
    paramexp_T *pe = xmalloc(sizeof *pe);
    pe->pe_type = 0;
    pe->pe_name = NULL;
    pe->pe_match = pe->pe_subst = NULL;

    assert(cbuf.contents[cindex] == L'{');
    cindex++;

    /* PT_NUMBER を解析 */
    ensure_buffer(3);
    if (cbuf.contents[cindex] == L'#'
	    && cbuf.contents[cindex + 1] != L'}'
	    && (cbuf.contents[cindex + 1] != L'#'
		|| (cbuf.contents[cindex + 1] != L'\0'
		    && cbuf.contents[cindex + 2] == L'}'))) {
	pe->pe_type |= PT_NUMBER;
	cindex++;
    }

    /* 入れ子のパラメータ展開を解析 */
    /* ensure_buffer(2); */
    if (!posixly_correct && cbuf.contents[cindex] == L'{') {
	pe->pe_type |= PT_NEST;
	pe->pe_nest = parse_paramexp_in_brase();
    } else if (!posixly_correct &&
	    (cbuf.contents[cindex] == L'$' || cbuf.contents[cindex] == L'`')) {
	size_t neststartindex = cindex;
	pe->pe_nest = parse_special_word_unit();
	if (cindex != neststartindex)
	    pe->pe_type |= PT_NEST;
	else
	    goto parse_name;
    } else {
parse_name:;
	/* 入れ子でなければ、普通に変数名を取り出す */
	size_t namestartindex = cindex;
	switch (cbuf.contents[cindex]) {
	    case L'@':  case L'*':  case L'#':  case L'?':
	    case L'-':  case L'$':  case L'!':
		cindex++;
		goto make_name;
	}
	while (ensure_buffer(1), is_name_char(cbuf.contents[cindex]))
	    cindex++;
	if (namestartindex == cindex) {
	    serror(Ngt("parameter name missing or invalid"));
	    goto fail;
	}
make_name:
	pe->pe_name = malloc_wcsntombs(
		cbuf.contents + namestartindex, cindex - namestartindex);
	assert(pe->pe_name != NULL);
    }

    /* PT_COLON を解析 */
    ensure_buffer(3);
    if (cbuf.contents[cindex] == L':') {
	pe->pe_type |= PT_COLON;
	cindex++;
    }

    /* '-' とか '+' とか '#' とかを解析 */
    switch (cbuf.contents[cindex]) {
    case L'-':   pe->pe_type |= PT_MINUS;                    goto parse_subst;
    case L'+':   pe->pe_type |= PT_PLUS;                     goto parse_subst;
    case L'=':   pe->pe_type |= PT_ASSIGN;                   goto parse_subst;
    case L'?':   pe->pe_type |= PT_ERROR;                    goto parse_subst;
    case L'#':   pe->pe_type |= PT_MATCH | PT_MATCHHEAD;     goto parse_match;
    case L'%':   pe->pe_type |= PT_MATCH | PT_MATCHTAIL;     goto parse_match;
    case L'/':   pe->pe_type |= PT_SUBST | PT_MATCHLONGEST;  goto parse_match;
    case L'}':
	pe->pe_type |= PT_NONE;
	if (pe->pe_type & PT_COLON)
	    serror(Ngt("invalid use of `:' in parameter expansion"));
	goto check_closing_paren_and_finish;
    case L'\0':  case L'\n':
	serror(Ngt("`%ls' missing"), L"}");
	goto fail;
    default:
	serror(Ngt("invalid character `%lc' in parameter expansion"),
		(wint_t) cbuf.contents[cindex]);
	goto fail;
    }

parse_match:
    if (pe->pe_type & PT_COLON) {
	if ((pe->pe_type & PT_MASK) == PT_SUBST)
	    pe->pe_type |= PT_MATCHHEAD | PT_MATCHTAIL;
	else
	    serror(Ngt("invalid use of `:' in parameter expansion"));
	cindex += 1;
    } else if (cbuf.contents[cindex] == cbuf.contents[cindex + 1]) {
	if ((pe->pe_type & PT_MASK) == PT_MATCH)
	    pe->pe_type |= PT_MATCHLONGEST;
	else
	    pe->pe_type |= PT_SUBSTALL;
	cindex += 2;
    } else if (cbuf.contents[cindex] == L'/') {
	if (cbuf.contents[cindex + 1] == L'#') {
	    pe->pe_type |= PT_MATCHHEAD;
	    cindex += 2;
	} else if (cbuf.contents[cindex + 1] == L'%') {
	    pe->pe_type |= PT_MATCHTAIL;
	    cindex += 2;
	} else {
	    cindex += 1;
	}
    } else {
	cindex += 1;
    }
    if ((pe->pe_type & PT_MASK) == PT_MATCH) {
	pe->pe_match = parse_word_to(noalias, is_closing_brace);
	goto check_closing_paren_and_finish;
    } else {
	pe->pe_match = parse_word_to(noalias, is_slash_or_closing_brace);
    }

    /* ensure_buffer(1); */
    if (cbuf.contents[cindex] != L'/')
	goto check_closing_paren_and_finish;
parse_subst:
    cindex++;
    pe->pe_subst = parse_word_to(noalias, is_closing_brace);

check_closing_paren_and_finish:
    /* ensure_buffer(1); */
    if (cbuf.contents[cindex] == L'}')
	cindex++;
    else
	serror(Ngt("`%ls' missing"), L"}");
    if ((pe->pe_type & PT_NUMBER) && (pe->pe_type & PT_MASK) != PT_NONE)
	serror(Ngt("invalid use of `#' flag in parameter expansion"));

    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_PARAM;
    result->wu_param = pe;
    return result;

fail:
    paramfree(pe);
    return NULL;
}

/* "$(" で始まるコマンド置換を解析する。
 * cindex は '(' を指した状態で呼ばれ、対応する ')' の直後の文字を指した状態で
 * 返る。 */
wordunit_T *parse_cmdsubst_in_paren(void)
{
    // TODO parser: parse_cmdsubst_in_paren: エイリアスを一時的に無効にする
    plist_T save_pending_heredocs = pending_heredocs;
    pl_init(&pending_heredocs);

    assert(cbuf.contents[cindex] == L'(');

    size_t startindex = ++cindex;
    andorsfree(parse_compound_list());
    assert(startindex <= cindex);

    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_CMDSUB;
    result->wu_cmdsub = xwcsndup(
	    cbuf.contents + startindex, cindex - startindex);

    ensure_buffer(1);
    if (cbuf.contents[cindex] == L')')
	cindex++;
    else
	serror(Ngt("`%ls' missing"), L")");

    pl_destroy(&pending_heredocs);
    pending_heredocs = save_pending_heredocs;
    return result;
}

/* '`' で始まるコマンド置換を解析する。
 * cindex は '`' の直後の文字を指した状態で呼ばれ、対応する '`' の直後の文字を
 * 指した状態で返る。 */
wordunit_T *parse_cmdsubst_in_backquote(void)
{
    xwcsbuf_T buf;
    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_CMDSUB;

    assert(cbuf.contents[cindex - 1] == L'`');
    wb_init(&buf);
    for (;;) {
	ensure_buffer(1);
	switch (cbuf.contents[cindex]) {
	case L'`':
	    cindex++;
	    goto end;
	case L'\0':
	    if (read_more_input() != 0) {
		serror(Ngt("backquoted command substitution not closed"));
		goto end;
	    }
	    break;
	case L'\\':
	    cindex++;
	    switch (cbuf.contents[cindex]) {
		case L'$':  case L'`':  case L'\\':
		    goto default_;
		default:
		    wb_wccat(&buf, L'\\');
		    continue;
	    }
	case L'\n':
	    cinfo->lineno++;
	    /* falls thru! */
	default:  default_:
	    wb_wccat(&buf, cbuf.contents[cindex]);
	    cindex++;
	    break;
	}
    }
end:
    result->wu_cmdsub = wb_towcs(&buf);
    return result;
}

/* "$((" で始まる数式展開を解析する。
 * cindex は '$' の直後の '(' を指した状態で呼ばれ、成功すれば対応する "))" の
 * 直後の文字を指した状態で返る。数式展開に失敗すれば、cindex は元のままで
 * NULL を返す。 */
wordunit_T *tryparse_arith(void)
{
    size_t savecindex = cindex;
    assert(cbuf.contents[cindex] == L'(' && cbuf.contents[cindex + 1] == L'(');
    cindex += 2;

    wordunit_T *first = NULL, **lastp = &first, *wu;
    size_t startindex = cindex;
    int nestparen = 0;

    for (;;) {
	ensure_buffer(1);
	switch (cbuf.contents[cindex]) {
	case L'\\':
	    if (cbuf.contents[cindex + 1] == L'\n') {
		line_continuation(cindex);
		continue;
	    } else if (cbuf.contents[cindex + 1] != L'\0') {
		cindex += 2;
		continue;
	    }
	    break;
	case L'$':
	case L'`':
	    MAKE_WORDUNIT_STRING;
	    wu = parse_special_word_unit();
	    startindex = cindex;
	    if (wu) {
		*lastp = wu;
		lastp = &wu->next;
		continue;
	    } else if (cbuf.contents[cindex] == L'\0') {
		continue;
	    }
	    break;
	case L'(':
	    nestparen++;
	    break;
	case L')':
	    nestparen--;
	    if (nestparen < 0) {
		ensure_buffer(2);
		if (cbuf.contents[cindex + 1] == L')')
		    goto end;
		else
		    goto fail;
	    }
	    break;
	default:
	    break;
	}
	cindex++;
    }
end:
    MAKE_WORDUNIT_STRING;
    cindex += 2;

    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_ARITH;
    result->wu_arith = first;
    return result;

fail:
    wordfree(first);
    cindex = savecindex;
    return NULL;
}

/* 現在位置にある WORD トークンを文字列として取り出す。
 * 結果は新しく malloc したワイド文字列として返す。
 * cindex は次のトークンを指すように skip_blanks_and_comment した位置まで進む。
 * この関数は常に非 NULL を返す。 */
wchar_t *parse_word_as_wcs(void)
{
    size_t index = cindex;
    wordfree(parse_word(globalonly));

    /* 元のインデックスと現在のインデックスの間にあるワードを取り出す */
    wchar_t *result = xwcsndup(cbuf.contents + index, cindex - index);
    /* ワード末尾の空白を除去 */
    index = cindex - index;
    while (index-- > 0 && iswblank(result[index]));
    result[++index] = L'\0';
    return result;
}

/* 複合コマンドを解析する。command はコマンド名。 */
/* parse_group, parse_if などの複合コマンド解析関数は、skip_blanks_and_comment
 * の呼出しおよびリダイレクトの解析をせずに戻る。 */
command_T *parse_compound_command(const wchar_t *command)
{
    command_T *result;
    switch (command[0]) {
    case L'(':
	result = parse_group(CT_SUBSHELL);
	break;
    case L'{':
	result = parse_group(CT_GROUP);
	break;
    case L'i':
	result = parse_if();
	break;
    case L'f':
	result = parse_for();
	break;
    case L'w':
	result = parse_while(true);
	break;
    case L'u':
	result = parse_while(false);
	break;
    case L'c':
	result = parse_case();
	break;
    default:
	assert(false);
    }
    skip_blanks_and_comment();
    parse_redirect_list(&result->c_redirs);
    return result;
}

/* グループコマンドを解析する。type は CT_GROUP か CT_SUBSHELL。 */
command_T *parse_group(commandtype_T type)
{
    const wchar_t *terminator;

    if (type == CT_GROUP) {
	assert(cbuf.contents[cindex] == L'{');
	terminator = L"}";
    } else if (type == CT_SUBSHELL) {
	assert(cbuf.contents[cindex] == L'(');
	terminator = L")";
    } else {
	assert(false);
    }
    cindex++;

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = type;
    result->c_lineno = cinfo->lineno;
    result->c_redirs = NULL;
    result->c_subcmds = parse_compound_list();
    if (!result->c_subcmds)
	serror(Ngt("no commands in command group"));
    if (cbuf.contents[cindex] != terminator[0])
	print_errmsg_token_missing(terminator, cindex);
    cindex++;
    return result;
}

/* if コマンドを解析する */
command_T *parse_if(void)
{
    assert(is_token_at(L"if", cindex));
    cindex += 2;

    ifcommand_T *first = NULL, **lastp = &first;
    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_IF;
    result->c_lineno = cinfo->lineno;
    result->c_redirs = NULL;

    bool els = false;
    while (!cerror) {
	ifcommand_T *ic = xmalloc(sizeof *ic);
	*lastp = ic;
	lastp = &ic->next;
	ic->next = NULL;
	if (!els) {
	    ic->ic_condition = parse_compound_list();
	    if (!ic->ic_condition)
		serror(Ngt("no commands between `if' and `then'"));
	    ensure_buffer(5);
	    if (is_token_at(L"then", cindex))
		cindex += 4;
	    else
		print_errmsg_token_missing(L"then", cindex);
	} else {
	    ic->ic_condition = NULL;
	}
	ic->ic_commands = parse_compound_list();
	if (!ic->ic_commands)
	    serror(Ngt("no commands after `%ls'"), els ? L"else" : L"then");
	ensure_buffer(5);
	if (!els) {
	    if (is_token_at(L"else", cindex)) {
		cindex += 4;
		els = true;
	    } else if (is_token_at(L"elif", cindex)) {
		cindex += 4;
	    } else if (is_token_at(L"fi", cindex)) {
		cindex += 2;
		break;
	    } else {
		print_errmsg_token_missing(L"fi", cindex);
	    }
	} else {
	    if (is_token_at(L"fi", cindex))
		cindex += 2;
	    else
		print_errmsg_token_missing(L"fi", cindex);
	    break;
	}
    }
    result->c_ifcmds = first;
    return result;
}

/* for コマンドを解析する */
command_T *parse_for(void)
{
    assert(is_token_at(L"for", cindex));
    cindex += 3;
    skip_blanks_and_comment();

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_FOR;
    result->c_lineno = cinfo->lineno;
    result->c_redirs = NULL;

    wchar_t *name = parse_word_as_wcs();
    if (name[0] == L'\0')
	serror(Ngt("no identifier after `for'"));
    else if (*skip_name(name) != L'\0')
	serror(Ngt("`%ls' is not valid identifier"), name);
    result->c_forname = realloc_wcstombs(name);
    if (!result->c_forname)
	serror(Ngt("unexpected error in wide character conversion"));

    skip_to_next_token();
    ensure_buffer(3);
    if (is_token_at(L"in", cindex)) {
	redir_T *redirs = NULL;
	cindex += 2;
	skip_blanks_and_comment();
	result->c_forwords = parse_words_and_redirects(&redirs, false);
	if (redirs) {
	    serror(Ngt("redirections not allowed after `in'"));
	    redirsfree(redirs);
	}
    } else {
	result->c_forwords = NULL;
    }
    if (cbuf.contents[cindex] == L';')
	cindex++;
    skip_to_next_token();
    ensure_buffer(3);
    if (is_token_at(L"do", cindex))
	cindex += 2;
    else
	print_errmsg_token_missing(L"do", cindex);
    result->c_forcmds = parse_compound_list();
    if (!result->c_forcmds)
	serror(Ngt("no commands between `do' and `done'"));
    ensure_buffer(5);
    if (is_token_at(L"done", cindex))
	cindex += 4;
    else
	print_errmsg_token_missing(L"done", cindex);
    return result;
}

/* while/until コマンドを解析する */
command_T *parse_while(bool whltype)
{
    assert(is_token_at(whltype ? L"while" : L"until", cindex));
    cindex += 5;

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_WHILE;
    result->c_lineno = cinfo->lineno;
    result->c_redirs = NULL;
    result->c_whltype = whltype;
    result->c_whlcond = parse_compound_list();
    if (!result->c_whlcond)
	serror(Ngt("no commands after `%ls'"), whltype ? L"while" : L"until");
    ensure_buffer(3);
    if (is_token_at(L"do", cindex))
	cindex += 2;
    else
	print_errmsg_token_missing(L"do", cindex);
    result->c_whlcmds = parse_compound_list();
    if (!result->c_whlcmds)
	serror(Ngt("no commands between `do' and `done'"));
    ensure_buffer(5);
    if (is_token_at(L"done", cindex))
	cindex += 4;
    else
	print_errmsg_token_missing(L"done", cindex);
    return result;
}

/* case コマンドを解析する */
command_T *parse_case(void)
{
    assert(is_token_at(L"case", cindex));
    cindex += 4;
    skip_blanks_and_comment();

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_CASE;
    result->c_lineno = cinfo->lineno;
    result->c_redirs = NULL;
    result->c_casword = parse_word(globalonly);
    if (!result->c_casword)
	serror(Ngt("no word after `%ls'"), L"case");
    skip_to_next_token();
    ensure_buffer(3);
    if (is_token_at(L"in", cindex))
	cindex += 2;
    else
	print_errmsg_token_missing(L"in", cindex);
    result->c_casitems = parse_case_list();
    ensure_buffer(5);
    if (is_token_at(L"esac", cindex))
	cindex += 4;
    else
	print_errmsg_token_missing(L"esac", cindex);
    return result;
}

/* case コマンドの本体 (`in' と `esac' の間) を解析する。
 * この関数を呼ぶ前に skip_to_next_token は不要。 */
caseitem_T *parse_case_list(void)
{
    caseitem_T *first = NULL, **lastp = &first;

    while (!cerror) {
	skip_to_next_token();
	ensure_buffer(5);
	if (is_token_at(L"esac", cindex))
	    break;

	caseitem_T *ci = xmalloc(sizeof *ci);
	*lastp = ci;
	lastp = &ci->next;
	ci->next = NULL;
	ci->ci_patterns = parse_case_patterns();
	ci->ci_commands = parse_compound_list();
	/* for や while とは異なり、ci_commands は NULL でも良い。 */
	ensure_buffer(2);
	if (cbuf.contents[cindex] == L';' && cbuf.contents[cindex+1] == L';') {
	    cindex += 2;
	} else {
	    break;
	}
    }
    return first;
}

/* case 項目のパタン部分を解析する。
 * cindex は ')' の次の文字 (not トークン) まで進む。
 * 予め ensure_buffer(1), skip_blanks_and_comment しておくこと。 */
void **parse_case_patterns(void)
{
    plist_T wordlist;

    pl_init(&wordlist);
    if (cbuf.contents[cindex] == L'(') {  /* 最初の '(' は無視 */
	cindex++;
	skip_blanks_and_comment();
    }
    while (!cerror) {
	if (is_token_delimiter_char(cbuf.contents[cindex]))
	    serror(Ngt("invalid character `%lc' in case pattern"),
		    (wint_t) cbuf.contents[cindex]);
	pl_add(&wordlist, parse_word(globalonly));
	ensure_buffer(1);
	if (cbuf.contents[cindex] == L'|') {
	    cindex++;
	} else if (cbuf.contents[cindex] == L')') {
	    cindex++;
	    break;
	} else {
	    serror(Ngt("`%ls' missing"), L")");
	    break;
	}
	skip_blanks_and_comment();
    }
    return pl_toary(&wordlist);
}

/* 現在位置に関数定義コマンドがあればそれを解析する。
 * この関数は内部で行連結の削除を行うので、行番号は呼出し元で覚えておくこと。 */
command_T *tryparse_function(void)
{
    size_t savecindex = cindex;
    unsigned long lineno = cinfo->lineno;

    /* 関数の名前を取り出す */
    wchar_t *nameend;
    size_t namelen;

readname:
    while (*(nameend = skip_name(cbuf.contents + cindex)) == L'\0'
	    && read_more_input() == 0);
    namelen = nameend - (cbuf.contents + cindex);
    if (namelen == 0 || !is_token_delimiter_char(*nameend)) {
	ensure_buffer(2);
	if (nameend[0] == L'\\' && nameend[1] == L'\n') { /* 行連結なら削除 */
	    line_continuation(nameend - cbuf.contents);
	    goto readname;
	}
	goto fail;
    }
    cindex += namelen;
    skip_blanks_and_comment();

    /* 括弧の解析 */
    if (cbuf.contents[cindex] != L'(')
	goto fail;
    cindex++;
    skip_blanks_and_comment();
    if (cbuf.contents[cindex] != L')') {
	serror(Ngt("`(' must be followed by `)' in function definition"));
	return NULL;
    }
    cindex++;
    skip_to_next_token();

    /* 関数本体の解析 */
    const wchar_t *t = check_opening_token_at(cindex);
    if (!t) {
	serror(Ngt("function body must be compound command"));
	return NULL;
    }
    command_T *body = parse_compound_command(t);
    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_FUNCDEF;
    result->c_lineno = lineno;
    result->c_redirs = NULL;
    result->c_funcname = realloc_wcstombs(
	    xwcsndup(cbuf.contents + savecindex, namelen));
    result->c_funcbody = body;
    if (!result->c_funcname) {
	cerror = true;
	xerror(0, Ngt("unexpected error in wide character conversion"));
    }
    return result;

fail:
    cindex = savecindex;
    return NULL;
}

/* ヒアドキュメントの内容を読み込む。 */
void read_heredoc_contents(redir_T *r)
{
    if (wcschr(r->rd_hereend, L'\n')) {
	serror(Ngt("end-of-heredoc indicator containing newline"));
	return;
    }

    assert(r->rd_type == RT_HERE || r->rd_type == RT_HERERT);
    if (wcspbrk(r->rd_hereend, QUOTES))
	read_heredoc_contents_without_expand(r);
    else
	read_heredoc_contents_with_expand(r);
}

/* パラメータ展開などの無いヒアドキュメントの内容を読み込む。 */
void read_heredoc_contents_without_expand(redir_T *r)
{
    wchar_t *eoc = unquote(r->rd_hereend);
    size_t eoclen = wcslen(eoc);
    xwcsbuf_T buf;
    wb_init(&buf);
    while (!is_end_of_heredoc_contents(eoc, eoclen, r->rd_type == RT_HERERT)
	    && cbuf.contents[cindex] != L'\0') {
	wb_cat(&buf, cbuf.contents + cindex);
	cindex = cbuf.length;
	if (cbuf.contents[cindex - 1] == L'\n')
	    cinfo->lineno++;
    }
    free(eoc);
    
    wordunit_T *wu = xmalloc(sizeof *wu);
    wu->next = NULL;
    wu->wu_type = WT_STRING;
    wu->wu_string = escapefree(wb_towcs(&buf), L"\\");
    r->rd_herecontent = wu;
}

/* パラメータ展開・コマンド置換・数式展開を含むヒアドキュメントの内容を
 * 読み込む。 */
void read_heredoc_contents_with_expand(redir_T *r)
{
    wordunit_T *first = NULL, **lastp = &first;
    const wchar_t *eoc = r->rd_hereend;
    size_t eoclen = wcslen(eoc);

    while (!is_end_of_heredoc_contents(eoc, eoclen, r->rd_type == RT_HERERT)
	    && cbuf.contents[cindex] != L'\0') {
	wordunit_T *wu = parse_string_to(true, true);
	if (wu) {
	    *lastp = wu;
	    while (wu->next)
		wu = wu->next;
	    lastp = &wu->next;
	}
    }
    r->rd_herecontent = first;
}

/* 現在位置の行全体がヒアドキュメントの終わりを表す文字列 eoc かどうか調べる。
 * eoc: 終わりを表す文字列
 * eoclen: wcslen(eoc)
 * skiptab: true なら行頭のタブを無視。
 * 戻り値が true なら、cindex は eoc を含む行の改行の直後まで進む。
 * false なら、cindex はそのまま。ただし skiptab が true ならタブの直後まで
 * cindex を進める。 */
bool is_end_of_heredoc_contents(const wchar_t *eoc, size_t eoclen, bool skiptab)
{
    if (cinfo->lastinputresult != 0)
	return true;

    assert(wcslen(eoc) == eoclen);
    assert(cbuf.length > 0 && cbuf.contents[cindex - 1] == L'\n');
    read_more_input();
    while (skiptab && cbuf.contents[cindex] == L'\t') {
	cindex++;
    }

    const wchar_t *m = matchwcsprefix(cbuf.contents + cindex, eoc);
    if (m && *m == L'\n') {
	cindex += eoclen + 1;
	cinfo->lineno++;
	return true;
    } else if (m && *m == L'\0') {
	cindex += eoclen;
	return true;
    } else {
	return false;
    }
}

/* 現在位置の文字列を解析する。パラメータ展開・コマンド置換・数式展開を
 * 認識するが、一重・二重引用符は引用符として扱わない。
 * backquote: バッククォートで囲んだコマンド置換を解釈するかどうか
 * stoponnewline: true なら、改行のところで解析を終了する。このとき改行は解析
 *     結果に含まれる。false なら現在位置以降全て (EOF まで) を解析する。 */
wordunit_T *parse_string_to(bool backquote, bool stoponnewline)
{
    wordunit_T *first = NULL, **lastp = &first, *wu;
    size_t startindex = cindex;

    for (;;) {
	switch (cbuf.contents[cindex]) {
	case L'\0':
	    goto done;
	case L'\\':
	    if (cbuf.contents[cindex + 1] == L'\n') {
		line_continuation(cindex);
		continue;
	    } else if (cbuf.contents[cindex + 1] != L'\0') {
		cindex += 2;
		continue;
	    }
	    break;
	case L'\n':
	    cinfo->lineno++;
	    if (stoponnewline) {
		cindex++;
		goto done;
	    }
	    read_more_input();
	    break;
	case L'`':
	    if (!backquote)
		break;
	    /* falls thru! */
	case L'$':
	    MAKE_WORDUNIT_STRING;
	    wu = parse_special_word_unit();
	    startindex = cindex;
	    if (wu) {
		*lastp = wu;
		lastp = &wu->next;
		continue;
	    } else if (cbuf.contents[cindex] == L'\0') {
		continue;
	    }
	    break;
	default:
	    break;
	}
	cindex++;
    }
done:
    MAKE_WORDUNIT_STRING;
    return first;
}

/* 指定した入力に含まれるパラメータ展開・数式展開・コマンド置換
 * ("$(...)" のみ) を解釈する。
 * この関数は入力全体を最後まで解釈する。
 * info: 解析情報へのポインタ。全ての値を初期化しておくこと。
 * result: 成功したら *result に解析結果が入る。
 *         入力が空文字列なら *result は NULL になる。
 * 戻り値: 成功したかどうか (エラーがなかったかどうか)。
 * この関数は原則として再入不可能である。 */
bool parse_string(parseinfo_T *restrict info, wordunit_T **restrict result)
{
    cinfo = info;
    cerror = false;
    cindex = 0;
    wb_init(&cbuf);

    cinfo->lastinputresult = 0;
    read_more_input();
    if (cinfo->lastinputresult == 1) {
	wb_destroy(&cbuf);
	return false;
    }
    pl_init(&pending_heredocs);

    *result = parse_string_to(false, false);

    wb_destroy(&cbuf);
    pl_destroy(&pending_heredocs);
    if (cinfo->lastinputresult == 1 || cerror) {
	wordfree(*result);
	return false;
    } else {
	return true;
    }
}


/***** エラーメッセージ関連 *****/

const char *get_errmsg_unexpected_token(const wchar_t *token)
{
    switch (token[0]) {
	case L')': return Ngt("`%ls' without matching `('");
	case L'}': return Ngt("`%ls' without matching `{'");
	case L';': return Ngt("`%ls' used outside `case'");
	case L'!': return Ngt("`%ls' cannot be used as command name");
	case L'i': return Ngt("`%ls' cannot be used as command name");
	case L'f': return Ngt("`%ls' without matching `if'");
	case L't': return Ngt("`%ls' used without `if'");
	case L'd':
	    assert(token[1] == L'o');
	    if (token[2] == L'\0')
		return Ngt("`%ls' used without `for', `while', or `until'");
	    else
		return Ngt("`%ls' without matching `do'");
	case L'e':
	    if (token[1] == L's')
		return Ngt("`%ls' without matching `case'");
	    else
		if (token[2] == L's')
		    return Ngt("`%ls' used without `if'");
		else
		    return Ngt("`%ls' used without `if'");
	default:
	    assert(false);
    }
}

void print_errmsg_token_missing(const wchar_t *token, size_t index)
{
    const wchar_t *atoken = check_closing_token_at(index);
    if (atoken)
	serror(get_errmsg_unexpected_token(atoken), atoken);
    else
	serror(Ngt("`%ls' missing"), token);
}


/********** 構文木を文字列に戻すルーチン **********/

static void print_and_or_lists(
	xwcsbuf_T *restrict buf, const and_or_T *restrict andors,
	bool omit_last_semicolon)
    __attribute__((nonnull(1)));
static void print_pipelines(
	xwcsbuf_T *restrict buf, const pipeline_T *restrict pipelines)
    __attribute__((nonnull(1)));
static void print_commands(
	xwcsbuf_T *restrict buf, const command_T *restrict commands)
    __attribute__((nonnull(1)));
static void print_command_content(
	xwcsbuf_T *restrict buf, const command_T *restrict command)
    __attribute__((nonnull));
static void print_caseitems(
	xwcsbuf_T *restrict buf, const caseitem_T *restrict caseitems)
    __attribute__((nonnull(1)));
static void print_assigns(
	xwcsbuf_T *restrict buf, const assign_T *restrict assigns)
    __attribute__((nonnull(1)));
static void print_redirs(
	xwcsbuf_T *restrict buf, const redir_T *restrict redirs)
    __attribute__((nonnull(1)));
static void print_word(
	xwcsbuf_T *restrict buf, const wordunit_T *restrict word)
    __attribute__((nonnull(1)));
static void print_paramexp(
	xwcsbuf_T *restrict buf, const paramexp_T *restrict param)
    __attribute__((nonnull));
static void trim_end_of_buffer(xwcsbuf_T *buf)
    __attribute__((nonnull));

#if 0
/* 構文木をワイド文字列に変換して返す。
 * 戻り値は free 可能なワイド文字列へのポインタ。 */
wchar_t *commands_to_wcstring(const and_or_T *commands)
{
    xwcsbuf_T buf;

    wb_init(&buf);
    print_and_or_lists(&buf, commands, true);
    trim_end_of_buffer(&buf);
    return wb_towcs(&buf);
}
#endif

/* 一つ以上のパイプラインをワイド文字列に変換して返す。
 * 戻り値は free 可能なワイド文字列へのポインタ。 */
wchar_t *pipelines_to_wcs(const pipeline_T *pipelines)
{
    xwcsbuf_T buf;

    wb_init(&buf);
    print_pipelines(&buf, pipelines);
    trim_end_of_buffer(&buf);
    return wb_towcs(&buf);
}

/* 一つのコマンドをワイド文字列に変換して返す。
 * 戻り値は free 可能なワイド文字列へのポインタ。 */
wchar_t *command_to_wcs(const command_T *command)
{
    xwcsbuf_T buf;

    wb_init(&buf);
    print_command_content(&buf, command);
    trim_end_of_buffer(&buf);
    return wb_towcs(&buf);
}

void print_and_or_lists(
	xwcsbuf_T *restrict buf, const and_or_T *restrict c, bool omitsemicolon)
{
    while (c) {
	print_pipelines(buf, c->ao_pipelines);
	assert(iswblank(buf->contents[buf->length - 1]));
	if (c->ao_async)
	    wb_insert(buf, buf->length - 1, L"&");
	else if (!omitsemicolon || c->next)
	    wb_insert(buf, buf->length - 1, L";");
	c = c->next;
    }
}

void print_pipelines(xwcsbuf_T *restrict buf, const pipeline_T *restrict p)
{
    for (bool first = true; p; p = p->next, first = false) {
	if (!first)
	    wb_cat(buf, p->pl_cond ? L"&& " : L"|| ");
	if (p->pl_neg)
	    wb_cat(buf, L"! ");
	if (p->pl_loop)
	    wb_cat(buf, L"| ");
	print_commands(buf, p->pl_commands);
    }
}

void print_commands(xwcsbuf_T *restrict buf, const command_T *restrict c)
{
    while (c) {
	print_command_content(buf, c);
	print_redirs(buf, c->c_redirs);
	if (c->next)
	    wb_cat(buf, L"| ");
	c = c->next;
    }
}

void print_command_content(xwcsbuf_T *restrict buf, const command_T *restrict c)
{
    switch (c->c_type) {
    case CT_SIMPLE:
	print_assigns(buf, c->c_assigns);
	for (void **w = c->c_words; *w; w++) {
	    print_word(buf, *w);
	    wb_wccat(buf, L' ');
	}
	break;
    case CT_GROUP:
	wb_cat(buf, L"{ ");
	print_and_or_lists(buf, c->c_subcmds, false);
	wb_cat(buf, L"} ");
	break;
    case CT_SUBSHELL:
	wb_wccat(buf, L'(');
	print_and_or_lists(buf, c->c_subcmds, true);
	assert(iswblank(buf->contents[buf->length - 1]));
	wb_insert(buf, buf->length - 1, L")");
	break;
    case CT_IF:
	wb_cat(buf, L"if ");
	for (ifcommand_T *ic = c->c_ifcmds;;) {
	    print_and_or_lists(buf, ic->ic_condition, false);
	    wb_cat(buf, L"then ");
	    print_and_or_lists(buf, ic->ic_commands, false);
	    ic = ic->next;
	    if (!ic) {
		break;
	    } else if (!ic->ic_condition) {
		wb_cat(buf, L"else ");
		print_and_or_lists(buf, ic->ic_commands, false);
		break;
	    } else {
		wb_cat(buf, L"elif ");
	    }
	}
	wb_cat(buf, L"fi ");
	break;
    case CT_FOR:
	wb_cat(buf, L"for ");
	wb_mbscat(buf, c->c_forname);
	if (c->c_forwords) {
	    wb_cat(buf, L" in");
	    for (void **w = c->c_forwords; *w; w++) {
		wb_wccat(buf, L' ');
		print_word(buf, *w);
	    }
	}
	wb_cat(buf, L"; do ");
	print_and_or_lists(buf, c->c_forcmds, false);
	wb_cat(buf, L"done ");
	break;
    case CT_WHILE:
	wb_cat(buf, c->c_whltype ? L"while " : L"until ");
	print_and_or_lists(buf, c->c_whlcond, false);
	wb_cat(buf, L"do ");
	print_and_or_lists(buf, c->c_whlcmds, false);
	wb_cat(buf, L"done ");
	break;
    case CT_CASE:
	wb_cat(buf, L"case ");
	print_word(buf, c->c_casword);
	wb_cat(buf, L" in ");
	print_caseitems(buf, c->c_casitems);
	wb_cat(buf, L"esac ");
	break;
    case CT_FUNCDEF:
	wb_mbscat(buf, c->c_funcname);
	wb_cat(buf, L" () ");
	print_commands(buf, c->c_funcbody);
	break;
    }
}

void print_caseitems(xwcsbuf_T *restrict buf, const caseitem_T *restrict i)
{
    while (i) {
	bool first = true;

	wb_wccat(buf, L'(');
	for (void **w = i->ci_patterns; *w; w++) {
	    if (!first)
		wb_wccat(buf, L'|');
	    print_word(buf, *w);
	    first = false;
	}
	wb_cat(buf, L") ");
	print_and_or_lists(buf, i->ci_commands, true);
	wb_cat(buf, L";; ");

	i = i->next;
    }
}

void print_assigns(xwcsbuf_T *restrict buf, const assign_T *restrict a)
{
    while (a) {
	wb_mbscat(buf, a->name);
	wb_wccat(buf, L'=');
	print_word(buf, a->value);
	wb_wccat(buf, L' ');
	a = a->next;
    }
}

void print_redirs(xwcsbuf_T *restrict buf, const redir_T *restrict r)
{
    while (r) {
	const wchar_t *s;
	bool ishere;

	switch (r->rd_type) {
	    case RT_INPUT:    s = L"<";    ishere = false;  break;
	    case RT_OUTPUT:   s = L">";    ishere = false;  break;
	    case RT_CLOBBER:  s = L">|";   ishere = false;  break;
	    case RT_APPEND:   s = L">>";   ishere = false;  break;
	    case RT_INOUT:    s = L"<>";   ishere = false;  break;
	    case RT_DUPIN:    s = L"<&";   ishere = false;  break;
	    case RT_DUPOUT:   s = L">&";   ishere = false;  break;
	    case RT_HERE:     s = L"<<";   ishere = true;   break;
	    case RT_HERERT:   s = L"<<-";  ishere = true;   break;
	    default: assert(false);
	}
	wb_wprintf(buf, L"%d%ls", r->rd_fd, s);
	if (!ishere)
	    print_word(buf, r->rd_filename);
	else
	    wb_cat(buf, r->rd_hereend);
	wb_wccat(buf, L' ');

	r = r->next;
    }
}

void print_word(xwcsbuf_T *restrict buf, const wordunit_T *restrict w)
{
    while (w) {
	switch (w->wu_type) {
	case WT_STRING:
	    wb_cat(buf, w->wu_string);
	    break;
	case WT_PARAM:
	    print_paramexp(buf, w->wu_param);
	    break;
	case WT_CMDSUB:
	    wb_cat(buf, L"$(");
	    wb_cat(buf, w->wu_cmdsub);
	    wb_cat(buf, L")");
	    break;
	case WT_ARITH:
	    wb_cat(buf, L"$((");
	    print_word(buf, w->wu_arith);
	    wb_cat(buf, L"))");
	    break;
	}
	w = w->next;
    }
}

void print_paramexp(xwcsbuf_T *restrict buf, const paramexp_T *restrict p)
{
    wchar_t c;

    wb_cat(buf, L"${");
    if (p->pe_type & PT_NUMBER)
	wb_wccat(buf, L'#');
    if (p->pe_type & PT_NEST)
	print_word(buf, p->pe_nest);
    else
	wb_mbscat(buf, p->pe_name);
    if (p->pe_type & PT_COLON)
	wb_wccat(buf, L':');
    switch (p->pe_type & PT_MASK) {
    case PT_PLUS:
	wb_wccat(buf, L'+');
	goto append_subst;
    case PT_MINUS:
	wb_wccat(buf, L'-');
	goto append_subst;
    case PT_ASSIGN:
	wb_wccat(buf, L'=');
	goto append_subst;
    case PT_ERROR:
	wb_wccat(buf, L'?');
	goto append_subst;
    case PT_MATCH:
	if (p->pe_type & PT_MATCHHEAD) {
	    c = L'#';
	} else {
	    assert(p->pe_type & PT_MATCHTAIL);
	    c = L'%';
	}
	wb_wccat(buf, c);
	if (p->pe_type & PT_MATCHLONGEST)
	    wb_wccat(buf, c);
	print_word(buf, p->pe_match);
	break;
    case PT_SUBST:
	wb_wccat(buf, L'/');
	if (p->pe_type & PT_SUBSTALL)
	    wb_wccat(buf, L'/');
	else if (p->pe_type & PT_MATCHHEAD)
	    wb_wccat(buf, L'#');
	else if (p->pe_type & PT_MATCHTAIL)
	    wb_wccat(buf, L'%');
	print_word(buf, p->pe_match);
	wb_wccat(buf, L'/');
	goto append_subst;
append_subst:
	print_word(buf, p->pe_subst);
	break;
    }
    wb_wccat(buf, L'}');
}

void trim_end_of_buffer(xwcsbuf_T *buf)
{
    size_t i = buf->length;
    while (i > 0 && iswblank(buf->contents[--i]));
    wb_remove(buf, i + 1, SIZE_MAX);
}


/* vim: set ts=8 sts=4 sw=4 noet: */
