/* Yash: yet another shell */
/* parser.c: syntax parser */
/* (C) 2007-2011 magicant */

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
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#if YASH_ENABLE_ALIAS
# include "alias.h"
#endif
#include "expand.h"
#include "input.h"
#include "option.h"
#include "parser.h"
#include "plist.h"
#include "strbuf.h"
#include "util.h"
#if YASH_ENABLE_LINEEDIT
# include "lineedit/lineedit.h"
#endif


/********** Functions That Free Parse Trees **********/

static void pipesfree(pipeline_T *p);
static void ifcmdsfree(ifcommand_T *i);
static void caseitemsfree(caseitem_T *i);
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
	c->refcount--;
	if (c->refcount > 0)
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

void wordfree(wordunit_T *w)
{
    while (w != NULL) {
	switch (w->wu_type) {
	    case WT_STRING:
		free(w->wu_string);
		break;
	    case WT_PARAM:
		paramfree(w->wu_param);
		break;
	    case WT_CMDSUB:
		embedcmdfree(w->wu_cmdsub);
		break;
	    case WT_ARITH:
		wordfree(w->wu_arith);
		break;
	}

	wordunit_T *next = w->next;
	free(w);
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

static wchar_t *skip_name(const wchar_t *s)
    __attribute__((pure,nonnull));


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
 * If there is no identifier, the argument `s' is simply returned. */
wchar_t *skip_name(const wchar_t *s)
{
    if (!iswdigit(*s))
	while (is_name_char(*s))
	    s++;
    return (wchar_t *) s;
}

/* Returns true iff the specified string constitutes a valid identifier. */
bool is_name(const wchar_t *s)
{
    return s[0] != L'\0' && skip_name(s)[0] == L'\0';
}

/* Returns true iff the string is a reserved word. */
bool is_keyword(const wchar_t *s)
{
    /* List of keywords:
     *    case do done elif else esac fi for function if in then until while
     *    { } !
     * The following words are currently not keywords:
     *    select [[ ]] */
    switch (s[0]) {
	case L'c':
	    return s[1] == L'a' && s[2] == L's' && s[3] == L'e' && s[4]== L'\0';
	case L'd':
	    return s[1] == L'o' && (s[2] == L'\0' ||
		    (s[2] == L'n' && s[3] == L'e' && s[4] == L'\0'));
	case L'e':
	    return ((s[1] == L'l'
			&& ((s[2] == L's' && s[3] == L'e')
			    || (s[2] == L'i' && s[3] == L'f')))
		    || (s[1] == L's' && s[2] == L'a' && s[3] == L'c'))
		&& s[4] == L'\0';
	case L'f':
	    return (s[1] == L'i' && s[2] == L'\0')
		|| (s[1] == L'o' && s[2] == L'r' && s[3] == L'\0')
		|| (s[1] == L'u' && s[2] == L'n' && s[3] == L'c'
			&& s[4] == L't' && s[5] == L'i' && s[6] == L'o'
			&& s[7] == L'n' && s[8] == L'\0');
	case L'i':
	    return (s[1] == L'f' || s[1] == L'n') && s[2] == L'\0';
	case L't':
	    return s[1] == L'h' && s[2] == L'e' && s[3] == L'n' && s[4]== L'\0';
	case L'u':
	    return s[1] == L'n' && s[2] == L't' && s[3] == L'i' && s[4] == L'l'
		&& s[5] == L'\0';
	case L'w':
	    return s[1] == L'h' && s[2] == L'i' && s[3] == L'l' && s[4] == L'e'
		&& s[5] == L'\0';
	case L'{':
	case L'}':
	case L'!':
	    return s[1] == L'\0';
	default:
	    return false;
    }
}


/********** Parser **********/

/* Holds data that are used in parsing. */
typedef struct parsestate_T {
    parseinfo_T *cinfo;
    bool cerror;
    struct xwcsbuf_T cbuf;
    size_t cindex;
    struct plist_T pending_heredocs;
#if YASH_ENABLE_ALIAS
    bool enable_alias, reparse_alias;
    struct aliaslist_T *caliases;
#endif
} parsestate_T;
/* cinfo: contains parameter that affect the behavior of parsing.
 * cerror: set to true when an parsing error occurs.
 * cbuf: a buffer that contains the source code to parse.
 * cindex: the index to the string in `cbuf'.
 *         indicates the character position that is being parsed.
 * pending_heredocs: a list of here-documents whose contents have not been read.
 * enable_alias: indicates if alias substitution should be performed.
 * reparse_alias: indicates that the current word must be re-parsed because
 *         alias substitution has been performed at the current position.
 * caliases: a list of alias substitutions that were performed at the current
 *         position. */

typedef enum { AT_NONE, AT_GLOBAL, AT_ALL, } aliastype_T;

static void serror(parsestate_T *restrict ps, const char *restrict format, ...)
    __attribute__((nonnull(1,2),format(printf,2,3)));
static inputresult_T read_more_input(parsestate_T *ps)
    __attribute__((nonnull));
static void line_continuation(parsestate_T *ps, size_t index)
    __attribute__((nonnull));
static void ensure_buffer(parsestate_T *ps, size_t n)
    __attribute__((nonnull));
static size_t count_name_length(parsestate_T *ps, bool isnamechar(wchar_t c))
    __attribute__((nonnull));
static void skip_blanks_and_comment(parsestate_T *ps)
    __attribute__((nonnull));
static bool skip_to_next_token(parsestate_T *ps)
    __attribute__((nonnull));
static void next_line(parsestate_T *ps)
    __attribute__((nonnull));
static bool is_command_delimiter_char(wchar_t c)
    __attribute__((const));
static bool is_comma_or_closing_bracket(wchar_t c)
    __attribute__((const));
static bool is_slash_or_closing_brace(wchar_t c)
    __attribute__((const));
static bool is_closing_brace(wchar_t c)
    __attribute__((const));
static bool has_token(const parsestate_T *ps, const wchar_t *t)
    __attribute__((pure,nonnull));
static const wchar_t *check_opening_token(parsestate_T *ps)
    __attribute__((nonnull));
static const wchar_t *check_closing_token(parsestate_T *ps)
    __attribute__((nonnull));
static and_or_T *parse_command_list(parsestate_T *ps)
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
static redir_T **parse_assignments_and_redirects(parsestate_T *ps, command_T *c)
    __attribute__((nonnull,malloc,warn_unused_result));
static void **parse_words_and_redirects(
	parsestate_T *ps, redir_T **redirlastp, bool first)
    __attribute__((nonnull,malloc,warn_unused_result));
static void parse_redirect_list(parsestate_T *ps, redir_T **lastp)
    __attribute__((nonnull));
static assign_T *tryparse_assignment(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static void **parse_words_to_paren(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static redir_T *tryparse_redirect(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static wordunit_T *parse_word(parsestate_T *ps, aliastype_T type)
    __attribute__((nonnull,malloc,warn_unused_result));
static wordunit_T *parse_word_to(parsestate_T *ps, bool testfunc(wchar_t c))
    __attribute__((nonnull,malloc,warn_unused_result));
static void skip_to_next_single_quote(parsestate_T *ps)
    __attribute__((nonnull));
static wordunit_T *parse_special_word_unit(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static wordunit_T *tryparse_paramexp_raw(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static wordunit_T *parse_paramexp_in_brace(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static wordunit_T *parse_cmdsubst_in_paren(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static embedcmd_T extract_command_in_paren(parsestate_T *ps)
    __attribute__((nonnull,warn_unused_result));
#if YASH_ENABLE_ALIAS
static wchar_t *extract_command_in_paren_unparsed(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
#endif
static wordunit_T *parse_cmdsubst_in_backquote(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static wordunit_T *tryparse_arith(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static wchar_t *parse_word_as_wcs(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *parse_compound_command(
	parsestate_T *ps, const wchar_t *command)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *parse_group(parsestate_T *ps, commandtype_T type)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *parse_if(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *parse_for(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *parse_while(parsestate_T *ps, bool whltype)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *parse_case(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static caseitem_T *parse_case_list(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static void **parse_case_patterns(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *parse_function(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static command_T *tryparse_function(parsestate_T *ps)
    __attribute__((nonnull,malloc,warn_unused_result));
static void read_heredoc_contents(parsestate_T *ps, redir_T *redir)
    __attribute__((nonnull));
static void read_heredoc_contents_without_expansion(
	parsestate_T *ps, redir_T *r)
    __attribute__((nonnull));
static void read_heredoc_contents_with_expansion(parsestate_T *ps, redir_T *r)
    __attribute__((nonnull));
static bool is_end_of_heredoc_contents(
	parsestate_T *ps, const wchar_t *eoc, size_t eoclen, bool skiptab)
    __attribute__((nonnull));
static wordunit_T *parse_string_to(
	parsestate_T *ps, bool backquote, bool stoponnewline)
    __attribute__((nonnull,malloc,warn_unused_result));
static const char *get_errmsg_unexpected_token(const wchar_t *t)
    __attribute__((nonnull));
static void print_errmsg_token_missing(parsestate_T *ps, const wchar_t *t)
    __attribute__((nonnull));

#define QUOTES L"\"'\\"


/* The functions below may return non-NULL even on error.
 * The error condition must be tested by the `cerror' flag.
 * It is set to true when `serror' is called. */
/* Every function named `parse_*' advances the current position (`cindex') to
 * the index of the first character that has not yet been parsed. */


/* The main entry point to the parser.
 * This function reads at least one line of input and parses it.
 * All the members of `info' except `lastinputresult' must have been initialized
 * beforehand.
 * The resulting parse tree is assigned to `*result' if successful. If there is
 * no command in the next line or the shell was interrupted while reading input,
 * `*result' is assigned NULL.
 * Returns PR_OK           if successful,
 *         PR_SYNTAX_ERROR if a syntax error occurred,
 *         PR_INPUT_ERROR  if an input error occurred, or
 *         PR_EOF          if the input reached the end of file (EOF).
 * If PR_SYNTAX_ERROR or PR_INPUT_ERROR is returned, at least one error message
 * has been printed in this function.
 * Note that `*result' is assigned if and only if the return value is PR_OK. */
parseresult_T read_and_parse(
	parseinfo_T *restrict info, and_or_T **restrict result)
{
    parsestate_T ps = {
	.cinfo = info,
	.cerror = false,
	.cindex = 0,
#if YASH_ENABLE_ALIAS
	.enable_alias = info->enable_alias,
	.reparse_alias = false,
	.caliases = NULL,
#endif
    };
    wb_init(&ps.cbuf);

    if (info->interactive) {
	struct input_interactive_info_T *intrinfo = info->inputinfo;
	intrinfo->prompttype = 1;
    }

    ps.cinfo->lastinputresult = INPUT_OK;
    switch (read_more_input(&ps)) {
	case INPUT_OK:
	    break;
	case INPUT_EOF:
	    wb_destroy(&ps.cbuf);
	    return PR_EOF;
	case INPUT_INTERRUPTED:
	    wb_destroy(&ps.cbuf);
	    *result = NULL;
	    return PR_OK;
	case INPUT_ERROR:
	    wb_destroy(&ps.cbuf);
	    return PR_INPUT_ERROR;
    }
    pl_init(&ps.pending_heredocs);

    and_or_T *r = parse_command_list(&ps);

    wb_destroy(&ps.cbuf);
    pl_destroy(&ps.pending_heredocs);
#if YASH_ENABLE_ALIAS
    destroy_aliaslist(ps.caliases);
#endif

    switch (ps.cinfo->lastinputresult) {
	case INPUT_OK:
	case INPUT_EOF:
	    if (ps.cerror) {
		andorsfree(r);
		return PR_SYNTAX_ERROR;
	    } else {
		assert(ps.cindex == ps.cbuf.length);
		*result = r;
		return PR_OK;
	    }
	case INPUT_INTERRUPTED:
	    andorsfree(r);
	    *result = NULL;
	    return PR_OK;
	case INPUT_ERROR:
	    andorsfree(r);
	    return PR_INPUT_ERROR;
    }
    assert(false);
}

/* Prints the specified error message to the standard error.
 * `format' is passed to `gettext' in this function.
 * `format' need not to have a trailing newline since a newline is automatically
 * appended in this function.
 * The flag `ps->cerror' is set to true in this function. */
void serror(parsestate_T *restrict ps, const char *restrict format, ...)
{
    va_list ap;

    if (ps->cinfo->print_errmsg &&
	    ps->cinfo->lastinputresult != INPUT_INTERRUPTED) {
	if (ps->cinfo->filename != NULL)
	    fprintf(stderr, "%s:%lu: ", ps->cinfo->filename, ps->cinfo->lineno);
	fprintf(stderr, gt("syntax error: "));
	va_start(ap, format);
	vfprintf(stderr, gt(format), ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
    }
    ps->cerror = true;
}

/* Reads the next line of input and returns the result type, which is assigned
 * to `ps->cinfo->lastinputresult'.
 * If `ps->cinfo->lastinputresult' is not INPUT_OK, it is simply returned
 * without reading any input.
 * If input is from an interactive terminal and `ps->cerror' is true, no input
 * is read and INPUT_INTERRUPTED is returned. */
inputresult_T read_more_input(parsestate_T *ps)
{
    if (ps->cerror && ps->cinfo->interactive)
	return INPUT_INTERRUPTED;
    if (ps->cinfo->lastinputresult == INPUT_OK) {
	size_t savelength = ps->cbuf.length;
	ps->cinfo->lastinputresult =
	    ps->cinfo->input(&ps->cbuf, ps->cinfo->inputinfo);

	if (ps->cinfo->enable_verbose && shopt_verbose)
#if YASH_ENABLE_LINEEDIT
	    if (!(le_state & LE_STATE_ACTIVE))
#endif
		fprintf(stderr, "%ls", &ps->cbuf.contents[savelength]);
    }
    return ps->cinfo->lastinputresult;
}

/* Removes a line continuation at the specified index in `ps->cbuf', increments
 * line number, and reads the next line. */
void line_continuation(parsestate_T *ps, size_t index)
{
    assert(ps->cbuf.contents[index] == L'\\' &&
	    ps->cbuf.contents[index + 1] == L'\n');
    wb_remove(&ps->cbuf, index, 2);
    ps->cinfo->lineno++;
    read_more_input(ps);
}

/* If a line continuation is found within `n' characters from the current
 * position `ps->cindex', removes the backslash-newline pair and reads the next
 * line.
 * If a backslash that is not a line continuation is found within `n' characters
 * from the current position, this function does nothing. */
/* For quickness, `n' should be as small as possible. */
void ensure_buffer(parsestate_T *ps, size_t n)
{
    size_t index = ps->cindex;
    if (ps->cbuf.contents[index] == L'\0')
	read_more_input(ps);
    while (index - ps->cindex < n) {
	switch (ps->cbuf.contents[index]) {
	case L'\0':  case L'\'':
	    return;
	case L'\\':
	    if (ps->cbuf.contents[index + 1] != L'\n')
		return;
	    assert(ps->cbuf.contents[index + 2] == L'\0');
	    line_continuation(ps, index);
	    if (ps->cinfo->lastinputresult != INPUT_OK)
		return;
	    break;
	default:
	    index++;
	    break;
	}
    }
}

/* Returns the length of the name at the current position.
 * Whether a character can be part of the name is determined by `isnamechar'.
 * This function processes line continuations and reads so many lines that the
 * variable/alias name under the current position is fully available. */
size_t count_name_length(parsestate_T *ps, bool isnamechar(wchar_t c))
{
    size_t saveindex = ps->cindex;
    while (ensure_buffer(ps, 1), isnamechar(ps->cbuf.contents[ps->cindex]))
	ps->cindex++;

    size_t result = ps->cindex - saveindex;
    ps->cindex = saveindex;
    return result;
}

/* Advances the current position `ps->cindex', skipping blank characters,
 * comments, and line continuations.
 * This function calls `read_more_input' if the current position is at the end
 * of line or when a line continuation is encountered.
 * The position is advanced to the next non-blank character.
 * Line continuations are actually removed rather than skipped. */
/* Note that a newline is not a blank character. After a comment was skipped,
 * the position will be the newline character (or EOF) that follows. */
void skip_blanks_and_comment(parsestate_T *ps)
{
    if (ps->cbuf.contents[ps->cindex] == L'\0')
	if (read_more_input(ps) != INPUT_OK)
	    return;

start:
    /* skip blanks */
    while (iswblank(ps->cbuf.contents[ps->cindex]))
	ps->cindex++;

    /* skip a comment */
    if (ps->cbuf.contents[ps->cindex] == L'#') {
	do {
	    ps->cindex++;
	} while (ps->cbuf.contents[ps->cindex] != L'\n' &&
		ps->cbuf.contents[ps->cindex] != L'\0');
    }

    /* remove line continuation */
    if (ps->cbuf.contents[ps->cindex] == L'\\' &&
	    ps->cbuf.contents[ps->cindex + 1] == L'\n') {
	line_continuation(ps, ps->cindex);
	goto start;
    }
}

/* Advances the current position `ps->cindex', skipping blank characters,
 * comments and newlines, up to the next token.
 * This function calls `read_more_input' if the next token cannot be found in
 * the current line.
 * Returns true iff at least one newline token is skipped. */
bool skip_to_next_token(parsestate_T *ps)
{
    bool newline = false;

    skip_blanks_and_comment(ps);
    while (ps->cinfo->lastinputresult == INPUT_OK
	    && ps->cbuf.contents[ps->cindex] == L'\n') {
	newline = true;
	next_line(ps);
	skip_blanks_and_comment(ps);
    }
    return newline;
}

/* Parses the NEWLINE token at the current position and proceeds to the next
 * line. The contents of pending here-documents is read if any. */
void next_line(parsestate_T *ps)
{
    assert(ps->cbuf.contents[ps->cindex] == L'\n');
    ps->cindex++;
    ps->cinfo->lineno++;
    assert(ps->cbuf.contents[ps->cindex] == L'\0');

    for (size_t i = 0; i < ps->pending_heredocs.length; i++)
	read_heredoc_contents(ps, ps->pending_heredocs.contents[i]);
    pl_clear(&ps->pending_heredocs, 0);
}

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

/* Checks if the specified character delimits a simple command. */
bool is_command_delimiter_char(wchar_t c)
{
    switch (c) {
	case L'\0':  case L'\n':  case L';':   case L'&':   case L'|':
	case L'(':   case L')':
	    return true;
	default:
	    return false;
    }
}

bool is_comma_or_closing_bracket(wchar_t c)
{
    return c == L']' || c == L',' || c == L'\0';
}

bool is_slash_or_closing_brace(wchar_t c)
{
    return c == L'/' || c == L'}' || c == L'\0';
}

bool is_closing_brace(wchar_t c)
{
    return c == L'}' || c == L'\0';
}

/* Checks if token `t' exists at the current position in `ps->cbuf'.
 * `t' must not be a proper substring of another operator token. (For example,
 * `t' cannot be L"&" because it is a proper substring of another operator token
 * L"&&". However, L"do" is OK for `t' even though it is a substring of the
 * keyword L"done", which is not an operator token.)
 * This function does not handle line continuations. The caller may need to call
 * `ensure_buffer(wcslen(t))' before calling this function. */
bool has_token(const parsestate_T *ps, const wchar_t *t)
{
    const wchar_t *c = matchwcsprefix(&ps->cbuf.contents[ps->cindex], t);
    return c != NULL && is_token_delimiter_char(*c);
}

/* Checks if there is an 'opening' token such as "(", "{", and "if" at the
 * current position. If there is one, the token string is returned.
 * Otherwise, NULL is returned.
 * This function calls `ensure_buffer(ps, 9)'. */
const wchar_t *check_opening_token(parsestate_T *ps)
{
    ensure_buffer(ps, 9);
    if (ps->cbuf.contents[ps->cindex] == L'(') return L"(";
    if (has_token(ps, L"{"))        return L"{";
    if (has_token(ps, L"if"))       return L"if";
    if (has_token(ps, L"for"))      return L"for";
    if (has_token(ps, L"while"))    return L"while";
    if (has_token(ps, L"until"))    return L"until";
    if (has_token(ps, L"case"))     return L"case";
    if (has_token(ps, L"function")) return L"function";
    return NULL;
}

/* Checks if there is a 'closing' token such as ")", "}", and "fi" at the
 * current position. If there is one, the token string is returned.
 * Otherwise, NULL is returned.
 * This function calls `ensure_buffer(ps, 5)'. */
/* Closing tokens delimit and/or lists. */
const wchar_t *check_closing_token(parsestate_T *ps)
{
    ensure_buffer(ps, 5);
    if (ps->cbuf.contents[ps->cindex] == L')')
	return L")";
    if (ps->cbuf.contents[ps->cindex] == L';' &&
	    ps->cbuf.contents[ps->cindex + 1] == L';')
	return L";;";
    if (has_token(ps, L"}"))    return L"}";
    if (has_token(ps, L"then")) return L"then";
    if (has_token(ps, L"else")) return L"else";
    if (has_token(ps, L"elif")) return L"elif";
    if (has_token(ps, L"fi"))   return L"fi";
    if (has_token(ps, L"do"))   return L"do";
    if (has_token(ps, L"done")) return L"done";
    if (has_token(ps, L"esac")) return L"esac";
    return NULL;
}

/* Parses up to the end of the current line.
 * You don't have to call `skip_blanks_and_comment' beforehand. */
and_or_T *parse_command_list(parsestate_T *ps)
{
    and_or_T *first = NULL, **lastp = &first;
    bool need_separator = false;
    /* For a command to be parsed after another, it must be separated by L"&",
     * L";", or newlines. */

    while (!ps->cerror) {
	skip_blanks_and_comment(ps);
	if (ps->cbuf.contents[ps->cindex] == L'\0') {
	    break;
	} else if (ps->cbuf.contents[ps->cindex] == L'\n') {
	    next_line(ps);
	    break;
	} else if (ps->cbuf.contents[ps->cindex] == L')') {
	    serror(ps, get_errmsg_unexpected_token(L")"), L")");
	    break;
	} else if (need_separator) {
	    serror(ps, Ngt("`;' or `&' is missing"));
	    break;
	}

	and_or_T *ao = parse_and_or_list(ps);
	if (ao != NULL) {
	    *lastp = ao;
	    lastp = &ao->next;
	}
#if YASH_ENABLE_ALIAS
	if (ps->reparse_alias) {
	    assert(ao == NULL);
	    continue;
	}
#endif

	need_separator = true;
	ensure_buffer(ps, 2);
	if (ps->cbuf.contents[ps->cindex] == L'&'
		|| (ps->cbuf.contents[ps->cindex] == L';'
		    && ps->cbuf.contents[ps->cindex + 1] != L';')) {
	    ps->cindex++;
	    need_separator = false;
	}
    }
#if YASH_ENABLE_ALIAS
    ps->reparse_alias = false;
#endif
    return first;
}

/* Parses commands until a closing token is found.
 * You don't have to call `skip_blanks_and_comment' beforehand. */
and_or_T *parse_compound_list(parsestate_T *ps)
{
    and_or_T *first = NULL, **lastp = &first;
    bool savecerror = ps->cerror;
    bool need_separator = false;
    /* For a command to be parsed after another, it must be separated by L"&",
     * L";", or newlines. */

    if (!ps->cinfo->interactive)
	ps->cerror = false;
    while (!ps->cerror) {
	if (skip_to_next_token(ps))
	    need_separator = false;
	if (need_separator
		|| ps->cbuf.contents[ps->cindex] == L'\0'
		|| check_closing_token(ps))
	    break;

	and_or_T *ao = parse_and_or_list(ps);
	if (ao != NULL) {
	    *lastp = ao;
	    lastp = &ao->next;
	}
#if YASH_ENABLE_ALIAS
	if (ps->reparse_alias) {
	    assert(ao == NULL);
	    continue;
	}
#endif

	need_separator = true;
	ensure_buffer(ps, 2);
	if (ps->cbuf.contents[ps->cindex] == L'&'
		|| (ps->cbuf.contents[ps->cindex] == L';'
		    && ps->cbuf.contents[ps->cindex + 1] != L';')) {
	    ps->cindex++;
	    need_separator = false;
	}
    }
    ps->cerror |= savecerror;
#if YASH_ENABLE_ALIAS
    ps->reparse_alias = false;
#endif
    return first;
}

/* Parses one and/or list.
 * The result reflects the trailing "&" or ";", but `ps->cindex' points to the
 * delimiter "&" or ";" when the function returns.
 * If the first word was alias-substituted, the `ps->reparse_alias' flag is set
 * and NULL is returned. */
and_or_T *parse_and_or_list(parsestate_T *ps)
{
    pipeline_T *p = parse_pipelines_in_and_or(ps);
#if YASH_ENABLE_ALIAS
    if (ps->reparse_alias) {
	assert(p == NULL);
	return NULL;
    }
#endif

    and_or_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->ao_pipelines = p;
    result->ao_async = (ps->cbuf.contents[ps->cindex] == L'&');
    return result;
}

/* Parses all pipelines in one and/or list.
 * If the first word was alias-substituted, the `ps->reparse_alias' flag is set
 * and NULL is returned. */
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
#if YASH_ENABLE_ALIAS
	if (ps->reparse_alias) {
	    assert(p == NULL);
	    if (first != NULL)
		goto next;
	    else
		break;
	}
#endif

	ensure_buffer(ps, 2);
	if (ps->cbuf.contents[ps->cindex] == L'&'
		&& ps->cbuf.contents[ps->cindex + 1] == L'&') {
	    cond = true;
	} else if (ps->cbuf.contents[ps->cindex] == L'|'
		&& ps->cbuf.contents[ps->cindex + 1] == L'|') {
	    cond = false;
	} else {
	    break;
	}
	ps->cindex += 2;
#if YASH_ENABLE_ALIAS
next:
#endif
	skip_to_next_token(ps);
    }
    return first;
}

/* Parses one pipeline.
 * If the first word was alias-substituted, the `ps->reparse_alias' flag is set
 * and NULL is returned. */
pipeline_T *parse_pipeline(parsestate_T *ps)
{
    bool neg;
    command_T *c;

    ensure_buffer(ps, 2);
    if (has_token(ps, L"!")) {
	neg = true;
	ps->cindex++;
#if YASH_ENABLE_ALIAS
	do {
#endif
	    skip_blanks_and_comment(ps);
	    c = parse_commands_in_pipeline(ps);
#if YASH_ENABLE_ALIAS
	    if (ps->reparse_alias)
		assert(c == NULL);
	} while (ps->reparse_alias);
#endif
    } else {
	neg = false;
	c = parse_commands_in_pipeline(ps);
#if YASH_ENABLE_ALIAS
	if (ps->reparse_alias) {
	    assert(c == NULL);
	    return NULL;
	}
#endif
    }

    pipeline_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->pl_commands = c;
    result->pl_neg = neg;
    result->pl_cond = false;
    return result;
}

/* Parses the body of the pipeline.
 * If the first word was alias-substituted, the `ps->reparse_alias' flag is set
 * and NULL is returned. */
command_T *parse_commands_in_pipeline(parsestate_T *ps)
{
    command_T *first = NULL, **lastp = &first;

    for (;;) {
	command_T *c = parse_command(ps);
	if (c != NULL) {
	    *lastp = c;
	    lastp = &c->next;
	}
#if YASH_ENABLE_ALIAS
	if (ps->reparse_alias) {
	    assert(c == NULL);
	    if (first != NULL)
		goto next;
	    else
		break;
	}
#endif

	ensure_buffer(ps, 2);
	if (ps->cbuf.contents[ps->cindex] == L'|' &&
		ps->cbuf.contents[ps->cindex + 1] != L'|') {
	    ps->cindex++;
	} else {
	    break;
	}
#if YASH_ENABLE_ALIAS
next:
#endif
	skip_to_next_token(ps);
    }
    return first;
}

/* Parses one command.
 * If the first word was alias-substituted, the `ps->reparse_alias' flag is set
 * and NULL is returned. */
command_T *parse_command(parsestate_T *ps)
{
#if YASH_ENABLE_ALIAS
    ps->reparse_alias = false;
#endif

    /* Note: `check_closing_token' calls `ensure_buffer(ps, 5)'. */
    const wchar_t *t = check_closing_token(ps);
    if (t != NULL) {
	serror(ps, get_errmsg_unexpected_token(t), t);
	return NULL;
    } else if (has_token(ps, L"!")) {
	serror(ps, get_errmsg_unexpected_token(L"!"), L"!");
	return NULL;
    } else if (has_token(ps, L"in")) {
	serror(ps, get_errmsg_unexpected_token(L"in"), L"in");
	return NULL;
    } else if (ps->cbuf.contents[ps->cindex] == L'(') {
	return parse_compound_command(ps, L"(");
    } else if (is_command_delimiter_char(ps->cbuf.contents[ps->cindex])) {
	if (ps->cbuf.contents[ps->cindex] == L'\0' ||
		ps->cbuf.contents[ps->cindex] == L'\n')
	    serror(ps, Ngt("a command is missing at the end of input"));
	else
	    serror(ps, Ngt("a command is missing before `%lc'"),
		    (wint_t) ps->cbuf.contents[ps->cindex]);
	return NULL;
    }

    t = check_opening_token(ps);
    if (t != NULL)
	return parse_compound_command(ps, t);

#if YASH_ENABLE_ALIAS
    if (ps->enable_alias && count_name_length(ps, is_alias_name_char) > 0) {
	substaliasflags_T flags = AF_NONGLOBAL | AF_NORECUR;
	if (substitute_alias(&ps->cbuf, ps->cindex, &ps->caliases, flags)) {
	    ps->reparse_alias = true;
	    return NULL;
	}
    }
#endif

    command_T *result = tryparse_function(ps);
    if (result != NULL)
	return result;

    /* parse as a simple command */
    result = xmalloc(sizeof *result);
    redir_T **redirlastp;

    result->next = NULL;
    result->refcount = 1;
    result->c_lineno = ps->cinfo->lineno;
    result->c_type = CT_SIMPLE;
    redirlastp = parse_assignments_and_redirects(ps, result);
    result->c_words = parse_words_and_redirects(ps, redirlastp, true);

    ensure_buffer(ps, 1);
    if (ps->cbuf.contents[ps->cindex] == L'(')
	serror(ps, Ngt("invalid use of `%lc'"), (wint_t) L'(');
    return result;
}

/* Parses assignments and redirections.
 * Tokens but the first one are subject to any-type alias substitution,
 * including the word just after the parsed assignments and redirections.
 * The results are assigned to `c->c_assigns' and `c->c_redirs'.
 * The return value is a pointer to the `next' member of the last resultant
 * redirection (redir_T). If no redirections were parsed, the result value is a
 * pointer to `c->c_redirs'. */
redir_T **parse_assignments_and_redirects(parsestate_T *ps, command_T *c)
{
    assign_T **assgnlastp = &c->c_assigns;
    redir_T **redirlastp = &c->c_redirs;
    assign_T *assgn;
    redir_T *redir;

    c->c_assigns = NULL;
    c->c_redirs = NULL;
    while (ensure_buffer(ps, 1),
	    !is_command_delimiter_char(ps->cbuf.contents[ps->cindex])) {
	if ((redir = tryparse_redirect(ps)) != NULL) {
	    *redirlastp = redir;
	    redirlastp = &redir->next;
	} else if ((assgn = tryparse_assignment(ps)) != NULL) {
	    *assgnlastp = assgn;
	    assgnlastp = &assgn->next;
	} else {
	    break;
	}
#if YASH_ENABLE_ALIAS
	if (ps->enable_alias && count_name_length(ps, is_alias_name_char) > 0) {
	    substitute_alias(
		    &ps->cbuf, ps->cindex, &ps->caliases, AF_NONGLOBAL);
	    skip_blanks_and_comment(ps);
	}
#endif
    }
    return redirlastp;
}

/* Parses words and redirections.
 * The parsing result of redirections is assigned to `*redirlastp'
 * The parsing result of assignments is returned as an array of pointers to
 * word units that are cast to (void *).
 * `*redirlastp' must have been initialized to NULL beforehand.
 * If `first' is true, alias substitution is not performed on the first word. */
void **parse_words_and_redirects(
	parsestate_T *ps, redir_T **redirlastp, bool first)
{
    plist_T wordlist;
    redir_T *redir;
    wordunit_T *word;

    pl_init(&wordlist);
    while (ensure_buffer(ps, 1),
	    !is_command_delimiter_char(ps->cbuf.contents[ps->cindex])) {
#if YASH_ENABLE_ALIAS
	if (!first) {
	    if (ps->enable_alias &&
		    count_name_length(ps, is_alias_name_char) > 0) {
		substitute_alias(&ps->cbuf, ps->cindex, &ps->caliases, 0);
		skip_blanks_and_comment(ps);
	    }
	}
#endif
	if ((redir = tryparse_redirect(ps)) != NULL) {
	    *redirlastp = redir;
	    redirlastp = &redir->next;
	} else if ((word = parse_word(ps, AT_NONE)) != NULL) {
	    pl_add(&wordlist, word);
	    skip_blanks_and_comment(ps);
	    first = false;
	} else {
	    break;
	}
    }
    return pl_toary(&wordlist);
}

/* Parses as many redirections as possible.
 * The parsing result is assigned to `*redirlastp'
 * `*redirlastp' must have been initialized to NULL beforehand. */
void parse_redirect_list(parsestate_T *ps, redir_T **lastp)
{
    for (;;) {
#if YASH_ENABLE_ALIAS
	if (!posixly_correct && ps->enable_alias)
	    if (count_name_length(ps, is_alias_name_char) > 0)
		substitute_alias(&ps->cbuf, ps->cindex, &ps->caliases, 0);
#endif

	redir_T *redir = tryparse_redirect(ps);
	if (redir == NULL)
	    break;
	*lastp = redir;
	lastp = &redir->next;
    }
}

/* If there is an assignment at the current position, parses and returns it.
 * Otherwise, returns NULL without moving the position. */
assign_T *tryparse_assignment(parsestate_T *ps)
{
    if (iswdigit(ps->cbuf.contents[ps->cindex]))
	return NULL;

    size_t namelen = count_name_length(ps, is_name_char);
    if (namelen == 0 || ps->cbuf.contents[ps->cindex + namelen] != L'=')
	return NULL;

    assign_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->a_name = xwcsndup(&ps->cbuf.contents[ps->cindex], namelen);
    ps->cindex += namelen + 1;

    ensure_buffer(ps, 1);
    if (posixly_correct || ps->cbuf.contents[ps->cindex] != L'(') {
	result->a_type = A_SCALAR;
	result->a_scalar = parse_word(ps, AT_NONE);
    } else {
	ps->cindex++;
	skip_to_next_token(ps);
	result->a_type = A_ARRAY;
	result->a_array = parse_words_to_paren(ps);
	if (ps->cbuf.contents[ps->cindex] == L')')
	    ps->cindex++;
	else
	    serror(ps, Ngt("`%ls' is missing"), L")");
    }
    skip_blanks_and_comment(ps);
    return result;
}

/* Parses words until the next closing parentheses.
 * Delimiter characters other than ')' and '\n' are not allowed.
 * Returns a newly malloced array of pointers to newly malloced `wordunit_T's.*/
void **parse_words_to_paren(parsestate_T *ps)
{
    plist_T list;

    pl_init(&list);
    while (ps->cbuf.contents[ps->cindex] != L')') {
	wordunit_T *word = parse_word(ps, AT_GLOBAL);
	if (word != NULL)
	    pl_add(&list, word);
	else
	    break;
	skip_to_next_token(ps);
    }
    return pl_toary(&list);
}

/* If there is a redirection at the current position, parses and returns it.
 * Otherwise, returns NULL without moving the position. */
redir_T *tryparse_redirect(parsestate_T *ps)
{
    int fd;

    ensure_buffer(ps, 2);
    if (iswdigit(ps->cbuf.contents[ps->cindex])) {
	unsigned long lfd;
	wchar_t *endptr;

reparse:
	errno = 0;
	lfd = wcstoul(&ps->cbuf.contents[ps->cindex], &endptr, 10);
	if (errno != 0 || lfd > INT_MAX)
	    fd = -1;  /* invalid fd */
	else
	    fd = (int) lfd;
	if (endptr[0] == L'\\' && endptr[1] == L'\n') {
	    line_continuation(ps, endptr - ps->cbuf.contents);
	    goto reparse;
	} else if (endptr[0] != L'<' && endptr[0] != L'>') {
	    return NULL;
	}
	ps->cindex = endptr - ps->cbuf.contents;
    } else if (ps->cbuf.contents[ps->cindex] == L'<') {
	fd = STDIN_FILENO;
    } else if (ps->cbuf.contents[ps->cindex] == L'>') {
	fd = STDOUT_FILENO;
    } else {
	return NULL;
    }

    redir_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->rd_fd = fd;
    ensure_buffer(ps, 3);
    switch (ps->cbuf.contents[ps->cindex]) {
    case L'<':
	switch (ps->cbuf.contents[ps->cindex + 1]) {
	case L'<':
	    if (ps->cbuf.contents[ps->cindex + 2] == L'-') {
		result->rd_type = RT_HERERT;
		ps->cindex += 3;
	    } else if (!posixly_correct &&
		    ps->cbuf.contents[ps->cindex + 2] == L'<') {
		result->rd_type = RT_HERESTR;
		ps->cindex += 3;
	    } else {
		result->rd_type = RT_HERE;
		ps->cindex += 2;
	    }
	    break;
	case L'(':
	    if (!posixly_correct) {
		result->rd_type = RT_PROCIN;
		goto parse_command;
	    } else {
		result->rd_type = RT_INPUT;
		ps->cindex += 1;
	    }
	    break;
	case L'>':  result->rd_type = RT_INOUT;  ps->cindex += 2;  break;
	case L'&':  result->rd_type = RT_DUPIN;  ps->cindex += 2;  break;
	default:    result->rd_type = RT_INPUT;  ps->cindex += 1;  break;
	}
	break;
    case L'>':
	switch (ps->cbuf.contents[ps->cindex + 1]) {
	case L'(':
	    if (!posixly_correct) {
		result->rd_type = RT_PROCOUT;
		goto parse_command;
	    } else {
		result->rd_type = RT_OUTPUT;
		ps->cindex += 1;
	    }
	    break;
	case L'>':
	    if (!posixly_correct && ps->cbuf.contents[ps->cindex + 2] == L'|') {
		result->rd_type = RT_PIPE;
		ps->cindex += 3;
	    } else {
		result->rd_type = RT_APPEND;
		ps->cindex += 2;
	    }
	    break;
	case L'|':  result->rd_type = RT_CLOBBER; ps->cindex += 2;  break;
	case L'&':  result->rd_type = RT_DUPOUT;  ps->cindex += 2;  break;
	default:    result->rd_type = RT_OUTPUT;  ps->cindex += 1;  break;
	}
	break;
    default:
	assert(false);
    }
    skip_blanks_and_comment(ps);
    if (result->rd_type != RT_HERE && result->rd_type != RT_HERERT) {
	result->rd_filename = parse_word(ps, AT_GLOBAL);
	if (result->rd_filename == NULL) {
	    serror(ps, Ngt("the redirection target is missing"));
	    free(result);
	    return NULL;
	}
    } else {
	wchar_t *endofheredoc = parse_word_as_wcs(ps);
	if (endofheredoc[0] == L'\0') {
	    serror(ps, Ngt("the end-of-here-document indicator is missing"));
	    free(endofheredoc);
	    free(result);
	    return NULL;
	}
	result->rd_hereend = endofheredoc;
	result->rd_herecontent = NULL;
	pl_add(&ps->pending_heredocs, result);
    }
    skip_blanks_and_comment(ps);
    return result;

parse_command:
    ps->cindex += 1;
    result->rd_command = extract_command_in_paren(ps);
    ensure_buffer(ps, 1);
    if (ps->cbuf.contents[ps->cindex] == L')')
	ps->cindex++;
    skip_blanks_and_comment(ps);
    return result;
}

/* Expands an alias, if any, and parses a word at the current position.
 * `type' specifies the type of aliases to be expanded.  */
wordunit_T *parse_word(parsestate_T *ps, aliastype_T type)
{
#if YASH_ENABLE_ALIAS
    if (ps->enable_alias) {
	switch (type) {
	case AT_NONE:
	    break;
	case AT_GLOBAL:
	case AT_ALL:
	    if (count_name_length(ps, is_alias_name_char) > 0) {
		substaliasflags_T flags =
		    (type == AT_GLOBAL) ? 0 : AF_NONGLOBAL;
		substitute_alias(&ps->cbuf, ps->cindex, &ps->caliases, flags);
	    }
	    skip_blanks_and_comment(ps);
	    break;
	}
    }
#else /* !YASH_ENABLE_ALIAS */
    (void) type;
#endif

    return parse_word_to(ps, is_token_delimiter_char);
}

/* Parses a word at the current position.
 * `testfunc' is a function that determines if a character is a word delimiter.
 * It must return true for L'\0'.
 * The parsing proceeds up to an unescaped character for which `testfunc'
 * returns false.
 * It is not an error if there is no characters to be a word, in which case
 * NULL is returned. */
wordunit_T *parse_word_to(parsestate_T *ps, bool testfunc(wchar_t c))
{
    wordunit_T *first = NULL, **lastp = &first, *wu;
    bool indq = false;  /* in double quotes? */
    size_t startindex = ps->cindex;

/* appends the substring from `startindex' to `cindex' as a new word unit
 * to `*lastp' */
#define MAKE_WORDUNIT_STRING                                                  \
    do {                                                                      \
        if (startindex != ps->cindex) {                                       \
            wordunit_T *w = xmalloc(sizeof *w);                               \
            w->next = NULL;                                                   \
            w->wu_type = WT_STRING;                                           \
            w->wu_string = xwcsndup(                                          \
                    ps->cbuf.contents + startindex, ps->cindex - startindex); \
            *lastp = w;                                                       \
            lastp = &w->next;                                                 \
        }                                                                     \
    } while (0)

    while (indq ? ps->cbuf.contents[ps->cindex] != L'\0'
		: !testfunc(ps->cbuf.contents[ps->cindex])) {

	switch (ps->cbuf.contents[ps->cindex]) {
	case L'\\':
	    if (ps->cbuf.contents[ps->cindex + 1] == L'\n') {
		line_continuation(ps, ps->cindex);
		continue;
	    } else if (ps->cbuf.contents[ps->cindex + 1] != L'\0') {
		ps->cindex += 2;
		continue;
	    }
	    break;
	case L'\n':
	    ps->cinfo->lineno++;
	    read_more_input(ps);
	    break;
	case L'$':
	case L'`':
	    MAKE_WORDUNIT_STRING;
	    wu = parse_special_word_unit(ps);
	    startindex = ps->cindex;
	    if (wu) {
		*lastp = wu;
		lastp = &wu->next;
		continue;
	    } else if (ps->cbuf.contents[ps->cindex] == L'\0') {
		continue;
	    }
	    break;
	case L'\'':
	    if (!indq) {
		ps->cindex++;
		skip_to_next_single_quote(ps);
		if (ps->cbuf.contents[ps->cindex] == L'\'')
		    ps->cindex++;
		continue;
	    }
	    break;
	case L'"':
	    indq = !indq;
	    /* falls thru! */
	default:
	    break;
	}
	ps->cindex++;
    }
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
	switch (ps->cbuf.contents[ps->cindex]) {
	case L'\'':
	    return;
	case L'\0':
	    if (read_more_input(ps) != INPUT_OK) {
		serror(ps, Ngt("the single quotation is not closed"));
		return;
	    }
	    continue;
	case L'\n':
	    ps->cinfo->lineno++;
	    /* falls thru! */
	default:
	    break;
	}
	ps->cindex++;
    }
}

/* Parses a parameter expansion or command substitution that starts with '$' or
 * '`'. The character at the current position must be '$' or '`' when this
 * function is called and the position is advanced to right after the expansion
 * or substitution.
 * If the character at the current position is '$' but it is not an expansion,
 * the position is not moved and the return value is NULL. Otherwise, The
 * position is advanced by at least one character. */
wordunit_T *parse_special_word_unit(parsestate_T *ps)
{
    switch (ps->cbuf.contents[ps->cindex++]) {
    case L'$':
	ensure_buffer(ps, 2);
	switch (ps->cbuf.contents[ps->cindex]) {
	case L'{':
	    return parse_paramexp_in_brace(ps);
	case L'(':
	    if (ps->cbuf.contents[ps->cindex + 1] == L'(') {
		wordunit_T *wu = tryparse_arith(ps);
		if (wu != NULL)
		    return wu;
	    }
	    return parse_cmdsubst_in_paren(ps);
	default:
	    return tryparse_paramexp_raw(ps);
	}
    case L'`':
	return parse_cmdsubst_in_backquote(ps);
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
    paramexp_T *pe;
    size_t namelen;  /* parameter name length */

    ensure_buffer(ps, 1);
    switch (ps->cbuf.contents[ps->cindex]) {
	case L'@':  case L'*':  case L'#':  case L'?':
	case L'-':  case L'$':  case L'!':
	    namelen = 1;
	    goto success;
    }
    if (!is_portable_name_char(ps->cbuf.contents[ps->cindex]))
	goto error;
    if (iswdigit(ps->cbuf.contents[ps->cindex]))
	namelen = 1;
    else
	namelen = count_name_length(ps, is_portable_name_char);

success:
    pe = xmalloc(sizeof *pe);
    pe->pe_type = PT_NONE;
    pe->pe_name = xwcsndup(&ps->cbuf.contents[ps->cindex], namelen);
    pe->pe_start = pe->pe_end = pe->pe_match = pe->pe_subst = NULL;

    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_PARAM;
    result->wu_param = pe;
    ps->cindex += namelen;
    return result;

error:
    ps->cindex--;
    assert(ps->cbuf.contents[ps->cindex] == L'$');
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

    assert(ps->cbuf.contents[ps->cindex] == L'{');
    ps->cindex++;

    /* parse PT_NUMBER */
    ensure_buffer(ps, 3);
    if (ps->cbuf.contents[ps->cindex] == L'#') {
	switch (ps->cbuf.contents[ps->cindex + 1]) {
	    case L'\0': case L'}':
	    case L'+':  case L'=':  case L':':  case L'/':  case L'%':
		break;
	    case L'-':  case L'?':  case L'#':
		if (ps->cbuf.contents[ps->cindex + 2] != L'}')
		    break;
		/* falls thru! */
	    default:
		pe->pe_type |= PT_NUMBER;
		ps->cindex++;
		break;
	}
    }

    /* parse nested expansion */
    // ensure_buffer(2);  // we've already called `ensure_buffer'
    if (!posixly_correct && ps->cbuf.contents[ps->cindex] == L'{') {
	pe->pe_type |= PT_NEST;
	pe->pe_nest = parse_paramexp_in_brace(ps);
    } else if (!posixly_correct
	    && (ps->cbuf.contents[ps->cindex] == L'`'
		|| (ps->cbuf.contents[ps->cindex] == L'$'
		    && (ps->cbuf.contents[ps->cindex + 1] == L'{'
			|| ps->cbuf.contents[ps->cindex + 1] == L'(')))) {
	size_t neststartindex = ps->cindex;
	pe->pe_nest = parse_special_word_unit(ps);
	if (ps->cindex != neststartindex)
	    pe->pe_type |= PT_NEST;
	else
	    goto parse_name;
    } else {
parse_name:;
	/* no nesting: parse parameter name normally */
	size_t namestartindex = ps->cindex;
	switch (ps->cbuf.contents[ps->cindex]) {
	    case L'@':  case L'*':  case L'#':  case L'?':
	    case L'-':  case L'$':  case L'!':
		ps->cindex++;
		break;
	    default:
		while (ensure_buffer(ps, 1),
			is_name_char(ps->cbuf.contents[ps->cindex]))
		    ps->cindex++;
		break;
	}
	size_t namelen = ps->cindex - namestartindex;
	if (namelen == 0) {
	    serror(ps, Ngt("the parameter name is missing or invalid"));
	    goto end;
	}
	pe->pe_name = xwcsndup(&ps->cbuf.contents[namestartindex], namelen);
    }

    /* parse indices */
    ensure_buffer(ps, 3);
    if (!posixly_correct && ps->cbuf.contents[ps->cindex] == L'[') {
	ps->cindex++;
	pe->pe_start = parse_word_to(ps, is_comma_or_closing_bracket);
	if (pe->pe_start == NULL)
	    serror(ps, Ngt("the index is missing"));
	if (ps->cbuf.contents[ps->cindex] == L',') {
	    ps->cindex++;
	    pe->pe_end = parse_word_to(ps, is_comma_or_closing_bracket);
	    if (pe->pe_end == NULL)
		serror(ps, Ngt("the index is missing"));
	}
	if (ps->cbuf.contents[ps->cindex] == L']')
	    ps->cindex++;
	else
	    serror(ps, Ngt("`%ls' is missing"), L"]");
	ensure_buffer(ps, 3);
    }

    /* parse PT_COLON */
    if (ps->cbuf.contents[ps->cindex] == L':') {
	pe->pe_type |= PT_COLON;
	ps->cindex++;
    }

    /* parse '-', '+', '#', etc. */
    switch (ps->cbuf.contents[ps->cindex]) {
    case L'-':   pe->pe_type |= PT_MINUS;                    goto parse_subst;
    case L'+':   pe->pe_type |= PT_PLUS;                     goto parse_subst;
    case L'=':   pe->pe_type |= PT_ASSIGN;                   goto parse_subst;
    case L'?':   pe->pe_type |= PT_ERROR;                    goto parse_subst;
    case L'#':   pe->pe_type |= PT_MATCH | PT_MATCHHEAD;     goto parse_match;
    case L'%':   pe->pe_type |= PT_MATCH | PT_MATCHTAIL;     goto parse_match;
    case L'/':   pe->pe_type |= PT_SUBST | PT_MATCHLONGEST;  goto parse_match;
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
		(wint_t) ps->cbuf.contents[ps->cindex]);
	goto end;
    }

parse_match:
    if (pe->pe_type & PT_COLON) {
	if ((pe->pe_type & PT_MASK) == PT_SUBST)
	    pe->pe_type |= PT_MATCHHEAD | PT_MATCHTAIL;
	else
	    serror(ps, Ngt("invalid use of `%lc' in parameter expansion"),
		    (wint_t) L':');
	ps->cindex += 1;
    } else if (ps->cbuf.contents[ps->cindex] ==
	    ps->cbuf.contents[ps->cindex + 1]) {
	if ((pe->pe_type & PT_MASK) == PT_MATCH)
	    pe->pe_type |= PT_MATCHLONGEST;
	else
	    pe->pe_type |= PT_SUBSTALL;
	ps->cindex += 2;
    } else if (ps->cbuf.contents[ps->cindex] == L'/') {
	if (ps->cbuf.contents[ps->cindex + 1] == L'#') {
	    pe->pe_type |= PT_MATCHHEAD;
	    ps->cindex += 2;
	} else if (ps->cbuf.contents[ps->cindex + 1] == L'%') {
	    pe->pe_type |= PT_MATCHTAIL;
	    ps->cindex += 2;
	} else {
	    ps->cindex += 1;
	}
    } else {
	ps->cindex += 1;
    }
    if ((pe->pe_type & PT_MASK) == PT_MATCH) {
	pe->pe_match = parse_word_to(ps, is_closing_brace);
	goto check_closing_brace;
    } else {
	pe->pe_match = parse_word_to(ps, is_slash_or_closing_brace);
	ensure_buffer(ps, 1);
	if (ps->cbuf.contents[ps->cindex] != L'/')
	    goto check_closing_brace;
    }

parse_subst:
    ps->cindex++;
    pe->pe_subst = parse_word_to(ps, is_closing_brace);

check_closing_brace:
    ensure_buffer(ps, 1);
    if (ps->cbuf.contents[ps->cindex] == L'}')
	ps->cindex++;
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
 * The current position must be at the opening parenthesis L'(' when this
 * function is called and the position is advanced to the closing parenthesis
 * L')'. */
wordunit_T *parse_cmdsubst_in_paren(parsestate_T *ps)
{
    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_CMDSUB;
    result->wu_cmdsub = extract_command_in_paren(ps);

    ensure_buffer(ps, 1);
    if (ps->cbuf.contents[ps->cindex] == L')')
	ps->cindex++;
    else
	serror(ps, Ngt("`%ls' is missing"), L")");
    return result;
}

/* Extracts commands between '(' and ')'.
 * The current position must be at the opening parenthesis L'(' when this
 * function is called. The position is advanced to the closing parenthesis
 * L')'. */
embedcmd_T extract_command_in_paren(parsestate_T *ps)
{
    plist_T save_pending_heredocs;
    embedcmd_T result;

    assert(ps->cbuf.contents[ps->cindex] == L'(');

    save_pending_heredocs = ps->pending_heredocs;
    pl_init(&ps->pending_heredocs);

#if YASH_ENABLE_ALIAS
    if (posixly_correct && ps->cinfo->enable_alias) {
	result.is_preparsed = false;
	result.value.unparsed = extract_command_in_paren_unparsed(ps);
    } else
#endif
    {
	ps->cindex++;
	result.is_preparsed = true;
	result.value.preparsed = parse_compound_list(ps);
    }

    pl_destroy(&ps->pending_heredocs);
    ps->pending_heredocs = save_pending_heredocs;

    return result;
}

#if YASH_ENABLE_ALIAS
/* Parses commands between '(' and ')'.
 * The current position must be at the opening parenthesis L'(' when this
 * function is called. The position is advanced to the closing parenthesis
 * L')'. */
wchar_t *extract_command_in_paren_unparsed(parsestate_T *ps)
{
    bool save_enable_alias = ps->enable_alias;
    ps->enable_alias = false;

    size_t startindex = ++ps->cindex;
    andorsfree(parse_compound_list(ps));
    assert(startindex <= ps->cindex);

    wchar_t *result = xwcsndup(
	    &ps->cbuf.contents[startindex], ps->cindex - startindex);

    ps->enable_alias = save_enable_alias;
    return result;
}
#endif

/* Parses a command substitution enclosed by backquotes.
 * When this function is called, the current position must be at the character
 * that just follows the opening backquote L'`'. This function advances the
 * position to the character that just follows the closing backquote L'~'. */
wordunit_T *parse_cmdsubst_in_backquote(parsestate_T *ps)
{
    xwcsbuf_T buf;
    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_CMDSUB;
    result->wu_cmdsub.is_preparsed = false;

    assert(ps->cbuf.contents[ps->cindex - 1] == L'`');
    wb_init(&buf);
    for (;;) {
	ensure_buffer(ps, 1);
	switch (ps->cbuf.contents[ps->cindex]) {
	case L'`':
	    ps->cindex++;
	    goto end;
	case L'\0':
	    if (read_more_input(ps) != 0) {
		serror(ps,
		    Ngt("the backquoted command substitution is not closed"));
		goto end;
	    }
	    break;
	case L'\\':
	    ps->cindex++;
	    switch (ps->cbuf.contents[ps->cindex]) {
		case L'$':  case L'`':  case L'\\':
		    goto default_;
		default:
		    wb_wccat(&buf, L'\\');
		    continue;
	    }
	case L'\n':
	    ps->cinfo->lineno++;
	    /* falls thru! */
	default:  default_:
	    wb_wccat(&buf, ps->cbuf.contents[ps->cindex]);
	    ps->cindex++;
	    break;
	}
    }
end:
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
    size_t savecindex = ps->cindex;
    assert(ps->cbuf.contents[ps->cindex] == L'(' &&
	    ps->cbuf.contents[ps->cindex + 1] == L'(');
    ps->cindex += 2;

    wordunit_T *first = NULL, **lastp = &first;
    size_t startindex = ps->cindex;
    int nestparen = 0;

    for (;;) {
	ensure_buffer(ps, 1);
	switch (ps->cbuf.contents[ps->cindex]) {
	case L'\0':
	    goto fail;
	case L'\\':
	    if (ps->cbuf.contents[ps->cindex + 1] == L'\n') {
		line_continuation(ps, ps->cindex);
		continue;
	    } else if (ps->cbuf.contents[ps->cindex + 1] != L'\0') {
		ps->cindex += 2;
		continue;
	    }
	    break;
	case L'$':
	case L'`':
	    MAKE_WORDUNIT_STRING;
	    wordunit_T *wu = parse_special_word_unit(ps);
	    startindex = ps->cindex;
	    if (wu != NULL) {
		*lastp = wu;
		lastp = &wu->next;
		continue;
	    } else if (ps->cbuf.contents[ps->cindex] == L'\0') {
		continue;
	    }
	    break;
	case L'(':
	    nestparen++;
	    break;
	case L')':
	    nestparen--;
	    if (nestparen < 0) {
		ensure_buffer(ps, 2);
		if (ps->cbuf.contents[ps->cindex + 1] == L')')
		    goto end;
		else
		    goto fail;
	    }
	    break;
	default:
	    break;
	}
	ps->cindex++;
    }
end:
    MAKE_WORDUNIT_STRING;
    ps->cindex += 2;

    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_ARITH;
    result->wu_arith = first;
    return result;

fail:
    wordfree(first);
    ps->cindex = savecindex;
    return NULL;
}

/* Returns a word token at the current index as a newly malloced string.
 * The current position is advanced to the character that just follows the word.
 * This function never returns NULL, but may return an empty string. */
wchar_t *parse_word_as_wcs(parsestate_T *ps)
{
    size_t startindex = ps->cindex;
    wordfree(parse_word(ps, AT_GLOBAL));
    assert(startindex <= ps->cindex);
    return xwcsndup(&ps->cbuf.contents[startindex], ps->cindex - startindex);
}

/* Parses a compound command.
 * `command' is the name of the command to parse such as "(" and "if". */
command_T *parse_compound_command(parsestate_T *ps, const wchar_t *command)
{
    /* `parse_group', `parse_if', etc. don't call `skip_blanks_and_comment'
     * before they return nor parse redirections. */
    command_T *result;
    switch (command[0]) {
    case L'(':
	result = parse_group(ps, CT_SUBSHELL);
	break;
    case L'{':
	result = parse_group(ps, CT_GROUP);
	break;
    case L'i':
	result = parse_if(ps);
	break;
    case L'f':
	switch (command[1]) {
	    case L'o':
		result = parse_for(ps);
		break;
	    case L'u':
		result = parse_function(ps);
		break;
	    default:
		assert(false);
	}
	break;
    case L'w':
	result = parse_while(ps, true);
	break;
    case L'u':
	result = parse_while(ps, false);
	break;
    case L'c':
	result = parse_case(ps);
	break;
    default:
	assert(false);
    }
    skip_blanks_and_comment(ps);
    parse_redirect_list(ps, &result->c_redirs);
    return result;
}

/* Parses a command group.
 * `type' must be either CT_GROUP or CT_SUBSHELL. */
command_T *parse_group(parsestate_T *ps, commandtype_T type)
{
    const wchar_t *start, *end;

    switch (type) {
	case CT_GROUP:
	    start = L"{", end = L"}";
	    assert(has_token(ps, start));
	    break;
	case CT_SUBSHELL:
	    start = L"(", end = L")";
	    assert(ps->cbuf.contents[ps->cindex] == start[0]);
	    break;
	default:
	    assert(false);
    }
    ps->cindex++;

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = type;
    result->c_lineno = ps->cinfo->lineno;
    result->c_redirs = NULL;
    result->c_subcmds = parse_compound_list(ps);
    if (posixly_correct && result->c_subcmds == NULL)
	serror(ps, Ngt("commands are missing between `%ls' and `%ls'"),
		start, end);
    if (ps->cbuf.contents[ps->cindex] == end[0])
	ps->cindex++;
    else
	print_errmsg_token_missing(ps, end);
    return result;
}

/* Parses a if command */
command_T *parse_if(parsestate_T *ps)
{
    assert(has_token(ps, L"if"));
    ps->cindex += 2;

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_IF;
    result->c_lineno = ps->cinfo->lineno;
    result->c_redirs = NULL;
    result->c_ifcmds = NULL;

    ifcommand_T **lastp = &result->c_ifcmds;
    bool els = false;
    while (!ps->cerror) {
	ifcommand_T *ic = xmalloc(sizeof *ic);
	*lastp = ic;
	lastp = &ic->next;
	ic->next = NULL;
	if (!els) {
	    ic->ic_condition = parse_compound_list(ps);
	    if (posixly_correct && ic->ic_condition == NULL)
		serror(ps, Ngt("commands are missing between `%ls' and `%ls'"),
			(result->c_ifcmds->next == NULL) ? L"if" : L"elif",
			L"then");
	    ensure_buffer(ps, 5);
	    if (has_token(ps, L"then"))
		ps->cindex += 4;
	    else
		print_errmsg_token_missing(ps, L"then");
	} else {
	    ic->ic_condition = NULL;
	}
	ic->ic_commands = parse_compound_list(ps);
	if (posixly_correct && ic->ic_commands == NULL)
	    serror(ps, Ngt("commands are missing after `%ls'"),
		    els ? L"else" : L"then");
	ensure_buffer(ps, 5);
	if (!els) {
	    if (has_token(ps, L"else")) {
		ps->cindex += 4;
		els = true;
	    } else if (has_token(ps, L"elif")) {
		ps->cindex += 4;
	    } else if (has_token(ps, L"fi")) {
		ps->cindex += 2;
		break;
	    } else {
		print_errmsg_token_missing(ps, L"fi");
	    }
	} else {
	    if (has_token(ps, L"fi"))
		ps->cindex += 2;
	    else
		print_errmsg_token_missing(ps, L"fi");
	    break;
	}
    }
    return result;
}

/* Parses a for command. */
command_T *parse_for(parsestate_T *ps)
{
    assert(has_token(ps, L"for"));
    ps->cindex += 3;
    skip_blanks_and_comment(ps);

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_FOR;
    result->c_lineno = ps->cinfo->lineno;
    result->c_redirs = NULL;

    wchar_t *name = parse_word_as_wcs(ps);
    if (name[0] == L'\0')
	serror(ps, Ngt("an identifier is required after `for'"));
    else if (skip_name(name)[0] != L'\0')
	serror(ps, Ngt("`%ls' is not a valid identifier"), name);
    result->c_forname = name;

    skip_to_next_token(ps);
    ensure_buffer(ps, 3);
    if (has_token(ps, L"in")) {
	redir_T *redirs = NULL;
	ps->cindex += 2;
	skip_blanks_and_comment(ps);
	result->c_forwords = parse_words_and_redirects(ps, &redirs, false);
	if (redirs != NULL) {
	    serror(ps, Ngt("redirections are not allowed after `in'"));
	    redirsfree(redirs);
	}
	if (ps->cbuf.contents[ps->cindex] == L';')
	    ps->cindex++;
    } else {
	result->c_forwords = NULL;
	if (ps->cbuf.contents[ps->cindex] == L';') {
	    ps->cindex++;
	    if (posixly_correct)
		serror(ps, Ngt("`;' is not allowed "
			    "just after the identifier in a for loop"));
	}
    }
    skip_to_next_token(ps);
    ensure_buffer(ps, 3);
    if (has_token(ps, L"do"))
	ps->cindex += 2;
    else
	serror(ps, Ngt("`%ls' is missing"), L"do");
	// print_errmsg_token_missing(ps, L"do");
    result->c_forcmds = parse_compound_list(ps);
    if (posixly_correct && result->c_forcmds == NULL)
	serror(ps, Ngt("commands are missing between `%ls' and `%ls'"),
		L"do", L"done");
    ensure_buffer(ps, 5);
    if (has_token(ps, L"done"))
	ps->cindex += 4;
    else
	print_errmsg_token_missing(ps, L"done");
    return result;
}

/* Parses a while/until command.
 * `whltype' must be true for the while command and false for the until command.
 */
command_T *parse_while(parsestate_T *ps, bool whltype)
{
    assert(has_token(ps, whltype ? L"while" : L"until"));
    ps->cindex += 5;

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_WHILE;
    result->c_lineno = ps->cinfo->lineno;
    result->c_redirs = NULL;
    result->c_whltype = whltype;
    result->c_whlcond = parse_compound_list(ps);
    if (posixly_correct && result->c_whlcond == NULL)
	serror(ps, Ngt("commands are missing after `%ls'"),
		whltype ? L"while" : L"until");
    ensure_buffer(ps, 3);
    if (has_token(ps, L"do"))
	ps->cindex += 2;
    else
	print_errmsg_token_missing(ps, L"do");
    result->c_whlcmds = parse_compound_list(ps);
    if (posixly_correct && result->c_whlcmds == NULL)
	serror(ps, Ngt("commands are missing between `%ls' and `%ls'"),
		L"do", L"done");
    ensure_buffer(ps, 5);
    if (has_token(ps, L"done"))
	ps->cindex += 4;
    else
	print_errmsg_token_missing(ps, L"done");
    return result;
}

/* Parses a case command. */
command_T *parse_case(parsestate_T *ps)
{
    assert(has_token(ps, L"case"));
    ps->cindex += 4;
    skip_blanks_and_comment(ps);

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_CASE;
    result->c_lineno = ps->cinfo->lineno;
    result->c_redirs = NULL;
    result->c_casword = parse_word(ps, AT_GLOBAL);
    if (result->c_casword == NULL)
	serror(ps, Ngt("a word is required after `%ls'"), L"case");
    skip_to_next_token(ps);
    ensure_buffer(ps, 3);
    if (has_token(ps, L"in")) {
	ps->cindex += 2;
	result->c_casitems = parse_case_list(ps);
    } else {
	serror(ps, Ngt("`%ls' is missing"), L"in");
	// print_errmsg_token_missing(ps, L"in");
	result->c_casitems = NULL;
    }
    ensure_buffer(ps, 5);
    if (has_token(ps, L"esac"))
	ps->cindex += 4;
    else
	print_errmsg_token_missing(ps, L"esac");
    return result;
}

/* Parses the body of a case command (the part between "in" and "esac").
 * You don't have to call `skip_to_next_token' before calling this function. */
caseitem_T *parse_case_list(parsestate_T *ps)
{
    caseitem_T *first = NULL, **lastp = &first;

    do {
	skip_to_next_token(ps);
	ensure_buffer(ps, 5);
	if (has_token(ps, L"esac"))
	    break;

	caseitem_T *ci = xmalloc(sizeof *ci);
	*lastp = ci;
	lastp = &ci->next;
	ci->next = NULL;
	ci->ci_patterns = parse_case_patterns(ps);
	ci->ci_commands = parse_compound_list(ps);
	/* `ci_commands' may be NULL unlike for and while commands */
	ensure_buffer(ps, 2);
	if (ps->cbuf.contents[ps->cindex] == L';' &&
		ps->cbuf.contents[ps->cindex + 1] == L';') {
	    ps->cindex += 2;
	} else {
	    break;
	}
    } while (!ps->cerror);
    return first;
}

/* Parses patterns of a case item.
 * The current position is advanced to the character that just follows ')', not
 * to the next token.
 * Call `skip_to_next_token' and `ensure_buffer(ps, 1)' before calling this
 * function. */
void **parse_case_patterns(parsestate_T *ps)
{
    plist_T wordlist;
    pl_init(&wordlist);

    if (ps->cbuf.contents[ps->cindex] == L'(') {  /* ignore the first '(' */
	ps->cindex++;
	skip_blanks_and_comment(ps);
	if (posixly_correct) {
	    ensure_buffer(ps, 5);
	    if (has_token(ps, L"esac"))
		serror(ps, Ngt(
		    "an unquoted `esac' cannot be the first case pattern"));
	}
    }
    do {
	if (is_token_delimiter_char(ps->cbuf.contents[ps->cindex])) {
	    if (ps->cbuf.contents[ps->cindex] != L'\0') {
		if (ps->cbuf.contents[ps->cindex] == L'\n')
		    serror(ps, Ngt("a word is required after `%ls'"), L"(");
		else
		    serror(ps, Ngt("encountered an invalid character `%lc' "
				"in the case pattern"),
			    (wint_t) ps->cbuf.contents[ps->cindex]);
	    }
	    break;
	}
	pl_add(&wordlist, parse_word(ps, AT_GLOBAL));
	skip_blanks_and_comment(ps);
	ensure_buffer(ps, 1);
	if (ps->cbuf.contents[ps->cindex] == L'|') {
	    ps->cindex++;
	} else if (ps->cbuf.contents[ps->cindex] == L')') {
	    ps->cindex++;
	    break;
	} else {
	    serror(ps, Ngt("`%ls' is missing"), L")");
	    break;
	}
	skip_blanks_and_comment(ps);
    } while (!ps->cerror);
    return pl_toary(&wordlist);
}

/* Parses a function definition that starts with the "function" keyword. */
command_T *parse_function(parsestate_T *ps)
{
    if (posixly_correct)
	serror(ps, Ngt("`%ls' cannot be used as a command name"), L"function");

    assert(has_token(ps, L"function"));
    ps->cindex += 8;
    skip_blanks_and_comment(ps);

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_FUNCDEF;
    result->c_lineno = ps->cinfo->lineno;
    result->c_redirs = NULL;
    result->c_funcname = parse_word(ps, AT_GLOBAL);
    if (result->c_funcname == NULL)
	serror(ps, Ngt("a word is required after `%ls'"), L"function");
    skip_blanks_and_comment(ps);

    /* parse parentheses */
    size_t savecindex = ps->cindex;
    if (ps->cbuf.contents[ps->cindex] == L'(') {
	ps->cindex++;
	skip_blanks_and_comment(ps);
	if (ps->cbuf.contents[ps->cindex] == L')')
	    ps->cindex++;
	else
	    ps->cindex = savecindex;
    }
    skip_to_next_token(ps);

    /* parse function body */
    const wchar_t *t = check_opening_token(ps);
    if (t == NULL) {
	serror(ps, Ngt("a function body must be a compound command"));
	result->c_funcbody = NULL;
    } else {
	result->c_funcbody = parse_compound_command(ps, t);
    }

    return result;
}

/* Parses a function definition if any.
 * This function may process line continuations, which increases the line
 * number. */
command_T *tryparse_function(parsestate_T *ps)
{
    size_t saveindex = ps->cindex;
    unsigned long lineno = ps->cinfo->lineno;

    if (iswdigit(ps->cbuf.contents[ps->cindex]))
	goto fail;

    size_t namelen = count_name_length(ps, is_name_char);
    ps->cindex += namelen;
    if (namelen == 0 || !is_token_delimiter_char(ps->cbuf.contents[ps->cindex]))
	goto fail;
    skip_blanks_and_comment(ps);

    /* parse parentheses */
    if (ps->cbuf.contents[ps->cindex] != L'(')
	goto fail;
    ps->cindex++;
    skip_blanks_and_comment(ps);
    if (ps->cbuf.contents[ps->cindex] != L')') {
	serror(ps, Ngt("`(' must be followed by `)' in a function definition"));
	return NULL;
    }
    ps->cindex++;
    skip_to_next_token(ps);

    /* parse function body */
    const wchar_t *t = check_opening_token(ps);
    if (t == NULL) {
	serror(ps, Ngt("a function body must be a compound command"));
	return NULL;
    }
    command_T *body = parse_compound_command(ps, t);
    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_FUNCDEF;
    result->c_lineno = lineno;
    result->c_redirs = NULL;
    result->c_funcname = xmalloc(sizeof *result->c_funcname);
    result->c_funcname->next = NULL;
    result->c_funcname->wu_type = WT_STRING;
    result->c_funcname->wu_string =
	xwcsndup(&ps->cbuf.contents[saveindex], namelen);
    result->c_funcbody = body;
    return result;

fail:
    ps->cindex = saveindex;
    return NULL;
}

/* Reads the contents of a here-document. */
void read_heredoc_contents(parsestate_T *ps, redir_T *r)
{
    if (wcschr(r->rd_hereend, L'\n')) {
	serror(ps, Ngt("the end-of-here-document indicator contains a newline"));
	return;
    }

    assert(r->rd_type == RT_HERE || r->rd_type == RT_HERERT);
    if (wcspbrk(r->rd_hereend, QUOTES))
	read_heredoc_contents_without_expansion(ps, r);
    else
	read_heredoc_contents_with_expansion(ps, r);
}

/* Reads the contents of a here-document without any parameter expansions. */
void read_heredoc_contents_without_expansion(parsestate_T *ps, redir_T *r)
{
    wchar_t *eoc = unquote(r->rd_hereend);  // end-of-contents marker
    size_t eoclen = wcslen(eoc);
    bool skiptab = (r->rd_type == RT_HERERT);
    xwcsbuf_T buf;

    wb_init(&buf);
    while (!is_end_of_heredoc_contents(ps, eoc, eoclen, skiptab) &&
	    ps->cbuf.contents[ps->cindex] != L'\0') {
	wb_cat(&buf, &ps->cbuf.contents[ps->cindex]);
	ps->cindex = ps->cbuf.length;
	if (ps->cbuf.contents[ps->cindex - 1] == L'\n')
	    ps->cinfo->lineno++;
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
    size_t eoclen = wcslen(eoc);
    bool skiptab = (r->rd_type == RT_HERERT);

    while (!is_end_of_heredoc_contents(ps, eoc, eoclen, skiptab) &&
	    ps->cbuf.contents[ps->cindex] != L'\0') {
	wordunit_T *wu = parse_string_to(ps, true, true);
	if (wu != NULL) {
	    *lastp = wu;
	    while (wu->next != NULL)
		wu = wu->next;
	    lastp = &wu->next;
	}
    }
}

/* Checks if the whole current line is end-of-heredoc `eoc'.
 * `eoclen' must be `wcslen(eoc)'.
 * If `skiptab' is true, leading tabs in the line are skipped.
 * If an end-of-heredoc is found, returns true and advances the current position
 * to the next line. Otherwise, returns false with the position unchanged
 * (except that leading tabs are skipped). */
bool is_end_of_heredoc_contents(
	parsestate_T *ps, const wchar_t *eoc, size_t eoclen, bool skiptab)
{
    if (ps->cinfo->lastinputresult != INPUT_OK)
	return true;

    assert(wcslen(eoc) == eoclen);
    assert(ps->cbuf.length > 0 && ps->cbuf.contents[ps->cindex - 1] == L'\n');
    read_more_input(ps);
    if (skiptab)
	while (ps->cbuf.contents[ps->cindex] == L'\t')
	    ps->cindex++;

    const wchar_t *m = matchwcsprefix(&ps->cbuf.contents[ps->cindex], eoc);
    if (m != NULL && m[0] == L'\n') {
	ps->cindex += eoclen + 1;
	ps->cinfo->lineno++;
	return true;
    } else if (m != NULL && m[0] == L'\0') {
	ps->cindex += eoclen;
	return true;
    } else {
	return false;
    }
}

/* Parses a string.
 * Parameter expansions, command substitutions and arithmetic expansions are
 * recognized, but single and double quotes are not treated as quotes.
 * Command substitutions enclosed by backquotes are recognized iff `backquote'
 * is true. If `stoponnewline' is true, stops parsing right after the next
 * newline is parsed. Otherwise, parsing continues up to the end-of-file. */
wordunit_T *parse_string_to(
	parsestate_T *ps, bool backquote, bool stoponnewline)
{
    wordunit_T *first = NULL, **lastp = &first;
    size_t startindex = ps->cindex;

    for (;;) {
	switch (ps->cbuf.contents[ps->cindex]) {
	case L'\0':
	    switch (read_more_input(ps)) {
		case INPUT_OK:
		    continue;
		case INPUT_EOF:
		case INPUT_INTERRUPTED:
		case INPUT_ERROR:
		    goto done;  /* ignore errors. */
	    }
	    break;
	case L'\\':
	    if (ps->cbuf.contents[ps->cindex + 1] == L'\n') {
		line_continuation(ps, ps->cindex);
		continue;
	    } else if (ps->cbuf.contents[ps->cindex + 1] != L'\0') {
		ps->cindex += 2;
		continue;
	    }
	    break;
	case L'\n':
	    ps->cinfo->lineno++;
	    if (stoponnewline) {
		ps->cindex++;
		goto done;
	    }
	    break;
	case L'`':
	    if (!backquote)
		break;
	    /* falls thru! */
	case L'$':
	    MAKE_WORDUNIT_STRING;
	    wordunit_T *wu = parse_special_word_unit(ps);
	    startindex = ps->cindex;
	    if (wu != NULL) {
		*lastp = wu;
		lastp = &wu->next;
		continue;
	    } else if (ps->cbuf.contents[ps->cindex] == L'\0') {
		continue;
	    }
	    break;
	default:
	    break;
	}
	ps->cindex++;
    }
done:
    MAKE_WORDUNIT_STRING;
    return first;
}

/* Parses a string recognizing parameter expansions, command substitutions of
 * the form "$(...)" and arithmetic expansions.
 * All the members of `info' except `lastinputresult' must have been initialized
 * beforehand.
 * This function reads and parses the input to the end of file.
 * Iff successful, the result is assigned to `*result' and true is returned.
 * If the input is empty, NULL is assigned.
 * On error, the value of `*result' is undefined. */
bool parse_string(parseinfo_T *restrict info, wordunit_T **restrict result)
{
    parsestate_T ps = {
	.cinfo = info,
	.cerror = false,
	.cindex = 0,
#if YASH_ENABLE_ALIAS
	.enable_alias = false,
	.reparse_alias = false,
	.caliases = NULL,
#endif
    };
    wb_init(&ps.cbuf);

    ps.cinfo->lastinputresult = INPUT_OK;
    read_more_input(&ps);
    pl_init(&ps.pending_heredocs);

    *result = parse_string_to(&ps, false, false);

    wb_destroy(&ps.cbuf);
    pl_destroy(&ps.pending_heredocs);
#if YASH_ENABLE_ALIAS
    assert(ps.caliases == NULL);
    //destroy_aliaslist(ps.caliases);
#endif
    if (ps.cinfo->lastinputresult != INPUT_EOF || ps.cerror) {
	wordfree(*result);
	return false;
    } else {
	return true;
    }
}


/***** Auxiliaries about Error Messages *****/

const char *get_errmsg_unexpected_token(const wchar_t *t)
{
    switch (t[0]) {
	case L')':
	    assert(wcscmp(t, L")") == 0);
	    return Ngt("encountered `%ls' without a matching `('");
	case L'}':
	    assert(wcscmp(t, L"}") == 0);
	    return Ngt("encountered `%ls' without a matching `{'");
	case L';':
	    assert(wcscmp(t, L";;") == 0);
	    return Ngt("`%ls' is used outside `case'");
	case L'!':
	    assert(wcscmp(t, L"!") == 0);
	    return Ngt("`%ls' cannot be used as a command name");
	case L'i':
	    assert(wcscmp(t, L"in") == 0);
	    return Ngt("`%ls' cannot be used as a command name");
	case L'f':
	    assert(wcscmp(t, L"fi") == 0);
	    return Ngt("encountered `%ls' "
		    "without a matching `if' and/or `then'");
	case L't':
	    assert(wcscmp(t, L"then") == 0);
	    return Ngt("encountered `%ls' without a matching `if' or `elif'");
	case L'd':
	    assert(t[1] == L'o');
	    if (t[2] == L'\0') {
		assert(wcscmp(t, L"do") == 0);
		return Ngt("encountered `%ls' "
			"without a matching `for', `while', or `until'");
	    } else {
		assert(wcscmp(t, L"done") == 0);
		return Ngt("encountered `%ls' without a matching `do'");
	    }
	case L'e':
	    if (t[1] == L's') {
		assert(wcscmp(t, L"esac") == 0);
		return Ngt("encountered `%ls' without a matching `case'");
	    } else {
		assert(wcscmp(t, L"else") == 0 || wcscmp(t, L"elif") == 0);
		return Ngt("encountered `%ls' "
			"without a matching `if' and/or `then'");
	    }
	default:
	    assert(false);
    }
}

void print_errmsg_token_missing(parsestate_T *ps, const wchar_t *t)
{
    const wchar_t *atoken = check_closing_token(ps);
    if (atoken != NULL) {
	serror(ps, get_errmsg_unexpected_token(atoken), atoken);
	serror(ps, Ngt("(maybe you missed `%ls'?)"), t);
    } else {
	serror(ps, Ngt("`%ls' is missing"), t);
    }
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
    while (ao) {
	print_pipelines(pr, ao->ao_pipelines, indent);
	trim_end_of_buffer(&pr->buffer);
	if (ao->ao_async)
	    wb_wccat(&pr->buffer, L'&');
	else if (!pr->multiline && (ao->next || !omitsemicolon))
	    wb_wccat(&pr->buffer, L';');
	if (ao->next || !omitsemicolon)
	    print_space_or_newline(pr);

	ao = ao->next;
    }
}

void print_pipelines(struct print *restrict pr, const pipeline_T *restrict pl,
	unsigned indent)
{
    if (!pl)
	return;
    for (;;) {
	print_indent(pr, indent);
	if (pl->pl_neg)
	    wb_cat(&pr->buffer, L"! ");
	print_commands(pr, pl->pl_commands, indent);

	pl = pl->next;
	if (!pl)
	    break;

	wb_cat(&pr->buffer, pl->pl_cond ? L"&&" : L"||");
	print_space_or_newline(pr);
    }
}

void print_commands(
	struct print *restrict pr, const command_T *restrict c, unsigned indent)
{
    if (!c)
	return;
    for (;;) {
	print_one_command(pr, c, indent);

	c = c->next;
	if (!c)
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
	case CT_FUNCDEF:
	    print_function_definition(pr, c, indent);
	    assert(!c->c_redirs);
	    return;  // break;
    }
    print_redirections(pr, c->c_redirs, indent);
}

void print_simple_command(
	struct print *restrict pr, const command_T *restrict c, unsigned indent)
{
    assert(c->c_type == CT_SIMPLE);

    print_assignments(pr, c->c_assigns, indent);
    for (void **w = c->c_words; *w; w++) {
	print_word(pr, *w, indent);
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
	if (!ic) {
	    break;
	} else if (!ic->ic_condition && !ic->next) {
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
    if (c->c_forwords) {
	wb_cat(&pr->buffer, L" in");
	for (void **w = c->c_forwords; *w; w++) {
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
    while (ci) {
	void **w = ci->ci_patterns;

	print_indent(pr, indent);
	wb_wccat(&pr->buffer, L'(');
	for (;;) {
	    print_word(pr, *w, indent);
	    w++;
	    if (!*w)
		break;
	    wb_cat(&pr->buffer, L" | ");
	}
	wb_wccat(&pr->buffer, L')');
	print_space_or_newline(pr);

	if (ci->ci_commands) {
	    print_and_or_lists(pr, ci->ci_commands, indent + 1, true);
	    print_space_or_newline(pr);
	}

	print_indent(pr, indent + 1);
	wb_cat(&pr->buffer, L";;");
	print_space_or_newline(pr);

	ci = ci->next;
    }
}

void print_function_definition(
	struct print *restrict pr, const command_T *restrict c, unsigned indent)
{
    assert(c->c_type == CT_FUNCDEF);

    if (c->c_funcname->next != NULL
	    || c->c_funcname->wu_type != WT_STRING
	    || !is_name(c->c_funcname->wu_string))
	wb_cat(&pr->buffer, L"function ");
    print_word(pr, c->c_funcname, indent);
    wb_cat(&pr->buffer, L"()");
    print_space_or_newline(pr);

    print_indent(pr, indent);
    assert(!c->c_funcbody->next);
    print_one_command(pr, c->c_funcbody, indent);
}

void print_assignments(
	struct print *restrict pr, const assign_T *restrict a, unsigned indent)
{
    while (a) {
	wb_cat(&pr->buffer, a->a_name);
	wb_wccat(&pr->buffer, L'=');
	switch (a->a_type) {
	    case A_SCALAR:
		print_word(pr, a->a_scalar, indent);
		break;
	    case A_ARRAY:
		wb_wccat(&pr->buffer, L'(');
		for (void **w = a->a_array; *w; w++) {
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
    while (rd) {
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
    while (wu) {
	switch (wu->wu_type) {
	    case WT_STRING:
		wb_cat(&pr->buffer, wu->wu_string);
		break;
	    case WT_PARAM:
		print_parameter(pr, wu->wu_param, indent);
		break;
	    case WT_CMDSUB:
		wb_cat(&pr->buffer, L"$(");
		print_embedded_command(pr, wu->wu_cmdsub, indent + 1);
		wb_wccat(&pr->buffer, L')');
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
    if (pe->pe_start) {
	wb_wccat(&pr->buffer, L'[');
	print_word(pr, pe->pe_start, indent);
	if (pe->pe_end) {
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
    if (ec.is_preparsed)
	print_and_or_lists(pr, ec.value.preparsed, indent, true);
    else
	wb_cat(&pr->buffer, ec.value.unparsed);
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
	for (size_t i = 0; i < pr->pending_heredocs.length; i++) {
	    const redir_T *rd = pr->pending_heredocs.contents[i];
	    print_word(pr, rd->rd_herecontent, 1);
	    wb_catfree(&pr->buffer, unquote(rd->rd_hereend));
	    wb_wccat(&pr->buffer, L'\n');
	}
	pl_clear(&pr->pending_heredocs, 0);
    } else {
	wb_wccat(&pr->buffer, L' ');
    }
}

void trim_end_of_buffer(xwcsbuf_T *buf)
{
    size_t i = buf->length;
    while (i > 0 && buf->contents[i - 1] == L' ')
	i--;
    wb_truncate(buf, i);
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
