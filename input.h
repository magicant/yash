/* Yash: yet another shell */
/* input.h: functions for input of command line */
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


#ifndef INPUT_H
#define INPUT_H

#include <stdio.h>
#include <wchar.h>


extern bool set_nonblocking(int fd);
extern bool unset_nonblocking(int fd);

struct xwcsbuf_T;

/* inputfunc_T に適合する入力関数の宣言 */
extern int input_mbs(struct xwcsbuf_T *buf, void *inputinfo)
    __attribute__((nonnull));
extern int input_wcs(struct xwcsbuf_T *buf, void *inputinfo)
    __attribute__((nonnull));
extern int input_file(struct xwcsbuf_T *buf, void *inputinfo)
    __attribute__((nonnull));
extern int input_readline(struct xwcsbuf_T *buf, void *inputinfo)
    __attribute__((nonnull));

/* input_mbs の inputinfo として使う構造体 */
struct input_mbs_info {
    const char *src;   /* 入力として返すソースコード */
    size_t srclen;     /* 末尾の '\0' を含む src のバイト数 */
    mbstate_t state;   /* ワイド文字列への変換の為のシフト状態 */
};

/* input_wcs の inputinfo として使う構造体 */
struct input_wcs_info {
    const wchar_t *src;  /* 入力として返すソースコード */
};

/* input_readline の inputinfo として使う構造体 */
struct input_readline_info {
    FILE *fp;   /* 入力元のストリーム */
    int type;   /* プロンプトの種類を指定する */
};


#endif /* INPUT_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
