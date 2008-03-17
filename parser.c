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
#include "yash.h"
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "parser.h"


/********** 構文解析ルーチン **********/

static void serror(const char *restrict format, ...)
	__attribute__((nonnull(1),format(printf,1,2)));
static inline int read_more_input(void);
static inline int ensure_buffer(size_t n);
static void skip_blanks_and_comment(void);
static void skip_to_next_token(void);
static void next_line(void);
static bool is_token_delimiter_char(wchar_t c);
static bool is_command_delimiter_char(wchar_t c);
static bool is_token_at(const wchar_t *token, size_t index)
	__attribute__((nonnull));
static const wchar_t *check_opening_token_at(size_t index);
static const wchar_t *check_closing_token_at(size_t index);
static and_or_T *parse_command_list(void)
	__attribute__((malloc));
static and_or_T *parse_and_or_list(void)
	__attribute__((malloc));
static pipeline_T *parse_pipelines_in_and_or(void)
	__attribute__((malloc));
static pipeline_T *parse_pipeline(void)
	__attribute__((malloc));
static command_T *parse_commands_in_pipeline(void)
	__attribute__((malloc));
static command_T *parse_command(void)
	__attribute__((malloc));
static redir_T **parse_assignments_and_redirects(command_T *c)
	__attribute__((malloc,nonnull));
static void **parse_words_and_redirects(redir_T **redirlastp)
	__attribute__((malloc,nonnull));
static assign_T *tryparse_assignment(void)
	__attribute__((malloc));
static redir_T *tryparse_redirect(void)
	__attribute__((malloc));
static wordunit_T *parse_word(bool global_alias_only)
	__attribute__((malloc));
static void read_heredoc_contents(redir_T *redir);
static const char *get_errmsg_unexpected_token(const wchar_t *token)
	__attribute__((nonnull));


/* 解析中のソースのデータ */
static parseinfo_T *cinfo;
/* 現在の解析でエラーが発生しているなら true */
static bool cerror;
/* 解析するソースを取り込むバッファ */
static xwcsbuf_T cbuf;
/* 現在解析している箇所の cbuf におけるインデックス */
static size_t cindex;
/* 最後の read_more_input の結果 */
static int lastinputresult;
/* 読み込みを控えているヒアドキュメントのリスト */
static plist_T pending_heredocs;


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
 * この関数は再入不可能である。 */
int read_and_parse(parseinfo_T *restrict info, and_or_T **restrict result)
{
	cinfo = info;
	cerror = false;
	cindex = 0;
	wb_init(&cbuf);
	pl_init(&pending_heredocs);
	lastinputresult = cinfo->input(&cbuf, cinfo->inputinfo);
	if (lastinputresult == EOF) {
		return EOF;
	} else if (lastinputresult == 1) {
		*result = NULL;
		return 0;
	}

	and_or_T *r = parse_command_list();

	wb_destroy(&cbuf);
	pl_destroy(&pending_heredocs);

	if (!cerror) {
		assert(cindex == cbuf.length);
		*result = r;
		return 0;
	} else {
		andorsfree(r);
		return 1;
	}
}

/* 構文エラーメッセージを stderr に出力する。
 * format はこの関数内で gettext に通す。format の最後に改行は不要。 */
static void serror(const char *restrict format, ...)
{
	va_list ap;

	fflush(stdout);
	fprintf(stderr, gt("%s:%lu: syntax error: "),
			cinfo->filename ? cinfo->filename : yash_program_invocation_name,
			cinfo->lineno);
	va_start(ap, format);
	vfprintf(stderr, gt(format), ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
	cerror = true;
}

/* 更なる入力を読み込む
 * 戻り値: 0:   何らかの入力があった。
 *         1:   対話モードで SIGINT を受けた。(buf の内容は不定)
 *         EOF: EOF に達したか、エラーがあった。(buf の内容は不定) */
static inline int read_more_input(void)
{
	if (lastinputresult == 0)
		lastinputresult = cinfo->input(&cbuf, cinfo->inputinfo);
	return lastinputresult;
}

/* cbuf.length >= cindex + n となるように、必要に応じて read_more_input する。
 * ただし、改行を越えては読み込まない。
 * 戻り値: lastinputresult */
static inline int ensure_buffer(size_t n)
{
	int lir = lastinputresult;
	while (lir == 0 && cbuf.length < cindex + n) {
		if (cbuf.length > 0 && cbuf.contents[cbuf.length - 1] == L'\n')
			break;
		lir = read_more_input();
	}
	return lir;
}

/* cindex を増やして blank 文字およびコメントを飛ばす。必要に応じて
 * read_more_input する。cindex は次の非 blank 文字を指す。コメントを飛ばした
 * 場合はコメントの直後の文字 (普通は改行文字) を指す。 */
static void skip_blanks_and_comment(void)
{
skipblanks:
	while (iswblank(cbuf.contents[cindex]))
		cindex++;
	if (cbuf.contents[cindex] == L'\0' && read_more_input() == 0)
		goto skipblanks;

skiptonewline:
	if (cbuf.contents[cindex] == L'#') {
		do {
			cindex++;
		} while (cbuf.contents[cindex] != L'\n'
				&& cbuf.contents[cindex] != L'\0');
	}
	if (cbuf.contents[cindex] == L'\0' && read_more_input() == 0)
		goto skiptonewline;
}

/* 次のトークンに達するまで cindex を増やして blank 文字・改行文字・コメントを
 * 飛ばす。必要に応じて read_more_input する。 */
static void skip_to_next_token(void)
{
	skip_blanks_and_comment();
	while (lastinputresult == 0 && cbuf.contents[cindex] == L'\n') {
		next_line();
		skip_blanks_and_comment();
	}
}

/* 現在位置が改行文字のとき、次の行に進む。
 * ヒアドキュメントの読み込みを控えているならそれも行う。 */
static void next_line(void)
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
static bool is_token_delimiter_char(wchar_t c)
{
	return iswblank(c) || c == L'\0' || c == L'\n'
		|| c == L';' || c == L'&' || c == L'|'
		|| c == L'<' || c == L'>' || c == L'(' || c == L')';
}

/* 指定した文字が単純コマンドを区切る文字かどうかを調べる */
static bool is_command_delimiter_char(wchar_t c)
{
	return c == L'\0' || c == L'\n'
		|| c == L';' || c == L'&' || c == L'|' || c == L'(' || c == L')';
}

/* cbuf の指定したインデックスにトークン token があるか調べる。
 * token: 調べる非演算子トークン
 * index: cbuf へのインデックス (cbuf.length 以下) */
/* token には、他の演算子トークンの真の部分文字列であるような文字列を
 * 指定してはならない。 */
static bool is_token_at(const wchar_t *token, size_t index)
{
	wchar_t *c = matchwcsprefix(cbuf.contents + index, token);
	return c && is_token_delimiter_char(*c);
}

/* cbuf の指定したインデックスに ( や { や if などの「開く」トークンがあるか
 * 調べる。あれば、そのトークンを示す文字列定数を返す。なければ NULL を返す。 */
static const wchar_t *check_opening_token_at(size_t index)
{
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
 * 調べる。あれば、そのトークンを示す文字列定数を返す。なければ NULL を返す。 */
static const wchar_t *check_closing_token_at(size_t index)
{
	ensure_buffer(5);
	if (cbuf.contents[index] == L')') return L")";
	if (is_token_at(L"}",    index))  return L"}";
	if (is_token_at(L";;",   index))  return L";;";
	if (is_token_at(L"fi",   index))  return L"fi";
	if (is_token_at(L"done", index))  return L"done";
	if (is_token_at(L"esac", index))  return L"esac";
	return NULL;
}

/* 区切りの良いところまで解析を進める。すなわち、少くとも一行の入力を読み取り、
 * 複合コマンドなどの内部でない改行の直後まで解析する。 */
static and_or_T *parse_command_list(void)
{
	and_or_T *first = NULL, **lastp = &first;

	while (!cerror) {
		skip_blanks_and_comment();
		if (cbuf.contents[cindex] == L'\0') {
			break;
		} else if (cbuf.contents[cindex] == L'\n') {
			next_line();
			break;
		}

		and_or_T *ao = parse_and_or_list();
		if (ao) {
			*lastp = ao;
			lastp = &ao->next;
		}
	}
	return first;
}

/* 一つの and/or リストを (末尾の '&' または ';' も含めて) 解析する。 */
static and_or_T *parse_and_or_list(void)
{
	and_or_T *result = xmalloc(sizeof *result);
	result->next = NULL;
	result->ao_pipelines = parse_pipelines_in_and_or();
	result->ao_async = false;

	switch (cbuf.contents[cindex]) {
		case L'&':
			result->ao_async = true;
			/* falls thru! */
		case L';':
			cindex++;
			skip_blanks_and_comment();
			break;
	}
	return result;
}

/* and/or リスト内の全てのパイプラインを解析する。 */
static pipeline_T *parse_pipelines_in_and_or(void)
{
	pipeline_T *first = NULL, **lastp = &first;

	for (;;) {
		pipeline_T *p = parse_pipeline();
		if (p) {
			*lastp = p;
			lastp = &p->next;
		}

		ensure_buffer(2);
		if (cbuf.contents[cindex] == L'&'
				&& cbuf.contents[cindex+1] == L'&') {
			p->pl_next_cond = true;
		} else if (cbuf.contents[cindex] == L'|'
				&& cbuf.contents[cindex+1] == L'|') {
			p->pl_next_cond = false;
		} else {
			break;
		}
		cindex += 2;
		skip_to_next_token();
	}
	return first;
}

/* 一つのパイプライン ('|' で繋がった一つ以上のコマンド) を解析する。 */
static pipeline_T *parse_pipeline(void)
{
	pipeline_T *result = xmalloc(sizeof *result);
	result->next = NULL;
	result->pl_next_cond = false;

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
static command_T *parse_commands_in_pipeline(void)
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
static command_T *parse_command(void)
{
	/* Note: check_closing_token_at は ensure_buffer(5) を含む */
	const wchar_t *t = check_closing_token_at(cindex);
	if (t) {
		serror(get_errmsg_unexpected_token(t));
		return NULL;
	} else if (is_command_delimiter_char(cbuf.contents[cindex])) {
		if (cbuf.contents[cindex] == L'\0')
			serror(Ngt("command missing at end of input"));
		else
			serror(Ngt("command missing before `%lc'"),
					(wint_t) cbuf.contents[cindex]);
		return NULL;
	}

	command_T *result = xmalloc(sizeof *result);
	redir_T **redirlastp = &result->c_redirs;

	result->next = NULL;
	result->c_lineno = cinfo->lineno;

	t = check_opening_token_at(cindex);
	if (t) {
		// TODO parser.c: parse_command: ( や { や if
		serror("%ls: NOT IMPLEMENTED", t);
		free(result);
		return NULL;
	}

	// TODO parser.c: parse_command: 関数の解析

	/* 普通の単純なコマンドを解析 */
	result->c_type = CT_SIMPLE;
	redirlastp = parse_assignments_and_redirects(result);
	result->c_words = parse_words_and_redirects(redirlastp);
	return result;
}

/* 変数代入とリダイレクトを解析する。
 * 結果を c->c_assigns と c->c_redirs に代入し、新しい redirlastp を返す。 */
static redir_T **parse_assignments_and_redirects(command_T *c)
{
	assign_T **assgnlastp = &c->c_assigns;
	redir_T **redirlastp = &c->c_redirs;
	assign_T *assgn;
	redir_T *redir;

	c->c_assigns = NULL;
	c->c_redirs = NULL;
	ensure_buffer(1);
	while (!is_command_delimiter_char(cbuf.contents[cindex])) {
		if ((redir = tryparse_redirect())) {
			*redirlastp = redir;
			redirlastp = &redir->next;
		} else if ((assgn = tryparse_assignment())) {
			*assgnlastp = assgn;
			assgnlastp = &assgn->next;
		} else {
			break;
		}
		ensure_buffer(1);
	}
	return redirlastp;
}

/* ワードとリダイレクトを解析する。
 * リダイレクトの解析結果は *redirlastp に入る。
 * ワードの解析結果は wordunit_T * を void * に変換したものの配列への
 * ポインタとして返す。 */
static void **parse_words_and_redirects(redir_T **redirlastp)
{
	plist_T wordlist;
	redir_T *redir;
	wordunit_T *word;
	bool first = true;

	pl_init(&wordlist);
	ensure_buffer(1);
	while (!is_command_delimiter_char(cbuf.contents[cindex])) {
		if ((redir = tryparse_redirect())) {
			*redirlastp = redir;
			redirlastp = &redir->next;
		} else if ((word = parse_word(!first))) {
			pl_add(&wordlist, word);
			first = false;
		} else {
			break;
		}
		ensure_buffer(1);
	}
	return pl_toary(&wordlist);
}

/* 現在位置に変数代入があれば、それを解析して結果を返す。
 * なければ、何もせず NULL を返す。 */
static assign_T *tryparse_assignment(void)
{
	// TODO parser.c: tryparse_assignment: 未実装
	return NULL;
}

static void wordfree(wordunit_T *w);

/* 現在位置にリダイレクトがあれば、それを解析して結果を返す。
 * なければ、何もせず NULL を返す。 */
static redir_T *tryparse_redirect(void)
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
		if (!is_token_delimiter_char(*endptr)) {
			if (*endptr == L'\0' && read_more_input() == 0)
				goto reparse;
			else
				return NULL;
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
		result->rd_filename = parse_word(true);
	} else {
		unsigned long lineno = cinfo->lineno;
		size_t index = cindex;
		wordfree(parse_word(true));
		assert(index != cindex);
		if (lineno != cinfo->lineno)
			serror(Ngt("end-of-heredoc indicator containing newline"));

		/* 元のインデックスと現在のインデックスの間にあるワードを取り出す */
		wchar_t *endofheredoc = xwcsndup(cbuf.contents + index, cindex - index);
		/* ワード末尾の空白を除去 */
		index = cindex - index;
		while (index > 0 && iswblank(endofheredoc[--index]));
		endofheredoc[++index] = L'\0';
		result->rd_hereend = endofheredoc;
		result->rd_herecontent = NULL;
	}
	return result;
}

/* 現在位置のエイリアスを展開し、ワードを解析する。
 * global_alias_only: true ならグローバルエイリアスしか展開しない。false なら
 *         全てのエイリアスを展開の対象にする。 */
static wordunit_T *parse_word(bool global_alias_only)
{
	size_t startindex = cindex;

	ensure_buffer(1);
	while (!is_token_delimiter_char(cbuf.contents[cindex])) {
		cindex++;
		ensure_buffer(1);
	}

	wordunit_T *result = xmalloc(sizeof *result);
	result->next = NULL;
	result->wu_type = WT_STRING;
	result->wu_string = xwcsndup(
			cbuf.contents + startindex, cindex - startindex);
	skip_blanks_and_comment();
	(void) global_alias_only;
	return result;
	// TODO parser.c: parse_word: 暫定実装: 正確なワードの解析
	// TODO parser.c: parse_word: エイリアス
}

/* ヒアドキュメントの内容を読み込む。 */
static void read_heredoc_contents(redir_T *redir)
{
	wordunit_T *wu = xmalloc(sizeof *wu);
	wu->next = NULL;
	wu->wu_type = WT_STRING;
	wu->wu_string = xwcsdup(L"");
	redir->rd_herecontent = wu;
	// TODO parser.c: read_heredoc_contents: 未実装
}


/***** エラーメッセージを返す関数 *****/

static const char *get_errmsg_unexpected_token(const wchar_t *token)
{
	switch (token[0]) {
		case L')': return Ngt("`)' without matching `('");
		case L'}': return Ngt("`}' without matching `{'");
		case L';': return Ngt("`;;' used outside `case'");
		case L'f': return Ngt("`fi' without matching `if'");
		case L'd': return Ngt("`done' without matching `do'");
		case L'e': return Ngt("`esac' without matching `case'");
		default:   assert(false);
	}
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

static void print_and_or_lists(
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

static void print_pipelines(
		xwcsbuf_T *restrict buf, const pipeline_T *restrict p)
{
	while (p) {
		if (p->pl_neg)
			wb_cat(buf, L"! ");
		if (p->pl_loop)
			wb_cat(buf, L"| ");
		print_commands(buf, p->pl_commands);
		if (p->next)
			wb_cat(buf, p->pl_next_cond ? L"&& " : L"|| ");
		p = p->next;
	}
}

static void print_commands(
		xwcsbuf_T *restrict buf, const command_T *restrict c)
{
	while (c) {
		print_command_content(buf, c);
		print_redirs(buf, c->c_redirs);
		if (c->next)
			wb_cat(buf, L"| ");
		c = c->next;
	}
}

static void print_command_content(
		xwcsbuf_T *restrict buf, const command_T *restrict c)
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
		wb_cat(buf, L"( ");
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
		wb_cat(buf, c->c_forname);
		wb_cat(buf, L" in");
		for (void **w = c->c_forwords; *w; w++) {
			wb_wccat(buf, L' ');
			print_word(buf, *w);
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
		wb_cat(buf, L"in ");
		print_caseitems(buf, c->c_casitems);
		wb_cat(buf, L"esac ");
		break;
	}
}

static void print_caseitems(
		xwcsbuf_T *restrict buf, const caseitem_T *restrict i)
{
	while (i) {
		bool first = true;

		wb_wccat(buf, L'(');
		for (void **w = i->ci_patterns; *w; w++) {
			if (first)
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

static void print_assigns(
		xwcsbuf_T *restrict buf, const assign_T *restrict a)
{
	while (a) {
		wb_cat(buf, a->name);
		wb_wccat(buf, L'=');
		print_word(buf, a->value);
		wb_wccat(buf, L' ');
		a = a->next;
	}
}

static void print_redirs(
		xwcsbuf_T *restrict buf, const redir_T *restrict r)
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
		wb_printf(buf, L"%d%ls", r->rd_fd, s);
		if (!ishere)
			print_word(buf, r->rd_filename);
		else
			wb_cat(buf, r->rd_hereend);
		wb_wccat(buf, L' ');

		r = r->next;
	}
}

static void print_word(
		xwcsbuf_T *restrict buf, const wordunit_T *restrict w)
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

static void print_paramexp(
		xwcsbuf_T *restrict buf, const paramexp_T *restrict p)
{
	wchar_t c;

	wb_cat(buf, L"${");
	if (p->pe_type & PT_NUMBER)
		wb_wccat(buf, L'#');
	wb_cat(buf, p->pe_name);
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

static void trim_end_of_buffer(xwcsbuf_T *buf)
{
	size_t i = buf->length;
	while (i > 0 && iswblank(buf->contents[--i]));
	wb_remove(buf, i + 1, SIZE_MAX);
}


/********** 構文木データを解放するルーチン **********/

static void pipesfree(pipeline_T *p);
static void comsfree(command_T *c);
static void ifcmdsfree(ifcommand_T *i);
static void caseitemsfree(caseitem_T *i);
static void wordfree(wordunit_T *w);
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

static void pipesfree(pipeline_T *p)
{
	while (p) {
		comsfree(p->pl_commands);

		pipeline_T *next = p->next;
		free(p);
		p = next;
	}
}

static void comsfree(command_T *c)
{
	while (c) {
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
		}

		command_T *next = c->next;
		free(c);
		c = next;
	}
}

static void ifcmdsfree(ifcommand_T *i)
{
	while (i) {
		andorsfree(i->ic_condition);
		andorsfree(i->ic_commands);

		ifcommand_T *next = i->next;
		free(i);
		i = next;
	}
}

static void caseitemsfree(caseitem_T *i)
{
	while (i) {
		recfree(i->ci_patterns, wordfree_vp);
		andorsfree(i->ci_commands);

		caseitem_T *next = i->next;
		free(i);
		i = next;
	}
}

static void wordfree(wordunit_T *w)
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

static void wordfree_vp(void *w)
{
	wordfree((wordunit_T *) w);
}

static void paramfree(paramexp_T *p)
{
	if (p) {
		free(p->pe_name);
		wordfree(p->pe_match);
		wordfree(p->pe_subst);
	}
}

static void assignsfree(assign_T *a)
{
	while (a) {
		free(a->name);
		wordfree(a->value);

		assign_T *next = a->next;
		free(a);
		a = next;
	}
}

static void redirsfree(redir_T *r)
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
