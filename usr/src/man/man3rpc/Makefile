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

MANSECT=	3rpc

MANFILES=				\
	rstat.3rpc			\
	rusers.3rpc			\
	rwall.3rpc

MANSO=					\
	havedisk.3rpc			\
	rnusers.3rpc

havedisk.3rpc				:= SOFILE = man3rpc/rstat.3rpc
rnusers.3rpc				:= SOFILE = man3rpc/rusers.3rpc

include ../Makefile.man

install: $(ROOTMANFILES)
