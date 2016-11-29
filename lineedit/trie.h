/* Yash: yet another shell */
/* trie.h: trie library for lineedit */
/* (C) 2007-2016 magicant */

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
#include "key.h"


typedef union trievalue_T {
    const wchar_t *keyseq;
    le_command_func_T *cmdfunc;
    double probability;
} trievalue_T;
typedef struct trienode_T trie_T;
typedef struct trieget_T {
    enum {
	TG_NOMATCH     = 0,
	TG_EXACTMATCH  = 1 << 0,
	TG_PREFIXMATCH = 1 << 1,
	TG_AMBIGUOUS   = TG_EXACTMATCH | TG_PREFIXMATCH,
    } type;
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
extern int trie_foreachw(const trie_T *t,
	int (*func)(void *v, const wchar_t *key, le_command_func_T *cmd),
	void *v)
    __attribute__((nonnull(1,2)));
extern void trie_destroy(trie_T *t);

extern trie_T *trie_add_probability(trie_T *t, const wchar_t *keywcs, double p)
    __attribute__((nonnull,malloc,warn_unused_result));
extern wchar_t *trie_probable_key(const trie_T *t, const wchar_t *skipkey)
    __attribute__((nonnull,malloc,warn_unused_result));


#endif /* YASH_TRIE_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
