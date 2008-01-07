/* Yash: yet another shell */
/* builtin.c: shell builtin commands */
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


#include <error.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <readline/history.h>
#include "yash.h"
#include "util.h"
#include "sig.h"
#include "lineinput.h"
#include "expand.h"
#include "exec.h"
#include "path.h"
#include "builtin.h"
#include "alias.h"
#include "variable.h"
#include <assert.h>

#define ADDITIONAL_BUILTIN //XXX

void init_builtin(void);
BUILTIN *get_builtin(const char *name);
int builtin_true(int argc, char *const *argv);
#ifdef ADDITIONAL_BUILTIN
int builtin_false(int argc, char *const *argv);
#endif
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
 * 組込みコマンドは、argv の内容を変更してはならない。
 * ただし、getopt によって argv の順番を並べ替えるのはよい。 */

static struct hasht builtins;

static struct builtin_info {
	const char *name;
	BUILTIN builtin;
} builtins_array[] = {
	{ ":",       { builtin_true,    true  }},
#ifdef ADDITIONAL_BUILTIN
	{ "true",    { builtin_true,    false }},
	{ "false",   { builtin_false,   false }},
#endif
	{ "exit",    { builtin_exit,    true  }},
	{ "logout",  { builtin_exit,    false }},
	{ "kill",    { builtin_kill,    false }},
	{ "wait",    { builtin_wait,    false }},
	{ "suspend", { builtin_suspend, false }},
	{ "jobs",    { builtin_jobs,    false }},
	{ "disown",  { builtin_disown,  false }},
	{ "fg",      { builtin_fg,      false }},
	{ "bg",      { builtin_fg,      false }},
	{ "exec",    { builtin_exec,    true  }},
	{ "cd",      { builtin_cd,      false }},
	{ "umask",   { builtin_umask,   false }},
	{ "export",  { builtin_export,  true  }},
	{ ".",       { builtin_source,  true  }},
	{ "source",  { builtin_source,  false }},
	{ "history", { builtin_history, false }},
	{ "alias",   { builtin_alias,   false }},
	{ "unalias", { builtin_unalias, false }},
	{ "option",  { builtin_option,  false }},
	{ NULL,      { NULL,            false }},
};

/* 組込みコマンドに関するデータを初期化する。 */
void init_builtin(void)
{
	static struct builtin_info *b = builtins_array;
	ht_init(&builtins);
	ht_ensurecap(&builtins, 30);
	while (b->name) {
		ht_set(&builtins, b->name, &b->builtin);
		b++;
	}
}

/* 指定した名前に対応する組込みコマンド関数を取得する。
 * 対応するものがなければ NULL を返す。 */
BUILTIN *get_builtin(const char *name)
{
	return ht_get(&builtins, name);
}

/* :/true 組込みコマンド */
int builtin_true(int argc __attribute__((unused)),
		char *const *argv __attribute__((unused)))
{
	return EXIT_SUCCESS;
}

/* false 組込みコマンド */
int builtin_false(int argc __attribute__((unused)),
		char *const *argv __attribute__((unused)))
{
	return EXIT_FAILURE;
}

/* cd 組込みコマンド */
int builtin_cd(int argc, char *const *argv)
{
	const char *newpwd, *oldpwd;
	char *path;

	if (argc < 2) {
		newpwd = getvar(VAR_HOME);
		if (!newpwd) {
			error(0, 0, "%s: HOME directory not specified", argv[0]);
			return EXIT_FAILURE;
		}
	} else {
		newpwd = argv[1];
		if (strcmp(newpwd, "-") == 0) {
			newpwd = getvar(VAR_OLDPWD);
			if (!newpwd) {
				error(0, 0, "%s: OLDPWD directory not specified", argv[0]);
				return EXIT_FAILURE;
			} else {
				printf("%s\n", newpwd);
			}
		}
	}
	oldpwd = getvar(VAR_PWD);
	if (chdir(newpwd) < 0) {
		error(0, errno, "%s: %s", argv[0], newpwd);
		return EXIT_FAILURE;
	}
	if (oldpwd) {
		setvar(VAR_OLDPWD, oldpwd, true);
	}
	if ((path = xgetcwd())) {
		setvar(VAR_PWD, path, true);
		free(path);
	}
	return EXIT_SUCCESS;
}

/* umask 組込みコマンド */
/* XXX: 数字だけでなく文字列での指定に対応 */
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
	fprintf(stderr, "Usage:  umask [newumask]\n");
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
	xoptind = 0;
	xopterr = true;
	while ((opt = xgetopt(argv, "n")) >= 0) {
		switch (opt) {
			case 'n':
				remove = true;
				break;
			default:
				goto usage;
		}
	}
	for (; xoptind < argc; xoptind++) {
		char *c = argv[xoptind];

		if (remove) {
			unexport(c);
		} else {
			size_t namelen = strcspn(c, "=");
			if (c[namelen] == '=') {
				c[namelen] = '\0';
				if (!setvar(c, c + namelen + 1, true)) {
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
	fprintf(stderr, "Usage:  export NAME=VALUE ...\n");
	fprintf(stderr, "    or  export -n NAME ...\n");
	return EXIT_FAILURE;
}

/* source/. 組込みコマンド */
int builtin_source(int argc, char *const *argv)
{
	if (argc < 2) {
		error(0, 0, "%s: filename not given", argv[0]);
		goto usage;
	}
	exec_file(argv[1], false /* don't supress errors */);
	return laststatus;

usage:
	fprintf(stderr, "Usage:  %s filename\n", argv[0]);
	return EXIT_FAILURE;
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
	fprintf(stderr, "Usage:  history [n]\n");
	fprintf(stderr, "    or  history -d n\n");
	fprintf(stderr, "    or  history -rw file\n");
	fprintf(stderr, "    or  history -s arg\n");
	return EXIT_FAILURE;
}

/* alias 組込みコマンド
 * 引数なし or -p オプションありだと全てのエイリアスを出力する。
 * 引数があると、そのエイリアスを設定 or 出力する。
 * -g を指定するとグローバルエイリアスになる。 */
int builtin_alias(int argc, char *const *argv)
{
	int print_alias(const char *name, ALIAS *alias) {
		struct strbuf buf;
		sb_init(&buf);
		if (!posixly_correct)
			sb_append(&buf, "alias ");
		sb_printf(&buf, "%-3s%s=", alias->global ? "-g" : "", name);
		escape_sq(alias->value, &buf);
		puts(buf.contents);
		sb_destroy(&buf);
		return 0;
	}

	bool printall = argc <= 1;
	bool global = false;
	bool err = false;
	int opt;

	xoptind = 0;
	xopterr = true;
	while ((opt = xgetopt(argv, "gp")) >= 0) {
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
	for (; xoptind < argc; xoptind++) {
		char *c = argv[xoptind];
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
	fprintf(stderr, "Usage:  alias [-gp] [name[=value] ... ]\n");
	return EXIT_FAILURE;
}

/* unalias 組込みコマンド。指定した名前のエイリアスを消す。
 * -a オプションで全て消す。 */
int builtin_unalias(int argc, char *const *argv)
{
	bool removeall = false;
	bool err = false;
	int opt;

	if (argc < 2)
		goto usage;
	xoptind = 0;
	xopterr = true;
	while ((opt = xgetopt(argv, "a")) >= 0) {
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
	for (; xoptind < argc; xoptind++) {
		if (remove_alias(argv[xoptind]) < 0) {
			err = true;
			error(0, 0, "%s: %s: no such alias", argv[0], argv[xoptind]);
		}
	}
	return err ? EXIT_FAILURE : EXIT_SUCCESS;

usage:
	fprintf(stderr, "Usage:  unalias [-a] name [...]\n");
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

	xoptind = 0;
	xopterr = true;
	while ((opt = xgetopt(argv, "+d")) >= 0) {
		switch (opt) {
			case 'd':
				def = true;
				break;
			default:
				goto usage;
		}
	}
	if (xoptind < argc) {
		name = argv[xoptind++];
	} else {
		goto usage;  /* nothing to do */
	}
	if (xoptind < argc) {
		char *ve;

		if (def) {
			error(0, 0, "%s: invalid argument", argv[0]);
			goto usage;
		}
		value = argv[xoptind++];
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
	fprintf(stderr, "Usage:  option NAME [VALUE]\n");
	fprintf(stderr, "    or  option -d NAME\n");
	fprintf(stderr, "Available options:\n");
	for (const char **optname = option_names; *optname; optname++)
		fprintf(stderr, "\t%s\n", *optname);
	return EXIT_FAILURE;
}
