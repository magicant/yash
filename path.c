/* Yash: yet another shell */
/* path.c: path manipulaiton utilities */
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


#define  _POSIX_C_SOURCE 200112L
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "yash.h"
#include "util.h"
#include "parser.h"
#include "path.h"
#include "variable.h"
#include <assert.h>


/* path に含まれるディレクトリを走査し、ファイル名 name のフルパスを得る。
 * name が "/" か "./" か "../" で始まるなら、無条件で name のコピーを返す。
 * name:   探すファイル名
 * path:   PATH 環境変数のような、":" で区切ったディレクトリ名のリスト。
 *         NULL ならカレントディレクトリから探す。
 * cond:   ファイル名を受け取り、ファイルが条件に合致しているかどうかを
 *         判断する関数へのポインタ。cond が true を返した場合のみその結果が
 *         返る。cond が NULL ならファイルが存在すれば無条件でその結果が返る。
 *         cond の引数には、存在しないファイルの名前が渡ることがあるので注意。
 * 戻り値: 新しく malloc した、name のフルパス。見付からなかったら NULL。
 *         ただし path に相対パスが含まれていた場合、戻り値も相対パスになる。 */
char *which(const char *name, const char *path, bool (*cond)(const char *name))
{
	if (!name || !*name)
		return NULL;
	if (!path)
		path = "";
#if 0
	if (hasprefix(name, "/") || hasprefix(name, "./") || hasprefix(name, "../")
			|| strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
#else
	if (name[0] == '/' ||
			(name[0] == '.' && (name[1] == '\0' || name[1] == '/' ||
				 (name[1] == '.' && (name[2] == '\0' || name[2] == '/')))))
#endif
	{
		return xstrdup(name);
	}

	size_t namelen = strlen(name);
	for (;;) {
		size_t pathlen = strcspn(path, ":");
		char searchname[pathlen + namelen + 3];
		if (pathlen) {
			strncpy(searchname, path, pathlen);
			searchname[pathlen] = '/';
			searchname[pathlen + 1] = '\0';
		} else {
			searchname[0] = '.';
			searchname[1] = '/';
			searchname[2] = '\0';
		}
		strcat(searchname, name);
		if (cond ? cond(searchname) : (access(searchname, F_OK) == 0))
			return xstrdup(searchname);
		path += pathlen;
		if (!*path)
			break;
		path++;
	}
	return NULL;
}

/* path が実行可能な通常のファイルであるか判定する */
bool is_executable(const char *path)
{
	return access(path, X_OK) == 0;
}

/* path がディレクトリであるか判定する。 */
bool is_directory(const char *path)
{
	struct stat st;
	return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

/* 指定した名前のユーザのホームディレクトリを得る。
 * name が空文字列なら HOME 環境変数を返す。また name が "+", "-" のときは
 * それぞれ PWD, OLDPWD 環境変数を返す。
 * 戻り値: データが見付からなければ NULL。 */
static const char *get_tilde_dir(const char *name)
{
	if (!name[0]) {
		const char *home = getvar(VAR_HOME);
		if (home)
			return home;
		struct passwd *pwd = getpwuid(geteuid());
		if (pwd)
			return pwd->pw_dir;
	} else if (strcmp(name, "+") == 0) {
		const char *pwd = getvar(VAR_PWD);
		if (pwd)
			return pwd;
	} else if (strcmp(name, "-") == 0) {
		const char *oldpwd = getvar(VAR_OLDPWD);
		if (oldpwd)
			return oldpwd;
	} else {
		struct passwd *pwd = getpwnam(name);
		if (pwd)
			return pwd->pw_dir;
	}
	return NULL;
}

/* '~' で始まるパスを実際のホームディレクトリに展開する。
 *   例)  "~user/dir"  ->  "/home/user/dir"
 * path:   '~' で始まる文字列
 * 戻り値: 成功したら新しく malloc した文字列にパスを展開したもの。
 *         失敗 (データが見付からない場合を含む) なら NULL。 */
char *expand_tilde(const char *path)
{
	assert(path && path[0] == '~');
	path++;

	size_t len = strcspn(path, "/");
	char name[len + 1];
	strncpy(name, path, len);
	name[len] = '\0';

	const char *home = get_tilde_dir(name);
	if (!home)
		return NULL;

	char *result = xmalloc(strlen(home) + strlen(path + len) + 1);
	strcpy(result, home);
	strcat(result, path);
	return result;
}

/* ':' で区切った複数のパスのそれぞれに対してチルダ展開を行う。
 * 文字列内の引用符・エスケープは適切に飛ばす。
 * 戻り値: 新しく malloc した文字列に paths を展開したもの。 */
/* この関数は、変数代入の右辺値のチルダ展開で使う。 */
char *expand_tilde_multiple(const char *paths)
{
	struct strbuf buf;
	sb_init(&buf);

	while (*paths) {
		if (*paths == '~') {
			const char *end = skip_with_quote(paths, "/:");
			size_t len = end - (paths + 1);
			char name[len + 1];
			strncpy(name, paths + 1, len);
			name[len] = '\0';

			const char *home = get_tilde_dir(name);
			if (home) {
				sb_append(&buf, home);
				paths = end;
			}
		}
		const char *colon = skip_with_quote(paths, ":");
		if (*colon == ':')
			colon++;
		sb_nappend(&buf, paths, colon - paths);
		paths = colon;
	}
	return sb_tostr(&buf);
}

/* 指定したパスがホームディレクトリで始まるなら、それ以降の部分を返す。
 * 例えばホームディレクトリが /home/user で path が /home/user/dir ならば /dir
 * を返す。ホームディレクトリは HOME 環境変数から得る。
 * パスがホームディレクトリで始まらなければ (又は HOME 環境変数が得られなければ)
 * NULL を返す。戻り値は、(NULL でなければ) 引数 path に含まれる文字のどれかへの
 * ポインタであり、新しく malloc した文字列ではない。 */
char *skip_homedir(const char *path)
{
	const char *home;
	size_t homelen; 

	if (!path || !(home = getvar(VAR_HOME)))
		return NULL;
	homelen = strlen(home);
	if (strncmp(home, path, homelen) == 0)
		return (char *) (path + homelen);
	else
		return NULL;
}

/* 指定したパスがホームディレクトリで始まるなら、その部分を "~" に置き換えた
 * 文字列を返す。ホームディレクトリは HOME 環境変数から得る。
 * パスがホームディレクトリで始まらなければ (又は HOME 環境変数が得られなければ)
 * 元と同じ内容の文字列を返す。
 * 戻り値: path がホームディレクトリで始まっていればそれを "~" に置き換えた
 *         文字列。置き換えたかどうかにかかわらず、戻り値が NULL でなければ
 *         それは新しく malloc した文字列である。エラーがあると NULL を返す。 */
char *collapse_homedir(const char *path)
{
	const char *aftertilde = skip_homedir(path);
	char *result;

	if (!path || !aftertilde)
		return xstrdup(path);
	result = xmalloc(strlen(aftertilde) + 2);
	result[0] = '~';
	strcpy(result + 1, aftertilde);
	return result;
}

/* パスを正規化する。これは単なる文字列の操作であり、実際にパスにファイルが
 * 存在するかどうかなどは関係ない。
 * path:   正規化するパス。
 * 戻り値: 新しく malloc した文字列で、path を正規化した結果。 */
char *canonicalize_path(const char *path)
{
	if (!path)    return NULL;
	if (!path[0]) return xstrdup(path);

	/* path の先頭に三つ以上のスラッシュがある場合、それらを一つのスラッシュに
	 * まとめることができる。この場合、単にスラッシュを無視すればよい。
	 *   例) "///dir/file" -> "/dir/file"  */
	if (path[0] == '/' && path[1] == '/' && path[2] == '/') {
		while (*path == '/') path++;
		path--;
	}

	struct plist list;
	char save[strlen(path) + 1];  /* path を save にコピー */
	strcpy(save, path);
	pl_init(&list);

	/* save 内の '/' を全て '\0' に置き換え、パスの各構成要素ごとの文字列に
	 * 分解し、list に入れる。 */
	pl_append(&list, save);
	for (char *s = save; *s; s++) {
		if (*s == '/') {
			*s = '\0';
			pl_append(&list, s + 1);
		}
	}

	char **entries = (char **) list.contents;
	size_t i;

	/* まず、"" と "." を削除する。ただし……
	 * - リストの最初の構成要素が "" の場合、それはパスが絶対パスであることを
	 *   示している。よってこれは削除してはいけない。これを削除してしまうと、
	 *   例えば /usr/bin が usr/bin になってしまう。また、さらに二つ目の構成
	 *   要素も "" の場合、それも削除してはいけない。この場合は path が "//"
	 *   で始まっている訳だが、これはシステムによっては特殊な意味を持つので
	 *   勝手にスラッシュを一つにしてはいけない。スラッシュが三つ以上ある場合は
	 *   スラッシュを一つにまとめることができるが、これは既に関数の最初で
	 *   処理してある。
	 *   もちろん、path の先頭でない "" は自由に削除できる。
	 * - "." が唯一の構成要素なら、削除してはいけない。 */
	if (entries[0][0] || !entries[1])
		i = 0;  /* 相対パス */
	else if (entries[1][0])
		i = 1;  /* "/xxx" */
	else if (!entries[2])
		i = 2;  /* "/" */
	else if (entries[2][0])
		i = 2;  /* "//xxx" */
	else if (!entries[3])
		i = 3;  /* "//" */
	else 
		assert(false);
	while (entries[i]) {
		if (!entries[i][0] ||
				(strcmp(entries[i], ".") == 0 && (i > 0 || list.length >= 2))) {
			pl_remove(&list, i, 1);

			/* 末尾の要素を消したとき、その親がルートなら '/' の数を合わせる
			 * ために "" を追加する。 */
			if (i == list.length && i > 0 && !entries[i - 1][0]) {
				pl_append(&list, entries[0]);
				i++;
				break;
			}
		} else {
			i++;
		}
	}
	assert(list.length == i);

	/* 続いて、".." とその親要素を削除する。ただし……
	 * - ".." の手前がルートまたは ".." なら削除してはいけない。消してもよい ""
	 *   は既に消してあるので、".." の手前が "" か ".." なら消してはいけない、
	 *   ということになる。
	 * ……というのが POSIX の定めである。しかし "/../" が "/" にならないのは
	 * やはり変なので、非 posixly_correct のときは、
	 * ".." の手前が "" の場合には ".." を削除する。 */
	i = 1;
	while (i < list.length) {
		if (strcmp(entries[i], "..") == 0) {
			if (!entries[i - 1][0]) {  /* 手前が "" の場合 */
				if (!posixly_correct) {
					pl_remove(&list, i, 1);
					goto next;
				}
			} else if (strcmp(entries[i - 1], "..") != 0) {
				i--;
				pl_remove(&list, i, 2);
				if (i == 0)
					i = 1;
				goto next;
			}
		}
		i++;
		continue;
next:
		/* 末尾の要素を消したとき、その親がルートなら '/' の数を合わせる
		 * ために "" を追加する。 */
		if (i == list.length && i > 0 && !entries[i - 1][0]) {
			pl_append(&list, entries[0]);
			break;
		}
	}

	char *result = strjoin(-1, entries, "/");
	pl_destroy(&list);
	return result;
}


/********** コマンド名ハッシュ **********/

/* コマンド名からコマンドのフルパスへのハッシュテーブル */
static struct hasht cmdhash;

/* コマンド名ハッシュを初期化する */
void init_cmdhash(void)
{
	ht_init(&cmdhash);
}

/* コマンド名ハッシュを空にする。 */
void clear_cmdhash(void)
{
	ht_freeclear(&cmdhash, free);
}

/* ハッシュテーブルが空ならば、PATH を走査してハッシュテーブルを埋める。
 * 既にハッシュテーブルに一つでもデータがあれば、何もしない。
 * PATH 環境変数がない場合も何もしない。 */
void fill_cmdhash(void)
{
	if (cmdhash.count) return;

	const char *path = getvar(VAR_PATH);
	if (!path) return;

	/* path の先頭の方にあるものを優先するため、path の後ろから検索し、
	 * 重複して見付かったものは上書きする */

	/* リストに path の各要素の先頭アドレスを入れる。 */
	const char *p = path;
	struct plist list;
	pl_init(&list);
	pl_append(&list, p);
	while (*p) {
		if (*p == ':') pl_append(&list, p + 1);
		p++;
	}

	/* リストを後ろからたどって各パスを調べる */
	size_t lindex = list.length - 1;
	do {
		p = list.contents[lindex];
		if (p[0] == '/') {  /* '/' で始まる絶対パスのみ探す */
			size_t dirlen = (lindex + 1 < list.length)
				? (size_t) ((const char *) list.contents[lindex + 1] - p) - 1
				: strlen(p);
			char dirname[dirlen + 1];
			strncpy(dirname, p, dirlen);
			dirname[dirlen] = '\0';
			DIR *dir = opendir(dirname);
			if (dir) {
				struct dirent *de;
				while ((de = readdir(dir))) {
					size_t namelen = strlen(de->d_name);
					char *fullpath = xmalloc(dirlen + namelen + 2);
					strcpy(fullpath, dirname);
					fullpath[dirlen] = '/';
					strcpy(fullpath + dirlen + 1, de->d_name);
					if (is_executable(fullpath))
						free(ht_set(&cmdhash, de->d_name, fullpath));
					else
						free(fullpath);
				}
				closedir(dir);
			}
		}
	} while (lindex-- > 0);
	pl_destroy(&list);
}

/* 指定したコマンドを PATH から探しだし、フルパスを返す。forcelookup が false
 * でハッシュにパスが登録されていればそれを返す。さもなくば PATH を一つずつ
 * 調べてハッシュに登録し直し、それを返す。
 * 戻り値: 見付かったらそのフルパス。見付からなければ NULL。
 *         フルパスが返ったらそれを変更したりしないこと。 */
const char *get_command_fullpath(const char *name, bool forcelookup)
{
	const char *fullpath;
	if (!forcelookup) {
		fullpath = ht_get(&cmdhash, name);
		if (fullpath)
			return fullpath;
	}

	fullpath = which(name, getvar(VAR_PATH), is_executable);
	if (fullpath) {
		free(ht_set(&cmdhash, name, fullpath));
		return fullpath;
	}

	return NULL;
}
