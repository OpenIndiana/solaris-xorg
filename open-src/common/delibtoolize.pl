#! /usr/perl5/bin/perl
#
# Copyright (c) 2007, 2013, Oracle and/or its affiliates. All rights reserved.
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
# Undo libtool damage to makefiles to allow us to control the linker
# settings that libtool tries to force on us.
#
# Usage: delibtoolize.pl [-P] [-s] [--shared=<so>] <path>
# -P - Use large pic flags (-KPIC/-fPIC) instead of default/small (-Kpic/-fpic)
# -s - Only track libraries from a single file at a time, instead of across all
#	files in a project
# --shared=libname.so.0 - force libname to be treated as a shared library, 
#	even if version flags are not found

use strict;
use warnings;
use integer;
use Getopt::Long;

use File::Find;

my $large_pic = '';	# -P
my $single_file = '';	# -s
my @shared_args = ();

die unless GetOptions(
    "P" => \$large_pic,
    "s" => \$single_file,
    "shared=s" => \@shared_args
);

my $pic_size = $large_pic ? "PIC" : "pic";

my %compiler_pic_flags = ( 'cc' => "-K$pic_size -DPIC",
			   'gcc' => "-f$pic_size -DPIC" );
my %compiler_sharedobj_flags = ( 'cc' => '-G -z allextract',
				 'gcc' => '-shared  -Wl,-z,allextract' );

my %so_versions = ();
my %ltlib_names = ();
my @Makefiles;

# initialize %so_versions with command line flags
foreach my $so ( @shared_args ) {
    if ($so =~ m{^(.*)\.so\.([\d\.]+)}) {
	$so_versions{$1} = $2;
    } elsif ($so =~ m{^(.*)\.so$}) {
	$so_versions{$1} = 'none';
    } else {
	die "Unable to parse --shared object name $so\n";
    }
}

sub rulename_to_filename {
  my $rulename = $_[0];
  if (exists($ltlib_names{$rulename})) {
    return $ltlib_names{$rulename};
  } else {
    return $rulename;
  }
}

# Expands make macros in a string
sub expand_macros {
  my ($in, $macro_ref) = @_;

  $in =~ s{\$\(([^\)]+)\)}{
    exists($macro_ref->{$1}) ? $macro_ref->{$1} : "" }msgex;

  return $in;
}

sub scan_file {
  if ($_ eq 'Makefile' && -f $_) {
    my $old_file = $_;

    open my $OLD, '<', $old_file
      or die "Can't open $old_file for reading: $!\n";

    # Read in original file and preprocess for data we'll need later
    my $l = "";
    my %makefile_macros = ();
    my %makefile_ldflags = ();
    my @makefile_ltlibs = ();

    while (my $n = <$OLD>) {
      $l .= $n;
      # handle line continuation
      next if ($n =~ m/\\$/);

      # Save macros for later expansion if needed
      if ($l =~ m/^([^\#\s]*)\s*=\s*(.*?)\s*$/ms) {
	$makefile_macros{$1} = $2;
      }

      if ($l =~ m/^([^\#\s]*)_la_LDFLAGS\s*=(.*)/ms) {
	my $libname = $1;
	my $flags = $2;

	$makefile_ldflags{$libname} = $flags;
      }
      elsif ($l =~ m/^[^\#\s]*_LTLIBRARIES\s*=(.*)$/ms) {
	push @makefile_ltlibs, $1;
      }
      $l = "";
    }
    close($OLD) or die;

    foreach my $ltline ( @makefile_ltlibs ) {
      my $ltline_exp = expand_macros($ltline, \%makefile_macros);
      foreach my $ltl (split /\s+/, $ltline_exp) {
	$ltl =~ s{\.la$}{}ms;
	my $transformed = $ltl;
	$transformed =~ s{[^\w\@]}{_}msg;
	$ltlib_names{$transformed} = $ltl;
      }
    }

    foreach my $librulename (keys %makefile_ldflags) {
      my $libname = rulename_to_filename($librulename);
      my $flags = expand_macros($makefile_ldflags{$librulename},
				\%makefile_macros);
      my $vers;

      if ($flags =~ m/[\b\s]-version-(number|info)\s+(\S+)/ms) {
	my $vtype = $1;
	my $v = $2;
	
	if (($vtype eq "info") && ($v =~ m/^(\d+):\d+:(\d+)$/ms)) {
	  $vers = $1 - $2;
	} elsif ($v =~ m/^(\d+)[:\d]*$/ms) {
	  $vers = $1;
	} else {
	  $vers = $v;
	}
      }
      elsif ($flags =~ m/-avoid-version\b/ms) {
	$vers = 'none';
      }

      my $ln = $libname;
      if ($single_file) {
	$ln = $File::Find::name . "::" . $libname;
      }
      if (defined($vers) && !defined($so_versions{$ln})) {
	$so_versions{$ln} = $vers;
#	print "Set version to $so_versions{$ln} for $ln.\n";
      }
    }

    push @Makefiles, $File::Find::name;
  }
}

sub modify_file {
  my ($filename) = @_;

  print "delibtoolizing $filename...\n";

  my $old_file = $filename . '~';
  my $new_file = $filename;
  rename($new_file, $old_file) or
    die "Can't rename $new_file to $old_file: $!\n";

  open my $OLD, '<', $old_file
    or die "Can't open $old_file for reading: $!\n";
  open my $NEW, '>', $new_file
    or die "Can't open $new_file for writing: $!\n";

  my $compiler;
  my @inlines = ();

  # Read in original file and preprocess for data we'll need later
  my $l = "";
  while (my $n = <$OLD>) {
    $l .= $n;
    # handle line continuation
    next if ($n =~ m/\\$/);

    if ($l =~ m/^\s*CC\s*=(?:.*\s+)?(\S*cc)/) {
      $compiler = $1;
    }

    push @inlines, $l;
    $l = "";
  }
  close($OLD) or die;

  my $compiler_type = 'cc'; # default to Sun Studio
  if (defined($compiler) && ($compiler =~ m/gcc/)) {
    $compiler_type = 'gcc';
  }

  my $picflags = $compiler_pic_flags{$compiler_type};
  my $sharedobjflags = $compiler_sharedobj_flags{$compiler_type};

  my $curtarget = "";

  foreach $l (@inlines) {
    chomp $l;

    # Remove libtool script from compile steps &
    # add PIC flags that libtool normally provides
    $l =~ s{\$\(LIBTOOL\)
	    (?:[\\\s]+ \$\(LT_QUIET\))?
	    (?:[\\\s]+ \$\(AM_V_lt\))?
	    (?:[\\\s]+ --tag=(?:CC|CXX))?
	    (?:[\\\s]+ \$\(AM_LIBTOOLFLAGS\) [\\\s]+ \$\(LIBTOOLFLAGS\))?
	    [\\\s]+ --mode=compile
	    [\\\s]+ (\$\(CC\)|\$\(CCAS\)|\$\(CXX\)|\$\(COMPILE\))
	  }{$1 $picflags}xs;

    # Remove libtool script from link step
    $l =~ s{\$\(LIBTOOL\)
	    (?:[\\\s]+ \$\(LT_QUIET\))?
	    (?:[\\\s]+ \$\(AM_V_lt\))?
	    (?:[\\\s]+ --tag=(?:CC|CXX))?
	    (?:[\\\s]+ \$\(AM_LIBTOOLFLAGS\) [\\\s]+ \$\(LIBTOOLFLAGS\))?
	    [\\\s]+ --mode=link
	  }{}xs;

    # Remove -rpath <directory> from link arguments
    $l =~ s{(\s*)-rpath[\\\s]+\S+(\s*)}{$1}msg;

    # Change flags for building shared object from arguments to libtool
    # script into arguments to linker
    if ($l =~ m/_la_LDFLAGS\s*=/) {
      $l =~ s{(\s*$sharedobjflags)+\b}{}msg;
      $l =~ s{(_la_LDFLAGS\s*=\s*)}{$1 $sharedobjflags }ms;
      $l =~ s{(\s+)-avoid-version\b}{$1}ms;
      $l =~ s{(\s+)-module\b}{$1}ms;
      $l =~ s{(\s+)-export-symbols(-regex)?\s*\S+}{$1}ms;
      $l =~ s{(\s+)-version-(?:number|info)\s+\S+}{$1-h \$\@}ms;
      $l =~ s{(\s+)-no-undefined\b}{$1-z defs}ms;
    }

    # Change file names
    my @so_list = keys %so_versions;
    if ($single_file) {
      my $pat = $filename . "::";
      @so_list = grep(/^$pat/, @so_list);
    }
    foreach my $so (@so_list) {
      my $v = $so_versions{$so};
      if ($v eq 'none') {
	$l =~ s{$so\.la\b}{$so.so}msg;
      } else {
	$l =~ s{$so\.la\b}{$so.so.$v}msg;
      }
    }
    $l =~ s{\.la\b}{.a}msg;
    $l =~ s{\.libs/([\*%])\.o\b}{$1.lo}msg;
    $l =~ s{\.lo\b}{.o}msg;

    my $newtarget = $curtarget;
    if ($l =~ m/^(\S+):/) {
      $newtarget = $1;
    } elsif ($l =~ m/^\s*$/) { 
      $newtarget = "";
    }

    if ($curtarget ne $newtarget) { # end of rules for a target
      # Need to add in .so links that libtool makes for .la installs
      if ($curtarget =~ m/^install-(.*)LTLIBRARIES$/ms) {
	my $dirname = $1;
	my $installrule = <<'END_RULE';
	list='$(<DIRNAME>_LTLIBRARIES)'; for p in $$list; do \
	  so=$${p%.[[:digit:]]} ; \
	  if [[ "$$p" != "$$so" ]] ; then \
		echo "rm -f $(DESTDIR)$(<DIRNAME>dir)/$$so" ; \
		rm -f $(DESTDIR)$(<DIRNAME>dir)/$$so ; \
		echo "ln -s $$p $(DESTDIR)$(<DIRNAME>dir)/$$so" ; \
		ln -s $$p $(DESTDIR)$(<DIRNAME>dir)/$$so ; \
	  fi; \
	done
END_RULE
	$installrule =~ s/\<DIRNAME\>/$dirname/msg;
	$l .= $installrule;
      }

      $curtarget = $newtarget;
    }

    # Static libraries
    if ($curtarget =~ m/^.*\.a$/) {
      $l =~ s{\$\(\w*LINK\)}{\$(AR) cru $curtarget}ms;
      $l =~ s{\$\(\w*(?:LIBS|LIBADD)\)}{}msg;
      $l =~ s{(\$\((?:\w*_)?AR\).*\s+)-R\s*\$\(libdir\)}{$1}msg;
    }

    print $NEW $l, "\n";
    $l = "";
  }
  close($NEW) or die;
}

find(\&scan_file, @ARGV);

foreach my $mf ( @Makefiles ) {
  modify_file($mf);
}
