/* Yash: yet another shell */
/* readline.h: readline interface */
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


#ifndef READLINE_H
#define READLINE_H


/* ソースを一行読み込んで返す関数。
 * ptype:  現在の状況。新しい文を読み始めるときは 1、文の途中で続きを読むときは
 *         2。(この引数はプロンプトの種類を変えたりするのに使う)
 * 戻り値: 読み込んだソース。末尾の改行はない。EOF に達したときは NULL。 */
typedef char *getline_t(int ptype);

extern char *history_filename;
extern int history_filesize;
extern int history_histsize;
extern char *readline_prompt1;
extern char *readline_prompt2;

extern FILE *yash_fgetline_input;
extern const char *yash_sgetline_src;
extern size_t yash_sgetline_offset;

void initialize_readline(void);
void finalize_readline(void);
getline_t yash_fgetline;
getline_t yash_sgetline;
getline_t yash_readline;


#endif /* READLINE_H */
