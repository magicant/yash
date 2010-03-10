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
#include <stdbool.h>
#include "../strbuf.h"
#include "../util.h"
#include "../xfnmatch.h"
#include "complete.h"
#include "compparse.h"
#include "editing.h"


bool le_get_context(le_context_T *ctxt)
{
    *ctxt = (le_context_T) {
	.quote = QUOTE_NORMAL,
	.type = CTXT_NORMAL,
	.srcindex = 0,
    };  // TODO
    ctxt->pwordc = 0;  // TODO
    ctxt->pwords = xmalloc(1 * sizeof *ctxt->pwords);  // TODO
    ctxt->pwords[0] = NULL;  // TODO
    ctxt->pattern = xwcsdup(le_main_buffer.contents);  // TODO
    extern wchar_t *unescape(const wchar_t *);  // TODO
    ctxt->src = unescape(ctxt->pattern);
    if (is_pathname_matching_pattern(ctxt->pattern)) {
	ctxt->substsrc = true;
    } else {
	xwcsbuf_T buf;
	ctxt->substsrc = false;
	ctxt->pattern =
	    wb_towcs(wb_wccat(wb_initwith(&buf, ctxt->pattern), L'*'));
    }  // TODO
    ctxt->cpattern = xfnm_compile(
	    ctxt->pattern, XFNM_HEADONLY | XFNM_TAILONLY);
    if (ctxt->cpattern == NULL) {
	le_compdebug("failed to compile pattern \"%ls\"", ctxt->pattern);
	return false;
    }
    return true;
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
