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

PROG=	make
OBJS=	ar.o		\
	depvar.o 	\
	dist.o		\
	dmake.o		\
	doname.o	\
	dosys.o		\
	files.o		\
	globals.o	\
	implicit.o	\
	macro.o		\
	main.o		\
	make.o		\
	misc.o		\
	nse.o		\
	nse_printdep.o	\
	parallel.o	\
	pmake.o		\
	read.o		\
	read2.o		\
	rep.o		\
	state.o		\
	version.o

.KEEP_STATE:

SRCDIR=		$(SRC)/cmd/sunpro/Make/bin/make/common
SRCS=		$(OBJS:%.o=$(SRCDIR)/%.cc)

all:	$(PROG)

include ../../../../../Makefile.tools

include ../../../../Makefile.com

LDLIBS += ../../../lib/bsd/src/libbsd.a
LDLIBS += ../../../lib/mksh/src/libmksh.a
LDLIBS += ../../../lib/vroot/src/libvroot.a
LDLIBS += -lnsl -lc

#
# We install the following binaries:
#	/opt/onbld/bin/$(ARCH)/make
#	/opt/onbld/bin/$(ARCH)/dmake

ROOTLINKS=	$(ROOTONBLDBINMACH)/dmake

ROOTRULES =	$(ROOTONBLDSHARE)/lib/make/make.rules

install: all $(ROOTONBLDMACHPROG) $(ROOTLINKS) $(ROOTRULES)

$(PROG):	$(OBJS)
	$(LINK.cc) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

%.o:	$(SRCDIR)/%.cc
	$(COMPILE.cc) -o $@ $<

$(ROOTONBLDBINMACH)/dmake:
	-$(RM) $@; $(SYMLINK) make $@

$(ROOTRULES) := FILEMODE = 0444

$(ROOTRULES): $(ROOTONBLDSHARE)/lib/make

$(ROOTONBLDSHARE)/lib/make: FRC
	$(INS.dir)

$(ROOTONBLDSHARE)/lib/make/%: $(SRCDIR)/%.file
	$(INS.rename)

lint:

clean:
	$(RM) $(OBJS)

FRC:

include ../../../../../Makefile.targ
