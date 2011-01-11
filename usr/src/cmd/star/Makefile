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
# Copyright 2010-2011 Jörg Schilling.  All rights reserved.
#

PROG =		star
ROOTFS_PROG =	$(PROG)

OBJS=		star.o header.o cpiohdr.o xheader.o xattr.o \
		list.o extract.o create.o append.o diff.o restore.o \
		remove.o star_unix.o acl_unix.o acltext.o fflags.o \
		buffer.o dirtime.o lhash.o \
		hole.o longnames.o \
		movearch.o table.o props.o \
		unicode.o \
		subst.o volhdr.o \
		chdir.o match.o defaults.o dumpdate.o \
		fifo.o device.o checkerr.o \
		findinfo.o

SRCS=	$(OBJS:%.o=%.c)

include ../Makefile.cmd

#
# for message cataloge
#
POFILE= star.po
POFILES= $(SRCS:%.c=%.po)
XGETFLAGS += -a -x star.xcl

#CFLAGS +=	
CPPFLAGS +=	-D__STAR__
CPPFLAGS +=	-DSET_CTIME -DFIFO -DUSE_MMAP -DUSE_REMOTE -DUSE_RCMD_RSH
CPPFLAGS +=	-DUSE_LARGEFILES
CPPFLAGS +=	-DUSE_FIND
CPPFLAGS +=	-DUSE_ACL
CPPFLAGS +=	-DUSE_XATTR
CPPFLAGS +=	-DUSE_FFLAGS
CPPFLAGS +=	-DCOPY_LINKS_DELAYED
CPPFLAGS +=	-DSTAR_FAT
CPPFLAGS +=	-DSCHILY_PRINT
CPPFLAGS +=	-DNO_PRINTFLIKE=1


#MAPFILES = $(MAPFILE.INT) $(MAPFILE.NGB)
#LDFLAGS += $(MAPFILES:%=-M%)
LDLIBS += -ldeflt -lrmt -lfind -lschily -lsec -lsocket -lnsl

.KEEP_STATE:

.PARALLEL:	$(OBJS)

all: $(ROOTFS_PROG)

$(PROG): $(OBJS) $(MAPFILES)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(POFILE): $(POFILES)
	$(RM) $@
	$(CAT)     $(POFILES) > $@

install: all $(ROOTPROG)
	$(RM) $(ROOTBIN)/ustar
	$(SYMLINK) star $(ROOTBIN)/ustar
	$(RM) $(ROOTBIN)/suntar
	$(SYMLINK) star $(ROOTBIN)/suntar
	$(RM) $(ROOTBIN)/gnutar
	$(SYMLINK) star $(ROOTBIN)/gnutar
	$(RM) $(ROOTBIN)/spax
	$(SYMLINK) star $(ROOTBIN)/spax
	$(RM) $(ROOTBIN)/scpio
	$(SYMLINK) star $(ROOTBIN)/scpio

clean:	
	$(RM) $(OBJS)

lint: lint_SRCS

include ../Makefile.targ
