/* Yash: yet another shell */
/* plist.c: modifiable list of pointers */
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
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "plist.h"


/********** ポインタ関連のユーティリティ関数 **********/

/* void へのポインタの配列 *list において、要素として NULL が現れるまでの
 * 要素数を返す。(NULL 要素は戻り値に含めない) */
size_t plcount(void *const *list)
{
	size_t count = 0;
	while (*list++) count++;
	return count;
}

/* ポインタの配列 *ary を、その要素であるポインタも含めて解放する。
 * 要素である各ポインタ p に対して freer(p) した後、free(ary) する。 */
void recfree(void **ary, void freer(void *elem))
{
	if (ary) {
		for (void **a = ary; *a; a++) freer(*a);
		free(ary);
	}
}


/********** ポインタリストの実装 **********/


/* ポインタリストは void へのポインタを要素とする可変長配列である。
 * plist_t 型の list について、list.contents[list.length] は常に NULL である。
 * しかし、これはリストの要素に NULL を含めることを妨げない。
 * また、リストの要素としてリストそのものへのポインタを入れることもできる。 */

/* 特に明記しない限り、以下の関数は引数の list を返す */


#define PLIST_INITSIZE 7


/* 未初期化のポインタリストを初期化する。 */
plist_t *pl_init(plist_t *list)
{
	list->contents = xmalloc((PLIST_INITSIZE + 1) * sizeof (void *));
	list->contents[0] = NULL;
	list->length = 0;
	list->maxlength = PLIST_INITSIZE;
	return list;
}

/* 初期化済みのポインタリストの内容を削除し、未初期化状態に戻す。
 * 配列の各要素は解放しないので注意。 */
void pl_destroy(plist_t *list)
{
	free(list->contents);
}

/* ポインタリストを解放し、内容を返す。ポインタリストを未初期化状態になる。
 * 戻り値は、void へのポインタの配列へのポインタである。この配列は呼出し元が
 * free すること。 */
void **pl_toary(plist_t *list)
{
	if (list->maxlength - list->length > 10)
		pl_setmax(list, list->length);
	return list->contents;
}

/* ポインタリストのサイズ (list->maxlength) を変更する。
 * 短くしすぎると、配列の末尾の要素はリストから消えてなくなる。 */
plist_t *pl_setmax(plist_t *list, size_t newmax)
{
	list->contents = xrealloc(list->contents, (newmax + 1) * sizeof (void *));
	list->maxlength = newmax;
	list->contents[newmax] = NULL;
	if (newmax < list->length)
		list->length = newmax;
	return list;
}

/* list->maxlength が max 未満なら、max 以上になるようにメモリを再確保する。 */
inline plist_t *pl_ensuremax(plist_t *list, size_t max)
{
	if (list->maxlength < max) {
		size_t newmax = list->maxlength;
		do
			newmax = newmax * 2 + 1;
		while (newmax < max);
		return pl_setmax(list, newmax);
	} else {
		return list;
	}
}

/* ポインタリストの内容を空にする。リストの容量は変化しない。
 * リストの各要素は解放しないので注意。 */
plist_t *pl_clear(plist_t *list)
{
	list->contents[list->length = 0] = NULL;
	return list;
}

/* ポインタリストの i 要素目から ln 要素を
 * ポインタの配列 a の最初の an 要素に置換する。
 * a の要素に NULL があってもそれは特別扱いしない。
 * list->length < i + ln ならばリストの i 要素目以降を全て置換する。
 * 特に、list->length <= i ならばリストの末尾に追加する。
 * a は list->contents の一部であってはならない。 */
plist_t *pl_replace(
		plist_t *restrict list, size_t i, size_t ln,
		void *const *restrict a, size_t an)
{
	if (i > list->length)
		i = list->length;
	if (ln > list->length - i)
		ln = list->length - i;

	size_t newlength = list->length - ln + an;
	pl_ensuremax(list, newlength);
	memmove(list->contents + i + an, list->contents + i + ln,
			(list->length - (i + ln) + 1) * sizeof (void *));
	memcpy(list->contents + i, a, an * sizeof (void *));
	list->length = newlength;
	return list;
}

/* ポインタリストの末尾に要素として p を追加する。
 * p は NULL でも list そのものでも良い。 */
plist_t *pl_add(plist_t *list, void *p)
{
	pl_ensuremax(list, list->length + 1);
	list->contents[list->length++] = p;
	list->contents[list->length] = NULL;
	return list;
}
