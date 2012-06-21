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

MANSECT=	3proc

MANFILES=				\
	proc_service.3proc		\
	ps_lgetregs.3proc		\
	ps_pglobal_lookup.3proc		\
	ps_pread.3proc			\
	ps_pstop.3proc

MANSO=					\
	ps_kill.3proc			\
	ps_lcontinue.3proc		\
	ps_lgetfpregs.3proc		\
	ps_lgetxregs.3proc		\
	ps_lgetxregsize.3proc		\
	ps_lrolltoaddr.3proc		\
	ps_lsetfpregs.3proc		\
	ps_lsetregs.3proc		\
	ps_lsetxregs.3proc		\
	ps_lstop.3proc			\
	ps_pcontinue.3proc		\
	ps_pdread.3proc			\
	ps_pdwrite.3proc		\
	ps_pglobal_sym.3proc		\
	ps_ptread.3proc			\
	ps_ptwrite.3proc		\
	ps_pwrite.3proc

ps_kill.3proc				:= SOFILE = man3proc/ps_pstop.3proc
ps_lcontinue.3proc			:= SOFILE = man3proc/ps_pstop.3proc
ps_lgetfpregs.3proc			:= SOFILE = man3proc/ps_lgetregs.3proc
ps_lgetxregs.3proc			:= SOFILE = man3proc/ps_lgetregs.3proc
ps_lgetxregsize.3proc			:= SOFILE = man3proc/ps_lgetregs.3proc
ps_lrolltoaddr.3proc			:= SOFILE = man3proc/ps_pstop.3proc
ps_lsetfpregs.3proc			:= SOFILE = man3proc/ps_lgetregs.3proc
ps_lsetregs.3proc			:= SOFILE = man3proc/ps_lgetregs.3proc
ps_lsetxregs.3proc			:= SOFILE = man3proc/ps_lgetregs.3proc
ps_lstop.3proc				:= SOFILE = man3proc/ps_pstop.3proc
ps_pcontinue.3proc			:= SOFILE = man3proc/ps_pstop.3proc
ps_pdread.3proc				:= SOFILE = man3proc/ps_pread.3proc
ps_pdwrite.3proc			:= SOFILE = man3proc/ps_pread.3proc
ps_pglobal_sym.3proc			:= SOFILE = man3proc/ps_pglobal_lookup.3proc
ps_ptread.3proc				:= SOFILE = man3proc/ps_pread.3proc
ps_ptwrite.3proc			:= SOFILE = man3proc/ps_pread.3proc
ps_pwrite.3proc				:= SOFILE = man3proc/ps_pread.3proc

include ../Makefile.man

install: $(ROOTMANFILES)
