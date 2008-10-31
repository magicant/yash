/* Yash: yet another shell */
/* yash.c: basic functions of the shell */
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
#include <assert.h>
#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "alias.h"
#include "builtin.h"
#include "exec.h"
#include "expand.h"
#include "history.h"
#include "input.h"
#include "job.h"
#include "option.h"
#include "parser.h"
#include "path.h"
#include "redir.h"
#include "sig.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"
#include "version.h"
#include "yash.h"


extern int main(int argc, char **argv)
    __attribute__((nonnull));
static void execute_profile(void);
static void execute_rcfile(const wchar_t *rcfile);
static void print_help(void);
static void print_version(void);

/* the process ID of the shell */
pid_t shell_pid;
/* the initial/current process group ID of the shell */
static pid_t initial_pgid;
pid_t shell_pgid;

/* an equivalent to the -f flag for "exit" builtin */
static bool forceexit;
/* the next value of `forceexit' */
bool nextforceexit;


int main(int argc, char **argv)
{
    void *wargv[argc + 1];
    bool help = false, version = false;
    bool do_job_control_set = false, is_interactive_set = false;
    bool noprofile = false, norcfile = false;
    bool option_error = false;
    wchar_t opt;
    const wchar_t *shortest_name;
    const wchar_t *rcfile = NULL;

    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
    setvbuf(stderr, NULL, _IOLBF, BUFSIZ);

    setlocale(LC_ALL, "");
#if HAVE_GETTEXT
    bindtextdomain(PACKAGE_NAME, LOCALEDIR);
    textdomain(PACKAGE_NAME);
#endif

    /* convert arguments into wide strings */
    for (int i = 0; i < argc; i++) {
	wargv[i] = malloc_mbstowcs(argv[i]);
	if (wargv[i] == NULL) {
	    fprintf(stderr,
		    gt("%s: cannot convert the argument `%s' into wide-"
			"character string: replaced with empty string\n"),
		    argv[0], argv[i]);
	    wargv[i] = xwcsdup(L"");
	}
    }
    wargv[argc] = NULL;

    yash_program_invocation_name = wargv[0] ? wargv[0] : L"";
    yash_program_invocation_short_name
	= wcsrchr(yash_program_invocation_name, L'/');
    if (yash_program_invocation_short_name)
	yash_program_invocation_short_name++;
    else
	yash_program_invocation_short_name = yash_program_invocation_name;
    command_name = yash_program_invocation_name;
    is_login_shell = (yash_program_invocation_name[0] == L'-');
    shortest_name = yash_program_invocation_short_name;
    if (shortest_name[0] == L'-')
	shortest_name++;
    if (wcscmp(shortest_name, L"sh") == 0)
	posixly_correct = true;

    /* parse options */
    xoptind = 0;
    xopterr = true;
    while (!option_error &&
	    (opt = xgetopt_long(wargv,
		    L"+*cilo:sV" SHELLSET_OPTIONS,
		    shell_long_options,
		    NULL))) {
	switch (opt) {
	case L'c':
	    if (xoptopt != L'-') {
		xerror(0, Ngt("%lc%lc: invalid option"),
			(wint_t) xoptopt, (wint_t) L'c');
		option_error = true;
	    }
	    shopt_read_arg = true;
	    break;
	case L'i':
	    is_interactive = (xoptopt == L'-');
	    is_interactive_set = true;
	    break;
	case L'l':
	    is_login_shell = (xoptopt == L'-');
	    break;
	case L'o':
	    if (!set_long_option(xoptarg)) {
		xerror(0, Ngt("%lco %ls: invalid option"),
			(wint_t) xoptopt, xoptarg);
		option_error = true;
	    }
	    break;
	case L's':
	    if (xoptopt != L'-') {
		xerror(0, Ngt("%lc%lc: invalid option"),
			(wint_t) xoptopt, (wint_t) L's');
		option_error = true;
	    }
	    shopt_read_stdin = true;
	    break;
	case L'V':
	    version = true;
	    break;
	case L'(':
	    noprofile = true;
	    break;
	case L')':
	    norcfile = true;
	    break;
	case L'!':
	    rcfile = xoptarg;
	    break;
	case L'-':
	    help = true;
	    break;
	case L'?':
	    option_error = true;
	    break;
	case L'm':
	    do_job_control_set = true;
	    /* falls thru! */
	default:
	    set_option(opt);
	    break;
	}
    }

    if (option_error) {
	print_help();
	exit(Exit_ERROR);
    }
    if (version)
	print_version();
    if (help)
	print_help();
    if (version || help)
	exit(Exit_SUCCESS);

    /* ignore "-" if it's the first argument */
    if (wargv[xoptind] && wcscmp(wargv[xoptind], L"-") == 0)
	xoptind++;

    shell_pid = getpid();
    initial_pgid = shell_pgid = getpgrp();
    init_cmdhash();
    init_homedirhash();
    init_variables();
    init_signal();
    init_shellfds();
    init_job();
    init_builtin();
#if YASH_ENABLE_ALIAS
    init_alias();
#endif

    wchar_t *command = command;
    FILE *input = input;
    const char *inputname = inputname;

    if (shopt_read_arg && shopt_read_stdin) {
	xerror(0, Ngt("both -c and -s options cannot be given at once"));
	exit(Exit_ERROR);
    }
    if (shopt_read_arg) {
	command = wargv[xoptind++];
	if (!command) {
	    xerror(0, Ngt("-c option requires an operand"));
	    exit(Exit_ERROR);
	}
	if (xoptind < argc)
	    command_name = wargv[xoptind++];
    } else {
	if (argc == xoptind)
	    shopt_read_stdin = true;
	if (shopt_read_stdin) {
	    input = stdin;
	    inputname = NULL;
	    if (!is_interactive_set && argc == xoptind
		    && isatty(STDIN_FILENO) && isatty(STDERR_FILENO))
		is_interactive = true;
	} else {
	    inputname = argv[xoptind];
	    input = fopen(inputname, "r");
	    input = reopen_with_shellfd(input, "r");
	    command_name = wargv[xoptind++];
	    if (!input) {
		int errno_ = errno;
		xerror(errno_, Ngt("cannot open `%s'"), inputname);
		exit(errno_ == ENOENT ? Exit_NOTFOUND : Exit_NOEXEC);
	    }
	}
    }

    is_interactive_now = is_interactive;
    if (!do_job_control_set)
	do_job_control = is_interactive;
    if (is_interactive || do_job_control)
	open_ttyfd();
    init_fixed_trap_set();
    set_signals();
    set_own_pgid();
    set_positional_parameters(wargv + xoptind);
    if (getuid() == geteuid() && getgid() == getegid()) {
	if (!noprofile)
	    execute_profile();
	if (!norcfile)
	    execute_rcfile(rcfile);
    }
#if YASH_ENABLE_HISTORY
    init_history();
#endif

    if (shopt_read_arg) {
	exec_wcs(command, posixly_correct ? "sh -c" : "yash -c", true);
    } else {
	exec_input(input, inputname, is_interactive, true);
    }
    assert(false);
}

/* Executes "$HOME/.yash_profile" if this is a login shell. */
static void execute_profile(void)
{
    if (!is_login_shell || posixly_correct)
	return;

    wchar_t *wpath = parse_and_expand_string(
	    L"$HOME/.yash_profile", NULL, false);
    if (wpath) {
	char *path = realloc_wcstombs(wpath);
	if (path) {
	    FILE *f = reopen_with_shellfd(fopen(path, "r"), "r");
	    if (f) {
		exec_input(f, path, false, false);
		fclose(f);
	    }
	    free(path);
	}
    }
}

/* Executes the initialization file if this is an interactive shell.
 * `rcfile' is the filename to source.
 * If `rcfile' is NULL, it defaults to "$HOME/.yashrc" or "$ENV". */
static void execute_rcfile(const wchar_t *rcfile)
{
    wchar_t *wpath;
    char *path;
    FILE *f;
    if (!is_interactive)
	return;
    if (posixly_correct) {
	const wchar_t *env = getvar(VAR_ENV);
	if (!env)
	    return;
	wpath = parse_and_expand_string(env, "$ENV", false);
	if (!wpath)
	    return;
	path = realloc_wcstombs(wpath);
    } else {
	if (rcfile) {
	    path = malloc_wcstombs(rcfile);
	} else {
	    wpath = parse_and_expand_string(L"$HOME/.yashrc", NULL, false);
	    if (!wpath)
		return;
	    path = realloc_wcstombs(wpath);
	}
    }
    if (!path)
	return;
    f = reopen_with_shellfd(fopen(path, "r"), "r");
    if (f) {
	exec_input(f, path, false, false);
	fclose(f);
    }
    free(path);
}

/* Exits the shell with the specified exit status.
 * If `status' is negative, the value of `laststatus' is used.
 * This function executes EXIT trap and calls `reset_own_pgid'.
 * This function never returns.
 * This function is reentrant and exits immediately if reentered. */
void exit_shell_with_status(int status)
{
    static bool exiting = false;
    int exitstatus;

    if (status >= 0)
	laststatus = status;
    exitstatus = laststatus;
    if (!exiting) {
	exiting = true;
	execute_exit_trap();
#if YASH_ENABLE_HISTORY
	if (is_interactive_now)
	    write_history(NULL, false);
#endif
    }
    finalize_shell();
    exit(exitstatus);
}

/* Does what to do before exiting/suspending the shell.
 * The EXIT trap should be executed beforehand if needed. */
void finalize_shell(void)
{
    reset_own_pgid();
}

/* Cancels `finalize_shell'. */
void reinitialize_shell(void)
{
    set_own_pgid();
}

/* Prints the help message to stdout */
void print_help(void)
{
    if (posixly_correct) {
	printf(gt("Usage:  sh [options] [filename [args...]]\n"
		  "        sh [options] -c command [command_name [args...]]\n"
		  "        sh [options] -s [args...]\n"));
	printf(gt("Options: -il%ls\n"), SHELLSET_OPTIONS);
    } else {
	printf(gt("Usage:  yash [options] [filename [args...]]\n"
		  "        yash [options] -c command [command_name [args...]]\n"
		  "        yash [options] -s [args...]\n"));
	printf(gt("Options: -il%lsV\n"
	          "         --interactive --login --noprofile "
		  "--norcfile --rcfile=filename\n"),
		SHELLSET_OPTIONS);
	printf(gt("Type `set --help' in the shell for other options.\n"));
//	printf(gt("Long options:\n"));
//	for (size_t i = 0; shell_long_options[i].name; i++)
//	    printf("\t--%ls\n", shell_long_options[i].name);
    }
}

/* Prints the version info to stdout. */
void print_version(void)
{
    printf(gt("Yet another shell, version %s\n"), PACKAGE_VERSION);
    printf(PACKAGE_COPYRIGHT "\n");
    if (shopt_verbose) {
	printf(gt("Compiled %s %s\n"), __DATE__, __TIME__);
	printf(""
#ifndef NDEBUG
		" * DEBUG\n"
#endif
#if YASH_ENABLE_ALIAS
		" * alias\n"
#endif
#if YASH_ENABLE_ARRAY
		" * array\n"
#endif
#if YASH_ENABLE_DIRSTACK
		" * dirstack\n"
#endif
#if YASH_ENABLE_HISTORY
		" * history\n"
#endif
#if YASH_ENABLE_PRINTF
		" * printf/echo\n"
#endif
#if YASH_ENABLE_SOCKET
		" * socket\n"
#endif
#if YASH_ENABLE_TEST
		" * test\n"
#endif
#if YASH_ENABLE_ULIMIT
		" * ulimit\n"
#endif
	      );
    }
}


/* If job control is active, sets the process group ID of the shell to its
 * process ID. */
void set_own_pgid(void)
{
    if (doing_job_control_now && initial_pgid > 0 && shell_pgid != shell_pid) {
	if (setpgid(0, 0) == 0) {
	    shell_pgid = shell_pid;
	    put_foreground(shell_pgid);
	}
    }
}

/* If job control is active, resets the process group ID of the shell.
 * The initial process group ID is restored. */
void reset_own_pgid(void)
{
    if (doing_job_control_now && initial_pgid > 0
	    && initial_pgid != shell_pgid) {
	if (setpgid(0, initial_pgid) == 0) {
	    shell_pgid = initial_pgid;
	    put_foreground(shell_pgid);
	}
    }
}

/* Forgets the value of `initial_pgid' so that `reset_own_pgid' is no longer
 * effective. */
void forget_initial_pgid(void)
{
    initial_pgid = 0;
}


/********** Functions to execute commands **********/

/* Parses a multibyte string and executes the commands.
 * `code' must start in a initial shift state.
 * `name' is printed in an error message on syntax error. `name' may be NULL.
 * If there are no commands in `code', `laststatus' is set to 0. */
void exec_mbs(const char *code, const char *name, bool finally_exit)
{
    struct input_mbs_info iinfo = {
	.src = code,
	.srclen = strlen(code) + 1,
    };
    struct parseinfo_T pinfo = {
	.print_errmsg = true,
	.enable_verbose = false,
	.filename = name,
	.lineno = 1,
	.input = input_mbs,
	.inputinfo = &iinfo,
	.intrinput = false,
	.inputisatty = false,
	.lastinputresult = 0,
    };
    memset(&iinfo.state, 0, sizeof iinfo.state);  // initialize the shift state

    parse_and_exec(&pinfo, finally_exit);
}

/* Parses a wide string and executes the commands.
 * `name' is printed in an error message on syntax error. `name' may be NULL.
 * If there are no commands in `code', `laststatus' is set to 0. */
void exec_wcs(const wchar_t *code, const char *name, bool finally_exit)
{
    struct input_wcs_info iinfo = {
	.src = code,
    };
    struct parseinfo_T pinfo = {
	.print_errmsg = true,
	.enable_verbose = false,
	.filename = name,
	.lineno = 1,
	.input = input_wcs,
	.inputinfo = &iinfo,
	.intrinput = false,
	.inputisatty = false,
	.lastinputresult = 0,
    };

    parse_and_exec(&pinfo, finally_exit);
}

/* Parses input from a file stream and executes the commands.
 * The stream `f' must be set to non-blocking if it is not stdin.
 * `name' is printed in an error message on syntax error. `name' may be NULL.
 * If `intrinput' is true, the input stream is considered interactive.
 * If there are no commands in input, `laststatus' is set to 0. */
void exec_input(FILE *f, const char *name, bool intrinput, bool finally_exit)
{
    struct input_readline_info rlinfo;
    struct parseinfo_T pinfo = {
	.print_errmsg = true,
	.enable_verbose = true,
	.filename = name,
	.lineno = 1,
	.intrinput = intrinput,
	.lastinputresult = 0,
    };
    if (intrinput) {
	rlinfo.fp = f;
	rlinfo.type = 1;
	pinfo.input = input_readline;
	pinfo.inputinfo = &rlinfo;
	pinfo.inputisatty = isatty(fileno(f));
    } else {
	pinfo.input = (f == stdin) ? input_stdin : input_file;
	pinfo.inputinfo = f;
	pinfo.inputisatty = false;
    }
    parse_and_exec(&pinfo, finally_exit);
}

/* Parses input using the specified `parseinfo_T' and executes the commands.
 * If there are no commands in `code', `laststatus' is set to 0. */
void parse_and_exec(parseinfo_T *pinfo, bool finally_exit)
{
    struct execinfo *ei = ei;
    if (!finally_exit)
	ei = save_execinfo();

    bool executed = false;

    for (;;) {
	and_or_T *commands;

	forceexit = nextforceexit;
	nextforceexit = false;
	if (return_pending()) {
	    reset_execinfo();
	    if (pinfo->intrinput)
		xerror(0, Ngt("return: not in function or sourced file"));
	    else
		goto finish;
	}

	switch (read_and_parse(pinfo, &commands)) {
	    case 0:  // OK
		if (commands) {
		    if (!shopt_noexec) {
			exec_and_or_lists(commands,
				finally_exit && !pinfo->intrinput &&
				pinfo->lastinputresult == EOF);
			executed = true;
		    }
		    andorsfree(commands);
		}
		break;
	    case EOF:
		if (shopt_ignoreeof && pinfo->inputisatty) {
		    fprintf(stderr, gt("Use `exit' to leave the shell.\n"));
		    break;
		}
		if (!executed)
		    laststatus = Exit_SUCCESS;
finish:
		if (finally_exit) {
		    wchar_t argv0[] = L"EOF";
		    exit_builtin(1, (void *[]) { argv0, NULL });
		} else
		    goto out;
		break;
	    case 1:  // syntax error
		laststatus = Exit_SYNERROR;
		if (pinfo->intrinput)
		    break;
		else if (finally_exit)
		    exit_shell();
		else
		    goto out;
	}
    }
    /* If no commands are executed, set `laststatus' to 0 finally.
     * We don't set it at first because the value of "$?" gets wrong in
     * execution. */
out:
    load_execinfo(ei);
}


/********** Builtins **********/

static const struct xoption force_help_options[] = {
    { L"force", xno_argument, L'f', },
    { L"help",  xno_argument, L'-', },
    { NULL, 0, 0, },
};

/* "exit" builtin.
 * If the shell is interactive, there are stopped jobs and the -f flag is not
 * specified, then prints a warning message and does not exit. */
int exit_builtin(int argc, void **argv)
{
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"f", force_help_options, NULL))) {
	switch (opt) {
	    case L'f':
		forceexit = true;
		break;
	    case L'-':
		print_builtin_help(ARGV(0));
		return Exit_SUCCESS;
	    default:  print_usage:
		fprintf(stderr, gt("Usage:  exit [-f] [n]\n"));
		SPECIAL_BI_ERROR;
		return Exit_ERROR;
	}
    }
    if (argc - xoptind > 1)
	goto print_usage;

    size_t sjc;
    if (is_interactive_now && !forceexit && (sjc = stopped_job_count()) > 0) {
	fprintf(stderr,
		ngt("You have %zu stopped job(s)!",
		    "You have a stopped job!",
		    "You have %zu stopped jobs!",
		    sjc),
		sjc);
	fprintf(stderr, gt("  Use `exit' again to exit anyway.\n"));
	nextforceexit = true;
	return Exit_FAILURE;
    }

    int status;
    const wchar_t *statusstr = ARGV(xoptind);
    if (statusstr == NULL) {
	status = -1;
    } else {
	wchar_t *endofstr;
	errno = 0;
	status = (int) (wcstoul(statusstr, &endofstr, 0) & 0xFF);
	if (errno || *endofstr != L'\0') {
	    /* ignore `statusstr' if not a valid non-negative integer */
	    status = -1;
	}
    }
    exit_shell_with_status(status);
    assert(false);
}

const char exit_help[] = Ngt(
"exit - exit shell\n"
"\texit [-f] [n]\n"
"Exits the shell with the exit status of <n>.\n"
"If <n> is not specified, it defaults to the exit status of the last executed\n"
"command. <n> should be between 0 and 255 inclusive.\n"
"If the shell is interactive and you have any stopped jobs, the shell prints\n"
"a warning message and does not exit. Use the -f option or use `exit' twice \n"
"in a row to avoid the warning and really exit.\n"
);

/* The "suspend" builtin */
/* This builtin accepts the -f (--force) option, which is silently ignored for
 * bash-compatibility. Yash's "suspend" builtin always suspends the shell
 * whether or not it is a login shell. */
int suspend_builtin(int argc, void **argv)
{
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"f", force_help_options, NULL))) {
	switch (opt) {
	    case L'f':
		break;
	    case L'-':
		print_builtin_help(ARGV(0));
		return Exit_SUCCESS;
	    default:  print_usage:
		fprintf(stderr, gt("Usage:  suspend\n"));
		return Exit_ERROR;
	}
    }
    if (argc != xoptind)
	goto print_usage;

    bool ok;

    finalize_shell();
    ok = send_sigstop_to_myself();
    if (!ok)
	xerror(errno, Ngt("cannot send SIGSTOP signal"));
    reinitialize_shell();
    return ok ? Exit_SUCCESS : Exit_FAILURE;
}

const char suspend_help[] = Ngt(
"suspend - suspend shell\n"
"\tsuspend\n"
"Suspends the shell until it receives a SIGCONT signal.\n"
);


/* vim: set ts=8 sts=4 sw=4 noet: */
