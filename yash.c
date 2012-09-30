/* Yash: yet another shell */
/* yash.c: basic functions of the shell */
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
#include <fcntl.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#if YASH_ENABLE_ALIAS
# include "alias.h"
#endif
#include "builtin.h"
#include "configm.h"
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
#include "yash.h"


extern int main(int argc, char **argv)
    __attribute__((nonnull));
static struct input_file_info_T *new_input_file_info(int fd, size_t bufsize)
    __attribute__((malloc,warn_unused_result));
static void execute_profile(const wchar_t *profile);
static void execute_rcfile(const wchar_t *rcfile);
static void print_help(void);
static void print_version(void);

static void parse_and_exec(struct parseparam_T *pinfo, bool finally_exit)
    __attribute__((nonnull(1)));
static bool input_is_interactive_terminal(const parseparam_T *pinfo)
    __attribute__((nonnull));

/* The process ID of the shell.
 * This value does not change in a subshell. */
pid_t shell_pid;
/* The process group ID of the shell.
 * In a job-controlled subshell, this value is set to the subshell's pgid. */
pid_t shell_pgid;

/* If this flag is true when the "exit" built-in is invoked, the -f option is
 * assumed to be specified. */
static bool forceexit;
/* The next value of `forceexit'. */
bool nextforceexit;

/* The `input_file_info_T' structure for reading from the standard input. */
struct input_file_info_T *stdin_input_file_info;


/* The "main" function. The execution of the shell starts here. */
int main(int argc, char **argv)
{
    void *wargv[argc + 1];
    const wchar_t *shortest_name;

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
		    gt("%s: cannot convert the argument `%s' "
			"into a wide character string"),
		    argv[0], argv[i]);
	    fprintf(stderr,
		    gt("%s: the argument is replaced with an empty string\n"),
		    argv[0]);
	    wargv[i] = xwcsdup(L"");
	}
    }
    wargv[argc] = NULL;

    /* parse argv[0] */
    yash_program_invocation_name = wargv[0];
    yash_program_invocation_short_name
	= wcsrchr(yash_program_invocation_name, L'/');
    if (yash_program_invocation_short_name != NULL)
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

    shell_pid = getpid();
    shell_pgid = getpgrp();
    stdin_input_file_info = new_input_file_info(STDIN_FILENO, 1);
    init_cmdhash();
    init_homedirhash();
    init_environment();
    init_signal();
    init_shellfds();
    init_job();
    init_builtin();
#if YASH_ENABLE_ALIAS
    init_alias();
#endif

    struct shell_invocation_T options = {
	.profile = NULL, .rcfile = NULL,
    };

    int optresult = parse_shell_options(argc, wargv, &options);
    if (optresult != Exit_SUCCESS)
	exit(optresult);
    if (options.version)
	print_version();
    if (options.help)
	print_help();
    if (options.version || options.help)
	exit(yash_error_message_count == 0 ? Exit_SUCCESS : Exit_FAILURE);

    init_variables();

    union {
	wchar_t *command;
	int fd;
    } input;
    const char *inputname;

    if (shopt_cmdline && shopt_stdin) {
	xerror(0, Ngt("the -c and -s options cannot be used both at once"));
	exit(Exit_ERROR);
    }
    if (shopt_cmdline) {
	input.command = wargv[xoptind++];
	if (input.command == NULL) {
	    xerror(0, Ngt("the -c option is specified "
			"but no command is given"));
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
	    shopt_stdin = true;
	if (shopt_stdin) {
	    input.fd = STDIN_FILENO;
	    inputname = NULL;
	    if (!options.is_interactive_set && argc == xoptind
		    && isatty(STDIN_FILENO) && isatty(STDERR_FILENO))
		is_interactive = true;
	    unset_nonblocking(STDIN_FILENO);
	} else {
	    inputname = argv[xoptind];
	    command_name = wargv[xoptind];
	    xoptind++;
	    input.fd = move_to_shellfd(open(inputname, O_RDONLY));
	    if (input.fd < 0) {
		int errno_ = errno;
		xerror(errno_, Ngt("cannot open file `%s'"), inputname);
		exit(errno_ == ENOENT ? Exit_NOTFOUND : Exit_NOEXEC);
	    }
	}
    }

#if YASH_ENABLE_LINEEDIT
    /* enable line editing if interactive and connected to a terminal */
    if (!options.lineedit_set && shopt_lineedit == SHOPT_NOLINEEDIT)
	if (is_interactive && isatty(STDIN_FILENO) && isatty(STDERR_FILENO))
	    set_lineedit_option(SHOPT_VI);
#endif

    is_interactive_now = is_interactive;
    if (!options.do_job_control_set)
	do_job_control = is_interactive;
    if (do_job_control) {
	open_ttyfd();
	if (do_job_control)
	    ensure_foreground();
    }
    set_signals();
    set_positional_parameters(&wargv[xoptind]);

    if (is_login_shell && !posixly_correct && !options.noprofile)
	if (getuid() == geteuid() && getgid() == getegid())
	    execute_profile(options.profile);
    if (is_interactive && !options.norcfile)
	if (getuid() == geteuid() && getgid() == getegid())
	    execute_rcfile(options.rcfile);

    if (shopt_cmdline) {
	exec_wcs(input.command, inputname, true);
    } else {
	exec_input(input.fd, inputname, is_interactive, true, true);
    }
    assert(false);
}

struct input_file_info_T *new_input_file_info(int fd, size_t bufsize)
{
    struct input_file_info_T *info
	= xmallocs(sizeof *info, bufsize, sizeof *info->buf);
    info->fd = fd;
    info->bufpos = info->bufmax = 0;
    info->bufsize = bufsize;
    memset(&info->state, 0, sizeof info->state);  // initial shift state
    return info;
}

/* Executes "$HOME/.yash_profile". */
void execute_profile(const wchar_t *profile)
{
    char *path;

    if (profile != NULL) {
	path = malloc_wcstombs(profile);
    } else {
	wchar_t *wpath =
	    parse_and_expand_string(L"$HOME/.yash_profile", NULL, false);
	if (wpath == NULL)
	    return;
	path = realloc_wcstombs(wpath);
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

/* Executes the initialization file.
 * `rcfile' is the filename to source.
 * If `rcfile' is NULL, it defaults to "$HOME/.yashrc" or "$ENV". */
void execute_rcfile(const wchar_t *rcfile)
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
	xprintf(gt(
	    "Usage:  sh [options] [filename [args...]]\n"
	    "        sh [options] -c command [command_name [args...]]\n"
	    "        sh [options] -s [args...]\n"));
	xprintf(gt("Options: -abCefhilmnuvx\n"));
    } else {
	xprintf(gt(
	    "Usage:  yash [options] [filename [args...]]\n"
	    "        yash [options] -c command [command_name [args...]]\n"
	    "        yash [options] -s [args...]\n"));
	xprintf(gt(
	    "Options: -abCefhilmnuVvx\n"
	    "         --interactive --login --noprofile --norcfile \n"
	    "         --profile=filename --rcfile=filename\n"));
	xprintf(gt("Type `set --help' in the shell for other options.\n"));
    }
}

/* Prints the version info to the standard output. */
void print_version(void)
{
    xprintf(gt("Yet another shell, version %s\n"), PACKAGE_VERSION);
    xprintf(PACKAGE_COPYRIGHT "\n");
    xprintf(gt("This is free software licensed under GNU GPL version 2.\n"
	    "You can modify and redistribute it, but there is NO WARRANTY.\n"));

    if (shopt_verbose) {
	xprintf(gt("\nEnabled features:\n"));
	xprintf(""
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
#if HAVE_GETTEXT
		" * nls\n"
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
    struct input_wcs_info_T iinfo = {
	.src = code,
    };
    struct parseparam_T pinfo = {
	.print_errmsg = true,
	.enable_verbose = false,
#if YASH_ENABLE_ALIAS
	.enable_alias = true,
#endif
	.filename = name,
	.lineno = 1,
	.input = input_wcs,
	.inputinfo = &iinfo,
	.interactive = false,
    };

    parse_and_exec(&pinfo, finally_exit);
}

/* Parses the input from the specified file descriptor and executes commands.
 * The file descriptor must be either STDIN_FILENO or a shell FD. If the file
 * descriptor is STDIN_FILENO, `finally_exit' must be true.
 * If `name' is non-NULL, it is printed in an error message on syntax error.
 * If `interactive' is true, the input is considered interactive.
 * If there are no commands in the input, `laststatus' is set to zero. */
void exec_input(int fd, const char *name,
	bool interactive, bool enable_alias, bool finally_exit)
{
    struct parseparam_T pinfo = {
	.print_errmsg = true,
	.enable_verbose = true,
#if YASH_ENABLE_ALIAS
	.enable_alias = enable_alias,
#endif
	.filename = name,
	.lineno = 1,
	.interactive = interactive,
    };
    struct input_interactive_info_T intrinfo;
    struct input_file_info_T *inputinfo;

    if (fd == STDIN_FILENO)
	inputinfo = stdin_input_file_info;
    else
	inputinfo = new_input_file_info(fd, BUFSIZ);

    if (interactive) {
	intrinfo.fileinfo = inputinfo;
	intrinfo.prompttype = 1;
	pinfo.input = input_interactive;
	pinfo.inputinfo = &intrinfo;
    } else {
	pinfo.input = input_file;
	pinfo.inputinfo = inputinfo;
    }
    parse_and_exec(&pinfo, finally_exit);

    assert(inputinfo != stdin_input_file_info);
    free(inputinfo);

#if !YASH_ENABLE_ALIAS
    (void) enable_alias;  // suppress compiler warning
#endif
}

/* Parses the input using the specified `parseinfo_T' and executes commands.
 * If no commands were executed, `laststatus' is set to zero. */
void parse_and_exec(parseparam_T *pinfo, bool finally_exit)
{
    bool executed = false;

    if (pinfo->interactive)
	disable_return();

    for (;;) {
	if (pinfo->interactive) {
	    set_laststatus_if_interrupted();
	    forceexit = nextforceexit;
	    nextforceexit = false;
	    pinfo->lineno = 1;
	} else {
	    if (need_break())
		goto out;
	}

	and_or_T *commands;
	switch (read_and_parse(pinfo, &commands)) {
	    case PR_OK:
		if (commands != NULL) {
		    if (shopt_exec || is_interactive) {
			exec_and_or_lists(commands,
				finally_exit && !pinfo->interactive &&
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
		} else {
		    goto out;
		}
		break;
	    case PR_SYNTAX_ERROR:
		if (!is_interactive_now)
		    exit_shell_with_status(Exit_SYNERROR);
		if (!pinfo->interactive) {
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
    cancel_return();
    if (finally_exit)
	exit_shell();
}

bool input_is_interactive_terminal(const parseparam_T *pinfo)
{
    if (!pinfo->interactive)
	return false;

    struct input_interactive_info_T *ir = pinfo->inputinfo;
    return isatty(ir->fileinfo->fd);
}


/********** Built-ins **********/

static const struct xgetopt_T force_help_options[] = {
    { L'f', L"force", OPTARG_NONE, true,  NULL, },
#if YASH_ENABLE_HELP
    { L'-', L"help",  OPTARG_NONE, false, NULL, },
#endif
    { L'\0', NULL, 0, false, NULL, },
};

/* The "exit" built-in.
 * If the shell is interactive, there are stopped jobs and the -f flag is not
 * specified, then prints a warning message and does not exit. */
int exit_builtin(int argc, void **argv)
{
    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, force_help_options, 0)) != NULL) {
	switch (opt->shortopt) {
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
		ngt("You have a stopped job!",
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
"exit the shell"
);
#endif

/* The "suspend" built-in, which accepts the following options:
 *  -f: suspend even if it may cause a deadlock. */
int suspend_builtin(int argc, void **argv)
{
    bool force = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, force_help_options, 0)) != NULL) {
	switch (opt->shortopt) {
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
	xerror(0, Ngt("refusing to suspend because of a possible deadlock.\n"
		    "Use the -f option to suspend anyway."));
	return Exit_FAILURE;
    }

    stop_myself();

    if (doing_job_control_now)
	ensure_foreground();
    return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;
}

#if YASH_ENABLE_HELP
const char suspend_help[] = Ngt(
"suspend the shell"
);
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
