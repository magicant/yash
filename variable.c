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
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "option.h"
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "hashtable.h"
#include "path.h"
#include "variable.h"
#include "exec.h"
#include "yash.h"


/* 変数環境 (=現在有効な変数・パラメータの集まり) を表す構造体。 */
/* 言葉が似ているが環境変数とは全く異なる。 */
typedef struct environ_T {
    struct environ_T *parent;      /* 親環境 */
    struct hashtable_T contents;   /* 変数を保持するハッシュテーブル */
} environ_T;
/* contents は char * から variable_T * へのハッシュテーブルである。 */
/* 位置パラメータは、contents に "*" という名前の配列として入れる。
 * 位置パラメータの番号と配列のインデックスが一つずれるので注意。 */
#define VAR_positional "*"


static void varfree(variable_T *v);
static void varkvfree(kvpair_T kv);
__attribute__((nonnull))
static variable_T *search_variable(const char *name, bool temp);


/* 現在の変数環境 */
static environ_T *current_env;

/* 一時的変数を保持するハッシュテーブル。
 * 一時的変数は、特殊組込みコマンドでない組込みコマンドを実行する際に
 * 一時的な代入を行うためのものである。例えば、"HOME=/ cd" というコマンドライン
 * を実行する際に HOME 変数を一時的に代入し、cd の実行が終わったら削除する。 */
static hashtable_T temp_variables;


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

    current_env = xmalloc(sizeof *current_env);
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

    // TODO variable: init_variables: PWD を設定
    // TODO variable: init_variables: PPID を設定
    // TODO variable: init_variables: YASH_VERSION を設定

    /* PATH に基づいて patharray を初期化する */
    reset_patharray(getenv(VAR_PATH));
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

/* 指定した名前のシェル変数を取得する。
 * 普通の変数 (VF_NORMAL) にのみ使える。
 * "$" や "@" などの特殊パラメータは得られない。
 * 変数がなければ NULL を返す。
 * 戻り値を変更したり free したりしてはならない。
 * 戻り値は変数を変更・削除するまで有効である。 */
const wchar_t *getvar(const char *name)
{
    variable_T *var = search_variable(name, true);
    if (var && (var->v_type & VF_MASK) == VF_NORMAL)
	return var->v_value;
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

    /* 普通の変数を探す */
    var = search_variable(name, true);
    if (var) {
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

/* 現在の環境の位置パラメータを設定する。既存の位置パラメータは削除する。
 * values[0] が $1、values[1] が $2、というようになる。 */
void set_positional_parameters(char *const *values)
{
    (void) values;
    // TODO variable: set_positional_parameters: 未実装
}

/* SHLVL 変数の値に change を加える。 */
void set_shlvl(int change)
{
    (void) change;
    // TODO variable: set_shlvl: 未実装
}


/* vim: set ts=8 sts=4 sw=4 noet: */
