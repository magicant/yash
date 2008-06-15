/* Yash: yet another shell */
/* option.c: option settings */
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
#include <stdbool.h>
#include <string.h>
#include "option.h"
#include "util.h"
#include "strbuf.h"


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


/* Long options for the shell and set builtin */
static const struct xoption long_options[] = {
    { L"interactive",  xno_argument, L'i', },
    { L"help",         xno_argument, L'-', },
    { L"version",      xno_argument, L'V', },
    { L"login",        xno_argument, L'l', },
    { L"noprofile",    xno_argument, L'(', },
    { L"norcfile",     xno_argument, L')', },
    { L"rcfile",       xrequired_argument, L'!', },
    /* Options above cannot be used in set builtin */
    { L"allexport",    xno_argument, L'a', },
    { L"hashondef",    xno_argument, L'h', },
    { L"noclobber",    xno_argument, L'C', },
    { L"noglob",       xno_argument, L'f', },
    { L"nocaseglob",   xno_argument, L'c', },
    { L"dotglob",      xno_argument, L'D', },
    { L"markdirs",     xno_argument, L'M', },
    { L"extendedglob", xno_argument, L'E', },
    { L"nullglob",     xno_argument, L'N', },
    { L"braceexpand",  xno_argument, L'B', },
    { L"errexit",      xno_argument, L'e', },
    { L"nounset",      xno_argument, L'u', },
    { L"noexec",       xno_argument, L'n', },
    { L"ignoreeof",    xno_argument, L'I', },
    { L"verbose",      xno_argument, L'v', },
    { L"xtrace",       xno_argument, L'x', },
    { L"monitor",      xno_argument, L'm', },
    { L"notify",       xno_argument, L'b', },
    { L"posix",        xno_argument, L'X', },
    { NULL,            0,            0,   },
};

const struct xoption *const shell_long_options = long_options;
const struct xoption *const set_long_options   = long_options + 4;

// TODO option: unimplemented options: -o{nolog,vi,emacs}


/* Switches a one-character option depending on whether `xoptopt' is '-' or not.
 * Option characters that can only be used in shell invokation are ignored. */
void set_option(wchar_t c)
{
    bool value = (xoptopt == L'-');
    switch (c) {
	case L'a':   shopt_allexport    = value;   break;
	case L'h':   shopt_hashondef    = value;   break;
	case L'C':   shopt_noclobber    = value;   break;
	case L'f':   shopt_noglob       = value;   break;
	case L'c':   shopt_nocaseglob   = value;   break;
	case L'D':   shopt_dotglob      = value;   break;
	case L'M':   shopt_markdirs     = value;   break;
	case L'E':   shopt_extendedglob = value;   break;
	case L'N':   shopt_nullglob     = value;   break;
	case L'B':   shopt_braceexpand  = value;   break;
	case L'e':   shopt_errexit      = value;   break;
	case L'u':   shopt_nounset      = value;   break;
	case L'n':   shopt_noexec       = value;   break;
	case L'I':   shopt_ignoreeof    = value;   break;
	case L'v':   shopt_verbose      = value;   break;
	case L'x':   shopt_xtrace       = value;   break;
	case L'm':   do_job_control     = value;   break;
	case L'b':   shopt_notify       = value;   break;
	case L'X':   posixly_correct    = value;   break;
    }
}

/* Switches the setting of a specified long option according to `xoptopt'.
 * Options that can only be used in shell invokation are ignored.
 * Returns true if successful, false for invalid options. */
bool set_long_option(const wchar_t *s)
{
    const struct xoption *opt = set_long_options;

    while (opt->name) {
	if (wcscmp(s, opt->name) == 0) {
	    set_option(opt->val);
	    return true;
	}
	opt++;
    }
    return false;
}

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


/* vim: set ts=8 sts=4 sw=4 noet: */
