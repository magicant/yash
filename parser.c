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


int read_and_parse(
		struct parse_info *restrict info, STATEMENT **restrict result);
static bool read_next_line(bool insertnl);
static void serror(const char *format, ...)
	__attribute__((format (printf, 1, 2)));
static STATEMENT *parse_statements(char stop);
static PIPELINE *parse_pipelines();
static PROCESS *parse_processes(bool *neg, bool *loop);
static bool parse_words(PROCESS *process);
static REDIR *tryparse_redir(void);
static void skip_with_quote_i(const char *delim, bool singquote);
char *skip_with_quote(const char *restrict s, const char *restrict delim);
char *skip_without_quote(const char *restrict s, const char *restrict delim);
char *make_statement_name(PIPELINE *pipelines);
char *make_pipeline_name(PROCESS *processes, bool neg, bool loop);
static void print_statements(struct strbuf *restrict b, STATEMENT *restrict s);
static void print_pipelines(struct strbuf *restrict b, PIPELINE *restrict pl);
static void print_processes(struct strbuf *restrict b, PROCESS *restrict p);
static void print_process(struct strbuf *restrict b, PROCESS *restrict p);
void redirsfree(REDIR *redirs);
void procsfree(PROCESS *processes);
void pipesfree(PIPELINE *pipelines);
void statementsfree(STATEMENT *statements);

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

	STATEMENT *statements = parse_statements('\0');
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

/* コマンド入力を解析する。
 * stop:   この文字が現れるまで解析を続ける。(エラーの場合を除く)
 *         ただし stop = ';' なら ";;" が現れるまで解析を続ける。
 * 戻り値: 成功したらその結果。失敗したら NULL かもしれない。 */
static STATEMENT *parse_statements(char stop)
{
	STATEMENT *first = NULL, **lastp = &first;
	char c;

	i_index = toi(skipwhites(fromi(i_index)));
	while ((c = *fromi(i_index)) != stop
			&& (stop != ';' || *fromi(i_index + 1) == ';')) {
		if (c == '\0') {
			if (!read_next_line(false)) {
				serror("missing `%c'", stop);
				goto end;
			}
			i_index = toi(skipwhites(fromi(i_index)));
			continue;
		} else if (c == ';' || c == '&') {
			serror("unexpected `%c'", c);
			i_index++;
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
			case ';':
				if (stop == ';' && *fromi(i_index + 1) == ';') {
					last = true;
					goto default_case;
				}
				/* falls thru! */
			case '\n':  case '\r':
				temp->s_bg = false;
				i_index++;
				break;
			case '&':
				temp->s_bg = true;
				i_index++;
				break;
			default:  default_case:  case '#':
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

/* 一つの文を解析する。
 * 戻り値: 成功したらその結果。失敗したら途中結果または NULL。 */
static PIPELINE *parse_pipelines()
{
	PIPELINE *first = NULL, **lastp = &first;

	for (;;) {
		char c = *fromi(i_index);
		if (c == '|' || c == '&') {
			serror("unexpected `%c'", c);
			i_index++;
			goto end;
		}
		if (first) {
			while (!*fromi(i_index)) {
				if (!read_next_line(false)) {
					serror("no command after `&&' or `||'");
					goto end;
				}
				i_index = toi(skipwhites(fromi(i_index)));
			}
		}

		PIPELINE *temp = xmalloc(sizeof *temp);
		bool last = false;

		temp->next = NULL;
		temp->pl_proc = parse_processes(&temp->pl_neg, &temp->pl_loop);
		if (!temp->pl_proc) {
			free(temp);
			i_error = true;
			goto end;
		}
		if (hasprefix(fromi(i_index), "||")) {
			temp->pl_next_cond = false;
			i_index = toi(skipwhites(fromi(i_index + 2)));
		} else if (hasprefix(fromi(i_index), "&&")) {
			temp->pl_next_cond = true;
			i_index = toi(skipwhites(fromi(i_index + 2)));
		} else {
			last = true;
		}
		*lastp = temp;
		lastp = &temp->next;
		if (last)
			goto end;
	}
end:
	return first;
}

/* 一つのパイプラインを解析する。
 * neg:    パイプラインの終了ステータスを反転するかどうかが
 *         このポインタが指す先に入る。
 * loop:   パイプラインが環状であるかどうかがこのポインタの指す先に入る。
 * 戻り値: 成功したらその結果。失敗したら途中結果または NULL。 */
static PROCESS *parse_processes(bool *neg, bool *loop)
{
	PROCESS *first = NULL, **lastp = &first;

	if (*fromi(i_index) == '!') {
		*neg = true;
		i_index++;
		i_index = toi(skipblanks(fromi(i_index)));
	} else {
		*neg = false;
	}

	char c;
	while ((c = *fromi(i_index))) {
		if (c == '|' && *fromi(i_index + 1) != '|') {
			serror("unexpected `%c", c);
			i_index++;
			goto end;
		}

		PROCESS *temp = xmalloc(sizeof *temp);
		*temp = (PROCESS) { .next = NULL, };
		if (!parse_words(temp)) {
			procsfree(temp);
			goto end;
		}

		if (*fromi(i_index) == '|' && *fromi(i_index + 1) != '|') {
			*loop = true;
			i_index++;
			i_index = toi(skipblanks(fromi(i_index)));
		} else {
			*loop = false;
		}

		*lastp = temp;
		lastp = &temp->next;
	}
end:
	return first;
}

/* 一つのコマンドを解析し、結果を p のアドレスに入れる。
 * この関数は p の p_type, p_assigns, p_args, p_subcmds, p_redirsを書き換える。
 * 戻り値: コマンドの解析に成功したら true、コマンドがなければ false。
 * 戻り値が false でも p の内容は変わっているかもしれない。 */
static bool parse_words(PROCESS *p)
{
	size_t initindex = i_index;
	bool result;
	REDIR *rfirst = NULL, **rlastp = &rfirst;

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
			p->p_type = PT_SUBSHELL;
			p->p_subcmds = parse_statements(')');
			if (*fromi(i_index) != ')') {
				result = true;
				goto end;
			}
			i_index = toi(skipblanks(fromi(i_index + 1)));
			subst_alias(&i_src, i_index, true);
			break;
		case '{':
			/* XXX: "{" はそれ自体はトークンではないので、"{abc" のような場合を
			 * 除外しないといけない。 */
			i_index++;
			p->p_type = PT_GROUP;
			p->p_subcmds = parse_statements('}');
			if (*fromi(i_index) != '}') {
				result = true;
				goto end;
			}
			i_index = toi(skipblanks(fromi(i_index + 1)));
			subst_alias(&i_src, i_index, true);
			break;
		case ')':
		case '}':
			result = false;
			goto end;
		default:
			p->p_type = PT_NORMAL;
			break;
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
			skip_with_quote_i(" \t;&|<>()\n\r", true);
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

	p->p_assigns = (char **) pl_toary(&assigns);
	if (p->p_type == PT_NORMAL) {
		p->p_args = (char **) pl_toary(&args);
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
	result = (initindex != i_index);
end:
	p->p_redirs = rfirst;
	return result;
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
	skip_with_quote_i(" \t;&|()\n\r", true);
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

	while (*fromi(i_index) && !strchr(delim, *fromi(i_index))) {
		switch (*fromi(i_index)) {
			case '\\':
				if (*fromi(i_index + 1) == '\0') {
					i_src.length = i_index;
					i_src.contents[i_index] = '\0';
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
								serror("missing `%c'", '}');
								goto end;
							}
						}
						break;
					case '(':
						//if (s[2] == '(') {} // XXX 算術式展開
						i_index = toi(skipwhites(fromi(i_index + 2)));
						bool saveraw = i_raw;
						i_raw = true;
						statementsfree(parse_statements(')'));
						i_raw = saveraw;
						if (*fromi(i_index) != ')') {
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
						serror("missing `%c'", '"');
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

/* 文中のパイプラインを元に STATEMENT の s_name を生成する。
 * 戻り値: 新しく malloc した p の表示名。
 *         表示名には '&' も ';' も含まれない。 */
char *make_statement_name(PIPELINE *p)
{
	struct strbuf buf;

	sb_init(&buf);
	print_pipelines(&buf, p);
	return sb_tostr(&buf);
}

/* パイプラインに含まれるプロセスを元に PIPELINE の表示名を生成する。
 * 戻り値: 新しく malloc した p の表示名。これには neg や loop に応じて
 *         先頭に "!"、末尾に "|" が付く。 */
char *make_pipeline_name(PROCESS *p, bool neg, bool loop)
{
	struct strbuf buf;

	sb_init(&buf);
	if (neg)
		sb_append(&buf, "! ");
	print_processes(&buf, p);
	if (loop)
		sb_append(&buf, " |");
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
		if (p->pl_neg)
			sb_append(b, "! ");
		print_processes(b, p->pl_proc);
		if (p->pl_loop)
			sb_append(b, " |");
		if (p->next)
			sb_append(b, p->pl_next_cond ? " && " : " || ");
		p = p->next;
	}
}

/* 各プロセスを文字列に変換して文字列バッファに追加する。 */
static void print_processes(struct strbuf *restrict b, PROCESS *restrict p)
{
	while (p) {
		print_process(b, p);
		p = p->next;
		if (p)
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
		default:
			assert(false);
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

void redirsfree(REDIR *r)
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
