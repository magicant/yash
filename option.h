/* Yash: yet another shell */
/* option.h: option settings */
/* (C) 2007-2010 magicant */

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


enum shopt_lineedit_T {
    shopt_nolineedit,
    shopt_vi,
    shopt_emacs,
};
enum shopt_yesnoauto_T {
    shopt_yes,
    shopt_no,
    shopt_auto,
};

extern _Bool posixly_correct;
extern _Bool is_login_shell;
extern _Bool is_interactive, is_interactive_now;
extern _Bool do_job_control, shopt_notify, shopt_notifyle;
extern _Bool shopt_read_arg, shopt_read_stdin;
extern const wchar_t *command_name;
extern _Bool shopt_allexport, shopt_hashondef;
extern _Bool shopt_errexit, shopt_nounset, shopt_noexec, shopt_ignoreeof,
       shopt_verbose, shopt_xtrace, shopt_curasync, shopt_curbg, shopt_curstop,
       shopt_histspace;
extern _Bool shopt_noglob, shopt_nocaseglob, shopt_dotglob, shopt_markdirs,
       shopt_extendedglob, shopt_nullglob;
extern _Bool shopt_braceexpand;
extern _Bool shopt_noclobber;
extern enum shopt_lineedit_T shopt_lineedit;
extern enum shopt_yesnoauto_T shopt_le_convmeta;
extern _Bool shopt_le_visiblebell, shopt_le_promptsp, shopt_le_alwaysrp,
       shopt_le_compdebug;

#define SHELLSET_OPTIONS L"abefhmnuvxC"

extern const struct xoption
    *const shell_long_options, *const set_long_options,
    *const all_option, *const help_option;
extern int setoptindex;

extern _Bool set_option(void);
extern void set_single_option(wchar_t c);
extern _Bool set_long_option(const wchar_t *s)
    __attribute__((nonnull));
extern wchar_t *get_hyphen_parameter(void)
    __attribute__((malloc,warn_unused_result));

/* Whether or not this shell process is doing job control right now. */
#define doing_job_control_now  (do_job_control && ttyfd >= 0)

extern int set_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern const char *set_help[];


#endif /* YASH_OPTION_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
