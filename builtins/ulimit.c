/* Yash: yet another shell */
/* ulimit.c: ulimit builtin */
/* (C) 2007-2012 magicant */

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
#include "ulimit.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#ifdef HAVE_GETTEXT
# include <libintl.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <wchar.h>
#include <wctype.h>
#include "../builtin.h"
#include "../exec.h"
#include "../util.h"
/* Including <stdint.h> is required before including <sys/resource.h> on
 * FreeBSD, but <stdint.h> is automatically included in <inttypes.h>. */
/* On old Mac OS X, <sys/resource.h> depends on but does not include
 * <sys/time.h>. We have to include it manually. */

#if !HAVE_RLIM_SAVED_MAX
# define RLIM_SAVED_MAX RLIM_INFINITY
#endif
#if !HAVE_RLIM_SAVED_CUR
# define RLIM_SAVED_CUR RLIM_INFINITY
#endif


struct resource {
    int type;
    rlim_t factor;
    const char *description;
};

static int print_all_limits(bool soft);
static void print_limit_value(bool soft, const struct rlimit *rlimit,
	const struct resource *resource)
    __attribute__((nonnull));

#define RES(type,factor,desc) \
    (struct resource) { type, factor, desc, }

static const struct resource res_fsize = {
    RLIMIT_FSIZE, 512, Ngt("file size (blocks)")
};

const struct xgetopt_T ulimit_options[] = {
    { L'H', L"hard", OPTARG_NONE, true, NULL, },
    { L'S', L"soft", OPTARG_NONE, true, NULL, },
    { L'a', L"all",  OPTARG_NONE, true, NULL, },
    { L'c', L"core", OPTARG_NONE, true,
	&RES(RLIMIT_CORE, 512, Ngt("core file size (blocks)")), },
    { L'd', L"data", OPTARG_NONE, true,
	&RES(RLIMIT_DATA, 1024, Ngt("data segment size (kbytes)")), },
#if HAVE_RLIMIT_NICE
    { L'e', L"nice", OPTARG_NONE, true,
	&RES(RLIMIT_NICE, 1, Ngt("max nice")), },
#endif
    { L'f', L"fsize", OPTARG_NONE, true, (void *) &res_fsize, },
#if HAVE_RLIMIT_SIGPENDING
    { L'i', L"sigpending", OPTARG_NONE, true,
	&RES(RLIMIT_SIGPENDING, 1, Ngt("pending signals")), },
#endif
#if HAVE_RLIMIT_MEMLOCK
    { L'l', L"memlock", OPTARG_NONE, true,
	&RES(RLIMIT_MEMLOCK, 1024, Ngt("locked memory (kbytes)")), },
#endif
#if HAVE_RLIMIT_RSS
    { L'm', L"rss", OPTARG_NONE, true,
	&RES(RLIMIT_RSS, 1024, Ngt("resident set size (kbytes)")), },
#endif
    { L'n', L"nofile", OPTARG_NONE, true,
	&RES(RLIMIT_NOFILE, 1, Ngt("open files")), },
#if HAVE_RLIMIT_MSGQUEUE
    { L'q', L"msgqueue", OPTARG_NONE, true,
	&RES(RLIMIT_MSGQUEUE, 1, Ngt("message queue size (bytes)")), },
#endif
#if HAVE_RLIMIT_RTPRIO
    { L'r', L"rtprio", OPTARG_NONE, true,
	&RES(RLIMIT_RTPRIO, 1, Ngt("real-time priority")), },
#endif
    { L's', L"stack", OPTARG_NONE, true,
	&RES(RLIMIT_STACK, 1024, Ngt("stack size (kbytes)")), },
    { L't', L"cpu", OPTARG_NONE, true,
	&RES(RLIMIT_CPU, 1, Ngt("CPU time (seconds)")), },
#if HAVE_RLIMIT_NPROC
    { L'u', L"nproc", OPTARG_NONE, true,
	&RES(RLIMIT_NPROC, 1, Ngt("user processes")), },
#endif
#if HAVE_RLIMIT_AS
    { L'v', L"as", OPTARG_NONE, true,
	&RES(RLIMIT_AS, 1024, Ngt("memory (kbytes)")), },
#endif
#if HAVE_RLIMIT_LOCKS
    { L'x', L"locks", OPTARG_NONE, true,
	&RES(RLIMIT_LOCKS, 1, Ngt("file locks")), },
#endif
#if YASH_ENABLE_HELP
    { L'-', L"help", OPTARG_NONE, false, NULL, },
#endif
    { L'\0', NULL, 0, false, NULL, },
};

/* The "ulimit" built-in. */
int ulimit_builtin(int argc, void **argv)
{
    enum { HARD = 1 << 0, SOFT = 1 << 1, } type = HARD | SOFT;
    wchar_t resourceoption = L'\0';
    const struct resource *resource = &res_fsize;
    bool print_all = false;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, ulimit_options, 0)) != NULL) {
	switch (opt->shortopt) {
	    case L'H':  type = HARD;       break;
	    case L'S':  type = SOFT;       break;
	    case L'a':  print_all = true;  break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		if (opt->ptr != NULL) {
		    resourceoption = opt->shortopt;
		    resource = opt->ptr;
		    break;
		} else {
		    return Exit_ERROR;
		}
	}
    }

    assert(type & (HARD | SOFT));
    if (print_all) {
	if (resourceoption != L'\0')
	    return mutually_exclusive_option_error(L'a', resourceoption);
	if (!validate_operand_count(argc - xoptind, 0, 0))
	    return Exit_ERROR;

	return print_all_limits(type & SOFT);
    }

    if (!validate_operand_count(argc - xoptind, 0, 1))
	return Exit_ERROR;

    struct rlimit rlimit;
    if (getrlimit(resource->type, &rlimit) < 0) {
	xerror(errno, Ngt("cannot get the current limit "
			"for the resource type of `%s'"),
			gt(resource->description));
	return Exit_FAILURE;
    }

    if (xoptind == argc) {
	print_limit_value(type & SOFT, &rlimit, resource);
	return yash_error_message_count == 0 ? Exit_SUCCESS : Exit_FAILURE;
    }

    /* parse the operand */
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

    if (type & HARD)
	rlimit.rlim_max = value;
    if (type & SOFT)
	rlimit.rlim_cur = value;
    
    /* check if soft limit does not exceed hard limit */
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
    }

    return Exit_SUCCESS;

err_format:
    xerror(0, Ngt("`%ls' is not a valid integer"), ARGV(xoptind));
    return Exit_ERROR;
}

/* Prints all current ulimit values to the standard output.
 * Returns the exit status of the ulimit built-in. */
int print_all_limits(bool soft)
{
    const struct xgetopt_T *opt;
    for (opt = ulimit_options; opt->shortopt != L'\0'; opt++) {
	const struct resource *resource = opt->ptr;
	if (resource == NULL)
	    continue;

	struct rlimit rlimit;
	if (getrlimit(resource->type, &rlimit) < 0) {
	    xerror(errno, Ngt("cannot get the current limit "
			    "for the resource type of `%s'"),
			    gt(resource->description));
	    continue;
	}

	xprintf(gt("-%lc: %-30s "),
		(wint_t) opt->shortopt, gt(resource->description));
	print_limit_value(soft, &rlimit, resource);
    }
    return yash_error_message_count == 0 ? Exit_SUCCESS : Exit_FAILURE;
}

void print_limit_value(bool soft, const struct rlimit *rlimit,
	const struct resource *resource)
{
    rlim_t value = soft ? rlimit->rlim_cur : rlimit->rlim_max;
    if (value == RLIM_INFINITY)
	xprintf("%s\n", gt("unlimited"));
    else
	xprintf("%ju\n", (uintmax_t) (value / resource->factor));
}

#if YASH_ENABLE_HELP
const char ulimit_help[] = Ngt(
"set or print a resource limitation"
);
const char ulimit_syntax[] = Ngt(
"\tulimit -a [-H|-S]\n"
"\tulimit [-H|-S] [-efilnqrstuvx] [limit]\n"
);
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
