/* Yash: yet another shell */
/* readline.c: interface to readline library */
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
#define NO_LINEINPUT_INLINE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#ifdef USE_READLINE
# ifdef HAVE_LIBREADLINE
#  include <readline/readline.h>
#  include <readline/history.h>
# else
#  include <readline.h>
#  include <history.h>
# endif
#endif
#include <sys/select.h>
#include <sys/stat.h>
#include "yash.h"
#include "util.h"
#include "lineinput.h"
#include "exec.h"
#include "job.h"
#include "path.h"
#include "builtin.h"
#include "alias.h"
#include "variable.h"
#include "version.h"
#include <assert.h>


static bool unset_nonblocking(int fd);
#ifdef USE_READLINE
//static int char_is_quoted_p(char *text, int index);
//static char *quote_filename(char *text, int matchtype, char *quotepointer);
//static char *unquote_filename(char *text, int quotechar);
static int check_inhibit_expansion(char *str, int index);
#endif
static char *expand_prompt(const char *s)
	__attribute__((nonnull));
#ifdef USE_READLINE
# if defined(RL_READLINE_VERSION) && RL_READLINE_VERSION >= 0x0402
static rl_completion_func_t yash_attempted_completion;
static rl_compentry_func_t yash_completion_entry;
# else
static char **yash_attempted_completion(const char *text, int start, int end);
static char *yash_completion_entry(const char *text, int noinit);
# endif
#endif


/* 履歴を保存するファイルのパス。NULL なら履歴は保存されない。 */
char *history_filename = NULL;
/* 履歴ファイルに入れる履歴の数 */
int history_filesize = 500;
/* シェル内で保存する履歴の数 */
int history_histsize = 500;
/* プロンプトとして表示する文字列。 */
char *readline_prompt1 = NULL;
char *readline_prompt2 = NULL;


/* readline を初期化する。
 * これは .yashrc の実行が終わった「後」に呼出す。 */
void initialize_readline(void)
{
#ifdef USE_READLINE
	rl_readline_name = "yash";
	rl_outstream = stderr;
	rl_getc_function = yash_getc;
	rl_attempted_completion_function = yash_attempted_completion;
	//rl_completion_entry_function = yash_completion_entry;
	rl_filename_quote_characters = " \t\n\\\"'<>|;&()#$`?*[!{";
	/* rl_char_is_quoted_p = XXX */
	/* rl_filename_quoting_function = XXX */
	/* rl_filename_dequoting_function = XXX */

	history_comment_char = '#';
	history_no_expand_chars = " \t\n={}|";
	history_quotes_inhibit_expansion = true;
	history_inhibit_expansion_function = check_inhibit_expansion;
	stifle_history(history_histsize);
	if (history_filename)
		read_history(history_filename);
#endif /* USE_READLINE */
}

/* readline を終了する */
void finalize_readline(void)
{
#ifdef USE_READLINE
	if (history_filename) {
		stifle_history(history_filesize);
		write_history(history_filename);
	}
#endif /* USE_READLINE */
}

/* 指定したファイルディスクリプタを非ブロッキングモードにする。
 * 戻り値: 成功なら true、エラーなら errno を設定して false。 */
static bool unset_nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL);
	if (flags >= 0) {
		if (flags & O_NONBLOCK) {
			return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) != -1;
		} else {
			return true;
		}
	}
	return false;
}

/* fgetc のラッパー。シグナルを正しく処理する。rl_getc_function として使う。 */
int yash_getc(FILE *stream)
{
	sigset_t newset, oldset;
	fd_set fds;
	int fd = fileno(stream);
	int result;

	sigfillset(&newset);
	sigemptyset(&oldset);
	if (sigprocmask(SIG_BLOCK, &newset, &oldset) < 0) {
		xerror(0, errno, NULL);
		return EOF;
	}
	for (;;) {
		handle_signals();
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		if (pselect(fd + 1, &fds, NULL, NULL, NULL, &oldset) < 0) {
			switch (errno) {
				case EINTR:
					continue;
				default:
#if defined(RL_ISSTATE) && defined(RL_STATE_READCMD) && defined(READERR)
					result = RL_ISSTATE(RL_STATE_READCMD) ? READERR : EOF;
#else
					result = EOF;
#endif
					goto end;
			}
		}
		if (FD_ISSET(fd, &fds)) {
			unsigned char c;
			ssize_t r = read(fd, &c, sizeof c);
			if (r == sizeof c) {
				result = c;
				goto end;
			} else if (r == 0) {
				result = EOF;
				goto end;
			} else if (errno == EINTR) {
				continue;
			} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
				if (unset_nonblocking(fd)) {
					continue;
				} else {
					result = EOF;
					goto end;
				}
			} else {
				result = EOF;
				goto end;
			}
		}
	}
end:
	if (sigprocmask(SIG_SETMASK, &oldset, NULL) < 0)
		xerror(0, errno, NULL);
	return result;
}

/* プロンプトを出さずに yash_getline_input から行を読み取る。
 * info:   struct fgetline_info へのポインタ。
 * 戻り値: 読み取った行。(新しく malloc した文字列)
 *         EOF が入力されたときは NULL。 */
char *yash_fgetline(int ptype __attribute__((unused)), void *info)
{
	struct fgetline_info *finfo = info;
	char *bufstart = finfo->buffer + finfo->bufoff;
	sigset_t newset, oldset;
	fd_set fds;
	struct strbuf buf;
	char *result;

	/* まず info 内のバッファに既に一行分のデータがないか調べる */
	char *end = memchr(bufstart, '\n', finfo->buflen);
	if (end) {
		result = xstrndup(bufstart, end - bufstart);
		finfo->buflen -= end - bufstart + 1;
		finfo->bufoff += end - bufstart + 1;
		return result;
	}

	/* 駄目だったら実際にファイルから読み込む */
	sigfillset(&newset);
	sigemptyset(&oldset);
	if (sigprocmask(SIG_BLOCK, &newset, &oldset) < 0) {
		xerror(0, errno, NULL);
		return NULL;
	}
	sb_init(&buf);
	sb_nappend(&buf, bufstart, finfo->buflen);
	finfo->bufoff = finfo->buflen = 0;
	for (;;) {
		handle_signals();
		FD_ZERO(&fds);
		FD_SET(finfo->fd, &fds);
		if (pselect(finfo->fd + 1, &fds, NULL, NULL, NULL, &oldset) < 0){
			if (errno == EINTR) {
				continue;
			} else {
				sb_destroy(&buf);
				result = NULL;
				goto end;
			}
		}
		if (FD_ISSET(finfo->fd, &fds)) {
			ssize_t r = read(finfo->fd, finfo->buffer, sizeof finfo->buffer);
			if (r < 0) {
				if (errno == EINTR) {
					continue;
				} else if ((errno == EAGAIN || errno == EWOULDBLOCK)
						&& unset_nonblocking(finfo->fd)) {
					continue;
				} else {
					sb_destroy(&buf);
					result = NULL;
					goto end;
				}
			} else if (r == 0) {
				/* ファイルの終端に達した場合、バッファに何文字か入っていれば
				 * それを返し、バッファが空なら NULL を返す。 */
				if (buf.length) {
					result = sb_tostr(&buf);
				} else {
					sb_destroy(&buf);
					result = NULL;
				}
				goto end;
			} else {
				end = memchr(finfo->buffer, '\n', r);
				if (end) {
					sb_nappend(&buf, finfo->buffer, end - finfo->buffer);
					finfo->bufoff = end - finfo->buffer + 1;
					finfo->buflen = r - finfo->bufoff;
					result = sb_tostr(&buf);
					goto end;
				} else {
					sb_nappend(&buf, finfo->buffer, sizeof finfo->buffer);
				}
			}
		}
	}
end:
	if (sigprocmask(SIG_SETMASK, &oldset, NULL) < 0)
		xerror(0, errno, NULL);
	return result;
}

/* 文字列 yash_sgetline_src の yash_sgetline_offset 文字目から行を読み取る。
 * info:   struct sgetline_info へのポインタ。
 *         読み取った文字数だけ offset が増える。
 * 戻り値: 読み取った行。(新しく malloc した文字列)
 *         文字列の終端に達したときは NULL。 */
char *yash_sgetline(int ptype __attribute__((unused)), void *info)
{
	struct sgetline_info *i = info;
	const char *start = i->src + i->offset;
	const char *newline = start;
	if (!*start) return NULL;
	while (*newline && *newline != '\n') newline++;
	size_t len = newline - start;
	if (*newline == '\n') {
		char *result = xstrndup(start, len);
		i->offset += len + 1;
		return result;
	}
	i->offset += len;
	return xstrdup(start);
}

/* プロンプトを表示して行を読み取る。
 * ptype が正数なら履歴展開を行い、履歴に追加する。
 * ptype:  プロンプトの種類。1~2 またはその負数。(PS1~PS2 に対応)
 * 戻り値: 読み取った行。(新しく malloc した文字列)
 *         EOF が入力されたときは NULL。 */
char *yash_readline(int ptype, void *info __attribute__((unused)))
{
	char *prompt, *actualprompt;
	char *line;
	bool hist;
	bool terminal_info_valid = false;
	struct termios old_terminal_info, new_terminal_info;
#ifndef USE_READLINE
	static struct fgetline_info glinfo = { .fd = STDIN_FILENO };
#endif
	
	if (ptype >= 0) {
		hist = !posixly_correct;
	} else {
		hist = false;
		ptype = -ptype;
	}
yash_readline_start:
	switch (ptype) {
		case 1:
			wait_chld();
			print_all_job_status(true /* changed only */, false);
			prompt = readline_prompt1 ? readline_prompt1 : "\\s-\\v\\$ ";
			break;
		case 2:
			prompt = readline_prompt2 ? readline_prompt2 : "> ";
			break;
		default:
			assert(false);
	}

	if (ttyfd >= 0 && tcsetpgrp(ttyfd, getpgrp()) < 0)
		xerror(0, errno, "tcsetpgrp before readline");

	if (tcgetattr(STDIN_FILENO, &old_terminal_info) == 0) {
		terminal_info_valid = true;
		new_terminal_info = old_terminal_info;
		new_terminal_info.c_iflag &= ~(INLCR | IGNCR);
		new_terminal_info.c_iflag |= ICRNL;
		new_terminal_info.c_lflag |= ICANON | ECHO | ECHOE | ECHOK;
		tcsetattr(STDIN_FILENO, TCSADRAIN, &new_terminal_info);
	}

	actualprompt = expand_prompt(prompt);
#ifdef USE_READLINE
	line = readline(actualprompt);
#else
	fflush(stdout);
	fputs(actualprompt, stderr);
	fflush(stderr);
	line = yash_fgetline(ptype, &glinfo);
#endif
	free(actualprompt);

	if (terminal_info_valid)
		tcsetattr(STDIN_FILENO, TCSADRAIN, &old_terminal_info);

	if (!line) {
		if (ptype == 1)
			fputs(is_loginshell ? "logout\n" : "exit\n", stderr);
		else if (ptype == 2)
			fputs("\n", stderr);
		return NULL;
	}

#ifdef USE_READLINE
	char *eline;
	if (hist) {
		switch (history_expand(line, &eline)) {
			case 1:  /* expansion successful */
				fprintf(stderr, "%s\n", eline);
				/* falls thru! */
			case 0:  /* expansion successful without changes */
			default:
				free(line);
				if (*skipspaces(eline))
					add_history(eline);
				return eline;
			case -1:  /* Error */
				free(line);
				xerror(0, 0, "%s", eline);
				free(eline);
				goto yash_readline_start;
			case 2:   /* No execution */
				free(line);
				fprintf(stderr, "%s\n", eline);
				if (*skipspaces(eline))
					add_history(eline);
				free(eline);
				goto yash_readline_start;
		}
	} else
#endif /* USE_READLINE */
	{
		return line;
	}
}

#ifdef USE_READLINE

#if 0
/* text[index] がクォートされているかどうか判定する。 */
static int char_is_quoted_p(char *text, int index)
{
	return 0;  /*XXX*/
}

static char *quote_filename(char *text, int matchtype, char *quotepointer)
{
	return 0;  /*XXX*/
}

static char *unquote_filename(char *text, int quotechar)
{
	return 0; /*XXX*/
}
#endif

/* str の index 文字目で行う予定の履歴展開を抑止すべきかどうかを判定する。 */
static int check_inhibit_expansion(char *str, int index)
{
	if (index > 0 && (str[index - 1] == '{' || str[index - 1] == '$'))
		return true;
	return false;
}

#endif /* USE_READLINE */


static struct tm *get_time(void)
{
	time_t timeval = time(NULL);
	if (timeval != (time_t) -1) {
		return localtime(&timeval);
	}
	return NULL;
}

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
 * \S    シェルの名前     (< 起動時の argv[0])
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
	struct strbuf result;
	struct tm *tm = NULL;

	sb_init(&result);
	while (*s) {
		if (*s != '\\') {
			sb_cappend(&result, *s);
		} else switch (*++s) {
			case 'a':
				sb_cappend(&result, '\a');
				break;
			case 'd':
				if (!tm)
					tm = get_time();
				if (tm)
					sb_strftime(&result, "%x", tm);
				break;
			case 'e':
				sb_cappend(&result, '\033');
				break;
			case 'h':
				{
					const char *host = getvar(VAR_HOSTNAME);
					if (host)
						sb_nappend(&result, host, strcspn(host, "."));
				}
				break;
			case 'H':
				{
					const char *host = getvar(VAR_HOSTNAME);
					if (host)
						sb_append(&result, host);
				}
				break;
			case 'j':
				sb_printf(&result, "%u", job_count());
				break;
			case 'l':
				{
					char *name = ttyname(STDIN_FILENO);
					if (name) {
						char bn[strlen(name) + 1];
						strcpy(bn, name);
						name = basename(bn);
						if (name)
							sb_append(&result, name);
					}
				}
				break;
			case 'L':
				{
					char *name = ttyname(STDIN_FILENO);
					if (name)
						sb_append(&result, name);
				}
				break;
			case 'n':
				sb_cappend(&result, '\n');
				break;
			case 'r':
				sb_cappend(&result, '\r');
				break;
			case 's':
				sb_append(&result, yash_program_invocation_short_name);
				break;
			case 'S':
				sb_append(&result, yash_program_invocation_name);
				break;
			case 't':
				if (!tm)
					tm = get_time();
				if (tm)
					sb_strftime(&result, "%X", tm);
				break;
			case 'T':
				if (!tm)
					tm = get_time();
				if (tm)
					sb_strftime(&result, "%I:%M:%S", tm);
				break;
			case '@':
				if (!tm)
					tm = get_time();
				if (tm)
					sb_strftime(&result, "%I:%M %p", tm);
				break;
			case 'u':
				{
					const char *user = getvar(VAR_USER);
					if (!user)
						user = getvar(VAR_LOGNAME);
					if (user)
						sb_append(&result, user);
				}
				break;
			case 'v':
				sb_append(&result, PACKAGE_VERSION);
				break;
			case 'w':
				{
					char *dir = collapse_homedir(getvar(VAR_PWD));
					if (dir) {
						sb_append(&result, dir);
						free(dir);
					}
				}
				break;
			case 'W':
				{
					char *dir = collapse_homedir(getvar(VAR_PWD));
					if (dir) {
						const char *dir2 = basename(dir);
						if (dir2)
							sb_append(&result, dir2);
						free(dir);
					}
				}
				break;
			case '!':
#ifdef USE_READLINE
				using_history();
				sb_printf(&result, "%d", history_base + where_history());
#endif
				break;
			case '$':
				sb_cappend(&result, geteuid() ? '$' : '#');
				break;
			case '0':  case '1':  case '2':  case '3':
			case '4':  case '5':  case '6':  case '7':
				if ('0' <= s[1] && s[1] <= '7' && '0' <= s[2] && s[2] <= '7') {
					sb_cappend(&result,
							((((s[0] - '0') << 3)
							  + s[1] - '0') << 3)
							  + s[2] - '0');
					s += 2;
				} else {
					goto default_case;
				}
				break;
			case '[':
#ifdef RL_PROMPT_START_IGNORE
				sb_cappend(&result, RL_PROMPT_START_IGNORE);
#endif
				break;
			case ']':
#ifdef RL_PROMPT_END_IGNORE
				sb_cappend(&result, RL_PROMPT_END_IGNORE);
#endif
				break;
			case '\0':
				sb_cappend(&result, '\\');
				continue;
			default:  default_case:
				sb_cappend(&result, '\\');
				sb_cappend(&result, *s);
				break;
		}
		s++;
	}
	return sb_tostr(&result);
}


/********** Readline 用補助関数 **********/

#ifdef USE_READLINE

/* 履歴のエントリを解放する */
void *yash_free_history_entry(HIST_ENTRY *entry)
{
# if defined(RL_READLINE_VERSION) && RL_READLINE_VERSION >= 0x0500
	return free_history_entry(entry);
# else
	if (!entry)
		return NULL;

	void *result = entry->data;
	free(entry->line);
	free(entry);
	return result;
# endif
}

#endif /* USE_READLINE */


/********** Readline 補完ルーチン **********/

#ifdef USE_READLINE

static char **yash_attempted_completion(const char *text, int start, int end);
static struct compinfo *define_completion(int start, int end);
static char *yash_completion_entry(const char *text, int noinit);
static char *yash_filename_completion(const char *text, int noinit);
static char *yash_keyword_completion(const char *text, int noinit);
static char *yash_builtin_completion(const char *text, int noinit);
static char *yash_external_command_completion(const char *text, int noinit);
static char *yash_alias_completion(const char *text, int noinit);
static char *yash_variable_completion(const char *text, int noinit);
static char *yash_function_completion(const char *text, int noinit);
static char *yash_job_completion(const char *text, int noinit);

static struct compinfo *currentinfo;
static bool ignorecase;
static size_t comp_iter_index;

/* path にチルダ展開を行ってから access(3p) する。 */
static int access_tilde(const char *path, int amode)
{
	if (path[0] != '~')
		return access(path, amode);

	char *newpath = expand_tilde(path);
	if (!newpath) {
		errno = ENOENT;
		return -1;
	}

	int result = access(newpath, amode);
	int saveerrno = errno;
	free(newpath);
	errno = saveerrno;
	return result;
}

/* path にチルダ展開を行ってから stat(3p) する。 */
static int stat_tilde(const char *restrict path, struct stat *restrict buf)
{
	if (path[0] != '~')
		return stat(path, buf);

	char *newpath = expand_tilde(path);
	if (!newpath) {
		errno = ENOENT;
		return -1;
	}

	int result = stat(newpath, buf);
	int saveerrno = errno;
	free(newpath);
	errno = saveerrno;
	return result;
}

/* 補完候補を生成する。Readline ライブラリから呼び出される。
 * text: 補完する文字列
 * start, end: rl_line_buffer における text の位置
 * 戻り値: 補完候補への NULL 終端配列。 */
static char **yash_attempted_completion(const char *text, int start, int end)
{
	const char *vv = NULL;
#if defined(RL_READLINE_VERSION) && RL_READLINE_VERSION >= 0x0501
	vv = rl_variable_value("completion-ignore-case");
#endif
	ignorecase = vv && (strcmp(vv, "on") == 0);

	currentinfo = define_completion(start, end);
	rl_attempted_completion_over = 1;
	return rl_completion_matches(text, yash_completion_entry);
}

/* これからどのような補完を行うべきか、補完動作の内容を決める。 */
static struct compinfo *define_completion(
		int start, int end __attribute__((unused)))
{
	static struct compinfo tempinfo;
	int index = start;

	if ((index > 0 && rl_line_buffer[index - 1] == '$')
			|| (index > 1 && rl_line_buffer[index - 1] == '{'
			              && rl_line_buffer[index - 2] == '$')) {
		/* シェル変数名の補完 */
		tempinfo = (struct compinfo) {
			.type = CT_EXPORT | CT_VAR | CT_FUNC
		};
		return &tempinfo;
	}
	while (--index >= 0 && xisblank(rl_line_buffer[index]));
	if (index < 0)
		goto command_completion;
	switch (rl_line_buffer[index]) {
		case '|':
			if (index > 0 && rl_line_buffer[index - 1] == '>')
				goto filename_completion;
			/* falls thru! */
		case '&':  case ';':  case '(':
command_completion:
			/* コマンド名の補完 */
			tempinfo = (struct compinfo) {
				.type = CT_KEYWORD | CT_BUILTIN | CT_EXTERN | CT_ALIAS
					| CT_GALIAS | CT_FUNC | CT_RUNNING | CT_STOPPED
			};
			return &tempinfo;
		case '>':  case '<':
			goto filename_completion;
	}

	//XXX コマンド名を見て info を変える */

	/* ファイル名の補完 */
filename_completion:
	tempinfo = (struct compinfo) {
		.type = CT_FILE | CT_DIR | CT_GALIAS
	};
	return &tempinfo;
}

/* 呼ばれる度に補完候補を一つずつ返す補完エントリ関数。
 * text: 補完する文字列
 * noinit: 新しく補完候補の列挙を開始するときは 0、2 回目以降に呼ばれるときは
 *         非 0。 */
static char *yash_completion_entry(const char *text, int noinit)
{
	static enum {
		compfile, compkeyword, compbuiltin, compextcom, compalias,
		compvariable, compfunction, compjob,
	} currenttype;
	static bool inoinit;
	
	/* まず最初に状態を初期化 */
	if (!noinit) {
		currenttype = 0;
		inoinit = false;
	}

	for (;;) {
# if defined(RL_READLINE_VERSION) && RL_READLINE_VERSION >= 0x0402
		rl_compentry_func_t *compfunc;
# else
		char *(*compfunc)(const char *, int);
# endif
		switch (currenttype) {
			case compfile:
				if (currentinfo->type & (CT_FILE | CT_DIR)) {
					compfunc = yash_filename_completion;
					break;
				}
			case compkeyword:
				if (currentinfo->type & CT_KEYWORD) {
					compfunc = yash_keyword_completion;
					break;
				}
			case compbuiltin:
				if (currentinfo->type & CT_BUILTIN) {
					compfunc = yash_builtin_completion;
					break;
				}
			case compextcom:
				if (currentinfo->type & CT_EXTERN) {
					compfunc = yash_external_command_completion;
					break;
				}
			case compalias:
				if (currentinfo->type & (CT_ALIAS | CT_GALIAS)) {
					compfunc = yash_alias_completion;
					break;
				}
			case compvariable:
				if (currentinfo->type & (CT_EXPORT | CT_VAR)) {
					compfunc = yash_variable_completion;
					break;
				}
			case compfunction:
				if (currentinfo->type & CT_FUNC) {
					compfunc = yash_function_completion;
					break;
				}
			case compjob:
				if (currentinfo->type & (CT_RUNNING | CT_STOPPED)) {
					compfunc = yash_job_completion;
					break;
				}
			default:
				return NULL;
		}
		char *result = compfunc(text, inoinit);
		inoinit = true;
		if (result)
			return result;

		currenttype++;
		inoinit = false;
	}
}

/* ファイル名の補完候補を挙げる補完エントリ関数。 */
static char *yash_filename_completion(const char *text, int noinit)
{
	const enum comptypes CT_BOTH = CT_FILE | CT_DIR;
	if (text[0] == '~' && !strchr(text, '/'))
		return rl_username_completion_function(text, noinit);
	for (;;) {
		char *result = rl_filename_completion_function(text, noinit);
		if (!result || ((currentinfo->type & CT_BOTH) == CT_BOTH))
			return result;

		/*ディレクトリのみまたはディレクトリ以外のみを返す場合はここで取捨する*/
		struct stat st;
		if ((stat_tilde(result, &st) < 0)
				|| (!(currentinfo->type & CT_DIR) == !S_ISDIR(st.st_mode)))
			return result;
		free(result);
		noinit = true;
	}
}

/* キーワードの補完候補を挙げる補完エントリ関数。 */
static char *yash_keyword_completion(const char *text, int noinit)
{
	(void) text, (void) noinit;
	//XXX yash_keyword_completion: 未実装
	return NULL;
}

/* 組込みコマンド名の補完候補を挙げる補完エントリ関数。 */
static char *yash_builtin_completion(const char *text, int noinit)
{
	const char *name;
	if (!noinit)
		comp_iter_index = 0;
	while ((name = ht_next(&builtins, &comp_iter_index).key)) {
		if (comp_prefix(text, name))
			return xstrdup(name);
	}
	return NULL;
}

/* 外部コマンド名の補完候補を挙げる補完エントリ関数。 */
static char *yash_external_command_completion(const char *text, int noinit)
{
	static enum { normal, slash, tilde } type;
	const char *name;
	if (!noinit) {
		comp_iter_index = 0;
		if (strchr(text, '/')) {
			type = slash;
		} else if (text[0] == '~') {
			type = tilde;
		} else {
			type = normal;
			fill_cmdhash(text);
		}
	}
	/* text が '/' を含んでいるか '~' で始まるなら
	 * 通常のファイル名補完候補のうち実行可能なもののみを返す。
	 * 入っていなければ PATH から全ての候補を探す。 */
	switch (type) {
		case normal:
			while ((name = ht_next(&cmdhash, &comp_iter_index).key)) {
				if (comp_prefix(text, name))
					return xstrdup(name);
			}
			break;
		case slash:
			for (;;) {
				char *result = rl_filename_completion_function(text, noinit);
				if (!result || access_tilde(result, X_OK) == 0)
					return result;
				free(result);
				noinit = true;
			}
			break;
		case tilde:
			return rl_username_completion_function(text, noinit);
	}
	return NULL;
}

/* エイリアスの補完候補を挙げる補完エントリ関数。 */
static char *yash_alias_completion(const char *text, int noinit)
{
	struct keyvaluepair kv;
	if (!noinit)
		comp_iter_index = 0;
	while ((kv = ht_next(&aliases, &comp_iter_index)).key) {
		if (comp_prefix(text, kv.key)) {
			if ((currentinfo->type & CT_ALIAS) && !((ALIAS *) kv.value)->global)
				return xstrdup(kv.key);
			if ((currentinfo->type & CT_GALIAS) && ((ALIAS *) kv.value)->global)
				return xstrdup(kv.key);
		}
	}
	return NULL;
}

/* シェル変数名の補完候補を挙げる補完エントリ関数。 */
static char *yash_variable_completion(const char *text, int noinit)
{
	static struct hasht *vars = NULL;
	struct keyvaluepair kv;
	if (!noinit) {
		comp_iter_index = 0;
		if (!vars)
			vars = get_variable_table();
	}
	while ((kv = ht_next(vars, &comp_iter_index)).key) {
		if (comp_prefix(text, kv.key)) {
			if ((currentinfo->type & CT_EXPORT)
					&& (((struct variable *) kv.value)->flags & VF_EXPORT))
				return xstrdup(kv.key);
			if ((currentinfo->type & CT_VAR)
					&& !(((struct variable *) kv.value)->flags & VF_EXPORT))
				return xstrdup(kv.key);
		}
	}
	return NULL;
}

/* シェル関数名の補完候補を挙げる補完エントリ関数。 */
static char *yash_function_completion(const char *text, int noinit)
{
	(void) text, (void) noinit;
	return NULL;
	//XXX yash_function_completion: 未実装
}

/* ジョブ名の補完候補を挙げる補完エントリ関数。 */
static char *yash_job_completion(const char *text, int noinit)
{
	(void) text, (void) noinit;
	return NULL;
	//XXX yash_function_completion: 未実装
}

#endif /* USE_READLINE */

/* 指定した文字列が補完候補として妥当かどうか調べる。
 * text: 補完する文字列
 * candidate: 補完候補の候補 */
bool comp_prefix(const char *text, const char *candidate)
{
#ifdef USE_READLINE
	if (!ignorecase)
		return matchprefix(candidate, text);

	while (*text) {
		if (!*candidate)
			return false;
		if (xtoupper(*text) != xtoupper(*candidate))
			return false;
		text++;
		candidate++;
	}
	return true;
#else
	return matchprefix(candidate, text);
#endif
}
