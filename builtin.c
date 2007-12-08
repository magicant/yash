/* Yash: yet another shell */
/* © 2007 magicant */

/* This software can be redistributed and/or modified under the terms of
 * GNU General Public License, version 2 or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRENTY. */


#define  _GNU_SOURCE
#include <ctype.h>
#include <error.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <readline/history.h>
#include "yash.h"
#include <assert.h>


BUILTIN assoc_builtin(const char *name);
int parse_jobspec(const char *str, bool forcePercent);
int builtin_null(int argc, char *const *argv);
int builtin_exit(int argc, char *const *argv);
static int get_signal(const char *name);
static const char *get_signal_name(int signal);
int builtin_kill(int argc, char *const *argv);
int builtin_wait(int argc, char *const *argv);
int builtin_suspend(int argc, char *const *argv);
int builtin_jobs(int argc, char *const *argv);
int builtin_disown(int argc, char *const *argv);
int builtin_fg(int argc, char *const *argv);
int builtin_cd(int argc, char *const *argv);
int builtin_umask(int argc, char *const *argv);
int builtin_export(int argc, char *const *argv);
int builtin_source(int argc, char *const *argv);
int builtin_history(int argc, char *const *argv);
int builtin_alias(int argc, char *const *argv);
int builtin_unalias(int argc, char *const *argv);
int builtin_option(int argc, char *const *argv);

/* 組込みコマンド一般仕様:
 * argc は少なくとも 1 で、argv[0] は呼び出されたコマンドの名前である。
 * 組込みコマンドは、argv の内容を変更してはならない。 */
/* 次のコマンドは exec.c の中で特殊な方法で処理する: exec */

const BUILTIN const builtins[] = {
	{ ":", builtin_null, },
	{ "exit", builtin_exit, },
	{ "logout", builtin_exit, },
	{ "suspend", builtin_suspend, },
	{ "jobs", builtin_jobs, },
	{ "disown", builtin_disown, },
	{ "fg", builtin_fg, },
	{ "bg", builtin_fg, },
	{ "cd", builtin_cd, },
	{ "umask", builtin_umask, },
	{ "kill", builtin_kill, },
	{ "wait", builtin_wait, },
	{ "export", builtin_export, },
	{ "source", builtin_source, },
	{ ".", builtin_source, },
	{ "history", builtin_history, },
	{ "alias", builtin_alias, },
	{ "unalias", builtin_unalias, },
	{ "option", builtin_option, },
	{ NULL, NULL, },
};

/* コマンド名に対応する BUILTIN を返す。
 * コマンドが見付からなければ { NULL, NULL } を返す。 */
BUILTIN assoc_builtin(const char *name)
{
	const BUILTIN *b = builtins;

	while (b->b_name && strcmp(b->b_name, name) != 0)
		b++;
	return *b;
}

/* jobspec を解析する。
 * 戻り値: str の jobnumber。無効な結果ならば負数。
 *         -1: 不正な書式   -2: ジョブが存在しない   -3: 指定が曖昧 */
int parse_jobspec(const char *str, bool forcePercent)
{
	int jobnumber;
	char *strend;

	if (str[0] == '%')
		str++;
	else if (forcePercent)
		return -1;
	if (!*str)
		return -1;
	errno = 0;
	jobnumber = strtol(str, &strend, 10);
	if (errno)
		return -2;
	if (str != strend && !strend[0] && 0 < jobnumber) {
		if (jobnumber < (ssize_t) joblistlen && joblist[jobnumber].j_pgid)
			return jobnumber;
		else
			return -2;
	}

	size_t len = strlen(str);
	jobnumber = 0;
	for (int i = 1; i < (ssize_t) joblistlen; i++) {
		if (joblist[i].j_pgid && strncmp(str, joblist[i].j_name, len) == 0) {
			if (!jobnumber)
				jobnumber = i;
			else
				return -3;
		}
	}
	if (!jobnumber)
		return -2;
	else
		return jobnumber;
}

/* : 組込みコマンド */
int builtin_null(int argc, char *const *argv)
{
	return EXIT_SUCCESS;
}

/* exit/logout 組込みコマンド
 * logout では、ログインシェルでないときにエラーを出す。
 * -f: ジョブが残っていても exit する。 */
int builtin_exit(int argc, char *const *argv)
{
	bool forceexit = false;
	int opt;
	int status = laststatus;

	if (strcmp(argv[0], "logout") == 0 && !is_loginshell) {
		error(0, 0, "%s: not login shell: use `exit'", argv[0]);
		return EXIT_FAILURE;
	}

	optind = 0;
	opterr = 1;
	while ((opt = getopt(argc, argv, "f")) >= 0) {
		switch (opt) {
			case 'f':
				forceexit = true;
				break;
			default:
				goto usage;
		}
	}
	if (optind < argc) {
		char *c = argv[optind];

		errno = 0;
		if (*c)
			status = strtol(c, &c, 0);
		if (errno || *c) {
			error(0, 0, "%s: invalid argument", argv[0]);
			goto usage;
		}
	}
	if (!forceexit) {
		wait_all(-2 /* non-blocking */);
		print_all_job_status(true /* changed only */, false /* not verbose */);
		if (job_count()) {
			error(0, 0, "There are undone jobs!"
					"  Use `-f' option to exit anyway.");
			return EXIT_FAILURE;
		}
	}

	yash_exit(status);
	assert(0);
	return EXIT_FAILURE;

usage:
	printf("Usage:  exit/logout [-f] [exitcode]\n");
	return EXIT_FAILURE;
}

typedef struct {
	int s_signal;
	const char *s_name;
} SIGDATA;

static const SIGDATA const sigdata[] = {
	{ SIGHUP, "HUP", }, { SIGINT, "INT", }, { SIGQUIT, "QUIT", },
	{ SIGILL, "ILL", }, { SIGABRT, "ABRT", }, { SIGFPE, "FPE", },
	{ SIGKILL, "KILL", }, { SIGSEGV, "SEGV", }, { SIGPIPE, "PIPE", },
	{ SIGALRM, "ALRM", }, { SIGTERM, "TERM", }, { SIGUSR1, "USR1", },
	{ SIGUSR2, "USR2", }, { SIGCHLD, "CHLD", }, { SIGCONT, "CONT", },
	{ SIGSTOP, "STOP", }, { SIGTSTP, "TSTP", }, { SIGTTIN, "TTIN", },
	{ SIGTTOU, "TTOU", }, { SIGBUS, "BUS", }, { SIGPOLL, "POLL", },
	{ SIGPROF, "PROF", }, { SIGSYS, "SYS", }, { SIGTRAP, "TRAP", },
	{ SIGURG, "URG", }, { SIGVTALRM, "VTALRM", }, { SIGXCPU, "XCPU", },
	{ SIGXFSZ, "XFSZ", }, { SIGIOT, "IOT", }, { SIGSTKFLT, "STKFLT", },
	{ SIGIO, "IO", }, { SIGCLD, "CLD", }, { SIGPWR, "PWR", },
	{ SIGWINCH, "WINCH", }, { SIGUNUSED, "UNUSED", },
	{ 0, NULL, },
};

/* シグナル名に対応するシグナルの番号を返す。
 * シグナルが見付からないときは 0 を返す。 */
static int get_signal(const char *name)
{
	const SIGDATA *sd = sigdata;

	assert(name != NULL);
	if ('0' <= name[0] && name[0] <= '9') {  /* name は番号 */
		char *c;
		int result = 0;

		errno = 0;
		if (*name)
			result = strtol(name, &c, 10);
		if (errno || *c)
			return 0;
		return result;
	} else {  /* name は名前 */
		if (strncmp(name, "SIG", 3) == 0)
			name += 3;
		while (sd->s_name) {
			if (strcmp(name, sd->s_name) == 0)
				return sd->s_signal;
			sd++;
		}
		return 0;
	}
}

/* シグナル番号からシグナル名を得る。
 * シグナル名が得られないときは NULL を返す。 */
static const char *get_signal_name(int signal)
{
	const SIGDATA *sd;

	for (sd = sigdata; sd->s_signal; sd++)
		if (sd->s_signal == signal)
			return sd->s_name;
	return NULL;
}

/* kill 組込みコマンド
 * -s signal: シグナルの指定。デフォルトは TERM。*/
int builtin_kill(int argc, char *const *argv)
{
	int sig = SIGTERM;
	bool err = false, list = false;
	int i = 1;

	if (argc == 1)
		goto usage;
	if (argv[1][0] == '-') {
		if (argv[1][1] == 's') {
			if (argv[1][2]) {
				sig = get_signal(argv[1] + 2);
			} else {
				sig = get_signal(argv[2]);
				i = 2;
			}
			if (!sig) {
				error(0, 0, "%s: %s: invalid signal", argv[0], argv[1] + 2);
				return EXIT_FAILURE;
			}
		} else if (isupper(argv[1][1])) {
			sig = get_signal(argv[1] + 1);
			if (!sig) {
				error(0, 0, "%s: %s: invalid signal", argv[0], argv[1] + 1);
				return EXIT_FAILURE;
			}
		} else if (argv[1][1] == 'l') {
			list = true;
		} else {
			goto usage;
		}
	} else {
		i = 0;
	}
	if (list)
		goto list;
	while (++i < argc) {
		char *target = argv[i];
		bool isjob = target[0] == '%';
		int targetnum = 0;
		pid_t targetpid;

		if (isjob) {
			targetnum = parse_jobspec(target, true);
			if (targetnum < 0) switch (targetnum) {
				case -1:  default:
					error(0, 0, "%s: %s: invalid job spec", argv[0], argv[i]);
					err = true;
					continue;
				case -2:
					error(0, 0, "%s: %s: no such job", argv[0], argv[i]);
					err = true;
					continue;
				case -3:
					error(0, 0, "%s: %s: ambiguous job spec", argv[0],argv[i]);
					err = true;
					continue;
			}
			targetpid = -joblist[targetnum].j_pgid;
		} else {
			errno = 0;
			if (*target)
				targetnum = strtol(target, &target, 10);
			if (errno || *target) {
				error(0, 0, "%s: %s: invalid target", argv[0], argv[i]);
				err = true;
				continue;
			}
			targetpid = (pid_t) targetnum;
		}
		if (kill(targetpid, sig) < 0) {
			error(0, errno, "%s: %s", argv[0], argv[i]);
			err = true;
		}
	}
	return err ? EXIT_FAILURE : EXIT_SUCCESS;

list:
	if (argc <= 2) {  /* no args: list all */
		for (size_t i = 0; sigdata[i].s_signal; i++) {
			printf("%2d: %-8s    ", sigdata[i].s_signal, sigdata[i].s_name);
			if (i % 4 == 3)
				printf("\n");
		}
		printf("\n");
	} else {
		for (i = 2; i < argc; i++) {
			const char *name;

			sig = get_signal(argv[i]);
			name = get_signal_name(sig);
			if (!sig || !name) {
				error(0, 0, "%s: %s: invalid signal", argv[0], argv[i]);
				err = 1;
			} else {
				printf("%2d: %s\n", sig, get_signal_name(sig));
			}
		}
	}
	return err ? EXIT_FAILURE : EXIT_SUCCESS;

usage:
	printf("Usage:  kill [-s signal] pid/jobspec ...\n");
	printf("    or  kill -l [signals]\n");
	return EXIT_FAILURE;
}

/* wait 組込みコマンド */
int builtin_wait(int argc, char *const *argv)
{
	void int_handler(int signal) { cancel_wait = true; }
	struct sigaction action, oldaction;
	int jobnumber = -1;

	if (argc == 2) {
		char *target = argv[1];
		bool isjob = target[0] == '%';

		if (isjob) {
			jobnumber = parse_jobspec(target, true);
			if (jobnumber < 0) switch (jobnumber) {
				case -1:
				default:
					error(0, 0, "%s: %s: invalid job spec", argv[0], argv[1]);
					return EXIT_NOTFOUND;
				case -2:
					error(0, 0, "%s: %s: no such job", argv[0], argv[1]);
					return EXIT_NOTFOUND;
				case -3:
					error(0, 0, "%s: %s: ambiguous job spec", argv[0],argv[1]);
					return EXIT_NOTFOUND;
			}
		} else {
			errno = 0;
			if (*target)
				jobnumber = strtol(target, &target, 10);
			if (errno || *target) {
				error(0, 0, "%s: %s: invalid target", argv[0], argv[1]);
				return EXIT_FAILURE;
			}
			jobnumber = get_jobnumber_from_pid(jobnumber);
			if (jobnumber < 0) {
				error(0, 0, "%s: %s: not a child of this shell",
						argv[0], argv[1]);
				return EXIT_NOTFOUND;
			}
		}
	} else if (argc > 2) {
		goto usage;
	}

	cancel_wait = false;
	action.sa_handler = int_handler;
	action.sa_flags = 0;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGINT, &action, &oldaction) < 0)
		error(EXIT_FAILURE, errno, "sigaction before wait");

	wait_all(jobnumber);

	if (sigaction(SIGINT, &oldaction, NULL) < 0)
		error(EXIT_FAILURE, errno, "sigaction after wait");
	cancel_wait = false;

	return EXIT_SUCCESS;

usage:
	printf("Usage:  wait [jobspec/pid]\n");
	return EXIT_FAILURE;
}

/* suspend 組込みコマンド
 * -f: ログインシェルでも警告しない */
int builtin_suspend(int argc, char *const *argv)
{
	bool force = false;
	int opt;

	optind = 0;
	opterr = 1;
	while ((opt = getopt(argc, argv, "f")) >= 0) {
		switch (opt) {
			case 'f':
				force = true;
				break;
			default:
				goto usage;
		}
	}
	if (optind < argc) {
		error(0, 0, "%s: invalid argument", argv[0]);
		goto usage;
	}
	if (is_loginshell && !force) {
		error(0, 0, "%s: cannot suspend a login shell;"
				"  Use `-f' option to suspend forcibly.", argv[0]);
		return EXIT_FAILURE;
	}
	if (raise(SIGSTOP) < 0) {
		error(0, errno, "%s", argv[0]);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;

usage:
	printf("Usage:  suspend [-f]\n");
	return EXIT_FAILURE;
}

/* jobs 組込みコマンド
 * -n: 変化があったジョブのみ報告する
 * args: ジョブ番号の指定 */
int builtin_jobs(int argc, char *const *argv)
{
	bool changedonly = false, printpids = false;
	int opt;

	optind = 0;
	opterr = 1;
	while ((opt = getopt(argc, argv, "ln")) >= 0) {
		switch (opt) {
			case 'l':
				printpids = true;
				break;
			case 'n':
				changedonly = true;
				break;
			default:
				goto usage;
		}
	}

	wait_all(-2 /* non-blocking */);
	
	if (optind >= argc) {  /* jobspec なし: 全てのジョブを報告 */
		print_all_job_status(changedonly, printpids);
		return EXIT_SUCCESS;
	}
	for (; optind < argc; optind++) {
		char *jobstr = argv[optind], *jobstro;
		size_t jobnumber = 0;

		// parse_jobspec を使わずに自前でやる
		// (ambiguous のときエラーにせず全部表示するため)
		if (jobstr[0] == '%')
			jobstr++;
		errno = 0;
		if (!*jobstr)
			goto invalidspec;
		jobstro = jobstr;
		jobnumber = strtol(jobstr, &jobstr, 10);
		if (errno) {
invalidspec:
			error(0, 0, "%s %s: invalid jobspec", argv[0], argv[optind]);
			return EXIT_FAILURE;
		}
		if (jobstr != jobstro && !*jobstr && 0 < jobnumber) {
			if (jobnumber < joblistlen && joblist[jobnumber].j_pgid)
				print_job_status(jobnumber, changedonly, printpids);
			else
				error(0, 0, "%%%zu: no such job", jobnumber);
			continue;
		}

		size_t len = strlen(jobstro);
		bool done = false;
		for (jobnumber = 1; jobnumber < joblistlen; jobnumber++) {
			if (joblist[jobnumber].j_pgid &&
					strncmp(jobstro, joblist[jobnumber].j_name, len) == 0) {
				print_job_status(jobnumber, changedonly, printpids);
				done = true;
			}
		}
		if (!done)
			error(0, 0, "%s: no such job", argv[optind]);
	}
	return EXIT_SUCCESS;

usage:
	printf("Usage:  jobs [-ln] [jobspecs]\n");
	return EXIT_FAILURE;
}

/* disown 組込みコマンド
 * -a:  all
 * -r:  running only
 * -h:  本当に disown するのではなく、終了時に SIGHUP を送らないようにする。 */
int builtin_disown(int argc, char *const *argv)
{
	int opt;
	bool all = false, runningonly = false, nohup = false, err = false;
	JOB *job;
	ssize_t jobnumber = currentjobnumber;
	sigset_t sigset, oldsigset;

	optind = 0;
	opterr = 1;
	while ((opt = getopt(argc, argv, "arh")) >= 0) {
		switch (opt) {
			case 'a':
				all = true;
				break;
			case 'r':
				runningonly = true;
				break;
			case 'h':
				nohup = true;
				break;
			default:
				goto usage;
		}
	}
	if (optind == argc)
		all = true;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGHUP);
	sigaddset(&sigset, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &sigset, &oldsigset) < 0)
		error(EXIT_FAILURE, errno, "sigprocmask");

	if (all) {
		for (size_t i = joblistlen; i > 0; i--) {
			job = joblist + i;
			if (!job->j_pgid ||
					(runningonly && job->j_status != JS_RUNNING))
				continue;
			if (nohup) {
				job->j_flags |= JF_NOHUP;
			} else {
				free(job->j_pids);
				free(job->j_name);
				memset(job, 0, sizeof *job);
			}
		}
	} else {
		for (; optind < argc; optind++) {
			char *target = argv[optind];

			jobnumber = parse_jobspec(target, false);
			if (jobnumber < 0) switch (jobnumber) {
				case -1:  default:
					error(0, 0, "%s: %s: invalid job spec", argv[0], target);
					err = true;
					continue;
				case -2:
					error(0, 0, "%s: %s: no such job", argv[0], target);
					err = true;
					continue;
				case -3:
					error(0, 0, "%s: %s: ambiguous job spec", argv[0], target);
					err = true;
					continue;
			}
			job = joblist + jobnumber;
			if (!job->j_pgid ||
					(runningonly && job->j_status != JS_RUNNING))
				continue;
			if (nohup) {
				job->j_flags |= JF_NOHUP;
			} else {
				free(job->j_pids);
				free(job->j_name);
				memset(job, 0, sizeof *job);
			}
		}
	}
	if (sigprocmask(SIG_SETMASK, &oldsigset, NULL) < 0)
		error(EXIT_FAILURE, errno, "sigprocmask");
	return err ? EXIT_FAILURE : EXIT_SUCCESS;

usage:
	printf("Usage:  disown [-ar] [-h] [jobspecs...]\n");
	return EXIT_FAILURE;
}

/* fg/bg 組込みコマンド */
int builtin_fg(int argc, char *const *argv)
{
	bool fg = strcmp(argv[0], "fg") == 0;
	bool err = false;
	JOB *job = NULL;
	ssize_t jobnumber = 0;
	pid_t pgid = 0;

	if (!is_interactive)
		return EXIT_FAILURE;
	if (argc < 2) {
		jobnumber = currentjobnumber;
		if (jobnumber < 1 || (size_t) jobnumber >= joblistlen
				|| !(pgid = joblist[jobnumber].j_pgid)) {
			/* カレントジョブなし: 番号の一番大きいジョブを選ぶ */
			jobnumber = joblistlen;
			while (--jobnumber > 0 && !(pgid = joblist[jobnumber].j_pgid));
			if (!jobnumber) {
				error(0, 0, "%s: there is no job", argv[0]);
				return EXIT_FAILURE;
			}
			currentjobnumber = jobnumber;
		}
		job = joblist + jobnumber;
		printf("[%zd]+ %5ld              %s\n",
				jobnumber, (long) pgid,
				job->j_name ? : "<<job>>");
		if (fg && tcsetpgrp(STDIN_FILENO, pgid) < 0) {
			error(0, errno, "%s %%%zd: tcsetpgrp", argv[0], jobnumber);
			err = true;
		}
		if (killpg(pgid, SIGCONT) < 0) {
			error(0, errno, "%s %%%zd: kill SIGCONT", argv[0], jobnumber);
			err = true;
		}
		job->j_status = JS_RUNNING;
	} else {
		if (fg && argc > 2) {
			error(0, 0, "%s: too many jobspecs", argv[0]);
			goto usage;
		}
		for (int i = 1; i < argc; i++) {
			char *jobstr = argv[i];

			jobnumber = parse_jobspec(jobstr, false);
			if (jobnumber < 0) switch (jobnumber) {
				case -1:  default:
					error(0, 0, "%s: %s: invalid job spec", argv[0], jobstr);
					err = true;
					continue;
				case -2:
					error(0, 0, "%s: %s: no such job", argv[0], jobstr);
					err = true;
					continue;
				case -3:
					error(0, 0, "%s: %s: ambiguous job spec", argv[0], jobstr);
					err = true;
					continue;
			}
			currentjobnumber = jobnumber;
			job = joblist + jobnumber;
			pgid = job->j_pgid;
			printf("[%zd]+ %5ld              %s\n",
					jobnumber, (long) pgid,
					job->j_name ? : "<<job>>");
			if (fg && tcsetpgrp(STDIN_FILENO, pgid) < 0) {
				error(0, errno, "%s %%%zd: tcsetpgrp", argv[0], jobnumber);
				err = true;
				continue;
			}
			if (killpg(pgid, SIGCONT) < 0) {
				error(0, errno, "%s %%%zd: kill SIGCONT", argv[0], jobnumber);
				err = true;
				continue;
			}
			job->j_status = JS_RUNNING;
		}
	}
	if (err)
		return EXIT_FAILURE;
	if (fg) {
		assert(0 < jobnumber && (size_t) jobnumber < joblistlen);
		wait_all(jobnumber);
		if (job->j_status == JS_DONE) {
			int exitcode = exitcode_from_status(job->j_exitstatus);
			return job->j_exitcodeneg ? !exitcode : exitcode;
		}
	}
	return EXIT_SUCCESS;

usage:
	if (fg)
		printf("Usage:  fg [jobspec]\n");
	else
		printf("Usage:  bg [jobspecs]\n");
	return EXIT_FAILURE;
}

/* cd 組込みコマンド */
int builtin_cd(int argc, char *const *argv)
{
	char *path, *oldpwd;

	if (argc < 2) {
		path = getenv(ENV_HOME);
		if (!path) {
			error(0, 0, "%s: HOME directory not specified", argv[0]);
			return EXIT_FAILURE;
		}
	} else {
		path = argv[1];
		if (strcmp(path, "-") == 0) {
			path = getenv(ENV_OLDPWD);
			if (!path) {
				error(0, 0, "%s: OLDPWD directory not specified", argv[0]);
				return EXIT_FAILURE;
			} else {
				printf("%s\n", path);
			}
		}
	}
	oldpwd = getcwd(NULL, 0);
	if (chdir(path) < 0) {
		error(0, errno, "%s: %s", argv[0], path);
		return EXIT_FAILURE;
	}
	if (oldpwd) {
		if (setenv(ENV_OLDPWD, oldpwd, 1 /* overwrite */) < 0)
			error(0, 0, "%s: failed to set env OLDPWD", argv[0]);
		free(oldpwd);
	}
	if ((path = getcwd(NULL, 0))) {
		char *spwd = collapse_homedir(path);

		if (setenv(ENV_PWD, path, 1 /* overwrite */) < 0)
			error(0, 0, "%s: failed to set env PWD", argv[0]);
		if (spwd) {
			if (setenv(ENV_SPWD, spwd, 1 /* overwrite */) < 0)
				error(0, 0, "%s: failed to set env SPWD", argv[0]);
			free(spwd);
		}
		free(path);
	}
	return EXIT_SUCCESS;
}

/* umask 組込みコマンド */
/* TODO: 数字だけでなく文字列での指定に対応 */
int builtin_umask(int argc, char *const *argv)
{
	if (argc < 2) {
		mode_t current = umask(0);
		umask(current);
		printf("%.3o\n", (unsigned) current);
	} else if (argc == 2) {
		mode_t newmask = 0;
		char *c = argv[1];

		errno = 0;
		if (*c)
			newmask = strtol(c, &c, 8);
		if (errno) {
			error(0, errno, "%s", argv[0]);
			goto usage;
		} else if (*c) {
			error(0, 0, "%s: invalid argument", argv[0]);
			goto usage;
		} else {
			umask(newmask);
		}
	} else {
		goto usage;
	}
	return EXIT_SUCCESS;

usage:
	printf("Usage:  umask [newumask]\n");
	return EXIT_FAILURE;
}

/* export 組込みコマンド
 * -n: 環境変数を削除する */
int builtin_export(int argc, char *const *argv)
{
	bool remove = false;
	int opt;

	if (argc == 1)
		goto usage;
	optind = 0;
	opterr = 1;
	while ((opt = getopt(argc, argv, "n")) >= 0) {
		switch (opt) {
			case 'n':
				remove = true;
				break;
			default:
				goto usage;
		}
	}
	for (; optind < argc; optind++) {
		char *c = argv[optind];

		if (remove) {
			if (unsetenv(c) < 0)
				error(0, 0, "%s: invalid name", argv[0]);
		} else {
			size_t namelen = strcspn(c, "=");
			if (c[namelen] == '=') {
				c[namelen] = '\0';
				if (setenv(c, c + namelen + 1, 1) < 0) {
					error(0, 0, "%s: memory shortage", argv[0]);
					c[namelen] = '=';
					return EXIT_FAILURE;
				}
				c[namelen] = '=';
			} else {
				error(0, 0, "%s: %s: invalid export format", argv[0], c);
				goto usage;
			}
		}
	}
	return EXIT_SUCCESS;

usage:
	printf("Usage:  export NAME=VALUE ...\n");
	printf("    or  export -n NAME ...\n");
	return EXIT_FAILURE;
}

/* source/. 組込みコマンド */
int builtin_source(int argc, char *const *argv)
{
	for (int i = 1; i < argc; i++)
		exec_file(argv[i], false /* don't supress errors */);
	return laststatus;
}

/* history 組込みコマンド
 * 引数なし: 全ての履歴を表示
 * 数値引数: 最近の n 個の履歴を表示
 * -c:       履歴を全て削除
 * -d n:     履歴番号 n を削除
 * -r file:  file から履歴を読み込む (今の履歴に追加)
 * -w file:  file に履歴を保存する (上書き)
 * -s X:     X を履歴に追加 */
int builtin_history(int argc, char *const *argv)
{
	int ierrno;

	using_history();
	if (argc >= 2 && strlen(argv[1]) == 2 && argv[1][0] == '-') {
		switch (argv[1][1]) {
			case 'c':
				clear_history();
				return EXIT_SUCCESS;
			case 'd':
				if (argc < 3) {
					error(0, 0, "%s: -d: missing argument", argv[0]);
					return EXIT_FAILURE;
				} else if (argc > 3) {
					error(0, 0, "%s: -d: too many arguments", argv[0]);
					return EXIT_FAILURE;
				} else {
					int pos = 0;
					char *numstr = argv[2];

					errno = 0;
					if (*numstr)
						pos = strtol(numstr, &numstr, 10);
					if (errno || *numstr)
						return EXIT_FAILURE;

					HIST_ENTRY *entry = remove_history(pos - history_base);
					if (!entry)
						return EXIT_FAILURE;
					else
						free_history_entry(entry);
				}
				return EXIT_SUCCESS;
			case 'r':
				ierrno = read_history(argv[2] ? : history_filename);
				if (ierrno) {
					error(0, ierrno, "%s", argv[0]);
					return EXIT_FAILURE;
				}
				return EXIT_SUCCESS;
			case 'w':
				ierrno = write_history(argv[2] ? : history_filename);
				if (ierrno) {
					error(0, ierrno, "%s", argv[0]);
					return EXIT_FAILURE;
				}
				return EXIT_SUCCESS;
			case 's':
				if (argc > 2) {
					char *line = strjoin(-1, argv + 2, " ");
					HIST_ENTRY *oldentry = replace_history_entry(
							where_history() - 1, line, NULL);
					if (oldentry)
						free_history_entry(oldentry);
					free(line);
				}
				return EXIT_SUCCESS;
			default:
				error(0, 0, "%s: invalid argument", argv[0]);
				goto usage;
		}
	}

	int count = INT_MAX;
	if (argc > 1) {
		char *numstr = argv[1];

		errno = 0;
		if (*numstr)
			count = strtol(numstr, &numstr, 10);
		if (errno || *numstr)
			return EXIT_FAILURE;
	}
	for (int offset = MAX(0, history_length - count);
			offset < history_length; offset++) {
		HIST_ENTRY *entry = history_get(history_base + offset);
		if (entry)
			printf("%5d\t%s\n", history_base + offset, entry->line);
	}
	return EXIT_SUCCESS;

usage:
	printf("Usage:  history [n]\n");
	printf("    or  history -d n\n");
	printf("    or  history -rw file\n");
	printf("    or  history -s arg\n");
	return EXIT_FAILURE;
}

/* alias 組込みコマンド
 * 引数なし or -p オプションありだと全てのエイリアスを出力する。
 * 引数があると、そのエイリアスを設定 or 出力する。
 * -g を指定するとグローバルエイリアスになる。 */
int builtin_alias(int argc, char *const *argv)
{
	int print_alias(const char *name, ALIAS *alias) {
		printf("%s=%s\n", name, alias->value);
		// XXX alias 出力形式
		return 0;
	}

	bool printall = argc <= 1;
	bool global = false;
	bool err = false;
	int opt;

	optind = 0;
	opterr = 1;
	while ((opt = getopt(argc, argv, "gp")) >= 0) {
		switch (opt) {
			case 'g':
				global = true;
				break;
			case 'p':
				printall = true;
				break;
			default:
				goto usage;
		}
	}
	for (; optind < argc; optind++) {
		char *c = argv[optind];
		size_t namelen = strcspn(c, "=");

		if (!namelen) {
			error(0, 0, "%s: %s: invalid argument", argv[0], c);
			err = true;
		} else if (c[namelen] == '=') {
			c[namelen] = '\0';
			set_alias(c, c + namelen + 1, global);
			c[namelen] = '=';
		} else {
			ALIAS *a = get_alias(c);
			if (a) {
				print_alias(c, a);
			} else {
				error(0, 0, "%s: %s: no such alias", argv[0], c);
				err = true;
			}
		}
	}
	if (printall)
		for_all_aliases(print_alias);
	return err ? EXIT_FAILURE : EXIT_SUCCESS;

usage:
	printf("Usage:  alias [-gp] [name[=value] ... ]\n");
	return EXIT_FAILURE;
}

/* unalias 組込みコマンド。指定した名前のエイリアスを消す。
 * -a オプションで全て消す。 */
int builtin_unalias(int argc, char *const *argv)
{
	bool removeall = false;
	bool err = false;
	int opt;

	optind = 0;
	opterr = 1;
	while ((opt = getopt(argc, argv, "a")) >= 0) {
		switch (opt) {
			case 'a':
				removeall = true;
				break;
			default:
				goto usage;
		}
	}
	if (removeall) {
		remove_all_aliases();
		return EXIT_SUCCESS;
	}
	for (; optind < argc; optind++) {
		if (remove_alias(argv[optind]) < 0) {
			err = true;
			error(0, 0, "%s: %s: no such alias", argv[0], argv[optind]);
		}
	}
	return err ? EXIT_FAILURE : EXIT_SUCCESS;

usage:
	printf("Usage:  unalias [-a] name [...]\n");
	return EXIT_FAILURE;
}

static const char *option_names[] = {
	OPT_HISTSIZE, OPT_HISTFILE, OPT_HISTFILESIZE,
	OPT_PS1, OPT_PROMPTCOMMAND, OPT_HUPONEXIT,
	NULL,
};

/* option 組込みコマンド
 * シェルのオプションを設定する */
/* 書式:  option NAME VALUE
 *   VALUE を省略すると現在値を表示する。
 *   -d オプションを付けるとデフォルトに戻す。*/
/* NAME:  histsize: 履歴に保存する数
 *        histfile: 履歴を保存するファイル
 *        histfilesize: ファイルに保存する履歴の数
 *        ps1: プロンプト文字列
 *        promptcommand: プロンプト前に実行するコマンド
 *        huponexit: 終了時に未了のジョブに SIGHUP を送る */
int builtin_option(int argc, char *const *argv)
{
	char *name, *value = NULL;
	int valuenum = 0, valuenumvalid = 0;
	bool def = false;
	int opt;

	optind = 0;
	opterr = 1;
	while ((opt = getopt(argc, argv, "d")) >= 0) {
		switch (opt) {
			case 'd':
				def = true;
				break;
			default:
				goto usage;
		}
	}
	if (optind < argc) {
		name = argv[optind++];
	} else {
		goto usage;  /* nothing to do */
	}
	if (optind < argc) {
		char *ve;

		if (def) {
			error(0, 0, "%s: invalid argument", argv[0]);
			goto usage;
		}
		value = argv[optind++];
		errno = 0;
		valuenum = strtol(value, &ve, 10);
		valuenumvalid = *value && !*ve;
	}

	if (strcmp(name, OPT_HISTSIZE) == 0) {
		if (value)
			if (valuenumvalid)
				history_histsize = valuenum;
			else
				goto valuenuminvalid;
		else if (def)
			history_histsize = 500;
		else
			printf("%s: %d\n", name, history_histsize);
	} else if (strcmp(name, OPT_HISTFILE) == 0) {
		if (value) {
			free(history_filename);
			history_filename = xstrdup(value);
		} else if (def) {
			free(history_filename);
			history_filename = NULL;
		} else {
			printf("%s: %s\n", name,
					history_filename ? history_filename : "(none)");
		}
	} else if (strcmp(name, OPT_HISTFILESIZE) == 0) {
		if (value)
			if (valuenumvalid)
				history_filesize = valuenum;
			else
				goto valuenuminvalid;
		else if (def)
			history_filesize = 500;
		else
			printf("%s: %d\n", name, history_filesize);
	} else if (strcmp(name, OPT_PS1) == 0) {
		if (value) {
			free(readline_prompt1);
			readline_prompt1 = xstrdup(value);
		} else if (def) {
			free(readline_prompt1);
			readline_prompt1 = NULL;
		} else {
			printf("%s: %s\n", name,
					readline_prompt1 ? readline_prompt1 : "(default)");
		}
	} else if (strcmp(name, OPT_PROMPTCOMMAND) == 0) {
		if (value) {
			free(prompt_command);
			prompt_command = xstrdup(value);
		} else if (def) {
			free(prompt_command);
			prompt_command = NULL;
		} else {
			printf("%s: %s\n", name,
					prompt_command ? prompt_command : "(none)");
		}
	} else if (strcmp(name, OPT_HUPONEXIT) == 0) {
		if (value) {
			if (strcasecmp(value, "yes") == 0)
				huponexit = true;
			else if (strcasecmp(value, "no") == 0)
				huponexit = false;
			else
				goto valueyesnoinvalid;
		} else if (def) {
			huponexit = false;
		} else {
			printf("%s: %s\n", name, huponexit ? "yes" : "no");
		}
	} else {
		error(0, 0, "%s: %s: unknown option", argv[0], name);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
	
valuenuminvalid:
	error(0, 0, "%s: value of `%s' must be a number", argv[0], name);
	return EXIT_FAILURE;
valueyesnoinvalid:
	error(0, 0, "%s: value of `%s' must be `yes' or `no'", argv[0], name);
	return EXIT_FAILURE;

usage:
	printf("Usage:  option NAME [VALUE]\n");
	printf("    or  option -d NAME\n");
	printf("Available options:\n");
	for (const char **optname = option_names; *optname; optname++)
		printf("\t%s\n", *optname);
	return EXIT_FAILURE;
}
