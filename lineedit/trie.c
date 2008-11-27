/* Yash: yet another shell */
/* trie.c: trie library for lineedit */
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


#include "../common.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include "../util.h"
#include "trie.h"


typedef union triekey_T {
    char as_char;
    wchar_t as_wchar;
} triekey_T;
typedef struct trieentry_T {
    triekey_T key;
    struct trienode_T *child;
} trieentry_T;
typedef struct trienode_T {
    bool valuevalid;
    trievalue_T value;
    size_t count;
    struct trieentry_T entries[];
} trienode_T;


#define RAISE_COUNT(count) ((count) | 3)
#define NODE_SIZE(count)   (sizeof(trienode_T) + (count) * sizeof(trieentry_T))

static inline bool isempty(const trienode_T *node)
    __attribute__((nonnull,pure));
static trienode_T *ensure_size(trienode_T *node, size_t count)
    __attribute__((nonnull,malloc,warn_unused_result));
static inline ssize_t search(trienode_T *node, char c)
    __attribute__((nonnull,pure));
static ssize_t binarysearch(trieentry_T *e, size_t count, char key)
    __attribute__((nonnull,pure));
static inline ssize_t searchw(trienode_T *node, wchar_t c)
    __attribute__((nonnull,pure));
static ssize_t binarysearchw(trieentry_T *e, size_t count, wchar_t key)
    __attribute__((nonnull,pure));


/* Creates a new empty trie. */
trienode_T *trie_create(void)
{
    trienode_T *result = xmalloc(NODE_SIZE(RAISE_COUNT(0)));
    result->valuevalid = false;
    result->count = 0;
    return result;
}

/* Checks if the node is empty. */
/* Empty nodes can be freed. */
bool isempty(const trienode_T *node)
{
    return node->count == 0 && !node->valuevalid;
}

/* Reallocates the node to ensure the enough node size for the specified entry
 * count. */
trienode_T *ensure_size(trienode_T *node, size_t count)
{
    count = RAISE_COUNT(count);
    if (RAISE_COUNT(node->count) >= count)
	return node;
    else
	return xrealloc(node, NODE_SIZE(count));
}

/* Search the node for the entry of the specified key.
 * If the entry is found, the index to it is returned.
 * If not found, the logical negation of the index that the not-found entry
 * would have is returned. */
ssize_t search(trienode_T *node, char key)
{
    return binarysearch(node->entries, node->count, key);
}

ssize_t binarysearch(trieentry_T *e, size_t count, char key)
{
    ssize_t offset = 0;

    while (count > 0) {
	size_t i = count / 2;
	char ekey = e[offset + i].key.as_char;

	if (key == ekey)
	    return offset + i;
	else if (key < ekey)
	    count = i;
	else
	    offset += i + 1, count -= i + 1;
    }
    return ~offset;
}

/* Search the node for the entry of the specified key.
 * If the entry is found, the index to it is returned.
 * If not found, the logical negation of the index that the not-found entry
 * would have is returned. */
ssize_t searchw(trienode_T *node, wchar_t key)
{
    return binarysearchw(node->entries, node->count, key);
}

ssize_t binarysearchw(trieentry_T *e, size_t count, wchar_t key)
{
    ssize_t offset = 0;

    while (count > 0) {
	size_t i = count / 2;
	wchar_t ekey = e[offset + i].key.as_wchar;

	if (key == ekey)
	    return offset + i;
	else if (key < ekey)
	    count = i;
	else
	    offset += i + 1, count -= i + 1;
    }
    return ~offset;
}

/* Adds a mapping from `keystr' to `v'.
 * The existing mapping for `keystr' is lost if any. */
trienode_T *trie_set(trienode_T *node, const char *keystr, trievalue_T v)
{
    if (keystr[0] == '\0') {
	node->valuevalid = true;
	node->value = v;
    } else {
	ssize_t index = search(node, keystr[0]);

	if (index < 0) {
	    index = ~index;
	    assert(0 <= index && (size_t) index <= node->count);
	    node = ensure_size(node, node->count + 1);
	    memmove(node->entries + index + 1, node->entries + index,
		    sizeof(trieentry_T) * (node->count - index));
	    node->entries[index].key.as_char = keystr[0];
	    node->entries[index].child = trie_create();
	    node->count++;
	}
	node->entries[index].child =
	    trie_set(node->entries[index].child, keystr + 1, v);
    }
    return node;
}

/* Adds a mapping from `keywcs' to `v'.
 * The existing mapping for `keywcs' is lost if any. */
trienode_T *trie_setw(trienode_T *node, const wchar_t *keywcs, trievalue_T v)
{
    if (keywcs[0] == L'\0') {
	node->valuevalid = true;
	node->value = v;
    } else {
	ssize_t index = searchw(node, keywcs[0]);

	if (index < 0) {
	    index = ~index;
	    assert(0 <= index && (size_t) index <= node->count);
	    node = ensure_size(node, node->count + 1);
	    memmove(node->entries + index + 1, node->entries + index,
		    sizeof(trieentry_T) * (node->count - index));
	    node->entries[index].key.as_wchar = keywcs[0];
	    node->entries[index].child = trie_create();
	    node->count++;
	}
	node->entries[index].child =
	    trie_setw(node->entries[index].child, keywcs + 1, v);
    }
    return node;
}

/* Removes the mapping for the specified key. */
trienode_T *trie_remove(trienode_T *node, const char *keystr)
{
    if (keystr[0] == '\0') {
	node->valuevalid = false;
    } else {
	ssize_t index = search(node, keystr[0]);

	if (index >= 0) {
	    node->entries[index].child =
		trie_remove(node->entries[index].child, keystr + 1);
	    if (isempty(node->entries[index].child)) {
		free(node->entries[index].child);
		memmove(node->entries + index, node->entries + index + 1,
			sizeof(trieentry_T) * (node->count - index - 1));
		node->count--;
		node = ensure_size(node, node->count);
	    }
	}
    }
    return node;
}

/* Removes the mapping for the specified key. */
trienode_T *trie_removew(trienode_T *node, const wchar_t *keywcs)
{
    if (keywcs[0] == L'\0') {
	node->valuevalid = false;
    } else {
	ssize_t index = searchw(node, keywcs[0]);

	if (index >= 0) {
	    node->entries[index].child =
		trie_removew(node->entries[index].child, keywcs + 1);
	    if (isempty(node->entries[index].child)) {
		free(node->entries[index].child);
		memmove(node->entries + index, node->entries + index + 1,
			sizeof(trieentry_T) * (node->count - index - 1));
		node->count--;
		node = ensure_size(node, node->count);
	    }
	}
    }
    return node;
}

/* Matches `keystr' with the entries of the trie.
 * `value' of the return value is valid only if `type' is TG_UNIQUE. */
trieget_T trie_get(trienode_T *t, const char *keystr)
{
    trieget_T result = { .type = TG_NOMATCH, .matchlength = 0, };

    if (keystr[0] == '\0') {
	if (t->count > 0)
	    result.type = TG_AMBIGUOUS;
	else if (t->valuevalid)
	    result.type = TG_UNIQUE, result.value = t->value;
	return result;
    }

    ssize_t index = search(t, keystr[0]);
    if (index <= 0) {
	if (t->valuevalid)
	    result.type = TG_UNIQUE, result.value = t->value;
	return result;
    }

    result = trie_get(t->entries[index].child, keystr + 1);
    switch (result.type) {
	case TG_NOMATCH:
	    if (t->valuevalid) {
		result.type = TG_UNIQUE;
		result.matchlength = 0;
		result.value = t->value;
	    }
	    break;
	case TG_UNIQUE:
	case TG_AMBIGUOUS:
	    result.matchlength++;
	    break;
	default:
	    assert(false);
    }
    return result;
}

/* Matches `keywcs' with the entries of the trie.
 * `value' of the return value is valid only if `type' is TG_UNIQUE. */
trieget_T trie_getw(trienode_T *t, const wchar_t *keywcs)
{
    trieget_T result = { .type = TG_NOMATCH, .matchlength = 0, };

    if (keywcs[0] == L'\0') {
	if (t->count > 0)
	    result.type = TG_AMBIGUOUS;
	else if (t->valuevalid)
	    result.type = TG_UNIQUE, result.value = t->value;
	return result;
    }

    ssize_t index = searchw(t, keywcs[0]);
    if (index <= 0) {
	if (t->valuevalid)
	    result.type = TG_UNIQUE, result.value = t->value;
	return result;
    }

    result = trie_getw(t->entries[index].child, keywcs + 1);
    switch (result.type) {
	case TG_NOMATCH:
	    if (t->valuevalid) {
		result.type = TG_UNIQUE;
		result.matchlength = 0;
		result.value = t->value;
	    }
	    break;
	case TG_UNIQUE:
	case TG_AMBIGUOUS:
	    result.matchlength++;
	    break;
	default:
	    assert(false);
    }
    return result;
}


/* vim: set ts=8 sts=4 sw=4 noet: */
