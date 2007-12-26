/* Yash: yet another shell */
/* builtin.h: interface to shell builtin commands */
/* © 2007 magicant */

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


#ifndef BUILTIN_H
#define BUILTIN_H


typedef int cbody(int argc, char *const *argv);

void init_builtin(void);
cbody *get_builtin(const char *name);

int builtin_exit(int argc, char *const *argv);
int builtin_exec(int argc, char *const *argv);

#define OPT_HISTSIZE      "histsize"
#define OPT_HISTFILE      "histfile"
#define OPT_HISTFILESIZE  "histfilesize"
#define OPT_PS1           "ps1"
#define OPT_PS2           "ps2"
#define OPT_PROMPTCOMMAND "promptcommand"
#define OPT_HUPONEXIT     "huponexit"


#endif /* BUILTIN_H */
