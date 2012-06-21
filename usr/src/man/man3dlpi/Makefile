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

MANSECT=	3dlpi

MANFILES=				\
	dlpi_arptype.3dlpi		\
	dlpi_bind.3dlpi			\
	dlpi_close.3dlpi		\
	dlpi_disabnotify.3dlpi		\
	dlpi_enabmulti.3dlpi		\
	dlpi_enabnotify.3dlpi		\
	dlpi_fd.3dlpi			\
	dlpi_get_physaddr.3dlpi		\
	dlpi_iftype.3dlpi		\
	dlpi_info.3dlpi			\
	dlpi_linkname.3dlpi		\
	dlpi_mactype.3dlpi		\
	dlpi_open.3dlpi			\
	dlpi_promiscon.3dlpi		\
	dlpi_recv.3dlpi			\
	dlpi_send.3dlpi			\
	dlpi_set_physaddr.3dlpi		\
	dlpi_set_timeout.3dlpi		\
	dlpi_strerror.3dlpi		\
	dlpi_unbind.3dlpi		\
	dlpi_walk.3dlpi

MANSO=					\
	dlpi_disabmulti.3dlpi		\
	dlpi_promiscoff.3dlpi

dlpi_disabmulti.3dlpi			:= SOFILE = man3dlpi/dlpi_enabmulti.3dlpi
dlpi_promiscoff.3dlpi			:= SOFILE = man3dlpi/dlpi_promiscon.3dlpi

include ../Makefile.man

install: $(ROOTMANFILES)
