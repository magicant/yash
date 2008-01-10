/* Yash: yet another shell */
/* builtin.c: variable-related shell builtin commands */
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


#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "builtin.h"
#include "variable.h"
#include <assert.h>

int builtin_export(int argc, char **argv);
int builtin_unset(int argc, char **argv);


/* export 組込みコマンド
 * -n: 環境変数を削除する */
int builtin_export(int argc, char **argv)
{
	bool remove = false;
	int opt;

	if (argc == 1)
		goto usage;
	xoptind = 0;
	xopterr = true;
	while ((opt = xgetopt(argv, "n")) >= 0) {
		switch (opt) {
			case 'n':
				remove = true;
				break;
			default:
				goto usage;
		}
	}
	for (; xoptind < argc; xoptind++) {
		char *c = argv[xoptind];

		if (remove) {
			unexport(c);
		} else {
			size_t namelen = strcspn(c, "=");
			if (c[namelen] == '=') {
				c[namelen] = '\0';
				if (!setvar(c, c + namelen + 1, true)) {
					c[namelen] = '=';
					return EXIT_FAILURE;
				}
				c[namelen] = '=';
			} else {
				export(c);
			}
		}
	}
	return EXIT_SUCCESS;

usage:
	fprintf(stderr, "Usage:  export NAME=VALUE ...\n");
	fprintf(stderr, "    or  export -n NAME ...\n");
	return EXIT_FAILURE;
}

/* unset 組込みコマンド
 * -v: 変数だけを削除する。(デフォルト)
 * -f: 関数だけを削除する。(未実装)
 * 一度に同名の変数と関数を両方削除することはできない。
 * readonly 変数は削除できない。 */
int builtin_unset(int argc __attribute__((unused)), char **argv)
{
	bool unfunction = (strcmp(argv[0], "unfunction") == 0);
	int opt;
	bool ok = false;

	xoptind = 0;
	xopterr = true;
	while ((opt = xgetopt(argv, "vf")) >= 0) {
		switch (opt) {
			case 'v':
				unfunction = false;
				break;
			case 'f':
				unfunction = true;
				break;
			default:
				goto usage;
		}
	}

	while (argv[xoptind]) {
		if (!is_name(argv[xoptind])) {
			xerror(0, 0, "%s: %s: invalid name", argv[0], argv[xoptind]);
			ok = false;
		} else {
			if (!unfunction) {  /* 変数を削除する */
				ok &= unsetvar(argv[xoptind]);
			} else {   /* 関数を削除する */
				//XXX 関数は未実装
			}
		}
		xoptind++;
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;

usage:
	fprintf(stderr, "Usage:  unset [-vf] name\n");
	return EXIT_FAILURE;
}
