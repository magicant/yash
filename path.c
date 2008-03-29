/* Yash: yet another shell */
/* path.c: path manipulation utilities and command path hashtable */
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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <sys/stat.h>
#include "option.h"
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "hashtable.h"
#include "path.h"


/* path が読み込み可能な普通のファイルかどうか判定する */
bool is_readable(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) && S_ISREG(st.st_mode)
	&& access(path, R_OK) == 0;
}

/* path が実行可能な普通のファイルかどうか判定する */
bool is_executable(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) && S_ISREG(st.st_mode)
	&& access(path, X_OK) == 0;
}

/* path がディレクトリであるか判定する */
bool is_directory(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

/* 二つのファイルが同じファイルかどうか調べる
 * stat に失敗した場合も false を返す。 */
bool is_same_file(const char *path1, const char *path2)
{
    struct stat stat1, stat2;
    return stat(path1, &stat1) == 0 && stat(path2, &stat2) == 0
	&& stat1.st_dev == stat2.st_dev && stat1.st_ino == stat2.st_ino;
}

/* パスを正規化する。これは単なる文字列の操作であり、実際にパスにファイルが
 * 存在するかどうかなどは関係ない。
 * path:   正規化するパス。
 * 戻り値: 新しく malloc したワイド文字列で、path を正規化した結果。 */
wchar_t *canonicalize_path(const wchar_t *path)
{
    if (!path[0])
	return xwcsdup(path);

    /* 先頭の L'/' の数を数える */
    size_t leadingslashcount = 0;
    while (path[0] == L'/') {
	leadingslashcount++;
	path++;
    }

    plist_T list;
    wchar_t p[wcslen(path) + 1];
    wcscpy(p, path);  /* p に path をコピー */
    pl_init(&list);

    /* p 内の L'/' を全て L'\0' に置き換えながら、各要素を list に追加する。
     * これにより、元々 L'/' で区切ってあった各部分が list に入る。 */
    pl_add(&list, p);
    for (wchar_t *pp = p; *pp; pp++) {
	if (*pp == L'/') {
	    *pp = L'\0';
	    pl_add(&list, pp + 1);
	}
    }

    /* まず、L"" と L"." をリストから削除する */
    for (size_t i = 0; i < list.length; ) {
	if (wcscmp(list.contents[i], L"") == 0
		|| wcscmp(list.contents[i], L".") == 0) {
	    pl_remove(&list, i, 1);
	} else {
	    i++;
	}
    }

    /* 続いて、L".." とその親要素を削除する */
    for (size_t i = 0; i < list.length; ) {
	if (wcscmp(list.contents[i], L"..") == 0) {
	    if (i == 0) {
		/* L".." が先頭にある場合、L".." は基本的に消してはいけない。
		 * しかしこれだと例えば L"/../" が L"/" にならなくて不自然なので
		 * 非 posixly_correct のときはルートの後の L".." は削除する。 */
		if (!posixly_correct && leadingslashcount > 0) {
		    pl_remove(&list, i, 1);
		    continue;
		}
	    } else {
		/* L".." が先頭以外にある場合は普通に L".." とその親を削除。
		 * ただし親が L".." なら削除しない */
		if (wcscmp(list.contents[i - 1], L"..") != 0) {
		    pl_remove(&list, i - 1, 2);
		    i--;
		    continue;
		}
	    }
	}
	i++;
    }

    /* 最後に list 内の各要素を全部つないで完成 */
    xwcsbuf_T result;
    wb_init(&result);
    switch (leadingslashcount) {  /* 先頭の L'/' を加える */
	case 0:
	    break;
	case 2:
	    wb_cat(&result, L"//");
	    break;
	default:  /* 先頭に L'/' が三つ以上あるときは一つに減らしてよい */
	    wb_wccat(&result, L'/');
	    break;
    }
    for (size_t i = 0; i < list.length; i++) {
	wb_cat(&result, list.contents[i]);
	if (i < list.length - 1)
	    wb_wccat(&result, L'/');
    }
    if (result.length == 0)
	/* 結果が空ならカレントディレクトリを表す L"." を返す */
	wb_wccat(&result, L'.');
    return wb_towcs(&result);
}

/* getcwd の結果を新しく malloc した文字列で返す。
 * エラーのときは errno を設定して NULL を返す。 */
char *xgetcwd(void)
{
    size_t pwdlen = 40;
    char *pwd = xmalloc(pwdlen);
    while (getcwd(pwd, pwdlen) == NULL) {
	if (errno == ERANGE) {
	    pwdlen *= 2;
	    pwd = xrealloc(pwd, pwdlen);
	} else {
	    free(pwd);
	    pwd = NULL;
	    break;
	}
    }
    return pwd;
}


/* which が非 NULL を返した直後、この値が true なら which の戻り値は
 * 非カレントディレクトリから得られたことを表す。 */
bool which_found_in_path;

/* 与えられた各ディレクトリの中から指定された名前のエントリを探し、最初に
 * 見付かったもののパスを返す。
 * name:   探すエントリの名前。これが絶対パスなら、検索を行わずにそのまま
 *         name のコピーを返す。
 * dirs:   検索するディレクトリ名の配列。空文字列はカレントディレクトリを示す。
 *         path そのものが NULL の場合はカレントディレクトリのみから探す。
 * cond:   ファイルパスを受け取り、ファイルが条件に合うかどうか調べる関数。
 *         cond が true を返すと実際にそのパスが返される。
 *         cond が true を返すと検索を続ける。
 * 戻り値: エントリが見付かって、cond が true を返したら、そのエントリへのパス
 *         (新しく malloc した文字列)。見付からなかったら、NULL。
 * name および paths 内の各文字列はすべて初期シフト状態で始まり、終わらなければ
 * ならない。which_found_in_path の値は、which が非 NULL を返した直後のみ
 * 有効である。
 * 返す文字列は、dirs のどれかの要素と name を '/' で繋いだものになる。 */
char *which(
	const char *restrict name,
	char *const *restrict dirs,
	bool cond(const char *path))
{
    which_found_in_path = false;
    if (!name[0])
	return NULL;
    if (!dirs)
	dirs = (char *[]) { "", NULL, };
    if (name[0] == '/')
	return xstrdup(name);

    size_t namelen = strlen(name);
    const char *dir;
    while ((dir = *dirs++)) {
	size_t dirlen = strlen(dir);
	char path[dirlen + namelen + 3];
	if (dirlen > 0) {
	    /* dir と name を繋げてエントリ名候補 path を作る */
	    strcpy(path, dir);
	    path[dirlen] = '/';
	    strcpy(path + dirlen + 1, name);
	    which_found_in_path = true;
	} else {
	    /* dir が空文字列ならカレントディレクトリとして扱う */
	    strcpy(path, name);
	    which_found_in_path = false;
	}
	if (cond ? cond(path) : (access(path, F_OK) == 0))
	    return xstrdup(path);
    }
    return NULL;
}


/********** patharray **********/

/* PATH 環境変数を ':' ごとに区切った各文字列へのポインタの配列へのポインタ。
 * 主に which の第二引数として使う。PATH 環境変数が変わるたびにリセットされる */
static char **patharray = NULL;

/* 新しい PATH 環境変数に合わせて patharray の内容を更新する
 * newpath: 新しい PATH の値。NULL でもよい。 */
void reset_path(const char *newpath)
{
    recfree((void **) patharray, free);

    if (!newpath)
	newpath = "";

    wchar_t *wpath = malloc_mbstowcs(newpath);
    if (!wpath) {
	xerror(0, 0, Ngt("PATH variable contains characters that cannot be "
		    "converted to wide characters: command path search will "
		    "be abandoned"));
	return;
    }

    plist_T list;
    pl_init(&list);

    /* wpath の中の L':' を L'\0' に置き換えつつ各要素を list に追加する */
    pl_add(&list, wpath);
    for (wchar_t *w = wpath; *w; w++) {
	if (*w == L':') {
	    *w = L'\0';
	    pl_add(&list, w + 1);
	}
    }

    /* list の各要素をマルチバイト文字列に戻す */
    for (size_t i = 0; i < list.length; i++) {
	list.contents[i] = malloc_wcstombs(list.contents[i]);
	if (!list.contents[i])
	    /* まさかここで変換エラーが起きることはないと思うが…… */
	    list.contents[i] = xstrdup("");
    }

    free(wpath);
    patharray = (char **) pl_toary(&list);
}


/********** コマンド名ハッシュ **********/

/* コマンド名からそのフルパスへのハッシュテーブル。
 * キーはコマンド名を表すマルチバイト文字列へのポインタで、
 * 値はコマンドのフルパスを表すマルチバイト文字列へのポインタである。
 * 各値は、malloc で確保した領域を指している。各キーは、その値であるフルパスの
 * 最後のスラッシュの直後の文字を指している。 */
static hashtable_T cmdhash;

/* コマンド名ハッシュを初期化する */
void init_cmdhash(void)
{
    static bool initialized = false;
    if (!initialized) {
	initialized = true;
	ht_init(&cmdhash, hashstr, htstrcmp);

	//TODO path.c: init_cmdhash: variable.c に移動すべし
	reset_path(getenv("PATH"));
    }
}

/* コマンド名ハッシュを空にする */
void clear_cmdhash(void)
{
    ht_clear(&cmdhash, vfree);
}

/* 指定したコマンドを PATH (patharray) から探しだし、フルパスを返す。
 * forcelookup が false でハッシュにパスが登録されていればそれを返す。
 * さもなくば which を呼んで検索し、結果をハッシュに登録してから返す。
 * 戻り値: 見付かったらそのフルパス。見付からなければ NULL。
 *         戻り値を変更したり free したりしないこと。 */
const char *get_command_path(const char *name, bool forcelookup)
{
    const char *path;

    if (!forcelookup) {
	path = ht_get(&cmdhash, name).value;
	if (path)
	    return path;
    }

    path = which(name, patharray, is_executable);
    if (path) {
	size_t namelen = strlen(name), pathlen = strlen(path);
	const char *pathname = path + pathlen - namelen;
	assert(strcmp(name, pathname) == 0);
	vfree(ht_set(&cmdhash, pathname, path));
    }
    return path;
}


/* vim: set ts=8 sts=4 sw=4 noet: */
