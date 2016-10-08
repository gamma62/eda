# top Makefile for eda
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

include conf.make

all:
	cd src && $(MAKE)

install:
	cd src && $(MAKE) install
	install -d -m 755 -p  $(DESTDIR)$(mandir)/man1/
	install -m 644 -p doc/eda.1  $(DESTDIR)$(mandir)/man1/
	install -d -m 755 -p  $(DESTDIR)$(mandir)/man5/
	install -m 644 -p doc/edamacro.5  $(DESTDIR)$(mandir)/man5/
	install -d -m 755 -p  $(DESTDIR)$(datadir)/eda
	install -m 644 -p doc/cmds.txt doc/eda-icon.png doc/eda.desktop doc/eda.menu  $(DESTDIR)$(datadir)/eda/
	install -d -m 755 -p  $(DESTDIR)$(docdir)/eda
	install -m 644 -p Copyright LICENSE ChangeLog NEWS README  $(DESTDIR)$(docdir)/eda/

uninstall:
	cd src && $(MAKE) uninstall
	if test -f $(DESTDIR)$(mandir)/man1/eda.1 ; then rm -f $(DESTDIR)$(mandir)/man1/eda.1 ; fi
	if test -f $(DESTDIR)$(mandir)/man5/edamacro.5 ; then rm -f $(DESTDIR)$(mandir)/man5/edamacro.5 ; fi
	-rm -f $(DESTDIR)$(datadir)/eda/*
	-rmdir $(DESTDIR)$(datadir)/eda || true
	-rm -f $(DESTDIR)$(docdir)/eda/*
	-rmdir $(DESTDIR)$(docdir)/eda || true

clean:
	cd src && $(MAKE) clean

