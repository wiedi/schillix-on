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
# Copyright 2010-2011 J. Schilling, Inc.  All rights reserved.
#

PROG= cpp
OBJS= cpp.o cpy.o

include ../Makefile.cmd

CFLAGS += $(CCVERBOSE)

CPPFLAGS +=	-Dunix=1		# Compile for UNIX environment
CPPFLAGS +=	-DFLEXNAMES		# Allow long identifier names
CPPFLAGS +=	-DSUN_SAYLINE		# Add enter/leave note to # line
CPPFLAGS +=	-DUSE_LARGEFILES
CPPFLAGS +=	-DNO_PRINTFLIKE=1

LDLIBS += -lschily

XGETFLAGS += -a -x $(PROG).xcl

.KEEP_STATE:

all: $(PROG)

$(PROG) : $$(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

ROOTCCSLIB=	$(ROOT)/usr/ccs/lib
ROOTCCSLIBPROG=	$(PROG:%=$(ROOTCCSLIB)/%) 
ROOTFS_LIBDIR=	$(ROOT)/lib
ROOTFS_LIBPROG=	$(PROG:%=$(ROOTFS_LIBDIR)/%)

install: all $(ROOTLIBPROG) $(ROOTCCSLIBPROG) $(ROOTFS_LIBPROG)

$(ROOTCCSLIBPROG):
	$(RM) $(ROOTCCSLIBPROG)
	$(SYMLINK) ../../lib/$(PROG) $(ROOTCCSLIBPROG)

$(ROOTFS_LIBPROG):
	$(RM) $(ROOTFS_LIBPROG)
	$(SYMLINK) ../usr/lib/$(PROG) $(ROOTFS_LIBPROG)

clean:
	$(RM) $(OBJS)

lint:	lint_PROG

include ../Makefile.targ
