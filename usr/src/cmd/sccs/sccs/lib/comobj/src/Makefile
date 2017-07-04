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

LIBRARY =	libcomobj.a
VERS =		.1

OBJECTS =	auxf.o \
		bulk.o \
		cdata.o change_ab.o change_ba.o chkid.o chksid.o cmpdate.o \
		date_ab.o date_ba.o date_bal.o \
		del_ab.o \
		del_ba.o dodelt.o dofile.o dohist.o doie.o dolist.o dometa.o \
		dtime.o encode.o \
		eqsid.o flushto.o fmterr.o getline.o getser.o \
		help.o \
		idsubst.o \
		lhash.o logname.o newsid.o \
		newstats.o permiss.o pf_ab.o putline.o putmeta.o \
		rdmod.o \
		sethome.o setinitpath.o setup.o sid_ab.o \
		sid_ba.o sidext_ab.o sidext_ba.o sidtoser.o sinit.o \
		stats_ab.o strptim.o \
		ssum.o \
		urand_ab.o urand_ba.o usum.o \
		xgmtime.o xlocaltime.o xmktime.o xtzset.o

.KEEP_STATE:

all:

include $(SRC)/lib/Makefile.lib

POFILE =        schilysccslibcomobj.po
POFILES =       $(OBJECTS:%.o=%.po)

include ../../../../Makefile.com

LIBS =		$(LIBRARY)

all: $(LIBS)

install: all

_msg:

lint:

#
# For message catalogue files
#
_msg: $(MSGDOMAIN) $(POFILE)
	$(RM) $(MSGDOMAIN)/$(POFILE)
	$(CP) $(POFILE) $(MSGDOMAIN)

include $(SRC)/lib/Makefile.targ
