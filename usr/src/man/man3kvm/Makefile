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
# Copyright 2012 Jörg Schilling.  All rights reserved.
#

#
# Definitions common to section subdirectories for manual pages.
#
# include global definitions; SRC should be defined in the shell.
# SRC is needed until RFE 1026993 is implemented.

MANSECT=	3kvm

MANFILES=				\
	kvm_getu.3kvm			\
	kvm_kread.3kvm			\
	kvm_nextproc.3kvm		\
	kvm_nlist.3kvm			\
	kvm_open.3kvm			\
	kvm_read.3kvm

MANSO=					\
	kvm_close.3kvm			\
	kvm_getcmd.3kvm			\
	kvm_getproc.3kvm		\
	kvm_kwrite.3kvm			\
	kvm_setproc.3kvm		\
	kvm_uread.3kvm			\
	kvm_uwrite.3kvm			\
	kvm_write.3kvm

kvm_close.3kvm				:= SOFILE = man3kvm/kvm_open.3kvm
kvm_getcmd.3kvm				:= SOFILE = man3kvm/kvm_getu.3kvm
kvm_getproc.3kvm			:= SOFILE = man3kvm/kvm_nextproc.3kvm
kvm_kwrite.3kvm				:= SOFILE = man3kvm/kvm_kread.3kvm
kvm_setproc.3kvm			:= SOFILE = man3kvm/kvm_nextproc.3kvm
kvm_uread.3kvm				:= SOFILE = man3kvm/kvm_kread.3kvm
kvm_uwrite.3kvm				:= SOFILE = man3kvm/kvm_kread.3kvm
kvm_write.3kvm				:= SOFILE = man3kvm/kvm_read.3kvm

include ../Makefile.man

install: $(ROOTMANFILES)
