/* Yash: yet another shell */
/* parser.c: syntax parser */
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
#include <stdio.h>
#include <wchar.h>
#include "strbuf.h"
#include "parser.h"


/* 少なくとも一行の入力を読み取り、解析する。
 * info: 解析情報へのポインタ。全ての値を初期化しておくこと。
 * result: 成功したら *result に解析結果が入る。
 *         コマンドがなければ *result は NULL になる。
 * 戻り値: 成功したら *result に結果を入れて 0 を返す。
 *         構文エラーならエラーメッセージを出して 1 を返す。
 *         EOF に達したか入力エラーなら EOF を返す。 */
int read_and_parse(parseinfo_t *restrict info, and_or_t **restrict result)
{
	(void) info, (void) result;
	//TODO parser.c: read_and_parse
	return EOF;
}
