# Makefile for eda sources
#
# Copyright 2003-2011 Attila Gy. Molnar
#
# This file is part of eda project.
#
# Eda is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Eda is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Eda.  If not, see <http://www.gnu.org/licenses/>.
#

#
# $@ - target
# $^ - all source(s)
# $< - 1st source
# $? - newer source(s)
#

include ../conf.make

PROGHOME = $(PREFIX)/share/eda
MANROOT = $(PREFIX)/share/man
DOCLINK = $(PREFIX)/share/doc/eda
ARCH = $(shell uname -m)
REV = 1368

CC = gcc
HARDENING = -g -O2 -fPIE -fstack-protector-all --param=ssp-buffer-size=4 \
	-Wformat -Werror=format-security -Wstack-protector
CFLAGS = -W -Wall -D_FORTIFY_SOURCE=2 $(HARDENING) \
	-Wshadow -Wpointer-arith -Wstrict-prototypes -I..
LDFLAGS = $(HARDENING) -fPIE -pie -Wl,-z,relro -Wl,-z,now \
	-rdynamic

HDRS = main.h curses_ext.h command.h proto.h
KEY_HDRS = keys.h rkeys.h
OBJS = main.o ed.o fh.o lll.o cmd.o disp.o keys.o cmdlib.o select.o filter.o \
	util.o search.o tags.o pipe.o rc.o ring.o
SRCS = $(OBJS:.o=.c)

# ------------------------------------

all: eda

-include .depend

eda: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS) -lncurses

.depend::
	$(CC) $(CFLAGS) -MM *.c > $@

clean:
	-rm -f *.o *.i eda *~

# ------------------------------------

macro::
	@sed edamacro -e 's|__PROGHOME__|'$(PROGHOME)'|g' > edamacro~

main.o: main.c $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -DREV=\"$(REV)\" -DVERSION=\"$(VERSION)\" -c -o $@ $<

rc.o: rc.c
	$(CC) $(CFLAGS) -DCONFPATH=\"$(CONFPATH)\" -DPROGHOME=\"$(PROGHOME)\" -c -o $@ $<

keys.o: keys.c
	$(CC) $(CFLAGS) -DCONFPATH=\"$(CONFPATH)\" -c -o $@ $<

# ------------------------------------

.SUFFIXES: .c .h .i .o

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

.c.i:
	$(CC) -E -W -Wall -I.. -c -o $@ $<

# ------------------------------------
