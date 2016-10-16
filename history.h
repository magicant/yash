/* Yash: yet another shell */
/* history.h: command history management */
/* (C) 2007-2012 magicant */

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
#include "xgetopt.h"


/* The structure type of doubly-linked list node. */
typedef struct histlink_T {
    struct histlink_T *prev, *next;
} histlink_T;
/* `prev' and `next' are always non-NULL: the newest entry's `next' and the
 * oldest entry's `prev' point to `histlist'. */

/* The structure type of history entries. */
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
 * around to 1, so a newer entry may have a smaller number than an older entry.
 * The limit is no less than $HISTSIZE, so all the entries have different
 * numbers anyway. */
/* When the time is unknown, `time' is -1. */

/* The structure type of the history list. */
typedef struct histlist_T {
    histlink_T link;
    unsigned count;
} histlist_T;
#define Newest link.prev
#define Oldest link.next

extern histlist_T histlist;
#define Histlist (&histlist.link)

/* Casts a pointer to `histlink_T' into a pointer to `histentry_T'. */
static inline histentry_T *ashistentry(const histlink_T *link)
{
#ifdef assert
    assert(link != &histlist.link);
#endif
    return (histentry_T *) link;
}

extern unsigned next_history_number(void)
    __attribute__((pure));
extern void maybe_init_history(void);
extern void finalize_history(void);
extern void close_history_file(void);
extern void add_history(const wchar_t *line)
    __attribute__((nonnull));
const histlink_T *get_history_entry(unsigned number)
    __attribute__((pure));
#if YASH_ENABLE_LINEEDIT
extern void start_using_history(void);
extern void end_using_history(void);
#endif

extern int fc_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char fc_help[], fc_syntax[];
#endif
extern const struct xgetopt_T fc_options[];

extern int history_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char history_help[], history_syntax[];
#endif
extern const struct xgetopt_T history_options[];


#endif /* YASH_HISTORY_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
