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
# Copyright 2012 J�rg Schilling.  All rights reserved.
#

include ../Makefile.master

COMMON_SUBDIRS=		\
	man1		\
	man1b		\
	man1c		\
	man1f		\
	man1m		\
	man1s		\
	man2		\
	man3		\
	man3bsm		\
	man3c		\
	man3c_db	\
	man3cfgadm	\
	man3commputil	\
	man3contract	\
	man3cpc		\
	man3curses	\
	man3dat		\
	man3devid	\
	man3devinfo	\
	man3dlpi	\
	man3dns_sd	\
	man3elf		\
	man3exacct	\
	man3ext		\
	man3fcoe	\
	man3fstyp	\
	man3gen		\
	man3gss		\
	man3head	\
	man3iscsit	\
	man3kstat	\
	man3kvm		\
	man3layout	\
	man3ldap	\
	man3lgrp	\
	man3lib		\
	man3libucb	\
	man3m		\
	man3mail	\
	man3malloc	\
	man3mlib	\
	man3mms		\
	man3mp		\
	man3mpapi	\
	man3mvec	\
	man3nsl		\
	man3nvpair	\
	man3pam		\
	man3papi	\
	man3perl	\
	man3picl	\
	man3picltree	\
	man3plot	\
	man3pool	\
	man3proc	\
	man3project	\
	man3resolv	\
	man3rmt		\
	man3rpc		\
	man3rsm		\
	man3sasl	\
	man3scf		\
	man3schily	\
	man3sec		\
	man3secdb	\
	man3sip		\
	man3slp		\
	man3snmp	\
	man3socket	\
	man3stmf	\
	man3sysevent	\
	man3tecla	\
	man3tnf		\
	man3tsol	\
	man3ucb		\
	man3uuid	\
	man3volmgt	\
	man3xcurses	\
	man3xnet	\
	man3xtsol	\
	man4		\
	man4b		\
	man5		\
	man6		\
	man7		\
	man7d		\
	man7fs		\
	man7i		\
	man7ipp		\
	man7m		\
	man7p		\
	man8		\
	man9		\
	man9e		\
	man9f		\
	man9p		\
	man9s

i386_SUBDIRS=		\


sparc_SUBDIRS=		\


all :=		TARGET = all
install :=	TARGET = install
clean :=	TARGET = clean
clobber :=	TARGET = clobber
lint :=		TARGET = lint

.KEEP_STATE:

SUBDIRS=	$(COMMON_SUBDIRS) $($(MACH)_SUBDIRS)

.PARALLEL:	$(SUBDIRS)

all install clean clobber lint: $(SUBDIRS)

$(SUBDIRS): FRC
	@if [ -f $@/Makefile  ]; then \
		cd $@; pwd; $(MAKE) $(TARGET); \
	else \
		true; \
	fi

FRC:
