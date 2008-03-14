/* Yash: yet another shell */
/* lineinput.h: functions for input, including Readline Library wrapper */
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


#ifndef LINEINPUT_H
#define LINEINPUT_H

#include <wchar.h>
#ifdef USE_READLINE
# ifdef HAVE_LIBREADLINE
#  include <readline/readline.h>
#  include <readline/history.h>
# else
#  include "readline.h"
#  include "history.h"
# endif
#endif


struct xwcsbuf_T;

/* inputfunc_T に適合する入力関数の宣言 */
extern int input_mbs(struct xwcsbuf_T *buf, void *inputinfo);
extern int input_file(struct xwcsbuf_T *buf, void *inputinfo);
extern int input_readline(struct xwcsbuf_T *buf, void *inputinfo);

/* input_mbs の inputinfo として使う構造体 */
struct input_mbs_info {
	const char *src;   /* 入力として返すソースコード */
	mbstate_t state;   /* ワイド文字列への変換の為のシフト状態 */
};


#endif /* LINEINPUT_H */
