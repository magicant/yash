/* Yash: yet another shell */
/* variable.c: shell variable manager */
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


#include <error.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include "yash.h"
#include "util.h"
#include "exec.h"
#include "variable.h"
#include <assert.h>


struct variable {
	char *value;
	/*union {
		int    as_int;
		double as_double;
	} avalue;*/
	enum {
		VF_READONLY = 1 << 0,
	} flags;
	const char *(*getter)(struct variable *var);
	bool (*setter)(struct variable *var, const char *value, bool export);
};
/* value が NULL なら、それは export 対象の環境変数であり、値は environ にある。
 * environ の中にもなければ、export 対象だが設定されていない変数である。
 * value が非 NULL なら、それは free 可能な文字列を指していて、変数は非 export
 * 対象である。変数の値が value と environ の両方に入っていることはない。 */
/* getter/setter は変数のゲッター・セッターである。
 * 非 NULL なら、値を取得・設定する際にフィルタとして使用する。
 * ゲッター・セッターの戻り値はそのまま getvar/setvar の戻り値になる。
 * ゲッターが非 NULL のとき、value は NULL (export 対象のとき) またはダミーの
 * 空文字列 (非 export 対象のとき) である。
 * セッターは、variable 構造体の value メンバを書き換える等、変数を設定するのに
 * 必要な動作を全て行う。セッターが NULL ならデフォルトの動作をする。 */

struct environment {
	struct environment *parent;  /* 親環境 */
	struct hasht variables;      /* 普通の変数のハッシュテーブル */
	struct plist positionals;    /* 位置パラメータのリスト */
};
/* $0 は位置パラメータではない。故に positionals.contents[0] は NULL である。 */


char *xgetcwd(void);
void init_var(void);
void set_shlvl(int change);
static struct variable *get_variable(const char *name);
const char *getvar(const char *name);
bool setvar(const char *name, const char *value, bool export);
bool is_exported(const char *name);
bool is_special_parameter_char(char c);
static const char *laststatus_getter(struct variable *var);
static const char *pid_getter(struct variable *var);
static const char *zero_getter(struct variable *var);


static struct environment *current_env;


/* getcwd(3) の結果を新しく malloc した文字列で返す。
 * エラー時は NULL を返し、errno を設定する。 */
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


/* 環境変数などを初期化する。 */
void init_var(void)
{
	struct variable *var;

	current_env = xmalloc(sizeof *current_env);
	current_env->parent = NULL;
	ht_init(&current_env->variables);
	pl_init(&current_env->positionals);

	/* まず current_env->variables に全ての環境変数を設定する。 */
	for (char **e = environ; *e; e++) {
		size_t namelen = strcspn(*e, "=");
		char name[namelen + 1];
		strncpy(name, *e, namelen);
		name[namelen] = '\0';
		if ((*e)[namelen] != '=')
			continue;

		struct variable *var = xmalloc(sizeof *var);
		*var = (struct variable) { .value = NULL, };
		ht_set(&current_env->variables, name, var);
	}

	/* 特殊パラメータを設定する。 */
	var = xmalloc(sizeof *var);
	*var = (struct variable) {
		.value = "",
		.getter = laststatus_getter,
	};
	ht_set(&current_env->variables, "?", var);
	var = xmalloc(sizeof *var);
	*var = (struct variable) {
		.value = "",
		.getter = pid_getter,
	};
	ht_set(&current_env->variables, "$", var);
	var = xmalloc(sizeof *var);
	*var = (struct variable) {
		.value = "",
		.getter = zero_getter,
	};
	ht_set(&current_env->variables, "0", var);
	//TODO 他のパラメータ

	/* PWD 環境変数を設定する */
	char *pwd = xgetcwd();
	if (pwd) {
		setvar(VAR_PWD, pwd, true);
		free(pwd);
	}
}

/* 環境変数 SHLVL に change を加える */
void set_shlvl(int change)
{
	const char *shlvl = getvar(VAR_SHLVL);
	int level = shlvl ? atoi(shlvl) : 0;
	char newshlvl[16];

	level += change;
	if (level < 0)
		level = 0;
	if (snprintf(newshlvl, sizeof newshlvl, "%d", level) >= 0) {
		if (!setvar(VAR_SHLVL, newshlvl, true))
			error(0, 0, "failed to set env SHLVL");
	}
}

/* 現在の変数環境や親変数環境から指定した変数を捜し出す。 */
static struct variable *get_variable(const char *name)
{
	struct environment *env = current_env;
	while (env) {
		struct variable *var = ht_get(&env->variables, name);
		if (var)
			return var;
		env = env->parent;
	}
	return NULL;
}

/* 指定した名前のシェル変数を取得する。
 * 変数が存在しないときは NULL を返す。 */
const char *getvar(const char *name)
{
	struct variable *var = get_variable(name);
	if (var) {
		if (var->getter)
			return var->getter(var);
		if (var->value)
			return var->value;
		return getenv(name);
	}
	return NULL;
}

/* シェル変数の値を設定する。変数が存在しない場合、基底変数環境に追加する。
 * export: true ならその変数を export 対象にする。false ならそのまま。
 * 戻り値: 成功なら true、エラーなら false。 */
bool setvar(const char *name, const char *value, bool export)
{
	struct variable *var = get_variable(name);
	error(0, 0, "DEBUG: name=%s var=%p", name, var);
	if (!var) {
		struct environment *env = current_env;
		while (env->parent)
			env = env->parent;
		var = xmalloc(sizeof *var);
		*var = (struct variable) {
			.value = NULL,
			.flags = 0,
			.getter = NULL,
			.setter = NULL,
		};
		ht_set(&env->variables, name, var);
	}
	if (var->setter) {
		return var->setter(var, value, export);
	} else {
		//TODO readonly
		free(var->value);
		if (export) {
			var->value = NULL;
			return setenv(name, value, true) == 0;
		} else {
			var->value = xstrdup(value);
			return true;
		}
	}
}

/* 指定した名前のシェル変数が存在しかつ export 対象かどうかを返す。 */
bool is_exported(const char *name)
{
	struct variable *var = get_variable(name);
	return var && !var->value;
}

/* 引数 c が特殊パラメータを名前であるかどうか判定する */
bool is_special_parameter_char(char c)
{
	return strchr("@*#?-$!_", c) != NULL;
}

/* 特殊パラメータ $? のゲッター。laststatus の値を返す。 */
static const char *laststatus_getter(
		struct variable *var __attribute__((unused)))
{
	static char result[INT_STRLEN_BOUND(int) + 1];
	if (snprintf(result, sizeof result, "%d", laststatus) >= 0)
		return result;
	return NULL;
}

/* 特殊パラメータ $$ のゲッター。シェルのプロセス ID の値を返す。 */
static const char *pid_getter(
		struct variable *var __attribute__((unused)))
{
	static char result[INT_STRLEN_BOUND(pid_t) + 1];
	if (snprintf(result, sizeof result, "%jd", (intmax_t) shell_pid) >= 0)
		return result;
	return NULL;
}

/* 特殊パラメータ $0 のゲッター。実行中のシェル(スクリプト)名を返す。 */
static const char *zero_getter(
		struct variable *var __attribute__((unused)))
{
	return command_name;
}
