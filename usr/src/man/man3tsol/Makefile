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

MANSECT=	3tsol

MANFILES=				\
	blcompare.3tsol			\
	blminmax.3tsol			\
	bltocolor.3tsol			\
	bltos.3tsol			\
	btohex.3tsol			\
	getdevicerange.3tsol		\
	getpathbylabel.3tsol		\
	getplabel.3tsol			\
	getuserrange.3tsol		\
	getzonelabelbyid.3tsol		\
	getzonerootbyid.3tsol		\
	hextob.3tsol			\
	label_to_str.3tsol		\
	labelbuilder.3tsol		\
	labelclipping.3tsol		\
	m_label.3tsol			\
	sbltos.3tsol			\
	setflabel.3tsol			\
	stobl.3tsol			\
	str_to_label.3tsol		\
	tsol_getrhtype.3tsol

MANSO=					\
	bcleartoh_r.3tsol		\
	bcleartoh.3tsol			\
	bcleartos.3tsol			\
	bldominates.3tsol		\
	blequal.3tsol			\
	blinrange.3tsol			\
	blmaximum.3tsol			\
	blminimum.3tsol			\
	blstrictdom.3tsol		\
	bltocolor_r.3tsol		\
	bsltoh_r.3tsol			\
	bsltoh.3tsol			\
	bsltos.3tsol			\
	getzoneidbylabel.3tsol		\
	getzonelabelbyname.3tsol	\
	getzonerootbylabel.3tsol	\
	getzonerootbyname.3tsol		\
	h_alloc.3tsol			\
	h_free.3tsol			\
	htobclear.3tsol			\
	htobsl.3tsol			\
	m_label_alloc.3tsol		\
	m_label_dup.3tsol		\
	m_label_free.3tsol		\
	sbcleartos.3tsol		\
	sbsltos.3tsol			\
	stobclear.3tsol			\
	stobsl.3tsol			\
	tsol_lbuild_create.3tsol	\
	tsol_lbuild_destroy.3tsol	\
	tsol_lbuild_get.3tsol		\
	tsol_lbuild_set.3tsol		\
	Xbcleartos.3tsol		\
	Xbsltos.3tsol

bcleartoh_r.3tsol			:= SOFILE = man3tsol/btohex.3tsol
bcleartoh.3tsol				:= SOFILE = man3tsol/btohex.3tsol
bcleartos.3tsol				:= SOFILE = man3tsol/bltos.3tsol
bldominates.3tsol			:= SOFILE = man3tsol/blcompare.3tsol
blequal.3tsol				:= SOFILE = man3tsol/blcompare.3tsol
blinrange.3tsol				:= SOFILE = man3tsol/blcompare.3tsol
blmaximum.3tsol				:= SOFILE = man3tsol/blminmax.3tsol
blminimum.3tsol				:= SOFILE = man3tsol/blminmax.3tsol
blstrictdom.3tsol			:= SOFILE = man3tsol/blcompare.3tsol
bltocolor_r.3tsol			:= SOFILE = man3tsol/bltocolor.3tsol
bsltoh_r.3tsol				:= SOFILE = man3tsol/btohex.3tsol
bsltoh.3tsol				:= SOFILE = man3tsol/btohex.3tsol
bsltos.3tsol				:= SOFILE = man3tsol/bltos.3tsol
getzoneidbylabel.3tsol			:= SOFILE = man3tsol/getzonelabelbyid.3tsol
getzonelabelbyname.3tsol		:= SOFILE = man3tsol/getzonelabelbyid.3tsol
getzonerootbylabel.3tsol		:= SOFILE = man3tsol/getzonerootbyid.3tsol
getzonerootbyname.3tsol			:= SOFILE = man3tsol/getzonerootbyid.3tsol
h_alloc.3tsol				:= SOFILE = man3tsol/btohex.3tsol
h_free.3tsol				:= SOFILE = man3tsol/btohex.3tsol
htobclear.3tsol				:= SOFILE = man3tsol/hextob.3tsol
htobsl.3tsol				:= SOFILE = man3tsol/hextob.3tsol
m_label_alloc.3tsol			:= SOFILE = man3tsol/m_label.3tsol
m_label_dup.3tsol			:= SOFILE = man3tsol/m_label.3tsol
m_label_free.3tsol			:= SOFILE = man3tsol/m_label.3tsol
sbcleartos.3tsol			:= SOFILE = man3tsol/sbltos.3tsol
sbsltos.3tsol				:= SOFILE = man3tsol/sbltos.3tsol
stobclear.3tsol				:= SOFILE = man3tsol/stobl.3tsol
stobsl.3tsol				:= SOFILE = man3tsol/stobl.3tsol
tsol_lbuild_create.3tsol		:= SOFILE = man3tsol/labelbuilder.3tsol
tsol_lbuild_destroy.3tsol		:= SOFILE = man3tsol/labelbuilder.3tsol
tsol_lbuild_get.3tsol			:= SOFILE = man3tsol/labelbuilder.3tsol
tsol_lbuild_set.3tsol			:= SOFILE = man3tsol/labelbuilder.3tsol
Xbcleartos.3tsol			:= SOFILE = man3tsol/labelclipping.3tsol
Xbsltos.3tsol				:= SOFILE = man3tsol/labelclipping.3tsol

include ../Makefile.man

install: $(ROOTMANFILES)
