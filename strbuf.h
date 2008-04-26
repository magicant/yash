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

#include <stdarg.h>
#include <stdint.h>
#include <wchar.h>


typedef struct xstrbuf_T {
    char *contents;
    size_t length, maxlength;
} xstrbuf_T;
typedef struct xwcsbuf_T {
    wchar_t *contents;
    size_t length, maxlength;
} xwcsbuf_T;

extern xstrbuf_T *sb_init(xstrbuf_T *buf)
    __attribute__((nonnull));
extern xstrbuf_T *sb_initwith(xstrbuf_T *restrict buf, char *restrict s)
    __attribute__((nonnull));
extern void sb_destroy(xstrbuf_T *buf)
    __attribute__((nonnull));
extern char *sb_tostr(xstrbuf_T *buf)
    __attribute__((nonnull));
extern xstrbuf_T *sb_setmax(xstrbuf_T *buf, size_t newmax)
    __attribute__((nonnull));
extern inline xstrbuf_T *sb_ensuremax(xstrbuf_T *buf, size_t max)
    __attribute__((nonnull));
extern xstrbuf_T *sb_clear(xstrbuf_T *buf)
    __attribute__((nonnull));
extern xstrbuf_T *sb_replace(
	xstrbuf_T *restrict buf, size_t i, size_t bn,
	const char *restrict s, size_t sn)
    __attribute__((nonnull));
static inline xstrbuf_T *sb_ninsert(
	xstrbuf_T *restrict buf, size_t i, const char *restrict s, size_t n)
    __attribute__((nonnull));
static inline xstrbuf_T *sb_insert(
	xstrbuf_T *restrict buf, size_t i, const char *restrict s)
    __attribute__((nonnull));
static inline xstrbuf_T *sb_ncat(
	xstrbuf_T *restrict buf, const char *restrict s, size_t n)
    __attribute__((nonnull));
static inline xstrbuf_T *sb_cat(
	xstrbuf_T *restrict buf, const char *restrict s)
    __attribute__((nonnull));
static inline xstrbuf_T *sb_catfree(
	xstrbuf_T *restrict buf, char *restrict s)
    __attribute__((nonnull));
static inline xstrbuf_T *sb_remove(xstrbuf_T *buf, size_t i, size_t n)
    __attribute__((nonnull));
extern xstrbuf_T *sb_ccat(xstrbuf_T *buf, char c)
    __attribute__((nonnull));
extern wchar_t *sb_wcscat(
	xstrbuf_T *restrict buf,
	const wchar_t *restrict s, mbstate_t *restrict ps)
    __attribute__((nonnull(1)));
extern int sb_vprintf(
	xstrbuf_T *restrict buf, const char *restrict format, va_list ap)
    __attribute__((nonnull(1,2),format(printf,2,0)));
extern int sb_printf(
	xstrbuf_T *restrict buf, const char *restrict format, ...)
    __attribute__((nonnull(1,2),format(printf,2,3)));
extern size_t sb_strftime(
	xstrbuf_T *restrict buf,
	const char *restrict format, const struct tm *restrict tm)
    __attribute__((nonnull,format(strftime,2,0)));

extern xwcsbuf_T *wb_init(xwcsbuf_T *buf)
    __attribute__((nonnull));
extern xwcsbuf_T *wb_initwith(xwcsbuf_T *restrict buf, wchar_t *restrict s)
    __attribute__((nonnull));
extern void wb_destroy(xwcsbuf_T *buf)
    __attribute__((nonnull));
extern wchar_t *wb_towcs(xwcsbuf_T *buf)
    __attribute__((nonnull));
extern xwcsbuf_T *wb_setmax(xwcsbuf_T *buf, size_t newmax)
    __attribute__((nonnull));
extern xwcsbuf_T *wb_ensuremax(xwcsbuf_T *buf, size_t max)
    __attribute__((nonnull));
extern xwcsbuf_T *wb_clear(xwcsbuf_T *buf)
    __attribute__((nonnull));
extern xwcsbuf_T *wb_replace(
	xwcsbuf_T *restrict buf, size_t i, size_t bn,
	const wchar_t *restrict s, size_t sn)
    __attribute__((nonnull));
static inline xwcsbuf_T *wb_ninsert(
	xwcsbuf_T *restrict buf, size_t i, const wchar_t *restrict s, size_t n)
    __attribute__((nonnull));
static inline xwcsbuf_T *wb_insert(
	xwcsbuf_T *restrict buf, size_t i, const wchar_t *restrict s)
    __attribute__((nonnull));
static inline xwcsbuf_T *wb_ncat(
	xwcsbuf_T *restrict buf, const wchar_t *restrict s, size_t n)
    __attribute__((nonnull));
static inline xwcsbuf_T *wb_cat(
	xwcsbuf_T *restrict buf, const wchar_t *restrict s)
    __attribute__((nonnull));
static inline xwcsbuf_T *wb_catfree(
	xwcsbuf_T *restrict buf, wchar_t *restrict s)
    __attribute__((nonnull));
static inline xwcsbuf_T *wb_remove(xwcsbuf_T *buf, size_t i, size_t n)
    __attribute__((nonnull));
extern xwcsbuf_T *wb_wccat(xwcsbuf_T *buf, wchar_t c)
    __attribute__((nonnull));
extern char *wb_mbscat(xwcsbuf_T *restrict buf, const char *restrict s)
    __attribute__((nonnull));
extern int wb_vwprintf(
	xwcsbuf_T *restrict buf, const wchar_t *restrict format, va_list ap)
    __attribute__((nonnull(1,2)));
extern int wb_wprintf(
	xwcsbuf_T *restrict buf, const wchar_t *restrict format, ...)
    __attribute__((nonnull(1,2)));

extern char *malloc_wcsntombs(const wchar_t *s, size_t n)
    __attribute__((nonnull,malloc,warn_unused_result));
static inline char *malloc_wcstombs(const wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));
static inline char *realloc_wcstombs(wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));
extern wchar_t *malloc_mbsntowcs(const char *s, size_t n)
    __attribute__((nonnull,malloc,warn_unused_result));
static inline wchar_t *malloc_mbstowcs(const char *s)
    __attribute__((nonnull,malloc,warn_unused_result));
static inline wchar_t *realloc_mbstowcs(char *s)
    __attribute__((nonnull,malloc,warn_unused_result));

static inline char *malloc_vprintf(const char *format, va_list ap)
    __attribute__((nonnull(1),malloc,warn_unused_result,format(printf,1,0)));
static inline char *malloc_printf(const char *format, ...)
    __attribute__((nonnull(1),malloc,warn_unused_result,format(printf,1,2)));
static inline wchar_t *malloc_vwprintf(const wchar_t *format, va_list ap)
    __attribute__((nonnull(1),malloc,warn_unused_result));
static inline wchar_t *malloc_wprintf(const wchar_t *format, ...)
    __attribute__((nonnull(1),malloc,warn_unused_result));


/* マルチバイト文字列 s の最初の n バイトを
 * バッファの i バイト目の手前に挿入する。
 * strlen(s) < n ならば s 全体を挿入する。
 * buf->length <= i ならば文字列の末尾に追加する。
 * s は buf->contents の一部であってはならない。 */
xstrbuf_T *sb_ninsert(
	xstrbuf_T *restrict buf, size_t i, const char *restrict s, size_t n)
{
    return sb_replace(buf, i, 0, s, n);
}

/* マルチバイト文字列 s をバッファの i バイト目の手前に挿入する。
 * buf->length <= i ならば文字列の末尾に追加する。
 * s は buf->contents の一部であってはならない。 */
xstrbuf_T *sb_insert(xstrbuf_T *restrict buf, size_t i, const char *restrict s)
{
    return sb_replace(buf, i, 0, s, SIZE_MAX);
}

/* マルチバイト文字列 s の最初の n バイトを文字列バッファに追加する。
 * strlen(s) < n ならば s 全体を追加する。
 * s は buf->contents の一部であってはならない。 */
xstrbuf_T *sb_ncat(xstrbuf_T *restrict buf, const char *restrict s, size_t n)
{
    return sb_replace(buf, SIZE_MAX, 0, s, n);
}

/* マルチバイト文字列 s を文字列バッファに追加する。
 * s は buf->contents の一部であってはならない。 */
xstrbuf_T *sb_cat(xstrbuf_T *restrict buf, const char *restrict s)
{
    return sb_replace(buf, SIZE_MAX, 0, s, SIZE_MAX);
}

/* マルチバイト文字列を文字列バッファに追加した後 free する。
 * s は buf->contents の一部であってはならない。 */
xstrbuf_T *sb_catfree(xstrbuf_T *restrict buf, char *restrict s)
{
    extern void free(void *ptr);

    sb_cat(buf, s);
    free(s);
    return buf;
}

/* マルチバイト文字列バッファの i バイト目から n バイトを削除する。
 * buf->length <= i ならば何もしない。
 * buf->length <= i + n ならば i バイト目以降全てを削除する。 */
xstrbuf_T *sb_remove(xstrbuf_T *buf, size_t i, size_t n)
{
    return sb_replace(buf, i, n, "", 0);
}


/* ワイド文字列 s の最初の n 文字をバッファの i 文字目の手前に挿入する。
 * wcslen(s) < n ならば s 全体を挿入する。
 * buf->length <= i ならば文字列の末尾に追加する。
 * s は buf->contents の一部であってはならない。 */
xwcsbuf_T *wb_ninsert(
	xwcsbuf_T *restrict buf, size_t i, const wchar_t *restrict s, size_t n)
{
    return wb_replace(buf, i, 0, s, n);
}

/* ワイド文字列 s をバッファの i 文字目の手前に挿入する。
 * buf->length <= i ならば文字列の末尾に追加する。
 * s は buf->contents の一部であってはならない。 */
xwcsbuf_T *wb_insert(
	xwcsbuf_T *restrict buf, size_t i, const wchar_t *restrict s)
{
    return wb_replace(buf, i, 0, s, SIZE_MAX);
}

/* ワイド文字列 s の最初の n 文字を文字列バッファに追加する。
 * wcslen(s) < n ならば s 全体を追加する。
 * s は buf->contents の一部であってはならない。 */
xwcsbuf_T *wb_ncat(xwcsbuf_T *restrict buf, const wchar_t *restrict s, size_t n)
{
    return wb_replace(buf, SIZE_MAX, 0, s, n);
}

/* ワイド文字列 s を文字列バッファに追加する。
 * s は buf->contents の一部であってはならない。 */
xwcsbuf_T *wb_cat(xwcsbuf_T *restrict buf, const wchar_t *restrict s)
{
    return wb_replace(buf, SIZE_MAX, 0, s, SIZE_MAX);
}

/* ワイド文字列を文字列バッファに追加した後 free する。
 * s は buf->contents の一部であってはならない。 */
xwcsbuf_T *wb_catfree(xwcsbuf_T *restrict buf, wchar_t *restrict s)
{
    extern void free(void *ptr);

    wb_cat(buf, s);
    free(s);
    return buf;
}

/* ワイド文字列バッファの i 文字目から n 文字を削除する。
 * buf->length <= i ならば何もしない。
 * buf->length <= i + n ならば i 文字目以降全てを削除する。 */
xwcsbuf_T *wb_remove(xwcsbuf_T *buf, size_t i, size_t n)
{
    return wb_replace(buf, i, n, L"", 0);
}

/* ワイド文字列 s をマルチバイト文字列に変換し、
 * 新しく malloc した文字列として返す。
 * 変換の際にエラーがあると NULL を返す。
 * 返すマルチバイト文字列は、初期シフト状態で始まり、終わる。 */
char *malloc_wcstombs(const wchar_t *s)
{
    return malloc_wcsntombs(s, SIZE_MAX);
}

/* ワイド文字列を直接マルチバイト文字列に変換する。
 * 返すマルチバイト文字列は、初期シフト状態で始まり、終わる。
 * 引数で与えた領域は適切に realloc される。
 * 変換に失敗すると NULL を返すが、いずれにしても元の文字列の領域は解放される */
char *realloc_wcstombs(wchar_t *s)
{
    extern void free(void *ptr);
    char *result = malloc_wcstombs(s);
    free(s);
    return result;
}

/* マルチバイト文字列 s をワイド文字列に変換し、
 * 新しく malloc した文字列として返す。
 * 変換の際にエラーがあると NULL を返す。 */
wchar_t *malloc_mbstowcs(const char *s)
{
    return malloc_mbsntowcs(s, SIZE_MAX);
}

/* マルチバイト文字列を直接ワイド文字列に変換する。
 * 引数で与えた領域は適切に realloc される。
 * 変換に失敗すると NULL を返すが、いずれにしても元の文字列の領域は解放される */
wchar_t *realloc_mbstowcs(char *s)
{
    extern void free(void *ptr);
    wchar_t *result = malloc_mbstowcs(s);
    free(s);
    return result;
}

/* vprintf の結果を新しく malloc した文字列として返す。 */
char *malloc_vprintf(const char *format, va_list ap)
{
    xstrbuf_T buf;
    sb_init(&buf);
    sb_vprintf(&buf, format, ap);
    return sb_tostr(&buf);
}

/* printf の結果を新しく malloc した文字列として返す。 */
char *malloc_printf(const char *format, ...)
{
    va_list ap;
    char *result;
    va_start(ap, format);
    result = malloc_vprintf(format, ap);
    va_end(ap);
    return result;
}

/* vwprintf の結果を新しく malloc した文字列として返す。 */
wchar_t *malloc_vwprintf(const wchar_t *format, va_list ap)
{
    xwcsbuf_T buf;
    wb_init(&buf);
    wb_vwprintf(&buf, format, ap);
    return wb_towcs(&buf);
}

/* wprintf の結果を新しく malloc した文字列として返す。 */
wchar_t *malloc_wprintf(const wchar_t *format, ...)
{
    va_list ap;
    wchar_t *result;
    va_start(ap, format);
    result = malloc_vwprintf(format, ap);
    va_end(ap);
    return result;
}


#endif /* STRBUF_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
