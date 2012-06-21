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

MANSECT=	1c

MANFILES=				\
	ct.1c				\
	cu.1c				\
	rup.1c				\
	uucp.1c				\
	uuencode.1c			\
	uuglist.1c			\
	uustat.1c			\
	uuto.1c				\
	uux.1c

MANSO=					\
	uudecode.1c			\
	uulog.1c			\
	uuname.1c			\
	uupick.1c

uudecode.1c		:= SOFILE = man1c/uuencode.1c
uulog.1c		:= SOFILE = man1c/uucp.1c
uuname.1c		:= SOFILE = man1c/uucp.1c
uupick.1c		:= SOFILE = man1c/uuto.1c

include ../Makefile.man

install: $(ROOTMANFILES)
