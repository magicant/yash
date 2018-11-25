/* Yash: yet another shell */
/* test.h: test builtin */
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


#ifndef YASH_TEST_H
#define YASH_TEST_H


#include <stddef.h>

extern int test_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char test_help[], test_syntax[];
#endif

extern _Bool is_unary_primary(const wchar_t *word)
    __attribute__((nonnull,pure));
extern _Bool is_binary_primary(const wchar_t *word)
    __attribute__((nonnull,pure));

#if YASH_ENABLE_DOUBLE_BRACKET
struct command_T;
extern int exec_double_bracket(const struct command_T *c)
    __attribute__((nonnull));
#endif


#endif /* YASH_TEST_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
