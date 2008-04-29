/* invoke.c: invokes command with given arguments */
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

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
	fprintf(stderr, "invoke: too few arguments\n");
	return 2;
    }
    char *givenname = argv[1];
    char *invokedcommand = argv[2];
    argv[2] = givenname;
    execvp(invokedcommand, argv + 2);
    perror("invoke: exec failed");
    return 126;
}

/* vim: set ts=8 sts=4 sw=4 noet: */
