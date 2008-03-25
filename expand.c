/* Yash: yet another shell */
/* expand.c: word expansion */
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
#include "strbuf.h"
#include "plist.h"
#include "expand.h"


/* コマンドライン上の各種展開を行う。
 * args: void * にキャストした wordunit_T へのポインタの配列。配列内の各ワードが
 *       展開される。配列の最後の要素は NULL でなければならない。
 * argcp: このポインタが指すところに展開結果の個数が入る。
 * argvp: このポインタが指すところに展開結果が入る。結果は、マルチバイト文字列
 *       へのポインタの配列へのポインタであり、配列の最後の要素は NULL である。
 * args で与えられる内容は変更されない。
 * 戻り値: 成功すると true、エラーがあると false。
 * エラーがあった場合、*argcp, *argvp の値は不定である */
bool expand_line(void *const *restrict args,
    int *restrict argcp, char ***restrict argvp)
{
    plist_T list;

    pl_init(&list);

    // TODO expand.c: expand_line: 暫定実装
    struct wordunit_T;
    wchar_t *word_to_wcs(struct wordunit_T *);
    while (*args) {
	pl_add(&list, word_to_wcs(*args));
	args++;
    }
    for (size_t i = 0; i < list.length; i++) {
	list.contents[i] = realloc_wcstombs(list.contents[i]);
    }

    *argcp = list.length;
    *argvp = (char **) pl_toary(&list);
    return true;
}


/* vim: set ts=8 sts=4 sw=4 noet: */
