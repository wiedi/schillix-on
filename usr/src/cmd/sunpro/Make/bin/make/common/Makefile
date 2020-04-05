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
# Copyright 2017 Jörg Schilling.  All rights reserved.
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
	archconf.o	\
	version.o

.KEEP_STATE:

POFILES= $(OBJS:%.o=%.po)

all:	$(PROG)

include ../../../../../Makefile.cmd

POFILE=	sunpromake.po

include ../../../../Makefile.com

LDLIBS += ../../../lib/bsd/src/libbsd.a
LDLIBS += ../../../lib/mksh/src/libmksh.a
LDLIBS += ../../../lib/vroot/src/libvroot.a
LDLIBS += -lnsl -lc

#
# We install the following binaries:
#	/usr/bin/make
#	/usr/bin/dmake
#	/usr/xpg4/bin/make
#	/usr/ccs/bin/make
#	/usr/ccs/lib/svr4.make
#	/usr/lib/svr4.make

ROOTLINKS=	$(ROOTCCSBIN)/make		\
		$(ROOTXPG4BIN)/make		\
		$(ROOTBIN)/dmake		\
		$(ROOTCCSLIB)/svr4.make		\
		$(ROOTLIB)/svr4.make

ROOTRULES =	$(ROOTSHLIB)/make/make.rules	\
		$(ROOTSHLIB)/make/svr4.make.rules

install: all $(ROOTPROG) $(ROOTLINKS) $(ROOTRULES)

$(PROG):	$(OBJS)
	$(LINK.cc) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

#
# We need to create a hardlink as otherwise make will not be able to tell
# whether it has been called as /usr/bin/make or /usr/xpg4/bin/make.
#
$(ROOTXPG4BIN)/make: $(ROOTPROG)
	-$(RM) $@; $(LN) $(ROOTPROG) $@

$(ROOTBIN)/dmake:
	-$(RM) $@; $(SYMLINK) make $@

$(ROOTCCSBIN)/make:
	-$(RM) $@; $(SYMLINK) ../../bin/make $@

$(ROOTCCSLIB)/svr4.make:
	-$(RM) $@; $(SYMLINK) ../../bin/make $@

$(ROOTLIB)/svr4.make:
	-$(RM) $@; $(SYMLINK) ../bin/make $@

$(ROOTRULES) := FILEMODE = 0444

$(ROOTRULES): $(ROOTSHLIB)/make

$(ROOTSHLIB)/make: FRC
	$(INS.dir)

$(ROOTSHLIB)/make/%: %.file
	$(INS.rename)

lint:

clean:
	$(RM) $(OBJS)

FRC:

include ../../../../../Makefile.targ
