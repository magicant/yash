/* Yash: yet another shell */
/* builtin.c: built-in commands */
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


#include "common.h"
#include <assert.h>
#include <errno.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if YASH_ENABLE_ALIAS
# include "alias.h"
#endif
#include "builtin.h"
#include "exec.h"
#include "hashtable.h"
#if YASH_ENABLE_HISTORY
# include "history.h"
#endif
#include "job.h"
#include "option.h"
#include "path.h"
#include "sig.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"
#include "xfnmatch.h"
#include "yash.h"
#if YASH_ENABLE_PRINTF
# include "builtins/printf.h"
#endif
#if YASH_ENABLE_TEST
# include "builtins/test.h"
#endif
#if YASH_ENABLE_ULIMIT
# include "builtins/ulimit.h"
#endif
#if YASH_ENABLE_LINEEDIT
# include "lineedit/complete.h"
# include "lineedit/keymap.h"
#endif


/* Rules about built-in commands:
 * - `argc' passed to a built-in is at least one; The command name is given in
 *   `argv[0]'.
 * - `argv' may be rearranged and the values of the argument strings may be
 *   changed in the built-in. However, the argument strings may not be `free'd
 *   or `realloc'ed.
 * - Built-ins may sleep or wait, but cannot be stopped. */


/* A hashtable from names of built-ins (const char *) to built-in info
 * structures (const builtin_T *). */
static hashtable_T builtins;

/* Initializes `builtins' */
void init_builtin(void)
{
    assert(builtins.capacity == 0);
    ht_initwithcapacity(&builtins, hashstr, htstrcmp, 53);

#if YASH_ENABLE_HELP
# define DEFBUILTIN(name,func,type,help,syntax) \
    do {                                                                      \
	static const builtin_T bi = { func, type, help, syntax, };            \
	ht_set(&builtins, name, &bi);                                         \
    } while (0)
#else
# define DEFBUILTIN(name,func,type,help,syntax) \
    do {                                                                      \
	static const builtin_T bi = { func, type, };                          \
	ht_set(&builtins, name, &bi);                                         \
    } while (0)
#endif

    /* defined in "builtin.c" */
    DEFBUILTIN(":", true_builtin, BI_SPECIAL, colon_help, colon_syntax);
    DEFBUILTIN("true", true_builtin, BI_SEMISPECIAL, true_help, true_syntax);
    DEFBUILTIN("false", false_builtin, BI_SEMISPECIAL, false_help,
	    false_syntax);
#if YASH_ENABLE_HELP
    DEFBUILTIN("help", help_builtin, BI_REGULAR, help_help, help_syntax);
#endif

    /* defined in "option.c" */
    DEFBUILTIN("set", set_builtin, BI_SPECIAL, set_help, set_syntax);

    /* defined in "path.c" */
    DEFBUILTIN("cd", cd_builtin, BI_SEMISPECIAL, cd_help, cd_syntax);
    DEFBUILTIN("pwd", pwd_builtin, BI_SEMISPECIAL, pwd_help, pwd_syntax);
    DEFBUILTIN("hash", hash_builtin, BI_REGULAR, hash_help, hash_syntax);
    DEFBUILTIN("umask", umask_builtin, BI_SEMISPECIAL, umask_help,
	    umask_syntax);

    /* defined in "alias.c" */
#if YASH_ENABLE_ALIAS
    DEFBUILTIN("alias", alias_builtin, BI_SEMISPECIAL, alias_help,
	    alias_syntax);
    DEFBUILTIN("unalias", unalias_builtin, BI_SEMISPECIAL, unalias_help,
	    unalias_syntax);
#endif

    /* defined in "variable.c" */
    DEFBUILTIN("typeset", typeset_builtin, BI_REGULAR, typeset_help,
	    typeset_syntax);
    DEFBUILTIN("export", typeset_builtin, BI_SPECIAL, typeset_help,
	    export_syntax);
    DEFBUILTIN("readonly", typeset_builtin, BI_SPECIAL, typeset_help,
	    readonly_syntax);
#if YASH_ENABLE_ARRAY
    DEFBUILTIN("array", array_builtin, BI_REGULAR, array_help, array_syntax);
#endif
    DEFBUILTIN("unset", unset_builtin, BI_SPECIAL, unset_help, unset_syntax);
    DEFBUILTIN("shift", shift_builtin, BI_SPECIAL, shift_help, shift_syntax);
    DEFBUILTIN("getopts", getopts_builtin, BI_SEMISPECIAL, getopts_help,
	    getopts_syntax);
    DEFBUILTIN("read", read_builtin, BI_SEMISPECIAL, read_help, read_syntax);
#if YASH_ENABLE_DIRSTACK
    DEFBUILTIN("pushd", pushd_builtin, BI_REGULAR, pushd_help, pushd_syntax);
    DEFBUILTIN("popd", popd_builtin, BI_REGULAR, popd_help, popd_syntax);
    DEFBUILTIN("dirs", dirs_builtin, BI_REGULAR, dirs_help, dirs_syntax);
#endif

    /* defined in "sig.c" */
    DEFBUILTIN("trap", trap_builtin, BI_SPECIAL, trap_help, trap_syntax);
    DEFBUILTIN("kill", kill_builtin, BI_SEMISPECIAL, kill_help, kill_syntax);

    /* defined in "job.c" */
    DEFBUILTIN("jobs", jobs_builtin, BI_SEMISPECIAL, jobs_help, jobs_syntax);
    DEFBUILTIN("fg", fg_builtin, BI_SEMISPECIAL, fg_help, fg_syntax);
    DEFBUILTIN("bg", fg_builtin, BI_SEMISPECIAL, bg_help, bg_syntax);
    DEFBUILTIN("wait", wait_builtin, BI_SEMISPECIAL, wait_help, wait_syntax);
    DEFBUILTIN("disown", disown_builtin, BI_REGULAR, disown_help,
	    disown_syntax);

    /* defined in "history.c" */
#if YASH_ENABLE_HISTORY
    DEFBUILTIN("fc", fc_builtin, BI_SEMISPECIAL, fc_help, fc_syntax);
    DEFBUILTIN("history", history_builtin, BI_REGULAR, history_help,
	    history_syntax);
#endif

    /* defined in "exec.c" */
    DEFBUILTIN("return", return_builtin, BI_SPECIAL, return_help,
	    return_syntax);
    DEFBUILTIN("break", break_builtin, BI_SPECIAL, break_help, break_syntax);
    DEFBUILTIN("continue", break_builtin, BI_SPECIAL, continue_help,
	    continue_syntax);
    DEFBUILTIN("eval", eval_builtin, BI_SPECIAL, eval_help, eval_syntax);
    DEFBUILTIN(".", dot_builtin, BI_SPECIAL, dot_help, dot_syntax);
    DEFBUILTIN("exec", exec_builtin, BI_SPECIAL, exec_help, exec_syntax);
    DEFBUILTIN("command", command_builtin, BI_SEMISPECIAL, command_help,
	    command_syntax);
    DEFBUILTIN("type", command_builtin, BI_REGULAR, type_help, type_syntax);
    DEFBUILTIN("times", times_builtin, BI_SPECIAL, times_help, times_syntax);

    /* defined in "yash.c" */
    DEFBUILTIN("exit", exit_builtin, BI_SPECIAL, exit_help, exit_syntax);
    DEFBUILTIN("suspend", suspend_builtin, BI_REGULAR, suspend_help,
	    suspend_syntax);

    /* defined in "builtins/ulimit.c" */
#if YASH_ENABLE_ULIMIT
    DEFBUILTIN("ulimit", ulimit_builtin, BI_REGULAR, ulimit_help,
	    ulimit_syntax);
#endif

    /* defined in "builtins/printf.c" */
#if YASH_ENABLE_PRINTF
    DEFBUILTIN("echo", echo_builtin, BI_REGULAR, echo_help, echo_syntax);
    DEFBUILTIN("printf", printf_builtin, BI_REGULAR, printf_help,
	    printf_syntax);
#endif

    /* defined in "builtins/test.c" */
#if YASH_ENABLE_TEST
    DEFBUILTIN("test", test_builtin, BI_REGULAR, test_help, test_syntax);
    DEFBUILTIN("[", test_builtin, BI_REGULAR, test_help, test_syntax);
#endif

    /* defined in "lineedit/complete.c" */
#if YASH_ENABLE_LINEEDIT
    DEFBUILTIN("complete", complete_builtin, BI_REGULAR, complete_help,
	    complete_syntax);
#endif

    /* defined in "lineedit/keymap.c" */
#if YASH_ENABLE_LINEEDIT
    DEFBUILTIN("bindkey", bindkey_builtin, BI_REGULAR, bindkey_help,
	    bindkey_syntax);
#endif

#undef DEFBUILTIN
}

/* Returns the built-in command of the specified name or NULL if not found. */
const builtin_T *get_builtin(const char *name)
{
    return ht_get(&builtins, name).value;
}

#if YASH_ENABLE_HELP

static int print_builtin_helps(void *const *builtin_names)
    __attribute__((nonnull));
static int print_builtin_help_body(const wchar_t *name)
    __attribute__((nonnull));

/* Prints description of the specified built-in to the standard output.
 * Returns Exit_SUCCESS if the built-in was found and the help was printed.
 * Returns Exit_FAILURE if the built-in was not found or an error occurred. */
int print_builtin_help(const wchar_t *name)
{
    return print_builtin_helps((void *[]) { (void *) name, NULL, });
}

/* Prints description of built-ins to the standard output.
 * `builtin_names' must point to a NULL-terminated array of pointers to
 * multibyte strings specifying the names of built-ins.
 * Returns Exit_SUCCESS if the built-in was found and the help was printed.
 * Returns Exit_FAILURE if the built-in was not found or an error occurred. */
int print_builtin_helps(void *const *builtin_names)
{
    for (; *builtin_names != NULL; builtin_names++)
	if (print_builtin_help_body(*builtin_names) != Exit_SUCCESS)
	    return Exit_FAILURE;

    if (!xprintf(gt("Try `man yash' for details.\n")))
	return Exit_FAILURE;

    return Exit_SUCCESS;
}

/* Prints description of the specified built-in to the standard output.
 * Returns Exit_SUCCESS if the built-in was found and the help was printed.
 * Returns Exit_FAILURE if the built-in was not found or an error occurred. */
int print_builtin_help_body(const wchar_t *name)
{
    char *mbsname = malloc_wcstombs(name);
    const builtin_T *bi = get_builtin(mbsname);
    free(mbsname);
    if (bi == NULL) {
	xerror(0, Ngt("no such built-in `%ls'"), name);
	return Exit_FAILURE;
    }

    if (!xprintf("%ls: %s\n", name, gt(bi->help_text)))
	return Exit_FAILURE;

    /* TRANSLATORS: This is printed before syntax info of a built-in. */
    if (!xprintf(gt("\nSyntax:\n%s\n"), gt(bi->syntax_text)))
	return Exit_FAILURE;

    return Exit_SUCCESS;
}

#endif /* YASH_ENABLE_HELP */

#if YASH_ENABLE_LINEEDIT

/* Generates completion candidates for built-in command names matching the
 * pattern. The CGT_SBUILTIN, CGT_SSBUILTIN, and CGT_RBUILTIN flags in
 * `compopt->type' specify what candidate to generate. The other flags are
 * ignored. */
/* The prototype of this function is declared in "lineedit/complete.h". */
void generate_builtin_candidates(const le_compopt_T *compopt)
{
    if (!(compopt->type & CGT_BUILTIN))
	return;

    le_compdebug("adding built-in command name candidates");
    if (!le_compile_cpatterns(compopt))
	return;

    size_t i = 0;
    kvpair_T kv;
    while ((kv = ht_next(&builtins, &i)).key != NULL) {
	le_candgentype_T type;
	switch (((const builtin_T *) kv.value)->type) {
	    case BI_SPECIAL:      type = CGT_SBUILTIN;   break;
	    case BI_SEMISPECIAL:  type = CGT_SSBUILTIN;  break;
	    case BI_REGULAR:      type = CGT_RBUILTIN;   break;
	    default:          assert(false);
	}
	if (!(compopt->type & type))
	    continue;

	if (le_match_comppatterns(compopt, kv.key))
	    le_new_candidate(CT_COMMAND,
		    malloc_mbstowcs(kv.key), NULL, compopt);
    }
}

#endif /* YASH_ENABLE_LINEEDIT */


/* The ":"/"true" built-in. */
int true_builtin(
	int argc __attribute__((unused)), void **argv __attribute__((unused)))
{
    return EXIT_SUCCESS;
}

/* The "false" built-in. */
int false_builtin(
	int argc __attribute__((unused)), void **argv __attribute__((unused)))
{
    return EXIT_FAILURE;
}

#if YASH_ENABLE_HELP

const char colon_help[] = Ngt(
"do nothing"
);
const char colon_syntax[] = Ngt(
"\t: [...]\n"
);

const char true_help[] = Ngt(
"do nothing successfully"
);
const char true_syntax[] = Ngt(
"\ttrue\n"
);

const char false_help[] = Ngt(
"do nothing unsuccessfully"
);
const char false_syntax[] = Ngt(
"\tfalse\n"
);

/* The "help" built-in. */
int help_builtin(int argc, void **argv)
{
    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, help_option, 0)) != NULL) {
	switch (opt->shortopt) {
	    case L'-':
		goto print_help;
	    default:
		fprintf(stderr, gt("Usage:  help command...\n"));
		return Exit_ERROR;
	}
    }

    if (xoptind == argc)
print_help:
	return print_builtin_help(ARGV(0));

    return print_builtin_helps(&argv[xoptind]);
}

const char help_help[] = Ngt(
"print usage of built-in commands"
);
const char help_syntax[] = Ngt(
"\thelp built-in\n"
);

#endif /* YASH_ENABLE_HELP */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
