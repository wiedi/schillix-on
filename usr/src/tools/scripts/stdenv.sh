#
# CDDL HEADER START
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
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2020 J. Schilling
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#

#
# Shell script fragment to set standard build environment variables,
# for use by bldenv(1) and nightly(1).  Can be overridden by the
# user's environment file.  Because bldenv and nightly are both ksh
# scripts, we can use ksh syntax here.
#

#
# the source product has no SCCS history, and is modified to remove source
# that cannot be shipped. EXPORT_SRC is where the clear files are copied, then
# modified with 'make EXPORT_SRC'.
#
[ -n "$EXPORT_SRC" ] || export EXPORT_SRC="$CODEMGR_WS/export_src"

#
# CRYPT_SRC is similar to EXPORT_SRC, but after 'make CRYPT_SRC' the files in
# xmod/cry_files are saved. They are dropped on the exportable source to create
# the domestic build.
#
[ -n "$CRYPT_SRC" ] || export CRYPT_SRC="$CODEMGR_WS/crypt_src"

#
# OPEN_SRCDIR is where we copy the open tree to so that we can be sure
# we don't have a hidden dependency on closed code.  The name ends in
# "DIR" to avoid confusion with the flags related to open source
# builds.
#
[ -n "$OPEN_SRCDIR" ] || export OPEN_SRCDIR="$CODEMGR_WS/open_src"

#
# Flag to enable creation of per-build-type proto areas.  If "yes",
# more proto areas are created, which speeds up incremental builds but
# increases storage consumption.  Will be forced to "yes" for
# OpenSolaris deliveries.
#
[ -n "$MULTI_PROTO" ] || export MULTI_PROTO=no

#
# Check whether the installed xgettext(1) binary supports the new -H option.
#
HAS_HFLG=`/usr/bin/xgettext -H -p/bla /dev/null 2>&1 | grep "illegal option -- H"`
if [ -z "$HAS_HFLG" ]; then
	export XGETTEXT_NO_HDR=-H
else
	export XGETTEXT_NO_HDR=""
fi

unset HAS_HFLG

