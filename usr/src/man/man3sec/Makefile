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
# Copyright 2012 J�rg Schilling.  All rights reserved.
#

#
# Definitions common to section subdirectories for manual pages.
#
# include global definitions; SRC should be defined in the shell.
# SRC is needed until RFE 1026993 is implemented.

MANSECT=	3sec

MANFILES=				\
	acl_check.3sec			\
	acl_free.3sec			\
	acl_get.3sec			\
	acl_strip.3sec			\
	acl_totext.3sec			\
	acl_trivial.3sec		\
	aclcheck.3sec			\
	aclsort.3sec			\
	acltomode.3sec			\
	acltotext.3sec

MANSO=					\
	acl_fromtext.3sec		\
	acl_set.3sec			\
	aclfrommode.3sec		\
	aclfromtext.3sec		\
	facl_get.3sec			\
	facl_set.3sec			\
	chkauthattr.3secdb		\
	endauthattr.3secdb		\
	endexecattr.3secdb		\
	endprofattr.3secdb		\
	enduserattr.3secdb		\
	fgetuserattr.3secdb		\
	free_authattr.3secdb		\
	free_execattr.3secdb		\
	free_profattr.3secdb		\
	free_proflist.3secdb		\
	free_userattr.3secdb		\
	getauthnam.3secdb		\
	getexecprof.3secdb		\
	getexecuser.3secdb		\
	getproflist.3secdb		\
	getprofnam.3secdb		\
	getusernam.3secdb		\
	getuseruid.3secdb		\
	match_execattr.3secdb		\
	setauthattr.3secdb		\
	setexecattr.3secdb		\
	setprofattr.3secdb		\
	setuserattr.3secdb

acl_fromtext.3sec			:= SOFILE = man3sec/acl_totext.3sec
acl_set.3sec				:= SOFILE = man3sec/acl_get.3sec
aclfrommode.3sec			:= SOFILE = man3sec/acltomode.3sec
aclfromtext.3sec			:= SOFILE = man3sec/acltotext.3sec
facl_get.3sec				:= SOFILE = man3sec/acl_get.3sec
facl_set.3sec				:= SOFILE = man3sec/acl_get.3sec
chkauthattr.3secdb			:= SOFILE = man3secdb/getauthattr.3secdb
endauthattr.3secdb			:= SOFILE = man3secdb/getauthattr.3secdb
endexecattr.3secdb			:= SOFILE = man3secdb/getexecattr.3secdb
endprofattr.3secdb			:= SOFILE = man3secdb/getprofattr.3secdb
enduserattr.3secdb			:= SOFILE = man3secdb/getuserattr.3secdb
fgetuserattr.3secdb			:= SOFILE = man3secdb/getuserattr.3secdb
free_authattr.3secdb			:= SOFILE = man3secdb/getauthattr.3secdb
free_execattr.3secdb			:= SOFILE = man3secdb/getexecattr.3secdb
free_profattr.3secdb			:= SOFILE = man3secdb/getprofattr.3secdb
free_proflist.3secdb			:= SOFILE = man3secdb/getprofattr.3secdb
free_userattr.3secdb			:= SOFILE = man3secdb/getuserattr.3secdb
getauthnam.3secdb			:= SOFILE = man3secdb/getauthattr.3secdb
getexecprof.3secdb			:= SOFILE = man3secdb/getexecattr.3secdb
getexecuser.3secdb			:= SOFILE = man3secdb/getexecattr.3secdb
getproflist.3secdb			:= SOFILE = man3secdb/getprofattr.3secdb
getprofnam.3secdb			:= SOFILE = man3secdb/getprofattr.3secdb
getusernam.3secdb			:= SOFILE = man3secdb/getuserattr.3secdb
getuseruid.3secdb			:= SOFILE = man3secdb/getuserattr.3secdb
match_execattr.3secdb			:= SOFILE = man3secdb/getexecattr.3secdb
setauthattr.3secdb			:= SOFILE = man3secdb/getauthattr.3secdb
setexecattr.3secdb			:= SOFILE = man3secdb/getexecattr.3secdb
setprofattr.3secdb			:= SOFILE = man3secdb/getprofattr.3secdb
setuserattr.3secdb			:= SOFILE = man3secdb/getuserattr.3secdb

include ../Makefile.man

install: $(ROOTMANFILES)
