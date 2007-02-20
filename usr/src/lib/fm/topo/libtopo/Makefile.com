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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#

LIBRARY = libtopo.a
VERS = .1

BUILTINSRCS = \
	cpu.c \
	dev.c \
	hc.c \
	mem.c \
	mod.c \
	pkg.c

LIBSRCS = \
	topo_2xml.c \
	topo_alloc.c \
	topo_builtin.c \
	topo_error.c \
	topo_file.c \
	topo_fmri.c \
	topo_list.c \
	topo_method.c \
	topo_mod.c \
	topo_module.c \
	topo_node.c \
	topo_nvl.c \
	topo_parse.c \
	topo_prop.c \
	topo_protocol.c \
	topo_rtld.c \
	topo_snap.c \
	topo_string.c \
	topo_subr.c \
	topo_tree.c \
	topo_xml.c

OBJECTS = $(BUILTINSRCS:%.c=%.o) $(LIBSRCS:%.c=%.o)

include ../../../../Makefile.lib
include ../../../Makefile.lib

SRCS = $(BUILTINSRCS:%.c=../common/%.c) $(LIBSRCS:%.c=../common/%.c)
LIBS = $(DYNLIB) $(LINTLIB)

SRCDIR =	../common

CLEANFILES += ../common/topo_error.c

CPPFLAGS += -I../common -I/usr/include/libxml2 -I.
CFLAGS += $(CCVERBOSE) $(C_BIGPICFLAGS)
CFLAGS += -D_POSIX_PTHREAD_SEMANTICS
CFLAGS64 += $(CCVERBOSE) $(C_BIGPICFLAGS)

LINTFLAGS = -msux
LINTFLAGS64 = -msux -Xarch=$(MACH64:sparcv9=v9)

$(DYNLIB)  := LDLIBS += \
	-lnvpair -lelf -lumem -lxml2 -lkstat -luuid -ldevinfo -lsmbios -lc

$(LINTLIB) := SRCS = $(SRCDIR)/$(LINTSRC)
$(LINTLIB) := LINTFLAGS = -nsvx
$(LINTLIB) := LINTFLAGS64 = -nsvx -Xarch=$(MACH64:sparcv9=v9)
$(LINTLIB) := LDLIBS += -lnvpair -lumem -lc

.KEEP_STATE:

all: $(LIBS)

lint: $(LINTLIB) lintcheck

pics/%.o: ../$(MACH)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

%.o: ../common/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

../common/topo_error.c: ../common/mkerror.sh ../common/topo_error.h
	sh ../common/mkerror.sh liberrors < ../common/topo_error.h > $@
	sh ../common/mkerror.sh properrors < ../common/libtopo.h >> $@
	sh ../common/mkerror.sh methoderrors < ../common/libtopo.h >> $@
	sh ../common/mkerror.sh moderrors < ../common/topo_mod.h >> $@

include ../../../../Makefile.targ
include ../../../Makefile.targ
