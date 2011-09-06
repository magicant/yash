/* Yash: yet another shell */
/* compparse.h: simple parser for command line completion */
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


#ifndef YASH_COMPPARSE_H
#define YASH_COMPPARSE_H


struct le_context_T;
extern struct le_context_T *le_get_context(void)
    __attribute__((malloc,warn_unused_result));


#endif /* YASH_COMPPARSE_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
