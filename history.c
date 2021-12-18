/* Yash: yet another shell */
/* history.c: command history management */
/* (C) 2007-2021 magicant */

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
#include "history.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include "builtin.h"
#include "exec.h"
#include "job.h"
#include "option.h"
#include "path.h"
#include "redir.h"
#include "sig.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"
#include "yash.h"


/* The maximum size of history list (<= INT_MAX / 10) */
#ifndef MAX_HISTSIZE
#define MAX_HISTSIZE     1000000
#endif
#if MAX_HISTSIZE > INT_MAX / 10
#error MAX_HISTSIZE is too large
#endif
/* The minimum value of `max_number',
 * which must be at least 32768 according to POSIX. */
/* Must be a power of 10. */
#ifndef HISTORY_MIN_MAX_NUMBER
#define HISTORY_MIN_MAX_NUMBER   100000
#endif
#if HISTORY_MIN_MAX_NUMBER < 32768
#error HISTORY_MIN_MAX_NUMBER is too small
#endif
/* The default size of the history list,
 * which must be at least 128 according to POSIX. */
#ifndef DEFAULT_HISTSIZE
#define DEFAULT_HISTSIZE 500
#endif
#if DEFAULT_HISTSIZE < 128
#error DEFAULT_HISTSIZE is too small
#endif
#if DEFAULT_HISTSIZE > HISTORY_MIN_MAX_NUMBER
#error DEFAULT_HISTSIZE cannot be larger than HISTORY_MIN_MAX_NUMBER
#endif

#ifndef HISTORY_DEFAULT_LINE_LENGTH
#define HISTORY_DEFAULT_LINE_LENGTH 127
#endif


/* The main history list. */
histlist_T histlist = {
    .link = { Histlist, Histlist, },
    .count = 0,
};
/* History entries are stored in this doubly-linked list.
 * The `Newest' (`link.prev') member points to the newest entry and the `Oldest'
 * (`link.next') member points to the oldest entry. When there's no entries,
 * `Newest' and `Oldest' point to `histlist' itself. */

/* The maximum limit of the number of an entry.
 * Must always be no less than `histsize' or `HISTORY_MIN_MAX_NUMBER'.
 * The number of any entry is not greater than this value. */
static unsigned max_number = HISTORY_MIN_MAX_NUMBER;
/* The size limit of the history list. */
static unsigned histsize = DEFAULT_HISTSIZE;
/* When a new entry is added, if there are entries that has the same value as
 * the new entry in the `histrmdup' newest entries, those entries are removed.*/
static unsigned histrmdup = 0;

/* File stream for the history file. */
static FILE *histfile = NULL;
/* The revision number of the history file. A valid revision number is
 * non-negative. */
static long histfilerev = -1;
/* The number of lines in the history file, not including the signature.
 * When this number reaches the threshold, the history file is refreshed. */
static size_t histfilelines = 0;
/* Indicates if the history file should be flushed before it is unlocked. */
static bool histneedflush = false;

/* The current time returned by `time' */
static time_t now = (time_t) -1;

/* If true, the history is locked, that is, readonly. */
static bool hist_lock = false;


struct search_result_T {
    histlink_T *prev, *next;
};

static void update_time(void);
static void set_histsize(unsigned newsize);
static histentry_T *new_entry(unsigned number, time_t time, const char *line)
    __attribute__((nonnull));
static bool need_remove_entry(unsigned number)
    __attribute__((pure));
static void remove_entry(histentry_T *e)
    __attribute__((nonnull));
static void remove_last_entry(void);
static void clear_all_entries(void);
static struct search_result_T search_entry_by_number(unsigned number)
    __attribute__((pure));
static histlink_T *get_nth_newest_entry(unsigned n)
    __attribute__((pure));
static histlink_T *search_entry_by_prefix(const char *prefix)
    __attribute__((nonnull,pure));
static bool search_result_is_newer(
	struct search_result_T sr1, struct search_result_T sr2)
    __attribute__((pure));
static bool entry_is_newer(const histentry_T *e1, const histentry_T *e2)
    __attribute__((nonnull,pure));

static void add_histfile_pid(pid_t pid);
static void remove_histfile_pid(pid_t pid);
static void clear_histfile_pids(void);
static void write_histfile_pids(void);

static FILE *open_histfile(void);
static bool lock_histfile(short type);
static bool read_line(FILE *restrict f, xwcsbuf_T *restrict buf)
    __attribute__((nonnull));
static bool try_read_line(FILE *restrict f, xwcsbuf_T *restrict buf)
    __attribute__((nonnull));
static long read_signature(void);
static void read_history_raw(void);
static void read_history(void);
static void parse_history_entry(const wchar_t *line)
    __attribute__((nonnull));
static void parse_removed_entry(const wchar_t *numstr)
    __attribute__((nonnull));
static void parse_process_id(const wchar_t *numstr)
    __attribute__((nonnull));
static void update_history(bool refresh);
static void maybe_refresh_file(void);
static int wprintf_histfile(const wchar_t *format, ...)
    __attribute__((nonnull));
static void write_signature(void);
static void write_history_entry(const histentry_T *entry)
    __attribute__((nonnull));
static void refresh_file(void);

static void add_history_line(const wchar_t *line, size_t maxlen)
    __attribute__((nonnull));
static void remove_duplicates(const char *line)
    __attribute__((nonnull));


/* Updates the value of `now'. */
void update_time(void)
{
    now = time(NULL);
}

/* Changes `histsize' to `newsize' and accordingly sets `max_number'. */
/* `histsize' is set only once in initialization. */
void set_histsize(unsigned newsize)
{
    if (newsize > MAX_HISTSIZE)
	newsize = MAX_HISTSIZE;
    if (newsize == 0)
	newsize = 1;
    histsize = newsize;

    max_number = HISTORY_MIN_MAX_NUMBER;
    while (max_number < 2 * histsize)
	max_number *= 10;
    /* `max_number' is the smallest power of 10 that is not less than
     * 2 * histsize. */
}

/* Adds a new history entry to the end of `histlist'.
 * Some oldest entries may be removed in this function if they conflict with the
 * new one or the list is full. */
histentry_T *new_entry(unsigned number, time_t time, const char *line)
{
    assert(!hist_lock);
    assert(number > 0);
    assert(number <= max_number);

    while (need_remove_entry(number))
	remove_entry(ashistentry(histlist.Oldest));

    histentry_T *new = xmallocs(sizeof *new,
	    add(strlen(line), 1), sizeof *new->value);
    new->Prev = histlist.Newest;
    new->Next = Histlist;
    histlist.Newest = new->Prev->next = &new->link;
    new->number = number;
    new->time = time;
    strcpy(new->value, line);

    histlist.count++;
    assert(histlist.count <= histsize);

    return new;
}

bool need_remove_entry(unsigned number)
{
    if (histlist.count == 0)
	return false;
    if (histlist.count >= histsize)
	return true;

    unsigned oldest = ashistentry(histlist.Oldest)->number;
    unsigned newest = ashistentry(histlist.Newest)->number;
    if (oldest <= newest)
	return oldest <= number && number <= newest;
    else
	return oldest <= number || number <= newest;
}

/* Removes the specified entry from `histlist'. */
void remove_entry(histentry_T *entry)
{
    assert(!hist_lock);
    assert(&entry->link != Histlist);
    entry->Prev->next = entry->Next;
    entry->Next->prev = entry->Prev;
    histlist.count--;
    free(entry);
}

/* Removes the newest entry. */
void remove_last_entry(void)
{
    if (histlist.count > 0)
	remove_entry(ashistentry(histlist.Newest));
}

/* Renumbers all the entries in `histlist', starting from 1. */
void renumber_all_entries(void)
{
    assert(!hist_lock);

    if (histlist.count > 0) {
	unsigned num = 0;
	for (histlink_T *l = histlist.Oldest; l != Histlist; l = l->next)
	    ashistentry(l)->number = ++num;
	assert(num == histlist.count);
    }
}

/* Removes all entries in the history list. */
void clear_all_entries(void)
{
    assert(!hist_lock);

    histlink_T *l = histlist.Oldest;
    while (l != Histlist) {
	histlink_T *next = l->next;
	free(ashistentry(l));
	l = next;
    }
    histlist.Oldest = histlist.Newest = Histlist;
    histlist.count = 0;
}

/* Searches for the entry that has the specified `number'.
 * If there is such an entry in the history, the both members of the returned
 * `search_result_T' structure will be pointers to the entry. Otherwise, the
 * returned structure will contain pointers to the nearest neighbor entries.
 * (If there is not the neighbor in either direction, the pointer will be
 * `Histlist'.) */
struct search_result_T search_entry_by_number(unsigned number)
{
    struct search_result_T result;

    if (histlist.count == 0) {
	result.prev = result.next = Histlist;
	return result;
    }

    unsigned oldestnum = ashistentry(histlist.Oldest)->number;
    unsigned nnewestnum = ashistentry(histlist.Newest)->number;
    unsigned nnumber = number;
    if (nnewestnum < oldestnum) {
	if (nnumber <= nnewestnum)
	    nnumber += max_number;
	nnewestnum += max_number;
    }
    if (nnumber < oldestnum) {
	result.prev = Histlist;
	result.next = histlist.Oldest;
	return result;
    } else if (nnumber > nnewestnum) {
	result.prev = histlist.Newest;
	result.next = Histlist;
	return result;
    }

    histlink_T *l;
    if (2 * (nnumber - oldestnum) < nnewestnum - oldestnum) {
	/* search from the oldest */
	l = histlist.Oldest;
	while (number < ashistentry(l)->number)
	    l = l->next;
	while (number > ashistentry(l)->number)
	    l = l->next;
	result.next = l;
	if (number != ashistentry(l)->number)
	    l = l->prev;
	result.prev = l;
    } else {
	/* search from the newest */
	l = histlist.Newest;
	while (number > ashistentry(l)->number)
	    l = l->prev;
	while (number < ashistentry(l)->number)
	    l = l->prev;
	result.prev = l;
	if (number != ashistentry(l)->number)
	    l = l->next;
	result.next = l;
    }
    return result;
}

/* Returns the nth newest entry (or the oldest entry if `n' is too big).
 * Returns `Histlist' if `n' is zero or the history is empty. */
histlink_T *get_nth_newest_entry(unsigned n)
{
    if (histlist.count <= n)
	return histlist.Oldest;

    histlink_T *l = Histlist;
    while (n-- > 0)
	l = l->prev;
    return l;
}

/* Searches for the newest entry whose value begins with the specified `prefix'.
 * Returns `Histlist' if not found. */
histlink_T *search_entry_by_prefix(const char *prefix)
{
    histlink_T *l;
    for (l = histlist.Newest; l != Histlist; l = l->prev)
	if (matchstrprefix(ashistentry(l)->value, prefix) != NULL)
	    break;
    return l;
}

/* Returns true iff `sr1' is newer than `sr2'.
 * For each search_result_T structure, the `prev' and `next' members must not be
 * both `Histlist'. */
bool search_result_is_newer(
	struct search_result_T sr1, struct search_result_T sr2)
{
    if (sr1.prev == Histlist || sr2.next == Histlist)
	return false;
    if (sr1.next == Histlist || sr2.prev == Histlist)
	return true;

    return entry_is_newer(ashistentry(sr1.prev), ashistentry(sr2.prev))
	|| entry_is_newer(ashistentry(sr1.next), ashistentry(sr2.next));
}

/* Returns true iff `e1' is newer than `e2'. */
bool entry_is_newer(const histentry_T *e1, const histentry_T *e2)
{
    assert(histlist.count > 0);

    unsigned n1 = e1->number;
    unsigned n2 = e2->number;
    unsigned newest = ashistentry(histlist.Newest)->number;
    unsigned oldest = ashistentry(histlist.Oldest)->number;

    return (n1 <= newest && newest < oldest && oldest <= n2)
	|| (n2 <= n1 && (oldest <= n2 || n1 <= newest));
}


/********** Process ID list **********/

struct pidlist_T {
    size_t count;
    pid_t *pids;
};

/* The process IDs of processes that share the history file.
 * The `pids' member may be null when the `count' member is zero. */
static struct pidlist_T histfilepids = { 0, NULL, };

/* Adds `pid' to `histfilepids'. `pid' must be positive. */
void add_histfile_pid(pid_t pid)
{
    assert(pid > 0);

    for (size_t i = 0; i < histfilepids.count; i++)
	if (histfilepids.pids[i] == pid)
	    return;  /* don't add if already added */

    histfilepids.pids = xrealloce(histfilepids.pids,
	    histfilepids.count, 1, sizeof *histfilepids.pids);
    histfilepids.pids[histfilepids.count++] = pid;
}

/* If `pid' is non-zero, removes `pid' from `histfilepids'.
 * If `pid' is zero, removes process IDs of non-existent processes from
 * `histfilepids'. */
void remove_histfile_pid(pid_t pid)
{
    for (size_t i = 0; i < histfilepids.count; ) {
	if (pid != 0 ? histfilepids.pids[i] == pid
	             : !process_exists(histfilepids.pids[i])) {
	    memmove(&histfilepids.pids[i], &histfilepids.pids[i + 1],
		    (histfilepids.count - i - 1) * sizeof *histfilepids.pids);
	    histfilepids.count--;
	    histfilepids.pids = xreallocn(histfilepids.pids,
		    histfilepids.count, sizeof *histfilepids.pids);
	} else {
	    i++;
	}
    }
}

/* Clears `histfilepids'. */
void clear_histfile_pids(void)
{
    free(histfilepids.pids);
    histfilepids = (struct pidlist_T) { 0, NULL, };
}

/* Writes process IDs in `histfilepids' to the history file. */
/* This function does not return any error status. The caller should check
 * `ferror' for the file. */
void write_histfile_pids(void)
{
    assert(histfile != NULL);
    for (size_t i = 0; i < histfilepids.count; i++)
	wprintf_histfile(L"p%jd\n", (intmax_t) histfilepids.pids[i]);
    histfilelines += histfilepids.count;
}


/********** History file functions **********/

/***** FORMAT OF THE HISTORY FILE *****
 *
 * The first line of the history file has the following form:
 *    #$# yash history v0 rXXX
 * where `XXX' is the revision number of the file. The revision number is
 * incremented each time the file is refreshed.
 * 
 * The rest of the file consists of lines containing history data, each entry
 * per line. The type of entry is determined by the first character of the line:
 *    0-9 or A-F    history entry
 *    c             history entry cancellation
 *    d             history entry deletion
 *    p             shell process addition/elimination info
 *    others        ignored line
 *
 * A history entry has the following form:
 *    NNN:TTT COMMAND
 * where `NNN' is the entry number, `TTT' is the time of the command, and
 * `COMMAND' is the command. Both `NNN' and `TTT' are uppercase hexadecimal
 * non-negative numbers. `COMMAND' may contain any characters except newline and
 * null.
 *
 * A history entry cancellation consists of a single character `c'. It cancels
 * the previous history entry.
 *
 * A history entry deletion has the form:
 *    dNNN
 * where `NNN' is the number of the entry to be removed (hexadecimal).
 *
 * A shell process addition/elimination is in the form:
 *    pXXX
 * where `XXX' is the process id (decimal integer). For addition `XXX' is
 * positive and for elimination `XXX' is negative.
 */

/* Opens the history file.
 * Returns NULL on failure. */
FILE *open_histfile(void)
{
    const wchar_t *vhistfile = getvar(L VAR_HISTFILE);
    if (vhistfile == NULL)
	return NULL;

    char *mbshistfile = malloc_wcstombs(vhistfile);
    if (mbshistfile == NULL)
	return NULL;

    int fd = open(mbshistfile, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    free(mbshistfile);
    if (fd < 0)
	return NULL;

    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)
	    || (st.st_mode & (S_IRWXG | S_IRWXO))) {
	xclose(fd);
	return NULL;
    }

    fd = move_to_shellfd(fd);
    if (fd < 0)
	return NULL;

    FILE *f = fdopen(fd, "r+");
    if (f == NULL) {
	remove_shellfd(fd);
	xclose(fd);
    }
    return f;
}

/* Locks the history file, which must have been open.
 * `type' must be one of `F_RDLCK', `F_WRLCK' and `F_UNLCK'.
 * If `type' is `F_UNLCK', the buffer for the history file is flushed before
 * unlocking the file.
 * When another process is holding a lock for the file, this process will be
 * blocked until the lock is freed.
 * Returns true iff successful. */
bool lock_histfile(short type)
{
    if (type == F_UNLCK && histneedflush) {
	histneedflush = false;
	fflush(histfile);
	/* We only flush the history file after writing. POSIX doesn't define
	 * the behavior of flush without writing. We don't use fseek instead of
	 * fflush because fseek is less reliable than fflush. In some
	 * implementations (including glibc), fseek doesn't flush the file. */
    }

    struct flock flock = {
	.l_type   = type,
	.l_whence = SEEK_SET,
	.l_start  = 0,
	.l_len    = 0, /* to the end of file */
    };
    int fd = fileno(histfile);
    int result;

    while ((result = fcntl(fd, F_SETLKW, &flock)) == -1 && errno == EINTR);
    return result != -1;
}

/* Reads one line from file `f'.
 * The line is appended to buffer `buf', which must have been initialized.
 * The terminating newline is not left in `buf'.
 * On failure, false is returned, in which case the contents of `buf' is
 * unspecified.
 * If there is no more line in the file, false is returned.
 * This function may ignore lines longer than LINE_MAX. */
bool read_line(FILE *restrict f, xwcsbuf_T *restrict buf)
{
    bool accept_current_line = true;
    size_t initial_length = buf->length;

    for (;;) {
	if (try_read_line(f, buf)) {
	    if (accept_current_line)
		return true;

	    /* Read one more line and accept it. */
	    accept_current_line = true;
	} else {
	    if (feof(f) || ferror(f))
		return false;

	    /* Now, the position of `f' is in the middle of a very long line
	     * that should be ignored. */
	    accept_current_line = false;
	}
	wb_truncate(buf, initial_length);
    }
}

/* Reads one line from file `f'.
 * The line is appended to buffer `buf', which must have been initialized.
 * If a line was read successfully, true is returned. The terminating newline is
 * not left in `buf'.
 * The return value is false if:
 *  * an error occurred (ferror()),
 *  * the end of the input was reached (feof()), or
 *  * the current line is too long (at least LINE_MAX characters), in which case
 *  the position of `f' is left in the middle of the current line.
 * The contents of `buf' is unspecified if the return value is false. */
bool try_read_line(FILE *restrict f, xwcsbuf_T *restrict buf)
{
#if FGETWS_BROKEN
    wb_ensuremax(buf, LINE_MAX);
#endif
    while (fgetws(&buf->contents[buf->length],
		buf->maxlength - buf->length + 1, f)) {
	size_t len = wcslen(&buf->contents[buf->length]);
	if (len == 0)
	    return false;
	buf->length += len;
	if (buf->contents[buf->length - 1] == L'\n') {
	    wb_truncate(buf, buf->length - 1);
	    return true;
	}
	if (buf->length > LINE_MAX)
	    return false;  /* Too long line. Give up. */
	wb_ensuremax(buf, add(buf->length, 80));
    }
    return false;
}

/* Reads the signature of the history file (`histfile') and checks if it is a
 * valid signature.
 * If valid:
 *   - the file is positioned just after the signature,
 *   - the return value is the revision of the file (non-negative).
 * Otherwise:
 *   - the file position is undefined,
 *   - the return value is negative. */
/* The history file should be locked. */
long read_signature(void)
{
    xwcsbuf_T buf;
    long rev = -1;
    const wchar_t *s;

    assert(histfile != NULL);
    rewind(histfile);
    if (!read_line(histfile, wb_initwithmax(&buf, HISTORY_DEFAULT_LINE_LENGTH)))
	goto end;
    
    s = matchwcsprefix(buf.contents, L"#$# yash history v0 r");
    if (s == NULL || !iswdigit(s[0]))
	goto end;

    if (!xwcstol(s, 10, &rev))
	rev = -1;
end:
    wb_destroy(&buf);
    return rev;
}

/* Reads history entries from the history file, which must have been open.
 * The file format is assumed a simple text, one entry per line.
 * The file is read from the current position.
 * The entries that were read from the file are appended to `histlist'. */
/* The file should be locked. */
/* This function does not return any error status. The caller should check
 * `ferror' and/or `feof' for the file. */
void read_history_raw(void)
{
    xwcsbuf_T buf;

    assert(histfile != NULL);
    wb_initwithmax(&buf, HISTORY_DEFAULT_LINE_LENGTH);
    while (read_line(histfile, &buf)) {
	char *line = malloc_wcstombs(buf.contents);
	if (line != NULL) {
	    new_entry(next_history_number(), -1, line);
	    free(line);
	}
	wb_clear(&buf);
    }
    wb_destroy(&buf);
}

/* Reads history entries from the history file.
 * The file is read from the current position.
 * The entries that were read from the file are appended to `histlist'.
 * `update_time' must be called before calling this function. */
/* The file should be locked. */
/* This function does not return any error status. The caller should check
 * `ferror' and/or `feof' for the file. */
void read_history(void)
{
    xwcsbuf_T buf;

    assert(histfile != NULL);
    wb_initwithmax(&buf, HISTORY_DEFAULT_LINE_LENGTH);
    while (read_line(histfile, &buf)) {
	histfilelines++;
	switch (buf.contents[0]) {
	    case L'0': case L'1': case L'2': case L'3': case L'4':
	    case L'5': case L'6': case L'7': case L'8': case L'9':
	    case L'A': case L'B': case L'C': case L'D': case L'E': case L'F':
		parse_history_entry(buf.contents);
		break;
	    case L'c':
		remove_last_entry();
		break;
	    case L'd':
		parse_removed_entry(buf.contents + 1);
		break;
	    case L'p':
		parse_process_id(buf.contents + 1);
		break;
	}
	wb_clear(&buf);
    }
    wb_destroy(&buf);
}

void parse_history_entry(const wchar_t *line)
{
    unsigned long num;
    time_t time;
    wchar_t *end;
    char *value;

    assert(iswxdigit(line[0]));

    errno = 0;
    num = wcstoul(line, &end, 0x10);
    if (errno || end[0] == L'\0' || num > max_number)
	return;

    if (end[0] == L':' && iswxdigit(end[1])) {
	unsigned long long t;

	errno = 0;
	t = wcstoull(&end[1], &end, 0x10);
	if (errno || end[0] == L'\0')
	    time = -1;
	else if (t > (unsigned long long) now)
	    time = now;
	else
	    time = (time_t) t;
    } else {
	time = -1;
    }

    if (!iswspace(end[0]))
	return;
    value = malloc_wcstombs(&end[1]);
    if (value != NULL) {
	new_entry((unsigned) num, time, value);
	free(value);
    }
}

void parse_removed_entry(const wchar_t *numstr)
{
    unsigned long num;
    wchar_t *end;

    if (histlist.count == 0)
	return;
    if (numstr[0] == L'\0')
	return;

    errno = 0;
    num = wcstoul(numstr, &end, 0x10);
    if (errno || (*end != L'\0' && !iswspace(*end)))
	return;
    if (num > max_number)
	return;

    struct search_result_T sr = search_entry_by_number((unsigned) num);
    if (sr.prev == sr.next)
	remove_entry(ashistentry(sr.prev));
}

void parse_process_id(const wchar_t *numstr)
{
    intmax_t num;
    wchar_t *end;

    if (numstr[0] == L'\0')
	return;

    errno = 0;
    num = wcstoimax(numstr, &end, 10);
    if (errno || (*end != L'\0' && !iswspace(*end)))
	return;
    if (num > 0)
	add_histfile_pid((pid_t) num);
    else if (num < 0)
	remove_histfile_pid((pid_t) -num);
    /* XXX: this cast and negation may be unsafe */
}

/* Re-reads history from the history file.
 * Changes that have been made to the file by other shell processes are brought
 * into this shell's history. The current data in this shell's history may be
 * changed.
 * If `refresh' is true, this function may call `refresh_file'.
 * On failure, `histfile' is closed and set to NULL.
 * `update_time' must be called before calling this function.
 * After calling this function, `histfile' must not be read without
 * repositioning. */
/* The history file should be locked (F_WRLCK if `refresh' is true or F_RDLCK if
 * `refresh' is false).
 * This function must be called just before writing to the history file. */
void update_history(bool refresh)
{
    bool posfail;
    fpos_t pos;
    long rev;

    if (histfile == NULL)
	return;
    assert(!hist_lock);

#if WIO_BROKEN
    posfail = true;
#else
    posfail = fgetpos(histfile, &pos);
#endif
    rev = read_signature();
    if (rev < 0)
	goto error;
    if (!posfail && rev == histfilerev) {
	/* The revision has not been changed. Just read new entries. */
	fsetpos(histfile, &pos);
	read_history();
    } else {
	/* The revision has been changed. Re-read everything. */
	clear_all_entries();
	clear_histfile_pids();
	add_histfile_pid(shell_pid);
	histfilerev = rev;
	histfilelines = 0;
	read_history();
    }
    if (ferror(histfile) || !feof(histfile))
	goto error;

    if (refresh)
	maybe_refresh_file();
    return;

error:
    close_history_file();
}

/* Refreshes the history file if it is time to do that.
 * `histfile' must not be NULL. */
void maybe_refresh_file(void)
{
    assert(histfile != NULL);
    if (histfilelines > 20
	    && histfilelines / 2 >= histlist.count + histfilepids.count) {
	remove_histfile_pid(0);
	refresh_file();
    }
}

/* Like `fwprintf(histfile, format, ...)', but the `histneedflush' flag is set.
 */
int wprintf_histfile(const wchar_t *format, ...)
{
    va_list ap;
    int result;

    histneedflush = true;
    va_start(ap, format);
    result = vfwprintf(histfile, format, ap);
    va_end(ap);
    return result;
}

/* Writes the signature with an incremented revision number, after emptying the
 * file. */
/* This function does not return any error status. The caller should check
 * `ferror' for the file. */
void write_signature(void)
{
    assert(histfile != NULL);
    rewind(histfile);
    while (ftruncate(fileno(histfile), 0) < 0 && errno == EINTR);

    if (histfilerev < 0 || histfilerev == LONG_MAX)
	histfilerev = 0;
    else
	histfilerev++;
    wprintf_histfile(L"#$# yash history v0 r%ld\n", histfilerev);
    histfilelines = 0;
}

/* Writes the specified entry to the history file. */
/* The file should be locked. */
/* This function does not return any error status. The caller should check
 * `ferror' for the file. */
void write_history_entry(const histentry_T *entry)
{
    assert(histfile != NULL);

    /* don't print very long line */
    if (xstrnlen(entry->value, LINE_MAX) >= LINE_MAX)
	return;

    if (entry->time >= 0)
	wprintf_histfile(L"%X:%lX %s\n",
		entry->number, (unsigned long) entry->time, entry->value);
    else
	wprintf_histfile(L"%X %s\n",
		entry->number, entry->value);
    histfilelines++;
}

/* Clears and rewrites the contents of the history file.
 * The file will have a new revision number. */
/* The file should be locked. */
/* This function does not return any error status. The caller should check
 * `ferror' for the file. */
void refresh_file(void)
{
    write_signature();
    write_histfile_pids();
    for (const histlink_T *l = histlist.Oldest; l != Histlist; l = l->next)
	write_history_entry(ashistentry(l));
}


/********** External functions **********/

/* Initializes history function if not yet initialized.
 * If the shell is not interactive, history is never initialized. */
void maybe_init_history(void)
{
    static bool initialized = false;

    if (!is_interactive_now || initialized)
	return;
    initialized = true;

    /* set `histsize' */
    const wchar_t *vhistsize = getvar(L VAR_HISTSIZE);
    if (vhistsize != NULL && vhistsize[0] != L'\0') {
	unsigned long size;
	if (xwcstoul(vhistsize, 10, &size))
	    set_histsize(size);
    }

    /* set `histrmdup' */
    const wchar_t *vhistrmdup = getvar(L VAR_HISTRMDUP);
    if (vhistrmdup != NULL && vhistrmdup[0] != L'\0') {
	unsigned long rmdup;
	if (xwcstoul(vhistrmdup, 10, &rmdup))
	    histrmdup = (rmdup <= histsize) ? rmdup : histsize;
    }

    update_time();

    /* open the history file and read it */
    histfile = open_histfile();
    if (histfile != NULL) {
	lock_histfile(F_WRLCK);
	histfilerev = read_signature();
	if (histfilerev < 0) {
	    rewind(histfile);
	    read_history_raw();
	    goto refresh;
	}
	read_history();
	if (ferror(histfile) || !feof(histfile)) {
	    close_history_file();
	    return;
	}

	remove_histfile_pid(0);
	if (histfilepids.count == 0) {
	    renumber_all_entries();
refresh:
	    refresh_file();
	} else {
	    maybe_refresh_file();
	}

	add_histfile_pid(shell_pid);
	wprintf_histfile(L"p%jd\n", (intmax_t) shell_pid);
	histfilelines++;

	lock_histfile(F_UNLCK);
    }
}

/* Closes the history file after writing info on the shell process that is
 * exiting. */
void finalize_history(void)
{
    if (!is_interactive_now || histfile == NULL)
	return;

    hist_lock = false;
    lock_histfile(F_WRLCK);
    update_time();
    update_history(true);
    if (histfile != NULL) {
	wprintf_histfile(L"p%jd\n", (intmax_t) -shell_pid);
	// histfilelines++;
	close_history_file();
    }
}

/* Closes the history file if open. */
void close_history_file(void)
{
    hist_lock = false;
    if (histfile == NULL)
	return;

    /* By closing the file descriptor for the history file, the file is
     * automatically unlocked. */
    // lock_histfile(F_UNLCK);
    remove_shellfd(fileno(histfile));
    fclose(histfile);
    histfile = NULL;
}

/* Calculates the number of the next new entry. */
unsigned next_history_number(void)
{
    unsigned number;
    if (histlist.count == 0) {
	number = 1;
    } else {
	number = ashistentry(histlist.Newest)->number + 1;
	if (number > max_number)
	    number = 1;
    }
    return number;
}

/* Adds the specified `line' to the history.
 * Must not be called while the history is locked.
 * If `line' contains newlines, `line' is separated into multiple entries.
 * Only lines that contain graph-class characters are added to the history.
 * If the `shopt_histspace' option is enabled and the `line' starts with a
 * blank, the `line' is not added. */
void add_history(const wchar_t *line)
{
    if (shopt_histspace && iswblank(line[0]))
	return;

    maybe_init_history();
    assert(!hist_lock);

    if (histfile != NULL)
	lock_histfile(F_WRLCK);
    update_time();
    update_history(true);

    for (;;) {
	size_t len = wcscspn(line, L"\n");
	add_history_line(line, len);
	line += len;
	if (line[0] == L'\0')
	    break;
	line++;
    }

    if (histfile != NULL)
	lock_histfile(F_UNLCK);
}

/* Adds the specified `line' to the history.
 * If the line is longer than `maxlen' characters, only the first `maxlen'
 * characters are added.
 * The string added to the history must not contain newlines.
 * `histfile' must be locked and `update_time' and `update_history' must have
 * been called.
 * If the string does not contain any graph-class characters, it is not added
 * to the history. */
void add_history_line(const wchar_t *line, size_t maxlen)
{
    /* Check if `line' contains `graph' characters */
    for (size_t i = 0; ; i++) {
	if (i >= maxlen || line[i] == L'\0')
	    return;
	assert(line[i] != L'\n');
	if (iswgraph(line[i]))
	    break;
    }

    char *mbsline = malloc_wcsntombs(line, maxlen);
    if (mbsline != NULL) {
	histentry_T *entry;

	remove_duplicates(mbsline);
	entry = new_entry(next_history_number(), now, mbsline);
	if (histfile != NULL)
	    write_history_entry(entry);
	free(mbsline);
    }
}

/* Removes entries whose value is the same as `line' in the `histrmdup' newest
 * entries.
 * `histfile' must be locked and `update_history' must have been called. */
void remove_duplicates(const char *line)
{
    histlink_T *l = histlist.Newest;
    for (unsigned i = histrmdup; i > 0 && l != Histlist; i--) {
	histlink_T *prev = l->prev;
	histentry_T *e = ashistentry(l);
	if (strcmp(e->value, line) == 0) {
	    if (histfile != NULL) {
		wprintf_histfile(L"d%X\n", e->number);
		histfilelines++;
	    }
	    remove_entry(e);
	}
	l = prev;
    }
}

/* Returns the history entry that has the specified `number', or `Histlist' if
 * there is no such entry. */
const histlink_T *get_history_entry(unsigned number)
{
    struct search_result_T sr = search_entry_by_number((unsigned) number);
    return (sr.prev == sr.next) ? sr.prev : Histlist;
}

#if YASH_ENABLE_LINEEDIT

/* Calls `maybe_init_history' or `update_history' and locks the history. */
void start_using_history(void)
{
    if (!hist_lock) {
	if (histfile != NULL) {
	    lock_histfile(F_RDLCK);
	    update_time();
	    update_history(false);
	    if (histfile != NULL)
		lock_histfile(F_UNLCK);
	} else {
	    maybe_init_history();
	}
	hist_lock = true;
    }
}

/* Unlocks the history. */
void end_using_history(void)
{
    hist_lock = false;
}

#endif /* YASH_ENABLE_LINEEDIT */


/********** Built-ins **********/

enum fcprinttype_T {
    FC_FULL, FC_NUMBERED, FC_UNNUMBERED, FC_RAW,
};

static struct search_result_T fc_search_entry(
	const wchar_t *prefix, int number);
static void fc_update_history(void);
static void fc_remove_last_entry(void);
static histlink_T *fc_search_entry_by_prefix(const wchar_t *prefix)
    __attribute__((nonnull));
static int fc_print_entries(
	FILE *f, const histentry_T *first, const histentry_T *last,
	bool reverse, enum fcprinttype_T type)
    __attribute__((nonnull));
static const char *fc_time_to_str(time_t time)
    __attribute__((pure));
static int fc_exec_entry(const histentry_T *entry,
	const wchar_t *old, const wchar_t *new, bool quiet)
    __attribute__((nonnull(1)));
static int fc_edit_and_exec_entries(
	const histentry_T *first, const histentry_T *last,
	bool reverse, const wchar_t *editor, bool quiet)
    __attribute__((nonnull(1,2)));
static void fc_read_history(FILE *f, bool quiet)
    __attribute__((nonnull));
static void history_clear_all(void);
static int history_delete(const wchar_t *s)
    __attribute__((nonnull));
static int history_read(const wchar_t *s)
    __attribute__((nonnull));
static int history_write(const wchar_t *s)
    __attribute__((nonnull));
static void history_refresh_file(void);

const struct xgetopt_T fc_options[] = {
    { L'e', L"editor",     OPTARG_REQUIRED, true,  NULL, },
    { L'l', L"list",       OPTARG_NONE,     true,  NULL, },
    { L'n', L"no-numbers", OPTARG_NONE,     true,  NULL, },
    { L'q', L"quiet",      OPTARG_NONE,     false, NULL, },
    { L'r', L"reverse",    OPTARG_NONE,     true,  NULL, },
    { L's', L"silent",     OPTARG_NONE,     true,  NULL, },
    { L'v', L"verbose",    OPTARG_NONE,     false, NULL, },
#if YASH_ENABLE_HELP
    { L'-', L"help",       OPTARG_NONE,     false, NULL, },
#endif
    { L'\0', NULL, 0, false, NULL, },
};

/* The "fc" built-in, which accepts the following options:
 *  -e: specify the editor to edit history
 *  -l: list history
 *  -n: don't print entry numbers
 *  -r: reverse entry order
 *  -s: execute without editing
 *  -v: print time for each entry */
int fc_builtin(int argc, void **argv)
{
    const wchar_t *editor = NULL;
    bool list = false, quiet = false, rev = false, silent = false;
    enum fcprinttype_T ptype = FC_NUMBERED;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, fc_options, XGETOPT_DIGIT))) {
	switch (opt->shortopt) {
	    case L'e':  editor = xoptarg;        break;
	    case L'l':  list   = true;           break;
	    case L'n':  ptype  = FC_UNNUMBERED;  break;
	    case L'q':  quiet  = true;           break;
	    case L'r':  rev    = true;           break;
	    case L's':  silent = true;           break;
	    case L'v':  ptype  = FC_FULL;        break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return Exit_ERROR;
	}
    }

    /* parse <old=new> */
    const wchar_t *old = NULL, *new = NULL;
    if (silent && xoptind < argc) {
	wchar_t *eq = wcschr(ARGV(xoptind), L'=');
	if (eq != NULL) {
	    eq[0] = L'\0';
	    old = ARGV(xoptind);
	    new = &eq[1];
	    xoptind++;
	}
    }

    /* error checks */
    if (editor && list)
	return mutually_exclusive_option_error(L'e', L'l');
    if (editor && silent)
	return mutually_exclusive_option_error(L'e', L's');
    if (list && quiet)
	return mutually_exclusive_option_error(L'l', L'q');
    if (list && silent)
	return mutually_exclusive_option_error(L'l', L's');
    if (rev && silent)
	return mutually_exclusive_option_error(L'r', L's');
    if (ptype != FC_NUMBERED && !list) {
	xerror(0, Ngt("the -n or -v option must be used with the -l option"));
	return Exit_ERROR;
    }
    if (!validate_operand_count(argc - xoptind, 0, silent ? 1 : 2))
	return Exit_ERROR;
    if (hist_lock) {
	xerror(0, Ngt("cannot be used during line-editing"));
	return Exit_FAILURE;
    }

    maybe_init_history();
    if (list) {
	fc_update_history();
    } else {
	/* remove the entry for this "fc" command */
	fc_remove_last_entry();
    }

    if (histlist.count == 0) {
	if (list) {
	    return Exit_SUCCESS;
	} else {
	    xerror(0, Ngt("the command history is empty"));
	    return Exit_FAILURE;
	}
    }

    /* parse <first> */
    const wchar_t *vfirst = (xoptind < argc) ? ARGV(xoptind++) : NULL;
    int nfirst;
    struct search_result_T lfirst;
    if (vfirst != NULL) {
	if (!xwcstoi(vfirst, 10, &nfirst) /* || nfirst == 0 */)
	    nfirst = 0;
    } else {
	nfirst = list ? -16 : -1;
    }
    lfirst = fc_search_entry(vfirst, nfirst);
    if (lfirst.prev == Histlist && lfirst.next == Histlist)
	return Exit_FAILURE;

    /* main part of our work with the -s flag */
    if (silent) {
	if (lfirst.prev != lfirst.next) {
	    assert(vfirst != NULL);
	    xerror(0, Ngt("no such history entry `%ls'"), vfirst);
	    return Exit_FAILURE;
	}
	return fc_exec_entry(ashistentry(lfirst.prev), old, new, quiet);
    }

    /* parse <last> */
    const wchar_t *vlast = (xoptind < argc) ? ARGV(xoptind++) : NULL;
    int nlast;
    struct search_result_T llast;
    if (vlast != NULL) {
	if (!xwcstoi(vlast, 10, &nlast) /* || nlast == 0 */)
	    nlast = 0;
    } else if (list) {
	nlast = -1;
    } else {
	llast = lfirst;
	goto check_rev;
    }
    llast = fc_search_entry(vlast, nlast);
    if (llast.prev == Histlist && llast.next == Histlist)
	return Exit_FAILURE;

check_rev:
    if (search_result_is_newer(lfirst, llast)) {
	struct search_result_T temp = lfirst;  lfirst = llast;  llast = temp;
	rev = !rev;
    }

    const histentry_T *efirst =
	    ashistentry(lfirst.next == Histlist ? lfirst.prev : lfirst.next);
    const histentry_T *elast =
	    ashistentry(llast.prev == Histlist ? llast.next : llast.prev);
    if (list)
	return fc_print_entries(stdout, efirst, elast, rev, ptype);
    else
	return fc_edit_and_exec_entries(efirst, elast, rev, editor, quiet);
}

/* Searches for the specified entry.
 * If `number` is not 0, the entry identified by the number is returned.
 * Otherwise, an entry that begins with the `prefix' is returned, in which case
 * non-NULL `prefix' must be given.
 * The `prev' and `next' members of the returned structure will be `Histlist'
 * if the prefix search fails. */
struct search_result_T fc_search_entry(const wchar_t *prefix, int number)
{
    struct search_result_T result;

    if (number == 0) {
	result.prev = result.next = fc_search_entry_by_prefix(prefix);
    } else if (number > 0) {
	result = search_entry_by_number((unsigned) number);
	assert(result.prev != Histlist || result.next != Histlist);
    } else {
	number = -number;
	result.next = get_nth_newest_entry((unsigned) number);
	if ((unsigned) number > histlist.count)
	    result.prev = Histlist;
	else
	    result.prev = result.next;
	assert(result.prev != Histlist || result.next != Histlist);
    }
    return result;
}

void fc_update_history(void)
{
    if (histfile != NULL) {
	lock_histfile(F_RDLCK);
	update_time();
	update_history(false);
	if (histfile != NULL)
	    lock_histfile(F_UNLCK);
    }
}

void fc_remove_last_entry(void)
{
    if (histfile != NULL) {
	lock_histfile(F_WRLCK);
	update_time();
	update_history(true);
	remove_last_entry();
	if (histfile != NULL) {
	    wprintf_histfile(L"c\n");
	    histfilelines++;
	    lock_histfile(F_UNLCK);
	}
    } else {
	remove_last_entry();
    }
}

/* Finds the newest entry that begins with the specified prefix.
 * Prints an error message and returns `Histlist' if not found. */
histlink_T *fc_search_entry_by_prefix(const wchar_t *prefix)
{
    char *s = malloc_wcstombs(prefix);
    if (s == NULL)
	return Histlist;

    histlink_T *l = search_entry_by_prefix(s);
    free(s);
    if (l == Histlist)
	xerror(0, Ngt("no such history entry beginning with `%ls'"), prefix);
    return l;
}

/* Print history entries between `first' and `last'.
 * Only byte-oriented output functions are used for `f'.
 * An error message is printed on error. */
int fc_print_entries(
	FILE *f, const histentry_T *first, const histentry_T *last,
	bool reverse, enum fcprinttype_T type)
{
    const histentry_T *start, *end, *e;
    if (!reverse)
	start = first, end = last;
    else
	start = last, end = first;
    e = start;
    for (;;) {
	int r;
	switch (type) {
	    case FC_FULL:
		r = fprintf(f, "%u\t%s\t%s\n",
			e->number, fc_time_to_str(e->time), e->value);
		break;
	    case FC_NUMBERED:
		r = fprintf(f, "%u\t%s\n", e->number, e->value);
		break;
	    case FC_UNNUMBERED:
		r = fprintf(f, "\t%s\n", e->value);
		break;
	    case FC_RAW:
		r = fprintf(f, "%s\n", e->value);
		break;
	    default:
		assert(false);
	}
	if (r < 0) {
	    xerror(errno, Ngt("cannot print to the standard output"));
	    return Exit_FAILURE;
	}
	if (e == end)
	    break;
	e = ashistentry(!reverse ? e->Next : e->Prev);
    }
    return Exit_SUCCESS;
}

/* Converts time to a string.
 * The return value is valid until the next call to this function. */
const char *fc_time_to_str(time_t time)
{
    static char s[80];

    if (time >= 0) {
	size_t size = strftime(s, sizeof s, "%c", localtime(&time));
	if (size > 0)
	    return s;
    }
    s[0] = '?', s[1] = '\0';
    return s;
}

/* Executes the value of `entry'.
 * If `old' is not NULL, the first occurrence of `old' in the command is
 * replaced with `new' before execution (but the entry's value is unchanged).
 * If `quiet' is false, prints the command before execution. */
int fc_exec_entry(const histentry_T *entry,
	const wchar_t *old, const wchar_t *new, bool quiet)
{
    wchar_t *code = malloc_mbstowcs(entry->value);
    if (code == NULL) {
	xerror(EILSEQ, Ngt("unexpected error"));
	return Exit_ERROR;
    }

    if (old != NULL) {
	xwcsbuf_T buf;
	wchar_t *p;

	wb_initwith(&buf, code);
	p = wcsstr(buf.contents, old);
	if (p != NULL)
	    wb_replace(&buf, p - buf.contents, wcslen(old), new, SIZE_MAX);
	code = wb_towcs(&buf);
    }

    add_history(code);
    if (!quiet)
	printf("%ls\n", code);
    exec_wcs(code, "fc", false);
    free(code);
    return laststatus;
}

/* Invokes the editor to let the user edit the history entries between `first'
 * and `last' and executes the edited entries. */
int fc_edit_and_exec_entries(
	const histentry_T *first, const histentry_T *last,
	bool reverse, const wchar_t *editor, bool quiet)
{
    char *temp;
    int fd;
    FILE *f;
    pid_t cpid;
    int savelaststatus;

    fd = create_temporary_file(&temp, ".sh", S_IRUSR | S_IWUSR);
    if (fd < 0) {
	xerror(errno, Ngt("cannot create a temporary file to edit history"));
	goto error1;
    }
    f = fdopen(fd, "w");
    if (f == NULL) {
	xerror(errno, Ngt("cannot open temporary file `%s'"), temp);
	xclose(fd);
	goto error2;
    }

    savelaststatus = laststatus;
    cpid = fork_and_reset(0, true, 0);
    if (cpid < 0) {  // fork failed
	xerror(0, Ngt("cannot invoke the editor to edit history"));
	fclose(f);
	if (unlink(temp) < 0)
	    xerror(errno, Ngt("failed to remove temporary file `%s'"), temp);
error2:
	free(temp);
error1:
	return Exit_FAILURE;
    } else if (cpid > 0) {  // parent process
	fclose(f);

	wchar_t **namep = wait_for_child(
		cpid,
		doing_job_control_now ? cpid : 0,
		doing_job_control_now);
	if (namep != NULL) {
	    *namep = malloc_wprintf(L"%ls %s",
		    editor ? editor : L"${FCEDIT:-ed}", temp);
	}

	if (laststatus != Exit_SUCCESS) {
	    xerror(0, Ngt("the editor returned a non-zero exit status"));
	    fd = -1;
	} else {
	    fd = move_to_shellfd(open(temp, O_RDONLY));
	    if (fd < 0)
		xerror(errno, Ngt("cannot read commands from file `%s'"), temp);
	}
	if (unlink(temp) < 0)
	    xerror(errno, Ngt("failed to remove temporary file `%s'"), temp);
	free(temp);

	if (fd < 0)
	    return Exit_FAILURE;

	f = fdopen(fd, "r");
	fc_read_history(f, quiet);
	lseek(fd, 0, SEEK_SET);
	laststatus = savelaststatus;
	exec_input(fd, "fc", XIO_SUBST_ALIAS);
	remove_shellfd(fd);
	fclose(f);
	return laststatus;
    } else {  // child process
	fc_print_entries(f, first, last, reverse, FC_RAW);
	fclose(f);

	wchar_t *command = malloc_wprintf(L"%ls %s",
		(editor != NULL) ? editor : L"${FCEDIT:-ed}", temp);
	free(temp);
	exec_wcs(command, "fc", true);
#ifndef NDEBUG
	free(command);
#endif
	assert(false);
    }
}

void fc_read_history(FILE *f, bool quiet)
{
    xwcsbuf_T buf;

    if (histfile != NULL)
	lock_histfile(F_WRLCK);
    update_time();
    update_history(false);

    wb_initwithmax(&buf, HISTORY_DEFAULT_LINE_LENGTH);
    while (read_line(f, &buf)) {
	if (!quiet)
	    printf("%ls\n", buf.contents);
	add_history_line(buf.contents, buf.length);
	wb_clear(&buf);
    }
    wb_destroy(&buf);

    if (histfile != NULL) {
	maybe_refresh_file();
	lock_histfile(F_UNLCK);
    }
}

#if YASH_ENABLE_HELP
const char fc_help[] = Ngt(
"list or re-execute command history"
);
const char fc_syntax[] = Ngt(
"\tfc [-qr] [-e editor] [first [last]]\n"
"\tfc -s [-q] [old=new] [first]\n"
"\tfc -l [-nrv] [first [last]]\n"
);
#endif

/* Options for the "history" built-in. */
const struct xgetopt_T history_options[] = {
    { L'c', L"clear",      OPTARG_NONE,     true,  NULL, },
    { L'd', L"delete",     OPTARG_REQUIRED, true,  NULL, },
    { L'r', L"read",       OPTARG_REQUIRED, true,  NULL, },
    { L's', L"set",        OPTARG_REQUIRED, true,  NULL, },
    { L'w', L"write",      OPTARG_REQUIRED, true,  NULL, },
    { L'F', L"flush-file", OPTARG_NONE,     true,  NULL, },
#if YASH_ENABLE_HELP
    { L'-', L"help",       OPTARG_NONE,     false, NULL, },
#endif
    { L'\0', NULL, 0, false, NULL, },
};

/* The "history" built-in, which accepts the following options:
 *  -c: clear whole history
 *  -d: remove history entry
 *  -r: read history from a file
 *  -s: add history entry
 *  -w: write history into a file
 *  -F: flush history file */
int history_builtin(int argc, void **argv)
{
    if (hist_lock) {
	xerror(0, Ngt("cannot be used during line-editing"));
	return Exit_FAILURE;
    }
    maybe_init_history();

    bool hasoption = false, removedthis = false;
    int result = Exit_SUCCESS;

    /* process options */
    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, history_options, 0)) != NULL) {
	hasoption = true;
	switch (opt->shortopt) {
	    case L'c':
		history_clear_all();
		break;
	    case L'd':
		result = history_delete(xoptarg);
		break;
	    case L'r':
		result = history_read(xoptarg);
		break;
	    case L's':
		if (!removedthis) {
		    fc_remove_last_entry();
		    removedthis = true;
		}
		add_history(xoptarg);
		break;
	    case L'w':
		result = history_write(xoptarg);
		break;
	    case L'F':
		history_refresh_file();
		break;
#if YASH_ENABLE_HELP
	    case L'-':
		result = print_builtin_help(ARGV(0));
		break;
#endif
	    default:
		return Exit_ERROR;
	}
	if (result != Exit_SUCCESS)
	    return result;
    }

    /* print history */
    int count;
    if (xoptind < argc) {
	if (!validate_operand_count(argc - xoptind, 0, 1))
	    return Exit_ERROR;

	if (!xwcstoi(ARGV(xoptind), 10, &count)) {
	    xerror(errno, Ngt("`%ls' is not a valid integer"), ARGV(xoptind));
	    return Exit_ERROR;
	}
	if (count <= 0)
	    return Exit_SUCCESS;
    } else if (!hasoption) {
	count = INT_MAX;
    } else {
	return Exit_SUCCESS;
    }
    fc_update_history();

    histlink_T *start = get_nth_newest_entry((unsigned) count);
    if (start == Histlist)
	return Exit_SUCCESS;
    return fc_print_entries(stdout,
	    ashistentry(start), ashistentry(histlist.Newest),
	    false, FC_NUMBERED);
}

/* Clears all the history. */
void history_clear_all(void)
{
    if (histfile != NULL) {
	lock_histfile(F_WRLCK);
	update_history(false);
    }
    clear_all_entries();
    if (histfile != NULL) {
	refresh_file();
	lock_histfile(F_UNLCK);
    }
}

/* Deletes a history entry specified by the argument string. */
int history_delete(const wchar_t *s)
{
    int n;
    histlink_T *l;

    if (histfile != NULL) {
	lock_histfile(F_WRLCK);
	update_time();
	update_history(true);
    }

    if (!xwcstoi(s, 10, &n) || n == 0) {
	l = fc_search_entry_by_prefix(s);
    } else {
	if (n >= 0) {
	    struct search_result_T sr = search_entry_by_number((unsigned) n);
	    l = (sr.prev == sr.next) ? sr.prev : Histlist;
	} else {
	    if (n != INT_MIN)
		n = -n;
	    else
		n = INT_MAX;
	    l = get_nth_newest_entry((unsigned) n);
	}
	if (l == Histlist)
	    xerror(0, Ngt("no such history entry `%ls'"), s);
    }

    if (l != Histlist) {
	histentry_T *e = ashistentry(l);
	if (histfile != NULL) {
	    wprintf_histfile(L"d%X\n", e->number);
	    histfilelines++;
	}
	remove_entry(e);
    }

    if (histfile != NULL)
	lock_histfile(F_UNLCK);

    return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;
}

/* Reads history from the specified file. */
int history_read(const wchar_t *s)
{
    FILE *f;

    /* The `fc_read_history' function assumes the argument stream wide-oriented.
     * We don't pass `stdin' to `fc_read_history' so that it remains
     * non-oriented. */
    if (wcscmp(s, L"-") == 0) {
	int fd = copy_as_shellfd(STDIN_FILENO);
	if (fd < 0)
	    goto error;
	f = fdopen(fd, "r");
    } else {
	char *mbsfilename = malloc_wcstombs(s);
	if (mbsfilename == NULL)
	    goto error;
	f = fopen(mbsfilename, "r");
	free(mbsfilename);
    }
    if (f == NULL)
	goto error;
    fc_read_history(f, true);
    bool error = ferror(f);
    error |= fclose(f);
    if (!error)
	return Exit_SUCCESS;

error:
    xerror(0, Ngt("cannot read history from file `%ls'"), s);
    return Exit_FAILURE;
}

/* Writes history into the specified file. */
int history_write(const wchar_t *s)
{
    FILE *f;

    fc_update_history();
    if (histlist.count == 0)
	return Exit_SUCCESS;

    if (wcscmp(s, L"-") == 0) {
	f = stdout;
    } else {
	char *mbsfilename = malloc_wcstombs(s);
	if (mbsfilename == NULL)
	    goto error;
	f = fopen(mbsfilename, "w");
	free(mbsfilename);
	if (f == NULL)
	    goto error;
    }
    
    int result = fc_print_entries(f,
	    ashistentry(histlist.Oldest), ashistentry(histlist.Newest),
	    false, FC_RAW);
    if (f != stdout)
	if (fclose(f) != 0)
	    goto error;
    return result;

error:
    xerror(0, Ngt("cannot write history to file `%ls'"), s);
    return Exit_FAILURE;
}

/* Refreshes the history file. */
void history_refresh_file(void)
{
    if (histfile != NULL) {
	lock_histfile(F_WRLCK);
	update_time();
	update_history(false);
	if (histfile != NULL) {
	    remove_histfile_pid(0);
	    refresh_file();
	    lock_histfile(F_UNLCK);
	}
    }
}

#if YASH_ENABLE_HELP
const char history_help[] = Ngt(
"manage command history"
);
const char history_syntax[] = Ngt(
"\thistory [-cF] [-d entry] [-s command] [-r file] [-w file] [count]\n"
);
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
