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


#include "common.h"
#include <errno.h>
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
#include "variable.h"
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
 *   \_ expand_word/expand_word_splitting/expand_arg
 *       \_ expand_brace
 *       \_ expand_tilde  (in path.c)
 *       \_ expand_subst
 *       |   \_ expand_dollar
 *       |   |   \_ expand_param
 *       |   |   |  \_ expand_param2
 *       |   |   |     \_ matchhead/matchtail/matchandsubst
 *       |   |   \_ get_comsub_code_p
 *       |   \_ get_comsub_code_bq
 *       |   \_ append_maybe_splitting
 *       |      \_ append_splitting
 *       \_ do_glob
 *           \_ unescape_for_glob
 *
 * 非対話的シェルにおいて展開時にエラーが発生したら、シェルはそのまま終了する
 */

struct expand {
	struct strbuf *current;
	struct plist  *fields;
	bool           indq;
	bool           split;
};
struct expand_args {
	bool indq, count, indirect, formatcolon;
	const char *full, *param, *format;
};
enum substtype {
	substonce, substhead, substtail, substwhole, substall
};

static inline const char *get_ifs(void);
static inline bool append_maybe_splitting(
		char *result, struct expand *info, bool escape)
	__attribute__((nonnull(2)));
static char **expand_word_splitting(const char *s)
	__attribute__((nonnull,malloc));
static bool expand_arg(const char *s, struct plist *argv)
	__attribute__((nonnull));
static bool expand_brace(char *s, struct plist *result)
	__attribute__((nonnull));
static bool expand_subst(char *s, struct plist *result, bool split)
	__attribute__((nonnull));
static bool expand_dollar(char **srcp, struct expand *restrict info)
	__attribute__((nonnull));
static bool expand_param(const char *s, struct expand *restrict info)
	__attribute__((nonnull));
static char *expand_param2(struct expand_args *args, const char *value)
	__attribute__((nonnull,malloc));
static char *matchhead(const char *s, char *pat)
	__attribute__((nonnull,malloc));
static char *matchtail(const char *s, char *pat)
	__attribute__((nonnull,malloc));
static char *matchandsubst(
		const char *s, char *pat, char *str, enum substtype type)
	__attribute__((nonnull(1,2),malloc));
static char *get_comsub_code_p(char **src)
	__attribute__((nonnull,malloc));
static char *get_comsub_code_bq(char **src)
	__attribute__((nonnull,malloc));
static bool do_glob(char **ss, struct plist *result)
	__attribute__((nonnull));
static char *unescape_for_glob(const char *s)
	__attribute__((malloc));


/* IFS 環境変数を取得する。環境変数がなければ " \t\n" を返す。 */
static inline const char *get_ifs(void)
{
	const char *ifs = getvar(VAR_IFS);
	if (!ifs) ifs = " \t\n";
	return ifs;
}

/* expand_subst 内で使う補助関数。result が非 NULL ならそれを info の current
 * に追加する。場合によっては単語分割をする。result はこの関数内で free する。
 * escape: 単語を追加するときにエスケープするかどうか。
 * 戻り値: result が非 NULL だったかどうか。 */
static inline bool append_maybe_splitting(
		char *result, struct expand *info, bool escape)
{
	if (result) {
		if (info->split && !info->indq) { /* 単語分割しつつ追加 */
			append_splitting(result, info->current, info->fields,
					get_ifs(), escape ? "\"'\\" : NULL);
		} else {  /* そのまま追加 */
			if (escape)
				escape_bs(result, "\"\\", info->current);
			else
				sb_append(info->current, result);
		}
		free(result);
		return true;
	} else {
		return false;
	}
}


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
	if (!expand_arg(arg, &alist)) {
		recfree(pl_toary(&alist), free);
		return NULL;
	}
	if (alist.length != 1) {
		xerror(is_interactive_now ? 0 : EXIT_FAILURE,
				0, "%s: parameter expanded to multiple words", arg);
		recfree(pl_toary(&alist), free);
		return NULL;
	}

	char *result = alist.contents[0];
	pl_destroy(&alist);
	return result;
}

/* 一つのフィールドに対してチルダ展開・パラメータ/数式展開・コマンド置換を行う。
 * パス名展開・引用符除去は行わない。
 * multitilde: true なら変数代入用のチルダ展開を行う。false なら普通に展開する。
 * 戻り値: 新しく malloc した、展開の結果。エラーなら NULL。 */
char *expand_word(const char *s, bool multitilde)
{
	char *s2;
	struct plist list;

	if (!multitilde) {
		if (s[0] != '~' || !(s2 = expand_tilde(s)))
			s2 = xstrdup(s);
	} else {
		s2 = expand_tilde_multiple(s);
	}
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

/* 一つの引数に対してチルダ展開とパラメータ展開を行い結果を配列で返す。
 * 一つの引数が単語分割によって複数の単語に分かれたり 0 個になったりすることが
 * ある。
 * 成功すると結果の配列を、失敗すると NULL を返す。失敗時はエラーを出力する。 */
static char **expand_word_splitting(const char *s)
{
	char *temp = (s[0] == '~') ? expand_tilde(s) : xstrdup(s);
	struct plist results;
	pl_init(&results);
	if (expand_subst(temp, &results, true)) {
		return (char **) pl_toary(&results);
	} else {
		recfree(pl_toary(&results), free);
		return NULL;
	}
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
	char *s1 = s, *s2 = strpbrk(s, "\"'\\$`");
	struct strbuf buf;
	struct expand info = {
		.current = &buf,
		.fields = result,
		.indq = false,
		.split = split,
	};

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
				info.indq = !info.indq;
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
				ok &= expand_dollar(&s1, &info);
				break;
			case '`':
				{
					char *code = get_comsub_code_bq(&s1);
					s2 = ok ? exec_and_read(code, true) : NULL;
					free(code);
					ok &= append_maybe_splitting(s2, &info, true);
				}
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
	if (info.indq)
		xerror(0, 0, "%s: unclosed quotation", s);
	if (buf.length > 0) {
		pl_append(result, sb_tostr(&buf));
	} else {
		sb_destroy(&buf);
	}
	free(s);
	return ok;
}

/* '$' で始まるパラメータ展開・コマンド置換を解釈し、結果を info に入れる。
 * srcp:   展開する '$' の次の文字へのポインタのポインタ。
 *         成功すると、*srcp は置換すべき部分文字列の最後の文字を指す。
 * 戻り値: 成功なら true、エラーなら NULL。 */
static bool expand_dollar(char **srcp, struct expand *restrict info)
{
	char *s = *srcp;
	char *ss, *result;

	assert(s[-1] == '$');
	switch (*s) {
		case '{':
			ss = skip_with_quote(s + 1, "}");
			if (*ss != '}') {
				xerror(0, 0, "$%s: missing `%c'", s, '}');
				*srcp = ss - 1;
				return false;
			} else {
				s++;
				*srcp = ss;
				char inside[ss - s + 1];
				strncpy(inside, s, ss - s);
				inside[ss - s] = '\0';
				return expand_param(inside, info);
			}
		case '(':
			ss = get_comsub_code_p(srcp);
			result = exec_and_read(ss, true);
			free(ss);
			return append_maybe_splitting(result, info, true);
		case '\0':
		default:
			if (is_special_parameter_char(*s) || xisdigit(*s)) {
				char name[] = { *s, '\0' };
				return expand_param(name, info);
			} else if (is_name_char(*s)) {
				ss = s + 1;
				while (is_name_char(*ss)) ss++;
				*srcp = ss - 1;
				char name[ss - s + 1];
				strncpy(name, s, ss - s);
				name[ss - s] = '\0';
				return expand_param(name, info);
			} else {
				*srcp = s - 1;
				return xstrdup("$");
			}
	}
}

/* 括弧 ${ } で囲んだパラメータの内容を展開する
 * s:      ${ と } の間の文字列。
 * info:   展開に関する情報へのポインタ。
 * 戻り値: 成功なら true、エラーなら false。 */
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
 *                    param が * か @ なら、yash では位置パラメータの数を返す。
 *    ${param#word}   word を glob パタンとみなして param の内容の先頭部分に
 *                    マッチさせ、一致した部分を削除して返す。(最短一致)
 *    ${param##word}  同様にマッチさせ、一致部分を削除して返す。(最長一致)
 *    ${param%word}   先頭ではなく末尾にマッチさせ、削除して返す。(最短一致)
 *    ${param%%word}  同様にマッチさせ、一致部分を削除して返す。(最長一致)
 *  これら四つは、param が * か @ か配列全体なら各要素に対して一致を行う。
 *  各 word は、使う前にチルダ展開・パラメータ/数式展開・コマンド置換を行う。
 *  word を使わない場合は展開・置換は行わない。
 *  以下は、POSIX にない書式である (基本的に bash とおなじ):
 *    ${prefix*}      名前が prefix で始まる全ての変数に展開する。
 *                    各変数は IFS の最初の文字で区切る。
 *    ${!prefix*}     同上
 *    ${param:off:len}  param の内容の off 文字目から最大 len 文字を返す。
 *                    off, len は数式展開する。len は省略可能。param が @ か
 *                    配列全体なら、部分配列を取り出す。
 *                    XXX 未実装
 *    ${param/pat/str}  pat を glob パタンとみなして param の内容にマッチさせ、
 *                    最初に一致した部分を str に置換して返す。pat の直前に #
 *                    があれば先頭部分のみ、% があれば末尾部分のみマッチさせる。
 *                    pat, str は word と同じように展開する。str は省略可能。
 *                    param が * か @ か配列全体なら、各要素ごとに置換する。
 *                    一致は常に最長一致とする。
 *    ${param//pat/str}  最初だけでなく全ての一致を同様に置換する。
 *    ${param:/pat/str}  pat が param の内容全体に一致するときのみ置換する。
 *  param の直前に ! がある場合、param を名前とする変数の内容ではなくて、
 *  param を名前とする変数の内容を名前とする変数の内容を用いる。ただし、
 *  ${!prefix*} は例外である。またこの記法は配列に対しては使えない。
 */
static bool expand_param(const char *s, struct expand *restrict info)
{
	struct expand_args args;
	args.full = s;
	args.indq = info->indq;

	args.count = (s[0] == '#' && s[1]);
	if (args.count) s++;
	args.indirect = (s[0] == '!');
	if (args.indirect) s++;
	
	const char *pstart = s;
	if (is_special_parameter_char(*s)) {
		s++;
	} else {
		while (xisalnum(*s) || *s == '_') s++;
	}
	if (pstart == s && args.indirect) {
		/* この場合 '!' は indirect のしるしではなく変数名そのもの */
		pstart--;
		args.indirect = false;
		assert(*pstart == '!');
	}

	char param[s - pstart + 1];
	strncpy(param, pstart, s - pstart);
	param[s - pstart] = '\0';
	args.param = param;

	args.formatcolon = (*s == ':');
	if (args.formatcolon) {
		s++;
		if (!*s)
			goto syntax_error;
	}
	switch (*s) {
		case '\0': case '+':  case '-':  case '=':  case '?':
		case '#':  case '%':  case '/':
			break;
		default:
			goto syntax_error;
	}
	args.format = s;

	// TODO ${!prefix*} パタン, 配列
	bool separate = false, singlevalue;
	const char *const *values;
	const char *singlevalues[2];
	if (args.param[0] == '@' || args.param[0] == '*') {
		struct plist *list = getarray(NULL);
		values = (const char **) list->contents + 1;
		separate = (args.indq && args.param[0] == '@');
		singlevalue = false;
	} else {
		/* 配列でない変数は、要素が一つの配列のように扱う。 */
		singlevalues[0] = getvar(args.param);
		singlevalues[1] = NULL;
		values = singlevalues;
		separate = false;
		singlevalue = true;

		if (args.indirect && singlevalues[0])
			singlevalues[0] = getvar(singlevalues[0]);
	}
	assert(values != NULL);

	if (args.count) {
		if (args.format[0])
			goto syntax_error;
		sb_printf(info->current, "%zu",
				singlevalue
					? (values[0] ? strlen(values[0]) : (size_t) 0)
					: parylen((void **) values));
		return true;
	}

	struct plist rlist;
	char **rvalues;
	pl_init(&rlist);
	for (size_t i = 0; values[i]; i++) {
		char *evalue = expand_param2(&args, values[i]);
		if (!evalue) {
			recfree(pl_toary(&rlist), free);
			return false;
		}
		pl_append(&rlist, evalue);
	}
	rvalues = (char **) pl_toary(&rlist);

	bool raw = false;
	bool isempty =
		!values[0] || (args.formatcolon && !values[0][0] && !values[1]); 
	switch (args.format[0]) {
		case '-':  case '+':
			if (isempty == (args.format[0] == '-')) {
				recfree((void **) rvalues, free);
				rvalues = expand_word_splitting(args.format + 1);
				if (!rvalues)
					return NULL;
				separate = false;
				raw = !args.indq || posixly_correct;
				if (!raw)
					for (size_t i = 0; rvalues[i]; i++)
						rvalues[i] = unescape(rvalues[i]);
			}
			break;
		case '=':
			if (isempty) {
				if (singlevalue && !args.indirect
						&& xisalpha(args.full[0]) && is_name(args.param)) {
					char *word1 = unescape(expand_word(args.format + 1, false));
					recfree((void **) rvalues, free);
					if (word1 && !setvar(args.param, word1, false)) {
						free(word1);
						return false;
					}
					return word1;
				} else {  /* 配列などは代入できない */
					xerror(0, 0, "${%s}: parameter cannot be "
							"assigned to in this way", args.full);
					recfree((void **) rvalues, free);
					return false;
				}
			}
			break;
		case '?':
			if (isempty) {
				if (args.format[1]) {
					char *word1 = unescape(expand_word(args.format + 1, false));
					if (word1) {
						xerror(is_interactive_now ? 0 : EXIT_FAILURE,
								0, "%s: %s", param, word1);
						free(word1);
					}
				} else {
					xerror(is_interactive_now ? 0 : EXIT_FAILURE,
							0,
							values[0] ? "%s: parameter null"
							          : "%s: parameter not set",
							args.param);
				}
				recfree((void **) rvalues, free);
				return false;
			}
			break;
	}

	if (info->split && (separate || raw)) {
		/* 配列の要素ごとに単語分割して追加 */
		char **rv = rvalues;
		if (*rv) {
			for (;;) {
				if (raw)
					sb_append(info->current, *rv);
				else
					escape_bs(*rv, args.indq ? "\"\\" : "\"\\'", info->current);
				free(*rv);
				rv++;
				if (!*rv) break;
				if (args.indq) sb_cappend(info->current, '"');
				pl_append(info->fields, sb_tostr(info->current));
				sb_init(info->current);
				if (args.indq) sb_cappend(info->current, '"');
			}
		}
		free(rvalues);
		return true;
	} else if (!rvalues[0]) {
		free(rvalues);
		return true;
	} else {
		char *result;
		if (!rvalues[1]) {
			result = rvalues[0];
			free(rvalues);
		} else {
			char sep[2] = { get_ifs()[0], '\0' };
			/* IFS が "" なら sep も "" となる。これは正しい動作である。 */
			result = strjoin(-1, rvalues, sep);
			recfree((void **) rvalues, free);
		}
		return append_maybe_splitting(result, info, !raw);
	}

syntax_error:
	xerror(is_interactive_now ? 0 : EXIT_FAILURE,
			0, "${%s}: bad substitution", args.full);
	return false;
}

/* パラメータの値を操作する書式を適用する。
 * info:   全ての値を設定してある struct expand_args。
 * 戻り値: 新しく malloc した、書式の適用結果。エラーなら NULL。 */
/* 書式の詳細は expand_param の説明を参照 */
static char *expand_param2(struct expand_args *args, const char *value)
{
	const char *format = args->format;
	char *word1, *word2;
	bool matchlong;

	switch (*format++) {
		/* '+', '-', '=', '?' の各記号は expand_param で処理する */
		case '#':
			matchlong = (format[0] == '#');
			if (matchlong) format++;
			word1 = unescape_for_glob(expand_word(format, false));
			if (!word1) return NULL;
			return matchlong ? matchandsubst(value, word1, NULL, substhead)
			                 : matchhead(value, word1);
		case '%':
			matchlong = (format[0] == '%');
			if (matchlong) format++;
			word1 = unescape_for_glob(expand_word(format, false));
			if (!word1) return NULL;
			return matchlong ? matchandsubst(value, word1, NULL, substtail)
			                 : matchtail(value, word1);
		case '/':;
			bool sall = (format[0] == '/');
			if (sall) format++;
			bool headonly = (format[0] == '#'), tailonly = (format[0] == '%');
			if (headonly || tailonly) format++;

			size_t patlen = skip_with_quote(format, "/") - format;
			char pat[patlen + 1];
			strncpy(pat, format, patlen);
			pat[patlen] = '\0';
			word1 = unescape_for_glob(expand_word(pat, false));
			if (!word1) return NULL;
			if (format[patlen] != '/') {
				word2 = NULL;
			} else {
				word2 = unescape(expand_word(format + patlen + 1, false));
				if (!word2) {
					free(word1);
					return NULL;
				}
			}
			return matchandsubst(value, word1, word2,
					args->formatcolon ? substwhole :
					sall              ? substall :
					headonly          ? substhead :
					tailonly          ? substtail :
					                    substonce);
	}
	return xstrdup(value ? value : "");
}

/* pat を s の先頭部分にマッチさせ、最短で一致した部分を取り除いた文字列を返す。
 * pat:       マッチさせる glob パタン。matchhead 内で free する。
 * 戻り値:    新しく malloc した、s の部分文字列。エラーなら NULL。 */
static char *matchhead(const char *s, char *pat)
{
	size_t len = strlen(s);
	size_t i = 0;
	char buf[len + 1];

	for (;;) {
		buf[i] = '\0';
		switch (fnmatch(pat, buf, 0)) {
			case 0:  /* 一致した */
				goto end;
			case FNM_NOMATCH:  /* 一致しなかった */
				break;
			default:  /* エラー */
				xerror(0, 0, "unexpected fnmatch error");
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
end:
	free(pat);
	return xstrdup(s + i);
}

/* pat を s の末尾部分にマッチさせ、最短で一致した部分を取り除いた文字列を返す。
 * pat:       マッチさせる glob パタン。matchtail 内で free する。
 * 戻り値:    新しく malloc した、s の部分文字列。エラーなら NULL。 */
static char *matchtail(const char *const s, char *pat)
{
	size_t i = strlen(s);
	for (;;) {
		switch (fnmatch(pat, s + i, 0)) {
			case 0:  /* 一致した */
				goto end;
			case FNM_NOMATCH:  /* 一致しなかった */
				break;
			default:  /* エラー */
				xerror(0, 0, "unexpected fnmatch error");
				free(pat);
				return NULL;
		}
		if (!i) {
			i = SIZE_MAX;
			goto end;
		}
		i--;
	}
end:
	free(pat);
	return xstrndup(s, i);
}

/* pat を s にマッチさせ、一致した部分を str に置換して返す。
 * pat:    マッチさせる glob パタン。この関数内で free する。
 * str:    置換する文字列。この関数内で free する。NULL なら空文字列に置換。
 * type:   置換のしかた。
 *           substonce  = 最初の一致だけ置換。
 *           substhead  = s の先頭部分の最初の一致だけ置換。
 *           substtail  = s の末尾部分の最初の一致だけ置換。
 *           substwhole = s 全体に一致した場合だけ置換。
 *           substall   = 一致した部分を全て置換。
 * 戻り値: 新しく malloc した、置換結果。エラーなら NULL。 */
static char *matchandsubst(
		const char *s, char *pat, char *str, enum substtype type)
{
	if (type == substwhole) {
		switch (fnmatch(pat, s, 0)) {
			case 0:  /* 一致した */
				free(pat);
				return str ? str : xstrdup("");
			case FNM_NOMATCH:  /* 一致しなかった */
				goto nomatch;
			default:  /* エラー */
				goto err;
		}
	} else if (type == substtail) {
		size_t i = 0;
		while (s[i]) {
			switch (fnmatch(pat, s + i, 0)) {
				case 0:  /* 一致した */
					free(pat);
					size_t slen = str ? strlen(str) : 0;
					char *result = xmalloc(i + slen + 1);
					strncpy(result, s, i);
					strcpy(result + i, str ? str : "");
					free(str);
					return result;
				case FNM_NOMATCH:  /* 一致しなかった */
					break;
				default:  /* エラー */
					goto err;
			}
			i++;
		}
		goto nomatch;
	} else if (type == substonce || type == substhead) {
		size_t len = strlen(s);
		char buf[len + 1];
		for (size_t i = 0; i <= len; i++) {
			size_t l = len - i;
			strcpy(buf, s + i);
			for (;;) {
				switch (fnmatch(pat, buf, 0)) {
					case 0:  /* 一致した */
						free(pat);
						size_t slen = str ? strlen(str) : 0;
						char *result = xmalloc(len - l + slen + 1);
						strncpy(result, s, i);
						if (str) {
							strcpy(result + i, str);
							free(str);
						}
						strcpy(result + i + slen, s + i + l);
						return result;
					case FNM_NOMATCH:  /* 一致しなかった */
						break;
					default:  /* エラー */
						goto err;
				}
				if (!l)
					break;
				buf[--l] = '\0';  /* buf の末尾の一文字を削る */
			}
			if (type == substhead)
				goto nomatch;
		}
		goto nomatch;
	} else {
		assert(type == substall);

		size_t len = strlen(s);
		char buf[len + 1];
		struct strbuf result;
		sb_init(&result);
		for (size_t i = 0; i <= len; ) {
			size_t l = len - i;
			strcpy(buf, s + i);
			for (;;) {
				switch (fnmatch(pat, buf, 0)) {
					case 0:  /* 一致した */
						if (str)
							sb_append(&result, str);
						i += l;
						goto cont;
					case FNM_NOMATCH:  /* 一致しなかった */
						break;
					default:  /* エラー */
						goto err;
				}
				if (!l)
					break;
				buf[--l] = '\0';  /* buf の末尾の一文字を削る */
			}
			sb_cappend(&result, s[i++]);
cont:;
		}
		free(pat);
		free(str);
		return sb_tostr(&result);
	}

nomatch:
	free(pat);
	free(str);
	return xstrdup(s);
err:
	xerror(0, 0, "unexpected fnmatch error");
	free(pat);
	free(str);
	return NULL;
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
void append_splitting(const char *str, struct strbuf *buf, struct plist *list,
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
		switch (glob(s, 0, NULL, &gbuf)) {
			case GLOB_NOSPACE:  case GLOB_ABORTED:  default:
				xerror(0, 0, "%s: glob error", *ss);
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
 * 戻り値: 新しく malloc した、s の展開結果。s が NULL なら NULL。 */
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
char *unescape(char *const s)
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

/* free 可能な文字列 s 文字のうち、文字列 q に含まれている文字を \ で
 * エスケープする。
 * s は escape 関数内で free し、結果として新しく malloc した文字列が返る。 */
char *escape(char *s, const char *q)
{
	if (!strpbrk(s, q))
		return s;

	struct strbuf buf;
	sb_init(&buf);
	escape_bs(s, q, &buf);
	free(s);
	return sb_tostr(&buf);
}
