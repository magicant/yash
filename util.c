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
#define  NO_UTIL_INLINE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "yash.h"
#include "util.h"
#include <assert.h>

static unsigned ht_hashstr(const char *s) __attribute__((pure));
static void ht_rehash(struct hasht *ht, size_t newcap);


/* calloc を試みる。失敗したらプログラムを強制終了する。
 * 戻り値: calloc の結果 */
void *xcalloc(size_t nmemb, size_t size)
{
	assert(nmemb > 0 && size > 0);

	void *result = calloc(nmemb, size);
	if (!result)
		xerror(2, ENOMEM, NULL);
	return result;
}

/* malloc を試みる。失敗したらプログラムを強制終了する。
 * 戻り値: malloc の結果 */
void *xmalloc(size_t size)
{
	assert(size > 0);

	void *result = malloc(size);
	if (!result)
		xerror(2, ENOMEM, NULL);
	return result;
}

/* realloc を試みる。失敗したらプログラムを強制終了する。
 * 戻り値: realloc の結果 */
void *xrealloc(void *ptr, size_t size)
{
	void *result = realloc(ptr, size);
	if (!result)
		xerror(2, ENOMEM, NULL);
	return result;
}

/* 文字列を新しく malloc した領域に複製する。
 * malloc に失敗するとプログラムを強制終了する。
 * len: 複製する文字列の長さ ('\0' を含まない)。
 *      len をいくら大きくしても strlen(s) より長い文字列にはならない。 */
char *xstrndup(const char *s, size_t len)
{
	size_t reallen = strlen(s);
	if (reallen < len)
		len = reallen;

	char *result = xmalloc(len + 1);
	result[len] = '\0';
	return strncpy(result, s, len);
}

/* 文字列を新しく malloc した領域に複製する。
 * malloc に失敗するとプログラムを強制終了する。 */
char *xstrdup(const char *s)
{
	return xstrndup(s, SIZE_MAX);
}

/* 文字列の配列のディープコピーを作る。失敗するとプログラムを強制終了する。 */
char **strarydup(char *const *ary)
{
	size_t count;
	char **result;

	assert(ary);
	for (count = 0; ary[count]; count++) ;
	result = xcalloc(count + 1, sizeof *ary);
	for (size_t i = 0; i < count; i++)
		result[i] = xstrdup(ary[i]);
	return result;
}

/* NULL 終端のポインタの配列の長さを求める。 */
size_t parylen(void *const *ary)
{
	size_t count = 0;
	while (*ary++) count++;
	return count;
}

/* NULL 終端のポインタの配列を、その要素も含めて解放する。
 * freer:  ary の各要素に対して呼び出す free 用関数。 */
void recfree(void **ary, void (*freer)(void *elem))
{
	if (ary) {
		for (void **a = ary; *a; a++) freer(*a);
		free(ary);
	}
}

/* 文字列の先頭にある空白文字 (スペースまたはタブ) を飛ばして、
 * 空白文字でない最初の文字のアドレスを返す。 */
char *skipblanks(const char *s)
{
	while (xisblank(*s)) s++;
	return (char *) s;
}

/* 文字列の先頭にある空白類文字 (スペースや改行) を飛ばして、
 * 空白類文字でない最初の文字のアドレスを返す。 */
char *skipspaces(const char *s)
{
	while (xisspace(*s)) s++;
	return (char *) s;
}

/* 文字列の先頭にある空白類文字やコメントを飛ばして、
 * 最初のトークンの文字のアドレスを返す。 */
char *skipwhites(const char *s)
{
	s = skipspaces(s);
	if (*s == '#') {
		s++;
		s += strcspn(s, "\n");
	}
	return (char *) s;
}

/* 文字列 s が prefix で始まるなら、文字列 s 内の prefix を飛ばした最初の文字
 * へのポインタを返す。文字列 s が prefix で始まらなければ NULL を返す。 */
char *matchprefix(const char *s, const char *prefix)
{
	assert(s && prefix);
	while (*prefix) {
		if (*prefix != *s)
			return NULL;
		prefix++;
		s++;
	}
	return (char *) s;
}

/* 文字列の先頭にある空白類文字 (スペースや改行) を削除する。
 * 文字列を直接書き換えた後、その文字列へのポインタ s を返す。 */
//char *strchug(char *s)
//{
//	size_t i = 0;
//
//	assert(s != NULL);
//	while (xisspace(s[i])) i++;
//	if (i)
//		memmove(s, s + i, strlen(s + i) + 1);
//	return s;
//}

/* 文字列の末尾にある空白類文字 (スペースや改行) を削除する。
 * 文字列を直接書き換えた後、その文字列へのポインタ s を返す。 */
//char *strchomp(char *s)
//{
//	char *ss = s;
//
//	while (*s) s++;                     /* 文字列の末尾に移動 */
//	while (--s >= ss && xisspace(*s));  /* 空白の分だけ戻る */
//	*++s = '\0';
//	return ss;
//}

/* 配列に含まれる文字列を全て順に連結し、新しく malloc した文字列として返す。
 * argc: argv から取り出す文字列の数。全て取り出すなら負数。
 * argv: この配列に含まれる文字列が連結される。
 * padding: 連結される各文字列の間に挟まれる文字列。NULL なら何も挟まない。 */
char *strjoin(
		int argc, char *const *restrict argv, const char *restrict padding)
{
	assert(argv != NULL);
	if (!padding)
		padding = "";

	size_t resultlen = 0;
	size_t paddinglen = strlen(padding);
	int strcount;
	if (argc < 0)
		argc = INT_MAX;
	for (strcount = 0; strcount < argc && argv[strcount]; strcount++)
		resultlen += strlen(argv[strcount]);
	if (!strcount)
		return xstrdup("");
	resultlen += paddinglen * (strcount - 1);

	char *restrict result = xmalloc(resultlen + 1);
	resultlen = 0;
	for (int i = 0; i < strcount; i++) {
		if (i) {
			strcpy(result + resultlen, padding);
			resultlen += paddinglen;
		}
		strcpy(result + resultlen, argv[i]);
		resultlen += strlen(argv[i]);
	}
	return result;
}

/* printf の結果を新しく malloc した文字列として返す。 */
char *mprintf(const char *restrict format, ...)
{
	va_list ap;
	int count;
	char temp[16];
	char *result;

	va_start(ap, format);
	count = vsnprintf(temp, sizeof temp, format, ap);
	va_end(ap);
	if (count < 0) {
		result = NULL;
	} else if ((unsigned) count < sizeof temp) {
		result = xstrdup(temp);
	} else {
		result = xmalloc((unsigned) count + 1);
		va_start(ap, format);
		vsprintf(result, format, ap);
		va_end(ap);
	}
	return result;
}


/********** Error utilities **********/

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
	if (errno_)
		fprintf(stderr, format ? ": %s" : "%s", strerror(errno_));
	if (!format && !errno_)
		fputs("unknown error", stderr);
	fputc('\n', stderr);
	fflush(stderr);
	if (status)
		exit(status);
}


/********** getopt **********/

char *xoptarg;
int xoptind = 0, xoptopt;
bool xopterr = true;

/* xgetopt_long で使う補助関数。argv[from] を argv[to] に持ってくる。 */
static void argshift(char *const *argv, int from, int to /* <= from */)
{
	char **ncargv = (char **) argv;
	char *s = ncargv[from];

	assert(from >= to);
	for (int i = from; i > to; i--)
		ncargv[i] = ncargv[i - 1];
	ncargv[to] = s;
}

/* GNU ライブラリにある getopt_long の自前の実装。
 * argv 内の各文字列をコマンドライン引数とみなし、オプションを取り出す。
 * この関数を呼び出すごとに、オプションを一つづつ取り出して返してゆく。詳細は
 * http://www.linux.or.jp/JM/html/LDP_man-pages/man3/getopt.3.html
 * 等も参照のこと。
 * argv:      解析する文字列の配列の先頭へのポインタ。const になっているが、
 *            配列の中の文字列は関数の中で並び変わることがある。
 *            配列の最後の要素の後には NULL ポインタが入っている必要がある。
 * optstring: 認識すべき一文字のオプションを指定する文字列へのポインタ。
 *            例えば -a, -d, -w の三種類のオプションを認識すべきなら、
 *            "ad:w" となる。'd' の後の ':' は -d が引数を取ることを示す。
 *            ':' を二つにすると下記の xoptional_argument のようになる。
 *            英数字以外のオプション文字は正しく動作しない。
 * longopts:  認識すべき長いオプションを指定する struct xoption 配列への
 *            ポインタ。配列の最後は { 0, 0, 0, 0, } とすること。
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
 * を使わずに、xoptind を 0 に戻すことでリセットする。 */
int xgetopt_long(char *const *restrict argv, const char *restrict optstring,
		const struct xoption *restrict longopts, int *restrict longindex)
{
	int initind;
	char *arg;
	static int aindex;
	bool optionsfirst = posixly_correct, plus = false;

	if (xoptind == 0) {
		aindex = 1;
		xoptind = 1;
	}
	if (optstring[0] == '+') {
		optionsfirst = true;
		optstring++;
	}
	if (optstring[0] == '*') {
		plus = true;
		optstring++;
	}

	initind = xoptind;
	while ((arg = argv[xoptind])) {
		if ((arg[0] != '-' && (!plus || (arg[0] != '+'))) || !arg[1]) {
			if (optionsfirst)
				break;
			xoptind++;
			continue;
		}
		if (arg[0] == '-' && arg[1] == '-') {  /* arg は "--" で始まる */
			char *arg2 = arg + 2;
			if (!*arg2) {  /* arg == "--" */
				argshift(argv, xoptind, initind);
				xoptind = initind + 1;
				return -1;
			}
			if (posixly_correct || !longopts)
				goto nosuchopt;

			int matchidx = -1;
			size_t len = strcspn(arg2, "=");
			for (int i = 0; longopts[i].name; i++) {
				if (strncmp(longopts[i].name, arg2, len) != 0) {  /* 一致せず */
					continue;
				} else if (longopts[i].name[len]) {  /* 部分一致 */
					if (matchidx < 0) {
						matchidx = i;
						continue;
					} else {
						arg = arg2;
						goto ambig;
					}
				} else {  /* 完全一致 */
					matchidx = i;
					goto long_found;
				}
			}
			if (matchidx < 0)  /* どれにも一致しなかった */
				goto nosuchopt;
long_found:
			if (longindex)
				*longindex = matchidx;
			xoptopt = '-';
			if (longopts[matchidx].has_arg) {
				char *eq = strchr(arg2, '=');
				if (!eq && longopts[matchidx].has_arg == xrequired_argument) {
					xoptarg = argv[xoptind + 1];
					argshift(argv, xoptind, initind);
					argshift(argv, xoptind + 1, initind + 1);
					xoptind = initind + 2;
				} else {
					xoptarg = eq + 1;
					argshift(argv, xoptind, initind);
					xoptind = initind + 1;
				}
			} else {
				argshift(argv, xoptind, initind);
				xoptind = initind + 1;
			}
			if (longopts[matchidx].flag) {
				*longopts[matchidx].flag = longopts[matchidx].val;
				return 0;
			} else {
				return longopts[matchidx].val;
			}
		} else {  /* 一文字のオプションを解析 */
			char argchar = arg[aindex];
			xoptopt = arg[0];
			optstring = strchr(optstring, argchar);
			if (!optstring) {
				xoptopt = argchar;
				goto nosuchopt;
			}
			if (optstring[1] == ':') {  /* 引数ありのオプション */
				xoptarg = &arg[aindex + 1];
				aindex = 1;
				if (!*xoptarg && optstring[2] != ':') {
					xoptarg = argv[xoptind + 1];
					if (!xoptarg)
						goto missingarg;
					argshift(argv, xoptind, initind);
					argshift(argv, xoptind + 1, initind + 1);
					xoptind = initind + 2;
				} else {
					argshift(argv, xoptind, initind);
					xoptind = initind + 1;
				}
			} else {
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
		}
	}
	xoptind = initind;
	return -1;

ambig:
	if (xopterr) {
		xerror(0, 0, "%s: --%s: ambiguous option", argv[0], arg);
#if 0
		for (int i = 0; longopts[i].name; i++)
			if (hasprefix(longopts[i].name, arg))
				fprintf(stderr, "\t--%s\n", longopts[i].name);
#endif
	}
	xoptind++;
	return '?';
nosuchopt:
	if (xopterr)
		xerror(0, 0, "%s: %s: invalid option", argv[0], argv[xoptind]);
	xoptind++;
	return '?';
missingarg:
	if (xopterr)
		xerror(0, 0, "%s: %s: argument missing", argv[0], argv[xoptind]);
	xoptind++;
	return '?';
}

/* 長いオプションがない getopt */
int xgetopt(char *const *restrict argv, const char *restrict optstring)
{
	return xgetopt_long(argv, optstring, NULL, NULL);
}


/********** 一時ファイル作成 **********/

/* 一時ファイルを作成し、それを開いたファイルディスクリプタを返す。
 * tempname: XXXXXX で終わるファイル名の候補となる文字列。XXXXXX を適当な
 *           文字列に置き換えて、それを一時ファイルの名前とする。
 * 戻り値:   一時ファイルを開いたファイルディスクリプタ。エラーなら負数。
 * エラーの場合でも errno の値は信用できない。
 * 作成した一時ファイルを削除するのは呼び出し元の責任である。 */
int xmkstemp(char *tempname)
{
	int resultfd;
	mode_t oldumask = umask(S_IXUSR | S_IRWXG | S_IRWXO);

#ifdef HAVE_MKSTEMP
	resultfd = mkstemp(tempname);
	goto end;
#else /* HAVE_MKSTEMP : mkstemp がなければ自前の実装を使う */
	char *s = tempname;
	while (*s) s++;
	s -= 6;
	if (s < tempname || strcmp(s, "XXXXXX") != 0) {
		errno = EINVAL;
		resultfd = -1;
		goto end;
	}

	/* v は不定値となる。(GCC の警告をなくすために自分自身を代入している) */
	uint32_t v = v;
	v ^= time(NULL);
	v ^= getpid() * 101;

	int trycount = 0;
	do {
		static const char letters[61] =
			"1234567890abcdefghijklmnopqrtsuvwxyzABCDEFGHIJKLMNPQRTSUVWXYZ";
		v = (v * 123456789) ^ shell_pid;
		s[0] = letters[v % 61];
		v /= 61;
		s[1] = letters[v % 61];
		v /= 61;
		s[2] = letters[v % 61];
		v /= 61;
		s[3] = letters[v % 61];
		v /= 61;
		s[4] = letters[v % 61];
		v /= 61;
		s[5] = letters[v % 61];

		resultfd = open(tempname, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	} while (resultfd < 0 && errno == EEXIST && ++trycount < 100);
#endif /* HAVE_MKSTEMP */

end:
	umask(oldumask);
	return resultfd;
}


/********** 文字列バッファ **********/

/* 未初期化の文字列バッファを初期化する。 */
void sb_init(struct strbuf *buf)
{
	buf->contents = xmalloc(STRBUF_INITSIZE + 1);
	buf->contents[0] = '\0';
	buf->length = 0;
	buf->maxlength = STRBUF_INITSIZE;
}

/* 文字列バッファを解放し、文字列を返す。文字列バッファは未初期化状態になる。
 * 戻り値: 文字列バッファに入っていた文字列。この文字列は呼び出し元で free
 *         すべし。 */
char *sb_tostr(struct strbuf *buf)
{
	if (buf->length + 30 < buf->maxlength)
		sb_trim(buf);

	char *result = buf->contents;
	return result;
}

/* 初期化済の文字列バッファの内容を削除し、未初期化状態に戻す。 */
void sb_destroy(struct strbuf *buf)
{
	free(buf->contents);
}

/* 文字列バッファの maxlength を変更する。短くしすぎると文字列の末尾が消える */
void sb_setmax(struct strbuf *buf, size_t newmax)
{
	buf->maxlength = newmax;
	buf->contents = xrealloc(buf->contents, newmax + 1);
	buf->contents[newmax] = '\0';
}

/* 文字列バッファの大きさを実際の内容ぎりぎりにする。 */
void sb_trim(struct strbuf *buf)
{
	buf->maxlength = buf->length;
	buf->contents = xrealloc(buf->contents, buf->maxlength + 1);
}

/* 文字列バッファを空にする。maxlength は変わらない。 */
void sb_clear(struct strbuf *buf)
{
	buf->length = 0;
	buf->contents[buf->length] = '\0';
}

/* 文字列バッファ内の前から i 文字目に文字列 s の最初の n 文字を挿入する。
 * s が n 文字に満たなければ s 全体を挿入する。
 * i が大きすぎて文字列の末尾を越えていれば、文字列の末尾に s を付け加える。
 * s は buf->contents の一部であってはならない。 */
void sb_ninsert(struct strbuf *restrict buf,
		size_t i, const char *restrict s, size_t n)
{
	size_t len = strlen(s);
	if (len > n)
		len = n;
	if (i > buf->length)
		i = buf->length;
	if (len + buf->length > buf->maxlength) {
		do
			buf->maxlength = buf->maxlength * 2 + 1;
		while (len + buf->length > buf->maxlength);
		buf->contents = xrealloc(buf->contents, buf->maxlength + 1);
	}
	memmove(buf->contents + i + len, buf->contents + i, buf->length - i);
	memcpy(buf->contents + i, s, len);
	buf->length += len;
	buf->contents[buf->length] = '\0';
}

/* 文字列バッファ内の前から i 文字目に文字列 s を挿入する。
 * i が大きすぎて文字列の末尾を越えていれば、文字列の末尾に s を付け加える。
 * s は buf->contents の一部であってはならない。 */
void sb_insert(struct strbuf *restrict buf, size_t i, const char *restrict s)
{
	sb_ninsert(buf, i, s, SIZE_MAX);
}

/* 文字列バッファ内の文字列の末尾に文字列 s の最初の n 文字を付け加える。
 * s が n 文字に満たなければ s 全体を付け加える。
 * s は buf->contents の一部であってはならない。 */
void sb_nappend(struct strbuf *restrict buf, const char *restrict s, size_t n)
{
	sb_ninsert(buf, SIZE_MAX, s, n);
}

/* 文字列バッファ内の文字列の末尾に文字列 s を付け加える。
 * s は buf->contents の一部であってはならない。 */
void sb_append(struct strbuf *restrict buf, const char *restrict s)
{
	sb_nappend(buf, s, SIZE_MAX);
}

/* 文字列バッファの末尾に一文字追加する。 */
void sb_cappend(struct strbuf *buf, char c)
{
	if (buf->length == buf->maxlength) {
		buf->maxlength = buf->maxlength * 2 + 1;
		buf->contents = xrealloc(buf->contents, buf->maxlength + 1);
	}
	buf->contents[buf->length++] = c;
	buf->contents[buf->length] = '\0';
}

/* 文字列バッファの i 文字目から n 文字を s に置き換える。
 * s は buf->contents の一部であってはならない。 */
void sb_replace(struct strbuf *restrict buf,
		size_t i, size_t n, const char *restrict s)
{
	size_t slen = strlen(s);
	if (i > buf->length)
		i = buf->length;
	if (n > buf->length - i)
		n = buf->length - i;
	if (n >= slen) {
		memcpy(buf->contents + i, s, slen);
		if (n > slen) {
			/* contents が短くなる場合 */
			memmove(buf->contents + i + slen, buf->contents + i + n,
					buf->length - i - n + 1);
			buf->length -= n - slen;
		}
	} else {
		/* contents が長くなる場合 */
		memcpy(buf->contents + i, s, n);
		sb_insert(buf, i + n, s + n);
	}
}

/* 文字列をフォーマットして、文字列バッファの末尾に付け加える。
 * format や ap 内の引数が buf->contents の一部であってはならない。 */
int sb_vprintf(struct strbuf *restrict buf,
		const char *restrict format, va_list ap)
{
	va_list ap2;
	va_copy(ap2, ap);

	ssize_t rest = buf->maxlength - buf->length + 1;
	int result = vsnprintf(buf->contents + buf->length, rest, format, ap);

	if (result >= rest) {  /* バッファが足りない */
		do
			buf->maxlength = buf->maxlength * 2 + 1;
		while (result + buf->length > buf->maxlength);
		buf->contents = xrealloc(buf->contents, buf->maxlength + 1);
		rest = buf->maxlength - buf->length + 1;
		result = vsnprintf(buf->contents + buf->length, rest, format, ap2);
	}
	if (result >= 0)
		buf->length += result;
	buf->contents[buf->length] = '\0';  /* just in case */
	va_end(ap2);
	return result;
}

/* 文字列をフォーマットして、文字列バッファの末尾に付け加える。
 * format やその他の引数が buf->contents の一部であってはならない。 */
int sb_printf(struct strbuf *restrict buf, const char *restrict format, ...)
{
	va_list ap;
	int result;

	va_start(ap, format);
	result = sb_vprintf(buf, format, ap);
	va_end(ap);
	return result;
}

/* strftime の結果を文字列に付け足す。
 * ただし結果が 100 文字以上なら何も付け足されない。
 * 戻り値: 増えた文字数 */
size_t sb_strftime(struct strbuf *restrict buf,
		const char *restrict format, const struct tm *restrict tm)
{
	char result[100];
	size_t count = strftime(result, sizeof result, format, tm);
	sb_append(buf, result);
	return count;
}


/********** ポインタリスト **********/

/* ポインタリストはポインタを要素とする可変長配列である。
 * struct plist list に対して、list.contents[list.length] は常に NULL である。
 * しかし、これはリストの要素に NULL ポインタを含めることを妨げない。 */

/* 未初期化のポインタリストを初期化する。 */
void pl_init(struct plist *list)
{
	list->contents = xmalloc((PLIST_INITSIZE + 1) * sizeof(void *));
	list->contents[0] = NULL;
	list->length = 0;
	list->maxlength = PLIST_INITSIZE;
}

/* 初期化済のポインタリストの内容を削除し、未初期化状態に戻す。
 * 配列内の各要素は解放されないので注意。 */
void pl_destroy(struct plist *list)
{
	free(list->contents);
}

/* ポインタリストを解放し、内容を返す。ポインタリストは未初期化状態になる。
 * 戻り値: ポインタリストに入っていた配列。この配列は呼び出し元で free
 *         すべし。 */
void **pl_toary(struct plist *list)
{
	if (list->length + 10 < list->maxlength)
		pl_trim(list);

	void **result = list->contents;
	return result;
}

/* ポインタリストの maxlength を変更する。短くしすぎると配列の末尾が消える */
void pl_setmax(struct plist *list, size_t newmax)
{
	list->maxlength = newmax;
	list->contents = xrealloc(list->contents, (newmax + 1) * sizeof(void *));
	list->contents[newmax] = NULL;
}

/* ポインタリストの大きさを実際の内容ぎりぎりにする。 */
void pl_trim(struct plist *list)
{
	list->maxlength = list->length;
	list->contents = xrealloc(list->contents,
			(list->maxlength + 1) * sizeof(void *));
}

/* ポインタリストを空にする。maxlength は変わらない。
 * 配列内の各要素は解放されないので注意。 */
void pl_clear(struct plist *list)
{
	list->length = 0;
	list->contents[list->length] = NULL;
}

/* ポインタリスト内の前から i 要素目に要素 e を挿入する。
 * i が大きすぎて配列の末尾を越えていれば、配列の末尾に e を付け加える。 */
void pl_insert(struct plist *list, size_t i, const void *e)
{
	if (i > list->length)
		i = list->length;
	if (list->length == list->maxlength)
		pl_setmax(list, list->maxlength * 2 + 1);
	assert(list->length < list->maxlength);
	memmove(list->contents + i + 1, list->contents + i,
			sizeof(void *) * (list->length - i));
	list->contents[i] = (void *) e;
	list->length++;
	list->contents[list->length] = NULL;
}

/* ポインタリスト内の前から i 要素目に、配列 *ps の最初の n 個の要素を挿入する。
 * i が大きすぎて配列の末尾を越えていれば、配列の末尾に要素を付け加える。
 * 配列 *ps は、必ず n 個の要素がなければならない
 * (途中で NULL 要素が出たらそこで挿入が終わるということはない)。
 * ps は list->contents の一部であってはならない。 */
void pl_aninsert(struct plist *list, size_t i, void *const *ps, size_t n)
{
	if (n + list->length > list->maxlength) {
		do
			list->maxlength = list->maxlength * 2 + 1;
		while (n + list->length > list->maxlength);
		list->contents = xrealloc(list->contents, 
				(list->maxlength + 1) * sizeof(void *));
	}
	if (i > list->length)
		i = list->length;
	memmove(list->contents + i + n, list->contents + i,
			sizeof(void *) * (list->length - i));
	memcpy(list->contents + i, ps, sizeof(void *) * n);
	list->length += n;
	list->contents[list->length] = NULL;
}

/* ポインタリスト内の配列の末尾に要素 e を付け加える。 */
void pl_append(struct plist *list, const void *e)
{
	pl_insert(list, SIZE_MAX, e);
}

/* ポインタリストの末尾に、配列 *ps の最初の n 個の要素を挿入する。
 * 配列 *ps は、必ず n 個の要素がなければならない
 * (途中で NULL 要素が出たらそこで挿入が終わるということはない)。*/
void pl_anappend(struct plist *list, void *const *ps, size_t n)
{
	pl_aninsert(list, SIZE_MAX, ps, n);
}

/* ポインタリスト内の前から i 要素目に、配列 *ps の要素を挿入する。
 * i が大きすぎて配列の末尾を越えていれば、配列の末尾に要素を付け加える。
 * 配列 *ps は、NULL 要素終端でなければならない。
 * ps は list->contents の一部であってはならない。 */
void pl_ainsert(struct plist *list, size_t i, void *const *ps)
{
	pl_aninsert(list, i, ps, parylen(ps));
}

/* ポインタリストの末尾に、配列 *ps の要素を挿入する。
 * 配列 *ps は、NULL 要素終端でなければならない。
 * ps は list->contents の一部であってはならない。 */
void pl_aappend(struct plist *list, void *const *ps)
{
	pl_aninsert(list, SIZE_MAX, ps, parylen(ps));
}

/* ポインタリストの i 要素目から count 個の要素を削除する。
 * 要素そのものは free しないので注意。 */
void pl_remove(struct plist *list, size_t i, size_t count)
{
	if (i < list->length) {
		if (count >= list->length - i) {
			list->contents[i] = NULL;
			list->length = i;
		} else {
			size_t rest = list->length - i - count;
			memmove(list->contents + i, list->contents + i + count,
					(rest + 1) * sizeof *list->contents);
			list->length -= count;
		}
	}
}


/********** Hashtable **********/

#define NOTHING ((ssize_t) -1)

/* ハッシュ関数 */
static unsigned ht_hashstr(const char *s)
{
	unsigned h = 0;
	while (*s) {
		h = (h * 31 + (unsigned) *s) ^ 0x55555555u;
		s++;
	}
	return h;
}

/* 未初期化のハッシュテーブルを初期化する。 */
void ht_init(struct hasht *ht)
{
	ht->capacity = HASHT_INITSIZE;
	ht->indices = xmalloc(HASHT_INITSIZE * sizeof(ssize_t));
	ht->entries = xmalloc(HASHT_INITSIZE * sizeof(struct hash_entry));

	ht->count = 0;
	ht->nullindex = NOTHING;
	ht->tailindex = 0;

	for (size_t i = 0, len = ht->capacity; i < len; i++) {
		ht->indices[i] = NOTHING;
		ht->entries[i] = (struct hash_entry) {
			.next = NOTHING,
			.hash = 0,
			.key = NULL,
			.value = NULL,
		};
	}
}

/* ハッシュテーブルを解放する。各エントリーは解放されないので、
 * 必要に応じてあらかじめ ht_each などを利用して解放しておくこと。 */
void ht_destroy(struct hasht *ht)
{
	for (size_t i = 0, len = ht->capacity; i < len; i++)
		free(ht->entries[i].key);
	free(ht->indices);
	free(ht->entries);
}

/* ハッシュテーブルの容量を変更する。 */
static void ht_rehash(struct hasht *ht, size_t newcap)
{
	assert(newcap > 0 && newcap >= ht->count);

	size_t oldcap = ht->capacity;
	ssize_t *oldindices = ht->indices;
	struct hash_entry *oldentries = ht->entries;
	ssize_t *newindices = xmalloc(newcap * sizeof(ssize_t));
	struct hash_entry *newentries = xmalloc(newcap * sizeof(struct hash_entry));
	ssize_t tail = 0;

	for (size_t i = 0; i < newcap; i++) {
		newindices[i] = NOTHING;
		newentries[i] = (struct hash_entry) {
			.next = NOTHING,
			.hash = 0,
			.key = NULL,
			.value = NULL,
		};
	}
	for (size_t i = 0; i < oldcap; i++) {
		char *key = oldentries[i].key;
		if (key) {
			unsigned hash = oldentries[i].hash;
			int newindex = hash % newcap;
			newentries[tail] = (struct hash_entry) {
				.next = newindices[newindex],
				.hash = hash,
				.key = key,
				.value = oldentries[i].value,
			};
			newindices[newindex] = tail;
			tail++;
		}
	}

	free(oldindices);
	free(oldentries);
	ht->capacity = newcap;
	ht->tailindex = tail;
	ht->indices = newindices;
	ht->entries = newentries;
}

/* ハッシュテーブルが少なくとも cap 以上の容量を持つようにする。 */
void ht_ensurecap(struct hasht *ht, size_t cap)
{
	size_t newcap = ht->capacity;
	while (newcap < cap)
		newcap = newcap * 2 + 1;
	ht_rehash(ht, newcap);
}

/* ハッシュテーブルの容量を要素の個数ぎりぎりに縮小する */
void ht_trim(struct hasht *ht)
{
	ht_rehash(ht, ht->count);
}

/* 各エントリに freer を適用し全てのエントリを削除する。容量は変わらない。
 * freer が NULL なら ht_clear に同じ。 */
void ht_freeclear(struct hasht *ht, void (*freer)(void *value))
{
	ht->count = 0;
	ht->nullindex = NOTHING;
	ht->tailindex = 0;

	for (size_t i = 0, len = ht->capacity; i < len; i++) {
		if (freer && ht->entries[i].key)
			freer(ht->entries[i].value);
		free(ht->entries[i].key);
		ht->indices[i] = NOTHING;
		ht->entries[i] = (struct hash_entry) {
			.next = NOTHING,
			.hash = 0,
			.key = NULL,
			.value = NULL,
		};
	}
}

/* ハッシュテーブルを空にする。各エントリーの値は解放されないので、
 * 必要に応じてあらかじめ ht_each などを利用して解放しておくこと。
 * 容量は変わらない。 */
void ht_clear(struct hasht *ht)
{
	ht_freeclear(ht, NULL);
}

/* ハッシュテーブルの値を取得する。対応する要素がない場合は NULL を返す。
 * また、key が NULL のときも NULL を返す。 */
void *ht_get(struct hasht *ht, const char *key)
{
	if (!key)
		return NULL;

	unsigned hash = ht_hashstr(key);
	ssize_t index = ht->indices[hash % ht->capacity];
	while (index >= 0) {
		struct hash_entry *entry = &ht->entries[index];
		if (entry->hash == hash && strcmp(entry->key, key) == 0)
			return entry->value;
		index = entry->next;
	}
	return NULL;
}

/* ハッシュテーブルに値を設定する。key は内部で strdup される。
 * key が NULL なら何もしないで NULL を返す。
 * 戻り値: もともと key に設定されていた値 */
void *ht_set(struct hasht *ht, const char *key, const void *value)
{
	if (!key)
		return NULL;

	/* まず、既存のエントリがあるならそれを置き換える。 */
	unsigned hash = ht_hashstr(key);
	size_t ii = hash % ht->capacity;
	ssize_t index = ht->indices[ii];
	while (index >= 0) {
		struct hash_entry *entry = &ht->entries[index];
		if (entry->hash == hash && strcmp(entry->key, key) == 0) {
			void *oldvalue = entry->value;
			entry->value = (void *) value;
			return oldvalue;
		}
		index = entry->next;
	}

	ht->count++;

	/* null entry があればそこに追加する。 */
	if (ht->nullindex >= 0) {
		struct hash_entry *entry = &ht->entries[index = ht->nullindex];
		ht->nullindex = entry->next;
		*entry = (struct hash_entry) {
			.next = ht->indices[ii],
			.hash = hash,
			.key = xstrdup(key),
			.value = (void *) value,
		};
		ht->indices[ii] = index;
		return NULL;
	}

	/* null entry がなければ tail entry に追加する。 */
	ht_ensurecap(ht, ht->count + 1);
	ii = hash % ht->capacity;
	ht->entries[ht->tailindex] = (struct hash_entry) {
		.next = ht->indices[ii],
		.hash = hash,
		.key = xstrdup(key),
		.value = (void *) value,
	};
	ht->indices[ii] = ht->tailindex;
	ht->tailindex++;
	return NULL;
}

/* ハッシュテーブルからエントリを削除する。
 * 戻り値は削除したエントリに入っていた値。 */
void *ht_remove(struct hasht *ht, const char *key)
{
	if (!key)
		return NULL;

	unsigned hash = ht_hashstr(key);
	ssize_t *indexp = &ht->indices[hash % ht->capacity];
	while (*indexp >= 0) {
		struct hash_entry *entry = &ht->entries[*indexp];
		if (entry->hash == hash && strcmp(entry->key, key) == 0) {
			void *oldvalue = entry->value;
			ssize_t index = *indexp;
			*indexp = entry->next;
			entry->next = ht->nullindex;
			ht->nullindex = index;
			free(entry->key);
			entry->hash = 0;
			entry->key = NULL;
			entry->value = NULL;
			ht->count--;
			return oldvalue;
		}
		indexp = &entry->next;
	}
	return NULL;
}

/* ハッシュテーブル内の全エントリに対して関数 f を呼び出す。
 * f の結果が非 0 ならば、この関数はただちにその値を返す。
 * 全ての f の呼び出しが 0 を返せば、この関数も 0 を返す。
 * この関数の実行中に ht の要素を追加・削除してはならない。 */
int ht_each(struct hasht *ht, int (*f)(const char *key, void *value))
{
	for (size_t i = 0, len = ht->capacity; i < len; i++) {
		struct hash_entry *entry = &ht->entries[i];
		if (entry->key) {
			int r = f(entry->key, entry->value);
			if (r) return r;
		}
	}
	return 0;
}

/* ハッシュテーブルの内容を列挙する。
 * 最初に列挙を開始する前に、size_t 変数を 0 に初期化しておく。
 * その後 struct hasht と size_t へのポインタをこの関数に渡す度にキーと値の
 * ペアが返される。size_t 変数は列挙がどこまで進んだかを覚えておくために
 * この関数が書き換える。列挙の途中でこの関数の外から size_t 変数の値を
 * 変えてはならない。また、列挙の途中でハッシュテーブルの値を追加・削除しては
 * ならない。
 * 全ての列挙が終わると key が NULL の keyvaluepair が返る。 */
struct keyvaluepair ht_next(struct hasht *ht, size_t *indexp)
{
	while (*indexp < ht->capacity) {
		struct keyvaluepair kv = {
			.key   = ht->entries[*indexp].key,
			.value = ht->entries[*indexp].value,
		};
		(*indexp)++;
		if (kv.key)
			return kv;
	}
	return (struct keyvaluepair) { .key = NULL };
}
