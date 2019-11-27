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

LIBRARY =	libmksh.a
VERS =		.1

OBJECTS =	dosys.o globals.o i18n.o macro.o misc.o mksh.o read.o

.KEEP_STATE:

all:

include $(SRC)/lib/Makefile.lib

include ../../../../Makefile.com

LIBS =		$(LIBRARY)
SRCDIR=		$(SRC)/cmd/sunpro/Make/lib/mksh/src

all: $(LIBS)

install: all

lint:

include $(SRC)/lib/Makefile.targ
