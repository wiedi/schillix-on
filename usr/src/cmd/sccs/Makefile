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

include		../Makefile.cmd

SUBDIRS=	sccs/cmd/src			\
		sccs/cmd/xpg4			\
		sccs/help.d			\
		sccs/lib/cassi/src		\
		sccs/lib/comobj/src		\
		sccs/lib/mpwlib/src

all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint
_msg:=          TARGET= _msg

.KEEP_STATE:

.PARALLEL:	$(SUBDIRS)

sccs/cmd/src sccs/cmd/xpg4:			\
			sccs/lib/cassi/src	\
			sccs/lib/comobj/src	\
			sccs/lib/mpwlib/src

all clean clobber install lint _msg: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
