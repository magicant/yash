/* Yash: yet another shell */
/* strbuf.h: modifiable string buffer */
/* (C) 2007-2009 magicant */

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


#ifndef YASH_STRBUF_H
#define YASH_STRBUF_H

#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>

#define Size_max ((size_t) -1)  // = SIZE_MAX


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
static inline void sb_destroy(xstrbuf_T *buf)
    __attribute__((nonnull));
extern char *sb_tostr(xstrbuf_T *buf)
    __attribute__((nonnull));
extern xstrbuf_T *sb_setmax(xstrbuf_T *buf, size_t newmax)
    __attribute__((nonnull));
extern xstrbuf_T *sb_ensuremax(xstrbuf_T *buf, size_t max)
    __attribute__((nonnull));
extern xstrbuf_T *sb_clear(xstrbuf_T *buf)
    __attribute__((nonnull));
extern xstrbuf_T *sb_replace_force(
	xstrbuf_T *restrict buf, size_t i, size_t bn,
	const char *restrict s, size_t sn)
    __attribute__((nonnull));
extern xstrbuf_T *sb_replace(
	xstrbuf_T *restrict buf, size_t i, size_t bn,
	const char *restrict s, size_t sn)
    __attribute__((nonnull));
static inline xstrbuf_T *sb_ninsert_force(
	xstrbuf_T *restrict buf, size_t i, const char *restrict s, size_t n)
    __attribute__((nonnull));
static inline xstrbuf_T *sb_ninsert(
	xstrbuf_T *restrict buf, size_t i, const char *restrict s, size_t n)
    __attribute__((nonnull));
static inline xstrbuf_T *sb_insert(
	xstrbuf_T *restrict buf, size_t i, const char *restrict s)
    __attribute__((nonnull));
static inline xstrbuf_T *sb_ncat_force(
	xstrbuf_T *restrict buf, const char *restrict s, size_t n)
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
extern xstrbuf_T *sb_ccat_repeat(xstrbuf_T *buf, char c, size_t n)
    __attribute__((nonnull));
extern _Bool sb_wccat(
	xstrbuf_T *restrict buf, wchar_t c, mbstate_t *restrict ps)
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
static inline void wb_destroy(xwcsbuf_T *buf)
    __attribute__((nonnull));
extern wchar_t *wb_towcs(xwcsbuf_T *buf)
    __attribute__((nonnull));
extern xwcsbuf_T *wb_setmax(xwcsbuf_T *buf, size_t newmax)
    __attribute__((nonnull));
extern xwcsbuf_T *wb_ensuremax(xwcsbuf_T *buf, size_t max)
    __attribute__((nonnull));
extern xwcsbuf_T *wb_clear(xwcsbuf_T *buf)
    __attribute__((nonnull));
extern xwcsbuf_T *wb_replace_force(
	xwcsbuf_T *restrict buf, size_t i, size_t bn,
	const wchar_t *restrict s, size_t sn)
    __attribute__((nonnull));
extern xwcsbuf_T *wb_replace(
	xwcsbuf_T *restrict buf, size_t i, size_t bn,
	const wchar_t *restrict s, size_t sn)
    __attribute__((nonnull));
static inline xwcsbuf_T *wb_ninsert_force(
	xwcsbuf_T *restrict buf, size_t i, const wchar_t *restrict s, size_t n)
    __attribute__((nonnull));
static inline xwcsbuf_T *wb_ninsert(
	xwcsbuf_T *restrict buf, size_t i, const wchar_t *restrict s, size_t n)
    __attribute__((nonnull));
static inline xwcsbuf_T *wb_insert(
	xwcsbuf_T *restrict buf, size_t i, const wchar_t *restrict s)
    __attribute__((nonnull));
static inline xwcsbuf_T *wb_ncat_force(
	xwcsbuf_T *restrict buf, const wchar_t *restrict s, size_t n)
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
extern xwcsbuf_T *wb_wccat_repeat(xwcsbuf_T *buf, wchar_t c, size_t n)
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


/* Frees a multibyte string buffer, abandoning the contents. */
void sb_destroy(xstrbuf_T *buf)
{
    free(buf->contents);
}

/* Inserts the first `n' bytes of a multibyte string `s' at the offset `i'
 * in the buffer `buf'.
 * No boundary checks are done and a null character is not considered special.
 * `s' must not be part of `buf->contents'. */
xstrbuf_T *sb_ninsert_force(
	xstrbuf_T *restrict buf, size_t i, const char *restrict s, size_t n)
{
    return sb_replace_force(buf, i, 0, s, n);
}

/* Inserts the first `n' bytes of a multibyte string `s' at the offset `i'
 * in the buffer `buf'.
 * If (strlen(s) <= n), the whole of `s' is inserted.
 * If (buf->length <= i), `s' is appended to the end of the buffer.
 * `s' must not be part of `buf->contents'. */
xstrbuf_T *sb_ninsert(
	xstrbuf_T *restrict buf, size_t i, const char *restrict s, size_t n)
{
    return sb_replace(buf, i, 0, s, n);
}

/* Inserts a multibyte string `s' at the offset `i' in the buffer `buf'.
 * If (buf->length <= i), `s' is appended to the end of the buffer.
 * `s' must not be part of `buf->contents'. */
xstrbuf_T *sb_insert(xstrbuf_T *restrict buf, size_t i, const char *restrict s)
{
    return sb_replace(buf, i, 0, s, Size_max);
}

/* Appends the first `n' bytes of a multibyte string `s' to the buffer `buf'.
 * No boundary checks are done and a null character is not considered special.
 * `s' must not be part of `buf->contents'. */
xstrbuf_T *sb_ncat_force(
	xstrbuf_T *restrict buf, const char *restrict s, size_t n)
{
    return sb_replace_force(buf, buf->length, 0, s, n);
}

/* Appends the first `n' bytes of a multibyte string `s' to the buffer `buf'.
 * If (strlen(s) <= n), the whole of `s' is appended.
 * `s' must not be part of `buf->contents'. */
xstrbuf_T *sb_ncat(xstrbuf_T *restrict buf, const char *restrict s, size_t n)
{
    return sb_replace(buf, Size_max, 0, s, n);
}

/* Appends a multibyte string `s' to the buffer `buf'.
 * `s' must not be part of `buf->contents'. */
xstrbuf_T *sb_cat(xstrbuf_T *restrict buf, const char *restrict s)
{
    return sb_replace(buf, Size_max, 0, s, Size_max);
}

/* Appends a multibyte string `s' to the buffer `buf' and free the string.
 * `s' must not be part of `buf->contents'. */
xstrbuf_T *sb_catfree(xstrbuf_T *restrict buf, char *restrict s)
{
    sb_cat(buf, s);
    free(s);
    return buf;
}

/* Removes `n' bytes at the offset `i' in the buffer `buf'.
 * If (buf->length <= i), `buf' is unchanged.
 * If (buf->length <= i + n), `buf' is truncated to `i' bytes. */
xstrbuf_T *sb_remove(xstrbuf_T *buf, size_t i, size_t n)
{
    return sb_replace(buf, i, n, "", 0);
}

/* Frees a wide string buffer, abandoning the contents. */
void wb_destroy(xwcsbuf_T *buf)
{
    free(buf->contents);
}

/* Inserts the first `n' characters of a wide string `s'
 * at the offset `i' in the buffer `buf'.
 * No boundary checks are done and a null character is not considered special.
 * `s' must not be part of `buf->contents'. */
xwcsbuf_T *wb_ninsert_force(
	xwcsbuf_T *restrict buf, size_t i, const wchar_t *restrict s, size_t n)
{
    return wb_replace_force(buf, i, 0, s, n);
}

/* Inserts the first `n' characters of a wide string
 * at the offset `i' in the buffer `buf'.
 * If (wcslen(s) <= n), the whole of `s' is inserted.
 * If (buf->length <= i), `s' is appended to the end of the buffer.
 * `s' must not be part of `buf->contents'. */
xwcsbuf_T *wb_ninsert(
	xwcsbuf_T *restrict buf, size_t i, const wchar_t *restrict s, size_t n)
{
    return wb_replace(buf, i, 0, s, n);
}

/* Inserts a wide string at the offset `i' in the buffer `buf'.
 * If (buf->length <= i), `s' is appended to the end of the buffer.
 * `s' must not be part of `buf->contents'. */
xwcsbuf_T *wb_insert(
	xwcsbuf_T *restrict buf, size_t i, const wchar_t *restrict s)
{
    return wb_replace(buf, i, 0, s, Size_max);
}

/* Appends the first `n' characters of a wide string `s' to the buffer `buf'.
 * No boundary checks are done and a null character is not considered special.
 * `s' must not be part of `buf->contents'. */
xwcsbuf_T *wb_ncat_force(
	xwcsbuf_T *restrict buf, const wchar_t *restrict s, size_t n)
{
    return wb_replace_force(buf, buf->length, 0, s, n);
}

/* Appends the first `n' characters of a wide string `s' to the buffer `buf'.
 * If (wcslen(s) <= n), the whole of `s' is appended.
 * `s' must not be part of `buf->contents'. */
xwcsbuf_T *wb_ncat(xwcsbuf_T *restrict buf, const wchar_t *restrict s, size_t n)
{
    return wb_replace(buf, Size_max, 0, s, n);
}

/* Appends a wide string `s' to the buffer `buf'.
 * `s' must not be part of `buf->contents'. */
xwcsbuf_T *wb_cat(xwcsbuf_T *restrict buf, const wchar_t *restrict s)
{
    return wb_replace(buf, Size_max, 0, s, Size_max);
}

/* Appends a wide string `s' to the buffer and free the string.
 * `s' must not be part of `buf->contents'. */
xwcsbuf_T *wb_catfree(xwcsbuf_T *restrict buf, wchar_t *restrict s)
{
    wb_cat(buf, s);
    free(s);
    return buf;
}

/* Removes `n' characters at the offset `i' in the buffer `buf'.
 * If (buf->length <= i), `buf' is unchanged.
 * If (buf->length <= i + n), `buf' is truncated to `i' characters. */
xwcsbuf_T *wb_remove(xwcsbuf_T *buf, size_t i, size_t n)
{
    return wb_replace(buf, i, n, L"", 0);
}

/* Converts a wide string to a newly malloced multibyte string.
 * Returns NULL on error.
 * The resulting string starts and ends in the initial shift state.*/
char *malloc_wcstombs(const wchar_t *s)
{
    return malloc_wcsntombs(s, Size_max);
}

/* Converts a wide string to a newly malloced multibyte string and free the
 * wide string.
 * Returns NULL on error. The wide string is freed anyway.
 * The resulting string starts and ends in the initial shift state.*/
char *realloc_wcstombs(wchar_t *s)
{
    char *result = malloc_wcstombs(s);
    free(s);
    return result;
}

/* Converts a multibyte string to a newly malloced wide string.
 * Returns NULL on error. */
wchar_t *malloc_mbstowcs(const char *s)
{
    return malloc_mbsntowcs(s, Size_max);
}

/* Converts a multibyte string to a newly malloced wide string and free the
 * multibyte string.
 * Returns NULL on error. The multibyte string is freed anyway. */
wchar_t *realloc_mbstowcs(char *s)
{
    wchar_t *result = malloc_mbstowcs(s);
    free(s);
    return result;
}

/* Returns the result of `vsprintf' as a newly malloced string. */
char *malloc_vprintf(const char *format, va_list ap)
{
    xstrbuf_T buf;
    sb_init(&buf);
    sb_vprintf(&buf, format, ap);
    return sb_tostr(&buf);
}

/* Returns the result of `sprintf' as a newly malloced string. */
char *malloc_printf(const char *format, ...)
{
    va_list ap;
    char *result;
    va_start(ap, format);
    result = malloc_vprintf(format, ap);
    va_end(ap);
    return result;
}

/* Returns the result of `vswprintf' as a newly malloced string. */
wchar_t *malloc_vwprintf(const wchar_t *format, va_list ap)
{
    xwcsbuf_T buf;
    wb_init(&buf);
    wb_vwprintf(&buf, format, ap);
    return wb_towcs(&buf);
}

/* Returns the result of `swprintf' as a newly malloced string. */
wchar_t *malloc_wprintf(const wchar_t *format, ...)
{
    va_list ap;
    wchar_t *result;
    va_start(ap, format);
    result = malloc_vwprintf(format, ap);
    va_end(ap);
    return result;
}


#undef Size_max

#endif /* YASH_STRBUF_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
