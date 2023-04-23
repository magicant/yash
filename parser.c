/* Yash: yet another shell */
/* parser.c: syntax parser */
/* (C) 2007-2023 magicant */

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
#include "parser.h"
#include <assert.h>
#include <errno.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include "alias.h"
#include "expand.h"
#include "input.h"
#include "option.h"
#include "plist.h"
#include "strbuf.h"
#include "util.h"
#if YASH_ENABLE_DOUBLE_BRACKET
# include "builtins/test.h"
#endif
#if YASH_ENABLE_LINEEDIT
# include "lineedit/lineedit.h"
#endif


/********** Functions That Free Parse Trees **********/

static void pipesfree(pipeline_T *p);
static void ifcmdsfree(ifcommand_T *i);
static void caseitemsfree(caseitem_T *i);
#if YASH_ENABLE_DOUBLE_BRACKET
static void dbexpfree(dbexp_T *e);
#endif
static void wordunitfree(wordunit_T *wu)
    __attribute__((nonnull));
static void wordfree_vp(void *w);
static void assignsfree(assign_T *a);
static void redirsfree(redir_T *r);
static void embedcmdfree(embedcmd_T c);

void andorsfree(and_or_T *a)
{
    while (a != NULL) {
	pipesfree(a->ao_pipelines);

	and_or_T *next = a->next;
	free(a);
	a = next;
    }
}

void pipesfree(pipeline_T *p)
{
    while (p != NULL) {
	comsfree(p->pl_commands);

	pipeline_T *next = p->next;
	free(p);
	p = next;
    }
}

void comsfree(command_T *c)
{
    while (c != NULL) {
	if (!refcount_decrement(&c->refcount))
	    break;

	redirsfree(c->c_redirs);
	switch (c->c_type) {
	    case CT_SIMPLE:
		assignsfree(c->c_assigns);
		plfree(c->c_words, wordfree_vp);
		break;
	    case CT_GROUP:
	    case CT_SUBSHELL:
		andorsfree(c->c_subcmds);
		break;
	    case CT_IF:
		ifcmdsfree(c->c_ifcmds);
		break;
	    case CT_FOR:
		free(c->c_forname);
		plfree(c->c_forwords, wordfree_vp);
		andorsfree(c->c_forcmds);
		break;
	    case CT_WHILE:
		andorsfree(c->c_whlcond);
		andorsfree(c->c_whlcmds);
		break;
	    case CT_CASE:
		wordfree(c->c_casword);
		caseitemsfree(c->c_casitems);
		break;
#if YASH_ENABLE_DOUBLE_BRACKET
	    case CT_BRACKET:
		dbexpfree(c->c_dbexp);
		break;
#endif /* YASH_ENABLE_DOUBLE_BRACKET */
	    case CT_FUNCDEF:
		wordfree(c->c_funcname);
		comsfree(c->c_funcbody);
		break;
	}

	command_T *next = c->next;
	free(c);
	c = next;
    }
}

void ifcmdsfree(ifcommand_T *i)
{
    while (i != NULL) {
	andorsfree(i->ic_condition);
	andorsfree(i->ic_commands);

	ifcommand_T *next = i->next;
	free(i);
	i = next;
    }
}

void caseitemsfree(caseitem_T *i)
{
    while (i != NULL) {
	plfree(i->ci_patterns, wordfree_vp);
	andorsfree(i->ci_commands);

	caseitem_T *next = i->next;
	free(i);
	i = next;
    }
}

#if YASH_ENABLE_DOUBLE_BRACKET
void dbexpfree(dbexp_T *e)
{
    if (e == NULL)
	return;

    free(e->operator);
    switch (e->type) {
	case DBE_OR:
	case DBE_AND:
	case DBE_NOT:
	    dbexpfree(e->lhs.subexp);
	    dbexpfree(e->rhs.subexp);
	    break;
	case DBE_UNARY:
	case DBE_BINARY:
	case DBE_STRING:
	    wordfree(e->lhs.word);
	    wordfree(e->rhs.word);
	    break;
    }
    free(e);
}
#endif /* YASH_ENABLE_DOUBLE_BRACKET */

void wordunitfree(wordunit_T *wu)
{
    switch (wu->wu_type) {
	case WT_STRING:
	    free(wu->wu_string);
	    break;
	case WT_PARAM:
	    paramfree(wu->wu_param);
	    break;
	case WT_CMDSUB:
	    embedcmdfree(wu->wu_cmdsub);
	    break;
	case WT_ARITH:
	    wordfree(wu->wu_arith);
	    break;
    }
    free(wu);
}

void wordfree(wordunit_T *w)
{
    while (w != NULL) {
	wordunit_T *next = w->next;
	wordunitfree(w);
	w = next;
    }
}

void wordfree_vp(void *w)
{
    wordfree((wordunit_T *) w);
}

void paramfree(paramexp_T *p)
{
    if (p != NULL) {
	if (p->pe_type & PT_NEST)
	    wordfree(p->pe_nest);
	else
	    free(p->pe_name);
	wordfree(p->pe_start);
	wordfree(p->pe_end);
	wordfree(p->pe_match);
	wordfree(p->pe_subst);
	free(p);
    }
}

void assignsfree(assign_T *a)
{
    while (a != NULL) {
	free(a->a_name);
	switch (a->a_type) {
	    case A_SCALAR:
		wordfree(a->a_scalar);
		break;
	    case A_ARRAY:
		plfree(a->a_array, wordfree_vp);
		break;
	}

	assign_T *next = a->next;
	free(a);
	a = next;
    }
}

void redirsfree(redir_T *r)
{
    while (r != NULL) {
	switch (r->rd_type) {
	    case RT_INPUT:  case RT_OUTPUT:  case RT_CLOBBER:  case RT_APPEND:
	    case RT_INOUT:  case RT_DUPIN:   case RT_DUPOUT:   case RT_PIPE:
	    case RT_HERESTR:
		wordfree(r->rd_filename);
		break;
	    case RT_HERE:  case RT_HERERT:
		free(r->rd_hereend);
		wordfree(r->rd_herecontent);
		break;
	    case RT_PROCIN:  case RT_PROCOUT:
		embedcmdfree(r->rd_command);
		break;
	}

	redir_T *next = r->next;
	free(r);
	r = next;
    }
}

void embedcmdfree(embedcmd_T c)
{
    if (c.is_preparsed)
	andorsfree(c.value.preparsed);
    else
	free(c.value.unparsed);
}


/********** Auxiliary Functions for Parser **********/

typedef enum tokentype_T {
    TT_UNKNOWN,
    TT_END_OF_INPUT,
    TT_WORD,
    TT_IO_NUMBER,
    /* operators */
    TT_NEWLINE,
    TT_AMP, TT_AMPAMP, TT_LPAREN, TT_RPAREN, TT_SEMICOLON, TT_DOUBLE_SEMICOLON,
    TT_PIPE, TT_PIPEPIPE, TT_LESS, TT_LESSLESS, TT_LESSAMP, TT_LESSLESSDASH,
    TT_LESSLESSLESS, TT_LESSGREATER, TT_LESSLPAREN, TT_GREATER,
    TT_GREATERGREATER, TT_GREATERGREATERPIPE, TT_GREATERPIPE, TT_GREATERAMP,
    TT_GREATERLPAREN,
    /* reserved words */
    TT_IF, TT_THEN, TT_ELSE, TT_ELIF, TT_FI, TT_DO, TT_DONE, TT_CASE, TT_ESAC,
    TT_WHILE, TT_UNTIL, TT_FOR, TT_LBRACE, TT_RBRACE, TT_BANG, TT_IN,
    TT_FUNCTION,
#if YASH_ENABLE_DOUBLE_BRACKET
    TT_DOUBLE_LBRACKET,
#endif
} tokentype_T;

static wchar_t *skip_name(const wchar_t *s, bool predicate(wchar_t))
    __attribute__((pure,nonnull));
static bool is_name_by_predicate(const wchar_t *s, bool predicate(wchar_t))
    __attribute__((pure,nonnull));
static bool is_portable_name(const wchar_t *s)
    __attribute__((pure,nonnull));
static tokentype_T identify_reserved_word_string(const wchar_t *s)
    __attribute__((pure,nonnull));
static bool is_single_string_word(const wordunit_T *wu)
    __attribute__((pure));
static bool is_digits_only(const wordunit_T *wu)
    __attribute__((pure));
static bool is_name_word(const wordunit_T *wu)
    __attribute__((pure));
static tokentype_T identify_reserved_word(const wordunit_T *wu)
    __attribute__((pure));
static bool is_closing_tokentype(tokentype_T tt)
    __attribute__((const));


/* Checks if the specified character can be used in a portable variable name.
 * Returns true for a digit. */
bool is_portable_name_char(wchar_t c)
{
    switch (c) {
	case L'0':  case L'1':  case L'2':  case L'3':  case L'4':
	case L'5':  case L'6':  case L'7':  case L'8':  case L'9':
	case L'a':  case L'b':  case L'c':  case L'd':  case L'e':  case L'f':
	case L'g':  case L'h':  case L'i':  case L'j':  case L'k':  case L'l':
	case L'm':  case L'n':  case L'o':  case L'p':  case L'q':  case L'r':
	case L's':  case L't':  case L'u':  case L'v':  case L'w':  case L'x':
	case L'y':  case L'z':
	case L'A':  case L'B':  case L'C':  case L'D':  case L'E':  case L'F':
	case L'G':  case L'H':  case L'I':  case L'J':  case L'K':  case L'L':
	case L'M':  case L'N':  case L'O':  case L'P':  case L'Q':  case L'R':
	case L'S':  case L'T':  case L'U':  case L'V':  case L'W':  case L'X':
	case L'Y':  case L'Z':  case L'_':
	    return true;
	default:
	    return false;
    }
}

/* Checks if the specified character can be used in a variable name.
 * Returns true for a digit. */
bool is_name_char(wchar_t c)
{
    return c == L'_' || iswalnum(c);
}

/* Skips an identifier at the head of the specified string and returns a
 * pointer to the character right after the identifier in the string.
 * If there is no identifier, the argument `s' is simply returned. `predicate`
 * determines if a character is valid. */
wchar_t *skip_name(const wchar_t *s, bool predicate(wchar_t))
{
    if (!iswdigit(*s))
	while (predicate(*s))
	    s++;
    return (wchar_t *) s;
}

/* Returns true iff the specified string constitutes a valid identifier that
 * is made up of characters accepted by `predicate'. */
bool is_name_by_predicate(const wchar_t *s, bool predicate(wchar_t))
{
    return s[0] != L'\0' && skip_name(s, predicate)[0] == L'\0';
}

/* Returns true iff the specified string constitutes a valid portable name. */
bool is_portable_name(const wchar_t *s)
{
    return is_name_by_predicate(s, is_portable_name_char);
}

/* Returns true iff the specified string constitutes a valid identifier. */
bool is_name(const wchar_t *s)
{
    return is_name_by_predicate(s, is_name_char);
}

/* Converts a string to the corresponding token type. Returns TT_WORD for
 * non-reserved words. */
tokentype_T identify_reserved_word_string(const wchar_t *s)
{
    /* List of keywords:
     *    case do done elif else esac fi for function if in then until while
     *    { } [[ !
     * The following words are currently not keywords:
     *    select ]] */
    switch (s[0]) {
	case L'c':
	    if (s[1] == L'a' && s[2] == L's' && s[3] == L'e' && s[4]== L'\0')
		return TT_CASE;
	    break;
	case L'd':
	    if (s[1] == L'o') {
		if (s[2] == L'\0')
		    return TT_DO;
		if (s[2] == L'n' && s[3] == L'e' && s[4] == L'\0')
		    return TT_DONE;
	    }
	    break;
	case L'e':
	    if (s[1] == L'l') {
		if (s[2] == L's' && s[3] == L'e' && s[4] == L'\0')
		    return TT_ELSE;
		if (s[2] == L'i' && s[3] == L'f' && s[4] == L'\0')
		    return TT_ELIF;
	    }
	    if (s[1] == L's' && s[2] == L'a' && s[3] == L'c' && s[4] == L'\0')
		return TT_ESAC;
	    break;
	case L'f':
	    if (s[1] == L'i' && s[2] == L'\0')
		return TT_FI;
	    if (s[1] == L'o' && s[2] == L'r' && s[3] == L'\0')
		return TT_FOR;
	    if (s[1] == L'u' && s[2] == L'n' && s[3] == L'c' && s[4] == L't' &&
		    s[5] == L'i' && s[6] == L'o' && s[7] == L'n' &&
		    s[8] == L'\0')
		return TT_FUNCTION;
	    break;
	case L'i':
	    if (s[1] == L'f' && s[2] == L'\0')
		return TT_IF;
	    if (s[1] == L'n' && s[2] == L'\0')
		return TT_IN;
	    break;
	case L't':
	    if (s[1] == L'h' && s[2] == L'e' && s[3] == L'n' && s[4]== L'\0')
		return TT_THEN;
	    break;
	case L'u':
	    if (s[1] == L'n' && s[2] == L't' && s[3] == L'i' && s[4] == L'l' &&
		    s[5] == L'\0')
		return TT_UNTIL;
	    break;
	case L'w':
	    if (s[1] == L'h' && s[2] == L'i' && s[3] == L'l' && s[4] == L'e' &&
		    s[5] == L'\0')
		return TT_WHILE;
	    break;
	case L'{':
	    if (s[1] == L'\0')
		return TT_LBRACE;
	    break;
	case L'}':
	    if (s[1] == L'\0')
		return TT_RBRACE;
	    break;
#if YASH_ENABLE_DOUBLE_BRACKET
	case L'[':
	    if (s[1] == L'[' && s[2] == L'\0')
		return TT_DOUBLE_LBRACKET;
	    break;
#endif /* YASH_ENABLE_DOUBLE_BRACKET */
	case L'!':
	    if (s[1] == L'\0')
		return TT_BANG;
	    break;
    }
    return TT_WORD;
}

/* Returns true iff the string is a reserved word. */
bool is_keyword(const wchar_t *s)
{
    return identify_reserved_word_string(s) != TT_WORD;
}

bool is_single_string_word(const wordunit_T *wu)
{
    return wu != NULL && wu->next == NULL && wu->wu_type == WT_STRING;
}

/* Tests if a word is made up of digits only. */
bool is_digits_only(const wordunit_T *wu)
{
    if (!is_single_string_word(wu))
	return false;

    const wchar_t *s = wu->wu_string;
    assert(s[0] != L'\0');
    while (iswdigit(*s))
	s++;
    return *s == L'\0';
}

bool is_name_word(const wordunit_T *wu)
{
    if (!is_single_string_word(wu))
	return false;

    return (posixly_correct ? is_portable_name : is_name)(wu->wu_string);
}

/* Converts a word to the corresponding token type. Returns TT_WORD for
 * non-reserved words. */
tokentype_T identify_reserved_word(const wordunit_T *wu)
{
    if (!is_single_string_word(wu))
	return TT_WORD;
    return identify_reserved_word_string(wu->wu_string);
}

/* Determines if the specified token is a 'closing' token such as ")", "}", and
 * "fi". Closing tokens delimit and/or lists. */
bool is_closing_tokentype(tokentype_T tt)
{
    switch (tt) {
	case TT_RPAREN:
	case TT_RBRACE:
	case TT_THEN:
	case TT_ELIF:
	case TT_ELSE:
	case TT_FI:
	case TT_DO:
	case TT_DONE:
	case TT_DOUBLE_SEMICOLON:
	case TT_ESAC:
	    return true;
	default:
	    return false;
    }
}


/********** Parser **********/

/* Holds data that are used in parsing. */
typedef struct parsestate_T {
    /* contains parameters that affect the behavior of parsing */
    parseparam_T *info;
    /* true iff any parsing error occurred */
    bool error;
    /* the source code being parsed */
    struct xwcsbuf_T src;
    /* the position of the current character or `token' */
    size_t index;
    /* the index just past the current `token' */
    size_t next_index;
    /* type of the current `token' */
    tokentype_T tokentype;
    /* the current token (NULL when `tokentype' is an operator token) */
    wordunit_T *token;
    /* here-documents whose contents have not been read */
    struct plist_T pending_heredocs;
    /* false when alias substitution is suppressed */
    bool enable_alias;
    /* true when non-global alias substitution has occurred */
    bool reparse;
    /* record of alias substitutions that are responsible for the current
     * `index' */
    struct aliaslist_T *aliases;
} parsestate_T;

static void serror(parsestate_T *restrict ps, const char *restrict format, ...)
    __attribute__((nonnull(1,2),format(printf,2,3)));
static void print_errmsg_token(parsestate_T *ps, const char *message)
    __attribute__((nonnull));
static const char *get_errmsg_unexpected_tokentype(tokentype_T tokentype)
    __attribute__((const));
static void print_errmsg_token_unexpected(parsestate_T *ps)
    __attribute__((nonnull));
static void print_errmsg_token_missing(parsestate_T *ps, const wchar_t *t)
    __attribute__((nonnull));

static inputresult_T read_more_input(parsestate_T *ps)
    __attribute__((nonnull));
static void line_continuation(parsestate_T *ps, size_t index)
    __attribute__((nonnull));
static void maybe_line_continuations(parsestate_T *ps, size_t index)
    __attribute__((nonnull));
static void rewind_index(parsestate_T *ps, size_t to)
    __attribute__((nonnull));
static size_t count_name_length(parsestate_T *ps, bool isnamechar(wchar_t c))
    __attribute__((nonnull));

static void next_token(parsestate_T *ps)
    __attribute__((nonnull));
static wordunit_T *parse_word(parsestate_T *ps, bool testfunc(wchar_t c))
    __attribute__((nonnull,malloc,warn_unused_result));
static void skip_to_next_single_quote(parsestate_T *ps)
    __attribute__((nonnull));
static wordunit_T *parse_special_word_unit(parsestate_T *ps, bool indq)
    __attribute__((nonnull,malloc,warn_unused_result));
static wordunit_T *tryparse_paramexp_raw(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static wordunit_T *parse_paramexp_in_brace(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static wordunit_T *parse_cmdsubst_in_paren(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static embedcmd_T extract_command_in_paren(parsestate_T *ps)
    __attribute__((nonnull,warn_unused_result));
static wchar_t *extract_command_in_paren_unparsed(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static wordunit_T *parse_cmdsubst_in_backquote(parsestate_T *ps, bool bsdq)
    __attribute__((nonnull,malloc,warn_unused_result));
static wordunit_T *tryparse_arith(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));

static void next_line(parsestate_T *ps)
    __attribute__((nonnull));
static bool parse_newline_list(parsestate_T *ps)
    __attribute__((nonnull));

static bool is_comma_or_closing_bracket(wchar_t c)
    __attribute__((const));
static bool is_slash_or_closing_brace(wchar_t c)
    __attribute__((const));
static bool is_closing_brace(wchar_t c)
    __attribute__((const));

static bool psubstitute_alias(parsestate_T *ps, substaliasflags_T f)
    __attribute__((nonnull));
static void psubstitute_alias_recursive(parsestate_T *ps, substaliasflags_T f)
    __attribute__((nonnull));

static and_or_T *parse_command_list(parsestate_T *ps, bool toeol)
    __attribute__((nonnull,malloc,warn_unused_result));
static and_or_T *parse_compound_list(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static and_or_T *parse_and_or_list(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static pipeline_T *parse_pipelines_in_and_or(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static pipeline_T *parse_pipeline(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *parse_commands_in_pipeline(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *parse_command(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static void **parse_simple_command_tokens(
	parsestate_T *ps, assign_T **assigns, redir_T **redirs)
    __attribute__((nonnull,malloc,warn_unused_result));
static void **parse_words(parsestate_T *ps, bool skip_newlines)
    __attribute__((nonnull,malloc,warn_unused_result));
static void parse_redirect_list(parsestate_T *ps, redir_T **lastp)
    __attribute__((nonnull));
static assign_T *tryparse_assignment(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static redir_T *tryparse_redirect(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static void validate_redir_operand(parsestate_T *ps)
    __attribute__((nonnull));
static command_T *parse_compound_command(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *parse_group(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *parse_if(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *parse_for(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *parse_while(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *parse_case(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static caseitem_T *parse_case_list(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static void **parse_case_patterns(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
#if YASH_ENABLE_DOUBLE_BRACKET
static command_T *parse_double_bracket(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static dbexp_T *parse_double_bracket_ors(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static dbexp_T *parse_double_bracket_ands(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static dbexp_T *parse_double_bracket_nots(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static dbexp_T *parse_double_bracket_primary(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static wordunit_T *parse_double_bracket_operand(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static wordunit_T *parse_double_bracket_operand_regex(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
#endif /* YASH_ENABLE_DOUBLE_BRACKET */
static command_T *parse_function(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *try_reparse_as_function(parsestate_T *ps, command_T *c)
    __attribute__((nonnull,warn_unused_result));

static void read_heredoc_contents(parsestate_T *ps, redir_T *redir)
    __attribute__((nonnull));
static void read_heredoc_contents_without_expansion(
	parsestate_T *ps, redir_T *r)
    __attribute__((nonnull));
static void read_heredoc_contents_with_expansion(parsestate_T *ps, redir_T *r)
    __attribute__((nonnull));
static bool is_end_of_heredoc_contents(
	parsestate_T *ps, const wchar_t *eoc, bool skiptab)
    __attribute__((nonnull));
static void reject_pending_heredocs(parsestate_T *ps)
    __attribute__((nonnull));
static wordunit_T **parse_string_without_quotes(
	parsestate_T *ps, bool backquote, bool stoponnewline,
	wordunit_T **lastp)
    __attribute__((nonnull));

#define QUOTES L"\"'\\"


/***** Entry points *****/

/* The functions below may return non-NULL even on error.
 * The error condition must be tested by the `error' flag of the parsestate_T
 * structure. It is set to true when `serror' is called. */
/* Every function named `parse_*' advances the current position (the `index'
 * value of the parsestate_T structure) to the index of the first character
 * that has not yet been parsed. Syntax parser functions also update the
 * current `token' and `tokentype' to the first unconsumed token, in which
 * case `index' points to the first character of the `token'. */


/* The main entry point to the parser.
 * This function reads at least one line of input and parses it.
 * All the members of `info' except `lastinputresult' must have been initialized
 * beforehand.
 * The resulting parse tree is assigned to `*resultp' if successful. If there is
 * no command in the next line or the shell was interrupted while reading input,
 * `*resultp' is assigned NULL.
 * Returns PR_OK           if successful,
 *         PR_SYNTAX_ERROR if a syntax error occurred,
 *         PR_INPUT_ERROR  if an input error occurred, or
 *         PR_EOF          if the input reached the end of file (EOF).
 * If PR_SYNTAX_ERROR or PR_INPUT_ERROR is returned, at least one error message
 * has been printed in this function.
 * Note that `*resultp' is assigned if and only if the return value is PR_OK. */
parseresult_T read_and_parse(parseparam_T *info, and_or_T **restrict resultp)
{
    parsestate_T ps = {
	.info = info,
	.error = false,
	.index = 0,
	.next_index = 0,
	.tokentype = TT_UNKNOWN,
	.token = NULL,
	.enable_alias = info->enable_alias,
	.reparse = false,
	.aliases = NULL,
    };

    if (ps.info->interactive) {
	struct input_interactive_info_T *intrinfo = ps.info->inputinfo;
	intrinfo->prompttype = 1;
    }

    ps.info->lastinputresult = INPUT_OK;
    wb_init(&ps.src);
    pl_init(&ps.pending_heredocs);

    and_or_T *r = parse_command_list(&ps, true);

    reject_pending_heredocs(&ps);

    size_t length = ps.src.length;
    wb_destroy(&ps.src);
    pl_destroy(&ps.pending_heredocs);
    destroy_aliaslist(ps.aliases);
    wordfree(ps.token);

    switch (ps.info->lastinputresult) {
	case INPUT_OK:
	case INPUT_EOF:
	    if (ps.error) {
		andorsfree(r);
		return PR_SYNTAX_ERROR;
	    } else if (length == 0) {
		andorsfree(r);
		return PR_EOF;
	    } else {
		assert(ps.index == length);
		*resultp = r;
		return PR_OK;
	    }
	case INPUT_INTERRUPTED:
	    andorsfree(r);
	    *resultp = NULL;
	    return PR_OK;
	case INPUT_ERROR:
	    andorsfree(r);
	    return PR_INPUT_ERROR;
    }
    assert(false);
}

/* Parses a string recognizing parameter expansions, command substitutions of
 * the form "$(...)" and arithmetic expansions.
 * All the members of `info' except `lastinputresult' must have been initialized
 * beforehand.
 * This function reads and parses the input to the end of file.
 * Iff successful, the result is assigned to `*resultp' and true is returned.
 * If the input is empty, NULL is assigned.
 * On error, the value of `*resultp' is undefined. */
bool parse_string(parseparam_T *info, wordunit_T **restrict resultp)
{
    parsestate_T ps = {
	.info = info,
	.error = false,
	.index = 0,
	.next_index = 0,
	.tokentype = TT_UNKNOWN,
	.token = NULL,
	.enable_alias = false,
	.reparse = false,
	.aliases = NULL,
    };
    wb_init(&ps.src);

    ps.info->lastinputresult = INPUT_OK;
    read_more_input(&ps);
    pl_init(&ps.pending_heredocs);

    resultp = parse_string_without_quotes(&ps, false, false, resultp);
    *resultp = NULL;

    wb_destroy(&ps.src);
    pl_destroy(&ps.pending_heredocs);
    assert(ps.aliases == NULL);
    //destroy_aliaslist(ps.aliases);
    wordfree(ps.token);

    if (ps.info->lastinputresult != INPUT_EOF || ps.error) {
	wordfree(*resultp);
	return false;
    } else {
	return true;
    }
}

/***** Error message utility *****/

/* Prints the specified error message to the standard error.
 * `format' is passed to `gettext' in this function.
 * `format' need not to have a trailing newline since a newline is automatically
 * appended in this function.
 * The `ps->error' flag is set to true in this function. */
void serror(parsestate_T *restrict ps, const char *restrict format, ...)
{
    va_list ap;

    if (ps->info->print_errmsg &&
	    ps->info->lastinputresult != INPUT_INTERRUPTED) {
	if (ps->info->filename != NULL)
	    fprintf(stderr, "%s:%lu: ", ps->info->filename, ps->info->lineno);
	fprintf(stderr, gt("syntax error: "));
	va_start(ap, format);
	vfprintf(stderr, gt(format), ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
    }
    ps->error = true;
}

void print_errmsg_token(parsestate_T *ps, const char *message)
{
    assert(ps->index <= ps->next_index);
    assert(ps->next_index <= ps->src.length);
    size_t length = ps->next_index - ps->index;
    wchar_t token[length + 1];
    wcsncpy(token, &ps->src.contents[ps->index], length);
    token[length] = L'\0';
    serror(ps, message, token);
}

const char *get_errmsg_unexpected_tokentype(tokentype_T tokentype)
{
    switch (tokentype) {
	case TT_RPAREN:
	    return Ngt("encountered `%ls' without a matching `('");
	case TT_RBRACE:
	    return Ngt("encountered `%ls' without a matching `{'");
	case TT_DOUBLE_SEMICOLON:
	    return Ngt("`%ls' is used outside `case'");
	case TT_BANG:
	    return Ngt("`%ls' cannot be used as a command name");
	case TT_IN:
	    return Ngt("`%ls' cannot be used as a command name");
	case TT_FI:
	    return Ngt("encountered `%ls' "
		    "without a matching `if' and/or `then'");
	case TT_THEN:
	    return Ngt("encountered `%ls' without a matching `if' or `elif'");
	case TT_DO:
	    return Ngt("encountered `%ls' "
		    "without a matching `for', `while', or `until'");
	case TT_DONE:
	    return Ngt("encountered `%ls' without a matching `do'");
	case TT_ESAC:
	    return Ngt("encountered `%ls' without a matching `case'");
	case TT_ELIF:
	case TT_ELSE:
	    return Ngt("encountered `%ls' "
			"without a matching `if' and/or `then'");
	default:
	    assert(false);
    }
}

void print_errmsg_token_unexpected(parsestate_T *ps)
{
    print_errmsg_token(ps, get_errmsg_unexpected_tokentype(ps->tokentype));
}

void print_errmsg_token_missing(parsestate_T *ps, const wchar_t *t)
{
    if (is_closing_tokentype(ps->tokentype)) {
	print_errmsg_token_unexpected(ps);
	serror(ps, Ngt("(maybe you missed `%ls'?)"), t);
    } else {
	serror(ps, Ngt("`%ls' is missing"), t);
    }
}

/***** Input buffer manipulators *****/

/* Reads the next line of input and returns the result type, which is assigned
 * to `ps->info->lastinputresult'.
 * If `ps->info->lastinputresult' is not INPUT_OK, it is simply returned
 * without reading any input.
 * If input is from an interactive terminal and `ps->error' is true, no input
 * is read and INPUT_INTERRUPTED is returned. */
inputresult_T read_more_input(parsestate_T *ps)
{
    if (ps->error && ps->info->interactive)
	return INPUT_INTERRUPTED;
    if (ps->info->lastinputresult == INPUT_OK) {
	size_t savelength = ps->src.length;
	ps->info->lastinputresult =
	    ps->info->input(&ps->src, ps->info->inputinfo);

	if (ps->info->enable_verbose && shopt_verbose)
#if YASH_ENABLE_LINEEDIT
	    if (!(le_state & LE_STATE_ACTIVE))
#endif
		fprintf(stderr, "%ls", &ps->src.contents[savelength]);
    }
    return ps->info->lastinputresult;
}

/* Removes a line continuation at the specified index in `ps->src', increments
 * line number, and reads the next line. */
void line_continuation(parsestate_T *ps, size_t index)
{
    assert(ps->src.contents[index] == L'\\' &&
	    ps->src.contents[index + 1] == L'\n');
    wb_remove(&ps->src, index, 2);
    shift_aliaslist_index(ps->aliases, index + 1, -2);
    ps->info->lineno++;
    if (ps->src.contents[index] == L'\0')
	read_more_input(ps);
}

/* Removes line continuations at the specified index.
 * The next line will be read if the removed line continuation is at the end of
 * the buffer. */
void maybe_line_continuations(parsestate_T *ps, size_t index)
{
    assert(index <= ps->src.length);
    if (index == ps->src.length)
	read_more_input(ps);
    while (ps->src.contents[index] == L'\\' &&
	    ps->src.contents[index + 1] == L'\n')
	line_continuation(ps, index);
}

/* Rewind `ps->index` to `oldindex' and decrease `ps->info->lineno' accordingly.
 * Note that `ps->next_index' is not updated in this function.
 *
 * You MUST use this function when rewinding the index in order to correctly
 * rewind the line number. The following pattern of code does not work because
 * it does not account for line continuations that have been removed from
 * `ps->src'.
 *
 *    size_t oldindex = ps->index;
 *    unsigned long oldlineno = ps->info->lineno;
 *
 *    do_something_incrementing_index_and_lineno(ps);
 *
 *    ps->index = oldindex;
 *    ps->info->lineno = oldlineno;
 * */
void rewind_index(parsestate_T *ps, size_t oldindex)
{
    while (oldindex < ps->index) {
	ps->index--;
	assert(ps->index < ps->src.length);
	if (ps->src.contents[ps->index] == L'\n')
	    ps->info->lineno--;
    }
}

/* Returns the length of the name at the current position.
 * Whether a character can be part of the name is determined by `isnamechar'.
 * This function processes line continuations and reads so many lines that the
 * variable/alias name under the current position is fully available. */
size_t count_name_length(parsestate_T *ps, bool isnamechar(wchar_t c))
{
    size_t index = ps->index;
    while (maybe_line_continuations(ps, index),
	    isnamechar(ps->src.contents[index]))
	index++;
    return index - ps->index;
}

/***** Tokenizer *****/

/* Moves to the next token, updating `index', `next_index', `tokentype', and
 * `token' of the parse state.
 * The existing `token' is freed. */
void next_token(parsestate_T *ps)
{
    wordfree(ps->token);
    ps->token = NULL;

    size_t index = ps->next_index;
    if (index == ps->src.length)
	read_more_input(ps);

skip_blanks:
    while (iswblank(ps->src.contents[index]))
	index++;

    if (ps->src.contents[index] == L'\\' &&
	    ps->src.contents[index + 1] == L'\n') {
	line_continuation(ps, index);
	goto skip_blanks;
    }

    /* skip any comment */
    if (ps->src.contents[index] == L'#')
	index += wcscspn(&ps->src.contents[index + 1], L"\n") + 1;

    size_t startindex = index;

    switch (ps->src.contents[index]) {
	case L'\0': ps->tokentype = TT_END_OF_INPUT;          break;
	case L'\n': ps->tokentype = TT_NEWLINE;      index++; break;
	case L'(':  ps->tokentype = TT_LPAREN;       index++; break;
	case L')':  ps->tokentype = TT_RPAREN;       index++; break;
	case L';':
	    maybe_line_continuations(ps, ++index);
	    if (ps->src.contents[index] == L';') {
		ps->tokentype = TT_DOUBLE_SEMICOLON;
		index++;
	    } else {
		ps->tokentype = TT_SEMICOLON;
	    }
	    break;
	case L'&':
	    maybe_line_continuations(ps, ++index);
	    if (ps->src.contents[index] == L'&') {
		ps->tokentype = TT_AMPAMP;
		index++;
	    } else {
		ps->tokentype = TT_AMP;
	    }
	    break;
	case L'|':
	    maybe_line_continuations(ps, ++index);
	    if (ps->src.contents[index] == L'|') {
		ps->tokentype = TT_PIPEPIPE;
		index++;
	    } else {
		ps->tokentype = TT_PIPE;
	    }
	    break;
	case L'<':
	    maybe_line_continuations(ps, ++index);
	    switch (ps->src.contents[index]) {
		default:   ps->tokentype = TT_LESS;                 break;
		case L'>': ps->tokentype = TT_LESSGREATER; index++; break;
		case L'(': ps->tokentype = TT_LESSLPAREN;  index++; break;
		case L'&': ps->tokentype = TT_LESSAMP;     index++; break;
		case L'<':
		    maybe_line_continuations(ps, ++index);
		    switch (ps->src.contents[index]) {
			default:
			    ps->tokentype = TT_LESSLESS;              break;
			case L'-':
			    ps->tokentype = TT_LESSLESSDASH; index++; break;
			case L'<':
			    ps->tokentype = TT_LESSLESSLESS; index++; break;
		    }
		    break;
	    }
	    break;
	case L'>':
	    maybe_line_continuations(ps, ++index);
	    switch (ps->src.contents[index]) {
		default:   ps->tokentype = TT_GREATER;                 break;
		case L'(': ps->tokentype = TT_GREATERLPAREN;  index++; break;
		case L'&': ps->tokentype = TT_GREATERAMP;     index++; break;
		case L'|': ps->tokentype = TT_GREATERPIPE;    index++; break;
		case L'>':
		    maybe_line_continuations(ps, ++index);
		    if (ps->src.contents[index] == L'|') {
			ps->tokentype = TT_GREATERGREATERPIPE;
			index++;
		    } else {
			ps->tokentype = TT_GREATERGREATER;
		    }
		    break;
	    }
	    break;

	default:
	    /* Okay, the next token seems to be a word, possibly being a
	     * reserved word or an IO_NUMBER token. */
	    ps->index = index;
	    wordunit_T *token = parse_word(ps, is_token_delimiter_char);
	    index = ps->index;

	    wordfree(ps->token);
	    ps->token = token;

	    /* Is this an IO_NUMBER token? */
	    if (ps->src.contents[index] == L'<' ||
		    ps->src.contents[index] == L'>') {
		if (is_digits_only(ps->token)) {
		    ps->tokentype = TT_IO_NUMBER;
		    break;
		}
	    }

	    /* Is this a reserved word? */
	    ps->tokentype = identify_reserved_word(ps->token);
	    break;
    }

    ps->index = startindex;
    ps->next_index = index;
}

/* Parses a word at the current position.
 * `testfunc' is a function that determines if a character is a word delimiter.
 * The parsing proceeds up to an unescaped character for which `testfunc'
 * returns false.
 * It is not an error if there is no characters to be a word, in which case
 * NULL is returned. */
wordunit_T *parse_word(parsestate_T *ps, bool testfunc(wchar_t c))
{
    wordunit_T *first = NULL, **lastp = &first;
    bool indq = false;  /* in double quotes? */
    size_t startindex = ps->index;

/* appends the substring from `startindex' to `index' as a new word unit
 * to `*lastp' */
#define MAKE_WORDUNIT_STRING                                             \
    do {                                                                 \
	size_t len = ps->index - startindex;                             \
        if (len > 0) {                                                   \
            wordunit_T *w = xmalloc(sizeof *w);                          \
            w->next = NULL;                                              \
            w->wu_type = WT_STRING;                                      \
            w->wu_string = xwcsndup(&ps->src.contents[startindex], len); \
            *lastp = w;                                                  \
            lastp = &w->next;                                            \
        }                                                                \
    } while (0)

    while (maybe_line_continuations(ps, ps->index),
	    indq || !testfunc(ps->src.contents[ps->index])) {

	switch (ps->src.contents[ps->index]) {
	case L'\0':
	    goto done;  // reached EOF
	case L'\\':
	    if (ps->src.contents[ps->index + 1] != L'\0') {
		assert(ps->src.contents[ps->index + 1] != L'\n');
		ps->index += 2;
		continue;
	    }
	    break;
	case L'\n':
	    ps->info->lineno++;
	    break;
	case L'$':
	case L'`':
	    MAKE_WORDUNIT_STRING;
	    wordunit_T *wu = parse_special_word_unit(ps, indq);
	    startindex = ps->index;
	    if (wu != NULL) {
		*lastp = wu;
		lastp = &wu->next;
		continue;
	    } else if (ps->src.contents[ps->index] == L'\0') {
		continue;
	    }
	    break;
	case L'\'':
	    if (!indq) {
		ps->index++;
		skip_to_next_single_quote(ps);
		if (ps->src.contents[ps->index] == L'\'')
		    ps->index++;
		continue;
	    }
	    break;
	case L'"':
	    indq = !indq;
	    /* falls thru! */
	default:
	    break;
	}
	ps->index++;
    }
done:
    MAKE_WORDUNIT_STRING;

    if (indq)
	serror(ps, Ngt("the double quotation is not closed"));

    return first;
}

/* Skips to the next single quote.
 * If the current position is already at a single quote, the position is not
 * moved.
 * It is an error if there is no single quote before the end of file. */
void skip_to_next_single_quote(parsestate_T *ps)
{
    for (;;) {
	switch (ps->src.contents[ps->index]) {
	case L'\'':
	    return;
	case L'\0':
	    if (read_more_input(ps) != INPUT_OK) {
		serror(ps, Ngt("the single quotation is not closed"));
		return;
	    }
	    continue;
	case L'\n':
	    ps->info->lineno++;
	    break;
	default:
	    break;
	}
	ps->index++;
    }
}

/* Parses a parameter expansion or command substitution that starts with '$' or
 * '`'. The character at the current position must be '$' or '`' when this
 * function is called and the position is advanced to right after the expansion
 * or substitution.
 * If the character at the current position is '$' but it is not an expansion,
 * the position is not moved and the return value is NULL. Otherwise, The
 * position is advanced by at least one character.
 * Between double quotes, `indq' must be true. */
wordunit_T *parse_special_word_unit(parsestate_T *ps, bool indq)
{
    switch (ps->src.contents[ps->index++]) {
    case L'$':
	maybe_line_continuations(ps, ps->index);
	switch (ps->src.contents[ps->index]) {
	case L'{':
	    return parse_paramexp_in_brace(ps);
	case L'(':
	    maybe_line_continuations(ps, ps->index + 1);
	    if (ps->src.contents[ps->index + 1] == L'(') {
		wordunit_T *wu = tryparse_arith(ps);
		if (wu != NULL)
		    return wu;
	    }
	    ps->next_index = ps->index + 1;
	    return parse_cmdsubst_in_paren(ps);
	default:
	    return tryparse_paramexp_raw(ps);
	}
    case L'`':
	return parse_cmdsubst_in_backquote(ps, indq);
    default:
	assert(false);
    }
}

/* Parses a parameter that is not enclosed by { }.
 * The current position must be at the first character of the parameter name
 * that follows L'$'. The position is advanced to right after the name.
 * If there is no parameter, the position is put back to L'$'. */
wordunit_T *tryparse_paramexp_raw(parsestate_T *ps)
{
    size_t namelen;  /* parameter name length */

    maybe_line_continuations(ps, ps->index);
    switch (ps->src.contents[ps->index]) {
	case L'@':  case L'*':  case L'#':  case L'?':
	case L'-':  case L'$':  case L'!':
	    namelen = 1;
	    goto success;
    }
    if (!is_portable_name_char(ps->src.contents[ps->index]))
	goto error;
    if (iswdigit(ps->src.contents[ps->index]))
	namelen = 1;
    else
	namelen = count_name_length(ps, is_portable_name_char);

success:;
    paramexp_T *pe = xmalloc(sizeof *pe);
    pe->pe_type = PT_NONE;
    pe->pe_name = xwcsndup(&ps->src.contents[ps->index], namelen);
    pe->pe_start = pe->pe_end = pe->pe_match = pe->pe_subst = NULL;

    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_PARAM;
    result->wu_param = pe;
    ps->index += namelen;
    return result;

error:
    ps->index--;
    assert(ps->src.contents[ps->index] == L'$');
    return NULL;
}

/* Parses a parameter expansion that starts with "${".
 * The current position must be at the opening brace L'{' when this function is
 * called and the position is advanced to the closing brace L'}'. */
wordunit_T *parse_paramexp_in_brace(parsestate_T *ps)
{
    paramexp_T *pe = xmalloc(sizeof *pe);
    pe->pe_type = 0;
    pe->pe_name = NULL;
    pe->pe_start = pe->pe_end = pe->pe_match = pe->pe_subst = NULL;

    assert(ps->src.contents[ps->index] == L'{');
    ps->index++;

    /* parse PT_NUMBER */
    maybe_line_continuations(ps, ps->index);
    if (ps->src.contents[ps->index] == L'#') {
	maybe_line_continuations(ps, ps->index + 1);
	switch (ps->src.contents[ps->index + 1]) {
	    case L'\0': case L'}':
	    case L'+':  case L'=':  case L':':  case L'/':  case L'%':
		break;
	    case L'-':  case L'?':  case L'#':
		maybe_line_continuations(ps, ps->index + 2);
		if (ps->src.contents[ps->index + 2] != L'}')
		    break;
		/* falls thru! */
	    default:
		pe->pe_type |= PT_NUMBER;
		ps->index++;
		break;
	}
    }

    /* parse nested expansion */
    // maybe_line_continuations(ps, ps->index); // already called above
    if (!posixly_correct && ps->src.contents[ps->index] == L'{') {
	pe->pe_type |= PT_NEST;
	pe->pe_nest = parse_paramexp_in_brace(ps);
    } else if (!posixly_correct
	    && (ps->src.contents[ps->index] == L'`'
		|| (ps->src.contents[ps->index] == L'$'
		    && (maybe_line_continuations(ps, ps->index + 1),
			ps->src.contents[ps->index + 1] == L'{'
			|| ps->src.contents[ps->index + 1] == L'(')))) {
	size_t neststartindex = ps->index;
	pe->pe_nest = parse_special_word_unit(ps, false);
	if (ps->index == neststartindex)
	    goto parse_name;
	pe->pe_type |= PT_NEST;
	maybe_line_continuations(ps, ps->index);
    } else {
parse_name:;
	/* no nesting: parse parameter name normally */
	size_t namestartindex = ps->index;
	switch (ps->src.contents[ps->index]) {
	    case L'@':  case L'*':  case L'#':  case L'?':
	    case L'-':  case L'$':  case L'!':
		ps->index++;
		break;
	    default:
		while (maybe_line_continuations(ps, ps->index),
			is_name_char(ps->src.contents[ps->index]))
		    ps->index++;
		break;
	}
	size_t namelen = ps->index - namestartindex;
	if (namelen == 0) {
	    serror(ps, Ngt("the parameter name is missing or invalid"));
	    goto end;
	}
	pe->pe_name = xwcsndup(&ps->src.contents[namestartindex], namelen);
    }

    /* parse indices */
    // maybe_line_continuations(ps, ps->index); // already called above
    if (!posixly_correct && ps->src.contents[ps->index] == L'[') {
	ps->index++;
	pe->pe_start = parse_word(ps, is_comma_or_closing_bracket);
	if (pe->pe_start == NULL)
	    serror(ps, Ngt("the index is missing"));
	if (ps->src.contents[ps->index] == L',') {
	    ps->index++;
	    pe->pe_end = parse_word(ps, is_comma_or_closing_bracket);
	    if (pe->pe_end == NULL)
		serror(ps, Ngt("the index is missing"));
	}
	if (ps->src.contents[ps->index] == L']') {
	    maybe_line_continuations(ps, ++ps->index);
	} else {
	    serror(ps, Ngt("`%ls' is missing"), L"]");
	}
    }

    /* parse PT_COLON */
    // maybe_line_continuations(ps, ps->index); // already called above
    if (ps->src.contents[ps->index] == L':') {
	pe->pe_type |= PT_COLON;
	maybe_line_continuations(ps, ++ps->index);
    }

    /* parse '-', '+', '#', etc. */
    // maybe_line_continuations(ps, ps->index); // already called above
    switch (ps->src.contents[ps->index]) {
    case L'-':   pe->pe_type |= PT_MINUS;                    goto parse_subst;
    case L'+':   pe->pe_type |= PT_PLUS;                     goto parse_subst;
    case L'=':   pe->pe_type |= PT_ASSIGN;                   goto parse_subst;
    case L'?':   pe->pe_type |= PT_ERROR;                    goto parse_subst;
    case L'#':   pe->pe_type |= PT_MATCH | PT_MATCHHEAD;     goto parse_match;
    case L'%':   pe->pe_type |= PT_MATCH | PT_MATCHTAIL;     goto parse_match;
    case L'/':
	if (posixly_correct)
	    serror(ps, Ngt("invalid character `%lc' in parameter expansion"),
		    (wint_t) L'/');
	pe->pe_type |= PT_SUBST | PT_MATCHLONGEST;
	goto parse_match;
    case L'\0':
    case L'\n':
    case L'}':
	pe->pe_type |= PT_NONE;
	if (pe->pe_type & PT_COLON)
	    serror(ps, Ngt("invalid use of `%lc' in parameter expansion"),
		    (wint_t) L':');
	goto check_closing_brace;
    default:
	serror(ps, Ngt("invalid character `%lc' in parameter expansion"),
		(wint_t) ps->src.contents[ps->index]);
	goto end;
    }

parse_match:
    maybe_line_continuations(ps, ps->index + 1);
    if (pe->pe_type & PT_COLON) {
	if ((pe->pe_type & PT_MASK) == PT_SUBST)
	    pe->pe_type |= PT_MATCHHEAD | PT_MATCHTAIL;
	else
	    serror(ps, Ngt("invalid use of `%lc' in parameter expansion"),
		    (wint_t) L':');
	maybe_line_continuations(ps, ++ps->index);
    } else if (ps->src.contents[ps->index] ==
	    ps->src.contents[ps->index + 1]) {
	if ((pe->pe_type & PT_MASK) == PT_MATCH)
	    pe->pe_type |= PT_MATCHLONGEST;
	else
	    pe->pe_type |= PT_SUBSTALL;
	ps->index += 2;
    } else if (ps->src.contents[ps->index] == L'/') {
	if (ps->src.contents[ps->index + 1] == L'#') {
	    pe->pe_type |= PT_MATCHHEAD;
	    ps->index += 2;
	} else if (ps->src.contents[ps->index + 1] == L'%') {
	    pe->pe_type |= PT_MATCHTAIL;
	    ps->index += 2;
	} else {
	    ps->index += 1;
	}
    } else {
	ps->index += 1;
    }
    if ((pe->pe_type & PT_MASK) == PT_MATCH) {
	pe->pe_match = parse_word(ps, is_closing_brace);
	goto check_closing_brace;
    } else {
	pe->pe_match = parse_word(ps, is_slash_or_closing_brace);
	// maybe_line_continuations(ps, ps->index); // called in parse_word
	if (ps->src.contents[ps->index] != L'/')
	    goto check_closing_brace;
    }

parse_subst:
    ps->index++;
    pe->pe_subst = parse_word(ps, is_closing_brace);

check_closing_brace:
    // maybe_line_continuations(ps, ps->index); // already called above
    if (ps->src.contents[ps->index] == L'}')
	ps->index++;
    else
	serror(ps, Ngt("`%ls' is missing"), L"}");
    if ((pe->pe_type & PT_NUMBER) && (pe->pe_type & PT_MASK) != PT_NONE)
	serror(ps, Ngt("invalid use of `%lc' in parameter expansion"),
		(wint_t) L'#');

end:;
    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_PARAM;
    result->wu_param = pe;
    return result;
}

/* Parses a command substitution that starts with "$(".
 * When this function is called, `ps->next_index' must be just after the opening
 * "(". When this function returns, `ps->index' is just after the closing ")".
 */
wordunit_T *parse_cmdsubst_in_paren(parsestate_T *ps)
{
    embedcmd_T cmd = extract_command_in_paren(ps);

    maybe_line_continuations(ps, ps->index);
    if (ps->src.contents[ps->index] == L')')
	ps->index++;
    else
	serror(ps, Ngt("`%ls' is missing"), L")");

    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_CMDSUB;
    result->wu_cmdsub = cmd;
    return result;
}

/* Extracts commands between '(' and ')'.
 * When this function is called, `ps->next_index' must be just after the opening
 * "(". When this function returns, the current token will be the closing ")".
 */
embedcmd_T extract_command_in_paren(parsestate_T *ps)
{
    plist_T save_pending_heredocs;
    embedcmd_T result;

    assert(ps->next_index > 0);
    assert(ps->src.contents[ps->next_index - 1] == L'(');

    save_pending_heredocs = ps->pending_heredocs;
    pl_init(&ps->pending_heredocs);

    if (posixly_correct && ps->info->enable_alias) {
	result.is_preparsed = false;
	result.value.unparsed = extract_command_in_paren_unparsed(ps);
    } else {
	next_token(ps);
	result.is_preparsed = true;
	result.value.preparsed = parse_compound_list(ps);
    }

    pl_destroy(&ps->pending_heredocs);
    ps->pending_heredocs = save_pending_heredocs;

    return result;
}

/* Parses commands between '(' and ')'.
 * The current token must be the opening parenthesis L'(' when this function is
 * called. The current token is advanced to the closing parenthesis L')'. */
wchar_t *extract_command_in_paren_unparsed(parsestate_T *ps)
{
    bool save_enable_alias = ps->enable_alias;
    ps->enable_alias = false;

    size_t startindex = ps->next_index;
    next_token(ps);
    andorsfree(parse_compound_list(ps));
    assert(startindex <= ps->index);

    wchar_t *result = xwcsndup(
	    &ps->src.contents[startindex], ps->index - startindex);

    ps->enable_alias = save_enable_alias;
    return result;
}

/* Parses a command substitution enclosed by backquotes.
 * When this function is called, the current position must be at the character
 * that just follows the opening backquote L'`'. This function advances the
 * position to the character that just follows the closing backquote L'`'.
 * If `bsdq' is true, backslash-escaped double-quotes are unquoted; otherwise,
 * they are left intact. */
wordunit_T *parse_cmdsubst_in_backquote(parsestate_T *ps, bool bsdq)
{
    assert(ps->src.contents[ps->index - 1] == L'`');

    xwcsbuf_T buf;
    wb_init(&buf);
    for (;;) {
	maybe_line_continuations(ps, ps->index);
	switch (ps->src.contents[ps->index]) {
	case L'\0':
	    serror(ps,
		    Ngt("the backquoted command substitution is not closed"));
	    goto end;
	case L'`':
	    ps->index++;
	    goto end;
	case L'\\':
	    ps->index++;
	    switch (ps->src.contents[ps->index]) {
		case L'$':  case L'`':  case L'\\':
		    goto default_;
		case L'"':
		    if (bsdq)
			goto default_;
		    /* falls thru! */
		default:
		    wb_wccat(&buf, L'\\');
		    continue;
	    }
	case L'\n':
	    ps->info->lineno++;
	    /* falls thru! */
	default:  default_:
	    wb_wccat(&buf, ps->src.contents[ps->index]);
	    ps->index++;
	    break;
	}
    }
end:;
    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_CMDSUB;
    result->wu_cmdsub.is_preparsed = false;
    result->wu_cmdsub.value.unparsed = wb_towcs(&buf);
    return result;
}

/* Parses an arithmetic expansion.
 * The current position must be at the first opening parenthesis L'(' when this
 * function is called and the position is advanced to the character that just
 * follows the last closing parenthesis L')'. If there is no arithmetic
 * expansion, the return value is NULL and the position is not moved. */
wordunit_T *tryparse_arith(parsestate_T *ps)
{
    size_t saveindex = ps->index;
    assert(ps->src.contents[ps->index] == L'(' &&
	    ps->src.contents[ps->index + 1] == L'(');
    ps->index += 2;

    wordunit_T *first = NULL, **lastp = &first;
    size_t startindex = ps->index;
    int nestparen = 0;

    for (;;) {
	maybe_line_continuations(ps, ps->index);
	switch (ps->src.contents[ps->index]) {
	case L'\0':
	    serror(ps, Ngt("`%ls' is missing"), L"))");
	    goto end;
	case L'\\':
	    if (ps->src.contents[ps->index + 1] != L'\0') {
		assert(ps->src.contents[ps->index + 1] != L'\n');
		ps->index += 2;
		continue;
	    }
	    break;
	case L'\n':
	    ps->info->lineno++;
	    break;
	case L'$':
	case L'`':
	    MAKE_WORDUNIT_STRING;
	    wordunit_T *wu = parse_special_word_unit(ps, false);
	    startindex = ps->index;
	    if (wu != NULL) {
		*lastp = wu;
		lastp = &wu->next;
		continue;
	    } else if (ps->src.contents[ps->index] == L'\0') {
		continue;
	    }
	    break;
	case L'(':
	    nestparen++;
	    break;
	case L')':
	    nestparen--;
	    if (nestparen >= 0)
		break;
	    maybe_line_continuations(ps, ps->index + 1);
	    switch (ps->src.contents[ps->index + 1]) {
		case L')':
		    MAKE_WORDUNIT_STRING;
		    ps->index += 2;
		    goto end;
		case L'\0':
		    serror(ps, Ngt("`%ls' is missing"), L")");
		    goto end;
		default:
		    goto not_arithmetic_expansion;
	    }
	default:
	    break;
	}
	ps->index++;
    }
end:;
    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_ARITH;
    result->wu_arith = first;
    return result;

not_arithmetic_expansion:
    wordfree(first);
    rewind_index(ps, saveindex);
    return NULL;
}

/***** Newline token parser *****/

/* Parses the newline token at the current position and proceeds to the next
 * line. The contents of pending here-documents are read if any. The current
 * token is cleared. */
void next_line(parsestate_T *ps)
{
    assert(ps->src.contents[ps->index] == L'\n');
    ps->index++;
    ps->info->lineno++;

    for (size_t i = 0; i < ps->pending_heredocs.length; i++)
	read_heredoc_contents(ps, ps->pending_heredocs.contents[i]);
    pl_truncate(&ps->pending_heredocs, 0);

    wordfree(ps->token);
    ps->token = NULL;
    ps->tokentype = TT_UNKNOWN;
    ps->next_index = ps->index;
}

/* Processes a sequence of newline tokens. Returns true if at least one newline
 * token has been processed; false if none. */
bool parse_newline_list(parsestate_T *ps)
{
    bool found = false;
    while (ps->tokentype == TT_NEWLINE) {
	found = true;
	next_line(ps);
	next_token(ps);
    }
    return found;
}

/***** Character classifiers *****/

/* Checks if the specified character is a token separator. */
bool is_token_delimiter_char(wchar_t c)
{
    switch (c) {
	case L'\0':  case L'\n':  case L';':   case L'&':   case L'|':
	case L'<':   case L'>':   case L'(':   case L')':
	    return true;
	default:
	    return iswblank(c);
    }
}

bool is_comma_or_closing_bracket(wchar_t c)
{
    return c == L']' || c == L',';
}

bool is_slash_or_closing_brace(wchar_t c)
{
    return c == L'/' || c == L'}';
}

bool is_closing_brace(wchar_t c)
{
    return c == L'}';
}

/***** Aliases *****/

/* Performs alias substitution with the given parse state. Proceeds to the
 * next token if substitution occurred. This function does not substitute an
 * IO_NUMBER token, but do a keyword token. */
bool psubstitute_alias(parsestate_T *ps, substaliasflags_T flags)
{
    if (!ps->enable_alias)
	return false;
    if (ps->tokentype == TT_IO_NUMBER)
	return false;
    if (!is_single_string_word(ps->token))
	return false;

    bool substituted = substitute_alias_range(
	    &ps->src, ps->index, ps->next_index, &ps->aliases, flags);
    if (substituted) {
	/* parse the result of the substitution. */
	ps->next_index = ps->index;
	next_token(ps);
    }
    return substituted;
}

/* Performs alias substitution recursively. This should not be used where the
 * substitution result may be recognized as a keyword, since keywords should not
 * be alias-substituted. */
void psubstitute_alias_recursive(parsestate_T *ps, substaliasflags_T flags)
{
    while (psubstitute_alias(ps, flags)) ;
}

/***** Syntax parser functions *****/

/* Parses commands.
 * If `toeol' is true, commands are parsed up to the end of the current input;
 * otherwise, up to the next closing token. */
and_or_T *parse_command_list(parsestate_T *ps, bool toeol)
{
    and_or_T *first = NULL, **lastp = &first;
    bool saveerror = ps->error;
    bool need_separator = false;
    /* For a command to be parsed after another, it must be separated by L"&",
     * L";", or newlines. */

    if (!toeol && !ps->info->interactive)
	ps->error = false;
    if (ps->tokentype == TT_UNKNOWN)
	next_token(ps);

    while (!ps->error) {
	if (toeol) {
	    if (ps->tokentype == TT_NEWLINE) {
		next_line(ps);
		need_separator = false;
		if (ps->next_index != ps->src.length) {
		    next_token(ps);
		    continue;
		}
		wordfree(ps->token);
		ps->token = NULL;
		ps->index = ps->next_index;
		ps->tokentype = TT_END_OF_INPUT;
	    }
	    if (ps->tokentype == TT_END_OF_INPUT) {
		break;
	    } else if (need_separator) {
		if (is_closing_tokentype(ps->tokentype))
		    print_errmsg_token_unexpected(ps);
		else
		    serror(ps, Ngt("`;' or `&' is missing"));
		break;
	    }
	} else {
	    if (parse_newline_list(ps))
		need_separator = false;
	    if (need_separator || ps->tokentype == TT_END_OF_INPUT ||
		    is_closing_tokentype(ps->tokentype))
		break;
	}

	and_or_T *ao = parse_and_or_list(ps);
	if (ao != NULL) {
	    *lastp = ao;
	    lastp = &ao->next;
	}
	if (ps->reparse) {
	    ps->reparse = false;
	    assert(ao == NULL);
	    continue;
	}

	if (ps->tokentype != TT_AMP && ps->tokentype != TT_SEMICOLON) {
	    need_separator = true;
	} else {
	    need_separator = false;
	    next_token(ps);
	}
    }
    if (!toeol)
	ps->error |= saveerror;
    return first;
}

/* Parses commands until a closing token is found. */
and_or_T *parse_compound_list(parsestate_T *ps)
{
    return parse_command_list(ps, false);
}

/* Parses one and/or list.
 * The result reflects the trailing "&" or ";", but `ps->index' points to the
 * delimiter "&" or ";" when the function returns.
 * If the first word was alias-substituted, the `ps->reparse' flag is set and
 * NULL is returned. */
and_or_T *parse_and_or_list(parsestate_T *ps)
{
    pipeline_T *p = parse_pipelines_in_and_or(ps);
    if (ps->reparse) {
	assert(p == NULL);
	return NULL;
    }

    and_or_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->ao_pipelines = p;
    result->ao_async = (ps->tokentype == TT_AMP);
    return result;
}

/* Parses all pipelines in one and/or list.
 * If the first word was alias-substituted, the `ps->reparse' flag is set and
 * NULL is returned. */
pipeline_T *parse_pipelines_in_and_or(parsestate_T *ps)
{
    pipeline_T *first = NULL, **lastp = &first;
    bool cond = false;

    for (;;) {
	pipeline_T *p = parse_pipeline(ps);
	if (p != NULL) {
	    p->pl_cond = cond;
	    *lastp = p;
	    lastp = &p->next;
	}
	if (ps->reparse) {
	    assert(p == NULL);
	    if (first != NULL) {
		ps->reparse = false;
		goto next;
	    } else {
		break;
	    }
	}

	if (ps->tokentype == TT_AMPAMP)
	    cond = true;
	else if (ps->tokentype == TT_PIPEPIPE)
	    cond = false;
	else
	    break;
	next_token(ps);
next:
	parse_newline_list(ps);
    }
    return first;
}

/* Parses one pipeline.
 * If the first word was alias-substituted, the `ps->reparse' flag is set and
 * NULL is returned. */
pipeline_T *parse_pipeline(parsestate_T *ps)
{
    bool neg;
    command_T *c;

    if (ps->tokentype == TT_BANG) {
	neg = true;
	if (posixly_correct && ps->src.contents[ps->next_index] == L'(')
	    serror(ps, Ngt("ksh-like extended glob pattern `!(...)' "
			"is not supported"));
	next_token(ps);

parse_after_bang:
	c = parse_commands_in_pipeline(ps);
	if (ps->reparse) {
	    ps->reparse = false;
	    assert(c == NULL);
	    goto parse_after_bang;
	}
    } else {
	neg = false;
	c = parse_commands_in_pipeline(ps);
	if (ps->reparse) {
	    assert(c == NULL);
	    return NULL;
	}
    }

    pipeline_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->pl_commands = c;
    result->pl_neg = neg;
    result->pl_cond = false;
    return result;
}

/* Parses the body of the pipeline.
 * If the first word was alias-substituted, the `ps->reparse' flag is set and
 * NULL is returned. */
command_T *parse_commands_in_pipeline(parsestate_T *ps)
{
    command_T *first = NULL, **lastp = &first;

    for (;;) {
	command_T *c = parse_command(ps);
	if (c != NULL) {
	    *lastp = c;
	    lastp = &c->next;
	}
	if (ps->reparse) {
	    assert(c == NULL);
	    if (first != NULL) {
		ps->reparse = false;
		goto next;
	    } else {
		break;
	    }
	}

	if (ps->tokentype != TT_PIPE)
	    break;

	next_token(ps);
next:
	parse_newline_list(ps);
    }
    return first;
}

/* Parses one command.
 * If the first word was alias-substituted, the `ps->reparse' flag is set and
 * NULL is returned. */
command_T *parse_command(parsestate_T *ps)
{
    if (ps->tokentype == TT_BANG || ps->tokentype == TT_IN ||
	    is_closing_tokentype(ps->tokentype)) {
	print_errmsg_token_unexpected(ps);
	return NULL;
    }

    command_T *result = parse_compound_command(ps);
    if (result != NULL)
	return result;

    if (psubstitute_alias(ps, AF_NONGLOBAL)) {
	/* After alias substitution, we need to re-parse the new current token,
	 * which may be consumed by the caller of this function. We set the
	 * `reparse' flag to tell the caller to re-parse the token. The caller
	 * will reset the flag when staring re-parsing. */
	ps->reparse = true;
	return NULL;
    }

    /* parse as a simple command */
    result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_lineno = ps->info->lineno;
    result->c_type = CT_SIMPLE;
    result->c_assigns = NULL;
    result->c_redirs = NULL;
    result->c_words = parse_simple_command_tokens(
	    ps, &result->c_assigns, &result->c_redirs);

    if (result->c_words[0] == NULL && result->c_assigns == NULL &&
	    result->c_redirs == NULL) {
	/* an empty command */
	comsfree(result);
	if (ps->tokentype == TT_END_OF_INPUT || ps->tokentype == TT_NEWLINE)
	    serror(ps, Ngt("a command is missing at the end of input"));
	else
	    serror(ps, Ngt("a command is missing before `%lc'"),
		    (wint_t) ps->src.contents[ps->index]);
	return NULL;
    }

    return try_reparse_as_function(ps, result);
}

/* Parses assignments, redirections, and words.
 * Assignments are parsed before words are parsed. Tokens are subject to
 * any-type alias substitution until the first word is parsed, except for the
 * first token which is parsed intact. The other words are subject to global
 * alias substitution. Redirections can appear anywhere.
 * Parsed Assignments and redirections are assigned to `*assigns' and `redirs',
 * respectively. They must have been initialized NULL (or anything) before
 * calling this function. Parsed words are returned as a newly-malloced
 * NULL-terminated array of pointers to newly-malloced wordunit_T's. */
void **parse_simple_command_tokens(
	parsestate_T *ps, assign_T **assigns, redir_T **redirs)
{
    bool is_first = true;
    plist_T words;
    pl_init(&words);

next:
    if (is_first)
	is_first = false;
    else
	psubstitute_alias_recursive(ps, words.length == 0 ? AF_NONGLOBAL : 0);

    redir_T *redir = tryparse_redirect(ps);
    if (redir != NULL) {
	*redirs = redir;
	redirs = &redir->next;
	goto next;
    }

    if (words.length == 0) {
	assign_T *assign = tryparse_assignment(ps);
	if (assign != NULL) {
	    *assigns = assign;
	    assigns = &assign->next;
	    goto next;
	}
    }

    if (ps->token != NULL) {
	pl_add(&words, ps->token), ps->token = NULL;
	next_token(ps);
	goto next;
    }

    return pl_toary(&words);
}

/* Parses words.
 * The resultant words are returned as a newly-malloced NULL-terminated array of
 * pointers to word units that are cast to (void *).
 * All words are subject to global alias substitution.
 * If `skip_newlines' is true, newline operators are skipped.
 * Words are parsed until an operator token is found. */
void **parse_words(parsestate_T *ps, bool skip_newlines)
{
    plist_T wordlist;

    pl_init(&wordlist);
    for (;;) {
	psubstitute_alias_recursive(ps, 0);
	if (skip_newlines && parse_newline_list(ps))
	    continue;
	if (ps->token == NULL)
	    break;
	pl_add(&wordlist, ps->token), ps->token = NULL;
	next_token(ps);
    }
    return pl_toary(&wordlist);
}

/* Parses as many redirections as possible.
 * The parsing result is assigned to `*redirlastp'
 * `*redirlastp' must have been initialized to NULL beforehand. */
void parse_redirect_list(parsestate_T *ps, redir_T **lastp)
{
    for (;;) {
	psubstitute_alias_recursive(ps, 0);

	redir_T *redir = tryparse_redirect(ps);
	if (redir == NULL)
	    break;
	*lastp = redir;
	lastp = &redir->next;
    }
}

/* Re-parses the current token as an assignment word. If successful, the token
 * is consumed and the assignment is returned. For an array assignment, all
 * tokens up to (and including) the closing parenthesis are consumed. If
 * unsuccessful, the current token is not modified and NULL is returned. */
assign_T *tryparse_assignment(parsestate_T *ps)
{
    if (ps->token == NULL)
	return NULL;
    if (ps->token->wu_type != WT_STRING)
	return NULL;

    const wchar_t *nameend = skip_name(ps->token->wu_string, is_name_char);
    size_t namelen = nameend - ps->token->wu_string;
    if (namelen == 0 || *nameend != L'=')
	return NULL;

    assign_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->a_name = xwcsndup(ps->token->wu_string, namelen);

    /* remove the name and '=' from the token */
    size_t index_after_first_token = ps->next_index;
    wordunit_T *first_token = ps->token;
    ps->token = NULL;
    wmemmove(first_token->wu_string, &nameend[1], wcslen(&nameend[1]) + 1);
    if (first_token->wu_string[0] == L'\0') {
	wordunit_T *wu = first_token->next;
	wordunitfree(first_token);
	first_token = wu;
    }

    next_token(ps);

    if (posixly_correct || first_token != NULL ||
	    ps->index != index_after_first_token ||
	    ps->tokentype != TT_LPAREN) {
	/* scalar assignment */
	result->a_type = A_SCALAR;
	result->a_scalar = first_token;
    } else {
	/* array assignment */
	next_token(ps);
	result->a_type = A_ARRAY;
	result->a_array = parse_words(ps, true);
	if (ps->tokentype == TT_RPAREN)
	    next_token(ps);
	else
	    serror(ps, Ngt("`%ls' is missing"), L")");
    }
    return result;
}

/* If there is a redirection at the current position, parses and returns it.
 * Otherwise, returns NULL without moving the position. */
redir_T *tryparse_redirect(parsestate_T *ps)
{
    int fd;

    if (ps->tokentype == TT_IO_NUMBER) {
	unsigned long lfd;
	wchar_t *endptr;

	assert(ps->token != NULL);
	assert(ps->token->wu_type == WT_STRING);
	assert(ps->token->next == NULL);
	errno = 0;
	lfd = wcstoul(ps->token->wu_string, &endptr, 10);
	if (errno != 0 || lfd > INT_MAX)
	    fd = -1;  /* invalid fd */
	else
	    fd = (int) lfd;
	assert(*endptr == L'\0');
	next_token(ps);
    } else if (ps->src.contents[ps->index] == L'<') {
	fd = STDIN_FILENO;
    } else if (ps->src.contents[ps->index] == L'>') {
	fd = STDOUT_FILENO;
    } else {
	return NULL;
    }

    redir_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->rd_fd = fd;
    switch (ps->tokentype) {
	case TT_LESS:
	    result->rd_type = RT_INPUT;
	    break;
	case TT_LESSGREATER:
	    result->rd_type = RT_INOUT;
	    break;
	case TT_LESSAMP:
	    result->rd_type = RT_DUPIN;
	    break;
	case TT_GREATER:
	    result->rd_type = RT_OUTPUT;
	    break;
	case TT_GREATERGREATER:
	    result->rd_type = RT_APPEND;
	    break;
	case TT_GREATERPIPE:
	    result->rd_type = RT_CLOBBER;
	    break;
	case TT_GREATERAMP:
	    result->rd_type = RT_DUPOUT;
	    break;
	case TT_GREATERGREATERPIPE:
	    if (posixly_correct)
		serror(ps, Ngt("pipe redirection is not supported "
			    "in the POSIXly-correct mode"));
	    result->rd_type = RT_PIPE;
	    break;
	case TT_LESSLPAREN:
	    result->rd_type = RT_PROCIN;
	    goto parse_command;
	case TT_GREATERLPAREN:
	    result->rd_type = RT_PROCOUT;
	    goto parse_command;
	case TT_LESSLESS:
	    result->rd_type = RT_HERE;
	    goto parse_here_document_tag;
	case TT_LESSLESSDASH:
	    result->rd_type = RT_HERERT;
	    goto parse_here_document_tag;
	case TT_LESSLESSLESS:
	    if (posixly_correct)
		serror(ps, Ngt("here-string is not supported "
			    "in the POSIXly-correct mode"));
	    result->rd_type = RT_HERESTR;
	    break;
	default:
	    assert(false);
    }

    /* parse redirection target file token */
    next_token(ps);
    validate_redir_operand(ps);
    result->rd_filename = ps->token, ps->token = NULL;
    if (result->rd_filename != NULL)
	next_token(ps);
    else
	serror(ps, Ngt("the redirection target is missing"));
    return result;

parse_here_document_tag:
    next_token(ps);
    validate_redir_operand(ps);
    result->rd_hereend =
	xwcsndup(&ps->src.contents[ps->index], ps->next_index - ps->index);
    result->rd_herecontent = NULL;
    if (ps->token == NULL) {
	serror(ps, Ngt("the end-of-here-document indicator is missing"));
    } else {
	pl_add(&ps->pending_heredocs, result);
	next_token(ps);
    }
    return result;

parse_command:
    if (posixly_correct)
	serror(ps, Ngt("process redirection is not supported "
		    "in the POSIXly-correct mode"));
    result->rd_command = extract_command_in_paren(ps);
    if (ps->tokentype == TT_RPAREN)
	next_token(ps);
    else
	serror(ps, Ngt("unclosed process redirection"));
    return result;
}

/* Performs alias substitution on the current token.
 * Rejects the current token if it is an IO_NUMBER token. */
void validate_redir_operand(parsestate_T *ps)
{
    do {
	if (posixly_correct && ps->tokentype == TT_IO_NUMBER) {
	    assert(ps->next_index > 0);
	    serror(ps, Ngt("put a space between `%lc' and `%lc' "
			"for disambiguation"),
		    ps->src.contents[ps->next_index - 1],
		    ps->src.contents[ps->next_index]);
	}
    } while (psubstitute_alias(ps, 0));
}

/* Parses a compound command.
 * `command' is the name of the command to parse such as "(" and "if".
 * Returns NULL iff the current token does not start a compound command. */
command_T *parse_compound_command(parsestate_T *ps)
{
    command_T *result;
    switch (ps->tokentype) {
    case TT_LPAREN:
    case TT_LBRACE:
	result = parse_group(ps);
	break;
    case TT_IF:
	result = parse_if(ps);
	break;
    case TT_FOR:
	result = parse_for(ps);
	break;
    case TT_FUNCTION:
	result = parse_function(ps);
	break;
    case TT_WHILE:
    case TT_UNTIL:
	result = parse_while(ps);
	break;
    case TT_CASE:
	result = parse_case(ps);
	break;
#if YASH_ENABLE_DOUBLE_BRACKET
    case TT_DOUBLE_LBRACKET:
	result = parse_double_bracket(ps);
	break;
#endif
    default:
	return NULL;
    }

    assert(result->c_redirs == NULL);
    parse_redirect_list(ps, &result->c_redirs);
    if (posixly_correct && result->c_redirs != NULL && ps->token != NULL &&
	    ps->tokentype != TT_WORD) {
	/* A token that follows a redirection cannot be a keyword because
	 * none of the rules in POSIX XCU 2.4 apply. This means
	 *   { { echo; } }
	 * and
	 *   { { echo; } >/dev/null; }
	 * are OK but
	 *   { { echo; } >/dev/null }
	 * is not. */
	serror(ps, Ngt("unexpected word after redirection"));
	serror(ps, Ngt("(maybe you missed `%ls'?)"), L";");
    }

    return result;
}

/* Parses a command group.
 * The current token must be the starting "(" or "{". Never returns NULL. */
command_T *parse_group(parsestate_T *ps)
{
    commandtype_T type;
    tokentype_T endtt;
    const wchar_t *starts, *ends;

    switch (ps->tokentype) {
	case TT_LBRACE:
	    type = CT_GROUP;
	    endtt = TT_RBRACE;
	    starts = L"{", ends = L"}";
	    break;
	case TT_LPAREN:
	    type = CT_SUBSHELL;
	    endtt = TT_RPAREN;
	    starts = L"(", ends = L")";
	    break;
	default:
	    assert(false);
    }
    next_token(ps);

    unsigned long lineno = ps->info->lineno;
    and_or_T *cmd = parse_compound_list(ps);

    if (posixly_correct && cmd == NULL)
	serror(ps, Ngt("commands are missing between `%ls' and `%ls'"),
		starts, ends);
    if (ps->tokentype == endtt)
	next_token(ps);
    else
	print_errmsg_token_missing(ps, ends);

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = type;
    result->c_lineno = lineno;
    result->c_redirs = NULL;
    result->c_subcmds = cmd;
    return result;
}

/* Parses an if command.
 * The current token must be the starting "if". Never returns NULL. */
command_T *parse_if(parsestate_T *ps)
{
    assert(ps->tokentype == TT_IF);
    next_token(ps);

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_IF;
    result->c_lineno = ps->info->lineno;
    result->c_redirs = NULL;
    result->c_ifcmds = NULL;

    ifcommand_T **lastp = &result->c_ifcmds;
    bool after_else = false;
    while (!ps->error) {
	ifcommand_T *ic = xmalloc(sizeof *ic);
	*lastp = ic;
	lastp = &ic->next;
	ic->next = NULL;
	if (!after_else) {
	    ic->ic_condition = parse_compound_list(ps);
	    if (posixly_correct && ic->ic_condition == NULL)
		serror(ps, Ngt("commands are missing between `%ls' and `%ls'"),
			(result->c_ifcmds->next == NULL) ? L"if" : L"elif",
			L"then");
	    if (ps->tokentype == TT_THEN)
		next_token(ps);
	    else
		print_errmsg_token_missing(ps, L"then");
	} else {
	    ic->ic_condition = NULL;
	}
	ic->ic_commands = parse_compound_list(ps);
	if (posixly_correct && ic->ic_commands == NULL)
	    serror(ps, Ngt("commands are missing after `%ls'"),
		    after_else ? L"else" : L"then");
	if (!after_else && ps->tokentype == TT_ELSE) {
	    next_token(ps);
	    after_else = true;
	} else if (!after_else && ps->tokentype == TT_ELIF) {
	    next_token(ps);
	} else if (ps->tokentype == TT_FI) {
	    next_token(ps);
	    break;
	} else {
	    print_errmsg_token_missing(ps, L"fi");
	    break;
	}
    }
    return result;
}

/* Parses a for command.
 * The current token must be the starting "for". Never returns NULL. */
command_T *parse_for(parsestate_T *ps)
{
    assert(ps->tokentype == TT_FOR);
    next_token(ps);
    psubstitute_alias_recursive(ps, 0);

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_FOR;
    result->c_lineno = ps->info->lineno;
    result->c_redirs = NULL;

    result->c_forname =
	xwcsndup(&ps->src.contents[ps->index], ps->next_index - ps->index);
    if (!is_name_word(ps->token)) {
	if (ps->token == NULL)
	    serror(ps, Ngt("an identifier is required after `for'"));
	else
	    serror(ps, Ngt("`%ls' is not a valid identifier"),
		    result->c_forname);
    }
    next_token(ps);

    bool on_next_line = false;
parse_in:
    on_next_line |= parse_newline_list(ps);
    if (ps->tokentype == TT_IN) {
	next_token(ps);
	result->c_forwords = parse_words(ps, false);
	if (ps->tokentype == TT_SEMICOLON)
	    next_token(ps);
    } else if (ps->tokentype != TT_DO && psubstitute_alias(ps, 0)) {
	goto parse_in;
    } else {
	result->c_forwords = NULL;
	if (ps->tokentype == TT_SEMICOLON) {
	    next_token(ps);
	    if (on_next_line)
		serror(ps, Ngt("`;' cannot appear on a new line"));
	}
    }

parse_do:
    parse_newline_list(ps);
    if (ps->tokentype == TT_DO)
	next_token(ps);
    else if (psubstitute_alias(ps, 0))
	goto parse_do;
    else
	serror(ps, Ngt("`%ls' is missing"), L"do");
	// print_errmsg_token_missing(ps, L"do");

    result->c_forcmds = parse_compound_list(ps);
    if (posixly_correct && result->c_forcmds == NULL)
	serror(ps, Ngt("commands are missing between `%ls' and `%ls'"),
		L"do", L"done");

    if (ps->tokentype == TT_DONE)
	next_token(ps);
    else
	print_errmsg_token_missing(ps, L"done");

    return result;
}

/* Parses a while/until command.
 * The current token must be the starting "while" or "until". Never returns
 * NULL. */
command_T *parse_while(parsestate_T *ps)
{
    bool whltype;
    switch (ps->tokentype) {
	case TT_WHILE:  whltype = true;   break;
	case TT_UNTIL:  whltype = false;  break;
	default:        assert(false);
    }
    next_token(ps);

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_WHILE;
    result->c_lineno = ps->info->lineno;
    result->c_redirs = NULL;
    result->c_whltype = whltype;

    result->c_whlcond = parse_compound_list(ps);
    if (posixly_correct && result->c_whlcond == NULL)
	serror(ps, Ngt("commands are missing after `%ls'"),
		whltype ? L"while" : L"until");

    if (ps->tokentype == TT_DO)
	next_token(ps);
    else
	print_errmsg_token_missing(ps, L"do");

    result->c_whlcmds = parse_compound_list(ps);
    if (posixly_correct && result->c_whlcmds == NULL)
	serror(ps, Ngt("commands are missing between `%ls' and `%ls'"),
		L"do", L"done");

    if (ps->tokentype == TT_DONE)
	next_token(ps);
    else
	print_errmsg_token_missing(ps, L"done");

    return result;
}

/* Parses a case command.
 * The current token must be the starting "case". Never returns NULL. */
command_T *parse_case(parsestate_T *ps)
{
    assert(ps->tokentype == TT_CASE);
    next_token(ps);
    psubstitute_alias_recursive(ps, 0);

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_CASE;
    result->c_lineno = ps->info->lineno;
    result->c_redirs = NULL;
    result->c_casword = ps->token, ps->token = NULL;
    if (result->c_casword != NULL)
	next_token(ps);
    else
	serror(ps, Ngt("a word is required after `%ls'"), L"case");

parse_in:
    parse_newline_list(ps);
    if (ps->tokentype == TT_IN) {
	next_token(ps);
	result->c_casitems = parse_case_list(ps);
    } else if (psubstitute_alias(ps, 0)) {
	goto parse_in;
    } else {
	serror(ps, Ngt("`%ls' is missing"), L"in");
	// print_errmsg_token_missing(ps, L"in");
	result->c_casitems = NULL;
    }

    if (ps->tokentype == TT_ESAC)
	next_token(ps);
    else
	print_errmsg_token_missing(ps, L"esac");

    return result;
}

/* Parses the body of a case command (the part between "in" and "esac"). */
caseitem_T *parse_case_list(parsestate_T *ps)
{
    caseitem_T *first = NULL, **lastp = &first;

    do {
	parse_newline_list(ps);
	if (ps->tokentype == TT_ESAC)
	    break;
	if (psubstitute_alias(ps, 0))
	    continue;

	caseitem_T *ci = xmalloc(sizeof *ci);
	*lastp = ci;
	lastp = &ci->next;
	ci->next = NULL;
	ci->ci_patterns = parse_case_patterns(ps);
	ci->ci_commands = parse_compound_list(ps);
	/* `ci_commands' may be NULL unlike for and while commands */
	if (ps->tokentype == TT_DOUBLE_SEMICOLON)
	    next_token(ps);
	else
	    break;
    } while (!ps->error);
    return first;
}

/* Parses patterns of a case item.
 * This function consumes the closing ")".
 * Perform alias substitution before calling this function. */
void **parse_case_patterns(parsestate_T *ps)
{
    plist_T wordlist;
    pl_init(&wordlist);

    if (ps->tokentype == TT_LPAREN) {  /* ignore the first '(' */
	next_token(ps);
	do {
	    if (posixly_correct && ps->tokentype == TT_ESAC)
		serror(ps,
		    Ngt("an unquoted `esac' cannot be the first case pattern"));
	} while (psubstitute_alias(ps, 0));
    }

    const wchar_t *predecessor = L"(";
    do {
	if (ps->token == NULL) {
	    if (ps->tokentype == TT_END_OF_INPUT) {
		// serror(ps, ...);
	    } else if (ps->tokentype == TT_NEWLINE) {
		serror(ps, Ngt("a word is required after `%ls'"),
			predecessor);
	    } else {
		serror(ps, Ngt("encountered an invalid character `%lc' "
			    "in the case pattern"),
			(wint_t) ps->src.contents[ps->index]);
	    }
	    break;
	}
	pl_add(&wordlist, ps->token), ps->token = NULL;

	next_token(ps);
	psubstitute_alias_recursive(ps, 0);
	if (ps->tokentype != TT_PIPE) {
	    if (ps->tokentype == TT_RPAREN)
		next_token(ps);
	    else
		serror(ps, Ngt("`%ls' is missing"), L")");
	    break;
	}
	predecessor = L"|";
	next_token(ps);
	psubstitute_alias_recursive(ps, 0);
    } while (!ps->error);

    return pl_toary(&wordlist);
}

#if YASH_ENABLE_DOUBLE_BRACKET

/* Parses a double-bracket command.
 * The current token must be the starting "[[".
 * Never returns NULL. The resultant `c_dbexp' may contain NULL sub-expression
 * in case of syntax error. */
command_T *parse_double_bracket(parsestate_T *ps)
{
    if (posixly_correct)
	serror(ps, Ngt("The [[ ... ]] syntax is not supported "
		    "in the POSIXly-correct mode"));

    assert(ps->tokentype == TT_DOUBLE_LBRACKET);
    next_token(ps);
    psubstitute_alias_recursive(ps, 0);

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_BRACKET;
    result->c_lineno = ps->info->lineno;
    result->c_redirs = NULL;
    result->c_dbexp = parse_double_bracket_ors(ps);

    if (is_single_string_word(ps->token) &&
	    wcscmp(ps->token->wu_string, L"]]") == 0) {
	next_token(ps);
	psubstitute_alias_recursive(ps, 0);
    } else if (!ps->error) {
	if (ps->tokentype == TT_NEWLINE)
	    serror(ps, Ngt("`%ls' is missing"), L"]]");
	else
	    print_errmsg_token(ps,
		    Ngt("invalid word `%ls' between `[[' and `]]'"));
    }

    return result;
}

/* Parses one or more "and" expressions separated by "||"s in the double-bracket
 * command. May return NULL on error. */
dbexp_T *parse_double_bracket_ors(parsestate_T *ps)
{
    dbexp_T *lhs = parse_double_bracket_ands(ps);
    if (lhs == NULL || ps->tokentype != TT_PIPEPIPE)
	return lhs;
    next_token(ps);
    psubstitute_alias_recursive(ps, 0);

    dbexp_T *result = xmalloc(sizeof *result);
    result->type = DBE_OR;
    result->operator = NULL;
    result->lhs.subexp = lhs;
    result->rhs.subexp = parse_double_bracket_ors(ps);
    return result;
}

/* Parses one or more "!" expressions separated by "&&"s in the double-bracket
 * command. May return NULL on error. */
dbexp_T *parse_double_bracket_ands(parsestate_T *ps)
{
    dbexp_T *lhs = parse_double_bracket_nots(ps);
    if (lhs == NULL || ps->tokentype != TT_AMPAMP)
	return lhs;
    next_token(ps);
    psubstitute_alias_recursive(ps, 0);

    dbexp_T *result = xmalloc(sizeof *result);
    result->type = DBE_AND;
    result->operator = NULL;
    result->lhs.subexp = lhs;
    result->rhs.subexp = parse_double_bracket_ands(ps);
    return result;
}

/* Parses a primary expression optionally prefixed by any number of "!"s in the
 * double-bracket command. May return NULL on error. */
dbexp_T *parse_double_bracket_nots(parsestate_T *ps)
{
    if (ps->tokentype != TT_BANG)
	return parse_double_bracket_primary(ps);
    next_token(ps);
    psubstitute_alias_recursive(ps, 0);

    dbexp_T *result = xmalloc(sizeof *result);
    result->type = DBE_NOT;
    result->operator = NULL;
    result->lhs.subexp = NULL;
    result->rhs.subexp = parse_double_bracket_nots(ps);
    return result;
}

/* Parses a primary expression in the double-bracket command. May return NULL on
 * error. The "(...)" operator is considered as a primary in this function. */
dbexp_T *parse_double_bracket_primary(parsestate_T *ps)
{
    if (ps->tokentype == TT_LPAREN) {
	/* parse "(...)" */
	next_token(ps);
	psubstitute_alias_recursive(ps, 0);

	dbexp_T *subexp = parse_double_bracket_ors(ps);
	if (ps->tokentype == TT_RPAREN) {
	    next_token(ps);
	    psubstitute_alias_recursive(ps, 0);
	} else if (!ps->error) {
	    if (ps->tokentype == TT_NEWLINE ||
		    (is_single_string_word(ps->token) &&
		     wcscmp(ps->token->wu_string, L"]]") == 0))
		serror(ps, Ngt("`%ls' is missing"), L")");
	    else
		print_errmsg_token(ps,
			Ngt("invalid word `%ls' between `[[' and `]]'"));
	}
	return subexp;
    }

    dbexptype_T type;
    wchar_t *op;
    wordunit_T *lhs, *rhs;
    bool rhs_regex = false;

    if (is_single_string_word(ps->token) &&
	    is_unary_primary(ps->token->wu_string)) {
	type = DBE_UNARY;
	lhs = NULL;
	goto parse_primary_operator;
    }

    lhs = parse_double_bracket_operand(ps);
    if (lhs == NULL)
	return NULL;

    if (ps->tokentype == TT_LESS || ps->tokentype == TT_GREATER) {
	type = DBE_BINARY;
	op = xwcsndup(&ps->src.contents[ps->index], ps->next_index - ps->index);
    } else if (is_single_string_word(ps->token) &&
	    is_binary_primary(ps->token->wu_string)) {
	type = DBE_BINARY;
	if (wcscmp(ps->token->wu_string, L"=~") == 0)
	    rhs_regex = true;
parse_primary_operator:
	op = ps->token->wu_string, ps->token->wu_string = NULL;
    } else {
	type = DBE_STRING;
	op = NULL;
	rhs = lhs, lhs = NULL;
	goto return_result;
    }
    next_token(ps);
    psubstitute_alias_recursive(ps, 0);

    if (rhs_regex)
	rhs = parse_double_bracket_operand_regex(ps);
    else
	rhs = parse_double_bracket_operand(ps);

return_result:;
    dbexp_T *result = xmalloc(sizeof *result);
    result->type = type;
    result->operator = op;
    result->lhs.word = lhs;
    result->rhs.word = rhs;
    return result;
}

/* Parses an operand token of a primary conditional expression in the double-
 * bracket command. Returns NULL on error. */
wordunit_T *parse_double_bracket_operand(parsestate_T *ps)
{
    if (is_single_string_word(ps->token) &&
	    wcscmp(ps->token->wu_string, L"]]") == 0) {
	serror(ps, Ngt("conditional expression "
		    "is missing or incomplete between `[[' and `]]'"));
	return NULL;
    }
    if (ps->token == NULL) {
	if (ps->tokentype == TT_NEWLINE)
	    serror(ps, Ngt("unexpected linebreak "
			"in the middle of the [[ ... ]] command"));
	else
	    print_errmsg_token(ps, Ngt("`%ls' is not a valid operand "
			"in the conditional expression"));
	return NULL;
    }

    wordunit_T *result = ps->token;
    ps->token = NULL;
    next_token(ps);
    psubstitute_alias_recursive(ps, 0);
    return result;
}

/* Parses the right-hand-side operand of a "=~" binary operator. Returns NULL on
 * error. */
wordunit_T *parse_double_bracket_operand_regex(parsestate_T *ps)
{
    /* Unlike a normal word token, this operand token is special because
     * vertical bars (`|') and parentheses (`(' and `)') can appear in the token
     * as if they were normal word characters. Additionally, parentheses can
     * even nest! */

    size_t grandstartindex = ps->index;

    /* So, before calling this function, the current token must have been parsed
     * as usual by the `next_token' function. We start by examining the current
     * status to determine how to continue parsing the token's possible
     * remainder.*/
    if (ps->token == NULL) {
	switch (ps->src.contents[ps->index]) {
	    case L'(':
	    case L'|':
		/* The current token begins with a character that should be
		 * parsed as part of the token. */
		break;
	    default:
		/* The current token is a normal operator. */
		return parse_double_bracket_operand(ps);
	}
    } else {
	ps->index = ps->next_index;
    }

    /* Keep the word units from being overwritten while parsing inner commands
     * that may be contained in the rest of the token. */
    wordunit_T *token = ps->token;
    ps->token = NULL;

    /* Find the place to append results. */
    wordunit_T **lastp = &token;
    while (*lastp != NULL)
	lastp = &(*lastp)->next;

    /* Now parse the remainder of the token. */
    int nestparen = 0;
    size_t startindex = ps->index;
    for (;;) {
	maybe_line_continuations(ps, ps->index);
	switch (ps->src.contents[ps->index]) {
	    case L'\0':
		serror(ps, Ngt("`%ls' is missing"), L"]]");
		goto end;
	    case L'\\':
		if (ps->src.contents[ps->index + 1] != L'\0') {
		    assert(ps->src.contents[ps->index + 1] != L'\n');
		    ps->index += 2;
		    continue;
		}
		break;
	    case L'$':
	    case L'`':
		MAKE_WORDUNIT_STRING;
		wordunit_T *wu = parse_special_word_unit(ps, false);
		startindex = ps->index;
		if (wu != NULL) {
		    *lastp = wu;
		    lastp = &wu->next;
		    continue;
		} else if (ps->src.contents[ps->index] == L'\0') {
		    continue;
		}
		break;
	    case L'(':
		nestparen++;
		break;
	    case L')':
		if (nestparen == 0) {
		    serror(ps, Ngt("encountered `%ls' without a matching `('"),
			    L")");
		    goto end;
		}
		nestparen--;
		break;
	    case L'|':
		break;
	    case L'\n':
		if (nestparen == 0)
		    goto end;
		ps->info->lineno++;
		break;
	    default:
		if (nestparen > 0)
		    break;
		if (is_token_delimiter_char(ps->src.contents[ps->index]))
		    goto end;
		break;
	}
	ps->index++;
    }

end:;
    MAKE_WORDUNIT_STRING;
    ps->next_index = ps->index;
    ps->index = grandstartindex;
    wordfree(ps->token), ps->token = token;
    ps->tokentype = TT_WORD;
    return parse_double_bracket_operand(ps);
}

#endif /* YASH_ENABLE_DOUBLE_BRACKET */

/* Parses a function definition that starts with the "function" keyword.
 * The current token must be "function". Never returns NULL. */
command_T *parse_function(parsestate_T *ps)
{
    if (posixly_correct)
	serror(ps, Ngt("`%ls' cannot be used as a command name"), L"function");

    assert(ps->tokentype == TT_FUNCTION);
    next_token(ps);
    psubstitute_alias_recursive(ps, 0);

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_FUNCDEF;
    result->c_lineno = ps->info->lineno;
    result->c_redirs = NULL;
    result->c_funcname = ps->token, ps->token = NULL;
    if (result->c_funcname == NULL)
	serror(ps, Ngt("a word is required after `%ls'"), L"function");

    bool paren = false;
    next_token(ps);
parse_parentheses:
    if (ps->tokentype == TT_LPAREN) {
	size_t saveindex = ps->index;
	next_token(ps);
parse_close_parenthesis:
	if (ps->tokentype == TT_RPAREN) {
	    paren = true;
	    next_token(ps);
	} else if (psubstitute_alias(ps, AF_NONGLOBAL)) {
	    goto parse_close_parenthesis;
	} else {
	    /* rewind to '(' */
	    rewind_index(ps, saveindex);
	    ps->next_index = ps->index;
	    next_token(ps);
	}
    }
parse_function_body:
    parse_newline_list(ps);

    result->c_funcbody = parse_compound_command(ps);
    if (result->c_funcbody == NULL) {
	if (psubstitute_alias(ps, 0)) {
	    if (paren)
		goto parse_function_body;
	    else
		goto parse_parentheses;
	}
	serror(ps, Ngt("a function body must be a compound command"));
    }

    return result;
}

/* Parses (part of) a function definition command that does not start with the
 * "function" keyword. This function must be called just after a simple command
 * has been parsed, which is given as `c'. If the next character is '(', it
 * should signify a function definition, so this function continues parsing the
 * rest of it. Otherwise, `c' is returned intact.
 * If successful, `c' is directly modified to the function definition parsed. */
command_T *try_reparse_as_function(parsestate_T *ps, command_T *c)
{
    if (ps->tokentype != TT_LPAREN) // not a function definition?
	return c;

    /* If this is a function definition, there must be exactly one command word
     * before '('. */
    assert(c->c_type == CT_SIMPLE);
    if (c->c_redirs != NULL || c->c_assigns != NULL
	    || c->c_words[0] == NULL || c->c_words[1] != NULL) {
	serror(ps, Ngt("invalid use of `%lc'"), (wint_t) L'(');
	return c;
    }

    /* The name must be valid. */
    wordunit_T *name = c->c_words[0];
    if (!is_name_word(name)) {
	serror(ps, Ngt("invalid function name"));
	return c;
    }

    /* Skip '('. */
    next_token(ps);

    /* Parse ')'. */
    psubstitute_alias_recursive(ps, 0);
    if (ps->tokentype != TT_RPAREN) {
	serror(ps, Ngt("`(' must be followed by `)' in a function definition"));
	return c;
    }
    next_token(ps);

    free(c->c_words);
    c->c_type = CT_FUNCDEF;
    c->c_funcname = name;

parse_function_body:
    parse_newline_list(ps);
    c->c_funcbody = parse_compound_command(ps);
    if (c->c_funcbody == NULL) {
	if (psubstitute_alias(ps, 0))
	    goto parse_function_body;
	serror(ps, Ngt("a function body must be a compound command"));
    }

    return c;
}

/***** Here-document contents *****/

/* Reads the contents of a here-document. */
void read_heredoc_contents(parsestate_T *ps, redir_T *r)
{
    if (wcschr(r->rd_hereend, L'\n') != NULL) {
	serror(ps,
		Ngt("the end-of-here-document indicator contains a newline"));
	return;
    }

    assert(r->rd_type == RT_HERE || r->rd_type == RT_HERERT);
    if (wcspbrk(r->rd_hereend, QUOTES) != NULL)
	read_heredoc_contents_without_expansion(ps, r);
    else
	read_heredoc_contents_with_expansion(ps, r);
}

/* Reads the contents of a here-document without any parameter expansions. */
void read_heredoc_contents_without_expansion(parsestate_T *ps, redir_T *r)
{
    wchar_t *eoc = unquote(r->rd_hereend);  // end-of-contents marker
    if (eoc == NULL) {
	serror(ps, Ngt("the end-of-here-document indicator "
		    "is not properly quoted"));
	return;
    }

    bool skiptab = (r->rd_type == RT_HERERT);
    xwcsbuf_T buf;

    wb_init(&buf);
    while (!is_end_of_heredoc_contents(ps, eoc, skiptab)) {
	const wchar_t *eol = wcschr(&ps->src.contents[ps->index], L'\n');
	size_t linelen;
	if (eol != NULL) {
	    linelen = eol - &ps->src.contents[ps->index] + 1;
	} else {
	    /* encountered EOF before reading an end-of-contents marker! */
	    linelen = ps->src.length - ps->index;
	    serror(ps, Ngt("the here-document content is not closed by `%ls'"),
		    eoc);
	}
	wb_ncat_force(&buf, &ps->src.contents[ps->index], linelen);
	ps->index += linelen;
	if (eol != NULL)
	    ps->info->lineno++;
	else
	    break;
    }
    free(eoc);
    
    wordunit_T *wu = xmalloc(sizeof *wu);
    wu->next = NULL;
    wu->wu_type = WT_STRING;
    wu->wu_string = escape(buf.contents, L"\\");
    r->rd_herecontent = wu;

    wb_destroy(&buf);
}

/* Reads the contents of a here-document that may contain parameter expansions,
 * command substitutions and arithmetic expansions. */
void read_heredoc_contents_with_expansion(parsestate_T *ps, redir_T *r)
{
    wordunit_T **lastp = &r->rd_herecontent;
    const wchar_t *eoc = r->rd_hereend;
    bool skiptab = (r->rd_type == RT_HERERT);

    while (!is_end_of_heredoc_contents(ps, eoc, skiptab)) {
	size_t oldindex = ps->index;
	lastp = parse_string_without_quotes(ps, true, true, lastp);
	if (ps->index == oldindex || ps->src.contents[ps->index - 1] != L'\n') {
	    /* encountered EOF before reading an end-of-contents marker! */
	    serror(ps, Ngt("the here-document content is not closed by `%ls'"),
		    eoc);
	    break;
	}
    }
}

/* Checks if the whole current line is end-of-heredoc `eoc'.
 * Reads the current line if not yet read.
 * If `skiptab' is true, leading tabs in the line are skipped.
 * If an end-of-heredoc is found, returns true and advances the current position
 * to the next line. Otherwise, returns false with the position unchanged
 * (except that leading tabs are skipped). */
bool is_end_of_heredoc_contents(
	parsestate_T *ps, const wchar_t *eoc, bool skiptab)
{
    if (ps->src.contents[ps->index] == L'\0')
	if (read_more_input(ps) != INPUT_OK)
	    return false;

    assert(ps->src.length > 0 && ps->src.contents[ps->index - 1] == L'\n');

    if (skiptab)
	while (ps->src.contents[ps->index] == L'\t')
	    ps->index++;

    const wchar_t *m = matchwcsprefix(&ps->src.contents[ps->index], eoc);
    if (m != NULL) {
	size_t matchendindex = m - ps->src.contents;
	switch (ps->src.contents[matchendindex]) {
	    case L'\0':
		ps->index = matchendindex;
		return true;
	    case L'\n':
		ps->index = matchendindex + 1;
		ps->info->lineno++;
		return true;
	}
    }
    return false;
}

/* Prints an error message for each pending here-document. */
void reject_pending_heredocs(parsestate_T *ps)
{
    for (size_t i = 0; i < ps->pending_heredocs.length; i++) {
	const redir_T *r = ps->pending_heredocs.contents[i];
	const char *operator;
	switch (r->rd_type) {
	    case RT_HERE:    operator = "<<";   break;
	    case RT_HERERT:  operator = "<<-";  break;
	    default:         assert(false);
	}
	serror(ps, Ngt("here-document content for %s%ls is missing"),
		operator, r->rd_hereend);
    }
}

/* Parses a string.
 * Parameter expansions, command substitutions and arithmetic expansions are
 * recognized, but single and double quotes are not treated as quotes.
 * Command substitutions enclosed by backquotes are recognized iff `backquote'
 * is true. If `stoponnewline' is true, stops parsing right after the next
 * newline is parsed. Otherwise, parsing continues up to the end of file.
 * The results are assigned ot `*lastp'.
 * The return value is a pointer to the `next' member of the last resultant word
 * unit (or `lastp' if no word unit resulted). */
wordunit_T **parse_string_without_quotes(
	parsestate_T *ps, bool backquote, bool stoponnewline,
	wordunit_T **lastp)
{
    size_t startindex = ps->index;

    for (;;) {
	maybe_line_continuations(ps, ps->index);
	switch (ps->src.contents[ps->index]) {
	case L'\0':
	    goto done;
	case L'\\':
	    if (ps->src.contents[ps->index + 1] != L'\0') {
		assert(ps->src.contents[ps->index + 1] != L'\n');
		ps->index += 2;
		continue;
	    }
	    break;
	case L'\n':
	    ps->info->lineno++;
	    if (stoponnewline) {
		ps->index++;
		goto done;
	    }
	    break;
	case L'`':
	    if (!backquote)
		break;
	    /* falls thru! */
	case L'$':
	    MAKE_WORDUNIT_STRING;
	    wordunit_T *wu = parse_special_word_unit(ps, true);
	    startindex = ps->index;
	    if (wu != NULL) {
		*lastp = wu;
		lastp = &wu->next;
		continue;
	    } else if (ps->src.contents[ps->index] == L'\0') {
		continue;
	    }
	    break;
	default:
	    break;
	}
	ps->index++;
    }
done:
    MAKE_WORDUNIT_STRING;
    return lastp;
}


/********** Functions that Convert Parse Trees into Strings **********/

struct print {
    xwcsbuf_T buffer;
    plist_T pending_heredocs;
    bool multiline;
};

static void print_and_or_lists(
	struct print *restrict pr, const and_or_T *restrict andors,
	unsigned indent, bool omitsemicolon)
    __attribute__((nonnull(1)));
static void print_pipelines(
	struct print *restrict er, const pipeline_T *restrict pipelines,
	unsigned indent)
    __attribute__((nonnull(1)));
static void print_commands(
	struct print *restrict pr, const command_T *restrict commands,
	unsigned indent)
    __attribute__((nonnull(1)));
static void print_one_command(
	struct print *restrict pr, const command_T *restrict command,
	unsigned indent)
    __attribute__((nonnull));
static void print_simple_command(
	struct print *restrict pr, const command_T *restrict command,
	unsigned indent)
    __attribute__((nonnull));
static void print_group(
	struct print *restrict pr, const command_T *restrict command,
	unsigned indent)
    __attribute__((nonnull));
static void print_subshell(
	struct print *restrict pr, const command_T *restrict command,
	unsigned indent)
    __attribute__((nonnull));
static void print_if(
	struct print *restrict pr, const command_T *restrict command,
	unsigned indent)
    __attribute__((nonnull));
static void print_for(
	struct print *restrict pr, const command_T *restrict command,
	unsigned indent)
    __attribute__((nonnull));
static void print_while(
	struct print *restrict pr, const command_T *restrict command,
	unsigned indent)
    __attribute__((nonnull));
static void print_case(
	struct print *restrict pr, const command_T *restrict command,
	unsigned indent)
    __attribute__((nonnull));
static void print_caseitems(
	struct print *restrict pr, const caseitem_T *restrict caseitems,
	unsigned indent)
    __attribute__((nonnull(1)));
#if YASH_ENABLE_DOUBLE_BRACKET
static void print_double_bracket(
	struct print *restrict pr, const command_T *restrict c, unsigned indent)
    __attribute__((nonnull));
static void print_double_bracket_expression(
	struct print *restrict pr, const dbexp_T *restrict e,
	dbexptype_T context, unsigned indent)
    __attribute__((nonnull));
#endif
static void print_function_definition(
	struct print *restrict pr, const command_T *restrict command,
	unsigned indent)
    __attribute__((nonnull));
static void print_assignments(
	struct print *restrict pr, const assign_T *restrict assigns,
	unsigned indent)
    __attribute__((nonnull(1)));
static void print_redirections(
	struct print *restrict pr, const redir_T *restrict redirections,
	unsigned indent)
    __attribute__((nonnull(1)));
static void print_word(
	struct print *restrict pr, const wordunit_T *restrict wordunits,
	unsigned indent)
    __attribute__((nonnull(1)));
static void print_parameter(
	struct print *restrict pr, const paramexp_T *restrict parameter,
	unsigned indent)
    __attribute__((nonnull));
static void print_embedded_command(struct print *pr, embedcmd_T command,
	unsigned indent)
    __attribute__((nonnull));
static void print_indent(struct print *pr, unsigned indent)
    __attribute__((nonnull));
static void print_space_or_newline(struct print* pr)
    __attribute__((nonnull));
static void print_pending_heredocs(struct print* pr)
    __attribute__((nonnull));
static void trim_end_of_buffer(xwcsbuf_T *buf)
    __attribute__((nonnull));

/* Converts the specified pipelines into a newly malloced wide string. */
wchar_t *pipelines_to_wcs(const pipeline_T *pipelines)
{
    struct print pr;

    wb_init(&pr.buffer);
    pl_init(&pr.pending_heredocs);
    pr.multiline = false;

    print_pipelines(&pr, pipelines, 0);
    trim_end_of_buffer(&pr.buffer);

    pl_destroy(&pr.pending_heredocs);
    return wb_towcs(&pr.buffer);
}

/* Converts the specified command into a newly malloced wide string.
 * If `multiline' is true, the result is pretty-formated with a terminating
 * newline. */
wchar_t *command_to_wcs(const command_T *command, bool multiline)
{
    struct print pr;

    wb_init(&pr.buffer);
    pl_init(&pr.pending_heredocs);
    pr.multiline = multiline;

    print_one_command(&pr, command, 0);

    trim_end_of_buffer(&pr.buffer);
    if (pr.multiline)
	print_space_or_newline(&pr);

    pl_destroy(&pr.pending_heredocs);
    return wb_towcs(&pr.buffer);
}

void print_and_or_lists(struct print *restrict pr, const and_or_T *restrict ao,
	unsigned indent, bool omitsemicolon)
{
    while (ao != NULL) {
	print_pipelines(pr, ao->ao_pipelines, indent);
	trim_end_of_buffer(&pr->buffer);
	if (ao->ao_async)
	    wb_wccat(&pr->buffer, L'&');
	else if (!pr->multiline && (ao->next != NULL || !omitsemicolon))
	    wb_wccat(&pr->buffer, L';');
	if (ao->next != NULL || !omitsemicolon)
	    print_space_or_newline(pr);

	ao = ao->next;
    }
}

void print_pipelines(struct print *restrict pr, const pipeline_T *restrict pl,
	unsigned indent)
{
    if (pl == NULL)
	return;
    for (;;) {
	print_indent(pr, indent);
	if (pl->pl_neg)
	    wb_cat(&pr->buffer, L"! ");
	print_commands(pr, pl->pl_commands, indent);

	pl = pl->next;
	if (pl == NULL)
	    break;

	wb_cat(&pr->buffer, pl->pl_cond ? L"&&" : L"||");
	print_space_or_newline(pr);
    }
}

void print_commands(
	struct print *restrict pr, const command_T *restrict c, unsigned indent)
{
    if (c == NULL)
	return;
    for (;;) {
	print_one_command(pr, c, indent);

	c = c->next;
	if (c == NULL)
	    break;

	wb_cat(&pr->buffer, L"| ");
    }
}

void print_one_command(
	struct print *restrict pr, const command_T *restrict c, unsigned indent)
{
    switch (c->c_type) {
	case CT_SIMPLE:
	    print_simple_command(pr, c, indent);
	    break;
	case CT_GROUP:
	    print_group(pr, c, indent);
	    break;
	case CT_SUBSHELL:
	    print_subshell(pr, c, indent);
	    break;
	case CT_IF:
	    print_if(pr, c, indent);
	    break;
	case CT_FOR:
	    print_for(pr, c, indent);
	    break;
	case CT_WHILE:
	    print_while(pr, c, indent);
	    break;
	case CT_CASE:
	    print_case(pr, c, indent);
	    break;
#if YASH_ENABLE_DOUBLE_BRACKET
	case CT_BRACKET:
	    print_double_bracket(pr, c, indent);
	    break;
#endif /* YASH_ENABLE_DOUBLE_BRACKET */
	case CT_FUNCDEF:
	    print_function_definition(pr, c, indent);
	    assert(c->c_redirs == NULL);
	    return;  // break;
    }
    print_redirections(pr, c->c_redirs, indent);
}

void print_simple_command(
	struct print *restrict pr, const command_T *restrict c, unsigned indent)
{
    assert(c->c_type == CT_SIMPLE);

    print_assignments(pr, c->c_assigns, indent);
    for (size_t i = 0; c->c_words[i] != NULL; i++) {
	const wordunit_T *wu = c->c_words[i];

	/* A simple command can start with a keyword if preceded by a
	 * redirection. Because we print the redirection after the command
	 * words, the keyword must be quoted to be re-parsed as a simple
	 * command. */
	if (i == 0 && c->c_assigns == NULL &&
		is_single_string_word(wu) && is_keyword(wu->wu_string))
	    wb_wccat(&pr->buffer, L'\\');

	print_word(pr, wu, indent);
	wb_wccat(&pr->buffer, L' ');
    }
}

void print_group(
	struct print *restrict pr, const command_T *restrict c, unsigned indent)
{
    assert(c->c_type == CT_GROUP);

    wb_wccat(&pr->buffer, L'{');
    print_space_or_newline(pr);
    print_and_or_lists(pr, c->c_subcmds, indent + 1, false);
    print_indent(pr, indent);
    wb_cat(&pr->buffer, L"} ");
}

void print_subshell(
	struct print *restrict pr, const command_T *restrict c, unsigned indent)
{
    assert(c->c_type == CT_SUBSHELL);

    wb_wccat(&pr->buffer, L'(');
    print_and_or_lists(pr, c->c_subcmds, indent + 1, true);
    trim_end_of_buffer(&pr->buffer);
    wb_cat(&pr->buffer, L") ");
}

void print_if(
	struct print *restrict pr, const command_T *restrict c, unsigned indent)
{
    const ifcommand_T *ic;

    assert(c->c_type == CT_IF);

    wb_cat(&pr->buffer, L"if ");
    ic = c->c_ifcmds;
    for (;;) {
	print_and_or_lists(pr, ic->ic_condition, indent + 1, false);
	print_indent(pr, indent);
	wb_cat(&pr->buffer, L"then");
	print_space_or_newline(pr);
	print_and_or_lists(pr, ic->ic_commands, indent + 1, false);

	ic = ic->next;
	if (ic == NULL) {
	    break;
	} else if (!ic->ic_condition && ic->next == NULL) {
	    print_indent(pr, indent);
	    wb_cat(&pr->buffer, L"else");
	    print_space_or_newline(pr);
	    print_and_or_lists(pr, ic->ic_commands, indent + 1, false);
	    break;
	} else {
	    print_indent(pr, indent);
	    wb_cat(&pr->buffer, L"elif ");
	}
    }
    print_indent(pr, indent);
    wb_cat(&pr->buffer, L"fi ");
}

void print_for(
	struct print *restrict pr, const command_T *restrict c, unsigned indent)
{
    assert(c->c_type == CT_FOR);

    wb_cat(&pr->buffer, L"for ");
    wb_cat(&pr->buffer, c->c_forname);
    if (c->c_forwords != NULL) {
	wb_cat(&pr->buffer, L" in");
	for (void **w = c->c_forwords; *w != NULL; w++) {
	    wb_wccat(&pr->buffer, L' ');
	    print_word(pr, *w, indent);
	}
	if (!pr->multiline)
	    wb_wccat(&pr->buffer, L';');
	print_space_or_newline(pr);
    } else {
	wb_wccat(&pr->buffer, L' ');
    }
    print_indent(pr, indent);
    wb_cat(&pr->buffer, L"do");
    print_space_or_newline(pr);

    print_and_or_lists(pr, c->c_forcmds, indent + 1, false);

    print_indent(pr, indent);
    wb_cat(&pr->buffer, L"done ");
}

void print_while(
	struct print *restrict pr, const command_T *restrict c, unsigned indent)
{
    assert(c->c_type == CT_WHILE);

    wb_cat(&pr->buffer, c->c_whltype ? L"while " : L"until ");
    print_and_or_lists(pr, c->c_whlcond, indent + 1, false);

    print_indent(pr, indent);
    wb_cat(&pr->buffer, L"do");
    print_space_or_newline(pr);

    print_and_or_lists(pr, c->c_whlcmds, indent + 1, false);

    print_indent(pr, indent);
    wb_cat(&pr->buffer, L"done ");
}

void print_case(
	struct print *restrict pr, const command_T *restrict c, unsigned indent)
{
    assert(c->c_type == CT_CASE);

    wb_cat(&pr->buffer, L"case ");
    print_word(pr, c->c_casword, indent);
    wb_cat(&pr->buffer, L" in");
    print_space_or_newline(pr);

    print_caseitems(pr, c->c_casitems, indent + 1);

    print_indent(pr, indent);
    wb_cat(&pr->buffer, L"esac ");
}

void print_caseitems(struct print *restrict pr, const caseitem_T *restrict ci,
	unsigned indent)
{
    while (ci != NULL) {
	print_indent(pr, indent);
	wb_wccat(&pr->buffer, L'(');
	for (void **w = ci->ci_patterns; ; ) {
	    print_word(pr, *w, indent);
	    w++;
	    if (*w == NULL)
		break;
	    wb_cat(&pr->buffer, L" | ");
	}
	wb_wccat(&pr->buffer, L')');
	print_space_or_newline(pr);

	if (ci->ci_commands != NULL) {
	    print_and_or_lists(pr, ci->ci_commands, indent + 1, true);
	    print_space_or_newline(pr);
	}

	print_indent(pr, indent + 1);
	wb_cat(&pr->buffer, L";;");
	print_space_or_newline(pr);

	ci = ci->next;
    }
}

#if YASH_ENABLE_DOUBLE_BRACKET

void print_double_bracket(
	struct print *restrict pr, const command_T *restrict c, unsigned indent)
{
    assert(c->c_type == CT_BRACKET);

    wb_cat(&pr->buffer, L"[[ ");
    print_double_bracket_expression(pr, c->c_dbexp, DBE_OR, indent);
    wb_cat(&pr->buffer, L"]] ");
}

void print_double_bracket_expression(
	struct print *restrict pr, const dbexp_T *restrict e,
	dbexptype_T context, unsigned indent)
{
    assert(context == DBE_OR || context == DBE_AND || context == DBE_NOT);

    switch (e->type) {
	case DBE_OR:
	    if (context != DBE_OR)
		wb_cat(&pr->buffer, L"( ");
	    print_double_bracket_expression(pr, e->lhs.subexp, DBE_OR, indent);
	    wb_cat(&pr->buffer, L"|| ");
	    print_double_bracket_expression(pr, e->rhs.subexp, DBE_OR, indent);
	    if (context != DBE_OR)
		wb_cat(&pr->buffer, L") ");
	    break;
	case DBE_AND:
	    if (context == DBE_NOT)
		wb_cat(&pr->buffer, L"( ");
	    print_double_bracket_expression(pr, e->lhs.subexp, DBE_AND, indent);
	    wb_cat(&pr->buffer, L"&& ");
	    print_double_bracket_expression(pr, e->rhs.subexp, DBE_AND, indent);
	    if (context == DBE_NOT)
		wb_cat(&pr->buffer, L") ");
	    break;
	case DBE_NOT:
	    wb_cat(&pr->buffer, L"! ");
	    print_double_bracket_expression(pr, e->rhs.subexp, DBE_NOT, indent);
	    break;
	case DBE_BINARY:
	    print_word(pr, e->lhs.word, indent);
	    wb_wccat(&pr->buffer, L' ');
	    /* falls thru! */
	case DBE_UNARY:
	    wb_cat(&pr->buffer, e->operator);
	    wb_wccat(&pr->buffer, L' ');
	    /* falls thru! */
	case DBE_STRING:
	    print_word(pr, e->rhs.word, indent);
	    wb_wccat(&pr->buffer, L' ');
	    break;
    }
}

#endif

void print_function_definition(
	struct print *restrict pr, const command_T *restrict c, unsigned indent)
{
    assert(c->c_type == CT_FUNCDEF);

    if (!is_name_word(c->c_funcname))
	wb_cat(&pr->buffer, L"function ");
    print_word(pr, c->c_funcname, indent);
    wb_cat(&pr->buffer, L"()");
    print_space_or_newline(pr);

    print_indent(pr, indent);
    assert(c->c_funcbody->next == NULL);
    print_one_command(pr, c->c_funcbody, indent);
}

void print_assignments(
	struct print *restrict pr, const assign_T *restrict a, unsigned indent)
{
    while (a != NULL) {
	wb_cat(&pr->buffer, a->a_name);
	wb_wccat(&pr->buffer, L'=');
	switch (a->a_type) {
	    case A_SCALAR:
		print_word(pr, a->a_scalar, indent);
		break;
	    case A_ARRAY:
		wb_wccat(&pr->buffer, L'(');
		for (void **w = a->a_array; *w != NULL; w++) {
		    print_word(pr, *w, indent);
		    wb_wccat(&pr->buffer, L' ');
		}
		trim_end_of_buffer(&pr->buffer);
		wb_wccat(&pr->buffer, L')');
		break;
	}
	wb_wccat(&pr->buffer, L' ');

	a = a->next;
    }
}

void print_redirections(
	struct print *restrict pr, const redir_T *restrict rd, unsigned indent)
{
    while (rd != NULL) {
	const wchar_t *s;
	enum { file, here, proc, } type;

	switch (rd->rd_type) {
            case RT_INPUT:    s = L"<";    type = file;  break;
            case RT_OUTPUT:   s = L">";    type = file;  break;
            case RT_CLOBBER:  s = L">|";   type = file;  break;
            case RT_APPEND:   s = L">>";   type = file;  break;
            case RT_INOUT:    s = L"<>";   type = file;  break;
            case RT_DUPIN:    s = L"<&";   type = file;  break;
            case RT_DUPOUT:   s = L">&";   type = file;  break;
            case RT_PIPE:     s = L">>|";  type = file;  break;
            case RT_HERE:     s = L"<<";   type = here;  break;
            case RT_HERERT:   s = L"<<-";  type = here;  break;
            case RT_HERESTR:  s = L"<<<";  type = file;  break;
            case RT_PROCIN:   s = L"<(";   type = proc;  break;
            case RT_PROCOUT:  s = L">(";   type = proc;  break;
            default: assert(false);
	}
	wb_wprintf(&pr->buffer, L"%d%ls", rd->rd_fd, s);
	switch (type) {
	    case file:
		print_word(pr, rd->rd_filename, indent);
		break;
	    case here:
		if (rd->rd_hereend[0] == L'-') {
		    wb_wccat(&pr->buffer, L' ');
		}
		wb_cat(&pr->buffer, rd->rd_hereend);
		pl_add(&pr->pending_heredocs, (void *) rd);
		break;
	    case proc:
		print_embedded_command(pr, rd->rd_command, indent + 1);
		wb_wccat(&pr->buffer, L')');
		break;
	}
	wb_wccat(&pr->buffer, L' ');

	rd = rd->next;
    }
}

void print_word(struct print *restrict pr, const wordunit_T *restrict wu,
	unsigned indent)
{
    while (wu != NULL) {
	switch (wu->wu_type) {
	    case WT_STRING:
		wb_cat(&pr->buffer, wu->wu_string);
		break;
	    case WT_PARAM:
		print_parameter(pr, wu->wu_param, indent);
		break;
	    case WT_CMDSUB:
		wb_cat(&pr->buffer, L"$(");
		size_t startindex = pr->buffer.length;
		print_embedded_command(pr, wu->wu_cmdsub, indent + 1);
		wb_wccat(&pr->buffer, L')');
		if (pr->buffer.contents[startindex] == L'(')
		    wb_insert(&pr->buffer, startindex, L" ");
		break;
	    case WT_ARITH:
		wb_cat(&pr->buffer, L"$((");
		print_word(pr, wu->wu_arith, indent);
		wb_cat(&pr->buffer, L"))");
		break;
	}

	wu = wu->next;
    }
}

void print_parameter(struct print *restrict pr, const paramexp_T *restrict pe,
	unsigned indent)
{
    wb_cat(&pr->buffer, L"${");
    if (pe->pe_type & PT_NUMBER)
	wb_wccat(&pr->buffer, L'#');
    if (pe->pe_type & PT_NEST)
	print_word(pr, pe->pe_nest, indent);
    else
	wb_cat(&pr->buffer, pe->pe_name);
    if (pe->pe_start != NULL) {
	wb_wccat(&pr->buffer, L'[');
	print_word(pr, pe->pe_start, indent);
	if (pe->pe_end != NULL) {
	    wb_wccat(&pr->buffer, L',');
	    print_word(pr, pe->pe_end, indent);
	}
	wb_wccat(&pr->buffer, L']');
    }
    if (pe->pe_type & PT_COLON)
	wb_wccat(&pr->buffer, L':');
    switch (pe->pe_type & PT_MASK) {
	wchar_t c;
	case PT_PLUS:    c = L'+';  goto append_subst;
	case PT_MINUS:   c = L'-';  goto append_subst;
	case PT_ASSIGN:  c = L'=';  goto append_subst;
	case PT_ERROR:   c = L'?';  goto append_subst;
	case PT_MATCH:
	    if (pe->pe_type & PT_MATCHHEAD) {
		c = L'#';
	    } else {
		assert(pe->pe_type & PT_MATCHTAIL);
		c = L'%';
	    }
	    wb_wccat(&pr->buffer, c);
	    if (pe->pe_type & PT_MATCHLONGEST)
		wb_wccat(&pr->buffer, c);
	    print_word(pr, pe->pe_match, indent);
	    break;
	case PT_SUBST:
	    wb_wccat(&pr->buffer, L'/');
	    if (pe->pe_type & PT_SUBSTALL)
		wb_wccat(&pr->buffer, L'/');
	    else if (pe->pe_type & PT_MATCHHEAD)
		wb_wccat(&pr->buffer, L'#');
	    else if (pe->pe_type & PT_MATCHTAIL)
		wb_wccat(&pr->buffer, L'%');
	    print_word(pr, pe->pe_match, indent);
	    c = L'/';
append_subst:
	    wb_wccat(&pr->buffer, c);
	    print_word(pr, pe->pe_subst, indent);
	    break;
    }
    wb_wccat(&pr->buffer, L'}');
}

void print_embedded_command(struct print *pr, embedcmd_T ec, unsigned indent)
{
    if (!ec.is_preparsed) {
	wb_cat(&pr->buffer, ec.value.unparsed);
	return;
    }

    size_t save_count = pr->pending_heredocs.length;
    size_t extended_count = save_count;
    if (extended_count == 0)
	extended_count = 1; // A variable-length array must not be empty.
    void *save_heredocs[extended_count];
    memcpy(save_heredocs, pr->pending_heredocs.contents, sizeof save_heredocs);
    pl_truncate(&pr->pending_heredocs, 0);

    print_and_or_lists(pr, ec.value.preparsed, indent, true);

    if (pr->multiline) {
	if (pr->pending_heredocs.length > 0) {
	    print_space_or_newline(pr); // print remaining heredocs
	    print_indent(pr, indent);
	}
    } else {
	pl_truncate(&pr->pending_heredocs, 0);
    }

    assert(pr->pending_heredocs.length == 0);
    pl_ncat(&pr->pending_heredocs, save_heredocs, save_count);
}

void print_indent(struct print *pr, unsigned indent)
{
#ifndef FORMAT_INDENT_WIDTH
#define FORMAT_INDENT_WIDTH 3
#endif

    if (pr->multiline) {
	xwcsbuf_T *buf = &pr->buffer;

	if (buf->length > 0 && buf->contents[buf->length - 1] != L'\n')
	    return;

	indent *= FORMAT_INDENT_WIDTH;
	while (indent > 0) {
	    wb_wccat(buf, L' ');
	    indent--;
	}
    }
}

void print_space_or_newline(struct print* pr)
{
    if (pr->multiline) {
	wb_wccat(&pr->buffer, L'\n');
	print_pending_heredocs(pr);
    } else {
	wb_wccat(&pr->buffer, L' ');
    }
}

void print_pending_heredocs(struct print* pr)
{
    for (size_t i = 0; i < pr->pending_heredocs.length; i++) {
	const redir_T *rd = pr->pending_heredocs.contents[i];
	print_word(pr, rd->rd_herecontent, 0);
	wb_catfree(&pr->buffer, unquote(rd->rd_hereend));
	wb_wccat(&pr->buffer, L'\n');
    }
    pl_truncate(&pr->pending_heredocs, 0);
}

void trim_end_of_buffer(xwcsbuf_T *buf)
{
    size_t i = buf->length;
    while (i > 0 && buf->contents[i - 1] == L' ')
	i--;
    wb_truncate(buf, i);
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
