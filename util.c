/* Yash: yet another shell */
/* util.c: miscellaneous utility functions */
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
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "yash.h"
#include "util.h"


/********** General functions **********/

/* calloc を試みる。失敗したらプログラムを強制終了する。 */
/* Unused function
void *xcalloc(size_t nmemb, size_t size)
{
	void *result = calloc(nmemb, size);
	if (result)
		return result;
	xerror(2, ENOMEM, NULL);
	assert(false);
}
*/

/* malloc を試みる。失敗したらプログラムを強制終了する。 */
void *xmalloc(size_t size)
{
	void *result = malloc(size);
	if (result)
		return result;
	xerror(2, ENOMEM, NULL);
	assert(false);
}

/* realloc を試みる。失敗したらプログラムを強制終了する。 */
void *xrealloc(void *ptr, size_t size)
{
	void *result = realloc(ptr, size);
	if (result)
		return result;
	xerror(2, ENOMEM, NULL);
	assert(false);
}


/********** String utilities **********/

/* 文字列を新しく malloc した領域に複製する。
 * malloc に失敗するとプログラムを強制終了する。
 * len: 複製する文字列の長さ ('\0' を含まない)。
 *      len が s の実際の長さより大きければ s 全体を複製する */
char *xstrndup(const char *s, size_t len)
{
	len = xstrnlen(s, len);

	char *result = xmalloc(len + 1);
	result[len] = '\0';
	return memcpy(result, s, len);
}

/* 文字列を新しく malloc した領域に複製する。
 * malloc に失敗するとプログラムを強制終了する。
 * len: 複製する文字列の長さ ('\0' を含まない)。
 *      len が s の実際の長さより大きければ s 全体を複製する */
wchar_t *xwcsndup(const wchar_t *s, size_t len)
{
	len = xwcsnlen(s, len);

	wchar_t *result = xmalloc((len + 1) * sizeof(wchar_t));
	result[len] = L'\0';
	return wmemcpy(result, s, len);
}

/* 文字列 s が prefix で始まるなら、s 内の prefix を飛ばした最初の文字への
 * ポインタを返し、さもなくば NULL を返す。 */
/* Unused function 
char *matchstrprefix(const char *s, const char *prefix)
{
	while (*prefix) {
		if (*prefix != *s)
			return NULL;
		prefix++;
		s++;
	}
	return (char *) s;
}
*/


/********** Error utilities **********/

const char *yash_program_invocation_name;
const char *yash_program_invocation_short_name;
unsigned yash_error_message_count = 0;

/* glibc の error 関数の独自の実装。エラーメッセージを stderr に出力する。
 * status: 非 0 ならメッセージを出した後 exit(status) を呼んで終了する。
 * errno_: 非 0 なら format メッセージに続けて errno に対応するメッセージを出す
 * format: エラーメッセージ。printf 用のフォーマット文字列。
 * 出力内容は以下の通り:
 *   - "%s: ", yash_program_invocation_name
 *   - format, ... の内容        (format が非 NULL の場合のみ)
 *   - ": %s", strerror(errno)   (errno が非 0 の場合のみ)
 *   - "\n"
 * format == NULL && errno_ == 0 なら、"unknown error" を出力する。 */
void xerror(int status, int errno_, const char *restrict format, ...)
{
	va_list ap;

	yash_error_message_count++;
	fflush(stdout);
	fprintf(stderr, "%s: ", yash_program_invocation_name);
	if (format) {
		va_start(ap, format);
		vfprintf(stderr, format, ap);
		va_end(ap);
	}
	if (errno_) {
		fprintf(stderr, format ? ": %s" : "%s", strerror(errno_));
	}
	if (format || errno_) {
		fputc('\n', stderr);
	} else {
		fputs(gt("Unknown error\n"), stderr);
	}
	fflush(stderr);
	if (status)
		exit(status);
}


/********** xgetopt **********/

char *xoptarg;
int xoptind = 0;
int xoptopt;
bool xopterr = true;

/* xgetopt_long で使う補助関数。argv[from] を argv[to] に持ってくる。 */
static void argshift(char **argv, int from, int to /* <= from */)
{
	char *s = argv[from];

	assert(from >= to);
	for (int i = from; i > to; i--)
		argv[i] = argv[i - 1];
	argv[to] = s;
}

/* GNU ライブラリにある getopt_long の自前の実装。
 * argv 内の各文字列をコマンドライン引数とみなし、オプションを取り出す。
 * この関数を呼び出すごとに、オプションを一つづつ取り出して返してゆく。詳細は
 * http://www.linux.or.jp/JM/html/LDP_man-pages/man3/getopt.3.html
 * 等も参照のこと。
 * argv:      解析する文字列の配列の先頭へのポインタ。
 *            配列の中の文字列は関数の中で並び変わることがある。
 *            配列の最後の要素の後には NULL ポインタが入っている必要がある。
 * optstring: 認識すべき一文字のオプションを指定する文字列へのポインタ。
 *            例えば -a, -d, -w の三種類のオプションを認識すべきなら、
 *            "ad:w" となる。'd' の後の ':' は -d が引数を取ることを示す。
 *            ':' を二つにすると下記の xoptional_argument のようになる。
 *            英数字以外のオプション文字は正しく動作しない。
 * longopts:  認識すべき長いオプションを指定する struct xoption 配列への
 *            ポインタ。配列の最後の要素は { 0, 0, 0, 0, } とすること。
 *            長いオプションを一切使わない場合は NULL でもよい。
 *            posixly_correct ならば、長いオプションは一切解析しない。
 * longindex: 長いオプションを認識したとき、longindex が非 NULL なら、
 *            そのオプションを表す struct xoption が longopts 内の何番目の要素
 *            であるかを示すインデックスが *longindex に入る。
 * 戻り値:    一文字のオプションを認識したときは、その一文字。
 *            長いオプションを認識したときは、下記説明を参照。
 *            optstring や longopts に無いオプションが出たときは '?'。
 *            もうオプションがない時は、-1。
 * posixly_correct || '+' フラグありの時を除いて、argv の中の文字列は
 * オプションとそれ以外の引数が混ざっていてもよい。この場合、全てのオプションを
 * 認識し終えて -1 を返した時点で、全てのオプションがそれ以外の引数よりも前に
 * 来るように argv の中の文字列は並び変わっている。(認識できないオプションが
 * あった場合を除く)
 * posixly_correct || '+' フラグありの時は、オプションでない引数が一つでも
 * 出た時点で認識は終了する。(よって、argv は並び変わらない)
 * いづれの場合も、-1 を返した時点で、argv[xoptind] はオプションでない最初の
 * 引数である。-1 が返ったら、それ以上 xgetopt_long を呼んではいけない。
 * それまでは、毎回同じ引数で xgetopt_long を呼ぶこと。とくに、途中で
 * argv や xoptind を外から書き換えてはいけない。
 * '*' フラグありの時は、'+' で始まる一文字のオプションも認識する。
 * '+' で始まるオプションを認識すると、xoptopt が '+' になる。
 * '+'/'*' フラグは optstring の先頭に付ける。
 * '+' フラグは '*' より先に指定する。
 * struct xoption のメンバの意味は以下の通り:
 * name:     長いオプションの名前。("--" より後の部分)
 *           引数ありのオプションの名前に '=' が入っているとうまく動かない。
 *           いづれにしても名前は英数字とハイフンのみからなるべきである。
 * has_arg:  オプションが引数を取るかどうか。xno_argument, xrequired_argument,
 *           xoptional_argument のどれかを指定する。xoptional_argument なら、
 *           オプションに対する引数は --opt arg のように分けることはできない。
 *           xrequired_argument では、--opt arg のように分けてもよいし、
 *           --opt=arg のように繋げてもよい。
 * flag,val: 長いオプションを認識したときの動作を決める。flag が非 NULL なら、
 *           *flag に val を代入して xgetopt_long は 0 を返す。flag が NULL
 *           なら、単に xgetopt_long は val を返す。
 * 外部結合変数 xoptind, xoptarg, xoptopt, xopterr の意味は以下の通り:
 * xoptind: argv 内で次に解析すべき文字列のインデックス。始めは 0 になっていて、
 *          解析が進むにつれて増える。新しい argv に対する解析を始める前には、
 *          0 に値を設定し直すこと。
 * xoptarg: 引数のあるオプションを認識したとき、その引数の最初の文字への
 *          ポインタが xoptarg に入る。xoptional_argument で引数がない場合、
 *          NULL ポインタが入る。
 * xoptopt: optstring に無い一文字のオプションがあると、その文字が xoptopt に
 *          入る。optstring にある一文字のオプションを認識したとき、オプションが
 *          '-' で始まるか '+' で始まるかによって '-' と '+' のどちらかが入る。
 *          長いオプションを認識したときは '-' になる。
 * xopterr: true なら、optstring や longopts に無いオプションが出たときに
 *          エラーメッセージを stderr に出力する。
 */
/* この実装は GNU 版の getopt_long や POSIX の定める getopt 関数と
 * 完全に互換性がある訳ではない。
 * getopt の実装によっては、新しい解析を始める前に argreset に 1 を代入する
 * ことで解析器をリセットするようになっているものもある。この実装では、argreset
 * を使わずに、xoptind を 0 に戻すことでリセットする。
 * この実装はマルチバイト文字には完全には対応していない。解析の対象となる文字は
 * すべて初期シフト状態で解釈する。 */
int xgetopt_long(
		char **restrict argv,
		const char *restrict optstring,
		const struct xoption *restrict longopts,
		int *restrict longindex)
{
	int initind;
	char *arg, *arg2, argchar;
	static int aindex;
	bool optionsfirst = posixly_correct, plusoptions = false;

	if (xoptind == 0) {  /* 新しい解析を始めるためにリセット */
		aindex = 1;
		xoptind = 1;
	}
	if (*optstring == '+') {
		optionsfirst = true;
		optstring++;
	}
	if (*optstring == '*') {
		plusoptions = true;
		optstring++;
	}

	initind = xoptind;
	while ((arg = argv[xoptind])) {
		if ((arg[0] != '-' && (!plusoptions || (arg[0] != '+'))) || !arg[1]) {
			/* arg はオプションではない */
			if (optionsfirst)
				break;
			xoptind++;
			continue;
		}

		if (arg[0] == '-' && arg[1] == '-') {
			/* arg は "--" で始まる */
			goto tryparselongoption;
		} else {
			/* 一文字のオプションを解析 */
			goto tryparseshortoption;
		}
	}
	xoptind = initind;
	return -1;

tryparseshortoption:
	argchar = arg[aindex];
	xoptopt = arg[0];
	optstring = strchr(optstring, argchar);
	if (!optstring) {
		xoptopt = argchar;
		goto nosuchoption;
	}

	/* 有効なオプションが見付かった */
	if (optstring[1] == ':') {
		/* 引数を取るオプション */
		xoptarg = &arg[aindex + 1];
		aindex = 1;
		if (!*xoptarg && optstring[2] != ':') {
			/* -x arg のように分かれている場合 */
			xoptarg = argv[xoptind + 1];
			if (!xoptarg)
				goto argumentmissing;
			argshift(argv, xoptind, initind);
			argshift(argv, xoptind + 1, initind + 1);
			xoptind = initind + 2;
		} else {
			/* 引数が省略された場合 */
			argshift(argv, xoptind, initind);
			xoptind = initind + 1;
		}
	} else {
		/* 引数を取らないオプション */
		if (arg[aindex + 1]) {
			aindex++;
			argshift(argv, xoptind, initind);
			xoptind = initind;
		} else {
			argshift(argv, xoptind, initind);
			xoptind = initind + 1;
		}
	}
	return argchar;

tryparselongoption:
	arg2 = &arg[2];
	if (!arg2[0]) {
		/* arg == "--" */
		argshift(argv, xoptind, initind);
		xoptind = initind + 1;
		return -1;
	}
	if (posixly_correct || !longopts)
		goto nosuchoption;

	/* 一致する長いオプションを探す */
	int matchindex = -1;
	size_t len = strcspn(arg2, "=");
	for (int i = 0; longopts[i].name; i++) {
		if (strncmp(longopts[i].name, arg2, len) == 0) {
			if (longopts[i].name[len]) {
				/* 部分一致 */
				if (matchindex < 0) {
					/* 初めての一致 */
					matchindex = i;
					continue;
				} else {
					/* すでに他にも一致したものがある */
					arg = arg2;
					goto ambiguousmatch;
				}
			} else {
				/* 完全一致 */
				matchindex = i;
				break;
			}
		}
	}
	if (matchindex < 0)
		goto nosuchoption;

	/* 長いオプションが見付かった */
	if (longindex)
		*longindex = matchindex;
	xoptopt = '-';
	if (longopts[matchindex].has_arg) {
		char *eq = strchr(arg2, '=');
		if (!eq) {
			/* --option arg のように分かれている場合 */
			xoptarg = argv[xoptind + 1];
			if (!xoptarg) {
				if (longopts[matchindex].has_arg == xrequired_argument)
					goto argumentmissing;
				argshift(argv, xoptind, initind);
				xoptind = initind + 1;
			} else {
				argshift(argv, xoptind, initind);
				argshift(argv, xoptind + 1, initind + 1);
				xoptind = initind + 2;
			}
		} else {
			/* --option=arg のように '=' で引数を指定している場合 */
			xoptarg = eq + 1;
			argshift(argv, xoptind, initind);
			xoptind = initind + 1;
		}
	} else {
		/* 引数を取らないオプション */
		argshift(argv, xoptind, initind);
		xoptind = initind + 1;
	}
	if (longopts[matchindex].flag) {
		*longopts[matchindex].flag = longopts[matchindex].val;
		return 0;
	} else {
		return longopts[matchindex].val;
	}

ambiguousmatch:
	if (xopterr) {
		fflush(stdout);
		fprintf(stderr, gt("%s: --%s: ambiguous option\n"), argv[0], arg);
#if 0
		for (int i = 0; longopts[i].name; i++)
			if (matchstrprefix(longopts[i].name, arg))
				fprintf(stderr, "\t--%s\n", longopts[i].name);
#endif
		fflush(stderr);
	}
	xoptind++;
	return '?';
nosuchoption:
	if (xopterr) {
		fflush(stdout);
		fprintf(stderr, gt("%s: %s: invalid option\n"), argv[0], argv[xoptind]);
		fflush(stderr);
	}
	xoptind++;
	return '?';
argumentmissing:
	if (xopterr) {
		fflush(stdout);
		fprintf(stderr, gt("%s: %s: argument missing\n"), argv[0], argv[xoptind]);
		fflush(stderr);
	}
	xoptind++;
	return '?';
}
