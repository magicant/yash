/* Yash: yet another shell */
/* path.c: filename-related utilities */
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
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
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
#include "wfnmatch.h"
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
    pl_destroy(&list);
    if (result.length == 0)
	/* 結果が空ならカレントディレクトリを表す L"." を返す */
	wb_wccat(&result, L'.');
    return wb_towcs(&result);
}

/* 指定したパスが正規化済みかどうか調べる */
bool is_canonicalized(const wchar_t *path)
{
    wchar_t *canon = canonicalize_path(path);
    bool result = wcscmp(path, canon);
    free(canon);
    return result;
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
	} else {
	    /* dir が空文字列ならカレントディレクトリとして扱う */
	    strcpy(path, name);
	}
	if (cond ? cond(path) : (access(path, F_OK) == 0))
	    return xstrdup(path);
    }
    return NULL;
}

/* ディレクトリを確実に閉じる。
 * closedir が EINTR を返したらやり直す。 */
int xclosedir(DIR *dir)
{
    int result;
    while ((result = closedir(dir)) < 0 && errno == EINTR);
    return result;
}


/********** patharray **********/

/* PATH 環境変数を ':' ごとに区切った各文字列へのポインタの配列へのポインタ。
 * 主に which の第二引数として使う。PATH 環境変数が変わるたびにリセットされる。
 * 各文字列は初期シフト状態で始まり、終わる。 */
static char **patharray = NULL;

/* 新しい PATH 環境変数に合わせて patharray の内容を更新する。
 * newpath: 新しい PATH の値。NULL でもよい。 */
void reset_patharray(const wchar_t *newpath)
{
    recfree((void **) patharray, free);
    if (!newpath)
	newpath = L"";

    wchar_t wpath[wcslen(newpath) + 1];
    wcscpy(wpath, newpath);

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

    /* list 内の重複要素を削除する */
    for (size_t i = 0; i < list.length; i++)
	for (size_t j = list.length; --j > i; )
	    if (wcscmp(list.contents[i], list.contents[j]) == 0)
		pl_remove(&list, j, 1);

    /* list の各要素をマルチバイト文字列に戻す */
    for (size_t i = 0; i < list.length; i++) {
	list.contents[i] = malloc_wcstombs(list.contents[i]);
	if (!list.contents[i])
	    /* まさかここで変換エラーが起きることはないと思うが…… */
	    list.contents[i] = xstrdup("");
    }

    patharray = (char **) pl_toary(&list);
}


/********** コマンド名ハッシュ **********/

/* コマンド名からそのフルパスへのハッシュテーブル。
 * キーはコマンド名を表すマルチバイト文字列へのポインタで、
 * 値はコマンドのフルパスを表すマルチバイト文字列へのポインタである。
 * キーと値を入れる領域はまとめて malloc され、値を free するとキーも一緒に
 * 解放されるようになっている必要がある。 */
static hashtable_T cmdhash;

/* コマンド名ハッシュを初期化する */
void init_cmdhash(void)
{
    static bool initialized = false;
    if (!initialized) {
	initialized = true;
	ht_init(&cmdhash, hashstr, htstrcmp);
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
	if (path && is_executable(path))
	    return path;
    }

    path = which(name, patharray, is_executable);
    if (path && path[0] == '/') {
	size_t namelen = strlen(name), pathlen = strlen(path);
	const char *pathname = path + pathlen - namelen;
	assert(strcmp(name, pathname) == 0);
	vfree(ht_set(&cmdhash, pathname, path));
    } else {
	vfree(ht_remove(&cmdhash, name));
    }
    return path;
}

/* prefix で始まるコマンド名を対象に patharray を検索して、コマンド名ハッシュを
 * 埋める。patharray が NULL なら何もしない。また patharray に相対パスが入って
 * いる場合は無視する。prefix が NULL または空文字列なら、全てのコマンドを
 * 対象とする。ignorecase が true ならコマンド名の大文字小文字を区別
 * しない。
 * この関数はエラーメッセージを出さない。
 * 処理を早くするため、この関数はマルチバイト文字の大文字小文字の区別を正確には
 * 処理しない。*/
void fill_cmdhash(const char *prefix, bool ignorecase)
{
    char *const *pa = patharray;
    if (!pa)
	return;

    if (!prefix)
	prefix = "";
    size_t plen = strlen(prefix);
    char pfx[plen + 1];
    if (ignorecase) {
	/* prefix を小文字にして pfx にコピー */
	for (size_t i = 0; i < plen; i++)
	    pfx[i] = tolower(prefix[i]);
	pfx[plen] = '\0';
    }

    /* patharray の中で前にあるパスを優先するため、配列の後ろの方から検索し、
     * 対象が重複して見付かったら後から見付かったもので上書きする。 */
    for (size_t i = plcount((void **) pa); i-- > 0; ) {
	const char *dirpath = pa[i];
	if (dirpath[0] != '/')
	    continue;  /* 絶対パスでなければ無視 */

	DIR *dir = opendir(dirpath);
	if (!dir)
	    continue;

	size_t dirpathlen = strlen(dirpath);
	struct dirent *de;
	while ((de = readdir(dir))) {
	    /* 名前が prefix に当てはまらなければ飛ばす */
	    if (!ignorecase) {
		if (strncmp(prefix, de->d_name, plen) != 0)
		    goto next;
	    } else {
		for (size_t j = 0; j < plen; j++)
		    if (tolower(de->d_name[j]) != pfx[j])
			goto next;
	    }

	    /* フルパスを生成する */
	    char *path = xmalloc(dirpathlen + strlen(de->d_name) + 2);
	    strncpy(path, dirpath, dirpathlen);
	    path[dirpathlen] = '/';
	    strcpy(path + dirpathlen + 1, de->d_name);

	    /* ファイルが実行可能ならハッシュに追加する */
	    if (is_executable(path))
		vfree(ht_set(&cmdhash, path + dirpathlen + 1, path));
	    else
		free(path);
next:;
	}

	xclosedir(dir);
    }
}


/********** ホームディレクトリキャッシュ **********/

/* getpwnam を確実に行う。getpwnam が EINTR を返したら、やり直す。 */
struct passwd *xgetpwnam(const char *name)
{
    struct passwd *pw;
    do {
	errno = 0;
	pw = getpwnam(name);
    } while (!pw && errno == EINTR);
    return pw;
}

/* ユーザ名からそのユーザのホームディレクトリのフルパスへのハッシュテーブル。
 * キーはユーザのログイン名を表すワイド文字列へのポインタで、
 * 値はホームディレクトリを表すワイド文字列へのポインタである。
 * キーと値を入れる領域はまとめて malloc され、値を free するとキーも一緒に
 * 解放されるようになっている必要がある。 */
static hashtable_T homedirhash;

/* ホームディレクトリハッシュを初期化する */
void init_homedirhash(void)
{
    static bool initialized = false;
    if (!initialized) {
	initialized = true;
	ht_init(&homedirhash, hashwcs, htwcscmp);
    }
}

/* ホームディレクトリハッシュを空にする */
void clear_homedirhash(void)
{
    ht_clear(&homedirhash, vfree);
}

/* 指定したログイン名のユーザのホームディレクトリ (初期作業ディレクトリ) を
 * 探し、フルパスを返す。
 * forcelookup が false でハッシュにパスが登録されていればそれを返す。
 * さもなくば getpwnam を呼んで検索し、結果をハッシュに登録してから返す。
 * 戻り値: 見付かったらそのフルパス。見付からなければ NULL。
 *         戻り値を変更したり free したりしないこと。 */
const wchar_t *get_home_directory(const wchar_t *username, bool forcelookup)
{
    const wchar_t *path;

    if (!forcelookup) {
	path = ht_get(&homedirhash, username).value;
	if (path)
	    return path;
    }

    char *mbsusername = malloc_wcstombs(username);
    if (!mbsusername)
	return NULL;

    struct passwd *pw = xgetpwnam(mbsusername);
    free(mbsusername);
    if (!pw)
	return NULL;

    /* 得られた情報をハッシュに登録して返す */
    xwcsbuf_T dir;
    wb_init(&dir);
    if (wb_mbscat(&dir, pw->pw_dir) != NULL) {
	wb_destroy(&dir);
	return NULL;
    }
    wb_wccat(&dir, L'\0');
    size_t usernameindex = dir.length;
    wb_cat(&dir, username);
    wchar_t *dirname = wb_towcs(&dir);
    vfree(ht_set(&homedirhash, dirname + usernameindex, dirname));
    return dirname;
}


/********** wglob **********/

enum wglbrflags {
    WGLB_followlink = 1 << 0,
    WGLB_period     = 1 << 1,
};

static int wglob_sortcmp(const void *v1, const void *v2)
    __attribute__((pure,nonnull));
static bool wglob_search(const wchar_t *restrict pattern, enum wglbflags flags,
	xstrbuf_T *restrict const dirname,
	plist_T *restrict dirstack, plist_T *restrict list)
    __attribute__((nonnull));
static bool wglob_start_recursive_search(const wchar_t *restrict pattern,
	enum wglbflags flags, enum wglbrflags rflags,
	xstrbuf_T *restrict const dirname,
	plist_T *restrict dirstack, plist_T *restrict list)
    __attribute__((nonnull));
static bool wglob_recursive_search(const wchar_t *restrict pattern,
	enum wglbflags flags, enum wglbrflags rflags,
	xstrbuf_T *restrict const dirname,
	plist_T *restrict dirstack, plist_T *restrict list)
    __attribute__((nonnull));
static bool is_reentry(const struct stat *st, const plist_T *dirstack)
    __attribute__((pure,nonnull));

/* ワイド文字列に対する glob の実装。
 * 指定したパターンに一致するファイルのパスをリストに追加する。
 * pattern: 検索するパターン
 * flags:   マッチングの種類を指定するフラグ。以下の値のビットごとの OR。
 *          WGLB_MARK: ディレクトリについてはパス名の最後に L'/' を付けて返す
 *          WGLB_NOESCAPE: パターンでバックスラッシュエスケープを無効にする
 *          WGLB_CASEFOLD: 大文字小文字を区別しない
 *          WGLB_PERIOD: L'*' や L'?' を先頭のピリオドにもマッチさせる
 *          WGLB_NOSORT: 検索結果をソートしない
 *          WGLB_RECDIR: L"**" パターンでディレクトリを再帰的に検索する
 * list:    検索結果を追加するリスト。見付かったファイルパスが
 *          新しく malloc したマルチバイト文字列へのポインタとしてこれに入る。
 * 戻り値:  エラーがなければ true。
 * パターンが不正な場合はすぐに false を返す。ファイル探索のための
 * パーミッションがない場合などは、できるだけエラーとはみなさない。
 * エラーがあっても list に途中結果が入るかもしれない。 */
bool wglob(const wchar_t *restrict pattern, enum wglbflags flags,
	plist_T *restrict list)
{
    size_t listbase = list->length;
    xstrbuf_T dir;
    plist_T dirstack;

    if (!pattern[0])
	return true;

    sb_init(&dir);
    if (flags & WGLB_RECDIR)
	pl_init(&dirstack);
    while (pattern[0] == L'/') {
	sb_ccat(&dir, '/');
	pattern++;
    }

    bool succ = wglob_search(pattern, flags, &dir, &dirstack, list);
    sb_destroy(&dir);
    if (flags & WGLB_RECDIR)
	pl_destroy(&dirstack);
    if (!succ)
	return false;

    if (!(flags & WGLB_NOSORT)) {
	/* 結果をソートして…… */
	size_t count = list->length - listbase;  /* 実際に追加した個数 */
	if (count > 0) {
	    qsort(list->contents + listbase, count, sizeof (void *),
		    wglob_sortcmp);
	    /* 重複を除く */
	    for (size_t i = list->length; --i > listbase; ) {
		if (strcmp(list->contents[i], list->contents[i-1]) == 0) {
		    free(list->contents[i]);
		    pl_remove(list, i, 1);
		}
	    }
	}
    }
    return true;
}

/* 結果を並べ替えるために qsort が呼ぶ関数。
 * 二つの引数はそれぞれ char * を void * にキャストしたものへのポインタ
 * である。二つのマルチバイト文字列を strcmp で比較した結果を返す。 */
int wglob_sortcmp(const void *v1, const void *v2)
{
    return strcoll(*(const char *const *) v1, *(const char *const *) v2);
}

/* 指定したディレクトリを探索してパターンにマッチするファイルをリストに追加する
 * dirname:  最後に '/' がついた、検索するディレクトリ名。または空文字列。
 * 他の引数や戻り値は wglob に準ずる。
 * pattern の先頭に L'/' があってはならない。
 * ルートディレクトリを検索するには、dirname に "/" を指定する。
 * dirname は、空文字列でない限り最後は '/' で終わっている必要がある。
 * 空文字列はカレントディレクトリを表す。
 * dirname は関数内で書き換えるかもしれないが、関数が返るときには
 * 内容は呼出し時のものに戻っている。 */
bool wglob_search(
	const wchar_t *restrict pattern,
	enum wglbflags flags,
	xstrbuf_T *restrict const dirname,
	plist_T *restrict dirstack,
	plist_T *restrict list)
{
    const size_t savedirlen = dirname->length;
#define RESTORE_DIRNAME \
    ((void) (dirname->contents[dirname->length = savedirlen] = '\0'))

    assert(pattern[0] != L'/');
    if (!pattern[0]) {
	/* パターンが空文字列なら dirname そのものを追加する。
	 * ただし dirname も空文字列の場合を除く。
	 * (dirname が空文字列なら is_directory は false を返す) */
	if (is_directory(dirname->contents))
	    pl_add(list, xstrdup(dirname->contents));
	return true;
    } else if (flags & WGLB_RECDIR) {
	/* 再帰検索パターンかどうかチェックする */
	const wchar_t *p = pattern;
	enum wglbrflags rflags = 0;
	if (p[0] == L'.') {
	    rflags |= WGLB_period;
	    p++;
	}
	if (wcsncmp(p, L"**/", 3) == 0) {
	    p += 3;
	    while (p[0] == L'/') p++;
	    return wglob_start_recursive_search(
		    p, flags, rflags, dirname, dirstack, list);
	} else if (wcsncmp(p, L"***/", 4) == 0) {
	    rflags |= WGLB_followlink;
	    p += 4;
	    while (p[0] == L'/') p++;
	    return wglob_start_recursive_search(
		    p, flags, rflags, dirname, dirstack, list);
	}
    }

    DIR *dir = opendir(dirname->contents[0] ? dirname->contents : ".");
    if (!dir)
	return true;

    size_t patlen = wcscspn(pattern, L"/");
    bool isleaf = (pattern[patlen] == L'\0');
    wchar_t pat[patlen + 1];
    wcsncpy(pat, pattern, patlen);
    pat[patlen] = L'\0';

    enum wfnmflags wfnmflags = wfnmflags;
    size_t sml = sml;  /* GCC の警告を黙らせるために自分自身を代入する */
    /* domatch が true なら wfnmatchl で普通にマッチングする。
     * domatch が false なら単純な文字列比較で済ませる。 */
    bool domatch = pattern_is_literal(pat);
    if (domatch) {
	wfnmflags = WFNM_PATHNAME | WFNM_PERIOD;
	if (flags & WGLB_NOESCAPE) wfnmflags |= WFNM_NOESCAPE;
	if (flags & WGLB_CASEFOLD) wfnmflags |= WFNM_CASEFOLD;
	if (flags & WGLB_PERIOD)   wfnmflags &= ~WFNM_PERIOD;
	sml = shortest_match_length(pat, wfnmflags);
    }

    struct dirent *de;
    bool ok = true;
    while (ok && (de = readdir(dir))) {
	if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
	    continue;

	wchar_t *wentname = malloc_mbstowcs(de->d_name);
	if (!wentname)
	    continue;

	size_t match = domatch
	    ? wfnmatchl(pat, wentname, wfnmflags, WFNM_WHOLE, sml)
	    : ((wcscmp(pat, wentname) == 0) ? 1 : WFNM_NOMATCH);
	free(wentname);
	if (match == WFNM_ERROR) {
	    ok = false;
	} else if (match != WFNM_NOMATCH) {
	    /* マッチした! */
	    if (isleaf) {
		/* マッチしたファイル名をリストに追加 */
		sb_cat(dirname, de->d_name);
		if ((flags & WGLB_MARK) && is_directory(dirname->contents))
		    sb_ccat(dirname, '/');
		pl_add(list, xstrdup(dirname->contents));
		RESTORE_DIRNAME;
	    } else {
		/* サブディレクトリを検索 */
		assert(pattern[patlen] == L'/');
		sb_cat(dirname, de->d_name);
		sb_ccat(dirname, '/');
		const wchar_t *subpat = pattern + patlen + 1;
		while (subpat[0] == L'/') {
		    sb_ccat(dirname, '/');
		    subpat++;
		}
		ok = wglob_search(
			subpat, flags, dirname, dirstack, list);
		RESTORE_DIRNAME;
	    }
	}
    }

    xclosedir(dir);
    return ok;
}

/* 指定したディレクトリに対する再帰的な検索を開始する。
 * rflags: 動作を指定するフラグ。以下の値のビットごとの OR。
 *         WGLB_followlink: シンボリックリンクの先のディレクトリも検索する
 *         WGLB_period: ピリオドで始まる名前のディレクトリも検索する
 * 他の引数や戻り値は wglob_search に準ずる。 */
bool wglob_start_recursive_search(
	const wchar_t *restrict pattern,
	enum wglbflags flags,
	enum wglbrflags rflags,
	xstrbuf_T *restrict const dirname,
	plist_T *restrict dirstack,
	plist_T *restrict list)
{
    char *dir = dirname->contents[0] ? dirname->contents : ".";
    bool followlink = rflags & WGLB_followlink;
    bool ok = true;
    struct stat st;
    if ((followlink ? stat : lstat)(dir, &st) >= 0
	    && S_ISDIR(st.st_mode) && !is_reentry(&st, dirstack)) {
	pl_add(dirstack, &st);
	ok = wglob_recursive_search(
		pattern, flags, rflags, dirname, dirstack, list);
	pl_pop(dirstack);
    }
    return ok;
}

/* 指定したディレクトリに対する再帰的な検索を実際に行う。 */
bool wglob_recursive_search(
	const wchar_t *restrict pattern,
	enum wglbflags flags,
	enum wglbrflags rflags,
	xstrbuf_T *restrict const dirname,
	plist_T *restrict dirstack,
	plist_T *restrict list)
{
    const size_t savedirlen = dirname->length;

    /* Step 1: まず dirname を検索 */
    if (!wglob_search(pattern, flags, dirname, dirstack, list))
	return false;

    assert(dirname->length == savedirlen);

    /* Step 2: 続いて dirname のサブディレクトリを再帰的に検索 */
    DIR *dir = opendir(dirname->contents[0] ? dirname->contents : ".");
    if (!dir)
	return true;

    struct dirent *de;
    bool ok = true;
    while (ok && (de = readdir(dir))) {
	if ((rflags & WGLB_period)
		? strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0
		: de->d_name[0] == '.')
	    continue;

	bool followlink = rflags & WGLB_followlink;
	struct stat st;
	sb_cat(dirname, de->d_name);
	if ((followlink ? stat : lstat)(dirname->contents, &st) >= 0
		&& S_ISDIR(st.st_mode) && !is_reentry(&st, dirstack)) {
	    /* ディレクトリなら再帰する */
	    sb_ccat(dirname, '/');
	    pl_add(dirstack, &st);
	    ok = wglob_recursive_search(pattern, flags, rflags,
		    dirname, dirstack, list);
	    pl_pop(dirstack);
	}
	RESTORE_DIRNAME;
    }

    xclosedir(dir);
    return ok;
}

/* 指定した stat 情報と同じ inode の stat 情報がリスト内にあるかどうか調べる */
bool is_reentry(const struct stat *st, const plist_T *dirstack)
{
    for (size_t i = 0; i < dirstack->length; i++) {
	const struct stat *st2 = dirstack->contents[i];
	if (st->st_dev == st2->st_dev && st->st_ino == st2->st_ino)
	    return true;
    }
    return false;
}


/* vim: set ts=8 sts=4 sw=4 noet: */
