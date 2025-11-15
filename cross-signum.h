/* Yash: yet another shell */
/* cross-signum.h: signum.h for cross-compiling */
/* (C) 2025 magicant */

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


#ifndef CROSS_SIGNUM_H
#define CROSS_SIGNUM_H

#include <limits.h>
#include <stddef.h>
#include <signal.h>

/* an injective function that returns an array index
 * corresponding to the given signal number,
 * which must be a valid non-realtime signal number
 * or zero. */
__attribute__((const))
static size_t sigindex(int signum) {
    // Not using the switch statement to avoid compiler errors that may happen
    // if some signal names share the same numeric values.
    if (signum == 0)
        return 0;
    if (signum == SIGHUP)
        return 1;
    if (signum == SIGINT)
        return 2;
    if (signum == SIGQUIT)
        return 3;
    if (signum == SIGILL)
        return 4;
#ifdef SIGTRAP
    if (signum == SIGTRAP)
        return 5;
#endif
    if (signum == SIGABRT)
        return 6;
    if (signum == SIGBUS)
        return 7;
    if (signum == SIGFPE)
        return 8;
    if (signum == SIGKILL)
        return 9;
    if (signum == SIGUSR1)
        return 10;
    if (signum == SIGSEGV)
        return 11;
    if (signum == SIGUSR2)
        return 12;
    if (signum == SIGPIPE)
        return 13;
    if (signum == SIGALRM)
        return 14;
    if (signum == SIGTERM)
        return 15;
#ifdef SIGSTKFLT
    if (signum == SIGSTKFLT)
        return 16;
#endif
    if (signum == SIGCHLD)
        return 17;
    if (signum == SIGCONT)
        return 18;
    if (signum == SIGSTOP)
        return 19;
    if (signum == SIGTSTP)
        return 20;
    if (signum == SIGTTIN)
        return 21;
    if (signum == SIGTTOU)
        return 22;
    if (signum == SIGURG)
        return 23;
#ifdef SIGXCPU
    if (signum == SIGXCPU)
        return 24;
#endif
#ifdef SIGXFSZ
    if (signum == SIGXFSZ)
        return 25;
#endif
#ifdef SIGVTALRM
    if (signum == SIGVTALRM)
        return 26;
#endif
#ifdef SIGPROF
    if (signum == SIGPROF)
        return 27;
#endif
#ifdef SIGWINCH
    if (signum == SIGWINCH)
        return 28;
#endif
#ifdef SIGIO
    if (signum == SIGIO)
        return 29;
#endif
#ifdef SIGPWR
    if (signum == SIGPWR)
        return 30;
#endif
#ifdef SIGSYS
    if (signum == SIGSYS)
        return 31;
#endif
#ifdef SIGPOLL
    if (signum == SIGPOLL)
        return 32;
#endif
#ifdef SIGIOT
    if (signum == SIGIOT)
        return 33;
#endif
#ifdef SIGEMT
    if (signum == SIGEMT)
        return 34;
#endif
#ifdef SIGCLD
    if (signum == SIGCLD)
        return 35;
#endif
#ifdef SIGLOST
    if (signum == SIGLOST)
        return 36;
#endif
#ifdef SIGWINDOW
    if (signum == SIGWINDOW)
        return 37;
#endif
#ifdef SIGINFO
    if (signum == SIGINFO)
        return 38;
#endif
#ifdef SIGTHR
    if (signum == SIGTHR)
        return 39;
#endif
#ifdef SIGMSG
    if (signum == SIGMSG)
        return 40;
#endif
#ifdef SIGDANGER
    if (signum == SIGDANGER)
        return 41;
#endif
#ifdef SIGMIGRATE
    if (signum == SIGMIGRATE)
        return 42;
#endif
#ifdef SIGPRE
    if (signum == SIGPRE)
        return 43;
#endif
#ifdef SIGVIRT
    if (signum == SIGVIRT)
        return 44;
#endif
#ifdef SIGALRM1
    if (signum == SIGALRM1)
        return 45;
#endif
#ifdef SIGWAITING
    if (signum == SIGWAITING)
        return 46;
#endif
#ifdef SIGKAP
    if (signum == SIGKAP)
        return 47;
#endif
#ifdef SIGGRANT
    if (signum == SIGGRANT)
        return 48;
#endif
#ifdef SIGRETRACT
    if (signum == SIGRETRACT)
        return 49;
#endif
#ifdef SIGSOUND
    if (signum == SIGSOUND)
        return 50;
#endif
#ifdef SIGSAK
    if (signum == SIGSAK)
        return 51;
#endif
#ifdef SIGLWP
    if (signum == SIGLWP)
        return 52;
#endif
#ifdef SIGFREEZE
    if (signum == SIGFREEZE)
        return 53;
#endif
#ifdef SIGTHAW
    if (signum == SIGTHAW)
        return 54;
#endif
#ifdef SIGCANCEL
    if (signum == SIGCANCEL)
        return 55;
#endif
#ifdef SIGXRES
    if (signum == SIGXRES)
        return 56;
#endif
#ifdef SIGRESERVE
    if (signum == SIGRESERVE)
        return 57;
#endif
#ifdef SIGDIL
    if (signum == SIGDIL)
        return 58;
#endif
#ifdef SIGUNUSED
    if (signum == SIGUNUSED)
        return 59;
#endif

    /* assert(false); */
    return 0;
}

/* max index returned by sigindex + 1 */
#define MAXSIGIDX 60

/* number of realtime signals that can be handled by yash */
#if defined(NSIG_MAX) && NSIG_MAX > 0
# define RTSIZE NSIG_MAX
#else
# define RTSIZE 100
#endif

#endif /* CROSS_SIGNUM_H */

/* vim: set ts=8 sts=4 sw=4 et tw=80: */
