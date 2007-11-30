# Makefile for yash: yet another shell
# © 2007 magicant
#
# This software can be redistributed and/or modified under the terms of
# GNU General Public License, version 2 or (at your option) any later version.
#
# This software is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRENTY.


CC=gcc
CFLAGS=-O3 -ggdb -Wall -Wextra -Wno-unused-parameter -std=gnu99
LDFLAGS=-lreadline -ltermcap
OBJS=util.o readline.o parser.o exec.o path.o builtin.o alias.o

yash: $(OBJS)

$(OBJS): yash.h

clean:
	rm -f $(OBJS) yash

.PHONY: all clean
