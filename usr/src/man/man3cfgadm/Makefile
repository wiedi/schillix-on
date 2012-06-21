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

MANSECT=	3cfgadm

MANFILES=				\
	config_admin.3cfgadm

MANSO=					\
	config_ap_id_cmp.3cfgadm	\
	config_change_state.3cfgadm	\
	config_list_ext.3cfgadm		\
	config_list.3cfgadm		\
	config_private_func.3cfgadm	\
	config_stat.3cfgadm		\
	config_strerror.3cfgadm		\
	config_test.3cfgadm		\
	config_unload_libs.3cfgadm

config_ap_id_cmp.3cfgadm		:= SOFILE = man3cfgadm/config_admin.3cfgadm
config_change_state.3cfgadm		:= SOFILE = man3cfgadm/config_admin.3cfgadm
config_list_ext.3cfgadm			:= SOFILE = man3cfgadm/config_admin.3cfgadm
config_list.3cfgadm			:= SOFILE = man3cfgadm/config_admin.3cfgadm
config_private_func.3cfgadm		:= SOFILE = man3cfgadm/config_admin.3cfgadm
config_stat.3cfgadm			:= SOFILE = man3cfgadm/config_admin.3cfgadm
config_strerror.3cfgadm			:= SOFILE = man3cfgadm/config_admin.3cfgadm
config_test.3cfgadm			:= SOFILE = man3cfgadm/config_admin.3cfgadm
config_unload_libs.3cfgadm		:= SOFILE = man3cfgadm/config_admin.3cfgadm

include ../Makefile.man

install: $(ROOTMANFILES)
