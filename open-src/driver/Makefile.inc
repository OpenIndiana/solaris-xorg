# -*- Makefile -*- rules common to all X.Org drivers
#
# Copyright (c) 2006, 2014, Oracle and/or its affiliates. All rights reserved.
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

PWD:sh=pwd
TOP=$(PWD)/../../..

MODTYPE=driver

### Include common definitions
include $(TOP)/open-src/common/Makefile.init

# Determine from MODULE_NAME if this is a video or input driver
DRIVER_CLASS_V=$(MODULE_NAME:xf86-video-%=video)
DRIVER_CLASS=$(DRIVER_CLASS_V:xf86-input-%=input)
DRIVER_NAME=$(MODULE_NAME:xf86-$(DRIVER_CLASS)-%=%)

# Hierarchical parent for pkg names in this area
MODTYPE_PKGHIER=x11/server/xorg/driver
MODTYPE_PKGHIER_SET=yes

# Default pkg names for drivers - map xf86- to xorg- in names
MODULE_PKGTAIL_DEFAULT		= $(MODULE_PKGTAIL_SET:yes=$(POUND_SIGN))
$(MODULE_PKGTAIL_DEFAULT)	MODULE_PKGTAIL=$(MODULE_NAME:xf86-%=xorg-%)
$(MODULE_PKGTAIL_DEFAULT)	MODULE_PKGTAIL_SET=yes

# Package classification - must be a valid value recognized by pkg gui
MODULE_PKGCLASS_input		= Drivers/Other Peripherals
MODULE_PKGCLASS_video		= Drivers/Display
MODULE_PKGCLASS_DEFAULT		= $(MODULE_PKGCLASS_SET:yes=$(POUND_SIGN))
$(MODULE_PKGCLASS_DEFAULT)	MODULE_PKGCLASS=$(MODULE_PKGCLASS_$(DRIVER_CLASS))
$(MODULE_PKGCLASS_DEFAULT)	MODULE_PKGCLASS_SET=yes

# Default module description for packages in this area
MODTYPE_DESC = the $(MODULE_NAME) driver

# Additional command line options to GNU autoconf configure script
MODTYPE_CONFIG_OPTS = \
	--docdir='$(X11_DOC_DIR)' \
	--with-xorg-module-dir='$(X11_SERVERMODS_DIR)'

# Set comments in ELF binaries to help identify versions/builds
MODTYPE_ADD_BUILD_TARGETS=set-elf-comments

MODTYPE_CPPFLAGS= $(PROG_CPPFLAGS) \
	-I$(PROTODIR)/usr/include		\
	-I$(PROTODIR)/usr/include/pixman-1	\
	-I$(PROTODIR)$(XORG_SDK_INCLUDES_DIR)	\
	-I$(PROTODIR)$(X11_INCLUDES_DIR)/drm	\
	-I$(PROTODIR)$(X11_INCLUDES_DIR)/X11/dri

MODTYPE_CFLAGS=$(PROG_CFLAGS)

# Replaces @symbol_visibilty@ in xorg-server.pc, since that fails when
# some drivers are compiled with a different compiler than the server
MODTYPE_CFLAGS_suncc_common = -xldscope=hidden
MODTYPE_CFLAGS_gcc_common = -fvisibility=hidden
MODTYPE_CFLAGS += $(MODTYPE_CFLAGS_$(MODULE_COMPILER)_common)

# Many drivers fail to list -lc in their dependencies upstream, but not
# all shared objects need it, so add it, but discard if not useful.
MODTYPE_LD_SHARED_OPTIONS = $(ZDISCARD_UNUSED_DEP) -lc

# Resolve calls to functions in Xorg against the Xorg binary
XORG_EXTERNS_FLAG = -z parent=$(PROTODIR)$(X11_BIN_DIR)/Xorg
# added to shared flags by default, but can be overridden if building
# non-driver shared objects in a driver module
$(XORG_EXTERNS_IN_LD_SHARED_OPTIONS:no=$(POUND_SIGN)) \
	MODTYPE_LD_SHARED_OPTIONS += $(XORG_EXTERNS_FLAG)

# Debug builds may leave references to pixman functions from inline function 
# calls in headers that normally get optimized out in non-debug builds.
$(BUILD_DEBUG:yes=)			MODTYPE_DEBUG_LD_OPTIONS = -lpixman-1
$(BUILD_DEBUG:yes=$(POUND_SIGN))	MODTYPE_DEBUG_LD_OPTIONS =
MODTYPE_LD_OPTIONS += $(MODTYPE_DEBUG_LD_OPTIONS)

# Need to fix libtool interference with our linker flags
DELIBTOOLIZE=yes

# Mark packages as only compatible with the current Xorg minor release series,
# since Xorg tends to break ABI's in each minor release train (1.8, 1.9, etc.)
MODTYPE_ATTRDATA_COMMANDS = @$(START_CMD_ECHO) ; \
	print "depend fmri=pkg:/x11/server/xorg@$(MIN_XSERVER_VERS) type=optional" >> $@ ; \
	print "depend fmri=pkg:/x11/server/xorg@$(NEXT_XSERVER_VERS) type=exclude" >> $@

### Include common rules
include $(TOP)/open-src/common/Makefile.inc
