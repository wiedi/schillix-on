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

MANSECT=	7p

MANFILES=				\
	arp.7p				\
	dlpi.7p				\
	icmp.7p				\
	icmp6.7p			\
	if_tcp.7p			\
	inet.7p				\
	inet6.7p			\
	ip.7p				\
	ip6.7p				\
	ipsec.7p			\
	ipsecah.7p			\
	ipsecesp.7p			\
	pf_key.7p			\
	rarp.7p				\
	route.7p			\
	routing.7p			\
	sctp.7p				\
	sip.7p				\
	slp.7p				\
	tcp.7p				\
	udp.7p

MANSO=					\
	AH.7p				\
	ARP.7p				\
	ESP.7p				\
	ICMP.7p				\
	if.7p				\
	IP.7p				\
	RARP.7p				\
	SCTP.7p				\
	TCP.7p				\
	UDP.7p

AH.7p					:= SOFILE = man7p/ipsecah.7p
ARP.7p					:= SOFILE = man7p/arp.7p
ESP.7p					:= SOFILE = man7p/ipsecesp.7p
ICMP.7p					:= SOFILE = man7p/icmp.7p
if.7p					:= SOFILE = man7p/if_tcp.7p
IP.7p					:= SOFILE = man7p/ip.7p
RARP.7p					:= SOFILE = man7p/rarp.7p
SCTP.7p					:= SOFILE = man7p/sctp.7p
TCP.7p					:= SOFILE = man7p/tcp.7p
UDP.7p					:= SOFILE = man7p/udp.7p

include ../Makefile.man

install: $(ROOTMANFILES)
