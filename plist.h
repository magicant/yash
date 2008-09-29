/* Yash: yet another shell */
/* plist.h: modifiable list of pointers */
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


#ifndef YASH_PLIST_H
#define YASH_PLIST_H

#include <stddef.h>

#define Size_max ((size_t) -1)  // = SIZE_MAX


typedef struct plist_T {
    void **contents;
    size_t length, maxlength;
} plist_T;

extern size_t plcount(void *const *list)
    __attribute__((pure,nonnull));
extern void recfree(void **ary, void freer(void *elem));

static inline plist_T *pl_init(plist_T *list)
    __attribute__((nonnull));
extern plist_T *pl_initwithmax(plist_T *list, size_t max)
    __attribute__((nonnull));
extern void pl_destroy(plist_T *list)
    __attribute__((nonnull));
extern void **pl_toary(plist_T *list)
    __attribute__((nonnull));
extern plist_T *pl_setmax(plist_T *list, size_t newmax)
    __attribute__((nonnull));
extern plist_T *pl_ensuremax(plist_T *list, size_t max)
    __attribute__((nonnull));
extern plist_T *pl_clear(plist_T *list)
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
extern plist_T *pl_add(plist_T *list, void *p)
    __attribute__((nonnull(1)));
extern void *pl_pop(plist_T *list)
    __attribute__((nonnull));


#define PLIST_DEFAULT_MAX 7


/* Initializes a pointer list as a new empty list. */
plist_T *pl_init(plist_T *list)
{
    return pl_initwithmax(list, PLIST_DEFAULT_MAX);
}

/* Inserts the first `n' elements of the array `a' at the offset `i'
 * in the pointer list `list'.
 * NULL elements in `a' are not treated specially.
 * If (list->length <= i), the array elements are appended to the list.
 * `a' must not be a part of `list->contents'. */
plist_T *pl_ninsert(
	plist_T *restrict list, size_t i, void *const *restrict a, size_t n)
{
    return pl_replace(list, i, 0, a, n);
}

/* Inserts the elements of the array `a' at the offset `i'
 * in the pointer list `list'.
 * The array `a' must be terminated by a NULL element, which is not inserted.
 * If (list->length <= i), the array elements are appended to the list.
 * `a' must not be a part of `list->contents'. */
plist_T *pl_insert(plist_T *restrict list, size_t i, void *const *restrict a)
{
    return pl_replace(list, i, 0, a, plcount(a));
}

/* Appends the first `n' elements of the array `a' to the pointer list `list'.
 * NULL elements in `a' are not treated specially.
 * `a' must not be a part of `list->contents'. */
plist_T *pl_ncat(plist_T *restrict list, void *const *restrict a, size_t n)
{
    return pl_replace(list, Size_max, 0, a, n);
}

/* Inserts the elements of the array `a' to the pointer list `list'.
 * The array `a' must be terminated by a NULL element, which is not inserted.
 * `a' must not be a part of `list->contents'. */
plist_T *pl_cat(plist_T *restrict list, void *const *restrict a)
{
    return pl_replace(list, Size_max, 0, a, plcount(a));
}

/* Removes the `n' elements at the offset `i' in the pointer list `list'.
 * It's the caller's responsibility to free the objects pointed by the removed
 * pointers. */
plist_T *pl_remove(plist_T *list, size_t i, size_t n)
{
    return pl_replace(list, i, n, (void *[]) { NULL, }, 0);
}


#undef Size_max

#endif /* YASH_PLIST_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
