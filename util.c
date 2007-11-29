/* Yash: yet another shell */
/* © 2007 magicant */

/* This software can be redistributed and/or modified under the terms of
 * GNU General Public License, version 2 or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRENTY. */


#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <error.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "yash.h"
#include <assert.h>

void *xcalloc(size_t nmemb, size_t size);
void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t len);
char **straryclone(char **ary);
char *skipblanks(const char *s);
char *skipspaces(const char *s);
char *skipwhites(const char *s);
char *stripspaces(char *s);
char *strjoin(int argc, char *const *argv, const char *padding);
char *read_all(int fd);


/* calloc を試みる。失敗したらプログラムを強制終了する。
 * 戻り値: calloc の結果 */
void *xcalloc(size_t nmemb, size_t size)
{
	assert(nmemb > 0 && size > 0);

	void *result = calloc(nmemb, size);
	if (!result)
		error(2, ENOMEM, NULL);
	return result;
}

/* malloc を試みる。失敗したらプログラムを強制終了する。
 * 戻り値: malloc の結果 */
void *xmalloc(size_t size)
{
	assert(size > 0);

	void *result = malloc(size);
	if (!result)
		error(2, ENOMEM, NULL);
	return result;
}

/* realloc を試みる。失敗したらプログラムを強制終了する。
 * 戻り値: realloc の結果 */
void *xrealloc(void *ptr, size_t size)
{
	void *result = realloc(ptr, size);
	if (!result)
		error(2, ENOMEM, NULL);
	return result;
}

/* 文字列を新しく malloc した領域に複製する。
 * malloc に失敗するとプログラムを強制終了する。 */
char *xstrdup(const char *s)
{
	char *result = strdup(s);
	if (!result)
		error(2, ENOMEM, NULL);
	return result;
}

/* 文字列を新しく malloc した領域に複製する。
 * malloc に失敗するとプログラムを強制終了する。
 * len: 複製する文字列の長さ ('\0' を含まない)。
 *      len をいくら大きくしても strlen(s) より長い文字列にはならない。 */
char *xstrndup(const char *s, size_t len)
{
	char *result = strndup(s, len);
	if (!result)
		error(2, ENOMEM, NULL);
	return result;
}

/* 文字列の配列のディープコピーを作る。失敗するとプログラムを強制終了する。 */
char **straryclone(char **ary)
{
	size_t count;
	char **result;

	assert(ary);
	for (count = 0; ary[count]; count++) ;
	result = xcalloc(count + 1, sizeof(char *));
	for (size_t i = 0; i < count; i++)
		result[i] = xstrdup(ary[i]);
	return result;
}

/* 文字列の先頭にある空白文字 (スペースまたはタブ) を飛ばして、
 * 空白文字でない最初の文字のアドレスを返す。 */
char *skipblanks(const char *s)
{
	while (isblank(*s)) s++;
	return (char *) s;
}

/* 文字列の先頭にある空白類文字 (スペースや改行) を飛ばして、
 * 空白類文字でない最初の文字のアドレスを返す。 */
char *skipspaces(const char *s)
{
	while (isspace(*s)) s++;
	return (char *) s;
}

/* 文字列の先頭にある空白類文字やコメントを飛ばして、
 * 最初のトークンの文字のアドレスを返す。 */
char *skipwhites(const char *s)
{
	for (;;) {
		s = skipspaces(s);
		if (*s != '#')
			return (char *) s;
		s++;
		while (*s != '\0' && *s != '\n' && *s != '\r')
			s++;
	}
}

/* 文字列の先頭にある空白類文字 (スペースや改行) を削除する。
 * 文字列を直接書き換えた後、その文字列へのポインタ s を返す。 */
char *stripspaces(char *s)
{
	size_t i = 0;

	assert(s != NULL);
	while (isspace(s[i])) i++;
	if (i)
		memmove(s, s + i, strlen(s + i) + 1);
	return s;
}

/* 配列に含まれる文字列を全て順に連結し、新しく malloc した文字列として返す。
 * argc: argv から取り出す文字列の数。全て取り出すなら負数。
 * argv: この配列に含まれる文字列が連結される。
 * padding: 連結される各文字列の間に挟まれる文字列。NULL なら何も挟まない。 */
char *strjoin(int argc, char *const *argv, const char *padding)
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

	char *result = xmalloc(resultlen + 1);
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

/* 指定したファイルディスクリプタからデータを全部読み込み、
 * 一つの文字列として返す。
 * ファイルディスクリプタは現在位置から最後まで読み込まれるが、close はしない。
 * 戻り値: 成功すれば、新しく malloc した文字列。失敗すれば errno を更新して
 * NULL を返す。*/
char *read_all(int fd)
{
	off_t oldoff = lseek(fd, 0, SEEK_CUR);
	if (oldoff < 0) {
		if (errno == EBADF)
			return NULL;
	}
	off_t initsize = lseek(fd, 0, SEEK_END);
	if (initsize < 0)
		initsize = 256;
	lseek(fd, oldoff, SEEK_SET);

	size_t max = initsize, offset = 0;
	char *buf = xmalloc(max + 1);
	ssize_t count;

	for (;;) {
		if (offset >= max) {
			max *= 2;
			buf = xrealloc(buf, max + 1);
		}
		count = read(fd, buf + offset, max - offset);
		if (count < 0 && errno != EINTR)
			goto onerror;
		if (count == 0)
			break;
		offset += count;
	}
	xrealloc(buf, offset + 1);
	buf[offset] = '\0';
	return buf;

onerror:
	free(buf);
	return NULL;
}
