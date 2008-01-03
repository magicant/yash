/* Yash: yet another shell */
/* yash.h: miscellaneous items */
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


#ifndef YASH_H
#define YASH_H

#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>


#define YASH_VERSION   "1.0 beta0"
#define YASH_COPYRIGHT "Copyright (C) 2007-2008 magicant"

#define ENV_USER            "USER"
#define ENV_LOGNAME         "LOGNAME"
#define ENV_HOME            "HOME"
#define ENV_LANG            "LANG"
#define ENV_PATH            "PATH"
#define ENV_PWD             "PWD"
#define ENV_SPWD            "SPWD"
#define ENV_OLDPWD          "OLDPWD"
#define ENV_SHLVL           "SHLVL"
#define ENV_HOSTNAME        "HOSTNAME"
#define ENV_POSIXLY_CORRECT "POSIXLY_CORRECT"

#define EXIT_NOEXEC   126
#define EXIT_NOTFOUND 127


extern bool is_loginshell;
extern bool is_interactive;
extern bool posixly_correct;

extern volatile sig_atomic_t sigint_received;

extern char *prompt_command;

int exec_file(const char *path, bool suppresserror)
	__attribute__((nonnull));
int exec_file_exp(const char *path, bool suppresserror)
	__attribute__((nonnull));
int exec_source(const char *code, const char *name)
	__attribute__((nonnull(1)));
void exec_source_and_exit(const char *code, const char *name)
	__attribute__((nonnull(1),noreturn));

void set_signals(void);
void unset_signals(void);
void wait_for_signal(void);
void handle_signals(void);
void set_unique_pgid(void);
void restore_pgid(void);
void forget_orig_pgrp(void);
void set_shell_env(void);
void unset_shell_env(void);

void yash_exit(int exitcode)
	__attribute__((noreturn));


#endif  /* YASH_H */
