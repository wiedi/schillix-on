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
# Copyright 2011 J�rg Schilling.  All rights reserved.
#

include ../Makefile.com

DATAFILES += depend

LICENSEFILES = $(SCHILLIX) $(SCHILY) $(LIC_IN_HDRS) $(CDDL) \
../../lib/libschily/THIRDPARTYLICENSE \
../../lib/libschily/THIRDPARTYLICENSE.fnmatch \
../../lib/libschily/THIRDPARTYLICENSE.pd \
../../lib/libschily/THIRDPARTYLICENSE.rmd160 \
../../lib/libschily/THIRDPARTYLICENSE.sha2 \
../../lib/libschily/THIRDPARTYLICENSE.sha3

.KEEP_STATE:

all: $(FILES)
install: all pkg

include ../Makefile.targ
