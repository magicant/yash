/* Yash: yet another shell */
/* plist.c: modifiable list of pointers */
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
#include "plist.h"
#include "util.h"


/********** Utilities about Pointers **********/

/* Counts the number of the elements in the NULL-terminated pointer array.
 * The NULL element at the end of the array is not counted. */
size_t plcount(void *const *list)
{
    size_t count = 0;
    while (*list++) count++;
    return count;
}

/* Frees a NULL-terminated array of pointers and the element pointers.
 * `freer' is called for each array element and finally the array is `free'd.
 * If `ary' is NULL, this function does nothing. */
void recfree(void **ary, void freer(void *elem))
{
    if (ary) {
	for (void **a = ary; *a; a++) freer(*a);
	free(ary);
    }
}


/********** Pointer List **********/


/* A pointer list is a variable-length array of pointers to 'void'.
 * For any pointer list `list', it is assumed that
 * `list.contents[list.length]' is always NULL.
 * Besides, a pointer list may contain NULL or an pointer to itself
 * as an element of it. */

/* If the type of the return value of the functions below is pointer list,
 * the return value is the argument `list'. */


/* Initializes a pointer list as a new empty list. */
plist_T *pl_initwithmax(plist_T *list, size_t max)
{
    list->contents = xmalloc((max + 1) * sizeof (void *));
    list->contents[0] = NULL;
    list->length = 0;
    list->maxlength = max;
    return list;
}

/* Frees a pointer list, abandoning the contents.
 * Note that the list elements are not `free'd in this function. */
void pl_destroy(plist_T *list)
{
    free(list->contents);
}

/* Frees a pointer list and returns the contents.
 * The caller must `free' the return value and its elements if needed.
 * This function does not change the value of `list->length'. The other members
 * of `*list' are no longer valid after this function.
 * If all the elements are pointers to a byte string, the return value can be
 * safely cast to (char **). */
void **pl_toary(plist_T *list)
{
    if (list->maxlength - list->length > 10)
	pl_setmax(list, list->length);
    return list->contents;
}

/* Changes `list->maxlength'.
 * If `newmax' is less than the current length of the list, the end of
 * the pointer list is truncated. */
plist_T *pl_setmax(plist_T *list, size_t newmax)
{
    list->contents = xrealloc(list->contents, (newmax + 1) * sizeof (void *));
    list->maxlength = newmax;
    list->contents[newmax] = NULL;
    if (newmax < list->length)
	list->length = newmax;
    return list;
}

/* If `list->maxlength' is less than `max', reallocates the list so that
 * `list->maxlength' is no less than `max'. */
inline plist_T *pl_ensuremax(plist_T *list, size_t max)
{
    if (list->maxlength < max) {
	size_t newmax = list->maxlength;
	do
	    newmax = newmax * 2 + 1;
	while (newmax < max);
	return pl_setmax(list, newmax);
    } else {
	return list;
    }
}

/* Clears the contents of a pointer list, preserving its `maxlength'.
 * If `freer' is non-null, `freer' is called for each element in the list. */
plist_T *pl_clear(plist_T *list, void freer(void *elem))
{
    if (freer)
	for (size_t i = 0; i < list->length; i++)
	    freer(list->contents[i]);
    list->contents[list->length = 0] = NULL;
    return list;
}

/* Replaces the contents of a pointer list with the elements of another array.
 * `ln' elements starting at the offset `i' in `list' is removed and
 * the first `an' elements of `a' take place of them.
 * NULL elements are not treated specially.
 * If (list->length < i + ln), all the elements after the offset `i' in the
 * list is replaced. Especially, if (list->length <= i), `a' is appended.
 * `a' must not be a part of `list->contents'. */
plist_T *pl_replace(
	plist_T *restrict list, size_t i, size_t ln,
	void *const *restrict a, size_t an)
{
    if (i > list->length)
	i = list->length;
    if (ln > list->length - i)
	ln = list->length - i;

    size_t newlength = list->length - ln + an;
    pl_ensuremax(list, newlength);
    memmove(list->contents + i + an, list->contents + i + ln,
	    (list->length - (i + ln) + 1) * sizeof (void *));
    memcpy(list->contents + i, a, an * sizeof (void *));
    list->length = newlength;
    return list;
}

/* Appends `p' to the end of the pointer list `list' as a new element.
 * `p' may be NULL or a pointer to the list itself. */
plist_T *pl_add(plist_T *list, void *p)
{
    pl_ensuremax(list, list->length + 1);
    list->contents[list->length++] = p;
    list->contents[list->length] = NULL;
    return list;
}

/* Removes the last element of a pointer list and returns it.
 * The list must not be empty. */
void *pl_pop(plist_T *list)
{
    assert(list->length > 0);
    void *result = list->contents[--list->length];
    list->contents[list->length] = NULL;
    return result;
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
