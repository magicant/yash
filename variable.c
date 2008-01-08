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
#include "expand.h"
#include "exec.h"
#include "variable.h"
#include <assert.h>


/* 変数を表す構造体 */
struct variable {
	char *value;
	/*union {
		int    as_int;
		double as_double;
	} avalue;*/
	enum {
		VF_EXPORT   = 1 << 0,
		VF_READONLY = 1 << 1,
	} flags;
	const char *(*getter)(struct variable *var);
	bool (*setter)(struct variable *var, const char *value, bool export);
};
/* VF_EXPORT フラグが立っている変数は、export 対象であり、value メンバの内容
 * よりも environ に入っている内容を優先する */
/* getter/setter は変数のゲッター・セッターである。
 * 非 NULL なら、値を取得・設定する際にフィルタとして使用する。
 * ゲッター・セッターの戻り値はそのまま getvar/setvar の戻り値になる。
 * ゲッターが非 NULL のとき、value は NULL である。
 * セッターは、variable 構造体の value メンバを書き換える等、変数を設定するのに
 * 必要な動作を全て行う。セッターが NULL ならデフォルトの動作をする。 */

/* 変数環境を表す構造体 */
struct environment {
	struct environment *parent;  /* 親環境 */
	struct hasht variables;      /* 普通の変数のハッシュテーブル */
	struct plist positionals;    /* 位置パラメータのリスト */
};
/* variables の要素は struct variable へのポインタ、positionals の要素は
 * char へのポインタである。 */
/* $0 は位置パラメータではない。故に positionals.contents[0] は NULL である。 */


char *xgetcwd(void);
void init_var(void);
void set_shlvl(int change);
void set_positionals(char *const *values);
static struct variable *get_variable(const char *name, bool temp);
const char *getvar(const char *name);
bool setvar(const char *name, const char *value, bool export);
struct plist *getarray(const char *name);
bool unsetvar(const char *name);
bool export(const char *name);
void unexport(const char *name);
bool is_exported(const char *name);
bool assign_variables(char **assigns, bool temp, bool export);
void unset_temporary(const char *name);
bool is_special_parameter_char(char c);
bool is_name_char(char c);
bool is_name(const char *c);
static const char *count_getter(struct variable *var);
static const char *laststatus_getter(struct variable *var);
static const char *pid_getter(struct variable *var);
static const char *last_bg_pid_getter(struct variable *var);
static const char *zero_getter(struct variable *var);


/* 変数を保存する環境 */
static struct environment *current_env;
/* 一時的変数を保存するハッシュテーブル */
static struct hasht temp_variables;


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
	pl_append(&current_env->positionals, NULL);

	ht_init(&temp_variables);

	/* まず current_env->variables に全ての環境変数を設定する。 */
	for (char **e = environ; *e; e++) {
		size_t namelen = strcspn(*e, "=");
		char name[namelen + 1];
		strncpy(name, *e, namelen);
		name[namelen] = '\0';
		if ((*e)[namelen] != '=')
			continue;

		struct variable *var = xmalloc(sizeof *var);
		*var = (struct variable) {
			.value = xstrdup(*e + namelen + 1),
			.flags = VF_EXPORT,
		};
		ht_set(&current_env->variables, name, var);
	}

	/* 特殊パラメータを設定する。 */
	var = xmalloc(sizeof *var);
	*var = (struct variable) {
		.value = NULL,
		.getter = count_getter,
	};
	ht_set(&current_env->variables, "#", var);
	var = xmalloc(sizeof *var);
	*var = (struct variable) {
		.value = NULL,
		.getter = laststatus_getter,
	};
	ht_set(&current_env->variables, "?", var);
	/*var = xmalloc(sizeof *var);
	*var = (struct variable) {
		.value = NULL,
		.getter = TODO $- parameter,
	};
	ht_set(&current_env->variables, "-", var);*/
	var = xmalloc(sizeof *var);
	*var = (struct variable) {
		.value = NULL,
		.getter = pid_getter,
	};
	ht_set(&current_env->variables, "$", var);
	var = xmalloc(sizeof *var);
	*var = (struct variable) {
		.value = NULL,
		.getter = last_bg_pid_getter,
	};
	ht_set(&current_env->variables, "!", var);
	var = xmalloc(sizeof *var);
	*var = (struct variable) {
		.value = NULL,
		.getter = zero_getter,
	};
	ht_set(&current_env->variables, "0", var);

	/* デフォルトの export を設定する。 */
	export(VAR_HOME);
	export(VAR_LANG);
	export(VAR_LC_ALL);
	export(VAR_LC_COLLATE);
	export(VAR_LC_CTYPE);
	export(VAR_LC_MESSAGES);
	export(VAR_LC_MONETARY);
	export(VAR_LC_NUMERIC);
	export(VAR_LC_TIME);
	export(VAR_PATH);
	export(VAR_PWD);
	export(VAR_OLDPWD);
	export(VAR_SHLVL);
	export(VAR_LINES);
	export(VAR_COLUMNS);

	/* PWD 環境変数を設定する */
	char *pwd = xgetcwd();
	if (pwd) {
		setvar(VAR_PWD, pwd, true);
		free(pwd);
	}
}

/* 現在の環境の位置パラメータを設定する。既存の位置パラメータは削除する。
 * values[0] が $1 に、values[1] が $2 に、という風になる。values[x] が NULL
 * になったら終わり。 */
void set_positionals(char *const *values)
{
	struct plist *pos = &current_env->positionals;

	/* まず古いのを消す。 */
	for (size_t i = 1; i < pos->length; i++)
		free(pos->contents[i]);
	pl_remove(pos, 1, SIZE_MAX);

	/* 新しいのを追加する。 */
	assert(pos->length == 1);
	while (*values) {
		pl_append(pos, xstrdup(*values));
		values++;
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
		if (!setvar(VAR_SHLVL, newshlvl, false))
			error(0, 0, "failed to set env SHLVL");
	}
}

/* 現在の変数環境や親変数環境・一時的変数から指定した変数を捜し出す。
 * 位置パラメータ、$*, $@ は取得できない。
 * temp: 一時的変数からも探すかどうか。 */
static struct variable *get_variable(const char *name, bool temp)
{
	if (temp) {
		struct variable *var = ht_get(&temp_variables, name);
		if (var)
			return var;
	}

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
 * 特殊パラメータ $* および $@ は得られない。
 * 変数が存在しないときは NULL を返す。 */
const char *getvar(const char *name)
{
	struct variable *var = get_variable(name, true);  /* 普通の変数を探す */
	if (var) {
		if (var->getter)
			return var->getter(var);
		if (var->flags & VF_EXPORT)
			return getenv(name);
		return var->value;
	}
	if (xisdigit(name[0])) {  /* 位置パラメータを探す */
		char *end;
		errno = 0;
		size_t posindex = strtoul(name, &end, 10);
		if (*end || errno)
			return NULL;
		if (0 < posindex && current_env->positionals.length)
			return current_env->positionals.contents[posindex];
	}
	return NULL;
}

/* シェル変数の値を設定する。変数が存在しない場合、基底変数環境に追加する。
 * この関数では位置・特殊パラメータは設定できない (してはいけない)。
 * 同名の一時的変数があれば、一時的変数を削除してから普通に変数を設定する。
 * name:   正しい変数名。
 * export: true ならその変数を export 対象にする。false ならそのまま。
 * 戻り値: 成功なら true、エラーならメッセージを出して false。 */
// TODO 配列も代入可能に
bool setvar(const char *name, const char *value, bool export)
{
	assert(!xisdigit(name[0]));
	assert(!is_special_parameter_char(name[0]));

	unset_temporary(name);

	struct variable *var = get_variable(name, false);
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
	if (var->flags & VF_READONLY) {
		error(0, 0, "%s: readonly variable cannot be assigned to", name);
		return false;
	}

	bool ok;
	if (var->setter) {
		ok = var->setter(var, value, export);
	} else {
		ok = true;
		if (export)
			var->flags |= VF_EXPORT;
		if (var->flags & VF_EXPORT) {
			if (setenv(name, value, true) < 0) {
				error(0, errno, "export %s=%s", name, value);
				ok = false;
			}
		}
		if (var->value != value) {
			free(var->value);
			var->value = xstrdup(value);
		}
	}
	assert(var->value != NULL);
	return ok;
}

/* 指定した名前の配列変数の内容を取得する。
 * name が NULL なら位置パラメータリストを取得する。
 * 配列変数が見付からなければ NULL を返す。 */
/* 位置パラメータのインデックスは 0 ではなく 1 から始まることに注意 */
struct plist *getarray(const char *name)
{
	if (!name)
		return &current_env->positionals;
	return NULL; //XXX 配列は未実装
}

/* シェル変数を削除する
 * 戻り値: エラーがなければ true、変数を削除できなければ false。
 *         変数が存在しなかったら true。 */
bool unsetvar(const char *name)
{
	unset_temporary(name);

	struct environment *env = current_env;
	while (env) {
		struct variable *var = ht_remove(&env->variables, name);
		if (var) {
			bool oldvarexport = var->flags & VF_EXPORT;
			if (var->flags & VF_READONLY) {
				ht_set(&env->variables, name, var);
				error(0, 0, "cannot unset readonly variable %s", name);
				return false;
			}
			free(var->value);
			free(var);

			/* 親環境に同名の変数があれば、environ の内容をそれに合わせる */
			var = get_variable(name, false);
			if (!var || !(var->flags & VF_EXPORT))
				unsetenv(name);
			else if (!getenv(name) && oldvarexport)
				setenv(name, var->value, true);
			return true;
		}
		env = env->parent;
	}
	return true;
}

/* 変数を export 対象にする。
 * 戻り値: 成功したかどうか */
bool export(const char *name)
{
	unset_temporary(name);

	struct variable *var = get_variable(name, false);
	if (var) {
		if (setenv(name, var->value, false) < 0) {
			error(0, errno, "export %s=%s", name, var->value);
			return false;
		}
	} else {
		struct environment *env = current_env;
		while (env->parent)
			env = env->parent;
		var = xmalloc(sizeof *var);
		*var = (struct variable) {
			.value = NULL,
			.flags = VF_EXPORT,
		};
		ht_set(&env->variables, name, var);
	}
	return true;
}

/* 変数を非 export 対象にする。 */
void unexport(const char *name)
{
	unset_temporary(name);

	struct variable *var = get_variable(name, false);
	if (var)
		var->flags &= ~VF_EXPORT;
	unsetenv(name);
}

/* 指定した名前のシェル変数が存在しかつ export 対象かどうかを返す。
 * 一時的変数は考慮しない。 */
bool is_exported(const char *name)
{
	struct variable *var = get_variable(name, false);
	return var && (var->flags & VF_EXPORT);
}

/* 変数代入を行う。
 * assigns: 代入する "変数=値" の配列へのポインタ。
 * temp:    true なら一時的変数として代入する。false なら普通に代入する。
 * export:  代入する変数を export 対象にするかどうか。
 * 戻り値:  成功すれば true、エラーなら false。
 * temp と export を両方 true にしてはいけない。
 * エラーになっても途中結果を元に戻す為のデータが *save に入る。 */
bool assign_variables(char **assigns, bool temp, bool export)
{
	assert(!temp || !export);

	char *assign;
	while ((assign = *assigns)) {
		//XXX 配列の代入は未対応
		size_t namelen = strcspn(assign, "=");
		char name[namelen + 1];
		strncpy(name, assign, namelen);
		name[namelen] = '\0';
		assert(namelen > 0 && assign[namelen] == '=');

		char *value = expand_word(assign + namelen + 1, true);
		if (!value) {
			return false;
		}
		if (temp) {
			struct variable *var = xmalloc(sizeof *var);
			*var = (struct variable) { .value = value, .flags = 0, };
			var = ht_set(&temp_variables, name, var);
			if (var) {
				free(var->value);
				free(var);
			}
		} else {
			if (!setvar(name, value, export)) {
				free(value);
				return false;
			}
		}
		assigns++;
	}
	return true;
}

/* unset_temporary で使う内部関数 */
static int free_tempvar(const char *name __attribute__((unused)), void *value)
{
	struct variable *var = value;
	assert(var != NULL);
	free(var->value);
	free(var);
	return 0;
}

/* 一時的変数を削除する。
 * name: 削除する一時的変数の名前。NULL なら全部の一時的変数を削除する。 */
void unset_temporary(const char *name)
{
	if (name) {
		struct variable *var = ht_remove(&temp_variables, name);
		if (var) {
			free(var->value);
			free(var);
		}
	} else {
		ht_each(&temp_variables, free_tempvar);
		ht_clear(&temp_variables);
	}
}

/* 引数 c が特殊パラメータの名前であるかどうか判定する。
 * 例外的に、'0' はこの関数では false を返す。 */
bool is_special_parameter_char(char c)
{
	return strchr("@*#?-$!_", c) != NULL;
}

/* 文字が変数名に使えるかどうか判定する。 */
bool is_name_char(char c)
{
	switch (c) {
		case 'a':  case 'b':  case 'c':  case 'd':  case 'e':  case 'f':
		case 'g':  case 'h':  case 'i':  case 'j':  case 'k':  case 'l':
		case 'm':  case 'n':  case 'o':  case 'p':  case 'q':  case 'r':
		case 's':  case 't':  case 'u':  case 'v':  case 'w':  case 'x':
		case 'y':  case 'z':  case '_':
		case 'A':  case 'B':  case 'C':  case 'D':  case 'E':  case 'F':
		case 'G':  case 'H':  case 'I':  case 'J':  case 'K':  case 'L':
		case 'M':  case 'N':  case 'O':  case 'P':  case 'Q':  case 'R':
		case 'S':  case 'T':  case 'U':  case 'V':  case 'W':  case 'X':
		case 'Y':  case 'Z':  case '0':  case '1':  case '2':  case '3':
		case '4':  case '5':  case '6':  case '7':  case '8':  case '9':
			return true;
		default:
			return false;
	}
}

/* 文字列が変数名として正しいかどうか判定する。 */
bool is_name(const char *s)
{
	if ('0' <= *s && *s <= '9')
		return false;
	while (is_name_char(*s)) s++;
	return !*s;
}

/* 特殊パラメータ $# のゲッター。位置パラメータの数を返す。 */
static const char *count_getter(
		struct variable *var __attribute__((unused)))
{
	static char result[INT_STRLEN_BOUND(size_t) + 1];
	if (snprintf(result, sizeof result, "%zd",
				current_env->positionals.length - 1) >= 0)
		return result;
	return NULL;
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

/* 特殊パラメータ $! のゲッター。最後に起動したバックグラウンドジョブの PID。 */
static const char *last_bg_pid_getter(
		struct variable *var __attribute__((unused)))
{
	static char result[INT_STRLEN_BOUND(pid_t) + 1];
	if (snprintf(result, sizeof result, "%.0jd", (intmax_t) last_bg_pid) >= 0)
		return result;
	return NULL;
}

/* 特殊パラメータ $0 のゲッター。実行中のシェル(スクリプト)名を返す。 */
static const char *zero_getter(
		struct variable *var __attribute__((unused)))
{
	return command_name;
}
