# Makefile for test of yash
# © 2007-2008 magicant
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
CFLAGS = -O
LDFLAGS = 

test-all:
	@$(MAKE) TEST_ITEMS='*.test' test
test-posix:
	@$(MAKE) TEST_ITEMS='*.posix.test' test
test: invoke
	@exec $(SHELL) ./run-test.sh

# Though these rules are exactly the same as the default rules defined by POSIX,
# we re-define them here because at least GNU Make is not fully compliant.
.c:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f invoke

.PHONY: test-all test-posix test clean
