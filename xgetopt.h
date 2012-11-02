/* Yash: yet another shell */
/* xgetopt.h: command option parser */
/* (C) 2012 magicant */

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


#ifndef YASH_XGETOPT_H
#define YASH_XGETOPT_H

#include <stddef.h>


extern wchar_t *xoptarg;
extern int xoptind;

enum xgetoptopt_T {
    XGETOPT_POSIX = 1 << 0,
    XGETOPT_DIGIT = 1 << 1,
};
enum optarg_T {
    OPTARG_NONE, OPTARG_REQUIRED, OPTARG_OPTIONAL,
};
struct xgetopt_T {
    wchar_t shortopt;
    const wchar_t *longopt;
    enum optarg_T optarg;
    _Bool posix;
    void *ptr;
};

extern struct xgetopt_T *xgetopt(
	void **restrict argv_,
	const struct xgetopt_T *restrict opts_,
	enum xgetoptopt_T getoptopt_)
    __attribute__((nonnull));


#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
