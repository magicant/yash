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
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "option.h"
#include "util.h"
#include "path.h"
#include "input.h"
#include "parser.h"
#include "variable.h"
#include "sig.h"
#include "redir.h"
#include "job.h"
#include "builtin.h"
#include "exec.h"
#include "yash.h"
#include "version.h"


extern int main(int argc, char **argv)
    __attribute__((nonnull));
static void print_help(void);
static void print_version(void);

/* the process ID of the shell */
pid_t shell_pid;
/* the initial process group ID of the shell */
pid_t initial_pgrp;


int main(int argc __attribute__((unused)), char **argv)
{
    bool help = false, version = false;
    bool do_job_control_set = false, is_interactive_set = false;
    bool option_error = false;
    int opt;
    const char *shortest_name;

    yash_program_invocation_name = argv[0] ? argv[0] : "";
    yash_program_invocation_short_name
	= strrchr(yash_program_invocation_name, '/');
    if (yash_program_invocation_short_name)
	yash_program_invocation_short_name++;
    else
	yash_program_invocation_short_name = yash_program_invocation_name;
    command_name = yash_program_invocation_name;
    is_login_shell = (yash_program_invocation_name[0] == '-');
    shortest_name = yash_program_invocation_short_name;
    if (shortest_name[0] == '-')
	shortest_name++;
    if (strcmp(shortest_name, "sh") == 0)
	posixly_correct = true;

    setlocale(LC_ALL, "");
#if HAVE_GETTEXT
    bindtextdomain(PACKAGE_NAME, LOCALEDIR);
    textdomain(PACKAGE_NAME);
#endif

    /* parse options */
    xoptind = 0;
    xopterr = true;
    while ((opt = xgetopt_long(argv,
		    "+*cilo:sV" SHELLSET_OPTIONS,
		    shell_long_options,
		    NULL))
	    >= 0) {
	switch (opt) {
	case 0:
	    break;
	case 'c':
	    if (xoptopt != '-') {
		xerror(0, Ngt("%c%c: invalid option"), xoptopt, 'c');
		option_error = true;
	    }
	    shopt_read_arg = true;
	    break;
	case 'i':
	    is_interactive = (xoptopt == '-');
	    is_interactive_set = true;
	    break;
	case 'l':
	    is_login_shell = (xoptopt == '-');
	    break;
	case 'o':
	    if (!set_long_option(xoptarg)) {
		xerror(0, Ngt("%co %s: invalid option"), xoptopt, xoptarg);
		option_error = true;
	    }
	    break;
	case 's':
	    if (xoptopt != '-') {
		xerror(0, Ngt("%c%c: invalid option"), xoptopt, 's');
		option_error = true;
	    }
	    shopt_read_stdin = true;
	    break;
	case 'V':
	    version = true;
	    break;
	case '!':
	    help = true;
	    break;
	case '?':
	    option_error = true;
	    break;
	case 'm':
	    do_job_control_set = true;
	    /* falls thru! */
	default:
	    set_option(opt);
	    break;
	}
    }

    if (option_error)
	exit(EXIT_ERROR);

    /* ignore "-" if it's the first argument */
    if (argv[xoptind] && strcmp(argv[xoptind], "-") == 0)
	xoptind++;

    if (version)
	print_version();
    if (help)
	print_help();
    if (version || help)
	exit(EXIT_SUCCESS);

    shell_pid = getpid();
    initial_pgrp = getpgrp();
    init_cmdhash();
    init_homedirhash();
    init_variables();
    init_signal();
    init_shellfds();
    init_job();
    init_builtin();

    if (shopt_read_arg && shopt_read_stdin) {
	xerror(0, Ngt("both -c and -s options cannot be given at once"));
	exit(EXIT_ERROR);
    }
    shopt_read_arg = shopt_read_arg;
    shopt_read_stdin = shopt_read_stdin;
    if (shopt_read_arg) {
	char *command = argv[xoptind++];
	if (!command) {
	    xerror(0, Ngt("-c option requires an operand"));
	    exit(EXIT_ERROR);
	}
	if (argv[xoptind])
	    command_name = argv[xoptind++];
	is_interactive_now = is_interactive;
	if (!do_job_control_set)
	    do_job_control = is_interactive;
	set_signals();
	open_ttyfd();
	set_own_pgrp();
	set_positional_parameters(argv + xoptind);
	exec_mbs(command, posixly_correct ? "sh -c" : "yash -c", true);
    } else {
	FILE *input;
	const char *inputname;
	if (!argv[xoptind])
	    shopt_read_stdin = true;
	if (shopt_read_stdin) {
	    input = stdin;
	    inputname = NULL;
	    if (!is_interactive_set && !argv[xoptind]
		    && isatty(STDIN_FILENO) && isatty(STDERR_FILENO))
		is_interactive = true;
	} else {
	    command_name = argv[xoptind++];
	    input = fopen(command_name, "r");
	    inputname = command_name;
	    input = reopen_with_shellfd(input, "r");
	    if (!input) {
		int errno_ = errno;
		xerror(errno_, Ngt("cannot open file `%s'"), command_name);
		exit(errno_ == ENOENT ? EXIT_NOTFOUND : EXIT_NOEXEC);
	    }
	}
	is_interactive_now = is_interactive;
	if (!do_job_control_set)
	    do_job_control = is_interactive;
	set_signals();
	open_ttyfd();
	set_own_pgrp();
	set_positional_parameters(argv + xoptind);
	exec_input(input, inputname, is_interactive, true);
    }
    assert(false);
    // TODO yashrc
}

/* Exits the shell with the specified exit status.
 * If `status' is negative, the value of `laststatus' is used.
 * This function executes EXIT trap and calls `reset_own_pgrp'.
 * This function never returns.
 * This function is reentrant and exits immediately if reentered. */
void exit_shell_with_status(int status)
{
    static bool exiting = false;
    static int exitstatus;
    if (!exiting) {
	exiting = true;
	exitstatus = (status < 0) ? laststatus : status;
	// TODO yash: exit_shell: execute EXIT trap
    }
    reset_own_pgrp();
    exit((status < 0) ? exitstatus : status);
}

/* Prints the help message to stdout */
void print_help(void)
{
    if (posixly_correct) {
	printf(gt("Usage:  sh [options] [filename [args...]]\n"
		  "        sh [options] -c command [command_name [args...]]\n"
		  "        sh [options] -s [args...]\n"));
	printf(gt("Options: -il%s\n"), SHELLSET_OPTIONS);
    } else {
	printf(gt("Usage:  yash [options] [filename [args...]]\n"
		  "        yash [options] -c command [args...]\n"
		  "        yash [options] -s [args...]\n"));
	printf(gt("Short options: -il%sV\n"), SHELLSET_OPTIONS);
	printf(gt("Long options:\n"));
	for (size_t i = 0; shell_long_options[i].name; i++)
	    printf("\t--%s\n", shell_long_options[i].name);
    }
}

/* Prints the version info to stdout. */
void print_version(void)
{
    printf(gt("Yet another shell, version %s\n"), PACKAGE_VERSION);
    printf(PACKAGE_COPYRIGHT "\n");
}


/* If job control is active, sets the process group ID of the shell to its
 * process ID. */
void set_own_pgrp(void)
{
    if (do_job_control) {
	setpgid(0, 0);
	make_myself_foreground();
    }
}

/* If job control is active, resets the process group ID of the shell.
 * The initial process group ID is restored. */
void reset_own_pgrp(void)
{
    if (do_job_control && initial_pgrp > 0) {
	setpgid(0, initial_pgrp);
	make_myself_foreground();
    }
}

/* Forgets the value of `initial_pgrp' so that `reset_own_pgrp' is no longer
 * effective. */
void forget_initial_pgrp(void)
{
    initial_pgrp = 0;
}


/********** Functions to execute commands **********/

/* Parses a multibyte string and executes the commands.
 * `code' must start in a initial shift state.
 * `name' is printed in an error message on syntax error. `name' may be NULL.
 * Returns true iff successful (no input/parse errors).
 * If there are no commands in `code', `laststatus' is set to 0. */
bool exec_mbs(const char *code, const char *name, bool finally_exit)
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

    return parse_and_exec(&pinfo, finally_exit);
}

/* Parses a wide string and executes the commands.
 * `name' is printed in an error message on syntax error. `name' may be NULL.
 * Returns true iff successful (no input/parse errors).
 * If there are no commands in `code', `laststatus' is set to 0. */
bool exec_wcs(const wchar_t *code, const char *name, bool finally_exit)
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

    return parse_and_exec(&pinfo, finally_exit);
}

/* Parses input from a file stream and executes the commands.
 * `name' is printed in an error message on syntax error. `name' may be NULL.
 * If `intrinput' is true, the input stream is considered interactive.
 * Returns true iff successful (no input/parse errors).
 * If there are no commands in `code', `laststatus' is set to 0. */
bool exec_input(FILE *f, const char *name, bool intrinput, bool finally_exit)
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
	pinfo.input = input_file;
	pinfo.inputinfo = f;
	pinfo.inputisatty = false;
    }
    return parse_and_exec(&pinfo, finally_exit);
}

/* Parses input using the specified `parseinfo_T' and executes the commands.
 * Returns true iff successful (no input/parse errors).
 * If there are no commands in `code', `laststatus' is set to 0. */
bool parse_and_exec(parseinfo_T *pinfo, bool finally_exit)
{
    bool executed = false;
    bool justwarned = false;

    for (;;) {
	and_or_T *commands;
	switch (read_and_parse(pinfo, &commands)) {
	    case 0:  // OK
		justwarned = false;
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
		} else if (pinfo->intrinput && !justwarned) {
		    // TODO yash: parse_and_exec: exit builtin should have the
		    // same warning mechanism
		    size_t sjc = stopped_job_count();
		    if (sjc > 0) {
			fprintf(stderr,
				ngt("You have %zu stopped job(s)!\n",
				    "You have a stopped job!\n",
				    "You have %zu stopped jobs!\n",
				    sjc),
				sjc);
			justwarned = true;
			break;
		    }
		}
		if (!executed)
		    laststatus = EXIT_SUCCESS;
		if (finally_exit)
		    exit_shell();
		else
		    return true;
	    case 1:  // syntax error
		justwarned = false;
		laststatus = EXIT_SYNERROR;
		if (pinfo->intrinput)
		    break;
		else if (finally_exit)
		    exit_shell();
		else
		    return false;
	}
    }
    /* If no commands are executed, set `laststatus' to 0 finally.
     * We don't set it at first because the value of "$?" gets wrong in
     * execution. */
}


/* vim: set ts=8 sts=4 sw=4 noet: */
