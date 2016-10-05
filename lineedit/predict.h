/* Yash: yet another shell */
/* predict.h: command line input prediction */
/* (C) 2016 magicant */

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


#ifndef YASH_PREDICT_H
#define YASH_PREDICT_H

#include <stddef.h>

extern void le_record_entered_command(const wchar_t *cmdline)
    __attribute__((nonnull));


#endif /* YASH_PREDICT_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
