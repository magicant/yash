/* Yash: yet another shell */
/* mail.c: mail checking */
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


#include "common.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <wchar.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "expand.h"
#include "hashtable.h"
#include "mail.h"
#include "parser.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"


/* The type of objects used to remember the status of files for mail checking.*/
typedef struct mailfile_T {
    time_t mtime;
    char filename[];
} mailfile_T;

static void activate(void);
static void inactivate(void);
static bool is_time_to_check_mail(void);
static void check_mail_and_print_message(void);
static char *get_path_and_message(const char *pathmsg, wchar_t **msgp)
    __attribute__((nonnull));
static bool is_update(const char *path)
    __attribute__((nonnull));
static void print_message(const wchar_t *message)
    __attribute__((nonnull));


/* Whether mail checking is active */
static bool active = false;

/* The hashtable to contain `mailfile_T' objects.
 * The keys are pointers to the `filename' member of `mailfile_T' objects, and
 * the values are pointers to the `mailfile_T' objects themselves.
 * This hashtable is valid when and only when `active' is true. */
static hashtable_T mailfiles;

/* The time of last mail check. */
static time_t lastchecktime = 0;


/* If it is time to check mail, Checks if the mail file is updated, and if so
 * prints a message. The parsing state must be saved with `save_parse_state'
 * before calling this function. */
void check_mail(void)
{
    if (is_time_to_check_mail())
	check_mail_and_print_message();
}

/* Activates `mailfiles'. */
void activate(void)
{
    if (!active) {
	active = true;
	ht_init(&mailfiles, hashstr, htstrcmp);
    }
}

/* Inactivates `mailfiles'. */
void inactivate(void)
{
    if (active) {
	active = false;
	ht_clear(&mailfiles, vfree);
	ht_destroy(&mailfiles);
	lastchecktime = 0;
    }
}

/* Decides if it is time to check mail now.
 * Returns false if the $MAILCHECK variable is not a valid integer.
 * Sets `lastchecktime' to now if the return value is true.
 * Calls `inactivate' if $MAILCHECK is not a valid integer. */
bool is_time_to_check_mail(void)
{
    const wchar_t *mailcheck = getvar(L VAR_MAILCHECK);
    if (!mailcheck || !mailcheck[0]) {
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
    char *const *mailpath = get_path_array(PA_MAILPATH);

    if (!mailpath) {
	const wchar_t *mail = getvar(L VAR_MAIL);
	if (mail) {
	    activate();

	    char *path = malloc_wcstombs(mail);
	    if (path) {
		if (is_update(path))
		    fprintf(stderr, "%s\n", gt("You have mail."));
		free(path);
	    }
	} else {
	    inactivate();
	}
    } else {
	activate();
	for (; *mailpath; mailpath++) {
	    char *path;
	    wchar_t *msg;

	    path = get_path_and_message(*mailpath, &msg);
	    if (path && msg) {
		if (is_update(path))
		    print_message(msg);
	    }
	    free(path);
	    free(msg);
	}
    }
}

/* Decomposes a mailpath element to the pathname and the message.
 * The pathname is returned as the return value and the message is assigned to
 * `*msgp'. If there is no message, the default message is assigned.
 * Both the pathname and the message should be freed by the caller.
 * On error, NULL may be returned or assigned. */
char *get_path_and_message(const char *pathmsg, wchar_t **msgp)
{
    *msgp = NULL;

    wchar_t *wpathmsg = malloc_mbstowcs(pathmsg);
    if (!wpathmsg)
	return NULL;

    xwcsbuf_T pathbuf;
    wb_init(&pathbuf);
    for (const wchar_t *w = wpathmsg; *w; w++) {
	if (*w == L'\\') {
	    w++;
	    if (*w)
		wb_wccat(&pathbuf, *w);
	    else
		break;
	} else if (*w == L'%') {
	    w++;
	    *msgp = xwcsdup(w);
	    break;
	} else {
	    wb_wccat(&pathbuf, *w);
	}
    }

    free(wpathmsg);
    if (*msgp == NULL)
	*msgp = malloc_mbstowcs(gt("You have mail."));
    return realloc_wcstombs(wb_towcs(&pathbuf));
}

/* Checks if the specified file is updated. */
bool is_update(const char *path)
{
    time_t newtime;
    {
	struct stat st;
	if (stat(path, &st) < 0)
	    newtime = 0;
	else
	    newtime = st.st_mtime;
    }

    mailfile_T *mf = ht_get(&mailfiles, path).value;
    if (!mf) {
	mf = xmalloc(sizeof *mf + strlen(path) + 1);
	mf->mtime = newtime;
	strcpy(mf->filename, path);
	ht_set(&mailfiles, mf->filename, mf);
    }

    bool result = mf->mtime != newtime && newtime != 0;
    mf->mtime = newtime;
    return result;
}

/* Prints the specified `message' after performing parameter expansion on it. */
void print_message(const wchar_t *message)
{
    /* assuming the parse state is saved */
    wchar_t *msg = parse_and_expand_string(message, NULL, true);
    if (msg) {
	fprintf(stderr, "%ls\n", msg);
	free(msg);
    }
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
