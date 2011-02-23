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

ACTION_SUNWperl510core=grep SUNWperl510core depend > /dev/null || \
	( chmod 666 depend; \
	echo "P SUNWperl510core	Perl 5.10 (core)" >> depend; \
	chmod 444 depend );

ACTION_SUNWperl510usr=grep SUNWperl510usr depend > /dev/null || \
	( chmod 666 depend; \
	echo "P SUNWperl510usr	Perl 5.10 (non-core)" >> depend; \
	chmod 444 depend );

.KEEP_STATE:

LICENSEFILES += ../../cmd/perl/THIRDPARTYLICENSE
CDDL=

all: $(FILES) action
install: all pkg

# action is a pseudotarget denoting completed work on the depend file
action: depend
	$(ACTION_SUNWperl510core)
	$(ACTION_SUNWperl510usr)
	touch $@

include ../Makefile.targ
