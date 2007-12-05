/* Yash: yet another shell */
/* © 2007 magicant */

/* This software can be redistributed and/or modified under the terms of
 * GNU General Public License, version 2 or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRENTY. */


#include <stdlib.h>
#include <string.h>
#include "yash.h"
#include <assert.h>


int set_alias(const char *name, const char *value);
int remove_alias(const char *name);
void remove_all_aliases(void);
const char *get_alias(const char *name);
const ALIAS *get_all_aliases(void);
int for_all_aliases(int (*func)(const char *name, const char *value));
//char *expand_alias(const char *s);


/* エイリアスの配列。
 * 名前の文字コード順の連鎖リスト。 */
ALIAS *aliases = NULL;


/* エイリアスを追加/変更する。
 * エイリアスの名前 name は 1 文字以上でなければならない。
 * 戻り値: 成功すると 0、失敗すると -1。 */
int set_alias(const char *name, const char *value)
{
	size_t namelen = strlen(name);
	size_t totallen = namelen + strlen(value) + 2;
	char *s = xmalloc(totallen);
	ALIAS *a = aliases;
	ALIAS **prevnext = &aliases;
	ALIAS *newalias;

	assert(name && *name);
	assert(value);
	strcpy(s, name);
	strcpy(s + namelen + 1, value);
	while (a) {
		int cmp = strcmp(name, a->name);
		if (cmp < 0) {
			break;
		} else if (cmp == 0) {
			free((void *) a->name);
			a->name = s;
			a->value = s + namelen + 1;
			return 0;
		}
		prevnext = &a->next;
		a = a->next;
	}
	newalias = xmalloc(sizeof(ALIAS));
	newalias->name = s;
	newalias->value = s + namelen + 1;
	newalias->next = a;
	*prevnext = newalias;
	return 0;
}

/* エイリアスを削除する
 * 戻り値: エイリアスが正しく削除されたら 0、見付からなかったら -1。 */
int remove_alias(const char *name)
{
	ALIAS *a = aliases;
	ALIAS **prevnext = &aliases;

	assert(name);
	while (a) {
		if (strcmp(name, a->name) == 0) {
			*prevnext = a->next;
			free((void *) a->name);
			free(a);
			return 0;
		}
		prevnext = &a->next;
		a = a->next;
	}
	return -1;
}

/* 全エイリアスを削除する。 */
void remove_all_aliases(void)
{
	ALIAS *a = aliases;

	while (a) {
		ALIAS *next = a->next;
		free((void *) a->name);
		free(a);
		a = next;
	}
	aliases = NULL;
}

/* エイリアスの値を取得する
 * 戻り値: 見付かったエイリアスの値。見付からなかったら NULL。
 *         戻り値の内容を変更してはいけない。 */
const char *get_alias(const char *name)
{
	ALIAS *a = aliases;

	assert(name);
	while (a) {
		if (strcmp(name, a->name) == 0)
			return a->value;
		a = a->next;
	}
	return NULL;
}

/* 全てのエイリアスを取得する。より正確に言うと、エイリアスを保存している
 * 連鎖リストの最初の要素を取得する。
 * 戻り値: エイリアスを保存している連鎖リストの最初の要素。エイリアスが一つも
 *         なければ NULL。戻り値の内容を外部で変更してはならない。 */
const ALIAS *get_all_aliases(void)
{
	return aliases;
}

/* 全てのエイリアスに対して一度ずつ関数 func を呼び出す。
 * エイリアスの順番は、name の文字コード順である。
 * 関数 func が非 0 を返すと、処理を中止してただちにその値を返す。
 * func が最後まで全て 0 を返すと、0 を返す。
 * 関数 func の中で name や value を変更したり、
 * add_alias や remove_alias を呼び出したりしてはならない。 */
int for_all_aliases(int (*func)(const char *name, const char *value))
{
	ALIAS *a = aliases;

	assert(func);
	while (a) {
		int result = func(a->name, a->value);
		if (result)
			return result;
		a = a->next;
	}
	return 0;
}

///* エイリアスを展開する。
// * 文字列の先頭にあるトークンに一致するエイリアスの名前があれば、
// * そのトークンをエイリアスの値に置き換える。
// * 戻り値: エイリアスが見付かったかどうかにかかわらず、新しく malloc
// *         した文字列が返る。エイリアスが見付からなかったら、文字列の内容は元の
// *         s に等しい。何かエラーが発生したら、NULL が返る。 */
//char *expand_alias(const char *s)
//{
//	size_t stokenlen = strcspn(s, " \t\"'\\|&;<>()\n\r#");
//	char *aliasname;
//	const char *aliasvalue;
//	char *result;
//	size_t resultlen;
//
//	if (!stokenlen || s[stokenlen] == '\\')
//		goto notfound;
//	aliasname = xstrndup(s, stokenlen);
//	aliasvalue = get_alias(aliasname);
//	if (!aliasvalue)
//		goto notfound;
//	resultlen = strlen(aliasvalue) + strlen(s + stokenlen);
//	result = xmalloc(resultlen + 1);
//	strcpy(result, aliasvalue);
//	strcat(result, s + stokenlen);
//	return result;
//
//notfound:
//	return xstrdup(s);
//}
