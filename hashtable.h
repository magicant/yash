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

typedef unsigned long hashfunc_t(const void *key);
typedef int keycmp_t(const void *key1, const void *key2);
typedef struct {
	size_t count, capacity;
	hashfunc_t *hashfunc;
	keycmp_t *keycmp;
	size_t emptyindex, tailindex;
	size_t *indices;
	struct hash_entry *entries;
} hashtable_t;
typedef struct {
	void *key, *value;
} kvpair_t;

static inline hashtable_t *ht_init(
		hashtable_t *ht, hashfunc_t *hashfunc, keycmp_t *keycmp)
	__attribute__((nonnull));
extern hashtable_t *ht_initwithcapacity(
		hashtable_t *ht, hashfunc_t *hashfunc, keycmp_t *keycmp,
		size_t capacity)
	__attribute__((nonnull));
extern void ht_destroy(hashtable_t *ht)
	__attribute__((nonnull));
extern hashtable_t *ht_ensurecapacity(hashtable_t *ht, size_t capacity)
	__attribute__((nonnull));
extern hashtable_t *ht_clear(
		hashtable_t *ht, void freer(kvpair_t kv))
	__attribute__((nonnull(1)));
extern kvpair_t ht_get(hashtable_t *ht, const void *key)
	__attribute__((nonnull(1)));
extern kvpair_t ht_set(hashtable_t *ht, const void *key, const void *value)
	__attribute__((nonnull(1)));
extern kvpair_t ht_remove(hashtable_t *ht, const void *key)
	__attribute__((nonnull(1)));
extern int ht_each(hashtable_t *ht, int f(kvpair_t kv))
	__attribute__((nonnull));
extern kvpair_t ht_next(hashtable_t *ht, size_t *indexp)
	__attribute__((nonnull));


#define HASHTABLE_DEFAULT_INIT_CAPACITY 5

/* 未初期化のハッシュテーブルを、デフォルトの初期容量で初期化する。
 * hashfunc はキーのハッシュ値を求めるハッシュ関数へのポインタ、
 * keycmp は二つのキーを比較する比較関数へのポインタである。
 * 比較関数は、二つのキーが等しいとき 0 を、異なるときに非 0 を返す。 */
static inline hashtable_t *ht_init(
		hashtable_t *ht, hashfunc_t *hashfunc, keycmp_t *keycmp)
{
	return ht_initwithcapacity(
			ht, hashfunc, keycmp, HASHTABLE_DEFAULT_INIT_CAPACITY);
}


#endif /* HASHTABLE_H */
