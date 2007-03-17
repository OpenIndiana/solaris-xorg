###############################################################################
#
# xf86-video-ati 1.x Makefile
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
# @(#)Makefile	1.9	07/03/07
#

# Package name used in tarballs
DRIVER_NAME=xf86-video-ati

# Version number (used in path names)
DRIVER_VERS=6.6.3

# Patches to apply to source after unpacking, in order
SOURCE_PATCHES = \
	6205248.patch 		\
	autoconfig.patch	\
	no-debug.patch		\
	autoconfig2.patch	\
	autoconfig3.patch	\
	mapfiles.patch		\
	6531742.patch

# Man pages to apply Sun footer to & attributes to list
SUNTOUCHED_MANPAGES=man/*.man
SUNTOUCH_MAN_FLAGS= \
 -a '{Architecture, x86} {Availability, SUNWxorg-graphics-ddx} {Interface Stability, Volatile}'

# Extra flags to pass to configure script
DRIVER_CONFIG_OPTS=--with-xorg-sdk-dir=$(PROTODIR)/usr/X11/include/xorg

# Paths to find libraries/modules to link with - libraries themselves listed
# in src/Makefile.am since ATI has so many submodules with different needs
DRIVER_ADD_LIBS= \
	-L$(PROTODIR)$(X11_SERVERMODS_DIR)$(ARCHLIBSUBDIR) \
	-L$(PROTODIR)$(X11_SERVERMODS_DIR)$(ARCHLIBSUBDIR)/linux \
	-L$(PROTODIR)$(X11_SERVERLIBS_DIR)$(ARCHLIBSUBDIR) \
	-R$(X11_SERVERMODS_DIR)$(ARCHLIBSUBDIR) \
	-R$(X11_SERVERMODS_DIR)/linux$(ARCHLIBSUBDIR) \
	-R$(X11_SERVERLIBS_DIR)$(ARCHLIBSUBDIR)

include ../Makefile.inc

source_gen:: $(UNPACK_TARGET)
	(cd $(SOURCE_DIR) && automake && autoconf)

install_gen::
	mkdir -p $(PROTODIR)$(X11_DOC_DIR)
	cp -p $(SOURCE_DIR)/README.ati $(SOURCE_DIR)/README.r128 \
	 $(PROTODIR)$(X11_DOC_DIR)/