/* Yash: yet another shell */
/* © 2007 magicant */

/* This software can be redistributed and/or modified under the terms of
 * GNU General Public License, version 2 or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRENTY. */


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
#include <assert.h>


void setsigaction(void);
void resetsigaction(void);
void exec_file(const char *path, bool suppresserror);
void exec_file_exp(const char *path, bool suppresserror);
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

void debug_sig(int signal)
{
	error(0, 0, "DEBUG: received signal %d. pid=%ld", signal, (long) getpid());
}

/* シグナルハンドラを初期化する */
void setsigaction(void)
{
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
	if (sigaction(SIGQUIT, &action, NULL) < 0)
		error(0, errno, "sigaction: signal=SIGQUIT");
	if (is_interactive) {
		for (signals = ignsignals; *signals; signals++)
			if (sigaction(*signals, &action, NULL) < 0)
				error(0, errno, "sigaction: signal=%d", *signals);
	}

	action.sa_handler = SIG_DFL;
	if (sigaction(SIGCHLD, &action, NULL) < 0)
		error(0, errno, "sigaction: signal=SIGCHLD");

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

	if (sigaction(SIGQUIT, &action, NULL) < 0)
		error(0, errno, "sigaction: signal=SIGQUIT");
	for (signals = ignsignals; *signals; signals++)
		if (sigaction(*signals, &action, NULL) < 0)
			error(0, errno, "sigaction: signal=%d", *signals);
	if (sigaction(SIGHUP, &action, NULL) < 0)
		error(0, errno, "sigaction: signal=SIGHUP");
}

/* 指定したファイルをシェルスクリプトとして実行する
 * suppresserror: true なら、ファイルが読み込めなくてもエラーを出さない */
void exec_file(const char *path, bool suppresserror)
{
	int fd = open(path, O_RDONLY);

	if (fd < 0) {
		if (!suppresserror)
			error(0, errno, "%s", path);
		return;
	}

	char *src = read_all(fd);
	close(fd);
	if (!src) {
		if (!suppresserror)
			error(0, errno, "%s", path);
		return;
	}

	STATEMENT *statements = parse_all(src, NULL);
	if (statements)
		exec_statements(statements);
	statementsfree(statements);
	free(src);
}

/* ファイルをシェルスクリプトとして実行する。
 * path: ファイルのパス。'~' で始まるならホームディレクトリを展開して
 *       ファイルを探す。 */
void exec_file_exp(const char *path, bool suppresserror)
{
	if (path[0] == '~') {
		char *newpath = expand_tilde(path);
		if (!newpath)
			return;
		exec_file(newpath, suppresserror);
		free(newpath);
	} else {
		exec_file(path, suppresserror);
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
	}
}

/* PROMPT_COMMAND を実行する。
 * 戻り値: 実行したコマンドの終了ステータス */
static int exec_promptcommand(void)
{
	int resultstatus = 0;

	if (prompt_command) {
		STATEMENT *ss = parse_all(prompt_command, NULL);

		if (ss) {
			int tmpstatus = laststatus;
			laststatus = 0;
			exec_statements(ss);
			statementsfree(ss);
			resultstatus = laststatus;
			laststatus = tmpstatus;
		}
	}
	return resultstatus;
}

/* 対話的動作を行う。この関数は返らない。 */
static void interactive_loop(void)
{
	bool more;
	char *line;
	struct strbuf linebuf;
	STATEMENT *statements;

	assert(is_interactive);
	strbuf_init(&linebuf);
	for (;;) {
		int ptype;

		if (linebuf.length == 0) {
			ptype = 1;
			exec_promptcommand();
		} else {
			ptype = 2;
		}
		switch (yash_readline(ptype, &line)) {
			case -2:  /* EOF */
				printf("\n");
				if (linebuf.length == 0) {
					wait_all(-2 /* non-blocking */);
					print_all_job_status(
							true /* changed only */, false /* not verbose */);
					if (job_count()) {
						error(0, 0, "There are undone jobs!"
								"  Use `exit -f' to exit forcibly.");
						continue;
					}
					goto exit;
				} else {
					line = NULL;
					break;
				}
			case -1:
				continue;
			case 0:
				break;
			default:
				error(2, 0, "internal error: yash_readline result");
		}
		if (line == NULL) {
			statements = parse_all(linebuf.contents, NULL);
			strbuf_clear(&linebuf);
		} else if (linebuf.length == 0) {
			statements = parse_all(line, &more);
			if (more) {
				strbuf_append(&linebuf, line);
				strbuf_append(&linebuf, "\n");
			}
		} else {
			strbuf_append(&linebuf, line);
			strbuf_append(&linebuf, "\n");
			statements = parse_all(linebuf.contents, &more);
			if (!more)
				strbuf_clear(&linebuf);
		}
		free(line);
		if (statements) {
			exec_statements(statements);
			statementsfree(statements);
		}
	}
exit:
	strbuf_destroy(&linebuf);
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
	joblistlen = 2;
	joblist = xcalloc(joblistlen, sizeof(JOB));
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

	setsigaction();
	init_env();

	if (directcommand) {
		directcommand = skipwhites(directcommand);
		if (!*directcommand) return EXIT_SUCCESS;

		STATEMENT *statements = parse_all(directcommand, NULL);
		
		is_interactive = 0;
		if (!statements) return 2;  /* syntax error */
		exec_statements(statements);
		return laststatus;
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
