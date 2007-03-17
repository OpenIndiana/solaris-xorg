# -*- Makefile -*- rules common to most X.Org doc modules
#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use subject to license terms.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, and/or sell copies of the Software, and to permit persons
# to whom the Software is furnished to do so, provided that the above
# copyright notice(s) and this permission notice appear in all copies of
# the Software and that both the above copyright notice(s) and this
# permission notice appear in supporting documentation.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
# OF THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
# HOLDERS INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL
# INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING
# FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
# NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
# WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
# 
# Except as contained in this notice, the name of a copyright holder
# shall not be used in advertising or otherwise to promote the sale, use
# or other dealings in this Software without prior written authorization
# of the copyright holder.
#
# @(#)Makefile.inc	1.2	07/01/31
#

### Makefile settings/rules common to all X.Org protocol modules

PWD:sh=pwd
TOP=$(PWD)/../..

# Not really 32 or 64-bit, but we call it 32-bit to avoid subdirs
BUILD_TYPES=32 

# Source tarball
SOURCE_TARBALL_NAME=$(DOC_NAME)-$(VERSION).tar.bz2

# Download site for source
SOURCE_URL=$(XORG_RELEASES_URL)/doc/$(SOURCE_TARBALL_NAME)

# Directory created by unpacking source
SOURCE_DIR=$(BUILD_DIR)/$(DOC_NAME)-$(VERSION)

### Include common rulesets
include $(TOP)/common/Makefile.inc

# Command line options to GNU autoconf configure script
DOC_CFG=--prefix=$(PROTODIR)$(X11_DIR) $(DOC_CONFIG_OPTS)

# Environment variable options to GNU autoconf configure script
CONFIG_ENV = PATH="$(PROTODIR)$(X11_DIR)/bin:$(PROTODIR)$(TOOLS_DIR)/bin:$$PATH"

# Environment variable options needed when building
BUILD_ENV = LD_LIBRARY_PATH="$(PROTODIR)$(X11_SERVERLIBS_DIR)" PATH="$(PROTODIR)$(X11_DIR)/bin:$(PROTODIR)$(TOOLS_DIR)/bin:$$PATH"

build_gen: $(SOURCE_DIR)/Makefile
	(cd $(SOURCE_DIR) ; $(BUILD_ENV) $(MAKE) $(MFLAGS))

# Run configure script
$(SOURCE_DIR)/Makefile: $(UNPACK_TARGET)
	(cd $(SOURCE_DIR) ; \
	 chmod a+x configure ; \
	 $(CONFIG_ENV) ./configure $(DOC_CFG) )

# Install
install_gen:: $(SOURCE_DIR)/Makefile
	(cd $(SOURCE_DIR) ; \
	 $(BUILD_ENV) $(MAKE) $(MFLAGS) install )


