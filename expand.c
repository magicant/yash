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


#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "yash.h"
#include "parser.h"
#include "expand.h"
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
char *expand_single(const char *arg);
static bool expand_arg(const char *s, struct plist *argv);
static bool expand_brace(char *s, struct plist *result);
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

	pl_init(&temp1);
	expand_brace(xstrdup(s), &temp1);  /* まずブレース展開 */

	pl_init(&temp2);
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
	pl_destroy(&temp1);

	//TODO expand_arg
	pl_aappend(argv, temp2.contents);
	pl_destroy(&temp2);
	return true;
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

/* 文字列 s を引用符 ' で囲む。ただし、文字列に ' 自身が含まれる場合は
 * \ でエスケープする。結果は buf に追加する。buf は初期化してから渡すこと。
 *   例)  abc"def'ghi  ->  'abcdef'\''ghi'  */
void escape_sq(const char *s, struct strbuf *buf)
{
	if (s) {
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
}

/* 文字列 s に含まれる " ` \ $ を \ でエスケープしつつ buf に追加する。
 * buf は初期化してから渡すこと。
 *   例)  abc"def'ghi`jkl\mno  ->  abc\"def'ghi\`jkl\\mno  */
void escape_dq(const char *s, struct strbuf *buf)
{
	if (s) {
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
}
