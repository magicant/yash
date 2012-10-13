/* Yash: yet another shell */
/* common.h: defines symbols common to all sources. */
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

/* This file should be included at the very first in every source file. */


#ifndef YASH_COMMON_H
#define YASH_COMMON_H

#include "config.h"

#ifndef __GNUC__
# define __attribute__(ignore)
#endif

#ifdef __CYGWIN__
# undef __STRICT_ANSI__
# define FD_SETSIZE 256
#endif

#if HAVE_GETTEXT
# define gt(MSGID)  ((const char *) gettext(MSGID))
# define Ngt(MSGID) MSGID
#else
# define gt(MSGID)  MSGID
# define Ngt(MSGID) MSGID
#endif

#if HAVE_NGETTEXT
# define ngt(MSGS,MSGP,N) ((const char *) ngettext(MSGS, MSGP, N))
#else
# define ngt(MSGS,MSGP,N) gt((N) == 1 ? (MSGS) : (MSGP))
#endif

#ifdef NDEBUG
# define INIT(x) x
#else
# define INIT(x) x = x
#endif

#define ARGV(i) ((wchar_t *) argv[i])

#endif /* YASH_COMMON_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
