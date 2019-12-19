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

INS_BASE=	/usr
SCCS_HELP_PRE=	ccs/
SCCS_BIN_PRE=	ccs/

INCS += -I$(SRC)/cmd/sccs/sgs/inc/common
INCS += -I$(SRC)/cmd/sccs/sccs/hdr

CPPFLAGS +=	-DSCHILY_INCLUDES	# Use portability helpers
CPPFLAGS +=	-DUSE_LARGEFILES	# Largefiles on for schily/mconfig.h
CPPFLAGS +=	-DUSE_NLS		# Needed for gettext()
CPPFLAGS +=	-DINS_BASE=\"$(INS_BASE)\"
CPPFLAGS +=	-DSCCS_HELP_PRE=\"${SCCS_HELP_PRE}\"
CPPFLAGS +=	-DSCCS_BIN_PRE=\"${SCCS_BIN_PRE}\"
CPPFLAGS +=	-DSCCS_FATALHELP	# auto call to help
CPPFLAGS +=	-DUSE_RECURSIVE		# Implement -R option
CPPFLAGS +=	-DSCCS_V6_ENV		# Assume -V6 if SCCS_V6= is present
CPPFLAGS +=	-DPROVIDER=\"SchilliX-ON\"



CFLAGS +=	$(INCS)

CFLAGS +=	$(CCVERBOSE)

#
# Have all strings in the same domain (even our libs)
#
TEXT_DOMAIN = SUNW_OST_OSCMD

$(POFILE): $(POFILES)
	$(CAT) $(POFILES) > $@
