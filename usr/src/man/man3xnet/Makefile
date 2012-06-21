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

MANSECT=	3xnet

MANFILES=				\
	accept.3xnet			\
	bind.3xnet			\
	connect.3xnet			\
	endhostent.3xnet		\
	endnetent.3xnet			\
	endprotoent.3xnet		\
	endservent.3xnet		\
	freeaddrinfo.3xnet		\
	gai_strerror.3xnet		\
	gethostname.3xnet		\
	getnameinfo.3xnet		\
	getpeername.3xnet		\
	getsockname.3xnet		\
	getsockopt.3xnet		\
	htonl.3xnet			\
	if_nametoindex.3xnet		\
	inet_addr.3xnet			\
	inet_ntop.3xnet			\
	listen.3xnet			\
	recv.3xnet			\
	recvfrom.3xnet			\
	recvmsg.3xnet			\
	send.3xnet			\
	sendmsg.3xnet			\
	sendto.3xnet			\
	setsockopt.3xnet		\
	shutdown.3xnet			\
	sockatmark.3xnet		\
	socket.3xnet			\
	socketpair.3xnet

MANSO=					\
	getaddrinfo.3xnet		\
	gethostbyaddr.3xnet		\
	gethostbyname.3xnet		\
	gethostent.3xnet		\
	getnetbyaddr.3xnet		\
	getnetbyname.3xnet		\
	getnetent.3xnet			\
	getprotobyname.3xnet		\
	getprotobynumber.3xnet		\
	getprotoent.3xnet		\
	getservbyname.3xnet		\
	getservbyport.3xnet		\
	getservent.3xnet		\
	htons.3xnet			\
	if_freenameindex.3xnet		\
	if_indextoname.3xnet		\
	if_nameindex.3xnet		\
	inet_lnaof.3xnet		\
	inet_makeaddr.3xnet		\
	inet_netof.3xnet		\
	inet_network.3xnet		\
	inet_ntoa.3xnet			\
	inet_pton.3xnet			\
	ntohl.3xnet			\
	ntohs.3xnet			\
	sethostent.3xnet		\
	setnetent.3xnet			\
	setprotoent.3xnet		\
	setservent.3xnet

getaddrinfo.3xnet			:= SOFILE = man3xnet/freeaddrinfo.3xnet
gethostbyaddr.3xnet			:= SOFILE = man3xnet/endhostent.3xnet
gethostbyname.3xnet			:= SOFILE = man3xnet/endhostent.3xnet
gethostent.3xnet			:= SOFILE = man3xnet/endhostent.3xnet
getnetbyaddr.3xnet			:= SOFILE = man3xnet/endnetent.3xnet
getnetbyname.3xnet			:= SOFILE = man3xnet/endnetent.3xnet
getnetent.3xnet				:= SOFILE = man3xnet/endnetent.3xnet
getprotobyname.3xnet			:= SOFILE = man3xnet/endprotoent.3xnet
getprotobynumber.3xnet			:= SOFILE = man3xnet/endprotoent.3xnet
getprotoent.3xnet			:= SOFILE = man3xnet/endprotoent.3xnet
getservbyname.3xnet			:= SOFILE = man3xnet/endservent.3xnet
getservbyport.3xnet			:= SOFILE = man3xnet/endservent.3xnet
getservent.3xnet			:= SOFILE = man3xnet/endservent.3xnet
htons.3xnet				:= SOFILE = man3xnet/htonl.3xnet
if_freenameindex.3xnet			:= SOFILE = man3xnet/if_nametoindex.3xnet
if_indextoname.3xnet			:= SOFILE = man3xnet/if_nametoindex.3xnet
if_nameindex.3xnet			:= SOFILE = man3xnet/if_nametoindex.3xnet
inet_lnaof.3xnet			:= SOFILE = man3xnet/inet_addr.3xnet
inet_makeaddr.3xnet			:= SOFILE = man3xnet/inet_addr.3xnet
inet_netof.3xnet			:= SOFILE = man3xnet/inet_addr.3xnet
inet_network.3xnet			:= SOFILE = man3xnet/inet_addr.3xnet
inet_ntoa.3xnet				:= SOFILE = man3xnet/inet_addr.3xnet
inet_pton.3xnet				:= SOFILE = man3xnet/inet_ntop.3xnet
ntohl.3xnet				:= SOFILE = man3xnet/htonl.3xnet
ntohs.3xnet				:= SOFILE = man3xnet/htonl.3xnet
sethostent.3xnet			:= SOFILE = man3xnet/endhostent.3xnet
setnetent.3xnet				:= SOFILE = man3xnet/endnetent.3xnet
setprotoent.3xnet			:= SOFILE = man3xnet/endprotoent.3xnet
setservent.3xnet			:= SOFILE = man3xnet/endservent.3xnet

include ../Makefile.man

install: $(ROOTMANFILES)
