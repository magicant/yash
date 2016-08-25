/* Yash: yet another shell */
/* path.h: filename-related utilities */
/* (C) 2007-2016 magicant */

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


#ifndef YASH_PATH_H
#define YASH_PATH_H

#include <stddef.h>
#include <sys/types.h>
#include "xgetopt.h"

struct stat;


extern _Bool is_file(const char *path)
    __attribute__((nonnull));
extern _Bool is_regular_file(const char *path)
    __attribute__((nonnull));
extern _Bool is_irregular_file(const char *path)
    __attribute__((nonnull));
extern _Bool is_readable(const char *path)
    __attribute__((nonnull));
extern _Bool is_writable(const char *path)
    __attribute__((nonnull));
extern _Bool is_executable(const char *path)
    __attribute__((nonnull));
extern _Bool is_readable_regular(const char *path)
    __attribute__((nonnull));
extern _Bool is_executable_regular(const char *path)
    __attribute__((nonnull));
extern _Bool is_directory(const char *path)
    __attribute__((nonnull));

extern _Bool stat_result_same_file(
	const struct stat *stat1, const struct stat *stat2)
    __attribute__((nonnull,pure));
extern _Bool is_same_file(const char *path1, const char *path2)
    __attribute__((nonnull));

extern _Bool is_normalized_path(const wchar_t *path)
    __attribute__((nonnull));

extern char *xgetcwd(void)
    __attribute__((malloc,warn_unused_result));

extern char *which(
	const char *restrict name,
	char *const *restrict dirs,
	_Bool cond(const char *path))
    __attribute__((nonnull(1),malloc,warn_unused_result));

extern int create_temporary_file(
	char **restrict filename, const char *restrict suffix, mode_t mode)
    __attribute__((nonnull));


/********** Command Hashtable **********/

extern void init_cmdhash(void);
extern void clear_cmdhash(void);
extern const char *get_command_path(const char *name, _Bool forcelookup)
    __attribute__((nonnull));
extern void fill_cmdhash(const char *prefix, _Bool ignorecase);
extern const char *get_command_path_default(const char *name)
    __attribute__((nonnull));


/********** Home Directory Cache **********/

extern void init_homedirhash(void);
extern const wchar_t *get_home_directory(
	const wchar_t *username, _Bool forcelookup)
    __attribute__((nonnull));


/********** wglob **********/

enum wglobflags_T {
    WGLB_MARK     = 1 << 0,
    WGLB_CASEFOLD = 1 << 1,
    WGLB_PERIOD   = 1 << 2,
    WGLB_NOSORT   = 1 << 3,
    WGLB_RECDIR   = 1 << 4,
};

struct plist_T;

extern _Bool wglob(const wchar_t *restrict pattern, enum wglobflags_T flags,
	struct plist_T *restrict list)
    __attribute__((nonnull));


/********** Built-ins **********/

extern int cd_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char cd_help[], cd_syntax[];
#endif

extern int pwd_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char pwd_help[], pwd_syntax[];
#endif

extern int hash_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char hash_help[], hash_syntax[];
#endif
extern const struct xgetopt_T hash_options[];

extern int umask_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char umask_help[], umask_syntax[];
#endif
extern const struct xgetopt_T umask_options[];

extern int change_directory(
	const wchar_t *newpwd, _Bool printnewdir, _Bool logical)
    __attribute__((nonnull,warn_unused_result));


#endif /* YASH_PATH_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
