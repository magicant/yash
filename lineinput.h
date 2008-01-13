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

#include <stddef.h>


/* ソースを一行読み込んで返す関数。
 * ptype:  現在の状況。新しい文を読み始めるときは 1、文の途中で続きを読むときは
 *         2。(この引数はプロンプトの種類を変えたりするのに使う)
 * info:   struct parse_info の inputinfo。
 * 戻り値: 読み込んだソース。末尾の改行はない。EOF に達したときは NULL。 */
typedef char *getline_t(int ptype, void *info);

extern char *history_filename;
extern int history_filesize;
extern int history_histsize;
extern char *readline_prompt1;
extern char *readline_prompt2;

void initialize_readline(void);
void finalize_readline(void);
int yash_getc(FILE *stream);
getline_t yash_fgetline;
getline_t yash_sgetline;
getline_t yash_readline;

/* yash_fgetline の第二引数として与える構造体。 */
struct fgetline_info {
	int fd;            /* 読み取るファイルディスクリプタ */
	char buffer[2000]; /* 読み取った内容を溜めるバッファ */
	size_t bufoff;     /* バッファ内の有効な文字の開始インデックス */
	size_t buflen;     /* バッファ内の有効な文字数 */
};
/* yash_sgetline の第二引数として与える構造体。 */
struct sgetline_info {
	const char *src;   /* 読み取るソースコード */
	size_t offset;     /* 次に読み取る位置。最初は 0。 */
};


/***** Readline 補完 *****/

/* 補完する候補の種類を定義する。 */
enum comptypes {
	CT_FILE    = 1 <<  0,     /* ディレクトリでないファイル */
	CT_DIR     = 1 <<  1,     /* ディレクトリ */
	CT_KEYWORD = 1 <<  2,     /* シェルの予約語 */
	CT_BUILTIN = 1 <<  3,     /* シェル組込みコマンド */
	CT_EXTERN  = 1 <<  4,     /* 外部コマンド */
	CT_ALIAS   = 1 <<  5,     /* 非グローバルエイリアス */
	CT_GALIAS  = 1 <<  6,     /* グローバルエイリアス */
	CT_EXPORT  = 1 <<  7,     /* export 対象の環境変数名 */
	CT_VAR     = 1 <<  8,     /* export しないシェル変数名 */
	CT_FUNC    = 1 <<  9,     /* シェル関数 */
	CT_RUNNING = 1 << 10,     /* 実行中のジョブ名 */
	CT_STOPPED = 1 << 11,     /* 停止中のジョブ名 */
};

/* 補完動作の定義 */
struct compinfo {
	enum comptypes type;
};


#endif /* READLINE_H */
