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
# Copyright 2011 J. Schilling, Inc.  All rights reserved.
#

PROG= patch
OPATCH=opatch
OBJS= patch.o pch.o inp.o util.o
SRCS = $(OBJS:.o=.c)

ROOTLINKS= $(ROOTBIN)/$(OPATCH)

include ../Makefile.cmd

POFILE= patch_cmd.po
POFILES= $(SRCS:%.c=%.po)

CFLAGS += $(CCVERBOSE)

CPPFLAGS +=	-DUSE_VPRINTF
CPPFLAGS +=	-DUSE_LARGEFILES
CPPFLAGS +=	-DNO_PRINTFLIKE=1

#XGETFLAGS += -a -x $(PROG).xcl

LDLIBS += -lschily

.KEEP_STATE:

all: $(PROG)

$(PROG) : $$(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTPROG) $(ROOTLINKS)

$(ROOTLINKS):	$(ROOTPROG)
	$(RM) $@
	$(LN) $(ROOTPROG) $@


$(POFILE): $(POFILES)
	$(RM) $@
	$(CAT) $(POFILES) > $@

clean:
	$(RM) $(OBJS)

lint:	lint_PROG

include ../Makefile.targ
