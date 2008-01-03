/* Yash: yet another shell */
/* expand.c: functions for command line expansion */
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
#include <error.h>
#include <fcntl.h>
#include <fnmatch.h>
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
 *  4. パラメータ/数式展開・コマンド置換
 *  5. 単語分割 (IFS)
 *  6. パス名展開 (glob)
 *  7. エスケープ・引用符の削除
 *
 * 各関数の呼出しツリー:
 *  expand_line/expand_single
 *   \_ expand_arg
 *       \_ expand_brace
 *       \_ expand_tilde
 *       \_ expand_subst
 *       |   \_ expand_param
 *       |   |   \_ expand_param2
 *       |   |   \_ get_comsub_code_p
 *       |   \_ add_splitting
 *       |   \_ escape_dq
 *       \_ do_glob
 *           \_ unescape_for_glob
 */

bool expand_line(char *const *args, int *argc, char ***argv);
char *expand_single(const char *arg);
char *expand_word(const char *s);
static bool expand_arg(const char *s, struct plist *argv)
	__attribute__((nonnull));
static bool expand_brace(char *s, struct plist *result)
	__attribute__((nonnull));
static bool expand_subst(char *s, struct plist *result, bool split)
	__attribute__((nonnull));
static char *expand_param(char **src, bool indq)
	__attribute__((nonnull));
static char *expand_param2(const char *s, bool indq)
	__attribute__((nonnull));
static char *matchhead(const char *s, char *pat, bool matchlong)
	__attribute__((nonnull));
static char *matchtail(const char *s, char *pat, bool matchlong)
	__attribute__((nonnull));
static char *get_comsub_code_p(char **src)
	__attribute__((nonnull));
static char *get_comsub_code_bq(char **src)
	__attribute__((nonnull));
void add_splitting(const char *str, struct strbuf *buf, struct plist *list,
		const char *ifs, const char *q);
static bool do_glob(char **ss, struct plist *result)
	__attribute__((nonnull));
static char *unescape_for_glob(const char *s)
	__attribute__((malloc));
static char *unescape(char *s)
	__attribute__((malloc));
void escape_sq(const char *s, struct strbuf *buf);
void escape_bs(const char *s, const char *q, struct strbuf *buf);


/* コマンドライン上の各種展開を行う。
 * args は入力引数であり、展開される文字列への配列である。これは NULL でもよい。
 * argc, argv は出力引数であり、各ポインタのアドレスにそれぞれ
 * 展開結果の個数・展開結果への配列へのポインタが入る。(エラーの有無に拘らず)
 * *argv は新しく malloc した文字列への新しく malloc した配列である。
 * args の内容は一切変更しない。
 * 成功すると true 、失敗すると false を返す。失敗時はエラーを出力する。 */
bool expand_line(char *const *args, int *argc, char ***argv)
{
	struct plist alist;
	bool ok = true;

	pl_init(&alist);
	if (!args) goto end;

	while (ok && *args) {
		ok = expand_arg(*args, &alist);
		args++;
	}

end:
	*argc = alist.length;
	*argv = (char **) pl_toary(&alist);
	return ok;
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
		recfree(pl_toary(&alist), free);
		return NULL;
	}

	char *result = alist.contents[0];
	pl_destroy(&alist);
	return result;
}

/* 一つのフィールドに対してチルダ展開・パラメータ/数式展開・コマンド置換を行う。
 * 戻り値: 新しく malloc した、展開の結果。エラーなら NULL。 */
char *expand_word(const char *s)
{
	char *s2;
	struct plist list;

	if (s[0] != '~' || !(s2 = expand_tilde(s)))
		s2 = xstrdup(s);
	pl_init(&list);
	if (!expand_subst(s2, &list, false)) {
		recfree(pl_toary(&list), free);
		return NULL;
	}
	assert(list.length <= 1);
	s2 = list.contents[0];
	pl_destroy(&list);
	return s2;
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

	for (size_t i = 0; ok && i < temp2.length; i++) { /* そしてパラメータ展開 */
		ok = expand_subst(temp2.contents[i], &temp1, true);
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

static bool noescape;

/* パラメータ展開・コマンド置換、そして split が true なら単語分割を行い、
 * 結果を result に追加する。
 * 引用符 (" と ') やバックスラッシュエスケープはそのまま展開結果に残る。
 * s:      free 可能な、展開・分割を行う対象の文字列。
 * split:  単語分割を行うかどうか。
 * エラー時はエラーメッセージを出力して false を返す。
 * ただし、エラーの場合でも途中結果が result に入っているかもしれない。
 * s は (展開や分割の結果が元と同じ時) *result に追加されるか、free される。 */
/* IFS による単語分割は、パラメータ展開の結果内においてのみ実行する。
 * これはセキュリティ上の理由による。 */
static bool expand_subst(char *const s, struct plist *result, bool split)
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
				noescape = false;
				s2 = expand_param(&s1, indq);
				goto append_s2;
			case '`':
				{
					char *code = get_comsub_code_bq(&s1);
					s2 = ok ? exec_and_read(code, true) : NULL;
					free(code);
					noescape = false;
				}
				goto append_s2;
append_s2:
				if (s2) {
//					error(0, 0, "DEBUG: %d %d appending %s",
//							(int) (split && !indq), (int) noescape, s2);
					if (split && !indq) {  /* 単語分割をしつつ追加 */
						const char *ifs = getenv("IFS");
						if (!ifs)
							ifs = " \t\n";
						add_splitting(s2, &buf, result, ifs,
								noescape ? NULL : "\"'\\");
					} else {  /* そのまま追加 */
						if (noescape)
							sb_append(&buf, s2);
						else
							escape_bs(s2, "\"\\", &buf);
					}
				} else {
					ok = false;
				}
				free(s2);
				break;
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
 * indq:   このコマンド置換が " 引用符の中にあるかどうか。
 * 戻り値: 新しく malloc したパラメータ置換の結果の文字列。エラーなら NULL。 */
static char *expand_param(char **src, bool indq)
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
			ss = xstrndup(s, ss - s);
			result = expand_param2(ss, indq);
			free(ss);
			return result;
		case '(':
			ss = get_comsub_code_p(src);
			result = exec_and_read(ss, true);
			free(ss);
			return result;

			//TODO ? や _ などの一文字のパラメータ
		case '\0':
		default:
			if (xisalpha(*s)) {
				ss = s + 1;
				// TODO 変数として利用可能な文字の判定を正確に
				while (xisalnum(*ss) || *ss == '_') ss++;
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

/* 括弧 ${ } で囲んだパラメータの内容を展開する
 * s:      ${ と } の間の文字列。
 * indq:   このコマンド置換が " 引用符の中にあるかどうか。
 * 戻り値: 新しく malloc した、パラメータの展開結果。エラーなら NULL。 */
/* 書式:
 *    ${param-word}   param が未定義なら代わりに word を返す。
 *    ${param+word}   param が未定義でなければ代わりに word を返す。
 *    ${param=word}   param が未定義なら word を param に代入する事を試みる。
 *                    最終的な param の値を返す。
 *    ${param?word}   param が未定義なら word をエラーとして出力し終了する。
 *                    word が無ければデフォルトのエラーメッセージを出す。
 *                    ただし対話的シェルはエラーでも終了しない。
 *  param の直後に ':' を付けて ${param:-word} のようにすると、「未定義なら」
 *  ではなく「未定義か空文字列なら」の条件で判定する。
 *    ${#param}       param の内容の文字数を返す。
 *                    param が * か @ なら、yash では XXX を返す。
 *    ${param#word}   word を glob パタンとみなして param の内容の先頭部分に
 *                    マッチさせ、一致した部分を削除して返す。(最短一致)
 *    ${param##word}  同様にマッチさせ、一致部分を削除して返す。(最長一致)
 *    ${param%word}   先頭ではなく末尾にマッチさせ、削除して返す。(最短一致)
 *    ${param%%word}  同様にマッチさせ、一致部分を削除して返す。(最長一致)
 *  各 word は、使う前にチルダ展開・パラメータ/数式展開・コマンド置換を行う。
 *  word を使わない場合は展開・置換は行わない。
 *  以下は、POSIX にない書式である (基本的に bash とおなじ):  XXX 未実装
 *    ${prefix*}      名前が prefix で始まる全ての変数に展開する。
 *                    各変数は IFS の最初の文字で区切る。
 *    ${!prefix*}     同上
 *    ${param:off:len}  param の内容の off 文字目から最大 len 文字を返す。
 *                    off, len は数式展開する。len は省略可能。param が @ なら、
 *                    XXX
 *    ${param/pat/str}  pat を glob パタンとみなして param の内容にマッチさせ、
 *                    最初に一致した部分を str に置換して返す。pat の直前に #
 *                    があれば先頭部分のみ、% があれば末尾部分のみマッチさせる。
 *                    pat, str は word と同じように展開する。str は省略可能。
 *                    param が * か @ なら、XXX
 *  param の直前に ! がある場合、param を名前とする変数の内容ではなくて、
 *  param を名前とする変数の内容を名前とする変数の内容を用いる。ただし、
 *  ${!prefix*} は例外である。
 */
static char *expand_param2(const char *s, bool indq)
{
	const char *start = s;
	bool count, indir, colon, matchlong;

	count = (*s == '#');
	if (count) s++;
	indir = (*s == '!');
	if (indir) s++;
	
	const char *pstart = s;
	// TODO 一文字の記号のパラメータ
	while (xisalnum(*s) || *s == '_') s++;

	char param[s - pstart + 1];
	strncpy(param, pstart, s - pstart);
	param[s - pstart] = '\0';

	char *result = getenv(param);
	char *word1, *word2;

	// TODO ${!prefix*} パタン
	if (result && indir)
		result = getenv(result);
	if (count) {
		if (*s)
			goto err;
		return mprintf("%zu", strlen(result ? result : ""));
	}
	colon = (*s == ':');
	if (colon) s++;
	switch (*s++) {
		case '\0':
			if (colon)
				goto err;
			break;
		case '-':
			if (!result || (colon && !*result))
				goto expand_escape_return;
			break;
		case '+':
			if (result && (!colon || *result))
				goto expand_escape_return;
			break;
		case '=':
			if (!result || (colon && !*result)) {
				if (!indir && xisalpha(*start)) {
					word1 = unescape(expand_word(s));
					if (setenv(param, word1, true) < 0) {
						error(0, 0, "${%s}: assignment failed", start);
						free(word1);
						return NULL;
					}
					result = getenv(param);
				}
			}
			break;
		case '?':
			if (!result || (colon && !*result)) {
				if (*s) {
					word1 = unescape(expand_word(s));
					error(is_interactive ? 0 : EXIT_FAILURE, 0,
							"%s: %s", param, word1);
					free(word1);
				} else {
					error(is_interactive ? 0 : EXIT_FAILURE, 0,
							result ? "%s: parameter null"
							       : "%s: parameter not set",
							param);
				}
				return NULL;
			}
			break;
		case '#':
			matchlong = (*s == '#');
			if (matchlong) s++;
			word1 = expand_word(s);
			word2 = unescape_for_glob(word1);
			if (!word2) return NULL;
			return matchhead(result, word2, matchlong);
		case '%':
			matchlong = (*s == '%');
			if (matchlong) s++;
			word1 = expand_word(s);
			word2 = unescape_for_glob(word1);
			if (!word2) return NULL;
			return matchtail(result, word2, matchlong);
		default:
err:
			error(0, 0, "${%s}: bad substitution", start);
			return NULL;
	}
	return xstrdup(result ? result : "");

expand_escape_return:
	noescape = !indq || posixly_correct;
	result = expand_word(s);
	if (!noescape)
		result = unescape(result);
	return result;
}

/* pat を s の先頭部分にマッチさせ、一致した部分を取り除いた文字列を返す。
 * pat:       マッチさせる glob パタン。matchhead 内で free する。
 * matchlong: true なら最長一致、false なら最短一致。
 * 戻り値:    新しく malloc した、s の部分文字列。エラーなら NULL。 */
static char *matchhead(const char *s, char *pat, bool matchlong)
{
	size_t len = strlen(s);
	size_t i;
	char buf[len + 1];

	errno = 0;
	if (matchlong) {
		i = len;
		strcpy(buf, s);
		for (;;) {
			switch (fnmatch(pat, buf, 0)) {
				case 0:  /* 一致した */
					goto end;
				case FNM_NOMATCH:  /* 一致しなかった */
					break;
				default:  /* エラー */
					error(0, errno, "unexpected fnmatch error");
					free(pat);
					return NULL;
			}
			if (!i)
				goto end;
			buf[--i] = '\0';  /* buf の末尾の一文字を削る */
		}
	} else {
		i = 0;
		for (;;) {
			buf[i] = '\0';
			switch (fnmatch(pat, buf, 0)) {
				case 0:  /* 一致した */
					goto end;
				case FNM_NOMATCH:  /* 一致しなかった */
					break;
				default:  /* エラー */
					error(0, errno, "unexpected fnmatch error");
					free(pat);
					return NULL;
			}
			if (i == len) {
				i = 0;
				goto end;
			}
			buf[i] = s[i];
			i++;
		}
	}
end:
	free(pat);
	return xstrdup(s + i);
}

/* pat を s の末尾部分にマッチさせ、一致した部分を取り除いた文字列を返す。
 * pat:       マッチさせる glob パタン。matchtail 内で free する。
 * matchlong: true なら最長一致、false なら最短一致。
 * 戻り値:    新しく malloc した、s の部分文字列。エラーなら NULL。 */
static char *matchtail(const char *const s, char *pat, bool matchlong)
{
	size_t i;

	if (matchlong) {
		i = 0;
		while (s[i]) {
			switch (fnmatch(pat, s + i, 0)) {
				case 0:  /* 一致した */
					goto end;
				case FNM_NOMATCH:  /* 一致しなかった */
					break;
				default:  /* エラー */
					error(0, errno, "unexpected fnmatch error");
					free(pat);
					return NULL;
			}
			i++;
		}
	} else {
		i = strlen(s);
		for (;;) {
			switch (fnmatch(pat, s + i, 0)) {
				case 0:  /* 一致した */
					goto end;
				case FNM_NOMATCH:  /* 一致しなかった */
					break;
				default:  /* エラー */
					error(0, errno, "unexpected fnmatch error");
					free(pat);
					return NULL;
			}
			if (!i) {
				i = SIZE_MAX;
				goto end;
			}
			i--;
		}
	}
end:
	free(pat);
	return xstrndup(s, i);
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
static char *get_comsub_code_bq(char **src)
{
	struct strbuf buf;
	char *s = *src;

	assert(*s == '`');
	s++;
	sb_init(&buf);
	while (*s && *s != '`') {
		if (*s == '\\')
			s++;
		if (*s)
			sb_cappend(&buf, *s);
		s++;
	}
	*src = s;
	return sb_tostr(&buf);
}

/* ifs に基づいて str を単語分割し、結果を buf, list にいれる。
 * buf, list は予め初期化しておく必要がある (空である必要は無い)。
 * 単語を分割する度に pl_append(list, sb_tostr(buf)) を行い buf を再初期化する。
 * 最後の単語以外の単語は free 可能な文字列として list に入る。
 * 最後の単語は buf に入ったままになる。
 * q が非 NULL なら、q に入っている文字を escape_bs でエスケープする。 */
void add_splitting(const char *str, struct strbuf *buf, struct plist *list,
		const char *ifs, const char *q)
{
	for (;;) {
		size_t len = strspn(str, ifs);
		if (len > 0) {
			if (buf->length > 0) {
				pl_append(list, sb_tostr(buf));
				sb_init(buf);
			} else {
				sb_clear(buf);
			}
		}
		str += len;
		if (!*str)
			break;
		len = strcspn(str, ifs);

		char ss[len + 1];
		strncpy(ss, str, len);
		ss[len] = '\0';
		if (q)
			escape_bs(ss, q, buf);
		else
			sb_append(buf, ss);
		str += len;
	}
}

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
//		error(0, 0, "DEBUG: glob(%s)(%s)", *ss, s);
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

	if (!s) return NULL;
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
				} else if (*s != '"' && *s != '`' && *s != '$') {
					sb_cappend(&buf, '\\');
					sb_cappend(&buf, '\\');
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
	char *ss;
	bool indq = false;
	struct strbuf buf;

	if (!s) return NULL;
	ss = strpbrk(s, "\"'\\");
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

/* 文字列 s を \ でエスケープしつつ buf に追加する。
 * 文字列 s の文字のうち、文字列 q に含まれている文字をエスケープする。
 * buf は初期化してから渡すこと。
 *   例) escape_bs("$%&'()", "$'", buf)  ->  "\$%&\'()"   */
void escape_bs(const char *s, const char *q, struct strbuf *buf)
{
	while (*s) {
		if (strchr(q, *s))
			sb_cappend(buf, '\\');
		sb_cappend(buf, *s);
		s++;
	}
}
