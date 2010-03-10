/* Yash: yet another shell */
/* compparse.h: simple parser for command line completion */
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


#ifndef YASH_COMPPARSE_H
#define YASH_COMPPARSE_H


struct le_context_T;
extern _Bool le_get_context(struct le_context_T *ctxt)
    __attribute__((nonnull));


#endif /* YASH_COMPPARSE_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
