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

MANSECT=	3commputil

MANFILES=				\
	sdp_add_origin.3commputil	\
	sdp_clone_session.3commputil	\
	sdp_delete_all_field.3commputil	\
	sdp_delete_media.3commputil	\
	sdp_find_attribute.3commputil	\
	sdp_find_media_rtpmap.3commputil\
	sdp_find_media.3commputil	\
	sdp_new_session.3commputil	\
	sdp_parse.3commputil		\
	sdp_session_to_str.3commputil

MANSO=					\
	sdp_add_attribute.3commputil	\
	sdp_add_bandwidth.3commputil	\
	sdp_add_connection.3commputil	\
	sdp_add_email.3commputil	\
	sdp_add_information.3commputil	\
	sdp_add_key.3commputil		\
	sdp_add_media.3commputil	\
	sdp_add_name.3commputil		\
	sdp_add_phone.3commputil	\
	sdp_add_repeat.3commputil	\
	sdp_add_time.3commputil		\
	sdp_add_uri.3commputil		\
	sdp_add_zone.3commputil		\
	sdp_delete_all_media_field.3commputil\
	sdp_delete_attribute.3commputil	\
	sdp_free_session.3commputil

sdp_add_attribute.3commputil		:= SOFILE = man3commputil/sdp_add_origin.3commputil
sdp_add_bandwidth.3commputil		:= SOFILE = man3commputil/sdp_add_origin.3commputil
sdp_add_connection.3commputil		:= SOFILE = man3commputil/sdp_add_origin.3commputil
sdp_add_email.3commputil		:= SOFILE = man3commputil/sdp_add_origin.3commputil
sdp_add_information.3commputil		:= SOFILE = man3commputil/sdp_add_origin.3commputil
sdp_add_key.3commputil			:= SOFILE = man3commputil/sdp_add_origin.3commputil
sdp_add_media.3commputil		:= SOFILE = man3commputil/sdp_add_origin.3commputil
sdp_add_name.3commputil			:= SOFILE = man3commputil/sdp_add_origin.3commputil
sdp_add_phone.3commputil		:= SOFILE = man3commputil/sdp_add_origin.3commputil
sdp_add_repeat.3commputil		:= SOFILE = man3commputil/sdp_add_origin.3commputil
sdp_add_time.3commputil			:= SOFILE = man3commputil/sdp_add_origin.3commputil
sdp_add_uri.3commputil			:= SOFILE = man3commputil/sdp_add_origin.3commputil
sdp_add_zone.3commputil			:= SOFILE = man3commputil/sdp_add_origin.3commputil
sdp_delete_all_media_field.3commputil	:= SOFILE = man3commputil/sdp_delete_all_field.3commputil
sdp_delete_attribute.3commputil		:= SOFILE = man3commputil/sdp_delete_media.3commputil
sdp_free_session.3commputil		:= SOFILE = man3commputil/sdp_new_session.3commputil

include ../Makefile.man

install: $(ROOTMANFILES)
