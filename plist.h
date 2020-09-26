/* Yash: yet another shell */
/* plist.h: modifiable list of pointers */
/* (C) 2007-2020 magicant */

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


#ifndef YASH_PLIST_H
#define YASH_PLIST_H

#include <stdlib.h>

#define Size_max ((size_t) -1)  // = SIZE_MAX


extern size_t plcount(void *const *list)
    __attribute__((pure,nonnull));
static inline void **pldup(void *const *array, void *copy(const void *p))
    __attribute__((malloc,warn_unused_result,nonnull(2)));
extern void **plndup(
	void *const *array, size_t count, void *copy(const void *p))
    __attribute__((malloc,warn_unused_result,nonnull(3)));
extern void plfree(void **ary, void freer(void *elem));


typedef struct plist_T {
    void **contents;
    size_t length, maxlength;
} plist_T;

static inline plist_T *pl_init(plist_T *list)
    __attribute__((nonnull));
static inline plist_T *pl_initwith(plist_T *list, void **array, size_t length)
    __attribute__((nonnull));
extern plist_T *pl_initwithmax(plist_T *list, size_t max)
    __attribute__((nonnull));
static inline void pl_destroy(plist_T *list)
    __attribute__((nonnull));
static inline void **pl_toary(plist_T *list)
    __attribute__((nonnull));
extern plist_T *pl_setmax(plist_T *list, size_t newmax)
    __attribute__((nonnull));
extern plist_T *pl_ensuremax(plist_T *list, size_t max)
    __attribute__((nonnull));
static inline plist_T *pl_truncate(plist_T *list, size_t newlength)
    __attribute__((nonnull));
extern plist_T *pl_clear(plist_T *list, void freer(void *elem))
    __attribute__((nonnull));
extern plist_T *pl_replace(
	plist_T *restrict list, size_t i, size_t ln,
	void *const *restrict a, size_t an)
    __attribute__((nonnull));
static inline plist_T *pl_ninsert(
	plist_T *restrict list, size_t i, void *const *restrict a, size_t n)
    __attribute__((nonnull));
static inline plist_T *pl_insert(
	plist_T *restrict list, size_t i, void *const *restrict a)
    __attribute__((nonnull));
static inline plist_T *pl_ncat(
	plist_T *restrict list, void *const *restrict a, size_t n)
    __attribute__((nonnull));
static inline plist_T *pl_cat(
	plist_T *restrict list, void *const *restrict a)
    __attribute__((nonnull));
static inline plist_T *pl_remove(plist_T *list, size_t i, size_t n)
    __attribute__((nonnull));
extern plist_T *pl_add(plist_T *list, const void *p)
    __attribute__((nonnull(1)));


#ifndef PLIST_DEFAULT_MAX
#define PLIST_DEFAULT_MAX 7
#endif


/* Clones the specified NULL-terminated array of pointers.
 * Each pointer element is passed to function `copy' and the return value is
 * assigned to a new array element.
 * If `array' is NULL, simply returns NULL. */
/* `xstrdup' and `copyaswcs' are suitable for `copy'. */
void **pldup(void *const *array, void *copy(const void *p))
{
    return plndup(array, Size_max, copy);
}


/* Initializes the specified pointer list as a new empty list. */
plist_T *pl_init(plist_T *list)
{
    return pl_initwithmax(list, PLIST_DEFAULT_MAX);
}

/* Initializes the specified pointer list with the specified array.
 * `array' must be a NULL-terminated array of pointers to void containing
 * exactly `length' elements and must be allocated by malloc.
 * `array' must not be modified or freed after the call to this function. */
plist_T *pl_initwith(plist_T *list, void **array, size_t length)
{
    list->contents = array;
    list->length = list->maxlength = length;
#ifdef assert
    assert(list->contents[list->length] == NULL);
#endif
    return list;
}

/* Frees the specified pointer list.
 * Note that the list elements are not `free'd in this function. */
void pl_destroy(plist_T *list)
{
    free(pl_toary(list));
}

/* Frees the specified pointer list and returns the contents.
 * The caller must `free' the return value and its elements if needed.
 * If all the elements are pointers to byte strings, the return value can be
 * safely cast to (char **). */
void **pl_toary(plist_T *list)
{
    void **a = list->contents;
#ifndef NDEBUG
    list->contents = &a[list->maxlength];
    list->length = list->maxlength = Size_max;
#endif
    return a;
}

/* Shrinks the length of the pointer list to `newlength'.
 * `newlength' must not be larger than the current length.
 * `maxlength' of the list is not changed.
 * It's the caller's responsibility to free the objects pointed by the removed
 * pointers. */
plist_T *pl_truncate(plist_T *list, size_t newlength)
{
#ifdef assert
    assert(newlength <= list->length);
#endif
    list->contents[list->length = newlength] = NULL;
    return list;
}

/* Inserts the first `n' elements of array `a' at offset `i' in pointer list
 * `list'.
 * NULL elements in `a' are not treated specially.
 * If (list->length <= i), the array elements are appended to the list.
 * Array `a' must not be part of `list->contents'. */
plist_T *pl_ninsert(
	plist_T *restrict list, size_t i, void *const *restrict a, size_t n)
{
    return pl_replace(list, i, 0, a, n);
}

/* Inserts the elements of array `a' at offset `i' in pointer list `list'.
 * Array `a' must be terminated by a NULL element, which is not inserted.
 * If (list->length <= i), the array elements are appended to the list.
 * Array `a' must not be a part of `list->contents'. */
plist_T *pl_insert(plist_T *restrict list, size_t i, void *const *restrict a)
{
    return pl_replace(list, i, 0, a, plcount(a));
}

/* Appends the first `n' elements of array `a' to pointer list `list'.
 * NULL elements in `a' are not treated specially.
 * Array `a' must not be a part of `list->contents'. */
plist_T *pl_ncat(plist_T *restrict list, void *const *restrict a, size_t n)
{
    return pl_replace(list, Size_max, 0, a, n);
}

/* Inserts the elements of array `a' to pointer list `list'.
 * Array `a' must be terminated by a NULL element, which is not inserted.
 * Array `a' must not be a part of `list->contents'. */
plist_T *pl_cat(plist_T *restrict list, void *const *restrict a)
{
    return pl_replace(list, Size_max, 0, a, plcount(a));
}

/* Removes the `n' elements at offset `i' in pointer list `list'.
 * It's the caller's responsibility to free the objects pointed by the removed
 * pointers. */
plist_T *pl_remove(plist_T *list, size_t i, size_t n)
{
    return pl_replace(list, i, n, (void *[]) { NULL, }, 0);
}


#undef Size_max

#endif /* YASH_PLIST_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
