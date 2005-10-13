#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License, Version 1.0 only
# (the "License").  You may not use this file except in compliance
# with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#
# lib/libmd5_psr/Makefile.com
#

#
# Create default so empty rules don't confuse make
#
CLASS= 32

LIBRARY= libmd5_psr.a
VERS= .1

OBJECTS= md5.o
COMMON= $(SRC)/common/crypto/md5

include $(SRC)/lib/Makefile.lib
include $(SRC)/Makefile.psm

#
# Macros to help build the shared object
#
LIBS= $(DYNLIB)
DYNFLAGS += $(BDIRECT)
LDLIBS += -lc
CPPFLAGS += -D__RESTRICT

#
# Macros for the mapfile. Other makefiles need to include this file
# after setting MAPDIR
#
MAPFILE=	$(MAPDIR)/mapfile-$(PLATFORM)
DYNFLAGS +=	-M$(MAPFILE)
CLOBBERFILES +=	$(MAPFILE)

#
# Used when building links in /platform/$(PLATFORM)/lib 
#
LINKED_PLATFORMS	= SUNW,Ultra-2
LINKED_PLATFORMS	+= SUNW,Ultra-4
LINKED_PLATFORMS	+= SUNW,Ultra-5_10
LINKED_PLATFORMS	+= SUNW,Ultra-30
LINKED_PLATFORMS	+= SUNW,Ultra-60
LINKED_PLATFORMS	+= SUNW,Ultra-80
LINKED_PLATFORMS	+= SUNW,Ultra-250
LINKED_PLATFORMS	+= SUNW,Ultra-Enterprise
LINKED_PLATFORMS	+= SUNW,Ultra-Enterprise-10000
LINKED_PLATFORMS	+= SUNW,UltraAX-i2
LINKED_PLATFORMS	+= SUNW,UltraSPARC-IIi-Netract
LINKED_PLATFORMS	+= SUNW,UltraSPARC-IIe-NetraCT-40
LINKED_PLATFORMS	+= SUNW,UltraSPARC-IIe-NetraCT-60
LINKED_PLATFORMS	+= SUNW,Sun-Blade-100
LINKED_PLATFORMS	+= SUNW,Sun-Blade-1000
LINKED_PLATFORMS	+= SUNW,Sun-Blade-1500
LINKED_PLATFORMS	+= SUNW,Sun-Blade-2500
LINKED_PLATFORMS	+= SUNW,A70
LINKED_PLATFORMS	+= SUNW,Sun-Fire
LINKED_PLATFORMS	+= SUNW,Sun-Fire-V240
LINKED_PLATFORMS	+= SUNW,Sun-Fire-V250
LINKED_PLATFORMS	+= SUNW,Sun-Fire-V440
LINKED_PLATFORMS	+= SUNW,Sun-Fire-280R
LINKED_PLATFORMS	+= SUNW,Sun-Fire-15000
LINKED_PLATFORMS	+= SUNW,Sun-Fire-880
LINKED_PLATFORMS	+= SUNW,Sun-Fire-480R
LINKED_PLATFORMS	+= SUNW,Sun-Fire-V890
LINKED_PLATFORMS	+= SUNW,Sun-Fire-V490
LINKED_PLATFORMS	+= SUNW,Serverblade1
LINKED_PLATFORMS	+= SUNW,Netra-T12
LINKED_PLATFORMS	+= SUNW,Netra-T4
LINKED_PLATFORMS	+= SUNW,Netra-CP2300
LINKED_PLATFORMS	+= SUNW,Netra-CP3010

.KEEP_STATE:
