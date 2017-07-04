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

INCS += -I$(SRC)/cmd/sccs/sgs/inc/common
INCS += -I$(SRC)/cmd/sccs/sccs/hdr

CPPFLAGS +=	-DSCHILY_INCLUDES	# Use portability helpers
CPPFLAGS +=	-DUSE_LARGEFILES	# Largefiles on for schily/mconfig.h
CPPFLAGS +=	-DUSE_NLS		# Needed for gettext()

CFLAGS +=	$(INCS)

CFLAGS +=	$(CCVERBOSE)

#
# Have all strings in the same domain (even our libs)
#
TEXT_DOMAIN = SUNW_OST_OSCMD

$(POFILE): $(POFILES)
	$(CAT) $(POFILES) > $@
