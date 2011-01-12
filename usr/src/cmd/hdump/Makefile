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
# Copyright 2011 J. Schilling.  All rights reserved.
#

PROG= hdump
ODPROG= od

ROOTLINKS= $(ROOTBIN)/$(ODPROG)
XPG4LINKS= $(ROOTXPG4BIN)/$(ODPROG)

include ../Makefile.cmd

CPPFLAGS +=	-DUSE_LARGEFILES
CPPFLAGS +=	-DSCHILY_PRINT
CPPFLAGS +=	-DNO_PRINTFLIKE=1
CPPFLAGS +=	-DUSE_NLS
CPPFLAGS +=	-DINS_BASE=\"/usr\"

LDLIBS += -lschily

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG) $(ROOTLINKS) $(XPG4LINKS)

$(ROOTLINKS):	$(ROOTPROG)
	$(RM) $@
	$(LN) $(ROOTPROG) $@

$(XPG4LINKS):	$(ROOTPROG)
	$(RM) $@
	$(LN) $(ROOTPROG) $@


clean:

lint:

include ../Makefile.targ
