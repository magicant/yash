/* Yash: yet another shell */
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
#include <unistd.h>
#include "yash.h"
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

bool expand_line(char **args, int *argc, char ***argv, REDIR **redirs);
static bool expand_redirections(
		char **args, struct plist *argv, REDIR **redirs);
static bool expand_arg(const char *s, struct plist *argv);
void escape_sq(const char *s, struct strbuf *buf);
void escape_dq(const char *s, struct strbuf *buf);


/* コマンドライン上の各種展開を行う。
 * args は入力引数であり、展開される文字列への配列である。これは NULL でもよい。
 * argc, argv, redirs は出力引数であり、各ポインタのアドレスにそれぞれ
 * 展開結果の個数・展開結果への配列・リダイレクトへのポインタが入る。
 * *argv は新しく malloc した文字列への新しく malloc した配列である。
 * args の内容は一切変更しない。
 * 成功すると true 、失敗すると false を返す。失敗時はエラーを出力する。 */
bool expand_line(char **args, int *argc, char ***argv, REDIR **redirs)
{
	struct plist alist;
	REDIR *firstr = NULL;
	//REDIR **lastrp = &firstr;

	plist_init(&alist);
	if (!args) goto end;

	// TODO
	for (char **a = args; *a; a++)
		plist_append(&alist, xstrdup(*a));

end:
	*argc = alist.length;
	*argv = (char **) plist_toary(&alist);
	*redirs = firstr;
	return 0;
}

/* args の中からリダイレクトを分離し、リダイレクト以外のものは argv に入れて、
 * リダイレクトは *redirs として返す。結果は全て展開済である。
 * args の内容は一切変更しない。
 * 成功すると 0 を返し、失敗すると -1 を返す。失敗時はエラーを出力する。 */
static bool expand_redirections(char **args, struct plist *argv, REDIR **redirs)
{
	char *a;
	bool result = true;
	REDIR *first = NULL, **lastp = &first;
	struct plist etemp;

	plist_init(&etemp);
	while ((a = *args)) {
		int fd;
		char *s;
		bool usedest = false;
		int flags = 0;

		if ('0' <= *a && *a <= '9') {
			fd = strtol(a, &s, 10);
		} else {
			switch (*a) {
				case '<':
					fd = STDIN_FILENO;
					break;
				case '>':
					fd = STDOUT_FILENO;
					break;
			}
			s = a;
		}
		switch (*s) {
			case '<':
				flags = O_RDONLY;
				s++;
				if (*s == '>') {
					flags = O_RDWR | O_CREAT;
					s++;
				} else if (*s == '&') {
					usedest = true;
					s++;
				} else if (*s == '<') {
					error(0, 0, "%s: here document not supported", a);
					goto next;
				}
				break;
			case '>':
				flags = O_WRONLY | O_CREAT;
				s++;
				if (*s == '>') {
					flags |= O_APPEND;
					s++;
				} else if (*s == '<') {
					flags = O_RDWR | O_CREAT;
					s++;
				} else if (*s == '&') {
					usedest = true;
					s++;
				}
				break;
			default:
				result &= expand_arg(a, argv);
				goto next;
		}
		if (!*s)
			a = s = *++args;
		if (!expand_arg(s, &etemp)) {
			result = false;
			goto next;
		} else if (etemp.length == 0) {
			error(0, 0, "%s: invalid redirect: "
					"argument expanded to null string", s);
			result = false;
			goto next;
		} else if (etemp.length > 1) {
			error(0, 0, "%s: invalid redirect: "
					"argument expanded to mulitple strings", s);
			result = false;
			goto next;
		} else {
			assert(etemp.length == 1);
			//TODO
			plist_clear(&etemp);
		}

next:
		args++;
	}

	recfree(plist_toary(&etemp));
	return result;
}

/* 一つの引数に対してブレース展開以降の全ての展開を行い結果を argv に追加する。
 * 一つの引数が単語分割によって複数の単語に分かれることがあるので、argv
 * に加わる要素は一つとは限らない。
 * 成功すると 0 を返し、失敗すると -1 を返す。失敗時はエラーを出力する。 */
static bool expand_arg(const char *s, struct plist *argv);

/* 文字列 s を引用符 ' で囲む。ただし、文字列に ' 自身が含まれる場合は
 * \ でエスケープする。結果は buf に追加する。
 *   例)  abc"def'ghi  ->  'abcdef'\''ghi'  */
void escape_sq(const char *s, struct strbuf *buf)
{
	if (s) {
		strbuf_cappend(buf, '\'');
		while (*s) {
			if (*s == '\'')
				strbuf_append(buf, "'\\''");
			else
				strbuf_cappend(buf, *s);
			s++;
		}
		strbuf_cappend(buf, '\'');
	}
}

/* 文字列 s に含まれる " ` \ $ を \ でエスケープしつつ buf に追加する。
 *   例)  abc"def'ghi`jkl\mno  ->  abc\"def'ghi\`jkl\\mno  */
void escape_dq(const char *s, struct strbuf *buf)
{
	if (s) {
		while (*s) {
			switch (*s) {
				case '"':  case '`':  case '$':  case '\\':
					strbuf_cappend(buf, '\\');
					/* falls thru! */
				default:
					strbuf_cappend(buf, *s);
					break;
			}
			s++;
		}
	}
}
