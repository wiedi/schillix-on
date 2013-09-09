#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License, Version 1.0 only
# (the "License").  You may not use this file except in compliance
# with the License.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.opensource.org/licenses/cddl1.txt
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
#ident	"%Z%%M%	%I%	%E% J. Schilling"
#
# Copyright 2013 J. Schilling
# Use is subject to license terms.
#

include $(SRC)/lib/Makefile.lib

HDRS=		wsreg.h
HDRDIR=		.

SUBDIRS=	$(MACH)

POFILE =	libwsreg.po
MSGFILES =	wsreg.c cluster_file_io.c
POFILES =	$(MSGFILES:%.c=%.po)

all	:= TARGET = all
clean	:= TARGET = clean
clobber	:= TARGET = clobber
check	:= TARGET = check
debug	:= TARGET = debug
install	:= TARGET = install
install_h := TARGET = install_h
lint	:= TARGET = lint
_msg	:= TARGET = _msg

.KEEP_STATE:

.PARALLEL: $(SUBDIRS)

all clean clobber debug lint: $(SUBDIRS)

install: $(ROOTDIRS) $(SUBDIRS)

check:		$(CHECKHDRS)
install_h:	$(ROOTHDRS)

$(POFILE):	$(POFILES)
	$(RM) $@
	$(CAT) $(POFILES) > $@

_msg:		$(MSGDOMAINPOFILE)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

include $(SRC)/lib/Makefile.targ
include $(SRC)/Makefile.msg.targ
