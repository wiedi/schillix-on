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

MANSECT=	3socket

MANFILES=				\
	accept.3socket			\
	bind.3socket			\
	byteorder.3socket		\
	connect.3socket			\
	ethers.3socket			\
	getaddrinfo.3socket		\
	getipnodebyname.3socket		\
	getnetbyname.3socket		\
	getpeername.3socket		\
	getprotobyname.3socket		\
	getservbyname.3socket		\
	getsockname.3socket		\
	getsockopt.3socket		\
	getsourcefilter.3socket		\
	icmp6_filter.3socket		\
	if_nametoindex.3socket		\
	inet.3socket			\
	inet6_opt.3socket		\
	inet6_rth.3socket		\
	listen.3socket			\
	rcmd.3socket			\
	recv.3socket			\
	rexec.3socket			\
	sctp_bindx.3socket		\
	sctp_getladdrs.3socket		\
	sctp_getpaddrs.3socket		\
	sctp_opt_info.3socket		\
	sctp_peeloff.3socket		\
	sctp_recvmsg.3socket		\
	sctp_send.3socket		\
	sctp_sendmsg.3socket		\
	send.3socket			\
	shutdown.3socket		\
	socket.3socket			\
	socketpair.3socket		\
	spray.3socket

MANSO=					\
	endnetent.3socket		\
	endprotoent.3socket		\
	endservent.3socket		\
	ether_aton.3socket		\
	ether_hostton.3socket		\
	ether_line.3socket		\
	ether_ntoa.3socket		\
	ether_ntohost.3socket		\
	freeaddrinfo.3socket		\
	freehostent.3socket		\
	gai_strerror.3socket		\
	getipnodebyaddr.3socket		\
	getipv4sourcefilter.3socket	\
	getnameinfo.3socket		\
	getnetbyaddr_r.3socket		\
	getnetbyaddr.3socket		\
	getnetbyname_r.3socket		\
	getnetent_r.3socket		\
	getnetent.3socket		\
	getprotobyname_r.3socket	\
	getprotobynumber_r.3socket	\
	getprotobynumber.3socket	\
	getprotoent_r.3socket		\
	getprotoent.3socket		\
	getservbyname_r.3socket		\
	getservbyport_r.3socket		\
	getservbyport.3socket		\
	getservent_r.3socket		\
	getservent.3socket		\
	htonl.3socket			\
	htonll.3socket			\
	htons.3socket			\
	if_freenameindex.3socket	\
	if_indextoname.3socket		\
	if_nameindex.3socket		\
	inet_addr.3socket		\
	inet_aton.3socket		\
	inet_lnaof.3socket		\
	inet_makeaddr.3socket		\
	inet_netof.3socket		\
	inet_network.3socket		\
	inet_ntoa.3socket		\
	inet_ntop.3socket		\
	inet_pton.3socket		\
	inet6_opt_append.3socket	\
	inet6_opt_find.3socket		\
	inet6_opt_finish.3socket	\
	inet6_opt_get_val.3socket	\
	inet6_opt_init.3socket		\
	inet6_opt_next.3socket		\
	inet6_opt_set_val.3socket	\
	inet6_rth_add.3socket		\
	inet6_rth_getaddr.3socket	\
	inet6_rth_init.3socket		\
	inet6_rth_reverse.3socket	\
	inet6_rth_segments.3socket	\
	inet6_rth_space.3socket		\
	inet6.3socket			\
	ntohl.3socket			\
	ntohll.3socket			\
	ntohs.3socket			\
	rcmd_af.3socket			\
	recvfrom.3socket		\
	recvmsg.3socket			\
	rexec_af.3socket		\
	rresvport_af.3socket		\
	rresvport.3socket		\
	ruserok.3socket			\
	sctp_freeladdrs.3socket		\
	sctp_freepaddrs.3socket		\
	sendmsg.3socket			\
	sendto.3socket			\
	setipv4sourcefilter.3socket	\
	setnetent.3socket		\
	setprotoent.3socket		\
	setservent.3socket		\
	setsockopt.3socket		\
	setsourcefilter.3socket

endnetent.3socket			:= SOFILE = man3socket/getnetbyname.3socket
endprotoent.3socket			:= SOFILE = man3socket/getprotobyname.3socket
endservent.3socket			:= SOFILE = man3socket/getservbyname.3socket
ether_aton.3socket			:= SOFILE = man3socket/ethers.3socket
ether_hostton.3socket			:= SOFILE = man3socket/ethers.3socket
ether_line.3socket			:= SOFILE = man3socket/ethers.3socket
ether_ntoa.3socket			:= SOFILE = man3socket/ethers.3socket
ether_ntohost.3socket			:= SOFILE = man3socket/ethers.3socket
freeaddrinfo.3socket			:= SOFILE = man3socket/getaddrinfo.3socket
freehostent.3socket			:= SOFILE = man3socket/getipnodebyname.3socket
gai_strerror.3socket			:= SOFILE = man3socket/getaddrinfo.3socket
getipnodebyaddr.3socket			:= SOFILE = man3socket/getipnodebyname.3socket
getipv4sourcefilter.3socket		:= SOFILE = man3socket/getsourcefilter.3socket
getnameinfo.3socket			:= SOFILE = man3socket/getaddrinfo.3socket
getnetbyaddr_r.3socket			:= SOFILE = man3socket/getnetbyname.3socket
getnetbyaddr.3socket			:= SOFILE = man3socket/getnetbyname.3socket
getnetbyname_r.3socket			:= SOFILE = man3socket/getnetbyname.3socket
getnetent_r.3socket			:= SOFILE = man3socket/getnetbyname.3socket
getnetent.3socket			:= SOFILE = man3socket/getnetbyname.3socket
getprotobyname_r.3socket		:= SOFILE = man3socket/getprotobyname.3socket
getprotobynumber_r.3socket		:= SOFILE = man3socket/getprotobyname.3socket
getprotobynumber.3socket		:= SOFILE = man3socket/getprotobyname.3socket
getprotoent_r.3socket			:= SOFILE = man3socket/getprotobyname.3socket
getprotoent.3socket			:= SOFILE = man3socket/getprotobyname.3socket
getservbyname_r.3socket			:= SOFILE = man3socket/getservbyname.3socket
getservbyport_r.3socket			:= SOFILE = man3socket/getservbyname.3socket
getservbyport.3socket			:= SOFILE = man3socket/getservbyname.3socket
getservent_r.3socket			:= SOFILE = man3socket/getservbyname.3socket
getservent.3socket			:= SOFILE = man3socket/getservbyname.3socket
htonl.3socket				:= SOFILE = man3socket/byteorder.3socket
htonll.3socket				:= SOFILE = man3socket/byteorder.3socket
htons.3socket				:= SOFILE = man3socket/byteorder.3socket
if_freenameindex.3socket		:= SOFILE = man3socket/if_nametoindex.3socket
if_indextoname.3socket			:= SOFILE = man3socket/if_nametoindex.3socket
if_nameindex.3socket			:= SOFILE = man3socket/if_nametoindex.3socket
inet_addr.3socket			:= SOFILE = man3socket/inet.3socket
inet_aton.3socket			:= SOFILE = man3socket/inet.3socket
inet_lnaof.3socket			:= SOFILE = man3socket/inet.3socket
inet_makeaddr.3socket			:= SOFILE = man3socket/inet.3socket
inet_netof.3socket			:= SOFILE = man3socket/inet.3socket
inet_network.3socket			:= SOFILE = man3socket/inet.3socket
inet_ntoa.3socket			:= SOFILE = man3socket/inet.3socket
inet_ntop.3socket			:= SOFILE = man3socket/inet.3socket
inet_pton.3socket			:= SOFILE = man3socket/inet.3socket
inet6_opt_append.3socket		:= SOFILE = man3socket/inet6_opt.3socket
inet6_opt_find.3socket			:= SOFILE = man3socket/inet6_opt.3socket
inet6_opt_finish.3socket		:= SOFILE = man3socket/inet6_opt.3socket
inet6_opt_get_val.3socket		:= SOFILE = man3socket/inet6_opt.3socket
inet6_opt_init.3socket			:= SOFILE = man3socket/inet6_opt.3socket
inet6_opt_next.3socket			:= SOFILE = man3socket/inet6_opt.3socket
inet6_opt_set_val.3socket		:= SOFILE = man3socket/inet6_opt.3socket
inet6_rth_add.3socket			:= SOFILE = man3socket/inet6_rth.3socket
inet6_rth_getaddr.3socket		:= SOFILE = man3socket/inet6_rth.3socket
inet6_rth_init.3socket			:= SOFILE = man3socket/inet6_rth.3socket
inet6_rth_reverse.3socket		:= SOFILE = man3socket/inet6_rth.3socket
inet6_rth_segments.3socket		:= SOFILE = man3socket/inet6_rth.3socket
inet6_rth_space.3socket			:= SOFILE = man3socket/inet6_rth.3socket
inet6.3socket				:= SOFILE = man3socket/inet.3socket
ntohl.3socket				:= SOFILE = man3socket/byteorder.3socket
ntohll.3socket				:= SOFILE = man3socket/byteorder.3socket
ntohs.3socket				:= SOFILE = man3socket/byteorder.3socket
rcmd_af.3socket				:= SOFILE = man3socket/rcmd.3socket
recvfrom.3socket			:= SOFILE = man3socket/recv.3socket
recvmsg.3socket				:= SOFILE = man3socket/recv.3socket
rexec_af.3socket			:= SOFILE = man3socket/rexec.3socket
rresvport_af.3socket			:= SOFILE = man3socket/rcmd.3socket
rresvport.3socket			:= SOFILE = man3socket/rcmd.3socket
ruserok.3socket				:= SOFILE = man3socket/rcmd.3socket
sctp_freeladdrs.3socket			:= SOFILE = man3socket/sctp_getladdrs.3socket
sctp_freepaddrs.3socket			:= SOFILE = man3socket/sctp_getpaddrs.3socket
sendmsg.3socket				:= SOFILE = man3socket/send.3socket
sendto.3socket				:= SOFILE = man3socket/send.3socket
setipv4sourcefilter.3socket		:= SOFILE = man3socket/getsourcefilter.3socket
setnetent.3socket			:= SOFILE = man3socket/getnetbyname.3socket
setprotoent.3socket			:= SOFILE = man3socket/getprotobyname.3socket
setservent.3socket			:= SOFILE = man3socket/getservbyname.3socket
setsockopt.3socket			:= SOFILE = man3socket/getsockopt.3socket
setsourcefilter.3socket			:= SOFILE = man3socket/getsourcefilter.3socket

include ../Makefile.man

install: $(ROOTMANFILES)
