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
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "util.h"
#include "strbuf.h"
#include "lineinput.h"
#include "parser.h"
#include "variable.h"
#include "sig.h"
#include "expand.h"
#include "job.h"


/********** 入力関数 **********/

static void print_prompt(int type);
static wchar_t *expand_ps1(wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));

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
		if (buf->contents[buf->length++] == '\n') {
		    buf->contents[buf->length] = L'\0';
		    return 0;
		}
		break;
	    case (size_t) -2:  // バイト列が途中で終わっている
	    case (size_t) -1:  // 不正なバイト列である
		goto err;
	}
    }
err:
    xerror(errno, Ngt("cannot convert multibyte character to wide character"));
    return EOF;

    /*
    wb_ensuremax(buf, buf->length + 120);
    count = mbsrtowcs(buf->contents + buf->length, &info->src,
	    buf->maxlength - buf->length + 1, &info->state);
    if (count == (size_t) -1) {
	xerror(errno,
		Ngt("cannot convert multibyte character to wide character"));
	return EOF;
    }
    buf->length += count;
    return 0;
    */
}

/* ワイド文字列をソースとして読み取る入力用関数。
 * この関数は、inputinfo として input_wcs_info 構造体へのポインタを受け取る。
 * そしてそれに入っているワイド文字列を一行ずつバッファ buf に入れる。
 * 文字列の途中まで読み取ったとき、src メンバは次に読み取る最初の文字を示す。
 * 最後まで読み取ると、src は NULL になる。 */
int input_wcs(struct xwcsbuf_T *buf, void *inputinfo)
{
    struct input_wcs_info *info = inputinfo;
    const wchar_t *src = info->src;
    size_t count = 0;

    if (!src)
	return EOF;

    while (src[count] != L'\0' && src[count++] != L'\n');

    if (count == 0) {
	info->src = NULL;
	return EOF;
    } else {
	wb_ncat(buf, src, count);
	info->src = src + count;
	return 0;
    }
}

/* ファイルストリームからワイド文字列をソースとして読み取る入力用関数。
 * この関数は、inputinfo として FILE へのポインタを受け取り、fgetws によって
 * 入力を読み取って buf に追加する。 */
int input_file(struct xwcsbuf_T *buf, void *inputinfo)
{
    FILE *f = inputinfo;
    int fd = fileno(f);
    size_t initlen = buf->length;

    if (!set_nonblocking(fd))
	return EOF;
start:
    wb_ensuremax(buf, buf->length + 100);
    if (fgetws(buf->contents + buf->length, buf->maxlength - buf->length, f)) {
	// XXX fflush を入れるとセグフォる。glibc のバグ?
	//fflush(f);  fseek(f, 0, SEEK_CUR);
	assert(wcslen(buf->contents + buf->length) > 0);
	buf->length += wcslen(buf->contents + buf->length);
	if (buf->contents[buf->length - 1] != L'\n')
	    goto start;
	else
	    goto end;
    } else {
	buf->contents[buf->length] = L'\0';
	if (feof(f)) {
	    clearerr(f);
	    goto end;
	}
	assert(ferror(f));
	switch (errno) {
	    case EINTR:
	    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
	    case EWOULDBLOCK:
#endif
		clearerr(f);
		wait_for_input(fd, true);
		goto start;
	    default:
		//fflush(f);  fseek(f, 0, SEEK_CUR);  XXX
		xerror(errno, Ngt("cannot read input"));
		goto end;
	}
    }
end:
    unset_nonblocking(fd);
    return (initlen == buf->length) ? EOF : 0;
}

/* 端末にプロンプトを出して入力を受け取る入力用関数。
 * inputinfo として struct input_readline_info へのポインタを受け取る。
 * type はプロンプトの種類を指定し、1 なら PS1 を、2 なら PS2 をプロンプトする。
 * 1 <= type <= 4 でなければならない。
 * type が 1 ならこの関数内で 2 に変更する。 */
int input_readline(struct xwcsbuf_T *buf, void *inputinfo)
{
    print_job_status(PJS_ALL, true, false, stderr);
    // TODO prompt command

    struct input_readline_info *info = inputinfo;
    print_prompt(info->type);
    if (info->type == 1)
	info->type = 2;
    return input_file(buf, info->fp);
}

/* 指定したタイプのプロンプトを表示する。
 * type: プロンプトのタイプ。1 以上 3 以下。 */
void print_prompt(int type)
{
    const wchar_t *ps;
    switch (type) {
	case 1:   ps = getvar(VAR_PS1);   break;
	case 2:   ps = getvar(VAR_PS2);   break;
	case 3:   ps = getvar(VAR_PS3);   goto just_print;
	default:  assert(false);
    }

    struct input_wcs_info winfo = {
	.src = ps,
    };
    parseinfo_T info = {
	.print_errmsg = true,
	.filename = gt("prompt"),
	.lineno = 1,
	.input = input_wcs,
	.inputinfo = &winfo,
    };
    wordunit_T *word;
    wchar_t *prompt;

    if (ps == NULL)
	return;
    if (!parse_string_saving(&info, &word))
	goto just_print;
    prompt = expand_string(word, false);
    wordfree(word);
    if (type == 1)
	prompt = expand_ps1(prompt);
    fprintf(stderr, "%ls", prompt);
    fflush(stderr);
    free(prompt);
    return;

just_print:
    if (ps) {
	fprintf(stderr, "%ls", ps);
	fflush(stderr);
    }
}

/* PS1 の内容を展開する。
 * 引数は free 可能な文字列へのポインタであり、関数内で free する。
 * 戻り値: 展開後のプロンプト文字列。呼出し元で free すること。 */
/* posixly_correct が true なら ! を履歴番号に展開する。
 * posixly_correct が false なら各種のバックスラッシュエスケープを展開する。 */
wchar_t *expand_ps1(wchar_t *s)
{
    return s; // TODO expand_ps1
}

/* 指定したファイルディスクリプタの O_NONBLOCK フラグを設定する。
 * fd が負なら何もしない。
 * 戻り値: 成功なら true、エラーなら errno を設定して false。 */
bool set_nonblocking(int fd)
{
    if (fd >= 0) {
	int flags = fcntl(fd, F_GETFL);
	if (flags < 0)
	    return false;
	if (!(flags & O_NONBLOCK))
	    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
    }
    return true;
}

/* 指定したファイルディスクリプタの O_NONBLOCK フラグを解除する。
 * fd が負なら何もしない。
 * 戻り値: 成功なら true、エラーなら errno を設定して false。 */
bool unset_nonblocking(int fd)
{
    if (fd >= 0) {
	int flags = fcntl(fd, F_GETFL);
	if (flags < 0)
	    return false;
	if (flags & O_NONBLOCK)
	    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) != -1;
    }
    return true;
}


/* vim: set ts=8 sts=4 sw=4 noet: */
