/* Yash: yet another shell */
/* builtin.c: builtin commands */
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


#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "option.h"
#include "strbuf.h"
#include "hashtable.h"
#include "path.h"
#include "alias.h"
#include "variable.h"
#include "sig.h"
#include "job.h"
#include "builtin.h"
#include "exec.h"
#include "yash.h"


/* Rules about builtin commands:
 * - `argc' passed to a builtin is at least one; The command name is given in
 *   `argv[0]'.
 * - `argv' may be rearranged and the values of the argument strings may be
 *   changed in builtins. However, the argument strings may not be `free'd or
 *   `realloc'ed.
 * - Any non-special builtin may not execute other commands without calling
 *   `fix_temporary_variables' because variable environments may be corrupted.
 *   A builtin must save `current_builtin_name' when executing other commands
 *   in case it is overridden by another builtin.
 * - Builtins may sleep/wait, but cannot be stopped. */


/* A hashtable from names of builtins (const char *) to builtin info structures
 * (const builtin_T *). */
static hashtable_T builtins;

/* Initializes `builtins' */
void init_builtin(void)
{
    ht_initwithcapacity(&builtins, hashstr, htstrcmp, 71);

#define DEFBUILTIN(strname,func,type,help) \
    do {                                                                      \
	static const builtin_T bi = { func, type, help, };                    \
	ht_set(&builtins, strname, &bi);                                      \
    } while (0)

    /* defined in "builtin.c" */
    DEFBUILTIN(":", true_builtin, BI_SPECIAL, colon_help);
    DEFBUILTIN("true", true_builtin, BI_SEMISPECIAL, true_help);
    DEFBUILTIN("false", false_builtin, BI_SEMISPECIAL, false_help);

    /* defined in "option.c" */
    DEFBUILTIN("set", set_builtin, BI_SPECIAL, set_help);

    /* defined in "path.c" */
    DEFBUILTIN("cd", cd_builtin, BI_SEMISPECIAL, cd_help);
    DEFBUILTIN("pwd", pwd_builtin, BI_SEMISPECIAL, pwd_help);
    DEFBUILTIN("umask", umask_builtin, BI_SEMISPECIAL, umask_help);

    /* defined in "alias.c" */
    DEFBUILTIN("alias", alias_builtin, BI_SEMISPECIAL, alias_help);
    DEFBUILTIN("unalias", unalias_builtin, BI_SEMISPECIAL, unalias_help);

    /* defined in "variable.c" */
    DEFBUILTIN("typeset", typeset_builtin, BI_REGULAR, typeset_help);
    DEFBUILTIN("export", typeset_builtin, BI_SPECIAL, typeset_help);
    DEFBUILTIN("readonly", typeset_builtin, BI_SPECIAL, typeset_help);
    DEFBUILTIN("unset", unset_builtin, BI_SPECIAL, unset_help);
    DEFBUILTIN("shift", shift_builtin, BI_SPECIAL, shift_help);

    /* defined in "sig.c" */
    DEFBUILTIN("trap", trap_builtin, BI_SPECIAL, trap_help);
    DEFBUILTIN("kill", kill_builtin, BI_SEMISPECIAL, kill_help);

    /* defined in "job.c" */
    DEFBUILTIN("jobs", jobs_builtin, BI_SEMISPECIAL, jobs_help);
    DEFBUILTIN("fg", fg_builtin, BI_SEMISPECIAL, fg_help);
    DEFBUILTIN("bg", fg_builtin, BI_SEMISPECIAL, bg_help);
    DEFBUILTIN("wait", wait_builtin, BI_SEMISPECIAL, wait_help);
    DEFBUILTIN("disown", disown_builtin, BI_REGULAR, disown_help);

    /* defined in "exec.c" */
    DEFBUILTIN("return", return_builtin, BI_SPECIAL, return_help);
    DEFBUILTIN("break", break_builtin, BI_SPECIAL, break_help);
    DEFBUILTIN("continue", break_builtin, BI_SPECIAL, continue_help);
    DEFBUILTIN("eval", eval_builtin, BI_SPECIAL, eval_help);
    DEFBUILTIN(".", dot_builtin, BI_SPECIAL, dot_help);
    DEFBUILTIN("exec", exec_builtin, BI_SPECIAL, exec_help);
    DEFBUILTIN("times", times_builtin, BI_SPECIAL, times_help);

    /* defined in "yash.c" */
    DEFBUILTIN("exit", exit_builtin, BI_SPECIAL, exit_help);

#undef DEFBUILTIN
}

/* Returns the builtin command of the specified name
 * or NULL if not found. */
const builtin_T *get_builtin(const char *name)
{
    return ht_get(&builtins, name).value;
}

/* Prints usage description of the specified builtin to stdout. */
void print_builtin_help(const wchar_t *name)
{
    char *mbsname = malloc_wcstombs(name);
    const builtin_T *bi = get_builtin(mbsname);
    free(mbsname);
    if (bi)
	fputs(gt(bi->help), stdout);
}


/* ":"/"true" builtin */
int true_builtin(
	int argc __attribute__((unused)), void **argv __attribute__((unused)))
{
    return EXIT_SUCCESS;
}

/* "false" builtin */
int false_builtin(
	int argc __attribute__((unused)), void **argv __attribute__((unused)))
{
    return EXIT_FAILURE;
}

const char colon_help[] = Ngt(
": - null utility\n"
"\t: [arg...]\n"
"Does nothing. Any arguments are ignored.\n"
"Note that arguments are expanded and redirections are performed as usual.\n"
"This command has the same effect as the \"true\" command, but \":\" is a\n"
"special builtin while \"true\" is a semi-special.\n"
);

const char true_help[] = Ngt(
"true - return true value\n"
"\ttrue\n"
"Does nothing successfully.\n"
"Any arguments are ignored and the exit status is always zero.\n"
"This command has the same effect as the \":\" command, but \":\" is a\n"
"special builtin while \"true\" is a semi-special.\n"
"Naturally the opposite of this command is the \"false\" command.\n"
);

const char false_help[] = Ngt(
"false - return false value\n"
"\tfalse\n"
"Does nothing unsuccessfully.\n"
"Any arguments are ignored and the exit status is always non-zero.\n"
"Naturally the opposite of this command is the \"true\" command.\n"
);


/* vim: set ts=8 sts=4 sw=4 noet: */
