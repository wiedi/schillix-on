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

MANSECT=	3sysevent

MANFILES=				\
	sysevent_bind_handle.3sysevent	\
	sysevent_free.3sysevent		\
	sysevent_get_attr_list.3sysevent\
	sysevent_get_class_name.3sysevent\
	sysevent_get_vendor_name.3sysevent\
	sysevent_post_event.3sysevent	\
	sysevent_subscribe_event.3sysevent

MANSO=					\
	sysevent_get_pid.3sysevent	\
	sysevent_get_pub_name.3sysevent	\
	sysevent_get_seq.3sysevent	\
	sysevent_get_size.3sysevent	\
	sysevent_get_subclass_name.3sysevent\
	sysevent_get_time.3sysevent	\
	sysevent_unbind_handle.3sysevent\
	sysevent_unsubscribe_event.3sysevent

sysevent_get_pid.3sysevent		:= SOFILE = man3sysevent/sysevent_get_vendor_name.3sysevent
sysevent_get_pub_name.3sysevent		:= SOFILE = man3sysevent/sysevent_get_vendor_name.3sysevent
sysevent_get_seq.3sysevent		:= SOFILE = man3sysevent/sysevent_get_class_name.3sysevent
sysevent_get_size.3sysevent		:= SOFILE = man3sysevent/sysevent_get_class_name.3sysevent
sysevent_get_subclass_name.3sysevent	:= SOFILE = man3sysevent/sysevent_get_class_name.3sysevent
sysevent_get_time.3sysevent		:= SOFILE = man3sysevent/sysevent_get_class_name.3sysevent
sysevent_unbind_handle.3sysevent	:= SOFILE = man3sysevent/sysevent_bind_handle.3sysevent
sysevent_unsubscribe_event.3sysevent	:= SOFILE = man3sysevent/sysevent_subscribe_event.3sysevent

include ../Makefile.man

install: $(ROOTMANFILES)
