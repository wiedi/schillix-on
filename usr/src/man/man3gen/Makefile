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

MANSECT=	3gen

MANFILES=				\
	bgets.3gen			\
	bufsplit.3gen			\
	copylist.3gen			\
	gmatch.3gen			\
	isencrypt.3gen			\
	mkdirp.3gen			\
	p2open.3gen			\
	pathfind.3gen			\
	regexpr.3gen			\
	strccpy.3gen			\
	strfind.3gen

MANSO=					\
	advance.3gen			\
	compile.3gen			\
	p2close.3gen			\
	rmdirp.3gen			\
	step.3gen			\
	str.3gen			\
	strcadd.3gen			\
	streadd.3gen			\
	strecpy.3gen			\
	strrspn.3gen			\
	strtrns.3gen

advance.3gen				:= SOFILE = man3gen/regexpr.3gen
compile.3gen				:= SOFILE = man3gen/regexpr.3gen
p2close.3gen				:= SOFILE = man3gen/p2open.3gen
rmdirp.3gen				:= SOFILE = man3gen/mkdirp.3gen
step.3gen				:= SOFILE = man3gen/regexpr.3gen
str.3gen				:= SOFILE = man3gen/strfind.3gen
strcadd.3gen				:= SOFILE = man3gen/strccpy.3gen
streadd.3gen				:= SOFILE = man3gen/strccpy.3gen
strecpy.3gen				:= SOFILE = man3gen/strccpy.3gen
strrspn.3gen				:= SOFILE = man3gen/strfind.3gen
strtrns.3gen				:= SOFILE = man3gen/strfind.3gen

include ../Makefile.man

install: $(ROOTMANFILES)
