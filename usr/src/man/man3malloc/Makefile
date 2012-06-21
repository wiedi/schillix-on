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

MANSECT=	3malloc

MANFILES=				\
	bsdmalloc.3malloc		\
	malloc.3malloc			\
	mapmalloc.3malloc		\
	mtmalloc.3malloc		\
	umem_alloc.3malloc		\
	umem_cache_create.3malloc	\
	umem_debug.3malloc		\
	watchmalloc.3malloc

MANSO=					\
	calloc.3malloc			\
	free.3malloc			\
	mallinfo.3malloc		\
	mallocctl.3malloc		\
	mallopt.3malloc			\
	memalign.3malloc		\
	realloc.3malloc			\
	umem_cache_alloc.3malloc	\
	umem_cache_destroy.3malloc	\
	umem_cache_free.3malloc		\
	umem_free.3malloc		\
	umem_nofail_callback.3malloc	\
	umem_zalloc.3malloc		\
	valloc.3malloc

calloc.3malloc				:= SOFILE = man3malloc/malloc.3malloc
free.3malloc				:= SOFILE = man3malloc/malloc.3malloc
mallinfo.3malloc			:= SOFILE = man3malloc/malloc.3malloc
mallocctl.3malloc			:= SOFILE = man3malloc/mtmalloc.3malloc
mallopt.3malloc				:= SOFILE = man3malloc/malloc.3malloc
memalign.3malloc			:= SOFILE = man3malloc/malloc.3malloc
realloc.3malloc				:= SOFILE = man3malloc/malloc.3malloc
umem_cache_alloc.3malloc		:= SOFILE = man3malloc/umem_cache_create.3malloc
umem_cache_destroy.3malloc		:= SOFILE = man3malloc/umem_cache_create.3malloc
umem_cache_free.3malloc			:= SOFILE = man3malloc/umem_cache_create.3malloc
umem_free.3malloc			:= SOFILE = man3malloc/umem_alloc.3malloc
umem_nofail_callback.3malloc		:= SOFILE = man3malloc/umem_alloc.3malloc
umem_zalloc.3malloc			:= SOFILE = man3malloc/umem_alloc.3malloc
valloc.3malloc				:= SOFILE = man3malloc/malloc.3malloc

include ../Makefile.man

install: $(ROOTMANFILES)
