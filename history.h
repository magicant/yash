/* Yash: yet another shell */
/* history.h: command history management */
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


#ifndef YASH_HISTORY_H
#define YASH_HISTORY_H

#include <stddef.h>
#include <sys/types.h>


typedef struct histlink_T {
    struct histentry_T *prev, *next;
} histlink_T;
/* `prev' and `next' are always non-NULL: the newest entry's `next' and the
 * oldest entry's `prev' point to `histlist'. */

/* Structure of history entries. */
typedef struct histentry_T {
    histlink_T link;
    unsigned number;
    time_t time;
    char value[];
} histentry_T;
#define Prev link.prev
#define Next link.next
/* The value is stored as a multibyte string rather than a wide string to save
 * memory space. The value must not contain newlines. */
/* Basically the `number' is increased for each entry, but the numbers are
 * limited to some extent. If the number exceeds the limit, it is wrapped
 * around to 1, so a newer entry may have a smaller number than that of a older
 * entry. The limit is no less than $HISTSIZE, so all the entries have different
 * numbers anyway. */
/* When the time is unknown, `time' is -1. */

#define Histlist ((histentry_T *) &histlist)
#define Newest link.prev
#define Oldest link.next
/* Structure of the history list. */
typedef struct histlist_T {
    histlink_T link;
    unsigned count;
} histlist_T;

extern unsigned hist_next_number;

extern void maybe_init_history(void);
extern void finalize_history(void);
extern void add_history(const wchar_t *line)
    __attribute__((nonnull));
extern void start_using_history(void);
extern void end_using_history(void);

extern int fc_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern int history_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern const char fc_help[], history_help[];


#endif /* YASH_HISTORY_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
