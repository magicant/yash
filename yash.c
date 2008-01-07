/* Yash: yet another shell */
/* yash.c: basic functions of the shell */
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


#define _GNU_SOURCE
#include <error.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include "yash.h"
#include "util.h"
#include "sig.h"
#include "lineinput.h"
#include "parser.h"
#include "exec.h"
#include "path.h"
#include "builtin.h"
#include "alias.h"
#include "variable.h"
#include <assert.h>


int exec_file(const char *path, bool suppresserror);
int exec_file_exp(const char *path, bool suppresserror);
int exec_source(const char *code, const char *name);
void exec_source_and_exit(const char *code, const char *name);
void set_unique_pgid(void);
void restore_pgid(void);
void forget_orig_pgrp(void);
void set_shell_env(void);
void unset_shell_env(void);
static int exec_promptcommand(void);
static void interactive_loop(void) __attribute__((noreturn));
/* int main(int argc, char **argv); */
void print_help(void);
void print_version(void);
void yash_exit(int exitcode);

/* このプロセスがログインシェルなら非 0 */
bool is_loginshell;
/* 対話的シェルなら非 0 */
bool is_interactive;
/* POSIX の規定に厳密に従うなら非 0 */
bool posixly_correct;

/* コマンド名。特殊パラメータ $0 の値。 */
const char *command_name;
/* シェルのプロセス ID。サブシェルでも変わらない。 */
pid_t shell_pid;

/* プライマリプロンプトの前に実行するコマンド */
char *prompt_command = NULL;

/* 指定したファイルをシェルスクリプトとしてこのシェルの中で実行する
 * suppresserror: true なら、ファイルが読み込めなくてもエラーを出さない
 * 戻り値: エラーがなければ 0、エラーなら非 0。 */
int exec_file(const char *path, bool suppresserror)
{
	FILE *f = fopen(path, "r");
	char *mygetline(int ptype __attribute__((unused))) {
		char *line = NULL;
		size_t size = 0;
		ssize_t len = getline(&line, &size, f);
		if (len < 0) return NULL;
		if (line[size - 1] == '\n')
			line[size - 1] = '\0';
		return line;
	}

	if (!f) {
		if (!suppresserror)
			error(0, errno, "%s", path);
		return EXIT_FAILURE;
	}

	int result;
	set_line_number(0);
	for (;;) {
		STATEMENT *statements;
		switch (read_and_parse(mygetline, path, &statements)) {
			case 0:  /* OK */
				if (statements) {
					unsigned savelinenum = get_line_number();
					exec_statements(statements);
					statementsfree(statements);
					set_line_number(savelinenum);
				}
				break;
			case EOF:
				result = 0;
				goto end;
			case 1:  /* syntax error */
			default:
				result = -1;
				goto end;
		}
	}
end:
	if (fclose(f) != 0)
		error(0, errno, "%s", path);
	return result;
}

/* ファイルをシェルスクリプトとして実行する。
 * path: ファイルのパス。'~' で始まるならホームディレクトリを展開して
 *       ファイルを探す。
 * 戻り値: エラーがなければ 0、エラーなら非 0。 */
int exec_file_exp(const char *path, bool suppresserror)
{
	if (path[0] == '~') {
		char *newpath = expand_tilde(path);
		if (!newpath)
			return -1;
		int result = exec_file(newpath, suppresserror);
		free(newpath);
		return result;
	} else {
		return exec_file(path, suppresserror);
	}
}

/* code をシェルスクリプトのソースコードとして解析し、このシェル内で実行する。
 * code:   実行するコード。NULL なら何も行わない。
 * name:   構文エラー時に表示するコード名。
 * 戻り値: エラーがなければ 0、エラーなら非 0。 */
int exec_source(const char *code, const char *name)
{
	size_t index = 0;
	char *mygetline(int ptype __attribute__((unused))) {
		size_t len = strcspn(&code[index], "\n\r");
		if (!len) return NULL;

		char *result = xstrndup(&code[index], len);
		index += len;
		index += strspn(&code[index], "\n\r");
		return result;
	}

	if (!code)
		return 0;

	set_line_number(0);
	for (;;) {
		STATEMENT *statements;
		switch (read_and_parse(mygetline, name, &statements)) {
			case 0:  /* OK */
				if (statements) {
					unsigned savelinenum = get_line_number();
					exec_statements(statements);
					statementsfree(statements);
					set_line_number(savelinenum);
				}
				break;
			case EOF:
				return 0;
			case 1:  /* syntax error */
			default:
				return -1;
		}
	}
}

/* code をシェルスクリプトのソースコードとして解析し、このシェル内でし、
 * そのまま終了する。
 * code: 実行するコード。NULL なら何も実行せず終了する。
 * name: 構文エラー時に表示するコード名。 */
void exec_source_and_exit(const char *code, const char *name)
{
	char *mygetline(int ptype) {
		if (ptype == 1) return xstrdup(code);
		else            return NULL;
	}

	/* 改行を含むコードは一度に解析できないので、普通に exec_source を使う */
	if (strpbrk(code, "\n\r")) {
		exec_source(code, name);
		exit(laststatus);
	}

	STATEMENT *statements;
	set_line_number(0);
	switch (read_and_parse(mygetline, name, &statements)) {
		case 0:  /* OK */
			exec_statements_and_exit(statements);
		default:  /* error */
			exit(2);
	}
}


static pid_t orig_pgrp = 0;

/* このシェル自身が独立したプロセスグループに属するように、
 * このシェルのプロセスグループ ID をこのシェルのプロセス ID に変更する。 */
void set_unique_pgid(void)
{
	if (is_interactive) {
		orig_pgrp = getpgrp();
		setpgrp();
	}
}

/* このシェルのプロセスグループ ID を、set_unique_pgid を実行する前のものに
 * 戻す。 */
void restore_pgid(void)
{
	if (orig_pgrp > 0) {
		if (setpgid(0, orig_pgrp) < 0 && errno != EPERM)
			error(0, errno, "cannot restore process group");
		if (tcsetpgrp(STDIN_FILENO, orig_pgrp) < 0)
			error(0, errno, "cannot restore foreground process group");
		orig_pgrp = 0;
	}
}

/* orig_pgrp をリセットする */
void forget_orig_pgrp(void)
{
	orig_pgrp = 0;
}

static bool noprofile = false, norc = false; 
static char *rcfile = "~/.yashrc";

/* シェルのシグナルハンドラなどの初期化を行う */
void set_shell_env(void)
{
	static bool initialized = false;

	if (is_interactive) {
		set_signals();
		set_unique_pgid();
		set_shlvl(1);
		if (!initialized) {
			if (is_loginshell) {
				if (!noprofile)
					exec_file_exp("~/.yash_profile", true /* suppress error */);
			} else if (!norc) {
				exec_file_exp(rcfile, true /* suppress error */);
			}
			initialized = true;
		}
		initialize_readline();
	}
}

/* シェルのシグナルハンドラなどを元に戻す */
void unset_shell_env(void)
{
	if (is_interactive) {
		finalize_readline();
		set_shlvl(-1);
		restore_pgid();
		unset_signals();
	}
}

/* PROMPT_COMMAND を実行する。
 * 戻り値: 実行したコマンドの終了ステータス */
static int exec_promptcommand(void)
{
	int resultstatus = 0;
	int savestatus = laststatus;
	exec_source(prompt_command, "prompt command");
	resultstatus = laststatus;
	laststatus = savestatus;
	return resultstatus;
}

/* 対話的動作を行う。この関数は返らない。 */
static void interactive_loop(void)
{
	const char *exitargv[] = { "exit", NULL, };

	assert(is_interactive);
	for (;;) {
		STATEMENT *statements;

		exec_promptcommand();
		set_line_number(0);
		switch (read_and_parse(yash_readline, NULL, &statements)) {
			case 0:  /* OK */
				if (statements) {
					exec_statements(statements);
					statementsfree(statements);
				}
				break;
			case 1:  /* syntax error */
				break;
			case EOF:
				laststatus = builtin_exit(1, (char **) exitargv);
				break;
		}
	}
}

static struct xoption long_opts[] = {
	{ "help",        xno_argument,       NULL, '?', },
	{ "version",     xno_argument,       NULL, 'V', },
	{ "rcfile",      xrequired_argument, NULL, 'r', },
	{ "noprofile",   xno_argument,       NULL, 'E', },
	{ "norc",        xno_argument,       NULL, 'O', },
	{ "login",       xno_argument,       NULL, 'l', },
	{ "interactive", xno_argument,       NULL, 'i', },
	{ "posix",       xno_argument,       NULL, 'X', },
	{ NULL, 0, NULL, 0, },
};

int main(int argc __attribute__((unused)), char **argv)
{
	bool help = false, version = false;
	int opt;
	char *directcommand = NULL;
	const char *short_opts = "c:il";

	command_name = argv[0];
	is_loginshell = argv[0][0] == '-';
	is_interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
	posixly_correct = getenv(VAR_POSIXLY_CORRECT) != NULL;
	setlocale(LC_ALL, "");

	xoptind = 0;
	xopterr = true;
	while ((opt = xgetopt_long(argv, short_opts, long_opts, NULL)) >= 0){
		switch (opt) {
			case 0:
				break;
			case 'c':
				directcommand = xoptarg;
				break;
			case 'i':
				is_interactive = true;
				break;
			case 'l':
				is_loginshell = true;
				break;
			case 'O':
				norc = 1;
				break;
			case 'E':
				noprofile = true;
				break;
			case 'X':
				posixly_correct = true;
				break;
			case 'r':
				rcfile = xoptarg;
				break;
			case 'V':
				version = true;
				break;
			case '?':
				help = true;
				break;
			default:
				assert(false);
				return EXIT_FAILURE;
		}
	}
	if (help) {
		print_help();
		return EXIT_SUCCESS;
	} else if (version) {
		print_version();
		return EXIT_SUCCESS;
	}

	shell_pid = getpid();
	init_signal();
	init_exec();
	init_var();
	init_alias();
	init_builtin();

	if (directcommand) {
		is_interactive = false;
		set_shell_env();
		if (argv[xoptind]) {
			command_name = argv[xoptind];
			set_positionals(argv + xoptind + 1);
		}
		exec_source_and_exit(directcommand, "yash -c");
	}
	if (argv[xoptind]) {
		is_interactive = false;
		set_shell_env();
		command_name = argv[xoptind];
		set_positionals(argv + xoptind + 1);
		exec_file(command_name, false /* don't suppress error */);
		exit(laststatus);
	}
	if (is_interactive) {
		set_shell_env();
		interactive_loop();
	}
	return EXIT_SUCCESS;
}

void print_help(void)
{
	printf("Usage:  yash [-il] [-c command] [long options] [file]\n");
	printf("Long options:\n");
	for (size_t index = 0; long_opts[index].name; index++)
		printf("\t--%s\n", long_opts[index].name);
}

void print_version(void)
{
	printf("Yet another shell, version " YASH_VERSION
			" (compiled " __DATE__ " " __TIME__ ")\n"
			YASH_COPYRIGHT "\n");
}

/* 終了前の手続きを行って、終了する。*/
void yash_exit(int exitcode) {
	//wait_chld();
	//print_all_job_status(false /* all jobs */, false /* not verbose */);
	if (is_loginshell)
		exec_file("~/.yash_logout", true /* suppress error */);
	unset_shell_env();
	if (huponexit)
		send_sighup_to_all_jobs();
	exit(exitcode);
}
