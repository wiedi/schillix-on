#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License, Version 1.0 only
# (the "License").  You may not use this file except in compliance
# with the License.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.opensource.org/licenses/cddl1.txt
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
#ident	"%Z%%M%	%I%	%E% J. Schilling"
#
# Copyright 2013 J. Schilling
# Use is subject to license terms.
#

PROG= prodreg
OBJS=	prodreg.o \
	prodreg_browse.o \
	prodreg_browse_num.o \
	prodreg_info.o \
	prodreg_list.o \
	prodreg_reg.o \
	prodreg_uninst.o \
	prodreg_unreg.o \
	prodreg_util.o

SRCS=$(OBJS:%.o=%.c)

HDRS= boolean.h localized_strings.h prodreg_cli.h

REGPROG=	regconvert
REGOBJS =	Regconvert.o 
REGSRCS=$(REGOBJS:%.o=%.c)

ROOTINSTALLBIN= $(ROOT)/usr/sadm/install/bin
ROOTINSTALLPROG=$(REGPROG:%=$(ROOTINSTALLBIN)/%)

ROOTETC = 		$(ROOT)/etc
ROOTETCSECURITY =	$(ROOTETC)/security

ESDFILES =		exec_attr
ETCSECURITYDIR =	$(ESDFILES:%=$(ROOTETCSECURITY)/%.d)
ETCSECURITYDFILES =	$(ESDFILES:%=$(ROOTETCSECURITY)/%.d/SUNWwsr2)



CLOBBERFILES += $(REGPROG)

include ../Makefile.cmd

POFILE=prodreg_all.po
POFILES = $(OBJS:%.o=%.po) $(REGOBJS:%.o=%.po)

CFLAGS	+= $(CCVERBOSE)

CPPFLAGS += -DEUC -D_FILE_OFFSET_BITS=64 -DNDEBUG

LDFLAGS += $(ZIGNORE)
LDLIBS += -lsecdb -ladm -lwsreg

.KEEP_STATE:

all: $(PROG) $(REGPROG)

$(PROG):	$(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(REGPROG):	$(REGOBJS)
	$(LINK.c) $(REGOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(POFILE): $(POFILES)
	$(RM) $@
	cat $(POFILES) > $@

check:	$(SRCS) $(REGSRCS)
	$(CSTYLE) -pP $(SRCS) $(REGSRCS)

install: all $(ROOTPROG) $(ROOTINSTALLPROG) $(ETCSECURITYDFILES)

clean:
	$(RM) $(OBJS) $(REGOBJS) $(POFILES)

lint:	lint_SRCS

include ../Makefile.targ

$(ETCSECURITYDFILES): $$(@D)

$(ROOTETCSECURITY) $(ETCSECURITYDIR):
	$(INS.dir)

$(ROOTETCSECURITY)/%.d/SUNWwsr2: %
	$(INS.rename)

