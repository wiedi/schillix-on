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
# Copyright 2010-2019 Jörg Schilling.  All rights reserved.
#

LIBRARY=	libfind.a
VERS=		.3
OBJECTS=	find.o walk.o fetchdir.o cmpdir.o find_misc.o find_list.o \
		find_main.o idcache.o ptime.o

include ../../Makefile.lib
#
# To put this library to /lib instead of /usr/lib
# uncomment the following line
include ../../Makefile.rootfs

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-lschily -lc
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-D_REENTRANT

CPPFLAGS +=      -DUSE_LARGEFILES
CPPFLAGS +=      -DUSE_ACL
CPPFLAGS +=      -DUSE_XATTR
CPPFLAGS +=      -DUSE_NLS
CPPFLAGS +=      -DUSE_DGETTEXT			# _() -> dgettext()
CPPFLAGS +=      -DSCHILY_PRINT


all: $(LIBS)

lint: lintcheck

.KEEP_STATE:

include ../../Makefile.targ
