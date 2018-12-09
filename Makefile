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
	cd doc && $(MAKE)
	cd src && $(MAKE)

install:
	cd doc && $(MAKE) install
	cd src && $(MAKE) install

uninstall:
	cd doc && $(MAKE) uninstall
	cd src && $(MAKE) uninstall

clean:
	cd doc && $(MAKE) clean
	cd src && $(MAKE) clean

