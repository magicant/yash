/* Yash: yet another shell */
/* plist.c: modifiable list of pointers */
/* (C) 2007-2015 magicant */

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
#include "plist.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"


/********** Utilities about Pointer Arrays **********/

/* Counts the number of the elements in the NULL-terminated pointer array.
 * The NULL element at the end of the array is not counted. */
size_t plcount(void *const *list)
{
    size_t count = 0;
    while (list[count] != NULL)
	count++;
    return count;
}

/* Clones the specified NULL-terminated array of pointers.
 * Each pointer element is passed to function `copy' and the return value is
 * assigned to the new array element.
 * If the array contains more than `count' elements, only the first `count'
 * elements are copied. If the array elements are fewer than `count', the whole
 * array is copied.
 * If `array' is NULL, simply returns NULL. */
/* `xstrdup' and `copyaswcs' are suitable for `copy'. */
void **plndup(void *const *array, size_t count, void *copy(const void *p))
{
    if (array == NULL)
	return NULL;

    size_t realcount = 0;
    while (array[realcount] != NULL && realcount < count)
	realcount++;

    void **result = xmalloce(realcount, 1, sizeof *result);
    for (size_t i = 0; i < realcount; i++)
	result[i] = copy(array[i]);
    result[realcount] = NULL;
    return result;
}

/* Frees the NULL-terminated array of pointers and its elements.
 * `freer' is called for each array element and finally the array is `free'd.
 * If `ary' is NULL, this function does nothing. */
void plfree(void **ary, void freer(void *elem))
{
    if (ary != NULL) {
	for (void **a = ary; *a != NULL; a++)
	    freer(*a);
	free(ary);
    }
}


/********** Pointer List **********/


/* A pointer list is a variable-length array of pointers to 'void'.
 * For any pointer list `list', it is assumed that `list.contents[list.length]'
 * is always NULL.
 * Besides, a pointer list may contain NULL or an pointer to itself
 * as an element of it. */

/* Many of the following pointer-list-related functions returns the list that
 * was given as the first argument. */


/* Initializes the pointer list as a new empty list with the specified initial
 * capacity. */
plist_T *pl_initwithmax(plist_T *list, size_t max)
{
    list->contents = xmalloce(max, 1, sizeof (void *));
    list->contents[0] = NULL;
    list->length = 0;
    list->maxlength = max;
    return list;
}

/* Changes the capacity of the specified list.
 * If `newmax' is less than the current length of the list, the end of
 * the pointer list is truncated. */
plist_T *pl_setmax(plist_T *list, size_t newmax)
{
    list->contents = xrealloce(list->contents, newmax, 1, sizeof (void *));
    list->maxlength = newmax;
    list->contents[newmax] = NULL;
    if (newmax < list->length)
	list->length = newmax;
    return list;
}

/* Increases the capacity of the list so that the capacity is no less than the
 * specified. */
plist_T *pl_ensuremax(plist_T *list, size_t max)
{
    if (max <= list->maxlength)
	return list;

    size_t len15 = list->maxlength + (list->maxlength >> 1);
    if (max < len15)
	max = len15;
    if (max < list->maxlength + 6)
	max = list->maxlength + 6;
    return pl_setmax(list, max);
}

/* Clears the contents of the pointer list.
 * `freer' is called for each element in the list. */
plist_T *pl_clear(plist_T *list, void freer(void *elem))
{
    for (size_t i = 0; i < list->length; i++)
	freer(list->contents[i]);
    list->contents[list->length = 0] = NULL;
    return list;
}

/* Replaces the contents of pointer list `list' with the elements of another
 * array `a'.
 * `ln' elements starting at offset `i' in `list' is removed and
 * the first `an' elements of `a' take place of them.
 * NULL elements are not treated specially.
 * If (list->length < i + ln), all the elements after offset `i' in the list is
 * replaced. Especially, if (list->length <= i), `a' is appended.
 * `a' must not be part of `list->contents'. */
plist_T *pl_replace(
	plist_T *restrict list, size_t i, size_t ln,
	void *const *restrict a, size_t an)
{
    if (i > list->length)
	i = list->length;
    if (ln > list->length - i)
	ln = list->length - i;

    size_t newlength = add(list->length - ln, an);
    pl_ensuremax(list, newlength);
    memmove(list->contents + i + an, list->contents + i + ln,
	    (list->length - (i + ln) + 1) * sizeof (void *));
    memcpy(list->contents + i, a, an * sizeof (void *));
    list->length = newlength;
    return list;
}

/* Appends `p' to the end of pointer list `list' as a new element.
 * `p' may be NULL or a pointer to the list itself. */
plist_T *pl_add(plist_T *list, const void *p)
{
    pl_ensuremax(list, add(list->length, 1));
    list->contents[list->length++] = (void *) p;
    list->contents[list->length] = NULL;
    return list;
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
