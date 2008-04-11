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


extern bool posixly_correct;
extern bool is_login_shell;
extern bool is_interactive, is_interactive_now;
extern bool do_job_control;
extern const char *command_name;
extern bool shopt_noglob, shopt_nocaseglob, shopt_dotglob, shopt_markdirs,
       shopt_extendedglob, shopt_nullglob;

#define SHELLSET_OPTIONS "fm"

extern const struct xoption *const shell_long_options, *const set_long_options;

extern void set_option(char c);


#endif /* OPTION_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
