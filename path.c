/* Yash: yet another shell */
/* path.c: filename-related utilities */
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
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#if HAVE_PATHS_H
# include <paths.h>
#endif
#include "builtin.h"
#include "exec.h"
#include "hashtable.h"
#include "option.h"
#include "path.h"
#include "plist.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"
#include "wfnmatch.h"


/* Checks if `path' is a regular file. */
bool is_regular_file(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) && S_ISREG(st.st_mode);
}

/* Checks if `path' is a non-regular file. */
bool is_irregular_file(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) && !S_ISREG(st.st_mode);
}

/* Checks if `path' is a readable regular file. */
bool is_readable(const char *path)
{
    return is_regular_file(path) && access(path, R_OK) == 0;
}

/* Checks if `path' is an executable regular file. */
bool is_executable(const char *path)
{
    return is_regular_file(path) && access(path, X_OK) == 0;
}

/* Checks if `path' is a directory. */
bool is_directory(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

/* Checks if two files are the same file. */
bool is_same_file(const char *path1, const char *path2)
{
    struct stat stat1, stat2;
    return stat(path1, &stat1) == 0 && stat(path2, &stat2) == 0
	&& stat1.st_dev == stat2.st_dev && stat1.st_ino == stat2.st_ino;
}

/* Canonicalizes a pathname.
 * This is only a string manipulation function; the actual file system doesn't
 * affect the result.
 * `path' must not be NULL and the result is a newly malloced string. */
wchar_t *canonicalize_path(const wchar_t *path)
{
    if (!path[0])
	return xwcsdup(path);

    /* count the number of leading L'/'s */
    size_t leadingslashcount = 0;
    while (path[0] == L'/') {
	leadingslashcount++;
	path++;
    }

    plist_T list;
    wchar_t p[wcslen(path) + 1];
    wcscpy(p, path);
    pl_init(&list);

    /* add each pathname component to `list', replacing each L'/' with L'\0'
     * in `p'. */
    pl_add(&list, p);
    for (wchar_t *pp = p; *pp; pp++) {
	if (*pp == L'/') {
	    *pp = L'\0';
	    pl_add(&list, pp + 1);
	}
    }

    /* firstly, remove L""s and L"."s from the list */
    for (size_t i = 0; i < list.length; ) {
	if (wcscmp(list.contents[i], L"") == 0
		|| wcscmp(list.contents[i], L".") == 0) {
	    pl_remove(&list, i, 1);
	} else {
	    i++;
	}
    }

    /* next, remove L".."s and their parent */
    for (size_t i = 0; i < list.length; ) {
	if (wcscmp(list.contents[i], L"..") == 0) {
	    if (i == 0) {
		/* Speaking posixly correctly, L".." must not be removed if it
		 * is the first pathname component. However, in this rule,
		 * L"/../" is not canonicalized to L"/", which is an unnatural
		 * behavior. So if `posixly_correct' is false, we remove L".."s
		 * following the root. */
		if (!posixly_correct && leadingslashcount > 0) {
		    pl_remove(&list, i, 1);
		    continue;
		}
	    } else {
		/* If L".." is not the first component, we remove it and its
		 * parent, except when the parent is also L"..". */
		if (wcscmp(list.contents[i - 1], L"..") != 0) {
		    pl_remove(&list, i - 1, 2);
		    i--;
		    continue;
		}
	    }
	}
	i++;
    }

    /* finally, concatenate all the components in the list */
    xwcsbuf_T result;
    wb_init(&result);
    switch (leadingslashcount) {  /* put the leading slash(es) */
	case 0:
	    break;
	case 2:
	    wb_cat(&result, L"//");
	    break;
	default:  /* more than 2 slashes are canonicalized to a single slash */
	    wb_wccat(&result, L'/');
	    break;
    }
    for (size_t i = 0; i < list.length; i++) {
	wb_cat(&result, list.contents[i]);
	if (i < list.length - 1)
	    wb_wccat(&result, L'/');
    }
    pl_destroy(&list);
    if (result.length == 0)
	/* if the result is empty, we return L"." instead */
	wb_wccat(&result, L'.');
    return wb_towcs(&result);
}

/* Checks if the specified `path' is already canonicalized. */
bool is_canonicalized(const wchar_t *path)
{
    wchar_t *canon = canonicalize_path(path);
    bool result = wcscmp(path, canon) == 0;
    free(canon);
    return result;
}

/* Returns the result of `getcwd' as a newly malloced string.
 * On error, `errno' is set and NULL is returned. */
char *xgetcwd(void)
{
#if GETCWD_AUTO_MALLOC
    char *pwd = getcwd(NULL, 0);
    return pwd ? xrealloc(pwd, strlen(pwd) + 1) : NULL;
#else
    size_t pwdlen = 40;
    char *pwd = xmalloc(pwdlen);
    while (getcwd(pwd, pwdlen) == NULL) {
	if (errno == ERANGE) {
	    pwdlen *= 2;
	    pwd = xrealloc(pwd, pwdlen);
	} else {
	    free(pwd);
	    pwd = NULL;
	    break;
	}
    }
    return pwd;
#endif
}


/* Searches the specified directories `dirs' for a file named `name' that
 * satisfies the specified condition.
 * name:   a pathname to be searched for.
 *         If `name' is an absolute path, a copy of it is simply returned.
 * dirs:   a NULL-terminated array of strings that are the names of
 *         directories to search. An empty string is treated as the current
 *         directory. if `dirs' is NULL, only the current directory is searched.
 * cond:   a function that determines the specified pathname satisfies a
 *         certain condition. If `cond' is NULL, the pathname is checked for
 *         its existence.
 * For each directory in `dirs', in order, the directory name and "/" and
 * `name' are concatenated to produce a pathname and `cond' is called with
 * the pathname. If `cond' returns true, search is complete and the pathname
 * is returned as a newly malloced string. If `cond' returns false to all the
 * produced pathnames, NULL is returned.
 * `name' and all the directory names in `dirs' must start and end in a initial
 * shift state. */
char *which(
	const char *restrict name,
	char *const *restrict dirs,
	bool cond(const char *path))
{
    static char *const null[] = { "", NULL, };

    if (!name[0])
	return NULL;
    if (!dirs)
	dirs = null;
    if (name[0] == '/')
	return xstrdup(name);

    size_t namelen = strlen(name);
    const char *dir;
    while ((dir = *dirs++)) {
	size_t dirlen = strlen(dir);
	char path[dirlen + namelen + 3];
	if (dirlen > 0) {
	    /* concatenate `dir' and `name' to produce a pathname `path' */
	    strcpy(path, dir);
	    path[dirlen] = '/';
	    strcpy(path + dirlen + 1, name);
	} else {
	    /* if `dir' is empty, it's considered to be the current directory */
	    strcpy(path, name);
	}
	if (cond ? cond(path) : (access(path, F_OK) == 0))
	    return xstrdup(path);
    }
    return NULL;
}

/* Closes `DIR' surely;
 * Calls `closedir' until it doesn't return EINTR. */
int xclosedir(DIR *dir)
{
    int result;
    while ((result = closedir(dir)) < 0 && errno == EINTR);
    return result;
}


/********** Command Hashtable **********/

static wchar_t *get_default_path(void)
    __attribute__((malloc,warn_unused_result));

/* A hashtable from command names to their full path.
 * Keys are pointers to a multibyte string containing a command name and
 * values are pointers to a multibyte string containing the commands' full path.
 * For each entry, the key string is part of the value, that is, the last
 * pathname component of it. */
static hashtable_T cmdhash;

/* Initializes the command hashtable. */
void init_cmdhash(void)
{
    assert(cmdhash.capacity == 0);
    ht_init(&cmdhash, hashstr, htstrcmp);
}

/* Empties the command hashtable. */
void clear_cmdhash(void)
{
    ht_clear(&cmdhash, vfree);
}

/* Searches PATH for the specified command and returns its full pathname.
 * If `forcelookup' is false and the command is already entered in the command
 * hashtable, the value in the hashtable is returned. Otherwise, `which' is
 * called to search for the command, the result is entered into the hashtable
 * and then it is returned. If no command is found, NULL is returned. */
const char *get_command_path(const char *name, bool forcelookup)
{
    const char *path;

    if (!forcelookup) {
	path = ht_get(&cmdhash, name).value;
	if (path && is_executable(path))
	    return path;
    }

    path = which(name, get_path_array(PA_PATH), is_executable);
    if (path && path[0] == '/') {
	size_t namelen = strlen(name), pathlen = strlen(path);
	const char *pathname = path + pathlen - namelen;
	assert(strcmp(name, pathname) == 0);
	vfree(ht_set(&cmdhash, pathname, path));
    } else {
	vfree(ht_remove(&cmdhash, name));
    }
    return path;
}

/* Fills the command hashtable, searching PATH for all commands whose name
 * starts with the specified prefix.
 * If PATH is not set, this function does nothing.
 * Relative pathnames in PATH are ignored.
 * If `prefix' is NULL or empty, all the commands in PATH is entered.
 * This function never prints error messages.
 * If `ignorecase' is true, search is done case-insensitively, though
 * multibyte characters are not handled properly to search quickly. */
void fill_cmdhash(const char *prefix, bool ignorecase)
{
    char *const *pa = get_path_array(PA_PATH);
    if (!pa)
	return;

    if (!prefix)
	prefix = "";
    size_t plen = strlen(prefix);
    char pfx[plen + 1];
    if (ignorecase) {
	for (size_t i = 0; i < plen; i++)
	    pfx[i] = tolower(prefix[i]);
	pfx[plen] = '\0';
    }

    /* We search PATH in reverse order, and ones that are found later
     * (= placed at lower indeces in PATH) survive. */
    for (size_t i = plcount((void **) pa); i-- > 0; ) {
	const char *dirpath = pa[i];
	if (dirpath[0] != '/')
	    continue;  /* ignore relative path */

	DIR *dir = opendir(dirpath);
	if (!dir)
	    continue;

	size_t dirpathlen = strlen(dirpath);
	struct dirent *de;
	while ((de = readdir(dir))) {
	    /* go to next if `prefix' doesn't match */
	    if (!ignorecase) {
		if (strncmp(prefix, de->d_name, plen) != 0)
		    goto next;
	    } else {
		for (size_t j = 0; j < plen; j++)
		    if (tolower(de->d_name[j]) != pfx[j])
			goto next;
	    }

	    /* produce a full path */
	    char *path = xmalloc(dirpathlen + strlen(de->d_name) + 2);
	    strncpy(path, dirpath, dirpathlen);
	    path[dirpathlen] = '/';
	    strcpy(path + dirpathlen + 1, de->d_name);

	    /* enter to hashtable if it is executable */
	    if (is_executable(path))
		vfree(ht_set(&cmdhash, path + dirpathlen + 1, path));
	    else
		free(path);
next:;
	}

	xclosedir(dir);
    }
}

/* Last result of `get_command_path_default'. */
static char *gcpd_value;
/* Paths for `get_command_path_default'. */
static char **default_path;

/* Searches for the specified command using the system's default PATH.
 * The full path of the command is returned if found, NULL otherwise.
 * The return value is valid until the next call to this function. */
const char *get_command_path_default(const char *name)
{
    assert(name != gcpd_value);
    free(gcpd_value);

    if (!default_path) {
	wchar_t *defpath = get_default_path();
	if (!defpath)
	    return gcpd_value = NULL;
	default_path = decompose_paths(defpath);
	free(defpath);
    }
    return gcpd_value = which(name, default_path, is_executable);
}

/* Returns the system's default PATH as a newly malloced string.
 * The default PATH is (assumed to be) guaranteed to contain all the standard
 * utilities. */
wchar_t *get_default_path(void)
{
#if HAVE_PATHS_H && defined _PATH_STDPATH
    return malloc_mbstowcs(_PATH_STDPATH);
#else
    size_t size = 100;
    char *buf = xmalloc(size);
    size_t s = confstr(_CS_PATH, buf, size);

    if (s > size) {
	size = s;
	buf = xrealloc(buf, size);
	s = confstr(_CS_PATH, buf, size);
    }
    if (s == 0 || s > size) {
	free(buf);
	return NULL;
    }
    return realloc_mbstowcs(buf);
#endif
}


/********** Home Directory Cache **********/

/* Calls `getpwnam' until it doesn't return EINTR. */
struct passwd *xgetpwnam(const char *name)
{
    struct passwd *pw;
    do {
	errno = 0;
	pw = getpwnam(name);
    } while (!pw && errno == EINTR);
    return pw;
}

/* A hashtable from users' names to their home directory paths.
 * Keys are pointers to a wide string containing a user's login name and
 * values are pointers to a wide string containing their home directory name.
 * A memory block for the key/value string must be allocated at once;
 * When the value is `free'd, the key is `free'd as well. */
static hashtable_T homedirhash;

/* Initializes the home directory hashtable. */
void init_homedirhash(void)
{
    static bool initialized = false;
    if (!initialized) {
	initialized = true;
	ht_init(&homedirhash, hashwcs, htwcscmp);
    }
}

/* Empties the home directory hashtable. */
void clear_homedirhash(void)
{
    ht_clear(&homedirhash, vfree);
}

/* Returns the full pathname of the specified user's home directory.
 * If `forcelookup' is false and the path is already entered in the home
 * directory hashtable, the value in the hashtable is returned. Otherwise,
 * `getpwnam' is called, the result is entered into the hashtable and then
 * it is returned. If no entry is returned by `getpwnam', NULL is returned.  */
const wchar_t *get_home_directory(const wchar_t *username, bool forcelookup)
{
    const wchar_t *path;

    if (!forcelookup) {
	path = ht_get(&homedirhash, username).value;
	if (path)
	    return path;
    }

    char *mbsusername = malloc_wcstombs(username);
    if (!mbsusername)
	return NULL;

    struct passwd *pw = xgetpwnam(mbsusername);
    free(mbsusername);
    if (!pw)
	return NULL;

    /* enter to the hashtable */
    xwcsbuf_T dir;
    wb_init(&dir);
    if (wb_mbscat(&dir, pw->pw_dir) != NULL) {
	wb_destroy(&dir);
	return NULL;
    }
    wb_wccat(&dir, L'\0');
    size_t usernameindex = dir.length;
    wb_cat(&dir, username);
    wchar_t *dirname = wb_towcs(&dir);
    vfree(ht_set(&homedirhash, dirname + usernameindex, dirname));
    return dirname;
}


/********** wglob **********/

enum wglbrflags {
    WGLB_followlink = 1 << 0,
    WGLB_period     = 1 << 1,
};

static int wglob_sortcmp(const void *v1, const void *v2)
    __attribute__((pure,nonnull));
static bool wglob_search(const wchar_t *restrict pattern, enum wglbflags flags,
	xstrbuf_T *restrict const dirname, xwcsbuf_T *restrict const wdirname,
	plist_T *restrict dirstack, plist_T *restrict list)
    __attribute__((nonnull));
static bool wglob_start_recursive_search(const wchar_t *restrict pattern,
	enum wglbflags flags, enum wglbrflags rflags,
	xstrbuf_T *restrict const dirname, xwcsbuf_T *restrict const wdirname,
	plist_T *restrict dirstack, plist_T *restrict list)
    __attribute__((nonnull));
static bool wglob_recursive_search(const wchar_t *restrict pattern,
	enum wglbflags flags, enum wglbrflags rflags,
	xstrbuf_T *restrict const dirname, xwcsbuf_T *restrict const wdirname,
	plist_T *restrict dirstack, plist_T *restrict list)
    __attribute__((nonnull));
static bool is_reentry(const struct stat *st, const plist_T *dirstack)
    __attribute__((pure,nonnull));

/* A wide string version of `glob'.
 * Adds all pathnames that matches the specified pattern.
 * pattern: a pattern
 * flags:   a bitwise OR of the following flags:
 *          WGLB_MARK:     directory items have '/' appended to their name
 *          WGLB_NOESCAPE: backslashes in the pattern are not treated specially
 *          WGLB_CASEFOLD: do matching case-insensitively
 *          WGLB_PERIOD:   L'*' and L'?' match L'.' at the head
 *          WGLB_NOSORT:   don't sort resulting items
 *          WGLB_RECDIR:   allow recursive search with L"**"
 * list:    a list of pointers to wide strings to which resulting items are
 *          added.
 * Returns true iff successful. However, some result items may be added to the
 * list even if unsuccessful.
 * If the pattern is invalid, immediately returns false.
 * Minor errors such as permission errors are ignored. */
bool wglob(const wchar_t *restrict pattern, enum wglbflags flags,
	plist_T *restrict list)
{
    size_t listbase = list->length;
    xstrbuf_T dir;
    xwcsbuf_T wdir;
    plist_T dirstack;

    if (!pattern[0])
	return true;

    sb_init(&dir);
    wb_init(&wdir);
    if (flags & WGLB_RECDIR)
	pl_init(&dirstack);
    while (pattern[0] == L'/') {
	sb_ccat(&dir, '/');
	wb_wccat(&wdir, L'/');
	pattern++;
    }

    bool succ = wglob_search(pattern, flags, &dir, &wdir, &dirstack, list);
    sb_destroy(&dir);
    wb_destroy(&wdir);
    if (flags & WGLB_RECDIR)
	pl_destroy(&dirstack);
    if (!succ)
	return false;

    if (!(flags & WGLB_NOSORT)) {
	size_t count = list->length - listbase;  /* # of resulting items */
	if (count > 0) {
	    qsort(list->contents + listbase, count, sizeof (void *),
		    wglob_sortcmp);
	    /* remove duplicates */
	    for (size_t i = list->length; --i > listbase; ) {
		if (wcscmp(list->contents[i], list->contents[i-1]) == 0) {
		    free(list->contents[i]);
		    pl_remove(list, i, 1);
		}
	    }
	}
    }
    return true;
}

/* This function is passed to `qsort' in `wglob'. */
int wglob_sortcmp(const void *v1, const void *v2)
{
    return wcscoll(*(const wchar_t *const *) v1, *(const wchar_t *const *) v2);
}

/* Searches the specified directory and add filenames that match the pattern.
 * `dirname' is the name of directory to search, ending with '/' or empty.
 * `wdirname' is a wide string version of `dirname'.
 * For the meaning of the other arguments, see `wglob'.
 * `pattern' must not start with L'/'.
 * `dirname' must end with '/' except when empty. An empty `dirname' specifies
 * the current directory. Though the contents of `dirname' may be changed
 * during this function, the contents are restored when the function returns.
 * `wdirname' must contain the same string as that of `dirname' and the same
 * properties apply. */
bool wglob_search(
	const wchar_t *restrict pattern,
	enum wglbflags flags,
	xstrbuf_T *restrict const dirname,
	xwcsbuf_T *restrict const wdirname,
	plist_T *restrict dirstack,
	plist_T *restrict list)
{
    const size_t savedirlen = dirname->length;
    const size_t savewdirlen = wdirname->length;
#define RESTORE_DIRNAME \
    ((void) (dirname->contents[dirname->length = savedirlen] = '\0'))
#define RESTORE_WDIRNAME \
    ((void) (wdirname->contents[wdirname->length = savewdirlen] = L'\0'))

    assert(pattern[0] != L'/');
    if (!pattern[0]) {
	/* If the pattern is empty, add `dirname' itself
	 * except when `dirname' is also empty.
	 * Note that `is_directory' returns false for an empty string. */
	if (is_directory(dirname->contents))
	    pl_add(list, xwcsdup(wdirname->contents));
	return true;
    } else if (flags & WGLB_RECDIR) {
	/* check if it's a recursive search pattern */
	const wchar_t *p = pattern;
	enum wglbrflags rflags = 0;
	if (p[0] == L'.') {
	    rflags |= WGLB_period;
	    p++;
	}
	if (wcsncmp(p, L"**/", 3) == 0) {
	    p += 3;
	    while (p[0] == L'/') p++;
	    return wglob_start_recursive_search(
		    p, flags, rflags, dirname, wdirname, dirstack, list);
	} else if (wcsncmp(p, L"***/", 4) == 0) {
	    rflags |= WGLB_followlink;
	    p += 4;
	    while (p[0] == L'/') p++;
	    return wglob_start_recursive_search(
		    p, flags, rflags, dirname, wdirname, dirstack, list);
	}
    }

    DIR *dir = opendir(dirname->contents[0] ? dirname->contents : ".");
    if (!dir)
	return true;

    size_t patlen = wcscspn(pattern, L"/");
    bool isleaf = (pattern[patlen] == L'\0');
    wchar_t pat[patlen + 1];
    wcsncpy(pat, pattern, patlen);
    pat[patlen] = L'\0';

    enum wfnmflags wfnmflags = wfnmflags;
    size_t sml = sml;
    /* If the pattern is literal, we can use `wcscmp' instead of `wfnmatchl'. */
    bool domatch = pattern_is_nonliteral(pat);
    if (domatch) {
	wfnmflags = WFNM_PATHNAME | WFNM_PERIOD;
	if (flags & WGLB_NOESCAPE) wfnmflags |= WFNM_NOESCAPE;
	if (flags & WGLB_CASEFOLD) wfnmflags |= WFNM_CASEFOLD;
	if (flags & WGLB_PERIOD)   wfnmflags &= ~WFNM_PERIOD;
	sml = shortest_match_length(pat, wfnmflags);
    }

    struct dirent *de;
    bool ok = true;
    while (ok && (de = readdir(dir))) {
	if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
	    continue;

	wchar_t *wentname = malloc_mbstowcs(de->d_name);
	if (!wentname)
	    continue;

	size_t match = domatch
	    ? wfnmatchl(pat, wentname, wfnmflags, WFNM_WHOLE, sml)
	    : ((wcscmp(pat, wentname) == 0) ? 1 : WFNM_NOMATCH);
	if (match == WFNM_ERROR) {
	    ok = false;
	} else if (match != WFNM_NOMATCH) {
	    /* matched! */
	    if (isleaf) {
		/* add the matched pathname to the list */
		wb_cat(wdirname, wentname);
		if (flags & WGLB_MARK) {
		    sb_cat(dirname, de->d_name);
		    if (is_directory(dirname->contents))
			wb_wccat(wdirname, L'/');
		    RESTORE_DIRNAME;
		}
		pl_add(list, xwcsdup(wdirname->contents));
		RESTORE_WDIRNAME;
	    } else {
		/* search the subdirectories */
		assert(pattern[patlen] == L'/');
		sb_cat(dirname, de->d_name);
		sb_ccat(dirname, '/');
		wb_cat(wdirname, wentname);
		wb_wccat(wdirname, L'/');
		const wchar_t *subpat = pattern + patlen + 1;
		while (subpat[0] == L'/') {
		    sb_ccat(dirname, '/');
		    wb_wccat(wdirname, L'/');
		    subpat++;
		}
		ok = wglob_search(
			subpat, flags, dirname, wdirname, dirstack, list);
		RESTORE_DIRNAME;
		RESTORE_WDIRNAME;
	    }
	}
	free(wentname);
    }

    xclosedir(dir);
    return ok;
}

/* Starts a recursive search of the specified directory.
 * `rflags' is a bitwise OR of the following flags:
 *   WGLB_followlink: search symlinked subdirectories
 *   WGLB_period:     search even if the directory name starts with a period
 * See `wglob_search' for the meaning of the other arguments and the return
 * value. */
bool wglob_start_recursive_search(
	const wchar_t *restrict pattern,
	enum wglbflags flags,
	enum wglbrflags rflags,
	xstrbuf_T *restrict const dirname,
	xwcsbuf_T *restrict const wdirname,
	plist_T *restrict dirstack,
	plist_T *restrict list)
{
    char *dir = dirname->contents[0] ? dirname->contents : ".";
    bool followlink = rflags & WGLB_followlink;
    bool ok = true;
    struct stat st;
    if ((followlink ? stat : lstat)(dir, &st) >= 0
	    && S_ISDIR(st.st_mode) && !is_reentry(&st, dirstack)) {
	pl_add(dirstack, &st);
	ok = wglob_recursive_search(
		pattern, flags, rflags, dirname, wdirname, dirstack, list);
	pl_pop(dirstack);
    }
    return ok;
}

/* Does a recursive search */
bool wglob_recursive_search(
	const wchar_t *restrict pattern,
	enum wglbflags flags,
	enum wglbrflags rflags,
	xstrbuf_T *restrict const dirname,
	xwcsbuf_T *restrict const wdirname,
	plist_T *restrict dirstack,
	plist_T *restrict list)
{
    const size_t savedirlen = dirname->length;
    const size_t savewdirlen = wdirname->length;

    /* Step 1: search `dirname' itself */
    if (!wglob_search(pattern, flags, dirname, wdirname, dirstack, list))
	return false;

    assert(dirname->length == savedirlen);
    assert(wdirname->length == savewdirlen);

    /* Step 2: recursively search the subdirectories of `dirname' */
    DIR *dir = opendir(dirname->contents[0] ? dirname->contents : ".");
    if (!dir)
	return true;

    struct dirent *de;
    bool ok = true;
    while (ok && (de = readdir(dir))) {
	if ((rflags & WGLB_period)
		? strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0
		: de->d_name[0] == '.')
	    continue;

	bool followlink = rflags & WGLB_followlink;
	struct stat st;
	sb_cat(dirname, de->d_name);
	if ((followlink ? stat : lstat)(dirname->contents, &st) >= 0
		&& S_ISDIR(st.st_mode) && !is_reentry(&st, dirstack)) {
	    /* recurse if it's a directory */
	    wchar_t *wentname = malloc_mbstowcs(de->d_name);
	    if (wentname) {
		sb_ccat(dirname, '/');
		wb_cat(wdirname, wentname);
		wb_wccat(wdirname, L'/');
		free(wentname);
		pl_add(dirstack, &st);
		ok = wglob_recursive_search(pattern, flags, rflags,
			dirname, wdirname, dirstack, list);
		pl_pop(dirstack);
		RESTORE_WDIRNAME;
	    }
	}
	RESTORE_DIRNAME;
    }

    xclosedir(dir);
    return ok;
}

/* Checks if it is a reentrance.
 * `dirstack' is a list of pointers to a `struct stat'.
 * Returns true iff any of the `struct stat' has the same inode as that of `st'.
 */
bool is_reentry(const struct stat *st, const plist_T *dirstack)
{
    for (size_t i = 0; i < dirstack->length; i++) {
	const struct stat *st2 = dirstack->contents[i];
	if (st->st_dev == st2->st_dev && st->st_ino == st2->st_ino)
	    return true;
    }
    return false;
}


/********** Builtins **********/

static inline void print_umask_octal(mode_t mode);
static inline void print_umask_symbolic(mode_t mode);
static inline int set_umask(const wchar_t *newmask)
    __attribute__((nonnull));
static inline mode_t copy_user_mask(mode_t mode)
    __attribute__((const));
static inline mode_t copy_group_mask(mode_t mode)
    __attribute__((const));
static inline mode_t copy_other_mask(mode_t mode)
    __attribute__((const));

/* options for "cd" and "pwd" builtins */
static const struct xoption cd_pwd_options[] = {
    { L"logical",  xno_argument, L'L', },
    { L"physical", xno_argument, L'P', },
    { L"help",     xno_argument, L'-', },
    { NULL, 0, 0, },
};

/* "cd" builtin, which accepts the following options:
 * -L: don't resolve symlinks (default)
 * -P: resolve symlinks
 * -L and -P are mutually exclusive: the one specified last is used. */
/* When the `autocd' option is enabled, this builtin may be called with an
 * argument which is a directory name rather than the string "cd". */
int cd_builtin(int argc, void **argv)
{
    bool logical = true;
    bool printnewdir = false;
    bool err = false;
    const wchar_t *oldpwd, *newpwd;
    xwcsbuf_T curpath;
    wchar_t opt;

    xoptind = 0, xopterr = true;
    if (wcscmp(ARGV(0), L"cd") == 0) {
	while ((opt = xgetopt_long(argv, L"LP", cd_pwd_options, NULL))) {
	    switch (opt) {
		case L'L':  logical = true;   break;
		case L'P':  logical = false;  break;
		case L'-':
		    print_builtin_help(ARGV(0));
		    return EXIT_SUCCESS;
		default:
		    fprintf(stderr, gt("Usage:  cd [-L|-P] [dir]\n"));
		    return EXIT_ERROR;
	    }
	}
    }

    oldpwd = getvar(VAR_PWD);
    if (oldpwd == NULL || oldpwd[0] != L'/') {
	char *pwd = xgetcwd();
	if (pwd == NULL) {
	    xerror(errno, Ngt("cannot find out current directory"));
	    err = true;
	} else {
	    wchar_t *wpwd = realloc_mbstowcs(pwd);
	    if (wpwd != NULL) {
		if (!set_variable(VAR_PWD, wpwd, false, false))
		    err = true;
		oldpwd = getvar(VAR_PWD);
	    } else {
		xerror(0, Ngt("name of current directory contains "
			    "invalid characters"));
		err = true;
	    }
	}
	if (oldpwd == NULL)
	    oldpwd = L".";  /* final resort */
    }

    if (argc <= xoptind) {  /* step 1-2 */
	newpwd = getvar(VAR_HOME);
	if (newpwd == NULL || newpwd[0] == L'\0') {
	    xerror(0, Ngt("$HOME not set"));
	    return EXIT_FAILURE1;
	}
    } else if (wcscmp(ARGV(xoptind), L"-") == 0) {
	newpwd = getvar(VAR_OLDPWD);
	if (newpwd == NULL || newpwd[0] == L'\0') {
	    xerror(0, Ngt("$OLDPWD not set"));
	    return EXIT_FAILURE1;
	}
	printnewdir = true;
    } else {
	newpwd = ARGV(xoptind);
    }

    wb_init(&curpath);

    if (newpwd[0] != L'.' || (newpwd[1] != L'\0' && newpwd[1] != L'/' &&
	    (newpwd[1] != L'.' || (newpwd[2] != L'\0' && newpwd[2] != L'/')))) {
	/* step 5: `newpwd' doesn't start with neither "." nor ".." */
	char *mbsnewpwd = malloc_wcstombs(newpwd);
	if (mbsnewpwd == NULL) {
	    wb_destroy(&curpath);
	    xerror(0, Ngt("unexpected error"));
	    return EXIT_ERROR;
	}
	char *path = which(mbsnewpwd, get_path_array(PA_CDPATH), is_directory);
	if (path != NULL) {
	    if (strcmp(mbsnewpwd, path) != 0)
		printnewdir = true;

	    /* If `path' is not an absolute path, we prepend `oldpwd' to make it
	     * absolute, although POSIX doesn't provide that we do this. */
	    if (path[0] != L'/')
		wb_wccat(wb_cat(&curpath, oldpwd), L'/');

	    wb_mbscat(&curpath, path);
	    free(mbsnewpwd);
	    free(path);
	    goto step7;
	}
	free(mbsnewpwd);
    }

    /* step 6: concatenate PWD, a slash and the operand */
    wb_cat(&curpath, oldpwd);
    wb_wccat(&curpath, L'/');
    wb_cat(&curpath, newpwd);

step7:
    if (!logical) {
	/* step 7 */
	char *mbscurpath = realloc_wcstombs(wb_towcs(&curpath));
	if (mbscurpath == NULL) {
	    xerror(0, Ngt("unexpected error"));
	    return EXIT_ERROR;
	}
	if (chdir(mbscurpath) < 0) {
	    xerror(errno, Ngt("`%s'"), mbscurpath);
	    free(mbscurpath);
	    return EXIT_FAILURE1;
	}
	free(mbscurpath);

	if (!set_variable(VAR_OLDPWD, xwcsdup(oldpwd), false, false))
	    err = true;

	char *actualpwd = xgetcwd();
	if (actualpwd == NULL) {
	    xerror(errno, Ngt("cannot find out new working directory"));
	    return EXIT_FAILURE1;
	} else {
	    if (printnewdir)
		printf("%s\n", actualpwd);

	    wchar_t *wactualpwd = realloc_mbstowcs(actualpwd);
	    if (wactualpwd == NULL) {
		xerror(0, Ngt("cannot convert multibyte characters "
			    "into wide characters"));
		return EXIT_FAILURE1;
	    } else {
		if (!set_variable(VAR_PWD, wactualpwd, false, false))
		    err = true;
	    }
	}
    } else {
	/* step 8-9 */
	wchar_t *path = canonicalize_path(curpath.contents);
	wb_destroy(&curpath);
	if (path[0] != L'\0') {
	    char *mbspath = malloc_wcstombs(path);
	    if (mbspath == NULL) {
		xerror(0, Ngt("unexpected error"));
		free(path);
		return EXIT_ERROR;
	    }
	    if (chdir(mbspath) < 0) {
		xerror(errno, Ngt("`%s'"), mbspath);
		free(mbspath);
		free(path);
		return EXIT_FAILURE1;
	    }
	    if (printnewdir)
		printf("%s\n", mbspath);
	    free(mbspath);

	    if (!set_variable(VAR_OLDPWD, xwcsdup(oldpwd), false, false))
		err = true;
	    if (!set_variable(VAR_PWD, path, false, false))
		err = true;
	}
    }

    return err ? EXIT_FAILURE1 : EXIT_SUCCESS;
}

const char cd_help[] = Ngt(
"cd - change directory\n"
"\tcd [-L|-P] [dir]\n"
"Changes the working directory to <dir>.\n"
"If <dir> is not specified, it defaults to $HOME.\n"
"If <dir> is \"-\", the working directory is changed to $OLDPWD.\n"
"If <dir> is a relative path which does not start with \".\" or \"..\",\n"
"paths in $CDPATH are searched to find a new directory.\n"
"If the working directory is successfully changed, $PWD is set to the new\n"
"directory and $OLDPWD is set to the previous $PWD.\n"
"When the -L (--logical) option is specified, symbolic links in $PWD are left\n"
"unchanged. When the -P (--physical) option is specified, symbolic links are\n"
"resolved so that $PWD does not contain any symbolic links.\n"
"-L and -P are mutually exclusive: the one specified last is used.\n"
"If neither is specified, -L is the default.\n"
);

/* "pwd" builtin, which accepts the following options:
 * -L: don't resolve symlinks (default)
 * -P: resolve symlinks
 * -L and -P are mutually exclusive: the one specified last is used. */
int pwd_builtin(int argc __attribute__((unused)), void **argv)
{
    bool logical = true;
    char *mbspwd;
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"LP", cd_pwd_options, NULL))) {
	switch (opt) {
	    case L'L':  logical = true;   break;
	    case L'P':  logical = false;  break;
	    case L'-':
		print_builtin_help(ARGV(0));
		return EXIT_SUCCESS;
	    default:
		fprintf(stderr, gt("Usage:  pwd [-L|-P]\n"));
		return EXIT_ERROR;
	}
    }

    if (logical) {
	const wchar_t *pwd = getvar(VAR_PWD);
	if (pwd != NULL && pwd[0] == L'/' && is_canonicalized(pwd)) {
	    mbspwd = malloc_wcstombs(pwd);
	    if (mbspwd != NULL) {
		if (is_same_file(mbspwd, ".")) {
		    printf("%s\n", mbspwd);
		    free(mbspwd);
		    return EXIT_SUCCESS;
		}
		free(mbspwd);
	    }
	}
    }

    mbspwd = xgetcwd();
    if (mbspwd == NULL) {
	xerror(errno, Ngt("cannot find out current directory"));
	return EXIT_FAILURE1;
    }
    printf("%s\n", mbspwd);
    if (posixly_correct) {
	wchar_t *pwd = malloc_mbstowcs(mbspwd);
	if (pwd != NULL)
	    set_variable(VAR_PWD, pwd, false, false);
    }
    free(mbspwd);
    return EXIT_SUCCESS;
}

const char pwd_help[] = Ngt(
"pwd - print working directory\n"
"\tpwd [-L|-P]\n"
"Prints the absolute pathname of the current working directory.\n"
"When the -L (--logical) option is specified, $PWD is printed if it is\n"
"correct. It may contain symbolic links in the pathname.\n"
"When the -P (--physical) option is specified, the printed pathname does not\n"
"contain any symbolic links.\n"
"-L and -P are mutually exclusive: the one specified last is used.\n"
"If neither is specified, -L is the default.\n"
"If the shell is in POSIXly correct mode and the -P option is specified,\n"
"$PWD is set to the printed pathname.\n"
);

/* The "umask" builtin, which accepts the following option:
 * -S: symbolic output */
int umask_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"symbolic", xno_argument, L'S', },
	{ L"help",     xno_argument, L'-', },
	{ NULL, 0, 0, },
    };

    wchar_t opt;
    bool symbolic = false;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"S", long_options, NULL))) {
	switch (opt) {
	    case L'S':
		symbolic = true;
		break;
	    case L'-':
		print_builtin_help(ARGV(0));
		return EXIT_SUCCESS;
	    default:
		goto print_usage;
	}
    }

    if (xoptind == argc) {
	mode_t mode = umask(0);
	umask(mode);
	if (symbolic)
	    print_umask_symbolic(mode);
	else
	    print_umask_octal(mode);
    } else if (xoptind + 1 == argc) {
	return set_umask(ARGV(xoptind));
    } else {
	goto print_usage;
    }
    return EXIT_SUCCESS;

print_usage:
    fprintf(stderr, gt("Usage:  umask [-S] [mask]\n"));
    return EXIT_ERROR;
}

void print_umask_octal(mode_t mode)
{
    printf("0%.3jo\n", (uintmax_t) mode);
}

void print_umask_symbolic(mode_t mode)
{
    putchar('u');
    putchar('=');
    if (!(mode & S_IRUSR)) putchar('r');
    if (!(mode & S_IWUSR)) putchar('w');
    if (!(mode & S_IXUSR)) putchar('x');
    putchar(',');
    putchar('g');
    putchar('=');
    if (!(mode & S_IRGRP)) putchar('r');
    if (!(mode & S_IWGRP)) putchar('w');
    if (!(mode & S_IXGRP)) putchar('x');
    putchar(',');
    putchar('o');
    putchar('=');
    if (!(mode & S_IROTH)) putchar('r');
    if (!(mode & S_IWOTH)) putchar('w');
    if (!(mode & S_IXOTH)) putchar('x');
    putchar('\n');
}

int set_umask(const wchar_t *maskstr)
{
    if (iswdigit(maskstr[0])) {
	/* parse as an octal number */
	uintmax_t mask;
	wchar_t *end;

	errno = 0;
	mask = wcstoumax(maskstr, &end, 8);
	if (errno || *end) {
	    xerror(0, Ngt("`%ls': invalid mask"), maskstr);
	    return EXIT_ERROR;
	}
	umask((mode_t) (mask & (S_IRWXU | S_IRWXG | S_IRWXO)));
	return EXIT_SUCCESS;
    }

    /* otherwise parse as a symbolic mode specification */
    mode_t origmask = ~umask(0);
    mode_t newmask = origmask;
    const wchar_t *savemaskstr = maskstr;

    do {
	mode_t who, perm;
	char op;  /* '+', '-' or '=' */

	/* parse 'who' */
	who = 0;
	for (;; maskstr++) switch (*maskstr) {
	    case L'u':  who |= S_IRWXU;  break;
	    case L'g':  who |= S_IRWXG;  break;
	    case L'o':  who |= S_IRWXO;  break;
	    case L'a':  who = S_IRWXU | S_IRWXG | S_IRWXO;  break;
	    default:    goto who_end;
	}
who_end:
	if (who == 0)
	    who = S_IRWXU | S_IRWXG | S_IRWXO;

	/* parse 'op' */
op_start:
	op = *maskstr++;
	switch (op) {
	    case L'+':  case L'-':  case L'=':
		break;
	    default:
		goto err;
	}

	/* parse 'perm' */
	switch (*maskstr) {
	    case L'u':  perm = copy_user_mask(origmask);  maskstr++;  break;
	    case L'g':  perm = copy_group_mask(origmask); maskstr++;  break;
	    case L'o':  perm = copy_other_mask(origmask); maskstr++;  break;
	    default:
		perm = 0;
		for (;; maskstr++) switch (*maskstr) {
		    case L'r':  perm |= S_IRUSR | S_IRGRP | S_IROTH;  break;
		    case L'w':  perm |= S_IWUSR | S_IWGRP | S_IWOTH;  break;
		    case L'X':
			if (!(origmask & (S_IXUSR | S_IXGRP | S_IXOTH)))
			    break;
			/* falls thru! */
		    case L'x':  perm |= S_IXUSR | S_IXGRP | S_IXOTH;  break;
		    case L's':  perm |= S_ISUID | S_ISGID;            break;
		    default:    goto perm_end;
		}
perm_end:
		break;
	}

	/* set newmask */
	switch (op) {
	    case L'+':  newmask |= who & perm;                      break;
	    case L'-':  newmask &= ~(who & perm);                   break;
	    case L'=':  newmask = (~who & newmask) | (who & perm);  break;
	    default:    assert(false);
	}

	switch (*maskstr) {
	    case L'\0':  goto parse_end;
	    case L',':   break;
	    default:     goto op_start;
	}
	maskstr++;
    } while (1);
parse_end:
    umask(~newmask);
    return EXIT_SUCCESS;

err:
    umask(~origmask);
    xerror(0, Ngt("`%ls': invalid mask"), savemaskstr);
    return EXIT_ERROR;
}

mode_t copy_user_mask(mode_t mode)
{
    return ((mode & S_IRUSR) ? (S_IRUSR | S_IRGRP | S_IROTH) : 0)
         | ((mode & S_IWUSR) ? (S_IWUSR | S_IWGRP | S_IWOTH) : 0)
	 | ((mode & S_IXUSR) ? (S_IXUSR | S_IXGRP | S_IXOTH) : 0);
}

mode_t copy_group_mask(mode_t mode)
{
    return ((mode & S_IRGRP) ? (S_IRUSR | S_IRGRP | S_IROTH) : 0)
         | ((mode & S_IWGRP) ? (S_IWUSR | S_IWGRP | S_IWOTH) : 0)
	 | ((mode & S_IXGRP) ? (S_IXUSR | S_IXGRP | S_IXOTH) : 0);
}

mode_t copy_other_mask(mode_t mode)
{
    return ((mode & S_IROTH) ? (S_IRUSR | S_IRGRP | S_IROTH) : 0)
         | ((mode & S_IWOTH) ? (S_IWUSR | S_IWGRP | S_IWOTH) : 0)
	 | ((mode & S_IXOTH) ? (S_IXUSR | S_IXGRP | S_IXOTH) : 0);
}

const char umask_help[] = Ngt(
"umask - print or set file creation mask\n"
"\tumask mode\n"
"\tumask [-S]\n"
"Sets the file mode creation mask of the shell to <mask>. The mask will be\n"
"inherited by succeedingly invoked commands.\n"
"If <mode> is not specified, the current setting of the mask is printed.\n"
"The -S (--symbolic) option makes a symbolic output.\n"
);


/* vim: set ts=8 sts=4 sw=4 noet: */
