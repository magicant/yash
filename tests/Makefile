# Makefile for test of yash
# (C) 2007-2009 magicant
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

.POSIX:
.SUFFIXES: .c .h .o

CC = c99
CFLAGS = -O1
LDFLAGS = 

TEST_ITEMS = *.tst  # the default test items

INVOKEOBJS = invoke.o
BYPRODUCTS = invoke

test test-all: invoke
	@$(SHELL) ./run-test.sh
test-posix:
	@$(MAKE) TEST_ITEMS='*.p.tst' test

# Though these rules are exactly the same as the default rules defined by POSIX,
# we re-define them here for maximum portability.
.c:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
.c.o:
	$(CC) $(CFLAGS) -c $<

invoke: $(INVOKEOBJS)

distcontents: ;

clean:
	rm -f $(BYPRODUCTS) $(INVOKEOBJS)

.PHONY: test-all test-posix test distcontents clean
