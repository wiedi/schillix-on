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
# Copyright 2016 Jörg Schilling.  All rights reserved.
#

PROG=	bosh

include ../Makefile.cmd

SUBDIRS=	$(MACH)
$(BUILD64)SUBDIRS += $(MACH64)

all	:=	TARGET = all
install	:=	TARGET = install
clean	:=	TARGET = clean
clobber	:=	TARGET = clobber
lint	:=	TARGET = lint

.KEEP_STATE:

all:	$(SUBDIRS)

clean clobber lint:	$(SUBDIRS)

install:	$(SUBDIRS)
	$(RM) $(ROOTPROG)
	$(LN) $(ISAEXEC) $(ROOTPROG)
	$(RM) $(ROOTBIN)/jbosh
	$(LN) $(ISAEXEC) $(ROOTBIN)/jbosh
	$(RM) $(ROOTBIN)/pfbosh 
	$(LN) $(ISAEXEC) $(ROOTBIN)/pfbosh
	$(RM) $(ROOTBIN)/rbosh 
	$(LN) $(ISAEXEC) $(ROOTBIN)/rbosh

$(SUBDIRS):	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

bosh.po:
	@cd $(MACH); pwd; $(MAKE) $(TARGET) _msg

FRC:

include ../Makefile.targ
