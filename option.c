/* Yash: yet another shell */
/* option.c: option settings */
/* (C) 2007-2023 magicant */

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
#include "option.h"
#include <assert.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "builtin.h"
#include "exec.h"
#include "job.h"
#include "plist.h"
#include "redir.h"
#include "sig.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"


/********** Option Definitions **********/

/* The value of special parameter $0. */
const wchar_t *command_name;

/* If set, the shell behaves strictly as defined in POSIX.
 * Corresponds to the --posixly-correct option. */
bool posixly_correct = false;

/* If set, this shell is a login shell.
 * Corresponds to the --login option. */
bool is_login_shell = false;

/* If set, this shell is interactive.
 * In subshells, `is_interactive_now' is set to false while `is_interactive'
 * remains unchanged.
 * Correspond to the -i/--interactive option. */
bool is_interactive = false, is_interactive_now = false;

/* `shopt_cmdline' is set if the shell is executing a command in the command-
 * line argument to the shell. `shopt_stdin' is set if the shell is reading
 * commands to execute from the standard input.
 * Correspond to the -c and -s options respectively. */
bool shopt_cmdline = false, shopt_stdin = false;

/* If set, the shell performs job control.
 * Corresponds to the -m/--monitor option. */
bool do_job_control = false;
/* If set, the shell immediately notifies on change of job status.
 * Corresponds to the -b/--notify option. */
bool shopt_notify = false;
#if YASH_ENABLE_LINEEDIT
/* If set, the shell immediately notifies on change of job status during line-
 * editing. Ignored if `shopt_notify' is set. Corresponds to the --notifyle
 * option. */
bool shopt_notifyle = false;
#endif
/* If set, a background job is set to the current job when:
 *  - invoked as an asynchronous command
 *  - resumed in the background by the "bg" command
 *  - stopped
 * respectively.
 * Correspond to the --curasync, --curbg, --curstop options. */
bool shopt_curasync = true, shopt_curbg = true, shopt_curstop = true;

/* If set, any variable is exported when assigned.
 * Corresponds to the -a/--allexport option. */
bool shopt_allexport = false;
/* If set, when a function is defined, all the commands in the function
 * are hashed. Corresponds to the -h/--hashondef option. */
bool shopt_hashondef = false;
/* If set, the 'for' loop iteration variable will be made local. */
bool shopt_forlocal = true;

/* If set, when a command returns a non-zero status, the shell exits.
 * Corresponds to the -e/--errexit option. */
bool shopt_errexit = false;
/* Like `shopt_errexit', but instead of exiting the shell, the "return"
 * built-in is executed on a non-zero status. */
bool shopt_errreturn = false;
/* If set, the last non-zero exit status in a pipeline becomes the exit status
 * of the whole pipeline. Otherwise, the last command in the pipeline always
 * defines the exit status of the whole pipeline. Corresponds to the --pipefail
 * option. */
bool shopt_pipefail = false;
/* If set, undefined variables are expanded to an empty string.
 * Corresponds to the +u/--unset option. */
bool shopt_unset = true;
/* If set, commands are actually executed.
 * Corresponds to the +n/--exec option. */
bool shopt_exec = true;
/* If set, the shell doesn't exit when EOF is entered (from a terminal).
 * Corresponds to the --ignoreeof option. */
bool shopt_ignoreeof = false;
/* If set, the shell echos the input to the shell.
 * Corresponds to the -v/--verbose option. */
bool shopt_verbose = false;
/* If set, the shell prints the trace of command execution and assignment.
 * Corresponds to the -x/--xtrace option. */
bool shopt_xtrace = false;

/* If set, the "xtrace" option is not ignored while executing auxiliary
 * commands. */
bool shopt_traceall = true;

#if YASH_ENABLE_HISTORY
/* If set, lines that start with a space are not saved in the history.
 * Corresponds to the --histspace option. */
bool shopt_histspace = false;
#endif
/* Function definition commands are saved in the history only when this option
 * is set.
 * Corresponds to the --log option.
 * THIS OPTION IS NOT SUPPORTED. */
static bool shopt_log = true;

/* If set, the shell performs filename expansion.
 * Corresponds to the +f/--glob option. */
bool shopt_glob = true;
/* If set, filename expansion is case-sensitive.
 * Corresponds to the --caseglob option. */
bool shopt_caseglob = true;
/* If set, in filename expansion, a wildcard character matches a dot at the
 * beginning of a filename.
 * Corresponds to the --dotglob option. */
bool shopt_dotglob = false;
/* If set, in filename expansion, a directory name is expanded with a slash
 * appended.
 * Corresponds to the --markdirs option. */
bool shopt_markdirs = false;
/* If set, extended features in filename expansion are enabled.
 * Corresponds to the --extendedglob option. */
bool shopt_extendedglob = false;
/* If set, a glob pattern is removed from the command line rather than left
 * intact when there are no matches for it.
 * Corresponds to the --nullglob option. */
bool shopt_nullglob = false;

/* If set, brace expansion is enabled.
 * Corresponds to the --braceexpand option. */
bool shopt_braceexpand = false;

/* If set, empty last field is preserved in field splitting.
 * Corresponds to the --emptylastfield option. */
bool shopt_emptylastfield = false;

/* If set, it is allowed to overwrite existing files by redirections.
 * Corresponds to the +C/--clobber option. */
bool shopt_clobber = true;

#if YASH_ENABLE_LINEEDIT
/* When line-editing is disabled, `shopt_lineedit' is SHOPT_NOLINEEDIT.
 * When line-editing is enabled, `shopt_lineedit' is set to another value that
 * specifies the type of key bindings.
 * `shopt_vi' and `shopt_emacs' correspond to the --vi and --emacs options,
 * respectively. `shopt_lineedit' is set according to the values of them. */
enum shopt_lineedit_T shopt_lineedit = SHOPT_NOLINEEDIT;
static bool shopt_vi = false, shopt_emacs = false;
/* `shopt_le_convmeta' defines treatment of the 8th bit of input characters in
 * line-editing. The value of `shopt_le_convmeta' is set according to those of
 * `shopt_le_yesconvmeta' and `shopt_le_noconvmeta', which correspond to the
 * --le-convmeta and --le-noconvmeta options, respectively. */
enum shopt_yesnoauto_T shopt_le_convmeta = SHOPT_AUTO;
static bool shopt_le_yesconvmeta = false, shopt_le_noconvmeta = false;
/* If set, line-editing uses a flash (instead of a bell) to alert the user. */
bool shopt_le_visiblebell = false;
/* If set, a special character sequence is printed when starting line-editing
 * to make sure the prompt starts at the beginning of line. */
bool shopt_le_promptsp = true;
/* If set, the right prompt is always visible on the screen. */
bool shopt_le_alwaysrp = false;
/* If set, auto-suggest the most probable command line as the user enters a
 * command line. */
bool shopt_le_predict = false;
/* If set, auto-suggest also provides suggestions for empty input lines. */
bool shopt_le_predictempty = false;
/* If set, debugging information is printed during command completion. */
bool shopt_le_compdebug = false;
/* If set, the right prompt will trim the extra space left at end for cursor
 * to sit if prompt is too large */
bool shopt_le_trimright = false;
#endif


struct option_T {
    wchar_t shortopt_enable, shortopt_disable;
    const wchar_t *longopt;
    bool *optp;
    bool resettable;
};

/* List of the shell options. */
static const struct option_T shell_options[] = {
    { L'a', 0,    L"allexport",      &shopt_allexport,      true, },
    { 0,    0,    L"braceexpand",    &shopt_braceexpand,    true, },
    { 0,    0,    L"caseglob",       &shopt_caseglob,       true, },
    { 0,    L'C', L"clobber",        &shopt_clobber,        true, },
    { L'c', 0,    L"cmdline",        &shopt_cmdline,        false, },
    { 0,    0,    L"curasync",       &shopt_curasync,       true, },
    { 0,    0,    L"curbg",          &shopt_curbg,          true, },
    { 0,    0,    L"curstop",        &shopt_curstop,        true, },
    { 0,    0,    L"dotglob",        &shopt_dotglob,        true, },
#if YASH_ENABLE_LINEEDIT
    { 0,    0,    L"emacs",          &shopt_emacs,          true, },
#endif
    { 0,    0,    L"emptylastfield", &shopt_emptylastfield, true, },
    { L'e', 0,    L"errexit",        &shopt_errexit,        true, },
    { 0,    0,    L"errreturn",      &shopt_errreturn,      true, },
    { 0,    L'n', L"exec",           &shopt_exec,           true, },
    { 0,    0,    L"extendedglob",   &shopt_extendedglob,   true, },
    { 0,    0,    L"forlocal",       &shopt_forlocal,       true, },
    { 0,    L'f', L"glob",           &shopt_glob,           true, },
    { L'h', 0,    L"hashondef",      &shopt_hashondef,      true, },
#if YASH_ENABLE_HISTORY
    { 0,    0,    L"histspace",      &shopt_histspace,      true, },
#endif
    { 0,    0,    L"ignoreeof",      &shopt_ignoreeof,      true, },
    { L'i', 0,    L"interactive",    &is_interactive,       false, },
#if YASH_ENABLE_LINEEDIT
    { 0,    0,    L"lealwaysrp",     &shopt_le_alwaysrp,    true, },
    { 0,    0,    L"lecompdebug",    &shopt_le_compdebug,   true, },
    { 0,    0,    L"leconvmeta",     &shopt_le_yesconvmeta, true, },
    { 0,    0,    L"lenoconvmeta",   &shopt_le_noconvmeta,  true, },
    { 0,    0,    L"lepredict",      &shopt_le_predict,     true, },
    { 0,    0,    L"lepredictempty", &shopt_le_predictempty,true, },
    { 0,    0,    L"lepromptsp",     &shopt_le_promptsp,    true, },
    { 0,    0,    L"letrimright",    &shopt_le_trimright,   true, },
    { 0,    0,    L"levisiblebell",  &shopt_le_visiblebell, true, },
#endif
    { 0,    0,    L"log",            &shopt_log,            true, },
    { L'l', 0,    L"login",          &is_login_shell,       false, },
    { 0,    0,    L"markdirs",       &shopt_markdirs,       true, },
    { L'm', 0,    L"monitor",        &do_job_control,       true, },
    { L'b', 0,    L"notify",         &shopt_notify,         true, },
#if YASH_ENABLE_LINEEDIT
    { 0,    0,    L"notifyle",       &shopt_notifyle,       true, },
#endif
    { 0,    0,    L"nullglob",       &shopt_nullglob,       true, },
    { 0,    0,    L"pipefail",       &shopt_pipefail,       true, },
    { 0,    0,    L"posixlycorrect", &posixly_correct,      true, },
    { L's', 0,    L"stdin",          &shopt_stdin,          false, },
    { 0,    0,    L"traceall",       &shopt_traceall,       true, },
    { 0,    L'u', L"unset",          &shopt_unset,          true, },
    { L'v', 0,    L"verbose",        &shopt_verbose,        true, },
#if YASH_ENABLE_LINEEDIT
    { 0,    0,    L"vi",             &shopt_vi,             true, },
#endif
    { L'x', 0,    L"xtrace",         &shopt_xtrace,         true, },
    { 0, 0, NULL, NULL, false, },
};

enum normal_option_index_T {
#if YASH_ENABLE_HELP
    NOI_HELP,
#endif
    NOI_VERSION,
    NOI_NOPROFILE,
    NOI_NORCFILE,
    NOI_PROFILE,
    NOI_RCFILE,
    NOI_N,
};

/* List of the normal options.
 * The normal options are the options that can be used on the shell invocation
 * but that are not shell options. */
/* A non-NULL `ptr' member indicates that the option can be used for the "set"
 * built-in. */
static const struct xgetopt_T normal_options[] = {
#if YASH_ENABLE_HELP
    [NOI_HELP]      = { L'-', L"help",      OPTARG_NONE,     false,
			(void *) &normal_options, },
#endif
    [NOI_VERSION]   = { L'V', L"version",   OPTARG_NONE,     false, NULL, },
    [NOI_NOPROFILE] = { L'-', L"noprofile", OPTARG_NONE,     false, NULL, },
    [NOI_NORCFILE]  = { L'-', L"norcfile",  OPTARG_NONE,     false, NULL, },
    [NOI_PROFILE]   = { L'-', L"profile",   OPTARG_REQUIRED, false, NULL, },
    [NOI_RCFILE]    = { L'-', L"rcfile",    OPTARG_REQUIRED, false, NULL, },
    [NOI_N]         = { L'\0', NULL, 0, false, NULL, },
};



/********** Functions **********/

static wchar_t *normalize_option_name(const wchar_t *optname)
    __attribute__((nonnull,malloc,warn_unused_result));
static int parse_short_option(void *const *argv, bool enable,
	struct shell_invocation_T *shell_invocation)
    __attribute__((nonnull(1)));
static int parse_option_character(
	wchar_t opt, bool enable, struct shell_invocation_T *shell_invocation);
static int parse_long_option(void *const *argv, bool enable,
	struct shell_invocation_T *shell_invocation)
    __attribute__((nonnull(1)));
static size_t collect_matching_shell_options(
	const wchar_t *optstr, plist_T *options)
    __attribute__((nonnull));
static void search_shell_options(const wchar_t *optname, plist_T *resultlist)
    __attribute__((nonnull));
static void search_normal_options(const wchar_t *optname, plist_T *resultlist,
	const struct shell_invocation_T *shell_invocation)
    __attribute__((nonnull(1,2)));
static int handle_search_result(plist_T *options, void *const *argv,
	bool enable, size_t shelloptindex, size_t noshelloptindex,
	struct shell_invocation_T *shell_invocation)
    __attribute__((nonnull(1, 2)));
static int set_shell_option(const struct option_T *option, bool enable,
	struct shell_invocation_T *shell_invocation)
    __attribute__((nonnull(1)));
#if YASH_ENABLE_LINEEDIT
static void update_lineedit_option(void);
static void update_le_convmeta_option(void);
#endif
static int set_normal_option(const struct xgetopt_T *opt, const wchar_t *arg,
	struct shell_invocation_T *shell_invocation)
    __attribute__((nonnull(1)));
#if YASH_ENABLE_TEST
static int test_option_unique(const wchar_t *s)
    __attribute__((nonnull,pure));
#endif

/* Parses the specified command line arguments and accordingly sets the shell
 * options and positional parameters.
 * When this function is called from the `main' function of the shell,
 * `shell_invocation' must be a pointer to a structure to which parse result is
 * stored. Otherwise, that is, when called from the set built-in,
 * `shell_invocation' must be NULL.
 * Positional parameters are set only when `shell_invocation' is NULL.
 * `xoptind' is set to the index of the first operand.
 * Returns Exit_SUCCESS if successful. Otherwise, returns Exit_FAILURE or
 * Exit_ERROR after printing an error message. */
int parse_shell_options(int argc, void *const *argv,
	struct shell_invocation_T *shell_invocation)
{
    /* We don't use the xgetopt function because of the non-standard syntax. */
    for (xoptind = 1; xoptind < argc; xoptind++) {
	const wchar_t *arg = ARGV(xoptind);
	bool enable;
	switch (arg[0]) {
	    case L'-':  enable = true;   break;
	    case L'+':  enable = false;  break;
	    default:    goto main;
	}

	if (arg[1] == L'\0') {
	    if (enable && shell_invocation != NULL)  /* ignore first "-" */
		xoptind++;
	    goto main;
	}

	int result;
	if (arg[0] == arg[1]) {
	    if (arg[2] == L'\0') {
		if (enable)  /* `arg' is L"--" */
		    xoptind++;
		goto set_positional_parameters;
	    }

	    result = parse_long_option(argv, enable, shell_invocation);
	} else {
	    result = parse_short_option(argv, enable, shell_invocation);
	}
	if (result != Exit_SUCCESS)
	    return result;
    }

main:
    if (xoptind < argc) {
set_positional_parameters:
	assert(xoptind <= argc);
	if (shell_invocation == NULL)
	    set_positional_parameters(&argv[xoptind]);
    }

    return Exit_SUCCESS;
}

/* Returns a newly malloced string containing the same string as the argument
 * string except that the returned string does not contain non-alphanumeric
 * characters. */
wchar_t *normalize_option_name(const wchar_t *optname)
{
    xwcsbuf_T result;
    wb_initwithmax(&result, wcslen(optname));
    for (const wchar_t *s = optname; *s != L'\0'; s++)
	if (iswalnum(*s))
	    wb_wccat(&result, towlower(*s));
    return wb_towcs(&result);
}

/* Parses the single-character option at `xoptind' in the specified arguments
 * and enables/disables the option.
 * Returns Exit_SUCCESS iff successful. Prints an error message on error. */
int parse_short_option(void *const *argv, bool enable,
	struct shell_invocation_T *shell_invocation)
{
    const wchar_t *optstr = ARGV(xoptind);

    assert(optstr[0] != L'\0');
    for (size_t i = 1; optstr[i] != L'\0'; i++) {
	if (optstr[i] == L'o') {
	    const wchar_t *optname = &optstr[i + 1];
	    if (*optname == L'\0') {
		optname = ARGV(++xoptind);;
		if (optname == NULL) {
		    xerror(0, Ngt("the -%lc option requires an argument"),
			    (wint_t) L'o');
		    return special_builtin_error(Exit_ERROR);
		}
	    }

	    plist_T options;
	    pl_init(&options);

	    size_t shelloptindex =
		collect_matching_shell_options(optname, &options);

	    return handle_search_result(&options, argv, enable,
		    shelloptindex, options.length, shell_invocation);
	} else {
	    int result = parse_option_character(
		    optstr[i], enable, shell_invocation);
	    if (result != Exit_SUCCESS)
		return result;
	}
    }
    return Exit_SUCCESS;
}

/* Parses the specified single-character option and enables/disables the option.
 * Returns Exit_SUCCESS iff successful. Prints an error message on error. */
int parse_option_character(
	wchar_t opt, bool enable, struct shell_invocation_T *shell_invocation)
{
    assert(opt != L'\0');
    for (const struct option_T *o = shell_options; o->longopt != NULL; o++) {
	if (opt == o->shortopt_enable)
	    return set_shell_option(o, enable, shell_invocation);
	else if (opt == o->shortopt_disable)
	    return set_shell_option(o, !enable, shell_invocation);
    }
    if (opt != L'-') {
	const struct xgetopt_T *o;
	for (o = normal_options; o->shortopt != L'\0'; o++) {
	    if (!o->posix && posixly_correct)
		continue;
	    if (opt == o->shortopt &&
		    (shell_invocation != NULL || o->ptr != NULL))
		return set_normal_option(o, NULL, shell_invocation);
	}
    }

    xerror(0, Ngt("`%ls' is not a valid option"), (wchar_t[]) { opt, L'\0' });
    return special_builtin_error(Exit_ERROR);
}

/* Parses the long option at `xoptind' in the specified arguments and
 * enables/disables the option.
 * Returns Exit_SUCCESS iff successful. Prints an error message on error. */
int parse_long_option(void *const *argv, bool enable,
	struct shell_invocation_T *shell_invocation)
{
    const wchar_t *optstr = ARGV(xoptind);
    plist_T options;
    size_t shelloptindex, noshelloptindex;

    pl_init(&options);

    if (posixly_correct) {
	/* No long options are available in the POSIXly correct mode. */
	shelloptindex = noshelloptindex = 0;
    } else {
	shelloptindex = collect_matching_shell_options(optstr, &options);
	noshelloptindex = options.length;

	if (enable) {
	    assert(matchwcsprefix(optstr, L"--"));
	    search_normal_options(&optstr[2], &options, shell_invocation);
	}
    }

    return handle_search_result(&options, argv, enable,
	    shelloptindex, noshelloptindex, shell_invocation);
}

/* Collects shell options that match the specified name.
 * This function adds to `options' pointers to shell options (struct option_T)
 * whose names start with `optname'.
 * If `optname' starts with "no", options whose names start with `optname'
 * without "no" are also added. The index of the first such option is returned.
 */
size_t collect_matching_shell_options(const wchar_t *optname, plist_T *options)
{
    wchar_t *normoptname = normalize_option_name(optname);
    search_shell_options(normoptname, options);

    size_t nooptindex = options->length;
    if (matchwcsprefix(normoptname, L"no"))
	search_shell_options(&normoptname[2], options);

    free(normoptname);
    return nooptindex;
}

/* Adds pointers to shell options (struct option_T) whose names match `optname'
 * to the specified list.
 * If `optname' exactly matches one of the shell option names, `resultlist' is
 * cleared and only a pointer to that option is added. */
void search_shell_options(const wchar_t *optname, plist_T *resultlist)
{
    for (const struct option_T *o = shell_options; o->longopt != NULL; o++) {
	const wchar_t *s = matchwcsprefix(o->longopt, optname);
	if (s != NULL) {
	    if (*s == L'\0') {
		/* exact match! */
		pl_truncate(resultlist, 0);
		pl_add(resultlist, o);
		break;
	    } else {
		/* partial match */
		pl_add(resultlist, o);
	    }
	}
    }
}

/* Adds to `resultlist' pointers to shell options (struct option_T) whose names
 * match `optname'.
 * If `optname' exactly matches one of shell option names, `resultlist' is
 * cleared and only a pointer to that option is added. */
void search_normal_options(const wchar_t *optname, plist_T *resultlist,
	const struct shell_invocation_T *shell_invocation)
{
    size_t namelen = wcscspn(optname, L"=");
    for (const struct xgetopt_T *o = normal_options; o->longopt != NULL; o++) {
	if (!o->posix && posixly_correct)
	    continue;
	if (wcsncmp(o->longopt, optname, namelen) == 0 &&
		(shell_invocation != NULL || o->ptr != NULL)) {
	    if (o->longopt[namelen] == L'\0') {
		/* exact match! */
		pl_truncate(resultlist, 0);
		pl_add(resultlist, o);
		break;
	    } else {
		/* partial match */
		pl_add(resultlist, o);
	    }
	}
    }
}

/* Handles the result of option search.
 * List `options' must contain pointers to options (struct option_T/xgetopt_T)
 * that match the currently parsed option name. List members with indices less
 * than `noshelloptindex' are pointers to shell options (struct option_T) and
 * the others to normal options (struct xgetopt_T). Inverting shell options,
 * which have the "no" prefix in their names, must have indices no less than
 * `shelloptindex'. The list is destroyed in this function. */
int handle_search_result(plist_T *options, void *const *argv, bool enable,
	size_t shelloptindex, size_t noshelloptindex,
	struct shell_invocation_T *shell_invocation)
{
    assert(shelloptindex <= noshelloptindex);
    assert(noshelloptindex <= options->length);

    const wchar_t *optstr = ARGV(xoptind);

    switch (options->length) {
	case 0:
	    pl_destroy(options);
	    xerror(0, Ngt("`%ls' is not a valid option"), optstr);
	    return special_builtin_error(Exit_ERROR);
	case 1:
	    if (noshelloptindex > 0) {
		const struct option_T *opt = options->contents[0];
		pl_destroy(options);
		if (shelloptindex == 0)
		    enable = !enable;
		return set_shell_option(opt, enable, shell_invocation);
	    } else {
		const struct xgetopt_T *opt = options->contents[0];
		const wchar_t *eq = wcschr(optstr, L'=');
		const wchar_t *optarg;
		pl_destroy(options);
		if (opt->optarg != OPTARG_NONE) {
		    if (eq == NULL) {
			switch (opt->optarg) {
			    case OPTARG_OPTIONAL:
				optarg = NULL;
				break;
			    case OPTARG_REQUIRED:
				optarg = ARGV(++xoptind);
				if (optarg == NULL) {
				    xerror(0, Ngt("the --%ls option requires "
						"an argument"), opt->longopt);
				    return special_builtin_error(Exit_ERROR);
				}
				break;
			    default:
				assert(false);
			}
		    } else {
			optarg = &eq[1];
		    }
		} else {
		    if (eq != NULL) {
			xerror(0, Ngt("%ls: the --%ls option does not take "
				    "an argument"), optstr, opt->longopt);
			return special_builtin_error(Exit_ERROR);
		    }
		    optarg = NULL;
		}
		return set_normal_option(opt, optarg, shell_invocation);
	    }
	default:
	    xerror(0, Ngt("option `%ls' is ambiguous"), optstr);
#if LIST_AMBIGUOUS_OPTIONS
	    size_t i = 0;
	    for (; i < shelloptindex; i++)
		fprintf(stderr, "\t--%ls\n",
		    ((const struct option_T *) options->contents[i])->longopt);
	    for (; i < noshelloptindex; i++)
		fprintf(stderr, "\t--no%ls\n",
		    ((const struct option_T *) options->contents[i])->longopt);
	    for (; i < options->length; i++)
		fprintf(stderr, "\t--%ls\n",
		    ((const struct xgetopt_T *) options->contents[i])->longopt);
#endif
	    pl_destroy(options);
	    return special_builtin_error(Exit_ERROR);
    }
}

/* Sets the specified shell option.
 * Returns Exit_SUCCESS iff successful. Print an error message on error. */
int set_shell_option(const struct option_T *option, bool enable,
	struct shell_invocation_T *shell_invocation)
{
    if (shell_invocation == NULL && !option->resettable) {
	xerror(0, Ngt("the %ls option cannot be changed "
		    "once the shell has been initialized"),
		option->longopt);
	return special_builtin_error(Exit_ERROR);
    }

    *option->optp = enable;

    if (shell_invocation != NULL) {
	if (option->optp == &is_interactive)
	    shell_invocation->is_interactive_set = true;
	else if (option->optp == &do_job_control)
	    shell_invocation->do_job_control_set = true;
    }
    if (shell_invocation == NULL && option->optp == &do_job_control) {
	if (do_job_control && ttyfd < 0)
	    open_ttyfd();
	if (do_job_control)
	    ensure_foreground();
	reset_job_signals();
    }
#if YASH_ENABLE_LINEEDIT
    if (option->optp == &shopt_vi) {
	if (enable)
	    shopt_emacs = false;
	update_lineedit_option();
	if (shell_invocation != NULL)
	    shell_invocation->lineedit_set = true;
    } else if (option->optp == &shopt_emacs) {
	if (enable)
	    shopt_vi = false;
	update_lineedit_option();
	if (shell_invocation != NULL)
	    shell_invocation->lineedit_set = true;
    } else if (option->optp == &shopt_le_yesconvmeta) {
	if (enable)
	    shopt_le_noconvmeta = false;
	update_le_convmeta_option();
    } else if (option->optp == &shopt_le_noconvmeta) {
	if (enable)
	    shopt_le_yesconvmeta = false;
	update_le_convmeta_option();
    }
#endif

    return (*option->optp == enable) ?
	    Exit_SUCCESS : special_builtin_error(Exit_FAILURE);
}

#if YASH_ENABLE_LINEEDIT

/* Updates the value of `shopt_lineedit' according to the values of `shopt_vi'
 * and `shopt_emacs'. */
void update_lineedit_option(void)
{
    if (shopt_vi)
	shopt_lineedit = SHOPT_VI;
    else if (shopt_emacs)
	shopt_lineedit = SHOPT_EMACS;
    else
	shopt_lineedit = SHOPT_NOLINEEDIT;
}

/* Updates the value of `shopt_le_convmeta' according to the values of
 * `shopt_le_yesconvmeta' and `shopt_le_noconvmeta'. */
void update_le_convmeta_option(void)
{
    if (shopt_le_yesconvmeta)
	shopt_le_convmeta = SHOPT_YES;
    else if (shopt_le_noconvmeta)
	shopt_le_convmeta = SHOPT_NO;
    else
	shopt_le_convmeta = SHOPT_AUTO;
}

#endif

/* Sets the specified normal option.
 * Returns Exit_SUCCESS iff successful. Print an error message on error. */
int set_normal_option(const struct xgetopt_T *opt, const wchar_t *arg,
	struct shell_invocation_T *shell_invocation)
{
    enum normal_option_index_T index = opt - normal_options;

    if (shell_invocation == NULL) {
	switch (index) {
#if YASH_ENABLE_HELP
	    case NOI_HELP:
		return print_builtin_help(L"set");
#endif
	    default:
		assert(false);
	}
    } else {
	switch (index) {
#if YASH_ENABLE_HELP
	    case NOI_HELP:
		shell_invocation->help = true;
		break;
#endif
	    case NOI_VERSION:
		shell_invocation->version = true;
		break;
	    case NOI_NOPROFILE:
		shell_invocation->noprofile = true;
		break;
	    case NOI_NORCFILE:
		shell_invocation->norcfile = true;
		break;
	    case NOI_PROFILE:
		assert(arg != NULL);
		shell_invocation->profile = arg;
		break;
	    case NOI_RCFILE:
		assert(arg != NULL);
		shell_invocation->rcfile = arg;
		break;
	    case NOI_N:
		assert(false);
	}
    }
    return Exit_SUCCESS;
}

#if YASH_ENABLE_LINEEDIT

/* Sets the line-editing option to the specified value. */
void set_lineedit_option(enum shopt_lineedit_T v)
{
    shopt_vi = shopt_emacs = false;
    shopt_lineedit = v;
    switch (v) {
	case SHOPT_VI:          shopt_vi    = true;  break;
	case SHOPT_EMACS:       shopt_emacs = true;  break;
	case SHOPT_NOLINEEDIT:  break;
    }
}

#endif

#if YASH_ENABLE_TEST

/* Returns true iff the specified string is a valid option name that can be used
 * as the argument to the "-o" option. */
bool is_valid_option_name(const wchar_t *s)
{
    return test_option_unique(s);
}

/* Returns true iff the specified string is a valid option name that can be used
 * as the argument to the "-o" option and the option is enabled. */
bool option_is_enabled(const wchar_t *s)
{
    return test_option_unique(s) == 2;
}

/* Tests if the specified string is a valid option name that can be used
 * as the argument to the "-o" option. Returns:
 * 0 if the string is not a valid option name,
 * 1 if it is a valid option that is currently disabled, or
 * 2 if it is a valid option that is currently enabled. */
int test_option_unique(const wchar_t *s)
{
    plist_T options;
    pl_init(&options);

    size_t nooptindex = collect_matching_shell_options(s, &options);
    int result = (options.length == 1);

    if (result) {
	struct option_T *opt = options.contents[0];
	bool negated = (nooptindex == 0);
	result += *opt->optp ^ negated;
    }

    pl_destroy(&options);
    return result;
}

#endif /* YASH_ENABLE_TEST */

/* Returns the current value of special parameter $- as a newly malloced string.
 */
wchar_t *get_hyphen_parameter(void)
{
    xwcsbuf_T buf;
    wb_init(&buf);

    for (const struct option_T *o = shell_options; o->longopt != NULL; o++) {
	if (o->shortopt_enable != L'\0' && *o->optp)
	    wb_wccat(&buf, o->shortopt_enable);
	if (o->shortopt_disable != L'\0' && !*o->optp)
	    wb_wccat(&buf, o->shortopt_disable);
    }

    return wb_towcs(&buf);
}

#if YASH_ENABLE_HELP

/* Prints a list of all shell options to the standard output.
 * Returns true iff successful. */
bool print_shopts_body(bool include_normal_options)
{
    if (include_normal_options)
	if (!print_option_list(normal_options))
	    return false;

    for (const struct option_T *o = shell_options; o->longopt != NULL; o++) {
	if (!xprintf("\t"))
	    return false;

	if (o->shortopt_enable != L'\0') {
	    if (!xprintf("-%lc", o->shortopt_enable))
		return false;
	} else if (o->shortopt_disable != L'\0') {
	    if (!xprintf("+%lc", o->shortopt_disable))
		return false;
	} else {
	    if (!xprintf("  "))
		return false;
	}

	if (!xprintf("       -o %ls\n", o->longopt))
	    return false;
    }

    return true;
}

#endif /* YASH_ENABLE_HELP */


/********** Built-in **********/

const struct xgetopt_T all_help_options[] = {
    { L'a', L"all",  OPTARG_NONE, true,  NULL, },
#if YASH_ENABLE_HELP
    { L'-', L"help", OPTARG_NONE, false, NULL, },
#endif
    { L'\0', NULL, 0, false, NULL, },
};
/* Note: `help_option' is defined as (&all_help_options[1]). */


static int set_builtin_print_current_settings(void);
static int set_builtin_print_restoring_commands(void);

/* The "set" built-in. */
int set_builtin(int argc, void **argv)
{
    if (argc <= 1) {
	return typeset_builtin(argc, argv);
    } else if (argc == 2) {
	if (wcscmp(ARGV(1), L"-o") == 0)
	    return set_builtin_print_current_settings();
	else if (wcscmp(ARGV(1), L"+o") == 0)
	    return set_builtin_print_restoring_commands();
    }

    return parse_shell_options(argc, argv, NULL);
}

int set_builtin_print_current_settings(void)
{
    const char *vals[] = {
	[true]  = gt("on"),
	[false] = gt("off"),
    };

    for (const struct option_T *o = shell_options; o->longopt != NULL; o++) {
	if (!xprintf("%-15ls %s\n", o->longopt, vals[(bool) *o->optp]))
	    return special_builtin_error(Exit_FAILURE);
    }

    return Exit_SUCCESS;
}

int set_builtin_print_restoring_commands(void)
{
    for (const struct option_T *o = shell_options; o->longopt != NULL; o++) {
	if (o->resettable)
	    if (!xprintf("set %co %ls\n", *o->optp ? '-' : '+', o->longopt))
		return special_builtin_error(Exit_FAILURE);
    }

    return Exit_SUCCESS;
}

#if YASH_ENABLE_HELP
const char set_help[] = Ngt(
"set shell options and positional parameters"
);
const char set_syntax[] = Ngt(
"\tset [option...] [--] [new_positional_parameter...]\n"
"\tset -o|+o  # print current settings\n"
);
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
