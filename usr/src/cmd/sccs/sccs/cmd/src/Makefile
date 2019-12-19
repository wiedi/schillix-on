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

PROGS=	admin comb delta get help prs prt rmchg \
	rcs2sccs sccs sccscvt sccsdiff sccslog unget val vc what

CLOBBERFILES=	$(PROGS)

.PARALLEL:	$(PROGS)

.KEEP_STATE:

POFILES= $(PROGS:%=%.po) 

all:	$(PROGS)

include ../../../../Makefile.cmd

POFILE=	schilysccs.po

include ../../../Makefile.com

LDLIBS += ../../lib/comobj/src/libcomobj.a
LDLIBS += ../../lib/cassi/src/libcassi.a
LDLIBS += ../../lib/mpwlib/src/libmpw.a

admin :=	LDLIBS += -lschily
comb  :=	LDLIBS += -lschily
get   :=	LDLIBS += -lschily
delta :=	LDLIBS += -lschily
prs   :=	LDLIBS += -lschily
prt   :=	LDLIBS += -lschily
rmchg :=	LDLIBS += -lschily
sccs  :=	LDLIBS += -lfind -lschily
sccscvt :=	LDLIBS += -lschily
sccslog :=	LDLIBS += -lschily
unget :=	LDLIBS += -lschily
val   :=	LDLIBS += -lschily

ROOTBINS=	$(PROGS:%=$(ROOTBIN)/%)

ROOTBINLINKS1=	$(ROOTBIN)/rmdel $(ROOTBIN)/cdc

ROOTBINLINKS2=	$(ROOTBIN)/sact

ROOTBINLINKS3=	$(ROOTBIN)/sccshelp

ROOTCCSLINKS=	$(PROGS:%=$(ROOTCCSBIN)/%)	\
		$(ROOTCCSBIN)/diff		\
		$(ROOTCCSBIN)/bdiff

install: all $(ROOTBINS)			\
		$(ROOTBINLINKS1)		\
		$(ROOTBINLINKS2)		\
		$(ROOTBINLINKS3)		\
		$(ROOTCCSLINKS)

aa:
	echo $(ROOTPROG)


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

$(ROOTCCSBIN)/%:
	-$(RM) $@; $(SYMLINK) ../../bin/$(@F) $@

$(ROOTBINLINKS1):
	-$(RM) $@; $(LN) $(ROOTBIN)/rmchg $@

$(ROOTBINLINKS2):
	-$(RM) $@; $(LN) $(ROOTBIN)/unget $@

$(ROOTBINLINKS3):
	-$(RM) $@; $(LN) $(ROOTBIN)/help $@

lint:

clean:
	$(RM) $(OBJS)

FRC:

include ../../../../Makefile.targ
