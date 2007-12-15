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


#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <error.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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
void recfree(void **ary);
char *skipblanks(const char *s);
char *skipspaces(const char *s);
char *skipwhites(const char *s);
char *strchug(char *s);
char *strchomp(char *s);
char *strjoin(int argc, char *const *argv, const char *padding);

void strbuf_init(struct strbuf *buf);
void strbuf_destroy(struct strbuf *buf);
char *strbuf_tostr(struct strbuf *buf);
void strbuf_setmax(struct strbuf *buf, size_t newmax);
void strbuf_trim(struct strbuf *buf);
void strbuf_clear(struct strbuf *buf);
void strbuf_ninsert(struct strbuf *buf, size_t i, const char *s, size_t n);
void strbuf_insert(struct strbuf *buf, size_t i, const char *s);
void strbuf_nappend(struct strbuf *buf, const char *s, size_t n);
void strbuf_append(struct strbuf *buf, const char *s);
void strbuf_cappend(struct strbuf *buf, char c);
void strbuf_replace(struct strbuf *buf, size_t i, size_t n, const char *s);
int strbuf_vprintf(struct strbuf *buf, const char *format, va_list ap);
int strbuf_printf(struct strbuf *buf, const char *format, ...);

void plist_init(struct plist *list);
void plist_destroy(struct plist *list);
void **plist_toary(struct plist *list);
void plist_setmax(struct plist *list, size_t newmax);
void plist_trim(struct plist *list);
void plist_clear(struct plist *list);
void plist_insert(struct plist *list, size_t i, void *e);
void plist_append(struct plist *list, void *e);

static unsigned ht_hashstr(const char *s);
void ht_init(struct hasht *ht);
void ht_destroy(struct hasht *ht);
static void ht_rehash(struct hasht *ht, size_t newcap);
void ht_ensurecap(struct hasht *ht, size_t newcap);
void ht_trim(struct hasht *ht);
void ht_clear(struct hasht *ht);
void *ht_get(struct hasht *ht, const char *key);
void *ht_set(struct hasht *ht, const char *key, void *value);
void *ht_remove(struct hasht *ht, const char *key);
int ht_each(struct hasht *ht, int (*func)(const char *key, void *value));


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
	char *result = strndup(s, len);  // _GNU_SOURCE
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

/* NULL 終端のポインタの配列を、その要素も含めて解放する。 */
void recfree(void **ary)
{
	if (ary) {
		for (void **a = ary; *a; a++) free(*a);
		free(ary);
	}
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
		s += strcspn(s, "\n\r");
	}
}

/* 文字列の先頭にある空白類文字 (スペースや改行) を削除する。
 * 文字列を直接書き換えた後、その文字列へのポインタ s を返す。 */
char *strchug(char *s)
{
	size_t i = 0;

	assert(s != NULL);
	while (isspace(s[i])) i++;
	if (i)
		memmove(s, s + i, strlen(s + i) + 1);
	return s;
}

/* 文字列の末尾にある空白類文字 (スペースや改行) を削除する。
 * 文字列を直接書き換えた後、その文字列へのポインタ s を返す。 */
char *strchomp(char *s)
{
	char *ss = s;

	while (*s) s++;                    /* 文字列の末尾に移動 */
	while (--s >= ss && isspace(*s));  /* 空白の分だけ戻る */
	*++s = '\0';
	return ss;
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

///* 指定したファイルディスクリプタからデータを全部読み込み、
// * 一つの文字列として返す。
// * ファイルディスクリプタは現在位置から最後まで読み込まれるが、close はしない。
// * 戻り値: 成功すれば、新しく malloc した文字列。失敗すれば errno を更新して
// * NULL を返す。*/
//char *read_all(int fd)
//{
//	off_t oldoff = lseek(fd, 0, SEEK_CUR);
//	if (oldoff < 0) {
//		if (errno == EBADF)
//			return NULL;
//	}
//	off_t initsize = lseek(fd, 0, SEEK_END);
//	if (initsize < 0)
//		initsize = 256;
//	lseek(fd, oldoff, SEEK_SET);
//
//	size_t max = initsize, offset = 0;
//	char *buf = xmalloc(max + 1);
//	ssize_t count;
//
//	for (;;) {
//		if (offset >= max) {
//			max *= 2;
//			buf = xrealloc(buf, max + 1);
//		}
//		count = read(fd, buf + offset, max - offset);
//		if (count < 0 && errno != EINTR)
//			goto onerror;
//		if (count == 0)
//			break;
//		offset += count;
//	}
//	xrealloc(buf, offset + 1);
//	buf[offset] = '\0';
//	return buf;
//
//onerror:
//	free(buf);
//	return NULL;
//}


/********** 文字列バッファ **********/

/* 未初期化の文字列バッファを初期化する。 */
void strbuf_init(struct strbuf *buf)
{
	buf->contents = xmalloc(STRBUF_INITSIZE + 1);
	buf->contents[0] = '\0';
	buf->length = 0;
	buf->maxlength = STRBUF_INITSIZE;
}

/* 初期化済の文字列バッファの内容を削除し、未初期化状態に戻す。 */
void strbuf_destroy(struct strbuf *buf)
{
	free(buf->contents);
#ifndef NDEBUG
	*buf = (struct strbuf) {
		.contents = NULL,
		.length = 0,
		.maxlength = 0,
	};
#endif
}

/* 文字列バッファを解放し、文字列を返す。文字列バッファは未初期化状態になる。
 * 戻り値: 文字列バッファに入っていた文字列。この文字列は呼び出し元で free
 *         すべし。 */
char *strbuf_tostr(struct strbuf *buf)
{
	strbuf_trim(buf);

	char *result = buf->contents;
	*buf = (struct strbuf) {
		.contents = NULL,
		.length = 0,
		.maxlength = 0,
	};
	return result;
}

/* 文字列バッファの maxlength を変更する。短くしすぎると文字列の末尾が消える */
void strbuf_setmax(struct strbuf *buf, size_t newmax)
{
	buf->maxlength = newmax;
	buf->contents = xrealloc(buf->contents, newmax + 1);
	buf->contents[newmax] = '\0';
}

/* 文字列バッファの大きさを実際の内容ぎりぎりにする。 */
void strbuf_trim(struct strbuf *buf)
{
	buf->maxlength = buf->length;
	buf->contents = xrealloc(buf->contents, buf->maxlength + 1);
}

/* 文字列バッファを空にする。maxlength は変わらない。 */
void strbuf_clear(struct strbuf *buf)
{
	buf->length = 0;
	buf->contents[buf->length] = '\0';
}

/* 文字列バッファ内の前から i 文字目に文字列 s の最初の n 文字を挿入する。
 * s が n 文字に満たなければ s 全体を挿入する。
 * i が大きすぎて文字列の末尾を越えていれば、文字列の末尾に s を付け加える。 */
void strbuf_ninsert(struct strbuf *buf, size_t i, const char *s, size_t n)
{
	if (s) {
		size_t len = strlen(s);
		len = MIN(len, n);
		i = MIN(i, buf->length);
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
}

/* 文字列バッファ内の前から i 文字目に文字列 s を挿入する。
 * i が大きすぎて文字列の末尾を越えていれば、文字列の末尾に s を付け加える。 */
void strbuf_insert(struct strbuf *buf, size_t i, const char *s)
{
	return strbuf_ninsert(buf, i, s, SIZE_MAX);
}

/* 文字列バッファ内の文字列の末尾に文字列 s の最初の n 文字を付け加える。
 * s が n 文字に満たなければ s 全体を付け加える。 */
void strbuf_nappend(struct strbuf *buf, const char *s, size_t n)
{
	return strbuf_ninsert(buf, SIZE_MAX, s, n);
}

/* 文字列バッファ内の文字列の末尾に文字列 s を付け加える。 */
void strbuf_append(struct strbuf *buf, const char *s)
{
	return strbuf_nappend(buf, s, SIZE_MAX);
}

/* 文字列バッファの末尾に一文字追加する。 */
void strbuf_cappend(struct strbuf *buf, char c)
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
void strbuf_replace(struct strbuf *buf, size_t i, size_t n, const char *s)
{
	size_t slen = strlen(s);
	n = MIN(n, buf->length - i);
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
		strbuf_insert(buf, i + n, s + n);
	}
}

/* 文字列をフォーマットして、文字列バッファの末尾に付け加える。 */
int strbuf_vprintf(struct strbuf *buf, const char *format, va_list ap)
{
	ssize_t rest = buf->maxlength - buf->length + 1;
	int result = vsnprintf(buf->contents + buf->length, rest, format, ap);

	if (result >= rest) {  /* バッファが足りない */
		do
			buf->maxlength = buf->maxlength * 2 + 1;
		while (result + buf->length > buf->maxlength);
		buf->contents = xrealloc(buf->contents, buf->maxlength + 1);
		rest = buf->maxlength - buf->length + 1;
		result = vsnprintf(buf->contents + buf->length, rest, format, ap);
	}
	if (result >= 0)
		buf->length += result;
	buf->contents[buf->length] = '\0';  /* just in case */
	return result;
}

/* 文字列をフォーマットして、文字列バッファの末尾に付け加える。 */
int strbuf_printf(struct strbuf *buf, const char *format, ...)
{
	va_list ap;
	int result;

	va_start(ap, format);
	result = strbuf_vprintf(buf, format, ap);
	va_end(ap);
	return result;
}


/********** ポインタリスト **********/

/* ポインタリストはポインタを要素とする可変長配列である。
 * struct plist list に対して、list.contents[list.length] は常に NULL である。
 * しかし、これはリストの要素に NULL ポインタを含めることを妨げない。 */

/* 未初期化のポインタリストを初期化する。 */
void plist_init(struct plist *list)
{
	list->contents = xmalloc((PLIST_INITSIZE + 1) * sizeof(void *));
	list->contents[0] = NULL;
	list->length = 0;
	list->maxlength = PLIST_INITSIZE;
}

/* 初期化済のポインタリストの内容を削除し、未初期化状態に戻す。
 * 配列内の各要素は解放されないので注意。 */
void plist_destroy(struct plist *list)
{
	free(list->contents);
#ifndef NDEBUG
	*list = (struct plist) {
		.contents = NULL,
		.length = 0,
		.maxlength = 0,
	};
#endif
}

/* ポインタリストを解放し、内容を返す。ポインタリストは未初期化状態になる。
 * 戻り値: ポインタリストに入っていた配列。この配列は呼び出し元で free
 *         すべし。 */
void **plist_toary(struct plist *list)
{
	plist_trim(list);

	void **result = list->contents;
	*list = (struct plist) {
		.contents = NULL,
		.length = 0,
		.maxlength = 0,
	};
	return result;
}

/* ポインタリストの maxlength を変更する。短くしすぎると配列の末尾が消える */
void plist_setmax(struct plist *list, size_t newmax)
{
	list->maxlength = newmax;
	list->contents = xrealloc(list->contents, (newmax + 1) * sizeof(void *));
	list->contents[newmax] = NULL;
}

/* ポインタリストの大きさを実際の内容ぎりぎりにする。 */
void plist_trim(struct plist *list)
{
	list->maxlength = list->length;
	list->contents = xrealloc(list->contents,
			(list->maxlength + 1) * sizeof(void *));
}

/* ポインタリストを空にする。maxlength は変わらない。
 * 配列内の各要素は解放されないので注意。 */
void plist_clear(struct plist *list)
{
	list->length = 0;
	list->contents[list->length] = NULL;
}

/* ポインタリスト内の前から i 要素目に要素 e を挿入する。
 * i が大きすぎて配列の末尾を越えていれば、配列の末尾に e を付け加える。 */
void plist_insert(struct plist *list, size_t i, void *e)
{
	i = MIN(i, list->length);
	if (list->length == list->maxlength) {
		plist_setmax(list, list->maxlength * 2 + 1);
	}
	assert(list->length < list->maxlength);
	list->contents[list->length] = e;
	list->length++;
	list->contents[list->length] = NULL;
}

/* ポインタリスト内の配列の末尾に要素 e を付け加える。 */
void plist_append(struct plist *list, void *e)
{
	return plist_insert(list, SIZE_MAX, e);
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

	ht_clear(ht);
}

/* ハッシュテーブルを解放する。各エントリーは解放されないので、
 * 必要に応じてあらかじめ ht_each などを利用して解放しておくこと。 */
void ht_destroy(struct hasht *ht)
{
	for (size_t i = 0, len = ht->capacity; i < len; i++)
		free(ht->entries[i].key);
	free(ht->indices);
	free(ht->entries);
#ifndef NDEBUG
	*ht = (struct hasht) {
		.capacity = 0,
		.count = 0,
		.nullindex = NOTHING,
		.tailindex = 0,
		.indices = NULL,
		.entries = NULL,
	};
#endif
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

/* ハッシュテーブルを空にする。各エントリーの値は解放されないので、
 * 必要に応じてあらかじめ ht_each などを利用して解放しておくこと。
 * 容量は変わらない。 */
void ht_clear(struct hasht *ht)
{
	ht->count = 0;
	ht->nullindex = NOTHING;
	ht->tailindex = 0;

	for (size_t i = 0, len = ht->capacity; i < len; i++) {
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
void *ht_set(struct hasht *ht, const char *key, void *value)
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
			entry->value = value;
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
			.value = value,
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
		.value = value,
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
