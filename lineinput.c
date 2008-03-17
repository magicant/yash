/* Yash: yet another shell */
/* lineinput.c: functions for input, including Readline Library wrapper */
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
#include <stdio.h>
#include <wchar.h>
#ifdef HAVE_GETTEXT
# include <libintl.h>
#endif
#include "util.h"
#include "strbuf.h"
#include "lineinput.h"


/********** 入力関数 **********/

/* マルチバイト文字列をソースとして読み取る入力用関数。
 * この関数は、inputinfo として input_mbs_info 構造体へのポインタを受け取る。
 * そしてそれに入っているマルチバイト文字列をワイド文字列に変換してバッファ buf
 * に入れる。input_mbs_info 構造体の state メンバは予め適当なシフト状態を表して
 * いる必要がある。解析が進むにつれ src, srclen, state メンバは変わる。
 * 文字列の途中まで読み取ったとき、src/state メンバは次に読み取る最初のバイトと
 * そのシフト状態を示す。最後まで読み取ると、src は NULL になり state は初期状態
 * を示す。 */
int input_mbs(struct xwcsbuf_T *buf, void *inputinfo)
{
	struct input_mbs_info *info = inputinfo;
	size_t initbuflen = buf->length;
	size_t count;

	if (!info->src)
		return EOF;

	while (info->srclen > 0) {
		wb_ensuremax(buf, buf->length + 1);
		count = mbrtowc(buf->contents + buf->length,
				info->src, info->srclen, &info->state);
		switch (count) {
			case 0:  // L'\0' を読み取った
				info->src = NULL;
				info->srclen = 0;
				return (buf->length == initbuflen) ? EOF : 0;
			default:  // L'\0' 以外の文字を読み取った
				info->src += count;
				info->srclen -= count;
				if (buf->contents[buf->length++] == '\n')
					return 0;
				break;
			case (size_t) -2:  // バイト列が途中で終わっている
			case (size_t) -1:  // 不正なバイト列である
				goto err;
		}
	}
err:
	xerror(0, errno,
			gt("cannot convert multibyte character to wide character"));
	return EOF;

	/*
	wb_ensuremax(buf, buf->length + 120);
	count = mbsrtowcs(buf->contents + buf->length, &info->src,
			buf->maxlength - buf->length + 1, &info->state);
	if (count == (size_t) -1) {
		xerror(0, errno,
				gt("cannot convert multibyte character to wide character"));
		return EOF;
	}
	buf->length += count;
	return 0;
	*/
}
