/* Yash: yet another shell */
/* yash.c: basic functions of the shell */
/* (C) 2007-2009 magicant */

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
#include <fcntl.h>
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
#if YASH_ENABLE_ALIAS
# include "alias.h"
#endif
#include "builtin.h"
#include "exec.h"
#include "expand.h"
#if YASH_ENABLE_HISTORY
# include "history.h"
#endif
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

static void parse_and_exec(struct parseinfo_T *pinfo, _Bool finally_exit)
    __attribute__((nonnull(1)));
static bool input_is_interactive_terminal(const parseinfo_T *pinfo)
    __attribute__((nonnull));

/* The process ID of the shell.
 * This value does not change in a subshell. */
pid_t shell_pid;
/* The process group ID of the shell.
 * In a job-controlled subshell, this value is set to the subshell's pgid. */
pid_t shell_pgid;

/* If this flag is true when the "exit" builtin is invoked, the -f option is
 * assumed to be specified. */
static bool forceexit;
/* The next value of `forceexit'. */
bool nextforceexit;


/* The "main" function. The execution of the shell starts here. */
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

    /* parse argv[0] */
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
		    &setoptindex))) {
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
	case L'L':
	    set_option();
	    break;
	case L'o':
	    do_job_control_set |= wcscmp(xoptarg, L"monitor") == 0;
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
	    set_single_option(opt);
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

    /* `shell_pgid' must be initialized after the options have been parsed.
     * This is required for the `set_monitor_option' function to work. */
    shell_pid = getpid();
    shell_pgid = getpgrp();
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

    union {
	wchar_t *command;
	int fd;
    } input;
    const char *inputname;

    if (shopt_read_arg && shopt_read_stdin) {
	xerror(0, Ngt("both of -c and -s options cannot be used at once"));
	exit(Exit_ERROR);
    }
    if (shopt_read_arg) {
	input.command = wargv[xoptind++];
	if (input.command == NULL) {
	    xerror(0, Ngt("-c option specified but no command given"));
	    exit(Exit_ERROR);
	}
	if (xoptind < argc) {
	    inputname = argv[xoptind];
	    command_name = wargv[xoptind++];
	} else {
	    inputname = posixly_correct ? "sh -c" : "yash -c";
	}
    } else {
	if (argc == xoptind)
	    shopt_read_stdin = true;
	if (shopt_read_stdin) {
	    input.fd = STDIN_FILENO;
	    inputname = NULL;
	    if (!is_interactive_set && argc == xoptind
		    && isatty(STDIN_FILENO) && isatty(STDERR_FILENO))
		is_interactive = true;
	} else {
	    inputname = argv[xoptind];
	    command_name = wargv[xoptind];
	    xoptind++;
	    input.fd = move_to_shellfd(open(inputname, O_RDONLY));
	    if (input.fd < 0) {
		int errno_ = errno;
		xerror(errno_, Ngt("cannot open `%s'"), inputname);
		exit(errno_ == ENOENT ? Exit_NOTFOUND : Exit_NOEXEC);
	    }
	}
    }

#if YASH_ENABLE_LINEEDIT
    /* enable lineedit if interactive and connected to a terminal */
    if (is_interactive && shopt_lineedit == shopt_nolineedit)
	if (!is_interactive_set
		|| (isatty(STDIN_FILENO) && isatty(STDERR_FILENO)))
	    shopt_lineedit = shopt_vi;
#endif

    is_interactive_now = is_interactive;
    if (!do_job_control_set)
	do_job_control = is_interactive;
    if (do_job_control) {
	open_ttyfd();
	if (do_job_control)
	    ensure_foreground();
    }
    set_signals();
    set_positional_parameters(wargv + xoptind);

    if (is_login_shell && !posixly_correct && !noprofile)
	if (getuid() == geteuid() && getgid() == getegid())
	    execute_profile();
    if (is_interactive && !norcfile)
	if (getuid() == geteuid() && getgid() == getegid())
	    execute_rcfile(rcfile);

    if (shopt_read_arg) {
	exec_wcs(input.command, inputname, true);
    } else {
	exec_input(input.fd, inputname, is_interactive, true, true);
    }
    assert(false);
}

/* Executes "$HOME/.yash_profile". */
static void execute_profile(void)
{
    wchar_t *wpath =
	parse_and_expand_string(L"$HOME/.yash_profile", NULL, false);
    if (wpath != NULL) {
	char *path = realloc_wcstombs(wpath);
	if (path != NULL) {
	    int fd = move_to_shellfd(open(path, O_RDONLY));
	    if (fd >= 0) {
		exec_input(fd, path, false, true, false);
		remove_shellfd(fd);
		xclose(fd);
	    }
	    free(path);
	}
    }
}

/* Executes the initialization file.
 * `rcfile' is the filename to source.
 * If `rcfile' is NULL, it defaults to "$HOME/.yashrc" or "$ENV". */
static void execute_rcfile(const wchar_t *rcfile)
{
    char *path;

    if (posixly_correct) {
	const wchar_t *env = getvar(L VAR_ENV);
	if (env == NULL)
	    return;
	wchar_t *wpath = parse_and_expand_string(env, "$ENV", false);
	if (wpath == NULL)
	    return;
	path = realloc_wcstombs(wpath);
    } else {
	if (rcfile != NULL) {
	    path = malloc_wcstombs(rcfile);
	} else {
	    wchar_t *wpath =
		parse_and_expand_string(L"$HOME/.yashrc", NULL, false);
	    if (wpath == NULL)
		return;
	    path = realloc_wcstombs(wpath);
	}
    }
    if (path == NULL)
	return;

    int fd = move_to_shellfd(open(path, O_RDONLY));
    if (fd >= 0) {
	exec_input(fd, path, false, true, false);
	remove_shellfd(fd);
	xclose(fd);
    }
    free(path);
}

/* Exits the shell with the specified exit status.
 * When this function is first called and `status' is negative, the value of
 * `laststatus' is used for `status'. When this function is called again and
 * `status' is negative, the value for the first call is used.
 * This function executes the EXIT trap.
 * This function never returns.
 * This function is reentrant and exits immediately if reentered. */
void exit_shell_with_status(int status)
{
    static int exitstatus = -1;

    if (status >= 0)
	laststatus = status;
    assert(laststatus >= 0);
    if (exitstatus < 0) {
	exitstatus = laststatus;
	execute_exit_trap();
    } else {
	if (status >= 0)
	    exitstatus = status;
    }
#if YASH_ENABLE_HISTORY
    finalize_history();
#endif
    exit(exitstatus);
}

/* Prints the help message to the standard output. */
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

/* Prints the version info to the standard output. */
void print_version(void)
{
    printf(gt("Yet another shell, version %s\n"), PACKAGE_VERSION);
    printf(PACKAGE_COPYRIGHT "\n");
    printf(gt("This is free software licensed under GNU GPL version 2.\n"
	    "You can modify and redistribute it, but there is NO WARRANTY.\n"));

    if (shopt_verbose) {
	printf(gt("\nCompiled %s %s\nEnabled features:\n"), __DATE__, __TIME__);
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
#if YASH_ENABLE_HELP
		" * help\n"
#endif
#if YASH_ENABLE_HISTORY
		" * history\n"
#endif
#if YASH_ENABLE_LINEEDIT
		" * lineedit\n"
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


/********** Functions to Execute Commands **********/

/* Parses the specified wide string and executes it as commands.
 * `name' is printed in an error message on syntax error. `name' may be NULL.
 * If there are no commands in `code', `laststatus' is set to zero. */
void exec_wcs(const wchar_t *code, const char *name, bool finally_exit)
{
    struct input_wcs_info iinfo = {
	.src = code,
    };
    struct parseinfo_T pinfo = {
	.print_errmsg = true,
	.enable_verbose = false,
#if YASH_ENABLE_ALIAS
	.enable_alias = true,
#endif
	.filename = name,
	.lineno = 1,
	.input = input_wcs,
	.inputinfo = &iinfo,
	.intrinput = false,
    };

    parse_and_exec(&pinfo, finally_exit);
}

/* Parses the input from the specified file descriptor and executes commands.
 * The file descriptor must be either STDIN_FILENO or a shell FD.
 * If `name' is non-NULL, it is printed in an error message on syntax error.
 * If `intrinput' is true, the input is considered interactive.
 * If there are no commands in the input, `laststatus' is set to zero. */
void exec_input(int fd, const char *name,
	bool intrinput, bool enable_alias, bool finally_exit)
{
    struct parseinfo_T pinfo = {
	.print_errmsg = true,
	.enable_verbose = true,
#if YASH_ENABLE_ALIAS
	.enable_alias = enable_alias,
#endif
	.filename = name,
	.lineno = 1,
	.intrinput = intrinput,
    };
    struct input_interactive_info intrinfo;

    size_t bufsize = (fd == STDIN_FILENO) ? 1 : BUFSIZ;
    struct input_file_info *info = xmalloc(sizeof *info + bufsize);
    info->fd = fd;
    info->bufpos = info->bufmax = 0;
    info->bufsize = bufsize;
    memset(&info->state, 0, sizeof info->state);

    if (intrinput) {
	intrinfo.fileinfo = info;
	intrinfo.prompttype = 1;

	pinfo.input = input_interactive;
	pinfo.inputinfo = &intrinfo;
    } else {
	pinfo.input = input_file;
	pinfo.inputinfo = info;
    }
    parse_and_exec(&pinfo, finally_exit);
    free(info);

#if !YASH_ENABLE_ALIAS
    (void) enable_alias;  // suppress compiler warning
#endif
}

/* Parses the input using the specified `parseinfo_T' and executes commands.
 * If no commands were executed, `laststatus' is set to zero. */
void parse_and_exec(parseinfo_T *pinfo, bool finally_exit)
{
#ifdef NDEBUG
    struct execinfo *ei;
#else
    struct execinfo *ei = ei;
#endif
    if (!finally_exit)
	ei = save_execinfo();

    bool executed = false;

    for (;;) {
	if (pinfo->intrinput) {
	    forceexit = nextforceexit;
	    nextforceexit = false;
	    if (!is_interrupted() && return_pending()) {
		xerror(0, Ngt("return: not in function or sourced file"));
		laststatus = Exit_FAILURE;
	    }
	    reset_execinfo();
	    pinfo->lineno = 1;
	} else {
	    if (return_pending())
		goto out;
	}

	and_or_T *commands;
	switch (read_and_parse(pinfo, &commands)) {
	    case PR_OK:
		if (commands) {
		    if (!shopt_noexec) {
			exec_and_or_lists(commands,
				finally_exit && !pinfo->intrinput &&
				pinfo->lastinputresult == INPUT_EOF);
			executed = true;
		    }
		    andorsfree(commands);
		}
		break;
	    case PR_EOF:
		if (shopt_ignoreeof && input_is_interactive_terminal(pinfo)) {
		    fprintf(stderr, gt("Use `exit' to leave the shell.\n"));
		    break;
		}
		if (!executed)
		    laststatus = Exit_SUCCESS;
		if (finally_exit) {
		    wchar_t argv0[] = L"EOF";
		    exit_builtin(1, (void *[]) { argv0, NULL });
		} else
		    goto out;
		break;
	    case PR_SYNTAX_ERROR:
		if (!is_interactive_now)
		    exit_shell_with_status(Exit_SYNERROR);
		if (!pinfo->intrinput) {
		    laststatus = Exit_SYNERROR;
		    goto out;
		}
		break;
	    case PR_INPUT_ERROR:
		laststatus = Exit_ERROR;
		goto out;
	}
    }
out:
    if (finally_exit)
	exit_shell();
    load_execinfo(ei);
}

bool input_is_interactive_terminal(const parseinfo_T *pinfo)
{
    if (!pinfo->intrinput)
	return false;

    struct input_interactive_info *ir = pinfo->inputinfo;
    return isatty(ir->fileinfo->fd);
}


/********** Builtins **********/

static const struct xoption force_help_options[] = {
    { L"force", xno_argument, L'f', },
#if YASH_ENABLE_HELP
    { L"help",  xno_argument, L'-', },
#endif
    { NULL, 0, 0, },
};

/* The "exit" builtin.
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
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
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
	forceexit = nextforceexit = true;
	return Exit_FAILURE;
    }

    int status;
    const wchar_t *statusstr = ARGV(xoptind);
    if (statusstr != NULL) {
	if (!xwcstoi(statusstr, 10, &status) || status < 0) {
	    xerror(0, Ngt("`%ls' is not a valid integer"), statusstr);
	    status = Exit_ERROR;
	    SPECIAL_BI_ERROR;
	}
    } else {
	status = -1;
    }
    exit_shell_with_status(status);
    assert(false);
}

#if YASH_ENABLE_HELP
const char exit_help[] = Ngt(
"exit - exit shell\n"
"\texit [-f] [n]\n"
"Exits the shell with the exit status of <n>.\n"
"If <n> is not specified, it defaults to the exit status of the last executed\n"
"command. <n> should be between 0 and 255 inclusive.\n"
"If the shell is interactive and you have any stopped jobs, the shell prints\n"
"a warning message and does not exit. Use the -f (--force) option or use\n"
"`exit' twice in a row to avoid the warning and really exit.\n"
);
#endif

/* The "suspend" builtin, which accepts the following options:
 *  -f: suspend even if it may cause a deadlock. */
int suspend_builtin(int argc, void **argv)
{
    bool force = false;
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"f", force_help_options, NULL))) {
	switch (opt) {
	    case L'f':
		force = true;
		break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:  print_usage:
		fprintf(stderr, gt("Usage:  suspend [-f]\n"));
		return Exit_ERROR;
	}
    }
    if (argc != xoptind)
	goto print_usage;
    if (!force && is_interactive_now && getsid(0) == shell_pgid) {
	xerror(0, Ngt("refusing to suspend because of possible deadlock.\n"
		    "Use the -f option to suspend anyway."));
	return Exit_FAILURE;
    }

    bool ok = send_sigstop_to_myself();
    if (!ok)
	xerror(errno, Ngt("cannot send SIGSTOP signal"));
    if (doing_job_control_now)
	ensure_foreground();
    return ok ? Exit_SUCCESS : Exit_FAILURE;
}

#if YASH_ENABLE_HELP
const char suspend_help[] = Ngt(
"suspend - suspend shell\n"
"\tsuspend [-f]\n"
"Suspends the shell until it receives SIGCONT.\n"
"If the shell is interactive and is a session leader, this command refuses to\n"
"suspend it in order to avoid a possible deadlock. You can use the -f option\n"
"to force the shell to suspend anyway.\n"
);
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
