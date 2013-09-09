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
# Copyright 2013 Jörg Schilling.  All rights reserved.
#

include ../Makefile.com

BASEDIR=/usr

DATAFILES += depend
CLEANFILES += action
CLOBBERFILES += action

ACTION_SUNWproduct-registry-root=grep SUNWproduct-registry-root depend > /dev/null || \
	( chmod 666 depend; \
	echo "P SUNWproduct-registry-root	Solaris Product Registry runtime support (root)" >> depend; \
	chmod 444 depend );


LICENSEFILES += $(SCHILY) ../../cmd/makeuuid/THIRDPARTYLICENSE

.KEEP_STATE:

all: $(FILES) action
install: all pkg

# action is a pseudotarget denoting completed work on the depend file
action: depend
	$(ACTION_SUNWproduct-registry-root)
	touch $@

include ../Makefile.targ
