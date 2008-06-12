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
#include "input.h"
#include "parser.h"
#include "variable.h"
#include "expand.h"
#include "exec.h"
#include "yash.h"
#include "version.h"


const char *path_variables[PATHTCOUNT] = {
    [PA_PATH] = VAR_PATH,
    [PA_CDPATH] = VAR_CDPATH,
};


/* variable environment (= set of variables) */
/* not to be confused with environment variables */
typedef struct environ_T {
    struct environ_T *parent;      /* parent environment */
    struct hashtable_T contents;   /* hashtable containing variables */
    char **paths[PATHTCOUNT];
} environ_T;
/* `contents' is a hashtable from (char *) to (variable_T *).
 * A variable name may contain any characters except '\0' and '=', though
 * assignment syntax allows only limited characters.
 * Variable names starting with '=' are used for special purposes.
 * The positional parameter is treated as an array whose name is "=".
 * Note that the number of positional parameters is offseted by 1 against the
 * array index. */
/* Elements of `path' are arrays of the pathnames contained in the $PATH/CDPATH
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
static bool assign_temporary(const char *name, wchar_t *value, bool export)
    __attribute__((nonnull));

static void lineno_getter(variable_T *var)
    __attribute__((nonnull));
static void random_getter(variable_T *var)
    __attribute__((nonnull));
static unsigned next_random(void);

static void variable_set(const char *name, variable_T *var)
    __attribute__((nonnull));

static void reset_path(path_T name, variable_T *var)
    __attribute__((nonnull));

static void funcfree(function_T *f);
static void funckvfree(kvpair_T kv);

static void hash_all_commands_recursively(const command_T *c);
static void hash_all_commands_in_and_or(const and_or_T *ao);
static void hash_all_commands_in_if(const ifcommand_T *ic);
static void hash_all_commands_in_case(const caseitem_T *ci);
static void tryhash_word_as_command(const wordunit_T *w);


/* the current environment */
static environ_T *current_env;
/* the initial environment (the farthest from the current) */
static environ_T *top_env;
/* whether the current environment is temporary */
static bool current_env_is_temporary;

/* Temporary variables are used when a non-special builtin is executed.
 * They are assigned in a temporary environment and valid only while the command
 * is executed. */

/* whether $RANDOM is functioning as a random number */
static bool random_active;

/* Hashtable from function names (char *) to functions (function_T *). */
static hashtable_T functions;


/* Frees the value of a variable, but not the variable itself. */
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

/* Calls `update_enrivon' for `kv.key' and frees the variable. */
void varkvfree_reexport(kvpair_T kv)
{
    update_enrivon(kv.key);
    varkvfree(kv);
}

static bool initialized = false;

/* Initializes the initial environment. */
void init_variables(void)
{
    if (initialized)
	return;
    initialized = true;

    top_env = current_env = xmalloc(sizeof *current_env);
    current_env->parent = NULL;
    ht_init(&current_env->contents, hashstr, htstrcmp);
    for (size_t i = 0; i < PATHTCOUNT; i++)
	current_env->paths[i] = NULL;

    ht_init(&functions, hashstr, htstrcmp);

    /* add all the existing environment variables to the variable environment */
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

    /* set $LINENO */
    {
	variable_T *v = new_global(VAR_LINENO);
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
	    !posixly_correct ? L"\\$ " : geteuid() ? L"$ " : L"# ";
	set_variable(VAR_PS1, xwcsdup(ps1),    false, false);
	set_variable(VAR_PS2, xwcsdup(L"> "),  false, false);
	if (!posixly_correct)
	    set_variable(VAR_PS3, xwcsdup(L"#? "), false, false);
	set_variable(VAR_PS4, xwcsdup(L"+ "),  false, false);
    }

    /* set $PWD */
    init_pwd();

    /* set $PPID */
    set_variable(VAR_PPID, malloc_wprintf(L"%jd", (intmax_t) getppid()),
	    false, false);

    /* set $RANDOM */
    if (!posixly_correct && !getvar(VAR_RANDOM)) {
	variable_T *v = new_global(VAR_RANDOM);
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
variable_T *search_variable(const char *name, bool temp)
{
    variable_T *var;
    environ_T *env = current_env;
    if (!temp && current_env_is_temporary)
	env = env->parent;
    while (env) {
	var = ht_get(&env->contents, name).value;
	if (var)
	    return var;
	env = env->parent;
    }
    return NULL;
}

/* Update the value in `environ' for the variable with the specified name. */
void update_enrivon(const char *name)
{
    environ_T *env = current_env;
    while (env) {
	variable_T *var = ht_get(&env->contents, name).value;
	if (var && (var->v_type & VF_EXPORT) && var->v_value) {
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

/* Resets the locate settings for the specified variable.
 * If `name' is not any of "LANG", "LC_ALL", etc., does nothing. */
void reset_locale(const char *name)
{
    if (strcmp(name, VAR_LANG) == 0) {
	goto reset_locale_all;
    } else if (strncmp(name, "LC_", 3) == 0) {
	/* POSIX forbids resetting LC_CTYPE even if the value of the variable
	 * is changed. */
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

/* Creates a new global variable if there is none.
 * If a local variable already exists, it is cleared and returned.
 *
 * On error, NULL is returned. Otherwise a (new) variable is returned.
 * `v_type' is the only valid member of the variable; the other members must be
 * initialized by the caller. If `v_type' of the return value has the VF_EXPORT
 * flag set, the caller must call `update_enrivon'.
 *
 * Temporary variables are ignored. */
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
	varvaluefree(var);
    }
    return var;
}

/* Creates a new local variable.
 * If a variable already exists, it is cleared and returned.
 *
 * On error, NULL is returned. Otherwise a (new) variable is returned.
 * `v_type' is the only valid member of the variable; the other members must be
 * initialized by the caller. If `v_type' of the return value has the VF_EXPORT
 * flag set, the caller must call `update_enrivon'.
 *
 * Temporary variables are ignored. */
variable_T *new_local(const char *name)
{
    environ_T *env = current_env;
    if (current_env_is_temporary)
	env = env->parent;
    variable_T *var = ht_get(&env->contents, name).value;
    if (!var) {
	var = xmalloc(sizeof *var);
	var->v_type = 0;
	ht_set(&env->contents, xstrdup(name), var);
    } else if (var->v_type & VF_READONLY) {
	xerror(0, Ngt("%s: readonly"), name);
	return NULL;
    } else {
	varvaluefree(var);
    }
    return var;
}

/* Creates a scalar variable with the specified name and value.
 * `value' must be a `free'able string. The caller must not modify or `free'
 * `value' hereafter, whether or not this function is successful.
 * If `export' is true, the variable is exported. `export' being false doesn't
 * mean the variable is no longer exported.
 * Returns true iff successful. */
bool set_variable(const char *name, wchar_t *value, bool local, bool export)
{
    variable_T *var = local ? new_local(name) : new_global(name);
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
 * Returns true iff successful. */
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

/* Sets the positional parameters of the current environment.
 * The existent parameters are cleared.
 * `values[0]' will be the new $1, `values[1]' $2, and so on.
 * When a new environment is created, this function must be called at least once
 * except for a temporary environment. */
void set_positional_parameters(char *const *values)
{
    set_array(VAR_positional, values, !current_env_is_temporary);
}

/* Does an assignment to a temporary variable.
 * `value' must be a `free'able string. The caller must not modify or `free'
 * `value' hereafter, whether or not this function is successful.
 * If `export' is true, the variable is exported. `export' being false doesn't
 * mean the variable is no longer exported.
 * Returns true iff successful. */
bool assign_temporary(const char *name, wchar_t *value, bool export)
{
    if (!current_env_is_temporary) {
	open_new_environment();
	current_env_is_temporary = true;
    }

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
    if (export) {
	var->v_type |= VF_EXPORT;
	update_enrivon(name);
    }
    return true;
}

/* Does assignments.
 * If `shopt_xtrace' is true, traces are printed to stderr.
 * If `export' is true, the variables are exported. `export' being false doesn't
 * mean the variables are no longer exported.
 * Returns true iff successful. An error on an assignment may leave some other
 * variables assigned. */
bool do_assignments(const assign_T *assign, bool temp, bool export)
{
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
    variable_T *var = search_variable(name, true);
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
		value = malloc_mbstowcs(command_name);
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
	var = search_variable(VAR_positional, false);
	assert(var != NULL && (var->v_type & VF_MASK) == VF_ARRAY);
	if (v <= 0 || (uintmax_t) var->v_valc < (uintmax_t) v)
	    return NULL;  /* index out of bounds */
	value = xwcsdup(var->v_vals[v - 1]);
	goto return_single;
    }

    /* now it should be a normal variable */
    var = search_variable(name, true);
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

/* Creates a new variable environment.
 * The current environment will be the parent of the new environment.
 * Don't forget to call `set_positional_parameters'. */
void open_new_environment(void)
{
    environ_T *newenv = xmalloc(sizeof *newenv);

    assert(!current_env_is_temporary);
    newenv->parent = current_env;
    ht_init(&newenv->contents, hashstr, htstrcmp);
    for (size_t i = 0; i < PATHTCOUNT; i++)
	newenv->paths[i] = NULL;
    current_env = newenv;
}

/* Closes the current variable environment.
 * The parent of the current will be the new current. */
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

/* Clears all temporary variables. */
void clear_temporary_variables(void)
{
    if (current_env_is_temporary) {
	close_current_environment();
	current_env_is_temporary = false;
    }
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

/* general callback that is called after an assignment. */
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
	    if ((var->v_type & VF_MASK) == VF_NORMAL && var->v_value) {
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
	// TODO variable: variable_set: other variables
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

/* Reconstructs the path array of the specified variable in the environment. */
void reset_path(path_T name, variable_T *var)
{
    if (name == PA_PATH)
	clear_cmdhash();

    environ_T *env = current_env;
    while (env) {
	recfree((void **) env->paths[name], free);

	variable_T *v = ht_get(&env->contents, path_variables[name]).value;
	if (v) {
	    switch (v->v_type & VF_MASK) {
		    plist_T list;
		case VF_NORMAL:
		    env->paths[name] = decompose_paths(v->v_value);
		    break;
		case VF_ARRAY:
		    pl_init(&list);
		    pl_setmax(&list, v->v_valc);
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
	}
	env = env->parent;
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


/* vim: set ts=8 sts=4 sw=4 noet: */
