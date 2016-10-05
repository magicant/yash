/* Yash: yet another shell */
/* predict.c: command line input prediction */
/* (C) 2016 magicant */

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


#include "../common.h"
#include "predict.h"
#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <wchar.h>
#include "../exec.h"
#include "../hashtable.h"
#include "../history.h"
#include "../plist.h"
#include "../redir.h"
#include "../strbuf.h"
#include "../util.h"
#include "../variable.h"


typedef struct attr_count_T {
    /* Hash code of the string (or anything) that identifies this attribute */
    hashval_T attrhash;
    /* How many times the command was executed with this attribute being true */
    size_t count;
} attr_count_T;

typedef struct record_T {
    attr_count_T *attrs; // Array of attr_count_T's. NULL if attrslen == 0
    size_t attrslen;
} record_T;

#define PREV_ATTR_NAME L"prev:1="


/* Hashtable mapping command lines (wchar_t *) to record data (record_T *). */
static hashtable_T stattable;

/* File stream for the statistics file. */
static FILE *file = NULL;


static bool open_file(void);
static void close_file(void);
static kvpair_T find_or_create_record(const wchar_t *cmdline)
    __attribute__((nonnull));
static size_t find_attr_count_index(const record_T *r, hashval_T attrhash)
    __attribute__((nonnull,pure));
static attr_count_T *find_or_create_attr_count(record_T *r, hashval_T attrhash)
    __attribute__((nonnull));
static void increase_attr(record_T *r, const wchar_t *attr)
    __attribute__((nonnull));
static bool read_records_from_file(void);
static void maybe_init(void);
static void record_attr(record_T *r, const wchar_t *attr_format, ...)
    __attribute__((nonnull(1,2)));


/* Opens the statistics file. Returns true iff successful. */
bool open_file(void)
{
    assert(file == NULL);

    const wchar_t *home = getvar(L VAR_HOME);
    if (home == NULL || home[0] != L'/')
	return false;

    // TODO filename should be configurable
    xwcsbuf_T wfilename; // filename = ~/.yash_history2
    wb_init(&wfilename);
    wb_cat(&wfilename, home);
    if (wfilename.contents[wfilename.length - 1] != L'/')
	wb_wccat(&wfilename, L'/');
    wb_cat(&wfilename, L".yash_history2");

    char *mbsfilename = realloc_wcstombs(wb_towcs(&wfilename));
    if (mbsfilename == NULL)
	return false;

    int fd = open(mbsfilename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    free(mbsfilename);
    if (fd < 0)
	return false;

    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)
	    || (st.st_mode & (S_IRWXG | S_IRWXO))) {
	xclose(fd);
	return false;
    }

    fd = move_to_shellfd(fd);
    if (fd < 0)
	return false;

    file = fdopen(fd, "r+");
    if (file == NULL) {
	remove_shellfd(fd);
	xclose(fd);
	return false;
    }

    return true;
}

/* Closes the statistics file if open. */
void close_file(void)
{
    if (file == NULL)
	return;
    fclose(file);
    file = NULL;
}

kvpair_T find_or_create_record(const wchar_t *cmdline)
{
    kvpair_T kv = ht_get(&stattable, cmdline);
    if (kv.key == NULL) {
	cmdline = xwcsdup(cmdline);
	record_T *r = xmalloc(sizeof *r);
	r->attrs = NULL;
	r->attrslen = 0;
	ht_set(&stattable, cmdline, r);
	kv = (kvpair_T) { .key = (void *) cmdline, .value = (void *) r, };
    }
    return kv;
}

/* Perform binary search for an attr_count_T entry that have the specified
 * `attrhash' in the specified record `r'. If such entry is not found, returns
 * the index of the succeeding entry. */
size_t find_attr_count_index(const record_T *r, hashval_T attrhash)
{
    size_t min_index = 0, max_index = r->attrslen;
    while (max_index - min_index > 1) {
	size_t mid_index = (max_index - min_index) / 2 + min_index;
	if (r->attrs[mid_index].attrhash < attrhash)
	    min_index = mid_index;
	else
	    max_index = mid_index;
    }
    if (min_index == max_index || r->attrs[min_index].attrhash >= attrhash)
	return min_index;
    else
	return max_index; // == min_index + 1
}

attr_count_T *find_or_create_attr_count(record_T *r, hashval_T attrhash)
{
    size_t index = find_attr_count_index(r, attrhash);
    if (index < r->attrslen && r->attrs[index].attrhash == attrhash)
	return &r->attrs[index];

    // create a new attr_count_T entry
    r->attrs = xreallocn(r->attrs, r->attrslen + 1, sizeof *r->attrs);
    memmove(&r->attrs[index + 1], &r->attrs[index],
	    (r->attrslen - index) * sizeof *r->attrs);
    r->attrslen++;

    attr_count_T *ac = &r->attrs[index];
    ac->attrhash = attrhash;
    ac->count = 1; // not 0. Cromwell's rule.
    return ac;
}

/* Increases the attribute count for the specified attribute. */
void increase_attr(record_T *r, const wchar_t *attr)
{
    hashval_T attrhash = hashwcs(attr);
    attr_count_T *ac = find_or_create_attr_count(r, attrhash);
    ac->count++;
}

/* Reads statistics from the file. Returns false on error.
 * In addition to attributes recorded in the file, this function adds the
 * prev:1 attribute for each record based on the command line of the previous
 * record. */
bool read_records_from_file(void)
{
    xwcsbuf_T buf, prev_attr;
    wb_init(&buf);
    wb_init(&prev_attr);

    assert(file != NULL);
    while (wb_truncate(&buf, 0), read_line(file, &buf)) {
	if (buf.contents[0] != L'_' || buf.contents[1] != L'=')
	    break;

	kvpair_T kv = find_or_create_record(&buf.contents[2]);
	const wchar_t *cmdline = kv.key;
	record_T *r = kv.value;

	// Add the prev:1 attribute.
	if (prev_attr.length == 0) {
	    // No previous command. Just prepare prev_attr.
	    wb_cat(&prev_attr, PREV_ATTR_NAME);
	} else {
	    increase_attr(r, prev_attr.contents);
	    wb_truncate(&prev_attr, wcslen(PREV_ATTR_NAME));
	}
	// Save cmdline for next record
	wb_cat(&prev_attr, cmdline);

	// Read attributes from file, each attribute per line.
	while (wb_truncate(&buf, 0), read_line(file, &buf) && buf.length > 0)
	    increase_attr(r, buf.contents);
    }

    wb_destroy(&prev_attr);
    wb_destroy(&buf);

    return !ferror(file);
}

/* Initialize the prediction module if not yet initialized. */
void maybe_init(void)
{
    if (stattable.capacity == 0)
	ht_init(&stattable, hashwcs, htwcscmp);

    if (file == NULL)
	if (open_file())
	    if (!read_records_from_file())
		close_file();
}

/* Constructs an attribute name by formatting `attr_format' with the remaining
 * arguments and increments the count for the attribute in the specified record.
 * The attribute name must not include a newline. */
void record_attr(record_T *r, const wchar_t *attr_format, ...)
{
    va_list va;
    va_start(va, attr_format);

    wchar_t *attr = malloc_vwprintf(attr_format, va);
    increase_attr(r, attr);
    if (file != NULL) {
	fputws(attr, file);
	fputwc(L'\n', file);
    }
    free(attr);

    va_end(va);
}

/* Records the argument command line in the statistics file.
 * Only the first line of `cmdline' is recorded. */
void le_record_entered_command(const wchar_t *cmdline)
{
    maybe_init();

    // Drop lines other than the first.
    wchar_t *firstline = NULL;
    const wchar_t *newline = wcschr(cmdline, L'\n');
    if (newline != NULL)
	cmdline = firstline = xwcsndup(cmdline, newline - cmdline);

    kvpair_T kv = find_or_create_record(cmdline);
    cmdline = kv.key;
    record_T *r = kv.value;

    if (file != NULL)
	fwprintf(file, L"_=%ls\n", cmdline);

    record_attr(r, L"prev_exit_status=%d", laststatus);

    const wchar_t *pwd = getvar(L VAR_PWD);
    if (pwd != NULL)
	record_attr(r, L"var:PWD=%ls", pwd);

    const histlink_T *newest = histlist.Newest;
    if (newest != &histlist.link) {
	// Don't use record_attr for PREV_ATTR_NAME because we don't write the
	// attribute to the statistics file.
	const histentry_T *prev = (const histentry_T *) newest;
	wchar_t *attr = malloc_wprintf(L"%ls%s", PREV_ATTR_NAME, prev->value);
	increase_attr(r, attr);
	free(attr);
    }

    if (file != NULL) {
	fputwc(L'\n', file);
	fflush(file);
    }

    free(firstline);
}


// TODO need to support a case where the statistics file is written to by
// another yash process.
// TODO need to lock the statistics file while accessing it.
// TODO need to add an option to disable this feature.


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
