/* Yash: yet another shell */
/* hashtable.h: hashtable library */
/* (C) 2007-2018 magicant */

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


#ifndef YASH_HASHTABLE_H
#define YASH_HASHTABLE_H

#include <stdint.h>
#include <stdlib.h>


#if defined UINT64_MAX && UINT64_MAX == UINT_FAST32_MAX
typedef uint64_t hashval_T;
# define FNVPRIME 1099511628211
#elif defined UINT128_MAX && UINT128_MAX == UINT_FAST32_MAX
typedef uint128_t hashval_T;
# define FNVPRIME 309485009821345068724781371
#else
typedef uint32_t hashval_T;
# define FNVPRIME 16777619
#endif

/* The type of hash functions.
 * Hash functions must return the same value for two keys that compare equal. */
typedef hashval_T hashfunc_T(const void *key);

/* The type of functions that compare two keys.
 * Returns zero if two keys are equal, or non-zero if unequal. */
typedef int keycmp_T(const void *key1, const void *key2);

typedef struct hashtable_T {
    size_t capacity, count;
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
static inline void ht_destroy(hashtable_T *ht)
    __attribute__((nonnull));
extern hashtable_T *ht_setcapacity(hashtable_T *ht, size_t newcapacity)
    __attribute__((nonnull));
extern hashtable_T *ht_ensurecapacity(hashtable_T *ht, size_t capacity)
    __attribute__((nonnull));
extern hashtable_T *ht_clear(hashtable_T *ht, void freer(kvpair_T kv))
    __attribute__((nonnull(1)));
extern kvpair_T ht_get(const hashtable_T *ht, const void *key)
    __attribute__((nonnull(1)));
extern kvpair_T ht_set(hashtable_T *ht, const void *key, const void *value)
    __attribute__((nonnull(1,2)));
extern kvpair_T ht_remove(hashtable_T *ht, const void *key)
    __attribute__((nonnull(1)));
extern int ht_each(const hashtable_T *ht, int f(kvpair_T kv))
    __attribute__((nonnull));
extern kvpair_T ht_next(const hashtable_T *restrict ht, size_t *restrict indexp)
    __attribute__((nonnull));
extern kvpair_T *ht_tokvarray(const hashtable_T *ht)
    __attribute__((nonnull,malloc,warn_unused_result));

extern hashval_T hashstr(const void *s)             __attribute__((pure));
//extern int htstrcmp(const void *s1, const void *s2) __attribute__((pure));
// Also include <string.h> to use `htstrcmp'.
#define htstrcmp ((keycmp_T *) strcmp)
extern hashval_T hashwcs(const void *s)             __attribute__((pure));
extern int htwcscmp(const void *s1, const void *s2) __attribute__((pure));
extern int keystrcoll(const void *kv1, const void *kv2) __attribute__((pure));
extern int keywcscoll(const void *kv1, const void *kv2) __attribute__((pure));
extern void kfree(kvpair_T kv);
extern void vfree(kvpair_T kv);
extern void kvfree(kvpair_T kv);


#ifndef HASHTABLE_DEFAULT_INIT_CAPACITY
#define HASHTABLE_DEFAULT_INIT_CAPACITY 5
#endif

/* Initializes the specified hashtable with the default capacity.
 * `hashfunc' is the hash function to hash keys.
 * `keycmp' is the function that compares two keys. */
hashtable_T *ht_init(hashtable_T *ht, hashfunc_T *hashfunc, keycmp_T *keycmp)
{
    return ht_initwithcapacity(
	    ht, hashfunc, keycmp, HASHTABLE_DEFAULT_INIT_CAPACITY);
}

/* Destroys the specified hashtable.
 * Note that this function doesn't `free' any keys or values. */
void ht_destroy(hashtable_T *ht)
{
    free(ht->indices);
    free(ht->entries);
}


#endif /* YASH_HASHTABLE_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
