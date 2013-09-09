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

PROG= makeuuid
OBJS=etheraddr.o flock.o main.o sysdep.o uuid.o
SRCS=$(OBJS:%.o=%.c)
CHECKSRCS=$(SRCS:%.c=%.check)

HDRS=etheraddr.h flock.h sysdep.h uuid.h uuid_strings.h
CHECKHDRS=$(HDRS:%.h=%.check)

include ../Makefile.cmd

POFILES = $(OBJS:%.o=%.po)

CFLAGS	+= $(CCVERBOSE)

CPPFLAGS += -DEUC -D_FILE_OFFSET_BITS=64

LDLIBS += -lsocket

.KEEP_STATE:

all: $(PROG) 

$(PROG):	$(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(POFILE): $(POFILES)
	$(RM) $@
	cat $(POFILES) > $@


check:	$(SRCS)
	$(CSTYLE) -pP $(SRCS)

install: all $(ROOTPROG)

clean:
	$(RM) $(OBJS) $(POFILES)

lint:	lint_SRCS

include ../Makefile.targ
