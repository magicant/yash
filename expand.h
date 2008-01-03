/* Yash: yet another shell */
/* expand.h: functions for command line expansion */
/* © 2007-2008 magicant */

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


#ifndef EXPAND_H
#define EXPAND_H

#include <stdbool.h>
#include "util.h"


bool expand_line(char *const *args, int *argc, char ***argv)
	__attribute__((nonnull(2,3)));
char *expand_single(const char *arg)
	__attribute__((nonnull));
char *expand_word(const char *s)
	__attribute__((nonnull));
void add_splitting(const char *str, struct strbuf *buf, struct plist *list,
		const char *ifs, const char *q)
	__attribute__((nonnull(1,2,3,4)));
void escape_sq(const char *s, struct strbuf *buf)
	__attribute__((nonnull));
void escape_bs(const char *s, const char *q, struct strbuf *buf)
	__attribute__((nonnull));


#endif /* EXPAND_H */
