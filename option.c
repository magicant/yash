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
#include "option.h"
#include "util.h"


/* true なら POSIX に厳密に従う。--posix オプションに対応 */
bool posixly_correct;

/* ログインシェルかどうか。--login オプションに対応  */
bool is_login_shell;

/* 対話的シェルかどうか。-i オプションに対応。
 * is_interactive_now はサブシェルでは false になる。 */
bool is_interactive, is_interactive_now;

/* ジョブ制御が有効かどうか。-m オプションに対応 */
bool do_job_control;

/* コマンド名。特殊パラメータ $0 の値。 */
const char *command_name;


/* シェルと set コマンドの長いオプション。 */
static const struct xoption long_options[] = {
    { "interactive", xno_argument, NULL, 'i', },
    { "help",        xno_argument, NULL, '?', },
    { "version",     xno_argument, NULL, 'V', },
    { "login",       xno_argument, NULL, 'l', },
    /* ↑ 以上のオプションは set コマンドでは使えない。 */
    { "job-control", xno_argument, NULL, 'm', },
    { "posix",       xno_argument, NULL, 'X', },
    { NULL,          0,            NULL, 0,   },
};

const struct xoption *const shell_long_options = long_options;
const struct xoption *const set_long_options   = long_options + 4;

// TODO option: unimplemented options: -abCefhnuvx -o


/* 一文字のオプションを xoptopt が '-' かどうかによってオン・オフする。
 * シェル起動にしか使えないオプションの文字を指定しても何も起こらない。 */
void set_option(char c)
{
    bool value = (xoptopt == '-');
    switch (c) {
	case 'm':
	    do_job_control = value;
	    break;
	case 'X':
	    posixly_correct = value;
	    break;
    }
}


/* vim: set ts=8 sts=4 sw=4 noet: */
