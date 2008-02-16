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
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "yash.h"
#include "util.h"
#include "sig.h"
#include "lineinput.h"
#include "parser.h"
#include "exec.h"
#include "job.h"
#include "path.h"
#include "builtin.h"
#include "alias.h"
#include "variable.h"
#include "version.h"
#include <assert.h>


static int exec_promptcommand(void);
static void interactive_loop(void) __attribute__((noreturn));
int main(int argc, char **argv) __attribute__((nonnull));
void print_help(void);
void print_version(void);
void yash_exit(int exitcode);

/* main に渡された argv[0] の値。 */
const char *yash_program_invocation_name, *yash_program_invocation_short_name;
/* コマンド名。特殊パラメータ $0 の値。 */
const char *command_name;
/* シェルのプロセス ID。サブシェルでも変わらない。 */
pid_t shell_pid;

/* このプロセスがログインシェルかどうか */
bool is_loginshell;
/* 対話的シェルかどうか
 * is_loginshell, is_interactive の値はサブシェルでも変わらない。
 * is_interactive_now はサブシェルでは常に false である。 */
bool is_interactive, is_interactive_now;
/* POSIX の規定に厳密に従うかどうか */
bool posixly_correct;

/* プライマリプロンプトの前に実行するコマンド */
char *prompt_command = NULL;

/* 指定したファイルをシェルスクリプトとしてこのシェルの中で実行する
 * path:   実行するファイル名。
 * pathsearch: true なら path が相対パスのとき PATH から検索する。
 * suppresserror: true なら、ファイルが読み込めなくてもエラーを出さない
 *         false なら、非対話的シェルでファイルが読み込めなければ終了する。
 * 戻り値: エラーがなければ 0、エラーなら非 0。 */
int exec_file(const char *path, char *const *positionals,
		bool pathsearch, bool suppresserror)
{
	char *rpath = NULL;
	if (pathsearch && strchr(path, '/') == NULL) {
		rpath = which(path, getenv(VAR_PATH), is_readable);
		if (!rpath) {
			if (!suppresserror)
				xerror(is_interactive_now ? 0 : EXIT_FAILURE,
						ENOENT, "%s", path);
			return -1;
		}
	}

	int fd = open(rpath ? rpath : path, O_RDONLY);
	free(rpath);
	if (fd < 0) {
		if (!suppresserror)
			xerror(is_interactive_now ? 0 : EXIT_FAILURE, errno, "%s", path);
		return -1;
	}
	/* SHELLFD 以上のファイルディスクリプタで開くことを保証する。 */
	if (fd < SHELLFD) {
		int newfd = fcntl(fd, F_DUPFD, SHELLFD);
		int olderrno = errno;
		if (close(fd) != 0)
			xerror(0, errno, "%s", path);
		if (newfd < 0) {
			xerror(0, olderrno, "%s", path);
			return -1;
		}
		fd = newfd;
	}

	bool executed = false;
	struct fgetline_info finfo = {
		.fd = fd,
	};
	struct parse_info info = {
		.filename = path,
		.lineno = 0,
		.input = yash_fgetline,
		.inputinfo = &finfo,
	};
	int result;
	extend_environment();
	if (positionals)
		set_positionals(positionals);
	for (;;) {
		STATEMENT *statements;
		switch (read_and_parse(&info, &statements)) {
			case 0:  /* OK */
				if (statements) {
					exec_statements(statements);
					statementsfree(statements);
					executed = true;
				}
				break;
			case EOF:
				result = 0;
				if (!executed)
					laststatus = EXIT_SUCCESS;
				goto end;
			case 1:  /* syntax error */
			default:
				result = -1;
				laststatus = 2;
				goto end;
		}
	}
end:
	unextend_environment();
	if (close(fd) != 0)
		xerror(0, errno, "%s", path);
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
		int result = exec_file(newpath, NULL, false, suppresserror);
		free(newpath);
		return result;
	} else {
		return exec_file(path, NULL, false, suppresserror);
	}
}

/* code をシェルスクリプトのソースコードとして解析し、このシェル内で実行する。
 * code:   実行するコード。NULL なら何も行わない。
 * name:   構文エラー時に表示するコード名。
 * 戻り値: エラーがなければ 0、エラーなら非 0。 */
int exec_source(const char *code, const char *name)
{
	if (!code)
		return 0;

	bool executed = false;
	struct sgetline_info sinfo = {
		.src = code,
		.offset = 0,
	};
	struct parse_info pinfo = {
		.filename = name,
		.lineno = 0,
		.input = yash_sgetline,
		.inputinfo = &sinfo,
	};
	for (;;) {
		STATEMENT *statements;
		switch (read_and_parse(&pinfo, &statements)) {
			case 0:  /* OK */
				if (statements) {
					exec_statements(statements);
					statementsfree(statements);
					executed = true;
				}
				break;
			case EOF:
				if (!executed)
					laststatus = EXIT_SUCCESS;
				return 0;
			case 1:  /* syntax error */
			default:
				laststatus = 2;
				return -1;
		}
	}
}

/* exec_source_and_exit でつかう getline_t 関数 */
static char *exec_source_and_exit_getline(int ptype, void *code) {
	if (ptype == 1) return xstrdup(code);
	else            return NULL;
}

/* code をシェルスクリプトのソースコードとして解析し、このシェル内でし、
 * そのまま終了する。
 * code: 実行するコード。NULL なら何も実行せず終了する。
 * name: 構文エラー時に表示するコード名。 */
void exec_source_and_exit(const char *code, const char *name)
{
	/* 改行を含むコードは一度に解析できないので、普通に exec_source を使う */
	if (strchr(code, '\n')) {
		exec_source(code, name);
		exit(laststatus);
	}

	struct parse_info info = {
		.filename = name,
		.lineno = 0,
		.input = exec_source_and_exit_getline,
		.inputinfo = (void *) code,
	};
	STATEMENT *statements;
	switch (read_and_parse(&info, &statements)) {
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
	if (is_interactive_now) {
		orig_pgrp = getpgrp();
		setpgid(0, 0);  // setpgrp();
	}
}

/* このシェルのプロセスグループ ID を、set_unique_pgid を実行する前のものに
 * 戻す。 */
void restore_pgid(void)
{
	if (orig_pgrp > 0) {
		if (setpgid(0, orig_pgrp) < 0 && errno != EPERM)
			xerror(0, errno, "cannot restore process group");
		if (tcsetpgrp(STDIN_FILENO, orig_pgrp) < 0)
			xerror(0, errno, "cannot restore foreground process group");
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

	if (is_interactive_now) {
		set_signals();
		set_unique_pgid();
		set_shlvl(1);
		if (!initialized) {
			if (is_loginshell) {
				if (!noprofile)
					exec_file_exp("~/.yash_profile", true /* suppress error */);
			} else if (!norc) {
				if (rcfile[0] == '/' || rcfile[0] == '~') {
					exec_file_exp(rcfile, true /* suppress error */);
				} else {
					char rcfile2[strlen(rcfile) + 3];
					rcfile2[0] = '.';
					rcfile2[1] = '/';
					strcpy(rcfile2 + 2, rcfile);
					exec_file_exp(rcfile2, true /* suppress error */);
				}
			}
			initialized = true;
		}
		initialize_readline();
	}
}

/* シェルのシグナルハンドラなどを元に戻す */
void unset_shell_env(void)
{
	if (is_interactive_now) {
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
	static const char *exitargv[] = { "exit", NULL, };
	struct parse_info info = { .input = yash_readline };

	assert(is_interactive && is_interactive_now);
	for (;;) {
		STATEMENT *statements;

		exec_promptcommand();
		switch (read_and_parse(&info, &statements)) {
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
	const char *short_opts = "+c:il";

	yash_program_invocation_name = argv[0] ? argv[0] : "";
	yash_program_invocation_short_name
		= strrchr(yash_program_invocation_name, '/');
	if (yash_program_invocation_short_name)
		yash_program_invocation_short_name++;
	else
		yash_program_invocation_short_name = yash_program_invocation_name;

	command_name = yash_program_invocation_name;
	is_loginshell = yash_program_invocation_name[0] == '-';
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
	init_jobcontrol();
	init_var();
	init_alias();
	init_builtin();
	init_cmdhash();

	if (directcommand) {
		is_interactive = is_interactive_now = false;
		set_shell_env();
		if (argv[xoptind]) {
			command_name = argv[xoptind];
			set_positionals(argv + xoptind + 1);
		}
		exec_source_and_exit(directcommand, "yash -c");
	}
	if (argv[xoptind]) {
		is_interactive = is_interactive_now = false;
		set_shell_env();
		command_name = argv[xoptind];
		exec_file(command_name, argv + xoptind + 1, false, false);
		exit(laststatus);
	}
	if (is_interactive) {
		is_interactive_now = true;
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
	printf("Yet another shell, version " PACKAGE_VERSION
			" (compiled " __DATE__ " " __TIME__ ")\n"
			PACKAGE_COPYRIGHT "\n");
}

/* 終了前の手続きを行って、終了する。*/
void yash_exit(int exitcode) {
	//wait_chld();
	//print_all_job_status(false /* all jobs */, false /* not verbose */);
	if (is_interactive_now && is_loginshell)
		exec_file_exp("~/.yash_logout", true /* suppress error */);
	unset_shell_env();
	if (huponexit)
		send_sighup_to_all_jobs();
	exit(exitcode);
}

/* 自分自身に exec する。
 * argv をコマンドラインとして実行されたかのように、commandpath を
 * シェルスクリプトとして実行する。
 * この関数はシェルの状態を完全に初期状態に戻した後、main を実行する。 */
void selfexec(const char *commandpath, char **argv)
{
	unset_shell_env();
	finalize_jobcontrol();
	finalize_var();
	finalize_alias();
	laststatus = 0;

	struct plist newargs;
	pl_init(&newargs);
	pl_append(&newargs, argv[0]);
	pl_append(&newargs, commandpath);
	pl_aappend(&newargs, (void **) (argv + 1));
	exit(main(newargs.length, (char **) newargs.contents));
}
