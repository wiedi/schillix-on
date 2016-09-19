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

LIBRARY=	libschily.a
VERS=		.1

OBJECTS_STDIO=	cvmod.o dat.o fcons.o fdown.o fdup.o \
		ffileread.o ffilewrite.o \
		fgetaline.o \
		fgetline.o fgetstr.o file_getraise.o file_raise.o \
		fileclose.o fileluopen.o fileopen.o filemopen.o \
		filepos.o fileread.o filereopen.o fileseek.o filesize.o \
		filestat.o filewrite.o flag.o flush.o fpipe.o \
		getdelim.o \
		niread.o niwrite.o nixread.o nixwrite.o openfd.o peekc.o \
		fcons64.o fdup64.o fileluopen64.o fileopen64.o filemopen64.o \
		filepos64.o filereopen64.o fileseek64.o filesize64.o \
		filestat64.o openfd64.o

#
# Note: getfp.o must be before getav0.o
# Otherwise getfp.o would be made as dependency of avoffset
#

OBJECTS_REST=	abspath.o \
		astoi.o astoll.o astoul.o astoull.o \
		basename.o breakline.o \
		checkerr.o chown.o cmpbytes.o cmpmbytes.o cmpnullbytes.o \
		comerr.o cvt.o dirent.o \
		dirname.o diropen.o \
		dlfcn.o \
		eaccess.o error.o \
		faccessat.o \
		fchdir.o fchmodat.o fchownat.o fcomerr.o \
		fconv.o fdopendir.o fexec.o \
		fillbytes.o findbytes.o findinpath.o \
		findline.o fjmem.o fnmatch.o \
		format.o fprformat.o \
		fstatat.o fstream.o \
		futimens.o futimesat.o \
		getargs.o getav0.o getdomainname.o getdtablesize.o \
		geterrno.o getexecpath.o \
		getfp.o \
		getgrent.o gethostid.o gethostname.o getlogin.o \
		getnstimeofday.o \
		getnum.o getpagesize.o \
		getperm.o getpwent.o \
		gettimeofday.o \
		gettnum.o getxnum.o getxtnum.o \
		gid.o \
		handlecond.o \
		jmem.o \
		jsdprintf.o jsprintf.o jssnprintf.o jssprintf.o \
		kill.o \
		lchmod.o linkat.o lutimens.o lxchdir.o \
		match.o matchl.o matchw.o matchwl.o \
		mem.o \
		mkdirat.o mkdirs.o \
		mkfifo.o mkfifoat.o \
		mkgmtime.o mknodat.o mkstemp.o movebytes.o \
		openat.o permtostr.o procnameat.o \
		raisecond.o readlinkat.o rename.o renameat.o resolvepath.o \
		saveargs.o savewd.o searchinpath.o \
		serrmsg.o seterrno.o setfp.o \
		setnstimeofday.o \
		sleep.o \
		spawn.o \
		strcasecmp.o strcasemap.o \
		strcat.o strcatl.o strchr.o strcmp.o strcpy.o strcspn.o \
		strdup.o \
		streql.o strlcat.o strlcatl.o strlcpy.o strlen.o \
		strncasecmp.o \
		strncat.o strncmp.o strncpy.o strndup.o strnlen.o strrchr.o \
		strspn.o strstr.o strtod.o \
		swabbytes.o symlinkat.o \
		timegm.o \
		uid.o uname.o unlinkat.o usleep.o \
		utimens.o utimensat.o \
		wcscat.o wcscatl.o wcschr.o wcscmp.o wcscpy.o wcscspn.o \
		wcsdup.o \
		wcseql.o wcslcat.o wcslcatl.o wcslcpy.o wcslen.o wcsncat.o \
		wcsncmp.o \
		wcsncpy.o wcsndup.o wcsnlen.o wcsrchr.o wcsspn.o wcsstr.o \
		wdabort.o \
		zerobytes.o

OOO_OBJECTS_REST=	astoi.o astoll.o astoull.o basename.o breakline.o \
		checkerr.o comerr.o fcomerr.o cmpbytes.o cmpnullbytes.o \
		dirname.o \
		eaccess.o error.o \
		fconv.o fexec.o fillbytes.o \
		findinpath.o \
		findbytes.o findline.o fnmatch.o format.o \
		fstream.o \
		getargs.o getav0.o geterrno.o getexecpath.o getfp.o \
		getdomainname.o gethostid.o gethostname.o getpagesize.o \
		getnum.o getxnum.o \
		gettnum.o getxtnum.o \
		getperm.o \
		gettimeofday.o \
		handlecond.o \
		jsprintf.o jssnprintf.o jssprintf.o \
		match.o matchl.o matchw.o matchwl.o movebytes.o \
		mem.o jmem.o fjmem.o \
		raisecond.o rename.o \
		saveargs.o \
		searchinpath.o serrmsg.o seterrno.o setfp.o \
		sleep.o \
		snprintf.o \
		spawn.o \
		strcat.o strcatl.o strchr.o strcmp.o strcpy.o \
		strdup.o streql.o strlen.o strlcat.o strlcpy.o strncat.o strncmp.o \
		strncpy.o strndup.o strnlen.o strrchr.o \
		swabbytes.o \
		usleep.o \
		wcscat.o wcscatl.o wcschr.o wcscmp.o wcscpy.o \
		wcsdup.o wcseql.o wcslen.o wcslcat.o wcslcpy.o wcsncat.o wcsncmp.o \
		wcsncpy.o wcsndup.o wcsnlen.o wcsrchr.o

OBJECTS=	$(OBJECTS_STDIO) $(OBJECTS_REST)


include ../../Makefile.lib
#
# libschily must be installed in the root filesystem for /sbin/sh = Bourne Shell
# To put this library to /lib instead of /usr/lib
# uncomment the following line
include ../../Makefile.rootfs

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-lc
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

C99MODE=	$(C99_ENABLE)

CFLAGS +=	$(CCVERBOSE)

CPPFLAGS +=	-D_REENTRANT

CPPFLAGS +=	-DUSE_SCANSTACK	# Try to scan stack frames
CPPFLAGS +=	-DPORT_ONLY	# Add missing funcs line snprintf for porting
CPPFLAGS +=	-DNO_PRINTFLIKE=1
CPPFLAGS +=	-I../include

all: $(LIBS)

lint: lintcheck

.KEEP_STATE:

include ../../Makefile.targ

objs/%.o pics/%.o: $(SRCDIR)/common/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRCDIR)/common/stdio/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)
