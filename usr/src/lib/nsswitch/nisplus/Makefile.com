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
# Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#
# lib/nsswitch/nisplus/Makefile.com

LIBRARY =	libnss_nisplus.a
VERS =		.1

OBJECTS =	nisplus_common.o \
		gethostent.o	\
		gethostent6.o	\
		getrpcent.o	\
		getnetent.o	\
		getprotoent.o	\
		getservent.o	\
		getpwnam.o	\
		getgrent.o	\
		getspent.o	\
		bootparams_getbyname.o \
		ether_addr.o	\
		getnetgrent.o	\
		netmasks.o	\
		getprinter.o	\
		getauthattr.o	\
		getexecattr.o	\
		getprofattr.o	\
		getuserattr.o	\
		getauuser.o

# include common nsswitch library definitions.
include		../../Makefile.com

# install this library in the root filesystem
include ../../../Makefile.rootfs

LINTFLAGS +=	-erroff=E_GLOBAL_COULD_BE_STATIC2
LINTFLAGS64 +=	-erroff=E_GLOBAL_COULD_BE_STATIC2

LDLIBS +=	-lnsl -lsocket
DYNLIB1 =	nss_nisplus.so$(VERS)
