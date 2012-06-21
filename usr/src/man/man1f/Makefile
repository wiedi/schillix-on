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

MANSECT=	1f

MANFILES=				\
	coproc.1f			\
	echo.1f				\
	fmlcut.1f			\
	fmlexpr.1f			\
	fmlgrep.1f			\
	getfrm.1f			\
	getitems.1f			\
	indicator.1f			\
	message.1f			\
	pathconv.1f			\
	readfile.1f			\
	regex.1f			\
	reinit.1f			\
	reset.1f			\
	run.1f				\
	set.1f				\
	setcolor.1f			\
	shell.1f			\
	test.1f				\
	vsig.1f

MANSO=					\
	cocheck.1f			\
	cocreate.1f			\
	codestroy.1f			\
	coreceive.1f			\
	cosend.1f			\
	longline.1f			\
	unset.1f

cocheck.1f		:= SOFILE = man1f/coproc.1f
cocreate.1f		:= SOFILE = man1f/coproc.1f
codestroy.1f		:= SOFILE = man1f/coproc.1f
coreceive.1f		:= SOFILE = man1f/coproc.1f
cosend.1f		:= SOFILE = man1f/coproc.1f
longline.1f		:= SOFILE = man1f/readfile.1f
unset.1f		:= SOFILE = man1f/set.1f

include ../Makefile.man

install: $(ROOTMANFILES)
