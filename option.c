/* Yash: yet another shell */
/* option.c: option settings */
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
#include <stdbool.h>
#include <string.h>
#include "option.h"
#include "util.h"
#include "strbuf.h"


/* true なら POSIX に厳密に従う。--posix オプションに対応 */
bool posixly_correct;

/* ログインシェルかどうか。--login オプションに対応  */
bool is_login_shell;

/* 対話的シェルかどうか。-i オプションに対応。
 * is_interactive_now はサブシェルでは false になる。 */
bool is_interactive, is_interactive_now;

/* ジョブ制御が有効かどうか。-m オプションに対応 */
bool do_job_control;
/* ジョブの状態変化をいつでもすぐに通知するかどうか。-b オプションに対応 */
bool shopt_notify;

/* コマンドをどこから読んでいるか。それぞれ -c, -s オプションに対応 */
bool shopt_read_arg, shopt_read_stdin;

/* コマンド名。特殊パラメータ $0 の値。 */
const char *command_name;

/* コマンドの実行を行わないかどうか。-n オプションに対応 */
bool shopt_noexec;
/* プロンプトで EOF を無視するかどうか。-o ignoreeof オプションに対応 */
bool shopt_ignoreeof;

/* パス名展開を行わないかどうか。-f オプションに対応。 */
bool shopt_noglob;
/* それぞれ WGLB_CASEFOLD, WGLB_PERIOD, WGLB_MARK, WGLB_RECDIR に対応 */
bool shopt_nocaseglob, shopt_dotglob, shopt_markdirs, shopt_extendedglob;
/* パス名展開に一致するファイル名が一つもないとき、元のパターンを返さない。 */
bool shopt_nullglob;
/* ブレース展開をするかどうか */
bool shopt_braceexpand;
/* リダイレクトでファイルの上書きを防止するかどうか */
bool shopt_noclobber;


/* シェルと set コマンドの長いオプション。 */
static const struct xoption long_options[] = {
    { "interactive",  xno_argument, NULL, 'i', },
    { "help",         xno_argument, NULL, '!', },
    { "version",      xno_argument, NULL, 'V', },
    { "login",        xno_argument, NULL, 'l', },
    /* ↑ 以上のオプションは set コマンドでは使えない。 */
    { "noclobber",    xno_argument, NULL, 'C', },
    { "noglob",       xno_argument, NULL, 'f', },
    { "nocaseglob",   xno_argument, NULL, 'c', },
    { "dotglob",      xno_argument, NULL, 'D', },
    { "markdirs",     xno_argument, NULL, 'M', },
    { "extendedglob", xno_argument, NULL, 'E', },
    { "nullglob",     xno_argument, NULL, 'N', },
    { "braceexpand",  xno_argument, NULL, 'B', },
    { "noexec",       xno_argument, NULL, 'n', },
    { "ignoreeof",    xno_argument, NULL, 'I', },
    { "monitor",      xno_argument, NULL, 'm', },
    { "notify",       xno_argument, NULL, 'b', },
    { "posix",        xno_argument, NULL, 'X', },
    { NULL,           0,            NULL, 0,   },
};

const struct xoption *const shell_long_options = long_options;
const struct xoption *const set_long_options   = long_options + 4;

// TODO option: unimplemented options: -aehuvx -o{nolog,vi,emacs}


/* 一文字のオプションを xoptopt が '-' かどうかによってオン・オフする。
 * シェル起動にしか使えないオプションの文字を指定しても何も起こらない。 */
void set_option(char c)
{
    bool value = (xoptopt == '-');
    switch (c) {
	case 'C':
	    shopt_noclobber = value;
	    break;
	case 'f':
	    shopt_noglob = value;
	    break;
	case 'c':
	    shopt_nocaseglob = value;
	    break;
	case 'D':
	    shopt_dotglob = value;
	    break;
	case 'M':
	    shopt_markdirs = value;
	    break;
	case 'E':
	    shopt_extendedglob = value;
	    break;
	case 'N':
	    shopt_nullglob = value;
	    break;
	case 'B':
	    shopt_braceexpand = value;
	    break;
	case 'n':
	    shopt_noexec = value;
	    break;
	case 'I':
	    shopt_ignoreeof = value;
	    break;
	case 'm':
	    do_job_control = value;
	    break;
	case 'b':
	    shopt_notify = value;
	    break;
	case 'X':
	    posixly_correct = value;
	    break;
    }
}

/* "noglob" や "noexec" などの長いオプション名を受け取り、現在の xoptopt に
 * 従って、そのオプションをオン・オフする。
 * シェルの起動時しか使えないオプション名は指定できない。
 * 戻り値: エラーが無ければ true、指定したオプション名が無効なら false。 */
bool set_long_option(const char *s)
{
    const struct xoption *opt = set_long_options;

    while (opt->name) {
	if (strcmp(s, opt->name) == 0) {
	    set_option(opt->val);
	    return true;
	}
	opt++;
    }
    return false;
}

/* 現在の $- パラメータの値を取得する。
 * 戻り値: 新しく malloc したワイド文字列。 */
wchar_t *get_hyphen_parameter(void)
{
    xwcsbuf_T buf;
    wb_init(&buf);

    if (shopt_notify)      wb_wccat(&buf, L'b');
    if (shopt_read_arg)    wb_wccat(&buf, L'c');
    if (shopt_noglob)      wb_wccat(&buf, L'f');
    if (is_interactive)    wb_wccat(&buf, L'i');
    if (do_job_control)    wb_wccat(&buf, L'm');
    if (shopt_noexec)      wb_wccat(&buf, L'n');
    if (shopt_read_stdin)  wb_wccat(&buf, L's');
    if (shopt_noclobber)   wb_wccat(&buf, L'C');

    return wb_towcs(&buf);
}


/* vim: set ts=8 sts=4 sw=4 noet: */
