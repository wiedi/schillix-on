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
include ../../Makefile.cmd.64

install: all $(ROOTPROG64)
	$(RM) $(ROOTBIN64)/jbosh
	$(LN) $(ROOTBIN64)/bosh $(ROOTBIN64)/jbosh
	$(RM) $(ROOTBIN64)/pfbosh
	$(LN) $(ROOTBIN64)/bosh $(ROOTBIN64)/pfbosh
	$(RM) $(ROOTBIN64)/rbosh
	$(LN) $(ROOTBIN64)/bosh $(ROOTBIN64)/rbosh

qqqinstall: all $(ROOTSBINPROG)
	$(RM) $(ROOTSBIN)/jbosh
	$(SYMLINK) bosh $(ROOTSBIN)/jbosh
	$(RM) $(ROOTSBIN)/pfbosh
	$(SYMLINK) bosh $(ROOTSBIN)/pfbosh
	$(RM) $(ROOTBIN)/bosh
	$(SYMLINK) ../../sbin/bosh $(ROOTBIN)/bosh
	$(RM) $(ROOTBIN)/jbosh
	$(SYMLINK) ../../sbin/bosh $(ROOTBIN)/jbosh
	$(RM) $(ROOTBIN)/pfbosh 
	$(SYMLINK) ../../sbin/bosh $(ROOTBIN)/pfbosh
	$(RM) $(ROOTBIN)/rbosh 
	$(SYMLINK) ../../sbin/bosh $(ROOTBIN)/rbosh
