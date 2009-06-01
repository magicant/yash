/* Yash: yet another shell */
/* hashtable.c: hashtable library */
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


#include "common.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "hashtable.h"
#include "util.h"


/* A hashtable is a mapping from keys to values.
 * Keys and values are all of type (void *).
 * NULL is allowed as a value, but not as a key.
 * The capacity of a hashtable is always no less than one. */
/* The implementation is a chained closed hashing. */


//#define DEBUG_HASH 1
#if DEBUG_HASH   /* For debugging */
# define DEBUG_PRINT_STATISTICS(ht) (print_statistics(ht))
# include <stdio.h>
static void print_statistics(const hashtable_T *ht);
#else
# define DEBUG_PRINT_STATISTICS(ht) ((void) 0)
#endif


/* The null index */
#define NOTHING ((size_t) -1)

/* hashtable entry */
struct hash_entry {
    size_t next;
    hashval_T hash;
    kvpair_T kv;
};
/* An entry is occupied iff `.kv.key' is non-NULL.
 * When an entry is unoccupied, the values of the other members of the entry are
 * unspecified. */

static hashtable_T *ht_rehash(hashtable_T *ht, size_t newcapacity);


/* Initializes a hashtable with the specified capacity.
 * `hashfunc' is a hash function to hash keys.
 * `keycmp' is a function that compares two keys. */
hashtable_T *ht_initwithcapacity(
	hashtable_T *ht, hashfunc_T *hashfunc, keycmp_T *keycmp,
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

/* Destroys a hashtable.
 * Note that this function doesn't `free' any of the keys and the values. */
void ht_destroy(hashtable_T *ht)
{
    free(ht->indices);
    free(ht->entries);
}

/* Changes the capacity of a hashtable.
 * Note that the capacity must not be zero. */
hashtable_T *ht_rehash(hashtable_T *ht, size_t newcapacity)
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

    /* move the data from oldentries to newentries */
    for (size_t i = 0; i < oldcapacity; i++) {
	void *key = oldentries[i].kv.key;
	if (key) {
	    hashval_T hash = oldentries[i].hash;
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

/* Increases the capacity if needed
 * so that the capacity is no less than the specified. */
inline hashtable_T *ht_ensurecapacity(hashtable_T *ht, size_t capacity)
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

/* Removes all the entries of a hashtable.
 * If `freer' is non-NULL, it is called for each entry removed.
 * The capacity of the hashtable is not changed. */
hashtable_T *ht_clear(hashtable_T *ht, void freer(kvpair_T kv))
{
    size_t *indices = ht->indices;
    struct hash_entry *entries = ht->entries;

    if (ht->count == 0)
	return ht;

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

/* Returns the entry whose key is equal to the specified `key',
 * or { NULL, NULL } if `key' is NULL or there is no such entry. */
kvpair_T ht_get(hashtable_T *ht, const void *key)
{
    if (key) {
	hashval_T hash = ht->hashfunc(key);
	size_t index = ht->indices[(size_t) hash % ht->capacity];
	while (index != NOTHING) {
	    struct hash_entry *entry = &ht->entries[index];
	    if (entry->hash == hash && ht->keycmp(entry->kv.key, key) == 0)
		return entry->kv;
	    index = entry->next;
	}
    }
    return (kvpair_T) { NULL, NULL, };
}

/* Makes a new entry with the specified key and value,
 * removing and returning the old entry with an equal key.
 * If there is no such old entry, { NULL, NULL } is returned.
 * `key' must not be NULL. */
kvpair_T ht_set(hashtable_T *ht, const void *key, const void *value)
{
    assert(key != NULL);

    /* if there is an entry with an equal key, replace it */
    hashval_T hash = ht->hashfunc(key);
    size_t mhash = (size_t) hash % ht->capacity;
    size_t index = ht->indices[mhash];
    while (index != NOTHING) {
	struct hash_entry *entry = &ht->entries[index];
	if (entry->hash == hash && ht->keycmp(entry->kv.key, key) == 0) {
	    kvpair_T oldkv = entry->kv;
	    entry->kv = (kvpair_T) { (void *) key, (void *) value, };
	    DEBUG_PRINT_STATISTICS(ht);
	    return oldkv;
	}
	index = entry->next;
    }

    /* no entry with an equal key found, so add a new entry */
    index = ht->emptyindex;
    if (index != NOTHING) {
	/* if there are empty entries, use one of them */
	struct hash_entry *entry = &ht->entries[index];
	ht->emptyindex = entry->next;
	*entry = (struct hash_entry) {
	    .next = ht->indices[mhash],
	    .hash = hash,
	    .kv = (kvpair_T) { (void *) key, (void *) value, },
	};
	ht->indices[mhash] = index;
    } else {
	/* if there are no empty entries, use a tail entry */
	ht_ensurecapacity(ht, ht->count + (ht->count >> 2) + 1);
	mhash = (size_t) hash % ht->capacity;
	index = ht->tailindex;
	ht->entries[index] = (struct hash_entry) {
	    .next = ht->indices[mhash],
	    .hash = hash,
	    .kv = (kvpair_T) { (void *) key, (void *) value, },
	};
	ht->indices[mhash] = index;
	ht->tailindex++;
    }
    ht->count++;
    DEBUG_PRINT_STATISTICS(ht);
    return (kvpair_T) { NULL, NULL, };
}

/* Removes and returns the entry whose key is equal to the specified `key'.
 * If `key' is NULL or there is no such entry, { NULL, NULL } is returned. */
kvpair_T ht_remove(hashtable_T *ht, const void *key)
{
    if (key) {
	hashval_T hash = ht->hashfunc(key);
	size_t *indexp = &ht->indices[(size_t) hash % ht->capacity];
	while (*indexp != NOTHING) {
	    size_t index = *indexp;
	    struct hash_entry *entry = &ht->entries[index];
	    if (entry->hash == hash && ht->keycmp(entry->kv.key, key) == 0) {
		kvpair_T oldkv = entry->kv;
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
    return (kvpair_T) { NULL, NULL, };
}

/* Calls the specified function `f' once for each entry in the hashtable `ht'.
 * The order of calls is unspecified.
 * If `f' returns a non-zero value for some entry, `f' is not called any more
 * and `ht_each' immediately returns the non-zero value. Otherwise, that is,
 * if `f' returns zero for all the entry, `ht_each' also returns zero.
 * You must not add or remove any entry during this function. */
int ht_each(hashtable_T *ht, int f(kvpair_T kv))
{
    struct hash_entry *entries = ht->entries;

    for (size_t i = 0, cap = ht->capacity; i < cap; i++) {
	kvpair_T kv = entries[i].kv;
	if (kv.key) {
	    int r = f(kv);
	    if (r)
		return r;
	}
    }
    return 0;
}

/* Iterates the entries of a hashtable.
 * Firstly, `*indexp' must be initialized to zero.
 * Each time this function is called, it returns the entry to be iterated next
 * and increases `*indexp'.
 * You must not change the value of `*indexp' from outside this function or
 * add/remove any entry in the hashtable.
 * Each entry is returned exactly once, in an unspecified order.
 * If there is no more entry to be iterated, { NULL, NULL } is returned. */
kvpair_T ht_next(hashtable_T *restrict ht, size_t *restrict indexp)
{
    while (*indexp < ht->capacity) {
	kvpair_T kv = ht->entries[*indexp].kv;
	(*indexp)++;
	if (kv.key)
	    return kv;
    }
    return (kvpair_T) { NULL, NULL, };
}

/* Returns a newly malloced array of key-value pairs that contains all the
 * element of the specified hashtable.
 * The returned array is terminated by the { NULL, NULL } element. */
kvpair_T *ht_tokvarray(hashtable_T *ht)
{
    kvpair_T *array = xmalloc(sizeof *array * (ht->count + 1));
    size_t index = 0;
    for (size_t i = 0; i < ht->capacity; i++) {
	if (ht->entries[i].kv.key) {
	    assert(index < ht->count);
	    array[index++] = ht->entries[i].kv;
	}
    }
    assert(index == ht->count);
    array[index] = (kvpair_T) { NULL, NULL, };
    return array;
}


/* A hash function for a multibyte string.
 * The argument is cast from (const char *) to (const void *).
 * You can use `htstrcmp' for a corresponding comparison function. */
hashval_T hashstr(const void *s)
{
    /* The hashing algorithm is FNV hash.
     * cf. http://www.isthe.com/chongo/tech/comp/fnv/ */
    const unsigned char *c = s;
    hashval_T h = 0;
    while (*c)
	h = (h ^ (hashval_T) *c++) * FNVPRIME;
    return h;
}

/* A hash function for a wide string.
 * The argument is cast from (const wchar_t *) to (const void *).
 * You can use `htwcscmp' for a corresponding comparison function. */
hashval_T hashwcs(const void *s)
{
    /* The hashing algorithm is a slightly modified version of FNV hash.
     * cf. http://www.isthe.com/chongo/tech/comp/fnv/ */
    const wchar_t *c = s;
    hashval_T h = 0;
    while (*c)
	h = (h ^ (hashval_T) *c++) * FNVPRIME;
    return h;
}

/* A comparison function for wide strings.
 * The argument is cast from (const wchar_t *) to (const void *).
 * You can use `hashwcs' for a corresponding hash function. */
int htwcscmp(const void *s1, const void *s2)
{
    return wcscmp((const wchar_t *) s1, (const wchar_t *) s2);
}

/* A comparison function for key-value pairs with multibyte-string keys.
 * The arguments are pointers to kvpair_T's, cast to (void *). */
int keystrcoll(const void *k1, const void *k2)
{
    return strcoll(((const kvpair_T *) k1)->key, ((const kvpair_T *) k2)->key);
}

/* A comparison function for key-value pairs with wide-string keys.
 * The arguments are pointers to kvpair_T's, cast to (void *). */
int keywcscoll(const void *k1, const void *k2)
{
    return wcscoll(((const kvpair_T *) k1)->key, ((const kvpair_T *) k2)->key);
}

/* Just `free's the key of the key-value pair `kv'.
 * Can be used as a freer function to `ht_clear'. */
void kfree(kvpair_T kv)
{
    free(kv.key);
}

/* Just `free's the value of the key-value pair `kv'.
 * Can be used as a freer function to `ht_clear'. */
void vfree(kvpair_T kv)
{
    free(kv.value);
}

/* Just `free's the key and the value of the key-value pair `kv'.
 * Can be used as a freer function to `ht_clear'. */
void kvfree(kvpair_T kv)
{
    free(kv.key);
    free(kv.value);
}


#if DEBUG_HASH
/* Prints statistics.
 * This function is used in debugging. */
void print_statistics(const hashtable_T *ht)
{
    fprintf(stderr, "DEBUG: hash->count=%zu, capacity=%zu\n",
	    ht->count, ht->capacity);
    fprintf(stderr, "DEBUG: hash->emptyindex=%zu, tailindex=%zu\n",
	    ht->emptyindex, ht->tailindex);

    unsigned emptycount = 0, collcount = 0;
    for (size_t i = ht->emptyindex; i != NOTHING; i = ht->entries[i].next)
	emptycount++;
    for (size_t i = 0; i < ht->capacity; i++)
	if (ht->entries[i].kv.key && ht->entries[i].next != NOTHING)
	    collcount++;
    fprintf(stderr, "DEBUG: hash empties=%u collisions=%u\n\n",
	    emptycount, collcount);
}
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
