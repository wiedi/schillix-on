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

FILES=	ad bd cb cm cmds co de default ge he pr prs rc sc un ut va vc

CLOBBERFILES=

.PARALLEL:	$(FILES)

.KEEP_STATE:

all:

include ../../../Makefile.cmd
include ../../Makefile.com


ROOTFILES=	$(FILES:%=$(ROOTCCSLIB)/help/locale/C/%)

$(ROOTFILES) := FILEMODE = 0444

$(ROOTCCSLIB)/help/locale/C:
	$(INS.dir)

$(ROOTCCSLIB)/help/locale/C/%: %
	$(INS.file)

install: all $(ROOTCCSLIB)/help/locale/C $(ROOTFILES)

lint:

clean:

clobber: clean

_msg:

FRC:

#include ../../../Makefile.targ
