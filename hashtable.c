/* Yash: yet another shell */
/* hashtable.c: hashtable library */
/* (C) 2007-2012 magicant */

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
#include "hashtable.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "util.h"


/* A hashtable is a mapping from keys to values.
 * Keys and values are all of type (void *).
 * NULL is allowed as a value, but not as a key.
 * The capacity of a hashtable is always no less than one. */

/* The hashtable_T structure is defined as follows:
 *   struct hashtable_T {
 *      size_t             capacity;
 *      size_t             count;
 *      hashfunc_T        *hashfunc;
 *      keycmp             keycmp;
 *      size_t             emptyindex;
 *      size_t             tailindex;
 *      size_t            *indices;
 *      struct hash_entry *entries;
 *   }
 * `capacity' is the size of array `entries'.
 * `count' is the number of entries contained in the hashtable.
 * `hashfunc' is a pointer to the hash function.
 * `keycmp' is a pointer to the function that compares keys.
 * `emptyindex' is the index of the first empty entry.
 * `tailindex' is the index of the first tail entry.
 * `indices' is a pointer to the bucket array.
 * `entries' is a pointer to the array of entries.
 *
 * The collision resolution strategy used in this implementation is a kind of
 * separate chaining, but it differs from normal chaining in that entries are
 * stored in a single array (`entries'). An advantage over normal chaining,
 * which stores entries in linked lists, is spatial locality: entries can be
 * quickly referenced because they are collected in one array. Another advantage
 * is that we don't have to call `malloc' or `free' each time an entry is added
 * or removed. */


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


/* Initializes a hashtable with the specified capacity.
 * `hashfunc' is a hash function to hash keys.
 * `keycmp' is a function that compares two keys. */
hashtable_T *ht_initwithcapacity(
	hashtable_T *ht, hashfunc_T *hashfunc, keycmp_T *keycmp,
	size_t capacity)
{
    if (capacity == 0)
	capacity = 1;

    ht->capacity = capacity;
    ht->count = 0;
    ht->hashfunc = hashfunc;
    ht->keycmp = keycmp;
    ht->emptyindex = NOTHING;
    ht->tailindex = 0;
    ht->indices = xmallocn(capacity, sizeof *ht->indices);
    ht->entries = xmallocn(capacity, sizeof *ht->entries);

    for (size_t i = 0; i < capacity; i++) {
	ht->indices[i] = NOTHING;
	ht->entries[i].kv.key = NULL;
    }

    return ht;
}

/* Changes the capacity of the specified hashtable.
 * If the specified new capacity is smaller than the number of the entries in
 * the hashtable, the capacity is not changed.
 * Note that the capacity cannot be zero. If `newcapacity' is zero, it is
 * assumed to be one. */
/* Capacity should be an odd integer, especially a prime number. */
hashtable_T *ht_setcapacity(hashtable_T *ht, size_t newcapacity)
{
    if (newcapacity == 0)
	newcapacity = 1;
    if (newcapacity < ht->count)
	newcapacity = ht->count;

    size_t oldcapacity = ht->capacity;
    size_t *oldindices = ht->indices;
    size_t *newindices = xmallocn(newcapacity, sizeof *ht->indices);
    struct hash_entry *oldentries = ht->entries;
    struct hash_entry *newentries = xmallocn(newcapacity, sizeof *ht->entries);
    size_t tail = 0;

    for (size_t i = 0; i < newcapacity; i++) {
	newindices[i] = NOTHING;
	newentries[i].kv.key = NULL;
    }

    /* move the data from oldentries to newentries */
    for (size_t i = 0; i < oldcapacity; i++) {
	void *key = oldentries[i].kv.key;
	if (key != NULL) {
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

/* Increases the capacity as large as necessary
 * so that the capacity is no less than the specified. */
hashtable_T *ht_ensurecapacity(hashtable_T *ht, size_t capacity)
{
    if (capacity <= ht->capacity)
	return ht;

    size_t cap15 = ht->capacity + (ht->capacity >> 1);
    if (capacity < cap15)
	capacity = cap15;
    if (capacity < ht->capacity + 6)
	capacity = ht->capacity + 6;
    return ht_setcapacity(ht, capacity);
}

/* Removes all the entries of a hashtable.
 * If `freer' is non-NULL, it is called for each entry removed (in an
 * unspecified order).
 * The capacity of the hashtable is not changed. */
hashtable_T *ht_clear(hashtable_T *ht, void freer(kvpair_T kv))
{
    size_t *indices = ht->indices;
    struct hash_entry *entries = ht->entries;

    if (ht->count == 0)
	return ht;

    for (size_t i = 0, cap = ht->capacity; i < cap; i++) {
	indices[i] = NOTHING;
	if (entries[i].kv.key != NULL) {
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
kvpair_T ht_get(const hashtable_T *ht, const void *key)
{
    if (key != NULL) {
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
 * removing and returning the old entry for the key.
 * If there is no such old entry, { NULL, NULL } is returned.
 * `key' must not be NULL. */
kvpair_T ht_set(hashtable_T *ht, const void *key, const void *value)
{
    assert(key != NULL);

    /* if there is an entry with the specified key, simply replace the value */
    hashval_T hash = ht->hashfunc(key);
    size_t mhash = (size_t) hash % ht->capacity;
    size_t index = ht->indices[mhash];
    struct hash_entry *entry;
    while (index != NOTHING) {
	entry = &ht->entries[index];
	if (entry->hash == hash && ht->keycmp(entry->kv.key, key) == 0) {
	    kvpair_T oldkv = entry->kv;
	    entry->kv = (kvpair_T) { (void *) key, (void *) value, };
	    DEBUG_PRINT_STATISTICS(ht);
	    return oldkv;
	}
	index = entry->next;
    }

    /* No entry with the specified key was found; we add a new entry. */
    index = ht->emptyindex;
    if (index != NOTHING) {
	/* if there is an empty entry, use it */
	entry = &ht->entries[index];
	ht->emptyindex = entry->next;
    } else {
	/* if there is no empty entry, use a tail entry */
	ht_ensurecapacity(ht, ht->count + 1);
	mhash = (size_t) hash % ht->capacity;
	index = ht->tailindex++;
	entry = &ht->entries[index];
    }
    *entry = (struct hash_entry) {
	.next = ht->indices[mhash],
	.hash = hash,
	.kv = (kvpair_T) { (void *) key, (void *) value, },
    };
    ht->indices[mhash] = index;
    ht->count++;
    DEBUG_PRINT_STATISTICS(ht);
    return (kvpair_T) { NULL, NULL, };
}

/* Removes and returns the entry with the specified key.
 * If `key' is NULL or there is no such entry, { NULL, NULL } is returned. */
kvpair_T ht_remove(hashtable_T *ht, const void *key)
{
    if (key != NULL) {
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

#if 0

/* Calls the specified function `f' for each entry in the specified hashtable.
 * The order in which the entries are applied the function to is unspecified.
 * If `f' returns a non-zero value for some entry, `f' is not called any more
 * and `ht_each' immediately returns the non-zero value. Otherwise, that is,
 * if `f' returns zero for all the entry, `ht_each' also returns zero.
 * You must not add or remove any entry inside function `f'. */
int ht_each(const hashtable_T *ht, int f(kvpair_T kv))
{
    struct hash_entry *entries = ht->entries;

    for (size_t i = 0, cap = ht->capacity; i < cap; i++) {
	kvpair_T kv = entries[i].kv;
	if (kv.key != NULL) {
	    int r = f(kv);
	    if (r != 0)
		return r;
	}
    }
    return 0;
}

#endif

/* Iterates the entries of the specified hashtable.
 * When starting new iteration, `*indexp' must have been initialized to zero.
 * Each time this function is called, it updates `*indexp' and returns one
 * entry.
 * You must not change the value of `*indexp' from outside this function or
 * add/remove any entry in the hashtable until the iteration finishes.
 * Each entry is returned exactly once, in an unspecified order.
 * If there is no more entry to be iterated, { NULL, NULL } is returned. */
kvpair_T ht_next(const hashtable_T *restrict ht, size_t *restrict indexp)
{
    while (*indexp < ht->capacity) {
	kvpair_T kv = ht->entries[*indexp].kv;
	(*indexp)++;
	if (kv.key != NULL)
	    return kv;
    }
    return (kvpair_T) { NULL, NULL, };
}

/* Returns a newly malloced array of key-value pairs that contains all the
 * elements of the specified hashtable.
 * The returned array is terminated by the { NULL, NULL } element. */
kvpair_T *ht_tokvarray(const hashtable_T *ht)
{
    kvpair_T *array = xmalloce(ht->count, 1, sizeof *array);
    size_t index = 0;

    for (size_t i = 0; i < ht->capacity; i++) {
	if (ht->entries[i].kv.key != NULL)
	    array[index++] = ht->entries[i].kv;
    }

    assert(index == ht->count);
    array[index] = (kvpair_T) { NULL, NULL, };
    return array;
}


/* A hash function for a byte string.
 * The argument is a pointer to a byte string (const char *).
 * You can use `htstrcmp' as a corresponding comparison function. */
hashval_T hashstr(const void *s)
{
    /* The hashing algorithm is FNV hash.
     * Cf. http://www.isthe.com/chongo/tech/comp/fnv/ */
    const unsigned char *c = s;
    hashval_T h = 0;
    while (*c != '\0')
	h = (h ^ (hashval_T) *c++) * FNVPRIME;
    return h;
}

/* A hash function for a wide string.
 * The argument is a pointer to a wide string (const wchar_t *).
 * You can use `htwcscmp' for a corresponding comparison function. */
hashval_T hashwcs(const void *s)
{
    /* The hashing algorithm is a slightly modified version of FNV hash.
     * Cf. http://www.isthe.com/chongo/tech/comp/fnv/ */
    const wchar_t *c = s;
    hashval_T h = 0;
    while (*c != L'\0')
	h = (h ^ (hashval_T) *c++) * FNVPRIME;
    return h;
}

/* A comparison function for wide strings.
 * The arguments are pointers to wide strings (const wchar_t *).
 * You can use `hashwcs' for a corresponding hash function. */
int htwcscmp(const void *s1, const void *s2)
{
    return wcscmp((const wchar_t *) s1, (const wchar_t *) s2);
}

/* A comparison function for key-value pairs with multibyte-string keys.
 * The arguments are pointers to kvpair_T's (const kvpair_T *) whose keys are
 * multibyte strings. */
int keystrcoll(const void *k1, const void *k2)
{
    return strcoll(((const kvpair_T *) k1)->key, ((const kvpair_T *) k2)->key);
}

/* A comparison function for key-value pairs with wide-string keys.
 * The arguments are pointers to kvpair_T's (const kvpair_T *) whose keys are
 * wide strings. */
int keywcscoll(const void *k1, const void *k2)
{
    return wcscoll(((const kvpair_T *) k1)->key, ((const kvpair_T *) k2)->key);
}

/* `Free's the key of the specified key-value pair.
 * Can be used as the freer function to `ht_clear'. */
void kfree(kvpair_T kv)
{
    free(kv.key);
}

/* `Free's the value of the specified key-value pair.
 * Can be used as the freer function to `ht_clear'. */
void vfree(kvpair_T kv)
{
    free(kv.value);
}

/* `Free's the key and the value of the specified key-value pair.
 * Can be used as the freer function to `ht_clear'. */
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
    fprintf(stderr, "DEBUG: id=%p hash->count=%zu, capacity=%zu\n",
	    (void *) ht, ht->count, ht->capacity);
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
