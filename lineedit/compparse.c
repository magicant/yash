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
#include "../parser.h"
#include "../strbuf.h"
#include "../util.h"
#include "../xfnmatch.h"
#include "complete.h"
#include "compparse.h"
#include "editing.h"


/* This structure contains data used during parsing */
typedef struct cparseinfo_T {
    xwcsbuf_T buf;
    size_t bufindex;
    size_t mainindex;
    le_context_T *ctxt;
} cparseinfo_T;
/* The `buf' buffer contains the first `le_main_index' characters of the edit
 * buffer. During parsing, alias substitution may be performed on this buffer.
 * The `bufindex' and `mainindex' indices indicate the point the parser is
 * currently parsing. `bufindex' is an index into the `buf' buffer and
 * `mainindex' into the edit buffer (`le_main_buffer').
 * The `ctxt' member points to the structure in which the final result is saved.
 */

/* This structure contains data used during parsing */
static cparseinfo_T *pi;

#define BUF       (pi->buf.contents)
#define LEN       (pi->buf.length)
#define INDEX     (pi->bufindex)
#define MAINBUF   ((const wchar_t *) le_main_buffer.contents)
#define MAINLEN   (le_main_buffer.length)
#define MAININDEX (pi->mainindex)


static bool cparse_commands(void);
static void skip_blanks(void);
static bool cparse_command(void);
static bool token_at_current(const wchar_t *token)
    __attribute__((nonnull,pure));
static bool cparse_simple_command(void);
static bool cparse_redirections(void);
static bool ctryparse_redirect(void);
static bool cparse_for_command(void);
static bool cparse_case_command(void);


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
    parseinfo.bufindex = parseinfo.mainindex = 0;
    parseinfo.ctxt = ctxt;
    wb_init(&parseinfo.buf);
    wb_ncat_force(&parseinfo.buf, le_main_buffer.contents, le_main_index);

    pi = &parseinfo;
    while (!cparse_commands())
	parseinfo.bufindex++, parseinfo.mainindex++;

    wb_destroy(&parseinfo.buf);

    if (ctxt->type == CTXT_NORMAL
	    && is_pathname_matching_pattern(ctxt->pattern)) {
	ctxt->substsrc = true;
    } else {
	xwcsbuf_T buf;
	ctxt->substsrc = false;
	ctxt->pattern =
	    wb_towcs(wb_wccat(wb_initwith(&buf, ctxt->pattern), L'*'));
    }
    ctxt->cpattern = xfnm_compile(
	    ctxt->pattern, XFNM_HEADONLY | XFNM_TAILONLY);
    if (ctxt->cpattern == NULL) {
	le_compdebug("failed to compile pattern \"%ls\"", ctxt->pattern);
	return false;
    }
    return true;
}

/* Following parser functions return true iff parsing is finished and the result
 * is saved in `pi->ctxt'.
 * The result is saved in the following variables of the context structure:
 *     quote, type, pwordc, pwords, src, pattern, srcindex. */

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
		INDEX++, MAININDEX++;
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
    pi->ctxt->src = xwcsdup(L"");
    pi->ctxt->pattern = xwcsdup(L"");
    pi->ctxt->srcindex = MAININDEX;
    return true;
}

/* Skips blank characters. */
void skip_blanks(void)
{
    while (iswblank(BUF[INDEX]))
	INDEX++, MAININDEX++;
}

/* Parses a command from the current position. */
bool cparse_command(void)
{
    if (BUF[INDEX] == L'(') {
	INDEX++, MAININDEX++;
	if (cparse_commands())
	    return true;
	if (BUF[INDEX] == L')')
	    INDEX++, MAININDEX++;
	return cparse_redirections();
    } else if (token_at_current(L"{") || token_at_current(L"!")) {
	INDEX++, MAININDEX++;
	return false;
    } else if (token_at_current(L"if") || token_at_current(L"do")) {
	INDEX += 2, MAININDEX += 2;
	return false;
    } else if (token_at_current(L"then") || token_at_current(L"else")
	    || token_at_current(L"elif")) {
	INDEX += 4, MAININDEX += 4;
	return false;
    } else if (token_at_current(L"while") || token_at_current(L"until")) {
	INDEX += 5, MAININDEX += 5;
	return false;
    } else if (token_at_current(L"}")) {
	INDEX++, MAININDEX++;
	return cparse_redirections();
    } else if (token_at_current(L"fi")) {
	INDEX += 2, MAININDEX += 2;
	return cparse_redirections();
    } else if (token_at_current(L"done") || token_at_current(L"esac")) {
	INDEX += 4, MAININDEX += 4;
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
    return false; //TODO
}

/* Parses redirections until the next command. */
bool cparse_redirections(void)
{
    size_t saveindex;
    do {
	skip_blanks();
	saveindex = INDEX;
	if (ctryparse_redirect())
	    return true;
    } while (saveindex != INDEX);

    return false; // TODO global alias
}

/* Parses a redirection if any.
 * `skip_blanks' should be called before this function is called. */
bool ctryparse_redirect(void)
{
    return false; //TODO
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

/* Frees the contents of the specified `le_context_T' data. */
void le_free_context(le_context_T *ctxt)
{
    recfree(ctxt->pwords, free);
    free(ctxt->src);
    free(ctxt->pattern);
    xfnm_free(ctxt->cpattern);
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
