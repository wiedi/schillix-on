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
CLOBBERFILES += action

ACTION_SUNWsmagt=grep SUNWsmagt depend > /dev/null || \
	( chmod 666 depend; \
	echo "P SUNWsmagt	System Management Agent files and libraries" >> depend; \
	chmod 444 depend );

ACTION_SUNWsmf-snmp-notifyr=grep SUNWsmf-snmp-notifyr depend > /dev/null || \
	( chmod 666 depend; \
	echo "P SUNWsmf-snmp-notifyr	SNMP Notification Daemon for System Events (Root)" >> depend; \
	chmod 444 depend );


.KEEP_STATE:

all: $(FILES) action
install: all pkg

# action is a pseudotarget denoting completed work on the depend file
action: depend
	$(ACTION_SUNWsmagt)
	$(ACTION_SUNWsmf-snmp-notifyr)
	touch $@

include ../Makefile.targ
