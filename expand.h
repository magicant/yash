/* Yash: yet another shell */
/* expand.h: word expansion */
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

/* チルダ展開の種類 */
typedef enum { tt_none, tt_single, tt_multi, } tildetype_T;

__attribute__((nonnull))
extern bool expand_line(
	void *const *restrict args,
	int *restrict argcp,
	char ***restrict argvp);
__attribute__((nonnull,malloc,warn_unused_result))
extern wchar_t *escape(const wchar_t *restrict s, const wchar_t *restrict t);


#endif /* EXPAND_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
