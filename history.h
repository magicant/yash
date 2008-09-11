/* Yash: yet another shell */
/* history.h: command history management */
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


#ifndef HISTORY_H
#define HISTORY_H

#include <stddef.h>
#include <stdbool.h>


extern unsigned hist_next_number;

extern void init_history(void);
extern bool read_history(const wchar_t *histfile);
extern bool write_history(const wchar_t *histfile, bool append);
extern void clear_history(void);
extern bool add_history(const wchar_t *line, bool removelast)
    __attribute__((nonnull));

extern int fc_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern const char fc_help[];


#endif /* HISTORY_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
