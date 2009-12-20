/* Yash: yet another shell */
/* siglist.h: defines list of signals */
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


#ifndef YASH_SIGLIST_H
#define YASH_SIGLIST_H

#include <signal.h>
#include <stddef.h>


/* signal number and name */
typedef struct signal_T {
    int no;
    const wchar_t *name;
} signal_T;

/* list of signals */
static const signal_T signals[] = {

    /* signals defined by SUSv2 & POSIX.1-2001 (SUSv3) */
    { SIGHUP,  L"HUP",  }, { SIGINT,  L"INT",  }, { SIGQUIT, L"QUIT", },
    { SIGILL,  L"ILL",  }, { SIGABRT, L"ABRT", }, { SIGBUS,  L"BUS",  },
    { SIGFPE,  L"FPE",  }, { SIGKILL, L"KILL", }, { SIGSEGV, L"SEGV", },
    { SIGPIPE, L"PIPE", }, { SIGALRM, L"ALRM", }, { SIGTERM, L"TERM", },
    { SIGUSR1, L"USR1", }, { SIGUSR2, L"USR2", }, { SIGCHLD, L"CHLD", },
    { SIGCONT, L"CONT", }, { SIGSTOP, L"STOP", }, { SIGTSTP, L"TSTP", },
    { SIGTTIN, L"TTIN", }, { SIGTTOU, L"TTOU", }, { SIGURG,  L"URG",  },
#ifdef SIGTRAP
    { SIGTRAP, L"TRAP", },
#endif
#ifdef SIGXCPU
    { SIGXCPU, L"XCPU", },
#endif
#ifdef SIGXFSZ
    { SIGXFSZ, L"XFSZ", },
#endif
#ifdef SIGVTALRM
    { SIGVTALRM, L"VTALRM", },
#endif
#ifdef SIGPROF
    { SIGPROF, L"PROF", },
#endif
#ifdef SIGPOLL
    { SIGPOLL, L"POLL", },
#endif
#ifdef SIGSYS
    { SIGSYS, L"SYS", },
#endif

    /* below are non-standardized signals */
#ifdef SIGIOT
    { SIGIOT, L"IOT", },
#endif
#ifdef SIGEMT
    { SIGEMT, L"EMT", },
#endif
#ifdef SIGSTKFLT
    { SIGSTKFLT, L"STKFLT", },
#endif
#ifdef SIGIO
    { SIGIO, L"IO", },
#endif
#ifdef SIGCLD
    { SIGCLD, L"CLD", },
#endif
#ifdef SIGPWR
    { SIGPWR, L"PWR", },
#endif
#ifdef SIGLOST
    { SIGLOST, L"LOST", },
#endif
#ifdef SIGWINCH
    { SIGWINCH, L"WINCH", },
#endif
#ifdef SIGWINDOW
    { SIGWINDOW, L"WINDOW", },
#endif

    /* from BSD */
#ifdef SIGINFO
    { SIGINFO, L"INFO", },
#endif
#ifdef SIGTHR
    { SIGTHR, L"THR", },
#endif

    /* from AIX */
#ifdef SIGMSG
    { SIGMSG, L"MSG", },
#endif
#ifdef SIGDANGER
    { SIGDANGER, L"DANGER", },
#endif
#ifdef SIGMIGRATE
    { SIGMIGRATE, L"MIGRATE", },
#endif
#ifdef SIGPRE
    { SIGPRE, L"PRE", },
#endif
#ifdef SIGVIRT
    { SIGVIRT, L"VIRT", },
#endif
#ifdef SIGALRM1
    { SIGALRM1, L"ALRM1", },
#endif
#ifdef SIGWAITING
    { SIGWAITING, L"WAITING", },
#endif
#ifdef SIGKAP
    { SIGKAP, L"KAP", },
#endif
#ifdef SIGGRANT
    { SIGGRANT, L"GRANT", },
#endif
#ifdef SIGRETRACT
    { SIGRETRACT, L"RETRACT", },
#endif
#ifdef SIGSOUND
    { SIGSOUND, L"SOUND", },
#endif
#ifdef SIGSAK
    { SIGSAK, L"SAK", },
#endif

    /* from SunOS5 */
#ifdef SIGLWP
    { SIGLWP, L"LWP", },
#endif
#ifdef SIGFREEZE
    { SIGFREEZE, L"FREEZE", },
#endif
#ifdef SIGTHAW
    { SIGTHAW, L"THAW", },
#endif
#ifdef SIGCANCEL
    { SIGCANCEL, L"CANCEL", },
#endif
#ifdef SIGXRES
    { SIGXRES, L"XRES", },
#endif

    /* from HP-UX */
#ifdef SIGRESERVE
    { SIGRESERVE, L"RESERVE", },
#endif
#ifdef SIGDIL
    { SIGDIL, L"DIL", },
#endif

#ifdef SIGUNUSED
    { SIGUNUSED, L"UNUSED", },
#endif

    /* end of array: any signal number is non-zero (C99 7.14) */
    { 0, NULL, },

};


#endif  /* YASH_SIGLIST_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
