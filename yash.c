/* Yash: yet another shell */
/* yash.c: basic functions of the shell */
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


#include "common.h"
#include "version.h"
#include <locale.h>
#include <stdio.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif


int main(int argc, char **argv) __attribute__((nonnull));
void print_version(void);


int main(int argc, char **argv)
{
	setlocale(LC_ALL, "");
#if HAVE_GETTEXT
	bindtextdomain(PACKAGE_NAME, LOCALEDIR);
	textdomain(PACKAGE_NAME);
#endif

	print_version();
	return 0;
}

void print_version(void)
{
	printf(gt("Yet another shell, version %s\n"), PACKAGE_VERSION);
	printf(PACKAGE_COPYRIGHT "\n");
}
