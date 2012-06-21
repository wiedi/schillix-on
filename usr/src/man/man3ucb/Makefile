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
# Copyright 2012 Jörg Schilling.  All rights reserved.
#

#
# Definitions common to section subdirectories for manual pages.
#
# include global definitions; SRC should be defined in the shell.
# SRC is needed until RFE 1026993 is implemented.

MANSECT=	3ucb

MANFILES=				\
	dbm.3ucb			\
	flock.3ucb			\
	fopen.3ucb			\
	gettimeofday.3ucb		\
	mctl.3ucb			\
	nice.3ucb			\
	nlist.3ucb			\
	printf.3ucb			\
	psignal.3ucb			\
	rand.3ucb			\
	readdir.3ucb			\
	scandir.3ucb			\
	setjmp.3ucb			\
	sigblock.3ucb			\
	siginterrupt.3ucb		\
	signal.3ucb			\
	sigstack.3ucb			\
	sigvec.3ucb			\
	sleep.3ucb			\
	syscall.3ucb			\
	times.3ucb			\
	wait.3ucb

MANSO=					\
	_longjmp.3ucb			\
	_setjmp.3ucb			\
	alphasort.3ucb			\
	dbmclose.3ucb			\
	dbminit.3ucb			\
	delete.3ucb			\
	fetch.3ucb			\
	firstkey.3ucb			\
	fprintf.3ucb			\
	freopen.3ucb			\
	longjmp.3ucb			\
	nextkey.3ucb			\
	settimeofday.3ucb		\
	sigmask.3ucb			\
	sigpause.3ucb			\
	sigsetmask.3ucb			\
	sprintf.3ucb			\
	srand.3ucb			\
	store.3ucb			\
	sys_siglist.3ucb		\
	vfprintf.3ucb			\
	vprintf.3ucb			\
	vsprintf.3ucb			\
	wait3.3ucb			\
	wait4.3ucb			\
	waitpid.3ucb			\
	WIFEXITED.3ucb			\
	WIFSIGNALED.3ucb		\
	WIFSTOPPED.3ucb

_longjmp.3ucb				:= SOFILE = man3ucb/setjmp.3ucb
_setjmp.3ucb				:= SOFILE = man3ucb/setjmp.3ucb
alphasort.3ucb				:= SOFILE = man3ucb/scandir.3ucb
dbmclose.3ucb				:= SOFILE = man3ucb/dbm.3ucb
dbminit.3ucb				:= SOFILE = man3ucb/dbm.3ucb
delete.3ucb				:= SOFILE = man3ucb/dbm.3ucb
fetch.3ucb				:= SOFILE = man3ucb/dbm.3ucb
firstkey.3ucb				:= SOFILE = man3ucb/dbm.3ucb
fprintf.3ucb				:= SOFILE = man3ucb/printf.3ucb
freopen.3ucb				:= SOFILE = man3ucb/fopen.3ucb
longjmp.3ucb				:= SOFILE = man3ucb/setjmp.3ucb
nextkey.3ucb				:= SOFILE = man3ucb/dbm.3ucb
settimeofday.3ucb			:= SOFILE = man3ucb/gettimeofday.3ucb
sigmask.3ucb				:= SOFILE = man3ucb/sigblock.3ucb
sigpause.3ucb				:= SOFILE = man3ucb/sigblock.3ucb
sigsetmask.3ucb				:= SOFILE = man3ucb/sigblock.3ucb
sprintf.3ucb				:= SOFILE = man3ucb/printf.3ucb
srand.3ucb				:= SOFILE = man3ucb/rand.3ucb
store.3ucb				:= SOFILE = man3ucb/dbm.3ucb
sys_siglist.3ucb			:= SOFILE = man3ucb/psignal.3ucb
vfprintf.3ucb				:= SOFILE = man3ucb/printf.3ucb
vprintf.3ucb				:= SOFILE = man3ucb/printf.3ucb
vsprintf.3ucb				:= SOFILE = man3ucb/printf.3ucb
wait3.3ucb				:= SOFILE = man3ucb/wait.3ucb
wait4.3ucb				:= SOFILE = man3ucb/wait.3ucb
waitpid.3ucb				:= SOFILE = man3ucb/wait.3ucb
WIFEXITED.3ucb				:= SOFILE = man3ucb/wait.3ucb
WIFSIGNALED.3ucb			:= SOFILE = man3ucb/wait.3ucb
WIFSTOPPED.3ucb				:= SOFILE = man3ucb/wait.3ucb

include ../Makefile.man

install: $(ROOTMANFILES)
