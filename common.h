/* Yash: yet another shell */
/* common.h: defines symbols common to all sources. */
/* © 2007-2008 magicant */

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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_LIBREADLINE
# define USE_READLINE
#endif

/* _POSIX_C_SOURCE 機能検査マクロを定義してから各種ヘッダファイルを
 * インクルードすることで POSIX で定められた定数や関数が宣言されるが、
 * POSIX にない拡張機能は宣言されない (はずである)。
 * 以下に、configure で利用可能と判断したものについて手動で宣言を行う */

#ifdef HAVE_STRNLEN
#include <stddef.h>
extern size_t strnlen(const char *, size_t);
#endif

#ifdef HAVE_STRSIGNAL
extern char *strsignal(int);
#endif

#endif /* COMMON_H */
