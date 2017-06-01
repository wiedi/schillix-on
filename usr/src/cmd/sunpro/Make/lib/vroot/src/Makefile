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
# CDDL HEADER END
#

#
# Copyright 2017 Jörg Schilling.  All rights reserved.
#

LIBRARY =	libvroot.a
VERS =		.1

OBJECTS =	access.o \
		args.o \
		chdir.o \
		chmod.o \
		chown.o \
		chroot.o \
		creat.o \
		execve.o \
		lock.o \
		lstat.o \
		mkdir.o \
		mount.o \
		open.o \
		readlink.o \
		report.o \
		rmdir.o \
		setenv.o \
		stat.o \
		statfs.o \
		truncate.o \
		unlink.o \
		unmount.o \
		utimes.o \
		vroot.o

.KEEP_STATE:

all:

include $(SRC)/lib/Makefile.lib

POFILE =	sunpromakelibvroot.po
POFILES =	$(OBJECTS:%.o=%.po)

include ../../../../Makefile.com

LIBS =		$(LIBRARY)

all: $(LIBS)

install: all

lint:

#
# For message catalogue files
#
_msg: $(MSGDOMAIN) $(POFILE)
	$(RM) $(MSGDOMAIN)/$(POFILE)
	$(CP) $(POFILE) $(MSGDOMAIN)

include $(SRC)/lib/Makefile.targ
include $(SRC)/Makefile.msg.targ
