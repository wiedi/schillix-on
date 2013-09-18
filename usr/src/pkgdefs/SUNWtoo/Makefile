#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
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
#ident	"%Z%%M%	%I%	%E% SMI"
#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

include ../Makefile.com

DATAFILES += depend
CLOBBERFILES += action

ACTION_SCHILYsystem-library-schily-root=grep SCHILYsystem-library-schily-root depend > /dev/null || \
	( chmod 666 depend; \
	echo "P SCHILYsystem-library-schily-root	Schily System Libraries (Root)" >> depend; \
	chmod 444 depend );

ACTION_SCHILYsystem-library-schily=grep 'SCHILYsystem-library-schily	' depend > /dev/null || \
	( chmod 666 depend; \
	echo "P SCHILYsystem-library-schily	Schily System Libraries (Usr)" >> depend; \
	chmod 444 depend );


.KEEP_STATE:

LICENSEFILES += $(ATT) $(OSBL)

all: $(FILES) action
install: all pkg

# action is a pseudotarget denoting completed work on the depend file
action: depend
	$(ACTION_SCHILYsystem-library-schily-root)
	$(ACTION_SCHILYsystem-library-schily)
	touch $@

include ../Makefile.targ
