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

MANSECT=	3dns_sd

MANFILES=				\
	DNSServiceBrowse.3dns_sd	\
	DNSServiceConstructFullName.3dns_sd\
	DNSServiceCreateConnection.3dns_sd\
	DNSServiceEnumerateDomains.3dns_sd\
	DNSServiceProcessResult.3dns_sd	\
	DNSServiceQueryRecord.3dns_sd	\
	DNSServiceReconfirmRecord.3dns_sd\
	DNSServiceRefDeallocate.3dns_sd	\
	DNSServiceRefSockFD.3dns_sd	\
	DNSServiceRegister.3dns_sd	\
	DNSServiceResolve.3dns_sd	\
	TXTRecordCreate.3dns_sd

MANSO=					\
	DNSServiceAddRecord.3dns_sd	\
	DNSServiceRegisterRecord.3dns_sd\
	DNSServiceRemoveRecord.3dns_sd	\
	DNSServiceUpdateRecord.3dns_sd	\
	TXTRecordContainsKey.3dns_sd	\
	TXTRecordDeallocate.3dns_sd	\
	TXTRecordGetBytesPtr.3dns_sd	\
	TXTRecordGetCount.3dns_sd	\
	TXTRecordGetItemAtIndex.3dns_sd	\
	TXTRecordGetLength.3dns_sd	\
	TXTRecordGetValuePtr.3dns_sd	\
	TXTRecordRemoveValue.3dns_sd	\
	TXTRecordSetValue.3dns_sd

DNSServiceAddRecord.3dns_sd		:= SOFILE = man3dns_sd/DNSServiceCreateConnection.3dns_sd
DNSServiceRegisterRecord.3dns_sd	:= SOFILE = man3dns_sd/DNSServiceCreateConnection.3dns_sd
DNSServiceRemoveRecord.3dns_sd		:= SOFILE = man3dns_sd/DNSServiceCreateConnection.3dns_sd
DNSServiceUpdateRecord.3dns_sd		:= SOFILE = man3dns_sd/DNSServiceCreateConnection.3dns_sd
TXTRecordContainsKey.3dns_sd		:= SOFILE = man3dns_sd/TXTRecordCreate.3dns_sd
TXTRecordDeallocate.3dns_sd		:= SOFILE = man3dns_sd/TXTRecordCreate.3dns_sd
TXTRecordGetBytesPtr.3dns_sd		:= SOFILE = man3dns_sd/TXTRecordCreate.3dns_sd
TXTRecordGetCount.3dns_sd		:= SOFILE = man3dns_sd/TXTRecordCreate.3dns_sd
TXTRecordGetItemAtIndex.3dns_sd		:= SOFILE = man3dns_sd/TXTRecordCreate.3dns_sd
TXTRecordGetLength.3dns_sd		:= SOFILE = man3dns_sd/TXTRecordCreate.3dns_sd
TXTRecordGetValuePtr.3dns_sd		:= SOFILE = man3dns_sd/TXTRecordCreate.3dns_sd
TXTRecordRemoveValue.3dns_sd		:= SOFILE = man3dns_sd/TXTRecordCreate.3dns_sd
TXTRecordSetValue.3dns_sd		:= SOFILE = man3dns_sd/TXTRecordCreate.3dns_sd

include ../Makefile.man

install: $(ROOTMANFILES)
