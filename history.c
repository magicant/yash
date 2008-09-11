/* Yash: yet another shell */
/* history.c: command history management */
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
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include "builtin.h"
#include "exec.h"
#include "history.h"
#include "job.h"
#include "option.h"
#include "path.h"
#include "redir.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"
#include "yash.h"


/* The maximum size of history list (<= INT_MAX) */
#define MAX_HISTORY      1000000
/* The minimum value of `max_number',
 * which must be at least 32768 according to POSIX. */
#define MIN_MAX_NUMBER   100000
/* The default size of the history list,
 * which must be at least 128 according to POSIX. */
#define DEFAULT_HISTSIZE 500


typedef struct histentry_T {
    struct histentry_T *prev, *next;
    unsigned number;
    time_t time;
    char value[];
} histentry_T;
/* `prev' and `next' are always non-NULL: the newest entry's `next' and the
 * oldest entry's `prev' point to `histlist'. */
/* The value is stored as a multibyte string rather than a wide string to save
 * memory space. The value does not have a newline at the end. */
/* Basically the `number' is increased for each entry, but the numbers are
 * limited to some extent. If the number exceeds the limit, it is wrapped
 * around to 1, so a newer entry may have a smaller number than that of a older
 * entry. The limit is no less than $HISTSIZE, so all the entries have different
 * numbers anyway. */

/* The main history list. */
#define HISTLIST ((histentry_T *) &histlist)
static struct {
    histentry_T *newest, *oldest;
    unsigned count;
} histlist = {
    .newest = HISTLIST,
    .oldest = HISTLIST,
    .count = 0,
};
/* `newest' and `oldest' correspond to `prev' and `next' of a histentry_T.
 * They must be the first two elements of the structure so that `histlist' is
 * cast to histentry_T and they are used as `prev' and `next' properly. */

/* The number of the next new entry. Must be positive. */
static unsigned next_number = 1;
/* The maximum limit of the number of an entry,
 * which is always no less than `histsize' or `MIN_MAX_NUMBER'.
 * The number of any entry is not greater than this value. */
static unsigned max_number = MIN_MAX_NUMBER;
/* The size limit of the history list. */
static unsigned histsize = DEFAULT_HISTSIZE;

/* The current time returned by `time' */
static time_t now = (time_t) -1;

/* A pointer to the last appended entry. Must be non-NULL.
 * This entry is (considered to be) the last entry in the history file.
 * When `write_history' is called with the append flag set, entries after this
 * entry are written to the file. */
static histentry_T *lastappended = HISTLIST;


static void update_time(void);
static void set_histsize(unsigned newsize);
static histentry_T *new_entry(const char *line)
    __attribute__((nonnull));
static void remove_entry(histentry_T *entry)
    __attribute__((nonnull));
static void remove_last_entry(void);
static histentry_T *replace_entry(histentry_T *entry, const char *line)
    __attribute__((nonnull));
static histentry_T *find_entry(unsigned number, bool newer_if_not_found)
    __attribute__((pure));
static histentry_T *get_nth_newest_entry(unsigned n)
    __attribute__((pure));
static histentry_T *search_entry(const char *s, bool exact)
    __attribute__((nonnull,pure));
static void trim_list_size(void);
static bool is_reverse(const histentry_T *e1, const histentry_T *e2)
    __attribute__((nonnull,pure));


/* Updates the value of `now'. */
void update_time(void)
{
    now = time(NULL);
}

/* Changes `histsize' to `newsize' and `max_number' accordingly. */
/* `histsize' is set only once in initialization. */
void set_histsize(unsigned newsize)
{
    histsize = newsize;

    max_number = 10;
    while (max_number < 2 * histsize)
	max_number *= 10;
    /* `max_number' is the smallest power of 10 that is not less than
     * 2 * histsize. */
    if (max_number < MIN_MAX_NUMBER)
	max_number = MIN_MAX_NUMBER;
}

/* Add a new entry at the end of the list and returns it.
 * `line' is the value of the new entry.
 * If the number of the new entry is the same as that of the oldest existing
 * entry, the oldest entry is removed. */
histentry_T *new_entry(const char *line)
{
    if (histlist.oldest != HISTLIST && histlist.oldest->number == next_number)
	remove_entry(histlist.oldest);

    histentry_T *new = xmalloc(sizeof *new + strlen(line) + 1);
    new->prev = histlist.newest;
    new->next = HISTLIST;
    histlist.newest = new->prev->next = new;
    new->number = next_number++;
    new->time = now;
    strcpy(new->value, line);
    histlist.count++;

    if (next_number > max_number)
	next_number = 1;
    return new;
}

/* Removes the specified entry from the history */
void remove_entry(histentry_T *entry)
{
    assert(entry != NULL && entry != HISTLIST);
    entry->prev->next = entry->next;
    entry->next->prev = entry->prev;
    histlist.count--;
    if (lastappended == entry)
	lastappended = HISTLIST;
    free(entry);
}

/* Removes the newest entry and decreases `next_number'. */
void remove_last_entry(void)
{
    if (histlist.count > 0) {
	next_number = histlist.newest->number;
	remove_entry(histlist.newest);
    }
}

/* Replaces the value of `entry' with `line'.
 * `entry->time' is set to `now'. `entry->number' is not changed.
 * `entry' is `realloc'ed in this function.
 * Returns the new address of the entry. */
histentry_T *replace_entry(histentry_T *entry, const char *line)
{
    entry = xrealloc(entry, sizeof *entry + strlen(line) + 1);
    entry->prev->next = entry->next->prev = entry;
    entry->time = now;
    strcpy(entry->value, line);
    return entry;
}

/* Returns the entry that has the specified `number'.
 * If not found, the nearest neighbor entry is returned, newer or older
 * according to `newer_if_not_found'. If there is no neighbor either, HISTLIST
 * is returned. */
histentry_T *find_entry(unsigned number, bool newer_if_not_found)
{
    if (histlist.count == 0)
	return HISTLIST;

    unsigned oldestnum, nnewestnum, nnumber;
    oldestnum = histlist.oldest->number;
    nnewestnum = histlist.newest->number;
    nnumber = number;
    if (nnewestnum < oldestnum) {
	if (nnumber <= nnewestnum)
	    nnumber += max_number;
	nnewestnum += max_number;
    }

    if (nnumber < oldestnum)
	return newer_if_not_found ? histlist.oldest : HISTLIST;
    if (nnumber > nnewestnum)
	return newer_if_not_found ? HISTLIST : histlist.newest;

    histentry_T *e;
    if (2 * (nnumber - oldestnum) < nnewestnum - oldestnum) {
	/* search from the oldest */
	e = histlist.oldest;
	while (number < e->number)
	    e = e->next;
	while (number > e->number)
	    e = e->next;
	if (number != e->number && !newer_if_not_found)
	    e = e->prev;
    } else {
	/* search from the newest */
	e = histlist.newest;
	while (number > e->number)
	    e = e->prev;
	while (number < e->number)
	    e = e->prev;
	if (number != e->number && newer_if_not_found)
	    e = e->next;
    }
    return e;
}

/* Returns the nth newest entry (or the oldest entry if `n' is too big).
 * Returns HISTLIST if `n' is zero. */
histentry_T *get_nth_newest_entry(unsigned n)
{
    histentry_T *e = HISTLIST;

    if (histlist.count <= n)
	return histlist.oldest;
    while (n > 0)
	e = e->prev, n--;
    return e;
}

/* Searches for the newest entry whose value begins with `s' (if not `exact') or
 * whose value is equal to `s' (if `exact').
 * Returns HISTLIST if not found. */
histentry_T *search_entry(const char *s, bool exact)
{
    histentry_T *e = histlist.newest;

    while (e != HISTLIST) {
	if (exact ? strcmp(e->value, s) == 0
		: matchstrprefix(e->value, s) != NULL)
	    break;
	e = e->prev;
    }
    return e;
}

/* Removes oldest entries so that the list size is no greater than `histsize'.*/
void trim_list_size(void)
{
    while (histlist.count > histsize)
	remove_entry(histlist.oldest);
}

/* Returns true iff `e1' is newer than `e2'. */
bool is_reverse(const histentry_T *e1, const histentry_T *e2)
{
    assert(histlist.count > 0);

    unsigned n1 = e1->number;
    unsigned n2 = e2->number;
    unsigned newest = histlist.newest->number;
    unsigned oldest = histlist.oldest->number;

    return (n1 <= newest && newest < oldest && oldest <= n2)
	|| (n2 <= n1 && (oldest <= n2 || n1 <= newest));
}


/********** External functions **********/

/* Initializes history function.
 * Called just once after the yashrc file is sourced.
 * Does nothing if the shell is not interactive. */
void init_history(void)
{
    if (!is_interactive)
	return;

    const wchar_t *vhistsize = getvar(VAR_HISTSIZE);
    if (vhistsize && vhistsize[0]) {
	unsigned size;
	wchar_t *end;
	errno = 0;
	size = wcstoul(vhistsize, &end, 10);
	if (!errno && !*end)
	    set_histsize(size);
    }

    read_history(NULL);
}

/* Reads history entries from a history file.
 * `histfile' is the pathname to the history file.
 * If `histfile' is NULL, entries are read from $HISTFILE.
 * The entries read are appended to the current history list.
 * Returns true iff successful. */
bool read_history(const wchar_t *histfile)
{
    if (!histfile) {
	histfile = getvar(VAR_HISTFILE);
	if (!histfile)
	    return true;
    }
    update_time();

    char *filename = malloc_wcstombs(histfile);
    if (!filename)
	return false;

    FILE *f = fopen(filename, "r");
    free(filename);
    if (!f)
	return false;

    xstrbuf_T buf;
    sb_init(&buf);
    while (fgets(buf.contents + buf.length, buf.maxlength - buf.length + 1, f)){
	size_t len = strlen(buf.contents + buf.length);
	// `len' may be 0 if a null character is input
	if (len == 0)
	    break;
	buf.length += len;
	if (buf.contents[buf.length - 1] == L'\n') {
	    buf.contents[--buf.length] = L'\0';  // remove the trailing newline
	    new_entry(buf.contents);
	    sb_clear(&buf);
	} else {
	    if (buf.maxlength - buf.length < 80)
		sb_setmax(&buf, buf.maxlength + 150);
	}
    }
    sb_destroy(&buf);
    fclose(f);

    trim_list_size();
    lastappended = histlist.newest;
    return true;
}

/* Writes the history entries to the specified file.
 * If `histfile' is NULL, entries are written into $HISTFILE.
 * If `append' is true, entries are appended to the end of the file. */
bool write_history(const wchar_t *histfile, bool append)
{
    if (!histfile) {
	histfile = getvar(VAR_HISTFILE);
	if (!histfile)
	    return true;
    }

    char *filename = malloc_wcstombs(histfile);
    if (!filename)
	return false;

    /* The history file should not be readable by other users for privacy
     * reasons, hence such default permissions. */
    int fd = open(filename,
	    O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC),
	    S_IRUSR | S_IWUSR);
    free(filename);
    if (fd < 0)
	return false;

    FILE *f = fdopen(fd, append ? "a" : "w");
    if (!f) {
	xclose(fd);
	return false;
    }

    const histentry_T *e = append ? lastappended : HISTLIST;
    while ((e = e->next) != HISTLIST) {
	fputs(e->value, f);
	fputc('\n', f);
    }

    return fclose(f) == 0;
}

/* Removes all entries in the history list. */
void clear_history(void)
{
    histentry_T *e = histlist.oldest;
    while (e != HISTLIST) {
	histentry_T *next = e->next;
	free(e);
	e = next;
    }
    histlist.oldest = histlist.newest = HISTLIST;
    histlist.count = 0;
    lastappended = HISTLIST;
}

/* Adds the specified `line' to the history list.
 * If `removelast' is true, the last entry is removed before addition. */
bool add_history(const wchar_t *line, bool removelast)
{
    char *l = malloc_wcstombs(line);
    if (!l)
	return false;

    update_time();
    if (removelast && histlist.newest != HISTLIST)
	replace_entry(histlist.newest, l);
    else
	new_entry(l);
    free(l);
    trim_list_size();
    return true;
}


/********** Builtins **********/

enum fcprinttype_T {
    NUMBERED, UNNUMBERED, RAW,
};

static histentry_T *fc_search_entry(const wchar_t *prefix)
    __attribute__((nonnull));
static int fc_print_entries(
	FILE *f, const histentry_T *first, const histentry_T *last,
	bool reverse, enum fcprinttype_T type)
    __attribute__((nonnull));
static int fc_exec_entry(const histentry_T *entry,
	const wchar_t *old, const wchar_t *new)
    __attribute__((nonnull(1)));
static int fc_edit_and_exec_entries(
	const histentry_T *first, const histentry_T *last,
	bool reverse, const wchar_t *editor)
    __attribute__((nonnull(1,2)));
static void fc_read_history(const char *filename)
    __attribute__((nonnull));

/* The "fc" builtin, which accepts the following options:
 * -e: specify the editor to edit history
 * -l: list history
 * -n: suppress numbering entries
 * -r: reverse entry order
 * -s: execute without editing */
int fc_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"editor",     xrequired_argument, L'e', },
	{ L"list",       xno_argument,       L'l', },
	{ L"no-numbers", xno_argument,       L'n', },
	{ L"reverse",    xno_argument,       L'r', },
	{ L"silent",     xno_argument,       L's', },
	{ L"help",       xno_argument,       L'-', },
	{ NULL, 0, 0, },
    };

    const wchar_t *editor = NULL;
    bool list = false, nonum = false, rev = false, silent = false;

    wchar_t opt;
    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"e:lnrs", long_options, NULL))) {
	switch (opt) {
	    case L'e':  editor = xoptarg;  break;
	    case L'l':  list   = true;     break;
	    case L'n':  nonum  = true;     break;
	    case L'r':  rev    = true;     break;
	    case L's':  silent = true;     break;
	    case L'-':
		print_builtin_help(ARGV(0));
		return EXIT_SUCCESS;
	    default:
		goto print_usage;
	}
    }
    if ((editor && (list || silent))
	    || (list && silent)
	    || (rev && silent)
	    || (nonum && !list)
	    || (argc - xoptind > 2))
	goto print_usage;

    /* remove the entry for this "fc" command */
    if (!list)
	remove_last_entry();

    if (histlist.count == 0) {
	if (list) {
	    return EXIT_SUCCESS;
	} else {
	    xerror(0, Ngt("history is empty"));
	    return EXIT_FAILURE1;
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
	wchar_t *end;
	errno = 0;
	first = wcstol(vfirst, &end, 10);
	if (!vfirst[0] || errno || *end || first == 0) {
	    efirst = fc_search_entry(vfirst);
	    if (!efirst)
		return EXIT_FAILURE1;
	} else {
	    if (first > INT_MAX)
		first = INT_MAX;
	    else if (first < INT_MIN)
		first = INT_MIN;
	}
    } else {
	first = list ? -16 : -1;
    }
    if (vlast) {
	wchar_t *end;
	errno = 0;
	last = wcstol(vlast, &end, 10);
	if (!vlast[0] || errno || *end || last == 0) {
	    elast = fc_search_entry(vlast);
	    if (!elast)
		return EXIT_FAILURE1;
	} else {
	    if (last > INT_MAX)
		last = INT_MAX;
	    else if (last < INT_MIN)
		last = INT_MIN;
	}
    } else {
	last = list ? -1 : 0;
    }

    /* find the first and last entries */
    if (!efirst) {
	if (first >= 0) {
	    efirst = find_entry(first, true);
	    if (silent && efirst->number != first)
		efirst = HISTLIST;
	} else {
	    if (silent && -first > histlist.count)
		efirst = HISTLIST;
	    else
		efirst = get_nth_newest_entry(-first);
	}
	if (efirst == HISTLIST) {
	    assert(vfirst != NULL);
	    xerror(0, Ngt("%ls: no such entry"), vfirst);
	    return EXIT_FAILURE1;
	}
    }
    if (!elast) {
	if (last == 0)
	    elast = efirst;
	else if (last >= 0)
	    elast = find_entry(last, false);
	else
	    elast = get_nth_newest_entry(-last);
	if (elast == HISTLIST) {
	    assert(vlast != NULL);
	    xerror(0, Ngt("%ls: no such entry"), vlast);
	    return EXIT_FAILURE1;
	}
    }
    if (is_reverse(efirst, elast)) {
	const histentry_T *temp = efirst;  efirst = elast;  elast = temp;
	rev = !rev;
    }
    assert(efirst != NULL);  assert(efirst != HISTLIST);
    assert(elast  != NULL);  assert(elast  != HISTLIST);

    update_time();

    if (list)
	return fc_print_entries(stdout, efirst, elast, rev,
		nonum ? UNNUMBERED : NUMBERED);
    else if (silent)
	return fc_exec_entry(efirst, old, new);
    else
	return fc_edit_and_exec_entries(efirst, elast, rev, editor);

print_usage:
    fprintf(stderr, gt("Usage:  fc [-r] [-e editor] [first [last]]\n"
                       "        fc -s [old=new] [first]\n"
		       "        fc -l [-nr] [first [last]]\n"));
    return EXIT_ERROR;
}

histentry_T *fc_search_entry(const wchar_t *prefix)
{
    char *s = malloc_wcstombs(prefix);
    histentry_T *e = NULL;
    if (s)
	e = search_entry(s, false);
    free(s);
    if (!e)
	xerror(0, Ngt("no such entry beginning with `%ls'"), prefix);
    return e;
}

/* Print history entries between `first' and `last'. */
int fc_print_entries(
	FILE *f, const histentry_T *first, const histentry_T *last,
	bool reverse, enum fcprinttype_T type)
{
    const histentry_T *start, *end;
    if (!reverse)
	start = first, end = last;
    else
	start = last, end = first;
    for (;;) {
	switch (type) {
	    case NUMBERED:
		fprintf(f, "%u\t%s\n", start->number, start->value);
		break;
	    case UNNUMBERED:
		fprintf(f, "\t%s\n", start->value);
		break;
	    case RAW:
		fprintf(f, "%s\n", start->value);
		break;
	}
	if (start == end)
	    break;
	start = !reverse ? start->next : start->prev;
    }
    return EXIT_SUCCESS;
}

/* Executes the value of `entry'.
 * If `old' is not NULL, the first occurrence of `old' in the command is
 * replaced with `new' before execution (but the entry's value is unchanged). */
int fc_exec_entry(const histentry_T *entry,
	const wchar_t *old, const wchar_t *new)
{
    wchar_t *code = malloc_mbstowcs(entry->value);
    if (!code) {
	xerror(0, Ngt("unexpected error"));
	return EXIT_ERROR;
    }

    if (old) {
	xwcsbuf_T buf;
	wchar_t *p;

	wb_initwith(&buf, code);
	p = wcsstr(buf.contents, old);
	if (p)
	    wb_replace(&buf, p - buf.contents, wcslen(old), new, SIZE_MAX);
	code = wb_towcs(&buf);

	char *mbscode = malloc_wcstombs(code);
	if (mbscode)
	    new_entry(mbscode);
	free(mbscode);
    } else {
	new_entry(entry->value);
    }

    exec_wcs(code, "fc", false);
    free(code);
    return laststatus;
}

int fc_edit_and_exec_entries(
	const histentry_T *first, const histentry_T *last,
	bool reverse, const wchar_t *editor)
{
    char *temp;
    int fd;
    FILE *f;
    pid_t cpid;

    fd = create_temporary_file(&temp, S_IRUSR | S_IWUSR);
    if (fd < 0) {
	xerror(errno, Ngt("cannot create temporary file to edit history"));
	return EXIT_FAILURE1;
    }
    f = fdopen(fd, "w");
    if (!f) {
	xerror(errno, Ngt("cannot open temporary file to edit history"));
	xclose(fd);
	return EXIT_FAILURE1;
    }

    cpid = fork_and_reset(0, true, 0);
    if (cpid < 0) {  // fork failed
	xerror(0, Ngt("cannot invoke editor to edit history"));
	fclose(f);
	if (unlink(temp) < 0)
	    xerror(errno, Ngt("cannot remove temporary file `%s'"), temp);
	free(temp);
	return EXIT_FAILURE1;
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

	if (laststatus != EXIT_SUCCESS) {
	    xerror(0, Ngt("editor returned non-zero status"));
	    f = NULL;
	} else {
	    f = reopen_with_shellfd(fopen(temp, "r"), "r");
	    if (f == NULL)
		xerror(errno, Ngt("cannot read command from `%s'"), temp);
	}
	if (f != NULL)
	    fc_read_history(temp);

	if (unlink(temp) < 0)
	    xerror(errno, Ngt("cannot remove temporary file `%s'"), temp);
	free(temp);

	if (f != NULL) {
	    exec_input(f, "fc", false, false);
	    remove_shellfd(fileno(f));
	    fclose(f);
	}
	return laststatus;
    } else {  // child process
	fc_print_entries(f, first, last, reverse, RAW);
	fclose(f);

	wchar_t *command = malloc_wprintf(L"%ls %s",
		editor ? editor : L"${FCEDIT:-ed}", temp);
	exec_wcs(command, "fc", true);
	assert(false);
    }
}

void fc_read_history(const char *filename)
{
    wchar_t *wname = malloc_mbstowcs(filename);
    if (wname) {
	read_history(wname);
	free(wname);
    }
}

const char fc_help[] = Ngt(
"fc - list or re-execute command history\n"
"\tfc [-r] [-e editor] [first [last]]\n"
"\tfc -s [old=new] [first]\n"
"\tfc -l [-nr] [first [last]]\n"
"The first form invokes an editor to edit a temporary file containing command\n"
"history and, after the editor exited, executes commands in the file.\n"
"The second form, with the -s (--silent) option, re-executes commands in the\n"
"history without invoking an editor. If <old=new> is given, <old> in the\n"
"command is replaced with <new> before execution.\n"
"The third form, with the -l (--list) option, prints command history. In this\n"
"form commands are not re-executed.\n"
"\n"
"The -e (--editor) option can be used to specify the editor. If this option\n"
"not given, the value of $FCEDIT is used as the default editor. If $FCEDIT is\n"
"not set either, \"ed\" is the last resort.\n"
"The -n (--no-numbers) option suppresses command numbers, which would\n"
"otherwise be printed preceding each command.\n"
"The -r (--reverse) option reverses the order of commands to be edited or\n"
"printed.\n"
"\n"
"The range of command history to be edited or printed can be specified by\n"
"parameters <first> and <last>. If the value of <first> or <last> is an\n"
"integer, it is considered a history number. If it is negative, the command\n"
"is counted from the end of history: for example, -2 represents the second\n"
"previous command. If the value is not an integer, it is considered part of a\n"
"command: it indicates the most recent command beginning with it.\n"
"If <first> is omitted, it defaults to -16 (with -l) or -1 (without -l).\n"
"If <last> is omitted, it defaults to -1 (with -l) or <first> (without -l).\n"
);


/* vim: set ts=8 sts=4 sw=4 noet: */
