#! /usr/perl5/bin/perl
#
# Copyright (c) 2010, 2013, Oracle and/or its affiliates. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#
#

#
# Merge pkg(5) metadata files to reduce duplicates and combine attribute
# values for the same attribute name into a single line
#

use strict;
use warnings;
use diagnostics;

my %attributes = ();
my %licenses = ();

while (my $in = <>) {
  chomp $in;
  if ($in =~ m{^set name=(\S+) value="(.*)"$}) {
    my ($name, $value) = ($1, $2);
    $attributes{$name}->{$value} = 1;
  } elsif ($in =~ m{^license }) {
    $licenses{$in} = 1;
  } else {
    # Pass through other lines unchanged
    print $in, "\n";
  }
}

foreach my $n (sort keys %attributes) {
  print qq(set name="$n");
  foreach my $v (sort keys %{$attributes{$n}}) {
    print qq( value="$v");
  }
  print "\n";
}

# If there's just one license for the whole package, we promote some of the
# license attributes to be package level attributes.
# If there's more than one license, and not all licenses are the same, 
# just pass all the lines through as we got them.

my $license_count = scalar(keys %licenses);
my $license_lines = join("\n", sort keys %licenses);
if ($license_count == 1) {
  while ($license_lines =~
      s{^(license\s+.*?)\s+
	(com\.oracle\.info\.(?:name|version|description|tpno))="([^"]*)"
	(.*)}{$1$4\nset name=$2 value="$3"}gmx)
  {
    # all the work is done in the while (...) statement
  }
  # if no description is provided, copy the pkg.summary for it
  $license_lines =~ 
      s{set name=com.oracle.info.description value=""}
       {<transform set name=pkg.summary -> emit set name=com.oracle.info.description value=%{pkg.summary}>};
}
print "\n", $license_lines, "\n";
