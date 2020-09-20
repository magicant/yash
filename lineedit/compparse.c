/* Yash: yet another shell */
/* compparse.c: simple parser for command line completion */
/* (C) 2007-2020 magicant */

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
#include "compparse.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "../alias.h"
#include "../expand.h"
#include "../option.h"
#include "../parser.h"
#include "../plist.h"
#include "../strbuf.h"
#include "../util.h"
#include "../variable.h"
#include "../xfnmatch.h"
#include "complete.h"
#include "editing.h"
#include "lineedit.h"


/* This structure contains data used in parsing */
typedef struct cparseinfo_T {
    xwcsbuf_T buf;
    size_t bufindex;
    struct aliaslist_T *aliaslist;
    le_context_T *ctxt;
} cparseinfo_T;
/* The `buf' buffer contains the first `le_main_index' characters of the edit
 * buffer. During parsing, alias substitution may be performed on this buffer.
 * The `bufindex' index indicates the point the parser is currently parsing.
 * The `ctxt' member points to the structure in which the final result is saved.
 */

/* This structure contains data used during parsing */
static cparseinfo_T *pi;

#define BUF   (pi->buf.contents)
#define LEN   (pi->buf.length)
#define INDEX (pi->bufindex)


static void empty_pwords(void);
static void set_pwords(plist_T *pwords)
    __attribute__((nonnull));
static bool cparse_commands(void);
static void skip_blanks(void);
static void skip_blanks_and_newlines(void);
static bool csubstitute_alias(substaliasflags_T flags);
static bool cparse_command(void);
static bool has_token(const wchar_t *token)
    __attribute__((nonnull,pure));
static bool cparse_simple_command(void);
static bool cparse_redirections(void);
static bool ctryparse_assignment(void);
static bool ctryparse_redirect(void);
static bool cparse_for_command(void);
static bool cparse_case_command(void);
static bool cparse_function_definition(void);
static bool ctryparse_word(tildetype_T tilde, le_contexttype_T ctxttype);
static wordunit_T *cparse_word(
	bool testfunc(wchar_t c), tildetype_T tilde, le_contexttype_T ctxttype)
    __attribute__((nonnull,malloc,warn_unused_result));
static bool ctryparse_tilde(void);
static wordunit_T *cparse_special_word_unit(le_contexttype_T ctxttype)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *cparse_paramexp_raw(le_contexttype_T ctxttype)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *cparse_paramexp_in_brace(le_contexttype_T ctxttype)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *cparse_arith(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *cparse_cmdsubst_in_paren(void)
    __attribute__((malloc,warn_unused_result));
static wordunit_T *cparse_cmdsubst_in_backquote(void)
    __attribute__((malloc,warn_unused_result));
static bool is_slash_or_closing_brace(wchar_t c)
    __attribute__((const));
static bool is_closing_brace(wchar_t c)
    __attribute__((const));
static bool is_closing_bracket(wchar_t c)
    __attribute__((const));
static bool is_arith_delimiter(wchar_t c)
    __attribute__((pure));
static bool remove_braceexpand(wchar_t *s)
    __attribute__((nonnull));
static bool remove_braceexpand_inner(xwcsbuf_T *buf, size_t *index)
    __attribute__((nonnull));


/* Parses the contents of the edit buffer (`le_main_buffer') from the beginning
 * up to the current cursor position (`le_main_index') and determines the
 * current completion context.
 * The results are returned as a newly malloced `le_context_T' data. */
le_context_T *le_get_context(void)
{
    assert(wcslen(le_main_buffer.contents) == le_main_buffer.length);

    le_context_T *ctxt = xmalloc(sizeof *ctxt);

    cparseinfo_T parseinfo;
    wb_initwithmax(&parseinfo.buf, le_main_index);
    wb_ncat_force(&parseinfo.buf, le_main_buffer.contents, le_main_index);
    parseinfo.bufindex = 0;
    parseinfo.aliaslist = NULL;
    parseinfo.ctxt = ctxt;

    pi = &parseinfo;
    while (!cparse_commands())
	parseinfo.bufindex++;
#ifndef NDEBUG
    pi = NULL;
#endif

    wb_destroy(&parseinfo.buf);
    destroy_aliaslist(parseinfo.aliaslist);

    if (shopt_braceexpand)
	if (remove_braceexpand(ctxt->pattern))
	    ctxt->type |= CTXT_EBRACED;
    ctxt->src = unescape(ctxt->pattern);
    if (is_pathname_matching_pattern(ctxt->pattern)) {
	ctxt->substsrc = true;
    } else {
	ctxt->substsrc = false;

	xwcsbuf_T buf;
	wb_initwith(&buf, ctxt->pattern);
	wb_wccat(&buf, L'*');
	ctxt->pattern = wb_towcs(&buf);
    }
    ctxt->origindex = le_main_index;

    return ctxt;
}

/* If `pi->ctxt->pwords' is NULL, assigns a new empty list to it. */
void empty_pwords(void)
{
    if (pi->ctxt->pwords == NULL) {
	pi->ctxt->pwordc = 0;
	pi->ctxt->pwords = xmalloc(1 * sizeof *pi->ctxt->pwords);
	pi->ctxt->pwords[0] = NULL;
    }
}

/* Sets `pi->ctxt->pwords' to the contents of `pwords'. */
void set_pwords(plist_T *pwords)
{
    if (pi->ctxt->pwords == NULL) {
	pi->ctxt->pwordc = pwords->length;
	pi->ctxt->pwords = pl_toary(pwords);
    } else {
	plfree(pl_toary(pwords), free);
    }
}

/* Following parser functions return true iff parsing is finished and the result
 * is saved in the context structure `pi->ctxt'.
 * The result is saved in the following variables of the context structure:
 *     quote, type, pwordc, pwords, pattern, srcindex. */

/* Parses commands from the current position until a right parenthesis (")") is
 * found or the whole line is parsed. */
bool cparse_commands(void)
{
    for (;;) {
	skip_blanks();
	switch (BUF[INDEX]) {
	    case L'\n':  case L';':  case L'&':  case L'|':
		INDEX++;
		continue;
	    case L')':
		return false;
	}

	if (cparse_command())
	    return true;
    }
}

/* Skips blank characters. */
void skip_blanks(void)
{
    while (iswblank(BUF[INDEX]))
	INDEX++;
}

/* Skips blank characters and newlines. */
void skip_blanks_and_newlines(void)
{
    while (BUF[INDEX] == L'\n' || iswblank(BUF[INDEX]))
	INDEX++;
}

/* Performs alias substitution at the current position.
 * See the description of `substitute_alias' for the usage of `flags'.
 * Returns true iff any alias is substituted.
 * If the word at the current INDEX is at the end of BUF, the word is not
 * substituted because it is the word being completed. */
bool csubstitute_alias(substaliasflags_T flags)
{
    return substitute_alias(&pi->buf, INDEX, &pi->aliaslist, flags | AF_NOEOF);
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
    } else if (has_token(L"{") || has_token(L"!")) {
	INDEX++;
	return false;
    } else if (has_token(L"if") || has_token(L"do")) {
	INDEX += 2;
	return false;
    } else if (has_token(L"then") || has_token(L"else")
	    || has_token(L"elif")) {
	INDEX += 4;
	return false;
    } else if (has_token(L"while") || has_token(L"until")) {
	INDEX += 5;
	return false;
    } else if (has_token(L"}")) {
	INDEX++;
	return cparse_redirections();
    } else if (has_token(L"fi")) {
	INDEX += 2;
	return cparse_redirections();
    } else if (has_token(L"done") || has_token(L"esac")) {
	INDEX += 4;
	return cparse_redirections();
    } else if (has_token(L"for")) {
	return cparse_for_command();
    } else if (has_token(L"case")) {
	return cparse_case_command();
    } else if (!posixly_correct && has_token(L"function")) {
	return cparse_function_definition();
    } else {
	return cparse_simple_command();
    }
}

/* Returns true iff the specified word is at the current position (and it is not
 * at the end of the buffer). */
bool has_token(const wchar_t *token)
{
    const wchar_t *c = matchwcsprefix(BUF + INDEX, token);
    return c != NULL && *c != L'\0' && is_token_delimiter_char(*c);
}

/* Parses a simple command. */
bool cparse_simple_command(void)
{
cparse_simple_command:
    if (csubstitute_alias(AF_NONGLOBAL))
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

    plist_T pwords;
    pl_init(&pwords);
    for (;;) {
	switch (BUF[INDEX]) {
	    case L'\n':  case L';':  case L'&':  case L'|':
	    case L'(':   case L')':
		plfree(pl_toary(&pwords), free);
		return false;
	}

	wordunit_T *w = cparse_word(is_token_delimiter_char, TT_SINGLE,
		pwords.length == 0 ? CTXT_COMMAND : CTXT_ARGUMENT);
	if (w == NULL) {
	    set_pwords(&pwords);
	    return true;
	} else {
	    expand_multiple(w, &pwords);
	    wordfree(w);
	}

	do {
	    skip_blanks();
	    if (ctryparse_redirect()) {
		plfree(pl_toary(&pwords), free);
		return true;
	    }
	    skip_blanks();
	} while (csubstitute_alias(0));
    }
}

/* Parses redirections as many as possible.
 * Global aliases are substituted if necessary. */
bool cparse_redirections(void)
{
    for (;;) {
	skip_blanks();
	size_t saveindex = INDEX;
	if (ctryparse_redirect())
	    return true;
	if (saveindex == INDEX) {
	    skip_blanks();
	    if (!csubstitute_alias(0))
		return false;
	}
    }
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

    if (BUF[INDEX] != L'(') {
	/* scalar variable */
	if (ctryparse_word(TT_MULTI, CTXT_ASSIGN)) {
	    empty_pwords();
	    return true;
	} else {
	    return false;
	}
    } else {
	/* parse array contents */
	plist_T pwords;
	pl_init(&pwords);
	INDEX++;
	for (;;) {
	    do
		skip_blanks();
	    while (csubstitute_alias(0));

	    if (BUF[INDEX] != L'\0' && is_token_delimiter_char(BUF[INDEX])) {
		if (BUF[INDEX] == L')')
		    INDEX++;
		plfree(pl_toary(&pwords), free);
		return false;
	    }

	    wordunit_T *w = cparse_word(
		    is_token_delimiter_char, TT_SINGLE, CTXT_ASSIGN);
	    if (w == NULL) {
		set_pwords(&pwords);
		return true;
	    } else {
		expand_multiple(w, &pwords);
		wordfree(w);
	    }
	}
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
	switch (BUF[index + 1]) {
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
    if (ctryparse_word(TT_SINGLE, type)) {
	empty_pwords();
	return true;
    } else {
	return false;
    }

parse_inner:
    result = cparse_commands();
    if (!result) {
	assert(BUF[INDEX] == L')');
	INDEX++;
    }
    return result;
}

/* Parses a for command.
 * There must be the "for" keyword at the current position. */
bool cparse_for_command(void)
{
    assert(wcsncmp(&BUF[INDEX], L"for", 3) == 0);
    INDEX += 3;
    do
	skip_blanks();
    while (csubstitute_alias(0));

    /* parse variable name */
    wordunit_T *w = cparse_word(is_token_delimiter_char, TT_NONE, CTXT_VAR);
    if (w == NULL) {
	empty_pwords();
	return true;
    }
    wordfree(w);
    skip_blanks_and_newlines();

    /* parse "in" ... */
    bool in = has_token(L"in");
    if (in) {
	plist_T pwords;
	pl_init(&pwords);
	INDEX += 2;
	for (;;) {
	    do
		skip_blanks();
	    while (csubstitute_alias(0));

	    if (BUF[INDEX] == L';' || BUF[INDEX] == L'\n') {
		plfree(pl_toary(&pwords), free);
		INDEX++;
		break;
	    }
	    if (BUF[INDEX] != L'\0' && is_token_delimiter_char(BUF[INDEX])) {
		plfree(pl_toary(&pwords), free);
		return false;
	    }

	    w = cparse_word(is_token_delimiter_char, TT_SINGLE, CTXT_NORMAL);
	    if (w == NULL) {
		set_pwords(&pwords);
		return true;
	    } else {
		expand_multiple(w, &pwords);
		wordfree(w);
	    }
	}
	skip_blanks_and_newlines();
    }

    /* parse "do" ... */
    if (has_token(L"do")) {
	INDEX += 2;
	return false;
    }

    /* found no "in" or "do", so the next word should be one of them */
    w = cparse_word(is_token_delimiter_char, TT_NONE,
	    in ? CTXT_FOR_DO : CTXT_FOR_IN);
    if (w == NULL) {
	empty_pwords();
	return true;
    }
    wordfree(w);

    /* syntax error */
    return false;
}

/* Parses a case command.
 * There must be the "case" keyword at the current position. */
bool cparse_case_command(void)
{
    assert(wcsncmp(&BUF[INDEX], L"case", 4) == 0);
    INDEX += 4;
    do
	skip_blanks();
    while (csubstitute_alias(0));

    /* parse matched word */
    wordunit_T *w =
	cparse_word(is_token_delimiter_char, TT_SINGLE, CTXT_NORMAL);
    if (w == NULL) {
	empty_pwords();
	return true;
    }
    wordfree(w);
    skip_blanks_and_newlines();

    /* parse "in" */
    if (has_token(L"in")) {
	INDEX += 2;
	return false;
    }

    /* there is no "in", so the next word should be "in". */
    w = cparse_word(is_token_delimiter_char, TT_NONE, CTXT_CASE_IN);
    if (w == NULL) {
	empty_pwords();
	return true;
    }
    wordfree(w);

    /* syntax error */
    return false;
}

/* Parses a function definition command.
 * There must be the "function" keyword at the current position. */
bool cparse_function_definition(void)
{
    assert(wcsncmp(&BUF[INDEX], L"function", 8) == 0);
    INDEX += 8;
    skip_blanks();

    /* parse the function name */
    wordunit_T *w =
	cparse_word(is_token_delimiter_char, TT_NONE, CTXT_FUNCTION);
    if (w == NULL) {
	empty_pwords();
	return true;
    }
    wordfree(w);

    return false;
}

/* Parses the word at the current position.
 * `skip_blanks' should be called before this function is called.
 * The `ctxttype' parameter is the context type of the word parsed.
 * If the word was completely parsed, the return value is false.
 * If the parser reached the end of the input string, the return value is true
 * and the result is saved in `pi->ctxt'. However, `pi->ctxt->pwords' is NULL
 * when the preceding words need to be determined by the caller. In this case,
 * the caller must update the `pwordc' and `pwords' member. */
bool ctryparse_word(tildetype_T tilde, le_contexttype_T ctxttype)
{
    wordunit_T *w = cparse_word(is_token_delimiter_char, tilde, ctxttype);
    if (w == NULL)
	return true;
    wordfree(w);
    return false;
}

/* Parses the word at the current position.
 * `skip_blanks' should be called before this function is called.
 * `testfunc' is a function that determines if a character is a word delimiter.
 * The `ctxttype' parameter is the context type of the word parsed.
 * If the word was completely parsed, the word is returned.
 * If the parser reached the end of the input string, the return value is NULL
 * and the result is saved in `pi->ctxt'. However, `pi->ctxt->pwords' is NULL
 * when the preceding words need to be determined by the caller. In this case,
 * the caller must update the `pwordc' and `pwords' member. */
wordunit_T *cparse_word(
	bool testfunc(wchar_t c), tildetype_T tilde, le_contexttype_T ctxttype)
{
cparse_word:
    if (tilde != TT_NONE)
	if (ctryparse_tilde())
	    return NULL;

    wordunit_T *first = NULL, **lastp = &first;
    bool indq = false;   /* in double quotes? */
    size_t startindex = INDEX;
    size_t srcindex = le_main_index - (LEN - INDEX);

#define MAKE_WORDUNIT_STRING                                           \
    do {                                                               \
	wordunit_T *w = xmalloc(sizeof *w);                            \
	w->next = NULL;                                                \
	w->wu_type = WT_STRING;                                        \
	w->wu_string = xwcsndup(&BUF[startindex], INDEX - startindex); \
	*lastp = w, lastp = &w->next;                                  \
    } while (0)

    while (indq || !testfunc(BUF[INDEX])) {
	wordunit_T *wu;

	switch (BUF[INDEX]) {
	case L'\0':
	    goto done;
	case L'\\':
	    if (BUF[INDEX + 1] != L'\0') {
		INDEX += 2;
		continue;
	    }
	    break;
	case L'$':
	    MAKE_WORDUNIT_STRING;
	    wu = cparse_special_word_unit(
		    (ctxttype & CTXT_MASK) | (indq ? CTXT_QUOTED : 0));
	    if (wu == NULL) {
		if (pi->ctxt->pwords == NULL
			&& (pi->ctxt->type & CTXT_VBRACED)) {
		    xwcsbuf_T buf;
		    wchar_t *prefix =
			expand_single(first, tilde, Q_WORD, ES_QUOTED_HARD);
		    assert(prefix != NULL);
		    pi->ctxt->pattern = wb_towcs(wb_catfree(
				wb_initwith(&buf, prefix), pi->ctxt->pattern));
		    pi->ctxt->srcindex = srcindex;
		}
		wordfree(first);
		return NULL;
	    }
	    *lastp = wu, lastp = &wu->next;
	    startindex = INDEX;
	    continue;
	case L'`':
	    wu = cparse_cmdsubst_in_backquote();
	    if (wu == NULL) {
		wordfree(first);
		return NULL;
	    }
	    *lastp = wu, lastp = &wu->next;
	    startindex = INDEX;
	    continue;
	case L'\'':
	    if (!indq) {
		const wchar_t *end = wcschr(&BUF[INDEX + 1], L'\'');
		if (end == NULL)
		    goto end_single_quote;
		INDEX = end - BUF;
	    }
	    break;
	case L'"':
	    indq = !indq;
	    break;
	case L':':
	    if (!indq && tilde == TT_MULTI) {
		INDEX++;
		wordfree(first);
		goto cparse_word;
	    }
	    break;
	}
	INDEX++;
    }
done:
    MAKE_WORDUNIT_STRING;

    if (BUF[INDEX] != L'\0') {
	assert(first != NULL);
	return first;
    } else {
	pi->ctxt->quote = indq ? QUOTE_DOUBLE : QUOTE_NORMAL;
	goto finish;
    }

    /* if the word ends without a closing quote, add one */
end_single_quote:;
    wordunit_T *w = xmalloc(sizeof *w);
    w->next = NULL;
    w->wu_type = WT_STRING;
    w->wu_string = malloc_wprintf(L"%ls'", &BUF[startindex]);
    *lastp = w, lastp = &w->next;

    pi->ctxt->quote = QUOTE_SINGLE;
finish:
    pi->ctxt->type = ctxttype;
    pi->ctxt->pwordc = 0;
    pi->ctxt->pwords = NULL;
    pi->ctxt->pattern = expand_single(first, tilde, Q_WORD, ES_QUOTED_HARD);
    pi->ctxt->srcindex = srcindex;
    wordfree(first);
    return NULL;
}

/* Parses a tilde expansion at the current position if any.
 * If there is a tilde expansion to complete, this function returns true after
 * setting the members of `pi->ctxt'. (However, `pi->ctxt->pwords' is NULL
 * when the preceding words need to be determined by the caller. In this case,
 * the caller must update the `pwordc' and `pwords' member.)
 * Otherwise, this function simply returns false. */
bool ctryparse_tilde(void)
{
    if (BUF[INDEX] != L'~')
	return false;

    size_t index = INDEX;
    do {
	index++;
	switch (BUF[index]) {
	    case L' ':   case L'\t':  case L'\n':
	    case L';':   case L'&':   case L'|':
	    case L'<':   case L'>':   case L'(':  case L')':
	    case L'$':   case L'`':   case L'/':  case L':':
	    case L'\\':  case L'\'':  case L'"':
	    case L'?':   case L'*':   case L'[':
		return false;
	}
	if (iswblank(BUF[index]))
	    return false;
    } while (BUF[index] != L'\0');

    pi->ctxt->quote = QUOTE_NONE;
    pi->ctxt->type = CTXT_TILDE;
    pi->ctxt->pwordc = 0;
    pi->ctxt->pwords = NULL;
    pi->ctxt->pattern = xwcsdup(&BUF[INDEX + 1]);
    pi->ctxt->srcindex = le_main_index - (LEN - (INDEX + 1));
    return true;
}

/* Parses a parameter expansion or command substitution that starts with '$'.
 * The `ctxttype' parameter is the context type of the word containing this
 * expansion.
 * If the parser reached the end of the input string, the return value is NULL
 * and the result is saved in `pi->ctxt'. However, `pi->ctxt->pwords' is NULL
 * when the preceding words need to be determined by the caller. In this case,
 * the caller must update the `pwordc' and `pwords' member. */
wordunit_T *cparse_special_word_unit(le_contexttype_T ctxttype)
{
    assert(BUF[INDEX] == L'$');
    switch (BUF[INDEX + 1]) {
	case L'{':
	    INDEX++;
	    return cparse_paramexp_in_brace(ctxttype);
	case L'(':
	    if (BUF[INDEX + 2] == L'(')
		return cparse_arith();
	    else
		return cparse_cmdsubst_in_paren();
	default:
	    return cparse_paramexp_raw(ctxttype);
    }
}

/* Parses a parameter that is not enclosed by { }.
 * The `ctxttype' parameter is the context type of the word containing this
 * parameter expansion. Only the CTXT_QUOTED flag in `ctxttype' affect the
 * result.
 * If the parser reached the end of the input string, the return value is NULL
 * and the result is saved in `pi->ctxt'. However, `pi->ctxt->pwords' is NULL
 * when the preceding words need to be determined by the caller. In this case,
 * the caller must update the `pwordc' and `pwords' member. */
wordunit_T *cparse_paramexp_raw(le_contexttype_T ctxttype)
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
		    pi->ctxt->quote = QUOTE_NONE;
		    pi->ctxt->type = CTXT_VAR | (ctxttype & CTXT_QUOTED);
		    pi->ctxt->pwordc = 0;
		    pi->ctxt->pwords = xmalloc(1 * sizeof *pi->ctxt->pwords);
		    pi->ctxt->pwords[0] = NULL;
		    pi->ctxt->pattern = xwcsndup(&BUF[INDEX + 1], namelen);
		    pi->ctxt->srcindex = le_main_index - namelen;
		    return NULL;
		}
	    }
	    break;
    }

    wordunit_T *wu = xmalloc(sizeof *wu);
    wu->next = NULL;
    if (namelen == 0) {
	wu->wu_type = WT_STRING;
	wu->wu_string = xwcsdup(L"$");
    } else {
	wu->wu_type = WT_PARAM;
	wu->wu_param = xmalloc(sizeof *wu->wu_param);
	wu->wu_param->pe_type = PT_MINUS;
	wu->wu_param->pe_name = xwcsndup(&BUF[INDEX + 1], namelen);
	wu->wu_param->pe_start = wu->wu_param->pe_end =
	wu->wu_param->pe_match = wu->wu_param->pe_subst = NULL;
    }

    INDEX += namelen + 1;
    return wu;
}

/* Parses a parameter expansion enclosed by { }.
 * The `ctxttype' parameter is the context type of the word containing this
 * parameter expansion.
 * If the parser reached the end of the input string, the return value is NULL
 * and the result is saved in `pi->ctxt'. However, `pi->ctxt->pwords' is NULL
 * when the preceding words need to be determined by the caller. In this case,
 * the caller must update the `pwordc' and `pwords' member. */
wordunit_T *cparse_paramexp_in_brace(le_contexttype_T ctxttype)
{
    paramexp_T *pe = xmalloc(sizeof *pe);
    pe->pe_type = 0;
    pe->pe_name = NULL;
    pe->pe_start = pe->pe_end = pe->pe_match = pe->pe_subst = NULL;

    const size_t origindex = INDEX;
    assert(BUF[INDEX] == L'{');
    INDEX++;

    /* parse PT_NUMBER */
    if (BUF[INDEX] == L'#') {
	switch (BUF[INDEX + 1]) {
	    case L'}':  case L'+':  case L'=':
	    case L':':  case L'/':  case L'%':
		break;
	    case L'-':  case L'?':  case L'#':
		if (BUF[INDEX + 2] != L'}')
		    break;
		/* falls thru! */
	    case L'\0':  default:
		pe->pe_type |= PT_NUMBER;
		INDEX++;
		break;
	}
    }

    /* parse nested expansion */
    if (BUF[INDEX] == L'{') {
	pe->pe_type |= PT_NEST;
	pe->pe_nest = cparse_paramexp_in_brace(ctxttype & CTXT_MASK);
	if (pe->pe_nest == NULL)
	    goto return_null;
    } else if (BUF[INDEX] == L'`'
		|| (BUF[INDEX] == L'$'
		    && (BUF[INDEX + 1] == L'{' || BUF[INDEX + 1] == L'('))) {
	pe->pe_type |= PT_NEST;
	pe->pe_nest = cparse_special_word_unit(ctxttype & CTXT_MASK);
	if (pe->pe_nest == NULL)
	    goto return_null;
    } else {
	/* no nesting: parse parameter name */
	size_t namelen;
	switch (BUF[INDEX]) {
	    case L'@':  case L'*':  case L'#':  case L'?':
	    case L'-':  case L'$':  case L'!':
		namelen = 1;
		break;
	    default:
		namelen = 0;
		while (is_name_char(BUF[INDEX + namelen]))
		    namelen++;
		break;
	}
	if (BUF[INDEX + namelen] == L'\0') {
	    pi->ctxt->quote = QUOTE_NORMAL;
	    pi->ctxt->type = CTXT_VAR | CTXT_VBRACED | (ctxttype & CTXT_QUOTED);
	    pi->ctxt->pwordc = 0;
	    pi->ctxt->pwords = xmalloc(1 * sizeof *pi->ctxt->pwords);
	    pi->ctxt->pwords[0] = NULL;
	    pi->ctxt->pattern = xwcsndup(&BUF[INDEX], namelen);
	    pi->ctxt->srcindex = le_main_index - namelen;
	    goto return_null;
	}
	pe->pe_name = xwcsndup(&BUF[INDEX], namelen);
	INDEX += namelen;
    }

    /* parse index */
    if (BUF[INDEX] == L'[') {
	INDEX++;
	wordunit_T *w = cparse_word(is_closing_bracket, TT_NONE, CTXT_ARITH);
	if (w == NULL)
	    goto return_null;
	wordfree(w);
	/* don't assign the result to `pe->pe_start/end' since it may cause an
	 * arithmetic expansion error. */
	if (BUF[INDEX] == L']')
	    INDEX++;
    }

    /* parse PT_COLON */
    if (BUF[INDEX] == L':') {
	pe->pe_type |= PT_COLON;
	INDEX++;
    }

    /* parse '-', '+', '#', etc. */
    switch (BUF[INDEX]) {
	case L'+':
	    pe->pe_type |= PT_PLUS;
	    goto parse_subst;
	case L'-':  case L'=':  case L'?':
	    pe->pe_type |= PT_MINUS;
	    goto parse_subst;
	case L'#':
	    pe->pe_type |= PT_MATCH | PT_MATCHHEAD;
	    goto parse_match;
	case L'%':
	    pe->pe_type |= PT_MATCH | PT_MATCHTAIL;
	    goto parse_match;
	case L'/':
	    pe->pe_type |= PT_SUBST | PT_MATCHLONGEST;
	    goto parse_match;
	case L'}':
	    pe->pe_type |= PT_NONE;
	    goto check_closing_paren;
	default:
	    goto syntax_error;
    }

parse_match:
    if (pe->pe_type & PT_COLON) {
        if ((pe->pe_type & PT_MASK) == PT_SUBST)
            pe->pe_type |= PT_MATCHHEAD | PT_MATCHTAIL;
        else
	    goto syntax_error;
	INDEX++;
    } else if (BUF[INDEX] == BUF[INDEX + 1]) {
	if ((pe->pe_type & PT_MASK) == PT_MATCH)
	    pe->pe_type |= PT_MATCHLONGEST;
	else
	    pe->pe_type |= PT_SUBSTALL;
	INDEX += 2;
    } else if (BUF[INDEX] == L'/') {
	switch (BUF[INDEX + 1]) {
	    case L'#':
		pe->pe_type |= PT_MATCHHEAD;
		INDEX += 2;
		break;
	    case L'%':
		pe->pe_type |= PT_MATCHTAIL;
		INDEX += 2;
		break;
	    default:
		INDEX += 1;
		break;
	}
    } else {
	INDEX++;
    }
    if ((pe->pe_type & PT_MASK) == PT_MATCH) {
	pe->pe_match = cparse_word(
		is_closing_brace, TT_NONE, ctxttype | CTXT_VBRACED);
	if (pe->pe_match == NULL)
	    goto return_null;
	goto check_closing_paren;
    } else {
	pe->pe_match = cparse_word(
		is_slash_or_closing_brace, TT_NONE, ctxttype | CTXT_VBRACED);
	if (pe->pe_match == NULL)
	    goto return_null;
	if (BUF[INDEX] != L'/')
	    goto check_closing_paren;
    }

parse_subst:
    INDEX++;
    pe->pe_subst = cparse_word(
	    is_closing_brace, TT_NONE, ctxttype | CTXT_VBRACED);
    if (pe->pe_subst == NULL)
	goto return_null;

check_closing_paren:
    if (BUF[INDEX] == L'}')
	INDEX++;

    switch (pe->pe_type & PT_MASK) {
	case PT_MINUS:
	case PT_PLUS:
	    break;
	case PT_ASSIGN:
	case PT_ERROR:
	    assert(false);
	default:
	    if (pe->pe_type & PT_NEST)
		break;

	    /* make the variable expansion nested with the PT_MINUS flag
	     * to avoid a possible "nounset" error. */
	    paramexp_T *pe2 = xmalloc(sizeof *pe2);
	    pe2->pe_type = PT_MINUS;
	    pe2->pe_name = pe->pe_name;
	    pe2->pe_start = pe2->pe_end = pe2->pe_match = pe2->pe_subst = NULL;

	    wordunit_T *nest = xmalloc(sizeof *nest);
	    nest->next = NULL;
	    nest->wu_type = WT_PARAM;
	    nest->wu_param = pe2;
	    pe->pe_type |= PT_NEST;
	    pe->pe_nest = nest;
	    break;
    }

    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_PARAM;
    result->wu_param = pe;
    return result;

syntax_error:
    paramfree(pe);
    result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_STRING;
    result->wu_string = escapefree(
	    xwcsndup(&BUF[origindex], INDEX - origindex), NULL);
    return result;

return_null:
    paramfree(pe);
    return NULL;
}

/* Parses an arithmetic expansion.
 * If the parser reached the end of the input string, the return value is NULL
 * and the result is saved in `pi->ctxt'. */
wordunit_T *cparse_arith(void)
{
    size_t startindex = INDEX;

    assert(BUF[INDEX    ] == L'$');
    assert(BUF[INDEX + 1] == L'(');

    unsigned nestparen = 0;
    INDEX += 2;
    for (;;) {
	if (BUF[INDEX] != L'\0' && is_arith_delimiter(BUF[INDEX])) {
	    switch (BUF[INDEX++]) {
		case L'(':
		    nestparen++;
		    break;
		case L')':
		    if (nestparen == 0)
			goto return_content;
		    nestparen--;
		    break;
	    }
	} else {
	    wordunit_T *w =
		cparse_word(is_arith_delimiter, TT_NONE, CTXT_ARITH);
	    if (w == NULL) {
		empty_pwords();
		return NULL;
	    }
	    wordfree(w);
	}
    }

return_content:;
    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_STRING;
    result->wu_string = xwcsndup(&BUF[startindex], INDEX - startindex);
    return result;
}

/* Parses a command substitution enclosed by "$( )".
 * If the parser reached the end of the input string, the return value is NULL
 * and the result is saved in `pi->ctxt'. */
wordunit_T *cparse_cmdsubst_in_paren(void)
{
    size_t startindex = INDEX;

    assert(BUF[INDEX    ] == L'$');
    assert(BUF[INDEX + 1] == L'(');

    INDEX += 2;
    if (cparse_commands()) {
	return NULL;
    } else {
	assert(BUF[INDEX] == L')');
	INDEX++;

	wordunit_T *result = xmalloc(sizeof *result);
	result->next = NULL;
	result->wu_type = WT_STRING;
	result->wu_string = xwcsndup(&BUF[startindex], INDEX - startindex);
	return result;
    }
}

/* Parses a command substitution enclosed by backquotes.
 * If the parser reached the end of the input string, the return value is NULL
 * and the result is saved in `pi->ctxt'. */
wordunit_T *cparse_cmdsubst_in_backquote(void)
{
    size_t startindex = INDEX;
    size_t endindex;

    assert(BUF[INDEX] == L'`');
    INDEX++;
    endindex = INDEX;
    for (;;) {
	switch (BUF[endindex++]) {
	    case L'\0':
		goto parse_inside;
	    case L'`':
		goto return_content;
	    case L'\\':
		if (BUF[endindex] != L'\0')
		    endindex++;
		break;
	}
    }

parse_inside:
    while (!cparse_commands())
	INDEX++;
    return NULL;

return_content:
    INDEX = endindex;
    wordunit_T *result = xmalloc(sizeof *result);
    result->next = NULL;
    result->wu_type = WT_STRING;
    result->wu_string =
	escapefree(xwcsndup(&BUF[startindex], endindex - startindex), NULL);
    return result;
}

bool is_slash_or_closing_brace(wchar_t c)
{
    return c == L'/' || c == L'}' || c == L'\0';
}

bool is_closing_brace(wchar_t c)
{
    return c == L'}' || c == L'\0';
}
bool is_closing_bracket(wchar_t c)
{
    return c == L']' || c == L'\0';
}

bool is_arith_delimiter(wchar_t c)
{
    switch (c) {
	case L'\0':
	    return true;
	case L'$':  case L'`':  case L'"':  case L'\'':  case L'\\':  case L'_':
	    return false;
	default:
	    return !iswalnum(c);
    }
}

/* Remove brace expansion in the specified string and returns the last result of
 * expansion.
 * For example, remove_braceexpand("a{1,2{b,c}2{d,e") = "a2c2e".
 * The string is modified in place.
 * Returns true iff expansion is unclosed. */
bool remove_braceexpand(wchar_t *s)
{
    bool unclosed = false;
    xwcsbuf_T buf;

    wb_initwithmax(&buf, wcslen(s));
    wb_cat(&buf, s);

    for (size_t i = 0; i < buf.length; ) {
	switch (buf.contents[i]) {
	    case L'{':
		unclosed = remove_braceexpand_inner(&buf, &i);
		break;
	    case L'\\':
		i += 2;
		break;
	    default:
		i++;
		break;
	}
    }

    assert(buf.length <= wcslen(s));
    wmemcpy(s, buf.contents, buf.length + 1);
    wb_destroy(&buf);

    return unclosed;
}

bool remove_braceexpand_inner(xwcsbuf_T *buf, size_t *index)
{
    bool foundcomma = false;
    size_t i = *index;
    size_t leftbraceindex = i;
    assert(buf->contents[leftbraceindex] == L'{');

    i++;
    while (i < buf->length) {
	switch (buf->contents[i]) {
	case L'{':
	    remove_braceexpand_inner(buf, &i);
	    break;
	case L',':
	    foundcomma = true;
	    wb_remove(buf, leftbraceindex, i - leftbraceindex + 1);
	    i = leftbraceindex;
	    break;
	case L'}':
	    if (foundcomma)  /* remove right brace */
		wb_remove(buf, i, 1);
	    else
		i++;
	    *index = i;
	    return false;
	case L'\\':
	    i += 2;
	    break;
	default:
	    i++;
	    break;
	}
    }

    if (!foundcomma)  /* remove left brace */
	wb_remove(buf, leftbraceindex, 1);
    *index = buf->length;
    return true;
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
