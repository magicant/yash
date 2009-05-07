/* Yash: yet another shell */
/* alias.c: alias substitution */
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
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "alias.h"
#include "builtin.h"
#include "exec.h"
#include "expand.h"
#include "hashtable.h"
#include "option.h"
#include "parser.h"
#include "plist.h"
#include "strbuf.h"
#include "util.h"


typedef enum {
    AF_BLANKEND  = 1 << 0,  /* alias value ends with a blank */
    AF_GLOBAL    = 1 << 1,  /* is a global alias */
} aliasflags_T;

typedef struct alias_T {
    aliasflags_T flags;
    unsigned refcount;
    size_t valuelen;
    wchar_t value[];
} alias_T;

#define ALIAS_LIST_MAX 30
typedef struct aliaslist_T {
    size_t count;
    struct {
	size_t limitindex;
	alias_T *alias;
    } list[ALIAS_LIST_MAX];
} aliaslist_T;

static inline bool is_alias_name_char(wchar_t c)
    __attribute__((pure));
static void free_alias(alias_T *alias);
static inline void vfreealias(kvpair_T kv);
static void define_alias(
	const wchar_t *nameandvalue, const wchar_t *equal, bool global)
    __attribute__((nonnull));
static bool remove_alias(const wchar_t *name)
    __attribute__((nonnull));
static void remove_all_aliases(void);
static aliaslist_T *clone_aliaslist(const aliaslist_T *list)
    __attribute__((nonnull));
static bool contained_in_list(const aliaslist_T *list, const alias_T *alias)
    __attribute__((nonnull(1)));
static bool add_to_aliaslist(
	aliaslist_T *list, alias_T *alias, size_t limitindex)
    __attribute__((nonnull));
static void remove_expired_aliases(aliaslist_T *list, size_t index)
    __attribute__((nonnull));
static void shift_index(aliaslist_T *list, ptrdiff_t inc)
    __attribute__((nonnull));
static void print_alias(const wchar_t *name, const alias_T *alias, bool prefix);


/* If true, alias substitution is performed. */
bool alias_enabled = true;

/* Hashtable mapping alias names (wide strings) to alias_T's. */
hashtable_T aliases;


/* Initializes the alias module. */
void init_alias(void)
{
    assert(aliases.capacity == 0);
    ht_init(&aliases, hashwcs, htwcscmp);
}

/* Returns true iff `c' is a character that can be used in an alias name. */
bool is_alias_name_char(wchar_t c)
{
    return !iswblank(c) && !wcschr(L"\n=$<>\\'\"`;&|()#", c);
}

/* Skips an alias name at the head of the specified string and returns a
 * pointer to the character right after the name in the string. */
wchar_t *skip_alias_name(const wchar_t *s)
{
    while (is_alias_name_char(*s))
	s++;
    return (wchar_t *) s;
}

/* Decreases the reference count of `alias' and, if the count becomes zero,
 * frees it. */
void free_alias(alias_T *alias)
{
    if (alias != NULL && --alias->refcount == 0)
	free(alias);
}

/* Applies `free_alias' to the value of the pair. */
void vfreealias(kvpair_T kv)
{
    free_alias(kv.value);
}

/* Defines an alias.
 * `nameandvalue' must be a wide string of the form "name=value" and `equal'
 * must point to the first L'=' in `nameandvalue'.
 * This function doesn't check if the name and the value are valid. */
void define_alias(
	const wchar_t *nameandvalue, const wchar_t *equal, bool global)
{
    assert(wcschr(nameandvalue, L'=') == equal);

    size_t namelen = equal - nameandvalue;
    size_t valuelen = wcslen(equal + 1);
    alias_T *alias = xmalloc(
	    sizeof *alias + sizeof *alias->value * (namelen + valuelen + 2));

    alias->flags = 0;
    alias->refcount = 1;
    alias->valuelen = valuelen;
    if (global)
	alias->flags |= AF_GLOBAL;
    if (iswblank(equal[valuelen]))
	alias->flags |= AF_BLANKEND;
    wmemcpy(alias->value, equal + 1, valuelen);
    alias->value[valuelen] = L'\0';
    wmemcpy(alias->value + valuelen + 1, nameandvalue, namelen);
    alias->value[namelen + valuelen + 1] = L'\0';

    vfreealias(ht_set(&aliases, alias->value + valuelen + 1, alias));
}

/* Removes an alias definition with the specified name if any.
 * Returns true if the alias definition is successfully removed.
 * Returns false if no alias definition is found to be removed. */
bool remove_alias(const wchar_t *name)
{
    alias_T *alias = ht_remove(&aliases, name).value;
    free_alias(alias);
    return alias != NULL;
}

/* Removes all alias definitions. */
void remove_all_aliases(void)
{
    ht_clear(&aliases, vfreealias);
}

/* Returns the value of the specified alias (or null if there is no such). */
const wchar_t *get_alias_value(const wchar_t *aliasname)
{
    alias_T *alias = ht_get(&aliases, aliasname).value;

    if (alias)
	return alias->value;
    else
	return NULL;
}

/* Returns a new `aliaslist_T' object. */
aliaslist_T *new_aliaslist(void)
{
    aliaslist_T *list = xmalloc(sizeof *list);
    list->count = 0;
    return list;
}

/* Creates a copy of the alias list. */
aliaslist_T *clone_aliaslist(const aliaslist_T *list)
{
    aliaslist_T *new = xmalloc(sizeof *new);
    *new = *list;
    for (size_t i = list->count; i > 0; ) {
	i--;
	list->list[i].alias->refcount++;
    }
    return new;
}

/* Frees the alias list and its contents. */
void destroy_aliaslist(struct aliaslist_T *list)
{
    for (size_t i = list->count; i > 0; ) {
	i--;
	free_alias(list->list[i].alias);
    }
    free(list);
}


/* Checks if the alias list contains `alias' */
bool contained_in_list(const aliaslist_T *list, const alias_T *alias)
{
    for (size_t i = list->count; i > 0; ) {
	i--;
	if (list->list[i].alias == alias)
	    return true;
    }
    return false;
}

/* Adds `alias' to `list'.
 * Returns true iff successful. */
bool add_to_aliaslist(aliaslist_T *list, alias_T *alias, size_t limitindex)
{
    if (list->count < ALIAS_LIST_MAX) {
	list->list[list->count].limitindex = limitindex;
	list->list[list->count].alias = alias;
	list->count++;
	alias->refcount++;
	return true;
    } else {
	return false;
    }
}

void remove_expired_aliases(aliaslist_T *list, size_t index)
{
    for (size_t i = list->count; i > 0; ) {
	i--;
	if (list->list[i].limitindex <= index) {
	    free_alias(list->list[i].alias);
	    list->count = i;
	} else {
	    break;
	}
    }
}

/* Increases index of each entry of the list by `inc'. */
void shift_index(aliaslist_T *list, ptrdiff_t inc)
{
    for (size_t i = list->count; i > 0; ) {
	i--;
	list->list[i].limitindex += inc;
    }
}

/* Performs alias substitution at the specified index `i' in the buffer `buf'.
 * If `globalonly' is true, only global aliases are substituted. */
void substitute_alias(
	xwcsbuf_T *buf, size_t i, aliaslist_T *list, bool globalonly)
{
substitute_alias:
    if (!alias_enabled || (globalonly && posixly_correct))
	return;
    remove_expired_aliases(list, i);

    size_t j = i;
    while (is_alias_name_char(buf->contents[j]))
	j++;
    /* `i' is the starting index and `j' is the ending index of the word */

    if (i < j && is_token_delimiter_char(buf->contents[j])) {
	wchar_t savechar = buf->contents[j];
	buf->contents[j] = L'\0';
	alias_T *alias = ht_get(&aliases, buf->contents + i).value;
	buf->contents[j] = savechar;

	if (alias != NULL
		&& !contained_in_list(list, alias)
		&& (!globalonly || (alias->flags & AF_GLOBAL))) {
	    /* do substitution */
	    wb_replace_force(buf, i, j - i, alias->value, alias->valuelen);
	    shift_index(list, (ptrdiff_t) alias->valuelen - (ptrdiff_t)(j - i));

	    /* see note below */
	    if ((alias->flags & AF_BLANKEND) && !(alias->flags & AF_GLOBAL)) {
		size_t ii = i + alias->valuelen;
		aliaslist_T *savelist = clone_aliaslist(list);
		while (iswblank(buf->contents[ii]))
		    ii++;
		substitute_alias(buf, ii, savelist, globalonly);
		destroy_aliaslist(savelist);
	    }

	    if (add_to_aliaslist(list, alias, i + alias->valuelen)) {
		while (iswblank(buf->contents[i]))
		    i++;
		goto substitute_alias;  /* recursively substitute alias */
	    }
	}
    }
}
/* When the value of the alias ends with a blank, we substitute the following
 * word if and only if the alias is not global. This is required to prevent
 * infinite substitution that would happen in some cases such as:
 *    alias -g a='a a '
 *    echo a
 */

/* Prints an alias definition to stdout. */
void print_alias(const wchar_t *name, const alias_T *alias, bool prefix)
{
    wchar_t *qvalue = quote_sq(alias->value);
    const char *format;
    if (!prefix)
	format = "%ls=%ls\n";
    else if (alias->flags & AF_GLOBAL)
	if (name[0] == L'-')
	    format = "alias -g -- %ls=%ls\n";
	else
	    format = "alias -g %ls=%ls\n";
    else
	if (name[0] == L'-')
	    format = "alias -- %ls=%ls\n";
	else
	    format = "alias %ls=%ls\n";
    printf(format, name, qvalue);
    free(qvalue);
}

/* Prints an alias definition to stdout if defined.
 * This function is used in the "command" builtin.
 * Returns true iff the non-global alias is defined and printed. */
bool print_alias_if_defined(const wchar_t *aliasname, bool user_friendly)
{
    alias_T *alias = ht_get(&aliases, aliasname).value;
    if (alias && !(alias->flags & AF_GLOBAL)) {
	if (!user_friendly)
	    print_alias(aliasname, alias, true);
	else
	    printf(gt("%ls: alias for `%ls'\n"), aliasname, alias->value);
	return true;
    } else {
	return false;
    }
}


/********** Builtins **********/

/* The "alias" builtin, which accepts the following options:
 * -g: define global aliases
 * -p: print aliases in the form of whole commands */
int alias_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"global", xno_argument, L'g', },
	{ L"prefix", xno_argument, L'p', },
	{ L"help",   xno_argument, L'-', },
	{ NULL, 0, 0, },
    };

    bool global = false, prefix = false;
    bool err = false;
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv,
		    posixly_correct ? L"" : L"gp",
		    long_options, NULL))) {
	switch (opt) {
	    case L'g':  global = true;  break;
	    case L'p':  prefix = true;  break;
	    case L'-':
		print_builtin_help(ARGV(0));
		return Exit_SUCCESS;
	    default:
		fprintf(stderr, gt(posixly_correct
			    ? Ngt("Usage:  alias [name[=value]...]\n")
			    : Ngt("Usage:  alias [-gp] [name[=value]...]\n")));
		return Exit_ERROR;
	}
    }

    if (xoptind == argc) {
	/* print all aliases */
	kvpair_T *kvs = ht_tokvarray(&aliases);
	qsort(kvs, aliases.count, sizeof *kvs, keywcscoll);
	for (size_t i = 0; i < aliases.count; i++)
	    print_alias(kvs[i].key, kvs[i].value, prefix);
	free(kvs);
	return Exit_SUCCESS;
    }
    do {
	wchar_t *arg = ARGV(xoptind);
	wchar_t *nameend = arg;

	while (is_alias_name_char(*nameend))
	    nameend++;
	if (*nameend == L'=') {
	    if (!wcschr(nameend + 1, L'\n')) {
		define_alias(arg, nameend, global);
	    } else {
		xerror(0, Ngt("`%ls': alias cannot contain newlines"), arg);
		err = true;
	    }
	} else if (*nameend == L'\0') {
	    alias_T *alias = ht_get(&aliases, arg).value;
	    if (alias) {
		print_alias(arg, alias, prefix);
	    } else {
		xerror(0, Ngt("%ls: no such alias"), arg);
		err = true;
	    }
	} else {
	    xerror(0, Ngt("`%ls': invalid alias name"), arg);
	    err = true;
	}
    } while (++xoptind < argc);
    return err ? Exit_FAILURE : Exit_SUCCESS;
}

const char alias_help[] = Ngt(
"alias - define or print aliases\n"
"\talias [-gp] [name[=value]...]\n"
"Defines and/or prints aliases.\n"
"For each operands of the form <name=value>, an alias is (re)defined.\n"
"If the -g (--global) option is specified, it is defined as a global alias,\n"
"which is substituted even if it is not a command word.\n"
"For each operands of the form <name>, the alias definition is printed.\n"
"If the -p (--prefix) option is specified, the output format is suitable for\n"
"re-input to the shell.\n"
"If no operands are given, all alias definitions are printed.\n"
"No options are available in POSIXly correct mode.\n"
);

/* The "unalias" builtin, which accepts the following option:
 * -a: remove all aliases */
int unalias_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"all",  xno_argument, L'a', },
	{ L"help", xno_argument, L'-', },
	{ NULL, 0, 0, },
    };

    bool all = false;
    bool err = false;
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"a", long_options, NULL))) {
	switch (opt) {
	    case L'a':  all = true;  break;
	    case L'-':
		print_builtin_help(ARGV(0));
		return Exit_SUCCESS;
	    default:
		goto print_usage;
	}
    }

    if (all) {
	remove_all_aliases();
    } else {
	if (xoptind == argc)
	    goto print_usage;
	do {
	    const wchar_t *arg = ARGV(xoptind);
	    if (!remove_alias(arg)) {
		xerror(0, Ngt("%ls: no such alias"), arg);
		err = true;
	    }
	} while (++xoptind < argc);
    }
    return err ? Exit_FAILURE : Exit_SUCCESS;

print_usage:
    fprintf(stderr, gt("Usage:  unalias name[...]\n"
                       "        unalias -a\n"));
    return Exit_ERROR;
}

const char unalias_help[] = Ngt(
"unalias - undefine aliases\n"
"\tunalias name...\n"
"\tunalias -a\n"
"Removes the specified alias definitions.\n"
"If the -a option is specified, all definitions are removed.\n"
);


/* vim: set ts=8 sts=4 sw=4 noet: */
