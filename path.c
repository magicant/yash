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
#include <pwd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <sys/stat.h>
#include "option.h"
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "hashtable.h"
#include "wfnmatch.h"
#include "path.h"
#include "variable.h"


/* Checks if `path' is a readable regular file. */
bool is_readable(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) && S_ISREG(st.st_mode)
	&& access(path, R_OK) == 0;
}

/* Checks if `path' is a executable regular file. */
bool is_executable(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) && S_ISREG(st.st_mode)
	&& access(path, X_OK) == 0;
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
    bool result = wcscmp(path, canon);
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

/* A hashtable from command names to their full path.
 * Keys are pointers to a multibyte string containing a command name and
 * values are pointers to a multibyte string containing the commands' full path.
 * For each entry, the key string is part of the value, that is, the last
 * pathname component of it. */
static hashtable_T cmdhash;

/* Initializes the command hashtable. */
void init_cmdhash(void)
{
    static bool initialized = false;
    if (!initialized) {
	initialized = true;
	ht_init(&cmdhash, hashstr, htstrcmp);
    }
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
	xstrbuf_T *restrict const dirname,
	plist_T *restrict dirstack, plist_T *restrict list)
    __attribute__((nonnull));
static bool wglob_start_recursive_search(const wchar_t *restrict pattern,
	enum wglbflags flags, enum wglbrflags rflags,
	xstrbuf_T *restrict const dirname,
	plist_T *restrict dirstack, plist_T *restrict list)
    __attribute__((nonnull));
static bool wglob_recursive_search(const wchar_t *restrict pattern,
	enum wglbflags flags, enum wglbrflags rflags,
	xstrbuf_T *restrict const dirname,
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
 * list:    a list of pointers to multibyte strings to which resulting items are
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
    plist_T dirstack;

    if (!pattern[0])
	return true;

    sb_init(&dir);
    if (flags & WGLB_RECDIR)
	pl_init(&dirstack);
    while (pattern[0] == L'/') {
	sb_ccat(&dir, '/');
	pattern++;
    }

    bool succ = wglob_search(pattern, flags, &dir, &dirstack, list);
    sb_destroy(&dir);
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
		if (strcmp(list->contents[i], list->contents[i-1]) == 0) {
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
    return strcoll(*(const char *const *) v1, *(const char *const *) v2);
}

/* Searches the specified directory and add filenames that match the pattern.
 * `dirname' is the name of directory to search, ending with '/' or empty.
 * For the meaning of the other arguments, see `wglob'.
 * `pattern' must not start with L'/'.
 * `dirname' must end with '/' except when empty. An empty `dirname' specifies
 * the current directory. Though the contents of `dirname' may be changed
 * during this function, the contents are restored when the function returns. */
bool wglob_search(
	const wchar_t *restrict pattern,
	enum wglbflags flags,
	xstrbuf_T *restrict const dirname,
	plist_T *restrict dirstack,
	plist_T *restrict list)
{
    const size_t savedirlen = dirname->length;
#define RESTORE_DIRNAME \
    ((void) (dirname->contents[dirname->length = savedirlen] = '\0'))

    assert(pattern[0] != L'/');
    if (!pattern[0]) {
	/* If the pattern is empty, add `dirname' itself
	 * except when `dirname' is also empty.
	 * Note that `is_directory' returns false for an empty string. */
	if (is_directory(dirname->contents))
	    pl_add(list, xstrdup(dirname->contents));
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
		    p, flags, rflags, dirname, dirstack, list);
	} else if (wcsncmp(p, L"***/", 4) == 0) {
	    rflags |= WGLB_followlink;
	    p += 4;
	    while (p[0] == L'/') p++;
	    return wglob_start_recursive_search(
		    p, flags, rflags, dirname, dirstack, list);
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
	free(wentname);
	if (match == WFNM_ERROR) {
	    ok = false;
	} else if (match != WFNM_NOMATCH) {
	    /* matched! */
	    if (isleaf) {
		/* add the matched pathname to the list */
		sb_cat(dirname, de->d_name);
		if ((flags & WGLB_MARK) && is_directory(dirname->contents))
		    sb_ccat(dirname, '/');
		pl_add(list, xstrdup(dirname->contents));
		RESTORE_DIRNAME;
	    } else {
		/* search the subdirectories */
		assert(pattern[patlen] == L'/');
		sb_cat(dirname, de->d_name);
		sb_ccat(dirname, '/');
		const wchar_t *subpat = pattern + patlen + 1;
		while (subpat[0] == L'/') {
		    sb_ccat(dirname, '/');
		    subpat++;
		}
		ok = wglob_search(
			subpat, flags, dirname, dirstack, list);
		RESTORE_DIRNAME;
	    }
	}
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
		pattern, flags, rflags, dirname, dirstack, list);
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
	plist_T *restrict dirstack,
	plist_T *restrict list)
{
    const size_t savedirlen = dirname->length;

    /* Step 1: search `dirname' itself */
    if (!wglob_search(pattern, flags, dirname, dirstack, list))
	return false;

    assert(dirname->length == savedirlen);

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
	    sb_ccat(dirname, '/');
	    pl_add(dirstack, &st);
	    ok = wglob_recursive_search(pattern, flags, rflags,
		    dirname, dirstack, list);
	    pl_pop(dirstack);
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


/* vim: set ts=8 sts=4 sw=4 noet: */
