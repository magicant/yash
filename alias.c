/* Yash: yet another shell */
/* alias.c: alias substitution */
/* (C) 2007-2017 magicant */

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
#include "alias.h"
#include <assert.h>
#include <errno.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "builtin.h"
#include "exec.h"
#include "expand.h"
#include "hashtable.h"
#include "option.h"
#include "parser.h"
#include "plist.h"
#include "refcount.h"
#include "strbuf.h"
#include "util.h"
#if YASH_ENABLE_LINEEDIT
# include "xfnmatch.h"
# include "lineedit/complete.h"
#endif


typedef struct alias_T {
    bool isglobal;
    refcount_T refcount;
    size_t valuelen;     /* length of `value' */
    wchar_t value[];
} alias_T;

typedef struct aliaslist_T {
    struct aliaslist_T *next;
    alias_T *alias;
    size_t limitindex;
} aliaslist_T;
/* An alias list created of the `aliaslist_T' structure is used to prevent
 * infinite recursive substitution of an alias. When an alias is substituted,
 * the alias and the end index of the substituted string are saved in the list.
 * Substitution of the same alias is not performed before the saved index,
 * thus preventing recursive substitution.
 * `aliaslist_T' is also used to indicate which command words are subject to
 * substitution after another substitution that ends with a blank. */

static void free_alias(alias_T *alias);
static inline void vfreealias(kvpair_T kv);
static void define_alias(
	const wchar_t *nameandvalue, const wchar_t *equal, bool global)
    __attribute__((nonnull));
static bool remove_alias(const wchar_t *name)
    __attribute__((nonnull));
static void remove_all_aliases(void);
static bool contained_in_list(const aliaslist_T *list, const alias_T *alias)
    __attribute__((pure));
static void add_to_aliaslist(
	aliaslist_T **list, alias_T *alias, size_t limitindex)
    __attribute__((nonnull));
static size_t remove_expired_aliases(aliaslist_T **list, size_t index)
    __attribute__((nonnull));
static void shift_index(aliaslist_T *list, ptrdiff_t inc);
static bool is_after_blank(size_t i, size_t j, const xwcsbuf_T *buf)
    __attribute__((nonnull));
static bool is_redir_fd(const wchar_t *s)
    __attribute__((nonnull,pure));
static bool print_alias(const wchar_t *name, const alias_T *alias, bool prefix);


/* Hashtable mapping alias names (wide strings) to alias_T's. */
hashtable_T aliases;


/* Initializes the alias module. */
void init_alias(void)
{
    assert(aliases.capacity == 0);
    ht_init(&aliases, hashwcs, htwcscmp);
}

/* Returns true iff `c' is a character that can be used in an alias name. */
inline bool is_alias_name_char(wchar_t c)
{
    return !wcschr(L" \t\n=$<>\\'\"`;&|()#", c) && !iswblank(c);
}

/* Decreases the reference count of `alias' and, if the count becomes zero,
 * frees it. This function does nothing if `alias' is a null pointer. */
void free_alias(alias_T *alias)
{
    if (alias != NULL)
	if (refcount_decrement(&alias->refcount))
	    free(alias);
}

/* Applies `free_alias' to the value of key-value pair `kv'. */
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
    alias_T *alias = xmallocs(sizeof *alias,
	    add(add(namelen, valuelen), 2), sizeof *alias->value);

    alias->isglobal = global;
    alias->refcount = 1;
    alias->valuelen = valuelen;
    wmemcpy(alias->value, equal + 1, valuelen);
    alias->value[valuelen] = L'\0';
    wmemcpy(alias->value + valuelen + 1, nameandvalue, namelen);
    alias->value[namelen + valuelen + 1] = L'\0';

    vfreealias(ht_set(&aliases, alias->value + valuelen + 1, alias));
}

/* Removes the alias definition with the specified name if any.
 * Returns true if the alias definition is successfully removed.
 * Returns false if no alias definition is found to be removed. */
bool remove_alias(const wchar_t *name)
{
    alias_T *alias = ht_remove(&aliases, name).value;

    if (alias != NULL) {
	free_alias(alias);
	return true;
    } else {
	return false;
    }
}

/* Removes all alias definitions. */
void remove_all_aliases(void)
{
    ht_clear(&aliases, vfreealias);
}

/* Returns the value of the specified alias (or null if there is no such). */
const wchar_t *get_alias_value(const wchar_t *aliasname)
{
    const alias_T *alias = ht_get(&aliases, aliasname).value;

    if (alias != NULL)
	return alias->value;
    else
	return NULL;
}

/* Frees the specified alias list and its contents. */
void destroy_aliaslist(aliaslist_T *list)
{
    remove_expired_aliases(&list, SIZE_MAX);
    assert(list == NULL);
}

/* Checks if the specified alias list contains the specified alias. */
bool contained_in_list(const aliaslist_T *list, const alias_T *alias)
{
    while (list != NULL) {
	if (list->alias == alias)
	    return true;
	list = list->next;
    }
    return false;
}

/* Adds an alias to an alias list with the specified limit index. */
void add_to_aliaslist(aliaslist_T **list, alias_T *alias, size_t limitindex)
{
    aliaslist_T *oldhead = *list;
    aliaslist_T *newelem = xmalloc(sizeof *newelem);

    assert(oldhead == NULL || limitindex <= oldhead->limitindex);
    newelem->next = oldhead;
    newelem->alias = alias;
    refcount_increment(&newelem->alias->refcount);
    newelem->limitindex = limitindex;
    *list = newelem;
}

/* Removes items whose `limitindex' is less than or equal to `index'.
 * Returns `limitindex' of the last removed non-global alias if any were
 * removed. Returns 0 if no such aliases were removed. */
size_t remove_expired_aliases(aliaslist_T **list, size_t index)
{
    size_t lastlimitindex = 0;
    aliaslist_T *item = *list;

    /* List items are ordered by index; we don't have to check all the items. */
    while (item != NULL && item->limitindex <= index) {
	assert(lastlimitindex <= item->limitindex);
	if (!item->alias->isglobal)
	    lastlimitindex = item->limitindex;

	aliaslist_T *next = item->next;
	free_alias(item->alias);
	free(item);
	item = next;
    }
    *list = item;

    return lastlimitindex;
}

/* Increases the index of each item in the specified list by `inc'. */
void shift_index(aliaslist_T *list, ptrdiff_t inc)
{
    while (list != NULL) {
	list->limitindex += inc;
	list = list->next;
    }
}

/* Tests if the character just before `i` in `buf' is a blank and all the
 * characters between `i' and `j' are blanks. */
bool is_after_blank(size_t i, size_t j, const xwcsbuf_T *buf)
{
    assert(i <= j);
    assert(j <= buf->length);

    if (i == 0)
	return false;
    for (i--; i < j; i++)
	if (!iswblank(buf->contents[i]))
	    return false;
    return true;
}

/* Performs alias substitution at index `i' in buffer `buf'.
 * If AF_NONGLOBAL is not in `flags' and `i' is not after another substitution
 * that ends with a blank, only global aliases are substituted.
 * If AF_NORECUR is not in `flags', substitution is repeated until there is
 * no more alias applicable.
 * Returns true iff any alias was substituted. */
bool substitute_alias(xwcsbuf_T *restrict buf, size_t i,
	aliaslist_T **restrict list, substaliasflags_T flags)
{
    if (aliases.count == 0)
	return false;

    size_t lastlimitindex = remove_expired_aliases(list, i);
    if (is_after_blank(lastlimitindex, i, buf))
	flags |= AF_NONGLOBAL;

    if (!(flags & AF_NONGLOBAL) && posixly_correct)
	return false;

    bool subst = false;

substitute_alias:;

    /* count the length of the alias name */
    size_t j = i;
    while (is_alias_name_char(buf->contents[j]))
	j++;
    /* `i' is the starting index of the alias name and `j' is the ending index*/

    /* check if there is an alias name */
    if (i < j && is_token_delimiter_char(buf->contents[j])
	    && !is_redir_fd(buf->contents + i)) {

	alias_T *alias;
	wchar_t savechar;

	/* get alias definition */
	savechar = buf->contents[j];
	buf->contents[j] = L'\0';
	alias = ht_get(&aliases, buf->contents + i).value;
	buf->contents[j] = savechar;

	/* check if we should do substitution */
	if (alias != NULL
		&& ((flags & AF_NONGLOBAL) || alias->isglobal)
		&& !contained_in_list(*list, alias)) {

	    /* do substitution */
	    wb_replace_force(buf, i, j - i, alias->value, alias->valuelen);
	    shift_index(*list,
		    (ptrdiff_t) alias->valuelen - (ptrdiff_t) (j - i));
	    subst = true;

	    /* add the alias to the list to track recursion */
	    add_to_aliaslist(list, alias, i + alias->valuelen);

	    /* recursively substitute alias */
	    if (!(flags & AF_NORECUR)) {
		while (iswblank(buf->contents[i]))
		    i++;
		goto substitute_alias;
	    }
	}
    }
    return subst;
}

/* Returns true iff the specified string starts with any number of digits
 * followed by L'<' or L'>'. */
/* An IO_NUMBER token, which specifies the file descriptor a redirection
 * affects, must not be alias-substituted. This function check if the word is
 * such a token. */
bool is_redir_fd(const wchar_t *s)
{
    while (iswdigit(*s))
	s++;
    return *s == L'<' || *s == L'>';
}

/* Prints an alias definition to the standard output.
 * On error, an error message is printed to the standard error.
 * Returns true iff successful. */
bool print_alias(const wchar_t *name, const alias_T *alias, bool prefix)
{
    wchar_t *qvalue = quote_sq(alias->value);
    const char *format;
    bool success;

    if (!prefix)
	format = "%ls=%ls\n";
    else if (alias->isglobal)
	if (name[0] == L'-')
	    format = "alias -g -- %ls=%ls\n";
	else
	    format = "alias -g %ls=%ls\n";
    else
	if (name[0] == L'-')
	    format = "alias -- %ls=%ls\n";
	else
	    format = "alias %ls=%ls\n";

    success = xprintf(format, name, qvalue);
    free(qvalue);
    return success;
}

/* Prints an alias definition to the standard output if defined.
 * This function is used in the "command" built-in.
 * Returns true iff a non-global alias had been defined for the specified name.
 */
bool print_alias_if_defined(const wchar_t *aliasname, bool user_friendly)
{
    const alias_T *alias = ht_get(&aliases, aliasname).value;

    if (alias == NULL || alias->isglobal)
	return false;

    if (!user_friendly)
	print_alias(aliasname, alias, true);
    else
	xprintf(gt("%ls: an alias for `%ls'\n"), aliasname, alias->value);
    return true;
}

#if YASH_ENABLE_LINEEDIT

/* Generates candidates to complete an alias matching the pattern. */
/* The prototype of this function is declared in "lineedit/complete.h". */
void generate_alias_candidates(const le_compopt_T *compopt)
{
    if (!(compopt->type & CGT_ALIAS))
	return;

    le_compdebug("adding alias name candidates");
    if (!le_compile_cpatterns(compopt))
	return;

    size_t i = 0;
    kvpair_T kv;

    while ((kv = ht_next(&aliases, &i)).key != NULL) {
	const alias_T *alias = kv.value;
	le_candgentype_T type = alias->isglobal ? CGT_GALIAS : CGT_NALIAS;

	if (compopt->type & type)
	    if (le_wmatch_comppatterns(compopt, kv.key))
		le_new_candidate(CT_ALIAS, xwcsdup(kv.key), NULL, compopt);
    }
}

#endif /* YASH_ENABLE_LINEEDIT */


/********** Built-ins **********/

/* Options for the "alias" built-in. */
const struct xgetopt_T alias_options[] = {
    { L'g', L"global", OPTARG_NONE, false, NULL, },
    { L'p', L"prefix", OPTARG_NONE, false, NULL, },
#if YASH_ENABLE_HELP
    { L'-', L"help",   OPTARG_NONE, false, NULL, },
#endif
    { L'\0', NULL, 0, false, NULL, },
};

/* The "alias" built-in, which accepts the following options:
 *  -g: define global aliases
 *  -p: print aliases in the form of whole commands */
int alias_builtin(int argc, void **argv)
{
    bool global = false, prefix = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, alias_options, 0)) != NULL) {
	switch (opt->shortopt) {
	    case L'g':  global = true;  break;
	    case L'p':  prefix = true;  break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return Exit_ERROR;
	}
    }

    if (xoptind == argc) {
	/* print all aliases */
	kvpair_T *kvs = ht_tokvarray(&aliases);
	qsort(kvs, aliases.count, sizeof *kvs, keywcscoll);
	for (size_t i = 0; i < aliases.count; i++) {
	    print_alias(kvs[i].key, kvs[i].value, prefix);
	    if (yash_error_message_count > 0)
		break;
	}
	free(kvs);
    } else {
	/* define or print aliases */
	do {
	    wchar_t *arg = ARGV(xoptind);
	    wchar_t *nameend = arg;

	    while (is_alias_name_char(*nameend))
		nameend++;

	    if (nameend != arg && *nameend == L'=') {
		/* define alias */
		define_alias(arg, nameend, global);
	    } else if (nameend != arg && *nameend == L'\0') {
		/* print alias */
		const alias_T *alias = ht_get(&aliases, arg).value;
		if (alias != NULL) {
		    if (!print_alias(arg, alias, prefix))
			break;
		} else {
		    xerror(0, Ngt("no such alias `%ls'"), arg);
		}
	    } else {
		xerror(0, Ngt("`%ls' is not a valid alias name"), arg);
	    }
	} while (++xoptind < argc);
    }
    return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;
}

#if YASH_ENABLE_HELP
const char alias_help[] = Ngt(
"define or print aliases"
);
const char alias_syntax[] = Ngt(
"\talias [-gp] [name[=value]...]\n"
);
#endif

/* The "unalias" built-in, which accepts the following option:
 *  -a: remove all aliases */
int unalias_builtin(int argc, void **argv)
{
    bool all = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, all_help_options, 0)) != NULL) {
	switch (opt->shortopt) {
	    case L'a':  all = true;  break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return Exit_ERROR;
	}
    }

    if (all) {
	if (xoptind != argc)
	    return too_many_operands_error(0);

	remove_all_aliases();
    } else {
	if (xoptind == argc)
	    return insufficient_operands_error(1);

	do {
	    const wchar_t *arg = ARGV(xoptind);
	    if (!remove_alias(arg))
		xerror(0, Ngt("no such alias `%ls'"), arg);
	} while (++xoptind < argc);
    }
    return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;
}

#if YASH_ENABLE_HELP
const char unalias_help[] = Ngt(
"undefine aliases"
);
const char unalias_syntax[] = Ngt(
"\tunalias name...\n"
"\tunalias -a\n"
);
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
