# Makefile (GNU make syntax) for eda sources
#
# Copyright 2003-2016 Attila Gy. Molnar
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

include ../conf.make
CC = cc

ARCH = $(shell uname -m)
OS = $(shell uname -s)
REV = $(shell cat ../REV 2>/dev/null || echo 0)

CFLAGS = -Wall -O2 \
	-pipe -std=c99 -fstack-protector -fno-strict-aliasing \
	-Wstack-protector -Wshadow -Wpointer-arith -Wstrict-prototypes \
	-Wformat -Wformat-security -Wsign-conversion -Wcast-qual -Wcast-align \
	-Wwrite-strings -Wmissing-declarations -Wmissing-prototypes \
	-Wno-format-truncation -Wno-stringop-truncation -Wno-stringop-overflow \
	-I.. -DLINUX -D_GNU_SOURCE

LDFLAGS = -lncurses

OBJS = main.o ed.o fh.o lll.o cmd.o disp.o keys.o cmdlib.o select.o filter.o \
	util.o search.o tags.o pipe.o rc.o ring.o
SRCS = $(OBJS:.o=.c)

# ------------------------------------

all: eda

eda: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

main.o: main.c $(SRCS) ../config.h curses_ext.h main.h proto.h command.h
	$(CC) $(CFLAGS) -DREV=\"$(REV)\" -DVERSION=\"$(VERSION)\" -c $< -o $@

cmd.o: cmd.c ../config.h curses_ext.h main.h proto.h command.h keys.h rkeys.h
ed.o: ed.c ../config.h curses_ext.h main.h proto.h command.h keys.h rkeys.h

keys.o: keys.c ../config.h curses_ext.h main.h proto.h command.h keys.h rkeys.h
	$(CC) $(CFLAGS) -DCONFPATH=\"$(sysconfdir)/eda\" -c $< -o $@

rc.o: rc.c ../config.h main.h proto.h command.h keys.h rkeys.h
	$(CC) $(CFLAGS) -DCONFPATH=\"$(sysconfdir)/eda\" -DPROGHOME=\"$(datadir)/eda\" -c $< -o $@

cmdlib.o: cmdlib.c ../config.h main.h proto.h
disp.o: disp.c ../config.h main.h proto.h
fh.o: fh.c ../config.h main.h proto.h
filter.o: filter.c ../config.h main.h proto.h
lll.o: lll.c ../config.h main.h proto.h
pipe.o: pipe.c ../config.h main.h proto.h
ring.o: ring.c ../config.h main.h proto.h
search.o: search.c ../config.h main.h proto.h
select.o: select.c ../config.h main.h proto.h
tags.o: tags.c ../config.h main.h proto.h
util.o: util.c ../config.h main.h proto.h

install:
	install -d -m 755  $(DESTDIR)$(prefix)/bin/
	install -m 755 -p -s eda  $(DESTDIR)$(prefix)/bin/
	install -d -m 755 -p  $(DESTDIR)$(sysconfdir)/eda/
	install -m 644 -p edarc edakeys  $(DESTDIR)$(sysconfdir)/eda/
	install -m 644 -p edaseq.$(OS)  $(DESTDIR)$(sysconfdir)/eda/edaseq
	install -d -m 755  $(DESTDIR)$(datadir)/eda/
	install -m 644 -p edarc edakeys edaseq.* edamacro  $(DESTDIR)$(datadir)/eda/
	install -m 644 -p ../check_perl.sh ../check_python.sh ../check_xml.sh  $(DESTDIR)$(datadir)/eda/

uninstall:
	@for f in eda ; do \
	  if test -f $(DESTDIR)$(prefix)/bin/$$f ; then rm -f $(DESTDIR)$(prefix)/bin/$$f ; fi ; \
	done
	@for f in edarc edakeys edaseq ; do \
	  if test -f $(DESTDIR)$(sysconfdir)/eda/$$f ; then rm -f $(DESTDIR)$(sysconfdir)/eda/$$f ; fi ; \
	done
	@rmdir $(DESTDIR)$(sysconfdir)/eda 2>/dev/null || true
	@for f in edarc edakeys edaseq.* edamacro check_perl.sh check_python.sh check_xml.sh ; do \
	  if test -f $(DESTDIR)$(datadir)/eda/$$f ; then rm -f $(DESTDIR)$(datadir)/eda/$$f ; fi ; \
	done
	@rmdir $(DESTDIR)$(datadir)/eda 2>/dev/null || true

clean:
	-rm -f *.o *~ eda

# ------------------------------------

.SUFFIXES: .i .o

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<
.c.i:
	$(CC) $(CFLAGS) -E -o $@ -c $<

# ------------------------------------
