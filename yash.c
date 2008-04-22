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
#include "option.h"
#include "util.h"
#include "path.h"
#include "lineinput.h"
#include "parser.h"
#include "variable.h"
#include "sig.h"
#include "redir.h"
#include "job.h"
#include "exec.h"
#include "yash.h"
#include "version.h"


extern int main(int argc, char **argv)
    __attribute__((nonnull));
static void print_help(void);
static void print_version(void);

/* シェル本体のプロセス ID。 */
pid_t shell_pid;
/* シェルの元々のプロセスグループ */
pid_t initial_pgrp;


int main(int argc __attribute__((unused)), char **argv)
{
    bool help = false, version = false;
    bool exec_first_arg = false, read_stdin = false;
    bool do_job_control_set = false, is_interactive_set = false;
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
    while ((opt = xgetopt_long(argv,
		    "+*cilsV?" SHELLSET_OPTIONS,
		    shell_long_options,
		    NULL))
	    >= 0) {
	switch (opt) {
	case 0:
	    break;
	case 'c':
	    if (xoptopt != '-') {
		xerror(0, Ngt("+c: invalid option"));
		help = true;
	    }
	    exec_first_arg = true;
	    break;
	case 'i':
	    is_interactive = (xoptopt == '-');
	    is_interactive_set = true;
	    break;
	case 'l':
	    is_login_shell = (xoptopt == '-');
	    break;
	case 's':
	    if (xoptopt != '-') {
		xerror(0, Ngt("+s: invalid option"));
		help = true;
	    }
	    read_stdin = true;
	    break;
	case 'V':
	    version = true;
	    break;
	case '?':
	    help = true;
	    break;
	case 'm':
	    do_job_control_set = true;
	    /* falls thru! */
	default:
	    set_option(opt);
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
    initial_pgrp = getpgrp();
    init_cmdhash();
    init_homedirhash();
    init_variables();
    init_signal();
    init_shellfds();
    init_job();

    if (exec_first_arg && read_stdin) {
	xerror(0, Ngt("both -c and -s options cannot be given at once"));
	exit(2);
    }
    if (exec_first_arg) {
	char *command = argv[xoptind++];
	if (!command) {
	    xerror(0, Ngt("-c option requires an operand"));
	    exit(2);
	}
	if (argv[xoptind])
	    command_name = argv[xoptind++];
	is_interactive_now = is_interactive;
	if (!do_job_control_set)
	    do_job_control = is_interactive;
	set_signals();
	open_ttyfd();
	set_own_pgrp();
	set_positional_parameters(argv + xoptind);
	exec_mbs(command, posixly_correct ? "sh -c" : "yash -c", true);
    } else {
	FILE *input;
	const char *inputname;
	if (read_stdin || !argv[xoptind]) {
	    input = stdin;
	    inputname = "<stdin>";
	    if (!is_interactive_set && !argv[xoptind]
		    && isatty(STDIN_FILENO) && isatty(STDERR_FILENO))
		is_interactive = true;
	} else {
	    command_name = argv[xoptind++];
	    input = fopen(command_name, "r");
	    inputname = command_name;
	    input = reopen_with_shellfd(input, "r");
	    if (!input) {
		int errno_ = errno;
		xerror(errno_, Ngt("cannot open file `%s'"), command_name);
		exit(errno_ == ENOENT ? EXIT_NOTFOUND : EXIT_NOEXEC);
	    }
	}
	is_interactive_now = is_interactive;
	if (!do_job_control_set)
	    do_job_control = is_interactive;
	set_signals();
	open_ttyfd();
	set_own_pgrp();
	set_positional_parameters(argv + xoptind);
	exec_input(input, inputname, is_interactive, true);
    }
    assert(false);
    // TODO rc ファイル
}

void print_help(void)
{
    if (posixly_correct) {
	printf(gt("Usage:  sh [options] [filename [args...]]\n"
		  "        sh [options] -c command [command_name [args...]]\n"
		  "        sh [options] -s [args...]\n"));
	printf(gt("Options: -il%s\n"), SHELLSET_OPTIONS);
    } else {
	printf(gt("Usage:  yash [options] [filename [args...]]\n"
		  "        yash [options] -c command [args...]\n"
		  "        yash [options] -s [args...]\n"));
	printf(gt("Short options: -il%sV?\n"), SHELLSET_OPTIONS);
	printf(gt("Long options:\n"));
	for (size_t i = 0; shell_long_options[i].name; i++)
	    printf("\t--%s\n", shell_long_options[i].name);
    }
}

void print_version(void)
{
    printf(gt("Yet another shell, version %s\n"), PACKAGE_VERSION);
    printf(PACKAGE_COPYRIGHT "\n");
}


/* ジョブ制御が有効なら、自分自身のプロセスグループを
 * 自分のプロセス ID に変更する */
void set_own_pgrp(void)
{
    if (do_job_control) {
	setpgid(0, 0);
	make_myself_foreground();
    }
}

/* ジョブ制御が有効なら、自分自身のプロセスグループを set_own_pgrp 前に戻す */
void reset_own_pgrp(void)
{
    if (do_job_control && initial_pgrp > 0)
	setpgid(0, initial_pgrp);
}

/* reset_own_pgrp してもプロセスグループが元に戻らないようにする */
void forget_initial_pgrp(void)
{
    initial_pgrp = 0;
}


/********** コードを実行する関数 **********/

/* マルチバイト文字列をソースコードとしてコマンドを実行する。
 * コマンドを一つも実行しなかった場合、laststatus は 0 になる。
 * code: 実行するコード (初期シフト状態で始まる)
 * name: 構文エラーで表示するコード名。NULL でも良い。
 * finally_exit: true なら実行後にそのままシェルを終了する。
 * 戻り値: 構文エラー・入力エラーがなければ true */
bool exec_mbs(const char *code, const char *name, bool finally_exit)
{
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
	.ttyinput = false,
	.lastinputresult = 0,
    };
    memset(&iinfo.state, 0, sizeof iinfo.state);  // state を初期状態にする

    return parse_and_exec(&pinfo, finally_exit);
}

/* ワイド文字列をソースコードとしてコマンドを実行する。
 * コマンドを一つも実行しなかった場合、laststatus は 0 になる。
 * code: 実行するコード
 * name: 構文エラーで表示するコード名。NULL でも良い。
 * finally_exit: true なら実行後にそのままシェルを終了する。
 * 戻り値: 構文エラー・入力エラーがなければ true */
bool exec_wcs(const wchar_t *code, const char *name, bool finally_exit)
{
    struct input_wcs_info iinfo = {
	.src = code,
    };
    struct parseinfo_T pinfo = {
	.print_errmsg = true,
	.filename = name,
	.lineno = 1,
	.input = input_wcs,
	.inputinfo = &iinfo,
	.ttyinput = false,
	.lastinputresult = 0,
    };

    return parse_and_exec(&pinfo, finally_exit);
}

/* 入力ストリームを読み取ってコマンドを実行する。
 * コマンドを一つも実行しなかった場合、laststatus は 0 になる。
 * f: 入力元のストリーム
 * ttyinput: 入力が対話的端末からかどうか
 * name: 構文エラーで表示するコード名。NULL でも良い。
 * finally_exit: true なら実行後にそのままシェルを終了する。
 * 戻り値: 構文エラー・入力エラーがなければ true */
bool exec_input(FILE *f, const char *name, bool ttyinput, bool finally_exit)
{
    struct parseinfo_T pinfo = {
	.print_errmsg = true,
	.filename = name,
	.lineno = 1,
	.input = input_file,
	.inputinfo = f,
	.ttyinput = ttyinput,
	.lastinputresult = 0,
    };
    return parse_and_exec(&pinfo, finally_exit);
}

/* 指定した parseinfo_T に基づいてソースを読み込み、それを実行する。
 * コマンドを一つも実行しなかった場合、laststatus は 0 になる。
 * finally_exit: true なら実行後にそのままシェルを終了する。
 * 戻り値: 構文エラー・入力エラーがなければ true */
bool parse_and_exec(parseinfo_T *pinfo, bool finally_exit)
{
    bool executed = false;

    for (;;) {
	and_or_T *commands;
	switch (read_and_parse(pinfo, &commands)) {
	    case 0:  // OK
		if (commands) {
		    exec_and_or_lists(commands,
			    finally_exit && !pinfo->ttyinput &&
			    pinfo->lastinputresult == EOF);
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
		laststatus = EXIT_SYNERROR;
		if (pinfo->ttyinput)
		    break;
		else if (finally_exit)
		    exit(laststatus);
		else
		    return false;
	}
    }
    /* コマンドを一つも実行しなかった場合のみ後から laststatus を 0 にする。
     * 最初に laststatus を 0 にしてしまうと、コマンドを実行する際に $? の値が
     * 変わってしまう。 */
}


/* vim: set ts=8 sts=4 sw=4 noet: */
