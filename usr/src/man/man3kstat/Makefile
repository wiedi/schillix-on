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

#
# Definitions common to section subdirectories for manual pages.
#
# include global definitions; SRC should be defined in the shell.
# SRC is needed until RFE 1026993 is implemented.

MANSECT=	3kstat

MANFILES=				\
	kstat_chain_update.3kstat	\
	kstat_lookup.3kstat		\
	kstat_open.3kstat		\
	kstat_read.3kstat		\
	kstat.3kstat

MANSO=					\
	kstat_close.3kstat		\
	kstat_data_lookup.3kstat	\
	kstat_write.3kstat

kstat_close.3kstat			:= SOFILE = man3kstat/kstat_open.3kstat
kstat_data_lookup.3kstat		:= SOFILE = man3kstat/kstat_lookup.3kstat
kstat_write.3kstat			:= SOFILE = man3kstat/kstat_read.3kstat

include ../Makefile.man

install: $(ROOTMANFILES)
