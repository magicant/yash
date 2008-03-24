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


#include "common.h"
#include <assert.h>
#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "yash.h"
#include "util.h"
#include "lineinput.h"
#include "parser.h"
#include "sig.h"
#include "job.h"
#include "exec.h"
#include "version.h"


__attribute__((nonnull))
int main(int argc, char **argv);
static void print_help(void);
static void print_version(void);

/* true なら POSIX に厳密に従う */
bool posixly_correct;
/* 対話的シェルかどうか。is_interactive_now はサブシェルでは false になる。 */
bool is_interactive, is_interactive_now;
/* ログインシェルかどうか。 */
bool is_login_shell;
/* コマンド名。特殊パラメータ $0 の値。 */
const char *command_name;
/* シェル本体のプロセス ID。 */
pid_t shell_pid;


static const struct xoption long_options[] = {
	{ "interactive", xno_argument, NULL, 'i', },
	{ "login",       xno_argument, NULL, 'l', },
	{ "posix",       xno_argument, NULL, 'X', },
	{ "help",        xno_argument, NULL, '?', },
	{ "version",     xno_argument, NULL, 'V', },
	{ NULL,          0,            NULL, 0,   },
};

int main(int argc __attribute__((unused)), char **argv)
{
	bool help = false, version = false;
	bool exec_first_arg = false, read_stdin = false;
	int opt;
	const char *shortest_name;

	yash_program_invocation_name = argv[0] ? argv[0] : "";
	yash_program_invocation_short_name
		= strrchr(yash_program_invocation_name, '/');
	if (yash_program_invocation_short_name)
		yash_program_invocation_short_name++;
	else
		yash_program_invocation_short_name = yash_program_invocation_name;
	command_name = yash_program_invocation_name;
	is_login_shell = (yash_program_invocation_name[0] == '-');
	shortest_name = yash_program_invocation_short_name;
	if (shortest_name[0] == '-')
		shortest_name++;
	if (strcmp(shortest_name, "sh") == 0)
		posixly_correct = true;

	setlocale(LC_ALL, "");
#if HAVE_GETTEXT
	bindtextdomain(PACKAGE_NAME, LOCALEDIR);
	textdomain(PACKAGE_NAME);
#endif

	/* オプションを解釈する */
	xoptind = 0;
	xopterr = true;
	// TODO yash:main: unimplemented options: -abCefhimnuvx -o
	while ((opt = xgetopt_long(argv, "+cilsV?", long_options, NULL)) >= 0) {
		switch (opt) {
		case 0:
			break;
		case 'c':
			exec_first_arg = (xoptopt == '-');
			break;
		case 'i':
			is_interactive = (xoptopt == '-');
			break;
		case 'l':
			is_login_shell = (xoptopt == '-');
			break;
		case 's':
			read_stdin = (xoptopt == '-');
			break;
		case 'X':
			posixly_correct = true;
			break;
		case 'V':
			version = true;
			break;
		case '?':
			help = true;
			break;
		}
	}

	/* 最初の引数が "-" なら無視する */
	if (argv[xoptind] && strcmp(argv[xoptind], "-") == 0)
		xoptind++;

	if (version)
		print_version();
	if (help)
		print_help();
	if (version || help)
		return EXIT_SUCCESS;

	shell_pid = getpid();
	init_signal();
	init_job();

	if (exec_first_arg && read_stdin) {
		xerror(2, 0, Ngt("both -c and -s options cannot be given"));
	}
	if (exec_first_arg) {
		char *command = argv[xoptind++];
		if (!command)
			xerror(2, 0, Ngt("-c option requires an operand"));
		if (argv[xoptind])
			command_name = argv[xoptind++];
		do_job_control = is_interactive_now = is_interactive;
		// TODO yash:main:exec_first_arg シェル環境設定・位置パラメータ
		set_signals();
		exec_mbs(command, posixly_correct ? "sh -c" : "yash -c", true);
	} else {
		FILE *input;
		if (read_stdin || !argv[xoptind]) {
			input = stdin;
			if (!argv[xoptind] && isatty(STDIN_FILENO) && isatty(STDERR_FILENO))
				is_interactive = true;
		} else {
			command_name = argv[xoptind++];
			input = fopen(command_name, "r");
			// TODO yash:main: fd を shellfd 以上に移す
			if (!input)
				xerror(errno == ENOENT ? EXIT_NOTFOUND : EXIT_NOEXEC, errno,
						Ngt("cannot open file `%s'"), command_name);
		}
		do_job_control = is_interactive_now = is_interactive;
		set_signals();
		//TODO yash:main: read file and exec commands
		xerror(2, 0, "executing %s: NOT IMPLEMENTED", command_name);
	}
	assert(false);
}

static void print_help(void)
{
	if (posixly_correct) {
		printf(gt("Usage:  sh [options] [filename [args...]]\n"
		          "        sh [options] -c command [command_name [args...]]\n"
				  "        sh [options] -s [args...]\n"));
		printf(gt("Options: -il\n"));
	} else {
		printf(gt("Usage:  yash [options] [filename [args...]]\n"
		          "        yash [options] -c command [args...]\n"
				  "        yash [options] -s [args...]\n"));
		printf(gt("Short options: -ilV?\n"));
		printf(gt("Long options:\n"));
		for (size_t i = 0; long_options[i].name; i++)
			printf("\t--%s\n", long_options[i].name);
	}
}

static void print_version(void)
{
	printf(gt("Yet another shell, version %s\n"), PACKAGE_VERSION);
	printf(PACKAGE_COPYRIGHT "\n");
}


/********** コードを実行する関数 **********/

/* マルチバイト文字列をソースコードとしてコマンドを実行する。
 * code: 実行するコード (初期シフト状態で始まる)
 * name: 構文エラーで表示するコード名。NULL でも良い。
 * finally_exit: true なら実行後にそのままシェルを終了する。
 * 戻り値: 構文エラー・入力エラーがなければ true */
bool exec_mbs(const char *code, const char *name, bool finally_exit)
{
	bool executed = false;
	struct input_mbs_info iinfo = {
		.src = code,
		.srclen = strlen(code) + 1,
	};
	struct parseinfo_T pinfo = {
		.print_errmsg = true,
		.filename = name,
		.lineno = 1,
		.input = input_mbs,
		.inputinfo = &iinfo,
	};
	memset(&iinfo.state, 0, sizeof iinfo.state);  // state を初期状態にする

	for (;;) {
		and_or_T *commands;
		switch (read_and_parse(&pinfo, &commands)) {
			case 0:  // OK
				if (commands) {
					exec_and_or_lists(commands, finally_exit && !iinfo.src);
					andorsfree(commands);
					executed = true;
				}
				break;
			case EOF:
				if (!executed)
					laststatus = EXIT_SUCCESS;
				if (finally_exit)
					exit(laststatus);
				else
					return true;
			case 1:  // 構文エラー
				laststatus = 2;
				if (finally_exit)
					exit(laststatus);
				else
					return false;
		}
	}
	/* コマンドを一つも実行しなかった場合のみ後から laststatus を 0 にする。
	 * 最初に laststatus を 0 にしてしまうと、コマンドを実行する際に $? の値が
	 * 変わってしまう。 */
}
