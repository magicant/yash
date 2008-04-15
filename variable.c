/* Yash: yet another shell */
/* variable.c: deals with shell variables and parameters */
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
#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include "option.h"
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "hashtable.h"
#include "path.h"
#include "variable.h"
#include "exec.h"
#include "yash.h"
#include "version.h"


/* 変数環境 (=現在有効な変数・パラメータの集まり) を表す構造体。 */
/* 言葉が似ているが環境変数とは全く異なる。 */
typedef struct environ_T {
    struct environ_T *parent;      /* 親環境 */
    struct hashtable_T contents;   /* 変数を保持するハッシュテーブル */
} environ_T;
/* contents は char * から variable_T * へのハッシュテーブルである。 */
/* 位置パラメータは、contents に "=" という名前の配列として入れる。
 * 位置パラメータの番号と配列のインデックスが一つずれるので注意。 */
#define VAR_positional "="


static void varfree(variable_T *v);
static void varkvfree(kvpair_T kv);
static void init_pwd(void);
static inline bool is_name_char(char c)
    __attribute__((const));
static variable_T *search_variable(const char *name, bool temp)
    __attribute__((pure,nonnull));
static void update_enrivon(const char *name)
    __attribute__((nonnull));
static void reset_locale(const char *name)
    __attribute__((nonnull));
static void reset_locale_category(const char *name, int category)
    __attribute__((nonnull));
static variable_T *new_global(const char *name)
    __attribute__((nonnull));
static variable_T *new_local(const char *name)
    __attribute__((nonnull));
static void lineno_getter(variable_T *var)
    __attribute__((nonnull));
static void random_getter(variable_T *var)
    __attribute__((nonnull));
static unsigned next_random(void);
static void variable_set(const char *name, variable_T *var)
    __attribute__((nonnull));


/* 現在の変数環境 */
static environ_T *current_env;
/* 最も外側の変数環境 */
static environ_T *top_env;

/* 一時的変数を保持するハッシュテーブル。
 * 一時的変数は、特殊組込みコマンドでない組込みコマンドを実行する際に
 * 一時的な代入を行うためのものである。例えば、"HOME=/ cd" というコマンドライン
 * を実行する際に HOME 変数を一時的に代入し、cd の実行が終わったら削除する。 */
static hashtable_T temp_variables;

/* RANDOM 変数が乱数として機能しているかどうか */
static bool random_active;


/* variable_T * を free する */
void varfree(variable_T *v)
{
    if (v) {
	switch (v->v_type & VF_MASK) {
	    case VF_NORMAL:
		free(v->v_value);
		break;
	    case VF_ARRAY:
		recfree(v->v_vals, free);
		break;
	}
	free(v);
    }
}

/* 変数を保持するハッシュテーブルの内容だった kvpair_T を解放する。 */
void varkvfree(kvpair_T kv)
{
    free(kv.key);
    varfree(kv.value);
}

static bool initialized = false;

/* 変数関連のデータを初期化する */
void init_variables(void)
{
    if (initialized)
	return;
    initialized = true;

    top_env = current_env = xmalloc(sizeof *current_env);
    current_env->parent = NULL;
    ht_init(&current_env->contents, hashstr, htstrcmp);

    ht_init(&temp_variables, hashstr, htstrcmp);

    /* まず current_env->contents に全ての既存の環境変数を設定する。 */
    for (char **e = environ; *e; e++) {
	size_t namelen = strcspn(*e, "=");
	if ((*e)[namelen] != '=')
	    continue;

	char *name = xstrndup(*e, namelen);
	variable_T *v = xmalloc(sizeof *v);
	v->v_type = VF_NORMAL | VF_EXPORT;
	v->v_value = malloc_mbstowcs(*e + namelen + 1);
	v->v_getter = NULL;
	if (v->v_value) {
	    varkvfree(ht_set(&current_env->contents, name, v));
	} else {
	    xerror(0, Ngt("value of environment variable `%s' cannot be "
		    "converted to wide-character string"),
		    name);
	    free(name);
	    free(v);
	}
    }

    /* LINENO 特殊変数を設定する */
    {
	variable_T *v = new_global(VAR_LINENO);
	assert(v != NULL);
	v->v_type = VF_NORMAL | (v->v_type & VF_EXPORT);
	v->v_value = xwcsdup(L"");
	v->v_getter = lineno_getter;
	if (v->v_type & VF_EXPORT)
	    update_enrivon(VAR_LINENO);
    }

    /* PWD を設定する */
    init_pwd();

    /* PPID を設定する */
    set_variable(VAR_PPID, malloc_wprintf(L"%jd", (intmax_t) getppid()),
	    false, false);

    /* RANDOM 特殊変数を設定する */
    if (!posixly_correct && !getvar(VAR_RANDOM)) {
	variable_T *v = new_global(VAR_RANDOM);
	assert(v != NULL);
	v->v_type = VF_NORMAL;
	v->v_value = xwcsdup(L"");
	v->v_getter = random_getter;
	random_active = true;
	srand(shell_pid);
    } else {
	random_active = false;
    }

    /* YASH_VERSION を設定する */
    set_variable(VAR_YASH_VERSION, xwcsdup(L"" PACKAGE_VERSION), false, false);

    /* PATH に基づいて patharray を初期化する */
    reset_patharray(getvar(VAR_PATH));
}

/* 以下の場合に PWD 環境変数を設定する
 *  - posixly_correct が true である
 *  - PWD 変数が存在しないか '/' で始まらないか実際の作業ディレクトリとは異なる
 *  - PWD 変数の値が正規化されていない */
void init_pwd(void)
{
    if (posixly_correct)
	goto set;
    const char *pwd = getenv(VAR_PWD);
    if (!pwd || pwd[0] != '/' || !is_same_file(pwd, "."))
	goto set;
    const wchar_t *wpwd = getvar(VAR_PWD);
    if (!wpwd)
	goto set;
    wchar_t *cpwd = canonicalize_path(wpwd);
    bool iscanon = (wcscmp(wpwd, cpwd) == 0);
    free(cpwd);
    if (!iscanon)
	goto set;
    return;

    char *newpwd;
    wchar_t *wnewpwd;
set:
    newpwd = xgetcwd();
    if (!newpwd) {
	xerror(errno, Ngt("cannot set PWD"));
	return;
    }
    wnewpwd = malloc_mbstowcs(newpwd);
    free(newpwd);
    if (!wnewpwd) {
	xerror(0, Ngt("cannot set PWD"));
	return;
    }
    set_variable(VAR_PWD, wnewpwd, false, true);
}

/* 変数関連のデータを初期化前の状態に戻す */
void finalize_variables(void)
{
    environ_T *env = current_env;
    while (env) {
	ht_clear(&env->contents, varkvfree);

	environ_T *parent = env->parent;
	free(env);
	env = parent;
    }
    current_env = NULL;
    ht_clear(&temp_variables, varkvfree);

    initialized = false;
}

/* 引数が名前構成文字であるかどうか調べる */
bool is_name_char(char c)
{
    switch (c) {
    case '0':  case '1':  case '2':  case '3':  case '4':
    case '5':  case '6':  case '7':  case '8':  case '9':
    case 'a':  case 'b':  case 'c':  case 'd':  case 'e':  case 'f':
    case 'g':  case 'h':  case 'i':  case 'j':  case 'k':  case 'l':
    case 'm':  case 'n':  case 'o':  case 'p':  case 'q':  case 'r':
    case 's':  case 't':  case 'u':  case 'v':  case 'w':  case 'x':
    case 'y':  case 'z':
    case 'A':  case 'B':  case 'C':  case 'D':  case 'E':  case 'F':
    case 'G':  case 'H':  case 'I':  case 'J':  case 'K':  case 'L':
    case 'M':  case 'N':  case 'O':  case 'P':  case 'Q':  case 'R':
    case 'S':  case 'T':  case 'U':  case 'V':  case 'W':  case 'X':
    case 'Y':  case 'Z':  case '_':
	return true;
    default:
	return false;
    }
}

/* 指定した文字列が (位置パラメータや特殊パラメータではない) 普通の変数の名前
 * であるかどうか調べる */
bool is_name(const char *s)
{
    if (!xisdigit(*s))
	while (is_name_char(*s))
	    s++;
    return !*s;
}

/* 指定した名前の変数を捜し出す。
 * temp:   temp_variables からも探すかどうか。
 * 戻り値: 見付かった変数。見付からなければ NULL。 */
variable_T *search_variable(const char *name, bool temp)
{
    variable_T *var;
    if (temp) {
	var = ht_get(&temp_variables, name).value;
	if (var)
	    return var;
    }

    environ_T *env = current_env;
    while (env) {
	var = ht_get(&env->contents, name).value;
	if (var)
	    return var;
	env = env->parent;
    }
    return NULL;
}

/* 指定した名前の変数について、environ の値を更新する。
 * 一時的変数は無視する。 */
void update_enrivon(const char *name)
{
    environ_T *env = current_env;
    while (env) {
	variable_T *var = ht_get(&env->contents, name).value;
	if (var && (var->v_type & VF_EXPORT)) {
	    char *value = malloc_wcstombs(var->v_value);
	    if (value) {
		if (setenv(name, value, true) != 0)
		    xerror(errno, Ngt("cannot set environment variable `%s'"),
			    name);
		free(value);
	    } else {
		xerror(0, Ngt("environment variable `%s' contains characters "
			    "that cannot be converted from wide characters"),
			name);
	    }
	    return;
	}
	env = env->parent;
    }
    unsetenv(name);
}

/* 指定した名前のロケール設定を現在の変数の値に基づいて再設定する。
 * name がへんてこな文字列なら何もしない。 */
void reset_locale(const char *name)
{
    if (strcmp(name, VAR_LANG) == 0) {
	goto reset_locale_all;
    } else if (strncmp(name, "LC_", 3) == 0) {
	/* POSIX の定めによると、実行中に環境変数が変わっても
	 * LC_CTYPE はリセットしてはいけない。 */
	if (strcmp(name, VAR_LC_ALL) == 0) {
reset_locale_all:
	    reset_locale_category(VAR_LC_COLLATE,  LC_COLLATE);
//	    reset_locale_category(VAR_LC_CTYPE,    LC_CTYPE);
	    reset_locale_category(VAR_LC_MESSAGES, LC_MESSAGES);
	    reset_locale_category(VAR_LC_MONETARY, LC_MONETARY);
	    reset_locale_category(VAR_LC_NUMERIC,  LC_NUMERIC);
	    reset_locale_category(VAR_LC_TIME,     LC_TIME);
	} else if (strcmp(name, VAR_LC_COLLATE) == 0) {
	    reset_locale_category(VAR_LC_COLLATE,  LC_COLLATE);
//	} else if (strcmp(name, VAR_LC_CTYPE) == 0) {
//	    reset_locale_category(VAR_LC_CTYPE,    LC_CTYPE);
	} else if (strcmp(name, VAR_LC_MESSAGES) == 0) {
	    reset_locale_category(VAR_LC_MESSAGES, LC_MESSAGES);
	} else if (strcmp(name, VAR_LC_MONETARY) == 0) {
	    reset_locale_category(VAR_LC_MONETARY, LC_MONETARY);
	} else if (strcmp(name, VAR_LC_NUMERIC) == 0) {
	    reset_locale_category(VAR_LC_NUMERIC,  LC_NUMERIC);
	} else if (strcmp(name, VAR_LC_TIME) == 0) {
	    reset_locale_category(VAR_LC_TIME,     LC_TIME);
	}
    }
}

/* 指定したロケールのカテゴリを再設定する
 * name: LC_ALL 以外の LC_ で始まるカテゴリ名 */
void reset_locale_category(const char *name, int category)
{
    const wchar_t *v = getvar(VAR_LC_ALL);
    if (!v) {
	v = getvar(name);
	if (!v) {
	    v = getvar(VAR_LANG);
	    if (!v)
		v = L"";
	}
    }
    char *sv = malloc_wcstombs(v);
    if (sv) {
	setlocale(category, sv);
	free(sv);
    }
}

/* 新しいグローバル変数を用意する。
 * グローバルといっても、常に top_env に作成するわけではなく、既にローカル変数が
 * 存在していればそれを返すことになる。
 * 古い変数があれば削除し、古い変数と同じ環境に新しい変数を用意してそれを返す。
 * 古い変数がなければ、top_env に新しい変数を用意して返す。
 * いずれにしても、呼び出し元で戻り値の内容を正しく設定すること。
 * 戻り値の v_type 以外のメンバの値は信用してはいけない。
 * 戻り値の v_type に VF_EXPORT がついている場合、呼出し元で update_enrivon
 * を呼ぶこと。
 * 失敗すればエラーを出して (読み取り専用変数が既に存在するなど) NULL を返す。
 * 同じ名前の一時的変数はあっても無視する。 */
variable_T *new_global(const char *name)
{
    variable_T *var = search_variable(name, false);
    if (!var) {
	var = xmalloc(sizeof *var);
	var->v_type = 0;
	ht_set(&top_env->contents, xstrdup(name), var);
    } else if (var->v_type & VF_READONLY) {
	xerror(0, Ngt("%s: readonly"), name);
	return NULL;
    } else {
	switch (var->v_type & VF_MASK) {
	    case VF_NORMAL:
		free(var->v_value);
		break;
	    case VF_ARRAY:
		recfree(var->v_vals, free);
		break;
	}
    }
    return var;
}

/* 新しいローカル変数を用意する。
 * 古い変数があれば削除し、指定した名前の新しい変数を current_env->contents に
 * 設定して、それを返す。呼び出し元で戻り値の内容を正しく設定すること。
 * 戻り値の v_type 以外のメンバの値は信用してはいけない。
 * 戻り値の v_type に VF_EXPORT がついている場合、呼出し元で update_enrivon
 * を呼ぶこと。
 * 失敗すればエラーを出して (読み取り専用変数が既に存在するなど) NULL を返す。
 * 同じ名前の一時的変数はあっても無視する。 */
variable_T *new_local(const char *name)
{
    variable_T *var = ht_get(&current_env->contents, name).value;
    if (!var) {
	var = xmalloc(sizeof *var);
	var->v_type = 0;
	ht_set(&current_env->contents, xstrdup(name), var);
    } else if (var->v_type & VF_READONLY) {
	xerror(0, Ngt("%s: readonly"), name);
	return NULL;
    } else {
	switch (var->v_type & VF_MASK) {
	    case VF_NORMAL:
		free(var->v_value);
		break;
	    case VF_ARRAY:
		recfree(var->v_vals, free);
		break;
	}
    }
    return var;
}

/* 指定した名前と値で変数を作成する。
 * value: 予め wcsdup しておいた free 可能な文字列。
 *        この関数が返った後、呼出し元はこの文字列を一切使用してはならない。
 * export: VF_EXPORT フラグを追加するかどうか。
 * 戻り値: 成功すれば true、失敗すればエラーを出して false。 */
bool set_variable(const char *name, wchar_t *value, bool local, bool export)
{
    variable_T *var = local ? new_local(name) : new_global(name);
    if (!var)
	return false;

    var->v_type = VF_NORMAL
	| (var->v_type & (VF_EXPORT | VF_NODELETE))
	| (export ? VF_EXPORT : 0);
    var->v_value = value;
    var->v_getter = NULL;

    variable_set(name, var);
    if (var->v_type & VF_EXPORT)
	update_enrivon(name);
    return true;
}

/* 指定した名前と値の配列を作成する。
 * 戻り値: 成功すれば true、失敗すればエラーを出して false。 */
bool set_array(const char *name, char *const *values, bool local)
{
    variable_T *var = local ? new_local(name) : new_global(name);
    if (!var)
	return false;

    bool needupdate = (var->v_type & VF_EXPORT);

    plist_T list;
    pl_init(&list);
    while (*values) {
	wchar_t *wv = malloc_mbstowcs(*values);
	if (!wv) {
	    if (strcmp(name, VAR_positional) == 0)
		xerror(0,
			Ngt("new positional parameter $%zu contains characters "
			    "that cannot be converted to wide characters and "
			    "is replaced with null string"),
			list.length + 1);
	    else
		xerror(0, Ngt("new array element %s[%zu] contains characters "
			    "that cannot be converted to wide characters and "
			    "is replaced with null string"),
			name, list.length + 1);
	    wv = xwcsdup(L"");
	}
	pl_add(&list, wv);
	values++;
    }
    var->v_type = VF_ARRAY | (var->v_type & VF_NODELETE);
    var->v_valc = list.length;
    var->v_vals = pl_toary(&list);
    var->v_getter = NULL;

    variable_set(name, var);
    if (needupdate)
	update_enrivon(name);
    return true;
}

/* 現在の環境の位置パラメータを設定する。既存の位置パラメータは削除する。
 * values[0] が $1、values[1] が $2、というようになる。 */
void set_positional_parameters(char *const *values)
{
    set_array(VAR_positional, values, true);
}

/* 指定した名前のシェル変数を取得する。
 * 普通の変数 (VF_NORMAL) にのみ使える。
 * "$" や "@" などの特殊パラメータは得られない。
 * 変数がなければ NULL を返す。
 * 戻り値を変更したり free したりしてはならない。
 * 戻り値は変数を変更・削除するまで有効である。 */
const wchar_t *getvar(const char *name)
{
    variable_T *var = search_variable(name, true);
    if (var && (var->v_type & VF_MASK) == VF_NORMAL) {
	if (var->v_getter)
	    var->v_getter(var);
	return var->v_value;
    }
    return NULL;
}

/* シェル変数を配列として取得する。
 * 変数がなければ NULL を返す。
 * 戻り値は void * にキャストした wchar_t * の NULL 終端配列。
 * 配列および各要素は全て新しく malloc した領域である。
 * 配列でない普通の変数は、要素数 1 の配列として返す。
 * concat: 配列の要素を結合すべきかどうかが *concat に入る。
 *     name が "*" なら true、それ以外なら false になる。 */
void **get_variable(const char *name, bool *concat)
{
    void **result;
    wchar_t *value;
    variable_T *var;

    *concat = false;
    if (!name[0])
	return NULL;
    if (!name[1]) {
	switch (name[0]) {  /* 名前が一文字なので、特殊パラメータかどうか見る */
	    case '*':
		*concat = true;
		/* falls thru! */
	    case '@':
		var = search_variable(VAR_positional, false);
		assert(var != NULL && (var->v_type & VF_MASK) == VF_ARRAY);
		result = var->v_vals;
		goto return_array;
	    case '#':
		var = search_variable(VAR_positional, false);
		assert(var != NULL && (var->v_type & VF_MASK) == VF_ARRAY);
		value = malloc_wprintf(L"%zu", var->v_valc);
		goto return_single;
	    case '?':
		value = malloc_wprintf(L"%d", laststatus);
		goto return_single;
	    case '-':  // TODO variable:get_variable: 特殊パラメータ '-'
	    case '$':
		value = malloc_wprintf(L"%jd", (intmax_t) shell_pid);
		goto return_single;
	    case '!':
		value = malloc_wprintf(L"%jd", (intmax_t) lastasyncpid);
		goto return_single;
	    case '0':
		value = malloc_mbstowcs(command_name);
		goto return_single;
	}
    }

    if (xisdigit(name[0])) {  /* 名前の先頭が数字なので、位置パラメータである */
	char *nameend;
	errno = 0;
	long v = strtol(name, &nameend, 10);
	if (errno || *nameend != '\0')
	    return NULL;  /* 数値ではない or オーバーフロー */
	var = search_variable(VAR_positional, false);
	assert(var != NULL && (var->v_type & VF_MASK) == VF_ARRAY);
	if (v <= 0 || (uintmax_t) var->v_valc < (uintmax_t) v)
	    return NULL;  /* インデックスが範囲外 */
	value = xwcsdup(var->v_vals[v - 1]);
	goto return_single;
    }

    /* 普通の変数を探す */
    var = search_variable(name, true);
    if (var) {
	if (var->v_getter)
	    var->v_getter(var);
	switch (var->v_type & VF_MASK) {
	    case VF_NORMAL:
		value = xwcsdup(var->v_value);
		goto return_single;
	    case VF_ARRAY:
		result = var->v_vals;
		goto return_array;
	}
    }
    return NULL;

return_single:  /* 一つの値を要素数 1 の配列で返す。 */
    if (!value)
	return NULL;
    result = xmalloc(2 * sizeof *result);
    result[0] = value;
    result[1] = NULL;
    return result;

return_array:  /* 配列をコピーして返す */
    return dupwcsarray(result);
}

/* SHLVL 変数の値に change を加える。 */
void set_shlvl(long change)
{
    if (!posixly_correct) {
	long shlvl;
	const wchar_t *shlvlw = getvar(VAR_SHLVL);
	if (shlvlw) {
	    wchar_t *endp;
	    errno = 0;
	    shlvl = wcstol(shlvlw, &endp, 0);
	    if (errno || *endp != L'\0')
		shlvl = 0L;
	} else {
	    shlvl = 0L;
	}
	shlvl += change;
	set_variable(VAR_SHLVL, malloc_wprintf(L"%ld", shlvl), false, true);
    }
}


/********** 各種ゲッター **********/

/* 現在実行中のコマンドの行番号 */
unsigned long current_lineno;

/* LINENO 変数のゲッター */
void lineno_getter(variable_T *var)
{
    assert((var->v_type & VF_MASK) == VF_NORMAL);
    free(var->v_value);
    var->v_value = malloc_wprintf(L"%lu", current_lineno);
    if (var->v_type & VF_EXPORT)
	update_enrivon(VAR_LINENO);
}

/* RANDOM 変数のゲッター */
void random_getter(variable_T *var)
{
    assert((var->v_type & VF_MASK) == VF_NORMAL);
    free(var->v_value);
    var->v_value = malloc_wprintf(L"%u", next_random());
    if (var->v_type & VF_EXPORT)
	update_enrivon(VAR_RANDOM);
}

/* rand を呼び出して 0 以上 32767 以下の乱数を返す */
unsigned next_random(void)
{
#if RAND_MAX == 32767
    return rand();
#elif RAND_MAX == 65535
    return rand() >> 1;
#elif RAND_MAX == 2147483647
    return rand() >> 16;
#elif RAND_MAX == 4294967295
    return rand() >> 17;
#else
    unsigned max, value;
    do {
	max   = RAND_MAX;
	value = rand();
	while (max > 65535) {
	    max   >>= 1;
	    value >>= 1;
	}
	if (max == 65535)
	    return value >> 1;
    } while (value > 32767);
    return value;
#endif
}


/********** 各種セッター **********/

/* 変数が設定されたときに呼ばれる */
void variable_set(const char *name, variable_T *var)
{
    switch (name[0]) {
    case 'L':
	if (strcmp(name, VAR_LANG) == 0 || strncmp(name, "LC_", 3) == 0)
	    reset_locale(name);
	break;
    case 'P':
	if (strcmp(name, VAR_PATH) == 0) {
	    switch (var->v_type & VF_MASK) {
		case VF_NORMAL:
		    reset_patharray(var->v_value);
		    break;
		case VF_ARRAY:
		    reset_patharray(L"");
		    break;  // TODO variable: variable_set: PATH が配列の場合
	    }
	}
	break;
    case 'R':
	if (random_active && strcmp(name, VAR_RANDOM) == 0) {
	    if ((var->v_type & VF_MASK) == VF_NORMAL)
		var->v_getter = random_getter;
	}
	break;
	// TODO variable: variable_set: 他の変数
    }
}


/* vim: set ts=8 sts=4 sw=4 noet: */
