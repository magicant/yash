/* Yash: yet another shell */
/* option.c: option settings */
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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "builtin.h"
#include "exec.h"
#include "option.h"
#include "redir.h"
#include "sig.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"
#include "yash.h"


typedef void setoptfunc_T(void *argp);
struct setoptinfo_T {
    setoptfunc_T *func;
    void *argp;
};

static void set_ignore_option(void *argp);
static void set_bool_option(void *argp);
#if YASH_ENABLE_LINEEDIT
static void set_yesnoauto_option(void *argp);
#endif
static void set_monitor_option(void *argp);
#if YASH_ENABLE_LINEEDIT
static void set_lineedit_option(void *argp);
#endif


/* If set, the shell behaves strictly as defined in POSIX.
 * Corresponds to --posix option. */
bool posixly_correct;

/* If set, this shell is a login shell.
 * Corresponds to --login option. */
bool is_login_shell;

/* If set, this shell is interactive.
 * `is_interactive_now' is set false in subshells while `is_interactive'
 * remains unchanged.
 * Correspond to -i/--interactive option. */
bool is_interactive, is_interactive_now;

/* If set, the shell performs job control.
 * Corresponds to -m/--monitor option. */
bool do_job_control;
/* If set, the shell immediately notifies on a change of job status.
 * Corresponds to -b/--notify option. */
bool shopt_notify;
#if YASH_ENABLE_LINEEDIT
/* If set, the shell immediately notifies on a change of job status during line-
 * editing. Ignored if `shopt_notify' is set. Corresponds to --notifyle option. */
bool shopt_notifyle;
#endif

/* Set if commands are read from argument and stdin respectively.
 * Correspond to -c and -s options respectively. */
bool shopt_read_arg, shopt_read_stdin;

/* The value of special parameter $0. */
const wchar_t *command_name;

/* If set, any variable is exported when assigned.
 * Corresponds to -a/--allexport option. */
bool shopt_allexport;
/* If set, when a function is defined, all the commands in the function
 * are hashed. Corresponds to -h/--hashondef option. */
bool shopt_hashondef;

/* If set, when a command returns a non-zero status, exit the shell.
 * Corresponds to -e/--errexit option. */
bool shopt_errexit;
/* If set, treat an expansion of undefined parameter as an error.
 * Corresponds to -u/--nounset option. */
bool shopt_nounset;
/* If set, don't execute any command; just do syntax checking.
 * Corresponds to -n/--noexec option. */
bool shopt_noexec;
/* If set, don't exit when EOF is entered.
 * Corresponds to --ignoreeof option. */
bool shopt_ignoreeof;
/* If set, echo the input to the shell.
 * Corresponds to -v/--verbose option. */
bool shopt_verbose;
/* If set, print the trace of each command executed and variable assigned.
 * Corresponds to -x/--xtrace option. */
bool shopt_xtrace;
/* If set, a new asynchronous job is set to the current job when invoked. */
bool shopt_curasync;
/* If set, the shell changes the working directory when a command is a
 * directory rather than a executable. */
bool shopt_autocd;

/* If set, don't perform filename expansions.
 * Corresponds to -f/--noglob option. */
bool shopt_noglob;
/* Correspond to WGLB_CASEFOLD, WGLB_PERIOD, WGLB_MARK, WGLB_RECDIR resp. */
bool shopt_nocaseglob, shopt_dotglob, shopt_markdirs, shopt_extendedglob;
/* If set, globbing pattern is removed from command line rather than left
 * intact when there are no matches.
 * Corresponds to --nullglob option. */
bool shopt_nullglob;
/* If set, perform brace expansion.
 * Corresponds to --brace option.*/
bool shopt_braceexpand;
/* If set, prevent redirections from overwriting existent file.
 * Corresponds to -C/--noclobber option. */
bool shopt_noclobber;

#if YASH_ENABLE_LINEEDIT
/* Type of line editing keybind. */
enum shopt_lineedit_T shopt_lineedit = shopt_nolineedit;
/* Defines treatment of the 8th bit of input characters. */
enum shopt_yesnoauto_T shopt_le_convmeta = shopt_auto;
/* If set, a special character sequence is printed when starting line-editing
 * to make sure the prompt starts at the beginning of line. */
bool shopt_le_promptsp = true;
#endif


typedef enum shopt_index_T {
    SHOPT_INTERACTIVE, SHOPT_LOGIN, SHOPT_NOPROFILE, SHOPT_NORCFILE,
    SHOPT_RCFILE, SHOPT_VERSION,
    SHOPT_ALLEXPORT, SHOPT_HASHONDEF, SHOPT_NOCLOBBER, SHOPT_NOGLOB,
    SHOPT_NOCASEGLOB, SHOPT_DOTGLOB, SHOPT_MARKDIRS, SHOPT_EXTENDEDGLOB,
    SHOPT_NULLGLOB, SHOPT_BRACEEXPAND, SHOPT_CURASYNC, SHOPT_AUTOCD,
    SHOPT_ERREXIT, SHOPT_NOUNSET, SHOPT_NOEXEC, SHOPT_IGNOREEOF, SHOPT_VERBOSE,
    SHOPT_XTRACE, SHOPT_NOLOG, SHOPT_MONITOR, SHOPT_NOTIFY, SHOPT_NOTIFYLE,
    SHOPT_POSIX, SHOPT_VI, /* SHOPT_EMACS, */ SHOPT_LE_CONVMETA,
    SHOPT_LE_PROMPTSP, SHOPT_HELP,
    SHOPT_setopt = SHOPT_ALLEXPORT,
} shopt_index_T;


/* Long options for the shell and set builtin */
static const struct xoption long_options[] = {
    [SHOPT_INTERACTIVE]  = { L"interactive",  xno_argument, L'i', },
    [SHOPT_LOGIN]        = { L"login",        xno_argument, L'l', },
    [SHOPT_NOPROFILE]    = { L"noprofile",    xno_argument, L'(', },
    [SHOPT_NORCFILE]     = { L"norcfile",     xno_argument, L')', },
    [SHOPT_RCFILE]       = { L"rcfile",       xrequired_argument, L'!', },
    [SHOPT_VERSION]      = { L"version",      xno_argument, L'V', },
    /* Options above cannot be used in set builtin */
    [SHOPT_ALLEXPORT]    = { L"allexport",    xno_argument, L'L', },
    [SHOPT_HASHONDEF]    = { L"hashondef",    xno_argument, L'L', },
    [SHOPT_NOCLOBBER]    = { L"noclobber",    xno_argument, L'L', },
    [SHOPT_NOGLOB]       = { L"noglob",       xno_argument, L'L', },
    [SHOPT_NOCASEGLOB]   = { L"nocaseglob",   xno_argument, L'L', },
    [SHOPT_DOTGLOB]      = { L"dotglob",      xno_argument, L'L', },
    [SHOPT_MARKDIRS]     = { L"markdirs",     xno_argument, L'L', },
    [SHOPT_EXTENDEDGLOB] = { L"extendedglob", xno_argument, L'L', },
    [SHOPT_NULLGLOB]     = { L"nullglob",     xno_argument, L'L', },
    [SHOPT_BRACEEXPAND]  = { L"braceexpand",  xno_argument, L'L', },
    [SHOPT_CURASYNC]     = { L"curasync",     xno_argument, L'L', },
    [SHOPT_AUTOCD]       = { L"autocd",       xno_argument, L'L', },
    [SHOPT_ERREXIT]      = { L"errexit",      xno_argument, L'L', },
    [SHOPT_NOUNSET]      = { L"nounset",      xno_argument, L'L', },
    [SHOPT_NOEXEC]       = { L"noexec",       xno_argument, L'L', },
    [SHOPT_IGNOREEOF]    = { L"ignoreeof",    xno_argument, L'L', },
    [SHOPT_VERBOSE]      = { L"verbose",      xno_argument, L'L', },
    [SHOPT_XTRACE]       = { L"xtrace",       xno_argument, L'L', },
    [SHOPT_NOLOG]        = { L"nolog",        xno_argument, L'L', },
    [SHOPT_MONITOR]      = { L"monitor",      xno_argument, L'm', },
    [SHOPT_NOTIFY]       = { L"notify",       xno_argument, L'L', },
    [SHOPT_NOTIFYLE]     = { L"notifyle",     xno_argument, L'L', },
    [SHOPT_POSIX]        = { L"posix",        xno_argument, L'L', },
#if YASH_ENABLE_LINEEDIT
    [SHOPT_VI]           = { L"vi",           xno_argument, L'L', },
    // [SHOPT_EMACS]        = { L"emacs",        xno_argument, L'L', },
    [SHOPT_LE_CONVMETA]  = { L"le-convmeta",  xrequired_argument, L'L', },
    [SHOPT_LE_PROMPTSP]  = { L"le-promptsp",  xno_argument, L'L', },
#endif
    [SHOPT_HELP]         = { L"help",         xno_argument, L'-', },
    /* this one must be the last for `help_option' */
    { NULL, 0, 0, },
};

static const struct setoptinfo_T setoptinfo[] = {
    [SHOPT_ALLEXPORT]    = { set_bool_option, &shopt_allexport, },
    [SHOPT_HASHONDEF]    = { set_bool_option, &shopt_hashondef, },
    [SHOPT_NOCLOBBER]    = { set_bool_option, &shopt_noclobber, },
    [SHOPT_NOGLOB]       = { set_bool_option, &shopt_noglob, },
    [SHOPT_NOCASEGLOB]   = { set_bool_option, &shopt_nocaseglob, },
    [SHOPT_DOTGLOB]      = { set_bool_option, &shopt_dotglob, },
    [SHOPT_MARKDIRS]     = { set_bool_option, &shopt_markdirs, },
    [SHOPT_EXTENDEDGLOB] = { set_bool_option, &shopt_extendedglob, },
    [SHOPT_NULLGLOB]     = { set_bool_option, &shopt_nullglob, },
    [SHOPT_BRACEEXPAND]  = { set_bool_option, &shopt_braceexpand, },
    [SHOPT_CURASYNC]     = { set_bool_option, &shopt_curasync, },
    [SHOPT_AUTOCD]       = { set_bool_option, &shopt_autocd, },
    [SHOPT_ERREXIT]      = { set_bool_option, &shopt_errexit, },
    [SHOPT_NOUNSET]      = { set_bool_option, &shopt_nounset, },
    [SHOPT_NOEXEC]       = { set_bool_option, &shopt_noexec, },
    [SHOPT_IGNOREEOF]    = { set_bool_option, &shopt_ignoreeof, },
    [SHOPT_VERBOSE]      = { set_bool_option, &shopt_verbose, },
    [SHOPT_XTRACE]       = { set_bool_option, &shopt_xtrace, },
    [SHOPT_NOLOG]        = { set_ignore_option, NULL, },
    [SHOPT_MONITOR]      = { set_monitor_option, NULL, },
    [SHOPT_NOTIFY]       = { set_bool_option, &shopt_notify, },
#if YASH_ENABLE_LINEEDIT
    [SHOPT_NOTIFYLE]     = { set_bool_option, &shopt_notifyle, },
#endif
    [SHOPT_POSIX]        = { set_bool_option, &posixly_correct, },
#if YASH_ENABLE_LINEEDIT
    [SHOPT_VI]           = { set_lineedit_option, NULL, },
    //[SHOPT_EMACS]        = { set_lineedit_option, NULL, },
    [SHOPT_LE_CONVMETA]  = { set_yesnoauto_option, &shopt_le_convmeta, },
    [SHOPT_LE_PROMPTSP]  = { set_bool_option, &shopt_le_promptsp, },
#endif
    //[SHOPT_HELP]
    { 0, NULL, },
};

const struct xoption *const shell_long_options = long_options;
const struct xoption *const set_long_options   = long_options + SHOPT_setopt;
const struct xoption *const help_option        = long_options + SHOPT_HELP;

/* The index of the currently parsing option in the `long_options' and
 * `setoptinfo' arrays. */
int setoptindex;


/* Sets the option specified by `setoptindex'. */
void set_option(void)
{
    const struct setoptinfo_T *info = &setoptinfo[setoptindex];
    info->func(info->argp);
}

/* Sets the option specified by the character `c'.
 * An unrecognized character is ignored. */
void set_single_option(wchar_t c)
{
    switch (c) {
	case L'a':  setoptindex = SHOPT_ALLEXPORT;  break;
	case L'h':  setoptindex = SHOPT_HASHONDEF;  break;
	case L'C':  setoptindex = SHOPT_NOCLOBBER;  break;
	case L'f':  setoptindex = SHOPT_NOGLOB;     break;
	case L'e':  setoptindex = SHOPT_ERREXIT;    break;
	case L'u':  setoptindex = SHOPT_NOUNSET;    break;
	case L'n':  setoptindex = SHOPT_NOEXEC;     break;
	case L'v':  setoptindex = SHOPT_VERBOSE;    break;
	case L'x':  setoptindex = SHOPT_XTRACE;     break;
	case L'm':  setoptindex = SHOPT_MONITOR;    break;
	case L'b':  setoptindex = SHOPT_NOTIFY;     break;
	default:    return;
    }
    set_option();
}

/* Switches the setting of a specified long option.
 * Options that can only be used in shell invocation are ignored.
 * Returns true if successful, false for invalid options. */
bool set_long_option(const wchar_t *s)
{
    for (const struct xoption *optp = set_long_options; optp->name; optp++) {
	if (wcscmp(s, optp->name) == 0) {
	    if (optp->has_arg == xno_argument) {
		setoptindex = optp - long_options;
		set_option();
		return true;
	    } else {
		return false;
	    }
	}
    }
    return false;
}

/* This function does nothing. */
void set_ignore_option(void *argp __attribute__((unused))) { }

/* Changes the setting of a boolean option according to the current `xoptopt'.
 * `argp' must be a pointer to a boolean value. */
void set_bool_option(void *argp)
{
    bool *optp = argp;
    *optp = (xoptopt == L'-');
}

#if YASH_ENABLE_LINEEDIT

/* Changes the setting of a yes-no-auto option according to the current `xoptarg'.
 * `argp' must be a pointer to a yes-no-auto value. */
void set_yesnoauto_option(void *argp)
{
    enum shopt_yesnoauto_T *optp = argp;
    
    assert(xoptarg != NULL);
    switch (xoptarg[0]) {
	case L'Y':  case L'y':  *optp = shopt_yes;   break;
	case L'N':  case L'n':  *optp = shopt_no;    break;
	case L'A':  case L'a':  *optp = shopt_auto;  break;
    }
}

#endif

/* Changes the setting of the `-m' (--monitor) option.
 * This function's behavior depends on the value of `shell_pid'. */
void set_monitor_option(void *argp __attribute__((unused)))
{
    bool newvalue = (xoptopt == L'-');
    if (shell_pid == 0) {
	do_job_control = newvalue;
    } else {
	if (newvalue != do_job_control) {
	    reset_own_pgid();
	    do_job_control = newvalue;
	    if (newvalue && ttyfd < 0)
		open_ttyfd();
	    set_signals();
	    set_own_pgid();
	}
    }
    /* When `shell_pid' is zero, the shell is under initialization;
     * `set_own_pgid' and other functions are called later in the `main'
     * function. */
}

#if YASH_ENABLE_LINEEDIT

/* Changes the setting of the vi/emacs option. */
void set_lineedit_option(void *argp __attribute__((unused)))
{
    enum shopt_lineedit_T opt;

    switch (setoptindex) {
	case SHOPT_VI:     opt = shopt_vi;     break;
	//case SHOPT_EMACS:  opt = shopt_emacs;  break;
	default:           return;
    }
    if (xoptopt == L'-') {
	shopt_lineedit = opt;
	/* turn on lineedit */
    } else {
	/* turn off lineedit */
	if (shopt_lineedit == opt)
	    shopt_lineedit = shopt_nolineedit;
    }
}

#endif /* YASH_ENABLE_LINEEDIT */

/* Return current value of special parameter $- as a newly malloced string. */
wchar_t *get_hyphen_parameter(void)
{
    xwcsbuf_T buf;
    wb_init(&buf);

    if (shopt_allexport)   wb_wccat(&buf, L'a');
    if (shopt_notify)      wb_wccat(&buf, L'b');
    if (shopt_read_arg)    wb_wccat(&buf, L'c');
    if (shopt_errexit)     wb_wccat(&buf, L'e');
    if (shopt_noglob)      wb_wccat(&buf, L'f');
    if (shopt_hashondef)   wb_wccat(&buf, L'h');
    if (is_interactive)    wb_wccat(&buf, L'i');
    if (do_job_control)    wb_wccat(&buf, L'm');
    if (shopt_noexec)      wb_wccat(&buf, L'n');
    if (shopt_read_stdin)  wb_wccat(&buf, L's');
    if (shopt_nounset)     wb_wccat(&buf, L'u');
    if (shopt_verbose)     wb_wccat(&buf, L'v');
    if (shopt_xtrace)      wb_wccat(&buf, L'x');
    if (shopt_noclobber)   wb_wccat(&buf, L'C');

    return wb_towcs(&buf);
}


/********** Builtin **********/

static void set_builtin_print_current_settings(void);
static void set_builtin_print_restoring_commands(void);

int set_builtin(int argc, void **argv)
{
    wchar_t opt;

    if (argc <= 1) {
	return typeset_builtin(argc, argv);
    }
    if (argc == 2) {
	if (wcscmp(ARGV(1), L"-o") == 0) {
	    set_builtin_print_current_settings();
	    return Exit_SUCCESS;
	} else if (wcscmp(ARGV(1), L"+o") == 0) {
	    set_builtin_print_restoring_commands();
	    return Exit_SUCCESS;
	}
    }

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv,
		    L"+*o:" SHELLSET_OPTIONS,
		    set_long_options,
		    &setoptindex)))
    {
	switch (opt) {
	    case L'L':
		setoptindex += SHOPT_setopt;
		set_option();
		break;
	    case L'o':
		if (!set_long_option(xoptarg)) {
		    xerror(0, Ngt("%lco %ls: invalid option"),
			    (wint_t) xoptopt, xoptarg);
		    goto optionerror;
		}
		break;
	    case L'-':
		print_builtin_help(ARGV(0));
		return Exit_SUCCESS;
	    case L'?':  optionerror:
		fprintf(stderr,
		    gt("Usage:  set [-abefhmnuvxC] [-o option] [+o option] "
			"[arg...]\n"));
		SPECIAL_BI_ERROR;
		return Exit_ERROR;
	    default:
		set_single_option(opt);
		break;
	}
    }

    /* set positional parameters */
    if (xoptind < argc ||
	    (xoptind == argc && wcscmp(ARGV(xoptind - 1), L"--") == 0))
	set_positional_parameters(argv + xoptind);

    return Exit_SUCCESS;
}

void set_builtin_print_current_settings(void)
{
    const char *vals[] = {
	[shopt_yes]  = gt("yes"),
	[shopt_no]   = gt("no"),
	[shopt_auto] = gt("auto"),
    };
#define PRINTSETTING(name,value) \
    printf("%-15ls %s\n", L"" #name, (value) ? vals[shopt_yes] : vals[shopt_no])
#define PRINTSETTING_YNA(name,value) \
    printf("%-15ls %s\n", L"" #name, vals[value])

    PRINTSETTING(allexport, shopt_allexport);
    PRINTSETTING(autocd, shopt_autocd);
    PRINTSETTING(braceexpand, shopt_braceexpand);
    PRINTSETTING(curasync, shopt_curasync);
    PRINTSETTING(dotglob, shopt_dotglob);
#if YASH_ENABLE_LINEEDIT
    //PRINTSETTING(emacs, shopt_lineedit == shopt_emacs);
#endif
    PRINTSETTING(errexit, shopt_errexit);
    PRINTSETTING(extendedglob, shopt_extendedglob);
    PRINTSETTING(hashondef, shopt_hashondef);
    PRINTSETTING(ignoreeof, shopt_ignoreeof);
    PRINTSETTING(interactive, is_interactive);
#if YASH_ENABLE_LINEEDIT
    PRINTSETTING_YNA(le-convmeta, shopt_le_convmeta);
    PRINTSETTING(le-promptsp, shopt_le_promptsp);
#endif
    PRINTSETTING(login, is_login_shell);
    PRINTSETTING(markdirs, shopt_markdirs);
    PRINTSETTING(monitor, do_job_control);
    PRINTSETTING(nocaseglob, shopt_nocaseglob);
    PRINTSETTING(noclobber, shopt_noclobber);
    PRINTSETTING(noexec, shopt_noexec);
    PRINTSETTING(noglob, shopt_noglob);
    //PRINTSETTING(nolog, shopt_nolog);
    PRINTSETTING(notify, shopt_notify);
#if YASH_ENABLE_LINEEDIT
    PRINTSETTING(notifyle, shopt_notifyle);
#endif
    PRINTSETTING(nounset, shopt_nounset);
    PRINTSETTING(nullglob, shopt_nullglob);
    PRINTSETTING(posix, posixly_correct);
    PRINTSETTING(verbose, shopt_verbose);
#if YASH_ENABLE_LINEEDIT
    PRINTSETTING(vi, shopt_lineedit == shopt_vi);
#endif
    PRINTSETTING(xtrace, shopt_xtrace);
#undef PRINTSETTING
#undef PRINTSETTING_YNA
}

void set_builtin_print_restoring_commands(void)
{
#define PRINTSETTING(name,value) \
    printf("set %co %ls\n", (value) ? '-' : '+', L"" #name)

    PRINTSETTING(allexport, shopt_allexport);
    PRINTSETTING(autocd, shopt_autocd);
    PRINTSETTING(braceexpand, shopt_braceexpand);
    PRINTSETTING(curasync, shopt_curasync);
    PRINTSETTING(dotglob, shopt_dotglob);
#if YASH_ENABLE_LINEEDIT
    //PRINTSETTING(emacs, shopt_lineedit == shopt_emacs);
#endif
    PRINTSETTING(errexit, shopt_errexit);
    PRINTSETTING(extendedglob, shopt_extendedglob);
    PRINTSETTING(hashondef, shopt_hashondef);
    PRINTSETTING(ignoreeof, shopt_ignoreeof);
    //PRINTSETTING(interactive, is_interactive);
#if YASH_ENABLE_LINEEDIT
    PRINTSETTING(le-promptsp, shopt_le_promptsp);
#endif
    //PRINTSETTING(login, is_login_shell);
    PRINTSETTING(markdirs, shopt_markdirs);
    PRINTSETTING(monitor, do_job_control);
    PRINTSETTING(nocaseglob, shopt_nocaseglob);
    PRINTSETTING(noclobber, shopt_noclobber);
    PRINTSETTING(noexec, shopt_noexec);
    PRINTSETTING(noglob, shopt_noglob);
    //PRINTSETTING(nolog, shopt_nolog);
    PRINTSETTING(notify, shopt_notify);
#if YASH_ENABLE_LINEEDIT
    PRINTSETTING(notifyle, shopt_notifyle);
#endif
    PRINTSETTING(nounset, shopt_nounset);
    PRINTSETTING(nullglob, shopt_nullglob);
    PRINTSETTING(posix, posixly_correct);
    PRINTSETTING(verbose, shopt_verbose);
#if YASH_ENABLE_LINEEDIT
    PRINTSETTING(vi, shopt_lineedit == shopt_vi);
#endif
    PRINTSETTING(xtrace, shopt_xtrace);
#undef PRINTSETTING
}

const char set_help[] = Ngt(
"set - set shell options and positional parameters\n"
"\tset [-abefhmnuvxC] [+abefhmnuvxC] [-o option] [+o option] [--] [arg...]\n"
"\tset -o\n"
"\tset +o\n"
"The first form enables or disables the specified shell options and/or sets\n"
"positional parameters to <arg>s. If no <arg>s are given, the positional\n"
"parameters are not changed.\n"
"If no options or <arg>s are given at all, a list of all variables currently\n"
"defined in the shell is printed. To clear all positional parameters, use\n"
"`set --'.\n"
"The second form prints the current settings of the shell options in a human-\n"
"readable form.\n"
"The third form prints commands that can be used to restore the current\n"
"option settings later.\n"
"Below are the available options:\n"
" -a --allexport\n"
"\tAny variable is exported when assigned.\n"
" -b --notify\n"
"\tWhen the status of a job is changed, it is notified immediately.\n"
" --notifyle\n"
"\tSimilar to -b, but only notify during line-editing. Ignored if -b is\n"
"\tset.\n"
" -e --errexit\n"
"\tExit the shell immediately when any simple command returns a\n"
"\tnon-zero status.\n"
" -f --noglob\n"
"\tDisable pathname expansion (globbing).\n"
" --nocaseglob\n"
"\tPerform pathname expansion case-insensitively.\n"
" --dotglob\n"
"\tIn pathname expansion, '*' and '?' match a '.' at the beginning of\n"
"\tthe filename.\n"
" --markdirs\n"
"\tIn pathname expansion, pathnames expanded to directories have a '/'\n"
"\tat the end of the name.\n"
" --extendedglob\n"
"\tEnable extended pathname expansion.\n"
" --nullglob\n"
"\tIn pathname expansion, patterns that do not match any pathname are\n"
"\tremoved from the command line rather than left as is.\n"
" -h --hashondef\n"
"\tWhen a function is defined, all the commands in the function are\n"
"\tregistered in the command path cache.\n"
" -m --monitor\n"
"\tEnable job control. All jobs are run in their own process group.\n"
"\tWhen the status of a job is changed, the status is reported before\n"
"\tthe next prompt. This option is enabled by default for interactive\n"
"\tshells.\n"
" -n --noexec\n"
"\tCommands are parsed, but not executed.\n"
"\tUseful for syntax checking of a shell script file.\n"
" -u --nounset\n"
"\tExpanding an undefined variable causes an error rather than\n"
"\texpanding to an empty string.\n"
" -v --verbose\n"
"\tEcho each command to the standard error before execution.\n"
" -x --xtrace\n"
"\tAfter each command line is expanded, the expanded line is printed\n"
"\tto the standard error.\n"
" -C --noclobber\n"
"\tPrevent existent files from being overridden by the \">\"\n"
"\tredirection.\n"
" --ignoreeof\n"
"\tDo not exit when an EOF is entered.\n"
"\tThis option is effective in interactive shells only.\n"
" --braceexpand\n"
"\tEnable brace expansion.\n"
" --curasync\n"
"\tWhen a new background job is invoked, it becomes the current job.\n"
" --autocd\n"
"\tIf a simple command cannot be executed but it is a directory name,\n"
"\tthe shell performs the `cd' command to that directory.\n"
" --posix\n"
"\tMake the shell behave as the POSIX shell.\n"
" --vi\n"
"\tEnable vi-like editing.\n"
//" --emacs\n"
//"\tEnable emacs-like editing.\n"
" --le-convmeta=<yes|no|auto>\n"
"\tTreat 8th bit of input as a meta-key flag.\n"
" --le-promptsp\n"
"\tMove cursor to beginning of line each time when starting\n"
"\tline-editing. (enabled by default)\n"
"To disable options, put '+' before the option characters instead of '-'.\n"
"Long options in the form of `--xxx' are equivalent to `-o xxx'.\n"
"Use `+o xxx' to turn off a long option. You cannot use `+-xxx' or `++xxx'.\n"
);


/* vim: set ts=8 sts=4 sw=4 noet: */
