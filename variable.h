/* Yash: yet another shell */
/* variable.h: deals with shell variables and parameters */
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


#ifndef VARIABLE_H
#define VARIABLE_H

#include <stddef.h>


extern char **environ;

/* シェル変数名 */
#define VAR_CDPATH	"CDPATH"
#define VAR_COLUMNS	"COLUMNS"
#define VAR_ENV		"ENV"
#define VAR_FCEDIT	"FCEDIT"
#define VAR_HISTFILE	"HISTFILE"
#define VAR_HISTSIZE	"HISTSIZE"
#define VAR_HOME	"HOME"
#define VAR_IFS		"IFS"
#define VAR_LANG	"LANG"
#define VAR_LC_ALL	"LC_ALL"
#define VAR_LC_COLLATE	"LC_COLLATE"
#define VAR_LC_CTYPE	"LC_CTYPE"
#define VAR_LC_MESSAGES	"LC_MESSAGES"
#define VAR_LC_MONETARY	"LC_MONETARY"
#define VAR_LC_NUMERIC	"LC_NUMERIC"
#define VAR_LC_TIME	"LC_TIME"
#define VAR_LINENO	"LINENO"
#define VAR_LINES	"LINES"
#define VAR_MAIL	"MAIL"
#define VAR_MAILCHECK	"MAILCHECK"
#define VAR_MAILPATH	"MAILPATH"
#define VAR_NLSPATH	"NLSPATH"
#define VAR_OLDPWD	"OLDPWD"
#define VAR_PATH	"PATH"
#define VAR_PPID	"PPID"
#define VAR_PS1		"PS1"
#define VAR_PS2		"PS2"
#define VAR_PS3		"PS3"
#define VAR_PS4		"PS4"
#define VAR_PWD		"PWD"
#define VAR_RANDOM	"RANDOM"
#define VAR_SHLVL	"SHLVL"
#define VAR_YASH_VERSION "YASH_VERSION"

/* 変数の属性フラグ */
typedef enum vartype_T {
    VF_NORMAL,
    VF_ARRAY,
    VF_EXPORT   = 1 << 2,  /* 変数を export するかどうか */
    VF_READONLY = 1 << 3,  /* 変数が読み取り専用かどうか */
    VF_NODELETE = 1 << 4,  /* 変数を削除できないようにするかどうか */
} vartype_T;
#define VF_MASK ((1 << 2) - 1)
/* vartype_T の値は VF_NORMAL と VF_ARRAY のどちらかと他のフラグとの OR である。
 * VF_EXPORT は VF_NORMAL でのみ使える。 */

/* 変数を表す構造体 */
typedef struct variable_T {
    vartype_T v_type;
    union {
	wchar_t *value;
	struct {
	    void **vals;
	    size_t valc;
	} array;
    } v_contents;
    void (*v_getter)(struct variable_T *var);
} variable_T;
#define v_value v_contents.value
#define v_vals  v_contents.array.vals
#define v_valc  v_contents.array.valc
/* v_vals は、void * にキャストした wchar_t * の NULL 終端配列である。
 * v_valc はもちろん v_vals の要素数である。
 * v_value, v_vals および v_vals の要素は free 可能な領域を指す。
 * v_getter は変数が取得される前に呼ばれる関数。変数に代入すると v_getter は
 * NULL に戻る。 */


extern unsigned long current_lineno;

extern void init_variables(void);
extern void finalize_variables(void);
extern bool is_name(const char *s)
    __attribute__((nonnull,pure));
extern bool set_variable(
	const char *name, wchar_t *value, bool local, bool export)
    __attribute__((nonnull));
extern bool set_array(const char *name, char *const *values, bool local)
    __attribute__((nonnull));
extern void set_positional_parameters(char *const *values)
    __attribute__((nonnull));
extern const wchar_t *getvar(const char *name)
    __attribute__((pure,nonnull));
extern void **get_variable(const char *name, bool *concat)
    __attribute__((nonnull,malloc,warn_unused_result));
extern void set_shlvl(long change);


#endif /* VARIABLE_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
