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

MANSECT=	3mms

MANFILES=				\
	mms_init.3mms			\
	mms_rsp_type.3mms		\
	mms_send_cmd.3mms

MANSO=					\
	mms_agoodbye.3mms		\
	mms_ainit.3mms			\
	mms_free_rsp.3mms		\
	mms_get_attribute.3mms		\
	mms_get_str.3mms		\
	mms_get_tree.3mms		\
	mms_goodbye.3mms		\
	mms_handle_err_rsp.3mms		\
	mms_hello.3mms			\
	mms_read_response.3mms		\
	mms_send_acmd.3mms

mms_agoodbye.3mms			:= SOFILE = man3mms/mms_init.3mms
mms_ainit.3mms				:= SOFILE = man3mms/mms_init.3mms
mms_free_rsp.3mms			:= SOFILE = man3mms/mms_send_cmd.3mms
mms_get_attribute.3mms			:= SOFILE = man3mms/mms_rsp_type.3mms
mms_get_str.3mms			:= SOFILE = man3mms/mms_rsp_type.3mms
mms_get_tree.3mms			:= SOFILE = man3mms/mms_rsp_type.3mms
mms_goodbye.3mms			:= SOFILE = man3mms/mms_init.3mms
mms_handle_err_rsp.3mms			:= SOFILE = man3mms/mms_rsp_type.3mms
mms_hello.3mms				:= SOFILE = man3mms/mms_init.3mms
mms_read_response.3mms			:= SOFILE = man3mms/mms_send_cmd.3mms
mms_send_acmd.3mms			:= SOFILE = man3mms/mms_send_cmd.3mms

include ../Makefile.man

install: $(ROOTMANFILES)
