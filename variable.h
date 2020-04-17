/* Yash: yet another shell */
/* variable.h: deals with shell variables and parameters */
/* (C) 2007-2020 magicant */

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


#ifndef YASH_VARIABLE_H
#define YASH_VARIABLE_H

#include <stddef.h>
#include "xgetopt.h"


extern char **environ;

/* variable names */
#define VAR_CDPATH                    "CDPATH"
#define VAR_COLUMNS                   "COLUMNS"
#define VAR_COMMAND_NOT_FOUND_HANDLER "COMMAND_NOT_FOUND_HANDLER"
#define VAR_DIRSTACK                  "DIRSTACK"
#define VAR_ECHO_STYLE                "ECHO_STYLE"
#define VAR_ENV                       "ENV"
#define VAR_FCEDIT                    "FCEDIT"
#define VAR_HANDLED                   "HANDLED"
#define VAR_HISTFILE                  "HISTFILE"
#define VAR_HISTRMDUP                 "HISTRMDUP"
#define VAR_HISTSIZE                  "HISTSIZE"
#define VAR_HOME                      "HOME"
#define VAR_IFS                       "IFS"
#define VAR_LANG                      "LANG"
#define VAR_LC_ALL                    "LC_ALL"
#define VAR_LC_COLLATE                "LC_COLLATE"
#define VAR_LC_CTYPE                  "LC_CTYPE"
#define VAR_LC_MESSAGES               "LC_MESSAGES"
#define VAR_LC_MONETARY               "LC_MONETARY"
#define VAR_LC_NUMERIC                "LC_NUMERIC"
#define VAR_LC_TIME                   "LC_TIME"
#define VAR_LINENO                    "LINENO"
#define VAR_LINES                     "LINES"
#define VAR_MAIL                      "MAIL"
#define VAR_MAILCHECK                 "MAILCHECK"
#define VAR_MAILPATH                  "MAILPATH"
#define VAR_NLSPATH                   "NLSPATH"
#define VAR_OLDPWD                    "OLDPWD"
#define VAR_OPTARG                    "OPTARG"
#define VAR_OPTIND                    "OPTIND"
#define VAR_PATH                      "PATH"
#define VAR_PPID                      "PPID"
#define VAR_PROMPT_COMMAND            "PROMPT_COMMAND"
#define VAR_PS1                       "PS1"
#define VAR_PS2                       "PS2"
#define VAR_PS4                       "PS4"
#define VAR_PWD                       "PWD"
#define VAR_RANDOM                    "RANDOM"
#define VAR_TARGETWORD                "TARGETWORD"
#define VAR_TERM                      "TERM"
#define VAR_WORDS                     "WORDS"
#define VAR_YASH_AFTER_CD             "YASH_AFTER_CD"
#define VAR_YASH_LE_TIMEOUT           "YASH_LE_TIMEOUT"
#define VAR_YASH_LOADPATH             "YASH_LOADPATH"
#define VAR_YASH_VERSION              "YASH_VERSION"
#define L                             L""

struct variable_T;
struct assign_T;
struct command_T;

typedef enum path_T {
    PA_PATH, PA_CDPATH, PA_LOADPATH,
    PA_count,
} path_T;

extern void init_environment(void);
extern void init_variables(void);

extern char *get_exported_value(const wchar_t *name)
    __attribute__((nonnull,malloc,warn_unused_result));

typedef enum scope_T {
    SCOPE_GLOBAL, SCOPE_LOCAL, SCOPE_TEMP,
} scope_T;
extern _Bool set_variable(
	const wchar_t *name, wchar_t *value, scope_T scope, _Bool export)
    __attribute__((nonnull(1)));
extern struct variable_T *set_array(
	const wchar_t *name, size_t count, void **values,
	scope_T scope, _Bool export)
    __attribute__((nonnull));
extern _Bool set_array_element(
	const wchar_t *name, size_t index, wchar_t *value)
    __attribute__((nonnull));
extern void set_positional_parameters(void *const *values)
    __attribute__((nonnull));
extern _Bool do_assignments(
	const struct assign_T *assigns, _Bool temp, _Bool export);

struct get_variable_T {
    enum { GV_NOTFOUND, GV_SCALAR, GV_ARRAY, GV_ARRAY_CONCAT, } type;
    size_t count;
    void **values;
    _Bool freevalues;
};
extern const wchar_t *getvar(const wchar_t *name)
    __attribute__((pure,nonnull));
extern struct get_variable_T get_variable(const wchar_t *name)
    __attribute__((nonnull,warn_unused_result));
extern void save_get_variable_values(struct get_variable_T *gv)
    __attribute__((nonnull));

extern void open_new_environment(_Bool temp);
extern void close_current_environment(void);

extern void update_lineno(unsigned long lineno);

extern char **decompose_paths(const wchar_t *paths)
    __attribute__((malloc,warn_unused_result));
extern char *const *get_path_array(path_T name);

extern _Bool define_function(const wchar_t *name, struct command_T *body)
    __attribute__((nonnull));
extern struct command_T *get_function(const wchar_t *name)
    __attribute__((nonnull));

#if YASH_ENABLE_DIRSTACK
extern _Bool parse_dirstack_index(
	const wchar_t *restrict indexstr, size_t *restrict indexp,
	const wchar_t **restrict entryp, _Bool printerror)
    __attribute__((nonnull));
#endif

extern int typeset_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char typeset_help[], typeset_syntax[], export_help[],
       export_syntax[], local_help[], local_syntax[], readonly_help[],
       readonly_syntax[];
#endif
extern const struct xgetopt_T typeset_options[];
#define local_options (&typeset_options[2])

extern int array_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char array_help[], array_syntax[];
#endif
extern const struct xgetopt_T array_options[];

extern int unset_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char unset_help[], unset_syntax[];
#endif
extern const struct xgetopt_T unset_options[];

extern int shift_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char shift_help[], shift_syntax[];
#endif
extern const struct xgetopt_T shift_options[];

extern int getopts_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char getopts_help[], getopts_syntax[];
#endif

extern int read_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char read_help[], read_syntax[];
#endif
extern const struct xgetopt_T read_options[];

extern int pushd_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char pushd_help[], pushd_syntax[];
#endif
extern const struct xgetopt_T pushd_options[];
#if YASH_ENABLE_DIRSTACK
# define cd_options  (&pushd_options[1])
# define pwd_options (&pushd_options[2])
#else
# define cd_options  pushd_options
# define pwd_options (&pushd_options[1])
#endif

extern int popd_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char popd_help[], popd_syntax[];
#endif

extern int dirs_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char dirs_help[], dirs_syntax[];
#endif
extern const struct xgetopt_T dirs_options[];


#endif /* YASH_VARIABLE_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
