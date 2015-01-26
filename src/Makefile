# Makefile for eda sources
#
# Copyright 2003-2015 Attila Gy. Molnar
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

PROGHOME = $(datadir)/eda
ARCH = $(shell uname -m)

CC = gcc
HARDENING = -g -O2 -fPIE \
	-Wformat -Werror=format-security
CFLAGS = -W -Wall -D_FORTIFY_SOURCE=2 $(HARDENING) \
	-Wshadow -Wpointer-arith -Wstrict-prototypes -I..
LDFLAGS = $(HARDENING) -fPIE -pie -Wl,-z,relro -Wl,-z,now \
	-rdynamic

HDRS = main.h curses_ext.h command.h proto.h keys.h rkeys.h
OBJS = main.o ed.o fh.o lll.o cmd.o disp.o keys.o cmdlib.o select.o filter.o \
	util.o search.o tags.o pipe.o rc.o ring.o
SRCS = $(OBJS:.o=.c)

# ------------------------------------

all: eda

eda: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS) -lncurses

install:
	install -d -m 755  $(DESTDIR)$(PREFIX)/bin/
	install -m 755 -p -s eda  $(DESTDIR)$(PREFIX)/bin/
	install -m 755 -p xeda  $(DESTDIR)$(PREFIX)/bin/
	install -d -m 755 -p  $(DESTDIR)$(CONFPATH)/
	install -m 644 -p edarc edakeys edaseq  $(DESTDIR)$(CONFPATH)/
	install -d -m 755  $(DESTDIR)$(PROGHOME)/
	install -m 644 -p edarc edakeys edaseq edamacro sample.proj  $(DESTDIR)$(PROGHOME)/
	install -m 755 -p set_op.pl  $(DESTDIR)$(PROGHOME)/

uninstall:
	for f in eda xeda ; do \
	  if test -f $(DESTDIR)$(PREFIX)/bin/$$f ; then rm -f $(DESTDIR)$(PREFIX)/bin/$$f ; fi ; \
	done
	for f in edarc edakeys edaseq ; do \
	  if test -f $(DESTDIR)$(CONFPATH)/$$f ; then rm -f $(DESTDIR)$(CONFPATH)/$$f ; fi ; \
	done
	-rmdir $(DESTDIR)$(CONFPATH) || true
	for f in edarc edakeys edaseq edamacro sample.proj set_op.pl ; do \
	  if test -f $(DESTDIR)$(PROGHOME)/$$f ; then rm -f $(DESTDIR)$(PROGHOME)/$$f ; fi ; \
	done
	-rmdir $(DESTDIR)$(PROGHOME) || true

clean:
	-rm -f *.o *.i *~ core* eda tags

ctags: tags

tags: $(SRCS) $(HDRS)
	-ctags *.c *.h ../*.h

# ------------------------------------

main.o: main.c $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -DVERSION=\"$(VERSION)\" -c -o $@ $<

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
