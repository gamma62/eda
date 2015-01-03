#!/bin/bash
# this tool will generate 
#	keys.h and rkeys.h	from curses_ext.h
#	edakeys			from curses_ext.h and command.h
#
# Copyright 2003-2014 Attila Gy. Molnar
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

source ../conf.make

function usage
{
	cat <<.
usage:	$(basename $0) [keys.h] [rkeys.h] [edakeys]

.
	exit 1
}

function generate__keys
{
# awk separates words by space, keep define as one word
	[ -f _keys ] || \
	awk '!/reserved/ && /[#*]define KEY_/ {print $2}' \
		curses_ext.h | uniq > _keys
	[ -f _rkeys ] || \
	awk '!/internal use only/ && /reserved/ && /[#*]define KEY_/ {print $2}' \
		curses_ext.h | uniq > _rkeys
}

function gen_keys_h
{
	echo "/* keys.h : generated file, don't edit! */" > keys.h
	awk '{print "\t{ KN(" $1 "), },"}' \
		_keys >> keys.h
}

function gen_rkeys_h
{
	echo "/* rkeys.h : generated file, don't edit! */" > rkeys.h
	awk '{print "\t{ KN(" $1 "), },"}' \
		_rkeys >> rkeys.h
	echo "#define RESERVED_KEYS $(cat _rkeys | wc -l)" >> rkeys.h
}

function gen_xkeys
{
	TMPFILE=$(tempfile -p $(basename $0))

	echo "# System-wide configuration file" > $TMPFILE
	echo "# do not edit, save a copy to ~/.eda/ instead" >> $TMPFILE
	echo "" >> $TMPFILE
	echo "# edakeys - default Function -> Key bindings" >> $TMPFILE
	echo "# (see also macros and current setting by the cmds command)" >> $TMPFILE
	echo "" >> $TMPFILE

	echo "### reserved keys ###" >> $TMPFILE
	pr -5 -w90 -T _rkeys | sed -r 's/^(.*)$/# \1/' >> $TMPFILE
	echo >> $TMPFILE

	echo "### general keys ###" >> $TMPFILE
	pr -5 -w90 -T _keys | sed -r 's/^(.*)$/# \1/' >> $TMPFILE
	echo >> $TMPFILE

	echo "### Function -> Key bindings ###" >> $TMPFILE
	sed -r -n -e '/\x09KEY_[A-Z0-9_]*/p' \
		command.h |\
	sed -r -e 's/^.+(KEY_[A-Z0-9_]*),.+PN\(([a-zA-Z0-9_]+)\).+$/#\2\t\t\1/' >> $TMPFILE
	echo >> $TMPFILE

	if ! test -f edakeys ; then
		cat $TMPFILE > edakeys
	elif diff -q -b -B $TMPFILE edakeys | egrep -q differ ; then
		cat $TMPFILE > edakeys
	fi
	rm $TMPFILE
}

function clean_up
{
	if [ -f _keys ]; then rm _keys; fi
	if [ -f _rkeys ]; then rm _rkeys; fi
}

# ------------------------------------------------------------------------

if [ $# -lt 1 ]; then
	usage >&2
	exit 1
fi

clean_up
for f in $*; do
	case $f in
	keys.h)
		generate__keys
		gen_keys_h
		;;
	rkeys.h)
		generate__keys
		gen_rkeys_h
		;;
	edakeys)
		generate__keys
		gen_xkeys
		;;
	*)
		usage >&2
		exit 1
		;;
	esac
done
clean_up

