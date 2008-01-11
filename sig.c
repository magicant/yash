/* Yash: yet another shell */
/* signal.c: signal management */
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


#define  _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "util.h"
#include "sig.h"
#include "lineinput.h"
#include "exec.h"
#include <assert.h>


/* yash のシグナルハンドリングに付いて:
 * シェルの状態にかかわらず、SIGHUP と SIGCHLD は常に sig_handler
 * シグナルハンドラで受信する。sig_handler はシグナルを受信したことを示す
 * フラグを立てるだけで、実際にシグナルに対する処理を行うのは handle_signals
 * である。対話的シェルでは SIGINT も sig_handler で受信する。
 * SIGQUIT は常に無視する。
 * 対話的シェルでは、ignsignals に含まれるシグナルも無視する。 */

/* シェルで無視するシグナルのリスト */
static const int ignsignals[] = {
	SIGTERM, SIGTSTP, SIGTTIN, SIGTTOU, 0,
};

#if 0
static void debug_sig(int signal)
{
	xerror(0, 0, "DEBUG: received signal %d. pid=%ld", signal, (long) getpid());
}
#endif

static volatile sig_atomic_t sighup_received = false;
static volatile sig_atomic_t sigchld_received = false;
       volatile sig_atomic_t sigint_received = false;

const SIGDATA const sigdata[] = {
	/* POSIX.1-1990 signals */
	{ SIGHUP, "HUP", }, { SIGINT, "INT", }, { SIGQUIT, "QUIT", },
	{ SIGILL, "ILL", }, { SIGABRT, "ABRT", }, { SIGFPE, "FPE", },
	{ SIGKILL, "KILL", }, { SIGSEGV, "SEGV", }, { SIGPIPE, "PIPE", },
	{ SIGALRM, "ALRM", }, { SIGTERM, "TERM", }, { SIGUSR1, "USR1", },
	{ SIGUSR2, "USR2", }, { SIGCHLD, "CHLD", }, { SIGCONT, "CONT", },
	{ SIGSTOP, "STOP", }, { SIGTSTP, "TSTP", }, { SIGTTIN, "TTIN", },
	{ SIGTTOU, "TTOU", },
	/* SUSv2 & POSIX.1-2001 (SUSv3) signals */
	{ SIGBUS, "BUS", }, { SIGPOLL, "POLL", }, { SIGPROF, "PROF", },
	{ SIGSYS, "SYS", }, { SIGTRAP, "TRAP", }, { SIGURG, "URG", },
	{ SIGVTALRM, "VTALRM", }, { SIGXCPU, "XCPU", }, { SIGXFSZ, "XFSZ", },
	/* Other signals */
#ifdef SIGIOT
	{ SIGIOT, "IOT", },
#endif
#ifdef SIGEMT
	{ SIGEMT, "EMT", },
#endif
#ifdef SIGSTKFLT
	{ SIGSTKFLT, "STKFLT", },
#endif
#ifdef SIGIO
	{ SIGIO, "IO", },
#endif
#ifdef SIGCLD
	{ SIGCLD, "CLD", },
#endif
#ifdef SIGPWR
	{ SIGPWR, "PWR", },
#endif
#ifdef SIGINFO
	{ SIGINFO, "INFO", },
#endif
#ifdef SIGLOST
	{ SIGLOST, "LOST", },
#endif
#ifdef SIGWINCH
	{ SIGWINCH, "WINCH", },
#endif
#ifdef SIGMSG
	{ SIGMSG, "MSG", },
#endif
#ifdef SIGDANGER
	{ SIGDANGER, "DANGER", },
#endif
#ifdef SIGMIGRATE
	{ SIGMIGRATE, "MIGRATE", },
#endif
#ifdef SIGPRE
	{ SIGPRE, "PRE", },
#endif
#ifdef SIGGRANT
	{ SIGGRANT, "GRANT", },
#endif
#ifdef SIGRETRACT
	{ SIGRETRACT, "RETRACT", },
#endif
#ifdef SIGSOUND
	{ SIGSOUND, "SOUND", },
#endif
#ifdef SIGUNUSED
	{ SIGUNUSED, "UNUSED", },
#endif
	{ 0, NULL, },
	/* 番号 0 のシグナルは存在しない (C99 7.14) */
};

/* シグナル名に対応するシグナルの番号を返す。
 * シグナルが見付からないときは 0 を返す。 */
int get_signal(const char *name)
{
	const SIGDATA *sd = sigdata;

	assert(name != NULL);
	if (xisdigit(name[0])) {  /* name は番号 */
		char *end;
		int num;

		errno = 0;
		num = strtol(name, &end, 10);
		if (!errno && name[0] && !end[0])
			return num;
	} else {  /* name は名前 */
		if (hasprefix(name, "SIG") == 2)
			name += 3;
#if defined(SIGRTMAX) && defined(SIGRTMIN)
		if (strcmp(name, "RTMIN") == 0) {
			return SIGRTMIN;
		} else if (strcmp(name, "RTMAX") == 0) {
			return SIGRTMAX;
		} else if (hasprefix(name, "RTMIN+")) {
			char *end;
			int num;
			name += 6;
			errno = 0;
			num = strtol(name, &end, 10);
			if (!errno && name[0] && !end[0]) {
				num += SIGRTMIN;
				if (SIGRTMIN <= num && num <= SIGRTMAX)
					return num;
			}
		} else if (hasprefix(name, "RTMAX-")) {
			char *end;
			int num;
			name += 6;
			errno = 0;
			num = strtol(name, &end, 10);
			if (!errno && name[0] && !end[0]) {
				num = SIGRTMAX - num;
				if (SIGRTMIN <= num && num <= SIGRTMAX)
					return num;
			}
		}
#endif
		while (sd->s_name) {
			if (strcmp(name, sd->s_name) == 0)
				return sd->s_signal;
			sd++;
		}
	}
	return 0;
}

/* シグナル番号からシグナル名を得る。
 * シグナル名が得られないときは NULL を返す。
 * 戻り値は静的記憶域へのポインタであり、次にこの関数を呼ぶまで有効である。 */
const char *get_signal_name(int signal)
{
	const SIGDATA *sd;

	if (signal >= TERMSIGOFFSET)
		signal -= TERMSIGOFFSET;
	else if (signal >= 128)
		signal -= 128;
	for (sd = sigdata; sd->s_signal; sd++)
		if (sd->s_signal == signal)
			return sd->s_name;
#if defined(SIGRTMIN) && defined(SIGRTMAX)
	static char rtminname[16];
	int min = SIGRTMIN, max = SIGRTMAX;
	if (min <= signal && signal <= max) {
		snprintf(rtminname, sizeof rtminname, "RTMIN+%d", signal - min);
		return rtminname;
	}
#endif
	return NULL;
}

/* シグナルを説明する文字列を返す。
 * 戻り値は静的記憶域へのポインタであり、次にこの関数を呼ぶまで有効である。 */
const char *xstrsignal(int signal)
{
#ifdef HAVE_STRSIGNAL
	return strsignal(signal);
#else
	static char str[20] = "SIG";   // XXX 名前の長さに注意
	const char *name = get_signal_name(signal);
	strcpy(str + 3, name ? name : "???");
	return str;
#endif
}

/* SIGHUP, SIGCHLD, SIGINT, SIGQUIT のハンドラ */
static void sig_handler(int sig)
{
	switch (sig) {
		case SIGHUP:
			sighup_received = true;
			break;
		case SIGCHLD:
			sigchld_received = true;
			break;
		case SIGINT:
			sigint_received = true;
			break;
		case SIGQUIT:
			break;
	}
}

/* シグナルマスク・シグナルハンドラを初期化する。
 * この関数は main で一度だけ呼ばれる。 */
void init_signal(void)
{
	struct sigaction action;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;

	action.sa_handler = sig_handler;
	if (sigaction(SIGHUP, &action, NULL) < 0)
		xerror(0, errno, "sigaction: signal=SIGHUP");
	if (sigaction(SIGCHLD, &action, NULL) < 0)
		xerror(0, errno, "sigaction: signal=SIGCHLD");
	if (sigaction(SIGQUIT, &action, NULL) < 0)
		xerror(0, errno, "sigaction: signal=SIGQUIT");
	/* SIGQUIT にとって、sig_handler は何もしないシグナルハンドラなので、
	 * シグナル受信時の挙動は SIG_IGN と同じだが、exec の実行時に
	 * デフォルトの挙動に戻される点が異なる。 */

#if 0
	action.sa_handler = debug_sig;
	fprintf(stderr, "DEBUG: setting all sigaction\n");
	for (int i = 1; i < 32; i++)
		if (sigaction(i, &action, NULL) < 0) ;
#endif

	/* 全てのシグナルマスクをクリアする */
	sigemptyset(&action.sa_mask);
	if (sigprocmask(SIG_SETMASK, &action.sa_mask, NULL) < 0)
		xerror(0, errno, "sigprocmask(SETMASK, nothing)");
}

/* 対話的シェル用シグナルハンドラを初期化する。
 * この関数は unset_signals の後再び呼び直されることがある。 */
void set_signals(void)
{
	struct sigaction action;
	const int *signals;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = SIG_IGN;
	for (signals = ignsignals; *signals; signals++)
		if (sigaction(*signals, &action, NULL) < 0)
			xerror(0, errno, "sigaction: signal=%d", *signals);

	action.sa_handler = sig_handler;
	if (sigaction(SIGINT, &action, NULL) < 0)
		xerror(0, errno, "sigaction: signal=SIGINT");
}

/* 対話的シェル用シグナルハンドラを元に戻す */
void unset_signals(void)
{
	struct sigaction action;
	const int *signals;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = SIG_DFL;
	for (signals = ignsignals; *signals; signals++)
		if (sigaction(*signals, &action, NULL) < 0)
			xerror(0, errno, "sigaction: signal=%d", *signals);
	if (sigaction(SIGINT, &action, NULL) < 0)
		xerror(0, errno, "sigaction: signal=SIGINT");
}

/* sigsuspend でシグナルを待ち、受け取ったシグナルに対して処理を行う。
 * この関数で待つシグナルは、SIGHUP と SIGCHLD と SIGINT である。
 * SIGHUP を受け取ると、シェルは終了するのでこの関数は返らない。*/
void wait_for_signal(void)
{
	sigset_t ss, oldss;

	sigemptyset(&ss);
	sigaddset(&ss, SIGHUP);
	sigaddset(&ss, SIGCHLD);
	sigaddset(&ss, SIGINT);
	sigemptyset(&oldss);
	if (sigprocmask(SIG_BLOCK, &ss, &oldss) < 0)
		xerror(2, errno, "sigprocmask before sigsuspend");

	ss = oldss;
	sigdelset(&ss, SIGHUP);
	sigdelset(&ss, SIGCHLD);
	sigdelset(&ss, SIGINT);
	for (;;) {
		bool received = sighup_received || sigchld_received || sigint_received;
		handle_signals();
		if (received)
			break;
		if (sigsuspend(&ss) < 0 && errno != EINTR)
			xerror(0, errno, "sigsuspend");
	}

	if (sigprocmask(SIG_SETMASK, &oldss, NULL) < 0)
		xerror(2, errno, "sigprocmask after sigsuspend");
}

/* 受け取ったシグナルがあればそれに対して処理を行い、フラグをリセットする。
 * SIGHUP を受け取ると、シェルは終了するのでこの関数は返らない。 */
void handle_signals(void)
{
	if (sighup_received) {
		struct sigaction action;
		sigset_t ss;

		action.sa_flags = 0;
		action.sa_handler = SIG_DFL;
		sigemptyset(&action.sa_mask);
		if (sigaction(SIGHUP, &action, NULL) < 0)
			xerror(2, errno, "sigaction(SIGHUP)");

		send_sighup_to_all_jobs();
		finalize_readline();

		sigemptyset(&ss);
		sigaddset(&ss, SIGHUP);
		if (sigprocmask(SIG_UNBLOCK, &ss, NULL) < 0)
			xerror(2, errno, "sigprocmask before raising SIGHUP");

		raise(SIGHUP);
		assert(false);
	}
	if (sigchld_received) {
		wait_chld();
		sigchld_received = 0;
	}
	/* 注: SIGINT は wait 組込みコマンドが扱うので、ここでは扱わない。 */
}

