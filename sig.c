/* Yash: yet another shell */
/* sig.c: signal handling */
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
#include <stdbool.h>
#include <signal.h>
#include <sys/select.h>
#include "option.h"
#include "util.h"
#include "sig.h"
#include "job.h"
#include "signum.h"


/* このシェルのシグナルの扱いについて:
 * このシェルは、どんなときでも SIGCHLD, SIGHUP, SIGQUIT を捕捉する。
 * ジョブ制御が有効ならば、SIGTTOU と SIGTSTP も捕捉する。
 * 対話的シェルでは、SIGINT と SIGTERM も捕捉する。
 * それ以外のシグナルは、トラップに従って捕捉する。
 * シグナルのブロックは、wait_for_sigchld で SIGCHLD/SIGHUP をブロックする
 * 以外は行わない。 */


static void sig_handler(int signum);

/* シグナルの情報を管理する構造体 */
typedef struct signal_T {
    int no;
    const char *name;
} signal_T;

/* シグナル情報の配列 */
static const signal_T signals[] = {
    /* POSIX.1-1990 で定義されたシグナル */
    { SIGHUP,  "HUP",  }, { SIGINT,  "INT",  }, { SIGQUIT, "QUIT", },
    { SIGILL,  "ILL",  }, { SIGABRT, "ABRT", }, { SIGFPE,  "FPE",  },
    { SIGKILL, "KILL", }, { SIGSEGV, "SEGV", }, { SIGPIPE, "PIPE", },
    { SIGALRM, "ALRM", }, { SIGTERM, "TERM", }, { SIGUSR1, "USR1", },
    { SIGUSR2, "USR2", }, { SIGCHLD, "CHLD", }, { SIGCONT, "CONT", },
    { SIGSTOP, "STOP", }, { SIGTSTP, "TSTP", }, { SIGTTIN, "TTIN", },
    { SIGTTOU, "TTOU", },
    /* SUSv2 & POSIX.1-2001 (SUSv3) で定義されたシグナル */
    { SIGBUS,  "BUS",  }, { SIGPROF, "PROF", }, { SIGSYS,  "SYS",  },
    { SIGTRAP, "TRAP", }, { SIGURG,  "URG",  }, { SIGXCPU, "XCPU", },
    { SIGXFSZ, "XFSZ", },
#ifdef SIGPOLL
    { SIGPOLL, "POLL", },
#endif
#ifdef SIGVTALRM
    { SIGVTALRM, "VTALRM", },
#endif
    /* 他のシグナル */
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
#ifdef SIGMSG
    { SIGMSG, "MSG", },
#endif
#ifdef SIGWINCH
    { SIGWINCH, "WINCH", },
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
#ifdef SIGVIRT
    { SIGVIRT, "VIRT", },
#endif
#ifdef SIGKAP
    { SIGKAP, "KAP", },
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
    /* 番号 0 のシグナルは存在しない (C99 7.14)
     * よってこれを配列の終わりの目印とする */
};

/* シグナル番号に対応するシグナル名を返す。先頭に "SIG" は付かない。
 * 未知のシグナルに対しては "?" を返す。 */
const char *get_signal_name(int signum)
{
    for (const signal_T *s = signals; s->no; s++)
	if (s->no == signum)
	    return s->name;
    return "?";
}


/* 各シグナルについて、受け取った後まだ処理していないことを示すフラグ */
static volatile sig_atomic_t signal_received[MAXSIGIDX];
/* 各シグナルについて、受信時に実行するトラップコマンド */
static char *trap_command[MAXSIGIDX];
/* ↑ これらの配列の添字は sigindex で求める。
 * 配列の最初の要素は EXIT トラップに対応する。 */

/* リアルタイムシグナルの signal_received と trap_command */
static volatile sig_atomic_t rtsignal_received[RTSIZE];
static char *rttrap_command[RTSIZE];

/* SIGCHLD を受信すると、signal_received[sigindex(SIGCHLD)] だけでなく
 * この変数も true になる。
 * signal_received[...] は主にトラップを実行するためのフラグとして使うが、
 * sigchld_received は子プロセスの変化の通知を受け取るために使う。 */
static volatile sig_atomic_t sigchld_received;

/* SIGCHLD, HUP, QUIT, TTOU, TSTP, TERM, INT のアクションの初期値。
 * これはシェルが起動時にシグナルハンドラを設定する前の状態を保存しておく
 * ものである。これらのシグナルに対して一度でもトラップが設定されると、
 * そのシグナルの初期値は SIG_DFL に変更される。 */
static struct sigaction
    initsigchldaction, initsighupaction, initsigquitaction,
    initsigttouaction, initsigtstpaction,
    initsigtermaction, initsigintaction;

/* シグナルモジュールを初期化したかどうか
 * これが true のとき、initsig{chld,hup,quit}action の値が有効である */
static bool initialized = false;
/* ジョブ制御用のシグナルの初期化をしたかどうか
 * これが true のとき、initsig{ttou,tstp}action の値が有効である */
static bool job_initialized = false;
/* 対話的動作用のシグナルの初期化をしたかどうか
 * これが true のとき、initsig{int,term}action の値が有効である */
static bool interactive_initialized = false;

/* シグナルモジュールを初期化する */
void init_signal(void)
{
    struct sigaction action;
    sigset_t ss;

    if (!initialized) {
	initialized = true;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = sig_handler;
	if (sigaction(SIGCHLD, &action, &initsigchldaction) < 0)
	    xerror(errno, "sigaction(SIGCHLD)");
	if (sigaction(SIGHUP, &action, &initsighupaction) < 0)
	    xerror(errno, "sigaction(SIGHUP)");
	if (sigaction(SIGQUIT, &action, &initsigquitaction) < 0)
	    xerror(errno, "sigaction(SIGQUIT)");

	sigemptyset(&ss);
	if (sigprocmask(SIG_SETMASK, &ss, NULL) < 0)
	    xerror(errno, "sigprocmask(SETMASK, nothing)");
    }
}

/* do_job_control と is_interactive_now に従ってシグナルを設定する。 */
void set_signals(void)
{
    struct sigaction action;

    if (do_job_control && !job_initialized) {
	job_initialized = true;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = sig_handler;
	if (sigaction(SIGTTOU, &action, &initsigttouaction) < 0)
	    xerror(errno, "sigaction(SIGTTOU)");
	if (sigaction(SIGTSTP, &action, &initsigtstpaction) < 0)
	    xerror(errno, "sigaction(SIGTSTP)");
    }
    if (is_interactive_now && !interactive_initialized) {
	interactive_initialized = true;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = sig_handler;
	if (sigaction(SIGINT, &action, &initsigintaction) < 0)
	    xerror(errno, "sigaction(SIGINT)");
	if (sigaction(SIGTERM, &action, &initsigtermaction) < 0)
	    xerror(errno, "sigaction(SIGTERM)");
    }
}

/* シグナルハンドラを全て初期設定に戻す。 */
void reset_all_signals(void)
{
    if (initialized) {
	initialized = false;
	if (sigaction(SIGCHLD, &initsigchldaction, NULL) < 0)
	    xerror(errno, "sigaction(SIGCHLD)");
	if (sigaction(SIGHUP, &initsighupaction, NULL) < 0)
	    xerror(errno, "sigaction(SIGHUP)");
	if (sigaction(SIGQUIT, &initsigquitaction, NULL) < 0)
	    xerror(errno, "sigaction(SIGQUIT)");
    }
    reset_signals();
}

/* ジョブ制御・対話的動作に関連するシグナルハンドラを初期設定に戻す。 */
void reset_signals(void)
{
    if (job_initialized) {
	job_initialized = false;
	if (sigaction(SIGTTOU, &initsigttouaction, NULL) < 0)
	    xerror(errno, "sigaction(SIGTTOU)");
	if (sigaction(SIGTSTP, &initsigtstpaction, NULL) < 0)
	    xerror(errno, "sigaction(SIGTSTP)");
    }
    if (interactive_initialized) {
	interactive_initialized = false;
	if (sigaction(SIGINT, &initsigintaction, NULL) < 0)
	    xerror(errno, "sigaction(SIGINT)");
	if (sigaction(SIGTERM, &initsigtermaction, NULL) < 0)
	    xerror(errno, "sigaction(SIGTERM)");
    }
}

/* SIGQUIT と SIGINT をブロックする */
void block_sigquit_and_sigint(void)
{
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGQUIT);
    sigaddset(&ss, SIGINT);
    if (sigprocmask(SIG_BLOCK, &ss, NULL) < 0)
	xerror(errno, "sigprocmask(BLOCK, QUIT|INT)");
}

/* SIGTSTP をブロックする */
void block_sigtstp(void)
{
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGTSTP);
    if (sigprocmask(SIG_BLOCK, &ss, NULL) < 0)
	xerror(errno, "sigprocmask(BLOCK, TSTP)");
}

/* SIGTTOU をブロックする */
void block_sigttou(void)
{
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGTTOU);
    if (sigprocmask(SIG_BLOCK, &ss, NULL) < 0)
	xerror(errno, "sigprocmask(BLOCK, TTOU)");
}

/* SIGTTOU のブロックを解除する */
void unblock_sigttou(void)
{
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGTTOU);
    if (sigprocmask(SIG_UNBLOCK, &ss, NULL) < 0)
	xerror(errno, "sigprocmask(UNBLOCK, TTOU)");
}

/* SIGPIPE 以外の全てのシグナルをブロックし、SIGPIPE を SIG_DFL に戻す。 */
void block_all_but_sigpipe(void)
{
    sigset_t ss;
    sigfillset(&ss);
    sigdelset(&ss, SIGPIPE);
    if (sigprocmask(SIG_SETMASK, &ss, NULL) < 0)
	xerror(errno, "sigprocmask(SETMASK, all but PIPE)");

    struct sigaction action;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_handler = SIG_DFL;
    if (sigaction(SIGPIPE, &action, NULL) < 0)
	xerror(errno, "sigaction(SIGPIPE)");
}

/* 汎用のシグナルハンドラ */
void sig_handler(int signum)
{
#if defined SIGRTMIN && defined SIGRTMAX
    if (SIGRTMIN <= signum && signum <= SIGRTMAX) {
	size_t index = signum - SIGRTMIN;
	if (index < RTSIZE)
	    rtsignal_received[index] = true;
    } else
#endif
    {
	size_t index = sigindex(signum);
	assert(index < MAXSIGIDX);
	signal_received[index] = true;

	if (signum == SIGCHLD)
	    sigchld_received = true;
    }
}

/* SIGCHLD と SIGHUP をブロックする。 */
void block_sigchld_and_sighup(void)
{
    sigset_t ss;

    sigemptyset(&ss);
    sigaddset(&ss, SIGCHLD);
    sigaddset(&ss, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &ss, NULL) < 0)
	xerror(errno, "sigprocmask(BLOCK, CHLD|HUP)");
}

/* SIGCHLD と SIGHUP のブロックを解除する。 */
void unblock_sigchld_and_sighup(void)
{
    sigset_t ss;

    sigemptyset(&ss);
    sigaddset(&ss, SIGCHLD);
    sigaddset(&ss, SIGHUP);
    if (sigprocmask(SIG_UNBLOCK, &ss, NULL) < 0)
	xerror(errno, "sigprocmask(UNBLOCK, CHLD|HUP)");
}

/* SIGCHLD を受信するまで待機し、handle_sigchld_and_sighup する。
 * この関数は SIGCHLD と SIGHUP をブロックした状態で呼び出すこと。
 * 既にシグナルを受信済みの場合、待機せずにすぐ返る。
 * SIGHUP にトラップを設定していない場合に SIGHUP を受信すると、ただちに
 * シェルを終了する。 */
void wait_for_sigchld(void)
{
    sigset_t ss;

    sigemptyset(&ss);
    while (!sigchld_received && !signal_received[sigindex(SIGHUP)]) {
	if (sigsuspend(&ss) < 0 && errno != EINTR) {
	    xerror(errno, "sigsuspend");
	    break;
	}
    }

    handle_sigchld_and_sighup();
}

/* 指定したファイルディスクリプタが読めるようになるまで待つ。
 * 待っている間に SIGCHLD/SIGHUP を受け取ったら対処する。
 * この関数は SIGCHLD/SIGHUP をブロックした状態で呼び出さないと競合状態を生む。
 * trap が true ならトラップも実行する。 */
void wait_for_input(int fd, bool trap)
{
    sigset_t ss;

    sigemptyset(&ss);
    for (;;) {
	handle_sigchld_and_sighup();
	if (trap)
	    handle_traps();

	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);
	if (pselect(fd + 1, &fdset, NULL, NULL, NULL, &ss) >= 0) {
	    if (FD_ISSET(fd, &fdset))
		return;
	} else {
	    if (errno != EINTR) {
		xerror(errno, "pselect");
		return;
	    }
	}
    }
}

/* SIGCHLD/SIGHUP を受信していたら、対処する。
 * SIGHUP にトラップを設定していない場合に SIGHUP を受信すると、ただちに
 * シェルを終了する。 */
void handle_sigchld_and_sighup(void)
{
    if (sigchld_received) {
        sigchld_received = false;
        do_wait();
	if (shopt_notify) {
	    // XXX lineedit 中はプロンプトを再表示したい
	    print_job_status(PJS_ALL, true, false, stderr);
	}
    }
    // TODO sig.c: SIGHUP 受信時にシェルを終了する
}

/* トラップが設定されたシグナルを受信していたら、対応するコマンドを実行する。
 * この関数を呼び出すときアクティブジョブが存在してはならない。 */
void handle_traps(void)
{
    (void) trap_command;
    (void) rttrap_command;
    // TODO sig.c: handle_traps: 未実装
}

/* SIG_IGN 以外に設定したトラップを全て解除する */
void clear_traps(void)
{
    // TODO sig.c: clear_traps: 未実装
}


/* vim: set ts=8 sts=4 sw=4 noet: */
