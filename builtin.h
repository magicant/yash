/* Yash: yet another shell */
/* builtin.h: built-in commands */
/* (C) 2007-2012 magicant */

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
#if YASH_ENABLE_HELP
    const char *help_text, *syntax_text;
    const struct xgetopt_T *options;
#endif
} builtin_T;


extern void init_builtin(void);
extern const builtin_T *get_builtin(const char *name)
    __attribute__((pure));

extern int mutually_exclusive_option_error(wchar_t opt1, wchar_t opt2);
extern bool validate_operand_count(size_t count, size_t min, size_t max);
extern int insufficient_operands_error(size_t min_required_operand_count);
extern int too_many_operands_error(size_t max_accepted_operand_count);
extern int special_builtin_syntax_error(int exitstatus);

extern int print_builtin_help(const wchar_t *name)
    __attribute__((nonnull));
extern bool print_shopts(bool include_normal_options);
extern bool print_option_list(const struct xgetopt_T *options)
    __attribute__((nonnull));

extern int true_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern const char colon_help[], colon_syntax[], true_help[], true_syntax[];

extern int false_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern const char false_help[], false_syntax[];

extern int help_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern const char help_help[], help_syntax[];


#endif /* YASH_BUILTIN_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
