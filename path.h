/* Yash: yet another shell */
/* path.h: path manipulation utilities and command path hashtable */
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


#ifndef PATH_H
#define PATH_H

#include <stdbool.h>
#include <stddef.h>
#include <dirent.h>


__attribute__((nonnull))
extern bool is_readable(const char *path);
__attribute__((nonnull))
extern bool is_executable(const char *path);
__attribute__((nonnull))
extern bool is_directory(const char *path);

__attribute__((nonnull))
extern bool is_same_file(const char *path1, const char *path2);

__attribute__((nonnull,malloc,warn_unused_result))
extern wchar_t *canonicalize_path(const wchar_t *path);

__attribute__((nonnull(1),malloc,warn_unused_result))
extern char *which(
	const char *restrict name,
	char *const *restrict dirs,
	bool cond(const char *path));

__attribute__((nonnull))
extern int xclosedir(DIR *dir);


/********** patharray **********/

extern void reset_patharray(const char *newpath);


/********** コマンド名ハッシュ **********/

extern void init_cmdhash(void);
__attribute__((nonnull))
extern const char *get_command_path(const char *name, bool forcelookup);
extern void fill_cmdhash(const char *prefix, bool ignorecase);


/********** wglob **********/

enum wglbflags {
    WGLB_ERR      = 1 << 0,
    WGLB_MARK     = 1 << 1,
    WGLB_NOESCAPE = 1 << 2,
    WGLB_CASEFOLD = 1 << 3,
    WGLB_NOSORT   = 1 << 4,
    WGLB_RECDIR   = 1 << 5,
    WGLB_recdir   = 1 << 6,
    WGLB_followlk = 1 << 7,
};

struct plist_T;

__attribute__((nonnull))
extern bool wglob(const wchar_t *restrict pattern, enum wglbflags flags,
	struct plist_T *restrict list);


#endif /* PATH_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
