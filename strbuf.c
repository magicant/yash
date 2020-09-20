/* Yash: yet another shell */
/* strbuf.c: modifiable string buffer */
/* (C) 2007-2020 magicant */

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
#include "strbuf.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include "util.h"

#if HAVE_WCSNRTOMBS && !defined(wcsnrtombs)
size_t wcsnrtombs(char *restrict dst, const wchar_t **restrict src, size_t nwc,
	size_t len, mbstate_t *restrict ps);
#endif


/* If the type of the return value of the functions below is string buffer,
 * the return value is the argument buffer. */


/********** Multibyte String Buffer **********/

/* Initializes the specified string buffer as an empty string. */
xstrbuf_T *sb_initwithmax(xstrbuf_T *buf, size_t max)
{
    // buf->contents = xmalloce(max, 1, sizeof (char));
    buf->contents = xmalloc(add(max, 1));
    buf->contents[0] = '\0';
    buf->length = 0;
    buf->maxlength = max;
    return buf;
}

/* Initializes the specified multibyte string buffer with the specified string.
 * String `s' must be `free'able.
 * After calling this function, the string is used as the buffer, so you must
 * not touch or `free' it any more. */
xstrbuf_T *sb_initwith(xstrbuf_T *restrict buf, char *restrict s)
{
    buf->contents = s;
    buf->length = buf->maxlength = strlen(s);
    return buf;
}

/* Changes the maximum length of the specified buffer.
 * If `newmax' is less than the current length of the buffer, the end of
 * the buffer contents is truncated. */
xstrbuf_T *sb_setmax(xstrbuf_T *buf, size_t newmax)
{
    // buf->contents = xrealloce(buf->contents, newmax, 1, sizeof (char));
    buf->contents = xrealloc(buf->contents, add(newmax, 1));
    buf->maxlength = newmax;
    buf->contents[newmax] = '\0';
    if (newmax < buf->length)
	buf->length = newmax;
    return buf;
}

/* If `buf->maxlength' is less than `max', reallocates the buffer so that
 * `buf->maxlength' is no less than `max'. */
xstrbuf_T *sb_ensuremax(xstrbuf_T *buf, size_t max)
{
    if (max <= buf->maxlength)
	return buf;

    size_t len15 = buf->maxlength + (buf->maxlength >> 1);
    if (max < len15)
	max = len15;
    if (max < buf->maxlength + 10)
	max = buf->maxlength + 10;
    return sb_setmax(buf, max);
}

/* Replaces the specified part of the buffer with another string.
 * `bn' characters starting at offset `i' in buffer `buf' is removed and
 * the first `sn' characters of `s' take place of them.
 * No boundary checks are done and null characters are not considered special.
 * `s' must not be part of `buf->contents'. */
xstrbuf_T *sb_replace_force(
	xstrbuf_T *restrict buf, size_t i, size_t bn,
	const char *restrict s, size_t sn)
{
    size_t newlength = add(buf->length - bn, sn);
    sb_ensuremax(buf, newlength);
    memmove(buf->contents + i + sn, buf->contents + i + bn,
	    buf->length - (i + bn) + 1);
    memcpy(buf->contents + i, s, sn);
    buf->length = newlength;
    return buf;
}

/* Replaces the specified part of the buffer with another string.
 * `bn' characters starting at offset `i' in buffer `buf' is removed and
 * the first `sn' characters of `s' take place of them.
 * If (strlen(s) < sn), the whole of `s' is replaced with.
 * If (buf->length < i + sn), all the characters after offset `i' in the buffer
 * is replaced. Especially, if (buf->length <= i), `s' is appended.
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

/* Appends byte `c' to the end of string buffer `buf'.
 * The byte is appended even if it is a null byte. */
xstrbuf_T *sb_ccat(xstrbuf_T *buf, char c)
{
    sb_ensuremax(buf, add(buf->length, 1));
    buf->contents[buf->length++] = c;
    buf->contents[buf->length] = '\0';
    return buf;
}

/* Appends `n' bytes of `c' to the end of buffer `buf'.
 * The bytes are appended even if `c' is a null byte. */
xstrbuf_T *sb_ccat_repeat(xstrbuf_T *buf, char c, size_t n)
{
    sb_ensuremax(buf, add(buf->length, n));
    memset(&buf->contents[buf->length], c, n);
    buf->length += n;
    buf->contents[buf->length] = '\0';
    return buf;
}

/* Converts wide character `c' into a multibyte string and appends it to buffer
 * `buf'. Shift state `ps' is used for the conversion.
 * If `c' is a null character, the shift state is reset to the initial state but
 * the null character is not appended to the buffer.
 * Returns true iff successful. On error, `errno' is set to EILSEQ and the state
 * is left undefined. */
bool sb_wccat(xstrbuf_T *restrict buf, wchar_t c, mbstate_t *restrict ps)
{
    size_t count;

    sb_ensuremax(buf, add(buf->length, MB_CUR_MAX));
    count = wcrtomb(&buf->contents[buf->length], c, ps);
    if (count == (size_t) -1) {
	buf->contents[buf->length] = '\0';
	return false;
    }
    assert(0 < count && count <= buf->maxlength - buf->length);
    buf->length += count;
    if (c == L'\0')
	buf->length--;
    else
	buf->contents[buf->length] = '\0';
    assert(buf->contents[buf->length] == '\0');
    return true;
}

/* Appends first `n' characters of wide string `s' to multibyte buffer `buf'.
 * The wide string is converted to multibyte string using shift state `ps'.
 * If `n' is larger than the length of `s', the whole string is appended and
 * the shift state is reset to the initial shift state.
 * Returns NULL if the string is converted and appended successfully,
 * otherwise a pointer to the character in `s' that caused the error.
 * A partial result may be left in the buffer on error. */
wchar_t *sb_wcsncat(xstrbuf_T *restrict buf,
	const wchar_t *restrict s, size_t n, mbstate_t *restrict ps)
{
#if HAVE_WCSNRTOMBS
    for (;;) {
	const wchar_t *saves = s;
	size_t count = wcsnrtombs(&buf->contents[buf->length],
		(const wchar_t **) &s, n,
		buf->maxlength - buf->length, ps);
	if (count == (size_t) -1) {
	    buf->contents[buf->length] = '\0';
	    break;
	}
	buf->length += count;
	if (s == NULL)
	    break;
	assert((size_t) (s - saves) <= n);
	n -= s - saves;
	if (n == 0) {
	    buf->contents[buf->length] = '\0';
	    s = NULL;
	    break;
	}
	sb_ensuremax(buf, add(buf->maxlength, MB_CUR_MAX));
    }
    assert(buf->contents[buf->length] == '\0');
    return (wchar_t *) s;
#else
    while (n > 0) {
	if (!sb_wccat(buf, *s, ps))
	    return (wchar_t *) s;
	if (*s == L'\0')
	    return NULL;
	s++, n--;
    }
    return NULL;
#endif
}

/* Appends wide string `s' to multibyte buffer `buf'. The wide string is
 * converted to multibyte string using shift state `ps'. After successful
 * conversion, the shift state is reset to the initial shift state.
 * Returns NULL if the whole string is converted and appended successfully,
 * otherwise a pointer to the character in `s' that caused the error.
 * A partial result may be left in the buffer on error. */
#if !HAVE_WCSNRTOMBS
wchar_t *sb_wcscat(xstrbuf_T *restrict buf,
	const wchar_t *restrict s, mbstate_t *restrict ps)
{
    for (;;) {
	size_t count = wcsrtombs(&buf->contents[buf->length],
		(const wchar_t **) &s,
		buf->maxlength - buf->length,
		ps);
	if (count == (size_t) -1) {
	    buf->contents[buf->length] = '\0';
	    break;
	}
	buf->length += count;
	if (s == NULL)
	    break;
	sb_ensuremax(buf, add(buf->maxlength, MB_CUR_MAX));
    }
    assert(buf->contents[buf->length] == '\0');
    return (wchar_t *) s;
}
#endif

/* Appends the result of `vsprintf' to the specified buffer.
 * `format' and the following arguments must not be part of `buf->contents'.
 * Returns the number of appended bytes if successful.
 * On error, the buffer is not changed and -1 is returned. */
int sb_vprintf(xstrbuf_T *restrict buf, const char *restrict format, va_list ap)
{
    va_list copyap;
    va_copy(copyap, ap);

    // No need to check for overflow in `buf->maxlength - buf->length + 1' here.
    // Should overflow occur, the buffer would not have been allocated
    // successfully.
    size_t rest = buf->maxlength - buf->length + 1;
    int result = vsnprintf(&buf->contents[buf->length], rest, format, ap);

    // If the buffer was too small...
#if INT_MAX < SIZE_MAX
    if (result >= 0 && (size_t) result >= rest) {
#else // INT_MAX >= SIZE_MAX
    if (result >= (int) rest) {
	if (result > (int) SIZE_MAX)
	    alloc_failed();
#endif
	// retry with a larger buffer
	sb_ensuremax(buf, add(buf->length, (size_t) result));
	rest = buf->maxlength - buf->length + 1;
	result = vsnprintf(&buf->contents[buf->length], rest, format, copyap);
    }
#if INT_MAX < SIZE_MAX
    assert(result < 0 || (size_t) result < rest);
#else // INT_MAX >= SIZE_MAX
    assert(result < (int) rest);
#endif

    if (result >= 0)
	buf->length += result;
    else
	buf->contents[buf->length] = '\0';
    assert(buf->contents[buf->length] == '\0');
    va_end(copyap);
    return result;
}

/* Appends the result of `sprintf' to the specified buffer.
 * `format' and the following arguments must not be part of `buf->contents'.
 * Returns the number of appended bytes if successful.
 * On error, the buffer is not changed and -1 is returned. */
int sb_printf(xstrbuf_T *restrict buf, const char *restrict format, ...)
{
    va_list ap;
    int result;

    va_start(ap, format);
    result = sb_vprintf(buf, format, ap);
    va_end(ap);
    return result;
}


/********** Wide String Buffer **********/

/* Initializes the specified wide string buffer as an empty string. */
xwcsbuf_T *wb_initwithmax(xwcsbuf_T *buf, size_t max)
{
    buf->contents = xmalloce(max, 1, sizeof (wchar_t));
    buf->contents[0] = L'\0';
    buf->length = 0;
    buf->maxlength = max;
    return buf;
}

/* Initializes the specified wide string buffer with the specified string.
 * String `s' must be `free'able.
 * After calling this function, the string is used as the buffer, so you must
 * not touch or `free' it any more. */
xwcsbuf_T *wb_initwith(xwcsbuf_T *restrict buf, wchar_t *restrict s)
{
    buf->contents = s;
    buf->length = buf->maxlength = wcslen(s);
    return buf;
}

/* Changes the maximum length of the specified buffer.
 * If `newmax' is less than the current length of the buffer, the end of
 * the buffer contents is truncated. */
xwcsbuf_T *wb_setmax(xwcsbuf_T *buf, size_t newmax)
{
    buf->contents = xrealloce(buf->contents, newmax, 1, sizeof (wchar_t));
    buf->maxlength = newmax;
    buf->contents[newmax] = L'\0';
    if (newmax < buf->length)
	buf->length = newmax;
    return buf;
}

/* If `buf->maxlength' is less than `max', reallocates the buffer so that
 * `buf->maxlength' is no less than `max'. */
xwcsbuf_T *wb_ensuremax(xwcsbuf_T *buf, size_t max)
{
    if (max <= buf->maxlength)
	return buf;

    size_t len15 = buf->maxlength + (buf->maxlength >> 1);
    if (max < len15)
	max = len15;
    if (max < buf->maxlength + 8)
	max = buf->maxlength + 8;
    return wb_setmax(buf, max);
}

/* Replaces the specified part of the buffer with another string.
 * `bn' characters starting at offset `i' in buffer `buf' is removed and
 * the first `sn' characters of `s' take place of them.
 * No boundary checks are done and null characters are not considered special.
 * `s' must not be part of `buf->contents'. */
xwcsbuf_T *wb_replace_force(
	xwcsbuf_T *restrict buf, size_t i, size_t bn,
	const wchar_t *restrict s, size_t sn)
{
    size_t newlength = add(buf->length - bn, sn);
    wb_ensuremax(buf, newlength);
    wmemmove(buf->contents + i + sn, buf->contents + i + bn,
	    buf->length - (i + bn) + 1);
    wmemcpy(buf->contents + i, s, sn);
    buf->length = newlength;
    return buf;
}

/* Replaces the specified part of the buffer with another string.
 * `bn' characters starting at offset `i' in buffer `buf' is removed and
 * the first `sn' characters of `s' take place of them.
 * If (wcslen(s) < sn), the whole of `s' is replaced with.
 * If (buf->length < i + sn), all the characters after offset `i' in the buffer
 * is replaced. Especially, if (buf->length <= i), `s' is appended.
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

/* Appends wide character `c' to the end of buffer `buf'.
 * The character is appended even if it is a null wide character. */
xwcsbuf_T *wb_wccat(xwcsbuf_T *buf, wchar_t c)
{
    wb_ensuremax(buf, add(buf->length, 1));
    buf->contents[buf->length++] = c;
    buf->contents[buf->length] = L'\0';
    return buf;
}

/* Converts multibyte string `s' into a wide string and appends it to buffer
 * `buf'. The multibyte string is assumed to start in the initial shift state.
 * Returns NULL if the whole string is converted and appended successfully,
 * otherwise a pointer to the character in `s' that caused the error.
 * A partial result may be left in the buffer on error. */
char *wb_mbscat(xwcsbuf_T *restrict buf, const char *restrict s)
{
    mbstate_t state;
    size_t count;

    memset(&state, 0, sizeof state);  // initialize as the initial shift state

    for (;;) {
	count = mbsrtowcs(&buf->contents[buf->length], (const char **) &s,
		buf->maxlength - buf->length + 1, &state);
	if (count == (size_t) -1)
	    break;
	buf->length += count;
	if (s == NULL)
	    break;
	wb_ensuremax(buf, add(buf->maxlength, 1));
    }

    buf->contents[buf->length] = L'\0';
    return (char *) s;
}

/* Appends the result of `vswprintf' to the specified buffer.
 * `format' and the following arguments must not be part of `buf->contents'.
 * Returns the number of appended characters if successful.
 * On error, the buffer is not changed and -1 is returned. */
int wb_vwprintf(
	xwcsbuf_T *restrict buf, const wchar_t *restrict format, va_list ap)
{
    va_list copyap;
    size_t rest;
    int result;

    for (int i = 0; i < 20; i++) {
	va_copy(copyap, ap);
	rest = buf->maxlength - buf->length + 1;
	result = vswprintf(&buf->contents[buf->length], rest, format, copyap);
	va_end(copyap);

	if (0 <= result &&
#if INT_MAX < SIZE_MAX
		(size_t) result < rest)
#else // INT_MAX >= SIZE_MAX
		result < (int) rest)
#endif
	    break;
#if INT_MAX > SIZE_MAX
	if (result > (int) SIZE_MAX)
	    alloc_failed();
#endif

	/* According to POSIX, if the buffer is too short, `vswprintf' returns
	 * a negative integer. On some systems, however, it returns a desired
	 * buffer length as `vsprintf' does, which is rather preferable. */
	wb_ensuremax(buf,
		add(buf->length, result < 0 ? mul(2, rest) : (size_t) result));
    }
    if (result >= 0)
	buf->length += result;
    else
	buf->contents[buf->length] = L'\0';
    assert(buf->contents[buf->length] == L'\0');
    return result;
}

/* Appends the result of `swprintf' to the specified buffer.
 * `format' and the following arguments must not be part of `buf->contents'.
 * Returns the number of appended characters if successful.
 * On error, the buffer is not changed and -1 is returned. */
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

/* Converts the specified wide string into a newly malloced multibyte string.
 * Only the first `n' characters of `s' is converted at most.
 * Returns NULL on error.
 * The resulting string starts and ends in the initial shift state.*/
char *malloc_wcsntombs(const wchar_t *s, size_t n)
{
    xstrbuf_T buf;
    mbstate_t state;

    sb_init(&buf);
    memset(&state, 0, sizeof state);  // initialize as the initial shift state
    if (sb_wcsncat(&buf, s, n, &state) == NULL) {
	return sb_tostr(&buf);
    } else {
	sb_destroy(&buf);
	return NULL;
    }
}

/* Converts the specified wide string into a newly malloced multibyte string.
 * Returns NULL on error.
 * The resulting string starts and ends in the initial shift state.*/
#if !HAVE_WCSNRTOMBS
char *malloc_wcstombs(const wchar_t *s)
{
    xstrbuf_T buf;
    mbstate_t state;

    sb_init(&buf);
    memset(&state, 0, sizeof state);  // initialize as the initial shift state
    if (sb_wcscat(&buf, s, &state) == NULL) {
	return sb_tostr(&buf);
    } else {
	sb_destroy(&buf);
	return NULL;
    }
}
#endif

/* Converts the specified multibyte string into a newly malloced wide string.
 * Returns NULL on error. */
wchar_t *malloc_mbstowcs(const char *s)
{
    xwcsbuf_T buf;

    wb_init(&buf);
    if (wb_mbscat(&buf, s) == NULL) {
	return wb_towcs(&buf);
    } else {
	wb_destroy(&buf);
	return NULL;
    }
}


/********** Formatting Utilities **********/

/* Returns the result of `vsprintf' as a newly malloced string.
 * An error message is printed on error. The return value is non-NULL anyway. */
char *malloc_vprintf(const char *format, va_list ap)
{
    xstrbuf_T buf;
    sb_init(&buf);
    if (sb_vprintf(&buf, format, ap) < 0)
	xerror(errno, Ngt("unexpected error"));
    return sb_tostr(&buf);
}

/* Returns the result of `sprintf' as a newly malloced string.
 * An error message is printed on error. The return value is non-NULL anyway. */
char *malloc_printf(const char *format, ...)
{
    va_list ap;
    char *result;
    va_start(ap, format);
    result = malloc_vprintf(format, ap);
    va_end(ap);
    return result;
}

/* Returns the result of `vswprintf' as a newly malloced string.
 * An error message is printed on error. The return value is non-NULL anyway. */
wchar_t *malloc_vwprintf(const wchar_t *format, va_list ap)
{
    xwcsbuf_T buf;
    wb_init(&buf);
    if (wb_vwprintf(&buf, format, ap) < 0)
	xerror(errno, Ngt("unexpected error"));
    return wb_towcs(&buf);
}

/* Returns the result of `swprintf' as a newly malloced string.
 * An error message is printed on error. The return value is non-NULL anyway. */
wchar_t *malloc_wprintf(const wchar_t *format, ...)
{
    va_list ap;
    wchar_t *result;
    va_start(ap, format);
    result = malloc_vwprintf(format, ap);
    va_end(ap);
    return result;
}


/********** String Creating Utilities **********/

/* Joins the wide-character strings in the specified NULL-terminated array. The
 * array elements are considered pointers to wide strings.
 * `padding' is padded between each joined element.
 * Returns a newly malloced string. */
wchar_t *joinwcsarray(void *const *array, const wchar_t *padding)
{
    size_t elemcount, ccount = 0;

    /* count the full length of the resulting string */
    for (elemcount = 0; array[elemcount] != NULL; elemcount++)
	ccount = add(ccount, wcslen(array[elemcount]));
    if (elemcount > 0)
	ccount = add(ccount, mul(wcslen(padding), elemcount - 1));

    /* do copying */
    wchar_t *const result = xmalloce(ccount, 1, sizeof *result);
    wchar_t *s = result;
    for (size_t i = 0; i < elemcount; i++) {
	wchar_t *elem = array[i];
	while (*elem != L'\0')
	    *s++ = *elem++;
	if (i + 1 < elemcount) {
	    const wchar_t *pad = padding;
	    while (*pad != L'\0')
		*s++ = *pad++;
	}
    }
    *s = L'\0';

    return result;
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
