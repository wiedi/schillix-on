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

include ../Makefile.com

install: all $(ROOTSBINPROG)
	$(RM) $(ROOTSBIN)/sh
	$(SYMLINK) bosh $(ROOTSBIN)/sh
	$(RM) $(ROOTSBIN)/jbosh
	$(SYMLINK) bosh $(ROOTSBIN)/jbosh
	$(RM) $(ROOTSBIN)/pfbosh
	$(SYMLINK) bosh $(ROOTSBIN)/pfbosh
	$(RM) $(ROOTSBIN)/jsh
	$(SYMLINK) sh $(ROOTSBIN)/jsh
	$(RM) $(ROOTSBIN)/pfsh
	$(SYMLINK) sh $(ROOTSBIN)/pfsh
	$(RM) $(ROOTBIN32)/bosh
	$(SYMLINK) ../../../sbin/bosh $(ROOTBIN32)/bosh
	$(RM) $(ROOTBIN32)/jbosh
	$(SYMLINK) ../../../sbin/bosh $(ROOTBIN32)/jbosh
	$(RM) $(ROOTBIN32)/pfbosh
	$(SYMLINK) ../../../sbin/bosh $(ROOTBIN32)/pfbosh
	$(RM) $(ROOTBIN32)/rbosh
	$(SYMLINK) ../../../sbin/bosh $(ROOTBIN32)/rbosh
