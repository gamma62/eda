# Makefile for eda documentation
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

all:

install:
	install -d -m 755 -p  $(DESTDIR)$(mandir)/man1/
	install -m 644 -p eda.1  $(DESTDIR)$(mandir)/man1/
	install -d -m 755 -p  $(DESTDIR)$(mandir)/man5/
	install -m 644 -p edamacro.5  $(DESTDIR)$(mandir)/man5/
	install -d -m 755 -p  $(DESTDIR)$(datadir)/eda
	install -m 644 -p cmds.txt eda-icon.png eda.desktop  $(DESTDIR)$(datadir)/eda/
	install -d -m 755 -p  $(DESTDIR)$(docdir)/eda
	install -m 644 -p ../Copyright ../LICENSE ../ChangeLog ../NEWS ../README  $(DESTDIR)$(docdir)/eda/

uninstall:
	@if test -f $(DESTDIR)$(mandir)/man1/eda.1 ; then rm -f $(DESTDIR)$(mandir)/man1/eda.1 ; fi
	@if test -f $(DESTDIR)$(mandir)/man5/edamacro.5 ; then rm -f $(DESTDIR)$(mandir)/man5/edamacro.5 ; fi
	@for f in cmds.txt eda-icon.png eda.desktop ; do \
	  if test -f $(DESTDIR)$(datadir)/eda/$$f ; then rm -f $(DESTDIR)$(datadir)/eda/$$f ; fi ; \
	done
	@rmdir $(DESTDIR)$(datadir)/eda 2>/dev/null || true
	@for f in Copyright LICENSE ChangeLog NEWS README  ; do \
	  if test -f $(DESTDIR)$(docdir)/eda/$$f ; then rm -f $(DESTDIR)$(docdir)/eda/$$f ; fi ; \
	done
	@rmdir $(DESTDIR)$(docdir)/eda 2>/dev/null || true

clean:
	-rm -f *~

