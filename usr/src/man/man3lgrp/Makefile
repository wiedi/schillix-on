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

MANSECT=	3lgrp

MANFILES=				\
	lgrp_affinity_get.3lgrp		\
	lgrp_children.3lgrp		\
	lgrp_cookie_stale.3lgrp		\
	lgrp_cpus.3lgrp			\
	lgrp_fini.3lgrp			\
	lgrp_home.3lgrp			\
	lgrp_init.3lgrp			\
	lgrp_latency.3lgrp		\
	lgrp_mem_size.3lgrp		\
	lgrp_nlgrps.3lgrp		\
	lgrp_parents.3lgrp		\
	lgrp_resources.3lgrp		\
	lgrp_root.3lgrp			\
	lgrp_version.3lgrp		\
	lgrp_view.3lgrp

MANSO=					\
	lgrp_affinity_set.3lgrp		\
	lgrp_latency_cookie.3lgrp

lgrp_affinity_set.3lgrp			:= SOFILE = man3lgrp/lgrp_affinity_get.3lgrp
lgrp_latency_cookie.3lgrp		:= SOFILE = man3lgrp/lgrp_latency.3lgrp

include ../Makefile.man

install: $(ROOTMANFILES)
