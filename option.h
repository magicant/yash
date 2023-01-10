/* Yash: yet another shell */
/* option.h: option settings */
/* (C) 2007-2018 magicant */

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


#ifndef YASH_OPTION_H
#define YASH_OPTION_H

#include <stddef.h>
#include "xgetopt.h"


enum shopt_lineedit_T {
    SHOPT_NOLINEEDIT,
    SHOPT_VI,
    SHOPT_EMACS,
};
enum shopt_yesnoauto_T {
    SHOPT_YES,
    SHOPT_NO,
    SHOPT_AUTO,
};

extern const wchar_t *command_name;
extern _Bool posixly_correct;
extern _Bool is_login_shell;
extern _Bool is_interactive, is_interactive_now;
extern _Bool shopt_cmdline, shopt_stdin;
extern _Bool do_job_control, shopt_notify, shopt_notifyle,
       shopt_curasync, shopt_curbg, shopt_curstop;
extern _Bool shopt_allexport, shopt_hashondef, shopt_forlocal;
extern _Bool shopt_errexit, shopt_errreturn, shopt_pipefail, shopt_unset,
       shopt_exec, shopt_ignoreeof, shopt_verbose, shopt_xtrace;
extern _Bool shopt_traceall;
#if YASH_ENABLE_HISTORY
extern _Bool shopt_histspace;
#endif
extern _Bool shopt_glob, shopt_caseglob, shopt_dotglob, shopt_markdirs,
       shopt_extendedglob, shopt_nullglob;
extern _Bool shopt_braceexpand;
extern _Bool shopt_emptylastfield;
extern _Bool shopt_clobber;
#if YASH_ENABLE_LINEEDIT
extern enum shopt_lineedit_T shopt_lineedit;
extern enum shopt_yesnoauto_T shopt_le_convmeta;
extern _Bool shopt_le_visiblebell, shopt_le_promptsp, shopt_le_alwaysrp,
    shopt_le_predict, shopt_le_predictempty, shopt_le_compdebug,
    shopt_le_trimright;
#endif

/* Whether or not this shell process is doing job control right now. */
#define doing_job_control_now  (do_job_control && ttyfd >= 0)

struct shell_invocation_T {
    _Bool help, version;
    _Bool noprofile, norcfile;
    const wchar_t *profile, *rcfile;
    _Bool is_interactive_set, do_job_control_set, lineedit_set;
};

extern int parse_shell_options(int argc, void *const *argv,
	struct shell_invocation_T *shell_invocation)
    __attribute__((nonnull(2),warn_unused_result));
#if YASH_ENABLE_LINEEDIT
extern void set_lineedit_option(enum shopt_lineedit_T v);
#endif
extern wchar_t *get_hyphen_parameter(void)
    __attribute__((malloc,warn_unused_result));
#if YASH_ENABLE_TEST
extern _Bool is_valid_option_name(const wchar_t *s)
    __attribute__((nonnull,pure));
extern _Bool option_is_enabled(const wchar_t *s)
    __attribute__((nonnull,pure));
#endif
#if YASH_ENABLE_HELP
extern _Bool print_shopts_body(_Bool include_normal_options);
#endif

extern const struct xgetopt_T all_help_options[];
#define help_option (&all_help_options[1])

extern int set_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char set_help[], set_syntax[];
#endif


#endif /* YASH_OPTION_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
