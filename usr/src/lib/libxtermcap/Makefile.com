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

LIBRARY=	libxtermcap.a
VERS=		.1
OBJECTS=	tgetent.o tgoto.o tputs.o

include ../../Makefile.lib
#
# libxtermcap must be installed in the root filesystem for /sbin/sh = Bourne Shell
# To put this library to /lib instead of /usr/lib
# uncomment the following line
include ../../Makefile.rootfs

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-lc
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-D_REENTRANT

CPPFLAGS +=	-DNO_LIBSCHILY	# Do not enforce libschily just for geterrno()

all: $(LIBS)

lint: lintcheck

.KEEP_STATE:

include ../../Makefile.targ
