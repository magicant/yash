/* Yash: yet another shell */
/* builtin_job.c: implements shell builtin commands for job control */
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


#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "yash.h"
#include "util.h"
#include "sig.h"
#include "exec.h"
#include "path.h"
#include "builtin.h"
#include "variable.h"
#include <assert.h>

int parse_jobspec(const char *str, bool forcePercent);
int builtin_exit(int argc, char **argv);
int builtin_kill(int argc, char **argv);
int builtin_wait(int argc, char **argv);
int builtin_suspend(int argc, char **argv);
int builtin_jobs(int argc, char **argv);
int builtin_disown(int argc, char **argv);
int builtin_fg(int argc, char **argv);
int builtin_exec(int argc, char **argv);
int builtin_source(int argc, char **argv);


/* jobspec を解析する。
 * 戻り値: str の jobnumber。無効な結果ならば負数。
 *         -1: 不正な書式   -2: ジョブが存在しない   -3: 指定が曖昧 */
int parse_jobspec(const char *str, bool forcePercent)
{
	int jobnumber;
	char *strend;

	if (str[0] == '%') {
		str++;
		if (!*str) {  /* カレントジョブを探す */
			if (get_job(currentjobnumber))
				return currentjobnumber;
			else
				return -2;
		}
	} else if (forcePercent) {
		return -1;
	}
	if (!*str)
		return -1;
	errno = 0;
	jobnumber = strtol(str, &strend, 10);
	if (errno)
		return -2;
	if (str != strend && !strend[0] && 0 < jobnumber) {
		if (get_job(jobnumber))
			return jobnumber;
		else
			return -2;
	}

	jobnumber = 0;
	for (int i = 1; i < (ssize_t) joblist.length; i++) {
		JOB *job = get_job(i);
		if (job && hasprefix(job->j_name, str)) {
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

/* exit/logout 組込みコマンド
 * logout では、ログインシェルでないときにエラーを出す。
 * -f: ジョブが残っていても exit する。 */
int builtin_exit(int argc, char **argv)
{
	bool forceexit = false;
	int opt;
	int status = laststatus;

	if (strcmp(argv[0], "logout") == 0 && !is_loginshell) {
		xerror(0, 0, "%s: not login shell: use `exit'", argv[0]);
		return EXIT_FAILURE;
	}

	xoptind = 0;
	xopterr = true;
	while ((opt = xgetopt(argv, "f")) >= 0) {
		switch (opt) {
			case 'f':
				forceexit = true;
				break;
			default:
				goto usage;
		}
	}
	if (xoptind < argc) {
		char *c = argv[xoptind];

		errno = 0;
		if (*c)
			status = strtol(c, &c, 0);
		if (errno || *c) {
			xerror(0, 0, "%s: invalid argument", argv[0]);
			goto usage;
		}
	}
	if (is_interactive_now && !forceexit) {
		wait_chld();
		print_all_job_status(true /* changed only */, false /* not verbose */);
		if (stopped_job_count()) {
			xerror(0, 0, "There are stopped jobs!"
					"  Use `-f' option to exit anyway.");
			return EXIT_FAILURE;
		}
	}

	yash_exit(status);
	assert(false);

usage:
	fprintf(stderr, "Usage:  exit/logout [-f] [exitcode]\n");
	return EXIT_FAILURE;
}

/* kill 組込みコマンド
 * -s signal:   シグナルの指定。デフォルトは TERM。
 * -l [signal]: シグナル番号とシグナル名を相互変換する。 */
int builtin_kill(int argc, char **argv)
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
				xerror(0, 0, "%s: %s: invalid signal", argv[0], argv[1] + 2);
				return EXIT_FAILURE;
			}
		} else if (xisupper(argv[1][1]) || xisdigit(argv[1][1])) {
			sig = get_signal(argv[1] + 1);
			if (!sig) {
				xerror(0, 0, "%s: %s: invalid signal", argv[0], argv[1] + 1);
				return EXIT_FAILURE;
			}
		} else if (argv[1][1] == 'l') {
			list = true;
		} else if (argv[1][1] != '-') {
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

		if (strcmp(target, "--") == 0)
			continue;
		if (isjob) {
			targetnum = parse_jobspec(target, true);
			if (targetnum < 0) switch (targetnum) {
				case -1:  default:
					xerror(0, 0, "%s: %s: invalid job spec", argv[0], argv[i]);
					err = true;
					continue;
				case -2:
					xerror(0, 0, "%s: %s: no such job", argv[0], argv[i]);
					err = true;
					continue;
				case -3:
					xerror(0, 0, "%s: %s: ambiguous job spec", argv[0],argv[i]);
					err = true;
					continue;
			}
			targetpid = -(get_job(targetnum)->j_pgid);
		} else {
			errno = 0;
			if (*target)
				targetnum = strtol(target, &target, 10);
			if (errno || *target) {
				xerror(0, 0, "%s: %s: invalid target", argv[0], argv[i]);
				err = true;
				continue;
			}
			targetpid = (pid_t) targetnum;
		}
		if (kill(targetpid, sig) < 0) {
			xerror(0, errno, "%s: %s", argv[0], argv[i]);
			err = true;
		}
	}
	return err ? EXIT_FAILURE : EXIT_SUCCESS;

list:
	if (argc <= 2) {  /* no args: list all */
		if (posixly_correct) {
			for (size_t i = 0; sigdata[i].s_signal; i++) {
				printf("%s%c", sigdata[i].s_name,
						i % 10 == 9 || !sigdata[i+1].s_signal ? '\n' : ' ');
			}
#if defined(SIGRTMIN) && defined(SIGRTMAX)
			int min = SIGRTMIN, max = SIGRTMAX;
			for (int num = SIGRTMIN; num <= max; num++) {
				printf("RTMIN+%d%c", num - min,
						(num - min) % 8 == 7 || num == max ? '\n' : ' ');
			}
#endif
		} else {
			for (size_t i = 0; sigdata[i].s_signal; i++) {
				printf("%2d: %-10s%s", sigdata[i].s_signal, sigdata[i].s_name,
						i % 4 == 3 || !sigdata[i+1].s_signal ? "\n" : "    ");
			}
#if defined(SIGRTMIN) && defined(SIGRTMAX)
			int min = SIGRTMIN, max = SIGRTMAX;
			for (int num = SIGRTMIN; num <= max; num++) {
				printf("%2d: RTMIN+%-4d%s", num, num - min,
						(num - min) % 4 == 3 || num == max ? "\n" : "    ");
			}
#endif
		}
	} else {
		for (i = 2; i < argc; i++) {
			const char *name;

			if (strcmp(argv[i], "--") == 0)
				continue;
			sig = get_signal(argv[i]);
			name = get_signal_name(sig);
			if (!sig || !name) {
				xerror(0, 0, "%s: %s: invalid signal", argv[0], argv[i]);
				err = true;
			} else {
				if (xisdigit(argv[i][0]))
					printf("%s\n", name);
				else
					printf("%d\n", sig);
			}
		}
	}
	return err ? EXIT_FAILURE : EXIT_SUCCESS;

usage:
	fprintf(stderr, "Usage:  kill [-s signal] pid/jobspec ...\n");
	fprintf(stderr, "    or  kill -l [signals]\n");
	return EXIT_FAILURE;
}

/* wait 組込みコマンド */
int builtin_wait(int argc, char **argv)
{
	sigset_t newset, oldset;
	bool interrupted = false;
	int resultstatus = 0;

	sigemptyset(&newset);
	sigaddset(&newset, SIGINT);
	sigemptyset(&oldset);
	if (sigprocmask(SIG_BLOCK, &newset, &oldset) < 0)
		xerror(0, errno, "sigprocmask before wait");
	sigint_received = false;

	if (argc < 2) {  /* 引数無し: 全ジョブを待つ */
		for (;;) {
			if (is_interactive_now && !posixly_correct)
				print_all_job_status(true /* changed only */, false);
			if (is_interactive_now ? !running_job_count() : !undone_job_count())
				break;
			wait_for_signal();
			if (sigint_received) {
				interrupted = true;
				break;
			}
		}
		if (!is_interactive_now)
			remove_exited_jobs();
	} else {  /* 引数で指定されたジョブを待つ */
		for (int i = 1; !interrupted && i < argc; i++) {
			int jobnumber = 0;
			char *target = argv[i];
			bool isjob = target[0] == '%';

			if (target[0] == '-') {
				xerror(0, 0, "%s: %s: illegal option",
						argv[0], target);
				goto usage;
			} else if (isjob) {
				jobnumber = parse_jobspec(target, true);
				if (jobnumber < 0) switch (jobnumber) {
					case -1:
					default:
						xerror(0, 0, "%s: %s: invalid job spec",
								argv[0], target);
						resultstatus = EXIT_NOTFOUND;
						goto end;
					case -2:
						xerror(0, 0, "%s: %s: no such job",
								argv[0], target);
						resultstatus = EXIT_NOTFOUND;
						goto end;
					case -3:
						xerror(0, 0, "%s: %s: ambiguous job spec",
								argv[0], target);
						resultstatus = EXIT_NOTFOUND;
						goto end;
				}
			} else {
				errno = 0;
				if (*target)
					jobnumber = strtol(target, &target, 10);
				if (errno || *target) {
					xerror(0, 0, "%s: %s: invalid target", argv[0], target);
					resultstatus = EXIT_FAILURE;
					goto end;
				}
				jobnumber = get_jobnumber_from_pid(jobnumber);
				if (jobnumber < 0) {
					xerror(0, 0, "%s: %s: not a child of this shell",
							argv[0], target);
					resultstatus = EXIT_NOTFOUND;
					goto end;
				}
			}

			JOB *job = joblist.contents[jobnumber];
			for (;;) {
				if (is_interactive_now && !posixly_correct)
					print_all_job_status(true /* changed only */, false);
				if (job->j_status == JS_DONE
						|| (is_interactive_now && job->j_status == JS_STOPPED))
					break;
				wait_for_signal();
				if (sigint_received) {
					interrupted = true;
					break;
				}
			}
			resultstatus = exitcode_from_status(job->j_waitstatus);
			if (!is_interactive_now) {
				assert(job->j_status == JS_DONE);
				remove_job(jobnumber);
			}
			/* POSIX は、最後の引数に対応するジョブの終了コードを wait
			 * コマンドの終了コードにすることを定めている。
			 * 他の引数のジョブを待っている間に最後の引数のジョブが終了した場合
			 * print_all_job_status をするとジョブ情報がなくなってしまうので
			 * 正しい終了コードが得られない。 */
		}
	}

end:
	if (sigprocmask(SIG_SETMASK, &oldset, NULL) < 0)
		xerror(0, errno, "sigprocmask after wait");

	return interrupted ? 128 + SIGINT : resultstatus;

usage:
	if (sigprocmask(SIG_SETMASK, &oldset, NULL) < 0)
		xerror(0, errno, "sigprocmask after wait");
	fprintf(stderr, "Usage:  wait [jobspec/pid]...\n");
	return EXIT_FAILURE;
}

/* suspend 組込みコマンド
 * -f: ログインシェルでも警告しない */
int builtin_suspend(int argc, char **argv)
{
	bool force = false;
	int opt;

	xoptind = 0;
	xopterr = true;
	while ((opt = xgetopt(argv, "f")) >= 0) {
		switch (opt) {
			case 'f':
				force = true;
				break;
			default:
				goto usage;
		}
	}
	if (xoptind < argc) {
		xerror(0, 0, "%s: invalid argument", argv[0]);
		goto usage;
	}
	if (is_interactive_now && is_loginshell && !force) {
		xerror(0, 0, "%s: cannot suspend a login shell;"
				"  Use `-f' option to suspend anyway.", argv[0]);
		return EXIT_FAILURE;
	}

	/* このシェルがサブシェルで、親シェルがこのシェルをジョブ制御している場合、
	 * このシェルのプロセスグループ ID を元に戻してやらないと、このシェルが
	 * サスペンドしたことを親シェルが正しく認識できないかもしれない。
	 * また、元のプロセスグループにこのシェル以外のプロセスがあった場合、
	 * それらも一緒にサスペンドしないと、親シェルを混乱させるかもしれない。 */
	restore_pgid();
#if 1
	if (killpg(getpgrp(), SIGSTOP) < 0) {
#else
	if (raise(SIGSTOP) < 0) {
#endif
		set_unique_pgid();
		xerror(0, errno, "%s", argv[0]);
		return EXIT_FAILURE;
	}
	set_unique_pgid();
	return EXIT_SUCCESS;

usage:
	fprintf(stderr, "Usage:  suspend [-f]\n");
	return EXIT_FAILURE;
}

/* jobs 組込みコマンド
 * -l: 全てのプロセス番号を表示する。
 * -n: 変化があったジョブのみ報告する
 * args: ジョブ番号の指定 */
int builtin_jobs(int argc, char **argv)
{
	bool changedonly = false, printpids = false;
	bool err = false;
	int opt;

	xoptind = 0;
	xopterr = true;
	while ((opt = xgetopt(argv, "ln")) >= 0) {
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

	wait_chld();
	
	if (xoptind >= argc) {  /* jobspec なし: 全てのジョブを報告 */
		print_all_job_status(changedonly, printpids);
		return EXIT_SUCCESS;
	}
	for (; xoptind < argc; xoptind++) {
		char *jobstr = argv[xoptind], *jobstro;
		size_t jobnumber = 0;

		// parse_jobspec を使わずに自前でやる
		// (ambiguous のときエラーにせず全部表示するため)
		if (jobstr[0] == '%') {
			jobstr++;
			if (!jobstr[0]) {  /* カレントジョブを表示 */
				jobnumber = currentjobnumber;
				goto singlespec;
			}
		}
		errno = 0;
		if (!*jobstr)
			goto invalidspec;
		jobstro = jobstr;
		jobnumber = strtol(jobstr, &jobstr, 10);
		if (errno) {
invalidspec:
			xerror(0, 0, "%s: %s: invalid jobspec", argv[0], argv[xoptind]);
			err = true;
			continue;
		}
		if (jobstr != jobstro && !*jobstr && 0 < jobnumber) {
singlespec:
			if (get_job(jobnumber)) {
				print_job_status(jobnumber, changedonly, printpids);
			} else {
				xerror(0, 0, "%s: %s: no such job", argv[0], argv[xoptind]);
				err = true;
			}
			continue;
		}

		bool done = false;
		for (jobnumber = 1; jobnumber < joblist.length; jobnumber++) {
			JOB *job = get_job(jobnumber);
			if (job && hasprefix(job->j_name, jobstro)) {
				print_job_status(jobnumber, changedonly, printpids);
				done = true;
			}
		}
		if (!done) {
			xerror(0, 0, "%s: %s: no such job", argv[0], argv[xoptind]);
			err = true;
		}
	}
	return err ? EXIT_FAILURE : EXIT_SUCCESS;

usage:
	fprintf(stderr, "Usage:  jobs [-ln] [jobspecs]\n");
	return EXIT_FAILURE;
}

/* disown 組込みコマンド
 * -a:  all
 * -r:  running only
 * -h:  本当に disown するのではなく、終了時に SIGHUP を送らないようにする。 */
int builtin_disown(int argc, char **argv)
{
	int opt;
	bool all = false, runningonly = false, nohup = false, err = false;
	JOB *job;
	ssize_t jobnumber = currentjobnumber;

	xoptind = 0;
	xopterr = true;
	while ((opt = xgetopt(argv, "arh")) >= 0) {
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
	if (xoptind == argc)
		all = true;

	if (all) {
		for (size_t i = joblist.length; i > 0; i--) {
			job = get_job(i);
			if (!job || (runningonly && job->j_status != JS_RUNNING))
				continue;
			if (nohup) {
				job->j_flags |= JF_NOHUP;
			} else {
				remove_job(i);
			}
		}
	} else {
		for (; xoptind < argc; xoptind++) {
			char *target = argv[xoptind];

			jobnumber = parse_jobspec(target, false);
			if (jobnumber < 0) switch (jobnumber) {
				case -1:  default:
					xerror(0, 0, "%s: %s: invalid job spec", argv[0], target);
					err = true;
					continue;
				case -2:
					xerror(0, 0, "%s: %s: no such job", argv[0], target);
					err = true;
					continue;
				case -3:
					xerror(0, 0, "%s: %s: ambiguous job spec", argv[0], target);
					err = true;
					continue;
			}
			job = get_job(jobnumber);
			if (!job || (runningonly && job->j_status != JS_RUNNING))
				continue;
			if (nohup) {
				job->j_flags |= JF_NOHUP;
			} else {
				remove_job(jobnumber);
			}
		}
	}
	return err ? EXIT_FAILURE : EXIT_SUCCESS;

usage:
	fprintf(stderr, "Usage:  disown [-ar] [-h] [jobspecs...]\n");
	return EXIT_FAILURE;
}

/* fg/bg 組込みコマンド */
int builtin_fg(int argc, char **argv)
{
	bool fg = strcmp(argv[0], "fg") == 0;
	bool err = false;
	JOB *job = NULL;
	ssize_t jobnumber = 0;
	pid_t pgid = 0;

	if (!is_interactive_now) {
		xerror(0, 0, "%s: no job control", argv[0]);
		return EXIT_FAILURE;
	}
	if (argc < 2) {
		jobnumber = currentjobnumber;
		if (jobnumber < 1 || !(job = get_job(jobnumber))) {
			/* カレントジョブなし: 番号の一番大きいジョブを選ぶ */
			jobnumber = joblist.length;
			while (--jobnumber > 0 && !get_job(jobnumber));
			if (!jobnumber) {
				xerror(0, 0, "%s: there is no job", argv[0]);
				return EXIT_FAILURE;
			}
			currentjobnumber = jobnumber;
			job = get_job(jobnumber);
		}
		pgid = job->j_pgid;
		if (posixly_correct)
			printf(fg ? "%s\n" : "[%2$zd] %1$s &\n", job->j_name, jobnumber);
		else
			printf("[%zd]+ %5ld              %s%s\n",
					jobnumber, (long) pgid, job->j_name, fg ? "" : " &");
		if (fg && tcsetpgrp(STDIN_FILENO, pgid) < 0) {
			if (errno == EPERM || errno == ESRCH) {
				xerror(0, 0, "%s %%%zd: job has terminated", argv[0],jobnumber);
				return EXIT_FAILURE;
			} else {
				xerror(0, errno, "%s %%%zd: tcsetpgrp", argv[0], jobnumber);
				err = true;
			}
		}
		if (killpg(pgid, SIGCONT) < 0) {
			if (errno == ESRCH) {
				xerror(0, 0, "%s %%%zd: job has terminated", argv[0],jobnumber);
				return EXIT_FAILURE;
			} else {
				xerror(0, errno, "%s %%%zd: kill SIGCONT", argv[0], jobnumber);
				err = true;
			}
		}
		job->j_status = JS_RUNNING;
	} else {
		if (fg && argc > 2) {
			xerror(0, 0, "%s: too many jobspecs", argv[0]);
			goto usage;
		}
		for (int i = 1; i < argc; i++) {
			char *jobstr = argv[i];

			jobnumber = parse_jobspec(jobstr, false);
			if (jobnumber < 0) switch (jobnumber) {
				case -1:  default:
					xerror(0, 0, "%s: %s: invalid job spec", argv[0], jobstr);
					err = true;
					continue;
				case -2:
					xerror(0, 0, "%s: %s: no such job", argv[0], jobstr);
					err = true;
					continue;
				case -3:
					xerror(0, 0, "%s: %s: ambiguous job spec", argv[0], jobstr);
					err = true;
					continue;
			}
			currentjobnumber = jobnumber;
			job = get_job(jobnumber);
			pgid = job->j_pgid;
			if (posixly_correct)
				printf(fg ? "%s\n" : "[%2$zd] %1$s &\n",
						job->j_name, jobnumber);
			else
				printf("[%zd]+ %5ld              %s%s\n",
						jobnumber, (long) pgid, job->j_name, fg ? "" : " &");
			if (fg && tcsetpgrp(STDIN_FILENO, pgid) < 0) {
				if (errno == EPERM || errno == ESRCH) {
					xerror(0, 0, "%s %%%zd: job has terminated",
							argv[0], jobnumber);
				} else {
					xerror(0, errno, "%s %%%zd: tcsetpgrp",
							argv[0], jobnumber);
				}
				err = true;
				continue;
			}
			if (killpg(pgid, SIGCONT) < 0) {
				if (errno == EPERM || errno == ESRCH) {
					xerror(0, 0, "%s %%%zd: job has terminated",
							argv[0], jobnumber);
				} else {
					xerror(0, errno, "%s %%%zd: kill SIGCONT",
							argv[0], jobnumber);
				}
				err = true;
				continue;
			}
			job->j_status = JS_RUNNING;
		}
	}
	if (err)
		return EXIT_FAILURE;
	if (fg) {
		assert(0 < jobnumber && (size_t) jobnumber < joblist.length);
		while (job->j_status == JS_RUNNING)
			wait_for_signal();
		if (job->j_status == JS_DONE) {
			int r = exitcode_from_status(job->j_waitstatus);
			if (WIFSIGNALED(job->j_waitstatus)) {
				int sig = WTERMSIG(job->j_waitstatus);
				if (sig != SIGINT && sig != SIGPIPE)
					psignal(sig, NULL);  /* XXX : not POSIX */
			}
			remove_job((size_t) jobnumber);
			return r;
		} else if (job->j_status == JS_STOPPED) {
			fflush(stdout);
			fputs("\n", stderr);
			fflush(stderr);
		}
	}
	return EXIT_SUCCESS;

usage:
	if (fg)
		fprintf(stderr, "Usage:  fg [jobspec]\n");
	else
		fprintf(stderr, "Usage:  bg [jobspecs]\n");
	return EXIT_FAILURE;
}

/* exec 組込みコマンド
 * この関数が返るのはエラーが起きた時または引数がない時だけである。
 * -c:       環境変数なしで execve する。
 * -f:       対話的シェルで未了のジョブがあっても execve する。
 * -l:       ログインコマンドとして新しいコマンドを execve する。
 * -a name:  execve するとき argv[0] として name を渡す。 */
int builtin_exec(int argc, char **argv)
{
	int opt;
	bool clearenv = false, forceexec = false, login = false;
	char *argv0 = NULL;

	xoptind = 0;
	xopterr = true;
	while ((opt = xgetopt(argv, "+cfla:")) >= 0) {
		switch (opt) {
			case 'c':
				clearenv = true;
				break;
			case 'f':
				forceexec = true;
				break;
			case 'l':
				login = true;
				break;
			case 'a':
				argv0 = xoptarg;
				break;
			default:
				goto usage;
		}
	}

	if (is_interactive_now && !forceexec) {
		wait_chld();
		print_all_job_status(true /* changed only */, false /* not verbose */);
		if (stopped_job_count()) {
			xerror(0, 0, "There are stopped jobs!"
					"  Use `-f' option to exec anyway.");
			return EXIT_FAILURE;
		}
	}
	if (argc <= xoptind)
		return EXIT_SUCCESS;
	if (!argv0)
		argv0 = argv[xoptind];

	char *command = which(argv[xoptind],
			strchr(argv[xoptind], '/') ? "." : getvar(VAR_PATH),
			is_executable);
	if (!command) {
		xerror(0, 0, "%s: %s: command not found", argv[0], argv[xoptind]);
		if (!is_interactive_now) {
			//XXX shopt execfail
			exit(EXIT_NOTFOUND);
		}
		return EXIT_NOTFOUND;
	}

	struct plist args;
	pl_init(&args);
	if (login) {
		char *newargv0 = xmalloc(strlen(argv0) + 2);
		newargv0[0] = '-';
		strcpy(newargv0 + 1, argv0);
		pl_append(&args, newargv0);
	} else {
		pl_append(&args, xstrdup(argv0));
	}
	for (int i = xoptind + 1; i < argc; i++)
		pl_append(&args, argv[i]);

	unset_shell_env();
	execve(command, (char **) args.contents,
			clearenv ? (char *[]) { NULL, } : environ);
	set_shell_env();

	xerror(0, errno, "%s: %s", argv[0], argv[xoptind]);
	free(args.contents[0]);
	free(command);
	pl_destroy(&args);
	return EXIT_NOEXEC;

usage:
	fprintf(stderr, "Usage:  exec [-cfl] [-a name] command [args...]\n");
	return EXIT_FAILURE;
}

/* source/. 組込みコマンド */
int builtin_source(int argc, char **argv)
{
	if (argc < 2) {
		xerror(0, 0, "%s: filename not given", argv[0]);
		goto usage;
	}
	exec_file(argv[1], false /* don't supress errors */);
	return laststatus;

usage:
	fprintf(stderr, "Usage:  %s filename\n", argv[0]);
	return EXIT_FAILURE;
}
