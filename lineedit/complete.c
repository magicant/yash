/* Yash: yet another shell */
/* complete.c: command line completion */
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


#include "../common.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include "../option.h"
#include "complete.h"
#include "display.h"
#include "terminfo.h"


static void compdebug(const char *format, ...)
    __attribute__((nonnull,format(printf,1,2)));

/* The list of the currently active completion candidate pages. */
le_comppage_T *le_comppages;
/* The number of pages/candidates in `le_comppages'. */
size_t le_comppagecount, le_compcandcount;
/* The currently selected completion page/column/candidate. */
struct le_compcur_T le_compcur;


/* Performs command line completion. */
void le_complete(void)
{
    if (shopt_le_compdebug) {
	le_display_clear();
	le_restore_terminal();
	compdebug("completion start");
    }

    //TODO

    if (shopt_le_compdebug) {
	compdebug("completion end");
	le_setupterm(false);
	le_set_terminal();
    }
}

/* Prints the formatted string to the standard error.
 * The string is preceded by "[compdebug] " and followed by a newline. */
void compdebug(const char *format, ...)
{
    if (!shopt_le_compdebug)
	return;

    fputs("[compdebug] ", stderr);

    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);

    fputc('\n', stderr);
}

/* Frees the specified list of completion candidate pages.
 * `pages' must point to the first item of the linked list.
 * If `free_candidates' is true, all the candidates in each page are also
 * freed.
 * The `prev' member of each page is ignored in this function. */
void le_free_comppages(le_comppage_T *pages, bool free_candidates)
{
    while (pages != NULL) {
	le_comppage_T *next = pages->next;
	le_free_compcols(pages->firstcol, free_candidates);
	free(pages);
	pages = next;
    }
}

/* Frees the specified list of completion candidate columns.
 * `cols' must point to the first item of the linked list.
 * If `free_candidates' is true, all the candidates in each column are also
 * freed.
 * The `prev' member of each column is ignored in this function. */
void le_free_compcols(le_compcol_T *cols, bool free_candidates)
{
    while (cols != NULL) {
	le_compcol_T *next = cols->next;
	if (free_candidates)
	    le_free_compcands(cols->firstcand);
	free(cols);
	cols = next;
    }
}

/* Frees the specified list of completion candidates.
 * `cands' must point to the first item of the linked list.
 * The `prev' member of each candidate is ignored in this function. */
void le_free_compcands(le_compcand_T *cands)
{
    while (cands != NULL) {
	le_compcand_T *next = cands->next;
	free(cands->value);
	free(cands);
	cands = next;
    }
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
