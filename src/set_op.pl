#!/usr/bin/env perl -w
# set_op.pl
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

use strict;

#
# set operations, like symetrical difference, intersection and union
# two methods: word or line based
#
# separator on input sets: empty line(s) or "--", which comes first
#

my $usage = "\
usage:	set_op.pl [OPTIONS]   [ symdiff | inter | union ] \
	options: \n\
	-w | --words	set operations by words (default) \n\
	-l | --lines	set operations by lines \n\
";

my %old = ();		# for symetrical difference
my %new = ();		#
my %inter = ();		# intersection
my %union = ();		# union
my $maxlen = 0;
my $separate = 0;	# set separator
my $key;

my $opts;
my $task = -1;	# symetrical difference, intersection, union
my $base = 0;	# words, lines
while (@ARGV) {
	$opts = shift();
	if (($opts =~ /-w/) || ($opts =~ /--words?/)) {
		$base = 0;
	} elsif (($opts =~ /-l/) || ($opts =~ /--lines?/)) {
		$base = 1;
	} elsif ($opts =~ /-?-?\bsym/) {
		$task = 0;
	} elsif ($opts =~ /-?-?\binter/) {
		$task = 1;
	} elsif ($opts =~ /-?-?\bunion/) {
		$task = 2;
	} else {
		print STDERR $usage;
		exit 1;
	}
}
if ($task == -1 || $base == -1) {
	print STDERR $usage;
	exit 1;
}

sub pretty_word_output {
	my $llen = 0;
	my $key;
	foreach $key (sort @_) {
		if ($llen + length($key) > $maxlen) {
			print "\n";
			$llen = 0;
		}
		print "$key ";
		$llen += length($key)+1;
	}
	print "\n";
}
sub line_output {
	my $key;
	foreach $key (sort @_) {
		print "$key\n";
	}
}

if ($base == 0) {	# words

	# process input sets
	if ($task == 0 || $task == 1) {	# symetrical difference or intersection
		while (<>) {
			chomp();

			if (/^\s*$/) {	# check separator
				# don't increment before first set (keep 0)
				$separate++ if $separate;
				next;
			}

			if ($maxlen < length($_)) {	# maintain for the output
				$maxlen = length($_);
			}

			foreach $key (split()) {
				if ("$key" eq "--") {
					# begin new set
					$separate = 2;
				} elsif ($separate <= 1) {
					$old{$key} = 1;
					$separate = 1;
				} else {
					$new{$key} = 2;
				}
			}
		}
	}
	elsif ($task == 2) {	# union
		while (<>) {
			chomp();

			if ($maxlen < length($_)) {	# maintain for the output
				$maxlen = length($_);
			}

			foreach $key (split()) {
				if ("$key" ne "--") {
					$union{$key} = 1;
				}
			}
		}
	}

	if ($task == 0) {	# symetrical difference
		# delete common parts
		foreach $key (keys(%old)) {
			if (exists($new{$key})) {
				delete $old{$key};
				delete $new{$key};
			}
		}
	}
	elsif ($task == 1) {	# intersection
		# select common parts
		foreach $key (keys(%old)) {
			if (exists($new{$key})) {
				$inter{$key} = 1;
			}
		}
	}

	# --- pretty out ---
	$maxlen = 80 if ($maxlen > 80);
	$maxlen = 25 if ($maxlen < 25);
	if ($task == 0) {	# symetrical difference
		pretty_word_output(keys %old);
		print "\n";
		pretty_word_output(keys %new);
	}
	elsif ($task == 1) {	# intersection
		pretty_word_output(keys %inter);
	}
	elsif ($task == 2) {	# union
		pretty_word_output(keys %union);
	}

} elsif ($base == 1) {	# lines

	# process input sets
	if ($task == 0 || $task == 1) {	# symetrical difference or intersection
		while (<>) {
			chomp();

			if (/^\s*$/) {	# check separator
				# don't increment before first set (keep 0)
				$separate++ if $separate;
				next;
			}
			if (/^--/) {
				# begin new set
				$separate = 2;
				next;
			}

			if ($separate <= 1) {
				$old{$_} = 1;
				$separate = 1;
			} else {
				$new{$_} = 1;
			}
		}
	}
	elsif ($task == 2) {	# union
		while (<>) {
			chomp();

			if (/^\s*$/) {
				next;
			}
			if (/^--/) {
				next;
			}

			$union{$_} = 1;
		}
	}

	if ($task == 0) {	# symetrical difference
		# delete common lines
		foreach $key (keys(%old)) {
			if (exists($new{$key})) {
				delete $old{$key};
				delete $new{$key};
			}
		}
	}
	elsif ($task == 1) {	# intersection
		# select common lines
		foreach $key (keys(%old)) {
			if (exists($new{$key})) {
				$inter{$key} = 1;
			}
		}
	}

	# --- pretty out ---
	if ($task == 0) {	# symetrical difference
		line_output(keys %old);
		print "\n";
		line_output(keys %new);
	}
	elsif ($task == 1) {	# intersection
		line_output(keys %inter);
	}
	elsif ($task == 2) {	# union
		line_output(keys %union);
	}
}

exit 0;
