/* Yash: yet another shell */
/* variable.h: interface to shell variable manager */
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


extern char **environ;

#define VAR_USER            "USER"
#define VAR_LOGNAME         "LOGNAME"
#define VAR_HOME            "HOME"
#define VAR_LANG            "LANG"
#define VAR_PATH            "PATH"
#define VAR_PWD             "PWD"
#define VAR_OLDPWD          "OLDPWD"
#define VAR_IFS             "IFS"
#define VAR_SHLVL           "SHLVL"
#define VAR_HOSTNAME        "HOSTNAME"
#define VAR_POSIXLY_CORRECT "POSIXLY_CORRECT"
#define VAR_LC_ALL          "LC_ALL"
#define VAR_LC_COLLATE      "LC_COLLATE"
#define VAR_LC_CTYPE        "LC_CTYPE"
#define VAR_LC_MESSAGES     "LC_MESSAGES"
#define VAR_LC_MONETARY     "LC_MONETARY"
#define VAR_LC_NUMERIC      "LC_NUMERIC"
#define VAR_LC_TIME         "LC_TIME"
#define VAR_LINES           "LINES"
#define VAR_COLUMNS         "COLUMNS"

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
		VF_NODELETE = 1 << 2,
	} flags;
	const char *(*getter)(struct variable *var);
	bool (*setter)(struct variable *var);
};
/* VF_EXPORT フラグが立っている変数は、export 対象であり、value メンバの内容
 * よりも environ に入っている内容を優先する */
/* getter/setter/deleter は変数のゲッター・セッター・削除関数である。
 * 非 NULL なら、値を取得・設定する際にフィルタとして使用する。
 * ゲッター・セッターの戻り値はそのまま getvar/setvar の戻り値になる。
 * セッターは unsetvar でも value = NULL で呼ばれる。 */

/* 変数代入を元に戻すためのデータ */
struct save_assignment;

char *xgetcwd(void);
void init_var(void);
void set_shlvl(int change);
void set_positionals(char *const *values)
	__attribute__((nonnull));
const char *getvar(const char *name)
	__attribute__((nonnull));
bool setvar(const char *name, const char *value, bool export)
	__attribute__((nonnull));
struct hasht *get_variable_table(void);
struct plist *getarray(const char *name);
bool unsetvar(const char *name)
	__attribute__((nonnull));
bool export(const char *name)
	__attribute__((nonnull));
void unexport(const char *name)
	__attribute__((nonnull));
bool is_exported(const char *name)
	__attribute__((nonnull));
bool assign_variables(char **assigns, bool temp, bool export)
	__attribute__((nonnull));
void unset_temporary(const char *name);
bool is_special_parameter_char(char c);
bool is_name_char(char c);
bool is_name(const char *c)
	__attribute__((nonnull));
