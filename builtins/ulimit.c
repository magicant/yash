/* Yash: yet another shell */
/* ulimit.c: ulimit builtin */
/* (C) 2007-2010 magicant */

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


#include "../common.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <wchar.h>
#include <wctype.h>
#ifdef HAVE_GETTEXT
# include <libintl.h>
#endif
#include "../builtin.h"
#include "../exec.h"
#include "../util.h"
#include "ulimit.h"
/* Including <stdint.h> is required before including <sys/resource.h> on
 * FreeBSD, but <stdint.h> is automatically included in <inttypes.h>. */

#if !HAVE_RLIM_SAVED_MAX
# define RLIM_SAVED_MAX RLIM_INFINITY
#endif
#if !HAVE_RLIM_SAVED_CUR
# define RLIM_SAVED_CUR RLIM_INFINITY
#endif


static const struct resource {
    wchar_t option;
    int type;
    rlim_t factor;
    const char *description;
} resource_types[] = {
    { L'c', RLIMIT_CORE, 512, Ngt("core file size (blocks)"), },
    { L'd', RLIMIT_DATA, 1024, Ngt("data segment size (kbytes)"), },
#if HAVE_RLIMIT_NICE
    { L'e', RLIMIT_NICE, 1, Ngt("max nice"), },
#endif
    { L'f', RLIMIT_FSIZE, 512, Ngt("file size (blocks)"), },
#if HAVE_RLIMIT_SIGPENDING
    { L'i', RLIMIT_SIGPENDING, 1, Ngt("pending signals"), },
#endif
#if HAVE_RLIMIT_MEMLOCK
    { L'l', RLIMIT_MEMLOCK, 1024, Ngt("locked memory (kbytes)"), },
#endif
#if HAVE_RLIMIT_RSS
    { L'm', RLIMIT_RSS, 1024, Ngt("resident set size (kbytes)"), },
#endif
    { L'n', RLIMIT_NOFILE, 1, Ngt("open files"), },
#if HAVE_RLIMIT_MSGQUEUE
    { L'q', RLIMIT_MSGQUEUE, 1, Ngt("message queue size (bytes)"), },
#endif
#if HAVE_RLIMIT_RTPRIO
    { L'r', RLIMIT_RTPRIO, 1, Ngt("real-time priority"), },
#endif
    { L's', RLIMIT_STACK, 1024, Ngt("stack size (kbytes)"), },
    { L't', RLIMIT_CPU, 1, Ngt("CPU time (seconds)"), },
#if HAVE_RLIMIT_NPROC
    { L'u', RLIMIT_NPROC, 1, Ngt("user processes"), },
#endif
    { L'v', RLIMIT_AS, 1024, Ngt("memory (kbytes)"), },
#if HAVE_RLIMIT_LOCKS
    { L'x', RLIMIT_LOCKS, 1, Ngt("file locks"), },
#endif
    { 0, 0, 0, NULL, },
};

/* The "ulimit" builtin */
int ulimit_builtin(int argc, void **argv)
{
    const wchar_t *short_options = L"HSa"
	"c"
	"d"
#if HAVE_RLIMIT_NICE
	"e"
#endif
	"f"
#if HAVE_RLIMIT_SIGPENDING
	"i"
#endif
#if HAVE_RLIMIT_MEMLOCK
	"l"
#endif
#if HAVE_RLIMIT_RSS
	"m"
#endif
	"n"
#if HAVE_RLIMIT_MSGQUEUE
	"q"
#endif
#if HAVE_RLIMIT_RTPRIO
	"r"
#endif
	"s"
	"t"
#if HAVE_RLIMIT_NPROC
	"u"
#endif
	"v"
#if HAVE_RLIMIT_LOCKS
	"x"
#endif
	;

    static const struct xoption long_options[] = {
	{ L"hard", OPTARG_NONE, L'H', },
	{ L"soft", OPTARG_NONE, L'S', },
	{ L"all",  OPTARG_NONE, L'a', },
	{ L"core", OPTARG_NONE, L'c', },
	{ L"data", OPTARG_NONE, L'd', },
#if HAVE_RLIMIT_NICE
	{ L"nice", OPTARG_NONE, L'e', },
#endif
	{ L"fsize", OPTARG_NONE, L'f', },
#if HAVE_RLIMIT_SIGPENDING
	{ L"sigpending", OPTARG_NONE, L'i', },
#endif
#if HAVE_RLIMIT_MEMLOCK
	{ L"memlock", OPTARG_NONE, L'l', },
#endif
#if HAVE_RLIMIT_RSS
	{ L"rss", OPTARG_NONE, L'm', },
#endif
	{ L"nofile", OPTARG_NONE, L'n', },
#if HAVE_RLIMIT_MSGQUEUE
	{ L"msgqueue", OPTARG_NONE, L'q', },
#endif
#if HAVE_RLIMIT_RTPRIO
	{ L"rtprio", OPTARG_NONE, L'r', },
#endif
	{ L"stack", OPTARG_NONE, L's', },
	{ L"cpu", OPTARG_NONE, L't', },
#if HAVE_RLIMIT_NPROC
	{ L"nproc", OPTARG_NONE, L'u', },
#endif
	{ L"as", OPTARG_NONE, L'v', },
#if HAVE_RLIMIT_LOCKS
	{ L"locks", OPTARG_NONE, L'x', },
#endif
#if YASH_ENABLE_HELP
	{ L"help", OPTARG_NONE, L'-', },
#endif
	{ NULL, 0, 0, },
    };

    wchar_t opt;
    enum { hard = 1 << 0, soft = 1 << 1, } type = hard | soft;
    wchar_t rtype = L'f';
    bool print_all = false;

    xoptind = 0, xopterr = true;
    while ((opt = xgetopt_long(argv, short_options, long_options, NULL))) {
	switch (opt) {
	    case L'H':  type = hard;       break;
	    case L'S':  type = soft;       break;
	    case L'a':  print_all = true;  break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		for (const struct resource *r = resource_types; r->option; r++){
		    if (r->option == opt) {
			rtype = opt;
			goto opt_ok;
		    }
		}
		goto print_usage;
opt_ok:
		break;
	}
    }

    const struct resource *resource = NULL;
    struct rlimit rlimit;

    assert(type & (hard | soft));
    if (print_all) {
	clearerr(stdout);
	for (resource = resource_types; resource->option; resource++) {
	    if (getrlimit(resource->type, &rlimit) < 0) {
		xerror(errno, Ngt("cannot get the current limit"));
		return Exit_FAILURE;
	    }
	    rlim_t value = (type & soft) ? rlimit.rlim_cur : rlimit.rlim_max;
	    printf(gt("-%lc: %-30s "),
		    (wint_t) resource->option, gt(resource->description));
	    if (value == RLIM_INFINITY)
		puts(gt("unlimited"));
	    else
		printf("%ju\n", (uintmax_t) (value / resource->factor));
	}
	if (!ferror(stdout)) {
	    return Exit_SUCCESS;
	} else {
	    xerror(0, Ngt("cannot print to standard output"));
	    return Exit_FAILURE;
	}
    }

    if (xoptind + 1 < argc)
	goto print_usage;

    for (const struct resource *r = resource_types; r->option; r++) {
	if (r->option == rtype) {
	    resource = r;
	    break;
	}
    }
    assert(resource != NULL);
    if (getrlimit(resource->type, &rlimit) < 0) {
	xerror(errno, Ngt("cannot get the current limit"));
	return Exit_FAILURE;
    }
    if (xoptind == argc) {
	/* print value */
	rlim_t value = (type & soft) ? rlimit.rlim_cur : rlimit.rlim_max;
	if (value == RLIM_INFINITY)
	    puts(gt("unlimited"));
	else
	    printf("%ju\n", (uintmax_t) (value / resource->factor));
	if (!ferror(stdout)) {
	    return Exit_SUCCESS;
	} else {
	    xerror(errno, Ngt("cannot print to standard output"));
	    return Exit_FAILURE;
	}
    } else {
	/* set value */
	rlim_t value;
	if (wcscmp(ARGV(xoptind), L"hard") == 0) {
	    value = rlimit.rlim_max;
	} else if (wcscmp(ARGV(xoptind), L"soft") == 0) {
	    value = rlimit.rlim_cur;
	} else if (wcscmp(ARGV(xoptind), L"unlimited") == 0) {
	    value = RLIM_INFINITY;
	} else if (iswdigit(ARGV(xoptind)[0])) {
	    unsigned long v;

	    if (!xwcstoul(ARGV(xoptind), 10, &v))
		goto err_format;
	    value = (rlim_t) v * resource->factor;
	    if (value / resource->factor != v || value == RLIM_INFINITY
		    || value == RLIM_SAVED_MAX || value == RLIM_SAVED_CUR) {
		xerror(ERANGE, NULL);
		return Exit_FAILURE;
	    }
	} else {
	    goto err_format;
	}
	if (type & hard)
	    rlimit.rlim_max = value;
	if (type & soft)
	    rlimit.rlim_cur = value;
	
	/* check if soft limit exceeds hard limit */
	if (rlimit.rlim_max != RLIM_INFINITY
		&& rlimit.rlim_max != RLIM_SAVED_MAX
		&& rlimit.rlim_max != RLIM_SAVED_CUR
		&& (rlimit.rlim_cur == RLIM_INFINITY
		    || (rlimit.rlim_cur != RLIM_SAVED_MAX
			&& rlimit.rlim_cur != RLIM_SAVED_CUR
			&& rlimit.rlim_cur > rlimit.rlim_max))) {
	    xerror(0, Ngt("the soft limit cannot exceed the hard limit"));
	    return Exit_FAILURE;
	}

	if (setrlimit(resource->type, &rlimit) < 0) {
	    xerror(errno, Ngt("failed to set the limit"));
	    return Exit_FAILURE;
	} else {
	    return Exit_SUCCESS;
	}
    }

err_format:
    xerror(0, Ngt("`%ls' is not a valid integer"), ARGV(xoptind));
    return Exit_ERROR;
print_usage:
    fprintf(stderr, gt("Usage:  ulimit [-%ls] [limit]\n"), short_options);
    return Exit_ERROR;
}

#if YASH_ENABLE_HELP
const char ulimit_help[] = Ngt(
"ulimit - set or print resource limitation\n"
"\tulimit [-HSaefilnqrstuvx] [limit]\n"
"The ulimit built-in sets a resource limit to <limit>. The resource type can\n"
"be specified by the options listed below.\n"
"Each resource type has two limit values: the hard limit and the soft limit.\n"
"You can change a soft limit freely as long as it does not exceed the hard\n"
"limit. You can decrease a hard limit but cannot increase it without a proper\n"
"permission. <limit> must be a non-negative integer or one of `hard', `soft',\n"
"and `unlimited'.\n"
"The resource limits will affect the current shell process and all commands\n"
"invoked from the shell.\n"
"If <limit> is not specified, the current limit is printed.\n"
"Available options:\n"
" -H --hard\n"
"\tSet or print the hard limit only.\n"
" -S --soft\n"
"\tSet or print the soft limit only.\n"
"\tIf neither -H nor -S is specified, both the hard and soft limits are\n"
"\tset (if <limit> is specified) or only the soft limit is printed (if\n"
"\t<limit> is not specified).\n"
" -a --all\n"
"\tPrint all available resource types with the current limits.\n"
"The following options can be used to specify the resource type:\n"
" -c --core:       The maximum size of core files created (B)\n"
" -d --data:       The maximum size of a process's data segment (K)\n"
" -e --nice:       The maximum scheduling priority (`nice')\n"
" -f --fsize:      The maximum size of files created by a process (B)\n"
" -i --sigpending: The maximum number of pending signals\n"
" -l --memlock:    The maximum memory size that can be locked into RAM (K)\n"
" -m --rss:        The maximum size of a process's resident set (K)\n"
" -n --nofile:     The maximum file descriptor + 1\n"
" -q --msgqueue:   The maximum size of POSIX message queues\n"
" -r --rtprio:     The maximum real-time scheduling priority\n"
" -s --stack:      The maximum size of a process's stack (K)\n"
" -t --cpu:        The CPU time that can be used by a process (S)\n"
" -u --nproc:      The maximum number of processes for a user\n"
" -v --as:         The maximum size of memory used by a process (K)\n"
" -x --locks:      The maximum number of file locks\n"
"For resource types marked with (B), (K), and (S), the limits are set and\n"
"printed in 512-byte blocks, kilobytes, and seconds, respectively.\n"
"Available resource types vary depending on your system.\n"
);
#endif /* YASH_ENABLE_HELP */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
