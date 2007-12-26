/* Yash: yet another shell */
/* expand.c: functions for command line expansion */
/* © 2007 magicant */

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


#include <ctype.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <glob.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "yash.h"
#include "util.h"
#include "parser.h"
#include "expand.h"
#include "exec.h"
#include "path.h"
#include <assert.h>


/* コマンドの解釈は次の順序で行う:
 *  1. リダイレクトの分離
 *  2. ブレース展開
 *  3. チルダ展開
 *  4. パラメータや変数・算術式・コマンド置換
 *  5. 単語分割 (IFS)
 *  6. パス名展開 (glob)
 *  7. エスケープ・引用符の削除
 */

bool expand_line(char **args, int *argc, char ***argv);
char *expand_single(const char *arg)
	__attribute__((malloc, nonnull));
static bool expand_arg(const char *s, struct plist *argv);
static bool expand_brace(char *s, struct plist *result);
static bool expand_subst(char *s, struct plist *result);
static char *expand_param(char **src);
static char *get_comsub_code_p(char **src);
static char *get_comsub_code_bq(char **src);
static bool do_glob(char **ss, struct plist *result);
static char *unescape_for_glob(const char *s)
	__attribute__((malloc, nonnull));
static char *unescape(char *s)
	__attribute__((malloc, nonnull));
void escape_sq(const char *s, struct strbuf *buf);
void escape_dq(const char *s, struct strbuf *buf);


/* コマンドライン上の各種展開を行う。
 * args は入力引数であり、展開される文字列への配列である。これは NULL でもよい。
 * argc, argv は出力引数であり、各ポインタのアドレスにそれぞれ
 * 展開結果の個数・展開結果への配列へのポインタが入る。(エラーの有無に拘らず)
 * *argv は新しく malloc した文字列への新しく malloc した配列である。
 * args の内容は一切変更しない。
 * 成功すると true 、失敗すると false を返す。失敗時はエラーを出力する。 */
bool expand_line(char **args, int *argc, char ***argv)
{
	struct plist alist;
	bool result = true;

	pl_init(&alist);
	if (!args) goto end;

	while (*args) {
		result &= expand_arg(*args, &alist);
		args++;
	}

end:
	*argc = alist.length;
	*argv = (char **) pl_toary(&alist);
	return result;
}

/* 一つの引数に対して各種展開を行う。
 * 成功すると展開結果を新しく malloc した文字列として返す。エラーがあったら
 * (stderr に出力して) NULL を返す。結果が単語分割で複数 or 0 個になった場合も
 * エラーである。 */
char *expand_single(const char *arg)
{
	struct plist alist;
	pl_init(&alist);
	if (!expand_arg(arg, &alist) || alist.length != 1) {
		recfree(pl_toary(&alist));
		return NULL;
	}

	char *result = alist.contents[0];
	pl_destroy(&alist);
	return result;
}

/* 一つの引数に対してブレース展開以降の全ての展開を行い結果を argv に追加する。
 * 一つの引数が単語分割によって複数の単語に分かれることがあるので、argv
 * に加わる要素は一つとは限らない。(分割結果が 0 個になることもある)
 * 成功すると true を、失敗すると false を返す。失敗時はエラーを出力する。
 * 失敗しても途中結果が argv に追加されること場合もある。 */
static bool expand_arg(const char *s, struct plist *argv)
{
	struct plist temp1, temp2;
	bool ok = true;

	pl_init(&temp1);
	pl_init(&temp2);

	expand_brace(xstrdup(s), &temp1);  /* まずブレース展開 */

	for (size_t i = 0; i < temp1.length; i++) {  /* 続いてチルダ展開 */
		char *s1 = temp1.contents[i];
		char *s2;
		if (s1[0] == '~' && (s2 = expand_tilde(s1))) {
			pl_append(&temp2, s2);
			free(s1);
		} else {
			pl_append(&temp2, s1);
		}
	}
	pl_clear(&temp1);

	for (size_t i = 0; i < temp2.length; i++) {  /* そしてパラメータ展開 */
		ok &= expand_subst(temp2.contents[i], &temp1);
	}
	pl_clear(&temp2);

	/* 最後に glob */
	ok &= do_glob((char **) temp1.contents, argv);

	pl_destroy(&temp1);
	pl_destroy(&temp2);
	return ok;
}

/* ブレース展開を行い、結果を result に追加する。
 * 展開を行ったならば、展開結果は複数の文字列になる (各展開結果を新しく malloc
 * した文字列として result に追加する)。展開がなければ、元の s がそのまま
 * result に入る。
 * s:      展開を行う、free 可能な文字列へのポインタ。
 *         展開を行った場合、s は expand_brace 内で free される。
 *         展開がなければ、s は result に追加される。
 * result: 初期化済の plist へのポインタ
 * 戻り値: 展開を行ったら true、展開がなければ false。 */
static bool expand_brace(char *s, struct plist *result)
{
	struct plist ps;
	char *t;
	int count = 0;
	size_t headlen, tailidx;

	t = skip_with_quote(s, "{");
	if (*t != '{' || *++t == '\0') {
		pl_append(result, s);
		return false;
	}
	pl_init(&ps);
	pl_append(&ps, t);
	for (;;) {
		t = skip_with_quote(t, "{,}");
		switch (*t++) {
			case '{':
				count++;
				break;
			case ',':
				if (count == 0)
					pl_append(&ps, t);
				break;
			case '}':
				if (--count >= 0)
					break;
				if (ps.length >= 2) {
					pl_append(&ps, t);
					goto done;
				}
				/* falls thru! */
			default:
				pl_destroy(&ps);
				pl_append(result, s);
				return false;
		}
	}
done:
	tailidx = ps.length - 1;
	headlen = (char *) ps.contents[0] - s - 1;
	for (size_t i = 0, l = ps.length - 1; i < l; i++) {
		struct strbuf buf;
		sb_init(&buf);
		sb_nappend(&buf, s, headlen);
		sb_nappend(&buf, ps.contents[i],
				(char *) ps.contents[i+1] - (char *) ps.contents[i] - 1);
		sb_append(&buf, ps.contents[tailidx]);
		expand_brace(sb_tostr(&buf), result);
	}
	pl_destroy(&ps);
	free(s);
	return true;
}

/* パラメータ展開・コマンド置換、そして単語分割を行い、結果を *result
 * に追加する。
 * 引用符 (" と ') やバックスラッシュエスケープはそのまま展開結果に残る。
 * s:      free 可能な、展開・分割を行う対象の文字列。
 * エラー時はエラーメッセージを出力して false を返す。
 * ただし、エラーの場合でも途中結果が result に入っているかもしれない。
 * s は (展開や分割の結果が元と同じ時) *result に追加されるか、free される。 */
/* IFS による単語分割は、パラメータ展開の結果内においてのみ実行する。
 * これはセキュリティ上の理由による。 */
static bool expand_subst(char *const s, struct plist *result)
{
	bool ok = true;
	bool indq = false;  /* 引用符 " の中かどうか */
	char *s1 = s, *s2 = strpbrk(s, "\"'\\$`");
	struct strbuf buf;

	if (!s2) {
		pl_append(result, s);
		return ok;
	}
	sb_init(&buf);
	for (;;) {
		sb_nappend(&buf, s1, s2 - s1);
		s1 = s2;
		switch (*s1) {
			case '"':
				indq = !indq;
				goto default_case;
			case '\'':
				s1 = skip_without_quote(s1 + 1, "'");
				sb_nappend(&buf, s2, s1 - s2);
				goto default_case;
			case '\\':
				sb_cappend(&buf, '\\');
				if (*++s1)
					goto default_case;
				else
					break;
			case '$':
				s1++;
				s2 = expand_param(&s1);
				if (s2) {
					if (!indq) {  /* 単語分割をしつつ追加 */
						const char *ifs = getenv("IFS");  //XXX
						const char *s3 = s2;
						if (!ifs)
							ifs = " \t\n";
						for (;;) {
							size_t len = strspn(s3, ifs);
							if (len > 0) {
								if (buf.length > 0) {
									pl_append(result, sb_tostr(&buf));
									sb_init(&buf);
								} else {
									sb_clear(&buf);
								}
							}
							s3 += len;
							if (!*s3)
								break;
							len = strcspn(s3, ifs);

							char s4[len + 1];
							strncpy(s4, s3, len);
							s4[len] = '\0';
							escape_sq(s4, &buf);
							s3 += len;
						}
					} else {  /* そのまま追加 */
						escape_dq(s2, &buf);
					}
				} else {
					ok = false;
				}
				free(s2);
				break;
			case '`':
				//TODO コマンド置換
			default:  default_case:
				sb_cappend(&buf, *s1);
				break;
		}
		if (*s1) s1++;
		s2 = strpbrk(s1, "\"'\\$`");
		if (!s2) {
			sb_append(&buf, s1);
			break;
		}
	}
	if (indq)
		error(0, 0, "%s: unclosed quotation", s);
	if (buf.length > 0) {
		pl_append(result, sb_tostr(&buf));
	} else {
		sb_destroy(&buf);
	}
	free(s);
	return ok;
}

/* '$' で始まるパラメータ・コマンド置換を解釈し、展開結果を返す。
 * src:    パラメータ置換を表す '$' の次の文字へのポインタのポインタ。
 *         置換が成功すると、*s は置換すべき部分文字列の最後の文字を指す。
 * 戻り値: 新しく malloc したパラメータ置換の結果の文字列。エラーなら NULL。 */
static char *expand_param(char **src)
{
	char *s = *src;
	char *ss, *result;
	char temp[2] = "$";

	assert(s[-1] == '$');
	switch (*s) {
		case '{':
			ss = skip_with_quote(s + 1, "}");
			if (*ss != '}') {
				error(0, 0, "$%s: missing `}'", s);
				*src = ss - 1;
				return NULL;
			}
			s++;
			*src = ss;
			//TODO { } 内の特殊構文
			ss = xstrndup(s, ss - s);
			result = getenv(ss);  // XXX
			free(ss);
			return xstrdup(result ? result : "");
		case '(':
			ss = get_comsub_code_p(src);
			result = subst_command(ss);
			free(ss);
			return result;

			//TODO ? や _ などの一文字のパラメータ
		case '\0':
		default:
			if (isalpha(*s)) {
				ss = s;
				while (isalpha(*++ss) || *ss == '_');
				*src = ss - 1;
				ss = xstrndup(s, ss - s);
				result = getenv(ss);  // XXX
				free(ss);
				return xstrdup(result ? result : "");
			}
			*src = s - 1;
			return xstrdup(temp);
	}
}

/* 括弧 ( ) で囲んだコマンドの閉じ括弧を探す。
 * src:    開き括弧で始まる文字列へのポインタのポインタ。
 *         関数が返るとき、*src に閉じ括弧へのポインタが入る。
 * 戻り値: 新しく malloc した、括弧の間にあるコマンドの文字列。 */
static char *get_comsub_code_p(char **src)
{
	char *s = *src;
	char *init;
	int count = 0;

	assert(*s == '(');
	s++;
	init = s;
	while (*s) {
		s = skip_with_quote(s, "()");
		switch (*s) {
			case ')':
				if (--count < 0)
					goto end;
				break;
			case '(':
				count++;
				break;
		}
		s++;
	}
end:
	*src = s;
	return xstrndup(init, s - init);
}

/* ` ` で囲んだコマンドの閉じ ` を探す。
 * src:    開き ` で始まる文字列へのポインタのポインタ。
 *         関数が返るとき、*src に閉じ ` へのポインタが入る。
 * 戻り値: 新しく malloc した、` の間にあるコマンドの文字列。
 *         バックスラッシュエスケープは取り除いてある。 */
static char *get_comsub_code_bq(char **src);

/* glob を配列 *ss の各文字列に対して行い、全ての結果を *result に入れる。
 * *ss の各文字列は free されるか *result に入る。
 * ss:     free 可能な文字列の NULL 終端配列へのポインタ。
 * 戻り値: エラーがなければ true、エラーがあれば (エラーを表示して) false。 */
static bool do_glob(char **ss, struct plist *result)
{
	glob_t gbuf;
	bool ok = true;
	while (*ss) {
		char *s = unescape_for_glob(*ss);
		switch (glob(s, 0, NULL, &gbuf)) {
			case GLOB_NOSPACE:  case GLOB_ABORTED:  default:
				error(0, 0, "%s: glob error", *ss);
				free(*ss);
				ok = false;
				break;
			case GLOB_NOMATCH:
				assert(gbuf.gl_pathc == 0);
				pl_append(result, unescape(*ss));
				break;
			case 0:
				free(*ss);
				break;
		}
		free(s);
#if 1
		for (size_t i = 0; i < gbuf.gl_pathc; i++)
			pl_append(result, xstrdup(gbuf.gl_pathv[i]));
		globfree(&gbuf);
#else
		/* This is dangerous because gbuf.gl_pathv might not be 'free'able. */
		if (gbuf.gl_pathc > 0)
			pl_anappend(result, gbuf.gl_pathv, gbuf.gl_pathc);
		free(gbuf.gl_pathv);
#endif
		ss++;
	}
	return ok;
}

/* 引用符を削除し、通常の文字列に戻す。
 * バックスラッシュエスケープはそのまま残る。引用符の中にあった * や [ は、
 * glob で解釈されないようにバックスラッシュでエスケープする。
 * 戻り値: 新しく malloc した、s の展開結果。 */
static char *unescape_for_glob(const char *s)
{
	enum { NORM, INSQ, INDQ, } state = NORM;
	struct strbuf buf;

	sb_init(&buf);
	while (*s) {
		switch (*s) {
			case '"':
				switch (state) {
					case NORM:  state = INDQ;  break;
					case INDQ:  state = NORM;  break;
					case INSQ:  goto default_case;
				}
				break;
			case '\'':
				switch (state) {
					case NORM:  state = INSQ;  break;
					case INSQ:  state = NORM;  break;
					case INDQ:  goto default_case;
				}
				break;
			case '\\':
				if (state == INSQ) {
					sb_cappend(&buf, '\\');
					goto default_case;
				} else if (!*++s) {
					sb_cappend(&buf, '\\');
					break;
				} else if (state == NORM || *s == '\\') {
					sb_cappend(&buf, '\\');
					sb_cappend(&buf, *s);
					break;
				} else if (*s == '"' || *s == '`' || *s == '$') {
					sb_cappend(&buf, *s);
					break;
				}
				goto default_case;
			case '*':  case '?':  case '[':
				if (state != NORM)
					sb_cappend(&buf, '\\');
				goto default_case;
			default:  default_case:
				sb_cappend(&buf, *s);
				break;
		}
		if (*s) s++;
	}
	return sb_tostr(&buf);
}

/* 引用符とバックスラッシュエスケープを削除する。
 * s:      free 可能な文字列。
 * 戻り値: 引用符とエスケープを削除した結果。
 * s は unescape 内で free され、新しく malloc した文字列で結果が返る。 */
static char *unescape(char *const s)
{
	char *ss = strpbrk(s, "\"'\\");
	bool indq = false;
	struct strbuf buf;

	if (!ss) return s;
	sb_init(&buf);
	sb_nappend(&buf, s, ss - s);
	while (*ss) {
		switch (*ss) {
			case '"':
				indq = !indq;
				break;
			case '\'':
				if (!indq) {
					char *end = skip_without_quote(++ss, "'");
					sb_nappend(&buf, ss, end - ss);
					ss = end;
				} else {
					goto default_case;
				}
				break;
			case '\\':
				if (!*++ss) {
					sb_cappend(&buf, '\\');
					break;
				} else if (indq && *ss != '\\'
						&& *ss != '"' && *ss != '`' && *ss != '$') {
					sb_cappend(&buf, '\\');
					goto default_case;
				}
				goto default_case;
			default:  default_case:
				sb_cappend(&buf, *ss);
				break;
		}
		ss++;
	}
	free(s);
	return sb_tostr(&buf);
}

/* 文字列 s を引用符 ' で囲む。ただし、文字列に ' 自身が含まれる場合は
 * \ でエスケープする。結果は buf に追加する。buf は初期化してから渡すこと。
 *   例)  abc"def'ghi  ->  'abcdef'\''ghi'  */
void escape_sq(const char *s, struct strbuf *buf)
{
	sb_cappend(buf, '\'');
	while (*s) {
		if (*s == '\'')
			sb_append(buf, "'\\''");
		else
			sb_cappend(buf, *s);
		s++;
	}
	sb_cappend(buf, '\'');
}

/* 文字列 s に含まれる " ` \ $ を \ でエスケープしつつ buf に追加する。
 * buf は初期化してから渡すこと。
 *   例)  abc"def'ghi`jkl\mno  ->  abc\"def'ghi\`jkl\\mno  */
void escape_dq(const char *s, struct strbuf *buf)
{
	while (*s) {
		switch (*s) {
			case '"':  case '`':  case '$':  case '\\':
				sb_cappend(buf, '\\');
				/* falls thru! */
			default:
				sb_cappend(buf, *s);
				break;
		}
		s++;
	}
}
