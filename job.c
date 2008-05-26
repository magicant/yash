/* Yash: yet another shell */
/* job.c: job control */
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "option.h"
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "sig.h"
#include "job.h"


static inline job_T *get_job(size_t jobnumber)
    __attribute__((pure));
static void set_current_jobnumber(size_t no);
static size_t find_next_job(size_t numlimit);
static int calc_status(int status)
    __attribute__((const));
static wchar_t *get_job_name(const job_T *job)
    __attribute__((nonnull,warn_unused_result));
static char *get_process_status_string(const process_T *p, bool *needfree)
    __attribute__((nonnull,malloc,warn_unused_result));
static char *get_job_status_string(const job_T *job, bool *needfree)
    __attribute__((nonnull,malloc,warn_unused_result));


/* ジョブリスト。
 * joblist.contents[0] はアクティブジョブで、起動直後でまだジョブリストに
 * 入っていないジョブを表す。それ以降の要素は普通のジョブ。 */
static plist_T joblist;

/* 現在・前のジョブ番号。当該ジョブがなければ 0。 */
static size_t current_jobnumber, previous_jobnumber;

/* joblist を初期化する。 */
void init_job(void)
{
    static bool initialized = false;
    if (!initialized) {
	initialized = true;
	pl_init(&joblist);
	pl_add(&joblist, NULL);
    }
}

/* アクティブジョブを設定する。 */
void set_active_job(job_T *job)
{
    assert(joblist.contents[ACTIVE_JOBNO] == NULL);
    joblist.contents[ACTIVE_JOBNO] = job;
}

/* アクティブジョブをジョブリストに追加する。
 * current: true なら追加したジョブを現在のジョブにする。
 * current が false でも現在のジョブがなければ true とみなす。 */
void add_job(bool current)
{
    job_T *job = joblist.contents[ACTIVE_JOBNO];

    assert(job != NULL);
    joblist.contents[ACTIVE_JOBNO] = NULL;

    /* 空いているジョブ番号があればそこに追加する。 */
    for (size_t i = 1; i < joblist.length; i++) {
	if (joblist.contents[i] == NULL) {
	    joblist.contents[i] = job;
	    if (current || current_jobnumber == 0)
		set_current_jobnumber(i);
	    else if (previous_jobnumber == 0)
		previous_jobnumber = i;
	    return;
	}
    }

    /* 空いているジョブ番号がなければ最後に追加する。 */
    pl_add(&joblist, job);
    if (current || current_jobnumber == 0)
	set_current_jobnumber(joblist.length - 1);
    else if (previous_jobnumber == 0)
	previous_jobnumber = joblist.length - 1;
}

/* 指定した番号のジョブを無条件で削除する。
 * 使われていないジョブ番号を指定しないこと。
 * 指定したジョブが現在・前のジョブならば現在・前のジョブを再設定する。 */
void remove_job(size_t jobnumber)
{
    assert(jobnumber < joblist.length);

    job_T *job = joblist.contents[jobnumber];
    assert(job != NULL);
    joblist.contents[jobnumber] = NULL;

    /* ジョブの領域を解放する */
    for (size_t i = 0; i < job->j_pcount; i++)
	free(job->j_procs[i].pr_name);
    free(job);

    if (jobnumber == current_jobnumber) {
	current_jobnumber = previous_jobnumber;
	previous_jobnumber = find_next_job(current_jobnumber);
    } else if (jobnumber == previous_jobnumber) {
	previous_jobnumber = find_next_job(current_jobnumber);
    }
}

/* 全てのジョブを無条件で削除する */
void remove_all_jobs(void)
{
    for (size_t i = 0; i < joblist.length; i++)
	if (joblist.contents[i])
	    remove_job(i);
    /*
    pl_clear(&joblist);
    pl_add(&joblist, NULL);
    */
}

/* 指定した番号のジョブを取得する。ジョブが存在しなければ NULL を返す。 */
job_T *get_job(size_t jobnumber)
{
    return jobnumber < joblist.length ? joblist.contents[jobnumber] : NULL;
}

/* ジョブ番号の扱いについて
 *   * 現在のジョブが変わるとき、元の現在のジョブが新しい前のジョブになる
 *     * 'fg' コマンドは現在のジョブを変更する
 *     * set_active_job でジョブを追加するとそれは現在のジョブになる
 *   * 現在のジョブが終了すると、前のジョブが現在のジョブになる
 *   * 'bg' コマンドで現在・前のジョブを再開すると現在・前のジョブは再設定される
 *   * 'wait' コマンドは現在・前のジョブを変更しない
 */

/* 現在のジョブ番号を指定した番号にし、前のジョブ番号を再設定する。
 * 0 を指定すると前のジョブ番号を現在のジョブ番号にする。
 * 存在しないジョブ番号を指定しないこと。 */
void set_current_jobnumber(size_t jobnumber)
{
    assert(jobnumber == 0 || get_job(jobnumber) != NULL);

    previous_jobnumber = current_jobnumber;
    if (jobnumber == 0) {
	jobnumber = previous_jobnumber;
	if (jobnumber == 0 || get_job(jobnumber) == NULL)
	    jobnumber = find_next_job(0);
    }
    current_jobnumber = jobnumber;

    if (previous_jobnumber == 0
	    || previous_jobnumber == current_jobnumber)
	previous_jobnumber = find_next_job(current_jobnumber);
}

/* 指定した番号以外で、次の 現在のまたは前のジョブ番号を選び出す。
 * ジョブ番号の候補がなければ 0 を返す。 */
/* 候補の選び方:
 *   まず停止しているジョブから探す。停止しているジョブがなければ他をあたる。
 *   ジョブ番号は出来るだけ大きいものを優先して選ぶ */
size_t find_next_job(size_t excl)
{
    size_t jobnumber = joblist.length;
    while (--jobnumber > 0) {
	if (jobnumber != excl) {
	    job_T *job = get_job(jobnumber);
	    if (job != NULL && job->j_status == JS_STOPPED)
		return jobnumber;
	}
    }
    jobnumber = joblist.length;
    while (--jobnumber > 0) {
	if (jobnumber != excl) {
	    job_T *job = get_job(jobnumber);
	    if (job != NULL)
		return jobnumber;
	}
    }
    return 0;
}

/* ジョブリスト内のジョブの数を返す */
size_t job_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < joblist.length; i++)
	if (joblist.contents[i])
	    count++;
    return count;
}

/* ジョブリスト内の停止しているジョブの数を返す */
size_t stopped_job_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < joblist.length; i++) {
	job_T *job = joblist.contents[i];
	if (job && job->j_status == JS_STOPPED)
	    count++;
    }
    return count;
}


/* waitpid を実行してジョブリスト内の子プロセスの情報を更新する。
 * この関数はブロックしない。 */
void do_wait(void)
{
    pid_t pid;
    int status;
#ifdef WIFCONTINUED
    static int waitopts = WUNTRACED | WCONTINUED | WNOHANG;
#else
    static int waitopts = WUNTRACED | WNOHANG;
#endif

start:
    pid = waitpid(-1, &status, waitopts);
    if (pid < 0) {
	switch (errno) {
	    case EINTR:
		goto start;  /* 単にやり直す */
	    case ECHILD:
		return;      /* 子プロセスが存在しなかった */
	    case EINVAL:
#ifdef WIFCONTINUED
		/* bash のソースによると、WCONTINUED が定義されていても
		 * 使えない環境があるらしい。 → WCONTINUED なしで再挑戦 */
		if (waitopts & WCONTINUED) {
		    waitopts = WUNTRACED | WNOHANG;
		    goto start;
		}
#endif
		/* falls thru! */
	    default:
		xerror(errno, "waitpid");
		return;
	}
    } else if (pid == 0) {
	/* もう状態変化を通知していない子プロセスは残っていなかった */
	return;
    }

    size_t jobnumber, pnumber;
    job_T *job;
    process_T *pr;

    /* pid に対応する jobnumber, job, pr を特定する */
    for (jobnumber = 0; jobnumber < joblist.length; jobnumber++)
	if ((job = joblist.contents[jobnumber]))
	    for (pnumber = 0; pnumber < job->j_pcount; pnumber++)
		if ((pr = &job->j_procs[pnumber])->pr_pid == pid)
		    goto found;

    /* ジョブリスト内に pid が見付からなければ (これはジョブを disown した場合
     * などに起こりうる)、単に無視して start に戻る。 */
    goto start;

found:
    pr->pr_statuscode = status;
    if (WIFEXITED(status) || WIFSIGNALED(status))
	pr->pr_status = JS_DONE;
    else if (WIFSTOPPED(status))
	pr->pr_status = JS_STOPPED;
#ifdef WIFCONTINUED
    else if (WIFCONTINUED(status))
	pr->pr_status = JS_RUNNING;
#endif

    /* ジョブの各プロセスの状態を元に、ジョブ全体の状態を決める。
     * 少なくとも一つのプロセスが JS_RUNNING
     *     -> ジョブ全体も JS_RUNNING
     * JS_RUNNING のプロセスはないが、JS_STOPPED のプロセスはある
     *     -> ジョブ全体も JS_STOPPED
     * 全てのプロセスが JS_DONE
     *     -> ジョブ全体も JS_DONE
     */
    jobstatus_T oldstatus = job->j_status;
    bool anyrunning = false, anystopped = false;
    /* job 内に一つでも実行中・停止中のプロセスが残っているかどうか調べる */
    for (size_t i = 0; i < job->j_pcount; i++) {
	switch (job->j_procs[i].pr_status) {
	    case JS_RUNNING:  anyrunning = true;  goto out_of_loop;
	    case JS_STOPPED:  anystopped = true;  break;
	    default:                              break;
	}
    }
out_of_loop:
    job->j_status = anyrunning ? JS_RUNNING : anystopped ? JS_STOPPED : JS_DONE;
    if (job->j_status != oldstatus)
	job->j_statuschanged = true;

    goto start;
}

/* 指定した番号のジョブの状態変化を待つ。
 * 存在しないジョブ番号を指定しないこと。
 * return_on_stop が false ならば、ジョブが終了するまで待つ。
 * return_on_stop が true ならば、ジョブが停止または終了するまで待つ。
 * ジョブが既に終了 (または停止) していれば、待たずにすぐに返る。 */
void wait_for_job(size_t jobnumber, bool return_on_stop)
{
    job_T *job = joblist.contents[jobnumber];

    block_sigchld_and_sighup();
    for (;;) {
	if (job->j_status == JS_DONE)
	    break;
	if (return_on_stop && job->j_status == JS_STOPPED)
	    break;
	wait_for_sigchld();
    }
    unblock_sigchld_and_sighup();
}

/* waitpid が返したステータスから終了コードを算出する。 */
int calc_status(int status)
{
    if (WIFEXITED(status))
	return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
	return WTERMSIG(status) + TERMSIGOFFSET;
    if (WIFSTOPPED(status))
	return WSTOPSIG(status) + TERMSIGOFFSET;
#ifdef WIFCONTINUED
    if (WIFCONTINUED(status))
	return 0;
#endif
    assert(false);
}

/* 指定したジョブの終了コードを求める。
 * 状態が JS_RUNNING なジョブを指定しないこと。 */
int calc_status_of_job(const job_T *job)
{
    switch (job->j_status) {
    case JS_DONE:
	if (job->j_procs[job->j_pcount - 1].pr_pid)
	    return calc_status(job->j_procs[job->j_pcount - 1].pr_statuscode);
	else
	    return job->j_procs[job->j_pcount - 1].pr_statuscode;
    case JS_STOPPED:
	for (int i = job->j_pcount; --i >= 0; ) {
	    if (job->j_procs[i].pr_status == JS_STOPPED)
		return calc_status(job->j_procs[i].pr_statuscode);
	}
	/* falls thru! */
    default:
	assert(false);
    }
}

/* 指定したジョブ全体の名前 (コマンド) を取得する。
 * 戻り値: 新しく malloc した文字列 または job->j_procs[0].pr_name。 */
wchar_t *get_job_name(const job_T *job)
{
    if (job->j_pcount == 1)
	return job->j_procs[0].pr_name;

    xwcsbuf_T buf;
    wb_init(&buf);
    if (job->j_loop)
	wb_cat(&buf, L"| ");
    for (size_t i = 0; i < job->j_pcount; i++) {
	if (i > 0)
	    wb_cat(&buf, L" | ");
	wb_cat(&buf, job->j_procs[i].pr_name);
    }
    return wb_towcs(&buf);
}

/* プロセスの状態を示す "Running" とか "Stopped(SIGTSTP)" のような文字列を返す。
 * needfree: 戻り値を free すべきかどうかが *needfree に入る。 */
char *get_process_status_string(const process_T *p, bool *needfree)
{
    int status;

    switch (p->pr_status) {
    case JS_RUNNING:
	*needfree = false;
	return gt("Running");
    case JS_STOPPED:
	*needfree = true;
	return malloc_printf(gt("Stopped(SIG%s)"),
		get_signal_name(WSTOPSIG(p->pr_statuscode)));
    case JS_DONE:
	status = p->pr_statuscode;
	if (p->pr_pid == 0)
	    goto exitstatus;
	if (WIFEXITED(status)) {
	    status = WEXITSTATUS(status);
exitstatus:
	    if (status == EXIT_SUCCESS) {
		*needfree = false;
		return gt("Done");
	    } else {
		*needfree = true;
		return malloc_printf(gt("Done(%d)"), status);
	    }
	} else {
	    assert(WIFSIGNALED(status));
	    *needfree = true;
	    status = WTERMSIG(status);
#ifdef WCOREDUMP
	    if (WCOREDUMP(status)) {
		return malloc_printf(gt("Killed (SIG%s: core dumped)"),
			get_signal_name(status));
	    }
#endif
	    return malloc_printf(gt("Killed (SIG%s)"),
		    get_signal_name(status));
	}
    }
    assert(false);
}

/* ジョブの状態を示す "Running" とか "Stopped(SIGTSTP)" のような文字列を返す。
 * needfree: 戻り値を free すべきかどうかが *needfree に入る。 */
char *get_job_status_string(const job_T *job, bool *needfree)
{
    switch (job->j_status) {
    case JS_RUNNING:
	*needfree = false;
	return gt("Running");
    case JS_STOPPED:
	/* ジョブ内のプロセスのうち、実際に停止しているものを探して返す。 */
	for (size_t i = job->j_pcount; ; )
	    if (job->j_procs[--i].pr_status == JS_STOPPED)
		return get_process_status_string(&job->j_procs[i], needfree);
    case JS_DONE:
	return get_process_status_string(
		&job->j_procs[job->j_pcount - 1], needfree);
    }
    assert(false);
}

/* ジョブの状態を表示する。
 * 終了したジョブを表示したら、そのジョブは削除する。
 * jobnumber: 1 以上のジョブ番号または PJS_ALL (全てのジョブ)。
 *            存在しないジョブ番号を指定しても何もしない。
 * changedonly: 状態が変化したジョブだけを通知する。
 * verbose: 詳細化。パイプ内の全てのプロセスの情報を表示する。
 * f: 出力先。 */
void print_job_status(size_t jobnumber, bool changedonly, bool verbose, FILE *f)
{
    if (jobnumber == PJS_ALL) {
	for (size_t i = 1; i < joblist.length; i++)
	    print_job_status(i, changedonly, verbose, f);
	return;
    }

    job_T *job = get_job(jobnumber);
    if (!job || (changedonly && !job->j_statuschanged))
	return;

    char current;
    if      (jobnumber == current_jobnumber)  current = '+';
    else if (jobnumber == previous_jobnumber) current = '-';
    else                                      current = ' ';

    if (!verbose) {
	bool needfree;
	char *status = get_job_status_string(job, &needfree);
	wchar_t *jobname = get_job_name(job);

	/* TRANSLATORS: the translated format string can be different 
	 * from the original only in the number of spaces. */
	fprintf(f, gt("[%zu] %c %-20s %ls\n"),
		jobnumber, current, status, jobname);

	if (needfree)
	    free(status);
	if (jobname != job->j_procs[0].pr_name)
	    free(jobname);
    } else {
	bool needfree;
	pid_t pid = job->j_procs[0].pr_pid;
	char *status = get_process_status_string(&job->j_procs[0], &needfree);
	char looppipe = job->j_loop ? '|' : ' ';
	wchar_t *jobname = job->j_procs[0].pr_name;

	/* TRANSLATORS: the translated format string can be different 
	 * from the original only in the number of spaces. */
	fprintf(f, gt("[%zu] %c %5jd %-20s %c %ls\n"),
		jobnumber, current, (intmax_t) pid, status, looppipe, jobname);
	if (needfree)
	    free(status);

	for (size_t i = 1; i < job->j_pcount; i++) {
	    pid = job->j_procs[i].pr_pid;
	    status = get_process_status_string(&job->j_procs[i], &needfree);
	    jobname = job->j_procs[i].pr_name;

	    /* TRANSLATORS: the translated format string can be different 
	     * from the original only in the number of spaces. */
	    fprintf(f, gt("      %5jd %-20s | %ls\n"),
		    (intmax_t) pid, (posixly_correct ? "" : status), jobname);
	    if (needfree)
		free(status);
	}
    }
    job->j_statuschanged = false;
    if (job->j_status == JS_DONE)
	remove_job(jobnumber);
}


/* vim: set ts=8 sts=4 sw=4 noet: */
