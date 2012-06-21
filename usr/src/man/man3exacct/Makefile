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

MANSECT=	3exacct

MANFILES=				\
	ea_error.3exacct		\
	ea_open.3exacct			\
	ea_pack_object.3exacct		\
	ea_set_item.3exacct

MANSO=					\
	ea_alloc.3exacct		\
	ea_attach_to_group.3exacct	\
	ea_attach_to_object.3exacct	\
	ea_close.3exacct		\
	ea_copy_object_tree.3exacct	\
	ea_copy_object.3exacct		\
	ea_free_item.3exacct		\
	ea_free_object.3exacct		\
	ea_free.3exacct			\
	ea_get_creator.3exacct		\
	ea_get_hostname.3exacct		\
	ea_get_object_tree.3exacct	\
	ea_get_object.3exacct		\
	ea_match_object_catalog.3exacct	\
	ea_next_object.3exacct		\
	ea_previous_object.3exacct	\
	ea_set_group.3exacct		\
	ea_strdup.3exacct		\
	ea_strfree.3exacct		\
	ea_unpack_object.3exacct	\
	ea_write_object.3exacct

ea_alloc.3exacct			:= SOFILE = man3exacct/ea_set_item.3exacct
ea_attach_to_group.3exacct		:= SOFILE = man3exacct/ea_set_item.3exacct
ea_attach_to_object.3exacct		:= SOFILE = man3exacct/ea_set_item.3exacct
ea_close.3exacct			:= SOFILE = man3exacct/ea_open.3exacct
ea_copy_object_tree.3exacct		:= SOFILE = man3exacct/ea_pack_object.3exacct
ea_copy_object.3exacct			:= SOFILE = man3exacct/ea_pack_object.3exacct
ea_free_item.3exacct			:= SOFILE = man3exacct/ea_set_item.3exacct
ea_free_object.3exacct			:= SOFILE = man3exacct/ea_set_item.3exacct
ea_free.3exacct				:= SOFILE = man3exacct/ea_set_item.3exacct
ea_get_creator.3exacct			:= SOFILE = man3exacct/ea_pack_object.3exacct
ea_get_hostname.3exacct			:= SOFILE = man3exacct/ea_pack_object.3exacct
ea_get_object_tree.3exacct		:= SOFILE = man3exacct/ea_pack_object.3exacct
ea_get_object.3exacct			:= SOFILE = man3exacct/ea_pack_object.3exacct
ea_match_object_catalog.3exacct		:= SOFILE = man3exacct/ea_set_item.3exacct
ea_next_object.3exacct			:= SOFILE = man3exacct/ea_pack_object.3exacct
ea_previous_object.3exacct		:= SOFILE = man3exacct/ea_pack_object.3exacct
ea_set_group.3exacct			:= SOFILE = man3exacct/ea_set_item.3exacct
ea_strdup.3exacct			:= SOFILE = man3exacct/ea_set_item.3exacct
ea_strfree.3exacct			:= SOFILE = man3exacct/ea_set_item.3exacct
ea_unpack_object.3exacct		:= SOFILE = man3exacct/ea_pack_object.3exacct
ea_write_object.3exacct			:= SOFILE = man3exacct/ea_pack_object.3exacct

include ../Makefile.man

install: $(ROOTMANFILES)
