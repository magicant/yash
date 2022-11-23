/* Yash: yet another shell */
/* mail.c: mail checking */
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


#include "common.h"
#include "mail.h"
#include <errno.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <wchar.h>
#include "expand.h"
#include "hashtable.h"
#include "option.h"
#include "parser.h"
#include "plist.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"


/* The type of objects used to remember the status of files for mail checking.*/
typedef struct mailfile_T {
#if HAVE_ST_MTIM || HAVE_ST_MTIMESPEC
    struct timespec mf_mtim;
# define mf_mtime mf_mtim.tv_sec
#else
    time_t mf_mtime;
# if HAVE_ST_MTIMENSEC || HAVE___ST_MTIMENSEC
    unsigned long mf_mtimensec;
# endif
#endif
    char mf_filename[];
} mailfile_T;

static void activate(void);
static void inactivate(void);
static bool is_time_to_check_mail(void);
static void check_mail_and_print_message(void);
static void handle_mailpath(wchar_t *paths)
    __attribute__((nonnull));
static void handle_mailpath_element(const wchar_t *s)
    __attribute__((nonnull));
static bool is_update(const char *path)
    __attribute__((nonnull));
static void print_message(const wchar_t *message)
    __attribute__((nonnull));


/* A hashtable that contains `mailfile_T' objects.
 * The keys are pointers to the `mf_filename' member of `mailfile_T' objects,
 * and the values are pointers to the `mailfile_T' objects.
 * When mail checking is not activated, the capacity of the hashtable is set to
 * zero. */
static hashtable_T mailfiles;

/* The time of last mail check. */
static time_t lastchecktime = 0;


/* If it is time to check mail, checks if the mail file is updated, and if so
 * prints a message. */
void check_mail(void)
{
    if (is_time_to_check_mail())
	check_mail_and_print_message();
}

/* Activates `mailfiles'. */
void activate(void)
{
    if (mailfiles.capacity == 0)
	ht_init(&mailfiles, hashstr, htstrcmp);
}

/* Inactivates `mailfiles'. */
void inactivate(void)
{
    if (mailfiles.capacity > 0) {
	ht_destroy(ht_clear(&mailfiles, vfree));
	mailfiles.capacity = 0;
	lastchecktime = 0;
    }
}

/* Decides if it is time to check mail now.
 * Inactivates mail checking and returns false if the $MAILCHECK variable is not
 * a valid integer.
 * Sets `lastchecktime' to now if the return value is true. */
bool is_time_to_check_mail(void)
{
    const wchar_t *mailcheck = getvar(L VAR_MAILCHECK);
    if (mailcheck == NULL || mailcheck[0] == L'\0') {
	inactivate();
	return false;
    }

    long interval;
    if (!xwcstol(mailcheck, 10, &interval) || interval < 0) {
	inactivate();
	return false;
    }

    time_t now = time(NULL);
    if (now == -1 || now - lastchecktime < interval)
	return false;

    lastchecktime = now;
    return true;
}

/* Checks if the mail file is updated and prints a message if so. */
void check_mail_and_print_message(void)
{
    /* Firstly, check the $MAILPATH variable */
    struct get_variable_T mailpath = get_variable(L VAR_MAILPATH);
    switch (mailpath.type) {
	case GV_NOTFOUND:
	    break;
	case GV_SCALAR:
	    activate();
	    handle_mailpath(mailpath.values[0]);
	    goto mailpath_handled;
	case GV_ARRAY:
	case GV_ARRAY_CONCAT:
	    activate();
	    for (size_t i = 0; i < mailpath.count; i++)
		handle_mailpath_element(mailpath.values[i]);
mailpath_handled:
	    if (mailpath.freevalues)
		plfree(mailpath.values, free);
	    return;
    }

    /* Next, check the $MAIL variable */
    const wchar_t *mail = getvar(L VAR_MAIL);
    if (mail != NULL) {
	activate();

	char *path = malloc_wcstombs(mail);
	if (path != NULL) {
	    if (is_update(path))
		fprintf(stderr, "%s\n", gt("You have new mail."));
	    free(path);
	}
	return;
    }

    /* disable mail check since the variables are not set */
    inactivate();
}

/* Splits the specified string at colons and calls `handle_mailpath_element' for
 * each component.
 * This function directly modifies the string. */
void handle_mailpath(wchar_t *paths)
{
    const wchar_t *start;

next:
    start = paths;
    for (;;) {
	switch (*paths) {
	    case L'\0':
		handle_mailpath_element(start);
		return;
	    case L':':
		*paths = L'\0';
		handle_mailpath_element(start);
		paths++;
		goto next;
	    case L'\\':
		paths++;
		if (*paths == L'\0') {
		    handle_mailpath_element(start);
		    return;
		}
		/* falls thru! */
	    default:
		paths++;
		continue;
	}
    }
}

/* Parses the specified $MAILPATH component and checks for update. */
void handle_mailpath_element(const wchar_t *s)
{
    xstrbuf_T path;
    mbstate_t state;

    sb_init(&path);
    memset(&state, 0, sizeof state);
    for (;;) {
	switch (*s) {
	    case L'\0':
		goto check;
	    case L'%':
		s++;
		goto check;
	    case L'\\':
		s++;
		if (*s == L'\0')
		    goto check;
		/* falls thru! */
	    default:
		sb_wccat(&path, *s, &state);
		s++;
		continue;
	}
    }
check:
    sb_wccat(&path, L'\0', &state);
    if (is_update(path.contents)) {
	if (*s == L'\0')
	    fprintf(stderr, "%s\n", gt("You have new mail."));
	else
	    print_message(s);
    }
    sb_destroy(&path);
}

/* Checks if the specified file is updated. */
bool is_update(const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0) {
	st.st_size = 0;
	st.st_mtime = 0;
#if HAVE_ST_MTIM
	st.st_mtim.tv_nsec = 0;
#elif HAVE_ST_MTIMESPEC
	st.st_mtimespec.tv_nsec = 0;
#elif HAVE_ST_MTIMENSEC
	st.st_mtimensec = 0;
#elif HAVE___ST_MTIMENSEC
	st.__st_mtimensec = 0;
#endif
    }

    bool result;
    mailfile_T *mf = ht_get(&mailfiles, path).value;

    if (mf != NULL) {
	result = (st.st_size > 0 || posixly_correct) &&
	    (st.st_mtime != 0) && (st.st_mtime != mf->mf_mtime
#if HAVE_ST_MTIM
	    || st.st_mtim.tv_nsec != mf->mf_mtim.tv_nsec
#elif HAVE_ST_MTIMESPEC
	    || st.st_mtimespec.tv_nsec != mf->mf_mtim.tv_nsec
#elif HAVE_ST_MTIMENSEC
	    || (unsigned long) st.st_mtimensec != mf->mf_mtimensec
#elif HAVE___ST_MTIMENSEC
	    || (unsigned long) st.__st_mtimensec != mf->mf_mtimensec
#endif
	    );
    } else {
	mf = xmallocs(sizeof *mf,
		add(strlen(path), 1), sizeof *mf->mf_filename);
	strcpy(mf->mf_filename, path);
	ht_set(&mailfiles, mf->mf_filename, mf);
	result = false;
    }

#if HAVE_ST_MTIM
    mf->mf_mtim = st.st_mtim;
#elif HAVE_ST_MTIMESPEC
    mf->mf_mtim = st.st_mtimespec;
#else
    mf->mf_mtime = st.st_mtime;
# if HAVE_ST_MTIMENSEC
    mf->mf_mtimensec = (unsigned long) st.st_mtimensec;
# elif HAVE___ST_MTIMENSEC
    mf->mf_mtimensec = (unsigned long) st.__st_mtimensec;
# endif
#endif
    return result;
}

/* Prints the specified `message' after performing parameter expansion on it. */
void print_message(const wchar_t *message)
{
    /* assuming the parse state is saved */
    wchar_t *msg = parse_and_expand_string(message, NULL, true);
    if (msg != NULL) {
	fprintf(stderr, "%ls\n", msg);
	free(msg);
    }
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
