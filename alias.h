/* Yash: yet another shell */
/* alias.h: alias substitution */
/* (C) 2007-2018 magicant */

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


#ifndef YASH_ALIAS_H
#define YASH_ALIAS_H

#include <stddef.h>
#include "xgetopt.h"


struct xwcsbuf_T;
struct aliaslist_T;

typedef enum {
    AF_NONGLOBAL = 1 << 0,
    AF_NOEOF     = 1 << 1,
} substaliasflags_T;

extern void init_alias(void);
extern const wchar_t *get_alias_value(const wchar_t *aliasname)
    __attribute__((nonnull,pure));
extern void destroy_aliaslist(struct aliaslist_T *list);
extern void shift_aliaslist_index(
	struct aliaslist_T *list, size_t i, ptrdiff_t inc);
extern _Bool substitute_alias(
	struct xwcsbuf_T *restrict buf, size_t i,
	struct aliaslist_T **restrict list, substaliasflags_T flags)
    __attribute__((nonnull));
extern _Bool substitute_alias_range(
	struct xwcsbuf_T *restrict buf, size_t i, size_t j,
	struct aliaslist_T **restrict list, substaliasflags_T flags)
    __attribute__((nonnull));
extern _Bool print_alias_if_defined(
	const wchar_t *aliasname, _Bool user_friendly)
    __attribute__((nonnull));

extern int alias_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char alias_help[], alias_syntax[];
#endif
extern const struct xgetopt_T alias_options[];

extern int unalias_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char unalias_help[], unalias_syntax[];
#endif
extern const struct xgetopt_T unalias_options[];


#endif /* YASH_ALIAS_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
