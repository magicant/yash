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
#include <stdlib.h>
#include "hashtable.h"
#include "path.h"
#include "builtin.h"


/* Rules about builtin commands:
 * - `argc' passed to a builtin is at least one; The command name is given in
 *   `argv[0]'.
 * - `argv' may be rearranged and the values of the argument strings may be
 *   changed in builtins. However, the argument strings may not be `free'd or
 *   `realloc'ed.
 * - Any non-special builtin may not execute other commands without forking
 *   because variable environments may be corrupted.
 * - Builtins may sleep/wait, but cannot be stopped. */


/* Prototypes of builtins */
static main_T true_builtin, false_builtin;


/* A hashtable from names of builtins (const char *) to builtin info structures
 * (const builtin_T *). */
static hashtable_T builtins;

/* Initializes `builtins' */
void init_builtin(void)
{
	ht_initwithcapacity(&builtins, hashstr, htstrcmp, 5);

	/* defined in "builtin.c" */
	static const builtin_T b_colon = { true_builtin, BI_SPECIAL };
	ht_set(&builtins, ":", &b_colon);
	static const builtin_T b_true = { true_builtin, BI_SEMISPECIAL };
	ht_set(&builtins, "true", &b_true);
	static const builtin_T b_false = { false_builtin, BI_SEMISPECIAL };
	ht_set(&builtins, "false", &b_false);

	/* defined in "path.c" */
	static const builtin_T b_cd = { cd_builtin, BI_SEMISPECIAL };
	ht_set(&builtins, "cd", &b_cd);
}

/* Returns the builtin command of the specified name
 * or NULL if not found. */
const builtin_T *get_builtin(const char *name)
{
	return ht_get(&builtins, name).value;
}

/* Prints usage description of the specified builtin. */
void print_builtin_help(const wchar_t *name)
{
	// TODO print_builtin_help
}


/* ":"/"true" builtin */
int true_builtin(
		int argc __attribute__((unused)), void **argv __attribute__((unused)))
{
	return EXIT_SUCCESS;
}

int false_builtin(
		int argc __attribute__((unused)), void **argv __attribute__((unused)))
{
	return EXIT_FAILURE;
}
