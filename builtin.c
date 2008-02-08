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


#include "common.h"
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef USE_READLINE
# ifdef HAVE_LIBREADLINE
#  include <readline/history.h>
# else
#  include <history.h>
# endif
#endif
#include "yash.h"
#include "util.h"
#include "sig.h"
#include "lineinput.h"
#include "expand.h"
#include "exec.h"
#include "job.h"
#include "path.h"
#include "builtin.h"
#include "alias.h"
#include "variable.h"
#include <assert.h>


/* 組込みコマンド一般仕様:
 * - argc は少なくとも 1 で、argv[0] は呼び出されたコマンドの名前である。
 * - argv 内の文字列の内容を変更したり、引数の順番を並び替えたりしてもよい。
 * - 特殊組込みコマンドでない組込みコマンドは中で exec_single を
 *   呼んではいけない。一コマンド分しか一時的変数を記憶できないからである。
 *   また、fork してもならない。一時的変数が export されないからである。
 *   特殊組込みコマンドでは一時的変数は存在しない。 */

struct hasht builtins;

static struct builtin_info {
	const char *name;
	BUILTIN builtin;
} builtins_array[] = {

	/* builtin.c の組込みコマンド */
	{ ":",       { builtin_true,    true  }},
	{ "true",    { builtin_true,    false }},
	{ "false",   { builtin_false,   false }},
	{ "cd",      { builtin_cd,      false }},
	{ "pwd",     { builtin_pwd,     false }},
	{ "type",    { builtin_type,    false }},
	{ "umask",   { builtin_umask,   false }},
	{ "history", { builtin_history, false }},
	{ "alias",   { builtin_alias,   false }},
	{ "unalias", { builtin_unalias, false }},
	{ "option",  { builtin_option,  false }},
	{ "hash",    { builtin_hash,    false }},
	{ "rehash",  { builtin_hash,    false }},

	/* builtin_job.c の組込みコマンド */
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
	{ ".",       { builtin_source,  true  }},
	{ "source",  { builtin_source,  true  }},
	{ "eval",    { builtin_eval,    true  }},

	/* builtin_var.c の組込みコマンド */
	{ "export",  { builtin_export,  true  }},
	{ "unset",   { builtin_unset,   true  }},

	{ NULL,      { 0,               false }},

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

/* init_builtin で初期化した内容を消す */
void finalize_builtin(void)
{
	ht_destroy(&builtins);
}

/* 指定した名前に対応する組込みコマンド関数を取得する。
 * 対応するものがなければ NULL を返す。 */
BUILTIN *get_builtin(const char *name)
{
	return ht_get(&builtins, name);
}

/* :/true 組込みコマンド */
int builtin_true(int argc __attribute__((unused)),
		char **argv __attribute__((unused)))
{
	return EXIT_SUCCESS;
}

/* false 組込みコマンド */
int builtin_false(int argc __attribute__((unused)),
		char **argv __attribute__((unused)))
{
	return EXIT_FAILURE;
}

/* cd 組込みコマンド */
int builtin_cd(int argc, char **argv)
{
	bool logical = true, print = false;
	const char *newpwd, *oldpwd;
	char *path;
	int opt;

	xoptind = 0;
	xopterr = true;
	while ((opt = xgetopt(argv, "LP")) >= 0) {
		switch (opt) {
			case 'L':
				logical = true;
				break;
			case 'P':
				logical = false;
				break;
			default:
				goto usage;
		}
	}

	oldpwd = getvar(VAR_PWD);
	if (!oldpwd || !*oldpwd) {
		char *path = xgetcwd();
		if (!path) {
			xerror(0, errno, "%s: cannot identify current directory", argv[0]);
			return EXIT_FAILURE;
		}
		setvar(VAR_PWD, path, false);
		oldpwd = getvar(VAR_PWD);
		assert(oldpwd);
	}

	if (xoptind >= argc) {
		newpwd = getvar(VAR_HOME);
		if (!newpwd) {
			xerror(0, 0, "%s: HOME directory not specified", argv[0]);
			return EXIT_FAILURE;
		}
	} else {
		newpwd = argv[xoptind];
		if (strcmp(newpwd, "-") == 0) {
			newpwd = getvar(VAR_OLDPWD);
			print = true;
			if (!newpwd) {
				xerror(0, 0, "%s: OLDPWD directory not specified", argv[0]);
				return EXIT_FAILURE;
			}
		}
	}

#if 0
	if (matchprefix(newpwd, "./") || matchprefix(newpwd, "../")
			|| strcmp(newpwd, ".") == 0 || strcmp(newpwd, "..") == 0)
#else
	if ((newpwd[0] == '.' && (newpwd[1] == '\0' || newpwd[1] == '/' ||
			 (newpwd[1] == '.' && (newpwd[2] == '\0' || newpwd[2] == '/')))))
#endif
		path = NULL;
	else
		path = which(newpwd, getvar(VAR_CDPATH), is_directory);
	if (path) {
		print |= which_found_in_path;
	} else {
		struct strbuf buf;
		sb_init(&buf);
		sb_append(&buf, oldpwd);
		sb_cappend(&buf, '/');
		sb_append(&buf, newpwd);
		path = sb_tostr(&buf);
	}
	if (logical) {
		if (/* !posixly_correct && */ path[0] != '/') {
			/* path が絶対パスでなければ oldpwd を前置して絶対パスに直す。
			 * POSIX 仕様にはこの操作の規定はないが、ここで path を絶対パスに
			 * 直さないと、PWD 環境変数に path が相対パスのまま入ってしまう。 */
			/* oldpwd も絶対パスでなければ、-P オプションと同様に扱う */
			if (oldpwd[0] != '/')
				goto phisical;
			struct strbuf buf;
			sb_init(&buf);
			sb_append(&buf, oldpwd);
			sb_cappend(&buf, '/');
			sb_append(&buf, path);
			free(path);
			path = sb_tostr(&buf);
		}
		char *curpath = canonicalize_path(path);
		free(path);
		if (chdir(curpath) < 0) {
			xerror(0, errno, "%s: %s", argv[0], curpath);
			free(curpath);
			return EXIT_FAILURE;
		}
		setvar(VAR_OLDPWD, oldpwd, false);
		setvar(VAR_PWD, curpath, false);
		if (print)
			printf("%s\n", curpath);
		free(curpath);
	} else {
phisical:
		if (chdir(path) < 0) {
			xerror(0, errno, "%s: %s", argv[0], path);
			free(path);
			return EXIT_FAILURE;
		}
		free(path);
		setvar(VAR_OLDPWD, oldpwd, false);
		if ((path = xgetcwd())) {
			setvar(VAR_PWD, path, false);
			if (print)
				printf("%s\n", path);
			free(path);
		}
	}
	return EXIT_SUCCESS;

usage:
	fprintf(stderr, "Usage:  cd [-L|-P] dir\n");
	return EXIT_FAILURE;
}

/* pwd 組込みコマンド */
int builtin_pwd(int argc __attribute__((unused)), char **argv)
{
	bool logical = true;
	int opt;

	xoptind = 0;
	xopterr = true;
	while ((opt = xgetopt(argv, "LP")) >= 0) {
		switch (opt) {
			case 'L':
				logical = true;
				break;
			case 'P':
				logical = false;
				break;
			default:
				goto usage;
		}
	}

	if (logical) {
		const char *pwd = getvar(VAR_PWD);
		if (!pwd || pwd[0] != '/')
			goto notlogical;
		printf("%s\n", pwd);
	} else {
notlogical:;
		char *pwd = xgetcwd();
		if (!pwd) {
			xerror(0, errno, "%s", argv[0]);
			return EXIT_FAILURE;
		}
		if (posixly_correct)
			setvar(VAR_PWD, pwd, false);
		printf("%s\n", pwd);
		free(pwd);
	}
	return EXIT_SUCCESS;

usage:
	fprintf(stderr, "Usage:  pwd [-L|-P]\n");
	return EXIT_FAILURE;
}

/* type 組込みコマンド */
int builtin_type(int argc, char **argv)
{
	if (argc <= 1)
		goto usage;
	for (int i = 1; i < argc; i++) {
		if (strchr(argv[i], '/') == NULL) {
			ALIAS *alias = get_alias(argv[i]);
			if (alias && !alias->global) {
				printf("%s is aliased to `%s'\n", argv[i], alias->value);
			}
			BUILTIN *builtin = get_builtin(argv[i]);
			if (builtin) {
				printf("%s is a built-in command\n", argv[i]);
				continue;
			}
			const char *ext = ht_get(&cmdhash, argv[i]);
			if (ext) {
				printf("%s is in hashtable (%s)\n", argv[i], ext);
			} else {
				char *path = which(argv[i], getvar(VAR_PATH), is_executable);
				if (path) {
					printf("%s is %s\n", argv[i], path);
					free(path);
				} else {
					if (!alias)
						printf("%s: not found\n", argv[i]);
				}
			}
		} else {
			if (is_executable(argv[i]))
				printf("%s is executable\n", argv[i]);
			else if (access(argv[i], F_OK) == 0)
				printf("%s is not executable\n", argv[i]);
			else
				printf("%s: not found\n", argv[i]);
		}
	}
	return EXIT_SUCCESS;

usage:
	fprintf(stderr, "Usage:  type args...\n");
	return EXIT_FAILURE;
}

/* umask 組込みコマンド */
/* XXX: 数字だけでなく文字列での指定に対応 */
int builtin_umask(int argc, char **argv)
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
			xerror(0, errno, "%s", argv[0]);
			goto usage;
		} else if (*c) {
			xerror(0, 0, "%s: invalid argument", argv[0]);
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

/* history 組込みコマンド
 * 引数なし: 全ての履歴を表示
 * 数値引数: 最近の n 個の履歴を表示
 * -c:       履歴を全て削除
 * -d n:     履歴番号 n を削除
 * -r file:  file から履歴を読み込む (今の履歴に追加)
 * -w file:  file に履歴を保存する (上書き)
 * -s X:     X を履歴に追加 */
int builtin_history(int argc, char **argv)
{
#ifdef USE_READLINE
	int ierrno;

	using_history();
	if (argc >= 2 && strlen(argv[1]) == 2 && argv[1][0] == '-') {
		switch (argv[1][1]) {
			case 'c':
				clear_history();
				return EXIT_SUCCESS;
			case 'd':
				if (argc < 3) {
					xerror(0, 0, "%s: -d: missing argument", argv[0]);
					return EXIT_FAILURE;
				} else if (argc > 3) {
					xerror(0, 0, "%s: -d: too many arguments", argv[0]);
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
						free(yash_free_history_entry(entry));
				}
				return EXIT_SUCCESS;
			case 'r':
				ierrno = read_history(argv[2] ? argv[2] : history_filename);
				if (ierrno) {
					xerror(0, ierrno, "%s", argv[0]);
					return EXIT_FAILURE;
				}
				return EXIT_SUCCESS;
			case 'w':
				ierrno = write_history(argv[2] ? argv[2] : history_filename);
				if (ierrno) {
					xerror(0, ierrno, "%s", argv[0]);
					return EXIT_FAILURE;
				}
				return EXIT_SUCCESS;
			case 's':
				if (argc > 2) {
					char *line = strjoin(-1, argv + 2, " ");
					HIST_ENTRY *oldentry = replace_history_entry(
							where_history() - 1, line, NULL);
					if (oldentry)
						free(yash_free_history_entry(oldentry));
					free(line);
				}
				return EXIT_SUCCESS;
			default:
				xerror(0, 0, "%s: invalid argument", argv[0]);
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
	for (int offset = (history_length > count ? history_length - count : 0);
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
#else
	(void) argc;
	fprintf(stderr, "%s: readline/history unavailable\n", argv[0]);
	return EXIT_FAILURE;
#endif /* !USE_READLINE */
}

/* builtin_alias 内で各エイリアスを出力する内部関数 */
static int print_alias(const char *name, ALIAS *alias) {
	struct strbuf buf;
	if (!posixly_correct || !alias->global) {
		sb_init(&buf);
		if (!posixly_correct)
			sb_printf(&buf, "alias %-3s", alias->global ? "-g" : "");
		sb_append(&buf, name);
		sb_cappend(&buf, '=');
		escape_sq(alias->value, &buf);
		puts(buf.contents);
		sb_destroy(&buf);
	}
	return 0;
}

/* alias 組込みコマンド
 * 引数なし or -p オプションありだと全てのエイリアスを出力する。
 * 引数があると、そのエイリアスを設定 or 出力する。
 * -g を指定するとグローバルエイリアスになる。 */
int builtin_alias(int argc, char **argv)
{
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
			xerror(0, 0, "%s: %s: invalid argument", argv[0], c);
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
				xerror(0, 0, "%s: %s: no such alias", argv[0], c);
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
int builtin_unalias(int argc, char **argv)
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
			xerror(0, 0, "%s: %s: no such alias", argv[0], argv[xoptind]);
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
int builtin_option(int argc, char **argv)
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
			xerror(0, 0, "%s: invalid argument", argv[0]);
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
			if (strcmp(value, "yes") == 0)
				huponexit = true;
			else if (strcmp(value, "no") == 0)
				huponexit = false;
			else
				goto valueyesnoinvalid;
		} else if (def) {
			huponexit = false;
		} else {
			printf("%s: %s\n", name, huponexit ? "yes" : "no");
		}
	} else {
		xerror(0, 0, "%s: %s: unknown option", argv[0], name);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
	
valuenuminvalid:
	xerror(0, 0, "%s: value of `%s' must be a number", argv[0], name);
	return EXIT_FAILURE;
valueyesnoinvalid:
	xerror(0, 0, "%s: value of `%s' must be `yes' or `no'", argv[0], name);
	return EXIT_FAILURE;

usage:
	fprintf(stderr, "Usage:  option NAME [VALUE]\n");
	fprintf(stderr, "    or  option -d NAME\n");
	fprintf(stderr, "Available options:\n");
	for (const char **optname = option_names; *optname; optname++)
		fprintf(stderr, "\t%s\n", *optname);
	return EXIT_FAILURE;
}

/* hash/rehash 組込みコマンド
 * -r: ハッシュテーブルを空にする */
int builtin_hash(int argc, char **argv)
{
	bool rehash = (strcmp(argv[0], "rehash") == 0);
	bool err = false;
	int opt;

	/* 引数・オプションがなければ、ハッシュテーブルの内容を表示する。 */
	if (argc == 1 && !rehash) {
		size_t index = 0;
		struct keyvaluepair kv;
		while ((kv = ht_next(&cmdhash, &index)).key && kv.value) {
			printf("%s\n", (char *) kv.value);
		}
		return EXIT_SUCCESS;
	}

	xoptind = 0;
	xopterr = true;
	while ((opt = xgetopt(argv, "r")) >= 0) {
		switch (opt) {
			case 'r':
				rehash = true;
				break;
			default:
				goto usage;
		}
	}

	if (rehash) {
		clear_cmdhash();
	}
	while (xoptind < argc) {
		if (strchr(argv[xoptind], '/') != NULL) {
			xerror(0, 0, "%s: %s: contains `/'", argv[0], argv[xoptind]);
			err = true;
		} else if (!get_command_fullpath(argv[xoptind], true)) {
			xerror(0, 0, "%s: %s: not found", argv[0], argv[xoptind]);
			err = true;
		}
		xoptind++;
	}
	return err ? EXIT_FAILURE : EXIT_SUCCESS;

usage:
	fprintf(stderr, "Usage:  hash [-r] [name...]\n");
	fprintf(stderr, "        `rehash' is equivalent to `hash -r'\n");
	return EXIT_FAILURE;
}
