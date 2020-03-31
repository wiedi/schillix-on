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
# Copyright 2020 Jörg Schilling.  All rights reserved.
#

MANSECT=	3rmt

MANFILES=				\
	librmt.3rmt			\
	rmtgetconn.3rmt			\
	rmtopen.3rmt			\
	rmtstatus.3rmt

MANSO=					\
	mtg2rmtg.3rmt			\
	rmtclose.3rmt			\
	rmtdebug.3rmt			\
	rmtfilename.3rmt		\
	rmtg2mtg.3rmt			\
	rmthostname.3rmt		\
	rmtinit.3rmt			\
	rmtioctl.3rmt			\
	rmtread.3rmt			\
	rmtrmt.3rmt			\
	rmtrsh.3rmt			\
	rmtseek.3rmt			\
	rmtwrite.3rmt			\
	rmtxstatus.3rmt

mtg2rmtg.3rmt			:= SOFILE = man3rmt/rmtstatus.3rmt
rmtclose.3rmt			:= SOFILE = man3rmt/rmtopen.3rmt
rmtdebug.3rmt			:= SOFILE = man3rmt/rmtgetconn.3rmt
rmtfilename.3rmt		:= SOFILE = man3rmt/rmtgetconn.3rmt
rmtg2mtg.3rmt			:= SOFILE = man3rmt/rmtstatus.3rmt
rmthostname.3rmt		:= SOFILE = man3rmt/rmtgetconn.3rmt
rmtinit.3rmt			:= SOFILE = man3rmt/rmtgetconn.3rmt
rmtioctl.3rmt			:= SOFILE = man3rmt/rmtopen.3rmt
rmtread.3rmt			:= SOFILE = man3rmt/rmtopen.3rmt
rmtrmt.3rmt			:= SOFILE = man3rmt/rmtgetconn.3rmt
rmtrsh.3rmt			:= SOFILE = man3rmt/rmtgetconn.3rmt
rmtseek.3rmt			:= SOFILE = man3rmt/rmtopen.3rmt
rmtwrite.3rmt			:= SOFILE = man3rmt/rmtopen.3rmt
rmtxstatus.3rmt			:= SOFILE = man3rmt/rmtstatus.3rmt

all: $(MANSO)

include ../Makefile.man

install: $(ROOTMANFILES)
