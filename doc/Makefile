# Makefile for eda documentation
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

include ../conf.make

PROGHOME = $(PREFIX)/share/eda
MANROOT = $(PREFIX)/share/man
DOCLINK = $(PREFIX)/share/doc/eda
PIXMAPS = $(PREFIX)/share/pixmaps

all:

install:
	install -d -m 755 -p  $(DESTDIR)$(MANROOT)/man1/
	install -m 644 -p eda.1  $(DESTDIR)$(MANROOT)/man1/
	install -d -m 755 -p  $(DESTDIR)$(MANROOT)/man5/
	install -m 644 -p edamacro.5  $(DESTDIR)$(MANROOT)/man5/

uninstall:
	if test -f $(DESTDIR)$(MANROOT)/man1/eda.1 ; then rm -f $(DESTDIR)$(MANROOT)/man1/eda.1 ; fi
	if test -f $(DESTDIR)$(MANROOT)/man5/edamacro.5 ; then rm -f $(DESTDIR)$(MANROOT)/man5/edamacro.5 ; fi

clean:
	-rm -f *~

