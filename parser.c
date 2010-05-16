/* Yash: yet another shell */
/* parser.c: syntax parser */
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
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
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


/********** Functions That Free Parse Trees **********/

static void pipesfree(pipeline_T *p);
static void ifcmdsfree(ifcommand_T *i);
static void caseitemsfree(caseitem_T *i);
static void wordfree_vp(void *w);
static void paramfree(paramexp_T *p);
static void assignsfree(assign_T *a);
static void redirsfree(redir_T *r);
static void embedcmdfree(embedcmd_T c);

void andorsfree(and_or_T *a)
{
    while (a) {
	pipesfree(a->ao_pipelines);

	and_or_T *next = a->next;
	free(a);
	a = next;
    }
}

void pipesfree(pipeline_T *p)
{
    while (p) {
	comsfree(p->pl_commands);

	pipeline_T *next = p->next;
	free(p);
	p = next;
    }
}

void comsfree(command_T *c)
{
    while (c && --(c->refcount) == 0) {
	redirsfree(c->c_redirs);
	switch (c->c_type) {
	    case CT_SIMPLE:
		assignsfree(c->c_assigns);
		recfree(c->c_words, wordfree_vp);
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
		recfree(c->c_forwords, wordfree_vp);
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
    while (i) {
	andorsfree(i->ic_condition);
	andorsfree(i->ic_commands);

	ifcommand_T *next = i->next;
	free(i);
	i = next;
    }
}

void caseitemsfree(caseitem_T *i)
{
    while (i) {
	recfree(i->ci_patterns, wordfree_vp);
	andorsfree(i->ci_commands);

	caseitem_T *next = i->next;
	free(i);
	i = next;
    }
}

void wordfree(wordunit_T *w)
{
    while (w) {
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
    if (p) {
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
    while (a) {
	free(a->a_name);
	switch (a->a_type) {
	    case A_SCALAR:  wordfree(a->a_scalar);             break;
	    case A_ARRAY:   recfree(a->a_array, wordfree_vp);  break;
	}

	assign_T *next = a->next;
	free(a);
	a = next;
    }
}

void redirsfree(redir_T *r)
{
    while (r) {
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

static bool is_portable_name_char(wchar_t c)
    __attribute__((const));
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
 * Simply returns `s' if there is no identifier. */
wchar_t *skip_name(const wchar_t *s)
{
    if (!iswdigit(*s))
	while (is_name_char(*s))
	    s++;
    return (wchar_t *) s;
}

/* Checks if the specified string constitutes a valid identifier. */
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

/* Object used to save the state of parsing on the way. */
struct parsestate_T {
    parseinfo_T *cinfo;
    bool cerror;
    struct xwcsbuf_T cbuf;
    size_t cindex;
    struct plist_T pending_heredocs;
#if YASH_ENABLE_ALIAS
    bool enable_alias, reparse_alias;
    struct aliaslist_T *caliases;
#endif
};

typedef enum { noalias, globalonly, anyalias, } aliastype_T;

static void serror(const char *restrict format, ...)
    __attribute__((nonnull(1),format(printf,1,2)));
static inputresult_T read_more_input(void);
static inline void line_continuation(size_t index);
static void ensure_buffer(size_t n);
static size_t count_name_length(bool isnamechar(wchar_t c));
static void skip_blanks_and_comment(void);
static bool skip_to_next_token(void);
static void next_line(void);
static bool is_command_delimiter_char(wchar_t c)
    __attribute__((const));
static bool is_comma_or_closing_bracket(wchar_t c)
    __attribute__((const));
static bool is_slash_or_closing_brace(wchar_t c)
    __attribute__((const));
static bool is_closing_brace(wchar_t c)
    __attribute__((const));
static bool is_token_at(const wchar_t *t, size_t index)
    __attribute__((pure,nonnull));
static const wchar_t *check_opening_token(void);
static const wchar_t *check_closing_token(void);
static and_or_T *parse_command_list(void)
    __attribute__((malloc,warn_unused_result));
static and_or_T *parse_compound_list(void)
    __attribute__((malloc,warn_unused_result));
static and_or_T *parse_and_or_list(void)
    __attribute__((malloc,warn_unused_result));
static pipeline_T *parse_pipelines_in_and_or(void)
    __attribute__((malloc,warn_unused_result));
static pipeline_T *parse_pipeline(void)
    __attribute__((malloc,warn_unused_result));
static command_T *parse_commands_in_pipeline(void)
    __attribute__((malloc,warn_unused_result));
static command_T *parse_command(void)
    __attribute__((malloc,warn_unused_result));
static redir_T **parse_assignments_and_redirects(command_T *c)
    __attribute__((malloc,warn_unused_result,nonnull));
static void **parse_words_and_redirects(redir_T **redirlastp, bool first)
    __attribute__((malloc,warn_unused_result,nonnull));
static void parse_redirect_list(redir_T **lastp)
    __attribute__((nonnull));
static assign_T *tryparse_assignment(void)
    __attribute__((malloc,warn_unused_result));
static void **parse_words_to_paren(void)
    __attribute__((malloc,warn_unused_result));
static redir_T *tryparse_redirect(void)
    __attribute__((malloc,warn_unused_result));
static inline wordunit_T *parse_word(aliastype_T type)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *parse_word_to(bool testfunc(wchar_t c))
    __attribute__((malloc,warn_unused_result,nonnull));
static void skip_to_next_single_quote(void);
static wordunit_T *parse_special_word_unit(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *tryparse_paramexp_raw(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *parse_paramexp_in_brace(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *parse_cmdsubst_in_paren(void)
    __attribute__((malloc,warn_unused_result));
static embedcmd_T extract_command_in_paren(void)
    __attribute__((warn_unused_result));
#if YASH_ENABLE_ALIAS
static wchar_t *extract_command_in_paren_unparsed(void)
    __attribute__((malloc,warn_unused_result));
#endif
static wordunit_T *parse_cmdsubst_in_backquote(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *tryparse_arith(void)
    __attribute__((malloc,warn_unused_result));
static wchar_t *parse_word_as_wcs(void)
    __attribute__((malloc,warn_unused_result));
static command_T *parse_compound_command(const wchar_t *command)
    __attribute__((malloc,warn_unused_result,nonnull));
static command_T *parse_group(commandtype_T type)
    __attribute__((malloc,warn_unused_result));
static command_T *parse_if(void)
    __attribute__((malloc,warn_unused_result));
static command_T *parse_for(void)
    __attribute__((malloc,warn_unused_result));
static command_T *parse_while(bool whltype)
    __attribute__((malloc,warn_unused_result));
static command_T *parse_case(void)
    __attribute__((malloc,warn_unused_result));
static caseitem_T *parse_case_list(void)
    __attribute__((malloc,warn_unused_result));
static void **parse_case_patterns(void)
    __attribute__((malloc,warn_unused_result));
static command_T *parse_function(void)
    __attribute__((malloc,warn_unused_result));
static command_T *tryparse_function(void)
    __attribute__((malloc,warn_unused_result));
static void read_heredoc_contents(redir_T *redir);
static void read_heredoc_contents_without_expand(redir_T *r);
static void read_heredoc_contents_with_expand(redir_T *r);
static bool is_end_of_heredoc_contents(
	const wchar_t *eoc, size_t eoclen, bool skiptab)
    __attribute__((nonnull));
static wordunit_T *parse_string_to(bool backquote, bool stoponnewline)
    __attribute__((malloc,warn_unused_result));
static const char *get_errmsg_unexpected_token(const wchar_t *t)
    __attribute__((nonnull));
static void print_errmsg_token_missing(const wchar_t *t)
    __attribute__((nonnull));

#define QUOTES L"\"'\\"


/* object containing the info for parsing */
static parseinfo_T *cinfo;
/* set true when an error occurs. */
static bool cerror;
/* buffer to contain the source code */
static xwcsbuf_T cbuf;
/* index to the current parsing point in `cbuf' */
static size_t cindex;
/* list of here-documents whose contents are to be read */
static plist_T pending_heredocs;
#if YASH_ENABLE_ALIAS
/* If true, alias substitution is performed. */
static bool enable_alias;
/* If true, the current word is to be re-parsed as alias substitution has been
 * performed. */
static bool reparse_alias;
/* list of currently expanded aliases */
static struct aliaslist_T *caliases;
#endif


/* Saves the current parsing state to a `parsestate_T' object and returns it.
 * When `read_and_parse' or `parse_string' is called from inside themselves,
 * the parsing states must be saved using this function before the inner call.
 * Don't forget to call `restore_parse_state' to restore the state later. */
struct parsestate_T *save_parse_state(void)
{
    struct parsestate_T *result = xmalloc(sizeof *result);
    *result = (struct parsestate_T) {
	.cinfo = cinfo,
	.cerror = cerror,
	.cbuf = cbuf,
	.cindex = cindex,
	.pending_heredocs = pending_heredocs,
#if YASH_ENABLE_ALIAS
	.enable_alias = enable_alias,
	.reparse_alias = reparse_alias,
	.caliases = caliases,
#endif
    };
    return result;
}

/* Restores the parsing state to the specified state and frees the object. */
void restore_parse_state(struct parsestate_T *state)
{
    cinfo = state->cinfo;
    cerror = state->cerror;
    cbuf = state->cbuf;
    cindex = state->cindex;
    pending_heredocs = state->pending_heredocs;
#if YASH_ENABLE_ALIAS
    enable_alias = state->enable_alias;
    reparse_alias = state->reparse_alias;
    caliases = state->caliases;
#endif
    free(state);
}


/* The functions below may not return NULL even on error.
 * The error condition must be tested by checking `cerror'.
 * `cerror' is set to true when `serror' is called. */
/* All the functions named `parse_*' advance `cindex' to the index of the
 * character right after the functions have parsed. */


/* The main entry point to the parser. Reads at least one line and parses it.
 * All the members of `info' except `lastinputresult' must be initialized
 * beforehand.
 * The resulting parse tree is assigned to `*result'. If there is no command in
 * the next line, NULL is assigned.
 * Returns PR_OK           if successful,
 *         PR_SYNTAX_ERROR if a syntax error was encountered,
 *         PR_INPUT_ERROR  if an input error was encountered, or
 *         PR_EOF          if EOF was input.
 * If PR_SYNTAX_ERROR or PR_INPUT_ERROR is returned, at least one error message
 * is printed.
 * Note that `*result' is assigned if and only if the return value is PR_OK. */
parseresult_T read_and_parse(
	parseinfo_T *restrict info, and_or_T **restrict result)
{
    cinfo = info;
    cerror = false;
    cindex = 0;
    wb_init(&cbuf);

    if (info->intrinput)
	((struct input_readline_info *) info->inputinfo)->type = 1;

    cinfo->lastinputresult = INPUT_OK;
    switch (read_more_input()) {
	case INPUT_OK:
	    break;
	case INPUT_EOF:
	    wb_destroy(&cbuf);
	    return PR_EOF;
	case INPUT_INTERRUPTED:
	    wb_destroy(&cbuf);
	    *result = NULL;
	    return PR_OK;
	case INPUT_ERROR:
	    wb_destroy(&cbuf);
	    return PR_INPUT_ERROR;
    }
    pl_init(&pending_heredocs);
#if YASH_ENABLE_ALIAS
    enable_alias = cinfo->enable_alias;
    reparse_alias = false;
    caliases = new_aliaslist();
#endif

    and_or_T *r = parse_command_list();

    wb_destroy(&cbuf);
    pl_destroy(&pending_heredocs);
#if YASH_ENABLE_ALIAS
    destroy_aliaslist(caliases);
#endif

    switch (cinfo->lastinputresult) {
	case INPUT_OK:
	case INPUT_EOF:
	    if (cerror) {
		andorsfree(r);
		return PR_SYNTAX_ERROR;
	    } else {
		assert(cindex == cbuf.length);
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

/* Prints the specified error message to stderr.
 * `format' is passed to `gettext' in this function.
 * `format' need not to have a trailing newline. */
void serror(const char *restrict format, ...)
{
    va_list ap;

    if (cinfo->print_errmsg && cinfo->lastinputresult != INPUT_INTERRUPTED) {
	if (cinfo->filename)
	    fprintf(stderr, "%s:%lu: ", cinfo->filename, cinfo->lineno);
	fprintf(stderr, gt("syntax error: "));
	va_start(ap, format);
	vfprintf(stderr, gt(format), ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
    }
    cerror = true;
}

/* Reads the next line from the input and returns `cinfo->lastinputresult'.
 * If input is from an interactive terminal and `cerror' is true, returns
 * INPUT_INTERRUPTED. */
inputresult_T read_more_input(void)
{
    if (cerror && cinfo->intrinput)
	return INPUT_INTERRUPTED;
    if (cinfo->lastinputresult == INPUT_OK) {
	size_t savelength = cbuf.length;
	cinfo->lastinputresult = cinfo->input(&cbuf, cinfo->inputinfo);
	if (cinfo->enable_verbose && shopt_verbose)
	    fprintf(stderr, "%ls", cbuf.contents + savelength);
    }
    return cinfo->lastinputresult;
}

/* Removes a line continuation at the specified index in `cbuf', increments
 * line number, and reads the next line. */
void line_continuation(size_t index)
{
    assert(cbuf.contents[index] == L'\\' && cbuf.contents[index + 1] == L'\n');
    wb_remove(&cbuf, index, 2);
    cinfo->lineno++;
    read_more_input();
}

/* Processes line continuation within `n' character from the current `cindex'.*/
/* `n' should be as small as possible. */
void ensure_buffer(size_t n)
{
    size_t index = cindex;
    if (cbuf.contents[cindex] == L'\0')
	read_more_input();
    while (index - cindex < n) {
	switch (cbuf.contents[index]) {
	case L'\0':  case L'\'':
	    return;
	case L'\\':
	    if (cbuf.contents[index + 1] != L'\n')
		return;
	    assert(cbuf.contents[index + 2] == L'\0');
	    line_continuation(index);
	    if (cinfo->lastinputresult != INPUT_OK)
		return;
	    break;
	default:
	    index++;
	    break;
	}
    }
}

/* Returns the length of the name at the current index.
 * Whether a character can be part of the name is determined by `isnamechar'.
 * Processes line continuations and reads enough lines so that the
 * variable/alias name under the current index is fully available. */
size_t count_name_length(bool isnamechar(wchar_t c))
{
    size_t savecindex = cindex, newcindex;
    while (ensure_buffer(1), isnamechar(cbuf.contents[cindex]))
	cindex++;
    newcindex = cindex;
    cindex = savecindex;
    return newcindex - savecindex;
}

/* Advances `cindex' to skip blank characters, comments and line continuations.
 * This function may call `read_more_input' if needed. `cindex' is advanced to
 * the next non-blank character. If a comment is skipped, `cindex' will point to
 * the character right after the comment, typically a newline character.
 * Line continuations are actually removed rather than skipped. */
void skip_blanks_and_comment(void)
{
    if (cbuf.contents[cindex] == L'\0')
	if (read_more_input() != 0)
	    return;

start:
    /* skip blanks */
    while (iswblank(cbuf.contents[cindex]))
	cindex++;

    /* skip a comment */
    if (cbuf.contents[cindex] == L'#') {
	do {
	    cindex++;
	} while (cbuf.contents[cindex] != L'\n'
		&& cbuf.contents[cindex] != L'\0');
    }

    /* remove line continuation */
    if (cbuf.contents[cindex] == L'\\' && cbuf.contents[cindex + 1] == L'\n') {
	line_continuation(cindex);
	goto start;
    }
}

/* Advances `cindex' to skip blank characters, comments and newlines, until
 * the next token. This function may call `read_more_input' if needed.
 * Returns true iff at least one NEWLINE token is skipped. */
bool skip_to_next_token(void)
{
    bool newline = false;

    skip_blanks_and_comment();
    while (cinfo->lastinputresult == INPUT_OK
	    && cbuf.contents[cindex] == L'\n') {
	newline = true;
	next_line();
	skip_blanks_and_comment();
    }
    return newline;
}

/* Parses a NEWLINE token at the current index and proceeds to the next line.
 * The contents of pending here-documents is read if any. */
void next_line(void)
{
    assert(cbuf.contents[cindex] == L'\n');
    cindex++;
    cinfo->lineno++;
    assert(cbuf.contents[cindex] == L'\0');

    for (size_t i = 0; i < pending_heredocs.length; i++)
	read_heredoc_contents(pending_heredocs.contents[i]);
    pl_clear(&pending_heredocs, 0);
}

/* Checks if the specified character is a token separator */
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

/* Checks if token `t' exists at the specified index in `cbuf'.
 * `index' must not be greater than `cbuf.length'.
 * `t' must not be a proper substring of another operator token.
 * `ensure_buffer(wcslen(t))' must be done beforehand. */
bool is_token_at(const wchar_t *t, size_t index)
{
    const wchar_t *c = matchwcsprefix(cbuf.contents + index, t);
    return c != NULL && is_token_delimiter_char(*c);
}

/* Checks if there is an 'opening' token such as "(", "{" and "if" at the
 * current index in `cbuf'. If there is one, the token string is returned.
 * Otherwise NULL is returned. */
const wchar_t *check_opening_token(void)
{
    ensure_buffer(9);
    if (cbuf.contents[cindex] == L'(') return L"(";
    if (is_token_at(L"{",     cindex)) return L"{";
    if (is_token_at(L"if",    cindex)) return L"if";
    if (is_token_at(L"for",   cindex)) return L"for";
    if (is_token_at(L"while", cindex)) return L"while";
    if (is_token_at(L"until", cindex)) return L"until";
    if (is_token_at(L"case",  cindex)) return L"case";
    if (is_token_at(L"function", cindex)) return L"function";
    return NULL;
}

/* Checks if there is a 'closing' token such as ")", "}" and "fi" at the
 * current index in `cbuf'. If there is one, the token string is returned.
 * Otherwise NULL is returned.
 * Closing tokens delimit and/or lists. */
const wchar_t *check_closing_token(void)
{
    ensure_buffer(5);
    if (cbuf.contents[cindex] == L')') return L")";
    if (cbuf.contents[cindex] == L';' && cbuf.contents[cindex + 1] == L';')
				      return L";;";
    if (is_token_at(L"}",    cindex))  return L"}";
    if (is_token_at(L"then", cindex))  return L"then";
    if (is_token_at(L"else", cindex))  return L"else";
    if (is_token_at(L"elif", cindex))  return L"elif";
    if (is_token_at(L"fi",   cindex))  return L"fi";
    if (is_token_at(L"do",   cindex))  return L"do";
    if (is_token_at(L"done", cindex))  return L"done";
    if (is_token_at(L"esac", cindex))  return L"esac";
    return NULL;
}

/* Parses up to the end of the current line.
 * You don't have to call `skip_blanks_and_comment' beforehand. */
and_or_T *parse_command_list(void)
{
    and_or_T *first = NULL, **lastp = &first;
    bool separator = true;
    /* To parse a command after another, there must be "&", ";" or one or more
     * newlines as a separator. */

    while (!cerror) {
	skip_blanks_and_comment();
	if (cbuf.contents[cindex] == L'\0') {
	    break;
	} else if (cbuf.contents[cindex] == L'\n') {
	    next_line();
	    break;
	} else if (!separator) {
	    serror(Ngt("`;' or `&' missing"));
	    break;
	}

	and_or_T *ao = parse_and_or_list();
	if (ao) {
	    *lastp = ao;
	    lastp = &ao->next;
	}
#if YASH_ENABLE_ALIAS
	if (reparse_alias) {
	    assert(!ao);
	    continue;
	}
#endif

	separator = false;
	ensure_buffer(2);
	if (cbuf.contents[cindex] == L'&'
		|| (cbuf.contents[cindex] == L';'
		    && cbuf.contents[cindex + 1] != L';')) {
	    cindex++;
	    separator = true;
	}
    }
#if YASH_ENABLE_ALIAS
    reparse_alias = false;
#endif
    return first;
}

/* Parses commands until a closing token is found.
 * You don't have to call `skip_blanks_and_comment' beforehand. */
and_or_T *parse_compound_list(void)
{
    and_or_T *first = NULL, **lastp = &first;
    bool savecerror = cerror;
    bool separator = true;
    /* To parse a command after another, there must be "&", ";" or one or more
     * newlines as the separator. */

    if (!cinfo->intrinput)
	cerror = false;
    while (!cerror) {
	separator |= skip_to_next_token();
	if (!separator
		|| cbuf.contents[cindex] == L'\0'
		|| check_closing_token())
	    break;

	and_or_T *ao = parse_and_or_list();
	if (ao) {
	    *lastp = ao;
	    lastp = &ao->next;
	}
#if YASH_ENABLE_ALIAS
	if (reparse_alias) {
	    assert(!ao);
	    continue;
	}
#endif

	separator = false;
	ensure_buffer(2);
	if (cbuf.contents[cindex] == L'&'
		|| (cbuf.contents[cindex] == L';'
		    && cbuf.contents[cindex + 1] != L';')) {
	    cindex++;
	    separator = true;
	}
    }
    cerror |= savecerror;
#if YASH_ENABLE_ALIAS
    reparse_alias = false;
#endif
    return first;
}

/* Parses one and/or list.
 * The result reflects the trailing "&" or ";", but `cindex' points to the
 * delimiter "&" or ";" when the function returns. */
and_or_T *parse_and_or_list(void)
{
    pipeline_T *p = parse_pipelines_in_and_or();
#if YASH_ENABLE_ALIAS
    if (reparse_alias) {
	assert(!p);
	return NULL;
    }
#endif

    and_or_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->ao_pipelines = p;
    result->ao_async = (cbuf.contents[cindex] == L'&');
    return result;
}

/* Parses all pipelines in the and/or list. */
pipeline_T *parse_pipelines_in_and_or(void)
{
    pipeline_T *first = NULL, **lastp = &first;
    bool cond = false;

    for (;;) {
	pipeline_T *p = parse_pipeline();
	if (p) {
	    p->pl_cond = cond;
	    *lastp = p;
	    lastp = &p->next;
	}
#if YASH_ENABLE_ALIAS
	if (reparse_alias) {
	    assert(!p);
	    if (first)
		goto next;
	    else
		break;
	}
#endif

	ensure_buffer(2);
	if (cbuf.contents[cindex] == L'&'
		&& cbuf.contents[cindex+1] == L'&') {
	    cond = true;
	} else if (cbuf.contents[cindex] == L'|'
		&& cbuf.contents[cindex+1] == L'|') {
	    cond = false;
	} else {
	    break;
	}
	cindex += 2;
#if YASH_ENABLE_ALIAS
next:
#endif
	skip_to_next_token();
    }
    return first;
}

/* Parses one pipeline. */
pipeline_T *parse_pipeline(void)
{
    bool neg;
    command_T *c;

    ensure_buffer(2);
    if (is_token_at(L"!", cindex)) {
	neg = true;
	cindex++;
#if YASH_ENABLE_ALIAS
	do {
#endif
	    skip_blanks_and_comment();
	    c = parse_commands_in_pipeline();
#if YASH_ENABLE_ALIAS
	    assert(!reparse_alias || !c);
	} while (reparse_alias);
#endif
    } else {
	neg = false;
	c = parse_commands_in_pipeline();
#if YASH_ENABLE_ALIAS
	if (reparse_alias) {
	    assert(!c);
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

/* Parses the body of the pipeline. */
command_T *parse_commands_in_pipeline(void)
{
    command_T *first = NULL, **lastp = &first;

    for (;;) {
	command_T *c = parse_command();
	if (c) {
	    *lastp = c;
	    lastp = &c->next;
	}
#if YASH_ENABLE_ALIAS
	if (reparse_alias) {
	    assert(!c);
	    if (first)
		goto next;
	    else
		break;
	}
#endif

	ensure_buffer(2);
	if (cbuf.contents[cindex] == L'|' && cbuf.contents[cindex+1] != L'|') {
	    cindex++;
	} else {
	    break;
	}
#if YASH_ENABLE_ALIAS
next:
#endif
	skip_to_next_token();
    }
    return first;
}

/* Parses one command.
 * If the first word is alias-substituted, the `reparse_alias' flag is set and
 * NULL is returned. */
command_T *parse_command(void)
{
#if YASH_ENABLE_ALIAS
    reparse_alias = false;
#endif

    /* Note: `check_closing_token' includes `ensure_buffer(5)'. */
    const wchar_t *t = check_closing_token();
    if (t) {
	serror(get_errmsg_unexpected_token(t), t);
	return NULL;
    } else if (is_token_at(L"!", cindex)) {
	serror(get_errmsg_unexpected_token(L"!"), L"!");
	return NULL;
    } else if (is_token_at(L"in", cindex)) {
	serror(get_errmsg_unexpected_token(L"in"), L"in");
	return NULL;
    } else if (cbuf.contents[cindex] == L'(') {
	return parse_compound_command(L"(");
    } else if (is_command_delimiter_char(cbuf.contents[cindex])) {
	if (cbuf.contents[cindex] == L'\0' || cbuf.contents[cindex] == L'\n')
	    serror(Ngt("command missing at end of input"));
	else
	    serror(Ngt("command missing before `%lc'"),
		    (wint_t) cbuf.contents[cindex]);
	return NULL;
    }

    t = check_opening_token();
    if (t)
	return parse_compound_command(t);

#if YASH_ENABLE_ALIAS
    if (enable_alias) {
	size_t len = count_name_length(is_alias_name_char);
	substaliasflags_T flags = AF_NONGLOBAL | AF_NORECUR;
	if (substitute_alias(&cbuf, cindex, len, caliases, flags)) {
	    reparse_alias = true;
	    return NULL;
	}
    }
#endif

    command_T *result = tryparse_function();
    if (result)
	return result;

    /* parse as a simple command */
    result = xmalloc(sizeof *result);
    redir_T **redirlastp;

    result->next = NULL;
    result->refcount = 1;
    result->c_lineno = cinfo->lineno;
    result->c_type = CT_SIMPLE;
    redirlastp = parse_assignments_and_redirects(result);
    result->c_words = parse_words_and_redirects(redirlastp, true);

    ensure_buffer(1);
    if (cbuf.contents[cindex] == L'(')
	serror(Ngt("invalid use of `%lc'"), (wint_t) L'(');
    return result;
}

/* Parses assignments and redirections.
 * Tokens but the first one are subject to any-type alias substitution,
 * including the word just after the parsed assignments and redirections.
 * The first following word is subject to any-type alias substitution.
 * The results are assigned to `c->c_assigns' and `c->c_redirs'.
 * The new `redirlastp' is returned. */
redir_T **parse_assignments_and_redirects(command_T *c)
{
    assign_T **assgnlastp = &c->c_assigns;
    redir_T **redirlastp = &c->c_redirs;
    assign_T *assgn;
    redir_T *redir;

    c->c_assigns = NULL;
    c->c_redirs = NULL;
    while (ensure_buffer(1),
	    !is_command_delimiter_char(cbuf.contents[cindex])) {
	if ((redir = tryparse_redirect())) {
	    *redirlastp = redir;
	    redirlastp = &redir->next;
	} else if ((assgn = tryparse_assignment())) {
	    *assgnlastp = assgn;
	    assgnlastp = &assgn->next;
	} else {
	    break;
	}
#if YASH_ENABLE_ALIAS
	if (enable_alias) {
	    size_t len = count_name_length(is_alias_name_char);
	    substitute_alias(&cbuf, cindex, len, caliases, AF_NONGLOBAL);
	    skip_blanks_and_comment();
	}
#endif
    }
    return redirlastp;
}

/* Parses words and redirections.
 * The parsing result of redirections is assigned to `*redirlastp'
 * The parsing result of assignments is returned as an array of pointers to
 * word units that are cast to (void *).
 * `*redirlastp' must be initialized to NULL beforehand.
 * If `first' is true, alias substitution is not performed on the first word. */
void **parse_words_and_redirects(redir_T **redirlastp, bool first)
{
    plist_T wordlist;
    redir_T *redir;
    wordunit_T *word;

    pl_init(&wordlist);
    while (ensure_buffer(1),
	    !is_command_delimiter_char(cbuf.contents[cindex])) {
#if YASH_ENABLE_ALIAS
	if (!first && enable_alias) {
	    size_t len = count_name_length(is_alias_name_char);
	    substitute_alias(&cbuf, cindex, len, caliases, 0);
	    skip_blanks_and_comment();
	}
#endif
	if ((redir = tryparse_redirect())) {
	    *redirlastp = redir;
	    redirlastp = &redir->next;
	} else if ((word = parse_word(noalias))) {
	    pl_add(&wordlist, word);
	    skip_blanks_and_comment();
	    first = false;
	} else {
	    break;
	}
    }
    return pl_toary(&wordlist);
}

/* Parses zero or more redirections.
 * The parsing result is assigned to `*redirlastp'
 * `*redirlastp' must be initialized to NULL beforehand. */
void parse_redirect_list(redir_T **lastp)
{
    for (;;) {
#if YASH_ENABLE_ALIAS
	if (!posixly_correct && enable_alias) {
	    size_t len = count_name_length(is_alias_name_char);
	    substitute_alias(&cbuf, cindex, len, caliases, 0);
	}
#endif

	redir_T *redir = tryparse_redirect();
	if (!redir)
	    break;
	*lastp = redir;
	lastp = &redir->next;
    }
}

/* If there is an assignment at the current index, parses and returns it.
 * Otherwise, returns NULL without moving the index. */
assign_T *tryparse_assignment(void)
{
    if (iswdigit(cbuf.contents[cindex]))
	return NULL;

    size_t namelen = count_name_length(is_name_char);
    if (namelen == 0 || cbuf.contents[cindex + namelen] != L'=')
	return NULL;

    assign_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->a_name = xwcsndup(cbuf.contents + cindex, namelen);
    cindex += namelen + 1;

    ensure_buffer(1);
    if (posixly_correct || cbuf.contents[cindex] != L'(') {
	result->a_type = A_SCALAR;
	result->a_scalar = parse_word(noalias);
    } else {
	cindex++;
	skip_to_next_token();
	result->a_type = A_ARRAY;
	result->a_array = parse_words_to_paren();
	if (cbuf.contents[cindex] == L')')
	    cindex++;
	else
	    serror(Ngt("`%ls' missing"), L")");
    }
    skip_blanks_and_comment();
    return result;
}

/* Parses words until the next closing parentheses.
 * Delimiter characters other than ')' and '\n' are not allowed.
 * Returns a newly malloced array of pointers to newly malloced `wordunit_T's.*/
void **parse_words_to_paren(void)
{
    plist_T list;

    pl_init(&list);
    while (cbuf.contents[cindex] != L')') {
	wordunit_T *word = parse_word(globalonly);
	if (word)
	    pl_add(&list, word);
	else
	    break;
	skip_to_next_token();
    }
    return pl_toary(&list);
}

/* If there is a redirection at the current index, parses and returns it.
 * Otherwise, returns NULL without moving the index. */
redir_T *tryparse_redirect(void)
{
    int fd;

    ensure_buffer(2);
    if (iswdigit(cbuf.contents[cindex])) {
	wchar_t *endptr;

reparse:
	errno = 0;
	long lfd = wcstol(cbuf.contents + cindex, &endptr, 10);
	if (errno || lfd < 0 || lfd > INT_MAX)
	    fd = -1;  /* invalid fd */
	else
	    fd = (int) lfd;
	if (endptr[0] == L'\\' && endptr[1] == L'\n') {
	    line_continuation(endptr - cbuf.contents);
	    goto reparse;
	} else if (*endptr != L'<' && *endptr != L'>') {
	    return NULL;
	}
	cindex = endptr - cbuf.contents;
    } else if (cbuf.contents[cindex] == L'<') {
	fd = STDIN_FILENO;
    } else if (cbuf.contents[cindex] == L'>') {
	fd = STDOUT_FILENO;
    } else {
	return NULL;
    }

    redir_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->rd_fd = fd;
    ensure_buffer(3);
    assert(cbuf.contents[cindex] == L'<' || cbuf.contents[cindex] == L'>');
    if (cbuf.contents[cindex] == L'<') {
	switch (cbuf.contents[cindex + 1]) {
	case L'<':
	    if (cbuf.contents[cindex + 2] == L'-') {
		result->rd_type = RT_HERERT;  cindex += 3;
	    } else if (!posixly_correct && cbuf.contents[cindex + 2] == L'<') {
		result->rd_type = RT_HERESTR; cindex += 3;
	    } else {
		result->rd_type = RT_HERE;    cindex += 2;
	    }
	    break;
	case L'(':
	    if (!posixly_correct) {
		result->rd_type = RT_PROCIN; goto parse_command;
	    } else {
		result->rd_type = RT_INPUT;  cindex += 1;
	    }
	    break;
	case L'>':  result->rd_type = RT_INOUT;  cindex += 2;  break;
	case L'&':  result->rd_type = RT_DUPIN;  cindex += 2;  break;
	default:    result->rd_type = RT_INPUT;  cindex += 1;  break;
	}
    } else {
	switch (cbuf.contents[cindex + 1]) {
	case L'(':
	    if (!posixly_correct) {
		result->rd_type = RT_PROCOUT; goto parse_command;
	    } else {
		result->rd_type = RT_OUTPUT;  cindex += 1;
	    }
	    break;
	case L'>':
	    if (!posixly_correct && cbuf.contents[cindex + 2] == L'|') {
		result->rd_type = RT_PIPE;    cindex += 3;
	    } else {
		result->rd_type = RT_APPEND;  cindex += 2;
	    }
	    break;
	case L'|':  result->rd_type = RT_CLOBBER; cindex += 2;  break;
	case L'&':  result->rd_type = RT_DUPOUT;  cindex += 2;  break;
	default:    result->rd_type = RT_OUTPUT;  cindex += 1;  break;
	}
    }
    skip_blanks_and_comment();
    if (result->rd_type != RT_HERE && result->rd_type != RT_HERERT) {
	result->rd_filename = parse_word(globalonly);
	if (!result->rd_filename) {
	    serror(Ngt("redirection target missing"));
	    free(result);
	    return NULL;
	}
    } else {
	wchar_t *endofheredoc = parse_word_as_wcs();
	if (endofheredoc[0] == L'\0') {
	    serror(Ngt("here-document delimiter missing"));
	    free(endofheredoc);
	    free(result);
	    return NULL;
	}
	result->rd_hereend = endofheredoc;
	result->rd_herecontent = NULL;
	pl_add(&pending_heredocs, result);
    }
    skip_blanks_and_comment();
    return result;

parse_command:
    cindex += 1;
    result->rd_command = extract_command_in_paren();
    ensure_buffer(1);
    if (cbuf.contents[cindex] == L')')
	cindex++;
    skip_blanks_and_comment();
    return result;
}

/* Expands an alias and parses a word at the current index.
 * `type' specifies the type of aliases to be expanded.  */
wordunit_T *parse_word(aliastype_T type)
{
#if YASH_ENABLE_ALIAS
    if (enable_alias) {
	switch (type) {
	case noalias:
	    break;
	case globalonly:
	case anyalias:;
	    size_t len = count_name_length(is_alias_name_char);
	    substaliasflags_T flags = type == globalonly ? 0 : AF_NONGLOBAL;
	    substitute_alias(&cbuf, cindex, len, caliases, flags);
	    skip_blanks_and_comment();
	    break;
	}
    }
#else /* !YASH_ENABLE_ALIAS */
    (void) type;
#endif

    return parse_word_to(is_token_delimiter_char);
}

/* Parses a word at the current index.
 * `testfunc' is a function that determines if a character is a word delimiter.
 * It must return true for L'\0'.
 * The parsing proceeds up to an unescaped character `testfunc' returns false
 * for. It is not an error if there is no characters to be a word, in which case
 * NULL is returned. */
wordunit_T *parse_word_to(bool testfunc(wchar_t c))
{
    wordunit_T *first = NULL, **lastp = &first, *wu;
    bool indq = false;  /* in double quotes? */
    size_t startindex = cindex;

/* appends the substring from `startindex' to `cindex' as a new word unit
 * to `*lastp' */
#define MAKE_WORDUNIT_STRING                                                 \
    do {                                                                     \
        if (startindex != cindex) {                                          \
            wordunit_T *w = xmalloc(sizeof *w);                              \
            w->next = NULL;                                                  \
            w->wu_type = WT_STRING;                                          \
            w->wu_string = xwcsndup(                                         \
                    cbuf.contents + startindex, cindex - startindex);        \
            *lastp = w;                                                      \
            lastp = &w->next;                                                \
        }                                                                    \
    } while (0)

    while (indq ? cbuf.contents[cindex] != L'\0'
		: !testfunc(cbuf.contents[cindex])) {

	switch (cbuf.contents[cindex]) {
	case L'\\':
	    if (cbuf.contents[cindex + 1] == L'\n') {
		line_continuation(cindex);
		continue;
	    } else if (cbuf.contents[cindex + 1] != L'\0') {
		cindex += 2;
		continue;
	    }
	    break;
	case L'\n':
	    cinfo->lineno++;
	    read_more_input();
	    break;
	case L'$':
	case L'`':
	    MAKE_WORDUNIT_STRING;
	    wu = parse_special_word_unit();
	    startindex = cindex;
	    if (wu) {
		*lastp = wu;
		lastp = &wu->next;
		continue;
	    } else if (cbuf.contents[cindex] == L'\0') {
		continue;
	    }
	    break;
	case L'\'':
	    if (!indq) {
		cindex++;
		skip_to_next_single_quote();
		if (cbuf.contents[cindex] == L'\'')
		    cindex++;
		continue;
	    }
	    break;
	case L'"':
	    indq = !indq;
	    /* falls thru! */
	default:
	    break;
	}
	cindex++;
    }
    MAKE_WORDUNIT_STRING;

    if (indq)
	serror(Ngt("double-quote not closed"));

    return first;
}

/* Skips to the next single quote.
 * When the function returns, `cindex' points to the single quote.
 * If `cindex' points to a single quote when the function is called, `cindex' is
 * not advanced. It is an error if there is no single quote before EOF. */
void skip_to_next_single_quote(void)
{
    for (;;) {
	switch (cbuf.contents[cindex]) {
	case L'\'':
	    return;
	case L'\0':
	    if (read_more_input() != 0) {
		serror(Ngt("single-quote not closed"));
		return;
	    }
	    continue;
	case L'\n':
	    cinfo->lineno++;
	    /* falls thru! */
	default:
	    break;
	}
	cindex++;
    }
}

/* Parses a parameter expansion or command substitution that starts with '$' or
 * '`'. `cindex' must point to '$' or '`' when the function is called, and is
 * advanced to right after the expansion/substitution.
 * If `cindex' points to '$' but it's not an expansion, `cindex' is unchanged
 * and NULL is returned. Otherwise `cindex' is advanced at least by 1. */
wordunit_T *parse_special_word_unit(void)
{
    switch (cbuf.contents[cindex++]) {
    case L'$':
	ensure_buffer(2);
	switch (cbuf.contents[cindex]) {
	case L'{':
	    return parse_paramexp_in_brace();
	case L'(':
	    if (cbuf.contents[cindex + 1] == L'(') {
		wordunit_T *wu = tryparse_arith();
		if (wu)
		    return wu;
	    }
	    return parse_cmdsubst_in_paren();
	default:
	    return tryparse_paramexp_raw();
	}
    case L'`':
	return parse_cmdsubst_in_backquote();
    default:
	assert(false);
    }
}

/* Parses a parameter that is not enclosed by { }.
 * `cindex' points to the character right after '$' when the function is called,
 * and right after the parameter name when returns.
 * If there is not a parameter, NULL is returned with `cindex' pointing to '$'.
 */
wordunit_T *tryparse_paramexp_raw(void)
{
    paramexp_T *pe;
    size_t namelen;  /* length of parameter name */

    ensure_buffer(1);
    switch (cbuf.contents[cindex]) {
	case L'@':  case L'*':  case L'#':  case L'?':
	case L'-':  case L'$':  case L'!':
	    namelen = 1;
	    goto success;
    }
    if (!is_portable_name_char(cbuf.contents[cindex]))
	goto fail;
    if (iswdigit(cbuf.contents[cindex]))
	namelen = 1;
    else
	namelen = count_name_length(is_portable_name_char);

success:
    pe = xmalloc(sizeof *pe);
    pe->pe_type = PT_NONE;
    pe->pe_name = xwcsndup(cbuf.contents + cindex, namelen);
    pe->pe_start = pe->pe_end = pe->pe_match = pe->pe_subst = NULL;

    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_PARAM;
    result->wu_param = pe;
    cindex += namelen;
    return result;

fail:
    cindex--;
    return NULL;
}

/* Parses a parameter expansion starting with "${".
 * `cindex' points to '{' when the function is called, and '}' when returns. */
wordunit_T *parse_paramexp_in_brace(void)
{
    paramexp_T *pe = xmalloc(sizeof *pe);
    pe->pe_type = 0;
    pe->pe_name = NULL;
    pe->pe_start = pe->pe_end = pe->pe_match = pe->pe_subst = NULL;

    assert(cbuf.contents[cindex] == L'{');
    cindex++;

    /* parse PT_NUMBER */
    ensure_buffer(3);
    if (cbuf.contents[cindex] == L'#') {
	switch (cbuf.contents[cindex + 1]) {
	    case L'\0': case L'}':
	    case L'+':  case L'=':  case L':':  case L'/':  case L'%':
		break;
	    case L'-':  case L'?':  case L'#':
		if (cbuf.contents[cindex + 2] != L'}')
		    break;
		/* falls thru! */
	    default:
		pe->pe_type |= PT_NUMBER;
		cindex++;
		break;
	}
    }

    /* parse nested expansion */
    /* ensure_buffer(2); */
    if (!posixly_correct && cbuf.contents[cindex] == L'{') {
	pe->pe_type |= PT_NEST;
	pe->pe_nest = parse_paramexp_in_brace();
    } else if (!posixly_correct
	    && (cbuf.contents[cindex] == L'`'
		|| (cbuf.contents[cindex] == L'$'
		    && (cbuf.contents[cindex + 1] == L'{'
			|| cbuf.contents[cindex + 1] == L'(')))) {
	size_t neststartindex = cindex;
	pe->pe_nest = parse_special_word_unit();
	if (cindex != neststartindex)
	    pe->pe_type |= PT_NEST;
	else
	    goto parse_name;
    } else {
parse_name:;
	/* no nesting: parse parameter name */
	size_t namestartindex = cindex;
	switch (cbuf.contents[cindex]) {
	    case L'@':  case L'*':  case L'#':  case L'?':
	    case L'-':  case L'$':  case L'!':
		cindex++;
		goto make_name;
	}
	while (ensure_buffer(1), is_name_char(cbuf.contents[cindex]))
	    cindex++;
	if (namestartindex == cindex) {
	    serror(Ngt("parameter name missing or invalid"));
	    goto end;
	}
make_name:
	pe->pe_name = xwcsndup(
		cbuf.contents + namestartindex, cindex - namestartindex);
    }

    /* parse indices */
    ensure_buffer(3);
    if (!posixly_correct && cbuf.contents[cindex] == L'[') {
	cindex++;
	pe->pe_start = parse_word_to(is_comma_or_closing_bracket);
	if (!pe->pe_start)
	    serror(Ngt("index missing"));
	if (cbuf.contents[cindex] == L',') {
	    cindex++;
	    pe->pe_end = parse_word_to(is_comma_or_closing_bracket);
	    if (!pe->pe_end)
		serror(Ngt("index missing"));
	}
	if (cbuf.contents[cindex] == L']')
	    cindex++;
	else
	    serror(Ngt("`%ls' missing"), L"]");
	ensure_buffer(3);
    }

    /* parse PT_COLON */
    if (cbuf.contents[cindex] == L':') {
	pe->pe_type |= PT_COLON;
	cindex++;
    }

    /* parse '-', '+', '#', etc. */
    switch (cbuf.contents[cindex]) {
    case L'-':   pe->pe_type |= PT_MINUS;                    goto parse_subst;
    case L'+':   pe->pe_type |= PT_PLUS;                     goto parse_subst;
    case L'=':   pe->pe_type |= PT_ASSIGN;                   goto parse_subst;
    case L'?':   pe->pe_type |= PT_ERROR;                    goto parse_subst;
    case L'#':   pe->pe_type |= PT_MATCH | PT_MATCHHEAD;     goto parse_match;
    case L'%':   pe->pe_type |= PT_MATCH | PT_MATCHTAIL;     goto parse_match;
    case L'/':   pe->pe_type |= PT_SUBST | PT_MATCHLONGEST;  goto parse_match;
    case L'}':  case L'\0':  case L'\n':
	pe->pe_type |= PT_NONE;
	if (pe->pe_type & PT_COLON)
	    serror(Ngt("invalid use of `%lc' in parameter expansion"),
		    (wint_t) L':');
	goto check_closing_brace;
    default:
	serror(Ngt("invalid character `%lc' in parameter expansion"),
		(wint_t) cbuf.contents[cindex]);
	goto end;
    }

parse_match:
    if (pe->pe_type & PT_COLON) {
	if ((pe->pe_type & PT_MASK) == PT_SUBST)
	    pe->pe_type |= PT_MATCHHEAD | PT_MATCHTAIL;
	else
	    serror(Ngt("invalid use of `%lc' in parameter expansion"),
		    (wint_t) L':');
	cindex += 1;
    } else if (cbuf.contents[cindex] == cbuf.contents[cindex + 1]) {
	if ((pe->pe_type & PT_MASK) == PT_MATCH)
	    pe->pe_type |= PT_MATCHLONGEST;
	else
	    pe->pe_type |= PT_SUBSTALL;
	cindex += 2;
    } else if (cbuf.contents[cindex] == L'/') {
	if (cbuf.contents[cindex + 1] == L'#') {
	    pe->pe_type |= PT_MATCHHEAD;
	    cindex += 2;
	} else if (cbuf.contents[cindex + 1] == L'%') {
	    pe->pe_type |= PT_MATCHTAIL;
	    cindex += 2;
	} else {
	    cindex += 1;
	}
    } else {
	cindex += 1;
    }
    if ((pe->pe_type & PT_MASK) == PT_MATCH) {
	pe->pe_match = parse_word_to(is_closing_brace);
	goto check_closing_brace;
    } else {
	pe->pe_match = parse_word_to(is_slash_or_closing_brace);
	ensure_buffer(1);
	if (cbuf.contents[cindex] != L'/')
	    goto check_closing_brace;
    }

parse_subst:
    cindex++;
    pe->pe_subst = parse_word_to(is_closing_brace);

check_closing_brace:
    ensure_buffer(1);
    if (cbuf.contents[cindex] == L'}')
	cindex++;
    else
	serror(Ngt("`%ls' missing"), L"}");
    if ((pe->pe_type & PT_NUMBER) && (pe->pe_type & PT_MASK) != PT_NONE)
	serror(Ngt("invalid use of `%lc' in parameter expansion"),
		(wint_t) L'#');

end:;
    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_PARAM;
    result->wu_param = pe;
    return result;
}

/* Parses a command substitution starting with "$(".
 * `cindex' points to '(' when the function is called, and to the character
 * after ')' when returns. */
wordunit_T *parse_cmdsubst_in_paren(void)
{
    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_CMDSUB;
    result->wu_cmdsub = extract_command_in_paren();

    ensure_buffer(1);
    if (cbuf.contents[cindex] == L')')
	cindex++;
    else
	serror(Ngt("`%ls' missing"), L")");
    return result;
}

/* Extracts commands between '(' and ')'.
 * `cindex' points to '(' when the function is called, and to ')' when returns.
 */
embedcmd_T extract_command_in_paren(void)
{
    plist_T save_pending_heredocs;
    embedcmd_T result;

    assert(cbuf.contents[cindex] == L'(');

    save_pending_heredocs = pending_heredocs;
    pl_init(&pending_heredocs);

#if YASH_ENABLE_ALIAS
    if (posixly_correct && cinfo->enable_alias) {
	result.is_preparsed = false;
	result.value.unparsed = extract_command_in_paren_unparsed();
    } else
#endif
    {
	cindex++;
	result.is_preparsed = true;
	result.value.preparsed = parse_compound_list();
    }

    pl_destroy(&pending_heredocs);
    pending_heredocs = save_pending_heredocs;

    return result;
}

#if YASH_ENABLE_ALIAS
/* Parses commands between '(' and ')'.
 * `cindex' points to '(' when the function is called, and to ')' when returns.
 */
wchar_t *extract_command_in_paren_unparsed(void)
{
    bool save_enable_alias = enable_alias;
    enable_alias = false;

    size_t startindex = ++cindex;
    andorsfree(parse_compound_list());
    assert(startindex <= cindex);

    wchar_t *result = xwcsndup(cbuf.contents + startindex, cindex - startindex);

    enable_alias = save_enable_alias;
    return result;
}
#endif

/* Parses a command substitution enclosed by backquotes.
 * `cindex' points to the character right after the starting '`' when the
 * function is called, and right after the ending '`' when returns. */
wordunit_T *parse_cmdsubst_in_backquote(void)
{
    xwcsbuf_T buf;
    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_CMDSUB;
    result->wu_cmdsub.is_preparsed = false;

    assert(cbuf.contents[cindex - 1] == L'`');
    wb_init(&buf);
    for (;;) {
	ensure_buffer(1);
	switch (cbuf.contents[cindex]) {
	case L'`':
	    cindex++;
	    goto end;
	case L'\0':
	    if (read_more_input() != 0) {
		serror(Ngt("backquoted command substitution not closed"));
		goto end;
	    }
	    break;
	case L'\\':
	    cindex++;
	    switch (cbuf.contents[cindex]) {
		case L'$':  case L'`':  case L'\\':
		    goto default_;
		default:
		    wb_wccat(&buf, L'\\');
		    continue;
	    }
	case L'\n':
	    cinfo->lineno++;
	    /* falls thru! */
	default:  default_:
	    wb_wccat(&buf, cbuf.contents[cindex]);
	    cindex++;
	    break;
	}
    }
end:
    result->wu_cmdsub.value.unparsed = wb_towcs(&buf);
    return result;
}

/* Parses a arithmetic expansion.
 * `cindex' points to the character right after '$' when the function is called,
 * and right after the whole expansion when returns. If there is no arithmetic
 * expansion, NULL is returned with `cindex' unchanged. */
wordunit_T *tryparse_arith(void)
{
    size_t savecindex = cindex;
    assert(cbuf.contents[cindex] == L'(' && cbuf.contents[cindex + 1] == L'(');
    cindex += 2;

    wordunit_T *first = NULL, **lastp = &first, *wu;
    size_t startindex = cindex;
    int nestparen = 0;

    for (;;) {
	ensure_buffer(1);
	switch (cbuf.contents[cindex]) {
	case L'\0':
	    goto fail;
	case L'\\':
	    if (cbuf.contents[cindex + 1] == L'\n') {
		line_continuation(cindex);
		continue;
	    } else if (cbuf.contents[cindex + 1] != L'\0') {
		cindex += 2;
		continue;
	    }
	    break;
	case L'$':
	case L'`':
	    MAKE_WORDUNIT_STRING;
	    wu = parse_special_word_unit();
	    startindex = cindex;
	    if (wu) {
		*lastp = wu;
		lastp = &wu->next;
		continue;
	    } else if (cbuf.contents[cindex] == L'\0') {
		continue;
	    }
	    break;
	case L'(':
	    nestparen++;
	    break;
	case L')':
	    nestparen--;
	    if (nestparen < 0) {
		ensure_buffer(2);
		if (cbuf.contents[cindex + 1] == L')')
		    goto end;
		else
		    goto fail;
	    }
	    break;
	default:
	    break;
	}
	cindex++;
    }
end:
    MAKE_WORDUNIT_STRING;
    cindex += 2;

    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_ARITH;
    result->wu_arith = first;
    return result;

fail:
    wordfree(first);
    cindex = savecindex;
    return NULL;
}

/* Returns a word token at the current index as a newly malloced string.
 * `cindex' proceeds to just after the word.
 * This function never returns NULL, but may return an empty string. */
wchar_t *parse_word_as_wcs(void)
{
    size_t startindex = cindex;
    wordfree(parse_word(globalonly));
    assert(startindex <= cindex);
    return xwcsndup(cbuf.contents + startindex, cindex - startindex);
}

/* Parses a compound command.
 * `command' is the name of the command to parse such as "(" and "if". */
command_T *parse_compound_command(const wchar_t *command)
{
    /* `parse_group', `parse_if', etc. don't call `skip_blanks_and_comment'
     * before they return nor parse redirections. */
    command_T *result;
    switch (command[0]) {
    case L'(':
	result = parse_group(CT_SUBSHELL);
	break;
    case L'{':
	result = parse_group(CT_GROUP);
	break;
    case L'i':
	result = parse_if();
	break;
    case L'f':
	switch (command[1]) {
	    case L'o':
		result = parse_for();
		break;
	    case L'u':
		result = parse_function();
		break;
	    default:
		assert(false);
	}
	break;
    case L'w':
	result = parse_while(true);
	break;
    case L'u':
	result = parse_while(false);
	break;
    case L'c':
	result = parse_case();
	break;
    default:
	assert(false);
    }
    skip_blanks_and_comment();
    parse_redirect_list(&result->c_redirs);
    return result;
}

/* Parses a command group.
 * `type' must be either CT_GROUP or CT_SUBSHELL. */
command_T *parse_group(commandtype_T type)
{
    const wchar_t *start, *end;

    switch (type) {
	case CT_GROUP:
	    start = L"{", end = L"}";
	    assert(is_token_at(start, cindex));
	    break;
	case CT_SUBSHELL:
	    start = L"(", end = L")";
	    assert(cbuf.contents[cindex] == start[0]);
	    break;
	default:
	    assert(false);
    }
    cindex++;

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = type;
    result->c_lineno = cinfo->lineno;
    result->c_redirs = NULL;
    result->c_subcmds = parse_compound_list();
    if (posixly_correct && !result->c_subcmds)
	serror(Ngt("no commands between `%ls' and `%ls'"), start, end);
    if (cbuf.contents[cindex] == end[0])
	cindex++;
    else
	print_errmsg_token_missing(end);
    return result;
}

/* Parses a if command */
command_T *parse_if(void)
{
    assert(is_token_at(L"if", cindex));
    cindex += 2;

    ifcommand_T *first = NULL, **lastp = &first;
    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_IF;
    result->c_lineno = cinfo->lineno;
    result->c_redirs = NULL;

    bool els = false;
    while (!cerror) {
	ifcommand_T *ic = xmalloc(sizeof *ic);
	*lastp = ic;
	lastp = &ic->next;
	ic->next = NULL;
	if (!els) {
	    ic->ic_condition = parse_compound_list();
	    if (posixly_correct && !ic->ic_condition)
		serror(Ngt("no commands between `%ls' and `%ls'"),
			(first->next == NULL) ? L"if" : L"elif", L"then");
	    ensure_buffer(5);
	    if (is_token_at(L"then", cindex))
		cindex += 4;
	    else
		print_errmsg_token_missing(L"then");
	} else {
	    ic->ic_condition = NULL;
	}
	ic->ic_commands = parse_compound_list();
	if (posixly_correct && !ic->ic_commands)
	    serror(Ngt("no commands after `%ls'"), els ? L"else" : L"then");
	ensure_buffer(5);
	if (!els) {
	    if (is_token_at(L"else", cindex)) {
		cindex += 4;
		els = true;
	    } else if (is_token_at(L"elif", cindex)) {
		cindex += 4;
	    } else if (is_token_at(L"fi", cindex)) {
		cindex += 2;
		break;
	    } else {
		print_errmsg_token_missing(L"fi");
	    }
	} else {
	    if (is_token_at(L"fi", cindex))
		cindex += 2;
	    else
		print_errmsg_token_missing(L"fi");
	    break;
	}
    }
    result->c_ifcmds = first;
    return result;
}

/* Parses a for command. */
command_T *parse_for(void)
{
    assert(is_token_at(L"for", cindex));
    cindex += 3;
    skip_blanks_and_comment();

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_FOR;
    result->c_lineno = cinfo->lineno;
    result->c_redirs = NULL;

    wchar_t *name = parse_word_as_wcs();
    if (name[0] == L'\0')
	serror(Ngt("no identifier after `for'"));
    else if (*skip_name(name) != L'\0')
	serror(Ngt("`%ls' is not valid identifier"), name);
    result->c_forname = name;

    skip_to_next_token();
    ensure_buffer(3);
    if (is_token_at(L"in", cindex)) {
	redir_T *redirs = NULL;
	cindex += 2;
	skip_blanks_and_comment();
	result->c_forwords = parse_words_and_redirects(&redirs, false);
	if (redirs) {
	    serror(Ngt("redirection not allowed after `in'"));
	    redirsfree(redirs);
	}
	if (cbuf.contents[cindex] == L';')
	    cindex++;
    } else {
	result->c_forwords = NULL;
	if (cbuf.contents[cindex] == L';') {
	    cindex++;
	    if (posixly_correct)
		serror(Ngt("`;' not allowed "
			    "just after identifier in for loop"));
	}
    }
    skip_to_next_token();
    ensure_buffer(3);
    if (is_token_at(L"do", cindex))
	cindex += 2;
    else
	serror(Ngt("`%ls' missing"), L"do");
	// print_errmsg_token_missing(L"do");
    result->c_forcmds = parse_compound_list();
    if (posixly_correct && !result->c_forcmds)
	serror(Ngt("no commands between `%ls' and `%ls'"), L"do", L"done");
    ensure_buffer(5);
    if (is_token_at(L"done", cindex))
	cindex += 4;
    else
	print_errmsg_token_missing(L"done");
    return result;
}

/* Parses a while/until command. */
command_T *parse_while(bool whltype)
{
    assert(is_token_at(whltype ? L"while" : L"until", cindex));
    cindex += 5;

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_WHILE;
    result->c_lineno = cinfo->lineno;
    result->c_redirs = NULL;
    result->c_whltype = whltype;
    result->c_whlcond = parse_compound_list();
    if (posixly_correct && !result->c_whlcond)
	serror(Ngt("no commands after `%ls'"), whltype ? L"while" : L"until");
    ensure_buffer(3);
    if (is_token_at(L"do", cindex))
	cindex += 2;
    else
	print_errmsg_token_missing(L"do");
    result->c_whlcmds = parse_compound_list();
    if (posixly_correct && !result->c_whlcmds)
	serror(Ngt("no commands between `%ls' and `%ls'"), L"do", L"done");
    ensure_buffer(5);
    if (is_token_at(L"done", cindex))
	cindex += 4;
    else
	print_errmsg_token_missing(L"done");
    return result;
}

/* Parses a case command. */
command_T *parse_case(void)
{
    assert(is_token_at(L"case", cindex));
    cindex += 4;
    skip_blanks_and_comment();

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_CASE;
    result->c_lineno = cinfo->lineno;
    result->c_redirs = NULL;
    result->c_casword = parse_word(globalonly);
    if (!result->c_casword)
	serror(Ngt("no word after `%ls'"), L"case");
    skip_to_next_token();
    ensure_buffer(3);
    if (is_token_at(L"in", cindex)) {
	cindex += 2;
	result->c_casitems = parse_case_list();
    } else {
	serror(Ngt("`%ls' missing"), L"in");
	// print_errmsg_token_missing(L"in");
	result->c_casitems = NULL;
    }
    ensure_buffer(5);
    if (is_token_at(L"esac", cindex))
	cindex += 4;
    else
	print_errmsg_token_missing(L"esac");
    return result;
}

/* Parses the body of a case command (the part between "in" and "esac").
 * You don't have to call `skip_to_next_token' before calling this function. */
caseitem_T *parse_case_list(void)
{
    caseitem_T *first = NULL, **lastp = &first;

    do {
	skip_to_next_token();
	ensure_buffer(5);
	if (is_token_at(L"esac", cindex))
	    break;

	caseitem_T *ci = xmalloc(sizeof *ci);
	*lastp = ci;
	lastp = &ci->next;
	ci->next = NULL;
	ci->ci_patterns = parse_case_patterns();
	ci->ci_commands = parse_compound_list();
	/* `ci_commands' may be NULL unlike for and while commands */
	ensure_buffer(2);
	if (cbuf.contents[cindex] == L';' && cbuf.contents[cindex+1] == L';') {
	    cindex += 2;
	} else {
	    break;
	}
    } while (!cerror);
    return first;
}

/* Parses patterns of a case item.
 * `cindex' is advanced to the next character after ')', not the next token.
 * Call `skip_to_next_token' and `ensure_buffer(1)' beforehand. */
void **parse_case_patterns(void)
{
    plist_T wordlist;
    pl_init(&wordlist);

    if (cbuf.contents[cindex] == L'(') {  /* ignore the first '(' */
	cindex++;
	skip_blanks_and_comment();
    }
    do {
	if (is_token_delimiter_char(cbuf.contents[cindex])) {
	    if (cbuf.contents[cindex] != L'\0') {
		if (cbuf.contents[cindex] == L'\n')
		    serror(Ngt("no word after `%ls'"), L"(");
		else
		    serror(Ngt("invalid character `%lc' in case pattern"),
			    (wint_t) cbuf.contents[cindex]);
	    }
	    break;
	}
	pl_add(&wordlist, parse_word(globalonly));
	skip_blanks_and_comment();
	ensure_buffer(1);
	if (cbuf.contents[cindex] == L'|') {
	    cindex++;
	} else if (cbuf.contents[cindex] == L')') {
	    cindex++;
	    break;
	} else {
	    serror(Ngt("`%ls' missing"), L")");
	    break;
	}
	skip_blanks_and_comment();
    } while (!cerror);
    return pl_toary(&wordlist);
}

/* Parses a function definition that starts with the "function" keyword. */
command_T *parse_function(void)
{
    if (posixly_correct)
	serror(Ngt("invalid keyword `%ls'"), L"function");

    assert(is_token_at(L"function", cindex));
    cindex += 8;
    skip_blanks_and_comment();

    command_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->refcount = 1;
    result->c_type = CT_FUNCDEF;
    result->c_lineno = cinfo->lineno;
    result->c_redirs = NULL;
    result->c_funcname = parse_word(globalonly);
    if (result->c_funcname == NULL)
	serror(Ngt("no word after `%ls'"), L"function");
    skip_blanks_and_comment();

    /* parse parentheses */
    size_t savecindex = cindex;
    if (cbuf.contents[cindex] == L'(') {
	cindex++;
	skip_blanks_and_comment();
	if (cbuf.contents[cindex] == L')')
	    cindex++;
	else
	    cindex = savecindex;
    }
    skip_to_next_token();

    /* parse function body */
    const wchar_t *t = check_opening_token();
    if (t == NULL) {
	serror(Ngt("function body must be compound command"));
	result->c_funcbody = NULL;
    } else {
	result->c_funcbody = parse_compound_command(t);
    }

    return result;
}

/* Parses a function definition if any.
 * This function may do line continuations, increasing the line number. */
command_T *tryparse_function(void)
{
    size_t savecindex = cindex;
    unsigned long lineno = cinfo->lineno;

    if (iswdigit(cbuf.contents[cindex]))
	goto fail;

    size_t namelen = count_name_length(is_name_char);
    cindex += namelen;
    if (namelen == 0 || !is_token_delimiter_char(cbuf.contents[cindex]))
	goto fail;
    skip_blanks_and_comment();

    /* parse parentheses */
    if (cbuf.contents[cindex] != L'(')
	goto fail;
    cindex++;
    skip_blanks_and_comment();
    if (cbuf.contents[cindex] != L')') {
	serror(Ngt("`(' must be followed by `)' in function definition"));
	return NULL;
    }
    cindex++;
    skip_to_next_token();

    /* parse function body */
    const wchar_t *t = check_opening_token();
    if (!t) {
	serror(Ngt("function body must be compound command"));
	return NULL;
    }
    command_T *body = parse_compound_command(t);
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
	xwcsndup(cbuf.contents + savecindex, namelen);
    result->c_funcbody = body;
    return result;

fail:
    cindex = savecindex;
    return NULL;
}

/* Reads the contents of a here-document. */
void read_heredoc_contents(redir_T *r)
{
    if (wcschr(r->rd_hereend, L'\n')) {
	serror(Ngt("end-of-heredoc indicator containing newline"));
	return;
    }

    assert(r->rd_type == RT_HERE || r->rd_type == RT_HERERT);
    if (wcspbrk(r->rd_hereend, QUOTES))
	read_heredoc_contents_without_expand(r);
    else
	read_heredoc_contents_with_expand(r);
}

/* Reads the contents of a here-document without any parameter expansions. */
void read_heredoc_contents_without_expand(redir_T *r)
{
    wchar_t *eoc = unquote(r->rd_hereend);
    size_t eoclen = wcslen(eoc);
    xwcsbuf_T buf;
    wb_init(&buf);
    while (!is_end_of_heredoc_contents(eoc, eoclen, r->rd_type == RT_HERERT)
	    && cbuf.contents[cindex] != L'\0') {
	wb_cat(&buf, cbuf.contents + cindex);
	cindex = cbuf.length;
	if (cbuf.contents[cindex - 1] == L'\n')
	    cinfo->lineno++;
    }
    free(eoc);
    
    wordunit_T *wu = xmalloc(sizeof *wu);
    wu->next = NULL;
    wu->wu_type = WT_STRING;
    wu->wu_string = escapefree(wb_towcs(&buf), L"\\");
    r->rd_herecontent = wu;
}

/* Reads the contents of a here-document that may contain parameter expansions,
 * command substitutions and arithmetic expansions. */
void read_heredoc_contents_with_expand(redir_T *r)
{
    wordunit_T *first = NULL, **lastp = &first;
    const wchar_t *eoc = r->rd_hereend;
    size_t eoclen = wcslen(eoc);

    while (!is_end_of_heredoc_contents(eoc, eoclen, r->rd_type == RT_HERERT)
	    && cbuf.contents[cindex] != L'\0') {
	wordunit_T *wu = parse_string_to(true, true);
	if (wu) {
	    *lastp = wu;
	    while (wu->next)
		wu = wu->next;
	    lastp = &wu->next;
	}
    }
    r->rd_herecontent = first;
}

/* Checks if the whole line to which `cindex' points is end-of-heredoc `eoc'.
 * `eoclen' is `wcslen(eoc)'. If `skiptab' is true, leading tabs in the line are
 * skipped.
 * If an end-of-heredoc is found, returns true with `cindex' advanced to the
 * next line. Otherwise returns false with `cindex' unchanged (except that
 * leading tabs are skipped). */
bool is_end_of_heredoc_contents(const wchar_t *eoc, size_t eoclen, bool skiptab)
{
    if (cinfo->lastinputresult != INPUT_OK)
	return true;

    assert(wcslen(eoc) == eoclen);
    assert(cbuf.length > 0 && cbuf.contents[cindex - 1] == L'\n');
    read_more_input();
    while (skiptab && cbuf.contents[cindex] == L'\t') {
	cindex++;
    }

    const wchar_t *m = matchwcsprefix(cbuf.contents + cindex, eoc);
    if (m && *m == L'\n') {
	cindex += eoclen + 1;
	cinfo->lineno++;
	return true;
    } else if (m && *m == L'\0') {
	cindex += eoclen;
	return true;
    } else {
	return false;
    }
}

/* Parses a string.
 * Parameter expansions, command substitutions and arithmetic expansions are
 * recognized, but single quotes and doublequotes are not treated as quotes.
 * Command substitutions enclosed by backquotes are recognized iff `backquote'
 * is true. If `stoponnewline' is true, stops parsing right after the next
 * newline is parsed. Otherwise parsing continues to the EOF. */
wordunit_T *parse_string_to(bool backquote, bool stoponnewline)
{
    wordunit_T *first = NULL, **lastp = &first, *wu;
    size_t startindex = cindex;

    for (;;) {
	switch (cbuf.contents[cindex]) {
	case L'\0':
	    switch (read_more_input()) {
		case INPUT_OK:
		    continue;
		case INPUT_EOF:
		case INPUT_INTERRUPTED:
		case INPUT_ERROR:
		    goto done;  /* ignore errors. */
	    }
	    break;
	case L'\\':
	    if (cbuf.contents[cindex + 1] == L'\n') {
		line_continuation(cindex);
		continue;
	    } else if (cbuf.contents[cindex + 1] != L'\0') {
		cindex += 2;
		continue;
	    }
	    break;
	case L'\n':
	    cinfo->lineno++;
	    if (stoponnewline) {
		cindex++;
		goto done;
	    }
	    break;
	case L'`':
	    if (!backquote)
		break;
	    /* falls thru! */
	case L'$':
	    MAKE_WORDUNIT_STRING;
	    wu = parse_special_word_unit();
	    startindex = cindex;
	    if (wu) {
		*lastp = wu;
		lastp = &wu->next;
		continue;
	    } else if (cbuf.contents[cindex] == L'\0') {
		continue;
	    }
	    break;
	default:
	    break;
	}
	cindex++;
    }
done:
    MAKE_WORDUNIT_STRING;
    return first;
}

/* Parses a string recognizing parameter expansions, command substitutions of
 * the form "$(...)" and arithmetic expansions.
 * All the members of `info' except `lastinputresult' must be initialized
 * beforehand.
 * This function reads and parses the input to the EOF.
 * Iff successful, the result is assigned to `*result' and true is returned.
 * If the input is empty, NULL is assigned. */
bool parse_string(parseinfo_T *restrict info, wordunit_T **restrict result)
{
    cinfo = info;
    cerror = false;
    cindex = 0;
    wb_init(&cbuf);

    cinfo->lastinputresult = INPUT_OK;
    read_more_input();
    pl_init(&pending_heredocs);
#if YASH_ENABLE_ALIAS
    enable_alias = false;
    //caliases = new_aliaslist();
#endif

    *result = parse_string_to(false, false);

    wb_destroy(&cbuf);
    pl_destroy(&pending_heredocs);
#if YASH_ENABLE_ALIAS
    //destroy_aliaslist(caliases);
#endif
    if (cinfo->lastinputresult != INPUT_EOF || cerror) {
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
	    return Ngt("`%ls' without matching `('");
	case L'}':
	    assert(wcscmp(t, L"}") == 0);
	    return Ngt("`%ls' without matching `{'");
	case L';':
	    assert(wcscmp(t, L";;") == 0);
	    return Ngt("`%ls' used outside `case'");
	case L'!':
	    assert(wcscmp(t, L"!") == 0);
	    return Ngt("`%ls' cannot be used as command name");
	case L'i':
	    assert(wcscmp(t, L"in") == 0);
	    return Ngt("`%ls' cannot be used as command name");
	case L'f':
	    assert(wcscmp(t, L"fi") == 0);
	    return Ngt("`%ls' without matching `if' and/or `then'");
	case L't':
	    assert(wcscmp(t, L"then") == 0);
	    return Ngt("`%ls' used without `if' or `elif'");
	case L'd':
	    assert(t[1] == L'o');
	    if (t[2] == L'\0') {
		assert(wcscmp(t, L"do") == 0);
		return Ngt("`%ls' used without `for', `while', or `until'");
	    } else {
		assert(wcscmp(t, L"done") == 0);
		return Ngt("`%ls' without matching `do'");
	    }
	case L'e':
	    if (t[1] == L's') {
		assert(wcscmp(t, L"esac") == 0);
		return Ngt("`%ls' without matching `case'");
	    } else {
		assert(wcscmp(t, L"else") == 0 || wcscmp(t, L"elif") == 0);
		return Ngt("`%ls' used without `if' and/or `then'");
	    }
	default:
	    assert(false);
    }
}

void print_errmsg_token_missing(const wchar_t *t)
{
    const wchar_t *atoken = check_closing_token();
    if (atoken) {
	serror(get_errmsg_unexpected_token(atoken), atoken);
	serror(Ngt("(maybe you missed `%ls'?)"), t);
    } else {
	serror(Ngt("`%ls' missing"), t);
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
