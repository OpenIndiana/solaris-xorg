###############################################################################
#
# libpthread-stubs Makefile
#
# Copyright (c) 2008, 2016, Oracle and/or its affiliates. All rights reserved.
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

# Package name used in tarballs
MODULE_NAME=libpthread-stubs

# Package metadata
UPSTREAM_PROJECT = xcb
UPSTREAM_PROJECT_SET = yes
GIT_URL = $(FDO_GIT_URL)/xcb/pthread-stubs
GIT_URL_SET = yes

# Version number (used in path names)
MODULE_VERSION=0.3

# Checksums for upstream tarball
TARBALL_MD5  = e8fa31b42e13f87e8f5a7a2b731db7ee
TARBALL_SHA1 = 7fc486ad0ec54938f8b781cc374218f50eac8b99

# Package metadata
ORACLE_TPNO = $(ORACLE_TPNO_xorg)
ORACLE_TPNO_SET = yes

# Patches to apply to source after unpacking, in order
SOURCE_PATCHES = 

# Library name
LIBNAME=pthread-stubs

# Package classification (override default)
MODULE_PKGCLASS=Development/X11
MODULE_PKGCLASS_SET=yes

# ARC cases that covered this module
# LSARC/2008/657 pthread-stubs
MODULE_ARC_CASES = LSARC/2008/657

# Man pages to apply Sun footer to & attributes to list
#SUNTOUCHED_MANPAGES=man/*.man
MODULE_STABILITY=Volatile
LIB_MAN_SUFFIX=3libpthread-stubs

# pkg-config files that need to have -R added
FIX_PC_FILES=pthread-stubs.pc.in

include ../Makefile.inc
