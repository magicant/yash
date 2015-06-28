/* Yash: yet another shell */
/* refcount.h: reference counter utility */
/* (C) 2015 magicant */

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


#ifndef YASH_REFCOUNT_H
#define YASH_REFCOUNT_H

#include <stdint.h>
#include "util.h"

#ifdef UINTPTR_MAX
typedef uintptr_t refcount_T;
#else
typedef uintmax_t refcount_T;
#endif

static inline void refcount_increment(refcount_T *r)
	__attribute__((nonnull));
static inline _Bool refcount_decrement(refcount_T *r)
	__attribute__((nonnull));

void refcount_increment(refcount_T *rc)
{
    (*rc)++;
    if (*rc == 0)
	alloc_failed();
}

_Bool refcount_decrement(refcount_T *rc)
{
    (*rc)--;
    return *rc == 0;
}

#endif /* YASH_REFCOUNT_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
