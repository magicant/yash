/* Yash: yet another shell */
/* builtin.h: interface to shell builtin commands */
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


#ifndef BUILTIN_H
#define BUILTIN_H


typedef struct {
	int (*const main)(int argc, char **argv);
	const enum biflags {
		BI_SPECIAL     = 1 << 0,  /* 特殊組込みコマンド */
		BI_SEMISPECIAL = 1 << 1,  /* 準特殊組込みコマンド */
	} flags;
} BUILTIN;
/* (準)特殊組込みコマンドは、PATH になくても常に実行する。
 * posixly_correct のとき、それ以外の組込みコマンドは、
 * PATH に同等のコマンドがなければ実行しない。 */

extern struct hasht builtins;

void init_builtin(void);
void finalize_builtin(void);
BUILTIN *get_builtin(const char *name);

int builtin_true(int argc, char **argv);
int builtin_false(int argc, char **argv);
int builtin_cd(int argc, char **argv);
int builtin_pwd(int argc, char **argv);
int builtin_type(int argc, char **argv);
int builtin_umask(int argc, char **argv);
int builtin_history(int argc, char **argv);
int builtin_alias(int argc, char **argv);
int builtin_unalias(int argc, char **argv);
int builtin_option(int argc, char **argv);
int builtin_hash(int argc, char **argv);

int builtin_exit(int argc, char **argv);
int builtin_kill(int argc, char **argv);
int builtin_wait(int argc, char **argv);
int builtin_suspend(int argc, char **argv);
int builtin_jobs(int argc, char **argv);
int builtin_disown(int argc, char **argv);
int builtin_fg(int argc, char **argv);
int builtin_exec(int argc, char **argv);
int builtin_source(int argc, char **argv);
int builtin_eval(int argc, char **argv);

int builtin_export(int argc, char **argv);
int builtin_unset(int argc, char **argv);

#define OPT_HISTSIZE      "histsize"
#define OPT_HISTFILE      "histfile"
#define OPT_HISTFILESIZE  "histfilesize"
#define OPT_PS1           "ps1"
#define OPT_PS2           "ps2"
#define OPT_PROMPTCOMMAND "promptcommand"
#define OPT_HUPONEXIT     "huponexit"


#endif /* BUILTIN_H */
