/* Yash: yet another shell */
/* variable.h: interface to shell variable manager */
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


extern char **environ;

#define ENV_USER            "USER"
#define ENV_LOGNAME         "LOGNAME"
#define ENV_HOME            "HOME"
#define ENV_LANG            "LANG"
#define ENV_PATH            "PATH"
#define ENV_PWD             "PWD"
#define ENV_OLDPWD          "OLDPWD"
#define ENV_SHLVL           "SHLVL"
#define ENV_HOSTNAME        "HOSTNAME"
#define ENV_POSIXLY_CORRECT "POSIXLY_CORRECT"

char *xgetcwd(void);
void init_var(void);
void set_shlvl(int change);
const char *getvar(const char *name);
bool setvar(const char *name, const char *value, bool export);
bool is_exported(const char *name);
