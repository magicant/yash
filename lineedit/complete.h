/* Yash: yet another shell */
/* complete.h: command line completion */
/* (C) 2007-2022 magicant */

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
#include "../xgetopt.h"


typedef enum le_candtype_T {
    CT_WORD,       // normal word
    CT_FILE,       // file name
    CT_COMMAND,    // command name
    CT_ALIAS,      // alias name
    CT_OPTION,     // command option
    CT_VAR,        // variable name
    CT_JOB,        // job name
    CT_SIG,        // signal name
    CT_LOGNAME,    // user name
    CT_GRP,        // group name
    CT_HOSTNAME,   // host name
    CT_BINDKEY,    // line-editing command name
} le_candtype_T;
typedef struct le_rawvalue_T {
    char *raw;       // pre-printed version of candidate value/description
    int width;       // screen width of `raw'
} le_rawvalue_T;
typedef struct le_candidate_T {
    le_candtype_T type;
    wchar_t *value;               // candidate value without ignored prefix
    wchar_t *origvalue;           // candidate value including ignored prefix
    le_rawvalue_T rawvalue;
    wchar_t *desc;                // candidate description
    le_rawvalue_T rawdesc;
    _Bool terminate;              // if completed word should be terminated
    union {
	struct {
	    _Bool is_executable;
	    mode_t mode;
	    nlink_t nlink;
	    off_t size;
	} filestat;               // only used for CT_FILE
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
    CTXT_ARGUMENT,           // command argument word
    CTXT_TILDE,              // tilde expansion
    CTXT_VAR,                // variable name
    CTXT_ARITH,              // arithmetic expansion
    CTXT_ASSIGN,             // assignment
    CTXT_REDIR,              // redirection target (that is a file name)
    CTXT_REDIR_FD,           // redirection target (that is a file descriptor)
    CTXT_FOR_IN,             // where keyword "in" or "do" is expected
    CTXT_FOR_DO,             // where keyword "do" is expected
    CTXT_CASE_IN,            // where keyword "in" is expected
    CTXT_FUNCTION,           // where a function name is expected
    CTXT_MASK     = ((1 << 4) - 1),
    CTXT_EBRACED  = 1 << 4,  // completion occurs in brace expansion
    CTXT_VBRACED  = 1 << 5,  // completion occurs in variable expansion
    CTXT_QUOTED   = 1 << 6,  // unquote after completion
} le_contexttype_T;
typedef struct le_context_T {
    le_quote_T quote;
    le_contexttype_T type;
    int pwordc;             // number of `pwords' (non-negative)
    void **pwords;          // words preceding the source word
    wchar_t *src;           // source word
    wchar_t *pattern;       // source word as a matching pattern
    size_t srcindex;        // start index of source word
    size_t origindex;       // original `le_main_index'
    _Bool substsrc;         // substitute source word with candidates?
} le_context_T;
/* The `pwords' member is an array of pointers to wide strings containing the
 * expanded words preceding the source word.
 * The `src' member is the source word expanded by the four expansions, brace
 * expansion, word splitting, and quote removal.
 * The `pattern' member is like `src', but differs in that it may contain
 * backslash escapes and that it may have an additional asterisk at the end
 * to make it a pattern.
 * The `srcindex' member designates where the source word starts in the edit
 * line. The `origindex' member is the value of `le_main_index' before starting
 * completion.
 * The `substsrc' member designates whether the source word should be
 * substituted with obtained candidates. The value is true if and only if the
 * source word after word splitting is not a globbing pattern, in which case an
 * asterisk is appended to the source word to make it a pattern. */
/* If the source word is field-split into more than one word, the words but the
 * last are included in `pwords'. */
/* Examples:
 *   For the command line of "foo --bar='x", the completion parser function
 *   `le_get_context' returns:
 *     `quote'     = QUOTE_SINGLE
 *     `type'      = CTXT_NORMAL
 *     `pwordc'    = 1
 *     `pwords'    = { L"foo" }
 *     `src',      = L"--bar=x"
 *     `pattern',  = L"--bar=\\x"
 *     `srcindex'  = 4
 *     `origindex' = 12
 *     `substsrc'  = false
 */

typedef enum le_candgentype_T {
    CGT_FILE       = 1 << 0, // file of any kind
    CGT_DIRECTORY  = 1 << 1, // directory
    CGT_EXECUTABLE = 1 << 2, // executable file
    CGT_SBUILTIN   = 1 << 3, // special builtin
    CGT_MBUILTIN   = 1 << 4, // mandatory builtin
    CGT_LBUILTIN   = 1 << 5, // elective builtin
    CGT_XBUILTIN   = 1 << 6, // extension builtin
    CGT_UBUILTIN   = 1 << 7, // substitutive builtin
    CGT_BUILTIN    = CGT_SBUILTIN | CGT_MBUILTIN | CGT_LBUILTIN | CGT_XBUILTIN |
		     CGT_UBUILTIN,
    CGT_EXTCOMMAND = 1 << 8, // external command
    CGT_FUNCTION   = 1 << 9, // function
    CGT_COMMAND    = CGT_BUILTIN | CGT_EXTCOMMAND | CGT_FUNCTION,
    CGT_KEYWORD    = 1 << 10, // shell keyword
    CGT_NALIAS     = 1 << 11, // non-global alias
    CGT_GALIAS     = 1 << 12, // global alias
    CGT_ALIAS      = CGT_NALIAS | CGT_GALIAS,
    CGT_SCALAR     = 1 << 13, // scalar variable
    CGT_ARRAY      = 1 << 14, // array variable
    CGT_VARIABLE   = CGT_SCALAR | CGT_ARRAY,
    CGT_RUNNING    = 1 << 15, // running job
    CGT_STOPPED    = 1 << 16, // stopped job
    CGT_DONE       = 1 << 17, // finished job
    CGT_JOB        = CGT_RUNNING | CGT_STOPPED | CGT_DONE,
    CGT_SIGNAL     = 1 << 18, // signal name
    CGT_LOGNAME    = 1 << 19, // login user name
    CGT_GROUP      = 1 << 20, // group name
    CGT_HOSTNAME   = 1 << 21, // host name
    CGT_BINDKEY    = 1 << 22, // line-editing command name
    CGT_DIRSTACK   = 1 << 23, // directory stack entry
} le_candgentype_T;
typedef struct le_comppattern_T {
    struct le_comppattern_T *next;
    enum { CPT_ACCEPT, CPT_REJECT, } type;
    const wchar_t *pattern;       // pattern (not including ignored prefix)
    struct xfnmatch_T *cpattern;  // compiled pattern
} le_comppattern_T;
typedef struct le_compopt_T {
    const le_context_T *ctxt;    // completion context
    le_candgentype_T type;       // type of generated candidates
    const wchar_t *src;          // `ctxt->src' + wcslen(ignored prefix)
    le_comppattern_T *patterns;  // patterns to be matched to candidates
    const wchar_t *suffix;       // string appended to candidate values
    _Bool terminate;             // whether completed word should be terminated
} le_compopt_T;
/* The `patterns' member of the `le_compopt_T' structure must not be NULL and
 * the first element of the `le_comppattern_T' linked list must be of type
 * CPT_ACCEPT. */

typedef void le_compresult_T(void);


extern plist_T le_candidates;
extern size_t le_selected_candidate_index;

extern void le_complete(le_compresult_T lecr);
extern void lecr_nop(void);
extern void lecr_normal(void);
extern void lecr_substitute_all_candidates(void);
extern void lecr_longest_common_prefix(void);
extern void le_complete_select_candidate(int offset);
extern void le_complete_select_column(int offset);
extern void le_complete_select_page(int offset);
extern _Bool le_complete_fix_candidate(int index);
extern void le_complete_cleanup(void);
extern void le_compdebug(const char *format, ...)
    __attribute__((nonnull,format(printf,1,2)));

extern void set_completion_variables(void);

extern void le_new_command_candidate(wchar_t *cmdname)
    __attribute__((nonnull));
extern void le_new_candidate(le_candtype_T type,
	wchar_t *restrict value, wchar_t *restrict desc,
	const le_compopt_T *compopt)
    __attribute__((nonnull(4)));
extern void le_add_candidate(le_candidate_T *cand, const le_compopt_T *compopt)
    __attribute__((nonnull));
extern _Bool le_compile_cpatterns(const le_compopt_T *compopt)
    __attribute__((nonnull));
extern _Bool le_match_comppatterns(const le_compopt_T *compopt, const char *s)
    __attribute__((nonnull));
extern _Bool le_wmatch_comppatterns(
	const le_compopt_T *compopt, const wchar_t *s)
    __attribute__((nonnull));

extern int complete_builtin(int argc, void **argv)
    __attribute__((nonnull));
#if YASH_ENABLE_HELP
extern const char complete_help[], complete_syntax[];
#endif
extern const struct xgetopt_T complete_options[];


/* This function is defined in "../alias.c". */
extern void generate_alias_candidates(const le_compopt_T *compopt)
    __attribute__((nonnull));

/* This function is defined in "../builtin.c". */
extern void generate_builtin_candidates(const le_compopt_T *compopt)
    __attribute__((nonnull));

/* This function is defined in "../job.c". */
extern void generate_job_candidates(const le_compopt_T *compopt)
    __attribute__((nonnull));

/* This function is defined in "../sig.c". */
extern void generate_signal_candidates(const le_compopt_T *compopt)
    __attribute__((nonnull));

/* These functions are defined in "../variable.c". */
extern void generate_variable_candidates(const le_compopt_T *compopt)
    __attribute__((nonnull));
extern void generate_function_candidates(const le_compopt_T *compopt)
    __attribute__((nonnull));
extern void generate_dirstack_candidates(const le_compopt_T *compopt)
    __attribute__((nonnull));

/* This function is defined in "keymap.c". */
extern void generate_bindkey_candidates(const le_compopt_T *compopt)
    __attribute__((nonnull));


#endif /* YASH_COMPLETE_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
