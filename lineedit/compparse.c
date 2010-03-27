/* Yash: yet another shell */
/* compparse.c: simple parser for command line completion */
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


#include "../common.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wchar.h>
#if YASH_ENABLE_ALIAS
# include "../alias.h"
#endif
#include "../expand.h"
#include "../parser.h"
#include "../plist.h"
#include "../strbuf.h"
#include "../util.h"
#include "../variable.h"
#include "../xfnmatch.h"
#include "complete.h"
#include "compparse.h"
#include "editing.h"
#include "lineedit.h"


/* This structure contains data used during parsing */
typedef struct cparseinfo_T {
    xwcsbuf_T buf;
    size_t bufindex;
#if YASH_ENABLE_ALIAS
    struct aliaslist_T *aliaslist;
#endif
    le_context_T *ctxt;
} cparseinfo_T;
/* The `buf' buffer contains the first `le_main_index' characters of the edit
 * buffer. During parsing, alias substitution may be performed on this buffer.
 * The `bufindex' index indicates the point the parser is currently parsing.
 * The `ctxt' member points to the structure in which the final result is saved.
 */

/* This structure contains data used during parsing */
static cparseinfo_T *pi;

#define BUF       (pi->buf.contents)
#define LEN       (pi->buf.length)
#define INDEX     (pi->bufindex)


static void print_context_info(const le_context_T *ctxt)
    __attribute__((nonnull));
static bool cparse_commands(void);
static void skip_blanks(void);
#if YASH_ENABLE_ALIAS
static bool csubstitute_alias(substaliasflags_T flags);
#else
# define csubstitute_alias(flags) false
#endif
static bool cparse_command(void);
static bool token_at_current(const wchar_t *token)
    __attribute__((nonnull,pure));
static bool cparse_simple_command(void);
static bool cparse_redirections(void);
static bool ctryparse_assignment(void);
static bool ctryparse_redirect(void);
static bool cparse_for_command(void);
static bool cparse_case_command(void);
static bool cparse_word(bool testfunc(wchar_t c), wchar_t **resultword)
    __attribute__((nonnull(1)));
static bool cparse_and_expand_special_word_unit(
	le_quote_T quote, xwcsbuf_T *word)
    __attribute__((nonnull));
static wordunit_T *cparse_special_word_unit(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *cparse_paramexp_raw(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *cparse_paramexp_in_brace(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *cparse_arith(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *cparse_cmdsubst_in_paren(void)
    __attribute__((malloc,warn_unused_result));


/* Parses the contents of the edit buffer (`le_main_buffer') from the beginning
 * up to the current cursor position (`le_main_index') and determines the
 * current completion context.
 * The result is put in `*ctxt'.
 * Returns true iff successful.
 * The context data must be freed using the `le_free_context' function
 * regardless of whether successful or not. */
bool le_get_context(le_context_T *ctxt)
{
    assert(wcslen(le_main_buffer.contents) == le_main_buffer.length);

    cparseinfo_T parseinfo;
    wb_init(&parseinfo.buf);
    wb_ncat_force(&parseinfo.buf, le_main_buffer.contents, le_main_index);
    parseinfo.bufindex = 0;
#if YASH_ENABLE_ALIAS
    parseinfo.aliaslist = new_aliaslist();
#endif
    parseinfo.ctxt = ctxt;

    pi = &parseinfo;
    while (!cparse_commands())
	parseinfo.bufindex++;
#ifndef NDEBUG
    pi = NULL;
#endif

    wb_destroy(&parseinfo.buf);
#if YASH_ENABLE_ALIAS
    destroy_aliaslist(parseinfo.aliaslist);
#endif

    ctxt->src = unescape(ctxt->pattern);
    if (ctxt->type == CTXT_NORMAL
	    && is_pathname_matching_pattern(ctxt->pattern)) {
	ctxt->substsrc = true;
    } else {
	xwcsbuf_T buf;
	ctxt->substsrc = false;
	ctxt->pattern =
	    wb_towcs(wb_wccat(wb_initwith(&buf, ctxt->pattern), L'*'));
    }

    if (le_state == LE_STATE_SUSPENDED_COMPDEBUG)
	print_context_info(ctxt);

    ctxt->cpattern = xfnm_compile(
	    ctxt->pattern, XFNM_HEADONLY | XFNM_TAILONLY);
    if (ctxt->cpattern == NULL) {
	le_compdebug("failed to compile pattern \"%ls\"", ctxt->pattern);
	return false;
    }
    return true;
}

void print_context_info(const le_context_T *ctxt)
{
#ifdef NDEBUG
    const char *s;
#else
    const char *s = s;
#endif
    switch (ctxt->quote) {
	case QUOTE_NONE:    s = "none";    break;
	case QUOTE_NORMAL:  s = "normal";  break;
	case QUOTE_SINGLE:  s = "single";  break;
	case QUOTE_DOUBLE:  s = "double";  break;
    }
    le_compdebug("quote type: %s", s);
    switch (ctxt->type) {
	case CTXT_NORMAL:         s = "normal"; break;
	case CTXT_TILDE:          s = "tilde"; break;
	case CTXT_VAR:            s = "variable";  break;
	case CTXT_VAR_BRCK:       s = "variable in brace";  break;
	case CTXT_VAR_BRCK_WORD:  s = "word in braced variable"; break;
	case CTXT_ARITH:          s = "arithmetic";  break;
	case CTXT_REDIR:          s = "redirection";  break;
	case CTXT_REDIR_FD:       s = "redirection (fd)";  break;
    }
    le_compdebug("context type: %s", s);
    for (int i = 0; i < ctxt->pwordc; i++)
	le_compdebug("preceding word %d: \"%ls\"",
		i + 1, (const wchar_t *) ctxt->pwords[i]);
    le_compdebug("source word: \"%ls\"", ctxt->src);
    le_compdebug(" as pattern: \"%ls\"", ctxt->pattern);
}

/* Following parser functions return true iff parsing is finished and the result
 * is saved in `pi->ctxt'.
 * The result is saved in the following variables of the context structure:
 *     quote, type, pwordc, pwords, pattern, srcindex. */

/* Parses commands from the current position until a right parenthesis (")") is
 * found or the whole line is parsed. */
bool cparse_commands(void)
{
    do {
	skip_blanks();
	switch (BUF[INDEX]) {
	    case L'\0':
		goto end;
	    case L'\n':  case L';':  case L'&':  case L'|':
		INDEX++;
		continue;
	    case L')':
		return false;
	}
    } while (!cparse_command());
    return true;

end:
    pi->ctxt->quote = QUOTE_NORMAL;
    pi->ctxt->type = CTXT_NORMAL;
    pi->ctxt->pwordc = 0;
    pi->ctxt->pwords = xmalloc(1 * sizeof *pi->ctxt->pwords);
    pi->ctxt->pwords[0] = NULL;
    pi->ctxt->pattern = xwcsdup(L"");
    pi->ctxt->srcindex = le_main_index;
    return true;
}

/* Skips blank characters. */
void skip_blanks(void)
{
    while (iswblank(BUF[INDEX]))
	INDEX++;
}

/* Performs alias substitution at the current position.
 * See the description of `substitute_alias' for the usage of `flags'.
 * Returns true iff any alias is substituted. */
bool csubstitute_alias(substaliasflags_T flags)
{
    size_t len = 0;
    while (is_alias_name_char(BUF[INDEX + len]))
	len++;
    if (len == 0 || BUF[INDEX + len] == L'\0')
	return false;
    return substitute_alias(&pi->buf, INDEX, len, pi->aliaslist, flags);
}

/* Parses a command from the current position. */
bool cparse_command(void)
{
    if (BUF[INDEX] == L'(') {
	INDEX++;
	if (cparse_commands())
	    return true;
	assert(BUF[INDEX] == L')');
	INDEX++;
	return cparse_redirections();
    } else if (token_at_current(L"{") || token_at_current(L"!")) {
	INDEX++;
	return false;
    } else if (token_at_current(L"if") || token_at_current(L"do")) {
	INDEX += 2;
	return false;
    } else if (token_at_current(L"then") || token_at_current(L"else")
	    || token_at_current(L"elif")) {
	INDEX += 4;
	return false;
    } else if (token_at_current(L"while") || token_at_current(L"until")) {
	INDEX += 5;
	return false;
    } else if (token_at_current(L"}")) {
	INDEX++;
	return cparse_redirections();
    } else if (token_at_current(L"fi")) {
	INDEX += 2;
	return cparse_redirections();
    } else if (token_at_current(L"done") || token_at_current(L"esac")) {
	INDEX += 4;
	return cparse_redirections();
    } else if (token_at_current(L"for")) {
	return cparse_for_command();
    } else if (token_at_current(L"case")) {
	return cparse_case_command();
    } else {
	return cparse_simple_command();
    }
}

/* Returns true iff the specified word is at the current position. */
bool token_at_current(const wchar_t *token)
{
    const wchar_t *c = matchwcsprefix(BUF + INDEX, token);
    return c != NULL && is_token_delimiter_char(*c);
}

/* Parses a simple command. */
bool cparse_simple_command(void)
{
cparse_simple_command:
    if (csubstitute_alias(AF_NONGLOBAL | AF_NORECUR))
	return false;

    size_t saveindex;
    skip_blanks();
    saveindex = INDEX;
    if (ctryparse_redirect())
	return true;
    if (saveindex != INDEX) {
	skip_blanks();
	goto cparse_simple_command;
    }
    if (ctryparse_assignment())
	return true;
    if (saveindex != INDEX) {
	skip_blanks();
	goto cparse_simple_command;
    }

    wchar_t *resultword;
    plist_T pwords;
    pl_init(&pwords);

    for (;;) {
	switch (BUF[INDEX]) {
	    case L'\n':  case L';':  case L'&':  case L'|':
	    case L'(':  case L')':
		recfree(pl_toary(&pwords), free);
		return false;
	}
	if (cparse_word(is_token_delimiter_char, &resultword)) {
	    pi->ctxt->pwordc = pwords.length;
	    pi->ctxt->pwords = pl_toary(&pwords);
	    return true;
	}
	pl_add(&pwords, resultword);
	skip_blanks();
	if (csubstitute_alias(0))
	    skip_blanks();
	if (ctryparse_redirect()) {
	    recfree(pl_toary(&pwords), free);
	    return true;
	}
	skip_blanks();
    }
}

/* Parses redirections as many as possible.
 * Global aliases are substituted if necessary. */
bool cparse_redirections(void)
{
    size_t saveindex;
    do {
	skip_blanks();
	saveindex = INDEX;
	if (ctryparse_redirect())
	    return true;
    } while (saveindex != INDEX || csubstitute_alias(0));
    return false;
}

/* Parses an assignment if any.
 * `skip_blanks' should be called before this function is called. */
bool ctryparse_assignment(void)
{
    size_t index = INDEX;

    if (BUF[index] == L'=' || iswdigit(BUF[index]))
	return false;
    while (is_name_char(BUF[index]))
	index++;
    if (BUF[index] != L'=')
	return false;
    INDEX = index + 1;

    if (true /* TODO: BUF[INDEX] != L'('*/) {
	bool result = cparse_word(is_token_delimiter_char, NULL); // TODO temporary implementation
	if (result) {
	    pi->ctxt->pwordc = 0;
	    pi->ctxt->pwords = xmalloc(1 * sizeof *pi->ctxt->pwords);
	    pi->ctxt->pwords[0] = NULL;
	}
	return result;
    } else {
    }
}

/* Parses a redirection if any.
 * `skip_blanks' should be called before this function is called. */
bool ctryparse_redirect(void)
{
    size_t index = INDEX;
    le_contexttype_T type;
    bool result;

    while (iswdigit(BUF[index]))
	index++;
    switch (BUF[index]) {
    case L'<':
	switch (BUF[index + 1]) {
	case L'<':
	    type = CTXT_REDIR;
	    switch (BUF[index + 2]) {
	    case L'<':  /* here-string */
	    case L'-':  /* here-document */
		INDEX = index + 3;  break;
	    default:    /* here-document */
		INDEX = index + 2;  break;
	    }
	    break;
	case L'>':  INDEX = index + 2;  type = CTXT_REDIR;     break;
	case L'&':  INDEX = index + 2;  type = CTXT_REDIR_FD;  break;
	case L'(':  INDEX = index + 2;  goto parse_inner;
	default:    INDEX = index + 1;  type = CTXT_REDIR;     break;
	}
	break;
    case L'>':
	switch (BUF[INDEX + 1]) {
	case L'>':
	case L'|':  INDEX = index + 2;  type = CTXT_REDIR;  break;
	case L'(':  INDEX = index + 2;  goto parse_inner;
	case L'&':  INDEX = index + 2;  type = CTXT_REDIR_FD;  break;
	default:    INDEX = index + 1;  type = CTXT_REDIR;  break;
	}
	break;
    default:  /* not a redirection */
	return false;
    }

    skip_blanks();
    result = cparse_word(is_token_delimiter_char, NULL);
    if (result) {
	if (pi->ctxt->type == CTXT_NORMAL)
	    pi->ctxt->type = type;
	pi->ctxt->pwordc = 0;
	pi->ctxt->pwords = xmalloc(1 * sizeof *pi->ctxt->pwords);
	pi->ctxt->pwords[0] = NULL;
    }
    return result;

parse_inner:
    result = cparse_commands();
    if (!result) {
	assert(BUF[INDEX] == L')');
	INDEX++;
    }
    return result;
}

/* Parses a for command. */
bool cparse_for_command(void)
{
    return false; //TODO
}

/* Parses a case command. */
bool cparse_case_command(void)
{
    return false; //TODO
}

/* Parses the word at the current position.
 * `skip_blanks' should be called before this function is called.
 * `testfunc' is a function that determines if a character is a word delimiter.
 * It must return true for L'\0'.
 * If the word was completely parsed (that is, the return value is false) and
 * if `resultword' is non-NULL, the word (after quote removal) is assigned to
 * `*resultword' as a newly-malloced string.
 * If the parser reached the end of the string, the return value is true and
 * the result is saved in `pi->ctxt'. This function, however, updates only the
 * `type', `quote', `pattern' and `srcindex' members of `pi->ctxt'. The caller
 * must update the other members (`pwordc' and `pwords'). */
bool cparse_word(bool testfunc(wchar_t c), wchar_t **resultword)
{
    le_quote_T quote = QUOTE_NORMAL;
    size_t srcindex = le_main_index - (LEN - INDEX);
    xwcsbuf_T word;

    wb_init(&word);
    while (quote == QUOTE_NORMAL ? !testfunc(BUF[INDEX]) : BUF[INDEX] != L'\0'){
	switch (BUF[INDEX]) {
	case L'$':
	    if (cparse_and_expand_special_word_unit(quote, &word)) {
		wb_destroy(&word);
		return true;
	    }
	    break;
	case L'`':
	    //TODO
	case L'"':
	    quote = (quote == QUOTE_NORMAL) ? QUOTE_DOUBLE : QUOTE_NORMAL;
	    INDEX++;
	    break;
	case L'\'':
	    if (quote == QUOTE_NORMAL) {
		INDEX++;
		const wchar_t *end = wcschr(BUF + INDEX, L'\'');
		if (end == NULL) {
		    quote = QUOTE_SINGLE;
		    wb_cat(&word, BUF + INDEX);
		    goto return_true;
		} else {
		    wb_ncat_force(&word, BUF + INDEX, end - (BUF + INDEX));
		    INDEX = (end + 1) - BUF;
		    continue;
		}
	    }
	    /* falls thru! */
	default:
	    if (quote == QUOTE_NORMAL) {
		if (BUF[INDEX] != L'\\' || BUF[INDEX + 1] != L'\0')
		    wb_wccat(&word, BUF[INDEX]);
		INDEX++;
	    } else {
		if (BUF[INDEX] == L'\\') {
		    wchar_t nextchar = BUF[++INDEX];
		    if (nextchar == L'\0')
			goto return_true;
		    else if (wcschr(CHARS_ESCAPABLE, nextchar))
			wb_wccat(wb_wccat(&word, L'\\'), nextchar), INDEX++;
		    else
			wb_wccat(wb_wccat(&word, L'\\'), L'\\');
		} else {
		    wb_wccat(wb_wccat(&word, L'\\'), BUF[INDEX++]);
		}
	    }
	    break;
	}
    }

    if (BUF[INDEX] == L'\0') {
return_true:
	pi->ctxt->type = CTXT_NORMAL;
	pi->ctxt->quote = quote;
	pi->ctxt->pattern = wb_towcs(&word);
	pi->ctxt->srcindex = srcindex;
	return true;
    } else {
	if (resultword != NULL)
	    *resultword = wb_towcs(&word);
	else
	    wb_destroy(&word);
	return false;
    }
}

/* Parses a parameter expansion or command substitution that starts with '$'
 * and expands it.
 * If the expansion/substitution was completely expanded, the return value is
 * false and the result is appended to the `word' buffer.
 * If the parser reached the end of the string, the return value is true and
 * the result is saved in `pi->ctxt'. This function, however, updates only the
 * `type', `quote', `pattern' and `srcindex' members of `pi->ctxt'. The caller
 * must update the other members (`pwordc' and `pwords'). */
bool cparse_and_expand_special_word_unit(le_quote_T quote, xwcsbuf_T *word)
{
    assert(BUF[INDEX] == L'$');

    wordunit_T *wu = cparse_special_word_unit();
    if (wu == NULL) {
	pi->ctxt->quote = quote;
	return true;
    } else {
	wchar_t *s = expand_single(wu, tt_none);
	free(wu);
	assert(s != NULL);
	s = escapefree(s, (quote != QUOTE_NORMAL) ? CHARS_ESCAPED : NULL);
	wb_catfree(word, s);
	return false;
    }
}

/* Parses a parameter expansion or command substitution that starts with '$'.
 * If the parser reached the end of the string, the return value is NULL and
 * the result is saved in `pi->ctxt'. This function updates only the `type',
 * `pattern' and `srcindex' members of `pi->ctxt'. The caller must update the
 * other members (`quote', `pwordc' and `pwords'). */
wordunit_T *cparse_special_word_unit(void)
{
    assert(BUF[INDEX] == L'$');
    switch (BUF[INDEX + 1]) {
	case L'{':
	    INDEX++;
	    return cparse_paramexp_in_brace();
	case L'(':
	    if (BUF[INDEX + 2] == L'(')
		return cparse_arith();
	    else
		return cparse_cmdsubst_in_paren();
	default:
	    return cparse_paramexp_raw();
    }
}

/* Parses a parameter that is not enclosed by { }.
 * If the parser reached the end of the string, the return value is NULL and
 * the result is saved in `pi->ctxt'. This function updates only the `type',
 * `pattern' and `srcindex' members of `pi->ctxt'. The caller must update the
 * other members (`quote', `pwordc' and `pwords'). */
wordunit_T *cparse_paramexp_raw(void)
{
    assert(BUF[INDEX] == L'$');

    size_t namelen;
    switch (BUF[INDEX + 1]) {
	case L'@':  case L'*':  case L'#':  case L'?':
	case L'-':  case L'$':  case L'!':
	    namelen = 1;
	    break;
	default:
	    if (iswdigit(BUF[INDEX + 1])) {
		namelen = 1;
	    } else {
		namelen = 0;
		while (is_portable_name_char(BUF[INDEX + 1 + namelen]))
		    namelen++;
		if (BUF[INDEX + 1 + namelen] == L'\0') {
		    /* complete variable name */
		    pi->ctxt->type = CTXT_VAR;
		    pi->ctxt->quote = QUOTE_NONE;
		    pi->ctxt->pattern = xwcsndup(BUF + INDEX + 1, namelen);
		    pi->ctxt->srcindex = le_main_index - namelen;
		    return NULL;
		}
	    }
	    break;
    }

    wordunit_T *wu = xmalloc(sizeof *wu);
    wu->next = NULL;
    wu->wu_type = WT_PARAM;
    wu->wu_param = xmalloc(sizeof *wu->wu_param);
    wu->wu_param->pe_type = PT_MINUS;
    wu->wu_param->pe_name = xwcsndup(BUF + INDEX + 1, namelen);
    wu->wu_param->pe_start = wu->wu_param->pe_end =
    wu->wu_param->pe_match = wu->wu_param->pe_subst = NULL;

    INDEX += namelen + 1;
    return wu;
}

/* Parses a parameter enclosed by { }.
 * If the parser reached the end of the string, the return value is NULL and
 * the result is saved in `pi->ctxt'. This function updates only the `type',
 * `pattern' and `srcindex' members of `pi->ctxt'. The caller must update the
 * other members (`quote', `pwordc' and `pwords'). */
wordunit_T *cparse_paramexp_in_brace(void)
{
    paramexp_T *pe = xmalloc(sizeof *pe);
    pe->pe_type = 0;
    pe->pe_name = NULL;
    pe->pe_start = pe->pe_end = pe->pe_match = pe->pe_subst = NULL;

    assert(BUF[INDEX] == L'{');
    INDEX++;

    /* parse PT_NUMBER */
    if (BUF[INDEX] == L'#') {
	switch (BUF[INDEX + 1]) {
	    case L'\0': case L'}':
	    case L'+':  case L'=':  case L':':  case L'/':  case L'%':
		break;
	    case L'-':  case L'?':  case L'#':
		if (BUF[INDEX + 2] != L'}')
		    break;
		/* falls thru! */
	    default:
		pe->pe_type |= PT_NUMBER;
		INDEX++;
		break;
	}
    }

    /* parse nested expansion */
    if (BUF[INDEX] == L'{') {
	pe->pe_type |= PT_NEST;
	pe->pe_nest = cparse_paramexp_in_brace();
	if (pe->pe_nest == NULL)
	    goto return_null;
    } else if (BUF[INDEX] == L'`'
		|| (BUF[INDEX] == L'$'
		    && (BUF[INDEX + 1] == L'{' || BUF[INDEX + 1] == L'('))) {
	pe->pe_type |= PT_NEST;
	pe->pe_nest = cparse_special_word_unit();
	if (pe->pe_nest == NULL)
	    goto return_null;
    } else {
	/* no nesting: parse parameter name */
    }

    //TODO

    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_PARAM;
    result->wu_param = pe;
    return result;

return_null:
    free(pe);
    return NULL;
}

/* Parses an arithmetic expansion.
 * If the parser reached the end of the string, the return value is NULL and
 * the result is saved in `pi->ctxt'. This function updates only the `type',
 * `pattern' and `srcindex' members of `pi->ctxt'. The caller must update the
 * other members (`quote', `pwordc' and `pwords'). */
wordunit_T *cparse_arith(void)
{
    //TODO
    return false;
}

/* Parses a command substitution enclosed by "$( )".
 * If the parser reached the end of the string, the return value is NULL and
 * the result is saved in `pi->ctxt'. This function updates only the `type',
 * `pattern' and `srcindex' members of `pi->ctxt'. The caller must update the
 * other members (`quote', `pwordc' and `pwords'). */
wordunit_T *cparse_cmdsubst_in_paren(void)
{
    //TODO
    return false;
}

/* Frees the contents of the specified `le_context_T' data. */
void le_free_context(le_context_T *ctxt)
{
    recfree(ctxt->pwords, free);
    free(ctxt->src);
    free(ctxt->pattern);
    xfnm_free(ctxt->cpattern);
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
