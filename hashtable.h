/* Yash: yet another shell */
/* hashtable.h: hashtable library */
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


#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stddef.h>

typedef unsigned long hashfunc_T(const void *key);
typedef int keycmp_T(const void *key1, const void *key2);
typedef struct hashtable_T {
	size_t count, capacity;
	hashfunc_T *hashfunc;
	keycmp_T *keycmp;
	size_t emptyindex, tailindex;
	size_t *indices;
	struct hash_entry *entries;
} hashtable_T;
typedef struct kvpair_T {
	void *key, *value;
} kvpair_T;

static inline hashtable_T *ht_init(
		hashtable_T *ht, hashfunc_T *hashfunc, keycmp_T *keycmp)
	__attribute__((nonnull));
extern hashtable_T *ht_initwithcapacity(
		hashtable_T *ht, hashfunc_T *hashfunc, keycmp_T *keycmp,
		size_t capacity)
	__attribute__((nonnull));
extern void ht_destroy(hashtable_T *ht)
	__attribute__((nonnull));
extern hashtable_T *ht_ensurecapacity(hashtable_T *ht, size_t capacity)
	__attribute__((nonnull));
extern hashtable_T *ht_clear(
		hashtable_T *ht, void freer(kvpair_T kv))
	__attribute__((nonnull(1)));
extern kvpair_T ht_get(hashtable_T *ht, const void *key)
	__attribute__((nonnull(1)));
extern kvpair_T ht_set(hashtable_T *ht, const void *key, const void *value)
	__attribute__((nonnull(1)));
extern kvpair_T ht_remove(hashtable_T *ht, const void *key)
	__attribute__((nonnull(1)));
extern int ht_each(hashtable_T *ht, int f(kvpair_T kv))
	__attribute__((nonnull));
extern kvpair_T ht_next(hashtable_T *ht, size_t *indexp)
	__attribute__((nonnull));

extern unsigned long hashstr(const void *s);
extern int htstrcmp(const void *s1, const void *s2);
extern unsigned long hashwcs(const void *s);
extern int htwcscmp(const void *s1, const void *s2);


#define HASHTABLE_DEFAULT_INIT_CAPACITY 5

/* 未初期化のハッシュテーブルを、デフォルトの初期容量で初期化する。
 * hashfunc はキーのハッシュ値を求めるハッシュ関数へのポインタ、
 * keycmp は二つのキーを比較する比較関数へのポインタである。
 * 比較関数は、二つのキーが等しいとき 0 を、異なるときに非 0 を返す。 */
static inline hashtable_T *ht_init(
		hashtable_T *ht, hashfunc_T *hashfunc, keycmp_T *keycmp)
{
	return ht_initwithcapacity(
			ht, hashfunc, keycmp, HASHTABLE_DEFAULT_INIT_CAPACITY);
}


#endif /* HASHTABLE_H */
