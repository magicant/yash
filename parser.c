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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include <wctype.h>
#include "strbuf.h"
#include "parser.h"


/********** 構文解析ルーチン **********/

/* 少なくとも一行の入力を読み取り、解析する。
 * info: 解析情報へのポインタ。全ての値を初期化しておくこと。
 * result: 成功したら *result に解析結果が入る。
 *         コマンドがなければ *result は NULL になる。
 * 戻り値: 成功したら *result に結果を入れて 0 を返す。
 *         構文エラーならエラーメッセージを出して 1 を返す。
 *         EOF に達したか入力エラーなら EOF を返す。 */
int read_and_parse(parseinfo_t *restrict info, and_or_t **restrict result)
{
	(void) info, (void) result;
	//TODO parser.c: read_and_parse
	return EOF;
}


/********** 構文木を文字列に戻すルーチン **********/

static void print_and_or_lists(
		xwcsbuf_t *restrict buf, const and_or_t *restrict andors)
	__attribute__((nonnull(1)));
static void print_pipelines(
		xwcsbuf_t *restrict buf, const pipeline_t *restrict pipelines)
	__attribute__((nonnull(1)));
static void print_commands(
		xwcsbuf_t *restrict buf, const command_t *restrict commands)
	__attribute__((nonnull(1)));
static void print_assigns(
		xwcsbuf_t *restrict buf, const assign_t *restrict assigns)
	__attribute__((nonnull(1)));
static void print_command_content(
		xwcsbuf_t *restrict buf, const command_t *restrict command)
	__attribute__((nonnull));
static void print_redirs(
		xwcsbuf_t *restrict buf, const redir_t *restrict redirs)
	__attribute__((nonnull(1)));
static void print_word(
		xwcsbuf_t *restrict buf, const wordunit_t *restrict word)
	__attribute__((nonnull(1)));
static void print_paramexp(
		xwcsbuf_t *restrict buf, const paramexp_t *restrict param)
	__attribute__((nonnull));
static void trim_end_of_buffer(xwcsbuf_t *buf)
	__attribute__((nonnull));

/* 構文木をワイド文字列に変換して返す。
 * 戻り値は free 可能なワイド文字列へのポインタ。 */
wchar_t *commands_to_wcstring(const and_or_t *commands)
{
	xwcsbuf_t buf;

	wb_init(&buf);
	print_and_or_lists(&buf, commands);
	trim_end_of_buffer(&buf);
	return wb_towcs(&buf);
}

static void print_and_or_lists(
		xwcsbuf_t *restrict buf, const and_or_t *restrict c)
{
	while (c) {
		print_pipelines(buf, c->ao_pipelines);
		assert(iswblank(buf->contents[buf->length - 1]));
		if (c->ao_async)
			wb_insert(buf, buf->length - 1, L"&");
		else if (c->next)
			wb_insert(buf, buf->length - 1, L";");
		c = c->next;
	}
}

static void print_pipelines(
		xwcsbuf_t *restrict buf, const pipeline_t *restrict p)
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
		xwcsbuf_t *restrict buf, const command_t *restrict c)
{
	while (c) {
		print_assigns(buf, c->c_assigns);
		print_command_content(buf, c);
		print_redirs(buf, c->c_redirs);
		if (c->next)
			wb_cat(buf, L"| ");
		c = c->next;
	}
}

static void print_assigns(
		xwcsbuf_t *restrict buf, const assign_t *restrict a)
{
	while (a) {
		wb_cat(buf, a->name);
		wb_wccat(buf, L'=');
		print_word(buf, a->value);
		wb_wccat(buf, L' ');
		a = a->next;
	}
}

static void print_command_content(
		xwcsbuf_t *restrict buf, const command_t *restrict c)
{
	switch (c->c_type) {
	case CT_SIMPLE:
		for (void **w = c->c_words; *w; w++) {
			print_word(buf, *w);
			wb_wccat(buf, L' ');
		}
		break;
	case CT_GROUP:
		wb_cat(buf, L"{ ");
		print_and_or_lists(buf, c->c_subcmds);
		wb_cat(buf, L"} ");
		break;
	case CT_SUBSHELL:
		wb_cat(buf, L"( ");
		print_and_or_lists(buf, c->c_subcmds);
		wb_cat(buf, L") ");
		break;
	}
}

static void print_redirs(
		xwcsbuf_t *restrict buf, const redir_t *restrict r)
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

		r = r->next;
	}
}

static void print_word(
		xwcsbuf_t *restrict buf, const wordunit_t *restrict w)
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
		}
		w = w->next;
	}
}

static void print_paramexp(
		xwcsbuf_t *restrict buf, const paramexp_t *restrict p)
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

static void trim_end_of_buffer(xwcsbuf_t *buf)
{
	size_t i = buf->length;
	while (i > 0 && iswblank(buf->contents[--i]));
	wb_remove(buf, i, SIZE_MAX);
}
