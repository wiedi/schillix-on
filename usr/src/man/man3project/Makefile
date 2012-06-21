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

MANSECT=	3project

MANFILES=				\
	getprojent.3project		\
	project_walk.3project		\
	setproject.3project

MANSO=					\
	endprojent.3project		\
	fgetprojent.3project		\
	getdefaultproj.3project		\
	getprojbyid.3project		\
	getprojbyname.3project		\
	getprojidbyname.3project	\
	inproj.3project			\
	setprojent.3project

endprojent.3project			:= SOFILE = man3project/getprojent.3project
fgetprojent.3project			:= SOFILE = man3project/getprojent.3project
getdefaultproj.3project			:= SOFILE = man3project/getprojent.3project
getprojbyid.3project			:= SOFILE = man3project/getprojent.3project
getprojbyname.3project			:= SOFILE = man3project/getprojent.3project
getprojidbyname.3project		:= SOFILE = man3project/getprojent.3project
inproj.3project				:= SOFILE = man3project/getprojent.3project
setprojent.3project			:= SOFILE = man3project/getprojent.3project

include ../Makefile.man

install: $(ROOTMANFILES)
