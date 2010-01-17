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
    CT_ALIAS,      // alias name
    CT_VAR,        // variable name
    CT_FUNC,       // function name
    CT_JOB,        // job name
    CT_SHOPT,      // shell option name
    CT_FD,         // file descriptor
    CT_SIG,        // signal name
    CT_LOGNAME,    // user name
    CT_HOSTNAME,   // host name
} le_candtype_T;
typedef struct le_candidate_T {
    le_candtype_T type;
    wchar_t *value;
    char *rawvalue;
    int width;
} le_candidate_T;

typedef enum le_quote_T {
    QUOTE_NONE, QUOTE_NORMAL, QUOTE_SINGLE, QUOTE_DOUBLE,
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

extern plist_T le_candidates;
extern size_t le_selected_candidate_index;

extern le_context_T le_context;
extern size_t le_source_word_index;

extern void le_complete(void);
extern void le_complete_select(int offset);
extern void le_complete_cleanup(void);


#endif /* YASH_COMPLETE_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
