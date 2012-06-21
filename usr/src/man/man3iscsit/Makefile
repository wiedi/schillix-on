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

MANSECT=	3iscsit

MANFILES=				\
	it_config_load.3iscsit		\
	it_ini_create.3iscsit		\
	it_portal_create.3iscsit	\
	it_tgt_create.3iscsit		\
	it_tpg_create.3iscsit

MANSO=					\
	it_config_commit.3iscsit	\
	it_config_free.3iscsit		\
	it_config_setprop.3iscsit	\
	it_ini_delete.3iscsit		\
	it_ini_free.3iscsit		\
	it_ini_setprop.3iscsit		\
	it_portal_delete.3iscsit	\
	it_tgt_delete.3iscsit		\
	it_tgt_free.3iscsit		\
	it_tgt_setprop.3iscsit		\
	it_tpg_delete.3iscsit		\
	it_tpg_free.3iscsit		\
	it_tpgt_create.3iscsit		\
	it_tpgt_delete.3iscsit		\
	it_tpgt_free.3iscsit

it_config_commit.3iscsit		:= SOFILE = man3iscsit/it_config_load.3iscsit
it_config_free.3iscsit			:= SOFILE = man3iscsit/it_config_load.3iscsit
it_config_setprop.3iscsit		:= SOFILE = man3iscsit/it_config_load.3iscsit
it_ini_delete.3iscsit			:= SOFILE = man3iscsit/it_ini_create.3iscsit
it_ini_free.3iscsit			:= SOFILE = man3iscsit/it_ini_create.3iscsit
it_ini_setprop.3iscsit			:= SOFILE = man3iscsit/it_ini_create.3iscsit
it_portal_delete.3iscsit		:= SOFILE = man3iscsit/it_portal_create.3iscsit
it_tgt_delete.3iscsit			:= SOFILE = man3iscsit/it_tgt_create.3iscsit
it_tgt_free.3iscsit			:= SOFILE = man3iscsit/it_tgt_create.3iscsit
it_tgt_setprop.3iscsit			:= SOFILE = man3iscsit/it_tgt_create.3iscsit
it_tpg_delete.3iscsit			:= SOFILE = man3iscsit/it_tpg_create.3iscsit
it_tpg_free.3iscsit			:= SOFILE = man3iscsit/it_tpg_create.3iscsit
it_tpgt_create.3iscsit			:= SOFILE = man3iscsit/it_tgt_create.3iscsit
it_tpgt_delete.3iscsit			:= SOFILE = man3iscsit/it_tgt_create.3iscsit
it_tpgt_free.3iscsit			:= SOFILE = man3iscsit/it_tgt_create.3iscsit

include ../Makefile.man

install: $(ROOTMANFILES)
