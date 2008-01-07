# Makefile for yash: yet another shell
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


CC = gcc
CFLAGS = -O3 -ggdb -std=gnu99
ifeq (,$(findstring -DNDEBUG,$(CPPFLAGS)))
CFLAGS += -Wall -Wextra
endif
LDFLAGS = -lc -lreadline -ltermcap
OBJS = yash.o util.o sig.o lineinput.o parser.o expand.o exec.o path.o builtin.o builtin_job.o alias.o variable.o
TARGET = yash

$(TARGET): $(OBJS)

clean:
	rm -rf $(OBJS) $(TARGET) $(PACKAGENAME) $(PACKAGENAME).tar.gz

DISTCONTENTS = *.[ch] README.html COPYING Makefile TODO
PACKAGENAME = yash-1.0b1
dist:
	mkdir -p $(PACKAGENAME)
	-ln -f $(DISTCONTENTS) $(PACKAGENAME) && \
		tar -c $(PACKAGENAME) | gzip > $(PACKAGENAME).tar.gz
	rm -rf $(PACKAGENAME)

.PHONY: clean dist

# Dependencies
yash.o: yash.h util.h sig.h lineinput.h parser.h exec.h path.h builtin.h alias.h variable.h
util.o: yash.h util.h
sig.o: util.h sig.h lineinput.h exec.h
lineinput.o: yash.h util.h lineinput.h exec.h path.h variable.h
parser.o: yash.h util.h parser.h alias.h variable.h
expand.o: yash.h util.h parser.h expand.h exec.h path.h variable.h
exec.o: yash.h util.h expand.h exec.h path.h builtin.h variable.h
path.o: yash.h util.h parser.h path.h variable.h
builtin.o: yash.h util.h sig.h lineinput.h expand.h exec.h path.h builtin.h alias.h variable.h
builtin_job.o: yash.h util.h sig.h exec.h path.h builtin.h variable.h
alias.o: yash.h util.h alias.h
variable.o: yash.h util.h exec.h variable.h
