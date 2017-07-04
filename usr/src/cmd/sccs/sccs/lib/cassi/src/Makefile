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

LIBRARY =	libcassi.a
VERS =		.1

OBJECTS =	cmrcheck.o	\
		deltack.o	\
		error.o		\
		filehand.o	\
		gf.o

all:

include $(SRC)/lib/Makefile.lib

POFILE =        schilysccslibcassi.po
POFILES =       $(OBJECTS:%.o=%.po)

include ../../../../Makefile.com

LIBS =		$(LIBRARY)

all: $(LIBS)

install: all

_msg:

lint:

#
# For message catalogue files
#
_msg: $(MSGDOMAIN) $(POFILE)
	$(RM) $(MSGDOMAIN)/$(POFILE)
	$(CP) $(POFILE) $(MSGDOMAIN)

include $(SRC)/lib/Makefile.targ
