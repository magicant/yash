/* Yash: yet another shell */
/* variable.c: deals with shell variables and parameters */
/* (C) 2007-2009 magicant */

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
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "builtin.h"
#include "exec.h"
#include "expand.h"
#include "hashtable.h"
#include "input.h"
#include "option.h"
#include "parser.h"
#include "path.h"
#include "plist.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"
#include "version.h"
#include "yash.h"
#if YASH_ENABLE_LINEEDIT
# include "lineedit/lineedit.h"
# include "lineedit/terminfo.h"
#endif


const wchar_t *const path_variables[PA_count] = {
    [PA_PATH]     = L VAR_PATH,
    [PA_CDPATH]   = L VAR_CDPATH,
    [PA_MAILPATH] = L VAR_MAILPATH,
};


/* variable environment (= set of variables) */
/* not to be confused with environment variables */
typedef struct environ_T {
    struct environ_T *parent;      /* parent environment */
    struct hashtable_T contents;   /* hashtable containing variables */
    bool is_temporary;             /* for temporary assignment? */
    char **paths[PA_count];
} environ_T;
/* `contents' is a hashtable from (wchar_t *) to (variable_T *).
 * A variable name may contain any characters except L'\0' and L'=', though
 * assignment syntax allows only limited characters.
 * Variable names starting with L'=' are used for special purposes.
 * The positional parameter is treated as an array whose name is L"=".
 * Note that the number of positional parameters is offseted by 1 against the
 * array index. */
/* An environment whose `is_temporary' is true is used for temporary variables. 
 * Temporary variables may be exported, but cannot be readonly.
 * Elements of `path' are arrays of the pathnames contained in the $PATH/CDPATH
 * variables. They are NULL if the corresponding variables are not set. */
#define VAR_positional "="

/* flags for variable attributes */
typedef enum vartype_T {
    VF_NORMAL,
    VF_ARRAY,
    VF_EXPORT   = 1 << 2,
    VF_READONLY = 1 << 3,
    VF_NODELETE = 1 << 4,
} vartype_T;
#define VF_MASK ((1 << 2) - 1)
/* For all variables, the variable type is either of VF_NORMAL and VF_ARRAY,
 * possibly OR'ed with other flags. */

/* shell variable */
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
/* `v_vals' is a NULL-terminated array of pointers to wide strings cast to
 * (void *). `v_valc' is, of course, the number of elements in `v_vals'.
 * `v_value', `v_vals' and the elements of `v_vals' are `free'able.
 * `v_value' is NULL if the variable is declared but not yet assigned.
 * `v_vals' is always non-NULL, but it may contain no elements.
 * `v_getter' is the setter function, which is reset to NULL on reassignment.*/

/* shell function (defined later) */
typedef struct function_T function_T;


static void varvaluefree(variable_T *v)
    __attribute__((nonnull));
static void varfree(variable_T *v);
static void varkvfree(kvpair_T kv);
static void varkvfree_reexport(kvpair_T kv);

static void init_pwd(void);

static variable_T *search_variable(const wchar_t *name)
    __attribute__((pure,nonnull));
static variable_T *search_array_and_check_if_changeable(const wchar_t *name)
    __attribute__((pure,nonnull));
static void update_environment(const wchar_t *name)
    __attribute__((nonnull));
static void reset_locale(const wchar_t *name)
    __attribute__((nonnull));
static void reset_locale_category(const wchar_t *name, int category)
    __attribute__((nonnull));
static variable_T *new_global(const wchar_t *name)
    __attribute__((nonnull));
static variable_T *new_local(const wchar_t *name)
    __attribute__((nonnull));
static variable_T *new_temporary(const wchar_t *name)
    __attribute__((nonnull));
static variable_T *new_variable(const wchar_t *name, scope_T scope)
    __attribute__((nonnull));
static void xtrace_variable(const wchar_t *name, const wchar_t *value)
    __attribute__((nonnull));
static void xtrace_array(const wchar_t *name, void *const *values)
    __attribute__((nonnull));
static void get_all_variables_rec(hashtable_T *table, environ_T *env)
    __attribute__((nonnull));

static void lineno_getter(variable_T *var)
    __attribute__((nonnull));
static void random_getter(variable_T *var)
    __attribute__((nonnull));
static unsigned next_random(void);

static void variable_set(const wchar_t *name, variable_T *var)
    __attribute__((nonnull(1)));

static void reset_path(path_T name, variable_T *var);

static void funcfree(function_T *f);
static void funckvfree(kvpair_T kv);

static void hash_all_commands_recursively(const command_T *c);
static void hash_all_commands_in_and_or(const and_or_T *ao);
static void hash_all_commands_in_if(const ifcommand_T *ic);
static void hash_all_commands_in_case(const caseitem_T *ci);
static void tryhash_word_as_command(const wordunit_T *w);


/* the current environment */
static environ_T *current_env;
/* the top-level environment (the farthest from the current) */
static environ_T *first_env;

/* whether $RANDOM is functioning as a random number */
static bool random_active;

/* Hashtable from function names (char *) to functions (function_T *). */
static hashtable_T functions;

/* The index of the option charcter for the "getopts" builtin to parse next. */
static unsigned optind2 = 1;


/* Frees the value of a variable, but not the variable itself. */
/* This function does not change the value of `*v'. */
void varvaluefree(variable_T *v)
{
    switch (v->v_type & VF_MASK) {
	case VF_NORMAL:
	    free(v->v_value);
	    break;
	case VF_ARRAY:
	    recfree(v->v_vals, free);
	    break;
    }
}

/* Frees a variable. */
void varfree(variable_T *v)
{
    if (v) {
	varvaluefree(v);
	free(v);
    }
}

/* Frees a former variable environment entry. */
void varkvfree(kvpair_T kv)
{
    free(kv.key);
    varfree(kv.value);
}

/* Calls `variable_set' and `update_environment' for `kv.key' and
 * frees the variable. */
void varkvfree_reexport(kvpair_T kv)
{
    variable_set(kv.key, NULL);
    if (((variable_T *) kv.value)->v_type & VF_EXPORT)
	update_environment(kv.key);
    varkvfree(kv);
}

/* Initializes the top-level environment. */
void init_variables(void)
{
    first_env = current_env = xmalloc(sizeof *current_env);
    current_env->parent = NULL;
    current_env->is_temporary = false;
    ht_init(&current_env->contents, hashwcs, htwcscmp);
    for (size_t i = 0; i < PA_count; i++)
	current_env->paths[i] = NULL;

    ht_init(&functions, hashwcs, htwcscmp);

    /* add all the existing environment variables to the variable environment */
    for (char **e = environ; *e; e++) {
	wchar_t *we = malloc_mbstowcs(*e);
	if (!we)
	    continue;

	wchar_t *eqp = wcschr(we, L'=');
	variable_T *v = xmalloc(sizeof *v);
	v->v_type = VF_NORMAL | VF_EXPORT;
	v->v_value = eqp ? xwcsdup(eqp + 1) : NULL;
	v->v_getter = NULL;
	if (eqp) {
	    *eqp = L'\0';
	    we = xrealloc(we, sizeof *we * (eqp - we + 1));
	}
	varkvfree(ht_set(&current_env->contents, we, v));
    }

    /* set $IFS */
    set_variable(L VAR_IFS, xwcsdup(DEFAULT_IFS), SCOPE_GLOBAL, false);

    /* set $LINENO */
    {
	variable_T *v = new_variable(L VAR_LINENO, false);
	assert(v != NULL);
	v->v_type = VF_NORMAL | (v->v_type & VF_EXPORT);
	v->v_value = NULL;
	v->v_getter = lineno_getter;
	// variable_set(VAR_LINENO, v);
	if (v->v_type & VF_EXPORT)
	    update_environment(L VAR_LINENO);
    }

    /* set $MAILCHECK */
    if (!getvar(L VAR_MAILCHECK))
	set_variable(L VAR_MAILCHECK, xwcsdup(L"600"), SCOPE_GLOBAL, false);

    /* set $PS1~4 */
    {
	const wchar_t *ps1 =
	    !posixly_correct ? L"\\$ " : (geteuid() != 0) ? L"$ " : L"# ";
	set_variable(L VAR_PS1, xwcsdup(ps1),   SCOPE_GLOBAL, false);
	set_variable(L VAR_PS2, xwcsdup(L"> "), SCOPE_GLOBAL, false);
	if (!posixly_correct)
	    set_variable(L VAR_PS3, xwcsdup(L"#? "), SCOPE_GLOBAL, false);
	set_variable(L VAR_PS4, xwcsdup(L"+ "), SCOPE_GLOBAL, false);
    }

    /* set $PWD */
    init_pwd();

    /* export $OLDPWD */
    {
	variable_T *v = new_global(L VAR_OLDPWD);
	assert(v != NULL);
	v->v_type |= VF_EXPORT;
	variable_set(L VAR_OLDPWD, v);
    }

    /* set $PPID */
    set_variable(L VAR_PPID, malloc_wprintf(L"%jd", (intmax_t) getppid()),
	    SCOPE_GLOBAL, false);

    /* set $OPTIND */
    set_variable(L VAR_OPTIND, xwcsdup(L"1"), SCOPE_GLOBAL, false);

    /* set $RANDOM */
    if (!posixly_correct && !getvar(L VAR_RANDOM)) {
	variable_T *v = new_variable(L VAR_RANDOM, false);
	assert(v != NULL);
	v->v_type = VF_NORMAL;
	v->v_value = NULL;
	v->v_getter = random_getter;
	random_active = true;
	srand(shell_pid);
    } else {
	random_active = false;
    }

    /* set $YASH_VERSION */
    set_variable(L VAR_YASH_VERSION, xwcsdup(L PACKAGE_VERSION),
	    SCOPE_GLOBAL, false);

    /* initialize path according to $PATH/CDPATH */
    for (size_t i = 0; i < PA_count; i++)
	current_env->paths[i] = decompose_paths(getvar(path_variables[i]));
}

/* Reset the value of $PWD if
 *  - $PWD doesn't exist, or
 *  - the value of $PWD isn't an absolute path, or
 *  - the value of $PWD isn't the actual current directory, or
 *  - the value of $PWD isn't canonicalized. */
void init_pwd(void)
{
    const char *pwd = getenv(VAR_PWD);
    if (!pwd || pwd[0] != '/' || !is_same_file(pwd, "."))
	goto set;
    const wchar_t *wpwd = getvar(L VAR_PWD);
    if (!wpwd || !is_normalized_path(wpwd))
	goto set;
    return;

    char *newpwd;
    wchar_t *wnewpwd;
set:
    newpwd = xgetcwd();
    if (!newpwd) {
	xerror(errno, Ngt("cannot set $PWD"));
	return;
    }
    wnewpwd = realloc_mbstowcs(newpwd);
    if (!wnewpwd) {
	xerror(0, Ngt("cannot set $PWD"));
	return;
    }
    set_variable(L VAR_PWD, wnewpwd, SCOPE_GLOBAL, true);
}

/* Searches for a variable with the specified name.
 * Returns NULL if none found. */
variable_T *search_variable(const wchar_t *name)
{
    for (environ_T *env = current_env; env; env = env->parent) {
	variable_T *var = ht_get(&env->contents, name).value;
	if (var)
	    return var;
    }
    return NULL;
}

/* Searches for an array with the specified name and checks if it is not read
 * only. If unsuccessful, prints an error message and returns NULL. */
variable_T *search_array_and_check_if_changeable(const wchar_t *name)
{
    variable_T *array = search_variable(name);
    if (!array || (array->v_type & VF_MASK) != VF_ARRAY) {
	xerror(0, Ngt("%ls: no such array"), name);
	return NULL;
    } else if (array->v_type & VF_READONLY) {
	xerror(0, Ngt("%ls: readonly"), name);
	return NULL;
    }
    return array;
}

/* Update the value in `environ' for the variable with the specified name.
 * `name' must not contain '='. */
void update_environment(const wchar_t *name)
{
    char *mname = malloc_wcstombs(name);
    if (!mname)
	return;
    for (environ_T *env = current_env; env; env = env->parent) {
	variable_T *var = ht_get(&env->contents, name).value;
	if (var && (var->v_type & VF_EXPORT)) {
	    char *value;
	    switch (var->v_type & VF_MASK) {
		case VF_NORMAL:
		    if (!var->v_value)
			continue;
		    value = malloc_wcstombs(var->v_value);
		    break;
		case VF_ARRAY:
		    value = realloc_wcstombs(joinwcsarray(var->v_vals, L":"));
		    break;
		default:
		    assert(false);
	    }
	    if (value) {
		if (setenv(mname, value, true) < 0)
		    xerror(errno, Ngt("cannot set environment variable `%s'"),
			    mname);
		free(value);
	    } else {
		xerror(EILSEQ, Ngt("cannot set environment variable `%s'"),
			mname);
	    }
	    goto done;
	}
    }
    if (unsetenv(mname) < 0)
	xerror(errno, Ngt("cannot unset environment variable `%s'"), mname);
done:
    free(mname);
}

/* Resets the locate settings for the specified variable.
 * If `name' is not any of "LANG", "LC_ALL", etc., does nothing. */
void reset_locale(const wchar_t *name)
{
    if (wcscmp(name, L VAR_LANG) == 0) {
	goto reset_locale_all;
    } else if (wcsncmp(name, L"LC_", 3) == 0) {
	/* POSIX forbids resetting LC_CTYPE even if the value of the variable
	 * is changed, but we do reset LC_CTYPE if the shell is interactive and
	 * not in the POSIXly-correct mode. */
	if (wcscmp(name + 3, L VAR_LC_ALL + 3) == 0) {
reset_locale_all:
	    reset_locale_category(L VAR_LC_COLLATE, LC_COLLATE);
	    if (!posixly_correct && is_interactive_now)
		reset_locale_category(L VAR_LC_CTYPE, LC_CTYPE);
	    reset_locale_category(L VAR_LC_MESSAGES, LC_MESSAGES);
	    reset_locale_category(L VAR_LC_MONETARY, LC_MONETARY);
	    reset_locale_category(L VAR_LC_NUMERIC, LC_NUMERIC);
	    reset_locale_category(L VAR_LC_TIME, LC_TIME);
	} else if (wcscmp(name + 3, L VAR_LC_COLLATE + 3) == 0) {
	    reset_locale_category(L VAR_LC_COLLATE, LC_COLLATE);
	} else if (wcscmp(name + 3, L VAR_LC_CTYPE + 3) == 0) {
	    if (!posixly_correct && is_interactive_now)
		reset_locale_category(L VAR_LC_CTYPE, LC_CTYPE);
	} else if (wcscmp(name + 3, L VAR_LC_MESSAGES + 3) == 0) {
	    reset_locale_category(L VAR_LC_MESSAGES, LC_MESSAGES);
	} else if (wcscmp(name + 3, L VAR_LC_MONETARY + 3) == 0) {
	    reset_locale_category(L VAR_LC_MONETARY, LC_MONETARY);
	} else if (wcscmp(name + 3, L VAR_LC_NUMERIC + 3) == 0) {
	    reset_locale_category(L VAR_LC_NUMERIC, LC_NUMERIC);
	} else if (wcscmp(name + 3, L VAR_LC_TIME + 3) == 0) {
	    reset_locale_category(L VAR_LC_TIME, LC_TIME);
	}
    }
}

/* Resets the locale of the specified category.
 * `name' must be one of the `LC_*' constants except LC_ALL. */
void reset_locale_category(const wchar_t *name, int category)
{
    const wchar_t *v = getvar(L VAR_LC_ALL);
    if (!v) {
	v = getvar(name);
	if (!v) {
	    v = getvar(L VAR_LANG);
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

/* Creates a new scalar variable with the value unset.
 * If the variable already exists, it is returned without change.
 * So the return value may be an array variable.
 * Temporary variables with the `name' are cleared. */
variable_T *new_global(const wchar_t *name)
{
    variable_T *var;
    for (environ_T *env = current_env; env; env = env->parent) {
	var = ht_get(&env->contents, name).value;
	if (var) {
	    if (env->is_temporary) {
		varkvfree_reexport(ht_remove(&env->contents, name));
		continue;
	    }
	    return var;
	}
    }
    var = xmalloc(sizeof *var);
    var->v_type = VF_NORMAL;
    var->v_value = NULL;
    var->v_getter = NULL;
    ht_set(&first_env->contents, xwcsdup(name), var);
    return var;
}

/* Creates a new scalar variable with the value unset.
 * If the variable already exists, it is returned without change.
 * So the return value may be an array variable.
 * Temporary variables with the `name' are cleared. */
variable_T *new_local(const wchar_t *name)
{
    environ_T *env = current_env;
    while (env->is_temporary) {
	varkvfree_reexport(ht_remove(&env->contents, name));
	env = env->parent;
    }
    variable_T *var = ht_get(&env->contents, name).value;
    if (var)
	return var;
    var = xmalloc(sizeof *var);
    var->v_type = VF_NORMAL;
    var->v_value = NULL;
    var->v_getter = NULL;
    ht_set(&env->contents, xwcsdup(name), var);
    return var;
}

/* Creates a new scalar variable with the value unset.
 * If the variable already exists, it is returned without change.
 * So the return value may be an array variable.
 * The current environment must be a temporary environment.
 * If there is a readonly non-temporary variable with the specified name, it is
 * returned (no new temporary variable is created). */
variable_T *new_temporary(const wchar_t *name)
{
    environ_T *env = current_env;
    assert(env->is_temporary);

    /* check if readonly */
    variable_T *var = search_variable(name);
    if (var && (var->v_type & VF_READONLY))
	return var;

    var = ht_get(&env->contents, name).value;
    if (var)
	return var;
    var = xmalloc(sizeof *var);
    var->v_type = VF_NORMAL;
    var->v_value = NULL;
    var->v_getter = NULL;
    ht_set(&env->contents, xwcsdup(name), var);
    return var;
}

/* Creates a new variable if there is none.
 * If the variable already exists, it is cleared and returned.
 *
 * On error, NULL is returned. Otherwise a (new) variable is returned.
 * `v_type' is the only valid member of the variable and the all members
 * (including `v_type') must be initialized by the caller. If `v_type' of the
 * return value has the VF_EXPORT flag set, the caller must call
 * `update_environment'. */
variable_T *new_variable(const wchar_t *name, scope_T scope)
{
    variable_T *var;

    switch (scope) {
	case SCOPE_GLOBAL:  var = new_global(name);     break;
	case SCOPE_LOCAL:   var = new_local(name);      break;
	case SCOPE_TEMP:    var = new_temporary(name);  break;
	default:            assert(false);
    }
    if (var->v_type & VF_READONLY) {
	xerror(0, Ngt("%ls: readonly"), name);
	return NULL;
    } else {
	varvaluefree(var);
	return var;
    }
}

/* Creates a scalar variable with the specified name and value.
 * `value' must be a `free'able string or NULL. The caller must not modify or
 * `free' `value' hereafter, whether or not this function is successful.
 * If `export' is true, the variable is exported. `export' being false doesn't
 * mean the variable is no longer exported.
 * Returns true iff successful. */
bool set_variable(
	const wchar_t *name, wchar_t *value, scope_T scope, bool export)
{
    variable_T *var = new_variable(name, scope);
    if (!var) {
	free(value);
	return false;
    }

    var->v_type = VF_NORMAL
	| (var->v_type & (VF_EXPORT | VF_NODELETE))
	| (export ? VF_EXPORT : 0);
    var->v_value = value;
    var->v_getter = NULL;

    variable_set(name, var);
    if (var->v_type & VF_EXPORT)
	update_environment(name);
    return true;
}

/* Creates an array variable with the specified name and values.
 * `values' is a NULL-terminated array of pointers to wide strings. It is used
 * as the contents of the array variable hereafter, so you must not modify or
 * free the array or its elements whether or not this function succeeds.
 * `values' and its elements must be freeable.
 * `count' is the number of elements in `values'. If `count' is zero, the
 * number is counted in this function.
 * Returns true iff successful. */
bool set_array(const wchar_t *name, size_t count, void **values, scope_T scope)
{
    variable_T *var = new_variable(name, scope);
    if (!var) {
	recfree(values, free);
	return false;
    }

    var->v_type = VF_ARRAY | (var->v_type & (VF_EXPORT | VF_NODELETE));
    var->v_vals = values;
    var->v_valc = count ? count : plcount(var->v_vals);
    var->v_getter = NULL;

    variable_set(name, var);
    if (var->v_type & VF_EXPORT)
	update_environment(name);
    return true;
}

/* Changes the value of an element of an existing array.
 * `name' must be the name of an existing array.
 * `index' is the index of the element (counted from zero).
 * `value' is the new value, which must be a freeable string. Since `value' is
 * used as the contents of the array element, you must not modify or free
 * `value' after this function returned (whether successful or not).
 * Returns true iff successful. An error message is printed on failure. */
bool set_array_element(const wchar_t *name, size_t index, wchar_t *value)
{
    variable_T *array = search_array_and_check_if_changeable(name);
    if (!array)
	goto fail;
    if (array->v_valc <= index)
	goto invalid_index;

    free(array->v_vals[index]);
    array->v_vals[index] = value;
    if (array->v_type & VF_EXPORT)
	update_environment(name);
    return true;

invalid_index:
    xerror(0,
	    Ngt("%zu: index out of range (actual size of array `%ls' is %zu)"),
	    index + 1, name, array->v_valc);
fail:
    free(value);
    return false;
}

/* Sets the positional parameters of the current environment.
 * The existent parameters are cleared.
 * `values' is an NULL-terminated array of pointers to wide strings.
 * `values[0]' will be the new $1, `values[1]' $2, and so on.
 * When a new environment is created, this function must be called at least once
 * except for a temporary environment. */
void set_positional_parameters(void *const *values)
{
    set_array(L VAR_positional, 0, duparray(values, copyaswcs), SCOPE_LOCAL);
}

/* Performs assignments.
 * If `shopt_xtrace' is true, traces are printed to stderr.
 * If `temp' is true, the variables are assigned in the current environment,
 * whose `is_temporary' must be true. Otherwise they are assigned globally.
 * If `export' is true, the variables are exported. `export' being false doesn't
 * mean the variables are no longer exported.
 * Returns true iff successful. An error on an assignment may leave some other
 * variables assigned. */
bool do_assignments(const assign_T *assign, bool temp, bool export)
{
    if (temp)
	assert(current_env->is_temporary);

    scope_T scope = temp ? SCOPE_TEMP : SCOPE_GLOBAL;
    while (assign) {
	wchar_t *value;
	int count;
	void **values;

	switch (assign->a_type) {
	    case A_SCALAR:
		value = expand_single(assign->a_scalar, tt_multi);
		if (!value)
		    return false;
		value = unescapefree(value);
		if (shopt_xtrace)
		    xtrace_variable(assign->a_name, value);
		if (!set_variable(assign->a_name, value, scope, export))
		    return false;
		break;
	    case A_ARRAY:
		if (!expand_line(assign->a_array, &count, &values))
		    return false;
		assert(values != NULL);
		if (shopt_xtrace)
		    xtrace_array(assign->a_name, values);
		if (!set_array(assign->a_name, count, values, scope))
		    return false;
		break;
	}
	assign = assign->next;
    }
    return true;
}

/* Pushes a trace of an variable assignment to the xtrace buffer. */
void xtrace_variable(const wchar_t *name, const wchar_t *value)
{
    xwcsbuf_T *buf = get_xtrace_buffer();
    wb_wprintf(buf, L" %ls=%ls", name, value);
}

/* Pushes a trace of an array assignment to the xtrace buffer. */
void xtrace_array(const wchar_t *name, void *const *values)
{
    xwcsbuf_T *buf = get_xtrace_buffer();

    wb_wprintf(buf, L" %ls=(", name);
    if (*values) {
	for (;;) {
	    wb_cat(buf, *values);
	    values++;
	    if (!*values)
		break;
	    wb_wccat(buf, L' ');
	}
    }
    fputs(")", stderr);
}

/* Gets the value of the specified scalar variable.
 * Cannot be used for special parameters such as $$ and $@.
 * Returns the value of the variable, or NULL if not found.
 * The return value must not be modified or `free'ed by the caller and
 * is valid until the variable is re-assigned or unset. */
const wchar_t *getvar(const wchar_t *name)
{
    variable_T *var = search_variable(name);
    if (var && (var->v_type & VF_MASK) == VF_NORMAL) {
	if (var->v_getter) {
	    var->v_getter(var);
	    if ((var->v_type & VF_MASK) != VF_NORMAL)
		return NULL;
	}
	return var->v_value;
    }
    return NULL;
}

/* Returns the value(s) of the specified variable/array as an array.
 * The return value's type is `struct get_variable'. It has three members:
 * `type', `count' and `values'. `type' is the type of variable/array:
 *    GV_NOTFOUND:     no such variable/array
 *    GV_SCALAR:       a normal scalar variable
 *    GV_ARRAY:        an array of zero or more values
 *    GV_ARRAY_CONCAT: an array whose values should be concatenated by caller
 * `values' is the array containing the values of the variable/array.
 * A scalar value (GV_SCALAR) is returned as a newly-malloced array containing
 * exactly one newly-malloced wide string. An array (GV_ARRAY*) is returned as
 * a NULL-terminated array of pointers to wide strings, which must not be
 * changed or freed.  If no such variable is found (GV_NOTFOUND), the result
 * array will be NULL.
 * `count' is the number of elements in the `values'. */
struct get_variable get_variable(const wchar_t *name)
{
    struct get_variable result = {
	.type = GV_NOTFOUND, .count = 0, .values = NULL, };
    wchar_t *value;
    variable_T *var;

    if (!name[0])
	return result;
    if (!name[1]) {
	/* `name' is one-character long: check if it's a special parameter */
	switch (name[0]) {
	    case L'*':
		result.type = GV_ARRAY_CONCAT;
		goto positional_parameters;
	    case L'@':
		result.type = GV_ARRAY;
positional_parameters:
		var = search_variable(L VAR_positional);
		assert(var != NULL && (var->v_type & VF_MASK) == VF_ARRAY);
		result.count = var->v_valc;
		result.values = var->v_vals;
		return result;
	    case L'#':
		var = search_variable(L VAR_positional);
		assert(var != NULL && (var->v_type & VF_MASK) == VF_ARRAY);
		value = malloc_wprintf(L"%zu", var->v_valc);
		goto return_single;
	    case L'?':
		value = malloc_wprintf(L"%d", laststatus);
		goto return_single;
	    case L'-':
		value = get_hyphen_parameter();
		goto return_single;
	    case L'$':
		value = malloc_wprintf(L"%jd", (intmax_t) shell_pid);
		goto return_single;
	    case L'!':
		value = malloc_wprintf(L"%jd", (intmax_t) lastasyncpid);
		goto return_single;
	    case L'0':
		value = xwcsdup(command_name);
		goto return_single;
	}
    }

    if (iswdigit(name[0])) {
	/* `name' starts with a digit: a positional parameter */
	wchar_t *nameend;
	errno = 0;
	uintmax_t v = wcstoumax(name, &nameend, 10);
	if (errno || *nameend != L'\0')
	    return result;  /* not a number or overflow */
	var = search_variable(L VAR_positional);
	assert(var != NULL && (var->v_type & VF_MASK) == VF_ARRAY);
	if (v == 0 || var->v_valc < v)
	    return result;  /* index out of bounds */
	value = xwcsdup(var->v_vals[v - 1]);
	goto return_single;
    }

    /* now it should be a normal variable */
    var = search_variable(name);
    if (var) {
	if (var->v_getter)
	    var->v_getter(var);
	switch (var->v_type & VF_MASK) {
	    case VF_NORMAL:
		value = var->v_value ? xwcsdup(var->v_value) : NULL;
		goto return_single;
	    case VF_ARRAY:
		result.type = GV_ARRAY;
		result.count = var->v_valc;
		result.values = var->v_vals;
		return result;
	}
    }
    return result;

return_single:  /* return a scalar as a one-element array */
    if (!value)
	return result;
    result.type = GV_SCALAR;
    result.count = 1;
    result.values = xmalloc(2 * sizeof *result.values);
    result.values[0] = value;
    result.values[1] = NULL;
    return result;
}

/* Gathers all variables in the specified environment and all of its ancestors
 * into the specified hashtable.
 * Local variables may hide global variables.
 * keys and values added to the hashtable must not be modified or freed. */
void get_all_variables_rec(hashtable_T *table, environ_T *env)
{
    if (env->parent)
	get_all_variables_rec(table, env->parent);

    size_t i = 0;
    kvpair_T kv;

    while ((kv = ht_next(&env->contents, &i)).key)
	ht_set(table, kv.key, kv.value);
}

/* Creates a new variable environment.
 * `temp' specifies whether the new environment is for temporary assignments.
 * The current environment will be the parent of the new environment.
 * Don't forget to call `set_positional_parameters'. */
void open_new_environment(bool temp)
{
    environ_T *newenv = xmalloc(sizeof *newenv);

    newenv->parent = current_env;
    newenv->is_temporary = temp;
    ht_init(&newenv->contents, hashwcs, htwcscmp);
    for (size_t i = 0; i < PA_count; i++)
	newenv->paths[i] = NULL;
    current_env = newenv;
}

/* Closes the current variable environment.
 * The parent of the current becomes the new current. */
void close_current_environment(void)
{
    environ_T *oldenv = current_env;

    assert(oldenv != first_env);
    current_env = oldenv->parent;
    ht_clear(&oldenv->contents, varkvfree_reexport);
    ht_destroy(&oldenv->contents);
    for (size_t i = 0; i < PA_count; i++)
	recfree((void **) oldenv->paths[i], free);
    free(oldenv);
}


/********** Getters **********/

/* line number of the currently executing command */
unsigned long current_lineno;

/* getter for $LINENO */
void lineno_getter(variable_T *var)
{
    assert((var->v_type & VF_MASK) == VF_NORMAL);
    free(var->v_value);
    var->v_value = malloc_wprintf(L"%lu", current_lineno);
    // variable_set(VAR_LINENO, var);
    if (var->v_type & VF_EXPORT)
	update_environment(L VAR_LINENO);
}

/* getter for $RANDOM */
void random_getter(variable_T *var)
{
    assert((var->v_type & VF_MASK) == VF_NORMAL);
    free(var->v_value);
    var->v_value = malloc_wprintf(L"%u", next_random());
    // variable_set(VAR_RANDOM, var);
    if (var->v_type & VF_EXPORT)
	update_environment(L VAR_RANDOM);
}

/* Returns a random number between 0 and 32767 using `rand'. */
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


/********** Setter **********/

/* general callback that is called after an assignment.
 * `var' is NULL when the variable is unset. */
void variable_set(const wchar_t *name, variable_T *var)
{
    switch (name[0]) {
    case L'C':
	if (wcscmp(name, L VAR_CDPATH) == 0)
	    reset_path(PA_CDPATH, var);
	break;
    case L'L':
	if (wcscmp(name, L VAR_LANG) == 0 || wcsncmp(name, L"LC_", 3) == 0)
	    reset_locale(name);
	break;
    case L'M':
	if (wcscmp(name, L VAR_MAILPATH) == 0)
	    reset_path(PA_MAILPATH, var);
	break;
    case L'O':
	if (wcscmp(name, L VAR_OPTIND) == 0)
	    optind2 = 1;
	break;
    case L'P':
	if (wcscmp(name, L VAR_PATH) == 0) {
	    clear_cmdhash();
	    reset_path(PA_PATH, var);
	}
	break;
    case L'R':
	if (random_active && wcscmp(name, L VAR_RANDOM) == 0) {
	    random_active = false;
	    if (var && (var->v_type & VF_MASK) == VF_NORMAL && var->v_value) {
		unsigned long seed;
		if (xwcstoul(var->v_value, 0, &seed)) {
		    srand((unsigned) seed);
		    var->v_getter = random_getter;
		    random_active = true;
		}
	    }
	}
	break;
#if YASH_ENABLE_LINEEDIT
    case L'T':
	if (wcscmp(name, L VAR_TERM) == 0)
	    le_need_term_update = true;
	break;
#endif /* YASH_ENABLE_LINEEDIT */
    }
}


/********** Path Array Manipulation **********/

/* Splits the specified string at colons.
 * If `paths' is non-NULL, a newly-malloced NULL-terminated array of pointers to
 * newly-malloced multibyte strings is returned.
 * If `paths' is NULL, NULL is returned. */
char **decompose_paths(const wchar_t *paths)
{
    if (!paths)
	return NULL;

    wchar_t wpath[wcslen(paths) + 1];
    wcscpy(wpath, paths);

    plist_T list;
    pl_init(&list);

    /* add each element to `list', replacing each L':' with L'\0' in `wpath'. */
    pl_add(&list, wpath);
    for (wchar_t *w = wpath; *w; w++) {
	if (*w == L':') {
	    *w = L'\0';
	    pl_add(&list, w + 1);
	}
    }

    /* remove duplicates */
    for (size_t i = 0; i < list.length; i++)
	for (size_t j = list.length; --j > i; )
	    if (wcscmp(list.contents[i], list.contents[j]) == 0)
		pl_remove(&list, j, 1);

    /* convert each element back to multibyte string */
    for (size_t i = 0; i < list.length; i++) {
	list.contents[i] = malloc_wcstombs(list.contents[i]);
	/* We actually assert this conversion always succeeds, but... */
	if (!list.contents[i])
	    list.contents[i] = xstrdup("");
    }

    return (char **) pl_toary(&list);
}

/* Reconstructs the path array of the specified variable in the environment.
 * `var' may be NULL. */
void reset_path(path_T name, variable_T *var)
{
    for (environ_T *env = current_env; env; env = env->parent) {
	recfree((void **) env->paths[name], free);

	variable_T *v = ht_get(&env->contents, path_variables[name]).value;
	if (v) {
	    plist_T list;
	    switch (v->v_type & VF_MASK) {
		case VF_NORMAL:
		    env->paths[name] = decompose_paths(v->v_value);
		    break;
		case VF_ARRAY:
		    pl_initwithmax(&list, v->v_valc);
		    for (size_t i = 0; i < v->v_valc; i++) {
			char *p = malloc_wcstombs(v->v_vals[i]);
			if (!p)
			    p = xstrdup("");
			pl_add(&list, p);
		    }
		    env->paths[name] = (char **) pl_toary(&list);
		    break;
	    }
	    if (v == var)
		break;
	} else {
	    env->paths[name] = NULL;
	}
    }
}

/* Returns the path array of the specified variable.
 * The return value is NULL if the variable is not set.
 * The caller must not make any change to the returned array. */
char *const *get_path_array(path_T name)
{
    for (environ_T *env = current_env; env != NULL; env = env->parent)
	if (env->paths[name])
	    return env->paths[name];
    return NULL;
}


/********** Shell Functions **********/

/* function */
struct function_T {
    vartype_T  f_type;  /* only VF_READONLY and VF_NODELETE are valid */
    command_T *f_body;  /* body of function */
};

void funcfree(function_T *f)
{
    if (f) {
	comsfree(f->f_body);
	free(f);
    }
}

void funckvfree(kvpair_T kv)
{
    free(kv.key);
    funcfree(kv.value);
}

/* Defines a function.
 * It is an error to re-define a readonly function.
 * Returns true iff successful. */
bool define_function(const wchar_t *name, command_T *body)
{
    function_T *f = ht_get(&functions, name).value;
    if (f != NULL && (f->f_type & VF_READONLY)) {
	xerror(0, Ngt("cannot re-define readonly function `%ls'"), name);
	return false;
    }

    f = xmalloc(sizeof *f);
    f->f_type = 0;
    f->f_body = comsdup(body);
    if (shopt_hashondef)
	hash_all_commands_recursively(body);
    funckvfree(ht_set(&functions, xwcsdup(name), f));
    return true;
}

/* Gets the body of the function with the specified name.
 * Returns NULL if there is no such a function. */
command_T *get_function(const wchar_t *name)
{
    function_T *f = ht_get(&functions, name).value;
    if (f)
	return f->f_body;
    else
	return NULL;
}


/* Registers all the commands in the argument to the command hashtable. */
void hash_all_commands_recursively(const command_T *c)
{
    for (; c; c = c->next) {
	switch (c->c_type) {
	    case CT_SIMPLE:
		if (c->c_words)
		    tryhash_word_as_command(c->c_words[0]);
		break;
	    case CT_GROUP:
	    case CT_SUBSHELL:
		hash_all_commands_in_and_or(c->c_subcmds);
		break;
	    case CT_IF:
		hash_all_commands_in_if(c->c_ifcmds);
		break;
	    case CT_FOR:
		hash_all_commands_in_and_or(c->c_forcmds);
		break;
	    case CT_WHILE:
		hash_all_commands_in_and_or(c->c_whlcond);
		hash_all_commands_in_and_or(c->c_whlcmds);
		break;
	    case CT_CASE:
		hash_all_commands_in_case(c->c_casitems);
		break;
	    case CT_FUNCDEF:
		break;
	}
    }
}

void hash_all_commands_in_and_or(const and_or_T *ao)
{
    for (; ao; ao = ao->next) {
	for (pipeline_T *p = ao->ao_pipelines; p; p = p->next) {
	    hash_all_commands_recursively(p->pl_commands);
	}
    }
}

void hash_all_commands_in_if(const ifcommand_T *ic)
{
    for (; ic; ic = ic->next) {
	hash_all_commands_in_and_or(ic->ic_condition);
	hash_all_commands_in_and_or(ic->ic_commands);
    }
}

void hash_all_commands_in_case(const caseitem_T *ci)
{
    for (; ci; ci = ci->next) {
	hash_all_commands_in_and_or(ci->ci_commands);
    }
}

void tryhash_word_as_command(const wordunit_T *w)
{
    if (w && !w->next && w->wu_type == WT_STRING) {
	wchar_t *cmdname = unquote(w->wu_string);
	if (wcschr(cmdname, L'/') == NULL) {
	    char *mbsname = malloc_wcstombs(cmdname);
	    get_command_path(mbsname, false);
	    free(mbsname);
	}
	free(cmdname);
    }
}


/********** Directory Stack **********/

#if YASH_ENABLE_DIRSTACK

static variable_T *get_dirstack(void);

/* Returns the directory stack.
 * If the stack is not yet created, it is initialized as an empty stack and
 * returned.
 * Fails if a non-array variable or a readonly array already exists, in which
 * case NULL is returned. */
variable_T *get_dirstack(void)
{
    variable_T *var = search_variable(L VAR_DIRSTACK);
    if (var) {
	if (((var->v_type & VF_MASK) == VF_ARRAY)
		&& !(var->v_type & VF_READONLY))
	    return var;
	else
	    return NULL;
    }

    void **ary = xmalloc(1 * sizeof *ary);
    ary[0] = NULL;
    if (set_array(L VAR_DIRSTACK, 0, ary, SCOPE_GLOBAL)) {
	var = search_variable(L VAR_DIRSTACK);
	assert(var != NULL);
	assert((var->v_type & VF_MASK) == VF_ARRAY);
	assert(!(var->v_type & VF_READONLY));
	return var;
    } else {
	return NULL;
    }
}

/* Returns the number of elements in $DIRSTACK. */
size_t get_dirstack_size(void)
{
    variable_T *var = search_variable(L VAR_DIRSTACK);
    if (!var || ((var->v_type & VF_MASK) != VF_ARRAY))
	return 0;
    return var->v_valc;
}

/* Parses a string as the index of a directory stack entry.
 * The index string must start with the plus or minus sign.
 * If the corresponding entry is found, the entry is assigned to `*entryp' and
 * the index value is assigned to `*indexp'. If the index is out of range, it is
 * an error. If the string is not a valid integer, `indexstr' is assigned to
 * `*entryp' and SIZE_MAX is assigned to `*indexp'.
 * If the index denotes $PWD, the number of the entries in $DIRSTACK is assigned
 * to `*indexp'.
 * Returns true if successful (`*entryp' and `*indexp' are assigned to).
 * Returns false on error, in which case an error message is printed if
 * `printerror' is true. */
bool parse_dirstack_index(const wchar_t *indexstr,
	size_t *indexp, const wchar_t **entryp, bool printerror)
{
    long num;

    if (indexstr[0] != L'-' && indexstr[0] != L'+')
	goto not_index;
    if (!xwcstol(indexstr, 10, &num))
	goto not_index;

    variable_T *var = search_variable(L VAR_DIRSTACK);
    if (!var || ((var->v_type & VF_MASK) != VF_ARRAY)) {
	if (num == 0)
	    goto return_pwd;
	if (printerror)
	    xerror(0, Ngt("directory stack is empty"));
	return false;
    }
#if LONG_MAX > SIZE_MAX
    if (num > SIZE_MAX)
	goto out_of_range;
#endif
    if (indexstr[0] == L'+' && num >= 0) {
	if (num == 0) {
	    num = var->v_valc;
	    goto return_pwd;
	}
	if ((size_t) num > var->v_valc)
	    goto out_of_range;
	*indexp = var->v_valc - (size_t) num;
	*entryp = var->v_vals[*indexp];
	return true;
    } else if (indexstr[0] == L'-' && num <= 0) {
	if (num == LONG_MIN)
	    goto out_of_range;
	num = -num;
	assert(num >= 0);
	if ((size_t) num > var->v_valc)
	    goto out_of_range;
	if ((size_t) num == var->v_valc)
	    goto return_pwd;
	*indexp = (size_t) num;
	*entryp = var->v_vals[*indexp];
	return true;
    } else {
	goto not_index;
    }

    const wchar_t *pwd;
return_pwd:
    pwd = getvar(L VAR_PWD);
    if (!pwd) {
	if (printerror)
	    xerror(0, Ngt("$PWD not set"));
	return false;
    }
    *entryp = pwd;
    *indexp = (size_t) num;
    return true;
not_index:
    *entryp = indexstr;
    *indexp = SIZE_MAX;
    return true;
out_of_range:
    if (printerror)
	xerror(0, Ngt("%ls: index out of range"), indexstr);
    return false;
}

/* Pushs `value' to the directory stack ($DIRSTACK), which is created if none.
 * `value' is freed in this function. Returns true iff successful. */
bool push_dirstack(wchar_t *value)
{
    variable_T *var = get_dirstack();
    if (!var) {
	free(value);
	xerror(0, Ngt("cannot set directory stack"));
	return false;
    }

    size_t index = var->v_valc++;
    var->v_vals = xrealloc(var->v_vals, (index + 2) * sizeof *var->v_vals);
    var->v_vals[index] = value;
    var->v_vals[index + 1] = NULL;
    return true;
}

/* Removes the newest entry of the directory stack ($DIRSTACK) and returns it.
 * The return value must be freed by the caller.
 * Returns NULL on error. */
wchar_t *pop_dirstack(void)
{
    variable_T *var = search_variable(L VAR_DIRSTACK);
    if (!var || ((var->v_type & VF_MASK) != VF_ARRAY)
	    || (var->v_type & VF_READONLY) || (var->v_valc == 0))
	return NULL;

    wchar_t *result = var->v_vals[--var->v_valc];
    var->v_vals[var->v_valc] = NULL;
    return result;
}

/* Removes the specified entry of the directory stack.
 * Returns true iff the entry is successfully removed or no such entry is found.
 */
bool remove_dirstack_entry(size_t index)
{
    variable_T *var = search_variable(L VAR_DIRSTACK);
    if (!var || ((var->v_type & VF_MASK) != VF_ARRAY))
	return true;
    if (var->v_type & VF_READONLY) {
	xerror(0, Ngt("cannot set directory stack"));
	return false;
    }

    if (index < var->v_valc) {
	free(var->v_vals[index]);
	memmove(var->v_vals + index, var->v_vals + index + 1,
		(var->v_valc - index) * sizeof *var->v_vals);
	var->v_valc--;
    }
    return true;
}

#endif /* YASH_ENABLE_DIRSTACK */


/********** Builtins **********/

static bool print_variable(
	const wchar_t *name, const variable_T *var,
	const wchar_t *argv0, bool readonly, bool export)
    __attribute__((nonnull));
static bool print_function(
	const wchar_t *name, const function_T *func,
	const wchar_t *argv0, bool readonly)
    __attribute__((nonnull));
#if YASH_ENABLE_ARRAY
static bool array_remove_elements(
	variable_T *array, size_t count, void *const *indexwcss)
    __attribute__((nonnull));
static int compare_long(const void *lp1, const void *lp2)
    __attribute__((nonnull,pure));
static bool array_insert_elements(
	variable_T *array, size_t count, void *const *values)
    __attribute__((nonnull));
static bool array_set_element(const wchar_t *name, variable_T *array,
	const wchar_t *indexword, const wchar_t *value)
    __attribute__((nonnull));
#endif /* YASH_ENABLE_ARRAY */
static bool unset_function(const wchar_t *name)
    __attribute__((nonnull));
static bool unset_variable(const wchar_t *name)
    __attribute__((nonnull));
static bool check_options(const wchar_t *options)
    __attribute__((nonnull,pure));
static inline bool set_optind(unsigned long optind);
static inline bool set_optarg(const wchar_t *value);
static bool set_to(const wchar_t *varname, wchar_t value)
    __attribute__((nonnull));
static bool read_input(xwcsbuf_T *buf, bool noescape)
    __attribute__((nonnull));
static bool split_and_assign_array(const wchar_t *name, wchar_t *values,
	const wchar_t *ifs, bool raw)
    __attribute__((nonnull));

/* The "typeset" builtin, which accepts the following options:
 *  -f: affect functions rather than variables
 *  -g: global
 *  -p: print variables
 *  -r: make variables readonly
 *  -x: export variables
 *  -X: cancel exportation of variables
 * Equivalent builtins:
 *  export:   typeset -gx
 *  readonly: typeset -gr
 * The "set" builtin without any arguments is redirected to this builtin. */
int typeset_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"functions", xno_argument, L'f', },
	{ L"global",    xno_argument, L'g', },
	{ L"print",     xno_argument, L'p', },
	{ L"readonly",  xno_argument, L'r', },
	{ L"export",    xno_argument, L'x', },
	{ L"unexport",  xno_argument, L'X', },
	{ L"help",      xno_argument, L'-', },
	{ NULL, 0, 0, },
    };

    wchar_t opt;
    bool funcs = false, global = false, print = false;
    bool readonly = false, export = false, unexport = false;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, posixly_correct ? L"p" : L"fgprxX",
		    long_options, NULL))) {
	switch (opt) {
	    case L'f':  funcs    = true;  break;
	    case L'g':  global   = true;  break;
	    case L'p':  print    = true;  break;
	    case L'r':  readonly = true;  break;
	    case L'x':  export   = true;  break;
	    case L'X':  unexport = true;  break;
	    case L'-':
		return print_builtin_help(ARGV(0));
	    default:
		fprintf(stderr, gt(posixly_correct
			    ? Ngt("Usage:  %ls [-p] [name[=value]...]\n")
			    : Ngt("Usage:  %ls [-fgprxX] [name[=value]...]\n")),
			ARGV(0));
		SPECIAL_BI_ERROR;
		return Exit_ERROR;
	}
    }

    if (wcscmp(ARGV(0), L"export") == 0) {
	global = true;
	if (!unexport)
	    export = true;
    } else if (wcscmp(ARGV(0), L"readonly") == 0) {
	global = readonly = true;
    } else {
	assert(wcscmp(ARGV(0), L"typeset") == 0
	    || wcscmp(ARGV(0), L"set") == 0);
    }

    if (funcs && (export || unexport)) {
	xerror(0, Ngt("functions cannot be exported"));
	SPECIAL_BI_ERROR;
	return Exit_ERROR;
    } else if (export && unexport) {
	xerror(0, Ngt("-x and -X cannot be used at a time"));
	SPECIAL_BI_ERROR;
	return Exit_ERROR;
    }

    bool ok = true;
    if (xoptind == argc) {
	kvpair_T *kvs;
	size_t count;

	if (!funcs) {
	    /* print all variables */
	    hashtable_T table;

	    ht_init(&table, hashwcs, htwcscmp);
	    get_all_variables_rec(&table, current_env);
	    kvs = ht_tokvarray(&table);
	    count = table.count;
	    ht_destroy(&table);
	    qsort(kvs, count, sizeof *kvs, keywcscoll);
	    for (size_t i = 0; ok && i < count; i++)
		ok &= print_variable(
			kvs[i].key, kvs[i].value, ARGV(0), readonly, export);
	} else {
	    /* print all functions */
	    kvs = ht_tokvarray(&functions);
	    count = functions.count;
	    qsort(kvs, count, sizeof *kvs, keywcscoll);
	    for (size_t i = 0; ok && i < count; i++)
		ok &= print_function(
			kvs[i].key, kvs[i].value, ARGV(0), readonly);
	}
	free(kvs);
    } else {
	do {
	    wchar_t *arg = ARGV(xoptind);
	    wchar_t *wequal = wcschr(arg, L'=');
	    if (wequal)
		*wequal = L'\0';
	    if (funcs) {
		if (wequal) {
		    xerror(0, Ngt("cannot assign function"));
		    ok = false;
		    continue;
		}

		function_T *f = ht_get(&functions, arg).value;
		if (f) {
		    if (readonly)
			f->f_type |= VF_READONLY | VF_NODELETE;
		    if (print)
			ok &= print_function(arg, f, ARGV(0), readonly);
		} else {
		    xerror(0, Ngt("%ls: no such function"), arg);
		    ok = false;
		    continue;
		}
	    } else if (wequal || !print) {
		/* create/assign variable */
		variable_T *var = global ? new_global(arg) : new_local(arg);
		vartype_T saveexport = var->v_type & VF_EXPORT;
		if (wequal) {
		    if (var->v_type & VF_READONLY) {
			xerror(0, Ngt("%ls: readonly"), arg);
			ok = false;
		    } else {
			varvaluefree(var);
			var->v_type = VF_NORMAL | (var->v_type & ~VF_MASK);
			var->v_value = xwcsdup(wequal + 1);
			var->v_getter = NULL;
		    }
		}
		if (readonly)
		    var->v_type |= VF_READONLY | VF_NODELETE;
		if (export)
		    var->v_type |= VF_EXPORT;
		else if (unexport)
		    var->v_type &= ~VF_EXPORT;
		variable_set(arg, var);
		if (saveexport != (var->v_type & VF_EXPORT)
			|| (wequal && (var->v_type & VF_EXPORT)))
		    update_environment(arg);
	    } else {
		/* print the variable */
		variable_T *var = search_variable(arg);
		if (var) {
		    ok &= print_variable(arg, var, ARGV(0), readonly, export);
		} else {
		    xerror(0, Ngt("%ls: no such variable"), arg);
		    ok = false;
		    continue;
		}
	    }
	} while (++xoptind < argc);
    }

    return ok ? Exit_SUCCESS : Exit_FAILURE;
}

/* Prints the specified variable to stdout.
 * This function does not print special variables whose name begins with an '='.
 * If `readonly'/`export' is true, the variable is printed only if it is
 * readonly/exported. The `name' is quoted if `is_name(name)' is not true.
 * Returns true iff successful (no error). */
bool print_variable(
	const wchar_t *name, const variable_T *var,
	const wchar_t *argv0, bool readonly, bool export)
{
    bool ok = true;
    wchar_t *qname = NULL;

    if (name[0] == L'=')
	return ok;
    if (readonly && !(var->v_type & VF_READONLY))
	return ok;
    if (export && !(var->v_type & VF_EXPORT))
	return ok;

    if (!is_name(name))
	name = qname = quote_sq(name);
    switch (var->v_type & VF_MASK) {
	case VF_NORMAL:  goto print_variable;
	case VF_ARRAY:   goto print_array;
    }

print_variable:;
    wchar_t *qvalue = var->v_value ? quote_sq(var->v_value) : NULL;
    xstrbuf_T opts;
    switch (argv0[0]) {
	case L's':
	    assert(wcscmp(argv0, L"set") == 0);
	    if (!qname && qvalue) {
		if (printf("%ls=%ls\n", name, qvalue) < 0) {
		    xerror(errno, Ngt("cannot print to standard output"));
		    ok = false;
		}
	    }
	    break;
	case L'e':
	case L'r':
	    assert(wcscmp(argv0, L"export") == 0
		    || wcscmp(argv0, L"readonly") == 0);
	    if (printf(qvalue ? "%ls %ls=%ls\n" : "%ls %ls\n",
			argv0, name, qvalue) < 0) {
		xerror(errno, Ngt("cannot print to standard output"));
		ok = false;
	    }
	    break;
	case L't':
	    assert(wcscmp(argv0, L"typeset") == 0);
	    sb_init(&opts);
	    if (var->v_type & VF_EXPORT)
		sb_ccat(&opts, 'x');
	    if (var->v_type & VF_READONLY)
		sb_ccat(&opts, 'r');
	    if (opts.length > 0)
		sb_insert(&opts, 0, " -");
	    if (printf(qvalue ? "%ls%s %ls=%ls\n" : "%ls%s %ls\n",
			argv0, opts.contents, name, qvalue) < 0) {
		xerror(errno, Ngt("cannot print to standard output"));
		ok = false;
	    }
	    sb_destroy(&opts);
	    break;
	default:
	    assert(false);
    }
    free(qvalue);
    free(qname);
    return ok;

print_array:
    clearerr(stdout);
    printf("%ls=(", name);
    for (void **values = var->v_vals; *values; values++) {
	wchar_t *qvalue = quote_sq(*values);
	printf("%ls", qvalue);
	free(qvalue);
	if (*(values + 1))
	    printf(" ");
    }
    printf(")\n");
    switch (argv0[0]) {
	case L'a':
	    assert(wcscmp(argv0, L"array") == 0);
	    break;
	case L's':
	    assert(wcscmp(argv0, L"set") == 0);
	    break;
	case L'e':
	case L'r':
	    assert(wcscmp(argv0, L"export") == 0
		    || wcscmp(argv0, L"readonly") == 0);
	    printf("%ls %ls\n", argv0, name);
	    break;
	case L't':
	    assert(wcscmp(argv0, L"typeset") == 0);
	    sb_init(&opts);
	    if (var->v_type & VF_EXPORT)
		sb_ccat(&opts, 'x');
	    if (var->v_type & VF_READONLY)
		sb_ccat(&opts, 'r');
	    if (opts.length > 0)
		sb_insert(&opts, 0, " -");
	    printf("%ls%s %ls\n", argv0, opts.contents, name);
	    sb_destroy(&opts);
	    break;
	default:
	    assert(false);
    }
    free(qname);
    if (!ferror(stdout)) {
	return true;
    } else {
	xerror(0, Ngt("cannot print to standard output"));
	return false;
    }
}

bool print_function(
	const wchar_t *name, const function_T *func,
	const wchar_t *argv0, bool readonly)
{
    if (readonly && !(func->f_type & VF_READONLY))
	return true;

    wchar_t *qname = NULL;
    if (!is_name(name))
	name = qname = quote_sq(name);

    wchar_t *value = command_to_wcs(func->f_body);
    clearerr(stdout);
    printf("%ls () %ls\n", name, value);
	// XXX need "function" keyword for quoted names
    free(value);

    switch (argv0[0]) {
	case L'r':
	    assert(wcscmp(argv0, L"readonly") == 0);
	    if (func->f_type & VF_READONLY)
		printf("%ls -f %ls\n", argv0, name);
	    break;
	case L't':
	    assert(wcscmp(argv0, L"typeset") == 0);
	    if (func->f_type & VF_READONLY)
		printf("%ls -fr %ls\n", argv0, name);
	    break;
	default:
	    assert(false);
    }
    free(qname);
    if (!ferror(stdout)) {
	return true;
    } else {
	xerror(0, Ngt("cannot print to standard output"));
	return false;
    }
}

const char typeset_help[] = Ngt(
"typeset, export, readonly - set or print variables\n"
"\ttypeset  [-fgprxX] [name[=value]...]\n"
"\texport   [-prX]    [name[=value]...]\n"
"\treadonly [-fpxX]   [name[=value]...]\n"
"For each operands of the form <name>, the variable of the specified name is\n"
"created if not yet created, without assigning any value. If the -p (--print)\n"
"option is specified, the current value and attributes of the variable is\n"
"printed instead of creating the variable.\n"
"For each operands of the form <name=value>, the value is assigned to the\n"
"specified variable. The variable is created if necessary.\n"
"If no operands are given, all variables are printed.\n"
"\n"
"By default, the \"typeset\" builtin affects local variables. To declare\n"
"global variables inside functions, the -g (--global) option can be used.\n"
"The -r (--readonly) option makes the specified variables/functions readonly.\n"
"The -x (--export) option makes the variables exported to external commands.\n"
"The -X (--unexport) option undoes the exportation.\n"
"The -f (--functions) option can be used to specify functions instead of\n"
"variables. Functions cannot be assigned or exported with these builtins: the\n"
"-f option can only be used together with the -r or -p option to make\n"
"functions readonly or print them. Functions cannot be exported.\n"
"\n"
"\"export\" is equivalent to \"typeset -gx\".\n"
"\"readonly\" is equivalent to \"typeset -gr\".\n"
"Note that the typeset builtin is unavailable in the POSIXly correct mode.\n"
);

#if YASH_ENABLE_ARRAY

/* The "array" builtin */
int array_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"delete", xno_argument, L'd', },
	{ L"insert", xno_argument, L'i', },
	{ L"set",    xno_argument, L's', },
	{ L"help",   xno_argument, L'-', },
	{ NULL, 0, 0, },
    };

    enum {
	delete = 1 << 0,
	insert = 1 << 1,
	set    = 1 << 2,
    } options = 0;

    wchar_t opt;
    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"-dis", long_options, NULL))) {
	switch (opt) {
	    case L'd':  options |= delete;  break;
	    case L'i':  options |= insert;  break;
	    case L's':  options |= set;     break;
	    case L'-':
		return print_builtin_help(ARGV(0));
	    default:  print_usage:
		fprintf(stderr, gt("Usage:  array [name [value...]]\n"
		                   "        array -d name [index...]\n"
				   "        array -i name index [value...]\n"
				   "        array -s name index value\n"));
		return Exit_ERROR;
	}
    }
    if (options && (options & (options - 1))) {
	xerror(0, Ngt("more than one option cannot be used at a time"));
	return Exit_ERROR;
    }

    bool ok = true;
    if (xoptind == argc) {
	/* print all arrays */
	if (options != 0)
	    goto print_usage;

	hashtable_T table;
	ht_init(&table, hashwcs, htwcscmp);
	get_all_variables_rec(&table, current_env);

	kvpair_T *kvs = ht_tokvarray(&table);
	size_t count = table.count;
	ht_destroy(&table);
	qsort(kvs, count, sizeof *kvs, keywcscoll);
	for (size_t i = 0; ok && i < count; i++)
	    if ((((variable_T *) kvs[i].value)->v_type & VF_MASK) == VF_ARRAY)
		ok &= print_variable(
			kvs[i].key, kvs[i].value, ARGV(0), false, false);
	free(kvs);
    } else {
	const wchar_t *name = ARGV(xoptind++);
	if (wcschr(name, L'=')) {
	    xerror(0, Ngt("`%ls': invalid name"), name);
	    return Exit_FAILURE;
	}

	if (options == 0) {
	    ok = set_array(name, argc - xoptind,
		    duparray(argv + xoptind, copyaswcs), SCOPE_GLOBAL);
	} else {
	    variable_T *array = search_array_and_check_if_changeable(name);
	    if (!array) {
		return Exit_FAILURE;
	    }
	    switch (options) {
		case delete:
		    ok = array_remove_elements(
			    array, argc - xoptind, argv + xoptind);
		    break;
		case insert:
		    if (xoptind == argc)
			goto print_usage;
		    ok = array_insert_elements(
			    array, argc - xoptind, argv + xoptind);
		    break;
		case set:
		    if (xoptind + 2 != argc)
			goto print_usage;
		    ok = array_set_element(
			    name, array, ARGV(xoptind), ARGV(xoptind + 1));
		    break;
		default:
		    assert(false);
	    }
	}
    }
    return ok ? Exit_SUCCESS : Exit_FAILURE;
}

#if LONG_MAX < SIZE_MAX
# define LONG_LT_SIZE(longvalue,sizevalue) \
    ((size_t) (longvalue) < (sizevalue))
#else
# define LONG_LT_SIZE(longvalue,sizevalue) \
    ((longvalue) < (long) (sizevalue))
#endif

/* Removes elements from `array'.
 * `*indexwcss' is an NULL-terminated array of pointers to wide strings,
 * which are parsed as indices of elements to be removed.
 * `count' is the number of elements in `*indexwcss'.
 * Returns true iff successful. */
bool array_remove_elements(
	variable_T *array, size_t count, void *const *indexwcss)
{
    long indices[count], lastindex;
    plist_T list;

    assert((array->v_type & VF_MASK) == VF_ARRAY);

    /* convert all the strings into long integers */
    for (size_t i = 0; i < count; i++) {
	const wchar_t *indexwcs = indexwcss[i];
	if (!xwcstol(indexwcs, 10, &indices[i])) {
	    xerror(errno, Ngt("`%ls' is not a valid integer"), indexwcs);
	    return false;
	}
    }

    /* sort all the indices and remove elements in the descending order so that
     * an earlier removal does not affect the indices for later removals. */
    if (count > 1)
	qsort(indices, count, sizeof *indices, compare_long);

    pl_initwith(&list, array->v_vals, array->v_valc);
    lastindex = LONG_MIN;
    for (size_t i = count; i-- != 0; ) {
	long index = indices[i];
	if (index == lastindex)
	    continue;
	if (index > 0) {
	    if (LONG_LT_SIZE(index - 1, list.length)) {
		free(list.contents[index - 1]);
		pl_remove(&list, index - 1, 1);
	    }
	} else if (index < 0) {
	    index += array->v_valc;
	    if (index >= 0) {
		assert(LONG_LT_SIZE(index, list.length));
		free(list.contents[index]);
		pl_remove(&list, index, 1);
	    }
	}
	lastindex = index;
    }
    array->v_valc = list.length;
    array->v_vals = pl_toary(&list);
    return true;
}

int compare_long(const void *lp1, const void *lp2)
{
    long l1 = *(const long *) lp1, l2 = *(const long *) lp2;
    return l1 == l2 ? 0 : l1 < l2 ? -1 : 1;
}

/* Inserts elements into `array'.
 * `*values' is an NULL-terminated array of pointers to wide strings.
 * The first string in `*values' is parsed as the integer index that is
 * specifying where to insert elements. The other strings are inserted to the
 * array. `count' is the number of strings in `*values' including the first
 * index string.
 * Returns true iff successful. */
bool array_insert_elements(
	variable_T *array, size_t count, void *const *values)
{
    long index;
    plist_T list;

    assert((array->v_type & VF_MASK) == VF_ARRAY);
    assert(count > 0);
    assert(values[0] != NULL);
    {
	const wchar_t *indexword = *values;
	if (!xwcstol(indexword, 10, &index)) {
	    xerror(errno, Ngt("`%ls' is not a valid integer"), indexword);
	    return false;
	}
    }
    count--, values++;
    assert(plcount(values) == count);

    size_t uindex;
    if (index < 0) {
	index += array->v_valc + 1;
	if (index < 0)
	    index = 0;
    }
    uindex = LONG_LT_SIZE(index, array->v_valc) ? (size_t)index : array->v_valc;

    pl_initwith(&list, array->v_vals, array->v_valc);
    pl_insert(&list, uindex, values);
    for (size_t i = 0; i < count; i++)
	list.contents[uindex + i] = xwcsdup(list.contents[uindex + i]);
    array->v_valc = list.length;
    array->v_vals = pl_toary(&list);
    return true;
}

/* Sets the value of a single element of `array'.
 * `name' is the name of the array variable.
 * `indexword' is parsed as the integer index of the element. */
bool array_set_element(const wchar_t *name, variable_T *array,
	const wchar_t *indexword, const wchar_t *value)
{
    assert((array->v_type & VF_MASK) == VF_ARRAY);

    long index;
    if (!xwcstol(indexword, 10, &index)) {
	xerror(errno, Ngt("`%ls' is not a valid integer"), indexword);
	return false;
    }

    size_t uindex;
    if (index < 0) {
	index += array->v_valc;
	if (index < 0)
	    goto invalid_index;
	assert(LONG_LT_SIZE(index, array->v_valc));
	uindex = (size_t) index;
    } else if (index > 0) {
	if (!LONG_LT_SIZE(index - 1, array->v_valc))
	    goto invalid_index;
	uindex = (size_t) index - 1;
    } else {
	goto invalid_index;
    }
    assert(uindex < array->v_valc);
    free(array->v_vals[uindex]);
    array->v_vals[uindex] = xwcsdup(value);
    return true;

invalid_index:
    xerror(0,
	    Ngt("%ls: index out of range (actual size of array `%ls' is %zu)"),
	    indexword, name, array->v_valc);
    return false;
}

const char array_help[] = Ngt(
"array - manipulate array\n"
"\tarray\n"
"\tarray name [value...]\n"
"\tarray -d name [index...]\n"
"\tarray -i name index [value...]\n"
"\tarray -s name index value\n"
"The first form (without arguments) prints all existing arrays.\n"
"The second form sets values of an array. This is equivalent to an assignment\n"
"by \"name=(values)\".\n"
"The third form (with the -d (--delete) option) removes elements specified by\n"
"<index>es from the array.\n"
"The fourth form (with the -i (--insert) option) inserts elements after the\n"
"element specified by <index> in the array. The zero index means the elements\n"
"are inserted at the head of the array.\n"
"The fifth form (with the -s (--set) option) sets the value of a single\n"
"element of the array.\n"
);

#endif /* YASH_ENABLE_ARRAY */

/* The "unset" builtin, which accepts the following options:
 * -f: deletes functions
 * -v: deletes variables (default) */
int unset_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"functions", xno_argument, L'f', },
	{ L"variables", xno_argument, L'v', },
	{ L"help",      xno_argument, L'-', },
	{ NULL, 0, 0, },
    };

    wchar_t opt;
    bool funcs = false;
    bool err = false;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"fv", long_options, NULL))) {
	switch (opt) {
	    case L'f':  funcs = true;   break;
	    case L'v':  funcs = false;  break;
	    case L'-':
		return print_builtin_help(ARGV(0));
	    default:
		fprintf(stderr, gt("Usage:  unset [-fv] name...\n"));
		SPECIAL_BI_ERROR;
		return Exit_ERROR;
	}
    }

    for (; xoptind < argc; xoptind++) {
	const wchar_t *name = ARGV(xoptind);
	if (wcschr(name, L'=')) {
	    xerror(0, Ngt("`%ls': invalid name"), name);
	    err = true;
	    continue;
	}
	err |= funcs ? unset_function(name) : unset_variable(name);
    }

    return err ? Exit_FAILURE : Exit_SUCCESS;
}

/* Unsets the specified function.
 * Returns true ON ERROR. */
bool unset_function(const wchar_t *name)
{
    kvpair_T kv = ht_remove(&functions, name);
    function_T *f = kv.value;
    if (f) {
	if (!(f->f_type & VF_NODELETE)) {
	    funckvfree(kv);
	} else {
	    xerror(0, Ngt("%ls: readonly"), name);
	    ht_set(&functions, kv.key, kv.value);
	    return true;
	}
    }
    return false;
}

/* Unsets the specified variable.
 * Returns true ON ERROR. */
bool unset_variable(const wchar_t *name)
{
    for (environ_T *env = current_env; env; env = env->parent) {
	kvpair_T kv = ht_remove(&env->contents, name);
	variable_T *var = kv.value;
	if (var) {
	    if (!(var->v_type & VF_NODELETE)) {
		bool ue = var->v_type & VF_EXPORT;
		varkvfree(kv);
		variable_set(name, NULL);
		if (ue)
		    update_environment(name);
		return false;
	    } else {
		xerror(0, Ngt("%ls: readonly"), name);
		ht_set(&env->contents, kv.key, kv.value);
		return true;
	    }
	}
    }
    return false;
}

const char unset_help[] = Ngt(
"unset - remove variables or functions\n"
"\tunset [-fv] <name>...\n"
"Removes the specified variables or functions.\n"
"When the -f (--functions) options is specified, this command removes\n"
"functions. When the -v (--variables) option is specified, this command\n"
"removes variables.\n"
"-f and -v are mutually exclusive: the last specified one is used.\n"
"If neither is specified, -v is the default.\n"
);

/* The "shift" builtin */
int shift_builtin(int argc, void **argv)
{
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"", help_option, NULL))) {
	switch (opt) {
	    case L'-':
		return print_builtin_help(ARGV(0));
	    default:  print_usage:
		fprintf(stderr, gt("Usage:  shift [n]\n"));
		SPECIAL_BI_ERROR;
		return Exit_ERROR;
	}
    }
    if (argc - xoptind > 1)
	goto print_usage;

    size_t scount;
    if (xoptind < argc) {
	long count;
	if (!xwcstol(ARGV(xoptind), 10, &count)) {
	    xerror(errno, Ngt("`%ls' is not a valid integer"), ARGV(xoptind));
	    SPECIAL_BI_ERROR;
	    return Exit_ERROR;
	} else if (count < 0) {
	    xerror(0, Ngt("%ls: value must not be negative"), ARGV(xoptind));
	    SPECIAL_BI_ERROR;
	    return Exit_ERROR;
	}
#if LONG_MAX <= SIZE_MAX
	scount = (size_t) count;
#else
	scount = (count > (long) SIZE_MAX) ? SIZE_MAX : (size_t) count;
#endif
    } else {
	scount = 1;
    }

    variable_T *var = search_variable(L VAR_positional);
    assert(var != NULL && (var->v_type & VF_MASK) == VF_ARRAY);
    if (scount > var->v_valc) {
	xerror(0, Ngt("%zu: cannot shift so many"), scount);
	return Exit_FAILURE;
    }
    for (size_t i = 0; i < scount; i++)
	free(var->v_vals[i]);
    var->v_valc -= scount;
    memmove(var->v_vals, var->v_vals + scount,
	    (var->v_valc + 1) * sizeof *var->v_vals);
    return Exit_SUCCESS;
}

const char shift_help[] = Ngt(
"shift - remove some positional parameters\n"
"\tshift [n]\n"
"Removes the first <n> positional parameters.\n"
"If <n> is not specified, the first one positional parameter is removed.\n"
"<n> must be a non-negative integer that is not greater than $#.\n"
);

/* The "getopts" builtin */
int getopts_builtin(int argc, void **argv)
{
    wchar_t opt;
    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"+", help_option, NULL))) {
	switch (opt) {
	    case L'-':
		return print_builtin_help(ARGV(0));
	    default:
		goto print_usage;
	}
    }
    if (xoptind + 2 > argc)
	goto print_usage;

    const wchar_t *options = ARGV(xoptind++);
    const wchar_t *varname = ARGV(xoptind++);
    void *const *args;
    unsigned long optind;
    const wchar_t *arg, *optp;
    wchar_t optchar;

    if (wcschr(varname, L'=')) {
	xerror(0, Ngt("`%ls': invalid name"), varname);
	return Exit_FAILURE;
    } else if (!check_options(options)) {
	xerror(0, Ngt("`%ls': invalid option specification"), options);
	return Exit_FAILURE;
    }
    {
	const wchar_t *varoptind = getvar(L VAR_OPTIND);
	if (!varoptind || !xwcstoul(varoptind, 10, &optind))
	    goto optind_invalid;
	optind -= 1;
    }
    if (xoptind < argc) {
	if (optind >= (unsigned long) (argc - xoptind))
	    return Exit_FAILURE;
	args = argv + xoptind;
    } else {
	variable_T *var = search_variable(L VAR_positional);
	assert(var != NULL && (var->v_type & VF_MASK) == VF_ARRAY);
	if (optind >= var->v_valc)
	    return Exit_FAILURE;
	args = var->v_vals;
    }

#define TRY(exp)  do { if (!(exp)) return Exit_FAILURE; } while (0)
parse_arg:
    arg = args[optind];
    if (arg[0] != L'-' || arg[1] == L'\0') {
	goto no_more_options;
    } else if (arg[1] == L'-' && arg[2] == L'\0') {  /* arg == "--" */
	set_optind(optind + 1);
	goto no_more_options;
    } else if (xwcsnlen(arg, optind2 + 1) <= optind2) {
	optind++, optind2 = 1;
	TRY(set_optind(optind));
	goto parse_arg;
    }

    optchar = arg[optind2++];
    assert(optchar != L'\0');
    if (optchar == L':' || !(optp = wcschr(options, optchar))) {
	/* invalid option */
	TRY(set_to(varname, L'?'));
	if (options[0] == L':') {
	    TRY(set_optarg((wchar_t []) { optchar, L'\0' }));
	} else {
	    fprintf(stderr, gt("%ls: -%lc: invalid option\n"),
		    command_name, (wint_t) optchar);
	    TRY(!unset_variable(L VAR_OPTARG));
	}
    } else {
	/* valid option */
	if (optp[1] == L':') {
	    /* option with an argument */
	    const wchar_t *optarg = arg + optind2;
	    optind2 = 1;
	    if (optarg[0]) {
		optind++;
		TRY(set_optind(optind));
	    } else {
		optind++;
		optarg = args[optind];
		optind++;
		TRY(set_optind(optind));
		if (!optarg) {
		    /* argument is missing */
		    if (options[0] == L':') {
			TRY(set_to(varname, L':'));
			TRY(set_optarg((wchar_t []) { optchar, L'\0' }));
		    } else {
			fprintf(stderr, gt("%ls: -%lc: argument missing\n"),
				command_name, (wint_t) optchar);
			TRY(set_to(varname, L'?'));
			TRY(!unset_variable(L VAR_OPTARG));
		    }
		    return Exit_SUCCESS;
		}
	    }
	    TRY(set_optarg(optarg));
	} else {
	    /* option without an argument */
	    TRY(!unset_variable(L VAR_OPTARG));
	}
	TRY(set_to(varname, optchar));
    }
    return Exit_SUCCESS;
#undef TRY

no_more_options:
    set_to(varname, L'?');
    unset_variable(L VAR_OPTARG);
    return Exit_FAILURE;
optind_invalid:
    xerror(0, Ngt("$OPTIND not valid"));
    return Exit_FAILURE;
print_usage:
    fprintf(stderr, gt("Usage:  getopts options var [arg...]\n"));
    return Exit_ERROR;
}

/* Checks if the `options' is valid. Returns true iff ok. */
bool check_options(const wchar_t *options)
{
    if (*options == L':')
	options++;
    for (;;) switch (*options) {
	case L'\0':
	    return true;
	case L':':
	    return false;
	case L'?':
	    if (posixly_correct)
		return false;
	    /* falls thru! */
	default:
	    if (posixly_correct && !iswalpha(*options))
		return false;
	    options++;
	    if (*options == L':')
		options++;
	    continue;
    }
}

/* Sets $OPTIND to `optind' plus 1. */
bool set_optind(unsigned long optind)
{
    return set_variable(L VAR_OPTIND,
	    malloc_wprintf(L"%lu", optind + 1),
	    SCOPE_GLOBAL, shopt_allexport);
}

/* Sets $OPTARG to `value'. */
bool set_optarg(const wchar_t *value)
{
    return set_variable(L VAR_OPTARG, xwcsdup(value),
	    SCOPE_GLOBAL, shopt_allexport);
}

/* Sets the specified variable to the single character `value'. */
bool set_to(const wchar_t *varname, wchar_t value)
{
    wchar_t *v = xmalloc(sizeof *v * 2);
    v[0] = value;
    v[1] = L'\0';
    return set_variable(varname, v, SCOPE_GLOBAL, shopt_allexport);
}

const char getopts_help[] = Ngt(
"getopts - parse command options\n"
"\tgetopts options var [arg...]\n"
"Parses <options> that appear in <arg>s. Each time this command is invoked,\n"
"one option is parsed and the option character is assigned to a variable\n"
"named <var>. $OPTIND is updated to indicate the next argument to parse.\n"
"The <options> argument is a list of option characters to parse. An option\n"
"that takes an argument is specified by a colon following the character.\n"
"For example, if you want -a, -b and -c options to be parsed and the -b\n"
"option takes an argument, <options> is \"ab:c\".\n"
"The <var> argument is the name of a variable to which the option character\n"
"is assigned. If the parsed option appears in <options>, the option character\n"
"is assigned to the variable. Otherwise \"?\" is assigned.\n"
"The <arg>s arguments are arguments of a command which may contain options to\n"
"be parsed. If no <arg>s are given, the current positional parameters are\n"
"parsed.\n"
"\n"
"When an option that takes an argument is parsed, the option's argument is\n"
"assigned to $OPTARG.\n"
"The behavior depends on the first character of <options> when an option not\n"
"contained in <options> is found or when an option's argument is missing:\n"
"If <options> starts with a colon, the option character is assigned to\n"
"$OPTARG and the variable <var> is set to \"?\" (when the option is not in\n"
"<options>) or \":\" (when an option's argument is missing).\n"
"Otherwise, the variable <var> is set to \"?\", $OPTARG is unset and a error\n"
"message is printed.\n"
"\n"
"If an option is parsed, whether it is a valid option or not, the exit status\n"
"is zero. If there are no more options to parse, the exit status is non-zero\n"
"and $OPTIND is updated so that the $OPTIND'th argument of <arg>s is the\n"
"first operand (non-option argument). If there are no operands, $OPTIND will\n"
"be the number of <arg>s plus one.\n"
"When this command is invoked for the first time, $OPTIND must be \"1\",\n"
"which is the default value of the $OPTIND variable. Until all the options\n"
"are parsed, you must not change the value of $OPTIND and <options>, <var>\n"
"and <arg>s must be all the same for all invocations of this command.\n"
"Reset $OPTIND to \"1\" and then this command can be used with another\n"
"<options>, <var> and <arg>s.\n"
);

/* The "read" builtin, which accepts the following options:
 * -A: assign values to array
 * -r: don't treat backslashes specially
 */
int read_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"array",    xno_argument, L'A', },
	{ L"raw-mode", xno_argument, L'r', },
	{ L"help",     xno_argument, L'-', },
	{ NULL, 0, 0, },
    };

    wchar_t opt;
    bool array = false, raw = false;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv,
		    posixly_correct ? L"r" : L"Ar",
		    long_options, NULL))) {
	switch (opt) {
	    case L'A':  array = true;  break;
	    case L'r':  raw   = true;  break;
	    case L'-':
		return print_builtin_help(ARGV(0));
	    default:
		goto print_usage;
	}
    }
    if (xoptind == argc)
	goto print_usage;

    /* check if the identifiers are valid */
    for (int i = xoptind; i < argc; i++) {
	if (wcschr(ARGV(i), L'=')) {
	    xerror(0, Ngt("`%ls': invalid name"), ARGV(i));
	    return Exit_FAILURE;
	}
    }

    bool err = false;
    xwcsbuf_T buf;

    /* read input and remove trailing newline */
    wb_init(&buf);
    if (!read_input(&buf, raw)) {
	wb_destroy(&buf);
	return Exit_FAILURE;
    }
    if (buf.length > 0 && buf.contents[buf.length - 1] == L'\n') {
	wb_remove(&buf, buf.length - 1, 1);
    } else {
	/* no newline means the EOF was encountered */
	err = true;
    }

    const wchar_t *s = buf.contents;
    const wchar_t *ifs = getvar(L VAR_IFS);
    plist_T list;

    /* split fields */
    pl_init(&list);
    for (int i = xoptind; i < argc - 1; i++)
	pl_add(&list, split_next_field(&s, ifs, raw));
    pl_add(&list, (raw || array) ? xwcsdup(s) : unescape(s));
    wb_destroy(&buf);

    /* assign variables */
    assert(xoptind + list.length == (size_t) argc);
    for (size_t i = 0; i < list.length; i++) {
	const wchar_t *name = ARGV(xoptind + i);
	if (i + 1 == list.length)
	    trim_trailing_ifsws(list.contents[i], ifs);
	if (!array || i + 1 < list.length)
	    err |= !set_variable(name, list.contents[i],
		    SCOPE_GLOBAL, shopt_allexport);
	else
	    err |= !split_and_assign_array(name, list.contents[i], ifs, raw);
    }

    pl_destroy(&list);
    return err ? Exit_FAILURE : Exit_SUCCESS;

print_usage:
    if (posixly_correct)
	fprintf(stderr, gt("Usage:  read [-r] var...\n"));
    else
	fprintf(stderr, gt("Usage:  read [-Ar] var...\n"));
    return Exit_ERROR;
}

/* Reads input from stdin and remove line continuations if `noescape' is false.
 * The trailing newline and all the other backslashes are not removed. */
bool read_input(xwcsbuf_T *buf, bool noescape)
{
    if (noescape)
	return read_line_from_stdin(buf, false);

    size_t index;
    bool cont;
    bool first = true;
    do {
	index = buf->length;
	cont = false;
	if (!first && is_interactive_now && isatty(STDIN_FILENO))
	    print_prompt(2);
	if (!read_line_from_stdin(buf, false))
	    return false;
	first = false;
	while (index < buf->length) {
	    if (buf->contents[index] == L'\\') {
		if (buf->contents[index + 1] == L'\n') {
		    wb_remove(buf, index, 2);
		    cont = true;
		    assert(index == buf->length);
		    break;
		} else {
		    index += 2;
		    continue;
		}
	    }
	    index++;
	}
    } while (cont);
    return true;
}

/* Word-splits `values' and assigns them to the array named `name'.
 * `values' is freed in this function. */
bool split_and_assign_array(const wchar_t *name, wchar_t *values,
	const wchar_t *ifs, bool raw)
{
    plist_T list;

    pl_init(&list);
    if (values[0]) {
	const wchar_t *v = values;
	while (*v)
	    pl_add(&list, split_next_field(&v, ifs, raw));

	if (list.length > 0
		&& ((wchar_t *) list.contents[list.length - 1])[0] == L'\0') {
	    free(list.contents[list.length - 1]);
	    pl_remove(&list, list.length - 1, 1);
	}
    }

    bool ok = set_array(name, list.length, pl_toary(&list), SCOPE_GLOBAL);

    free(values);
    return ok;
}

const char read_help[] = Ngt(
"read - read a line from standard input\n"
"\tread [-Ar] var...\n"
"Reads a line from the standard input and splits it into words using $IFS as\n"
"separators. <var>s are considered to be variable names and the words are\n"
"assigned to the variables. The first word is assigned to the first <var>,\n"
"the second word to the second <var>, and so on. If <var>s are fewer than the\n"
"words, the leftover words are not split and assigned to the last <var>. If\n"
"the words are fewer than <var>s, the leftover <var>s are set to empty\n"
"strings.\n"
"If the -r (--raw-mode) option is not specified, backslashes in the input are\n"
"considered to be escape characters.\n"
"If the -A (--array) option is specified, the leftover words are assigned to\n"
"the last <var> as array elements, rather than as a single variable.\n"
"The -A option is not available in the POSIXly-correct mode.\n"
);

#if YASH_ENABLE_DIRSTACK

static const struct xoption dirs_options[] = {
    { L"clear",   xno_argument, L'c', },
    { L"verbose", xno_argument, L'v', },
    { L"help",    xno_argument, L'-', },
    { NULL, 0, 0, },
};

/* The "dirs" builtin, which accepts the following options:
 * -c: clear the stack
 * -v: verbose */
int dirs_builtin(int argc, void **argv)
{
    wchar_t opt;
    bool clear = false, verbose = false;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"-cv", dirs_options, NULL))) {
	switch (opt) {
	    case L'c':  clear   = true;  break;
	    case L'v':  verbose = true;  break;
	    case L'-':
		return print_builtin_help(ARGV(0));
	    default:
		fprintf(stderr, gt("Usage:  dirs [-cv] [index...]\n"));
		return Exit_ERROR;
	}
    }

    if (clear)
	return unset_variable(L VAR_DIRSTACK) ? Exit_FAILURE : Exit_SUCCESS;

    bool err = false;
    const wchar_t *dir;
    if (xoptind < argc) {
	/* print the specified only */
	do {
	    size_t index, size = get_dirstack_size();
	    if (parse_dirstack_index(ARGV(xoptind), &index, &dir, true)) {
		if (index != SIZE_MAX) {
		    int r;
		    if (verbose)
			r = printf("+%zu\t-%zu\t%ls\n",
				size - index, index, dir);
		    else
			r = printf("%ls\n", dir);
		    if (r < 0) {
			xerror(errno, Ngt("cannot print to standard output"));
			err = true;
			break;
		    }
		} else {
		    xerror(0, Ngt("`%ls' is not a valid index"),
			    ARGV(xoptind));
		    err = true;
		}
	    } else {
		err = true;
	    }
	} while (++xoptind < argc);
    } else {
	/* print all */
	variable_T *var = search_variable(L VAR_DIRSTACK);
	bool dirvalid = (var && ((var->v_type & VF_MASK) == VF_ARRAY));
	size_t size = dirvalid ? var->v_valc : 0;

	dir = getvar(L VAR_PWD);
	if (!dir) {
	    xerror(0, Ngt("$PWD not set"));
	    err = true;
	} else {
	    int r;
	    if (verbose)
		r = printf("+%zu\t-%zu\t%ls\n", (size_t) 0, size, dir);
	    else
		r = printf("%ls\n", dir);
	    if (r < 0) {
		xerror(errno, Ngt("cannot print to standard output"));
		err = true;
	    }
	}

	if (dirvalid) {
	    for (size_t i = var->v_valc; !err && i-- > 0; ) {
		int r;
		dir = var->v_vals[i];
		if (verbose)
		    r = printf("+%zu\t-%zu\t%ls\n", size - i, i, dir);
		else
		    r = printf("%ls\n", dir);
		if (r < 0) {
		    xerror(errno, Ngt("cannot print to standard output"));
		    err = true;
		}
	    }
	}
    }
    return err ? Exit_FAILURE : Exit_SUCCESS;
}

const char dirs_help[] = Ngt(
"dirs - print directory stack\n"
"\tdirs [-cv] [index...]\n"
"With no arguments, prints the contents of the directory stack.\n"
"If <index> is specified, only the specified entry is printed.\n"
"The -v (--verbose) option makes the entries preceded by indices.\n"
"The -c (--clear) option clears the stack.\n"
);

#endif /* YASH_ENABLE_DIRSTACK */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
