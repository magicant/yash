/* Yash: yet another shell */
/* complete.h: command line completion */
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


#ifndef YASH_COMPLETE_H
#define YASH_COMPLETE_H

#include <stddef.h>


typedef struct le_comppage_T {
    struct le_comppage_T *prev, *next;
    struct le_compcol_T *firstcol;
} le_comppage_T;
typedef struct le_compcol_T {
    struct le_compcol_T *prev, *next;
    struct le_compcand_T *firstcand;
    int width;  /* max width of the candidates */
} le_compcol_T;
typedef struct le_compcand_T {
    struct le_compcand_T *prev, *next;
    char *value;
    int width;
} le_compcand_T;

extern le_comppage_T *le_comppages;
extern size_t le_comppagecount, le_compcandcount;
extern struct le_compcur_T {
    le_comppage_T *page;
    le_compcol_T  *col;
    le_compcand_T *cand;
    size_t pageno, candno;  /* page/candidate number */
} le_compcur;

extern void le_complete(void);

extern void le_free_comppages(le_comppage_T *pages, _Bool free_candidates);
extern void le_free_compcols(le_compcol_T *cols, _Bool free_candidates);
extern void le_free_compcands(le_compcand_T *cands);


#endif /* YASH_COMPLETE_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
