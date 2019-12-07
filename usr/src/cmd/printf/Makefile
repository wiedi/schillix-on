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
# Copyright 2019 J. Schilling, Inc.  All rights reserved.
#

PROG=	printf
OBJS1=	misc.o
OBJS=	$(OBJS1) printf.o
SRCS=	$(OBJS1:%.o=%.c) ../bosh/printf.c

include ../Makefile.cmd

#
# printf.c is not needed as it is handled in bosh already
#
POFILE= printf_cmd.po
POFILES= $(OBJS1:%.o=%.po)

CFLAGS += $(CCVERBOSE)

#
# We need dyn arrays
#
C99MODE=	-xc99=%all
C99LMODE=	-Xc99=%all

CPPFLAGS +=	-I.			# For local defs.h
CPPFLAGS +=	-DUSE_LARGEFILES
CPPFLAGS +=	-DDO_SYSPRINTF_FLOAT	# Include float support in "printf"
CPPFLAGS +=	-DHAVE_PRINTF_A		# Include %a float support in "printf"

#
# The standalone printf(1) may use stdio and thus does not need format()
# from libschily
#
#LDLIBS += -lschily

XGETFLAGS += -a -x $(PROG).xcl

.KEEP_STATE:

all: $(PROG)

$(PROG) : $$(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

%.o:	../bosh/%.c
	$(COMPILE.c) $(OUTPUT_OPTION) $<
	$(POST_PROCESS_O)

install: all $(ROOTPROG)

$(POFILE): $(POFILES)
	$(RM) $@
	$(CAT) $(POFILES) > $@

clean:
	$(RM) $(OBJS)

lint:	lint_PROG

include ../Makefile.targ
