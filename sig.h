/* Yash: yet another shell */
/* sig.h: signal handling */
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


#ifndef SIG_H
#define SIG_H

#include <stdbool.h>


extern void init_signal(void);
extern void set_signals(void);
extern void reset_signals(void);
extern void block_sigquit_and_sigint(void);
extern void block_sigtstp(void);
extern void block_sigttou(void);
extern void unblock_sigttou(void);
extern void block_all_but_sigpipe(void);

extern void block_sigchld_and_sighup(void);
extern void unblock_sigchld_and_sighup(void);
extern void wait_for_sigchld(void);

extern void handle_traps(void);
extern void clear_traps(void);


#endif /* SIG_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
