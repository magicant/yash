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


/* signal number and name */
typedef struct signal_T {
    int no;
    const char *name;
} signal_T;

/* list of signals */
static const signal_T signals[] = {

    /* signals defined by POSIX.1-1990 */
    { SIGHUP,  "HUP",  }, { SIGINT,  "INT",  }, { SIGQUIT, "QUIT", },
    { SIGILL,  "ILL",  }, { SIGABRT, "ABRT", }, { SIGFPE,  "FPE",  },
    { SIGKILL, "KILL", }, { SIGSEGV, "SEGV", }, { SIGPIPE, "PIPE", },
    { SIGALRM, "ALRM", }, { SIGTERM, "TERM", }, { SIGUSR1, "USR1", },
    { SIGUSR2, "USR2", }, { SIGCHLD, "CHLD", }, { SIGCONT, "CONT", },
    { SIGSTOP, "STOP", }, { SIGTSTP, "TSTP", }, { SIGTTIN, "TTIN", },
    { SIGTTOU, "TTOU", },

    /* signals defined by SUSv2 & POSIX.1-2001 (SUSv3) */
    { SIGTRAP, "TRAP", }, { SIGBUS,  "BUS",  }, { SIGSYS,  "SYS",  },
    { SIGURG,  "URG",  }, { SIGXCPU, "XCPU", }, { SIGXFSZ, "XFSZ", },
#ifdef SIGVTALRM
    { SIGVTALRM, "VTALRM", },
#endif
    { SIGPROF, "PROF", },
#ifdef SIGPOLL
    { SIGPOLL, "POLL", },
#endif

    /* below are non-standardized signals */
#ifdef SIGIOT
    { SIGIOT, "IOT", },
#endif
#ifdef SIGEMT
    { SIGEMT, "EMT", },
#endif
#ifdef SIGSTKFLT
    { SIGSTKFLT, "STKFLT", },
#endif
#ifdef SIGIO
    { SIGIO, "IO", },
#endif
#ifdef SIGCLD
    { SIGCLD, "CLD", },
#endif
#ifdef SIGPWR
    { SIGPWR, "PWR", },
#endif
#ifdef SIGLOST
    { SIGLOST, "LOST", },
#endif
#ifdef SIGWINCH
    { SIGWINCH, "WINCH", },
#endif
#ifdef SIGWINDOW
    { SIGWINDOW, "WINDOW", },
#endif

    /* from BSD */
#ifdef SIGINFO
    { SIGINFO, "INFO", },
#endif
#ifdef SIGTHR
    { SIGTHR, "THR", },
#endif

    /* from AIX */
#ifdef SIGMSG
    { SIGMSG, "MSG", },
#endif
#ifdef SIGDANGER
    { SIGDANGER, "DANGER", },
#endif
#ifdef SIGMIGRATE
    { SIGMIGRATE, "MIGRATE", },
#endif
#ifdef SIGPRE
    { SIGPRE, "PRE", },
#endif
#ifdef SIGVIRT
    { SIGVIRT, "VIRT", },
#endif
#ifdef SIGALRM1
    { SIGALRM1, "ALRM1", },
#endif
#ifdef SIGWAITING
    { SIGWAITING, "WAITING", },
#endif
#ifdef SIGKAP
    { SIGKAP, "KAP", },
#endif
#ifdef SIGGRANT
    { SIGGRANT, "GRANT", },
#endif
#ifdef SIGRETRACT
    { SIGRETRACT, "RETRACT", },
#endif
#ifdef SIGSOUND
    { SIGSOUND, "SOUND", },
#endif
#ifdef SIGSAK
    { SIGSAK, "SAK", },
#endif

    /* from SunOS5 */
#ifdef SIGLWP
    { SIGLWP, "LWP", },
#endif
#ifdef SIGFREEZE
    { SIGFREEZE, "FREEZE", },
#endif
#ifdef SIGTHAW
    { SIGTHAW, "THAW", },
#endif
#ifdef SIGCANCEL
    { SIGCANCEL, "CANCEL", },
#endif
#ifdef SIGXRES
    { SIGXRES, "XRES", },
#endif

    /* from HP-UX */
#ifdef SIGRESERVE
    { SIGRESERVE, "RESERVE", },
#endif
#ifdef SIGDIL
    { SIGDIL, "DIL", },
#endif

#ifdef SIGUNUSED
    { SIGUNUSED, "UNUSED", },
#endif

    /* end of array: any signal number is non-zero (C99 7.14) */
    { 0, NULL, },

};


#endif  /* YASH_SIGLIST_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
