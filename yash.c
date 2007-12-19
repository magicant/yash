/* Yash: yet another shell */
/* yash.c: basic functions of the shell */
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


#define _GNU_SOURCE
#include <ctype.h>
#include <error.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "yash.h"
#include "util.h"
#include "parser.h"
#include "exec.h"
#include "path.h"
#include "builtin.h"
#include "alias.h"
#include <assert.h>


void setsigaction(void);
void resetsigaction(void);
int exec_file(const char *path, bool suppresserror);
int exec_file_exp(const char *path, bool suppresserror);
int exec_source(const char *name, const char *code);
void exec_source_and_exit(const char *name, const char *code);
static void set_shlvl(int change);
static void init_env(void);
void init_interactive(void);
void finalize_interactive(void);
static int exec_promptcommand(void);
static void interactive_loop(void) __attribute__ ((noreturn));
int main(int argc, char **argv);
void print_help(void);
void print_version(void);
void yash_exit(int exitcode);

/* このプロセスがログインシェルなら非 0 */
bool is_loginshell;
/* 対話的シェルなら非 0 */
bool is_interactive;

/* プライマリプロンプトの前に実行するコマンド */
char *prompt_command = NULL;

/* シェルで無視するシグナルのリスト */
const int ignsignals[] = {
	SIGINT, SIGTERM, SIGTSTP, SIGTTIN, SIGTTOU, 0,
};

#if 0
static void debug_sig(int signal)
{
	error(0, 0, "DEBUG: received signal %d. pid=%ld", signal, (long) getpid());
}
#endif

/* シグナルハンドラを初期化する */
void setsigaction(void)
{
	void sig_quit(int signum __UNUSED__) { }
	struct sigaction action;
	const int *signals;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
#if 0
	action.sa_handler = debug_sig;
	fprintf(stderr, "DEBUG: setting all sigaction\n");
	for (int i = 1; i < 32; i++)
		if (sigaction(i, &action, NULL) < 0) ;
#endif

	action.sa_handler = SIG_IGN;
	if (is_interactive) {
		for (signals = ignsignals; *signals; signals++)
			if (sigaction(*signals, &action, NULL) < 0)
				error(0, errno, "sigaction: signal=%d", *signals);
	}

	action.sa_handler = SIG_DFL;
	if (sigaction(SIGCHLD, &action, NULL) < 0)
		error(0, errno, "sigaction: signal=SIGCHLD");

	/* sig_quit は何もしないシグナルハンドラなので、シグナル受信時の挙動は
	 * SIG_IGN と同じだが、exec の実行時にデフォルトに戻される点が異なる。 */
	action.sa_handler = sig_quit;
	if (sigaction(SIGQUIT, &action, NULL) < 0)
		error(0, errno, "sigaction: signal=SIGQUIT");

	action.sa_handler = sig_hup;
	action.sa_flags = SA_RESETHAND;
	if (sigaction(SIGHUP, &action, NULL) < 0)
		error(0, errno, "sigaction: signal=SIGHUP");
}

/* シグナルハンドラを元に戻す */
void resetsigaction(void)
{
	struct sigaction action;
	const int *signals;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = SIG_DFL;

	for (signals = ignsignals; *signals; signals++)
		if (sigaction(*signals, &action, NULL) < 0)
			error(0, errno, "sigaction: signal=%d", *signals);
	if (sigaction(SIGHUP, &action, NULL) < 0)
		error(0, errno, "sigaction: signal=SIGHUP");
}

/* 指定したファイルをシェルスクリプトとしてこのシェルの中で実行する
 * suppresserror: true なら、ファイルが読み込めなくてもエラーを出さない
 * 戻り値: エラーがなければ 0、エラーなら非 0。 */
int exec_file(const char *path, bool suppresserror)
{
	FILE *f = fopen(path, "r");
	char *mygetline(int ptype __UNUSED__) {
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
		return EOF;
	}

	int result;
	set_line_number(0);
	for (;;) {
		STATEMENT *statements;
		switch (read_and_parse(path, &mygetline, &statements)) {
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
 * name:   構文エラー時に表示するコード名。
 * code:   実行するコード。NULL なら何も行わない。
 * 戻り値: エラーがなければ 0、エラーなら非 0。 */
int exec_source(const char *name, const char *code)
{
	size_t index = 0;
	char *mygetline(int ptype __UNUSED__) {
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
		switch (read_and_parse(name, &mygetline, &statements)) {
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
 * name:   構文エラー時に表示するコード名。
 * code: 実行するコード。NULL なら何も実行せず終了する。 */
void exec_source_and_exit(const char *name, const char *code)
{
	char *mygetline(int ptype) {
		if (ptype == 1) return xstrdup(code);
		else            return NULL;
	}

	if (strpbrk(code, "\n\r")) {
		exec_source(name, code);
		exit(laststatus);
	}

	STATEMENT *statements;
	set_line_number(0);
	switch (read_and_parse(name, &mygetline, &statements)) {
		case 0:  /* OK */
			exec_statements_and_exit(statements);
		default:  /* error */
			exit(2);
	}
}

/* 環境変数 SHLVL に change を加える */
static void set_shlvl(int change)
{
	char *shlvl = getenv(ENV_SHLVL);
	int level = shlvl ? atoi(shlvl) : 0;
	char newshlvl[16];

	level += change;
	if (level < 0)
		level = 0;
	if (snprintf(newshlvl, sizeof newshlvl, "%d", level) >= 0) {
		if (setenv(ENV_SHLVL, newshlvl, true /* overwrite */) < 0)
			error(0, 0, "failed to set env SHLVL");
	}
}

/* 実行環境を初期化する */
static void init_env(void)
{
	char *path = getcwd(NULL, 0);

	if (path) {
		char *spwd = collapse_homedir(path);

		if (setenv(ENV_PWD, path, true /* overwrite */) < 0)
			error(0, 0, "failed to set env PWD");
		if (spwd) {
			if (setenv(ENV_SPWD, spwd, true /* overwrite */) < 0)
				error(0, 0, "failed to set env SPWD");
			free(spwd);
		}
		free(path);
	}
}

static pid_t orig_pgrp = 0;
static bool noprofile = false, norc = false; 
static char *rcfile = "~/.yashrc";

/* 対話モードの初期化を行う */
void init_interactive(void)
{
	static bool initialized = false;

	if (is_interactive) {
		setsigaction();
		orig_pgrp = getpgrp();
		setpgrp();   /* シェル自身は独立した pgrp に属する */
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

/* 対話モードの終了処理を行う */
void finalize_interactive(void)
{
	if (is_interactive) {
		finalize_readline();
		set_shlvl(-1);
		if (orig_pgrp > 0 && tcsetpgrp(STDIN_FILENO, orig_pgrp) < 0)
			error(0, errno, "cannot reset foreground process group");
		if (orig_pgrp > 0 && setpgid(0, orig_pgrp) < 0 && errno != EPERM)
			error(0, errno, "cannot reset process group");
		resetsigaction();
	}
}

/* PROMPT_COMMAND を実行する。
 * 戻り値: 実行したコマンドの終了ステータス */
static int exec_promptcommand(void)
{
	int resultstatus = 0;
	int savestatus = laststatus;
	laststatus = 0;
	exec_source("prompt command", prompt_command);
	resultstatus = laststatus;
	laststatus = savestatus;
	return resultstatus;
}

/* 対話的動作を行う。この関数は返らない。 */
static void interactive_loop(void)
{
	assert(is_interactive);
	for (;;) {
		STATEMENT *statements;

		exec_promptcommand();
		set_line_number(0);
		switch (read_and_parse(NULL, &yash_readline, &statements)) {
			case 0:  /* OK */
				if (statements) {
					exec_statements(statements);
					statementsfree(statements);
				}
				break;
			case 1:  /* syntax error */
				break;
			case EOF:
				wait_all(-2 /* non-blocking */);
				print_all_job_status(
						true /* changed only */, false /* not verbose */);
				if (job_count()) {
					error(0, 0, "There are undone jobs!"
							"  Use `exit -f' to exit forcibly.");
					break;
				}
				goto exit;
		}
	}
exit:
	yash_exit(laststatus);
}

static struct option long_opts[] = {
	{ "help", 0, NULL, '?', },
	{ "version", 0, NULL, 'v' + 256, },
	{ "rcfile", 1, NULL, 'r', },
	{ "noprofile", 0, NULL, 'N' + 256, },
	{ "norc", 0, NULL, 'n' + 256, },
	{ "login", 0, NULL, 'l', },
	{ "interactive", 0, NULL, 'i', },
	{ NULL, 0, NULL, 0, },
};

int main(int argc, char **argv)
{
	bool help = false, version = false;
	int opt, index;
	char *directcommand = NULL;
	static const char *short_opts = "c:il";

	is_loginshell = argv[0][0] == '-';
	is_interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
	setlocale(LC_ALL, "");

	optind = 0;
	opterr = 1;
	while ((opt = getopt_long(argc, argv, short_opts, long_opts, &index)) >= 0){
		switch (opt) {
			case 0:
				break;
			case 'c':
				directcommand = optarg;
				break;
			case 'i':
				is_interactive = true;
				break;
			case 'l':
				is_loginshell = true;
				break;
			case 'n' + 256:
				norc = 1;
				break;
			case 'N' + 256:
				noprofile = true;
				break;
			case 'r':
				rcfile = optarg;
				break;
			case 'v' + 256:
				version = true;
				break;
			case '?':
				help = true;
				break;
			default:
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

	init_exec();
	init_env();
	init_alias();
	init_builtin();

	if (directcommand) {
		is_interactive = false;
		exec_source_and_exit("yash -c", directcommand);
	}
	if (argv[optind]) {
		is_interactive = false;
		exec_file(argv[optind], false /* don't suppress error */);
		exit(laststatus);
	}
	if (is_interactive) {
		init_interactive();
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
	wait_all(-2 /* non-blocking */);
	print_all_job_status(false /* all jobs */, false /* not verbose */);
	if (is_loginshell)
		exec_file("~/.yash_logout", true /* suppress error */);
	finalize_interactive();
	if (huponexit)
		send_sighup_to_all_jobs();
	exit(exitcode);
}
