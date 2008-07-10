/* Yash: yet another shell */
/* alias.c: alias substitution */
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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include "option.h"
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "hashtable.h"
#include "alias.h"
#include "parser.h"
#include "expand.h"
#include "builtin.h"
#include "exec.h"


typedef enum {
    AF_BLANKEND  = 1 << 0,  /* alias value ends with a blank */
    AF_GLOBAL    = 1 << 1,  /* is a global alias */
    AF_NORECURSE = 1 << 2,  /* prevent recursive substitution */
} aliasflags_T;
typedef struct alias_T {
    aliasflags_T flags;
    wchar_t value[];
} alias_T;

static inline bool is_alias_name_char(wchar_t c)
    __attribute__((pure));
static bool define_alias(const wchar_t *nameandvalue, bool global)
    __attribute__((nonnull));
static bool remove_alias(const wchar_t *name)
    __attribute__((nonnull));
static void remove_all_aliases(void);


#ifndef ALIAS_RECURSION_MAX
#define ALIAS_RECURSION_MAX 30
#endif

/* If true, alias substitution is performed. */
bool alias_enabled = true;

/* Hashtable mapping alias names (wide strings) to alias_T's. */
hashtable_T aliases;


/* Initializes the alias module. */
void init_alias(void)
{
    ht_init(&aliases, hashwcs, htwcscmp);
}

/* Returns true iff `c' is a character that can be used in an alias name. */
bool is_alias_name_char(wchar_t c)
{
    return !iswblank(c) && !wcschr(L"\n=$<>\\'\"`;&|()#", c);
}

/* Defines an alias.
 * `nameandvalue' should be a wide string of the form "name=value".
 * If `nameandvalue' does not contain an L'=' character or the alias name is
 * invalid, false is returned. Otherwise, the specified alias is (re)defined
 * and true is returned. */
bool define_alias(const wchar_t *nameandvalue, bool global)
{
    const wchar_t *equal = nameandvalue;
    while (is_alias_name_char(*equal))
	equal++;
    if (*equal != L'=' || *nameandvalue == L'=')
	return false;

    size_t namelen = equal - nameandvalue;
    size_t valuelen = wcslen(equal + 1);
    alias_T *alias = xmalloc(
	    sizeof *alias + sizeof *alias->value * (namelen + valuelen + 2));
    alias->flags = 0;
    if (global)
	alias->flags |= AF_GLOBAL;
    if (iswblank(equal[valuelen]))
	alias->flags |= AF_BLANKEND;
    wcscpy(alias->value, equal + 1);
    wmemcpy(alias->value + valuelen + 1, nameandvalue, namelen);
    alias->value[namelen + valuelen + 1] = L'\0';

    vfree(ht_set(&aliases, alias->value + valuelen + 1, alias));
    return true;
}

/* Removes an alias definition with the specified name if any.
 * Returns true if the alias definition is successfully removed.
 * Returns false if no alias definition is found to be removed. */
bool remove_alias(const wchar_t *name)
{
    alias_T *alias = ht_remove(&aliases, name).value;
    free(alias);
    return alias != NULL;
}

/* Removes all alias definitions. */
void remove_all_aliases(void)
{
    ht_clear(&aliases, vfree);
}

/* Performs alias substitution at the specified index `i' in the buffer `buf'.
 * If `globalonly' is true, only global aliases are substituted.
 * Returns true iff substitution occured. */
void substitute_alias(xwcsbuf_T *buf, size_t i, bool globalonly)
{
    static unsigned recursion = 0;
    if (!alias_enabled
	    || (globalonly && posixly_correct)
	    || recursion >= ALIAS_RECURSION_MAX)
	return;
    recursion++;

    size_t j = i;
    while (is_alias_name_char(buf->contents[j]))
	j++;
    if (i < j && is_token_delimiter_char(buf->contents[j])) {
	wchar_t savechar = buf->contents[j];
	buf->contents[j] = L'\0';
	alias_T *alias = ht_get(&aliases, buf->contents + i).value;
	buf->contents[j] = savechar;
	if (alias != NULL
		&& !(alias->flags & AF_NORECURSE)
		&& (!globalonly || (alias->flags & AF_GLOBAL))) {
	    size_t valuelen = wcslen(alias->value);
	    wb_replace_force(buf, i, j - i, alias->value, valuelen);
	    if (alias->flags & AF_BLANKEND) {
		size_t ii = i + valuelen;
		while (iswblank(buf->contents[ii]))
		    ii++;
		substitute_alias(buf, ii, globalonly);
	    }
	    alias->flags |= AF_NORECURSE;
	    substitute_alias(buf, i, globalonly);
	    alias->flags &= ~AF_NORECURSE;
	}
    }

    recursion--;
}


/********** Builtins **********/

static void print_alias(const wchar_t *name, const alias_T *alias, bool prefix);

/* Prints a alias definition to stderr. */
void print_alias(const wchar_t *name, const alias_T *alias, bool prefix)
{
    wchar_t *qvalue = quote_sq(alias->value);
    const char *format;
    if (!prefix)
	format = "%ls=%ls\n";
    else if (alias->flags & AF_GLOBAL)
	format = "alias -g %ls=%ls\n";
    else
	format = "alias %ls=%ls\n";
    printf(format, name, qvalue);
    free(qvalue);
}

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
		return EXIT_SUCCESS;
	    default:
		fprintf(stderr, gt(posixly_correct
			    ? "Usage:  alias [name[=value]...]\n"
			    : "Usage:  alias [-gp] [name[=value]...]\n"));
		return EXIT_ERROR;
	}
    }

    if (xoptind == argc) {
	/* print all aliases */
	kvpair_T *kvs = ht_tokvarray(&aliases);
	qsort(kvs, aliases.count, sizeof *kvs, keywcscoll);
	for (size_t i = 0; i < aliases.count; i++)
	    print_alias(kvs[i].key, kvs[i].value, prefix);
	free(kvs);
	return EXIT_SUCCESS;
    }
    do {
	const wchar_t *arg = ARGV(xoptind);
	if (!define_alias(arg, global)) {
	    alias_T *alias = ht_get(&aliases, arg).value;
	    if (alias) {
		print_alias(arg, alias, prefix);
	    } else {
		xerror(0, wcschr(arg, L'=')
			? Ngt("%ls: %ls: invalid alias name")
			: Ngt("%ls: %ls: no such alias"),
			ARGV(0), arg);
		err = true;
	    }
	}
    } while (++xoptind < argc);
    return err ? EXIT_FAILURE1 : EXIT_SUCCESS;
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
"All the options are available in POSIXly correct mode only.\n"
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
		return EXIT_SUCCESS;
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
		xerror(0, Ngt("%ls: %ls: no such alias"), ARGV(0), arg);
		err = true;
	    }
	} while (++xoptind < argc);
    }
    return err ? EXIT_FAILURE1 : EXIT_SUCCESS;

print_usage:
    fprintf(stderr, gt("Usage:  unalias name[...]\n"
                       "        unalias -a\n"));
    return EXIT_ERROR;
}

const char unalias_help[] = Ngt(
"unalias - undefine aliases\n"
"\tunalias name[...]\n"
"\tunalias -a\n"
"Removes the specified alias definitions.\n"
"If the -a option is specified, all definitions are removed.\n"
);


/* vim: set ts=8 sts=4 sw=4 noet: */
