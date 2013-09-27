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
# Copyright 2013 Jörg Schilling.  All rights reserved.
#

LIBRARY=	libwsreg.a
VERS=		.1

OBJECTS	=	\
		article.o \
		article_id.o \
		cluster_file_io.o \
		conversion.o \
		file_reader.o \
		file_token.o \
		file_util.o \
		hashtable.o \
		list.o \
		pkg_db_io.o \
		progress.o \
		reg_comp.o \
		reg_query.o \
		revision.o \
		simple_registry.o \
		stack.o \
		string_map.o \
		string_util.o \
		unz_article_input_stream.o \
		wsreg.o \
		xml_file_context.o \
		xml_file_io.o \
		xml_reg.o \
		xml_reg_io.o \
		xml_tag.o


include $(SRC)/lib/Makefile.lib
#
# To put this library to /lib instead of /usr/lib
# uncomment the following line
#include $(SRC)/lib/Makefile.rootfs

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-lsecdb -ladm -lm -lc
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

C99MODE =	$(C99_ENABLE)

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-D_REENTRANT

all: $(LIBS)

lint: lintcheck

.KEEP_STATE:

include $(SRC)/lib/Makefile.targ
