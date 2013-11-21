#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License, Version 1.0 only
# (the "License").  You may not use this file except in compliance
# with the License.
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
# Copyright 2013 J. Schilling
# Use is subject to license terms.
#

PROG =		bosh

OBJS=	abbrev.o alias.o args.o bltin.o cmd.o ctype.o defs.o \
	echo.o error.o expand.o fault.o func.o gmatch.o hash.o hashserv.o \
	io.o jobs.o macro.o main.o msg.o name.o print.o pwd.o \
	service.o sh_policy.o signames.o stak.o string.o \
	test.o ulimit.o umask.o word.o xec.o
SRCS=	$(OBJS:%.o=%.c)

#
# The files: abbrev.c, abbrev.h, signames.c
# have been borrowed from "bsh"
#


include ../Makefile.cmd

#
# for message catalogs
#
POFILE= bosh.po
POFILES= $(SRCS:%.c=%.po)
XGETFLAGS += -a -x bosh.xcl

C99MODE=	$(C99_ENABLE)

#
# Code that was in ../Makefile.cmd:
#
i386_SPFLAG=	-D_iBCS2
sparc_SPFLAG=
iBCS2FLAG =	$($(MACH)_SPFLAG)

#
# Enhanced/New #defines:
#
CPPFLAGS +=	-DSCHILY_INCLUDES	# Tell the code to use schily include files
CPPFLAGS +=	-DBOURNE_SHELL		# Tell the code that we compile for sh/bosh
CPPFLAGS +=	-DINTERACTIVE		# Include command line history editor
CPPFLAGS +=	-DUSE_LARGEFILES	# Allow Large Files (> 2 GB)
CPPFLAGS +=	-DUSE_NLS		# Enable native language support
#CPPFLAGS +=	-DNO_LOCALE		# Don't use setlocale()
#CPPFLAGS +=	-DNO_WCHAR		# Don't use wide chars
CPPFLAGS +=	-DDO_SYSALLOC		# Include the "alloc" debug builtin command
#CPPFLAGS +=	-DSTAK_DEBUG		# Include debug code for stak.c
#CPPFLAGS +=	-DARGS_RIGHT_TO_LEFT	# Evaluate var2=val2 var1=val1 left to right
#CPPFLAGS +=	-DMY_GMATCH		# Enforce to use our local gmatch()
					# instead if the gmatch() from -lgen
#
# Standard UNIX #defines for the Bourne Shell:
#
CPPFLAGS +=	-D_iBCS2 		# SCO echo compat support (unconditional)
#CPPFLAGS +=	$(iBCS2FLAG)		# SCO echo compat support (x86 only)
CPPFLAGS +=	-DACCT			# Include support for Shell Accounting
#CPPFLAGS +=	RES			# "Research" include "login", disable others

MAPFILES = $(MAPFILE.INT) $(MAPFILE.NGB)
LDFLAGS += $(MAPFILES:%=-M%)

#
# Use lazy loading for all libraries that are not needed when interpreting
# shell scripts.
#
# Note that libxtermcap is an indirect dependency via libshedit.
# Note that libschily is an indirect dependency via libshedit.
# These dependencies are loaded when libshedit is loaded because we are in
# interactive mode.
#
LAZYLIBS = $(ZLAZYLOAD) -lgen -lsecdb -lshedit $(ZNOLAZYLOAD)
lint := LAZYLIBS = -lgen -lsecdb -lshedit -lschily
LDLIBS += $(LAZYLIBS)

.KEEP_STATE:

.PARALLEL:	$(OBJS)

all: $(PROG)

$(PROG): $(OBJS) $(MAPFILES)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(POFILE): $(POFILES)
	$(RM) $@
	$(CAT)     $(POFILES) | $(SED) -f poxcl.sed > $@

install: all $(ROOTSBINPROG)
	$(RM) $(ROOTSBIN)/jbosh
	$(SYMLINK) bosh $(ROOTSBIN)/jbosh
	$(RM) $(ROOTSBIN)/pfbosh
	$(SYMLINK) bosh $(ROOTSBIN)/pfbosh
	$(RM) $(ROOTBIN)/bosh
	$(SYMLINK) ../../sbin/bosh $(ROOTBIN)/bosh
	$(RM) $(ROOTBIN)/jbosh
	$(SYMLINK) ../../sbin/bosh $(ROOTBIN)/jbosh
	$(RM) $(ROOTBIN)/pfbosh 
	$(SYMLINK) ../../sbin/bosh $(ROOTBIN)/pfbosh
	$(RM) $(ROOTBIN)/rbosh 
	$(SYMLINK) ../../sbin/bosh $(ROOTBIN)/rbosh

clean:	
	$(RM) $(OBJS)

lint: lint_SRCS

include ../Makefile.targ
