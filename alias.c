/* Yash: yet another shell */
/* alias.c: alias substitution */
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
#include <errno.h>
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
#if YASH_ENABLE_LINEEDIT
# include "xfnmatch.h"
# include "lineedit/complete.h"
#endif


typedef enum {
    AF_BLANKEND  = 1 << 0,  /* alias value ends with a blank */
    AF_GLOBAL    = 1 << 1,  /* is a global alias */
} aliasflags_T;

typedef struct alias_T {
    aliasflags_T flags;
    unsigned refcount;   /* reference count */
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
 * thus preventing recursive substitution. */

static void free_alias(alias_T *alias);
static inline void vfreealias(kvpair_T kv);
static void define_alias(
	const wchar_t *nameandvalue, const wchar_t *equal, bool global)
    __attribute__((nonnull));
static bool remove_alias(const wchar_t *name)
    __attribute__((nonnull));
static void remove_all_aliases(void);
static aliaslist_T *clone_aliaslist(const aliaslist_T *list)
    __attribute__((malloc,warn_unused_result));
static bool contained_in_list(const aliaslist_T *list, const alias_T *alias)
    __attribute__((nonnull(1),pure));
static void add_to_aliaslist(
	aliaslist_T **list, alias_T *alias, size_t limitindex)
    __attribute__((nonnull));
static aliaslist_T *remove_expired_aliases(aliaslist_T *list, size_t index)
    __attribute__((warn_unused_result));
static void shift_index(aliaslist_T *list, ptrdiff_t inc)
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
    if (alias != NULL) {
	alias->refcount--;
	if (alias->refcount == 0)
	    free(alias);
    }
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
    alias_T *alias = xmalloc(
	    sizeof *alias + sizeof *alias->value * (namelen + valuelen + 2));

    alias->flags = 0;
    alias->refcount = 1;
    alias->valuelen = valuelen;
    if (global)
	alias->flags |= AF_GLOBAL;
    if (iswblank(equal[valuelen]))  // `(equal + 1)[valuelen - 1]'
	alias->flags |= AF_BLANKEND;
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

/* Creates a copy of the specified alias list. */
aliaslist_T *clone_aliaslist(const aliaslist_T *list)
{
    aliaslist_T  *result = NULL;
    aliaslist_T **lastp  = &result;

    while (list != NULL) {
	aliaslist_T *copy = xmalloc(sizeof *copy);

	copy->next = NULL;
	copy->alias = list->alias;
	copy->alias->refcount++;
	copy->limitindex = list->limitindex;

	*lastp = copy;
	lastp = &copy->next;
	list = list->next;
    }
    return result;
}

/* Frees the specified alias list and its contents. */
void destroy_aliaslist(aliaslist_T *list)
{
    list = remove_expired_aliases(list, SIZE_MAX);
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
    newelem->alias->refcount++;
    newelem->limitindex = limitindex;
    *list = newelem;
}

/* Removes items whose `limitindex' is less than or equal to `index'. */
aliaslist_T *remove_expired_aliases(aliaslist_T *list, size_t index)
{
    /* List items are ordered by index; we don't have to check all the items. */
    while (list != NULL && list->limitindex <= index) {
	aliaslist_T *next = list->next;
	free_alias(list->alias);
	free(list);
	list = next;
    }
    return list;
}

/* Increases the index of each item in the specified list by `inc'. */
void shift_index(aliaslist_T *list, ptrdiff_t inc)
{
    while (list != NULL) {
	list->limitindex += inc;
	list = list->next;
    }
}

/* Performs alias substitution at index `i' in buffer `buf'.
 * If the length of the substituted word is known, it should be given as `len'.
 * Otherwise, `len' must be 0.
 * If AF_NONGLOBAL is not in `flags', only global aliases are substituted.
 * If AF_NORECUR is not in `flags', substitution is repeated until there is
 * no more alias applicable.
 * Returns true iff any alias was substituted. */
bool substitute_alias(xwcsbuf_T *restrict buf, size_t i, size_t len,
	aliaslist_T **restrict list, substaliasflags_T flags)
{
    if (aliases.count == 0)
	return false;
    if (!(flags & AF_NONGLOBAL) && posixly_correct)
	return false;

    bool subst = false;

substitute_alias:
    *list = remove_expired_aliases(*list, i);

    /* count the length of the alias name */
    size_t j = i + len;
    if (len == 0) {
	while (is_alias_name_char(buf->contents[j]))
	    j++;
    } else {
#ifndef NDEBUG
	for (size_t k = i; k < j; k++)
	    assert(is_alias_name_char(buf->contents[k]));
	assert(!is_alias_name_char(buf->contents[j]));
#endif
    }
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
		&& ((flags & AF_NONGLOBAL) | (alias->flags & AF_GLOBAL))
		&& !contained_in_list(*list, alias)) {

	    /* do substitution */
	    wb_replace_force(buf, i, j - i, alias->value, alias->valuelen);
	    shift_index(*list,
		    (ptrdiff_t) alias->valuelen - (ptrdiff_t) (j - i));
	    subst = true;

	    /* substitute the following word if AF_BLANKEND is set */
	    /* see note below */
	    if ((alias->flags & AF_BLANKEND) && !(alias->flags & AF_GLOBAL)) {
		size_t ii = i + alias->valuelen;
		aliaslist_T *savelist = clone_aliaslist(*list);
		while (iswblank(buf->contents[ii]))
		    ii++;
		substitute_alias(buf, ii, 0, &savelist, flags);
		destroy_aliaslist(savelist);
	    }

	    /* add the alias to the list to track recursion */
	    add_to_aliaslist(list, alias, i + alias->valuelen);

	    /* recursively substitute alias */
	    if (!(flags & AF_NORECUR)) {
		while (iswblank(buf->contents[i]))
		    i++;
		len = 0;
		goto substitute_alias;
	    }
	}
    }
    return subst;
}
/* When the value of the alias ends with a blank, we substitute the following
 * word if and only if the alias is not global. This is required to prevent
 * infinite substitution that would happen in some cases such as:
 *    alias -g a='a a '
 *    echo a
 */

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

    success = xprintf(format, name, qvalue);
    free(qvalue);
    return success;
}

/* Prints an alias definition to the standard output if defined.
 * This function is used in the "command" built-in.
 * Returns true iff a non-global alias had been defined and was printed. */
bool print_alias_if_defined(const wchar_t *aliasname, bool user_friendly)
{
    const alias_T *alias = ht_get(&aliases, aliasname).value;

    if (alias == NULL || (alias->flags & AF_GLOBAL))
	return false;

    if (!user_friendly)
	return print_alias(aliasname, alias, true);
    else
	return xprintf(gt("%ls: alias for `%ls'\n"), aliasname, alias->value);
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
	le_candgentype_T type =
	    (alias->flags & AF_GLOBAL) ? CGT_GALIAS : CGT_NALIAS;

	if (compopt->type & type)
	    if (le_wmatch_comppatterns(compopt, kv.key))
		le_new_candidate(CT_ALIAS, xwcsdup(kv.key), NULL, compopt);
    }
}

#endif /* YASH_ENABLE_LINEEDIT */


/********** Built-ins **********/

/* The "alias" built-in, which accepts the following options:
 *  -g: define global aliases
 *  -p: print aliases in the form of whole commands */
int alias_builtin(int argc, void **argv)
{
    static const struct xoption long_options[] = {
	{ L"global", OPTARG_NONE, L'g', },
	{ L"prefix", OPTARG_NONE, L'p', },
#if YASH_ENABLE_HELP
	{ L"help",   OPTARG_NONE, L'-', },
#endif
	{ NULL, 0, 0, },
    };

    bool global = false, prefix = false;
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv,
		    posixly_correct ? L"" : L"gp",
		    long_options, NULL))) {
	switch (opt) {
	    case L'g':  global = true;  break;
	    case L'p':  prefix = true;  break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
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
		if (!wcschr(nameend + 1, L'\n'))
		    define_alias(arg, nameend, global);
		else
		    xerror(0, Ngt("`%ls': alias cannot contain newlines"), arg);
	    } else if (nameend != arg && *nameend == L'\0') {
		/* print alias */
		const alias_T *alias = ht_get(&aliases, arg).value;
		if (alias != NULL) {
		    if (!print_alias(arg, alias, prefix))
			break;
		} else {
		    xerror(0, Ngt("%ls: no such alias"), arg);
		}
	    } else {
		xerror(0, Ngt("`%ls': invalid alias name"), arg);
	    }
	} while (++xoptind < argc);
    }
    return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;
}

#if YASH_ENABLE_HELP
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
#endif

/* The "unalias" built-in, which accepts the following option:
 *  -a: remove all aliases */
int unalias_builtin(int argc, void **argv)
{
    bool all = false;
    wchar_t opt;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, L"a", all_option, NULL))) {
	switch (opt) {
	    case L'a':  all = true;  break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		goto print_usage;
	}
    }

    if (all) {
	if (xoptind != argc)
	    goto print_usage;
	remove_all_aliases();
    } else {
	if (xoptind == argc)
	    goto print_usage;
	do {
	    const wchar_t *arg = ARGV(xoptind);
	    if (!remove_alias(arg))
		xerror(0, Ngt("%ls: no such alias"), arg);
	} while (++xoptind < argc);
    }
    return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;

print_usage:
    fprintf(stderr, gt("Usage:  unalias name[...]\n"
                       "        unalias -a\n"));
    return Exit_ERROR;
}

#if YASH_ENABLE_HELP
const char unalias_help[] = Ngt(
"unalias - undefine aliases\n"
"\tunalias name...\n"
"\tunalias -a\n"
"Removes the specified alias definitions.\n"
"If the -a (--all) option is specified, all definitions are removed.\n"
);
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
