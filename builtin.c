/* Yash: yet another shell */
/* builtin.c: builtin commands */
/* (C) 2007-2010 magicant */

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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
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


/* Rules about builtin commands:
 * - `argc' passed to a builtin is at least one; The command name is given in
 *   `argv[0]'.
 * - `argv' may be rearranged and the values of the argument strings may be
 *   changed in the builtin. However, the argument strings may not be `free'd or
 *   `realloc'ed.
 * - Builtins may sleep or wait, but cannot be stopped. */


/* A hashtable from names of builtins (const char *) to builtin info structures
 * (const builtin_T *). */
static hashtable_T builtins;

/* Initializes `builtins' */
void init_builtin(void)
{
    assert(builtins.capacity == 0);
    ht_initwithcapacity(&builtins, hashstr, htstrcmp, 53);

#if YASH_ENABLE_HELP
# define DEFBUILTIN(name,func,type,help) \
    do {                                                                      \
	static const builtin_T bi = { func, type, help, };                    \
	ht_set(&builtins, name, &bi);                                         \
    } while (0)
#else
# define DEFBUILTIN(name,func,type,help) \
    do {                                                                      \
	static const builtin_T bi = { func, type, };                          \
	ht_set(&builtins, name, &bi);                                         \
    } while (0)
#endif

    /* defined in "builtin.c" */
    DEFBUILTIN(":", true_builtin, BI_SPECIAL, colon_help);
    DEFBUILTIN("true", true_builtin, BI_SEMISPECIAL, true_help);
    DEFBUILTIN("false", false_builtin, BI_SEMISPECIAL, false_help);
#if YASH_ENABLE_HELP
    DEFBUILTIN("help", help_builtin, BI_REGULAR, help_help);
#endif

    /* defined in "option.c" */
    DEFBUILTIN("set", set_builtin, BI_SPECIAL, set_help);

    /* defined in "path.c" */
    DEFBUILTIN("cd", cd_builtin, BI_SEMISPECIAL, cd_help);
    DEFBUILTIN("pwd", pwd_builtin, BI_SEMISPECIAL, pwd_help);
    DEFBUILTIN("hash", hash_builtin, BI_REGULAR, hash_help);
    DEFBUILTIN("umask", umask_builtin, BI_SEMISPECIAL, umask_help);

    /* defined in "alias.c" */
#if YASH_ENABLE_ALIAS
    DEFBUILTIN("alias", alias_builtin, BI_SEMISPECIAL, alias_help);
    DEFBUILTIN("unalias", unalias_builtin, BI_SEMISPECIAL, unalias_help);
#endif

    /* defined in "variable.c" */
    DEFBUILTIN("typeset", typeset_builtin, BI_REGULAR, typeset_help);
    DEFBUILTIN("export", typeset_builtin, BI_SPECIAL, typeset_help);
    DEFBUILTIN("readonly", typeset_builtin, BI_SPECIAL, typeset_help);
#if YASH_ENABLE_ARRAY
    DEFBUILTIN("array", array_builtin, BI_REGULAR, array_help);
#endif
    DEFBUILTIN("unset", unset_builtin, BI_SPECIAL, unset_help);
    DEFBUILTIN("shift", shift_builtin, BI_SPECIAL, shift_help);
    DEFBUILTIN("getopts", getopts_builtin, BI_SEMISPECIAL, getopts_help);
    DEFBUILTIN("read", read_builtin, BI_SEMISPECIAL, read_help);
#if YASH_ENABLE_DIRSTACK
    DEFBUILTIN("pushd", pushd_builtin, BI_REGULAR, pushd_help);
    DEFBUILTIN("popd", popd_builtin, BI_REGULAR, popd_help);
    DEFBUILTIN("dirs", dirs_builtin, BI_REGULAR, dirs_help);
#endif

    /* defined in "sig.c" */
    DEFBUILTIN("trap", trap_builtin, BI_SPECIAL, trap_help);
    DEFBUILTIN("kill", kill_builtin, BI_SEMISPECIAL, kill_help);

    /* defined in "job.c" */
    DEFBUILTIN("jobs", jobs_builtin, BI_SEMISPECIAL, jobs_help);
    DEFBUILTIN("fg", fg_builtin, BI_SEMISPECIAL, fg_help);
    DEFBUILTIN("bg", fg_builtin, BI_SEMISPECIAL, bg_help);
    DEFBUILTIN("wait", wait_builtin, BI_SEMISPECIAL, wait_help);
    DEFBUILTIN("disown", disown_builtin, BI_REGULAR, disown_help);

    /* defined in "history.c" */
#if YASH_ENABLE_HISTORY
    DEFBUILTIN("fc", fc_builtin, BI_SEMISPECIAL, fc_help);
    DEFBUILTIN("history", history_builtin, BI_REGULAR, history_help);
#endif

    /* defined in "exec.c" */
    DEFBUILTIN("return", return_builtin, BI_SPECIAL, return_help);
    DEFBUILTIN("break", break_builtin, BI_SPECIAL, break_help);
    DEFBUILTIN("continue", break_builtin, BI_SPECIAL, continue_help);
    DEFBUILTIN("eval", eval_builtin, BI_SPECIAL, eval_help);
    DEFBUILTIN(".", dot_builtin, BI_SPECIAL, dot_help);
    DEFBUILTIN("exec", exec_builtin, BI_SPECIAL, exec_help);
    DEFBUILTIN("command", command_builtin, BI_SEMISPECIAL, command_help);
    DEFBUILTIN("type", command_builtin, BI_REGULAR, type_help);
    DEFBUILTIN("times", times_builtin, BI_SPECIAL, times_help);

    /* defined in "yash.c" */
    DEFBUILTIN("exit", exit_builtin, BI_SPECIAL, exit_help);
    DEFBUILTIN("suspend", suspend_builtin, BI_REGULAR, suspend_help);

    /* defined in "builtins/ulimit.c" */
#if YASH_ENABLE_ULIMIT
    DEFBUILTIN("ulimit", ulimit_builtin, BI_REGULAR, ulimit_help);
#endif

    /* defined in "builtins/printf.c" */
#if YASH_ENABLE_PRINTF
    DEFBUILTIN("echo", echo_builtin, BI_REGULAR, echo_help);
    DEFBUILTIN("printf", printf_builtin, BI_REGULAR, printf_help);
#endif

    /* defined in "builtins/test.c" */
#if YASH_ENABLE_TEST
    DEFBUILTIN("test", test_builtin, BI_REGULAR, test_help);
    DEFBUILTIN("[", test_builtin, BI_REGULAR, test_help);
#endif

    /* defined in "lineedit/complete.c" */
#if YASH_ENABLE_LINEEDIT
    DEFBUILTIN("complete", complete_builtin, BI_REGULAR, complete_help);
#endif

    /* defined in "lineedit/keymap.c" */
#if YASH_ENABLE_LINEEDIT
    DEFBUILTIN("bindkey", bindkey_builtin, BI_REGULAR, bindkey_help);
#endif

#undef DEFBUILTIN
}

/* Returns the builtin command of the specified name or NULL if not found. */
const builtin_T *get_builtin(const char *name)
{
    return ht_get(&builtins, name).value;
}

#if YASH_ENABLE_HELP

/* Prints description of the specified builtin to stdout.
 * Returns Exit_SUCCESS if the builtin was found and the help was printed.
 * Returns Exit_FAILURE if the builtin was not found or an error occurred. */
int print_builtin_help(const wchar_t *name)
{
    char *mbsname = malloc_wcstombs(name);
    const builtin_T *bi = get_builtin(mbsname);
    free(mbsname);
    if (bi) {
	if (fputs(gt(bi->help), stdout) == EOF) {
	    xerror(errno, Ngt("cannot print help of `%ls'"), name);
	    return Exit_FAILURE;
	}
	return Exit_SUCCESS;
    } else {
	xerror(0, Ngt("%ls: no such builtin"), name);
	return Exit_FAILURE;
    }
}

#endif /* YASH_ENABLE_HELP */

#if YASH_ENABLE_LINEEDIT

/* Generates completion candidates for builtin command names that match the glob
 * pattern in the specified context. The CGT_SBUILTIN, CGT_SSBUILTIN, and
 * CGT_RBUILTIN flags in `type' specify what candidate to generate. The other
 * flags are ignored. */
/* The prototype of this function is declared in "lineedit/complete.h". */
void generate_builtin_candidates(le_candgentype_T type, le_context_T *context)
{
    if (!(type & CGT_BUILTIN))
	return;

    le_compdebug("adding builtins for pattern \"%ls\"", context->pattern);
    if (!le_compile_cpattern(context))
	return;

    size_t i = 0;
    kvpair_T kv;
    while ((kv = ht_next(&builtins, &i)).key != NULL) {
	switch (((const builtin_T *) kv.value)->type) {
	    case BI_SPECIAL:
		if (!(type & CGT_SBUILTIN))
		    continue;
		break;
	    case BI_SEMISPECIAL:
		if (!(type & CGT_SSBUILTIN))
		    continue;
		break;
	    case BI_REGULAR:
		if (!(type & CGT_RBUILTIN))
		    continue;
		break;
	}
	if (xfnm_match(context->cpattern, kv.key) == 0)
	    le_new_candidate(CT_COMMAND, malloc_mbstowcs(kv.key), NULL);
    }
}

#endif /* YASH_ENABLE_LINEEDIT */


/* The ":"/"true" builtin */
int true_builtin(
	int argc __attribute__((unused)), void **argv __attribute__((unused)))
{
    return EXIT_SUCCESS;
}

/* The "false" builtin */
int false_builtin(
	int argc __attribute__((unused)), void **argv __attribute__((unused)))
{
    return EXIT_FAILURE;
}

#if YASH_ENABLE_HELP

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
"Naturally, the opposite of this command is the \"false\" command.\n"
);

const char false_help[] = Ngt(
"false - return false value\n"
"\tfalse\n"
"Does nothing unsuccessfully.\n"
"Any arguments are ignored and the exit status is always non-zero.\n"
"Naturally, the opposite of this command is the \"true\" command.\n"
);


/* The "help" builtin. */
int help_builtin(int argc, void **argv)
{
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"", help_option, NULL))) {
	switch (opt) {
	    case L'-':  print_help:
		return print_builtin_help(ARGV(0));
	    default:
		fprintf(stderr, gt("Usage:  help command...\n"));
		return Exit_ERROR;
	}
    }

    if (xoptind == argc)
	goto print_help;

    while (xoptind < argc)
	print_builtin_help(ARGV(xoptind++));
    return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;
}

const char help_help[] = Ngt(
"help - print usage of builtin\n"
"\thelp command...\n"
"Prints a description of <command>s.\n"
);

#endif /* YASH_ENABLE_HELP */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
