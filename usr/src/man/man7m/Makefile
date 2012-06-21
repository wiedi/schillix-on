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

MANSECT=	7m

MANFILES=				\
	bufmod.7m			\
	connld.7m			\
	kb.7m				\
	ldterm.7m			\
	pckt.7m				\
	pfmod.7m			\
	pipemod.7m			\
	ptem.7m				\
	sppptun.7m			\
	timod.7m			\
	tirdwr.7m			\
	ttcompat.7m			\
	usb_ah.7m			\
	usbkbm.7m			\
	usbms.7m			\
	vuidmice.7m

MANSO=					\
	vuid2ps2.7m			\
	vuid3ps2.7m			\
	vuidm3p.7m			\
	vuidm4p.7m			\
	vuidm5p.7m

vuid2ps2.7m				:= SOFILE = man7m/vuidmice.7m
vuid3ps2.7m				:= SOFILE = man7m/vuidmice.7m
vuidm3p.7m				:= SOFILE = man7m/vuidmice.7m
vuidm4p.7m				:= SOFILE = man7m/vuidmice.7m
vuidm5p.7m				:= SOFILE = man7m/vuidmice.7m

include ../Makefile.man

install: $(ROOTMANFILES)
