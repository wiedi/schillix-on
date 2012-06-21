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

MANSECT=	3mvec

MANFILES=				\
	vatan_.3mvec			\
	vatan2_.3mvec			\
	vcos_.3mvec			\
	vcospi_.3mvec			\
	vexp_.3mvec			\
	vhypot_.3mvec			\
	vlog_.3mvec			\
	vpow_.3mvec			\
	vrhypot_.3mvec			\
	vrsqrt_.3mvec			\
	vsin_.3mvec			\
	vsincos_.3mvec			\
	vsincospi_.3mvec		\
	vsinpi_.3mvec			\
	vsqrt_.3mvec			\
	vz_abs_.3mvec			\
	vz_exp_.3mvec			\
	vz_log_.3mvec			\
	vz_pow_.3mvec

MANSO=					\
	vatan2f_.3mvec			\
	vatanf_.3mvec			\
	vc_abs_.3mvec			\
	vc_exp_.3mvec			\
	vc_log_.3mvec			\
	vc_pow_.3mvec			\
	vcosf_.3mvec			\
	vcospif_.3mvec			\
	vexpf_.3mvec			\
	vhypotf_.3mvec			\
	vlogf_.3mvec			\
	vpowf_.3mvec			\
	vrhypotf_.3mvec			\
	vrsqrtf_.3mvec			\
	vsincosf_.3mvec			\
	vsincospif_.3mvec		\
	vsinf_.3mvec			\
	vsinpif_.3mvec			\
	vsqrtf_.3mvec

vatan2f_.3mvec				:= SOFILE = man3mvec/vatan2_.3mvec
vatanf_.3mvec				:= SOFILE = man3mvec/vatan_.3mvec
vc_abs_.3mvec				:= SOFILE = man3mvec/vz_abs_.3mvec
vc_exp_.3mvec				:= SOFILE = man3mvec/vz_exp_.3mvec
vc_log_.3mvec				:= SOFILE = man3mvec/vz_log_.3mvec
vc_pow_.3mvec				:= SOFILE = man3mvec/vz_pow_.3mvec
vcosf_.3mvec				:= SOFILE = man3mvec/vcos_.3mvec
vcospif_.3mvec				:= SOFILE = man3mvec/vcospi_.3mvec
vexpf_.3mvec				:= SOFILE = man3mvec/vexp_.3mvec
vhypotf_.3mvec				:= SOFILE = man3mvec/vhypot_.3mvec
vlogf_.3mvec				:= SOFILE = man3mvec/vlog_.3mvec
vpowf_.3mvec				:= SOFILE = man3mvec/vpow_.3mvec
vrhypotf_.3mvec				:= SOFILE = man3mvec/vrhypot_.3mvec
vrsqrtf_.3mvec				:= SOFILE = man3mvec/vrsqrt_.3mvec
vsincosf_.3mvec				:= SOFILE = man3mvec/vsincos_.3mvec
vsincospif_.3mvec			:= SOFILE = man3mvec/vsincospi_.3mvec
vsinf_.3mvec				:= SOFILE = man3mvec/vsin_.3mvec
vsinpif_.3mvec				:= SOFILE = man3mvec/vsinpi_.3mvec
vsqrtf_.3mvec				:= SOFILE = man3mvec/vsqrt_.3mvec

include ../Makefile.man

install: $(ROOTMANFILES)
