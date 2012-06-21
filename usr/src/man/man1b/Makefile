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

MANSECT=	1b

MANFILES=				\
	basename.1b			\
	biff.1b				\
	chown.1b			\
	df.1b				\
	du.1b				\
	echo.1b				\
	exportfs.1b			\
	expr.1b				\
	fastboot.1b			\
	file.1b				\
	from.1b				\
	groups.1b			\
	grpck.1b			\
	ln.1b				\
	logger.1b			\
	lpc.1b				\
	lpq.1b				\
	lpr.1b				\
	lprm.1b				\
	lptest.1b			\
	mail.1b				\
	mkstr.1b			\
	plot.1b				\
	printenv.1b			\
	ps.1b				\
	rusage.1b			\
	shutdown.1b			\
	stty.1b				\
	sum.1b				\
	test.1b				\
	touch.1b			\
	tset.1b				\
	users.1b			\
	vipw.1b				\
	whereis.1b			\
	whoami.1b

MANSO=					\
	Mail.1b				\
	aedplot.1b			\
	atoplot.1b			\
	bgplot.1b			\
	crtplot.1b			\
	dumbplot.1b			\
	fasthalt.1b			\
	gigiplot.1b			\
	hp7221plot.1b			\
	hpplot.1b			\
	implot.1b			\
	plottoa.1b			\
	reset.1b			\
	t300.1b				\
	t300s.1b			\
	t4013.1b			\
	t450.1b				\
	tek.1b				\
	vplot.1b

aedplot.1b		:= SOFILE = man1b/plot.1b
atoplot.1b		:= SOFILE = man1b/plot.1b
bgplot.1b		:= SOFILE = man1b/plot.1b
crtplot.1b		:= SOFILE = man1b/plot.1b
dumbplot.1b		:= SOFILE = man1b/plot.1b
fasthalt.1b		:= SOFILE = man1b/fastboot.1b
gigiplot.1b		:= SOFILE = man1b/plot.1b
hp7221plot.1b		:= SOFILE = man1b/plot.1b
hpplot.1b		:= SOFILE = man1b/plot.1b
implot.1b		:= SOFILE = man1b/plot.1b
Mail.1b			:= SOFILE = man1b/mail.1b
plottoa.1b		:= SOFILE = man1b/plot.1b
reset.1b		:= SOFILE = man1b/tset.1b
t300.1b			:= SOFILE = man1b/plot.1b
t300s.1b		:= SOFILE = man1b/plot.1b
t4013.1b		:= SOFILE = man1b/plot.1b
t450.1b			:= SOFILE = man1b/plot.1b
tek.1b			:= SOFILE = man1b/plot.1b
vplot.1b		:= SOFILE = man1b/plot.1b

include ../Makefile.man

install: $(ROOTMANFILES)
