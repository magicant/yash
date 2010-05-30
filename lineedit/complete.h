/* Yash: yet another shell */
/* complete.h: command line completion */
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


#ifndef YASH_COMPLETE_H
#define YASH_COMPLETE_H

#include <stddef.h>
#include <sys/types.h>
#include "../plist.h"


typedef enum le_candtype_T {
    CT_WORD,       // normal word
    CT_FILE,       // file name
    CT_COMMAND,    // command name
    CT_ALIAS,      // alias name
    CT_OPTION,     // normal command option
    CT_OPTIONA,    // command option that takes an argument
    CT_VAR,        // variable name
    CT_JOB,        // job name
    CT_FD,         // file descriptor
    CT_SIG,        // signal name
    CT_LOGNAME,    // user name
    CT_GRP,        // group name
    CT_HOSTNAME,   // host name
    CT_BINDKEY,    // line-editing command name
} le_candtype_T;
typedef struct le_candvalue_T {
    wchar_t *value;  // the value
    char *raw;       // pre-printed version of `value'
    int width;       // screen width of `raw'
} le_candvalue_T;
typedef struct le_candidate_T {
    le_candtype_T type;
    le_candvalue_T value, desc;  /* the candidate value and its description */
    union {
	struct {
	    _Bool is_executable;
	    mode_t mode;
	    nlink_t nlink;
	    off_t size;
	} filestat;               /* only used for CT_FILE */
    } appendage;
} le_candidate_T;

typedef enum le_quote_T {
    QUOTE_NONE,    // no quotation required
    QUOTE_NORMAL,  // not in single/double quotation; backslashes can be used
    QUOTE_SINGLE,  // in single quotation
    QUOTE_DOUBLE,  // in double quotation
} le_quote_T;
typedef enum le_contexttype_T {
    CTXT_NORMAL,             // normal word
    CTXT_COMMAND,            // command word
    CTXT_TILDE,              // tilde expansion
    CTXT_VAR,                // variable name
    CTXT_ARITH,              // arithmetic expansion
    CTXT_ASSIGN,             // assignment
    CTXT_REDIR,              // redirection target (that is a file name)
    CTXT_REDIR_FD,           // redirection target (that is a file descriptor)
    CTXT_FOR_IN,             // where keyword "in" or "do" is expected
    CTXT_FOR_DO,             // where keyword "do" is expected
    CTXT_CASE_IN,            // where keyword "in" is expected
    CTXT_MASK     = ((1 << 4) - 1),
    CTXT_BRACED   = 1 << 4,  // completion occurs in variable expansion
    CTXT_QUOTED   = 1 << 5,  // unquote after completion
} le_contexttype_T;
typedef struct le_context_T {
    le_quote_T quote;
    le_contexttype_T type;
    int pwordc;             // number of `pwords' (non-negative)
    void **pwords;          // words preceding the source word
    wchar_t *src;           // source word
    wchar_t *pattern;       // source word as a matching pattern
    struct xfnmatch_T *cpattern;
    size_t srcindex;        // start index of source word
    _Bool substsrc;         // substitute source word with candidates?
} le_context_T;
/* The `pwords' member is an array of pointers to wide strings containing the
 * expanded words preceding the source word.
 * The `src' member is the source word expanded by the four expansions, brace
 * expansion, word splitting, and quote removal. The `pattern' member is like
 * the `src' member, but differs in that it may contain backslash escapes and
 * that it may have an additional asterisk at the end to make it a pattern. If
 * an ignored prefix is found in the source word (cf. `source_word_skip'), the
 * prefix is stripped off `src' and `pattern'. The `cpattern' is the compiled
 * version of `pattern'.
 * The `srcindex' member designates where the source word starts in the edit
 * line.
 * The `substsrc' member designates whether the source word should be
 * substituted with obtained candidates. The value is true if and only if the
 * source word after word splitting is not a globbing pattern, in which case an
 * asterisk is appended to the source word to make it a pattern. */
/* If the source word is field-split into more than one word, the words but the
 * last are included in `pwords'. */

typedef enum le_candgentype_T {
    CGT_FILE       = 1 << 0, // file of any kind
    CGT_DIRECTORY  = 1 << 1, // directory
    CGT_EXECUTABLE = 1 << 2, // executable file
    CGT_SBUILTIN   = 1 << 3, // special builtin
    CGT_SSBUILTIN  = 1 << 4, // semi-special builtin
    CGT_RBUILTIN   = 1 << 5, // regular builtin
    CGT_BUILTIN    = CGT_SBUILTIN | CGT_SSBUILTIN | CGT_RBUILTIN,
    CGT_EXTCOMMAND = 1 << 6, // external command
    CGT_FUNCTION   = 1 << 7, // function
    CGT_COMMAND    = CGT_BUILTIN | CGT_EXTCOMMAND | CGT_FUNCTION,
    CGT_KEYWORD    = 1 << 8, // shell keyword
    CGT_OPTION	   = 1 << 9, // command option
    CGT_NALIAS     = 1 << 10, // non-global alias
    CGT_GALIAS     = 1 << 11, // global alias
    CGT_ALIAS      = CGT_NALIAS | CGT_GALIAS,
    CGT_SCALAR     = 1 << 12, // scalar variable
    CGT_ARRAY      = 1 << 13, // array variable
    CGT_VARIABLE   = CGT_SCALAR | CGT_ARRAY,
    CGT_RUNNING    = 1 << 14, // running job
    CGT_STOPPED    = 1 << 15, // stopped job
    CGT_DONE       = 1 << 16, // finished job
    CGT_JOB        = CGT_RUNNING | CGT_STOPPED | CGT_DONE,
    CGT_SIGNAL     = 1 << 17, // signal name
    CGT_LOGNAME    = 1 << 18, // login user name
    CGT_GROUP      = 1 << 19, // group name
    CGT_HOSTNAME   = 1 << 20, // host name
    CGT_BINDKEY    = 1 << 21, // line-editing command name
} le_candgentype_T;

typedef void le_compresult_T(void);


extern plist_T le_candidates;
extern size_t le_selected_candidate_index;

extern void le_complete(le_compresult_T lecr);
extern void lecr_normal(void);
extern void lecr_longest_common_prefix(void);
extern void le_complete_select_candidate(int offset);
extern void le_complete_select_column(int offset);
extern void le_complete_select_page(int offset);
extern void le_complete_cleanup(void);
extern void le_compdebug(const char *format, ...)
    __attribute__((nonnull,format(printf,1,2)));

extern void le_new_command_candidate(wchar_t *cmdname)
    __attribute__((nonnull));
extern void le_new_candidate(le_candtype_T type, wchar_t *value, wchar_t *desc);
extern void le_add_candidate(le_candidate_T *cand)
    __attribute__((nonnull));
extern _Bool le_compile_cpattern(le_context_T *context)
    __attribute__((nonnull));

extern int complete_builtin(int argc, void **argv)
    __attribute__((nonnull));
extern const char complete_help[];


/* This function is defined in "../alias.c". */
extern void generate_alias_candidates(
	le_candgentype_T type, le_context_T *context)
    __attribute__((nonnull));

/* This function is defined in "../builtin.c". */
extern void generate_builtin_candidates(
	le_candgentype_T type, le_context_T *context)
    __attribute__((nonnull));

/* This function is defined in "../exec.c". */
extern int generate_candidates_using_function(
	const wchar_t *funcname, le_context_T *context)
    __attribute__((nonnull(2)));

/* This function is defined in "../job.c". */
extern void generate_job_candidates(
	le_candgentype_T type, le_context_T *context)
    __attribute__((nonnull));

/* This function is defined in "../sig.c". */
extern void generate_signal_candidates(
	le_candgentype_T type, le_context_T *context)
    __attribute__((nonnull));

/* These functions are defined in "../variable.c". */
extern void generate_variable_candidates(
	le_candgentype_T type, le_context_T *context)
    __attribute__((nonnull));
extern void generate_function_candidates(
	le_candgentype_T type, le_context_T *context)
    __attribute__((nonnull));

/* This function is defined in "keymap.c". */
extern void generate_bindkey_candidates(
	le_candgentype_T type, le_context_T *context)
    __attribute__((nonnull));


#endif /* YASH_COMPLETE_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
