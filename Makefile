# Makefile for yash: yet another shell
# © 2007 magicant
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


CC = gcc
CFLAGS = -O3 -ggdb -std=gnu99
ifeq (,$(findstring -DNDEBUG,$(CPPFLAGS)))
CFLAGS += -Wall -Wextra
endif
LDFLAGS = -lreadline -ltermcap
OBJS = yash.o util.o readline.o parser.o expand.o exec.o path.o builtin.o alias.o
TARGET = yash

$(TARGET): $(OBJS)

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: clean

# Dependencies
yash.o: yash.h util.h parser.h exec.h path.h builtin.h alias.h
util.o: yash.h util.h
readline.o: yash.h util.h readline.h exec.h path.h
parser.o: yash.h parser.h alias.h
expand.o: yash.h parser.h expand.h
exec.o: yash.h util.h expand.h exec.h path.h builtin.h
path.o: yash.h util.h path.h
builtin.o: yash.h util.h expand.h exec.h path.h builtin.h alias.h
alias.o: yash.h alias.h
