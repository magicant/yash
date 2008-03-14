/* Yash: yet another shell */
/* exec.c: command execution */
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
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "parser.h"
#include "exec.h"


/* 最後に実行したコマンドの終了ステータス */
int laststatus;


/* コマンドを実行する。
 * finally_exit: true なら実行後そのまま終了する。 */
void exec_and_or_lists(and_or_T *a, bool finally_exit)
{
	// TODO exec.c: exec_and_or_lists
	wchar_t *s = commands_to_wcstring(a);
	fprintf(stderr, "DEBUG: <%ls>\n", s);
	if (finally_exit)
		exit(laststatus);
}
