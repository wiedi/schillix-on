#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.opensource.org/licenses/cddl1.txt
#

#
# Copyright 2010-2013 Jörg Schilling.  All rights reserved.
#

LIBRARY=	libshedit.a
VERS=		.1
OBJECTS=	edit.o inputc.o expand.o node.o map.o strsubs.o str.o ttymodes.o \
		fprint.o error.o fgetline.o fileopen.o fio.o comerr.o dat.o

#
# The files: comerr.c, error.c fgetline.c have been borrowed from "libschily"
# The files: bsh.h,  bshconf.h, map.h, node.h, str.h, strsubs.h and
# the files: expand.c, inputc.c, map.c, node.c, strsubs.c, ttymodes.c
# have been borrowed from "bsh"
# The files: dat.c, edit.c, fileopen.c, fio.c, fprintf.c, str.c
# are unique to "libshedit".
#

include ../../Makefile.lib
#
# libchedit must be installed in the root filesystem for /sbin/sh = Bourne Shell
# To put this library to /lib instead of /usr/lib
# uncomment the following line
include ../../Makefile.rootfs

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-lxtermcap -lschily -lc
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

#
# Note that we need to use CFLAGS += -I../include to make it appear before
# the -I$(ROOT)/usr/include that refers to the prototype system include.
#
CFLAGS +=	$(CCVERBOSE)
CFLAGS +=	-I../include		# Path to our modified stdio.h
CFLAGS64 +=	-I../include		# Path to our modified stdio.h
CPPFLAGS +=	-D_REENTRANT

CPPFLAGS +=	-DBSH			# Use Shell variant with map.c
CPPFLAGS +=	-DINTERACTIVE		# Switch on history editing code
#CPPFLAGS +=	-DBOURNE		# ???
CPPFLAGS +=	-DLIB_SHEDIT		# We compile libshedit
CPPFLAGS +=	-DUSE_LARGEFILES	# Enable large files
CPPFLAGS +=	-DNO_PRINTFLIKE=1	# Avoid gcc: "unknown conversion type"

all: $(LIBS)

lint: lintcheck

.KEEP_STATE:

include ../../Makefile.targ
