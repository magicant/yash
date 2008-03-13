/* Yash: yet another shell */
/* hashtable.c: hashtable library */
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
#include <assert.h>
#include <stdlib.h>
#include "util.h"
#include "hashtable.h"


/* ハッシュテーブルは、「キー」から「値」を引くための対応表である。
 * キーと値はどちらも void * 型である。値は NULL でも良いが、キーは NULL
 * にはできない。これらのポインタの指す先のオブジェクトを確保・解放するのは、
 * すべて呼出し元の責任である。 */
/* ハッシュテーブルの容量は 0 にはならない。 */
/* このハッシュテーブルの実装は、連鎖式クローズドハッシュ方式による。 */


/* 無効なインデックスを表す */
#define NOTHING ((size_t) -1)

/* struct hash_entry の定義 */
struct hash_entry {
	size_t next;
	unsigned long hash;
	kvpair_t kv;
};
/* ハッシュエントリが有効かどうかは、kv.key が NULL でないかどうかで判別する。
 * kv.key が NULL のとき、struct hash_entry 内の他のメンバは全て不定である。 */


/* 未初期化のハッシュテーブルを、指定した初期容量で初期化する。
 * hashfunc はキーのハッシュ値を求めるハッシュ関数へのポインタ、
 * keycmp は二つのキーを比較する比較関数へのポインタである。
 * 比較関数は、二つのキーが等しいとき 0 を、異なるときに非 0 を返す。 */
hashtable_t *ht_initwithcapacity(
		hashtable_t *ht, hashfunc_t *hashfunc, keycmp_t *keycmp,
		size_t capacity)
{
	if (capacity == 0)
		capacity = 1;

	ht->count = 0;
	ht->capacity = capacity;
	ht->hashfunc = hashfunc;
	ht->keycmp = keycmp;
	ht->emptyindex = NOTHING;
	ht->tailindex = 0;
	ht->indices = xmalloc(capacity * sizeof *ht->indices);
	ht->entries = xmalloc(capacity * sizeof *ht->entries);

	for (size_t i = 0; i < capacity; i++) {
		ht->indices[i] = NOTHING;
		ht->entries[i].kv.key = NULL;
	}

	return ht;
}

/* 初期化済みのハッシュテーブルを解放する。
 * ハッシュテーブルに含まれる各キー・各値の解放は予め行っておくこと。 */
void ht_destroy(hashtable_t *ht)
{
	free(ht->indices);
	free(ht->entries);
}

/* ハッシュテーブルの容量を変更する。 */
static hashtable_t *ht_rehash(hashtable_t *ht, size_t newcapacity)
{
	assert(newcapacity > 0 && newcapacity >= ht->count);

	size_t oldcapacity = ht->capacity;
	size_t *oldindices = ht->indices;
	size_t *newindices = xmalloc(newcapacity * sizeof *ht->indices);
	struct hash_entry *oldentries = ht->entries;
	struct hash_entry *newentries = xmalloc(newcapacity * sizeof *ht->entries);
	size_t tail = 0;

	for (size_t i = 0; i < newcapacity; i++) {
		newindices[i] = NOTHING;
		newentries[i].kv.key = NULL;
	}

	/* oldentries から newentries にデータを移す */
	for (size_t i = 0; i < oldcapacity; i++) {
		void *key = oldentries[i].kv.key;
		if (key) {
			unsigned long hash = oldentries[i].hash;
			size_t newindex = (size_t) hash % newcapacity;
			newentries[tail] = (struct hash_entry) {
				.next = newindices[newindex],
				.hash = hash,
				.kv = oldentries[i].kv,
			};
			newindices[newindex] = tail;
			tail++;
		}
	}

	free(oldindices);
	free(oldentries);
	ht->capacity = newcapacity;
	ht->emptyindex = NOTHING;
	ht->tailindex = tail;
	ht->indices = newindices;
	ht->entries = newentries;
	return ht;
}

/* ハッシュテーブルが少なくとも capacity 以上の容量を持つように拡張する。 */
inline hashtable_t *ht_ensurecapacity(hashtable_t *ht, size_t capacity)
{
	if (ht->capacity < capacity) {
		size_t newcapacity = ht->capacity;
		do
			newcapacity = newcapacity * 2 + 1;
		while (newcapacity < capacity);
		return ht_rehash(ht, newcapacity);
	} else {
		return ht;
	}
}

/* ハッシュテーブルの全エントリを削除する。freer が NULL でなければ、
 * 各エントリに対して freer を一回ずつ呼出す。
 * ハッシュテーブルの容量は変わらない。 */
hashtable_t *ht_clear(hashtable_t *ht, void freer(kvpair_t kv))
{
	size_t *indices = ht->indices;
	struct hash_entry *entries = ht->entries;

	for (size_t i = 0, cap = ht->capacity; i < cap; i++) {
		indices[i] = NOTHING;
		if (entries[i].kv.key) {
			if (freer)
				freer(entries[i].kv);
			entries[i].kv.key = NULL;
		}
	}

	ht->count = 0;
	ht->emptyindex = NOTHING;
	ht->tailindex = 0;
	return ht;
}

/* ハッシュテーブルの値を取得する。
 * key が NULL であるか、key に対応する要素がなければ { NULL, NULL } を返す。 */
kvpair_t ht_get(hashtable_t *ht, const void *key)
{
	if (key) {
		unsigned long hash = ht->hashfunc(key);
		size_t index = ht->indices[(size_t) hash % ht->capacity];
		while (index != NOTHING) {
			struct hash_entry *entry = &ht->entries[index];
			if (entry->hash == hash && ht->keycmp(entry->kv.key, key) == 0)
				return entry->kv;
			index = entry->next;
		}
	}
	return (kvpair_t) { NULL, NULL, };
}

/* ハッシュテーブルにエントリを設定する。
 * 戻り値は、これまでに設定されていた key に等しいキーとそれに対応する値である。
 * これまでに key に等しいキーを持つエントリがなかった場合は { NULL, NULL }
 * を返す。key が NULL ならば何もせずに { NULL, NULL } を返す。 */
kvpair_t ht_set(hashtable_t *ht, const void *key, const void *value)
{
	if (!key)
		return (kvpair_t) { NULL, NULL, };

	/* まず、key に等しいキーの既存のエントリがあるならそれを置き換える */
	unsigned long hash = ht->hashfunc(key);
	size_t mhash = (size_t) hash % ht->capacity;
	size_t index = ht->indices[mhash];
	while (index != NOTHING) {
		struct hash_entry *entry = &ht->entries[index];
		if (entry->hash == hash && ht->keycmp(entry->kv.key, key) == 0) {
			kvpair_t oldkv = entry->kv;
			entry->kv = (kvpair_t) { (void *) key, (void *) value, };
			return oldkv;
		}
		index = entry->next;
	}

	/* 既存のエントリがなかったので、新しいエントリを追加する。 */
	index = ht->emptyindex;
	if (index != NOTHING) {
		/* empty entry があればそこに追加する。 */
		struct hash_entry *entry = &ht->entries[index];
		ht->emptyindex = entry->next;
		*entry = (struct hash_entry) {
			.next = ht->indices[mhash],
			.hash = hash,
			.kv = (kvpair_t) { (void *) key, (void *) value, },
		};
		ht->indices[mhash] = index;
	} else {
		/* empty entry がなければ tail entry に追加する。 */
		ht_ensurecapacity(ht, ht->count + 1);
		mhash = (size_t) hash % ht->capacity;
		index = ht->tailindex;
		ht->entries[index] = (struct hash_entry) {
			.next = ht->indices[mhash],
			.hash = hash,
			.kv = (kvpair_t) { (void *) key, (void *) value, },
		};
		ht->indices[mhash] = index;
		ht->tailindex++;
	}
	ht->count++;
	return (kvpair_t) { NULL, NULL, };
}

/* ハッシュテーブルから key に等しいキーのエントリを削除する。
 * 戻り値は、削除したエントリのキーと値。key に等しいエントリがなかった場合は
 * { NULL, NULL } を返す。 */
kvpair_t ht_remove(hashtable_t *ht, const void *key)
{
	if (key) {
		unsigned long hash = ht->hashfunc(key);
		size_t *indexp = &ht->indices[(size_t) hash % ht->capacity];
		while (*indexp != NOTHING) {
			size_t index = *indexp;
			struct hash_entry *entry = &ht->entries[index];
			if (entry->hash == hash && ht->keycmp(entry->kv.key, key) == 0) {
				kvpair_t oldkv = entry->kv;
				*indexp = entry->next;
				entry->next = ht->emptyindex;
				ht->emptyindex = index;
				entry->kv.key = NULL;
				ht->count--;
				return oldkv;
			}
			indexp = &entry->next;
		}
	}
	return (kvpair_t) { NULL, NULL, };
}

/* ハッシュテーブルの各エントリに対して、関数 f を一回ずつ呼び出す。
 * 各エントリに対して f を呼び出す順序は不定である。
 * あるエントリに対して f が非 0 を返した場合、それ以降のエントリに対して f
 * は呼び出さず、ht_each はただちに f が返したその非 0 値を返す。
 * 全てのエントリに対して f が 0 を返した場合、ht_each も 0 を返す。
 * この関数の実行中に ht のエントリを追加・削除してはならない。 */
int ht_each(hashtable_t *ht, int f(kvpair_t kv))
{
	struct hash_entry *entries = ht->entries;

	for (size_t i = 0, cap = ht->capacity; i < cap; i++) {
		kvpair_t kv = entries[i].kv;
		if (kv.key) {
			int r = f(kv);
			if (r)
				return r;
		}
	}
	return 0;
}

/* ハッシュテーブルの内容を列挙する。
 * 最初に列挙を開始する前に、*indexp を 0 に初期化しておく。
 * その後同じ ht と indexp をこの関数に渡す度にキーと値のペアが返される。
 * *indexp は列挙がどこまで進んだかを覚えておくためにこの関数が書き換える。
 * 列挙の途中で勝手に *indexp の値を変えてはならない。
 * また、列挙の途中でハッシュテーブルのエントリを追加・削除してはならない。
 * この関数は各エントリを一度ずつ返すが、その順番は不定である。
 * 全ての列挙が終わると { NULL, NULL } が返る。 */
kvpair_t ht_next(hashtable_t *ht, size_t *indexp)
{
	while (*indexp < ht->capacity) {
		kvpair_t kv = ht->entries[*indexp].kv;
		(*indexp)++;
		if (kv.key)
			return kv;
	}
	return (kvpair_t) { NULL, NULL, };
}
