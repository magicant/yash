/* Yash: yet another shell */
/* builtin.h: builtin commands */
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


#ifndef YASH_BUILTIN_H
#define YASH_BUILTIN_H

#include <stddef.h>


typedef int main_T(int argc, void **argv)
    __attribute__((nonnull));

typedef enum builtintype_T {
    BI_SPECIAL, BI_SEMISPECIAL, BI_REGULAR,
} builtintype_T;

typedef struct builtin_T {
    main_T *body;
    builtintype_T type;
    const char *help;
} builtin_T;


extern void init_builtin(void);
extern const builtin_T *get_builtin(const char *name)
    __attribute__((pure));
extern void print_builtin_help(const wchar_t *name)
    __attribute__((nonnull));

extern int true_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern int false_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern const char colon_help[], true_help[], false_help[];


#define SPECIAL_BI_ERROR                                                      \
    do {                                                                      \
	if (posixly_correct && !is_interactive_now)                           \
	    exit_shell_with_status(Exit_ERROR);                               \
    } while (0)


#endif /* YASH_BUILTIN_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
