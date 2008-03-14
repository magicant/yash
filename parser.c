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
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "strbuf.h"
#include "parser.h"
#include "plist.h"


/********** 構文解析ルーチン **********/

/* 少なくとも一行の入力を読み取り、解析する。
 * info: 解析情報へのポインタ。全ての値を初期化しておくこと。
 * result: 成功したら *result に解析結果が入る。
 *         コマンドがなければ *result は NULL になる。
 * 戻り値: 成功したら *result に結果を入れて 0 を返す。
 *         構文エラーならエラーメッセージを出して 1 を返す。
 *         EOF に達したか入力エラーなら EOF を返す。 */
int read_and_parse(parseinfo_T *restrict info, and_or_T **restrict result)
{
	(void) info, (void) result;
	//TODO parser.c: read_and_parse
	return EOF;
}


/********** 構文木を文字列に戻すルーチン **********/

static void print_and_or_lists(
		xwcsbuf_T *restrict buf, const and_or_T *restrict andors)
	__attribute__((nonnull(1)));
static void print_pipelines(
		xwcsbuf_T *restrict buf, const pipeline_T *restrict pipelines)
	__attribute__((nonnull(1)));
static void print_commands(
		xwcsbuf_T *restrict buf, const command_T *restrict commands)
	__attribute__((nonnull(1)));
static void print_assigns(
		xwcsbuf_T *restrict buf, const assign_T *restrict assigns)
	__attribute__((nonnull(1)));
static void print_command_content(
		xwcsbuf_T *restrict buf, const command_T *restrict command)
	__attribute__((nonnull));
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
	print_and_or_lists(&buf, commands);
	trim_end_of_buffer(&buf);
	return wb_towcs(&buf);
}

static void print_and_or_lists(
		xwcsbuf_T *restrict buf, const and_or_T *restrict c)
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
		print_assigns(buf, c->c_assigns);
		print_command_content(buf, c);
		print_redirs(buf, c->c_redirs);
		if (c->next)
			wb_cat(buf, L"| ");
		c = c->next;
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

static void print_command_content(
		xwcsbuf_T *restrict buf, const command_T *restrict c)
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
	wb_remove(buf, i, SIZE_MAX);
}


/********** 構文木データを解放するルーチン **********/

static void pipesfree(pipeline_T *p);
static void comsfree(command_T *c);
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
		assignsfree(c->c_assigns);
		redirsfree(c->c_redirs);
		switch (c->c_type) {
			case CT_SIMPLE:
				recfree(c->c_words, wordfree_vp);
				break;
			case CT_GROUP:
			case CT_SUBSHELL:
				andorsfree(c->c_subcmds);
				break;
		}

		command_T *next = c->next;
		free(c);
		c = next;
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
