/* Yash: yet another shell */
/* trie.h: trie library for lineedit */
/* (C) 2007-2008 magicant */

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


#ifndef YASH_TRIE_H
#define YASH_TRIE_H

#include <stddef.h>


typedef union trievalue_T {
    const wchar_t *keyseq;
} trievalue_T;
typedef struct trienode_T trie_T;
typedef struct trieget_T {
    enum { TG_NOMATCH, TG_UNIQUE, TG_AMBIGUOUS, } type;
    size_t matchlength;
    trievalue_T value;
} trieget_T;


extern trie_T *trie_create(void)
    __attribute__((malloc,warn_unused_result));
extern trie_T *trie_set(trie_T *t, const char *keystr, trievalue_T v)
    __attribute__((nonnull(1,2),malloc,warn_unused_result));
extern trie_T *trie_set_null(trie_T *t, trievalue_T v)
    __attribute__((nonnull(1),malloc,warn_unused_result));
extern trie_T *trie_setw(trie_T *t, const wchar_t *keywcs, trievalue_T v)
    __attribute__((nonnull(1,2),malloc,warn_unused_result));
extern trie_T *trie_remove(trie_T *t, const char *keystr)
    __attribute__((nonnull(1,2),malloc,warn_unused_result));
extern trie_T *trie_removew(trie_T *t, const wchar_t *keywcs)
    __attribute__((nonnull(1,2),malloc,warn_unused_result));
extern trieget_T trie_get(const trie_T *t, const char *keystr, size_t keylen)
    __attribute__((nonnull));
extern trieget_T trie_getw(const trie_T *t, const wchar_t *keywcs)
    __attribute__((nonnull));
extern void trie_destroy(trie_T *t);


#endif /* YASH_TRIE_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
