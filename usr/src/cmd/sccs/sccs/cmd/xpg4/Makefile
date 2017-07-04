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

VPATH=	.:../src

PROGS=	get.xpg4 sccs.xpg4

CLOBBERFILES=	$(PROGS)

.PARALLEL:	$(PROGS)

.KEEP_STATE:

all:	$(PROGS)

include ../../../../Makefile.cmd
include ../../../Makefile.com

INS_BASE=	/usr

CPPFLAGS +=	-DUSE_RECURSIVE
CPPFLAGS +=	-DUSE_NLS
CPPFLAGS +=	-DINS_BASE=\"$(INS_BASE)\"
CPPFLAGS +=	-DSCCS_FATALHELP		# auto call to help
CPPFLAGS +=	-DSCCS_V6_ENV			# Assume -V6 if SCCS_V6= is present
CPPFLAGS +=	-DPROVIDER=\"SchilliX\"
CPPFLAGS +=	-DXPG4

LDLIBS += ../../lib/comobj/src/libcomobj.a
LDLIBS += ../../lib/cassi/src/libcassi.a
LDLIBS += ../../lib/mpwlib/src/libmpw.a

get.xpg4   :=	LDLIBS += -lschily
sccs.xpg4  :=	LDLIBS += -lfind -lschily

ROOTXPG4BINS=	$(PROGS:%.xpg4=$(ROOTXPG4BIN)/%)

install: all $(ROOTXPG4BINS)

VERSIONF=	../../hdr/version.c

LOCALIZE=	\
	SVERS=`$(CC) -E $(INCS) $(CPPFLAGS) $(VERSIONF) | grep '^version' | awk '{ print $$2 }' | sed 's/"//g'`\
	VDATE=`$(CC) -E $(INCS) $(CPPFLAGS) $(VERSIONF) | grep '^vdate' | awk '{ print $$2 }' | sed 's/"//g'`\
	SPROV=`$(CC) -E $(INCS) $(CPPFLAGS) $(VERSIONF) | grep '^provider' | awk '{ print $$2 }' | sed 's/"//g'`\
	SHOST=`$(CC) -E $(INCS) $(CPPFLAGS) $(VERSIONF) | grep '^host_sub' | awk '{ print $$2 }' | sed 's/"//g'`\
	export SVERS SPROV;\
	sed "s/VERSION/$$SVERS/;s,VDATE,$$VDATE,;s/PROVIDER/$$SPROV/;s/HOST_SUB/$$SHOST/;s,INS_BASE,$(INS_BASE)," $^ > $@ ; :

sccsdiff: sccsdiff.sh
	$(LOCALIZE)

rcs2sccs: rcs2sccs.sh
	$(LOCALIZE)

lint:

clean:
	$(RM) $(OBJS)

POFILE=

clobber: clean
	-$(RM) $(PROG) $(CLOBBERFILES)

_msg:

FRC:

#include ../../../../Makefile.targ
