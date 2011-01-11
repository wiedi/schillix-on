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
# Copyright 2010-2011 Jörg Schilling.  All rights reserved.
#

LIBRARY=	libdeflt.a
VERS=		.1
OBJECTS=	default.o

include ../../Makefile.lib
#
# To put this library to /lib instead of /usr/lib
# uncomment the following line
#include ../../Makefile.rootfs

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-lc
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-D_REENTRANT

all: $(LIBS)

lint: lintcheck

.KEEP_STATE:

include ../../Makefile.targ
