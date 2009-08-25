/* Yash: yet another shell */
/* input.h: functions for input of command line */
/* (C) 2007-2009 magicant */

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


#ifndef YASH_INPUT_H
#define YASH_INPUT_H

#include <stdio.h>
#include <wchar.h>


extern void print_prompt(int type);
extern _Bool set_nonblocking(int fd);
extern _Bool unset_nonblocking(int fd);

struct xwcsbuf_T;

extern _Bool read_line_from_stdin(struct xwcsbuf_T *buf, _Bool trap)
    __attribute__((nonnull));

/* input functions to be used as `inputfunc_T' */
extern int input_mbs(struct xwcsbuf_T *buf, void *inputinfo)
    __attribute__((nonnull));
extern int input_wcs(struct xwcsbuf_T *buf, void *inputinfo)
    __attribute__((nonnull));
extern int input_file(struct xwcsbuf_T *buf, void *inputinfo)
    __attribute__((nonnull));
extern int input_stdin(struct xwcsbuf_T *buf, void *inputinfo)
    __attribute__((nonnull(1)));
extern int input_readline(struct xwcsbuf_T *buf, void *inputinfo)
    __attribute__((nonnull));

/* to be used as `inputinfo' for `input_mbs' */
struct input_mbs_info {
    const char *src;   /* the source code input */
    size_t srclen;     /* # of bytes in `src' including trailing '\0' */
    mbstate_t state;   /* shift state for multibyte-wide conversion */
};

/* to be used as `inputinfo' for `input_wcs' */
struct input_wcs_info {
    const wchar_t *src;  /* the source code input */
};

/* to be used as `inputinfo' for `input_readline' */
struct input_readline_info {
    FILE *fp;   /* input stream */
    int type;   /* type of prompt */
};


#endif /* YASH_INPUT_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
