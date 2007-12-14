/* Yash: yet another shell */
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
#define _ATFILE_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <error.h>
#include <errno.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/stat.h>
#include "yash.h"
#include <assert.h>


void initialize_readline(void);
void finalize_readline(void);
char *yash_readline(int ptype);
//static int char_is_quoted_p(char *text, int index);
//static char *quote_filename(char *text, int matchtype, char *quotepointer);
//static char *unquote_filename(char *text, int quotechar);
static char **yash_completion(const char *text, int start, int end);
static int check_completion_type(int index);
static char *normal_file_completion_function(const char *text, int state);
static char *path_completion_function(const char *text, int state);
static char *env_completion_function(const char *text, int state);
static char *expand_prompt(const char *s);

/* 履歴を保存するファイルのパス。NULL なら履歴は保存されない。 */
char *history_filename = NULL;
/* 履歴ファイルに入れる履歴の数 */
int history_filesize = 500;
/* シェル内で保存する履歴の数 */
int history_histsize = 500;
/* プロンプトとして表示する文字列。 */
char *readline_prompt1 = NULL;
char *readline_prompt2 = NULL;

/* ファイル名補完で、非実行可能ファイルを候補に入れるかどうか。 */
static bool executable_only;
/* ファイル名補完で、対象を検索するパス。path_completion_function が使う。 */
static const char *searchpath;
/* ファイル名補完で、大文字小文字の区別の有無 */
static bool completion_ignorecase;

/* readline を初期化する。
 * これは .yashrc の実行が終わった「後」に呼出す。 */
void initialize_readline(void)
{
	rl_readline_name = "yash";
	rl_attempted_completion_function = yash_completion;
	rl_filename_quote_characters = rl_basic_word_break_characters;
	/* rl_char_is_quoted_p = TODO */
	/* rl_filename_quoting_function = TODO */
	/* rl_filename_dequoting_function = TODO */

	history_comment_char = '#';

	stifle_history(history_histsize);
	if (history_filename)
		read_history(history_filename);
}

/* readline を終了する */
void finalize_readline(void)
{
	if (history_filename) {
		stifle_history(history_filesize);
		write_history(history_filename);
	}
}

/* プロンプトを表示して行を読み取る。
 * ptype が 1 か 2 なら履歴展開を行い、履歴に追加する。
 * ptype:  プロンプトの種類。1~2。(PS1~PS2 に対応)
 * 戻り値: 読み取った行。(新しく malloc した文字列)
 *         EOF が入力されたときは NULL。 */
char *yash_readline(int ptype)
{
	char *prompt, *actualprompt;
	char *line, *eline;
	bool interrupted = false;
	bool terminal_info_valid = false;
	struct termios old_terminal_info, new_terminal_info;
	struct sigaction action, oldchldaction, oldintaction;
	void readline_signal_handler(int signal) {
		switch (signal) {
			case SIGCHLD:
				wait_all(-2 /* non-blocking */);
				break;
			case SIGINT:
				interrupted = true;
				/* XXX readline interrupt */
				break;
		}
	}
	
yash_readline_start:
	switch (ptype) {
		case 1:
			prompt = readline_prompt1 ? : "\\s-\\v\\$ ";
			// TODO exec_promptcommand
			break;
		case 2:
			prompt = readline_prompt2 ? : "> ";
			break;
		default:
			error(2, 0, "internal error: yash_readline_start ptype=%d", ptype);
			assert(false);
	}

	if (tcsetpgrp(STDIN_FILENO, getpgrp()) < 0)
		error(0, errno, "tcsetpgrp before readline");

	if (tcgetattr(STDIN_FILENO, &old_terminal_info) == 0) {
		terminal_info_valid = true;
		new_terminal_info = old_terminal_info;
		new_terminal_info.c_iflag &= ~(INLCR | IGNCR);
		new_terminal_info.c_iflag |= ICRNL;
		new_terminal_info.c_lflag |= ICANON | ECHO | ECHOE | ECHOK;
		tcsetattr(STDIN_FILENO, TCSADRAIN, &new_terminal_info);
	}

	sigemptyset(&action.sa_mask);
	action.sa_handler = readline_signal_handler;
	action.sa_flags = 0;
	if (sigaction(SIGCHLD, &action, &oldchldaction) < 0)
		error(EXIT_FAILURE, errno, "sigaction before readline");
	if (sigaction(SIGINT, &action, &oldintaction) < 0)
		error(EXIT_FAILURE, errno, "sigaction before readline");

	wait_all(-2 /* non-blocking */);
	print_all_job_status(true /* changed only */, false /* not verbose */);

	actualprompt = expand_prompt(prompt);
	line = readline(actualprompt);

	if (sigaction(SIGINT, &oldintaction, NULL) < 0)
		error(EXIT_FAILURE, errno, "sigaction after readline");
	if (sigaction(SIGCHLD, &oldchldaction, NULL) < 0)
		error(EXIT_FAILURE, errno, "sigaction after readline");

	free(actualprompt);

	if (terminal_info_valid)
		tcsetattr(STDIN_FILENO, TCSADRAIN, &old_terminal_info);

	if (!line) {
		if (ptype == 1)      printf("exit\n");
		else if (ptype == 2) printf("\n");
		return NULL;
	}
	if (!*skipspaces(line)) {
		free(line);
		goto yash_readline_start;
	}

	if (ptype == 1 || ptype == 2) {
		switch (history_expand(line, &eline)) {
			case 1:  /* expansion successful */
				printf("%s\n", eline);
				/* falls thru! */
			case 0:  /* expansion successful without changes */
			default:
				free(line);
				add_history(eline);
				return eline;
			case -1:  /* Error */
				free(line);
				error(0, 0, "%s", eline);
				free(eline);
				goto yash_readline_start;
			case 2:   /* No execution */
				free(line);
				printf("%s\n", eline);
				add_history(eline);
				free(eline);
				goto yash_readline_start;
		}
	} else {
		return line;
	}
}

#if 0
/* text[index] がクォートされているかどうか判定する。 */
static int char_is_quoted_p(char *text, int index)
{
	return 0;  /*TODO*/
}

static char *quote_filename(char *text, int matchtype, char *quotepointer)
{
	return 0;  /*TODO*/
}

static char *unquote_filename(char *text, int quotechar)
{
	return 0; /*TODO*/
}
#endif

/* コマンド補完を行う。ライブラリから呼び出される。 */
static char **yash_completion(const char *text, int start, int end)
{
	char **matches = NULL;
	rl_compentry_func_t *completer;

	completion_ignorecase = strcmp(
			rl_variable_value("completion-ignore-case"), "on") == 0;
	rl_filename_quoting_desired = 1;

	switch (check_completion_type(start)) {
		case 0:  /* 通常の補完 */
			if (!executable_only)
				rl_filename_completion_desired = 1;

			if (text[0] == '~') {
				completer = !strchr(text, '/')
					? rl_username_completion_function
					: normal_file_completion_function;
				rl_filename_completion_desired = 1;
			} else if (!executable_only || strncmp(text, "/", 1) == 0
					|| strncmp(text, "./", 2) == 0
					|| strncmp(text, "../", 3) == 0) {
				completer = normal_file_completion_function;
				rl_filename_completion_desired = 1;
			} else {
				searchpath = getenv(ENV_PATH);
				completer = path_completion_function;
			}
			break;
		case 1:  /* 環境変数名の補完 */
			completer = env_completion_function;
			break;
		default:
			assert(false);
	}
	matches = rl_completion_matches(text, completer);
	rl_attempted_completion_over = 1;

	return matches;
}

/* rl_line_buffer の index 直前の文字を見て、補完の種類を決める。
 * executable_only 変数を変更する。
 * 戻り値: 0: 通常の補完
 *         1: 環境変数名の補完 */
/* この関数は rl_line_buffer の index 以降の文字は見ない。それらの文字を見て
 * 補完の種類を決めるのは呼び出し元の責任である。 */
static int check_completion_type(int index)
{
	while (--index >= 0 && rl_line_buffer[index] == ' ');
	if (index < 0)
		goto command_completion;  /* Caution!  Dangerous goto */
	switch (rl_line_buffer[index]) {
		case '|':  case '&':  case ';':  case '(':
		command_completion:
			executable_only = true;
			return 0;
		case '$':
			executable_only = false;
			return 1;
		default:
			executable_only = false;
			return 0;
	}
}

/* 作業ディレクトリを相対パスの起点として、ファイル名に基づき補完候補を挙げる。
 * text が '/' で始まるなら絶対パスとみなす。 */
static char *normal_file_completion_function(const char *text, int state)
{
	/* getbasename: path の中の最後の '/' の直後の文字の位置を返す。
	 * '/' がなければ NULL を返す。 */
	char *getbasename(const char *path) {
		char *result = strrchr(path, '/');
		return result ? result + 1 : NULL;
	}
	static int (*comparer)(const char *a, const char *b, size_t n);
	static DIR *dir = NULL;
	static char *ldname = NULL, *bname = NULL;
	static size_t ldnamelen, bnamelen;
	static int dfd;
	struct dirent *dent;
	struct stat st;

	assert(text);
	if (!state) {  /* 初期化: 新たな補完を開始 */
		ldname = xstrdup(text);
		if (!*ldname) {
			dir = opendir(".");
		} else {
			char *dname = (text[0] == '~')
				? expand_tilde(ldname) : xstrdup(ldname);
			char *dnameend = getbasename(dname);

			if (dnameend) {
				*dnameend = '\0';
				dir = opendir(dname);
			} else {
				dir = opendir(".");
			}
			free(dname);
		}
		if (!dir)
			goto end;
		dfd = dirfd(dir);
		if (dfd < 0)
			goto end;
		bname = xstrdup(getbasename(text) ? : text);
		bnamelen = strlen(bname);
		*(getbasename(ldname) ? : ldname) = '\0';
		ldnamelen = strlen(ldname);
		comparer = completion_ignorecase ? strncasecmp : strncmp;
	}
	do {
		do {
			if (!(dent = readdir(dir)))
				goto end;
		} while (comparer(bname, dent->d_name, bnamelen) != 0
				|| strcmp(".", dent->d_name) == 0
				|| strcmp("..", dent->d_name) == 0);
		if (fstatat(dfd, dent->d_name, &st, 0) < 0)
			goto end;
	} while(executable_only && !(st.st_mode & S_IXUSR) && !S_ISDIR(st.st_mode));

	char *result = xmalloc(ldnamelen + strlen(dent->d_name) + 1);
	strcpy(result, ldname);
	strcpy(result + ldnamelen, dent->d_name);
	return result;

end:
	if (dir) {
		closedir(dir);
		dir = NULL;
	}
	free(ldname);
	ldname = NULL;
	free(bname);
	bname = NULL;
	return NULL;
}

/* コマンド名を生成する。コマンド補完でライブラリから呼び出される。 */
/* searchpath 内にあるファイルで該当するものを挙げる。
 * executable_only ならば組込みコマンド・エイリアスも候補に入れる。 */
static char *path_completion_function(const char *text, int state)
{
	/* builtin: 次に調べる組込みコマンドのインデックス。
	 *          全部調べ終わったら -1。 */
	static int (*comparer)(const char *a, const char *b, size_t n);
	static ssize_t builtin;
	static size_t textlen;
	//static const ALIAS *alias;
	static DIR *dir;
	static int dfd;
	static char *savepath, *path;
	struct dirent *dent;
	struct stat st;

	if (!state) {
		builtin = 0;
		//alias = get_all_aliases();
		dir = NULL;
		savepath = path = xstrdup(searchpath ? : "");
		textlen = strlen(text);
		comparer = completion_ignorecase ? strncasecmp : strncmp;
	}
	if (builtin >= 0) {
		const char *bname;

		while ((bname = builtins[builtin++].b_name)) {
			if (comparer(bname, text, textlen) == 0)
				return xstrdup(bname);
		}
		builtin = -1;
	}
//	while (alias) {
//		const char *aliasname = alias->name;
//		alias = alias->next;
//		if (comparer(aliasname, text, textlen) == 0)
//			return xstrdup(aliasname);
//	}
	do {
next:
		while (!dir || !(dent = readdir(dir))) {
			/* 次のディレクトリに進む */
			size_t len;
			char oldend;

			if (!path)
				goto end;
			if (dir)
				closedir(dir);
			len = strcspn(path, ":");
			if (len) {
				oldend = path[len];
				path[len] = '\0';
				dir = opendir(path);
				path[len] = oldend;
				path += len;
			} else {
				dir = opendir(".");
			}
			if (dir)
				dfd = dirfd(dir);
			if (*path == '\0')
				path = NULL;
			else if (*path == ':')
				path++;
		}
		if (comparer(dent->d_name, text, textlen) != 0)
			goto next;
		if (dfd < 0 || fstatat(dfd, dent->d_name, &st, 0) < 0) {
			goto end;
		}
	} while(executable_only && !(st.st_mode & S_IXUSR) && !S_ISDIR(st.st_mode));

	return xstrdup(dent->d_name);

end:
	if (dir) {
		closedir(dir);
		dir = NULL;
	}
	free(savepath);
	return NULL;
}

/* 環境変数名の補完候補を挙げる。補完でライブラリから呼び出される。 */
static char *env_completion_function(const char *text, int state)
{
	static int (*comparer)(const char *a, const char *b, size_t n);
	static char **env;
	static size_t textlen;
	const char *envent;

	if (!state) {
		env = environ;
		if (!env)
			return NULL;
		textlen = strlen(text);
		comparer = completion_ignorecase ? strncasecmp : strncmp;
	}
	while ((envent = *env++)) {
		if (comparer(envent, text, textlen) == 0) {
			return xstrndup(envent, strcspn(envent, "="));
		}
	}
	env = NULL;
	return NULL;
}


const char *weekdaynames[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};
const char *monthnames[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

/* 文字列に含まれる '\a' や '\n' をプロンプト用に展開する。
 * 戻り値: s を展開した結果 (新しく malloc された文字列)。失敗したら NULL。 */
/* \a    ベル文字 '\07'
 * \d    日付
 * \e    エスケープ '\033'
 * \h    ホスト名の最初の '.' より前の部分
 * \H    ホスト名全体
 * \j    ジョブ数
 * \l    端末のベース名 (< ttyname(3), basename(3))
 * \L    端末名 (< ttyname(3))
 * \n    改行
 * \r    復帰
 * \s    シェルのベース名 (< 起動時の argv[0])
 * \t    時刻 (標準形式)
 * \T    HH:MM:SS 形式の時刻 (12 時間制)
 * \@    HH:MM am/pm 形式の時刻
 * \u    ユーザ名
 * \v    yash のバージョン
 * \w    PWD
 * \W    PWD のベース名
 * \!    履歴番号
 * \$    実効 UID が 0 なら '#' さもなくば '$'
 * \nnn  八進数 nnn の文字
 * \\    バックスラッシュ '\\'
 * \[    RL_PROMPT_START_IGNORE
 * \]    RL_PROMPT_END_IGNORE */
static char *expand_prompt(const char *s)
{
	char *result;
	size_t sindex, rindex, rem;
	time_t timeval = 0;
	struct tm tm;
	int ensure_rem(size_t size) {  /* rem が size 以上になるよう拡張する */
		if (rem < size) {
			rem = size + 10;
			result = xrealloc(result, rindex + rem);
			return 0;
		}
		return 0;
	}
	int append_char(char c) {  /* 文字を result に追加する */
		if (ensure_rem(2) < 0)
			return -1;
		result[rindex++] = c;
		result[rindex] = '\0';
		return 0;
	}
	int append_str(const char *s) {  /* 文字列を result に追加する */
		size_t len;

		if (!s)
			return 0;
		len = strlen(s);
		if (ensure_rem(len + 1) < 0)
			return -1;
		strcpy(result + rindex, s);
		rindex += len;
		return 0;
	}
	int get_time(void) {  /* tmtime に時刻を入れる */
		if (!timeval) {
			timeval = time(NULL);
			if (!localtime_r(&timeval, &tm))
				return -1;
		}
		return 0;
	}

	assert(s);
	rem = strlen(s) + 30;
	result = xmalloc(rem);
	rindex = sindex = 0;
	result[0] = '\0';

	/* エラーの時は result を NULL にするとループを抜ける */
loop:
	while (result && *s) {
		int len;
		char *sav;

		if (*s != '\\') {
			append_char(*s);
		} else switch (*++s) {
			case 'a':
				append_char('\a');
				break;
			case 'd':
				if (get_time() < 0) {
					append_char('?');
				} else if (ensure_rem(15) >= 0) {
					rindex += strftime(result + rindex, rem, "%x", &tm);
				}
				break;
			case 'e':
				append_char('\033');
				break;
			case 'h':
				{
					char *host = getenv(ENV_HOSTNAME);
					if (host) {
						size_t len = strcspn(host, ".");
						if (ensure_rem(len + 1) >= 0) {
							strcpy(result + rindex, host);
							rindex += len;
						}
					}
				}
				break;
			case 'H':
				append_str(getenv(ENV_HOSTNAME));
				break;
			case 'j':
				len = snprintf(result + rindex, rem, "%u", job_count());
				goto check;
			case 'l':
				append_str(basename(ttyname(STDIN_FILENO)));
				/* ↑ basename が ttyname の戻り値を書き換えるかもしれない */
				break;
			case 'L':
				append_str(ttyname(STDIN_FILENO));
				break;
			case 'n':
				append_char('\n');
				break;
			case 'r':
				append_char('\r');
				break;
			case 's':
				append_str(program_invocation_short_name);
				break;
			case 't':
				if (get_time() < 0) {
					append_char('?');
				} else if (ensure_rem(15) >= 0) {
					rindex += strftime(result + rindex, rem, "%X", &tm);
				}
				break;
			case 'T':
				if (get_time() < 0) {
					append_char('?');
				} else if (ensure_rem(15) >= 0) {
					rindex += strftime(result + rindex, rem, "%I:%M:%S", &tm);
				}
				break;
			case '@':
				if (get_time() < 0) {
					append_char('?');
				} else if (ensure_rem(15) >= 0) {
					rindex += strftime(result + rindex, rem, "%I:%M %p", &tm);
				}
				break;
			case 'u':
				append_str(cuserid(NULL));
				break;
			case 'v':
				append_str(YASH_VERSION);
				break;
			case 'w':
				sav = collapse_homedir(getenv(ENV_PWD));
				if (sav) {
					append_str(sav);
					free(sav);
				}
				break;
			case 'W':
				sav = collapse_homedir(getenv(ENV_PWD));
				if (sav) {
					append_str(basename(sav));
					free(sav);
				}
				break;
			case '!':
				using_history();
				len = snprintf(result + rindex, rem, "%d",
						history_base + where_history());
				goto check;
			case '$':
				append_char(geteuid() ? '$' : '#');
				break;
			/* case '\\': */   /* 不要: default で処理 */
			case '0':  case '1':  case '2':  case '3':
			case '4':  case '5':  case '6':  case '7':
				if ('0' <= s[1] && s[1] <= '7' && '0' <= s[2] && s[2] <= '7') {
					append_char(
							((((s[0] - '0') << 3)
							  + s[1] - '0') << 3)
							  + s[2] - '0');
					s += 2;
				} else {
					goto ldefault;
				}
				break;
			case '[':
				append_char(RL_PROMPT_START_IGNORE);
				break;
			case ']':
				append_char(RL_PROMPT_END_IGNORE);
				break;
			case '\0':
				append_char('\\');
				continue;
			default:
			ldefault:
				append_char('\\');
				append_char(*s);
				break;
			check:
				if (len < 0) {
					append_char('?');
				} else if ((size_t) len >= rem) {
					ensure_rem(len + 1);
					goto loop;
				} else {
					rindex += len;
				}
				break;
		}
		s++;
	}
	return result;
}
