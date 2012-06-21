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

MANSECT=	3snmp

MANFILES=				\
	SSAAgentIsAlive.3snmp		\
	SSAOidCmp.3snmp			\
	SSAStringCpy.3snmp

MANSO=					\
	SSAGetTrapPort.3snmp		\
	SSAOidCpy.3snmp			\
	SSAOidDup.3snmp			\
	SSAOidFree.3snmp		\
	SSAOidInit.3snmp		\
	SSAOidNew.3snmp			\
	SSAOidString.3snmp		\
	SSAOidStrToOid.3snmp		\
	SSAOidZero.3snmp		\
	SSARegSubagent.3snmp		\
	SSARegSubtable.3snmp		\
	SSARegSubtree.3snmp		\
	SSASendTrap.3snmp		\
	SSAStringInit.3snmp		\
	SSAStringToChar.3snmp		\
	SSAStringZero.3snmp		\
	SSASubagentOpen.3snmp

SSAGetTrapPort.3snmp			:= SOFILE = man3snmp/SSAAgentIsAlive.3snmp
SSAOidCpy.3snmp				:= SOFILE = man3snmp/SSAOidCmp.3snmp
SSAOidDup.3snmp				:= SOFILE = man3snmp/SSAOidCmp.3snmp
SSAOidFree.3snmp			:= SOFILE = man3snmp/SSAOidCmp.3snmp
SSAOidInit.3snmp			:= SOFILE = man3snmp/SSAOidCmp.3snmp
SSAOidNew.3snmp				:= SOFILE = man3snmp/SSAOidCmp.3snmp
SSAOidString.3snmp			:= SOFILE = man3snmp/SSAOidCmp.3snmp
SSAOidStrToOid.3snmp			:= SOFILE = man3snmp/SSAOidCmp.3snmp
SSAOidZero.3snmp			:= SOFILE = man3snmp/SSAOidCmp.3snmp
SSARegSubagent.3snmp			:= SOFILE = man3snmp/SSAAgentIsAlive.3snmp
SSARegSubtable.3snmp			:= SOFILE = man3snmp/SSAAgentIsAlive.3snmp
SSARegSubtree.3snmp			:= SOFILE = man3snmp/SSAAgentIsAlive.3snmp
SSASendTrap.3snmp			:= SOFILE = man3snmp/SSAAgentIsAlive.3snmp
SSAStringInit.3snmp			:= SOFILE = man3snmp/SSAStringCpy.3snmp
SSAStringToChar.3snmp			:= SOFILE = man3snmp/SSAStringCpy.3snmp
SSAStringZero.3snmp			:= SOFILE = man3snmp/SSAStringCpy.3snmp
SSASubagentOpen.3snmp			:= SOFILE = man3snmp/SSAAgentIsAlive.3snmp

include ../Makefile.man

install: $(ROOTMANFILES)
