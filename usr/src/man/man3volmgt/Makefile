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

MANSECT=	3volmgt

MANFILES=				\
	media_findname.3volmgt		\
	media_getattr.3volmgt		\
	media_getid.3volmgt		\
	volmgt_acquire.3volmgt		\
	volmgt_check.3volmgt		\
	volmgt_feature_enabled.3volmgt	\
	volmgt_inuse.3volmgt		\
	volmgt_ownspath.3volmgt		\
	volmgt_release.3volmgt		\
	volmgt_root.3volmgt		\
	volmgt_running.3volmgt		\
	volmgt_symname.3volmgt

MANSO=					\
	media_setattr.3volmgt		\
	volmgt_symdev.3volmgt

media_setattr.3volmgt			:= SOFILE = man3volmgt/media_getattr.3volmgt
volmgt_symdev.3volmgt			:= SOFILE = man3volmgt/volmgt_symname.3volmgt

include ../Makefile.man

install: $(ROOTMANFILES)
