/* Yash: yet another shell */
/* strbuf.h: modifiable string buffer */
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


#ifndef STRBUF_H
#define STRBUF_H

#include <stddef.h>
#include <stdint.h>


typedef struct {
	char *contents;
	size_t length, maxlength;
} xstrbuf_t;
typedef struct {
	wchar_t *contents;
	size_t length, maxlength;
} xwcsbuf_t;

extern xstrbuf_t *sb_init(xstrbuf_t *buf)
	__attribute__((nonnull));
extern xstrbuf_t *sb_initwith(xstrbuf_t *restrict buf, char *restrict s)
	__attribute__((nonnull));
extern void sb_destroy(xstrbuf_t *buf)
	__attribute__((nonnull));
extern char *sb_tostr(xstrbuf_t *buf)
	__attribute__((nonnull));
extern xstrbuf_t *sb_setmax(xstrbuf_t *buf, size_t newmax)
	__attribute__((nonnull));
extern inline xstrbuf_t *sb_ensuremax(xstrbuf_t *buf, size_t max)
	__attribute__((nonnull));
extern xstrbuf_t *sb_clear(xstrbuf_t *buf)
	__attribute__((nonnull));
extern xstrbuf_t *sb_replace(
		xstrbuf_t *restrict buf, size_t i, size_t bn,
		const char *restrict s, size_t sn)
	__attribute__((nonnull));
static inline xstrbuf_t *sb_ninsert(
		xstrbuf_t *restrict buf, size_t i, const char *restrict s, size_t n)
	__attribute__((nonnull));
static inline xstrbuf_t *sb_insert(
		xstrbuf_t *restrict buf, size_t i, const char *restrict s)
	__attribute__((nonnull));
static inline xstrbuf_t *sb_ncat(
		xstrbuf_t *restrict buf, const char *restrict s, size_t n)
	__attribute__((nonnull));
static inline xstrbuf_t *sb_cat(
		xstrbuf_t *restrict buf, const char *restrict s)
	__attribute__((nonnull));
extern xstrbuf_t *sb_ccat(xstrbuf_t *buf, char c)
	__attribute__((nonnull));
extern wchar_t *sb_wcscat(
		xstrbuf_t *restrict buf,
		const wchar_t *restrict s, mbstate_t *restrict ps)
	__attribute__((nonnull(1)));
extern int sb_vprintf(
		xstrbuf_t *restrict buf, const char *restrict format, va_list ap)
	__attribute__((nonnull(1,2)));
extern int sb_printf(
		xstrbuf_t *restrict buf, const char *restrict format, ...)
	__attribute__((nonnull(1,2)));
extern size_t sb_strftime(
		xstrbuf_t *restrict buf,
		const char *restrict format, const struct tm *restrict tm)
	__attribute__((nonnull));

extern xwcsbuf_t *wb_init(xwcsbuf_t *buf)
	__attribute__((nonnull));
extern xwcsbuf_t *wb_initwith(xwcsbuf_t *restrict buf, wchar_t *restrict s)
	__attribute__((nonnull));
extern void wb_destroy(xwcsbuf_t *buf)
	__attribute__((nonnull));
extern wchar_t *wb_towcs(xwcsbuf_t *buf)
	__attribute__((nonnull));
extern xwcsbuf_t *wb_setmax(xwcsbuf_t *buf, size_t newmax)
	__attribute__((nonnull));
extern xwcsbuf_t *wb_clear(xwcsbuf_t *buf)
	__attribute__((nonnull));
extern xwcsbuf_t *wb_replace(
		xwcsbuf_t *restrict buf, size_t i, size_t bn,
		const wchar_t *restrict s, size_t sn)
	__attribute__((nonnull));
static inline xwcsbuf_t *wb_ninsert(
		xwcsbuf_t *restrict buf, size_t i, const wchar_t *restrict s, size_t n)
	__attribute__((nonnull));
static inline xwcsbuf_t *wb_insert(
		xwcsbuf_t *restrict buf, size_t i, const wchar_t *restrict s)
	__attribute__((nonnull));
static inline xwcsbuf_t *wb_ncat(
		xwcsbuf_t *restrict buf, const wchar_t *restrict s, size_t n)
	__attribute__((nonnull));
static inline xwcsbuf_t *wb_cat(
		xwcsbuf_t *restrict buf, const wchar_t *restrict s)
	__attribute__((nonnull));
extern xwcsbuf_t *wb_wccat(xwcsbuf_t *buf, wchar_t c)
	__attribute__((nonnull));
extern char *wb_mbscat(xwcsbuf_t *restrict buf, const char *restrict s)
	__attribute__((nonnull));


/* マルチバイト文字列 s の最初の n バイトを
 * バッファの i バイト目の手前に挿入する。
 * strlen(s) < n ならば s 全体を挿入する。
 * buf->length <= i ならば文字列の末尾に追加する。
 * s は buf->contents の一部であってはならない。 */
static inline xstrbuf_t *sb_ninsert(
		xstrbuf_t *restrict buf, size_t i, const char *restrict s, size_t n)
{
	return sb_replace(buf, i, 0, s, n);
}

/* マルチバイト文字列 s をバッファの i バイト目の手前に挿入する。
 * buf->length <= i ならば文字列の末尾に追加する。
 * s は buf->contents の一部であってはならない。 */
static inline xstrbuf_t *sb_insert(
		xstrbuf_t *restrict buf, size_t i, const char *restrict s)
{
	return sb_replace(buf, i, 0, s, SIZE_MAX);
}

/* マルチバイト文字列 s の最初の n バイトを文字列バッファに追加する。
 * strlen(s) < n ならば s 全体を追加する。
 * s は buf->contents の一部であってはならない。 */
static inline xstrbuf_t *sb_ncat(
		xstrbuf_t *restrict buf, const char *restrict s, size_t n)
{
	return sb_replace(buf, SIZE_MAX, 0, s, n);
}

/* マルチバイト文字列 s を文字列バッファに追加する。
 * s は buf->contents の一部であってはならない。 */
static inline xstrbuf_t *sb_cat(
		xstrbuf_t *restrict buf, const char *restrict s)
{
	return sb_replace(buf, SIZE_MAX, 0, s, SIZE_MAX);
}


/* ワイド文字列 s の最初の n 文字をバッファの i 文字目の手前に挿入する。
 * wcslen(s) < n ならば s 全体を挿入する。
 * buf->length <= i ならば文字列の末尾に追加する。
 * s は buf->contents の一部であってはならない。 */
static inline xwcsbuf_t *wb_ninsert(
		xwcsbuf_t *restrict buf, size_t i, const wchar_t *restrict s, size_t n)
{
	return wb_replace(buf, i, 0, s, n);
}

/* ワイド文字列 s をバッファの i 文字目の手前に挿入する。
 * buf->length <= i ならば文字列の末尾に追加する。
 * s は buf->contents の一部であってはならない。 */
static inline xwcsbuf_t *wb_insert(
		xwcsbuf_t *restrict buf, size_t i, const wchar_t *restrict s)
{
	return wb_replace(buf, i, 0, s, SIZE_MAX);
}

/* ワイド文字列 s の最初の n 文字を文字列バッファに追加する。
 * wcslen(s) < n ならば s 全体を追加する。
 * s は buf->contents の一部であってはならない。 */
static inline xwcsbuf_t *wb_ncat(
		xwcsbuf_t *restrict buf, const wchar_t *restrict s, size_t n)
{
	return wb_replace(buf, SIZE_MAX, 0, s, n);
}

/* ワイド文字列 s を文字列バッファに追加する。
 * s は buf->contents の一部であってはならない。 */
static inline xwcsbuf_t *wb_cat(
		xwcsbuf_t *restrict buf, const wchar_t *restrict s)
{
	return wb_replace(buf, SIZE_MAX, 0, s, SIZE_MAX);
}


#endif /* STRBUF_H */
