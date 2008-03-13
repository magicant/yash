/* Yash: yet another shell */
/* strbuf.c: modifiable string buffer */
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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include "util.h"
#include "strbuf.h"

#define XSTRBUF_INITSIZE 15
#define XWCSBUF_INITSIZE 15


/* 特に明記しない限り、以下の関数は引数の buf を返す */


/********** マルチバイト文字列バッファ **********/

/* 未初期化のマルチバイト文字列バッファ *buf を空文字列で初期化する。*/
xstrbuf_t *sb_init(xstrbuf_t *buf)
{
	buf->contents = xmalloc((XSTRBUF_INITSIZE + 1) * sizeof (char));
	buf->contents[0] = '\0';
	buf->length = 0;
	buf->maxlength = XSTRBUF_INITSIZE;
	return buf;
}

/* 未初期化の文字列バッファ *buf をマルチバイト文字列 *s で初期化する。
 * s は free 可能な文字列へのポインタであり、この関数の呼出しの後は完全に
 * この関数で初期化するバッファの所有物となる。 */
xstrbuf_t *sb_initwith(xstrbuf_t *restrict buf, char *restrict s)
{
	buf->contents = s;
	buf->length = buf->maxlength = strlen(s);
	return buf;
}

/* マルチバイト文字列バッファ *buf を解放し、未初期化状態に戻す。 */
void sb_destroy(xstrbuf_t *buf)
{
	free(buf->contents);
}

/* マルチバイト文字列バッファ *buf を解放し、バッファの内容だった文字列を返す。
 * *buf は未初期化状態になる。戻り値は呼出し元が free すること。 */
char *sb_tostr(xstrbuf_t *buf)
{
	if (buf->maxlength - buf->length > 20)
		sb_setmax(buf, buf->length);
	return buf->contents;
}

/* buf->maxlength を変更する。newmax < buf->length ならば末尾が消える。 */
xstrbuf_t *sb_setmax(xstrbuf_t *buf, size_t newmax)
{
	buf->contents = xrealloc(buf->contents, (newmax + 1) * sizeof (char));
	buf->maxlength = newmax;
	buf->contents[newmax] = '\0';
	if (newmax < buf->length)
		buf->length = newmax;
	return buf;
}

/* buf->maxlength が max 未満なら、max 以上になるようにメモリを再確保する。 */
inline xstrbuf_t *sb_ensuremax(xstrbuf_t *buf, size_t max)
{
	if (buf->maxlength < max) {
		size_t newmax = buf->maxlength;
		do
			newmax = newmax * 2 + 1;
		while (newmax < max);
		return sb_setmax(buf, newmax);
	} else {
		return buf;
	}
}

/* マルチバイト文字列バッファを空にする。buf->maxlength は変わらない。 */
xstrbuf_t *sb_clear(xstrbuf_t *buf)
{
	buf->contents[buf->length = 0] = '\0';
	return buf;
}

/* マルチバイト文字列バッファの i バイト目から bn バイトを
 * マルチバイト文字列 s の最初の sn バイトに置換する。
 * strlen(s) < sn ならば s 全体に置換する。
 * buf->length < i + sn ならばバッファの i バイト目以降を全て置換する。
 * 特に、buf->length <= i ならば文字列の末尾に追加する。
 * s は buf->contents の一部であってはならない。 */
xstrbuf_t *sb_replace(
		xstrbuf_t *restrict buf, size_t i, size_t bn,
		const char *restrict s, size_t sn)
{
	sn = xstrnlen(s, sn);
	if (i > buf->length)
		i = buf->length;
	if (bn > buf->length - i)
		bn = buf->length - i;

	size_t newlength = buf->length - bn + sn;
	sb_ensuremax(buf, newlength);
	memmove(buf->contents + i + sn, buf->contents + i + bn,
			buf->length - (i + bn) + 1);
	memcpy(buf->contents + i, s, sn);
	buf->length = newlength;
	return buf;
}

/* バッファの末尾にバイト c を追加する。 */
xstrbuf_t *sb_ccat(xstrbuf_t *buf, char c)
{
	sb_ensuremax(buf, buf->length + 1);
	buf->contents[buf->length++] = c;
	buf->contents[buf->length] = '\0';
	return buf;
}

/* ワイド文字列をマルチバイト文字列に変換しバッファに追加する。
 * 変換に伴い、シフト状態 *ps を更新する。
 * 戻り値: ワイド文字列が全て正しく変換されたら NULL、
 *         エラーがあったら、最後に変換できた文字の次の文字へのポインタ。
 * エラーがあった場合でも、途中までは buf->contents に文字列が書き込まれる。
 * s が NULL なら、シフト状態を初期状態に戻す文字列を追加する。 */
wchar_t *sb_wcscat(xstrbuf_t *restrict buf,
		const wchar_t *restrict s, mbstate_t *restrict ps)
{
	size_t count;

	if (s) {
		for (;;) {
			count = wcsrtombs(buf->contents + buf->length,
					(const wchar_t **) &s,
					buf->maxlength - buf->length + 1,
					ps);
			if (count == (size_t) -1)
				break;
			buf->length += count;
			if (!s)
				break;
			sb_ensuremax(buf, buf->maxlength * 2);
		}
	} else {
		mbstate_t saveps = *ps;
		count = wcrtomb(NULL, L'\0', &saveps);
		if (count == (size_t) -1)
			return NULL;
		sb_ensuremax(buf, buf->length + count - 1);
		count = wcrtomb(buf->contents + buf->length, L'\0', ps);
		assert(0 < count && count - 1 <= buf->maxlength - buf->length);
		buf->length += count - 1;
	}

	assert(buf->contents[buf->length] == '\0');
	return (wchar_t *) s;
}

/* 文字列をフォーマットして、文字列バッファの末尾に付け加える。
 * format や ap 内の引数が buf->contents の一部であってはならない。
 * format, ap 引数と戻り値の意味は vsprintf に準じる。 */
int sb_vprintf(xstrbuf_t *restrict buf, const char *restrict format, va_list ap)
{
	va_list saveap;
	va_copy(saveap, ap);

	ssize_t rest = buf->maxlength - buf->length + 1;
	int result = vsnprintf(buf->contents + buf->length, rest, format, ap);

	if (result >= rest) {
		/* バッファが足りなかった場合 */
		sb_ensuremax(buf, buf->length + result);
		rest = buf->maxlength - buf->length + 1;
		result = vsnprintf(buf->contents + buf->length, rest, format, saveap);
	}
	assert(result <= rest - 1);
	if (result >= 0)
		buf->length += result;
	assert(buf->contents[buf->length] == '\0');
	va_end(saveap);
	return result;
}

/* 文字列をフォーマットして、文字列バッファの末尾に付け加える。
 * format やそれ以降の引数が buf->contents の一部であってはならない。
 * format やそれ以降の引数と戻り値の意味は sprintf に準じる。 */
int sb_printf(xstrbuf_t *restrict buf, const char *restrict format, ...)
{
	va_list ap;
	int result;

	va_start(ap, format);
	result = sb_vprintf(buf, format, ap);
	va_end(ap);
	return result;
}

/* strftime の結果を文字列バッファの末尾に付け加える。
 * format は、フォーマット結果が 0 文字となるようなものであってはならない。
 * 戻り値: 増えた文字数 */
size_t sb_strftime(xstrbuf_t *restrict buf,
		const char *restrict format, const struct tm *restrict tm)
{
	/* まず 40 バイトのバッファで変換できるか試す */
	char result[40];
	size_t count = strftime(result, sizeof result, format, tm);
	if (count) {
		sb_cat(buf, result);
		return count;
	}

	/* 結果が 40 文字を越える場合はバッファを増やしながらやり直す */
	size_t len = sizeof result;
	do {
		len *= 2;
		sb_ensuremax(buf, buf->length + len);
		count = strftime(buf->contents + buf->length,
				buf->maxlength - buf->length + 1,
				format,
				tm);
	} while (count == 0);
	return count;
}


/********** ワイド文字列バッファ **********/

/* 未初期化のワイド文字列バッファ *buf を空文字列で初期化する。*/
xwcsbuf_t *wb_init(xwcsbuf_t *buf)
{
	buf->contents = xmalloc((XWCSBUF_INITSIZE + 1) * sizeof (wchar_t));
	buf->contents[0] = L'\0';
	buf->length = 0;
	buf->maxlength = XWCSBUF_INITSIZE;
	return buf;
}

/* 未初期化のワイド文字列バッファ *buf をワイド文字列 *s で初期化する。
 * s は free 可能なワイド文字列へのポインタであり、この関数の呼出しの後は完全に
 * この関数で初期化するバッファの所有物となる。 */
xwcsbuf_t *wb_initwith(xwcsbuf_t *restrict buf, wchar_t *restrict s)
{
	buf->contents = s;
	buf->length = buf->maxlength = wcslen(s);
	return buf;
}

/* ワイド文字列バッファ *buf を解放し、未初期化状態に戻す。 */
void wb_destroy(xwcsbuf_t *buf)
{
	free(buf->contents);
}

/* ワイド文字列バッファ *buf を解放し、バッファの内容だった文字列を返す。
 * *buf は未初期化状態になる。戻り値は呼出し元が free すること。 */
wchar_t *wb_towcs(xwcsbuf_t *buf)
{
	if (buf->maxlength - buf->length > 20)
		wb_setmax(buf, buf->length);
	return buf->contents;
}

/* buf->maxlength を変更する。newmax < buf->length ならば末尾が消える。 */
xwcsbuf_t *wb_setmax(xwcsbuf_t *buf, size_t newmax)
{
	buf->contents = xrealloc(buf->contents, (newmax + 1) * sizeof (wchar_t));
	buf->maxlength = newmax;
	buf->contents[newmax] = L'\0';
	if (newmax < buf->length)
		buf->length = newmax;
	return buf;
}

/* ワイド文字列バッファを空にする。buf->maxlength は変わらない。 */
xwcsbuf_t *wb_clear(xwcsbuf_t *buf)
{
	buf->contents[buf->length = 0] = L'\0';
	return buf;
}

/* buf->maxlength が max 未満なら、max 以上になるようにメモリを再確保する。 */
static inline xwcsbuf_t *wb_ensuremax(xwcsbuf_t *buf, size_t max)
{
	if (buf->maxlength < max) {
		size_t newmax = buf->maxlength;
		do
			newmax = newmax * 2 + 1;
		while (newmax < max);
		return wb_setmax(buf, newmax);
	} else {
		return buf;
	}
}

/* バッファの i 文字目から bn 文字をワイド文字列 s の最初の sn 文字に置換する。
 * wcslen(s) < sn ならば s 全体に置換する。
 * buf->length < i + sn ならばバッファの i 文字目以降を全て置換する。
 * 特に、buf->length <= i ならば文字列の末尾に追加する。
 * s は buf->contents の一部であってはならない。 */
xwcsbuf_t *wb_replace(
		xwcsbuf_t *restrict buf, size_t i, size_t bn,
		const wchar_t *restrict s, size_t sn)
{
	sn = xwcsnlen(s, sn);
	if (i > buf->length)
		i = buf->length;
	if (bn > buf->length - i)
		bn = buf->length - i;

	size_t newlength = buf->length - bn + sn;
	wb_ensuremax(buf, newlength);
	wmemmove(buf->contents + i + sn, buf->contents + i + bn,
			buf->length - (i + bn) + 1);
	wmemcpy(buf->contents + i, s, sn);
	buf->length = newlength;
	return buf;
}

/* バッファの末尾にワイド文字 c を追加する。 */
xwcsbuf_t *wb_wccat(xwcsbuf_t *buf, wchar_t c)
{
	wb_ensuremax(buf, buf->length + 1);
	buf->contents[buf->length++] = c;
	buf->contents[buf->length] = L'\0';
	return buf;
}

/* マルチバイト文字列をワイド文字列に変換しバッファに追加する。
 * 文字列の変換は初期シフト状態で始まる。
 * 戻り値: マルチバイト文字列が全て正しく変換されたら NULL、
 *         エラーがあったら、最後に変換できた文字の次の文字へのポインタ
 * エラーがあった場合でも、途中までは buf->contents に文字列が書き込まれる。 */
char *wb_mbscat(xwcsbuf_t *restrict buf, const char *restrict s)
{
	mbstate_t state;
	size_t count;

	memset(&state, 0, sizeof state);  /* state を初期シフト状態で初期化 */

	for (;;) {
		count = mbsrtowcs(buf->contents + buf->length, (const char **) &s,
				buf->maxlength - buf->length + 1, &state);
		if (count == (size_t) -1)
			break;
		buf->length += count;
		if (!s)
			break;
		wb_ensuremax(buf, buf->maxlength * 2);
	}

	buf->contents[buf->length] = L'\0';
	return (char *) s;
}
