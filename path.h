/* Yash: yet another shell */
/* path.h: path manipulaiton utilities */
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
#include <sys/stat.h>


char *which(const char *name, const char *path, bool (*cond)(const char *name))
	__attribute__((malloc));
bool is_executable(const char *path);
bool is_directory(const char *path);
char *expand_tilde(const char *path)
	__attribute__((malloc, nonnull));
char *expand_tilde_multiple(const char *paths)
	__attribute__((malloc, nonnull));
char *skip_homedir(const char *path);
char *collapse_homedir(const char *path)
	__attribute__((malloc));
char *canonicalize_path(const char *path)
	__attribute__((malloc));

extern struct hasht cmdhash;

void init_cmdhash(void);
void clear_cmdhash(void);
void fill_cmdhash(void);
const char *get_command_fullpath(const char *name, bool forcelookup)
	__attribute__((nonnull));


#endif /* PATH_H */
