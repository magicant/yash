/* Yash: yet another shell */
/* signal.h: signal management interface */
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


typedef struct {
	int s_signal;
	const char *s_name;
} SIGDATA;

extern const SIGDATA const sigdata[];

extern volatile sig_atomic_t sigint_received;

int get_signal(const char *name);
const char *get_signal_name(int signal);
const char *xstrsignal(int signal);
void init_signal(void);
void set_signals(void);
void unset_signals(void);
void wait_for_signal(void);
void handle_signals(void);


#endif /* SIG_H */
