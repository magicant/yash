/* Yash: yet another shell */
/* builtin.c: built-in commands */
/* (C) 2007-2022 magicant */

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
#include "builtin.h"
#include <assert.h>
#include <errno.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alias.h"
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
# define DEFBUILTIN(name,func,type,help,syntax,options) \
    do {                                                                      \
	static const builtin_T bi = { func, type, help, syntax, options, };   \
	ht_set(&builtins, name, &bi);                                         \
    } while (0)
#else
# define DEFBUILTIN(name,func,type,help,syntax,options) \
    do {                                                                      \
	static const builtin_T bi = { func, type, };                          \
	ht_set(&builtins, name, &bi);                                         \
    } while (0)
#endif

    /* defined in "builtin.c" */
    DEFBUILTIN(":", true_builtin, BI_SPECIAL, colon_help, colon_syntax, NULL);
    DEFBUILTIN("true", true_builtin, BI_MANDATORY, true_help, true_syntax,
	    NULL);
    DEFBUILTIN("false", false_builtin, BI_MANDATORY, false_help, false_syntax,
	    NULL);
#if YASH_ENABLE_HELP
    DEFBUILTIN("help", help_builtin, BI_ELECTIVE, help_help, help_syntax,
	    help_option);
#endif

    /* defined in "option.c" */
    DEFBUILTIN("set", set_builtin, BI_SPECIAL, set_help, set_syntax, NULL);

    /* defined in "path.c" */
    DEFBUILTIN("cd", cd_builtin, BI_MANDATORY, cd_help, cd_syntax,
	    cd_options);
    DEFBUILTIN("pwd", pwd_builtin, BI_MANDATORY, pwd_help, pwd_syntax,
	    pwd_options);
    DEFBUILTIN("hash", hash_builtin, BI_MANDATORY, hash_help, hash_syntax,
	    hash_options);
    DEFBUILTIN("umask", umask_builtin, BI_MANDATORY, umask_help, umask_syntax,
	    umask_options);

    /* defined in "alias.c" */
    DEFBUILTIN("alias", alias_builtin, BI_MANDATORY, alias_help, alias_syntax,
	    alias_options);
    DEFBUILTIN("unalias", unalias_builtin, BI_MANDATORY, unalias_help,
	    unalias_syntax, all_help_options);

    /* defined in "variable.c" */
    DEFBUILTIN("typeset", typeset_builtin, BI_ELECTIVE, typeset_help,
	    typeset_syntax, typeset_options);
    DEFBUILTIN("export", typeset_builtin, BI_SPECIAL, export_help,
	    export_syntax, typeset_options);
    DEFBUILTIN("local", typeset_builtin, BI_ELECTIVE, local_help,
	    local_syntax, local_options);
    DEFBUILTIN("readonly", typeset_builtin, BI_SPECIAL, readonly_help,
	    readonly_syntax, typeset_options);
#if YASH_ENABLE_ARRAY
    DEFBUILTIN("array", array_builtin, BI_EXTENSION, array_help, array_syntax,
	    array_options);
#endif
    DEFBUILTIN("unset", unset_builtin, BI_SPECIAL, unset_help, unset_syntax,
	    unset_options);
    DEFBUILTIN("shift", shift_builtin, BI_SPECIAL, shift_help, shift_syntax,
	    shift_options);
    DEFBUILTIN("getopts", getopts_builtin, BI_MANDATORY, getopts_help,
	    getopts_syntax, help_option);
    DEFBUILTIN("read", read_builtin, BI_MANDATORY, read_help, read_syntax,
	    read_options);
#if YASH_ENABLE_DIRSTACK
    DEFBUILTIN("pushd", pushd_builtin, BI_ELECTIVE, pushd_help, pushd_syntax,
	    pushd_options);
    DEFBUILTIN("popd", popd_builtin, BI_ELECTIVE, popd_help, popd_syntax,
	    help_option);
    DEFBUILTIN("dirs", dirs_builtin, BI_ELECTIVE, dirs_help, dirs_syntax,
	    dirs_options);
#endif

    /* defined in "sig.c" */
    DEFBUILTIN("trap", trap_builtin, BI_SPECIAL, trap_help, trap_syntax,
	    trap_options);
    DEFBUILTIN("kill", kill_builtin, BI_MANDATORY, kill_help, kill_syntax,
	    NULL);

    /* defined in "job.c" */
    DEFBUILTIN("jobs", jobs_builtin, BI_MANDATORY, jobs_help, jobs_syntax,
	    jobs_options);
    DEFBUILTIN("fg", fg_builtin, BI_MANDATORY, fg_help, fg_syntax,
	    help_option);
    DEFBUILTIN("bg", fg_builtin, BI_MANDATORY, bg_help, bg_syntax,
	    help_option);
    DEFBUILTIN("wait", wait_builtin, BI_MANDATORY, wait_help, wait_syntax,
	    help_option);
    DEFBUILTIN("disown", disown_builtin, BI_ELECTIVE, disown_help,
	    disown_syntax, all_help_options);

    /* defined in "history.c" */
#if YASH_ENABLE_HISTORY
    DEFBUILTIN("fc", fc_builtin, BI_MANDATORY, fc_help, fc_syntax,
	    fc_options);
    DEFBUILTIN("history", history_builtin, BI_ELECTIVE, history_help,
	    history_syntax, history_options);
#endif

    /* defined in "exec.c" */
    DEFBUILTIN("return", return_builtin, BI_SPECIAL, return_help, return_syntax,
	    return_options);
    DEFBUILTIN("break", break_builtin, BI_SPECIAL, break_help, break_syntax,
	    iter_options);
    DEFBUILTIN("continue", break_builtin, BI_SPECIAL, continue_help,
	    continue_syntax, iter_options);
    DEFBUILTIN("eval", eval_builtin, BI_SPECIAL, eval_help, eval_syntax,
	    iter_options);
    DEFBUILTIN(".", dot_builtin, BI_SPECIAL, dot_help, dot_syntax, dot_options);
    DEFBUILTIN("exec", exec_builtin, BI_SPECIAL, exec_help, exec_syntax,
	    exec_options);
    DEFBUILTIN("command", command_builtin, BI_MANDATORY, command_help,
	    command_syntax, command_options);
    DEFBUILTIN("type", command_builtin, BI_MANDATORY, type_help, type_syntax,
	    command_options);
    DEFBUILTIN("times", times_builtin, BI_SPECIAL, times_help, times_syntax,
	    help_option);

    /* defined in "yash.c" */
    DEFBUILTIN("exit", exit_builtin, BI_SPECIAL, exit_help, exit_syntax,
	    force_help_options);
    DEFBUILTIN("suspend", suspend_builtin, BI_ELECTIVE, suspend_help,
	    suspend_syntax, force_help_options);

    /* defined in "builtins/ulimit.c" */
#if YASH_ENABLE_ULIMIT
    DEFBUILTIN("ulimit", ulimit_builtin, BI_MANDATORY, ulimit_help,
	    ulimit_syntax, ulimit_options);
#endif

    /* defined in "builtins/printf.c" */
#if YASH_ENABLE_PRINTF
    DEFBUILTIN("echo", echo_builtin, BI_SUBSTITUTIVE, echo_help, echo_syntax,
	    NULL);
    DEFBUILTIN("printf", printf_builtin, BI_SUBSTITUTIVE, printf_help,
	    printf_syntax, help_option);
#endif

    /* defined in "builtins/test.c" */
#if YASH_ENABLE_TEST
    DEFBUILTIN("test", test_builtin, BI_SUBSTITUTIVE, test_help, test_syntax,
	    NULL);
    DEFBUILTIN("[", test_builtin, BI_SUBSTITUTIVE, test_help, test_syntax,
	    NULL);
#endif

    /* defined in "lineedit/complete.c" */
#if YASH_ENABLE_LINEEDIT
    DEFBUILTIN("complete", complete_builtin, BI_ELECTIVE, complete_help,
	    complete_syntax, complete_options);
#endif

    /* defined in "lineedit/keymap.c" */
#if YASH_ENABLE_LINEEDIT
    DEFBUILTIN("bindkey", bindkey_builtin, BI_ELECTIVE, bindkey_help,
	    bindkey_syntax, bindkey_options);
#endif

#undef DEFBUILTIN
}

/* Returns the built-in command of the specified name or NULL if not found. */
const builtin_T *get_builtin(const char *name)
{
    return ht_get(&builtins, name).value;
}

/* Prints the following error message and returns Exit_ERROR:
 * "the -X option cannot be used with the -Y option",
 * where X and Y are `opt1' and `opt2', respectively. */
int mutually_exclusive_option_error(wchar_t opt1, wchar_t opt2)
{
    xerror(0, Ngt("the -%lc option cannot be used with the -%lc option"),
	    (wint_t) opt1, (wint_t) opt2);
    return Exit_ERROR;
}

/* Checks if the number of operands is in an acceptable range.
 * `max' must not be less than `min'.
 * If `min <= count <= max', returns true.
 * Otherwise, prints an error message and returns false. */
bool validate_operand_count(size_t count, size_t min, size_t max)
{
    assert(min <= max);
    if (count < min) {
	insufficient_operands_error(min);
	return false;
    } else if (count > max) {
	too_many_operands_error(max);
	return false;
    }
    return true;
}

/* Prints the "this command requires an operand" error message and returns
 * Exit_ERROR. */
int insufficient_operands_error(size_t min_required_operand_count)
{
    xerror(0, ngt("this command requires an operand",
		"this command requires %zu operands",
		min_required_operand_count),
	    min_required_operand_count);
    return Exit_ERROR;
}

/* Prints the "too many operands" error message and returns Exit_ERROR. */
int too_many_operands_error(size_t max_accepted_operand_count)
{
    if (max_accepted_operand_count == 0)
	/* TRANSLATORS: This message is printed when a command that takes no
	 * operand was invoked with some operands. */
	xerror(0, Ngt("no operand is expected"));
    else
	/* TRANSLATORS: This message is printed when a command was invoked with
	 * the wrong number of operands. */
	xerror(0, Ngt("too many operands are specified"));
    return Exit_ERROR;
}

/* This function is called when an error occurred while executing a special
 * built-in. If `posixly_correct' and `special_builtin_executed' are true and
 * `is_interactive_now' is false, `exit_shell_with_status' is called with
 * `exitstatus'. Otherwise, this function just returns `exitstatus'. */
/* Even though this function is called only while executing a special built-in,
 * checking `special_builtin_executed' is necessary because
 * `exit_shell_with_status' should not be called if the special built-in is
 * being executed indirectly by a non-special built-in. */
int special_builtin_error(int exitstatus)
{
    if (posixly_correct && special_builtin_executed && !is_interactive_now)
	exit_shell_with_status(exitstatus);
    return exitstatus;
}

#if YASH_ENABLE_HELP

static int print_builtin_helps(void *const *builtin_names)
    __attribute__((nonnull));
static int print_builtin_help_body(const wchar_t *name)
    __attribute__((nonnull));
static bool print_builtin_options(const struct xgetopt_T *options);
static bool there_is_any_short_option(const struct xgetopt_T *options)
    __attribute__((nonnull,pure));
static void format_option_list_entry(
	const struct xgetopt_T *restrict opt, xstrbuf_T *restrict buf,
	bool print_short_option)
    __attribute__((nonnull));

/* Prints description of the specified built-in to the standard output.
 * Returns Exit_SUCCESS if the built-in was found and the help was printed.
 * Returns Exit_FAILURE if the built-in was not found or an error occurred. */
int print_builtin_help(const wchar_t *name)
{
    return print_builtin_helps((void *[]) { (void *) name, NULL, });
}

/* Prints description of built-ins to the standard output.
 * `builtin_names' must point to a NULL-terminated array of pointers to wide
 * strings specifying the names of built-ins.
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

    if (!xprintf("%ls: %s\n\n", name, gt(bi->help_text)))
	return Exit_FAILURE;

    /* TRANSLATORS: This is printed before syntax info of a built-in. */
    if (!xprintf(gt("Syntax:\n%s\n"), gt(bi->syntax_text)))
	return Exit_FAILURE;

    if (wcscmp(name, L"set") == 0) {
	if (!print_shopts(false))
	    return Exit_FAILURE;
    } else {
	if (!print_builtin_options(bi->options))
	    return Exit_FAILURE;
    }

    return Exit_SUCCESS;
}

/* Prints a list of all shell options to the standard output.
 * Returns true iff successful. */
bool print_shopts(bool include_normal_options)
{
    /* TRANSLATORS: This text is printed before a list of options. */
    if (!xprintf(gt("Options:\n")))
	return false;

    if (!print_shopts_body(include_normal_options))
	return false;

    if (!xprintf("\n"))
	return false;

    return true;
}

/* Prints a list of options for a built-in to the standard output.
 * Returns true iff successful. */
bool print_builtin_options(const struct xgetopt_T *options)
{
    if (options == NULL || options[0].shortopt == L'\0')
	return true;

    /* TRANSLATORS: This text is printed before a list of options. */
    if (!xprintf(gt("Options:\n")))
	return false;

    if (!print_option_list(options))
	return false;

    if (!xprintf("\n"))
	return false;

    return true;
}

/* Prints a list of options to the standard output.
 * Returns true iff successful. */
bool print_option_list(const struct xgetopt_T *options)
{
    bool print_short_option = there_is_any_short_option(options);

    xstrbuf_T line;
    sb_initwithmax(&line, 127);

    for (const struct xgetopt_T *opt = options; opt->shortopt != L'\0'; opt++) {
	if (posixly_correct && !opt->posix)
	    continue;

	sb_clear(&line);
	format_option_list_entry(opt, &line, print_short_option);

	if (!xprintf("%s\n", line.contents)) {
	    sb_destroy(&line);
	    return false;
	}
    }

    sb_destroy(&line);
    return true;
}

bool there_is_any_short_option(const struct xgetopt_T *options)
{
    for (const struct xgetopt_T *opt = options; opt->shortopt != L'\0'; opt++)
	if (opt->shortopt != L'-')
	    return true;
    return false;
}

/* Formats a line that describes the specified option.
 * The results are appended to buffer `buf'. `buf' must have been initialized
 * before calling this function. `buf' must be destroyed by the caller.
 * If `print_short_option' is false, the single-character option is omitted. */
void format_option_list_entry(
	const struct xgetopt_T *restrict opt, xstrbuf_T *restrict buf,
	bool print_short_option)
{
    sb_ccat(buf, '\t');

    if (print_short_option && opt->shortopt != L'-') {
	const char *INIT(optargmark, NULL);
	switch (opt->optarg) {
	    case OPTARG_NONE:      optargmark = "";       break;
	    case OPTARG_REQUIRED:  optargmark = " ...";   break;
	    case OPTARG_OPTIONAL:  optargmark = "[...]";  break;
	}

	sb_printf(buf, "-%lc%s", opt->shortopt, optargmark);
    }

    if (opt->longopt != NULL) {
	if (print_short_option && buf->length < 10)
	    sb_ccat_repeat(buf, ' ', 10 - buf->length);

	const char *INIT(optargmark, NULL);
	switch (opt->optarg) {
	    case OPTARG_NONE:      optargmark = "";        break;
	    case OPTARG_REQUIRED:  optargmark = "=...";    break;
	    case OPTARG_OPTIONAL:  optargmark = "[=...]";  break;
	}

	sb_printf(buf, "--%ls%s", opt->longopt, optargmark);
    }
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
	    case BI_SPECIAL:       type = CGT_SBUILTIN;  break;
	    case BI_MANDATORY:     type = CGT_MBUILTIN;  break;
	    case BI_ELECTIVE:      type = CGT_LBUILTIN;  break;
	    case BI_EXTENSION:     type = CGT_XBUILTIN;  break;
	    case BI_SUBSTITUTIVE:  type = CGT_UBUILTIN;  break;
	    default:               assert(false);
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
"\thelp [built-in...]\n"
);

#endif /* YASH_ENABLE_HELP */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
