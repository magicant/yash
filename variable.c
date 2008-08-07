/* Yash: yet another shell */
/* variable.c: deals with shell variables and parameters */
/* (C) 2007-2008 magicant */

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
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "option.h"
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "hashtable.h"
#include "path.h"
#include "input.h"
#include "parser.h"
#include "variable.h"
#include "expand.h"
#include "builtin.h"
#include "exec.h"
#include "yash.h"
#include "version.h"


const char *const path_variables[PATHTCOUNT] = {
    [PA_PATH] = VAR_PATH,
    [PA_CDPATH] = VAR_CDPATH,
};


/* variable environment (= set of variables) */
/* not to be confused with environment variables */
typedef struct environ_T {
    struct environ_T *parent;      /* parent environment */
    struct hashtable_T contents;   /* hashtable containing variables */
    bool is_temporary;             /* for temporary assignment? */
    char **paths[PATHTCOUNT];
} environ_T;
/* `contents' is a hashtable from (char *) to (variable_T *).
 * A variable name may contain any characters except '\0' and '=', though
 * assignment syntax allows only limited characters.
 * Variable names starting with '=' are used for special purposes.
 * The positional parameter is treated as an array whose name is "=".
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
 * possibly OR'ed with other flags. VF_EXPORT is only valid for VF_NORMAL. */

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

static inline bool is_name_char(char c)
    __attribute__((const));

static variable_T *search_variable(const char *name)
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
static variable_T *new_variable(const char *name, bool local)
    __attribute__((nonnull));
static bool assign_temporary(const char *name, wchar_t *value, bool export)
    __attribute__((nonnull(1)));
static void get_all_variables_rec(hashtable_T *table, environ_T *env)
    __attribute__((nonnull));

static void lineno_getter(variable_T *var)
    __attribute__((nonnull));
static void random_getter(variable_T *var)
    __attribute__((nonnull));
static unsigned next_random(void);

static void variable_set(const char *name, variable_T *var)
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
static environ_T *top_env;

/* Temporary variables are used when a non-special builtin is executed.
 * They are assigned in a temporary environment and valid only while the command
 * is executed. */

/* whether $RANDOM is functioning as a random number */
static bool random_active;

/* Hashtable from function names (char *) to functions (function_T *). */
static hashtable_T functions;


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

/* Calls `variable_set' and `update_enrivon' for `kv.key' and
 * frees the variable. */
void varkvfree_reexport(kvpair_T kv)
{
    variable_set(kv.key, NULL);
    if (((variable_T *) kv.value)->v_type & VF_EXPORT)
	update_enrivon(kv.key);
    varkvfree(kv);
}

/* Initializes the top-level environment. */
void init_variables(void)
{
    top_env = current_env = xmalloc(sizeof *current_env);
    current_env->parent = NULL;
    current_env->is_temporary = false;
    ht_init(&current_env->contents, hashstr, htstrcmp);
    for (size_t i = 0; i < PATHTCOUNT; i++)
	current_env->paths[i] = NULL;

    ht_init(&functions, hashstr, htstrcmp);

    /* add all the existing environment variables to the variable environment */
    for (char **e = environ; *e; e++) {
	wchar_t *we = malloc_mbstowcs(*e);
	if (!we)
	    continue;

	size_t namelen;
	for (namelen = 0; we[namelen] && we[namelen] != L'='; namelen++);

	char *name = malloc_wcsntombs(we, namelen);
	if (name) {
	    variable_T *v = xmalloc(sizeof *v);
	    v->v_type = VF_NORMAL | VF_EXPORT;
	    v->v_value = we[namelen] == L'=' ? xwcsdup(we + namelen + 1) : NULL;
	    v->v_getter = NULL;
	    varkvfree(ht_set(&current_env->contents, name, v));
	}
	free(we);
    }

    /* set $LINENO */
    {
	variable_T *v = new_variable(VAR_LINENO, false);
	assert(v != NULL);
	v->v_type = VF_NORMAL | (v->v_type & VF_EXPORT);
	v->v_value = NULL;
	v->v_getter = lineno_getter;
	if (v->v_type & VF_EXPORT)
	    update_enrivon(VAR_LINENO);
    }

    /* set $PS1~4 */
    {
	const wchar_t *ps1 =
	    !posixly_correct ? L"\\$ " : (geteuid() != 0) ? L"$ " : L"# ";
	set_variable(VAR_PS1, xwcsdup(ps1),    false, false);
	set_variable(VAR_PS2, xwcsdup(L"> "),  false, false);
	if (!posixly_correct)
	    set_variable(VAR_PS3, xwcsdup(L"#? "), false, false);
	set_variable(VAR_PS4, xwcsdup(L"+ "),  false, false);
    }

    /* set $PWD */
    init_pwd();

    /* export $OLDPWD */
    {
	variable_T *v = new_global(VAR_OLDPWD);
	assert(v != NULL);
	v->v_type |= VF_EXPORT;
	variable_set(VAR_OLDPWD, v);
    }

    /* set $PPID */
    set_variable(VAR_PPID, malloc_wprintf(L"%jd", (intmax_t) getppid()),
	    false, false);

    /* set $RANDOM */
    if (!posixly_correct && !getvar(VAR_RANDOM)) {
	variable_T *v = new_variable(VAR_RANDOM, false);
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
    set_variable(VAR_YASH_VERSION, xwcsdup(L"" PACKAGE_VERSION), false, false);

    /* initialize path according to $PATH/CDPATH */
    for (size_t i = 0; i < PATHTCOUNT; i++)
	current_env->paths[i] = decompose_paths(getvar(path_variables[i]));
}

/* Reset the value of $PWD if
 *  - `posixly_correct' is true, or
 *  - $PWD doesn't exist, or
 *  - the value of $PWD isn't an absolute path, or
 *  - the value of $PWD isn't the actual current directory, or
 *  - the value of $PWD isn't canonicalized. */
void init_pwd(void)
{
    if (posixly_correct)
	goto set;
    const char *pwd = getenv(VAR_PWD);
    if (!pwd || pwd[0] != '/' || !is_same_file(pwd, "."))
	goto set;
    const wchar_t *wpwd = getvar(VAR_PWD);
    if (!wpwd || !is_canonicalized(wpwd))
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
    wnewpwd = malloc_mbstowcs(newpwd);
    free(newpwd);
    if (!wnewpwd) {
	xerror(0, Ngt("cannot set $PWD"));
	return;
    }
    set_variable(VAR_PWD, wnewpwd, false, true);
}

/* Checks if the specified character can be used in a variable name in an
 * assignment. */
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

/* Checks if the specified string is a valid variable name.
 * Note that parameters are not variables. */
bool is_name(const char *s)
{
    if (!xisdigit(*s))
	while (is_name_char(*s))
	    s++;
    return !*s;
}

/* Searches for a variable with the specified name.
 * if `temp' is false, temporary variables are ignored.
 * Returns NULL if none found. */
variable_T *search_variable(const char *name)
{
    for (environ_T *env = current_env; env; env = env->parent) {
	variable_T *var = ht_get(&env->contents, name).value;
	if (var)
	    return var;
    }
    return NULL;
}

/* Update the value in `environ' for the variable with the specified name.
 * `name' must not contain '='. */
void update_enrivon(const char *name)
{
    for (environ_T *env = current_env; env; env = env->parent) {
	variable_T *var = ht_get(&env->contents, name).value;
	if (var && (var->v_type & VF_EXPORT) && var->v_value) {
	    assert((var->v_type & VF_MASK) == VF_NORMAL);
	    char *value = malloc_wcstombs(var->v_value);
	    if (value) {
		if (setenv(name, value, true) < 0)
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
    }
    if (unsetenv(name) < 0)
	xerror(errno, Ngt("cannot unset environment variable `%s'"), name);
}

/* Resets the locate settings for the specified variable.
 * If `name' is not any of "LANG", "LC_ALL", etc., does nothing. */
void reset_locale(const char *name)
{
    if (strcmp(name, VAR_LANG) == 0) {
	goto reset_locale_all;
    } else if (strncmp(name, "LC_", 3) == 0) {
	/* POSIX forbids resetting LC_CTYPE even if the value of the variable
	 * is changed. */
	if (strcmp(name + 3, VAR_LC_ALL + 3) == 0) {
reset_locale_all:
	    reset_locale_category(VAR_LC_COLLATE,  LC_COLLATE);
//	    reset_locale_category(VAR_LC_CTYPE,    LC_CTYPE);
	    reset_locale_category(VAR_LC_MESSAGES, LC_MESSAGES);
	    reset_locale_category(VAR_LC_MONETARY, LC_MONETARY);
	    reset_locale_category(VAR_LC_NUMERIC,  LC_NUMERIC);
	    reset_locale_category(VAR_LC_TIME,     LC_TIME);
	} else if (strcmp(name + 3, VAR_LC_COLLATE + 3) == 0) {
	    reset_locale_category(VAR_LC_COLLATE,  LC_COLLATE);
//	} else if (strcmp(name + 3, VAR_LC_CTYPE + 3) == 0) {
//	    reset_locale_category(VAR_LC_CTYPE,    LC_CTYPE);
	} else if (strcmp(name + 3, VAR_LC_MESSAGES + 3) == 0) {
	    reset_locale_category(VAR_LC_MESSAGES, LC_MESSAGES);
	} else if (strcmp(name + 3, VAR_LC_MONETARY + 3) == 0) {
	    reset_locale_category(VAR_LC_MONETARY, LC_MONETARY);
	} else if (strcmp(name + 3, VAR_LC_NUMERIC + 3) == 0) {
	    reset_locale_category(VAR_LC_NUMERIC,  LC_NUMERIC);
	} else if (strcmp(name + 3, VAR_LC_TIME + 3) == 0) {
	    reset_locale_category(VAR_LC_TIME,     LC_TIME);
	}
    }
}

/* Resets the locale of the specified category.
 * `name' must be one of the `LC_*' constants except LC_ALL. */
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

/* Creates a new scalar variable with the value unset.
 * If the variable already exists, it is returned without change.
 * So the return value may be an array variable.
 * Temporary variables with the `name' are cleared. */
variable_T *new_global(const char *name)
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
    ht_set(&top_env->contents, xstrdup(name), var);
    return var;
}

/* Creates a new scalar variable with the value unset.
 * If the variable already exists, it is returned without change.
 * So the return value may be an array variable.
 * Temporary variables with the `name' are cleared. */
variable_T *new_local(const char *name)
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
    ht_set(&env->contents, xstrdup(name), var);
    return var;
}

/* Creates a new variable if there is none.
 * If the variable already exists, it is cleared and returned.
 *
 * On error, NULL is returned. Otherwise a (new) variable is returned.
 * `v_type' is the only valid member of the variable and the all members
 * (including `v_type') must be initialized by the caller. If `v_type' of the
 * return value has the VF_EXPORT flag set, the caller must call
 * `update_enrivon'. */
variable_T *new_variable(const char *name, bool local)
{
    variable_T *var = local ? new_local(name) : new_global(name);
    if (var->v_type & VF_READONLY) {
	xerror(0, Ngt("%s: readonly"), name);
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
bool set_variable(const char *name, wchar_t *value, bool local, bool export)
{
    variable_T *var = new_variable(name, local);
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
	update_enrivon(name);
    return true;
}

/* Creates an array variable with the specified name and values.
 * `values' is a NULL-terminated array of pointers to wide strings.
 * Returns true iff successful. */
bool set_array(const char *name, void *const *values, bool local)
{
    variable_T *var = new_variable(name, local);
    if (!var)
	return false;

    bool needupdate = (var->v_type & VF_EXPORT);

    var->v_type = VF_ARRAY | (var->v_type & VF_NODELETE);
    var->v_vals = duparray(values, copyaswcs);
    var->v_valc = plcount(var->v_vals);
    var->v_getter = NULL;

    variable_set(name, var);
    if (needupdate)
	update_enrivon(name);
    return true;
}

/* Sets the positional parameters of the current environment.
 * The existent parameters are cleared.
 * `values' is an NULL-terminated array of pointers to wide strings.
 * `values[0]' will be the new $1, `values[1]' $2, and so on.
 * When a new environment is created, this function must be called at least once
 * except for a temporary environment. */
void set_positional_parameters(void *const *values)
{
    set_array(VAR_positional, values, true);
}

/* Does an assignment to a temporary variable.
 * `current_env->is_temporary' must be true.
 * `value' must be a `free'able string or NULL. The caller must not modify or
 * `free' `value' hereafter, whether or not this function is successful.
 * If `export' is true, the variable is exported. `export' being false doesn't
 * mean the variable is no longer exported.
 * Returns true iff successful. */
bool assign_temporary(const char *name, wchar_t *value, bool export)
{
    assert(current_env->is_temporary);

    variable_T *var = ht_get(&current_env->contents, name).value;
    if (!var) {
	var = xmalloc(sizeof *var);
	var->v_type = 0;
	ht_set(&current_env->contents, xstrdup(name), var);
    } else {
	varvaluefree(var);
    }

    var->v_type = VF_NORMAL;
    var->v_value = value;
    var->v_getter = NULL;

    variable_set(name, var);
    if (export) {
	var->v_type |= VF_EXPORT;
	update_enrivon(name);
    }
    return true;
}

/* Does assignments.
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
    if (shopt_xtrace && assign)
	print_prompt(4);

    while (assign) {
	wchar_t *value = expand_single(assign->value, tt_multi);
	if (!value)
	    return false;
	value = unescapefree(value);
	if (shopt_xtrace)
	    fprintf(stderr, "%s=%ls%c",
		    assign->name, value, (assign->next ? ' ' : '\n'));

	bool ok;
	if (temp)
	    ok = assign_temporary(assign->name, value, export);
	else
	    ok = set_variable(assign->name, value, false, export);
	if (!ok)
	    return false;

	assign = assign->next;
    }
    return true;
}

/* Gets the value of the specified scalar variable.
 * Cannot be used for special parameters such as $$ and $@.
 * Returns the value of the variable, or NULL if not found.
 * The return value must not be modified or `free'ed by the caller and
 * is valid until the variable is re-assigned or unset. */
const wchar_t *getvar(const char *name)
{
    variable_T *var = search_variable(name);
    if (var && (var->v_type & VF_MASK) == VF_NORMAL) {
	if (var->v_getter)
	    var->v_getter(var);
	return var->v_value;
    }
    return NULL;
}

/* Gets the value(s) of the specified variable as an array.
 * A scalar value is returned as a one-element array.
 * The returned array is a newly-malloced NULL-terminated array of pointers to
 * newly-malloced wide characters cast to (void *).
 * If no such variable is found, NULL is returned.
 * If `name' is "*", true is assigned to `*concat' to indicate the array
 * elements should be concatenated by the caller. Otherwise false is assigned.*/
void **get_variable(const char *name, bool *concat)
{
    void **result;
    wchar_t *value;
    variable_T *var;

    *concat = false;
    if (!name[0])
	return NULL;
    if (!name[1]) {
	/* `name' is one-character long: check if it's a special parameter */
	switch (name[0]) {
	    case '*':
		*concat = true;
		/* falls thru! */
	    case '@':
		var = search_variable(VAR_positional);
		assert(var != NULL && (var->v_type & VF_MASK) == VF_ARRAY);
		result = var->v_vals;
		goto return_array;
	    case '#':
		var = search_variable(VAR_positional);
		assert(var != NULL && (var->v_type & VF_MASK) == VF_ARRAY);
		value = malloc_wprintf(L"%zu", var->v_valc);
		goto return_single;
	    case '?':
		value = malloc_wprintf(L"%d", laststatus);
		goto return_single;
	    case '-':
		value = get_hyphen_parameter();
		goto return_single;
	    case '$':
		value = malloc_wprintf(L"%jd", (intmax_t) shell_pid);
		goto return_single;
	    case '!':
		value = malloc_wprintf(L"%jd", (intmax_t) lastasyncpid);
		goto return_single;
	    case '0':
		value = xwcsdup(command_name);
		goto return_single;
	}
    }

    if (xisdigit(name[0])) {
	/* `name' starts with a digit: a positional parameter */
	char *nameend;
	errno = 0;
	long v = strtol(name, &nameend, 10);
	if (errno || *nameend != '\0')
	    return NULL;  /* not a number or overflow */
	var = search_variable(VAR_positional);
	assert(var != NULL && (var->v_type & VF_MASK) == VF_ARRAY);
	if (v <= 0 || (uintmax_t) var->v_valc < (uintmax_t) v)
	    return NULL;  /* index out of bounds */
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
		result = var->v_vals;
		goto return_array;
	}
    }
    return NULL;

return_single:  /* return a scalar as a one-element array */
    if (!value)
	return NULL;
    result = xmalloc(2 * sizeof *result);
    result[0] = value;
    result[1] = NULL;
    return result;

return_array:  /* duplicate an array and return it */
    return duparray(result, copyaswcs);
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
    ht_init(&newenv->contents, hashstr, htstrcmp);
    for (size_t i = 0; i < PATHTCOUNT; i++)
	newenv->paths[i] = NULL;
    current_env = newenv;
}

/* Closes the current variable environment.
 * The parent of the current becomes the new current. */
void close_current_environment(void)
{
    environ_T *oldenv = current_env;

    assert(oldenv != top_env);
    current_env = oldenv->parent;
    ht_clear(&oldenv->contents, varkvfree_reexport);
    ht_destroy(&oldenv->contents);
    for (size_t i = 0; i < PATHTCOUNT; i++)
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
    if (var->v_type & VF_EXPORT)
	update_enrivon(VAR_LINENO);
}

/* getter for $RANDOM */
void random_getter(variable_T *var)
{
    assert((var->v_type & VF_MASK) == VF_NORMAL);
    free(var->v_value);
    var->v_value = malloc_wprintf(L"%u", next_random());
    if (var->v_type & VF_EXPORT)
	update_enrivon(VAR_RANDOM);
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
void variable_set(const char *name, variable_T *var)
{
    switch (name[0]) {
    case 'C':
	if (strcmp(name, VAR_CDPATH) == 0)
	    reset_path(PA_CDPATH, var);
	break;
    case 'L':
	if (strcmp(name, VAR_LANG) == 0 || strncmp(name, "LC_", 3) == 0)
	    reset_locale(name);
	break;
    case 'P':
	if (strcmp(name, VAR_PATH) == 0)
	    reset_path(PA_PATH, var);
	break;
    case 'R':
	if (random_active && strcmp(name, VAR_RANDOM) == 0) {
	    random_active = false;
	    if (var && (var->v_type & VF_MASK) == VF_NORMAL && var->v_value) {
		wchar_t *end;
		errno = 0;
		unsigned seed = wcstoul(var->v_value, &end, 0);
		if (!errno && *end == L'\0') {
		    srand(seed);
		    var->v_getter = random_getter;
		    random_active = true;
		}
	    }
	}
	break;
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
    if (name == PA_PATH)
	clear_cmdhash();

    for (environ_T *env = current_env; env; env = env->parent) {
	recfree((void **) env->paths[name], free);

	variable_T *v = ht_get(&env->contents, path_variables[name]).value;
	if (v) {
	    switch (v->v_type & VF_MASK) {
		    plist_T list;
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
    environ_T *env = current_env;
    while (env) {
	if (env->paths[name])
	    return env->paths[name];
	env = env->parent;
    }
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

/* Clears all functions ignoring VF_READONLY/VF_NODELETE flags. */
void clear_all_functions(void)
{
    ht_clear(&functions, funckvfree);
}

/* Defines a function.
 * It is an error to re-define a readonly function.
 * Returns true iff successful. */
bool define_function(const char *name, command_T *body)
{
    function_T *f = ht_get(&functions, name).value;
    if (f != NULL && (f->f_type & VF_READONLY)) {
	xerror(0, Ngt("cannot re-define readonly function `%s'"), name);
	return false;
    }

    f = xmalloc(sizeof *f);
    f->f_type = 0;
    f->f_body = comsdup(body);
    if (shopt_hashondef)
	hash_all_commands_recursively(body);
    funckvfree(ht_set(&functions, xstrdup(name), f));
    return true;
}

/* Gets the body of the function with the specified name.
 * Returns NULL if there is no such a function. */
command_T *get_function(const char *name)
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
    while (c) {
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
	c = c->next;
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


/********** Builtins **********/

static void print_variable(
	const char *name, const variable_T *var,
	const wchar_t *argv0, bool readonly, bool export)
    __attribute__((nonnull));
static void print_function(
	const char *name, const function_T *func,
	const wchar_t *argv0, bool readonly)
    __attribute__((nonnull));

/* The "typeset" builtin, which accepts the following options:
 *  -f: affect functions rather than variables
 *  -g: global
 *  -p: print variables
 *  -r: make variables readonly
 *  -x: export variables
 *  -X: cancel exportation of variables
 * Equivalent builtins:
 *  export:   typeset -x
 *  readonly: typeset -r
 * If `posixly_correct' is on, the -g flag is on by default.
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
    bool funcs = false, global = posixly_correct, print = false;
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
		print_builtin_help(ARGV(0));
		return EXIT_SUCCESS;
	    default:
		fprintf(stderr,
			gt("Usage:  %ls [-rgprxX] [name[=value]...]\n"),
			ARGV(0));
		return EXIT_ERROR;
	}
    }

    if (wcscmp(ARGV(0), L"export") == 0) {
	if (!unexport)
	    export = true;
    } else if (wcscmp(ARGV(0), L"readonly") == 0) {
	readonly = true;
    } else {
	assert(wcscmp(ARGV(0), L"typeset") == 0
	    || wcscmp(ARGV(0), L"set") == 0);
    }

    if (funcs && (export || unexport)) {
	xerror(0, Ngt("functions cannot be exported"));
	return EXIT_ERROR;
    } else if (export && unexport) {
	xerror(0, Ngt("-x and -X cannot be used at a time"));
	return EXIT_ERROR;
    }

    if (xoptind == argc) {
	kvpair_T *kvs;
	size_t count;

	if (!funcs) {
	    /* print all variables */
	    hashtable_T table;

	    ht_init(&table, hashstr, htstrcmp);
	    get_all_variables_rec(&table, current_env);
	    kvs = ht_tokvarray(&table);
	    count = table.count;
	    ht_destroy(&table);
	    qsort(kvs, count, sizeof *kvs, keystrcoll);
	    for (size_t i = 0; i < count; i++)
		print_variable(kvs[i].key, kvs[i].value, ARGV(0),
			readonly, export);
	} else {
	    /* print all functions */
	    kvs = ht_tokvarray(&functions);
	    count = functions.count;
	    qsort(kvs, count, sizeof *kvs, keystrcoll);
	    for (size_t i = 0; i < count; i++)
		print_function(kvs[i].key, kvs[i].value, ARGV(0), readonly);
	}
	free(kvs);
	return EXIT_SUCCESS;
    }

    bool err = false;
    do {
	wchar_t *warg = ARGV(xoptind);
	wchar_t *wequal = wcschr(warg, L'=');
	if (wequal)
	    *wequal = L'\0';
	char *name = malloc_wcstombs(warg);
	if (!name) {
	    xerror(0, Ngt("unexpected error"));
	    return EXIT_ERROR;
	} else if (!is_name(name)) {  // TODO do away with name restriction
	    xerror(0, Ngt("%s: invalid name"), name);
	    err = true;
	    goto next;
	}
	if (funcs) {
	    if (wequal) {
		xerror(0, Ngt("cannot assign function"));
		err = true;
		goto next;
	    }

	    function_T *f = ht_get(&functions, name).value;
	    if (f) {
		if (readonly)
		    f->f_type |= VF_READONLY | VF_NODELETE;
		if (print)
		    print_function(name, f, ARGV(0), readonly);
	    } else {
		xerror(0, Ngt("%s: no such function"), name);
		err = true;
		goto next;
	    }
	} else if (wequal || !print) {
	    /* create/assign variable */
	    variable_T *var = global ? new_global(name) : new_local(name);
	    vartype_T saveexport = var->v_type & VF_EXPORT;
	    if (wequal) {
		if (var->v_type & VF_READONLY) {
		    xerror(0, Ngt("%s: readonly"), name);
		    err = true;
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
	    if (saveexport || (var->v_type & VF_EXPORT))
		update_enrivon(name);
	} else {
	    /* print the variable */
	    variable_T *var = search_variable(name);
	    if (var) {
		print_variable(name, var, ARGV(0), readonly, export);
	    } else {
		xerror(0, Ngt("%s: no such variable"), name);
		err = true;
		goto next;
	    }
	}
next:
	free(name);
    } while (++xoptind < argc);

    return err ? EXIT_FAILURE1 : EXIT_SUCCESS;
}

/* Prints the specified variable to stdout.
 * This function does not print an array.
 * If `readonly'/`export' is true, the variable is printed only if it is
 * readonly/exported. */
static void print_variable(
	const char *name, const variable_T *var,
	const wchar_t *argv0, bool readonly, bool export)
{
    if (readonly && !(var->v_type & VF_READONLY))
	return;
    if (export && !(var->v_type & VF_EXPORT))
	return;
    if ((var->v_type & VF_MASK) == VF_ARRAY)
	return;

    wchar_t *qvalue = var->v_value ? quote_sq(var->v_value) : NULL;
    xstrbuf_T opts;
    switch (argv0[0]) {
	case L's':
	    assert(wcscmp(argv0, L"set") == 0);
	    if (qvalue)
		printf("%s=%ls\n", name, qvalue);
	    break;
	case L'e':
	case L'r':
	    assert(wcscmp(argv0, L"export") == 0
		    || wcscmp(argv0, L"readonly") == 0);
	    printf(qvalue ? "%ls %s=%ls\n" : "%ls %s\n",
		    argv0, name, qvalue);
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
	    printf(qvalue ? "%ls%s %s=%ls\n" : "%ls%s %s\n",
		    argv0, opts.contents, name, qvalue);
	    sb_destroy(&opts);
	    break;
	default:
	    assert(false);
    }
    free(qvalue);
}

void print_function(
	const char *name, const function_T *func,
	const wchar_t *argv0, bool readonly)
{
    if (readonly && !(func->f_type & VF_READONLY))
	return;

    wchar_t *value = command_to_wcs(func->f_body);
    printf("%s () %ls\n", name, value);
    free(value);

    switch (argv0[0]) {
	case L'r':
	    assert(wcscmp(argv0, L"readonly") == 0);
	    if (func->f_type & VF_READONLY)
		printf("%ls -f %s\n", argv0, name);
	    break;
	case L't':
	    assert(wcscmp(argv0, L"typeset") == 0);
	    if (func->f_type & VF_READONLY)
		printf("%ls -fr %s\n", argv0, name);
	    break;
	default:
	    assert(false);
    }
}

const char typeset_help[] = Ngt(
"typeset, export, readonly - set or print variables\n"
"\ttypeset  [-fgprxX] [name[=value]...]\n"
"\texport   [-fgprX]  [name[=value]...]\n"
"\treadonly [-fgpxX]  [name[=value]...]\n"
"For each operands of the form <name>, the variable of the specified name is\n"
"created if not yet created, without assigning any value. If the -p (--print)\n"
"option is specified, the current value and attributes of the variable is\n"
"printed instead of creating the variable.\n"
"For each operands of the form <name=value>, the value is assigned to the\n"
"specified variable. The variable is created if necessary.\n"
"If no operands are given, all variables are printed.\n"
"\n"
"By default, these builtins affect local variables. To declare global\n"
"variables inside functions, the -g (--global) option can be used.\n"
"In POSIXly correct mode, the -g option is always on.\n"
"The -r (--readonly) option makes the specified variables/functions readonly.\n"
"The -x (--export) option makes the variables exported to external commands.\n"
"The -X (--unexport) option undoes the exportation.\n"
"The -f (--functions) option can be used to specify functions instead of\n"
"variables. Functions cannot be assigned or exported with these builtins: the\n"
"-f option can only be used together with the -r or -p option to make\n"
"functions readonly or print them. Functions cannot be exported.\n"
"\n"
"\"export\" is equivalent to \"typeset -x\".\n"
"\"readonly\" is equivalent to \"typeset -r\".\n"
"Note that the typeset builtin is unavailable in the POSIXly correct mode.\n"
);

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
		print_builtin_help(ARGV(0));
		return EXIT_SUCCESS;
	    default:
		fprintf(stderr, gt("Usage:  unset [-fv] name...\n"));
		return EXIT_ERROR;
	}
    }

    for (; xoptind < argc; xoptind++) {
	char *name = malloc_wcstombs(ARGV(xoptind));
	if (!name) {
	    xerror(0, Ngt("unexpected error"));
	    return EXIT_ERROR;
	}

	if (funcs) {
	    /* remove function */
	    kvpair_T kv = ht_remove(&functions, name);
	    function_T *f = kv.value;
	    if (f) {
		if (!(f->f_type & VF_NODELETE)) {
		    funckvfree(kv);
		} else {
		    xerror(0, Ngt("%s: readonly"), name);
		    err = true;
		    ht_set(&functions, kv.key, kv.value);
		}
	    }
	} else {
	    /* remove variable */
	    for (environ_T *env = current_env; env; env = env->parent) {
		kvpair_T kv = ht_remove(&env->contents, name);
		variable_T *var = kv.value;
		if (var) {
		    if (!(var->v_type & VF_NODELETE)) {
			bool ue = var->v_type & VF_EXPORT;
			varkvfree(kv);
			variable_set(name, NULL);
			if (ue)
			    update_enrivon(name);
		    } else {
			xerror(0, Ngt("%s: readonly"), name);
			err = true;
			ht_set(&env->contents, kv.key, kv.value);
		    }
		    break;
		}
	    }
	}
	free(name);
    }

    return err ? EXIT_FAILURE1 : EXIT_SUCCESS;
}

const char unset_help[] = Ngt(
"unset - remove variables or functions\n"
"\tunset [-fv] <name>...\n"
"Removes the specified variables or functions.\n"
"When the -f (--functions) options is specified, this command removes\n"
"functions. When the -v (--variables) option is specified, this command\n"
"removes variables.\n"
"-f and -v are mutually exclusive: the one specified last is used.\n"
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
		print_builtin_help(ARGV(0));
		return EXIT_SUCCESS;
	    default:
		fprintf(stderr, gt("Usage:  shift [n]\n"));
		return EXIT_ERROR;
	}
    }

    size_t scount;
    if (xoptind < argc) {
	long count;
	wchar_t *end;
	errno = 0;
	count = wcstol(ARGV(xoptind), &end, 10);
	if (*end || errno) {
	    xerror(errno, Ngt("`%ls' is not a valid integer"), ARGV(xoptind));
	    return EXIT_ERROR;
	} else if (count < 0) {
	    xerror(0, Ngt("%ls: value must not be negative"), ARGV(xoptind));
	    return EXIT_ERROR;
	}
#if LONG_MAX <= SIZE_MAX
	scount = (size_t) count;
#else
	scount = (count > (long) SIZE_MAX) ? SIZE_MAX : (size_t) count;
#endif
    } else {
	scount = 1;
    }

    variable_T *var = search_variable(VAR_positional);
    assert(var != NULL && (var->v_type & VF_MASK) == VF_ARRAY);
    if (scount > var->v_valc) {
	xerror(0, Ngt("%zu: cannot shift so many"), scount);
	return EXIT_ERROR;
    }
    for (size_t i = 0; i < scount; i++)
	free(var->v_vals[i]);
    var->v_valc -= scount;
    memmove(var->v_vals, var->v_vals + scount,
	    (var->v_valc + 1) * sizeof *var->v_vals);
    return EXIT_SUCCESS;
}

const char shift_help[] = Ngt(
"shift - remove some positional parameters\n"
"\tshift [n]\n"
"Removes the first <n> positional parameters.\n"
"If <n> is not specified, the first one positional parameter is removed.\n"
"<n> must be a non-negative integer that is not greater than $#.\n"
);


/* vim: set ts=8 sts=4 sw=4 noet: */
