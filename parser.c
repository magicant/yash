/* Yash: yet another shell */
/* parser.c: syntax parser */
/* (C) 2007-2009 magicant */

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
#include "alias.h"
#include "expand.h"
#include "input.h"
#include "option.h"
#include "parser.h"
#include "plist.h"
#include "strbuf.h"
#include "util.h"


/********** Functions that Free Parse Trees **********/

static void pipesfree(pipeline_T *p);
static void ifcmdsfree(ifcommand_T *i);
static void caseitemsfree(caseitem_T *i);
static void wordfree_vp(void *w);
static void paramfree(paramexp_T *p);
static void assignsfree(assign_T *a);
static void redirsfree(redir_T *r);

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
		free(c->c_funcname);
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
		free(w->wu_cmdsub);
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
		free(r->rd_command);
		break;
	}

	redir_T *next = r->next;
	free(r);
	r = next;
    }
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
 * pointer to the character right after the identifier in the string. */
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

/* Returns true iff the string is a shell reserved word. */
bool is_keyword(const wchar_t *s)
{
    /* List of keywords:
     *    case do done elif else esac fi for if in then until while { } !
     * The following words are currently not keywords:
     *    [[ ]] function select */
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
		|| (s[1] == L'o' && s[2] == L'r' && s[3] == L'\0');
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
    struct aliaslist_T *caliases;
#endif
};

typedef enum { noalias, globalonly, anyalias, } aliastype_T;

static void serror(const char *restrict format, ...)
    __attribute__((nonnull(1),format(printf,1,2)));
static inline int read_more_input(void);
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
static const wchar_t *check_opening_token_at(size_t index);
static const wchar_t *check_closing_token_at(size_t index);
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
static wordunit_T *parse_word_to(aliastype_T type, bool testfunc(wchar_t c))
    __attribute__((malloc,warn_unused_result,nonnull));
static inline wordunit_T *parse_word(aliastype_T type)
    __attribute__((malloc,warn_unused_result));
static void skip_to_next_single_quote(void);
static wordunit_T *parse_special_word_unit(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *tryparse_paramexp_raw(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *parse_paramexp_in_brase(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *parse_cmdsubst_in_paren(void)
    __attribute__((malloc,warn_unused_result));
static wchar_t *extract_command_in_paren(void)
    __attribute__((malloc,warn_unused_result));
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
static void print_errmsg_token_missing(const wchar_t *t, size_t index)
    __attribute__((nonnull));

#define QUOTES L"\"'\\"


/* object containing the info for parsing */
static parseinfo_T *cinfo;
/* set true when an error occurs. */
static bool cerror;
/* buffer to contain the source code */
static xwcsbuf_T cbuf;
/* index to the point currently parsing in `cbuf' */
static size_t cindex;
/* list of here-documents whose contents are to be read */
static plist_T pending_heredocs;
#if YASH_ENABLE_ALIAS
/* list of currently expanded aliases */
static struct aliaslist_T *caliases;
#endif


/* Saves the current parsing state to an object and returns it.
 * Though `read_and_parse' and `parse_string' cannot be reentered by nature,
 * they actually can if the parsing state is saved.
 * Don't forget to call `restore_parse_state' to restore the state. */
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
    caliases = state->caliases;
#endif
    free(state);
}


/* The functions below may not return NULL even on error.
 * The error condition must be tested by checking `cerror'.
 * `cerror' is set to true when `serror' is called. */
/* All the functions named `parse_*' advance `cindex' to the index of the
 * character right after the functions have parsed. `cbuf.contents[cindex]' is
 * non-null when these functions return (except when EOF is read). */


/* An entry point to the parser. Reads at least one line and parses it.
 * All the members of `info' must be initialized beforehand.
 * The resulting parse tree is assigned to `*result'. If there is no command in
 * the next line, NULL is assigned.
 * Returns 0 if successful,
 *         1 if a syntax error is found, or
 *         EOF when EOF is input.
 * If 1 is returned, at least one error message is printed.
 * Note that `*result' is assigned if and only if the return value is 0.
 * This function is not reentrant in itself. */
int read_and_parse(parseinfo_T *restrict info, and_or_T **restrict result)
{
    cinfo = info;
    cerror = false;
    cindex = 0;
    wb_init(&cbuf);

    if (info->intrinput)
	((struct input_readline_info *) info->inputinfo)->type = 1;

    cinfo->lastinputresult = 0;
    read_more_input();
    if (cinfo->lastinputresult == EOF) {
	wb_destroy(&cbuf);
	return EOF;
    } else if (cinfo->lastinputresult == 1) {
	wb_destroy(&cbuf);
	*result = NULL;
	return 0;
    }
    pl_init(&pending_heredocs);
#if YASH_ENABLE_ALIAS
    caliases = new_aliaslist();
#endif

    and_or_T *r = parse_command_list();

    wb_destroy(&cbuf);
    pl_destroy(&pending_heredocs);
#if YASH_ENABLE_ALIAS
    destroy_aliaslist(caliases);
#endif

    if (cinfo->lastinputresult == 1) {
	andorsfree(r);
	*result = NULL;
	return 0;
    } else if (cerror) {
	andorsfree(r);
	return 1;
    } else {
	assert(cindex == cbuf.length);
	*result = r;
	return 0;
    }
}

/* Prints an error message to stderr.
 * `format' is passed to `gettext' in this function.
 * `format' need not to have a trailing newline. */
void serror(const char *restrict format, ...)
{
    va_list ap;

    if (cinfo->print_errmsg && cinfo->lastinputresult != 1) {
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

/* Reads more input and returns `cinfo->lastinputresult'.
 * If input is from a interactive terminal and `cerror' is true, returns 1. */
int read_more_input(void)
{
    if (cerror && cinfo->intrinput)
	return 1;
    if (cinfo->lastinputresult == 0) {
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

/* Reads the next line if `cbuf.contents[cindex]' is a null character.
 * If there is a line continuation within `n' characters from the current
 * `cindex', calls `line_continuation'.
 * `n' should be as small as possible. */
void ensure_buffer(size_t n)
{
    size_t index = cindex;
    if (cbuf.contents[cindex] == L'\0')
	read_more_input();
    while (index - cindex < n) {
	switch (cbuf.contents[index]) {
	case L'\0':  case L'\'':  case L'"':
	    return;
	case L'\\':
	    if (cbuf.contents[index + 1] != L'\n')
		return;
	    assert(cbuf.contents[index + 2] == L'\0');
	    line_continuation(index);
	    if (cinfo->lastinputresult != 0)
		return;
	    break;
	default:
	    index++;
	    break;
	}
    }
}

/* Returns the length of the name under the current index.
 * Whether a character can be part of the name is determined by `isnamechar'.
 * Processes line continuations and reads enough lines so that the variable/alias
 * name under the current index is fully available. */
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
    while (cinfo->lastinputresult == 0 && cbuf.contents[cindex] == L'\n') {
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
    return iswblank(c) || c == L'\0' || c == L'\n'
	|| c == L';' || c == L'&' || c == L'|'
	|| c == L'<' || c == L'>' || c == L'(' || c == L')';
}

/* Checks if the specified character delimits a simple command. */
bool is_command_delimiter_char(wchar_t c)
{
    return c == L'\0' || c == L'\n'
	|| c == L';' || c == L'&' || c == L'|' || c == L'(' || c == L')';
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

/* Checks if there is a token `t' at the specified index in `cbuf'.
 * `index' must not be greater than `cbuf.length'.
 * `t' must not be a proper substring of another operator token.
 * `ensure_buffer(wcslen(t))' must be done beforehand. */
bool is_token_at(const wchar_t *t, size_t index)
{
    wchar_t *c = matchwcsprefix(cbuf.contents + index, t);
    return c && is_token_delimiter_char(*c);
}

/* Checks if there is a 'opening' token such as "(", "{" and "if" at the
 * specified index in `cbuf'. If there is one, the token string is returned.
 * Otherwise NULL is returned. */
const wchar_t *check_opening_token_at(size_t index)
{
    if (true || index + 6 >= cindex)
	ensure_buffer(6);
    if (cbuf.contents[index] == L'(') return L"(";
    if (is_token_at(L"{",     index)) return L"{";
    if (is_token_at(L"if",    index)) return L"if";
    if (is_token_at(L"for",   index)) return L"for";
    if (is_token_at(L"while", index)) return L"while";
    if (is_token_at(L"until", index)) return L"until";
    if (is_token_at(L"case",  index)) return L"case";
    return NULL;
}

/* Checks if there is a 'closing' token such as ")", "}" and "fi" at the
 * specified index in `cbuf'. If there is one, the token string is returned.
 * Otherwise NULL is returned.
 * Closing tokens delimit and/or lists. */
const wchar_t *check_closing_token_at(size_t index)
{
    ensure_buffer(5);
    if (cbuf.contents[index] == L')') return L")";
    if (cbuf.contents[index] == L';' && cbuf.contents[index + 1] == L';')
				      return L";;";
    if (is_token_at(L"}",    index))  return L"}";
    if (is_token_at(L"then", index))  return L"then";
    if (is_token_at(L"else", index))  return L"else";
    if (is_token_at(L"elif", index))  return L"elif";
    if (is_token_at(L"fi",   index))  return L"fi";
    if (is_token_at(L"do",   index))  return L"do";
    if (is_token_at(L"done", index))  return L"done";
    if (is_token_at(L"esac", index))  return L"esac";
    return NULL;
}

/* Parses up to the end of the current line.
 * You don't have to call `skip_blanks_and_comment' beforehand. */
and_or_T *parse_command_list(void)
{
    and_or_T *first = NULL, **lastp = &first;
    bool separator = true;
    /* To parse a command after another, there must be "&", ";" or one or more
     * newlines as the separator. */

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

	separator = false;
	ensure_buffer(2);
	if (cbuf.contents[cindex] == L'&'
		|| (cbuf.contents[cindex] == L';'
		    && cbuf.contents[cindex + 1] != L';')) {
	    cindex++;
	    separator = true;
	}
    }
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
		|| check_closing_token_at(cindex))
	    break;

	and_or_T *ao = parse_and_or_list();
	if (ao) {
	    *lastp = ao;
	    lastp = &ao->next;
	}

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
    return first;
}

/* Parses one and/or list.
 * The result reflects the trailing "&" or ";", but `cindex' points to the
 * delimiter "&" or ";" when the function returns. */
and_or_T *parse_and_or_list(void)
{
    and_or_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->ao_pipelines = parse_pipelines_in_and_or();
    result->ao_async = (cbuf.contents[cindex] == L'&');
    return result;
}

/* Parses all pipelines in the and/or list. */
pipeline_T *parse_pipelines_in_and_or(void)
{
    pipeline_T *first = NULL, **lastp = &first;
    bool cond = cond;

    for (;;) {
	pipeline_T *p = parse_pipeline();
	if (p) {
	    p->pl_cond = cond;
	    *lastp = p;
	    lastp = &p->next;
	}

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
	skip_to_next_token();
    }
    return first;
}

/* Parses one pipeline. */
pipeline_T *parse_pipeline(void)
{
    pipeline_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->pl_cond = false;

    ensure_buffer(2);
    if (is_token_at(L"!", cindex)) {
	result->pl_neg = true;
	cindex++;
	skip_blanks_and_comment();
    } else {
	result->pl_neg = false;
    }

    result->pl_commands = parse_commands_in_pipeline();
    return result;
}

/* Parses the body of a pipeline. */
command_T *parse_commands_in_pipeline(void)
{
    command_T *first = NULL, **lastp = &first;

    for (;;) {
	command_T *c = parse_command();
	if (c) {
	    *lastp = c;
	    lastp = &c->next;
	}

	ensure_buffer(2);
	if (cbuf.contents[cindex] == L'|' && cbuf.contents[cindex+1] != L'|') {
	    cindex++;
	    skip_to_next_token();
	} else {
	    break;
	}
    }
    return first;
}

/* Parses one command. */
command_T *parse_command(void)
{
    /* Note: `check_closing_token_at` includes `ensure_buffer(5)'. */
    const wchar_t *t = check_closing_token_at(cindex);
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

    t = check_opening_token_at(cindex);
    if (t)
	return parse_compound_command(t);

    unsigned long lineno = cinfo->lineno;
    command_T *result = tryparse_function();
    if (result)
	return result;

    /* parse as a simple command */
    result = xmalloc(sizeof *result);
    redir_T **redirlastp;

    result->next = NULL;
    result->refcount = 1;
    result->c_lineno = lineno;
    result->c_type = CT_SIMPLE;
    redirlastp = parse_assignments_and_redirects(result);
    result->c_words = parse_words_and_redirects(redirlastp, true);

    ensure_buffer(1);
    if (cbuf.contents[cindex] == L'(')
	serror(Ngt("invalid use of `%lc'"), (wint_t) cbuf.contents[cindex]);
    return result;
}

/* Parses assignments and redirections.
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
#if YASH_ENABLE_ALIAS
	size_t len = count_name_length(is_alias_name_char);
	substitute_alias(&cbuf, cindex, len, caliases, false);
	skip_blanks_and_comment();
#endif
	if ((redir = tryparse_redirect())) {
	    *redirlastp = redir;
	    redirlastp = &redir->next;
	} else if ((assgn = tryparse_assignment())) {
	    *assgnlastp = assgn;
	    assgnlastp = &assgn->next;
	} else {
	    break;
	}
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
	if (!first) {
	    size_t len = count_name_length(is_alias_name_char);
	    substitute_alias(&cbuf, cindex, len, caliases, true);
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
	if (!posixly_correct) {
	    size_t len = count_name_length(is_alias_name_char);
	    substitute_alias(&cbuf, cindex, len, caliases, true);
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
 * Otherwise, does nothing and returns NULL. */
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
    if (cbuf.contents[cindex] != L'(') {
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
 * Returns a newly malloced array of pointers to newly malloced wordunit_T. */
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
 * Otherwise, does nothing and returns NULL. */
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
	    serror(Ngt("redirect target not specified"));
	    free(result);
	    return NULL;
	}
    } else {
	wchar_t *endofheredoc = parse_word_as_wcs();
	if (endofheredoc[0] == L'\0') {
	    serror(Ngt("here-document delimiter not specified"));
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
    if (cbuf.contents[cindex] == L')')
	cindex++;
    skip_blanks_and_comment();
    return result;
}

wordunit_T *parse_word(aliastype_T type)
{
    return parse_word_to(type, is_token_delimiter_char);
}

/* Expands an alias and parses a word at the current index.
 * `type' specifies the type of aliases to be expanded.
 * `testfunc' is a function that determines if a character is a word delimiter.
 * It must return true for L'\0'.
 * The parsing process proceeds up to an unescaped character `testfunc' returns
 * false for. It is not an error if there is no characters to be a word, in
 * which case NULL is returned. */
wordunit_T *parse_word_to(aliastype_T type, bool testfunc(wchar_t c))
{
#if YASH_ENABLE_ALIAS
    switch (type) {
	case noalias:
	    break;
	case globalonly:
	case anyalias:;
	    size_t len = count_name_length(is_alias_name_char);
	    substitute_alias(&cbuf, cindex, len, caliases, type == globalonly);
	    skip_blanks_and_comment();
	    break;
    }
#else /* !YASH_ENABLE_ALIAS */
    (void) type;
#endif

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
		serror(Ngt("single quote not closed"));
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
	    return parse_paramexp_in_brase();
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
wordunit_T *parse_paramexp_in_brase(void)
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
	    case L'\0': case L' ':
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
	pe->pe_nest = parse_paramexp_in_brase();
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
	    goto fail;
	}
make_name:
	pe->pe_name = xwcsndup(
		cbuf.contents + namestartindex, cindex - namestartindex);
	assert(pe->pe_name != NULL);
    }

    /* parse indices */
    ensure_buffer(3);
    if (!posixly_correct && cbuf.contents[cindex] == L'[') {
	cindex++;
	pe->pe_start = parse_word_to(noalias, is_comma_or_closing_bracket);
	if (!pe->pe_start)
	    serror(Ngt("index missing"));
	if (cbuf.contents[cindex] == L',') {
	    cindex++;
	    pe->pe_end = parse_word_to(noalias, is_comma_or_closing_bracket);
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
    case L'}':
	pe->pe_type |= PT_NONE;
	if (pe->pe_type & PT_COLON)
	    serror(Ngt("invalid use of `%lc' in parameter expansion"),
		    (wint_t) L':');
	goto check_closing_paren_and_finish;
    case L'\0':  case L'\n':
	serror(Ngt("`%ls' missing"), L"}");
	goto fail;
    default:
	serror(Ngt("invalid character `%lc' in parameter expansion"),
		(wint_t) cbuf.contents[cindex]);
	goto fail;
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
	pe->pe_match = parse_word_to(noalias, is_closing_brace);
	goto check_closing_paren_and_finish;
    } else {
	pe->pe_match = parse_word_to(noalias, is_slash_or_closing_brace);
    }

    ensure_buffer(1);
    if (cbuf.contents[cindex] != L'/')
	goto check_closing_paren_and_finish;
parse_subst:
    cindex++;
    pe->pe_subst = parse_word_to(noalias, is_closing_brace);

check_closing_paren_and_finish:
    ensure_buffer(1);
    if (cbuf.contents[cindex] == L'}')
	cindex++;
    else
	serror(Ngt("`%ls' missing"), L"}");
    if ((pe->pe_type & PT_NUMBER) && (pe->pe_type & PT_MASK) != PT_NONE)
	serror(Ngt("invalid use of `%lc' in parameter expansion"),
		(wint_t) L'#');

    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_PARAM;
    result->wu_param = pe;
    return result;

fail:
    paramfree(pe);
    return NULL;
}

/* Parses a command substitution starting with "$(".
 * `cindex' points to '(' when the function is called, and ')' when returns. */
wordunit_T *parse_cmdsubst_in_paren(void)
{
    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_CMDSUB;
    result->wu_cmdsub = extract_command_in_paren();
    return result;
}

/* Extracts command between '(' and ')'.
 * `cindex' points to '(' when the function is called, and ')' when returns. */
wchar_t *extract_command_in_paren(void)
{
#if YASH_ENABLE_ALIAS
    bool save_alias_enabled = alias_enabled;
    alias_enabled = false;
#endif
    plist_T save_pending_heredocs = pending_heredocs;
    pl_init(&pending_heredocs);

    assert(cbuf.contents[cindex] == L'(');

    size_t startindex = ++cindex;
    andorsfree(parse_compound_list());
    assert(startindex <= cindex);

    wchar_t *result = xwcsndup(cbuf.contents + startindex, cindex - startindex);

    ensure_buffer(1);
    if (cbuf.contents[cindex] == L')')
	cindex++;
    else
	serror(Ngt("`%ls' missing"), L")");

    pl_destroy(&pending_heredocs);
    pending_heredocs = save_pending_heredocs;
#if YASH_ENABLE_ALIAS
    alias_enabled = save_alias_enabled;
#endif
    return result;
}

/* Parses a command substitution enclosed by backquotes.
 * `cindex' points to the character right after the starting '`' when the
 * function is called, and right after the ending '`' when returns. */
wordunit_T *parse_cmdsubst_in_backquote(void)
{
    xwcsbuf_T buf;
    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_CMDSUB;

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
    result->wu_cmdsub = wb_towcs(&buf);
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
	result = parse_for();
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
    const wchar_t *terminator;

    if (type == CT_GROUP) {
	assert(cbuf.contents[cindex] == L'{');
	terminator = L"}";
    } else if (type == CT_SUBSHELL) {
	assert(cbuf.contents[cindex] == L'(');
	terminator = L")";
    } else {
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
	serror(Ngt("no commands in command group"));
    if (cbuf.contents[cindex] == terminator[0])
	cindex++;
    else
	print_errmsg_token_missing(terminator, cindex);
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
			L"if", L"then");
	    ensure_buffer(5);
	    if (is_token_at(L"then", cindex))
		cindex += 4;
	    else
		print_errmsg_token_missing(L"then", cindex);
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
		print_errmsg_token_missing(L"fi", cindex);
	    }
	} else {
	    if (is_token_at(L"fi", cindex))
		cindex += 2;
	    else
		print_errmsg_token_missing(L"fi", cindex);
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
	print_errmsg_token_missing(L"do", cindex);
    result->c_forcmds = parse_compound_list();
    if (posixly_correct && !result->c_forcmds)
	serror(Ngt("no commands between `%ls' and `%ls'"), L"do", L"done");
    ensure_buffer(5);
    if (is_token_at(L"done", cindex))
	cindex += 4;
    else
	print_errmsg_token_missing(L"done", cindex);
    return result;
}

/* Parses a while or until command. */
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
	print_errmsg_token_missing(L"do", cindex);
    result->c_whlcmds = parse_compound_list();
    if (posixly_correct && !result->c_whlcmds)
	serror(Ngt("no commands between `%ls' and `%ls'"), L"do", L"done");
    ensure_buffer(5);
    if (is_token_at(L"done", cindex))
	cindex += 4;
    else
	print_errmsg_token_missing(L"done", cindex);
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
    if (is_token_at(L"in", cindex))
	cindex += 2;
    else
	print_errmsg_token_missing(L"in", cindex);
    result->c_casitems = parse_case_list();
    ensure_buffer(5);
    if (is_token_at(L"esac", cindex))
	cindex += 4;
    else
	print_errmsg_token_missing(L"esac", cindex);
    return result;
}

/* Parses the body of a case command (the part between "in" and "esac").
 * You don't have to call `skip_to_next_token' before calling this function. */
caseitem_T *parse_case_list(void)
{
    caseitem_T *first = NULL, **lastp = &first;

    while (!cerror) {
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
    }
    return first;
}

/* Parses patterns of a case item.
 * `cindex' is advanced to the next character after ')', not the next token.
 * Call `skip_blanks_and_comment' and `ensure_buffer(1)' beforehand. */
void **parse_case_patterns(void)
{
    plist_T wordlist;

    pl_init(&wordlist);
    if (cbuf.contents[cindex] == L'(') {  /* ignore the first '(' */
	cindex++;
	skip_blanks_and_comment();
    }
    while (!cerror) {
	if (is_token_delimiter_char(cbuf.contents[cindex]))
	    serror(Ngt("invalid character `%lc' in case pattern"),
		    (wint_t) cbuf.contents[cindex]);
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
    }
    return pl_toary(&wordlist);
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
    const wchar_t *t = check_opening_token_at(cindex);
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
    result->c_funcname = xwcsndup(cbuf.contents + savecindex, namelen);
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

/* Checks if the whole line to which `cindex' points is an end-of-heredoc `eoc'.
 * `eoclen' is `wcslen(eoc)'. If `skiptab' is true, leading tabs in the line are
 * skipped.
 * If an end-of-heredoc is found, returns true with `cindex' advanced to the
 * next line. Otherwise returns false with `cindex' unchanged (except that
 * leading tabs are skipped). */
bool is_end_of_heredoc_contents(const wchar_t *eoc, size_t eoclen, bool skiptab)
{
    if (cinfo->lastinputresult != 0)
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
	    goto done;
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
	    read_more_input();
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

/* Parses a string recognizing parameter expansions, command substitutions in
 * "$(...)" form and arithmetic expansions.
 * This function reads and parses the input to the EOF.
 * Iff successful, the result is assigned to `*result' and true is returned.
 * If the input is empty, NULL is assigned.
 * This function is not reentrant in itself. */
bool parse_string(parseinfo_T *restrict info, wordunit_T **restrict result)
{
    cinfo = info;
    cerror = false;
    cindex = 0;
    wb_init(&cbuf);

    cinfo->lastinputresult = 0;
    read_more_input();
    if (cinfo->lastinputresult == 1) {
	wb_destroy(&cbuf);
	return false;
    }
    pl_init(&pending_heredocs);
#if YASH_ENABLE_ALIAS
    //caliases = new_aliaslist();
#endif

    *result = parse_string_to(false, false);

    wb_destroy(&cbuf);
    pl_destroy(&pending_heredocs);
#if YASH_ENABLE_ALIAS
    //destroy_aliaslist(caliases);
#endif
    if (cinfo->lastinputresult == 1 || cerror) {
	wordfree(*result);
	return false;
    } else {
	return true;
    }
}


/***** Error Message Auxiliaries *****/

const char *get_errmsg_unexpected_token(const wchar_t *t)
{
    switch (t[0]) {
	case L')': return Ngt("`%ls' without matching `('");
	case L'}': return Ngt("`%ls' without matching `{'");
	case L';': return Ngt("`%ls' used outside `case'");
	case L'!': return Ngt("`%ls' cannot be used as command name");
	case L'i': return Ngt("`%ls' cannot be used as command name");
	case L'f': return Ngt("`%ls' without matching `if'");
	case L't': return Ngt("`%ls' used without `if'");
	case L'd':
	    assert(t[1] == L'o');
	    if (t[2] == L'\0')
		return Ngt("`%ls' used without `for', `while', or `until'");
	    else
		return Ngt("`%ls' without matching `do'");
	case L'e':
	    if (t[1] == L's')
		return Ngt("`%ls' without matching `case'");
	    else
		if (t[2] == L's')
		    return Ngt("`%ls' used without `if'");
		else
		    return Ngt("`%ls' used without `if'");
	default:
	    assert(false);
    }
}

void print_errmsg_token_missing(const wchar_t *t, size_t index)
{
    const wchar_t *atoken = check_closing_token_at(index);
    if (atoken)
	serror(get_errmsg_unexpected_token(atoken), atoken);
    else
	serror(Ngt("`%ls' missing"), t);
}


/********** Functions that Convert Parse Trees into Strings**********/

static void print_and_or_lists(
	xwcsbuf_T *restrict buf, const and_or_T *restrict andors,
	bool omit_last_semicolon)
    __attribute__((nonnull(1)));
static void print_pipelines(
	xwcsbuf_T *restrict buf, const pipeline_T *restrict pipelines)
    __attribute__((nonnull(1)));
static void print_commands(
	xwcsbuf_T *restrict buf, const command_T *restrict commands)
    __attribute__((nonnull(1)));
static void print_command_content(
	xwcsbuf_T *restrict buf, const command_T *restrict command)
    __attribute__((nonnull));
static void print_caseitems(
	xwcsbuf_T *restrict buf, const caseitem_T *restrict caseitems)
    __attribute__((nonnull(1)));
static void print_assigns(
	xwcsbuf_T *restrict buf, const assign_T *restrict assigns)
    __attribute__((nonnull(1)));
static void print_redirs(
	xwcsbuf_T *restrict buf, const redir_T *restrict redirs)
    __attribute__((nonnull(1)));
static void print_word(
	xwcsbuf_T *restrict buf, const wordunit_T *restrict word)
    __attribute__((nonnull(1)));
static void print_paramexp(
	xwcsbuf_T *restrict buf, const paramexp_T *restrict param)
    __attribute__((nonnull));
static void trim_end_of_buffer(xwcsbuf_T *buf)
    __attribute__((nonnull));

#if 0
/* Converts a parse tree into a newly malloced wide string. */
wchar_t *commands_to_wcstring(const and_or_T *commands)
{
    xwcsbuf_T buf;

    wb_init(&buf);
    print_and_or_lists(&buf, commands, true);
    trim_end_of_buffer(&buf);
    return wb_towcs(&buf);
}
#endif

/* Converts pipelines into a newly malloced wide string. */
wchar_t *pipelines_to_wcs(const pipeline_T *pipelines)
{
    xwcsbuf_T buf;

    wb_init(&buf);
    print_pipelines(&buf, pipelines);
    trim_end_of_buffer(&buf);
    return wb_towcs(&buf);
}

/* Converts a command into a newly malloced wide string. */
wchar_t *command_to_wcs(const command_T *command)
{
    xwcsbuf_T buf;

    wb_init(&buf);
    print_command_content(&buf, command);
    print_redirs(&buf, command->c_redirs);
    trim_end_of_buffer(&buf);
    return wb_towcs(&buf);
}

void print_and_or_lists(
	xwcsbuf_T *restrict buf, const and_or_T *restrict c, bool omitsemicolon)
{
    while (c) {
	print_pipelines(buf, c->ao_pipelines);
	assert(iswblank(buf->contents[buf->length - 1]));
	if (c->ao_async)
	    wb_insert(buf, buf->length - 1, L"&");
	else if (!omitsemicolon || c->next)
	    wb_insert(buf, buf->length - 1, L";");
	c = c->next;
    }
}

void print_pipelines(xwcsbuf_T *restrict buf, const pipeline_T *restrict p)
{
    for (bool first = true; p; p = p->next, first = false) {
	if (!first)
	    wb_cat(buf, p->pl_cond ? L"&& " : L"|| ");
	if (p->pl_neg)
	    wb_cat(buf, L"! ");
	print_commands(buf, p->pl_commands);
    }
}

void print_commands(xwcsbuf_T *restrict buf, const command_T *restrict c)
{
    while (c) {
	print_command_content(buf, c);
	print_redirs(buf, c->c_redirs);
	if (c->next)
	    wb_cat(buf, L"| ");
	c = c->next;
    }
}

void print_command_content(xwcsbuf_T *restrict buf, const command_T *restrict c)
{
    switch (c->c_type) {
    case CT_SIMPLE:
	print_assigns(buf, c->c_assigns);
	for (void **w = c->c_words; *w; w++) {
	    print_word(buf, *w);
	    wb_wccat(buf, L' ');
	}
	break;
    case CT_GROUP:
	wb_cat(buf, L"{ ");
	print_and_or_lists(buf, c->c_subcmds, false);
	wb_cat(buf, L"} ");
	break;
    case CT_SUBSHELL:
	wb_wccat(buf, L'(');
	print_and_or_lists(buf, c->c_subcmds, true);
	assert(iswblank(buf->contents[buf->length - 1]));
	wb_insert(buf, buf->length - 1, L")");
	break;
    case CT_IF:
	wb_cat(buf, L"if ");
	for (ifcommand_T *ic = c->c_ifcmds;;) {
	    print_and_or_lists(buf, ic->ic_condition, false);
	    wb_cat(buf, L"then ");
	    print_and_or_lists(buf, ic->ic_commands, false);
	    ic = ic->next;
	    if (!ic) {
		break;
	    } else if (!ic->ic_condition) {
		wb_cat(buf, L"else ");
		print_and_or_lists(buf, ic->ic_commands, false);
		break;
	    } else {
		wb_cat(buf, L"elif ");
	    }
	}
	wb_cat(buf, L"fi ");
	break;
    case CT_FOR:
	wb_cat(buf, L"for ");
	wb_cat(buf, c->c_forname);
	if (c->c_forwords) {
	    wb_cat(buf, L" in");
	    for (void **w = c->c_forwords; *w; w++) {
		wb_wccat(buf, L' ');
		print_word(buf, *w);
	    }
	}
	wb_cat(buf, L"; do ");
	print_and_or_lists(buf, c->c_forcmds, false);
	wb_cat(buf, L"done ");
	break;
    case CT_WHILE:
	wb_cat(buf, c->c_whltype ? L"while " : L"until ");
	print_and_or_lists(buf, c->c_whlcond, false);
	wb_cat(buf, L"do ");
	print_and_or_lists(buf, c->c_whlcmds, false);
	wb_cat(buf, L"done ");
	break;
    case CT_CASE:
	wb_cat(buf, L"case ");
	print_word(buf, c->c_casword);
	wb_cat(buf, L" in ");
	print_caseitems(buf, c->c_casitems);
	wb_cat(buf, L"esac ");
	break;
    case CT_FUNCDEF:
	wb_cat(buf, c->c_funcname);
	wb_cat(buf, L" () ");
	print_commands(buf, c->c_funcbody);
	break;
    }
}

void print_caseitems(xwcsbuf_T *restrict buf, const caseitem_T *restrict i)
{
    while (i) {
	bool first = true;

	wb_wccat(buf, L'(');
	for (void **w = i->ci_patterns; *w; w++) {
	    if (!first)
		wb_wccat(buf, L'|');
	    print_word(buf, *w);
	    first = false;
	}
	wb_cat(buf, L") ");
	print_and_or_lists(buf, i->ci_commands, true);
	wb_cat(buf, L";; ");

	i = i->next;
    }
}

void print_assigns(xwcsbuf_T *restrict buf, const assign_T *restrict a)
{
    while (a) {
	wb_cat(buf, a->a_name);
	wb_wccat(buf, L'=');
	switch (a->a_type) {
	    case A_SCALAR:
		print_word(buf, a->a_scalar);
		break;
	    case A_ARRAY:
		wb_wccat(buf, L'(');
		for (void **w = a->a_array; *w; w++) {
		    print_word(buf, *w);
		    if (*(w + 1))
			wb_wccat(buf, L' ');
		}
		wb_wccat(buf, L')');
		break;
	}
	wb_wccat(buf, L' ');
	a = a->next;
    }
}

void print_redirs(xwcsbuf_T *restrict buf, const redir_T *restrict r)
{
    while (r) {
	const wchar_t *s;
	enum { file, here, proc, } type;

	switch (r->rd_type) {
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
	wb_wprintf(buf, L"%d%ls", r->rd_fd, s);
	switch (type) {
	    case file:
		print_word(buf, r->rd_filename);
		break;
	    case here:
		wb_cat(buf, r->rd_hereend);
		break;
	    case proc:
		wb_cat(buf, r->rd_command);
		wb_wccat(buf, L')');
		break;
	}
	wb_wccat(buf, L' ');

	r = r->next;
    }
}

void print_word(xwcsbuf_T *restrict buf, const wordunit_T *restrict w)
{
    while (w) {
	switch (w->wu_type) {
	case WT_STRING:
	    wb_cat(buf, w->wu_string);
	    break;
	case WT_PARAM:
	    print_paramexp(buf, w->wu_param);
	    break;
	case WT_CMDSUB:
	    wb_cat(buf, L"$(");
	    wb_cat(buf, w->wu_cmdsub);
	    wb_cat(buf, L")");
	    break;
	case WT_ARITH:
	    wb_cat(buf, L"$((");
	    print_word(buf, w->wu_arith);
	    wb_cat(buf, L"))");
	    break;
	}
	w = w->next;
    }
}

void print_paramexp(xwcsbuf_T *restrict buf, const paramexp_T *restrict p)
{
    wchar_t c;

    wb_cat(buf, L"${");
    if (p->pe_type & PT_NUMBER)
	wb_wccat(buf, L'#');
    if (p->pe_type & PT_NEST)
	print_word(buf, p->pe_nest);
    else
	wb_cat(buf, p->pe_name);
    if (p->pe_start) {
	wb_wccat(buf, L'[');
	print_word(buf, p->pe_start);
	if (p->pe_end) {
	    wb_wccat(buf, L',');
	    print_word(buf, p->pe_end);
	}
	wb_wccat(buf, L']');
    }
    if (p->pe_type & PT_COLON)
	wb_wccat(buf, L':');
    switch (p->pe_type & PT_MASK) {
    case PT_PLUS:
	wb_wccat(buf, L'+');
	goto append_subst;
    case PT_MINUS:
	wb_wccat(buf, L'-');
	goto append_subst;
    case PT_ASSIGN:
	wb_wccat(buf, L'=');
	goto append_subst;
    case PT_ERROR:
	wb_wccat(buf, L'?');
	goto append_subst;
    case PT_MATCH:
	if (p->pe_type & PT_MATCHHEAD) {
	    c = L'#';
	} else {
	    assert(p->pe_type & PT_MATCHTAIL);
	    c = L'%';
	}
	wb_wccat(buf, c);
	if (p->pe_type & PT_MATCHLONGEST)
	    wb_wccat(buf, c);
	print_word(buf, p->pe_match);
	break;
    case PT_SUBST:
	wb_wccat(buf, L'/');
	if (p->pe_type & PT_SUBSTALL)
	    wb_wccat(buf, L'/');
	else if (p->pe_type & PT_MATCHHEAD)
	    wb_wccat(buf, L'#');
	else if (p->pe_type & PT_MATCHTAIL)
	    wb_wccat(buf, L'%');
	print_word(buf, p->pe_match);
	wb_wccat(buf, L'/');
	goto append_subst;
append_subst:
	print_word(buf, p->pe_subst);
	break;
    }
    wb_wccat(buf, L'}');
}

void trim_end_of_buffer(xwcsbuf_T *buf)
{
    size_t i = buf->length;
    while (i > 0 && iswblank(buf->contents[--i]));
    wb_remove(buf, i + 1, SIZE_MAX);
}


/* vim: set ts=8 sts=4 sw=4 noet: */