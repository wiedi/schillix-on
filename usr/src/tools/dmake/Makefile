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

#include		../Makefile.cmd

SUBDIRS=	Make/lib/bsd/src		\
		Make/lib/makestate/src		\
		Make/lib/mksh/src		\
		Make/lib/vroot/src		\
		Make/bin/make/common

all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint

.KEEP_STATE:

.PARALLEL:	$(SUBDIRS)

all clean clobber install lint: $(SUBDIRS)

Make/bin/make/common:	Make/lib/bsd/src	\
			Make/lib/mksh/src	\
			Make/lib/vroot/src

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
