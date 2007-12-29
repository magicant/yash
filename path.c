/* Yash: yet another shell */
/* path.c: path manipulaiton utilities */
/* © 2007 magicant */

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


#include <errno.h>
#include <error.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "yash.h"
#include "util.h"
#include "path.h"
#include <assert.h>


char *which(const char *name, const char *path);
bool isexecutable(const char *path);
char *expand_tilde(const char *path);
char *skip_homedir(const char *path);
char *collapse_homedir(const char *path);


/* path に含まれるディレクトリを走査し、ファイル名 name のフルパスを得る。
 * name が "/" か "./" か "../" で始まるなら、path の走査は行わない。
 * name:   探すファイル名
 * path:   PATH 環境変数のような、":" で区切ったディレクトリ名のリスト。
 *         NULL ならカレントディレクトリから探す。
 * 戻り値: 新しく malloc した、name のフルパス。見付からなかったら NULL。
 *         ただし path に相対パスが含まれていた場合、戻り値も相対パスになる。 */
char *which(const char *name, const char *path)
{
	size_t namelen, pathlen, fullpathlen;
	char *fullpath;

	if (!name || !*name)
		return NULL;
	if (!path)
		path = "";
	if (hasprefix(name, "/") || hasprefix(name, "./") || hasprefix(name, "../"))
		return isexecutable(name) ? xstrdup(name) : NULL;

	namelen = strlen(name);
	fullpathlen = strlen(name) + 30;
	fullpath = xmalloc(fullpathlen);
	while (*path) {
		if (*path == ':' && !*++path)
			break;
		pathlen = strcspn(path, ":");
		while (fullpathlen < pathlen + namelen + 2) {
			fullpathlen *= 2;
			fullpath = xrealloc(fullpath, fullpathlen);
		}
		strncpy(fullpath, path, pathlen);
		if (pathlen)
			fullpath[pathlen] = '/';
		strcpy(fullpath + pathlen + 1, name);
		if (isexecutable(fullpath))
			return fullpath;
		path += pathlen;
		assert(*path == ':' || !*path);
	}
	return NULL;
}

/* path で示されるファイルが実行可能かどうか調べる。 */
bool isexecutable(const char *path)
{
	struct stat st;

	assert(path);
	if (stat(path, &st) < 0) {
		switch (errno) {
			default:
				error(0, errno, "%s", path);
				/* falls thru! */
			case ENOENT:  case ENOTDIR:  case EACCES:  case ELOOP:
				return false;
		}
	}
	return S_ISREG(st.st_mode) && !!(st.st_mode & S_IXUSR);
}

/* '~' で始まるパスを実際のホームディレクトリに展開する。
 * 展開先が自分のホームディレクトリを指しているときは、HOME 環境変数を優先して
 * 使用する。HOME 環境変数がないか、自分以外のホームディレクトリならば、
 * getpwnam 関数を使う。
 * path:   '~' で始まる文字列
 * 戻り値: 成功したら新しく malloc した文字列にパスを展開したもの。
 *         失敗なら NULL。 */
char *expand_tilde(const char *path)
{
	char *home, *result;
	struct passwd *pwd;

	assert(path && path[0] == '~');
	if (path[1] == '\0' || path[1] == '/') {
		path += 1;
		if ((home = getenv(ENV_HOME)))
			goto returnresult;
		errno = 0;
		if (!(pwd = getpwuid(getuid())))
			return NULL;
	} else if (path[1] == '+' && (path[2] == '\0' || path[2] == '/')) {
		path += 2;
		if ((home = getenv(ENV_PWD)))
			goto returnresult;
		return NULL;
	} else if (path[1] == '-' && (path[2] == '\0' || path[2] == '/')) {
		path += 2;
		if ((home = getenv(ENV_OLDPWD)))
			goto returnresult;
		return NULL;
	} else {
		size_t usernamelen = strcspn(path + 1, "/");
		char *username = xstrndup(path + 1, usernamelen);

		path += usernamelen + 1;
		errno = 0;
		pwd = getpwnam(username);
		free(username);
		if (!pwd)
			return NULL;
		home = pwd->pw_dir;
		goto returnresult;
	}

returnresult:
	result = xmalloc(strlen(home) + strlen(path) + 1);
	strcpy(result, home);
	strcat(result, path);
	return result;
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

	if (!path || !(home = getenv(ENV_HOME)))
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
