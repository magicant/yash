/* Yash: yet another shell */
/* parser.c: command line/shell script parser */
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
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "yash.h"
#include "util.h"
#include "parser.h"
#include "alias.h"
#include "variable.h"
#include <assert.h>


static bool read_next_line(bool insertnl);
static void serror(const char *format, ...)
	__attribute__((format (printf, 1, 2)));
static STATEMENT *parse_statements(bool oneline);
static PIPELINE *parse_pipelines();
static PIPELINE *parse_pipeline(void);
static PROCESS *parse_process(void);
static REDIR *tryparse_redir(void);
static void skip_with_quote_i(const char *delim, bool singquote);
static bool is_token_at(const char *token, size_t index);
static bool is_closing_token_at(size_t index);
static void print_statements(struct strbuf *restrict b, STATEMENT *restrict s);
static void print_pipelines(struct strbuf *restrict b, PIPELINE *restrict pl);
static void print_pipeline(struct strbuf *restrict b, PIPELINE *restrict p);
static void print_process(struct strbuf *restrict b, PROCESS *restrict p);
static void redirsfree(REDIR *r);

#define TOKENSEPARATOR " \t\n;&|<>()"

static struct parse_info *i_info;
static bool i_raw, i_finish, i_error;
static struct strbuf i_src;
static size_t i_index;

#define fromi(x) (i_src.contents + (x))
#define toi(x)   ((x) - i_src.contents)

/* コマンド入力を解析するエントリポイント。
 * info:   解析情報へのポインタ。全ての値を初期化しておくこと。
 * result: 成功したら *result に結果が入る。
 *         ソースに文が含まれなければ、結果は NULL となる。
 * 戻り値: 成功したら *result に結果を入れて 0 を返す。
 *         構文エラーなら 1 を、EOF に達したときは EOF を返す。 */
/* この関数はリエントラントではない */
int read_and_parse(
		struct parse_info *restrict info, STATEMENT **restrict result)
{
	char *src;

	i_info = info;
	i_raw = false;
	i_finish = false;
	i_error = false;
	i_index = 0;
	src = info->input(1, info->inputinfo);
	info->lineno++;
	if (!src)
		return EOF;

	sb_init(&i_src);
	sb_append(&i_src, src);
	free(src);

	STATEMENT *statements = parse_statements(true);
	if (*fromi(i_index))
		serror("invalid character: `%c'", *fromi(i_index));
	sb_destroy(&i_src);
	if (i_error) {
		statementsfree(statements);
		return 1;
	}
	*result = statements;
	return 0;
}

/* 更なる一行を読み込んで i_src に継ぎ足す。
 * insertnl: true なら新しく継ぎ足す行の前に改行を挿入する。
 * 戻り値:   読み込めたら true、エラーや EOF なら false。 */
static bool read_next_line(bool insertnl)
{
	char *line = i_finish ? NULL : i_info->input(2, i_info->inputinfo);

	if (!line) {
		i_finish = true;
		return false;
	}
	if (i_raw || insertnl)
		sb_cappend(&i_src, '\n');
	sb_append(&i_src, line);
	free(line);
	i_info->lineno++;
	return true;
}

/* 構文エラー発生時にエラーメッセージを stderr に出力する。
 * エラーメッセージには改行を入れておかなくてよい。 */
static void serror(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);

	fflush(stdout);
	if (i_info->filename)
		fprintf(stderr, "%s:%u: syntax error: ",
				i_info->filename, i_info->lineno);
	else
		fprintf(stderr, "%s: syntax error: ", yash_program_invocation_name);
	if (format)
		vfprintf(stderr, format, ap);
	else
		fprintf(stderr, "unknown error");
	fputc('\n', stderr);
	fflush(stderr);
	i_error = true;

	va_end(ap);
}

/* 複数の文を解析する。
 * oneline: true なら行末まで解析する。false なら ) や } が出るまで解析する。
 * 戻り値:  成功したらその結果。失敗したら NULL かもしれない。 */
static STATEMENT *parse_statements(bool oneline)
{
	STATEMENT *first = NULL, **lastp = &first;

	i_index = toi(skipwhites(fromi(i_index)));
	for (;;) {
		char c = *fromi(i_index);
		if (c == '\0') {
			if (oneline)
				goto end;
			if (!read_next_line(false))
				goto end;
			i_index = toi(skipwhites(fromi(i_index)));
			continue;
		} else if (c == ';' || c == '&') {
			char cc[2] = { c, '\0' };
			serror("unexpected `%s'", cc);
			i_index++;
			goto end;
		} else if (is_closing_token_at(i_index)) {
			goto end;
		}

		STATEMENT *temp = xmalloc(sizeof *temp);
		bool last = false;

		temp->next = NULL;
		temp->s_pipeline = parse_pipelines();
		if (!temp->s_pipeline) {
			free(temp);
			goto end;
		}
		switch (*fromi(i_index)) {
			case ';':  case '\n':
				temp->s_bg = false;
				i_index++;
				break;
			case '&':
				temp->s_bg = true;
				i_index++;
				break;
			default:  case '#':
				temp->s_bg = false;
				break;
		}
		i_index = toi(skipwhites(fromi(i_index)));
		*lastp = temp;
		lastp = &temp->next;
		if (last)
			goto end;
	}
end:
	return first;
}

/* 一つの文に含まれる一つ以上のパイプラインを解析する。
 * 戻り値: パイプラインの解析に成功したらその結果、さもなくば NULL。
 *         パイプラインが存在しない場合なども NULL を返す。 */
static PIPELINE *parse_pipelines()
{
	PIPELINE *first = NULL, **lastp = &first;

	for (;;) {
		if (*fromi(i_index) == '&') {
			serror("unexpected `%s'", "&");
			i_index++;
			goto end;
		}
		if (first != NULL) {
			while (!*fromi(i_index)) {
				if (!read_next_line(false)) {
					serror("no command after `%s'", "&&/||");
					goto end;
				}
				i_index = toi(skipwhites(fromi(i_index)));
			}
		}

		PIPELINE *pipe = parse_pipeline();
		if (!pipe) {
			i_error = true;
			goto end;
		}
		*lastp = pipe;
		lastp = &pipe->next;
		if (matchprefix(fromi(i_index), "||")) {
			pipe->pl_next_cond = false;
			i_index = toi(skipwhites(fromi(i_index + 2)));
		} else if (matchprefix(fromi(i_index), "&&")) {
			pipe->pl_next_cond = true;
			i_index = toi(skipwhites(fromi(i_index + 2)));
		} else {
			goto end;
		}
	}
end:
	return first;
}

/* 一つのパイプラインを解析する。
 * 戻り値: パイプラインの解析に成功したらその結果、さもなくば NULL。
 *         パイプラインが存在しない場合なども NULL を返す。 */
/* この関数内では '&&' と '||' は解析しない:
 * 戻り値の pl_next_cond は false となる */
static PIPELINE *parse_pipeline(void)
{
	PIPELINE *result = xmalloc(sizeof *result);
	PROCESS **lastp = &result->pl_proc;

	*result = (PIPELINE) { .next = NULL };

	if (is_token_at("!", i_index)) {
		result->pl_neg = true;
		i_index = toi(skipwhites(fromi(i_index + 1)));
	} else {
		result->pl_neg = false;
	}
	if (*fromi(i_index) == '|') {
		if (posixly_correct)
			serror("loop pipe not allowed in posix mode");
		result->pl_loop = true;
		i_index = toi(skipwhites(fromi(i_index + 1)));
	} else {
		result->pl_loop = false;
	}

	for (;;) {
		if (*fromi(i_index) == '|' && *fromi(i_index + 1) != '|') {
			serror("unexpected `%s'", "|");
			i_index++;
			goto returnnull;
		}
		if (result->pl_proc != NULL) {
			while (!*fromi(i_index)) {
				if (!read_next_line(false)) {
					serror("no command after `%s'", "|");
					goto returnnull;
				}
				i_index = toi(skipwhites(fromi(i_index)));
			}
		}

		PROCESS *proc = parse_process();
		if (!proc) {
			if (!i_error && (result->pl_neg || result->pl_loop))
				serror("no command after `%s'", result->pl_loop ? "|" : "!");
			if (!i_error && result->pl_proc != NULL)
				serror("no command after `%s'", "|");
			goto returnnull;
		}
		*lastp = proc;
		lastp = &proc->next;

		if (*fromi(i_index) == '|' && *fromi(i_index + 1) != '|') {
			i_index = toi(skipwhites(fromi(i_index + 1)));
		} else {
			break;
		}
	}
	return result;
returnnull:
	free(result);
	return NULL;
}

/* 一つのコマンドを解析する。
 * 戻り値: コマンドの解析に成功したらその結果、さもなくば NULL。
 *         コマンドが存在しない場合なども NULL を返す。 */
static PROCESS *parse_process(void)
{
	PROCESS *result = xmalloc(sizeof *result);
	REDIR **rlastp = &result->p_redirs;
	size_t initindex = i_index;

	*result = (PROCESS) { .next = NULL };

	if (posixly_correct) {
		/* parse_reserved_word(....) */
		subst_alias(&i_src, i_index, false);
	} else {
		subst_alias(&i_src, i_index, false);
		/* parse_reserved_word(....) */
	}
	switch (*fromi(i_index)) {
		case '(':
			i_index++;
			result->p_type = PT_SUBSHELL;
			result->p_subcmds = parse_statements(false);
			if (*fromi(i_index) != ')') {
				serror("missing `%s'", ")");
				goto returnnull;
			}
			i_index = toi(skipblanks(fromi(i_index + 1)));
			subst_alias(&i_src, i_index, true);
			break;
		case ')':
			goto returnnull;
		default:
			if (is_token_at("{", i_index)) {
				i_index++;
				result->p_type = PT_GROUP;
				result->p_subcmds = parse_statements(false);
				if (!is_token_at("}", i_index)) {
					serror("missing `%s'", "}");
					goto returnnull;
				}
				i_index = toi(skipblanks(fromi(i_index + 1)));
				subst_alias(&i_src, i_index, true);
				break;
			} else if (is_token_at("}", i_index)) {
				goto returnnull;
			} else {
				result->p_type = PT_NORMAL;
				break;
			}
	}

	bool assignment_acceptable = true;
	struct plist assigns, args;
	pl_init(&assigns);
	pl_init(&args);
	while (*fromi(i_index) != '#') {
		REDIR *rd = tryparse_redir();
		if (rd) {
			*rlastp = rd;
			rlastp = &rd->next;
		} else {
			size_t startindex = i_index;
			skip_with_quote_i(TOKENSEPARATOR, true);
			if (i_index == startindex) break;
			if (assignment_acceptable) {
				char *eq = fromi(startindex);
				while (is_name_char(*eq)) eq++;
				// TODO 配列の添字
				if (eq != fromi(startindex)
						&& !xisdigit(*fromi(startindex)) && *eq == '=') {
					// TODO () で囲んだ配列
					pl_append(&assigns,
							xstrndup(fromi(startindex), i_index - startindex));
				} else {
					assignment_acceptable = false;
				}
			}
			if (!assignment_acceptable) {
				pl_append(&args,
						xstrndup(fromi(startindex), i_index - startindex));
			}
		}
		i_index = toi(skipblanks(fromi(i_index)));
		subst_alias(&i_src, i_index, true);
	}

	result->p_assigns = (char **) pl_toary(&assigns);
	if (result->p_type == PT_NORMAL) {
		result->p_args = (char **) pl_toary(&args);
	} else {
		if (args.length > 0)
			serror("invalid token `%s'", (char *) args.contents[0]);
		recfree((void **) pl_toary(&args), free);
	}
	if (*fromi(i_index) == '(') {
		// XXX function parsing
		serror("`(' is not allowed here");
		i_index++;
	}
	if (initindex != i_index)
		return result;
returnnull:
	procsfree(result);
	return NULL;
}

/* 現在位置にあるリダイレクトを解釈する。
 * リダイレクトがなければ何もしないで NULL を返す。
 * リダイレクトを解釈できたらそれを返す。(i_index は解釈した部分の直後まで進む)
 * エラーの場合も NULL を返すが、i_index は進んでいるかもしれない。 */
static REDIR *tryparse_redir(void)
{
	REDIR *rd;
	char *init = fromi(i_index);
	char *symbol = init + strspn(init, "0123456789");
	char *symbol2;
	char *start, *end;
	int fd;
	enum redirect_type type;

	if (init == symbol) {
		fd = -1;
	} else {
		errno = 0;
		fd = strtol(init, &symbol2, 10);
		if (errno || fd < 0) fd = -2;
		if (symbol != symbol2) return NULL;
	}
	switch (symbol[0]) {
		case '<':
			if (fd == -1) fd = STDIN_FILENO;
			switch (symbol[1]) {
				case '<':
					return NULL;  // XXX here document not supported
				case '>':
					type = RT_INOUT;
					start = &symbol[2];
					break;
				case '&':
					type = RT_DUPIN;
					start = &symbol[2];
					break;
				default:
					type = RT_INPUT;
					start = &symbol[1];
					break;
			}
			break;
		case '>':
			if (fd == -1) fd = STDOUT_FILENO;
			switch (symbol[1]) {
				case '>':
					type = RT_APPEND;
					start = &symbol[2];
					break;
				case '&':
					type = RT_DUPOUT;
					start = &symbol[2];
					break;
				default:
					type = RT_OUTPUT;
					start = &symbol[1];
					break;
			}
			break;
		default:
			return NULL;
	}
	start += strspn(start, " \t");
	i_index = toi(start);
	subst_alias(&i_src, i_index, true);
	skip_with_quote_i(TOKENSEPARATOR, true);
	end = fromi(i_index);
	if (start == end) {
		serror("redirect target not specified");
		return NULL;
	}
	rd = xmalloc(sizeof *rd);
	*rd = (REDIR) {
		.next = NULL,
		.rd_type = type,
		.rd_fd = fd,
		.rd_file = xstrndup(start, end - start),
	};
	return rd;
}

/* 引用符 (" と `) とパラメータ ($) を解釈しつつ、delim 内の文字のどれかが
 * 現れるまで飛ばす。すなわち、delim に含まれない 0 個以上の文字を飛ばす。
 * singquote が true なら単一引用符 (') も解釈する。 */
static void skip_with_quote_i(const char *delim, bool singquote)
{
	bool saveenablealias = enable_alias;
	enable_alias = false;

	while (/* *fromi(i_index) && */ !strchr(delim, *fromi(i_index))) {
		switch (*fromi(i_index)) {
			case '\\':
				if (*fromi(i_index + 1) == '\0') {
					if (!i_raw)
						sb_replace(&i_src, i_index, SIZE_MAX, "");
					read_next_line(false);
				} else {
					i_index += 2;
				}
				continue;
			case '$':
				switch (*fromi(i_index + 1)) {
					case '{':
						i_index += 2;
						for (;;) {
							skip_with_quote_i("}", true);
							if (*fromi(i_index) == '}') break;
							if (!*fromi(i_index) && !read_next_line(true)) {
								serror("missing `%s'", "}");
								goto end;
							}
						}
						break;
					case '(':
						//if (s[2] == '(') {} // XXX 算術式展開
						i_index = toi(skipwhites(fromi(i_index + 2)));
						bool saveraw = i_raw;
						i_raw = true;
						statementsfree(parse_statements(false));
						i_raw = saveraw;
						if (*fromi(i_index) != ')') {
							serror("missing `%s'", ")");
							goto end;
						}
						break;
				}
				break;
			case '\'':
				if (!singquote) break;
				i_index++;
				for (;;) {
					i_index = toi(skip_without_quote(fromi(i_index), "'"));
					if (*fromi(i_index) == '\'') break;
					if (!*fromi(i_index) && !read_next_line(true)) {
						serror("missing \"'\"");
						goto end;
					}
				}
				break;
			case '"':
				i_index++;
				for (;;) {
					skip_with_quote_i("\"", false);
					if (*fromi(i_index) == '"') break;
					if (!*fromi(i_index) && !read_next_line(true)) {
						serror("missing `%s'", "\"");
						goto end;
					}
				}
				break;
			case '`':
				i_index++;
				for (;;) {
					skip_with_quote_i("`", false);
					if (*fromi(i_index) == '`') break;
					if (!*fromi(i_index) && !read_next_line(true)) {
						serror("missing \"`\"");
						goto end;
					}
				}
				break;
		}
		i_index++;
	}
end:
	enable_alias = saveenablealias;
}

/* i_src の位置 index に token トークンがあるかどうか調べる。
 * token: 調べる非演算子トークン
 * index: i_src 内のインデクス */
/* token には、他の演算子トークンの真の部分文字列であるような文字列を
 * 指定してはならない。 */
static bool is_token_at(const char *token, size_t index)
{
	char *c = matchprefix(fromi(index), token);
	return c && strchr(TOKENSEPARATOR, *c);
}

/* i_src の位置 index に ) や } や fi などのブロック終了トークンがあるかどうか
 * 調べる。 */
static bool is_closing_token_at(size_t index)
{
	return (*(fromi(index)) == ')')
		|| is_token_at("}", index)
		|| is_token_at("fi", index)
		|| is_token_at("done", index)
		|| is_token_at("esac", index);
}

/* 引用符などを考慮しつつ、delim 内の文字のどれかが現れるまで飛ばす。
 * すなわち、delim に含まれない 0 個以上の文字を飛ばす。 */
char *skip_with_quote(const char *restrict s, const char *restrict delim)
{
	while (*s && !strchr(delim, *s)) {
		switch (*s) {
			case '\\':
				s++;
				break;
			case '$':
				switch (*(s + 1)) {
					case '{':
						s = skip_with_quote(s + 2, "}");
						break;
					case '(':
						// XXX 算術式展開
						s = skip_with_quote(s + 2, ")");
						break;
				}
				break;
			case '\'':
				s = skip_without_quote(s + 1, "'");
				break;
			case '"':
				s = skip_with_quote(s + 1, "\"");
				break;
			case '`':
				s = skip_with_quote(s + 1, "`");
				break;
		}
		if (*s) s++;
	}
	return (char *) s;
}

/* delim 内の文字のどれかが現れるまで飛ばす。すなわち、delim に含まれない 0
 * 個以上の文字を飛ばす。s に含まれるエスケープなどは一切解釈しない。 */
char *skip_without_quote(const char *restrict s, const char *restrict delim)
{
	while (*s && !strchr(delim, *s)) s++;
	return (char *) s;
}

/* 文中の各パイプラインを元に STATEMENT の s_name を生成する。
 * 戻り値: 新しく malloc した p の表示名。
 *         表示名には '&' も ';' も含まれない。 */
char *make_statement_name(PIPELINE *p)
{
	struct strbuf buf;

	sb_init(&buf);
	print_pipelines(&buf, p);
	return sb_tostr(&buf);
}

/* 一つのパイプラインの表示名を生成する。
 * 戻り値: 新しく malloc した p の表示名。 */
char *make_pipeline_name(PIPELINE *p)
{
	struct strbuf buf;

	sb_init(&buf);
	print_pipeline(&buf, p);
	return sb_tostr(&buf);
}

/* 各文を文字列に変換して文字列バッファに追加する。 */
static void print_statements(struct strbuf *restrict b, STATEMENT *restrict s)
{
	while (s) {
		print_pipelines(b, s->s_pipeline);
		sb_append(b, s->s_bg ? "&" : ";");
		s = s->next;
		if (s)
			sb_append(b, " ");
	}
}

/* 各パイプラインを文字列に変換して文字列バッファに追加する。 */
static void print_pipelines(struct strbuf *restrict b, PIPELINE *restrict p)
{
	while (p) {
		print_pipeline(b, p);
		if (p->next)
			sb_append(b, p->pl_next_cond ? " && " : " || ");
		p = p->next;
	}
}

/* 一つのパイプラインの内容を文字列に変換して文字列バッファに追加する。 */
static void print_pipeline(struct strbuf *restrict b, PIPELINE *restrict p)
{
	PROCESS *pr = p->pl_proc;
	if (p->pl_neg)
		sb_append(b, "! ");
	if (p->pl_loop)
		sb_append(b, "| ");
	while (pr) {
		print_process(b, pr);
		pr = pr->next;
		if (pr)
			sb_append(b, " | ");
	}
}

/* 一つのプロセスを文字列に変換して文字列バッファに追加する。 */
static void print_process(struct strbuf *restrict b, PROCESS *restrict p)
{
	bool f = false;

	if (p->p_assigns) {
		char **assigns = p->p_assigns;
		while (*assigns) {
			if (f) sb_append(b, " ");
			sb_append(b, *assigns);
			f = true;
			assigns++;
		}
	}
	switch (p->p_type) {
		case PT_NORMAL:
			if (p->p_args) {
				char **args = p->p_args;
				while (*args) {
					if (f) sb_append(b, " ");
					sb_append(b, *args);
					f = true;
					args++;
				}
			}
			break;
		case PT_GROUP:
			if (f) sb_append(b, " ");
			sb_append(b, "{ ");
			print_statements(b, p->p_subcmds);
			sb_append(b, " }");
			f = true;
			break;
		case PT_SUBSHELL:
			if (f) sb_append(b, " ");
			sb_append(b, "( ");
			print_statements(b, p->p_subcmds);
			sb_append(b, " )");
			f = true;
			break;
	}
	if (p->p_redirs) {
		REDIR *redir = p->p_redirs;
		while (redir) {
			if (f) sb_append(b, " ");
			sb_printf(b, "%d", redir->rd_fd);
			switch (redir->rd_type) {
				case RT_INPUT:   sb_append(b, "<");   break;
				case RT_OUTPUT:  sb_append(b, ">");   break;
				case RT_APPEND:  sb_append(b, ">>");  break;
				case RT_INOUT:   sb_append(b, "<>");  break;
				case RT_DUPIN:   sb_append(b, "<&");  break;
				case RT_DUPOUT:  sb_append(b, ">&");  break;
			}
			sb_append(b, redir->rd_file);
			f = true;
			redir = redir->next;
		}
	}
}

static void redirsfree(REDIR *r)
{
	while (r) {
		free(r->rd_file);

		REDIR *rr = r->next;
		free(r);
		r = rr;
	}
}

/* 指定したプロセスのリストを解放する */
void procsfree(PROCESS *p)
{
	while (p) {
		recfree((void **) p->p_assigns, free);
		if (p->p_type == PT_NORMAL)
			recfree((void **) p->p_args, free);
		else
			statementsfree(p->p_subcmds);
		redirsfree(p->p_redirs);

		PROCESS *pp = p->next;
		free(p);
		p = pp;
	}
}

/* 指定したパイプラインのリストを解放する */
void pipesfree(PIPELINE *p)
{
	while (p) {
		procsfree(p->pl_proc);

		PIPELINE *pp = p->next;
		free(p);
		p = pp;
	}
}

/* 指定した文のリストを解放する */
void statementsfree(STATEMENT *s)
{
	while (s) {
		pipesfree(s->s_pipeline);

		STATEMENT *ss = s->next;
		free(s);
		s = ss;
	}
}
