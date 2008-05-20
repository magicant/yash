/* Yash: yet another shell */
/* option.h: option settings */
/* © 2007-2008 magicant */

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


#ifndef OPTION_H
#define OPTION_H

#include <stdbool.h>
#include <stddef.h>


extern bool posixly_correct;
extern bool is_login_shell;
extern bool is_interactive, is_interactive_now;
extern bool do_job_control, shopt_notify;
extern bool shopt_read_arg, shopt_read_stdin;
extern const char *command_name;
extern bool shopt_errexit, shopt_nounset, shopt_noexec, shopt_ignoreeof,
       shopt_verbose;
extern bool shopt_noglob, shopt_nocaseglob, shopt_dotglob, shopt_markdirs,
       shopt_extendedglob, shopt_nullglob;
extern bool shopt_braceexpand;
extern bool shopt_noclobber;

#define SHELLSET_OPTIONS "befmnuvC"

extern const struct xoption *const shell_long_options, *const set_long_options;

extern void set_option(char c);
extern bool set_long_option(const char *s)
    __attribute__((nonnull));
extern wchar_t *get_hyphen_parameter(void)
    __attribute__((malloc,warn_unused_result));

/* 今現在ジョブ制御が有効であるかどうか */
#define doing_job_control_now  (do_job_control && ttyfd >= 0)


#endif /* OPTION_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
