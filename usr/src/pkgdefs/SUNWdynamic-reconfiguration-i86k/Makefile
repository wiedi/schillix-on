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
# Copyright 2011 Jörg Schilling.  All rights reserved.
#

include ../Makefile.com

DATAFILES += depend
CLOBBERFILES += action

ACTION_SUNWpmr=grep SUNWpmr depend > /dev/null || \
	( chmod 666 depend; \
	echo "P SUNWpmr	Power Management binaries (Root)" >> depend; \
	chmod 444 depend );

ACTION_SUNWpmu=grep SUNWpmu depend > /dev/null || \
	( chmod 666 depend; \
	echo "P SUNWpmu	Power Management binaries (Usr)" >> depend; \
	chmod 444 depend );

ACTION_SUNWdynamic-reconfiguration-i86r=grep SUNWdynamic-reconfiguration-i86r depend > /dev/null || \
	( chmod 666 depend; \
	echo "P SUNWdynamic-reconfiguration-i86r	Dynamic Reconfiguration Modules for i86pc (Root)" >> depend; \
	chmod 444 depend );

LICENSEFILES += ../../cmd/acpihpd/THIRDPARTYLICENSE

.KEEP_STATE:

all: $(FILES) action
install: all pkg

# action is a pseudotarget denoting completed work on the depend file
action: depend
	$(ACTION_SUNWpmr)
	$(ACTION_SUNWpmu)
	$(ACTION_SUNWdynamic-reconfiguration-i86r)
	touch $@

include ../Makefile.targ
