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
const char *command_name;

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
    { "interactive",  xno_argument, NULL, 'i', },
    { "help",         xno_argument, NULL, '!', },
    { "version",      xno_argument, NULL, 'V', },
    { "login",        xno_argument, NULL, 'l', },
    /* Options above cannot be used in set builtin */
    { "allexport",    xno_argument, NULL, 'a', },
    { "hashondef",    xno_argument, NULL, 'h', },
    { "noclobber",    xno_argument, NULL, 'C', },
    { "noglob",       xno_argument, NULL, 'f', },
    { "nocaseglob",   xno_argument, NULL, 'c', },
    { "dotglob",      xno_argument, NULL, 'D', },
    { "markdirs",     xno_argument, NULL, 'M', },
    { "extendedglob", xno_argument, NULL, 'E', },
    { "nullglob",     xno_argument, NULL, 'N', },
    { "braceexpand",  xno_argument, NULL, 'B', },
    { "errexit",      xno_argument, NULL, 'e', },
    { "nounset",      xno_argument, NULL, 'u', },
    { "noexec",       xno_argument, NULL, 'n', },
    { "ignoreeof",    xno_argument, NULL, 'I', },
    { "verbose",      xno_argument, NULL, 'v', },
    { "xtrace",       xno_argument, NULL, 'x', },
    { "monitor",      xno_argument, NULL, 'm', },
    { "notify",       xno_argument, NULL, 'b', },
    { "posix",        xno_argument, NULL, 'X', },
    { NULL,           0,            NULL, 0,   },
};

const struct xoption *const shell_long_options = long_options;
const struct xoption *const set_long_options   = long_options + 4;

// TODO option: unimplemented options: -o{nolog,vi,emacs}


/* Switches a one-character option depending on whether `xoptopt' is '-' or not.
 * Option characters that can only be used in shell invokation are ignored. */
void set_option(char c)
{
    bool value = (xoptopt == '-');
    switch (c) {
	case 'a':   shopt_allexport    = value;   break;
	case 'h':   shopt_hashondef    = value;   break;
	case 'C':   shopt_noclobber    = value;   break;
	case 'f':   shopt_noglob       = value;   break;
	case 'c':   shopt_nocaseglob   = value;   break;
	case 'D':   shopt_dotglob      = value;   break;
	case 'M':   shopt_markdirs     = value;   break;
	case 'E':   shopt_extendedglob = value;   break;
	case 'N':   shopt_nullglob     = value;   break;
	case 'B':   shopt_braceexpand  = value;   break;
	case 'e':   shopt_errexit      = value;   break;
	case 'u':   shopt_nounset      = value;   break;
	case 'n':   shopt_noexec       = value;   break;
	case 'I':   shopt_ignoreeof    = value;   break;
	case 'v':   shopt_verbose      = value;   break;
	case 'x':   shopt_xtrace       = value;   break;
	case 'm':   do_job_control     = value;   break;
	case 'b':   shopt_notify       = value;   break;
	case 'X':   posixly_correct    = value;   break;
    }
}

/* Switches the setting of a specified long option according to `xoptopt'.
 * Options that can only be used in shell invokation are ignored.
 * Returns true if successful, false for invalid options. */
bool set_long_option(const char *s)
{
    const struct xoption *opt = set_long_options;

    while (opt->name) {
	if (strcmp(s, opt->name) == 0) {
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
