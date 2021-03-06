###############################################################################
#
# SPARC graphics utilities modules (fbconfig & VTS) Makefile
#
# Copyright (c) 2011, 2015, Oracle and/or its affiliates. All rights reserved.
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

# Package name used in tarballs
MODULE_NAME=fbconfig

# pkg(5) name we deliver the files in (override default)
MODULE_PKGNAME=system/graphics/fbconfig
MODULE_PKGNAME_SET=yes

# Package metadata
MODULE_LICENSE = Oracle
MODULE_LICENSE_SET = yes

# Dates to show in Oracle copyright notice in fbconfig pkg license file
# Earliest & latest of the copyrights in the Oracle files in the base pkg
ORACLE_COPYRIGHT_YEARS = 1999, 2015

# Package classification (override default)
MODULE_PKGCLASS=Drivers/Display
MODULE_PKGCLASS_SET=yes

# No upstream sources, only local sources
MODULE_VERSION=src
SOURCE_TARBALL_NAME=NONE
SOURCE_TARBALL_NAME_SET=yes
ADDITIONAL_SOURCE_DIR=sun-src

# Since the Makefiles in these subdirs haven't been updated to support VPATH
# builds, just clone the whole source tree to the build tree for now.
CLONE_SRC = yes

# Man pages to apply Sun footer to & attributes to list
SUNTOUCHED_MANPAGES= # None
MODULE_STABILITY=Committed

# No configure script to run, but need to clone sources into build dirs
CONFIGURE_TARGETS = $(BUILD_DIR)
CONFIGURE_TARGETS_SET=yes

# Since we don't have a configure script, pass configure flags to make
MODULE_BUILD_ENV  = $(CONFIG_ENV) PREFIX=$(MODULE_PREFIX)
MODULE_CONFIG_ENV = LIB_LDFLAGS="$(LIB_LDFLAGS) $(ARCH_FLAGS)"

# Allow linker to skip linking to libm if compiler inlined all math functions
MODULE_LD_OPTIONS = $(ZDISCARD_UNUSED_DEP)

include ../Makefile.inc

# Install metadata for fb-specific packages too
FB_MODULES = ast mga

EXTRA_METADATA_DIRS = $(FB_MODULES:%=$(MODULE_PKG_METADATA_DIR)-fbconfig-%)

EXTRA_ATTRDATA_FILES	= $(EXTRA_METADATA_DIRS:%=%/$(ATTRDATA_FILE_NAME))
EXTRA_LICENSE_FILES	= $(EXTRA_METADATA_DIRS:%=%/$(LICENSE_FILE_NAME))
EXTRA_METADATA_TARGETS	= $(EXTRA_METADATA_DIRS) \
                          $(EXTRA_ATTRDATA_FILES) \
                          $(EXTRA_LICENSE_FILES)

install_metadata: $(EXTRA_METADATA_TARGETS)

$(EXTRA_ATTRDATA_FILES) $(EXTRA_LICENSE_FILES): $(EXTRA_METADATA_DIRS)

$(EXTRA_METADATA_DIRS):
	mkdir -p $@

# Modules for specific frame buffers are covered only by Oracle copyright
AST_LICENSE_FILE = $(MODULE_PKG_METADATA_DIR)-fbconfig-ast/$(LICENSE_FILE_NAME)
MGA_LICENSE_FILE = $(MODULE_PKG_METADATA_DIR)-fbconfig-mga/$(LICENSE_FILE_NAME)

$(AST_LICENSE_FILE):=		DEVICE_NAME = ast
$(MGA_LICENSE_FILE):=		DEVICE_NAME = mga

# Module info to show in per-device pkg license files
$(EXTRA_LICENSE_FILES):=	MODULE_TPNO_NAME = fbconfig-$(DEVICE_NAME)
$(EXTRA_LICENSE_FILES):=	MODULE_TPNO_NAME_SET = YES
$(EXTRA_LICENSE_FILES):=	MODULE_DESC = fbconfig & VTS modules for graphics devices using the $(DEVICE_NAME) driver

# Dates to show in Oracle copyright notice in per-device pkg license files
$(AST_LICENSE_FILE):=		ORACLE_COPYRIGHT_YEARS = 2009, 2013
$(MGA_LICENSE_FILE):=		ORACLE_COPYRIGHT_YEARS = 2006, 2012

