#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in compliance with the terms version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy is of the CDDL is also available via the Internet
# at http://www.opensource.org/licenses/cddl1.txt
#

#
# Copyright 2010-2011 Jörg Schilling.  All rights reserved.
#

include ../Makefile.lib

SUBDIRS =	$(MACH)
$(BUILD64)SUBDIRS += $(MACH64)

# conditional assignments
all :=          TARGET= all
install :=      TARGET= install
clean :=        TARGET= clean
clobber :=      TARGET= clobber
lint :=         TARGET= lint
test :=         TARGET= test

# definitions for install_h target
HDRDIR=		include/schily
ROOTHDRDIR=	$(ROOT)/usr/include/schily
ROOTHDRS=       $(HDRS:%=$(ROOTHDRDIR)/%)
#CHECKHDRS=      $(HDRS:%.h=$(HDRDIR)/%.check)
CHECKHDRS=      $(HDRS:%.h=$(HDRDIR)/%.chk)

.KEEP_STATE:

all install clean clobber lint: $(SUBDIRS)

HDRS=	\
	i386-sunos5-cc/align.h \
	i386-sunos5-cc/avoffset.h \
	i386-sunos5-cc/xconfig.h \
	i386-sunos5-cc64/align.h \
	i386-sunos5-cc64/avoffset.h \
	i386-sunos5-cc64/xconfig.h \
	i386-sunos5-gcc/align.h \
	i386-sunos5-gcc/avoffset.h \
	i386-sunos5-gcc/xconfig.h \
	i386-sunos5-gcc64/align.h \
	i386-sunos5-gcc64/avoffset.h \
	i386-sunos5-gcc64/xconfig.h \
	sparc-sunos5-cc/align.h \
	sparc-sunos5-cc/avoffset.h \
	sparc-sunos5-cc/xconfig.h \
	sparc-sunos5-cc64/align.h \
	sparc-sunos5-cc64/avoffset.h \
	sparc-sunos5-cc64/xconfig.h \
	sparc-sunos5-gcc/align.h \
	sparc-sunos5-gcc/avoffset.h \
	sparc-sunos5-gcc/xconfig.h \
	sparc-sunos5-gcc64/align.h \
	sparc-sunos5-gcc64/avoffset.h \
	sparc-sunos5-gcc64/xconfig.h \
	_regex.h  \
	align.h  \
	alloca.h  \
	archdefs.h  \
	assert.h  \
	avoffset.h  \
	btorder.h  \
	ccomdefs.h  \
	checkerr.h  \
	ctype.h  \
	dbgmalloc.h  \
	deflts.h  \
	device.h  \
	dirent.h  \
	dlfcn.h  \
	errno.h  \
	fcntl.h  \
	fetchdir.h  \
	find.h  \
	float.h  \
	fnmatch.h  \
	fstream.h  \
	getargs.h  \
	getcwd.h  \
	grp.h  \
	hostname.h  \
	iconv.h  \
	idcache.h  \
	in.h  \
	inet.h  \
	intcvt.h  \
	io.h  \
	ioctl.h  \
	ipc.h  \
	jmpdefs.h  \
	libgen.h  \
	libport.h  \
	librmt.h  \
	limits.h  \
	locale.h  \
	math.h  \
	maxpath.h  \
	mconfig.h  \
	md4.h  \
	md5.h  \
	mman.h  \
	mtio.h  \
	netdb.h  \
	nlsdefs.h  \
	param.h  \
	patmatch.h  \
	priv.h  \
	procfs.h  \
	prototyp.h  \
	pwd.h  \
	regex.h  \
	resource.h  \
	rmd160.h  \
	rmtio.h  \
	schily.h  \
	schilyp.h  \
	select.h  \
	sem.h  \
	setjmp.h  \
	sha1.h  \
	sha2.h  \
	shadow.h  \
	shcall.h  \
	shm.h  \
	siconv.h  \
	sigblk.h  \
	signal.h  \
	sigset.h  \
	socket.h  \
	standard.h  \
	stat.h  \
	stdio.h  \
	stdlib.h  \
	stkframe.h  \
	string.h  \
	sysexits.h  \
	syslog.h  \
	systeminfo.h  \
	termcap.h  \
	termios.h  \
	time.h  \
	times.h  \
	types.h  \
	unistd.h  \
	utime.h  \
	utsname.h  \
	utypes.h  \
	varargs.h  \
	vfork.h  \
	wait.h  \
	walk.h  \
	wchar.h  \
	wctype.h  \
	xconfig.h  \
	xmconfig.h

# install rule for install_h target

$(ROOTHDRDIR)/%: $(HDRDIR)/%
	$(INS.file)

$(ROOTHDRDIR)/%: %
	$(INS.file)

install_h: $(ROOTHDRS)

# These header files are owned by the Schily project and checked against the
# rules of the Schily project.
%.chk: %.h
	@$(ECHO) "checking $<"; $(CSTYLE) -pP -l132 -b -K $< $(CSTYLE_TAIL); \
	$(HDRCHK) $< $(HDRCHK_TAIL)

check: $(CHECKHDRS)


$(SUBDIRS):	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
