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

MANSECT=	3devid

MANFILES=				\
	devid_get.3devid

MANSO=					\
	devid_compare.3devid		\
	devid_deviceid_to_nmlist.3devid	\
	devid_free_nmlist.3devid	\
	devid_free.3devid		\
	devid_get_minor_name.3devid	\
	devid_sizeof.3devid		\
	devid_str_decode.3devid		\
	devid_str_encode.3devid		\
	devid_str_free.3devid		\
	devid_valid.3devid

devid_compare.3devid			:= SOFILE = man3devid/devid_get.3devid
devid_deviceid_to_nmlist.3devid		:= SOFILE = man3devid/devid_get.3devid
devid_free_nmlist.3devid		:= SOFILE = man3devid/devid_get.3devid
devid_free.3devid			:= SOFILE = man3devid/devid_get.3devid
devid_get_minor_name.3devid		:= SOFILE = man3devid/devid_get.3devid
devid_sizeof.3devid			:= SOFILE = man3devid/devid_get.3devid
devid_str_decode.3devid			:= SOFILE = man3devid/devid_get.3devid
devid_str_encode.3devid			:= SOFILE = man3devid/devid_get.3devid
devid_str_free.3devid			:= SOFILE = man3devid/devid_get.3devid
devid_valid.3devid			:= SOFILE = man3devid/devid_get.3devid

include ../Makefile.man

install: $(ROOTMANFILES)
