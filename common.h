/* Yash: yet another shell */
/* common.h: defines symbols common to all sources. */
/* (C) 2007-2008 magicant */

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


#ifndef COMMON_H
#define COMMON_H

#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE   600

#ifndef __GNUC__
# define __attribute__(ignore)
#endif

#if HAVE_CONFIG_H
# include "config.h"
#endif

#if HAVE_GETTEXT
# define gt(MSGID)  gettext(MSGID)
# define Ngt(MSGID) MSGID
# if HAVE_NGETTEXT
#  define ngt(MSGIDS,MSGIDP,N) ngettext(MSGIDS, MSGIDP, N)
# else
#  define ngt(MSGIDS,MSGIDP,N) gt(MSGIDP)
# endif
#else
# define gt(MSGID)            MSGID
# define Ngt(MSGID)           MSGID
# define ngt(MSGIDS,MSGIDP,N) MSGIDP
#endif

#endif /* COMMON_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
