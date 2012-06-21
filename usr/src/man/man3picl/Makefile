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

MANSECT=	3picl

MANFILES=				\
	libpicl.3picl			\
	picl_find_node.3picl		\
	picl_get_first_prop.3picl	\
	picl_get_frutree_parent.3picl	\
	picl_get_next_by_row.3picl	\
	picl_get_node_by_path.3picl	\
	picl_get_prop_by_name.3picl	\
	picl_get_propinfo_by_name.3picl	\
	picl_get_propinfo.3picl		\
	picl_get_propval.3picl		\
	picl_get_root.3picl		\
	picl_initialize.3picl		\
	picl_set_propval.3picl		\
	picl_shutdown.3picl		\
	picl_strerror.3picl		\
	picl_wait.3picl			\
	picl_walk_tree_by_class.3picl

MANSO=					\
	picl_get_next_by_col.3picl	\
	picl_get_next_prop.3picl	\
	picl_get_propval_by_name.3picl	\
	picl_set_propval_by_name.3picl	\
	ptree_add_row_to_table.3picltree\
	ptree_delete_node.3picltree	\
	ptree_delete_prop.3picltree	\
	ptree_destroy_node.3picltree	\
	ptree_destroy_prop.3picltree	\
	ptree_get_next_by_col.3picltree	\
	ptree_get_next_prop.3picltree	\
	ptree_get_propval_by_name.3picltree\
	ptree_update_propval_by_name.3picltree

picl_get_next_by_col.3picl		:= SOFILE = man3picl/picl_get_next_by_row.3picl
picl_get_next_prop.3picl		:= SOFILE = man3picl/picl_get_first_prop.3picl
picl_get_propval_by_name.3picl		:= SOFILE = man3picl/picl_get_propval.3picl
picl_set_propval_by_name.3picl		:= SOFILE = man3picl/picl_set_propval.3picl
ptree_add_row_to_table.3picltree	:= SOFILE = man3picltree/ptree_create_table.3picltree
ptree_delete_node.3picltree		:= SOFILE = man3picltree/ptree_add_node.3picltree
ptree_delete_prop.3picltree		:= SOFILE = man3picltree/ptree_add_prop.3picltree
ptree_destroy_node.3picltree		:= SOFILE = man3picltree/ptree_create_node.3picltree
ptree_destroy_prop.3picltree		:= SOFILE = man3picltree/ptree_create_prop.3picltree
ptree_get_next_by_col.3picltree		:= SOFILE = man3picltree/ptree_get_next_by_row.3picltree
ptree_get_next_prop.3picltree		:= SOFILE = man3picltree/ptree_get_first_prop.3picltree
ptree_get_propval_by_name.3picltree	:= SOFILE = man3picltree/ptree_get_propval.3picltree
ptree_update_propval_by_name.3picltree	:= SOFILE = man3picltree/ptree_update_propval.3picltree

include ../Makefile.man

install: $(ROOTMANFILES)
