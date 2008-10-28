#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#

LIBRARY =	libmlsvc.a
VERS =		.1

OBJS_COMMON =		\
	dssetup_clnt.o	\
	dssetup_svc.o	\
	lsalib.o	\
	lsar_lookup.o	\
	lsar_open.o	\
	mlsvc_client.o	\
	mlsvc_init.o	\
	mlsvc_logr.o	\
	mlsvc_lsa.o	\
	mlsvc_netr.o	\
	mlsvc_sam.o	\
	mlsvc_srvsvc.o	\
	mlsvc_util.o	\
	mlsvc_winreg.o	\
	mlsvc_wkssvc.o	\
	netdfs.o	\
	netr_auth.o	\
	netr_logon.o	\
	samlib.o	\
	samr_open.o	\
	samr_lookup.o	\
	smb_autohome.o	\
	smb_logon.o	\
	smb_share.o	\
	srvsvc_client.o	\
	svcctl_scm.o	\
	svcctl_svc.o

# Automatically generated from .ndl files
NDLLIST =		\
	dssetup		\
	eventlog	\
	lsarpc		\
	netdfs		\
	netlogon	\
	samrpc		\
	spoolss		\
	srvsvc		\
	svcctl		\
	winreg

OBJECTS=        $(OBJS_COMMON) $(NDLLIST:%=%_ndr.o)

include ../../../Makefile.lib
include ../../Makefile.lib

INCS += -I$(SRC)/common/smbsrv

LDLIBS +=	$(MACH_LDLIBS)
LDLIBS += -lmlrpc -lsmbrdr -lsmb -lsmbns -lshare -lnsl -lpkcs11 -lscf	\
	-luutil -lc

SRCS=   $(OBJS_COMMON:%.o=$(SRCDIR)/%.c)        	\
        $(OBJS_SHARED:%.o=$(SRC)/common/smbsrv/%.c)

include ../../Makefile.targ
include ../../../Makefile.targ
