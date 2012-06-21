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

MANSECT=	3rsm

MANFILES=				\
	rsm_create_localmemory_handle.3rsm\
	rsm_get_controller.3rsm		\
	rsm_get_interconnect_topology.3rsm\
	rsm_get_segmentid_range.3rsm	\
	rsm_intr_signal_post.3rsm	\
	rsm_intr_signal_wait_pollfd.3rsm\
	rsm_memseg_export_create.3rsm	\
	rsm_memseg_export_publish.3rsm	\
	rsm_memseg_get_pollfd.3rsm	\
	rsm_memseg_import_connect.3rsm	\
	rsm_memseg_import_get.3rsm	\
	rsm_memseg_import_init_barrier.3rsm\
	rsm_memseg_import_map.3rsm	\
	rsm_memseg_import_open_barrier.3rsm\
	rsm_memseg_import_put.3rsm	\
	rsm_memseg_import_putv.3rsm	\
	rsm_memseg_import_set_mode.3rsm

MANSO=					\
	rsm_free_interconnect_topology.3rsm\
	rsm_free_localmemory_handle.3rsm\
	rsm_get_controller_attr.3rsm	\
	rsm_intr_signal_wait.3rsm	\
	rsm_memseg_export_destroy.3rsm	\
	rsm_memseg_export_rebind.3rsm	\
	rsm_memseg_export_republish.3rsm\
	rsm_memseg_export_unpublish.3rsm\
	rsm_memseg_import_close_barrier.3rsm\
	rsm_memseg_import_destroy_barrier.3rsm\
	rsm_memseg_import_disconnect.3rsm\
	rsm_memseg_import_get_mode.3rsm	\
	rsm_memseg_import_get16.3rsm	\
	rsm_memseg_import_get32.3rsm	\
	rsm_memseg_import_get64.3rsm	\
	rsm_memseg_import_get8.3rsm	\
	rsm_memseg_import_getv.3rsm	\
	rsm_memseg_import_order_barrier.3rsm\
	rsm_memseg_import_put16.3rsm	\
	rsm_memseg_import_put32.3rsm	\
	rsm_memseg_import_put64.3rsm	\
	rsm_memseg_import_put8.3rsm	\
	rsm_memseg_import_unmap.3rsm	\
	rsm_memseg_release_pollfd.3rsm	\
	rsm_release_controller.3rsm

rsm_free_interconnect_topology.3rsm	:= SOFILE = man3rsm/rsm_get_interconnect_topology.3rsm
rsm_free_localmemory_handle.3rsm	:= SOFILE = man3rsm/rsm_create_localmemory_handle.3rsm
rsm_get_controller_attr.3rsm		:= SOFILE = man3rsm/rsm_get_controller.3rsm
rsm_intr_signal_wait.3rsm		:= SOFILE = man3rsm/rsm_intr_signal_post.3rsm
rsm_memseg_export_destroy.3rsm		:= SOFILE = man3rsm/rsm_memseg_export_create.3rsm
rsm_memseg_export_rebind.3rsm		:= SOFILE = man3rsm/rsm_memseg_export_create.3rsm
rsm_memseg_export_republish.3rsm	:= SOFILE = man3rsm/rsm_memseg_export_publish.3rsm
rsm_memseg_export_unpublish.3rsm	:= SOFILE = man3rsm/rsm_memseg_export_publish.3rsm
rsm_memseg_import_close_barrier.3rsm	:= SOFILE = man3rsm/rsm_memseg_import_open_barrier.3rsm
rsm_memseg_import_destroy_barrier.3rsm	:= SOFILE = man3rsm/rsm_memseg_import_init_barrier.3rsm
rsm_memseg_import_disconnect.3rsm	:= SOFILE = man3rsm/rsm_memseg_import_connect.3rsm
rsm_memseg_import_get_mode.3rsm		:= SOFILE = man3rsm/rsm_memseg_import_set_mode.3rsm
rsm_memseg_import_get16.3rsm		:= SOFILE = man3rsm/rsm_memseg_import_get.3rsm
rsm_memseg_import_get32.3rsm		:= SOFILE = man3rsm/rsm_memseg_import_get.3rsm
rsm_memseg_import_get64.3rsm		:= SOFILE = man3rsm/rsm_memseg_import_get.3rsm
rsm_memseg_import_get8.3rsm		:= SOFILE = man3rsm/rsm_memseg_import_get.3rsm
rsm_memseg_import_getv.3rsm		:= SOFILE = man3rsm/rsm_memseg_import_putv.3rsm
rsm_memseg_import_order_barrier.3rsm	:= SOFILE = man3rsm/rsm_memseg_import_open_barrier.3rsm
rsm_memseg_import_put16.3rsm		:= SOFILE = man3rsm/rsm_memseg_import_put.3rsm
rsm_memseg_import_put32.3rsm		:= SOFILE = man3rsm/rsm_memseg_import_put.3rsm
rsm_memseg_import_put64.3rsm		:= SOFILE = man3rsm/rsm_memseg_import_put.3rsm
rsm_memseg_import_put8.3rsm		:= SOFILE = man3rsm/rsm_memseg_import_put.3rsm
rsm_memseg_import_unmap.3rsm		:= SOFILE = man3rsm/rsm_memseg_import_map.3rsm
rsm_memseg_release_pollfd.3rsm		:= SOFILE = man3rsm/rsm_memseg_get_pollfd.3rsm
rsm_release_controller.3rsm		:= SOFILE = man3rsm/rsm_get_controller.3rsm

include ../Makefile.man

install: $(ROOTMANFILES)
