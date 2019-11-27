#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright 2017-2019 Jörg Schilling.  All rights reserved.
#

include $(SRC)/tools/Makefile.tools

LIBRARY=	libmakestate.a
VERS=		.1
OBJECTS=	ld_file.o lock.o

.KEEP_STATE:

include $(SRC)/lib/Makefile.lib
include $(SRC)/tools/dmake/Makefile.com
#
# To put this library to /lib instead of /usr/lib
# uncomment the following line
#include ../../Makefile.rootfs

LIBS =		$(DYNLIB)
LDLIBS +=	-lc
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)
SRCDIR=		$(SRC)/cmd/sunpro/Make/lib/makestate/src

CPPFLAGS +=	-D_REENTRANT
CPPFLAGS +=     -DNO_LARGEFILES        # Largefiles not possible with libelf

FILEMODE= 755

all: $(LIBS)

lint:

.KEEP_STATE:

$(ROOTONBLDLIBMACH)/%: %
	$(INS.file)

$(ROOTONBLDLIBMACH64)/%: %
	$(INS.file)

include $(SRC)/lib/Makefile.targ
