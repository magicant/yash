/* Yash: yet another shell */
/* history.c: command history management */
/* (C) 2007-2010 magicant */

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
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "builtin.h"
#include "exec.h"
#include "history.h"
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
#define MAX_HISTSIZE     1000000
/* The minimum value of `max_number',
 * which must be at least 32768 according to POSIX. */
/* Must be a power of 10. */
#define MIN_MAX_NUMBER   100000
/* The default size of the history list,
 * which must be at least 128 according to POSIX. */
#define DEFAULT_HISTSIZE 500


/* The main history list. */
histlist_T histlist = {
    .link = { Histlist, Histlist, },
    .count = 0,
};
/* History entries are stored in this doubly-linked list.
 * The `Newest' (`link.prev') member points to the newest entry and the `Oldest'
 * (`link.next') member points to the oldest entry. When there's no entries,
 * `Newest' and `Oldest' point to `histlist' itself. */

/* The number of the next new history entry. Must be positive. */
unsigned hist_next_number = 1;
/* The maximum limit of the number of an entry.
 * Must always be no less than `histsize' or `MIN_MAX_NUMBER'.
 * The number of any entry is not greater than this value. */
static unsigned max_number = MIN_MAX_NUMBER;
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
/* The process IDs of processes that share the history file. */
static struct pidlist_T *histfilepids = NULL;

/* The current time returned by `time' */
static time_t now = (time_t) -1;

/* If true, the history is locked, that is, readonly. */
static bool hist_lock = false;


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
static histentry_T *find_entry(unsigned number, bool newer_if_not_found)
    __attribute__((pure));
static histentry_T *get_nth_newest_entry(unsigned n)
    __attribute__((pure));
static histentry_T *search_entry(const char *s, bool exact)
    __attribute__((nonnull,pure));
static bool is_newer(const histentry_T *e1, const histentry_T *e2)
    __attribute__((nonnull,pure));

static FILE *open_histfile(void);
static bool lock_file(int fd, short type)
    __attribute__((nonnull));
static bool read_line(FILE *restrict f, xwcsbuf_T *restrict buf)
    __attribute__((nonnull));
static long read_signature(FILE *f)
    __attribute__((nonnull));
static void read_history_raw(FILE *f)
    __attribute__((nonnull));
static void read_history(FILE *f)
    __attribute__((nonnull));
static void parse_history_entry(const wchar_t *line)
    __attribute__((nonnull));
static void parse_removed_entry(const wchar_t *numstr)
    __attribute__((nonnull));
static void parse_process_id(const wchar_t *numstr)
    __attribute__((nonnull));
static void update_history(bool refresh);
static void maybe_refresh_file(void);
static void write_signature(FILE *f)
    __attribute__((nonnull));
static void write_history_entry(
	const histentry_T *restrict entry, FILE *restrict f)
    __attribute__((nonnull));
static void refresh_file(FILE *f)
    __attribute__((nonnull));

static void add_histfile_pid(pid_t pid);
static void remove_histfile_pid(pid_t pid);
static void clear_histfile_pids(void);
static void check_histfile_pid(void);
static void write_histfile_pids(FILE *f)
    __attribute__((nonnull));

static void really_add_history(const wchar_t *line)
    __attribute__((nonnull));
static void remove_dups(const char *line)
    __attribute__((nonnull));


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


/* Updates the value of `now'. */
void update_time(void)
{
    now = time(NULL);
}

/* Changes `histsize' to `newsize' and accordingly `max_number'. */
/* `histsize' is set only once in initialization. */
void set_histsize(unsigned newsize)
{
    assert(newsize <= MAX_HISTSIZE);
    if (newsize == 0)
	newsize = 1;
    histsize = newsize;

    max_number = MIN_MAX_NUMBER;
    while (max_number < 2 * histsize)
	max_number *= 10;
    /* `max_number' is the smallest power of 10 that is not less than
     * 2 * histsize. */
}

/* Adds a new history entry to the end of `histlist'.
 * `hist_next_number' is updated in this function.
 * Some oldest entries may be removed in this function if they conflict with the
 * new one or the list is full. */
histentry_T *new_entry(unsigned number, time_t time, const char *line)
{
    assert(!hist_lock);
    assert(number > 0);
    assert(number <= max_number);

    while (need_remove_entry(number))
	remove_entry(histlist.Oldest);

    histentry_T *new = xmalloc(sizeof *new + strlen(line) + 1);
    new->Prev = histlist.Newest;
    new->Next = Histlist;
    histlist.Newest = new->Prev->Next = new;
    new->number = number;
    new->time = time;
    strcpy(new->value, line);
    histlist.count++;
    hist_next_number = (number == max_number) ? 1 : number + 1;
    return new;
}

bool need_remove_entry(unsigned number)
{
    if (histlist.count == 0)
	return false;
    if (histlist.count >= histsize)
	return true;

    unsigned oldest = histlist.Oldest->number;
    unsigned newest = histlist.Newest->number;
    if (oldest <= newest)
	return oldest <= number && number <= newest;
    else
	return oldest <= number || number <= newest;
}

/* Removes the specified entry from `histlist'. */
void remove_entry(histentry_T *entry)
{
    assert(!hist_lock);
    assert(entry != Histlist);
    entry->Prev->Next = entry->Next;
    entry->Next->Prev = entry->Prev;
    histlist.count--;
    free(entry);
}

/* Removes the newest entry and decreases `hist_next_number'. */
void remove_last_entry(void)
{
    if (histlist.count > 0) {
	hist_next_number = histlist.Newest->number;
	remove_entry(histlist.Newest);
    }
}

/* Renumbers all the entries in `histlist', starting from 1. */
void renumber_all_entries(void)
{
    assert(!hist_lock);

    unsigned num = 0;

    for (histentry_T *e = histlist.Oldest; e != Histlist; e = e->Next)
	e->number = ++num;

    if (histlist.count > 0) {
	hist_next_number = histlist.Newest->number + 1;
	if (hist_next_number >= max_number)
	    hist_next_number = 1;
    } else {
	hist_next_number = 1;
    }
}

/* Removes all entries in the history list. */
void clear_all_entries(void)
{
    assert(!hist_lock);

    histentry_T *e = histlist.Oldest;
    while (e != Histlist) {
	histentry_T *next = e->Next;
	free(e);
	e = next;
    }
    histlist.Oldest = histlist.Newest = Histlist;
    histlist.count = 0;
    hist_next_number = 1;
}

/* Returns the entry that has the specified `number'.
 * If not found, the nearest neighbor entry is returned, newer or older
 * according to `newer_if_not_found'. If there is no neighbor either, Histlist
 * is returned. */
histentry_T *find_entry(unsigned number, bool newer_if_not_found)
{
    if (histlist.count == 0)
	return Histlist;

    unsigned oldestnum, nnewestnum, nnumber;
    oldestnum = histlist.Oldest->number;
    nnewestnum = histlist.Newest->number;
    nnumber = number;
    if (nnewestnum < oldestnum) {
	if (nnumber <= nnewestnum)
	    nnumber += max_number;
	nnewestnum += max_number;
    }

    if (nnumber < oldestnum)
	return newer_if_not_found ? histlist.Oldest : Histlist;
    if (nnumber > nnewestnum)
	return newer_if_not_found ? Histlist : histlist.Newest;

    histentry_T *e;
    if (2 * (nnumber - oldestnum) < nnewestnum - oldestnum) {
	/* search from the oldest */
	e = histlist.Oldest;
	while (number < e->number)
	    e = e->Next;
	while (number > e->number)
	    e = e->Next;
	if (number != e->number && !newer_if_not_found)
	    e = e->Prev;
    } else {
	/* search from the newest */
	e = histlist.Newest;
	while (number > e->number)
	    e = e->Prev;
	while (number < e->number)
	    e = e->Prev;
	if (number != e->number && newer_if_not_found)
	    e = e->Next;
    }
    return e;
}

/* Returns the nth newest entry (or the oldest entry if `n' is too big).
 * Returns `Histlist' if `n' is zero. */
histentry_T *get_nth_newest_entry(unsigned n)
{
    histentry_T *e = Histlist;

    if (histlist.count <= n)
	return histlist.Oldest;
    while (n-- > 0)
	e = e->Prev;
    return e;
}

/* Searches for the newest entry whose value begins with `s' (if not `exact') or
 * whose value is equal to `s' (if `exact').
 * Returns `Histlist' if not found. */
histentry_T *search_entry(const char *s, bool exact)
{
    histentry_T *e = histlist.Newest;

    while (e != Histlist) {
	if (exact ? strcmp(e->value, s) == 0
		: matchstrprefix(e->value, s) != NULL)
	    break;
	e = e->Prev;
    }
    return e;
}

/* Returns true iff `e1' is newer than `e2'. */
bool is_newer(const histentry_T *e1, const histentry_T *e2)
{
    assert(histlist.count > 0);

    unsigned n1 = e1->number;
    unsigned n2 = e2->number;
    unsigned newest = histlist.Newest->number;
    unsigned oldest = histlist.Oldest->number;

    return (n1 <= newest && newest < oldest && oldest <= n2)
	|| (n2 <= n1 && (oldest <= n2 || n1 <= newest));
}


/********** History file functions **********/

/* Opens the history file.
 * Returns NULL on failure. */
FILE *open_histfile(void)
{
    const wchar_t *vhistfile = getvar(L VAR_HISTFILE);
    if (!vhistfile)
	return NULL;

    char *mbshistfile = malloc_wcstombs(vhistfile);
    if (!mbshistfile)
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
    if (!f) {
	remove_shellfd(fd);
	xclose(fd);
    }
    return f;
}

/* Locks the file referred to by the file descriptor `fd'.
 * `type' must be one of `F_RDLCK', `F_WRLCK' and `F_UNLCK'.
 * When another process is holding a lock for the file, this process will be
 * blocked until the lock becomes available. */
bool lock_file(int fd, short type)
{
    struct flock flock = {
	.l_type   = type,
	.l_whence = SEEK_SET,
	.l_start  = 0,
	.l_len    = 0, /* to the end of file */
    };
    int result;

    while ((result = fcntl(fd, F_SETLKW, &flock)) == -1 && errno == EINTR);
    return result != -1;
}

/* Reads one line from the file `f'.
 * The line is appended to the buffer `buf', which must have been initialized.
 * The terminating newline is not left in `buf'.
 * On failure, false is returned, in which case the contents of `buf' is
 * unspecified.
 * If there is no more line in the file, false is returned. */
bool read_line(FILE *restrict f, xwcsbuf_T *restrict buf)
{
    while (fgetws(buf->contents + buf->length,
		buf->maxlength - buf->length + 1, f)) {
	size_t len = wcslen(buf->contents + buf->length);
	if (len == 0)
	    return false;
	buf->length += len;
	if (buf->contents[buf->length - 1] == L'\n') {
	    wb_truncate(buf, buf->length - 1);
	    return true;
	}
	if (buf->length > LINE_MAX)
	    return false;  /* Too long line. Give up. */
	if (buf->maxlength - buf->length < 80)
	    wb_setmax(buf, buf->maxlength + 100);
    }
    return false;
}

/* Reads the signature of the history file and checks if it is a valid
 * signature.
 * If valid:
 *   - the file `f' is positioned just after the signature,
 *   - the return value is the revision of the file (non-negative).
 * Otherwise:
 *   - the file `f' is positioned at an unspecified place,
 *   - the return value is negative. */
/* The file `f' should be locked. */
long read_signature(FILE *f)
{
    xwcsbuf_T buf;
    long rev = -1;
    const wchar_t *s;

    wb_init(&buf);
    rewind(f);
    if (!read_line(f, &buf))
	goto end;
    
    s = matchwcsprefix(buf.contents, L"#$# yash history v0 r");
    if (!s || !iswdigit(s[0]))
	goto end;

    if (!xwcstol(s, 10, &rev))
	rev = -1;
end:
    wb_destroy(&buf);
    return rev;
}

/* Reads history entries from the specified file.
 * The file format is assumed a simple text, one entry per line.
 * The file is read from the current position.
 * The entries that were read from the file are appended to `histlist'. */
/* The file `f' should be locked. */
/* This function does not return any error status. The caller should check
 * `ferror' and/or `feof' for the file. */
void read_history_raw(FILE *f)
{
    xwcsbuf_T buf;

    wb_init(&buf);
    while (read_line(f, &buf)) {
	char *line = malloc_wcstombs(buf.contents);
	if (line) {
	    new_entry(hist_next_number, -1, line);
	    free(line);
	}
	wb_clear(&buf);
    }
    wb_destroy(&buf);
}

/* Reads history entries from the specified file.
 * The file is read from the current position.
 * The entries that were read from the file are appended to `histlist'.
 * `update_time' must be called before calling this function. */
/* The file `f' should be locked. */
/* This function does not return any error status. The caller should check
 * `ferror' and/or `feof' for the file. */
void read_history(FILE *f)
{
    xwcsbuf_T buf;

    wb_init(&buf);
    while (read_line(f, &buf)) {
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
    if (errno || *end == L'\0' || num > max_number)
	return;

    if (end[0] == L':' && iswxdigit(end[1])) {
	unsigned long long t;

	errno = 0;
	t = wcstoull(end + 1, &end, 0x10);
	if (errno || *end == L'\0')
	    time = -1;
	else if (t > (unsigned long long) now)
	    time = now;
	else
	    time = (time_t) t;
    } else {
	time = -1;
    }

    if (*end != L' ')
	return;
    end++;
    value = malloc_wcstombs(end);
    if (value) {
	new_entry((unsigned) num, time, value);
	free(value);
    }
}

void parse_removed_entry(const wchar_t *numstr)
{
    unsigned long i;
    wchar_t *end;

    if (numstr[0] == L'\0')
	return;

    errno = 0;
    i = wcstoul(numstr, &end, 0x10);
    if (errno || (*end != L'\0' && !iswspace(*end)))
	return;

    if (i <= max_number) {
	unsigned num = (unsigned) i;
	histentry_T *e = find_entry(num, false);
	if (e != Histlist && e->number == num)
	    remove_entry(e);
    }
}

void parse_process_id(const wchar_t *numstr)
{
    intmax_t i;
    wchar_t *end;

    if (numstr[0] == L'\0')
	return;

    errno = 0;
    i = wcstoimax(numstr, &end, 10);
    if (errno || (*end != L'\0' && !iswspace(*end)))
	return;
    if (i > 0)
	add_histfile_pid((pid_t) i);
    else if (i < 0)
	remove_histfile_pid((pid_t) -i);
    /* XXX: this cast and negation may be unsafe */
}

/* Re-read the history from the main history file.
 * Changes made to the file by other shell processes are brought into this
 * shell's history. The current data in this shell's history may be changed.
 * If `refresh' is true, this function may call `refresh_file'.
 * On failure, `histfile' is closed and set to NULL.
 * `update_time' must be called before calling this function.
 * After calling this function, `histfile' must not be read without
 * repositioning. */
/* The history file `histfile' should be locked (F_WRLCK if `refresh' is true or
 * F_RDLCK if `refresh' is false).
 * This function must be called just before writing to the history file. */
void update_history(bool refresh)
{
    bool posfail;
    fpos_t pos;
    long rev;

    if (!histfile)
	return;
    assert(!hist_lock);

#if WIO_BROKEN
    posfail = true;
#else
    posfail = fgetpos(histfile, &pos);
#endif
    rev = read_signature(histfile);
    if (rev < 0) {
	return;
    } else if (!posfail && rev == histfilerev) {
	/* The revision has not been changed. Just read new entries. */
	fsetpos(histfile, &pos);
    } else {
	/* The revision has been changed. Re-read everything. */
	clear_all_entries();
	clear_histfile_pids();
	add_histfile_pid(shell_pid);
	histfilerev = rev;
    }
    read_history(histfile);
    if (ferror(histfile) || !feof(histfile)) {
	lock_file(fileno(histfile), F_UNLCK);
	remove_shellfd(fileno(histfile));
	fclose(histfile);
	histfile = NULL;
	return;
    }

    if (refresh)
	maybe_refresh_file();
}

/* Refreshes the history file if it is time to do that.
 * `histfile' must not be NULL. */
void maybe_refresh_file(void)
{
#ifndef HISTORY_REFRESH_INTERVAL
#define HISTORY_REFRESH_INTERVAL 100
#endif
#if HISTORY_REFRESH_INTERVAL <= 0
#error invalid HISTORY_REFRESH_INTERVAL
#endif
    assert(histfile != NULL);
    if (hist_next_number % HISTORY_REFRESH_INTERVAL == 0) {
	check_histfile_pid();
	refresh_file(histfile);
    }
}

/* Writes the signature with an incremented revision number, after emptying the
 * file. */
/* This function does not return any error status. The caller should check
 * `ferror' for the file. */
void write_signature(FILE *f)
{
    rewind(f);
    while (ftruncate(fileno(f), 0) < 0 && errno == EINTR);

    if (histfilerev < 0 || histfilerev == LONG_MAX)
	histfilerev = 0;
    else
	histfilerev++;
    fwprintf(f, L"#$# yash history v0 r%ld\n", histfilerev);
}

/* Writes the specified entry to the file `f'. */
/* The file `f' should be locked. */
/* This function does not return any error status. The caller should check
 * `ferror' for the file. */
void write_history_entry(const histentry_T *restrict entry, FILE *restrict f)
{
    if (entry->time >= 0)
	fwprintf(f, L"%X:%lX %s\n",
		entry->number, (unsigned long) entry->time, entry->value);
    else
	fwprintf(f, L"%X %s\n",
		entry->number, entry->value);
}

/* Clears and rewrites the contents of the history file.
 * The file will have a new revision number. */
/* The file `f' should be locked. */
/* This function does not return any error status. The caller should check
 * `ferror' for the file. */
void refresh_file(FILE *f)
{
    write_signature(f);
    write_histfile_pids(f);
    for (const histentry_T *e = histlist.Oldest; e != Histlist; e = e->Next)
	write_history_entry(e, f);
}


/********** Process ID list **********/

/* The process ID list is sorted in the ascending order. */

struct pidlist_T {
    struct pidlist_T *next;
    pid_t pid;
};

/* Adds `pid' to `histfilepids'. `pid' must be positive. */
void add_histfile_pid(pid_t pid)
{
    struct pidlist_T **last, *list, *new;

    assert(pid > 0);
    last = &histfilepids, list = histfilepids;
    while (list != NULL)
	if (list->pid < pid)
	    last = &list->next, list = list->next;
	else if (list->pid == pid)
	    return;
	else
	    break;
    new = xmalloc(sizeof *new);
    new->next = list;
    new->pid = pid;
    *last = new;
}

/* Removes `pid' from `histfilepids'. */
void remove_histfile_pid(pid_t pid)
{
    struct pidlist_T **last, *list;

    last = &histfilepids, list = histfilepids;
    for (;;)
	if (list == NULL || list->pid > pid)
	    return;
	else if (list->pid < pid)
	    last = &list->next, list = list->next;
	else
	    break;
    *last = list->next;
    free(list);
}

/* Clears `histfilepids'. */
void clear_histfile_pids(void)
{
    struct pidlist_T *list = histfilepids;
    while (list != NULL) {
	struct pidlist_T *next = list->next;
	free(list);
	list = next;
    }
    histfilepids = NULL;
}

/* Checks the process IDs in `histfilepids'.
 * Process IDs of non-existent process are removed from the list. */
void check_histfile_pid(void)
{
    struct pidlist_T **last, *list;

    last = &histfilepids, list = histfilepids;
    while (list != NULL) {
	assert(list->pid > 0);
	if (!process_exists(list->pid)) {
	    *last = list->next;
	    free(list);
	    list = *last;
	} else {
	    last = &list->next, list = list->next;
	}
    }
}

/* Writes process IDs in `histfilepids' to the file `f'. */
/* This function does not return any error status. The caller should check
 * `ferror' for the file. */
void write_histfile_pids(FILE *f)
{
    for (struct pidlist_T *list = histfilepids; list != NULL; list = list->next)
	fwprintf(f, L"p%jd\n", (intmax_t) list->pid);
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
    if (vhistsize && vhistsize[0]) {
	unsigned long size;
	if (xwcstoul(vhistsize, 10, &size))
	    set_histsize(size <= MAX_HISTSIZE ? size : MAX_HISTSIZE);
    }

    /* set `histrmdup' */
    const wchar_t *vhistrmdup = getvar(L VAR_HISTRMDUP);
    if (vhistrmdup && vhistrmdup[0]) {
	unsigned long rmdup;
	if (xwcstoul(vhistrmdup, 10, &rmdup))
	    histrmdup = rmdup <= histsize ? rmdup : histsize;
    }

    update_time();

    /* open the history file and read it */
    histfile = open_histfile();
    if (histfile) {
	lock_file(fileno(histfile), F_WRLCK);
	histfilerev = read_signature(histfile);
	if (histfilerev < 0) {
	    rewind(histfile);
	    read_history_raw(histfile);
	    goto refresh;
	}
	read_history(histfile);
	if (ferror(histfile) || !feof(histfile)) {
	    lock_file(fileno(histfile), F_UNLCK);
	    remove_shellfd(fileno(histfile));
	    fclose(histfile);
	    histfile = NULL;
	} else {
	    check_histfile_pid();
	    if (!histfilepids) {
		renumber_all_entries();
refresh:
		refresh_file(histfile);
	    }
	    add_histfile_pid(shell_pid);
	    fwprintf(histfile, L"p%jd\n", (intmax_t) shell_pid);
	    fflush(histfile);
	    lock_file(fileno(histfile), F_UNLCK);
	}
    }
}

/* Closes the history file after writing info on the shell process that is
 * exiting. */
void finalize_history(void)
{
    if (!is_interactive_now || !histfile)
	return;

    hist_lock = false;
    lock_file(fileno(histfile), F_WRLCK);
    update_time();
    update_history(true);
    remove_histfile_pid(shell_pid);
    if (histfile) {
	fwprintf(histfile, L"p%jd\n", (intmax_t) -shell_pid);
	fflush(histfile);
	lock_file(fileno(histfile), F_UNLCK);
	remove_shellfd(fileno(histfile));
	fclose(histfile);
	histfile = NULL;
    }
}

/* Closes the history file if open. */
void close_history_file(void)
{
    hist_lock = false;
    if (histfile == NULL)
	return;
    fclose(histfile);
    histfile = NULL;
}

/* Adds the specified `line' to the history.
 * Must not be called while the history is locked.
 * If `line' contains newlines, `line' is separated into multiple entries.
 * Only lines that contain graph-class characters are added to the history. */
void add_history(const wchar_t *line)
{
    maybe_init_history();
    assert(!hist_lock);

    update_time();
    if (histfile) {
	lock_file(fileno(histfile), F_WRLCK);
	update_history(true);
    }

    const wchar_t *nl;
    while ((nl = wcschr(line, L'\n')) != NULL) {
	if (nl > line) {
	    wchar_t *line1 = xwcsndup(line, nl - line);
	    really_add_history(line1);
	    free(line1);
	}
	line = nl + 1;
    }
    really_add_history(line);

    if (histfile) {
	fflush(histfile);
	lock_file(fileno(histfile), F_UNLCK);
    }
}

/* Adds the specified `line' to the history.
 * `line' must not contain newlines.
 * `histfile' must be locked and `update_time' and `update_history' must have
 * been called.
 * If the `line' does not contain any graph-class characters, it is not added
 * to the history. If the `shopt_histspace' option is enabled and the `line'
 * starts with a space, the `line' is not added either. */
void really_add_history(const wchar_t *line)
{
    if (shopt_histspace && line[0] == ' ')
	return;

    /* Check if `line' contains `graph' characters */
    for (const wchar_t *s = line; ; ) {
	if (*s == L'\0')
	    return;
	if (iswgraph(*s))
	    break;
	s++;
    }

    char *mbsline = malloc_wcstombs(line);
    if (mbsline) {
	histentry_T *entry;

	remove_dups(mbsline);
	entry = new_entry(hist_next_number, now, mbsline);
	if (histfile)
	    write_history_entry(entry, histfile);
	free(mbsline);
    }
}

/* Removes entries whose value is the same as `line' in the `histrmdup' newest
 * entries.
 * `histfile' must be locked and `update_history' must have been called. The
 * history file should be flushed after this function. */
void remove_dups(const char *line)
{
    histentry_T *saveprev = histlist.Newest;
    for (unsigned i = histrmdup; i > 0; i--) {
	histentry_T *e = saveprev;
	saveprev = e->Prev;
	if (e == Histlist)
	    break;
	if (strcmp(e->value, line) == 0) {
	    if (histfile)
		fwprintf(histfile, L"d%X\n", e->number);
	    remove_entry(e);
	}
    }
}

/* Returns the history entry that has the specified `number', or NULL if there
 * is no such entry. */
const histentry_T *get_history_entry(unsigned number)
{
    const histentry_T *e = find_entry(number, false);
    if (e == Histlist || e->number != number)
	return NULL;
    return e;
}

#if YASH_ENABLE_LINEEDIT

/* Calls `maybe_init_history' or `update_history' and locks the history. */
void start_using_history(void)
{
    if (!hist_lock) {
	if (histfile) {
	    lock_file(fileno(histfile), F_RDLCK);
	    update_time();
	    update_history(false);
	    if (histfile)
		lock_file(fileno(histfile), F_UNLCK);
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


/********** Builtins **********/

enum fcprinttype_T {
    FC_FULL, FC_NUMBERED, FC_UNNUMBERED, FC_RAW,
};

static void fc_update_history(void);
static void fc_remove_last_entry(void);
static histentry_T *fc_search_entry(const wchar_t *prefix)
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

/* The "fc" builtin, which accepts the following options:
 *  -e: specify the editor to edit history
 *  -l: list history
 *  -n: don't print entry numbers
 *  -r: reverse entry order
 *  -s: execute without editing
 *  -v: print time for each entry */
int fc_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"editor",     OPTARG_REQUIRED, L'e', },
	{ L"list",       OPTARG_NONE,     L'l', },
	{ L"no-numbers", OPTARG_NONE,     L'n', },
	{ L"quiet",      OPTARG_NONE,     L'q', },
	{ L"reverse",    OPTARG_NONE,     L'r', },
	{ L"silent",     OPTARG_NONE,     L's', },
	{ L"verbose",    OPTARG_NONE,     L'v', },
#if YASH_ENABLE_HELP
	{ L"help",       OPTARG_NONE,     L'-', },
#endif
	{ NULL, 0, 0, },
    };

    const wchar_t *editor = NULL;
    bool list = false, quiet = false, rev = false, silent = false;
    enum fcprinttype_T ptype = FC_NUMBERED;

    wchar_t opt;
    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv,
		    posixly_correct ? L"-e:lnrs" : L"-e:lnqrsv",
		    long_options, NULL))) {
	switch (opt) {
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
		goto print_usage;
	}
    }
    if ((editor && (list || silent))
	    || (list && (quiet || silent))
	    || (rev && silent)
	    || (ptype != FC_NUMBERED && !list)
	    || (argc - xoptind > 2))
	goto print_usage;
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

    /* parse <old=new> */
    const wchar_t *old = NULL, *new = NULL;
    if (silent && xoptind < argc) {
	wchar_t *eq = wcschr(ARGV(xoptind), L'=');
	if (eq) {
	    *eq = L'\0';
	    old = ARGV(xoptind);
	    new = eq + 1;
	    xoptind++;
	}
    }

    /* parse <first> and <last> */
    const wchar_t *vfirst = (xoptind < argc) ? ARGV(xoptind++) : NULL;
    const wchar_t *vlast  = (xoptind < argc) ? ARGV(xoptind++) : NULL;
    long first, last;
    const histentry_T *efirst = NULL, *elast = NULL;
    if (silent && vlast)
	goto print_usage;
    if (vfirst) {
	if (!xwcstol(vfirst, 10, &first) || first == 0) {
	    efirst = fc_search_entry(vfirst);
	    if (efirst == Histlist)
		return Exit_FAILURE;
	} else {
	    if (first > INT_MAX)
		first = INT_MAX;
	    else if (first < INT_MIN + 1)
		first = INT_MIN + 1;
	}
    } else {
	first = list ? -16 : -1;
    }
    if (vlast) {
	if (!xwcstol(vlast, 10, &last) || last == 0) {
	    elast = fc_search_entry(vlast);
	    if (elast == Histlist)
		return Exit_FAILURE;
	} else {
	    if (last > INT_MAX)
		last = INT_MAX;
	    else if (last < INT_MIN + 1)
		last = INT_MIN + 1;
	}
    } else {
	last = list ? -1 : 0;
    }

    /* find the first and the last entries */
    if (!efirst) {
	if (first >= 0) {
	    efirst = find_entry(first, true);
	    if (silent && efirst->number != (unsigned) first)
		efirst = Histlist;
	} else {
	    if (silent && (unsigned) -first > histlist.count)
		efirst = Histlist;
	    else
		efirst = get_nth_newest_entry(-first);
	}
	if (efirst == Histlist) {
	    assert(vfirst != NULL);
	    xerror(0, Ngt("no such history entry `%ls'"), vfirst);
	    return Exit_FAILURE;
	}
    }
    if (!elast) {
	if (last == 0)
	    elast = efirst;
	else if (last >= 0)
	    elast = find_entry(last, false);
	else
	    elast = get_nth_newest_entry(-last);
	if (elast == Histlist) {
	    assert(vlast != NULL);
	    xerror(0, Ngt("no such history entry `%ls'"), vlast);
	    return Exit_FAILURE;
	}
    }
    if (is_newer(efirst, elast)) {
	const histentry_T *temp = efirst;  efirst = elast;  elast = temp;
	rev = !rev;
    }
    assert(efirst != NULL);  assert(efirst != Histlist);
    assert(elast  != NULL);  assert(elast  != Histlist);

    if (list)
	return fc_print_entries(stdout, efirst, elast, rev, ptype);
    else if (silent)
	return fc_exec_entry(efirst, old, new, quiet);
    else
	return fc_edit_and_exec_entries(efirst, elast, rev, editor, quiet);

print_usage:
    if (posixly_correct)
	fprintf(stderr, gt("Usage:  fc [-r] [-e editor] [first [last]]\n"
	                   "        fc -s [old=new] [first]\n"
	                   "        fc -l [-nr] [first [last]]\n"));
    else
	fprintf(stderr, gt("Usage:  fc [-qr] [-e editor] [first [last]]\n"
	                   "        fc -s [-q] [old=new] [first]\n"
	                   "        fc -l [-nrv] [first [last]]\n"));
    return Exit_ERROR;
}

void fc_update_history(void)
{
    if (histfile) {
	lock_file(fileno(histfile), F_RDLCK);
	update_time();
	update_history(false);
	if (histfile)
	    lock_file(fileno(histfile), F_UNLCK);
    }
}

void fc_remove_last_entry(void)
{
    if (histfile) {
	lock_file(fileno(histfile), F_WRLCK);
	update_time();
	update_history(false);
	remove_last_entry();
	if (histfile) {
	    fwprintf(histfile, L"c\n");
	    fflush(histfile);
	    lock_file(fileno(histfile), F_UNLCK);
	}
    } else {
	remove_last_entry();
    }
}

histentry_T *fc_search_entry(const wchar_t *prefix)
{
    char *s = malloc_wcstombs(prefix);
    histentry_T *e = NULL;
    if (s)
	e = search_entry(s, false);
    free(s);
    if (e == Histlist)
	xerror(0, Ngt("no such history entry beginning with `%ls'"), prefix);
    return e;
}

/* Print history entries between `first' and `last'.
 * `errno' is set on error. */
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
	e = !reverse ? e->Next : e->Prev;
    }
    return Exit_SUCCESS;
}

/* Converts time to a string.
 * The return value is valid until the next call to this function. */
const char *fc_time_to_str(time_t time)
{
    static char s[80];
    size_t size;

    if (time >= 0) {
	size = strftime(s, sizeof s, "%c", localtime(&time));
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
    if (!code) {
	xerror(EILSEQ, Ngt("unexpected error"));
	return Exit_ERROR;
    }

    if (old) {
	xwcsbuf_T buf;
	wchar_t *p;

	wb_initwith(&buf, code);
	p = wcsstr(buf.contents, old);
	if (p)
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

    fd = create_temporary_file(&temp, S_IRUSR | S_IWUSR);
    if (fd < 0) {
	xerror(errno, Ngt("cannot create a temporary file to edit history"));
	goto error1;
    }
    f = fdopen(fd, "w");
    if (!f) {
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
	if (namep) {
	    *namep = malloc_wprintf(L"%ls %s",
		    editor ? editor : L"${FCEDIT:-ed}", temp);
	}

	int fd;
	if (laststatus != Exit_SUCCESS) {
	    xerror(0, Ngt("editor returned non-zero status"));
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
	exec_input(fd, "fc", false, true, false);
	remove_shellfd(fd);
	fclose(f);
	return laststatus;
    } else {  // child process
	fc_print_entries(f, first, last, reverse, FC_RAW);
	fclose(f);

	wchar_t *command = malloc_wprintf(L"%ls %s",
		editor ? editor : L"${FCEDIT:-ed}", temp);
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

    update_time();
    if (histfile) {
	lock_file(fileno(histfile), F_WRLCK);
	update_history(false);
    }

    wb_init(&buf);
    while (read_line(f, &buf)) {
	if (!quiet)
	    printf("%ls\n", buf.contents);
	if (histfile)
	    maybe_refresh_file();
	really_add_history(buf.contents);
	wb_clear(&buf);
    }
    wb_destroy(&buf);

    if (histfile) {
	fflush(histfile);
	lock_file(fileno(histfile), F_UNLCK);
    }
}

#if YASH_ENABLE_HELP
const char *fc_help[] = { Ngt(
"fc - list or re-execute command history\n"
), Ngt(
"\tfc [-qr] [-e editor] [first [last]]\n"
"\tfc -s [-q] [old=new] [first]\n"
"\tfc -l [-nrv] [first [last]]\n"
), Ngt(
"The first form invokes an editor to edit a temporary file containing the\n"
"command history and, after the editor exited, executes commands in the file.\n"
), Ngt(
"The second form, with the -s (--silent) option, re-executes commands in the\n"
"history without invoking an editor. If <old=new> is given, string <old> in\n"
"the command is replaced with <new> before execution.\n"
), Ngt(
"The third form, with the -l (--list) option, prints the command history. In\n"
"this form commands are not re-executed.\n"
), (
"\n"
), Ngt(
"The -e (--editor) option can be used to specify the editor. If this option\n"
"is not given, the value of $FCEDIT is used as the default editor. If $FCEDIT\n"
"is not set either, `ed' is the last resort.\n"
), Ngt(
"The -n (--no-numbers) option suppresses command numbers, which would\n"
"otherwise be printed preceding each command.\n"
), Ngt(
"The -q (--quiet) option suppresses echoing the executed command.\n"
), Ngt(
"The -r (--reverse) option reverses the order of commands to be edited or\n"
"printed.\n"
), Ngt(
"The -v (--verbose) option prints command dates in the listing.\n"
), (
"\n"
), Ngt(
"The range of command history to be edited or printed can be specified by\n"
"parameters <first> and <last>. If the value of <first> or <last> is an\n"
"integer, it is considered a history number. If it is negative, the command\n"
"is counted from the end of history: for example, -2 represents the second\n"
"previous command. If the value is not an integer, it is considered part of a\n"
"command: it indicates the most recent command beginning with it.\n"
"If <first> is omitted, it defaults to -16 (with -l) or -1 (without -l).\n"
"If <last> is omitted, it defaults to -1 (with -l) or <first> (without -l).\n"
), (
"\n"
), Ngt(
"In the POSIXly correct mode, the -q and -v options are not available.\n"
), NULL };
#endif /* YASH_ENABLE_HELP */

/* The "history" builtin, which accepts the following options:
 *  -c: clear whole history
 *  -d: remove history entry
 *  -r: read history from a file
 *  -s: add history entry
 *  -w: write history into a file
 *  -F: flush history file */
int history_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"clear",      OPTARG_NONE,     L'c', },
	{ L"delete",     OPTARG_REQUIRED, L'd', },
	{ L"read",       OPTARG_REQUIRED, L'r', },
	{ L"set",        OPTARG_REQUIRED, L's', },
	{ L"write",      OPTARG_REQUIRED, L'w', },
	{ L"flush-file", OPTARG_NONE,     L'F', },
#if YASH_ENABLE_HELP
	{ L"help",       OPTARG_NONE,     L'-', },
#endif
        { NULL, 0, 0, },
    };

    if (hist_lock) {
	xerror(0, Ngt("cannot be used during line-editing"));
	return Exit_FAILURE;
    }
    maybe_init_history();

    bool hasoption = false, removedthis = false;
    wchar_t opt;
    int result;
    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"cd:r:s:w:F", long_options, NULL))) {
	hasoption = true;
	switch (opt) {
	    case L'c':
		history_clear_all();
		break;
	    case L'd':
		result = history_delete(xoptarg);
		if (result != Exit_SUCCESS)
		    return result;
		break;
	    case L'r':
		result = history_read(xoptarg);
		if (result != Exit_SUCCESS)
		    return result;
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
		if (result != Exit_SUCCESS)
		    return result;
		break;
	    case L'F':
		history_refresh_file();
		break;
#if YASH_ENABLE_HELP
	    case L'-':
		if (print_builtin_help(ARGV(0)) != Exit_SUCCESS)
		    return Exit_FAILURE;
		break;
#endif
	    default:  print_usage:
		fprintf(stderr, gt("Usage:  history [-cF] [-d entry] "
			    "[-s command] [-r file] [-w file] [n]\n"));
		return Exit_ERROR;
	}
    }

    /* print history */
    if (xoptind < argc) {
	if (xoptind + 1 != argc) {
	    xerror(0, Ngt("too many operands are specified"));
	    goto print_usage;
	}

	int count;
	if (!xwcstoi(ARGV(xoptind), 10, &count)) {
	    xerror(errno, Ngt("`%ls' is not a valid integer"), ARGV(xoptind));
	    return Exit_ERROR;
	}
	if (count <= 0)
	    return Exit_SUCCESS;
	fc_update_history();
	if (histlist.count == 0)
	    return Exit_SUCCESS;
	return fc_print_entries(stdout,
		get_nth_newest_entry(count), histlist.Newest,
		false, FC_NUMBERED);
    } else if (!hasoption) {
	fc_update_history();
	if (histlist.count == 0)
	    return Exit_SUCCESS;
	return fc_print_entries(stdout,
		histlist.Oldest, histlist.Newest, false, FC_NUMBERED);
    } else {
	return Exit_SUCCESS;
    }
}

/* Clears all the history. */
void history_clear_all(void)
{
    if (histfile) {
	lock_file(fileno(histfile), F_WRLCK);
	update_history(false);
    }
    clear_all_entries();
    if (histfile) {
	refresh_file(histfile);
	lock_file(fileno(histfile), F_UNLCK);
    }
}

/* Deletes a history entry specified by the argument string. */
int history_delete(const wchar_t *s)
{
    long n;
    histentry_T *e;
    int result;

    if (histfile) {
	lock_file(fileno(histfile), F_WRLCK);
	update_time();
	update_history(true);
    }

    if (!xwcstol(s, 10, &n) || n == 0) {
	e = fc_search_entry(s);
    } else {
	if (n >= 0) {
	    if (n > INT_MAX)
		n = INT_MAX;
	    e = find_entry((unsigned) n, true);
	    if (e->number != (unsigned) n)
		e = Histlist;
	} else {
	    if (n < INT_MIN + 1)
		n = INT_MIN + 1;
	    n = -n;
	    if ((unsigned) n > histlist.count)
		e = Histlist;
	    else
		e = get_nth_newest_entry((unsigned) n);
	}
	if (e == Histlist)
	    xerror(0, Ngt("no such history entry `%ls'"), s);
    }

    if (e != Histlist) {
	if (histfile)
	    fwprintf(histfile, L"d%X\n", e->number);
	remove_entry(e);
	result = Exit_SUCCESS;
    } else {
	result = Exit_FAILURE;
    }

    if (histfile)
	lock_file(fileno(histfile), F_UNLCK);

    return result;
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
    if ((ferror(f) != 0) | (fclose(f) != 0))
	goto error;
    return Exit_SUCCESS;

error:
    xerror(errno, Ngt("cannot read history from file `%ls'"), s);
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
    assert(histlist.count > 0);
    clearerr(f);
    fc_print_entries(f, histlist.Oldest, histlist.Newest, false, FC_RAW);
    if ((ferror(f) != 0) | (f != stdout && fclose(f) != 0))
	goto error;
    return Exit_SUCCESS;

error:
    xerror(errno, Ngt("cannot write history to file `%ls'"), s);
    return Exit_FAILURE;
}

/* Refreshes the history file. */
void history_refresh_file(void)
{
    if (histfile) {
	lock_file(fileno(histfile), F_WRLCK);
	update_time();
	update_history(false);
	if (histfile) {
	    check_histfile_pid();
	    refresh_file(histfile);
	    lock_file(fileno(histfile), F_UNLCK);
	}
    }
}

#if YASH_ENABLE_HELP
const char *history_help[] = { Ngt(
"history - manage command history\n"
), Ngt(
"\thistory [-cF] [-d entry] [-s command] [-r file] [-w file] [n]\n"
), Ngt(
"Without options, the history built-in prints the command history. The number\n"
"of entries to print can be specified by argument <n>.\n"
), Ngt(
"The -c (--clear) option clears the command history completely.\n"
), Ngt(
"The -d (--delete) option deletes the specified <entry>. You can specify\n"
"<entry> by the number or by the prefix.\n"
), Ngt(
"The -s (--set) option replaces the last history entry with <command>.\n"
), Ngt(
"The -r (--read) option reads history entries from <file>.\n"
), Ngt(
"The -w (--write) option writes all the history entries into <file>.\n"
), Ngt(
"The -F (--flush-file) option rebuilds the history file, removing unused old\n"
"data.\n"
), NULL };
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
