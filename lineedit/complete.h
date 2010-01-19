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
#include "../plist.h"


typedef enum le_candtype_T {
    CT_WORD,       // normal word
    CT_FILE,       // non-directory file name
    CT_DIR,        // directory name
    CT_COMMAND,    // command name
    CT_FUNC,       // function name
    CT_ALIAS,      // alias name
    CT_OPTION,     // command option
    CT_VAR,        // variable name
    CT_JOB,        // job name
    CT_SHOPT,      // shell option name
    CT_FD,         // file descriptor
    CT_SIG,        // signal name
    CT_LOGNAME,    // user name
    CT_HOSTNAME,   // host name
    CT_BINDKEY,    // line-editing command name
} le_candtype_T;
typedef struct le_candidate_T {
    le_candtype_T type;
    wchar_t *value;
    char *rawvalue;
    int width;
} le_candidate_T;

typedef enum le_quote_T {
    QUOTE_NONE,    // no quotation required
    QUOTE_NORMAL,  // not in single/double quotation; backslashes can be used
    QUOTE_SINGLE,  // in single quotation
    QUOTE_DOUBLE,  // in double quotation
} le_quote_T;
typedef enum le_context_T {
    CTXT_NORMAL,         // normal word
    CTXT_TILDE,          // tilde expansion
    CTXT_VAR,            // variable name not in brackets
    CTXT_VAR_BRCK,       // variable name in brackets
    CTXT_VAR_BRCK_WORD,  // matching pattern/substitution in bracketed variable
    CTXT_ARITH,          // arithmetic expansion
    CTXT_REDIR,          // redirection target (that is a file name)
    CTXT_REDIR_FD,       // redirection target (that is a file descriptor)
} le_context_T;

typedef enum le_candgentype_T {
    CGT_FILE       = 1 << 0, // file of any kind
    CGT_DIRECTORY  = 1 << 1, // directory
    CGT_EXECUTABLE = 1 << 2, // executable file
    CGT_SBUILTIN   = 1 << 3, // special builtin
    CGT_SSBUILTIN  = 1 << 4, // semi-special builtin
    CGT_RBUILTIN   = 1 << 5, // regular builtin
    CGT_EXTCOMMAND = 1 << 6, // external command
    CGT_FUNCTION   = 1 << 7, // function
    CGT_KEYWORD    = 1 << 8, // shell keyword
    CGT_COMMAND    = CGT_SBUILTIN | CGT_SSBUILTIN | CGT_RBUILTIN
		   | CGT_EXTCOMMAND | CGT_FUNCTION | CGT_KEYWORD,
    CGT_NALIAS     = 1 << 9, // non-global alias
    CGT_GALIAS     = 1 << 10, // global alias
    CGT_ALIAS      = CGT_NALIAS | CGT_GALIAS,
    CGT_SCALAR     = 1 << 11, // scalar variable
    CGT_ARRAY      = 1 << 12, // array variable
    CGT_VARIABLE   = CGT_SCALAR | CGT_ARRAY,
    CGT_RUNNING    = 1 << 13, // running job
    CGT_STOPPED    = 1 << 14, // stopped job
    CGT_DONE       = 1 << 15, // finished job
    CGT_JOB        = CGT_RUNNING | CGT_STOPPED | CGT_DONE,
    CGT_SHOPT      = 1 << 16, // shell option
    CGT_SIGNAL     = 1 << 17, // signal name
    CGT_LOGNAME    = 1 << 18, // login user name
    CGT_HOSTNAME   = 1 << 19, // host name
    CGT_BINDKEY    = 1 << 20, // line-editing command name
} le_candgentype_T;
typedef struct le_candgen_T {
    le_candgentype_T type;
    void **words;
    wchar_t *function;
} le_candgen_T;
/* The `le_candgen_T' structure specifies how to generate completion candidates.
 * The `type' member is bitwise OR of CGT_* values.
 * The `words' member is a pointer to an array of pointers to wide strings that
 * are added as candidates.
 * The `function' member is the name of the function that is called to generate
 * candidates.
 * The `words' and `function' members may be NULL. */


extern plist_T le_candidates;
extern size_t le_selected_candidate_index;

extern le_context_T le_context;
extern size_t le_source_word_index;

extern void le_complete(void);
extern void le_complete_select(int offset);
extern void le_complete_cleanup(void);


#endif /* YASH_COMPLETE_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
