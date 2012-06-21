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
# Copyright 2012 Jörg Schilling.  All rights reserved.
#

#
# Definitions common to section subdirectories for manual pages.
#
# include global definitions; SRC should be defined in the shell.
# SRC is needed until RFE 1026993 is implemented.

MANSECT=	3perl

MANFILES=				\
	Exacct\:\:Catalog.3perl		\
	Exacct\:\:File.3perl		\
	Exacct\:\:Object\:\:Group.3perl	\
	Exacct\:\:Object\:\:Item.3perl	\
	Exacct\:\:Object.3perl		\
	Exacct.3perl			\
	Kstat.3perl			\
	Lgrp.3perl			\
	Privilege.3perl			\
	Project.3perl			\
	Task.3perl			\
	Ucred.3perl

MANSO=					\

include ../Makefile.man

#
# Due to a dmake bug, escaped : and :: are put without escapes into
# the .make.state file. This would cause any but the first call to fail.
# We use a for loop for this reason.
#
install: FRC
	@for i in $(MANALL); do	\
		$(ECHO) "$(RM) $(MANDIR)/$$i; $(INS) -s -m $(FILEMODE) -f $(MANDIR) $$i"; \
		$(RM) $(MANDIR)/$$i; $(INS) -s -m $(FILEMODE) -f $(MANDIR) $$i; \
	done;
