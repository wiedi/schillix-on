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

MANSECT=	3pam

MANFILES=				\
	pam_acct_mgmt.3pam		\
	pam_authenticate.3pam		\
	pam_chauthtok.3pam		\
	pam_get_user.3pam		\
	pam_getenv.3pam			\
	pam_getenvlist.3pam		\
	pam_open_session.3pam		\
	pam_putenv.3pam			\
	pam_set_data.3pam		\
	pam_set_item.3pam		\
	pam_setcred.3pam		\
	pam_sm_acct_mgmt.3pam		\
	pam_sm_authenticate.3pam	\
	pam_sm_chauthtok.3pam		\
	pam_sm_open_session.3pam	\
	pam_sm_setcred.3pam		\
	pam_sm.3pam			\
	pam_start.3pam			\
	pam_strerror.3pam		\
	pam.3pam

MANSO=					\
	pam_close_session.3pam		\
	pam_end.3pam			\
	pam_get_data.3pam		\
	pam_get_item.3pam		\
	pam_sm_close_session.3pam

pam_close_session.3pam			:= SOFILE = man3pam/pam_open_session.3pam
pam_end.3pam				:= SOFILE = man3pam/pam_start.3pam
pam_get_data.3pam			:= SOFILE = man3pam/pam_set_data.3pam
pam_get_item.3pam			:= SOFILE = man3pam/pam_set_item.3pam
pam_sm_close_session.3pam		:= SOFILE = man3pam/pam_sm_open_session.3pam

include ../Makefile.man

install: $(ROOTMANFILES)
