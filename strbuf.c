/* Yash: yet another shell */
/* strbuf.c: modifiable string buffer */
/* (C) 2007-2008 magicant */

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
#include "strbuf.h"
#include "util.h"

#define XSTRBUF_INITSIZE 15
#define XWCSBUF_INITSIZE 15


/* If the type of the return value of the functions below is string buffer,
 * the return value is the argument `buf'. */


/********** Multibyte String Buffer **********/

/* Initializes a multibyte string buffer as a new empty string. */
xstrbuf_T *sb_init(xstrbuf_T *buf)
{
    buf->contents = xmalloc((XSTRBUF_INITSIZE + 1) * sizeof (char));
    buf->contents[0] = '\0';
    buf->length = 0;
    buf->maxlength = XSTRBUF_INITSIZE;
    return buf;
}

/* Initializes a multibyte string buffer with an already malloced string.
 * After calling this function, the string is used as the buffer
 * so you must not touch or `free' it any more. */
xstrbuf_T *sb_initwith(xstrbuf_T *restrict buf, char *restrict s)
{
    buf->contents = s;
    buf->length = buf->maxlength = strlen(s);
    return buf;
}

/* Frees a multibyte string buffer and returns the contents.
 * The caller must `free' the return value. */
char *sb_tostr(xstrbuf_T *buf)
{
    if (buf->maxlength - buf->length > 20)
	sb_setmax(buf, buf->length);
    return buf->contents;
}

/* Changes `buf->maxlength'.
 * If `newmax' is less than the current length of the buffer, the end of
 * the buffered string is truncated. */
xstrbuf_T *sb_setmax(xstrbuf_T *buf, size_t newmax)
{
    buf->contents = xrealloc(buf->contents, (newmax + 1) * sizeof (char));
    buf->maxlength = newmax;
    buf->contents[newmax] = '\0';
    if (newmax < buf->length)
	buf->length = newmax;
    return buf;
}

/* If `buf->maxlength' is less than `max', reallocates the buffer so that
 * `buf->maxlength' is no less than `max'. */
inline xstrbuf_T *sb_ensuremax(xstrbuf_T *buf, size_t max)
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

/* Clears the contents of a string buffer, preserving its `maxlength'. */
xstrbuf_T *sb_clear(xstrbuf_T *buf)
{
    buf->contents[buf->length = 0] = '\0';
    return buf;
}

/* Replaces the contents of a string buffer with another string.
 * `bn' characters starting at the offset `i' in the buffer is removed and
 * the first `sn' characters of `s' take place of them.
 * No boundary checks are done and a null character is not considered special.
 * `s' must not be part of `buf->contents'. */
xstrbuf_T *sb_replace_force(
	xstrbuf_T *restrict buf, size_t i, size_t bn,
	const char *restrict s, size_t sn)
{
    size_t newlength = buf->length - bn + sn;
    sb_ensuremax(buf, newlength);
    memmove(buf->contents + i + sn, buf->contents + i + bn,
	    buf->length - (i + bn) + 1);
    memcpy(buf->contents + i, s, sn);
    buf->length = newlength;
    return buf;
}

/* Replaces the contents of a string buffer with another string.
 * `bn' characters starting at the offset `i' in the buffer is removed and
 * the first `sn' characters of `s' take place of them.
 * If (strlen(s) < sn), the whole of `s' is replaced with.
 * If (buf->length < i + sn), all the characters after the offset `i' in the
 * buffer is replaced. Especially, if (buf->length <= i), `s' is appended.
 * `s' must not be part of `buf->contents'. */
xstrbuf_T *sb_replace(
	xstrbuf_T *restrict buf, size_t i, size_t bn,
	const char *restrict s, size_t sn)
{
    sn = xstrnlen(s, sn);
    if (i > buf->length)
	i = buf->length;
    if (bn > buf->length - i)
	bn = buf->length - i;
    return sb_replace_force(buf, i, bn, s, sn);
}

/* Appends a byte `c' to the end of a string buffer.
 * For `c', a null character is not treated specially. */
xstrbuf_T *sb_ccat(xstrbuf_T *buf, char c)
{
    sb_ensuremax(buf, buf->length + 1);
    buf->contents[buf->length++] = c;
    buf->contents[buf->length] = '\0';
    return buf;
}

/* Appends `n' bytes of `c' to the end of a string buffer.
 * For `c', a null character is not treated specially. */
xstrbuf_T *sb_ccat_repeat(xstrbuf_T *buf, char c, size_t n)
{
    sb_ensuremax(buf, buf->length + n);
    memset(buf->contents + buf->length, c, n);
    buf->length += n;
    buf->contents[buf->length] = '\0';
    return buf;
}

/* Converts a wide string to a multibyte string and appends it to the buffer.
 * The shift state `*ps' is updated to describe the new state.
 * Returns NULL if the whole string is converted and appended successfully, or,
 * on error, a pointer to the character in `s' that caused the error.
 * A partial result may have been appended to the buffer on error.
 * If `s' is NULL, the shift state is restored to an initial state. */
wchar_t *sb_wcscat(xstrbuf_T *restrict buf,
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

/* Appends a formatted string to the end of a buffer.
 * `format' and the following arguments must not be part of `buf->contents'. */
int sb_vprintf(xstrbuf_T *restrict buf, const char *restrict format, va_list ap)
{
    va_list saveap;
    va_copy(saveap, ap);

    int rest = buf->maxlength - buf->length + 1;
    int result = vsnprintf(buf->contents + buf->length, rest, format, ap);

    if (result >= rest) {
	/* If the buffer is too small... */
	sb_ensuremax(buf, buf->length + result);
	rest = buf->maxlength - buf->length + 1;
	result = vsnprintf(buf->contents + buf->length, rest, format, saveap);
    }
    assert(result < rest);
    if (result >= 0)
	buf->length += result;
    assert(buf->contents[buf->length] == '\0');
    va_end(saveap);
    return result;
}

/* Appends a formatted string to the end of a buffer.
 * `format' and any following arguments must not be part of `buf->contents'. */
int sb_printf(xstrbuf_T *restrict buf, const char *restrict format, ...)
{
    va_list ap;
    int result;

    va_start(ap, format);
    result = sb_vprintf(buf, format, ap);
    va_end(ap);
    return result;
}

/* Appends the result of `strftime' to the end of a string buffer.
 * The result of formatting must not be an empty string.
 * Returns the number of the appended characters. */
size_t sb_strftime(xstrbuf_T *restrict buf,
	const char *restrict format, const struct tm *restrict tm)
{
    /* try with a 40-byte-long buffer. */
    char result[40];
    size_t count = strftime(result, sizeof result, format, tm);
    if (count) {
	sb_cat(buf, result);
	return count;
    }

    /* if 40 bytes are too small, retry with a longer buffer. */
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


/********** Wide String Buffer **********/

/* Initializes a wide string buffer as a new empty string. */
xwcsbuf_T *wb_init(xwcsbuf_T *buf)
{
    buf->contents = xmalloc((XWCSBUF_INITSIZE + 1) * sizeof (wchar_t));
    buf->contents[0] = L'\0';
    buf->length = 0;
    buf->maxlength = XWCSBUF_INITSIZE;
    return buf;
}

/* Initializes a wide string buffer with an already malloced string.
 * After calling this function, the string is used as the buffer
 * so you must not touch or `free' it any more. */
xwcsbuf_T *wb_initwith(xwcsbuf_T *restrict buf, wchar_t *restrict s)
{
    buf->contents = s;
    buf->length = buf->maxlength = wcslen(s);
    return buf;
}

/* Frees a wide string buffer and returns the contents.
 * The caller must `free' the return value. */
wchar_t *wb_towcs(xwcsbuf_T *buf)
{
    if (buf->maxlength - buf->length > 20)
	wb_setmax(buf, buf->length);
    return buf->contents;
}

/* Changes `buf->maxlength'.
 * If `newmax' is less than the current length of the buffer, the end of
 * the buffered string is truncated. */
xwcsbuf_T *wb_setmax(xwcsbuf_T *buf, size_t newmax)
{
    buf->contents = xrealloc(buf->contents, (newmax + 1) * sizeof (wchar_t));
    buf->maxlength = newmax;
    buf->contents[newmax] = L'\0';
    if (newmax < buf->length)
	buf->length = newmax;
    return buf;
}

/* If `buf->maxlength' is less than `max', reallocates the buffer so that
 * `buf->maxlength' is no less than `max'. */
inline xwcsbuf_T *wb_ensuremax(xwcsbuf_T *buf, size_t max)
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

/* Clears the contents of a string buffer, preserving its `maxlength'. */
xwcsbuf_T *wb_clear(xwcsbuf_T *buf)
{
    buf->contents[buf->length = 0] = L'\0';
    return buf;
}

/* Replaces the contents of a string buffer with another string.
 * `bn' characters starting at the offset `i' in the buffer is removed and
 * the first `sn' characters of `s' take place of them.
 * No boundary checks are done and a null character is not considered special.
 * `s' must not be part of `buf->contents'. */
xwcsbuf_T *wb_replace_force(
	xwcsbuf_T *restrict buf, size_t i, size_t bn,
	const wchar_t *restrict s, size_t sn)
{
    size_t newlength = buf->length - bn + sn;
    wb_ensuremax(buf, newlength);
    wmemmove(buf->contents + i + sn, buf->contents + i + bn,
	    buf->length - (i + bn) + 1);
    wmemcpy(buf->contents + i, s, sn);
    buf->length = newlength;
    return buf;
}

/* Replaces the contents of a string buffer with another string.
 * `bn' characters starting at the offset `i' in the buffer is removed and
 * the first `sn' characters of `s' take place of them.
 * If (wcslen(s) < sn), the whole of `s' is replaced with.
 * If (buf->length < i + sn), all the characters after the offset `i' in the
 * buffer is replaced. Especially, if (buf->length <= i), `s' is appended.
 * `s' must not be part of `buf->contents'. */
xwcsbuf_T *wb_replace(
	xwcsbuf_T *restrict buf, size_t i, size_t bn,
	const wchar_t *restrict s, size_t sn)
{
    sn = xwcsnlen(s, sn);
    if (i > buf->length)
	i = buf->length;
    if (bn > buf->length - i)
	bn = buf->length - i;
    return wb_replace_force(buf, i, bn, s, sn);
}

/* Appends a wide character `c' to the end of a string buffer.
 * For `c', a null character is not treated specially. */
xwcsbuf_T *wb_wccat(xwcsbuf_T *buf, wchar_t c)
{
    wb_ensuremax(buf, buf->length + 1);
    buf->contents[buf->length++] = c;
    buf->contents[buf->length] = L'\0';
    return buf;
}

/* Appends `n' characters of `c' to the end of a string buffer.
 * For `c', a null character is not treated specially. */
xwcsbuf_T *wb_wccat_repeat(xwcsbuf_T *buf, wchar_t c, size_t n)
{
    wb_ensuremax(buf, buf->length + n);
    wmemset(buf->contents + buf->length, c, n);
    buf->length += n;
    buf->contents[buf->length] = L'\0';
    return buf;
}

/* Converts a multibyte string to a wide string and append it to the buffer.
 * The multibyte string is assumed to start in a initial shift state.
 * Returns NULL if the whole string is converted and appended successfully, or,
 * on error, a pointer to the character in `s' that caused the error.
 * A partial result may have been appended to the buffer on error. */
char *wb_mbscat(xwcsbuf_T *restrict buf, const char *restrict s)
{
    mbstate_t state;
    size_t count;

    memset(&state, 0, sizeof state);  /* initialize as a initial shift state */

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

/* Appends a formatted string to the end of a buffer.
 * `format' and the following arguments must not be part of `buf->contents'. */
int wb_vwprintf(
	xwcsbuf_T *restrict buf, const wchar_t *restrict format, va_list ap)
{
    va_list saveap;
    va_copy(saveap, ap);

    int rest, result;

    for (int i = 0; i < 10; i++) {
	rest = buf->maxlength - buf->length + 1;
	result = vswprintf(buf->contents + buf->length, rest, format, ap);

	if (0 <= result && result < rest)
	    break;

	/* According to POSIX, if the buffer is too short, `vswprintf' returns
	 * a negative integer. However, on some systems, it returns a desired
	 * buffer length like `vsprintf', which is rather preferable. */
	wb_ensuremax(buf, buf->length + (result < 0 ? 2 * rest : result));
	va_end(ap);
	va_copy(ap, saveap);
    }
    if (result >= 0)
	buf->length += result;
    assert(buf->contents[buf->length] == L'\0');
    va_end(saveap);
    return result;
}

/* Appends a formatted string to the end of a buffer.
 * `format' and the following arguments must not be part of `buf->contents'. */
int wb_wprintf(xwcsbuf_T *restrict buf, const wchar_t *restrict format, ...)
{
    va_list ap;
    int result;

    va_start(ap, format);
    result = wb_vwprintf(buf, format, ap);
    va_end(ap);
    return result;
}



/********** Multibyte-Wide Conversion Utilities **********/

/* Converts a wide string to a newly malloced multibyte string.
 * Only the first `n' characters of `s' is converted at most.
 * Returns NULL on error.
 * The resulting string starts and ends in the initial shift state.*/
char *malloc_wcsntombs(const wchar_t *s, size_t n)
{
    xstrbuf_T buf;
    mbstate_t state;
    size_t nn = xwcsnlen(s, n);
    wchar_t ss[nn + 1];

    if (s[nn] != L'\0') {
	wcsncpy(ss, s, nn);
	ss[nn] = L'\0';
	s = ss;
    }

    sb_init(&buf);
    memset(&state, 0, sizeof state);  /* initialize as a initial shift state */
    if (sb_wcscat(&buf, s, &state) == NULL
	    && sb_wcscat(&buf, NULL, &state) == NULL) {
	return sb_tostr(&buf);
    } else {
	sb_destroy(&buf);
	return NULL;
    }
}

/* Converts a multibyte string to a newly malloced wide string.
 * Only the first `n' bytes of `s' is converted at most.
 * Returns NULL on error.
 * The resulting string starts and ends in the initial shift state.*/
wchar_t *malloc_mbsntowcs(const char *s, size_t n)
{
    xwcsbuf_T buf;
    size_t nn = xstrnlen(s, n);
    char ss[nn + 1];

    if (s[nn] != '\0') {
	strncpy(ss, s, nn);
	ss[nn] = '\0';
	s = ss;
    }

    wb_init(&buf);
    if (wb_mbscat(&buf, s) == NULL) {
	return wb_towcs(&buf);
    } else {
	wb_destroy(&buf);
	return NULL;
    }
}


/* vim: set ts=8 sts=4 sw=4 noet: */
