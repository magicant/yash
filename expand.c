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
#include <string.h>
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

bool expand_line(char **args, int *argc, char ***argv);
bool expand_redirections(REDIR **redirs);
static bool expand_arg(const char *s, struct plist *argv);
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

	plist_init(&alist);
	if (!args) goto end;

	while (*args) {
		result &= expand_arg(*args, &alist);
		args++;
	}

end:
	*argc = alist.length;
	*argv = (char **) plist_toary(&alist);
	return result;
}

/* 各リダイレクトの rd_file を展開する。
 * *redirs の rd_file を直接書き換えるのではなくて、新しく malloc した
 * REDIR を *redirs に代入して返す。
 * 戻り値: エラー無しなら true、エラーありなら false。
 * エラーの有無にかかわらず *redirs には新しく malloc した REDIR が入る。
 * もっとも、*redirs が最初から NULL だったら NULL のままとなる。 */
bool expand_redirections(REDIR **redirs)
{
	REDIR *rd = *redirs;
	REDIR *first = NULL, **lastp = &first;
	struct plist etemp;
	bool ok = true;

	plist_init(&etemp);
	while (rd) {
		REDIR *newrd = xmalloc(sizeof *newrd);
		*newrd = *rd;
		*lastp = newrd;
		lastp = &newrd->next;
		if (!expand_arg(rd->rd_file, &etemp) || etemp.length != 1) {
			ok = false;
			for (size_t i = 0; i < etemp.length; i++)
				free(etemp.contents[i]);
		} else {
			newrd->rd_file = etemp.contents[0];
		}
		plist_clear(&etemp);
		rd = rd->next;
	}
	assert(etemp.length == 0);
	plist_destroy(&etemp);
	*redirs = first;
	return ok;
}

///* args の中からリダイレクトを分離し、リダイレクト以外のものは argv に入れて、
// * リダイレクトは *redirs として返す。結果は全て展開済である。
// * args の内容は一切変更しない。
// * 成功すると 0 を返し、失敗すると -1 を返す。失敗時はエラーを出力する。 */
//static bool expand_redirections(char **args, struct plist *argv, REDIR **redirs)
//{
//	char *a;
//	bool result = true;
//	REDIR *first = NULL, **lastp = &first;
//	struct plist etemp;
//
//	plist_init(&etemp);
//	while ((a = *args)) {
//		int fd = -1;
//		char *s;
//		bool usedest = false;
//		int flags = 0;
//
//		if ('0' <= *a && *a <= '9') {
//			fd = strtol(a, &s, 10);
//		} else {
//			switch (*a) {
//				case '<':
//					fd = STDIN_FILENO;
//					break;
//				case '>':
//					fd = STDOUT_FILENO;
//					break;
//			}
//			s = a;
//		}
//		switch (*s) {
//			case '<':
//				flags = O_RDONLY;
//				s++;
//				if (*s == '>') {
//					flags = O_RDWR | O_CREAT;
//					s++;
//				} else if (*s == '&') {
//					usedest = true;
//					s++;
//				} else if (*s == '<') {
//					error(0, 0, "%s: here document not supported", a);
//					goto next;
//				}
//				break;
//			case '>':
//				flags = O_WRONLY | O_CREAT;
//				s++;
//				if (*s == '>') {
//					flags |= O_APPEND;
//					s++;
//				} else if (*s == '<') {
//					flags = O_RDWR | O_CREAT;
//					s++;
//				} else if (*s == '&') {
//					usedest = true;
//					s++;
//				}
//				break;
//			default:
//				result &= expand_arg(a, argv);
//				goto next;
//		}
//		if (!*s) {
//			if (!(a = s = *++args)) {
//				error(0, 0, "redirect argument not given");
//				result = false;
//				break;
//			}
//		}
//		if (!expand_arg(s, &etemp)) {
//			result = false;
//			goto next;
//		} else if (etemp.length == 0) {
//			error(0, 0, "%s: invalid redirect: "
//					"argument expanded to null string", a);
//			result = false;
//			goto next;
//		} else if (etemp.length > 1) {
//			error(0, 0, "%s: invalid redirect: "
//					"argument expanded to mulitple strings", a);
//			result = false;
//			goto next;
//		} else {
//			REDIR *redir = xmalloc(sizeof *redir);
//			assert(etemp.length == 1);
//			s = etemp.contents[0];
//			plist_clear(&etemp);
//
//			redir->next = NULL;
//			redir->rd_fd = fd;
//			redir->rd_flags = flags;
//			if (usedest) {
//				redir->rd_file = NULL;
//				if (strcmp(s, "-") == 0) {
//					redir->rd_destfd = -1;
//				} else {
//					char *end;
//					fd = strtol(s, &end, 10);
//					if (!*s || *end) {
//						error(0, 0, "%s: redirect syntax error", a);
//						result = false;
//						free(redir);
//						redir = NULL;
//					} else {
//						redir->rd_destfd = fd;
//					}
//				}
//				free(s);
//			} else {
//				redir->rd_destfd = -1;
//				redir->rd_file = s;
//			}
//			if (redir) {
//				*lastp = redir;
//				lastp = &redir->next;
//			}
//		}
//
//next:
//		args++;
//	}
//
//	recfree(plist_toary(&etemp));
//	*redirs = first;
//	return result;
//}

/* 一つの引数に対してブレース展開以降の全ての展開を行い結果を argv に追加する。
 * 一つの引数が単語分割によって複数の単語に分かれることがあるので、argv
 * に加わる要素は一つとは限らない。
 * 成功すると true を、失敗すると false を返す。失敗時はエラーを出力する。
 * 失敗しても途中結果が argv に追加されること場合もある。 */
static bool expand_arg(const char *s, struct plist *argv)
{
	//TODO expand_arg
	plist_append(argv, xstrdup(s));
	return true;
}

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
