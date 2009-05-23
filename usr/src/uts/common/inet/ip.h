/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/* Copyright (c) 1990 Mentat Inc. */

#ifndef	_INET_IP_H
#define	_INET_IP_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/isa_defs.h>
#include <sys/types.h>
#include <inet/mib2.h>
#include <inet/nd.h>
#include <sys/atomic.h>
#include <net/if_dl.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/igmp.h>
#include <sys/neti.h>
#include <sys/hook.h>
#include <sys/hook_event.h>
#include <sys/hook_impl.h>
#include <inet/ip_stack.h>

#ifdef _KERNEL
#include <netinet/ip6.h>
#include <sys/avl.h>
#include <sys/list.h>
#include <sys/vmem.h>
#include <sys/squeue.h>
#include <net/route.h>
#include <sys/systm.h>
#include <sys/multidata.h>
#include <sys/list.h>
#include <net/radix.h>
#include <sys/modhash.h>

#ifdef DEBUG
#define	CONN_DEBUG
#endif

#define	IP_DEBUG
/*
 * The mt-streams(9F) flags for the IP module; put here so that other
 * "drivers" that are actually IP (e.g., ICMP, UDP) can use the same set
 * of flags.
 */
#define	IP_DEVMTFLAGS D_MP
#endif	/* _KERNEL */

#define	IP_MOD_NAME	"ip"
#define	IP_DEV_NAME	"/dev/ip"
#define	IP6_DEV_NAME	"/dev/ip6"

#define	UDP_MOD_NAME	"udp"
#define	UDP_DEV_NAME	"/dev/udp"
#define	UDP6_DEV_NAME	"/dev/udp6"

#define	TCP_MOD_NAME	"tcp"
#define	TCP_DEV_NAME	"/dev/tcp"
#define	TCP6_DEV_NAME	"/dev/tcp6"

#define	SCTP_MOD_NAME	"sctp"

#ifndef	_IPADDR_T
#define	_IPADDR_T
typedef uint32_t ipaddr_t;
#endif

/* Number of bits in an address */
#define	IP_ABITS		32
#define	IPV6_ABITS		128

#define	IP_HOST_MASK		(ipaddr_t)0xffffffffU

#define	IP_CSUM(mp, off, sum)		(~ip_cksum(mp, off, sum) & 0xFFFF)
#define	IP_CSUM_PARTIAL(mp, off, sum)	ip_cksum(mp, off, sum)
#define	IP_BCSUM_PARTIAL(bp, len, sum)	bcksum(bp, len, sum)
#define	IP_MD_CSUM(pd, off, sum)	(~ip_md_cksum(pd, off, sum) & 0xffff)
#define	IP_MD_CSUM_PARTIAL(pd, off, sum) ip_md_cksum(pd, off, sum)

/*
 * Flag to IP write side to indicate that the appln has sent in a pre-built
 * IP header. Stored in ipha_ident (which is otherwise zero).
 */
#define	IP_HDR_INCLUDED			0xFFFF

#define	ILL_FRAG_HASH_TBL_COUNT	((unsigned int)64)
#define	ILL_FRAG_HASH_TBL_SIZE	(ILL_FRAG_HASH_TBL_COUNT * sizeof (ipfb_t))

#define	IPV4_ADDR_LEN			4
#define	IP_ADDR_LEN			IPV4_ADDR_LEN
#define	IP_ARP_PROTO_TYPE		0x0800

#define	IPV4_VERSION			4
#define	IP_VERSION			IPV4_VERSION
#define	IP_SIMPLE_HDR_LENGTH_IN_WORDS	5
#define	IP_SIMPLE_HDR_LENGTH		20
#define	IP_MAX_HDR_LENGTH		60

#define	IP_MAX_OPT_LENGTH (IP_MAX_HDR_LENGTH-IP_SIMPLE_HDR_LENGTH)

#define	IP_MIN_MTU			(IP_MAX_HDR_LENGTH + 8)	/* 68 bytes */

/*
 * XXX IP_MAXPACKET is defined in <netinet/ip.h> as well. At some point the
 * 2 files should be cleaned up to remove all redundant definitions.
 */
#define	IP_MAXPACKET			65535
#define	IP_SIMPLE_HDR_VERSION \
	((IP_VERSION << 4) | IP_SIMPLE_HDR_LENGTH_IN_WORDS)

#define	UDPH_SIZE			8

/* Leave room for ip_newroute to tack on the src and target addresses */
#define	OK_RESOLVER_MP(mp)						\
	((mp) && ((mp)->b_wptr - (mp)->b_rptr) >= (2 * IP_ADDR_LEN))

/*
 * Constants and type definitions to support IP IOCTL commands
 */
#define	IP_IOCTL			(('i'<<8)|'p')
#define	IP_IOC_IRE_DELETE		4
#define	IP_IOC_IRE_DELETE_NO_REPLY	5
#define	IP_IOC_IRE_ADVISE_NO_REPLY	6
#define	IP_IOC_RTS_REQUEST		7

/* Common definitions used by IP IOCTL data structures */
typedef struct ipllcmd_s {
	uint_t	ipllc_cmd;
	uint_t	ipllc_name_offset;
	uint_t	ipllc_name_length;
} ipllc_t;

/* IP IRE Change Command Structure. */
typedef struct ipic_s {
	ipllc_t	ipic_ipllc;
	uint_t	ipic_ire_type;
	uint_t	ipic_max_frag;
	uint_t	ipic_addr_offset;
	uint_t	ipic_addr_length;
	uint_t	ipic_mask_offset;
	uint_t	ipic_mask_length;
	uint_t	ipic_src_addr_offset;
	uint_t	ipic_src_addr_length;
	uint_t	ipic_ll_hdr_offset;
	uint_t	ipic_ll_hdr_length;
	uint_t	ipic_gateway_addr_offset;
	uint_t	ipic_gateway_addr_length;
	clock_t	ipic_rtt;
	uint32_t ipic_ssthresh;
	clock_t	ipic_rtt_sd;
	uchar_t ipic_ire_marks;
} ipic_t;

#define	ipic_cmd		ipic_ipllc.ipllc_cmd
#define	ipic_ll_name_length	ipic_ipllc.ipllc_name_length
#define	ipic_ll_name_offset	ipic_ipllc.ipllc_name_offset

/* IP IRE Delete Command Structure. */
typedef struct ipid_s {
	ipllc_t	ipid_ipllc;
	uint_t	ipid_ire_type;
	uint_t	ipid_addr_offset;
	uint_t	ipid_addr_length;
	uint_t	ipid_mask_offset;
	uint_t	ipid_mask_length;
} ipid_t;

#define	ipid_cmd		ipid_ipllc.ipllc_cmd

#ifdef _KERNEL
/*
 * Temporary state for ip options parser.
 */
typedef struct ipoptp_s
{
	uint8_t		*ipoptp_next;	/* next option to look at */
	uint8_t		*ipoptp_end;	/* end of options */
	uint8_t		*ipoptp_cur;	/* start of current option */
	uint8_t		ipoptp_len;	/* length of current option */
	uint32_t	ipoptp_flags;
} ipoptp_t;

/*
 * Flag(s) for ipoptp_flags
 */
#define	IPOPTP_ERROR	0x00000001
#endif	/* _KERNEL */

/* Controls forwarding of IP packets, set via ndd */
#define	IP_FORWARD_NEVER	0
#define	IP_FORWARD_ALWAYS	1

#define	WE_ARE_FORWARDING(ipst)	((ipst)->ips_ip_g_forward == IP_FORWARD_ALWAYS)

#define	IPH_HDR_LENGTH(ipha)						\
	((int)(((ipha_t *)ipha)->ipha_version_and_hdr_length & 0xF) << 2)

#define	IPH_HDR_VERSION(ipha)						\
	((int)(((ipha_t *)ipha)->ipha_version_and_hdr_length) >> 4)

#ifdef _KERNEL
/*
 * IP reassembly macros.  We hide starting and ending offsets in b_next and
 * b_prev of messages on the reassembly queue.	The messages are chained using
 * b_cont.  These macros are used in ip_reassemble() so we don't have to see
 * the ugly casts and assignments.
 * Note that the offsets are <= 64k i.e. a uint_t is sufficient to represent
 * them.
 */
#define	IP_REASS_START(mp)		((uint_t)(uintptr_t)((mp)->b_next))
#define	IP_REASS_SET_START(mp, u)	\
	((mp)->b_next = (mblk_t *)(uintptr_t)(u))
#define	IP_REASS_END(mp)		((uint_t)(uintptr_t)((mp)->b_prev))
#define	IP_REASS_SET_END(mp, u)		\
	((mp)->b_prev = (mblk_t *)(uintptr_t)(u))

#define	IP_REASS_COMPLETE	0x1
#define	IP_REASS_PARTIAL	0x2
#define	IP_REASS_FAILED		0x4

/*
 * Test to determine whether this is a module instance of IP or a
 * driver instance of IP.
 */
#define	CONN_Q(q)	(WR(q)->q_next == NULL)

#define	Q_TO_CONN(q)	((conn_t *)(q)->q_ptr)
#define	Q_TO_TCP(q)	(Q_TO_CONN((q))->conn_tcp)
#define	Q_TO_UDP(q)	(Q_TO_CONN((q))->conn_udp)
#define	Q_TO_ICMP(q)	(Q_TO_CONN((q))->conn_icmp)
#define	Q_TO_RTS(q)	(Q_TO_CONN((q))->conn_rts)

/*
 * The following two macros are used by IP to get the appropriate
 * wq and rq for a conn. If it is a TCP conn, then we need
 * tcp_wq/tcp_rq else, conn_wq/conn_rq. IP can use conn_wq and conn_rq
 * from a conn directly if it knows that the conn is not TCP.
 */
#define	CONNP_TO_WQ(connp)	\
	(IPCL_IS_TCP(connp) ? (connp)->conn_tcp->tcp_wq : (connp)->conn_wq)

#define	CONNP_TO_RQ(connp)	RD(CONNP_TO_WQ(connp))

#define	GRAB_CONN_LOCK(q)	{				\
	if (q != NULL && CONN_Q(q))				\
		mutex_enter(&(Q_TO_CONN(q))->conn_lock);	\
}

#define	RELEASE_CONN_LOCK(q)	{				\
	if (q != NULL && CONN_Q(q))				\
		mutex_exit(&(Q_TO_CONN(q))->conn_lock);		\
}

/* "Congestion controlled" protocol */
#define	IP_FLOW_CONTROLLED_ULP(p)   ((p) == IPPROTO_TCP || (p) == IPPROTO_SCTP)

/*
 * Complete the pending operation. Usually an ioctl. Can also
 * be a bind or option management request that got enqueued
 * in an ipsq_t. Called on completion of the operation.
 */
#define	CONN_OPER_PENDING_DONE(connp)	{			\
	mutex_enter(&(connp)->conn_lock);			\
	(connp)->conn_oper_pending_ill = NULL;			\
	cv_broadcast(&(connp)->conn_refcv);			\
	mutex_exit(&(connp)->conn_lock);			\
	CONN_DEC_REF(connp);					\
}

/*
 * Flags for the various ip_fanout_* routines.
 */
#define	IP_FF_SEND_ICMP		0x01	/* Send an ICMP error */
#define	IP_FF_HDR_COMPLETE	0x02	/* Call ip_hdr_complete if error */
#define	IP_FF_CKSUM		0x04	/* Recompute ipha_cksum if error */
#define	IP_FF_RAWIP		0x08	/* Use rawip mib variable */
#define	IP_FF_SRC_QUENCH	0x10	/* OK to send ICMP_SOURCE_QUENCH */
#define	IP_FF_SYN_ADDIRE	0x20	/* Add IRE if TCP syn packet */
#define	IP_FF_IPINFO		0x80	/* Used for both V4 and V6 */
#define	IP_FF_SEND_SLLA		0x100	/* Send source link layer info ? */
#define	IPV6_REACHABILITY_CONFIRMATION	0x200	/* Flags for ip_xmit_v6 */
#define	IP_FF_NO_MCAST_LOOP	0x400	/* No multicasts for sending zone */

/*
 * Following flags are used by IPQoS to determine if policy processing is
 * required.
 */
#define	IP6_NO_IPPOLICY		0x800	/* Don't do IPQoS processing */
#define	IP6_IN_LLMCAST		0x1000	/* Multicast */

#define	IP_FF_LOOPBACK		0x2000	/* Loopback fanout */
#define	IP_FF_SCTP_CSUM_ERR	0x4000	/* sctp pkt has failed chksum */

#ifndef	IRE_DB_TYPE
#define	IRE_DB_TYPE	M_SIG
#endif

#ifndef	IRE_DB_REQ_TYPE
#define	IRE_DB_REQ_TYPE	M_PCSIG
#endif

#ifndef	IRE_ARPRESOLVE_TYPE
#define	IRE_ARPRESOLVE_TYPE	M_EVENT
#endif

/*
 * Values for squeue switch:
 */

#define	IP_SQUEUE_ENTER_NODRAIN	1
#define	IP_SQUEUE_ENTER	2
/*
 * This is part of the interface between Transport provider and
 * IP which can be used to set policy information. This is usually
 * accompanied with O_T_BIND_REQ/T_BIND_REQ.ip_bind assumes that
 * only IPSEC_POLICY_SET is there when it is found in the chain.
 * The information contained is an struct ipsec_req_t. On success
 * or failure, either the T_BIND_ACK or the T_ERROR_ACK is returned.
 * IPSEC_POLICY_SET is never returned.
 */
#define	IPSEC_POLICY_SET	M_SETOPTS

#define	IRE_IS_LOCAL(ire)	((ire != NULL) && \
				((ire)->ire_type & (IRE_LOCAL | IRE_LOOPBACK)))

#define	IRE_IS_TARGET(ire)	((ire != NULL) && \
				((ire)->ire_type != IRE_BROADCAST))

/* IP Fragmentation Reassembly Header */
typedef struct ipf_s {
	struct ipf_s	*ipf_hash_next;
	struct ipf_s	**ipf_ptphn;	/* Pointer to previous hash next. */
	uint32_t	ipf_ident;	/* Ident to match. */
	uint8_t		ipf_protocol;	/* Protocol to match. */
	uchar_t		ipf_last_frag_seen : 1;	/* Last fragment seen ? */
	time_t		ipf_timestamp;	/* Reassembly start time. */
	mblk_t		*ipf_mp;	/* mblk we live in. */
	mblk_t		*ipf_tail_mp;	/* Frag queue tail pointer. */
	int		ipf_hole_cnt;	/* Number of holes (hard-case). */
	int		ipf_end;	/* Tail end offset (0 -> hard-case). */
	uint_t		ipf_gen;	/* Frag queue generation */
	size_t		ipf_count;	/* Count of bytes used by frag */
	uint_t		ipf_nf_hdr_len; /* Length of nonfragmented header */
	in6_addr_t	ipf_v6src;	/* IPv6 source address */
	in6_addr_t	ipf_v6dst;	/* IPv6 dest address */
	uint_t		ipf_prev_nexthdr_offset; /* Offset for nexthdr value */
	uint8_t		ipf_ecn;	/* ECN info for the fragments */
	uint8_t		ipf_num_dups;	/* Number of times dup frags recvd */
	uint16_t	ipf_checksum_flags; /* Hardware checksum flags */
	uint32_t	ipf_checksum;	/* Partial checksum of fragment data */
} ipf_t;

/*
 * IPv4 Fragments
 */
#define	IS_V4_FRAGMENT(ipha_fragment_offset_and_flags)			\
	(((ntohs(ipha_fragment_offset_and_flags) & IPH_OFFSET) != 0) ||	\
	((ntohs(ipha_fragment_offset_and_flags) & IPH_MF) != 0))

#define	ipf_src	V4_PART_OF_V6(ipf_v6src)
#define	ipf_dst	V4_PART_OF_V6(ipf_v6dst)

typedef enum {
	IB_PKT = 0x01,
	OB_PKT = 0x02
} ip_pkt_t;

#define	UPDATE_IB_PKT_COUNT(ire)\
	{ \
	(ire)->ire_ib_pkt_count++; \
	if ((ire)->ire_ipif != NULL) { \
		/* \
		 * forwarding packet \
		 */ \
		if ((ire)->ire_type & (IRE_LOCAL|IRE_BROADCAST)) \
			atomic_add_32(&(ire)->ire_ipif->ipif_ib_pkt_count, 1);\
		else \
			atomic_add_32(&(ire)->ire_ipif->ipif_fo_pkt_count, 1);\
	} \
	}

#define	UPDATE_OB_PKT_COUNT(ire)\
	{ \
	(ire)->ire_ob_pkt_count++;\
	if ((ire)->ire_ipif != NULL) { \
		atomic_add_32(&(ire)->ire_ipif->ipif_ob_pkt_count, 1); \
	} \
	}

#define	IP_RPUT_LOCAL(q, mp, ipha, ire, recv_ill) \
{ \
	switch (ipha->ipha_protocol) { \
		case IPPROTO_UDP: \
			ip_udp_input(q, mp, ipha, ire, recv_ill); \
			break; \
		default: \
			ip_proto_input(q, mp, ipha, ire, recv_ill, 0); \
			break; \
	} \
}

/*
 * NCE_EXPIRED is TRUE when we have a non-permanent nce that was
 * found to be REACHABLE more than ip_ire_arp_interval ms ago.
 * This macro is used to age existing nce_t entries. The
 * nce's will get cleaned up in the following circumstances:
 * - ip_ire_trash_reclaim will free nce's using ndp_cache_reclaim
 *    when memory is low,
 * - ip_arp_news, when updates are received.
 * - if the nce is NCE_EXPIRED(), it will deleted, so that a new
 *   arp request will need to be triggered from an ND_INITIAL nce.
 *
 * Note that the nce state transition follows the pattern:
 *	ND_INITIAL -> ND_INCOMPLETE -> ND_REACHABLE
 * after which the nce is deleted when it has expired.
 *
 * nce_last is the timestamp that indicates when the nce_res_mp in the
 * nce_t was last updated to a valid link-layer address.  nce_last gets
 * modified/updated :
 *  - when the nce is created
 *  - every time we get a sane arp response for the nce.
 */
#define	NCE_EXPIRED(nce, ipst)	(nce->nce_last > 0 &&	\
	    ((nce->nce_flags & NCE_F_PERMANENT) == 0) &&	\
	    ((TICK_TO_MSEC(lbolt64) - nce->nce_last) > 		\
		(ipst)->ips_ip_ire_arp_interval))

#endif /* _KERNEL */

/* ICMP types */
#define	ICMP_ECHO_REPLY			0
#define	ICMP_DEST_UNREACHABLE		3
#define	ICMP_SOURCE_QUENCH		4
#define	ICMP_REDIRECT			5
#define	ICMP_ECHO_REQUEST		8
#define	ICMP_ROUTER_ADVERTISEMENT	9
#define	ICMP_ROUTER_SOLICITATION	10
#define	ICMP_TIME_EXCEEDED		11
#define	ICMP_PARAM_PROBLEM		12
#define	ICMP_TIME_STAMP_REQUEST		13
#define	ICMP_TIME_STAMP_REPLY		14
#define	ICMP_INFO_REQUEST		15
#define	ICMP_INFO_REPLY			16
#define	ICMP_ADDRESS_MASK_REQUEST	17
#define	ICMP_ADDRESS_MASK_REPLY		18

/* ICMP_TIME_EXCEEDED codes */
#define	ICMP_TTL_EXCEEDED		0
#define	ICMP_REASSEMBLY_TIME_EXCEEDED	1

/* ICMP_DEST_UNREACHABLE codes */
#define	ICMP_NET_UNREACHABLE		0
#define	ICMP_HOST_UNREACHABLE		1
#define	ICMP_PROTOCOL_UNREACHABLE	2
#define	ICMP_PORT_UNREACHABLE		3
#define	ICMP_FRAGMENTATION_NEEDED	4
#define	ICMP_SOURCE_ROUTE_FAILED	5
#define	ICMP_DEST_NET_UNKNOWN		6
#define	ICMP_DEST_HOST_UNKNOWN		7
#define	ICMP_SRC_HOST_ISOLATED		8
#define	ICMP_DEST_NET_UNREACH_ADMIN	9
#define	ICMP_DEST_HOST_UNREACH_ADMIN	10
#define	ICMP_DEST_NET_UNREACH_TOS	11
#define	ICMP_DEST_HOST_UNREACH_TOS	12

/* ICMP Header Structure */
typedef struct icmph_s {
	uint8_t		icmph_type;
	uint8_t		icmph_code;
	uint16_t	icmph_checksum;
	union {
		struct { /* ECHO request/response structure */
			uint16_t	u_echo_ident;
			uint16_t	u_echo_seqnum;
		} u_echo;
		struct { /* Destination unreachable structure */
			uint16_t	u_du_zero;
			uint16_t	u_du_mtu;
		} u_du;
		struct { /* Parameter problem structure */
			uint8_t		u_pp_ptr;
			uint8_t		u_pp_rsvd[3];
		} u_pp;
		struct { /* Redirect structure */
			ipaddr_t	u_rd_gateway;
		} u_rd;
	} icmph_u;
} icmph_t;

#define	icmph_echo_ident	icmph_u.u_echo.u_echo_ident
#define	icmph_echo_seqnum	icmph_u.u_echo.u_echo_seqnum
#define	icmph_du_zero		icmph_u.u_du.u_du_zero
#define	icmph_du_mtu		icmph_u.u_du.u_du_mtu
#define	icmph_pp_ptr		icmph_u.u_pp.u_pp_ptr
#define	icmph_rd_gateway	icmph_u.u_rd.u_rd_gateway

#define	ICMPH_SIZE	8

/*
 * Minimum length of transport layer header included in an ICMP error
 * message for it to be considered valid.
 */
#define	ICMP_MIN_TP_HDR_LEN	8

/* Aligned IP header */
typedef struct ipha_s {
	uint8_t		ipha_version_and_hdr_length;
	uint8_t		ipha_type_of_service;
	uint16_t	ipha_length;
	uint16_t	ipha_ident;
	uint16_t	ipha_fragment_offset_and_flags;
	uint8_t		ipha_ttl;
	uint8_t		ipha_protocol;
	uint16_t	ipha_hdr_checksum;
	ipaddr_t	ipha_src;
	ipaddr_t	ipha_dst;
} ipha_t;

/*
 * IP Flags
 *
 * Some of these constant names are copied for the DTrace IP provider in
 * usr/src/lib/libdtrace/common/{ip.d.in, ip.sed.in}, which should be kept
 * in sync.
 */
#define	IPH_DF		0x4000	/* Don't fragment */
#define	IPH_MF		0x2000	/* More fragments to come */
#define	IPH_OFFSET	0x1FFF	/* Where the offset lives */
#define	IPH_FRAG_HDR	0x8000	/* IPv6 don't fragment bit */

/* ECN code points for IPv4 TOS byte and IPv6 traffic class octet. */
#define	IPH_ECN_NECT	0x0	/* Not ECN-Capable Transport */
#define	IPH_ECN_ECT1	0x1	/* ECN-Capable Transport, ECT(1) */
#define	IPH_ECN_ECT0	0x2	/* ECN-Capable Transport, ECT(0) */
#define	IPH_ECN_CE	0x3	/* ECN-Congestion Experienced (CE) */

struct ill_s;

typedef	boolean_t ip_v6intfid_func_t(struct ill_s *, in6_addr_t *);
typedef	boolean_t ip_v6mapinfo_func_t(uint_t, uint8_t *, uint8_t *, uint32_t *,
    in6_addr_t *);
typedef boolean_t ip_v4mapinfo_func_t(uint_t, uint8_t *, uint8_t *, uint32_t *,
    ipaddr_t *);

/* IP Mac info structure */
typedef struct ip_m_s {
	t_uscalar_t		ip_m_mac_type;	/* From <sys/dlpi.h> */
	int			ip_m_type;	/* From <net/if_types.h> */
	ip_v4mapinfo_func_t	*ip_m_v4mapinfo;
	ip_v6mapinfo_func_t	*ip_m_v6mapinfo;
	ip_v6intfid_func_t	*ip_m_v6intfid;
} ip_m_t;

/*
 * The following functions attempt to reduce the link layer dependency
 * of the IP stack. The current set of link specific operations are:
 * a. map from IPv4 class D (224.0/4) multicast address range to the link
 * layer multicast address range.
 * b. map from IPv6 multicast address range (ff00::/8) to the link
 * layer multicast address range.
 * c. derive the default IPv6 interface identifier from the interface.
 * d. derive the default IPv6 destination interface identifier from
 * the interface (point-to-point only).
 */
#define	MEDIA_V4MINFO(ip_m, plen, bphys, maddr, hwxp, v4ptr) \
	(((ip_m)->ip_m_v4mapinfo != NULL) && \
	(*(ip_m)->ip_m_v4mapinfo)(plen, bphys, maddr, hwxp, v4ptr))
#define	MEDIA_V6MINFO(ip_m, plen, bphys, maddr, hwxp, v6ptr) \
	(((ip_m)->ip_m_v6mapinfo != NULL) && \
	(*(ip_m)->ip_m_v6mapinfo)(plen, bphys, maddr, hwxp, v6ptr))
#define	MEDIA_V6INTFID(ip_m, ill, v6ptr) \
	(((ip_m)->ip_m_v6intfid != NULL) && \
	(*(ip_m)->ip_m_v6intfid)(ill, v6ptr))
#define	MEDIA_V6DESTINTFID(ip_m, ill, v6ptr) \
	(((ip_m)->ip_m_v6destintfid != NULL) && \
	(*(ip_m)->ip_m_v6destintfid)(ill, v6ptr))

/* Router entry types */
#define	IRE_BROADCAST		0x0001	/* Route entry for broadcast address */
#define	IRE_DEFAULT		0x0002	/* Route entry for default gateway */
#define	IRE_LOCAL		0x0004	/* Route entry for local address */
#define	IRE_LOOPBACK		0x0008	/* Route entry for loopback address */
#define	IRE_PREFIX		0x0010	/* Route entry for prefix routes */
#define	IRE_CACHE		0x0020	/* Cached Route entry */
#define	IRE_IF_NORESOLVER	0x0040	/* Route entry for local interface */
					/* net without any address mapping. */
#define	IRE_IF_RESOLVER		0x0080	/* Route entry for local interface */
					/* net with resolver. */
#define	IRE_HOST		0x0100	/* Host route entry */
#define	IRE_HOST_REDIRECT	0x0200	/* only used for T_SVR4_OPTMGMT_REQ */

#define	IRE_INTERFACE		(IRE_IF_NORESOLVER | IRE_IF_RESOLVER)
#define	IRE_OFFSUBNET		(IRE_DEFAULT | IRE_PREFIX | IRE_HOST)
#define	IRE_CACHETABLE		(IRE_CACHE | IRE_BROADCAST | IRE_LOCAL | \
				IRE_LOOPBACK)
#define	IRE_FORWARDTABLE	(IRE_INTERFACE | IRE_OFFSUBNET)

/*
 * If an IRE is marked with IRE_MARK_CONDEMNED, the last walker of
 * the bucket should delete this IRE from this bucket.
 */
#define	IRE_MARK_CONDEMNED	0x0001

/*
 * An IRE with IRE_MARK_PMTU has ire_max_frag set from an ICMP error.
 */
#define	IRE_MARK_PMTU		0x0002

/*
 * An IRE with IRE_MARK_TESTHIDDEN is used by in.mpathd for test traffic.  It
 * can only be looked up by requesting MATCH_IRE_MARK_TESTHIDDEN.
 */
#define	IRE_MARK_TESTHIDDEN	0x0004

/*
 * An IRE with IRE_MARK_NOADD is created in ip_newroute_ipif when the outgoing
 * interface is specified by e.g. IP_PKTINFO.  The IRE is not added to the IRE
 * cache table.
 */
#define	IRE_MARK_NOADD		0x0008	/* Mark not to add ire in cache */

/*
 * IRE marked with IRE_MARK_TEMPORARY means that this IRE has been used
 * either for forwarding a packet or has not been used for sending
 * traffic on TCP connections terminated on this system.  In both
 * cases, this IRE is the first to go when IRE is being cleaned up.
 */
#define	IRE_MARK_TEMPORARY	0x0010

/*
 * IRE marked with IRE_MARK_USESRC_CHECK means that while adding an IRE with
 * this mark, additional atomic checks need to be performed. For eg: by the
 * time an IRE_CACHE is created, sent up to ARP and then comes back to IP; the
 * usesrc grouping could have changed in which case we want to fail adding
 * the IRE_CACHE entry
 */
#define	IRE_MARK_USESRC_CHECK	0x0020

/*
 * IRE_MARK_PRIVATE_ADDR is used for IP_NEXTHOP. When IP_NEXTHOP is set, the
 * routing table lookup for the destination is bypassed and the packet is
 * sent directly to the specified nexthop. The associated IRE_CACHE entries
 * should be marked with IRE_MARK_PRIVATE_ADDR flag so that they don't show up
 * in regular ire cache lookups.
 */
#define	IRE_MARK_PRIVATE_ADDR	0x0040

/*
 * When we send an ARP resolution query for the nexthop gateway's ire,
 * we use esballoc to create the ire_t in the AR_ENTRY_QUERY mblk
 * chain, and mark its ire_marks with IRE_MARK_UNCACHED. This flag
 * indicates that information from ARP has not been transferred to a
 * permanent IRE_CACHE entry. The flag is reset only when the
 * information is successfully transferred to an ire_cache entry (in
 * ire_add()). Attempting to free the AR_ENTRY_QUERY mblk chain prior
 * to ire_add (e.g., from arp, or from ip`ip_wput_nondata) will
 * require that the resources (incomplete ire_cache and/or nce) must
 * be cleaned up. The free callback routine (ire_freemblk()) checks
 * for IRE_MARK_UNCACHED to see if any resources that are pinned down
 * will need to be cleaned up or not.
 */

#define	IRE_MARK_UNCACHED	0x0080

/*
 * The comment below (and for other netstack_t references) refers
 * to the fact that we only do netstack_hold in particular cases,
 * such as the references from open streams (ill_t and conn_t's
 * pointers). Internally within IP we rely on IP's ability to cleanup e.g.
 * ire_t's when an ill goes away.
 */
typedef struct ire_expire_arg_s {
	int		iea_flush_flag;
	ip_stack_t	*iea_ipst;	/* Does not have a netstack_hold */
} ire_expire_arg_t;

/* Flags with ire_expire routine */
#define	FLUSH_ARP_TIME		0x0001	/* ARP info potentially stale timer */
#define	FLUSH_REDIRECT_TIME	0x0002	/* Redirects potentially stale */
#define	FLUSH_MTU_TIME		0x0004	/* Include path MTU per RFC 1191 */

/* Arguments to ire_flush_cache() */
#define	IRE_FLUSH_DELETE	0
#define	IRE_FLUSH_ADD		1

/*
 * Open/close synchronization flags.
 * These are kept in a separate field in the conn and the synchronization
 * depends on the atomic 32 bit access to that field.
 */
#define	CONN_CLOSING		0x01	/* ip_close waiting for ip_wsrv */
#define	CONN_IPSEC_LOAD_WAIT	0x02	/* waiting for load */
#define	CONN_CONDEMNED		0x04	/* conn is closing, no more refs */
#define	CONN_INCIPIENT		0x08	/* conn not yet visible, no refs */
#define	CONN_QUIESCED		0x10	/* conn is now quiescent */

/* Used to check connection state flags before caching the IRE */
#define	CONN_CACHE_IRE(connp)	\
	(!((connp)->conn_state_flags & (CONN_CLOSING|CONN_CONDEMNED)))

/*
 * Parameter to ip_output giving the identity of the caller.
 * IP_WSRV means the packet was enqueued in the STREAMS queue
 * due to flow control and is now being reprocessed in the context of
 * the STREAMS service procedure, consequent to flow control relief.
 * IRE_SEND means the packet is being reprocessed consequent to an
 * ire cache creation and addition and this may or may not be happening
 * in the service procedure context. Anything other than the above 2
 * cases is identified as IP_WPUT. Most commonly this is the case of
 * packets coming down from the application.
 */
#ifdef _KERNEL
#define	IP_WSRV			1	/* Called from ip_wsrv */
#define	IP_WPUT			2	/* Called from ip_wput */
#define	IRE_SEND		3	/* Called from ire_send */

/*
 * Extra structures need for per-src-addr filtering (IGMPv3/MLDv2)
 */
#define	MAX_FILTER_SIZE	64

typedef struct slist_s {
	int		sl_numsrc;
	in6_addr_t	sl_addr[MAX_FILTER_SIZE];
} slist_t;

/*
 * Following struct is used to maintain retransmission state for
 * a multicast group.  One rtx_state_t struct is an in-line field
 * of the ilm_t struct; the slist_ts in the rtx_state_t struct are
 * alloc'd as needed.
 */
typedef struct rtx_state_s {
	uint_t		rtx_timer;	/* retrans timer */
	int		rtx_cnt;	/* retrans count */
	int		rtx_fmode_cnt;	/* retrans count for fmode change */
	slist_t		*rtx_allow;
	slist_t		*rtx_block;
} rtx_state_t;

/*
 * Used to construct list of multicast address records that will be
 * sent in a single listener report.
 */
typedef struct mrec_s {
	struct mrec_s	*mrec_next;
	uint8_t		mrec_type;
	uint8_t		mrec_auxlen;	/* currently unused */
	in6_addr_t	mrec_group;
	slist_t		mrec_srcs;
} mrec_t;

/* Group membership list per upper conn */
/*
 * XXX add ilg info for ifaddr/ifindex.
 * XXX can we make ilg survive an ifconfig unplumb + plumb
 * by setting the ipif/ill to NULL and recover that later?
 *
 * ilg_ipif is used by IPv4 as multicast groups are joined using an interface
 * address (ipif).
 * ilg_ill is used by IPv6 as multicast groups are joined using an interface
 * index (phyint->phyint_ifindex).
 * ilg_ill is NULL for IPv4 and ilg_ipif is NULL for IPv6.
 *
 * ilg records the state of multicast memberships of a socket end point.
 * ilm records the state of multicast memberships with the driver and is
 * maintained per interface.
 *
 * There is no direct link between a given ilg and ilm. If the
 * application has joined a group G with ifindex I, we will have
 * an ilg with ilg_v6group and ilg_ill. There will be a corresponding
 * ilm with ilm_ill/ilm_v6addr recording the multicast membership.
 * To delete the membership:
 *
 *	a) Search for ilg matching on G and I with ilg_v6group
 *	   and ilg_ill. Delete ilg_ill.
 *	b) Search the corresponding ilm matching on G and I with
 *	   ilm_v6addr and ilm_ill. Delete ilm.
 *
 * For IPv4 the only difference is that we look using ipifs, not ills.
 */

/*
 * The ilg_t and ilm_t members are protected by ipsq. They can be changed only
 * by a thread executing in the ipsq. In other words add/delete of a
 * multicast group has to execute in the ipsq.
 */
#define	ILG_DELETED	0x1		/* ilg_flags */
typedef struct ilg_s {
	in6_addr_t	ilg_v6group;
	struct ipif_s	*ilg_ipif;	/* Logical interface we are member on */
	struct ill_s	*ilg_ill;	/* Used by IPv6 */
	uint_t		ilg_flags;
	mcast_record_t	ilg_fmode;	/* MODE_IS_INCLUDE/MODE_IS_EXCLUDE */
	slist_t		*ilg_filter;
} ilg_t;

/*
 * Multicast address list entry for ill.
 * ilm_ipif is used by IPv4 as multicast groups are joined using ipif.
 * ilm_ill is used by IPv6 as multicast groups are joined using ill.
 * ilm_ill is NULL for IPv4 and ilm_ipif is NULL for IPv6.
 *
 * The comment below (and for other netstack_t references) refers
 * to the fact that we only do netstack_hold in particular cases,
 * such as the references from open streams (ill_t and conn_t's
 * pointers). Internally within IP we rely on IP's ability to cleanup e.g.
 * ire_t's when an ill goes away.
 */
#define	ILM_DELETED	0x1		/* ilm_flags */
typedef struct ilm_s {
	in6_addr_t	ilm_v6addr;
	int		ilm_refcnt;
	uint_t		ilm_timer;	/* IGMP/MLD query resp timer, in msec */
	struct ipif_s	*ilm_ipif;	/* Back pointer to ipif for IPv4 */
	struct ilm_s	*ilm_next;	/* Linked list for each ill */
	uint_t		ilm_state;	/* state of the membership */
	struct ill_s	*ilm_ill;	/* Back pointer to ill for IPv6 */
	uint_t		ilm_flags;
	boolean_t	ilm_notify_driver; /* Need to notify the driver */
	zoneid_t	ilm_zoneid;
	int		ilm_no_ilg_cnt;	/* number of joins w/ no ilg */
	mcast_record_t	ilm_fmode;	/* MODE_IS_INCLUDE/MODE_IS_EXCLUDE */
	slist_t		*ilm_filter;	/* source filter list */
	slist_t		*ilm_pendsrcs;	/* relevant src addrs for pending req */
	rtx_state_t	ilm_rtx;	/* SCR retransmission state */
	ip_stack_t	*ilm_ipst;	/* Does not have a netstack_hold */
} ilm_t;

#define	ilm_addr	V4_PART_OF_V6(ilm_v6addr)

typedef struct ilm_walker {
	struct ill_s	*ilw_ill;	/* associated ill */
	struct ill_s	*ilw_ipmp_ill; 	/* associated ipmp ill (if any) */
	struct ill_s	*ilw_walk_ill; 	/* current ill being walked */
} ilm_walker_t;

/*
 * Soft reference to an IPsec SA.
 *
 * On relative terms, conn's can be persistent (living as long as the
 * processes which create them), while SA's are ephemeral (dying when
 * they hit their time-based or byte-based lifetimes).
 *
 * We could hold a hard reference to an SA from an ipsec_latch_t,
 * but this would cause expired SA's to linger for a potentially
 * unbounded time.
 *
 * Instead, we remember the hash bucket number and bucket generation
 * in addition to the pointer.  The bucket generation is incremented on
 * each deletion.
 */
typedef struct ipsa_ref_s
{
	struct ipsa_s	*ipsr_sa;
	struct isaf_s	*ipsr_bucket;
	uint64_t	ipsr_gen;
} ipsa_ref_t;

/*
 * IPsec "latching" state.
 *
 * In the presence of IPsec policy, fully-bound conn's bind a connection
 * to more than just the 5-tuple, but also a specific IPsec action and
 * identity-pair.
 *
 * As an optimization, we also cache soft references to IPsec SA's
 * here so that we can fast-path around most of the work needed for
 * outbound IPsec SA selection.
 *
 * Were it not for TCP's detached connections, this state would be
 * in-line in conn_t; instead, this is in a separate structure so it
 * can be handed off to TCP when a connection is detached.
 */
typedef struct ipsec_latch_s
{
	kmutex_t	ipl_lock;
	uint32_t	ipl_refcnt;

	uint64_t	ipl_unique;
	struct ipsec_policy_s	*ipl_in_policy; /* latched policy (in) */
	struct ipsec_policy_s	*ipl_out_policy; /* latched policy (out) */
	struct ipsec_action_s	*ipl_in_action;	/* latched action (in) */
	struct ipsec_action_s	*ipl_out_action; /* latched action (out) */
	cred_t		*ipl_local_id;
	struct ipsid_s	*ipl_local_cid;
	struct ipsid_s	*ipl_remote_cid;
	unsigned int
			ipl_out_action_latched : 1,
			ipl_in_action_latched : 1,
			ipl_out_policy_latched : 1,
			ipl_in_policy_latched : 1,

			ipl_ids_latched : 1,

			ipl_pad_to_bit_31 : 27;

	ipsa_ref_t	ipl_ref[2]; /* 0: ESP, 1: AH */

} ipsec_latch_t;

#define	IPLATCH_REFHOLD(ipl) { \
	atomic_add_32(&(ipl)->ipl_refcnt, 1);		\
	ASSERT((ipl)->ipl_refcnt != 0);			\
}

#define	IPLATCH_REFRELE(ipl, ns) {				\
	ASSERT((ipl)->ipl_refcnt != 0);				\
	membar_exit();						\
	if (atomic_add_32_nv(&(ipl)->ipl_refcnt, -1) == 0)	\
		iplatch_free(ipl, ns);			\
}

/*
 * peer identity structure.
 */

typedef struct conn_s conn_t;

/*
 * The old IP client structure "ipc_t" is gone. All the data is stored in the
 * connection structure "conn_t" now. The mapping of old and new fields looks
 * like this:
 *
 * ipc_ulp			conn_ulp
 * ipc_rq			conn_rq
 * ipc_wq			conn_wq
 *
 * ipc_laddr			conn_src
 * ipc_faddr			conn_rem
 * ipc_v6laddr			conn_srcv6
 * ipc_v6faddr			conn_remv6
 *
 * ipc_lport			conn_lport
 * ipc_fport			conn_fport
 * ipc_ports			conn_ports
 *
 * ipc_policy			conn_policy
 * ipc_latch			conn_latch
 *
 * ipc_irc_lock			conn_lock
 * ipc_ire_cache		conn_ire_cache
 *
 * ipc_state_flags		conn_state_flags
 * ipc_outgoing_ill		conn_outgoing_ill
 *
 * ipc_dontroute 		conn_dontroute
 * ipc_loopback 		conn_loopback
 * ipc_broadcast		conn_broadcast
 * ipc_reuseaddr		conn_reuseaddr
 *
 * ipc_multicast_loop		conn_multicast_loop
 * ipc_multi_router		conn_multi_router
 * ipc_draining 		conn_draining
 *
 * ipc_did_putbq		conn_did_putbq
 * ipc_unspec_src		conn_unspec_src
 * ipc_policy_cached		conn_policy_cached
 *
 * ipc_in_enforce_policy 	conn_in_enforce_policy
 * ipc_out_enforce_policy 	conn_out_enforce_policy
 * ipc_af_isv6			conn_af_isv6
 * ipc_pkt_isv6			conn_pkt_isv6
 *
 * ipc_ipv6_recvpktinfo		conn_ipv6_recvpktinfo
 *
 * ipc_ipv6_recvhoplimit	conn_ipv6_recvhoplimit
 * ipc_ipv6_recvhopopts		conn_ipv6_recvhopopts
 * ipc_ipv6_recvdstopts		conn_ipv6_recvdstopts
 *
 * ipc_ipv6_recvrthdr 		conn_ipv6_recvrthdr
 * ipc_ipv6_recvrtdstopts	conn_ipv6_recvrtdstopts
 * ipc_fully_bound		conn_fully_bound
 *
 * ipc_recvif			conn_recvif
 *
 * ipc_recvslla 		conn_recvslla
 * ipc_acking_unbind 		conn_acking_unbind
 * ipc_pad_to_bit_31 		conn_pad_to_bit_31
 *
 * ipc_proto			conn_proto
 * ipc_incoming_ill		conn_incoming_ill
 * ipc_pending_ill		conn_pending_ill
 * ipc_unbind_mp		conn_unbind_mp
 * ipc_ilg			conn_ilg
 * ipc_ilg_allocated		conn_ilg_allocated
 * ipc_ilg_inuse		conn_ilg_inuse
 * ipc_ilg_walker_cnt		conn_ilg_walker_cnt
 * ipc_refcv			conn_refcv
 * ipc_multicast_ipif		conn_multicast_ipif
 * ipc_multicast_ill		conn_multicast_ill
 * ipc_drain_next		conn_drain_next
 * ipc_drain_prev		conn_drain_prev
 * ipc_idl			conn_idl
 */

/*
 * This is used to match an inbound/outbound datagram with policy.
 */
typedef	struct ipsec_selector {
	in6_addr_t	ips_local_addr_v6;
	in6_addr_t	ips_remote_addr_v6;
	uint16_t	ips_local_port;
	uint16_t	ips_remote_port;
	uint8_t		ips_icmp_type;
	uint8_t		ips_icmp_code;
	uint8_t		ips_protocol;
	uint8_t		ips_isv4 : 1,
			ips_is_icmp_inv_acq: 1;
} ipsec_selector_t;

/*
 * Note that we put v4 addresses in the *first* 32-bit word of the
 * selector rather than the last to simplify the prefix match/mask code
 * in spd.c
 */
#define	ips_local_addr_v4 ips_local_addr_v6.s6_addr32[0]
#define	ips_remote_addr_v4 ips_remote_addr_v6.s6_addr32[0]

/* Values used in IP by IPSEC Code */
#define		IPSEC_OUTBOUND		B_TRUE
#define		IPSEC_INBOUND		B_FALSE

/*
 * There are two variants in policy failures. The packet may come in
 * secure when not needed (IPSEC_POLICY_???_NOT_NEEDED) or it may not
 * have the desired level of protection (IPSEC_POLICY_MISMATCH).
 */
#define	IPSEC_POLICY_NOT_NEEDED		0
#define	IPSEC_POLICY_MISMATCH		1
#define	IPSEC_POLICY_AUTH_NOT_NEEDED	2
#define	IPSEC_POLICY_ENCR_NOT_NEEDED	3
#define	IPSEC_POLICY_SE_NOT_NEEDED	4
#define	IPSEC_POLICY_MAX		5	/* Always max + 1. */

/*
 * Folowing macro is used whenever the code does not know whether there
 * is a M_CTL present in the front and it needs to examine the actual mp
 * i.e the IP header. As a M_CTL message could be in the front, this
 * extracts the packet into mp and the M_CTL mp into first_mp. If M_CTL
 * mp is not present, both first_mp and mp point to the same message.
 */
#define	EXTRACT_PKT_MP(mp, first_mp, mctl_present)	\
	(first_mp) = (mp);				\
	if ((mp)->b_datap->db_type == M_CTL) {		\
		(mp) = (mp)->b_cont;			\
		(mctl_present) = B_TRUE;		\
	} else {					\
		(mctl_present) = B_FALSE;		\
	}

/*
 * Check with IPSEC inbound policy if
 *
 * 1) per-socket policy is present - indicated by conn_in_enforce_policy.
 * 2) Or if we have not cached policy on the conn and the global policy is
 *    non-empty.
 */
#define	CONN_INBOUND_POLICY_PRESENT(connp, ipss)	\
	((connp)->conn_in_enforce_policy ||		\
	(!((connp)->conn_policy_cached) && 		\
	(ipss)->ipsec_inbound_v4_policy_present))

#define	CONN_INBOUND_POLICY_PRESENT_V6(connp, ipss)	\
	((connp)->conn_in_enforce_policy ||		\
	(!(connp)->conn_policy_cached &&		\
	(ipss)->ipsec_inbound_v6_policy_present))

#define	CONN_OUTBOUND_POLICY_PRESENT(connp, ipss)	\
	((connp)->conn_out_enforce_policy ||		\
	(!((connp)->conn_policy_cached) &&		\
	(ipss)->ipsec_outbound_v4_policy_present))

#define	CONN_OUTBOUND_POLICY_PRESENT_V6(connp, ipss)	\
	((connp)->conn_out_enforce_policy ||		\
	(!(connp)->conn_policy_cached &&		\
	(ipss)->ipsec_outbound_v6_policy_present))

/*
 * Information cached in IRE for upper layer protocol (ULP).
 *
 * Notice that ire_max_frag is not included in the iulp_t structure, which
 * it may seem that it should.  But ire_max_frag cannot really be cached.  It
 * is fixed for each interface.  For MTU found by PMTUd, we may want to cache
 * it.  But currently, we do not do that.
 */
typedef struct iulp_s {
	boolean_t	iulp_set;	/* Is any metric set? */
	uint32_t	iulp_ssthresh;	/* Slow start threshold (TCP). */
	clock_t		iulp_rtt;	/* Guestimate in millisecs. */
	clock_t		iulp_rtt_sd;	/* Cached value of RTT variance. */
	uint32_t	iulp_spipe;	/* Send pipe size. */
	uint32_t	iulp_rpipe;	/* Receive pipe size. */
	uint32_t	iulp_rtomax;	/* Max round trip timeout. */
	uint32_t	iulp_sack;	/* Use SACK option (TCP)? */
	uint32_t
		iulp_tstamp_ok : 1,	/* Use timestamp option (TCP)? */
		iulp_wscale_ok : 1,	/* Use window scale option (TCP)? */
		iulp_ecn_ok : 1,	/* Enable ECN (for TCP)? */
		iulp_pmtud_ok : 1,	/* Enable PMTUd? */

		iulp_not_used : 28;
} iulp_t;

/* Zero iulp_t. */
extern const iulp_t ire_uinfo_null;

/*
 * The conn drain list structure (idl_t).
 * The list is protected by idl_lock. Each conn_t inserted in the list
 * points back at this idl_t using conn_idl. IP primes the draining of the
 * conns queued in these lists, by qenabling the 1st conn of each list. This
 * occurs when STREAMS backenables ip_wsrv on the IP module. Each conn instance
 * of ip_wsrv successively qenables the next conn in the list.
 * idl_lock protects all other members of idl_t and conn_drain_next
 * and conn_drain_prev of conn_t. The conn_lock protects IPCF_DRAIN_DISABLED
 * flag of the conn_t and conn_idl.
 *
 * The conn drain list, idl_t, itself is part of tx cookie list structure.
 * A tx cookie list points to a blocked Tx ring and contains the list of
 * all conn's that are blocked due to the flow-controlled Tx ring (via
 * the idl drain list). Note that a link can have multiple Tx rings. The
 * drain list will store the conn's blocked due to Tx ring being flow
 * controlled.
 */

typedef uintptr_t ip_mac_tx_cookie_t;
typedef	struct idl_s idl_t;
typedef	struct idl_tx_list_s idl_tx_list_t;

struct idl_tx_list_s {
	ip_mac_tx_cookie_t	txl_cookie;
	kmutex_t		txl_lock;	/* Lock for this list */
	idl_t			*txl_drain_list;
	int			txl_drain_index;
};

struct idl_s {
	conn_t		*idl_conn;		/* Head of drain list */
	kmutex_t	idl_lock;		/* Lock for this list */
	conn_t		*idl_conn_draining;	/* conn that is draining */
	uint32_t
		idl_repeat : 1,			/* Last conn must re-enable */
						/* drain list again */
		idl_unused : 31;
	idl_tx_list_t	*idl_itl;
};

#define	CONN_DRAIN_LIST_LOCK(connp)	(&((connp)->conn_idl->idl_lock))
/*
 * Interface route structure which holds the necessary information to recreate
 * routes that are tied to an interface (namely where ire_ipif != NULL).
 * These routes which were initially created via a routing socket or via the
 * SIOCADDRT ioctl may be gateway routes (RTF_GATEWAY being set) or may be
 * traditional interface routes.  When an interface comes back up after being
 * marked down, this information will be used to recreate the routes.  These
 * are part of an mblk_t chain that hangs off of the IPIF (ipif_saved_ire_mp).
 */
typedef struct ifrt_s {
	ushort_t	ifrt_type;		/* Type of IRE */
	in6_addr_t	ifrt_v6addr;		/* Address IRE represents. */
	in6_addr_t	ifrt_v6gateway_addr;	/* Gateway if IRE_OFFSUBNET */
	in6_addr_t	ifrt_v6src_addr;	/* Src addr if RTF_SETSRC */
	in6_addr_t	ifrt_v6mask;		/* Mask for matching IRE. */
	uint32_t	ifrt_flags;		/* flags related to route */
	uint_t		ifrt_max_frag;		/* MTU (next hop or path). */
	iulp_t		ifrt_iulp_info;		/* Cached IRE ULP info. */
} ifrt_t;

#define	ifrt_addr		V4_PART_OF_V6(ifrt_v6addr)
#define	ifrt_gateway_addr	V4_PART_OF_V6(ifrt_v6gateway_addr)
#define	ifrt_src_addr		V4_PART_OF_V6(ifrt_v6src_addr)
#define	ifrt_mask		V4_PART_OF_V6(ifrt_v6mask)

/* Number of IP addresses that can be hosted on a physical interface */
#define	MAX_ADDRS_PER_IF	8192
/*
 * Number of Source addresses to be considered for source address
 * selection. Used by ipif_select_source[_v6].
 */
#define	MAX_IPIF_SELECT_SOURCE	50

#ifdef IP_DEBUG
/*
 * Trace refholds and refreles for debugging.
 */
#define	TR_STACK_DEPTH	14
typedef struct tr_buf_s {
	int	tr_depth;
	clock_t	tr_time;
	pc_t	tr_stack[TR_STACK_DEPTH];
} tr_buf_t;

typedef struct th_trace_s {
	int		th_refcnt;
	uint_t		th_trace_lastref;
	kthread_t	*th_id;
#define	TR_BUF_MAX	38
	tr_buf_t	th_trbuf[TR_BUF_MAX];
} th_trace_t;

typedef struct th_hash_s {
	list_node_t	thh_link;
	mod_hash_t	*thh_hash;
	ip_stack_t	*thh_ipst;
} th_hash_t;
#endif

/* The following are ipif_state_flags */
#define	IPIF_CONDEMNED		0x1	/* The ipif is being removed */
#define	IPIF_CHANGING		0x2	/* A critcal ipif field is changing */
#define	IPIF_SET_LINKLOCAL	0x10	/* transient flag during bringup */
#define	IPIF_ZERO_SOURCE	0x20	/* transient flag during bringup */

/* IP interface structure, one per local address */
typedef struct ipif_s {
	struct	ipif_s	*ipif_next;
	struct	ill_s	*ipif_ill;	/* Back pointer to our ill */
	int	ipif_id;		/* Logical unit number */
	uint_t	ipif_mtu;		/* Starts at ipif_ill->ill_max_frag */
	in6_addr_t ipif_v6lcl_addr;	/* Local IP address for this if. */
	in6_addr_t ipif_v6src_addr;	/* Source IP address for this if. */
	in6_addr_t ipif_v6subnet;	/* Subnet prefix for this if. */
	in6_addr_t ipif_v6net_mask;	/* Net mask for this interface. */
	in6_addr_t ipif_v6brd_addr;	/* Broadcast addr for this interface. */
	in6_addr_t ipif_v6pp_dst_addr;	/* Point-to-point dest address. */
	uint64_t ipif_flags;		/* Interface flags. */
	uint_t	ipif_metric;		/* BSD if metric, for compatibility. */
	uint_t	ipif_ire_type;		/* IRE_LOCAL or IRE_LOOPBACK */
	mblk_t	*ipif_arp_del_mp;	/* Allocated at time arp comes up, to */
					/* prevent awkward out of mem */
					/* condition later */
	mblk_t	*ipif_saved_ire_mp;	/* Allocated for each extra */
					/* IRE_IF_NORESOLVER/IRE_IF_RESOLVER */
					/* on this interface so that they */
					/* can survive ifconfig down. */
	kmutex_t ipif_saved_ire_lock;	/* Protects ipif_saved_ire_mp */

	mrec_t	*ipif_igmp_rpt;		/* List of group memberships which */
					/* will be reported on.  Used when */
					/* handling an igmp timeout.	   */

	/*
	 * The packet counts in the ipif contain the sum of the
	 * packet counts in dead IREs that were affiliated with
	 * this ipif.
	 */
	uint_t	ipif_fo_pkt_count;	/* Forwarded thru our dead IREs */
	uint_t	ipif_ib_pkt_count;	/* Inbound packets for our dead IREs */
	uint_t	ipif_ob_pkt_count;	/* Outbound packets to our dead IREs */
	/* Exclusive bit fields, protected by ipsq_t */
	unsigned int
		ipif_multicast_up : 1,	/* ipif_multicast_up() successful */
		ipif_was_up : 1,	/* ipif was up before */
		ipif_addr_ready : 1,	/* DAD is done */
		ipif_was_dup : 1,	/* DAD had failed */

		ipif_joined_allhosts : 1, /* allhosts joined */
		ipif_added_nce : 1,	/* nce added for local address */
		ipif_pad_to_31 : 26;

	uint_t	ipif_seqid;		/* unique index across all ills */
	uint_t	ipif_state_flags;	/* See IPIF_* flag defs above */
	uint_t	ipif_refcnt;		/* active consistent reader cnt */

	/* Number of ire's and ilm's referencing this ipif */
	uint_t	ipif_ire_cnt;
	uint_t	ipif_ilm_cnt;

	uint_t  ipif_saved_ire_cnt;
	zoneid_t ipif_zoneid;		/* zone ID number */
	timeout_id_t ipif_recovery_id;	/* Timer for DAD recovery */
	boolean_t ipif_trace_disable;	/* True when alloc fails */
	/*
	 * For an IPMP interface, ipif_bound_ill tracks the ill whose hardware
	 * information this ipif is associated with via ARP/NDP.  We can use
	 * an ill pointer (rather than an index) because only ills that are
	 * part of a group will be pointed to, and an ill cannot disappear
	 * while it's in a group.
	 */
	struct ill_s	*ipif_bound_ill;
	struct ipif_s	*ipif_bound_next; /* bound ipif chain */
	boolean_t	ipif_bound;	 /* B_TRUE if we successfully bound */
} ipif_t;

/*
 * IPIF_FREE_OK() means that there are no incoming references
 * to the ipif. Incoming refs would prevent the ipif from being freed.
 */
#define	IPIF_FREE_OK(ipif)	\
	((ipif)->ipif_ire_cnt == 0 && (ipif)->ipif_ilm_cnt == 0)
/*
 * IPIF_DOWN_OK() determines whether the incoming pointer reference counts
 * would permit the ipif to be considered quiescent. In order for
 * an ipif or ill to be considered quiescent, the ire and nce references
 * to that ipif/ill must be zero.
 *
 * We do not require the ilm references to go to zero for quiescence
 * because the quiescence checks are done to ensure that
 * outgoing packets do not use addresses from the ipif/ill after it
 * has been marked down, and incoming packets to addresses on a
 * queiscent interface are rejected. This implies that all the
 * ire/nce's using that source address need to be deleted and future
 * creation of any ires using that source address must be prevented.
 * Similarly incoming unicast packets destined to the 'down' address
 * will not be accepted once that ire is gone. However incoming
 * multicast packets are not destined to the downed address.
 * They are only related to the ill in question. Furthermore
 * the current API behavior allows applications to join or leave
 * multicast groups, i.e., IP_ADD_MEMBERSHIP / LEAVE_MEMBERSHIP, using a
 * down address. Therefore the ilm references are not included in
 * the _DOWN_OK macros.
 */
#define	IPIF_DOWN_OK(ipif)		((ipif)->ipif_ire_cnt == 0)

/*
 * The following table lists the protection levels of the various members
 * of the ipif_t. The following notation is used.
 *
 * Write once - Written to only once at the time of bringing up
 * the interface and can be safely read after the bringup without any lock.
 *
 * ipsq - Need to execute in the ipsq to perform the indicated access.
 *
 * ill_lock - Need to hold this mutex to perform the indicated access.
 *
 * ill_g_lock - Need to hold this rw lock as reader/writer for read access or
 * write access respectively.
 *
 * down ill - Written to only when the ill is down (i.e all ipifs are down)
 * up ill - Read only when the ill is up (i.e. at least 1 ipif is up)
 *
 *		 Table of ipif_t members and their protection
 *
 * ipif_next		ipsq + ill_lock +	ipsq OR ill_lock OR
 *			ill_g_lock		ill_g_lock
 * ipif_ill		ipsq + down ipif	write once
 * ipif_id		ipsq + down ipif	write once
 * ipif_mtu		ipsq
 * ipif_v6lcl_addr	ipsq + down ipif	up ipif
 * ipif_v6src_addr	ipsq + down ipif	up ipif
 * ipif_v6subnet	ipsq + down ipif	up ipif
 * ipif_v6net_mask	ipsq + down ipif	up ipif
 *
 * ipif_v6brd_addr
 * ipif_v6pp_dst_addr
 * ipif_flags		ill_lock		ill_lock
 * ipif_metric
 * ipif_ire_type	ipsq + down ill		up ill
 *
 * ipif_arp_del_mp	ipsq			ipsq
 * ipif_saved_ire_mp	ipif_saved_ire_lock	ipif_saved_ire_lock
 * ipif_igmp_rpt	ipsq			ipsq
 *
 * ipif_fo_pkt_count	Approx
 * ipif_ib_pkt_count	Approx
 * ipif_ob_pkt_count	Approx
 *
 * bit fields		ill_lock		ill_lock
 *
 * ipif_seqid		ipsq			Write once
 *
 * ipif_state_flags	ill_lock		ill_lock
 * ipif_refcnt		ill_lock		ill_lock
 * ipif_ire_cnt		ill_lock		ill_lock
 * ipif_ilm_cnt		ill_lock		ill_lock
 * ipif_saved_ire_cnt
 *
 * ipif_bound_ill	ipsq + ipmp_lock	ipsq OR ipmp_lock
 * ipif_bound_next	ipsq			ipsq
 * ipif_bound		ipsq			ipsq
 */

#define	IP_TR_HASH(tid)	((((uintptr_t)tid) >> 6) & (IP_TR_HASH_MAX - 1))

#ifdef DEBUG
#define	IPIF_TRACE_REF(ipif)	ipif_trace_ref(ipif)
#define	ILL_TRACE_REF(ill)	ill_trace_ref(ill)
#define	IPIF_UNTRACE_REF(ipif)	ipif_untrace_ref(ipif)
#define	ILL_UNTRACE_REF(ill)	ill_untrace_ref(ill)
#else
#define	IPIF_TRACE_REF(ipif)
#define	ILL_TRACE_REF(ill)
#define	IPIF_UNTRACE_REF(ipif)
#define	ILL_UNTRACE_REF(ill)
#endif

/* IPv4 compatibility macros */
#define	ipif_lcl_addr		V4_PART_OF_V6(ipif_v6lcl_addr)
#define	ipif_src_addr		V4_PART_OF_V6(ipif_v6src_addr)
#define	ipif_subnet		V4_PART_OF_V6(ipif_v6subnet)
#define	ipif_net_mask		V4_PART_OF_V6(ipif_v6net_mask)
#define	ipif_brd_addr		V4_PART_OF_V6(ipif_v6brd_addr)
#define	ipif_pp_dst_addr	V4_PART_OF_V6(ipif_v6pp_dst_addr)

/* Macros for easy backreferences to the ill. */
#define	ipif_wq			ipif_ill->ill_wq
#define	ipif_rq			ipif_ill->ill_rq
#define	ipif_net_type		ipif_ill->ill_net_type
#define	ipif_ipif_up_count	ipif_ill->ill_ipif_up_count
#define	ipif_type		ipif_ill->ill_type
#define	ipif_isv6		ipif_ill->ill_isv6

#define	SIOCLIFADDR_NDX 112	/* ndx of SIOCLIFADDR in the ndx ioctl table */

/*
 * mode value for ip_ioctl_finish for finishing an ioctl
 */
#define	CONN_CLOSE	1		/* No mi_copy */
#define	COPYOUT		2		/* do an mi_copyout if needed */
#define	NO_COPYOUT	3		/* do an mi_copy_done */
#define	IPI2MODE(ipi)	((ipi)->ipi_flags & IPI_GET_CMD ? COPYOUT : NO_COPYOUT)

/*
 * The IP-MT design revolves around the serialization objects ipsq_t (IPSQ)
 * and ipxop_t (exclusive operation or "xop").  Becoming "writer" on an IPSQ
 * ensures that no other threads can become "writer" on any IPSQs sharing that
 * IPSQ's xop until the writer thread is done.
 *
 * Each phyint points to one IPSQ that remains fixed over the phyint's life.
 * Each IPSQ points to one xop that can change over the IPSQ's life.  If a
 * phyint is *not* in an IPMP group, then its IPSQ will refer to the IPSQ's
 * "own" xop (ipsq_ownxop).  If a phyint *is* part of an IPMP group, then its
 * IPSQ will refer to the "group" xop, which is shorthand for the xop of the
 * IPSQ of the IPMP meta-interface's phyint.  Thus, all phyints that are part
 * of the same IPMP group will have their IPSQ's point to the group xop, and
 * thus becoming "writer" on any phyint in the group will prevent any other
 * writer on any other phyint in the group.  All IPSQs sharing the same xop
 * are chained together through ipsq_next (in the degenerate common case,
 * ipsq_next simply refers to itself).  Note that the group xop is guaranteed
 * to exist at least as long as there are members in the group, since the IPMP
 * meta-interface can only be destroyed if the group is empty.
 *
 * Incoming exclusive operation requests are enqueued on the IPSQ they arrived
 * on rather than the xop.  This makes switching xop's (as would happen when a
 * phyint leaves an IPMP group) simple, because after the phyint leaves the
 * group, any operations enqueued on its IPSQ can be safely processed with
 * respect to its new xop, and any operations enqueued on the IPSQs of its
 * former group can be processed with respect to their existing group xop.
 * Even so, switching xops is a subtle dance; see ipsq_dq() for details.
 *
 * An IPSQ's "own" xop is embedded within the IPSQ itself since they have have
 * identical lifetimes, and because doing so simplifies pointer management.
 * While each phyint and IPSQ point to each other, it is not possible to free
 * the IPSQ when the phyint is freed, since we may still *inside* the IPSQ
 * when the phyint is being freed.  Thus, ipsq_phyint is set to NULL when the
 * phyint is freed, and the IPSQ free is later done in ipsq_exit().
 *
 * ipsq_t synchronization:	read			write
 *
 *	ipsq_xopq_mphead	ipx_lock		ipx_lock
 *	ipsq_xopq_mptail	ipx_lock		ipx_lock
 *	ipsq_xop_switch_mp	ipsq_lock		ipsq_lock
 *	ipsq_phyint		write once		write once
 *	ipsq_next		RW_READER ill_g_lock	RW_WRITER ill_g_lock
 *	ipsq_xop 		ipsq_lock or ipsq	ipsq_lock + ipsq
 *	ipsq_swxop		ipsq			ipsq
 * 	ipsq_ownxop		see ipxop_t		see ipxop_t
 *	ipsq_ipst		write once		write once
 *
 * ipxop_t synchronization:     read			write
 *
 *	ipx_writer  		ipx_lock		ipx_lock
 *	ipx_xop_queued		ipx_lock 		ipx_lock
 *	ipx_mphead		ipx_lock		ipx_lock
 *	ipx_mptail		ipx_lock		ipx_lock
 *	ipx_ipsq		write once		write once
 *	ips_ipsq_queued		ipx_lock		ipx_lock
 *	ipx_waitfor		ipsq or ipx_lock	ipsq + ipx_lock
 *	ipx_reentry_cnt		ipsq or ipx_lock	ipsq + ipx_lock
 *	ipx_current_done	ipsq			ipsq
 *	ipx_current_ioctl	ipsq			ipsq
 *	ipx_current_ipif	ipsq or ipx_lock	ipsq + ipx_lock
 *	ipx_pending_ipif	ipsq or ipx_lock	ipsq + ipx_lock
 *	ipx_pending_mp		ipsq or ipx_lock	ipsq + ipx_lock
 *	ipx_forced		ipsq			ipsq
 *	ipx_depth		ipsq			ipsq
 *	ipx_stack		ipsq			ipsq
 */
typedef struct ipxop_s {
	kmutex_t	ipx_lock;	/* see above */
	kthread_t	*ipx_writer;  	/* current owner */
	mblk_t		*ipx_mphead;	/* messages tied to this op */
	mblk_t		*ipx_mptail;
	struct ipsq_s	*ipx_ipsq;	/* associated ipsq */
	boolean_t	ipx_ipsq_queued; /* ipsq using xop has queued op */
	int		ipx_waitfor;	/* waiting; values encoded below */
	int		ipx_reentry_cnt;
	boolean_t	ipx_current_done;  /* is the current operation done? */
	int		ipx_current_ioctl; /* current ioctl, or 0 if no ioctl */
	ipif_t		*ipx_current_ipif; /* ipif for current op */
	ipif_t		*ipx_pending_ipif; /* ipif for ipsq_pending_mp */
	mblk_t 		*ipx_pending_mp;   /* current ioctl mp while waiting */
	boolean_t	ipx_forced; 			/* debugging aid */
#ifdef DEBUG
	int		ipx_depth;			/* debugging aid */
#define	IPX_STACK_DEPTH	15
	pc_t		ipx_stack[IPX_STACK_DEPTH];	/* debugging aid */
#endif
} ipxop_t;

typedef struct ipsq_s {
	kmutex_t ipsq_lock;		/* see above */
	mblk_t	*ipsq_switch_mp;	/* op to handle right after switch */
	mblk_t	*ipsq_xopq_mphead;	/* list of excl ops (mostly ioctls) */
	mblk_t	*ipsq_xopq_mptail;
	struct phyint	*ipsq_phyint;	/* associated phyint */
	struct ipsq_s	*ipsq_next;	/* next ipsq sharing ipsq_xop */
	struct ipxop_s	*ipsq_xop;	/* current xop synchronization info */
	struct ipxop_s	*ipsq_swxop;	/* switch xop to on ipsq_exit() */
	struct ipxop_s	ipsq_ownxop;	/* our own xop (may not be in-use) */
	ip_stack_t	*ipsq_ipst;	/* does not have a netstack_hold */
} ipsq_t;

/*
 * ipx_waitfor values:
 */
enum {
	IPIF_DOWN = 1,	/* ipif_down() waiting for refcnts to drop */
	ILL_DOWN,	/* ill_down() waiting for refcnts to drop */
	IPIF_FREE,	/* ipif_free() waiting for refcnts to drop */
	ILL_FREE	/* ill unplumb waiting for refcnts to drop */
};

/* Operation types for ipsq_try_enter() */
#define	CUR_OP 0	/* request writer within current operation */
#define	NEW_OP 1	/* request writer for a new operation */
#define	SWITCH_OP 2	/* request writer once IPSQ XOP switches */

/*
 * Kstats tracked on each IPMP meta-interface.  Order here must match
 * ipmp_kstats[] in ip/ipmp.c.
 */
enum {
	IPMP_KSTAT_OBYTES,	IPMP_KSTAT_OBYTES64,	IPMP_KSTAT_RBYTES,
	IPMP_KSTAT_RBYTES64,	IPMP_KSTAT_OPACKETS,	IPMP_KSTAT_OPACKETS64,
	IPMP_KSTAT_OERRORS,	IPMP_KSTAT_IPACKETS,	IPMP_KSTAT_IPACKETS64,
	IPMP_KSTAT_IERRORS,	IPMP_KSTAT_MULTIRCV,	IPMP_KSTAT_MULTIXMT,
	IPMP_KSTAT_BRDCSTRCV,	IPMP_KSTAT_BRDCSTXMT,	IPMP_KSTAT_LINK_UP,
	IPMP_KSTAT_MAX		/* keep last */
};

/*
 * phyint represents state that is common to both IPv4 and IPv6 interfaces.
 * There is a separate ill_t representing IPv4 and IPv6 which has a
 * backpointer to the phyint structure for accessing common state.
 */
typedef struct phyint {
	struct ill_s	*phyint_illv4;
	struct ill_s	*phyint_illv6;
	uint_t		phyint_ifindex;		/* SIOCSLIFINDEX */
	uint64_t	phyint_flags;
	avl_node_t	phyint_avl_by_index;	/* avl tree by index */
	avl_node_t	phyint_avl_by_name;	/* avl tree by name */
	kmutex_t	phyint_lock;
	struct ipsq_s	*phyint_ipsq;		/* back pointer to ipsq */
	struct ipmp_grp_s *phyint_grp;		/* associated IPMP group */
	char		phyint_name[LIFNAMSIZ];	/* physical interface name */
	uint64_t	phyint_kstats0[IPMP_KSTAT_MAX];	/* baseline kstats */
} phyint_t;

#define	CACHE_ALIGN_SIZE 64
#define	CACHE_ALIGN(align_struct)	P2ROUNDUP(sizeof (struct align_struct),\
							CACHE_ALIGN_SIZE)
struct _phyint_list_s_ {
	avl_tree_t	phyint_list_avl_by_index;	/* avl tree by index */
	avl_tree_t	phyint_list_avl_by_name;	/* avl tree by name */
};

typedef union phyint_list_u {
	struct	_phyint_list_s_ phyint_list_s;
	char	phyint_list_filler[CACHE_ALIGN(_phyint_list_s_)];
} phyint_list_t;

#define	phyint_list_avl_by_index	phyint_list_s.phyint_list_avl_by_index
#define	phyint_list_avl_by_name		phyint_list_s.phyint_list_avl_by_name

/*
 * Fragmentation hash bucket
 */
typedef struct ipfb_s {
	struct ipf_s	*ipfb_ipf;	/* List of ... */
	size_t		ipfb_count;	/* Count of bytes used by frag(s) */
	kmutex_t	ipfb_lock;	/* Protect all ipf in list */
	uint_t		ipfb_frag_pkts; /* num of distinct fragmented pkts */
} ipfb_t;

/*
 * IRE bucket structure. Usually there is an array of such structures,
 * each pointing to a linked list of ires. irb_refcnt counts the number
 * of walkers of a given hash bucket. Usually the reference count is
 * bumped up if the walker wants no IRES to be DELETED while walking the
 * list. Bumping up does not PREVENT ADDITION. This allows walking a given
 * hash bucket without stumbling up on a free pointer.
 *
 * irb_t structures in ip_ftable are dynamically allocated and freed.
 * In order to identify the irb_t structures that can be safely kmem_free'd
 * we need to ensure that
 *  - the irb_refcnt is quiescent, indicating no other walkers,
 *  - no other threads or ire's are holding references to the irb,
 *	i.e., irb_nire == 0,
 *  - there are no active ire's in the bucket, i.e., irb_ire_cnt == 0
 */
typedef struct irb {
	struct ire_s	*irb_ire;	/* First ire in this bucket */
					/* Should be first in this struct */
	krwlock_t	irb_lock;	/* Protect this bucket */
	uint_t		irb_refcnt;	/* Protected by irb_lock */
	uchar_t		irb_marks;	/* CONDEMNED ires in this bucket ? */
#define	IRB_MARK_CONDEMNED	0x0001
#define	IRB_MARK_FTABLE		0x0002
	uint_t		irb_ire_cnt;	/* Num of active IRE in this bucket */
	uint_t		irb_tmp_ire_cnt; /* Num of temporary IRE */
	struct ire_s	*irb_rr_origin;	/* origin for round-robin */
	int		irb_nire;	/* Num of ftable ire's that ref irb */
	ip_stack_t	*irb_ipst;	/* Does not have a netstack_hold */
} irb_t;

#define	IRB2RT(irb)	(rt_t *)((caddr_t)(irb) - offsetof(rt_t, rt_irb))

/* The following are return values of ip_xmit_v4() */
typedef enum {
	SEND_PASSED = 0,	 /* sent packet out on wire */
	SEND_FAILED,	 /* sending of packet failed */
	LOOKUP_IN_PROGRESS, /* ire cache found, ARP resolution in progress */
	LLHDR_RESLV_FAILED  /* macaddr resl of onlink dst or nexthop failed */
} ipxmit_state_t;

#define	IP_V4_G_HEAD	0
#define	IP_V6_G_HEAD	1

#define	MAX_G_HEADS	2

/*
 * unpadded ill_if structure
 */
struct 	_ill_if_s_ {
	union ill_if_u	*illif_next;
	union ill_if_u	*illif_prev;
	avl_tree_t	illif_avl_by_ppa;	/* AVL tree sorted on ppa */
	vmem_t		*illif_ppa_arena;	/* ppa index space */
	uint16_t	illif_mcast_v1;		/* hints for		  */
	uint16_t	illif_mcast_v2;		/* [igmp|mld]_slowtimo	  */
	int		illif_name_len;		/* name length */
	char		illif_name[LIFNAMSIZ];	/* name of interface type */
};

/* cache aligned ill_if structure */
typedef union 	ill_if_u {
	struct  _ill_if_s_ ill_if_s;
	char 	illif_filler[CACHE_ALIGN(_ill_if_s_)];
} ill_if_t;

#define	illif_next		ill_if_s.illif_next
#define	illif_prev		ill_if_s.illif_prev
#define	illif_avl_by_ppa	ill_if_s.illif_avl_by_ppa
#define	illif_ppa_arena		ill_if_s.illif_ppa_arena
#define	illif_mcast_v1		ill_if_s.illif_mcast_v1
#define	illif_mcast_v2		ill_if_s.illif_mcast_v2
#define	illif_name		ill_if_s.illif_name
#define	illif_name_len		ill_if_s.illif_name_len

typedef struct ill_walk_context_s {
	int	ctx_current_list; /* current list being searched */
	int	ctx_last_list;	 /* last list to search */
} ill_walk_context_t;

/*
 * ill_g_heads structure, one for IPV4 and one for IPV6
 */
struct _ill_g_head_s_ {
	ill_if_t	*ill_g_list_head;
	ill_if_t	*ill_g_list_tail;
};

typedef union ill_g_head_u {
	struct _ill_g_head_s_ ill_g_head_s;
	char	ill_g_head_filler[CACHE_ALIGN(_ill_g_head_s_)];
} ill_g_head_t;

#define	ill_g_list_head	ill_g_head_s.ill_g_list_head
#define	ill_g_list_tail	ill_g_head_s.ill_g_list_tail

#define	IP_V4_ILL_G_LIST(ipst)	\
	(ipst)->ips_ill_g_heads[IP_V4_G_HEAD].ill_g_list_head
#define	IP_V6_ILL_G_LIST(ipst)	\
	(ipst)->ips_ill_g_heads[IP_V6_G_HEAD].ill_g_list_head
#define	IP_VX_ILL_G_LIST(i, ipst)	\
	(ipst)->ips_ill_g_heads[i].ill_g_list_head

#define	ILL_START_WALK_V4(ctx_ptr, ipst)	\
	ill_first(IP_V4_G_HEAD, IP_V4_G_HEAD, ctx_ptr, ipst)
#define	ILL_START_WALK_V6(ctx_ptr, ipst)	\
	ill_first(IP_V6_G_HEAD, IP_V6_G_HEAD, ctx_ptr, ipst)
#define	ILL_START_WALK_ALL(ctx_ptr, ipst)	\
	ill_first(MAX_G_HEADS, MAX_G_HEADS, ctx_ptr, ipst)

/*
 * Capabilities, possible flags for ill_capabilities.
 */

#define	ILL_CAPAB_AH		0x01		/* IPsec AH acceleration */
#define	ILL_CAPAB_ESP		0x02		/* IPsec ESP acceleration */
#define	ILL_CAPAB_MDT		0x04		/* Multidata Transmit */
#define	ILL_CAPAB_HCKSUM	0x08		/* Hardware checksumming */
#define	ILL_CAPAB_ZEROCOPY	0x10		/* Zero-copy */
#define	ILL_CAPAB_DLD		0x20		/* DLD capabilities */
#define	ILL_CAPAB_DLD_POLL	0x40		/* Polling */
#define	ILL_CAPAB_DLD_DIRECT	0x80		/* Direct function call */
#define	ILL_CAPAB_DLD_LSO	0x100		/* Large Segment Offload */

/*
 * Per-ill Multidata Transmit capabilities.
 */
typedef struct ill_mdt_capab_s ill_mdt_capab_t;

/*
 * Per-ill IPsec capabilities.
 */
typedef struct ill_ipsec_capab_s ill_ipsec_capab_t;

/*
 * Per-ill Hardware Checksumming capbilities.
 */
typedef struct ill_hcksum_capab_s ill_hcksum_capab_t;

/*
 * Per-ill Zero-copy capabilities.
 */
typedef struct ill_zerocopy_capab_s ill_zerocopy_capab_t;

/*
 * DLD capbilities.
 */
typedef struct ill_dld_capab_s ill_dld_capab_t;

/*
 * Per-ill polling resource map.
 */
typedef struct ill_rx_ring ill_rx_ring_t;

/*
 * Per-ill Large Segment Offload capabilities.
 */
typedef struct ill_lso_capab_s ill_lso_capab_t;

/* The following are ill_state_flags */
#define	ILL_LL_SUBNET_PENDING	0x01	/* Waiting for DL_INFO_ACK from drv */
#define	ILL_CONDEMNED		0x02	/* No more new ref's to the ILL */
#define	ILL_CHANGING		0x04	/* ILL not globally visible */
#define	ILL_DL_UNBIND_IN_PROGRESS	0x08	/* UNBIND_REQ is sent */

/* Is this an ILL whose source address is used by other ILL's ? */
#define	IS_USESRC_ILL(ill)			\
	(((ill)->ill_usesrc_ifindex == 0) &&	\
	((ill)->ill_usesrc_grp_next != NULL))

/* Is this a client/consumer of the usesrc ILL ? */
#define	IS_USESRC_CLI_ILL(ill)			\
	(((ill)->ill_usesrc_ifindex != 0) &&	\
	((ill)->ill_usesrc_grp_next != NULL))

/* Is this an virtual network interface (vni) ILL ? */
#define	IS_VNI(ill)							     \
	(((ill) != NULL) &&						     \
	(((ill)->ill_phyint->phyint_flags & (PHYI_LOOPBACK|PHYI_VIRTUAL)) == \
	PHYI_VIRTUAL))

/* Is this a loopback ILL? */
#define	IS_LOOPBACK(ill) \
	((ill)->ill_phyint->phyint_flags & PHYI_LOOPBACK)

/* Is this an IPMP meta-interface ILL? */
#define	IS_IPMP(ill)							\
	((ill)->ill_phyint->phyint_flags & PHYI_IPMP)

/* Is this ILL under an IPMP meta-interface? (aka "in a group?") */
#define	IS_UNDER_IPMP(ill)						\
	((ill)->ill_grp != NULL && !IS_IPMP(ill))

/* Is ill1 in the same illgrp as ill2? */
#define	IS_IN_SAME_ILLGRP(ill1, ill2)					\
	((ill1)->ill_grp != NULL && ((ill1)->ill_grp == (ill2)->ill_grp))

/* Is ill1 on the same LAN as ill2? */
#define	IS_ON_SAME_LAN(ill1, ill2)					\
	((ill1) == (ill2) || IS_IN_SAME_ILLGRP(ill1, ill2))

#define	ILL_OTHER(ill)							\
	((ill)->ill_isv6 ? (ill)->ill_phyint->phyint_illv4 :		\
	    (ill)->ill_phyint->phyint_illv6)

/*
 * IPMP group ILL state structure -- up to two per IPMP group (V4 and V6).
 * Created when the V4 and/or V6 IPMP meta-interface is I_PLINK'd.  It is
 * guaranteed to persist while there are interfaces of that type in the group.
 * In general, most fields are accessed outside of the IPSQ (e.g., in the
 * datapath), and thus use locks in addition to the IPSQ for protection.
 *
 * synchronization:		read			write
 *
 *	ig_if			ipsq or ill_g_lock	ipsq and ill_g_lock
 *	ig_actif		ipsq or ipmp_lock	ipsq and ipmp_lock
 *	ig_nactif		ipsq or ipmp_lock	ipsq and ipmp_lock
 *	ig_next_ill		ipsq or ipmp_lock	ipsq and ipmp_lock
 *	ig_ipmp_ill		write once		write once
 *	ig_cast_ill		ipsq or ipmp_lock	ipsq and ipmp_lock
 *	ig_arpent		ipsq			ipsq
 *	ig_mtu			ipsq			ipsq
 */
typedef struct ipmp_illgrp_s {
	list_t		ig_if; 		/* list of all interfaces */
	list_t		ig_actif;	/* list of active interfaces */
	uint_t		ig_nactif;	/* number of active interfaces */
	struct ill_s	*ig_next_ill;	/* next active interface to use */
	struct ill_s	*ig_ipmp_ill;	/* backpointer to IPMP meta-interface */
	struct ill_s	*ig_cast_ill;	/* nominated ill for multi/broadcast */
	list_t		ig_arpent;	/* list of ARP entries */
	uint_t		ig_mtu;		/* ig_ipmp_ill->ill_max_mtu */
} ipmp_illgrp_t;

/*
 * IPMP group state structure -- one per IPMP group.  Created when the
 * IPMP meta-interface is plumbed; it is guaranteed to persist while there
 * are interfaces in it.
 *
 * ipmp_grp_t synchronization:		read			write
 *
 *	gr_name				ipmp_lock		ipmp_lock
 *	gr_ifname			write once		write once
 *	gr_mactype			ipmp_lock		ipmp_lock
 *	gr_phyint			write once		write once
 *	gr_nif				ipmp_lock		ipmp_lock
 *	gr_nactif			ipsq			ipsq
 *	gr_v4				ipmp_lock		ipmp_lock
 *	gr_v6				ipmp_lock		ipmp_lock
 *	gr_nv4				ipmp_lock		ipmp_lock
 *	gr_nv6				ipmp_lock		ipmp_lock
 *	gr_pendv4			ipmp_lock		ipmp_lock
 *	gr_pendv6			ipmp_lock		ipmp_lock
 *	gr_linkdownmp			ipsq			ipsq
 *	gr_ksp				ipmp_lock		ipmp_lock
 *	gr_kstats0			atomic			atomic
 */
typedef struct ipmp_grp_s {
	char		gr_name[LIFGRNAMSIZ];	/* group name */
	char		gr_ifname[LIFNAMSIZ];	/* interface name */
	t_uscalar_t	gr_mactype;	/* DLPI mactype of group */
	phyint_t	*gr_phyint;	/* IPMP group phyint */
	uint_t		gr_nif;		/* number of interfaces in group */
	uint_t		gr_nactif; 	/* number of active interfaces */
	ipmp_illgrp_t	*gr_v4;		/* V4 group information */
	ipmp_illgrp_t	*gr_v6;		/* V6 group information */
	uint_t		gr_nv4;		/* number of ills in V4 group */
	uint_t		gr_nv6;		/* number of ills in V6 group */
	uint_t		gr_pendv4; 	/* number of pending ills in V4 group */
	uint_t		gr_pendv6; 	/* number of pending ills in V6 group */
	mblk_t		*gr_linkdownmp;	/* message used to bring link down */
	kstat_t		*gr_ksp;	/* group kstat pointer */
	uint64_t	gr_kstats0[IPMP_KSTAT_MAX]; /* baseline group kstats */
} ipmp_grp_t;

/*
 * IPMP ARP entry -- one per SIOCS*ARP entry tied to the group.  Used to keep
 * ARP up-to-date as the active set of interfaces in the group changes.
 */
typedef struct ipmp_arpent_s {
	mblk_t		*ia_area_mp;	/* AR_ENTRY_ADD pointer */
	ipaddr_t	ia_ipaddr; 	/* IP address for this entry */
	boolean_t	ia_proxyarp; 	/* proxy ARP entry? */
	boolean_t	ia_notified; 	/* ARP notified about this entry? */
	list_node_t	ia_node; 	/* next ARP entry in list */
} ipmp_arpent_t;

/*
 * IP Lower level Structure.
 * Instance data structure in ip_open when there is a device below us.
 */
typedef struct ill_s {
	ill_if_t *ill_ifptr;		/* pointer to interface type */
	queue_t	*ill_rq;		/* Read queue. */
	queue_t	*ill_wq;		/* Write queue. */

	int	ill_error;		/* Error value sent up by device. */

	ipif_t	*ill_ipif;		/* Interface chain for this ILL. */

	uint_t	ill_ipif_up_count;	/* Number of IPIFs currently up. */
	uint_t	ill_max_frag;		/* Max IDU from DLPI. */
	char	*ill_name;		/* Our name. */
	uint_t	ill_ipif_dup_count;	/* Number of duplicate addresses. */
	uint_t	ill_name_length;	/* Name length, incl. terminator. */
	char	*ill_ndd_name;		/* Name + ":ip?_forwarding" for NDD. */
	uint_t	ill_net_type;		/* IRE_IF_RESOLVER/IRE_IF_NORESOLVER. */
	/*
	 * Physical Point of Attachment num.  If DLPI style 1 provider
	 * then this is derived from the devname.
	 */
	uint_t	ill_ppa;
	t_uscalar_t	ill_sap;
	t_scalar_t	ill_sap_length;	/* Including sign (for position) */
	uint_t	ill_phys_addr_length;	/* Excluding the sap. */
	uint_t	ill_bcast_addr_length;	/* Only set when the DL provider */
					/* supports broadcast. */
	t_uscalar_t	ill_mactype;
	uint8_t	*ill_frag_ptr;		/* Reassembly state. */
	timeout_id_t ill_frag_timer_id; /* timeout id for the frag timer */
	ipfb_t	*ill_frag_hash_tbl;	/* Fragment hash list head. */
	ipif_t	*ill_pending_ipif;	/* IPIF waiting for DL operation. */

	ilm_t	*ill_ilm;		/* Multicast membership for ill */
	uint_t	ill_global_timer;	/* for IGMPv3/MLDv2 general queries */
	int	ill_mcast_type;		/* type of router which is querier */
					/* on this interface */
	uint16_t ill_mcast_v1_time;	/* # slow timeouts since last v1 qry */
	uint16_t ill_mcast_v2_time;	/* # slow timeouts since last v2 qry */
	uint8_t	ill_mcast_v1_tset;	/* 1 => timer is set; 0 => not set */
	uint8_t	ill_mcast_v2_tset;	/* 1 => timer is set; 0 => not set */

	uint8_t	ill_mcast_rv;		/* IGMPv3/MLDv2 robustness variable */
	int	ill_mcast_qi;		/* IGMPv3/MLDv2 query interval var */

	mblk_t	*ill_pending_mp;	/* IOCTL/DLPI awaiting completion. */
	/*
	 * All non-NULL cells between 'ill_first_mp_to_free' and
	 * 'ill_last_mp_to_free' are freed in ill_delete.
	 */
#define	ill_first_mp_to_free	ill_bcast_mp
	mblk_t	*ill_bcast_mp;		/* DLPI header for broadcasts. */
	mblk_t	*ill_resolver_mp;	/* Resolver template. */
	mblk_t	*ill_unbind_mp;		/* unbind mp from ill_dl_up() */
	mblk_t	*ill_promiscoff_mp;	/* for ill_leave_allmulti() */
	mblk_t	*ill_dlpi_deferred;	/* b_next chain of control messages */
	mblk_t	*ill_ardeact_mp;	/* deact mp from ipmp_ill_activate() */
	mblk_t	*ill_replumb_mp;	/* replumb mp from ill_replumb() */
	mblk_t	*ill_phys_addr_mp;	/* mblk which holds ill_phys_addr */
#define	ill_last_mp_to_free	ill_phys_addr_mp

	cred_t	*ill_credp;		/* opener's credentials */
	uint8_t	*ill_phys_addr;		/* ill_phys_addr_mp->b_rptr + off */

	uint_t	ill_state_flags;	/* see ILL_* flags above */

	/* Following bit fields protected by ipsq_t */
	uint_t
		ill_needs_attach : 1,
		ill_reserved : 1,
		ill_isv6 : 1,
		ill_dlpi_style_set : 1,

		ill_ifname_pending : 1,
		ill_join_allmulti : 1,
		ill_logical_down : 1,
		ill_is_6to4tun : 1,	/* Interface is a 6to4 tunnel */

		ill_promisc_on_phys : 1, /* phys interface in promisc mode */
		ill_dl_up : 1,
		ill_up_ipifs : 1,
		ill_note_link : 1,	/* supports link-up notification */
		ill_capab_reneg : 1, /* capability renegotiation to be done */
		ill_dld_capab_inprog : 1, /* direct dld capab call in prog */
		ill_need_recover_multicast : 1,
		ill_pad_to_bit_31 : 17;

	/* Following bit fields protected by ill_lock */
	uint_t
		ill_fragtimer_executing : 1,
		ill_fragtimer_needrestart : 1,
		ill_ilm_cleanup_reqd : 1,
		ill_arp_closing : 1,

		ill_arp_bringup_pending : 1,
		ill_arp_extend : 1,	/* ARP has DAD extensions */
		ill_pad_bit_31 : 26;

	/*
	 * Used in SIOCSIFMUXID and SIOCGIFMUXID for 'ifconfig unplumb'.
	 */
	int	ill_arp_muxid;		/* muxid returned from plink for arp */
	int	ill_ip_muxid;		/* muxid returned from plink for ip */

	/* Used for IP frag reassembly throttling on a per ILL basis.  */
	uint_t	ill_ipf_gen;		/* Generation of next fragment queue */
	uint_t	ill_frag_count;		/* Count of all reassembly mblk bytes */
	uint_t	ill_frag_free_num_pkts;	 /* num of fragmented packets to free */
	clock_t	ill_last_frag_clean_time; /* time when frag's were pruned */
	int	ill_type;		/* From <net/if_types.h> */
	uint_t	ill_dlpi_multicast_state;	/* See below IDS_* */
	uint_t	ill_dlpi_fastpath_state;	/* See below IDS_* */

	/*
	 * Capabilities related fields.
	 */
	uint_t  ill_dlpi_capab_state;	/* State of capability query, IDCS_* */
	uint_t	ill_capab_pending_cnt;
	uint64_t ill_capabilities;	/* Enabled capabilities, ILL_CAPAB_* */
	ill_mdt_capab_t	*ill_mdt_capab;	/* Multidata Transmit capabilities */
	ill_ipsec_capab_t *ill_ipsec_capab_ah;	/* IPsec AH capabilities */
	ill_ipsec_capab_t *ill_ipsec_capab_esp;	/* IPsec ESP capabilities */
	ill_hcksum_capab_t *ill_hcksum_capab; /* H/W cksumming capabilities */
	ill_zerocopy_capab_t *ill_zerocopy_capab; /* Zero-copy capabilities */
	ill_dld_capab_t *ill_dld_capab; /* DLD capabilities */
	ill_lso_capab_t	*ill_lso_capab;	/* Large Segment Offload capabilities */
	mblk_t	*ill_capab_reset_mp;	/* Preallocated mblk for capab reset */

	/*
	 * New fields for IPv6
	 */
	uint8_t	ill_max_hops;	/* Maximum hops for any logical interface */
	uint_t	ill_max_mtu;	/* Maximum MTU for any logical interface */
	uint_t	ill_user_mtu;	/* User-specified MTU via SIOCSLIFLNKINFO */
	uint32_t ill_reachable_time;	/* Value for ND algorithm in msec */
	uint32_t ill_reachable_retrans_time; /* Value for ND algorithm msec */
	uint_t	ill_max_buf;		/* Max # of req to buffer for ND */
	in6_addr_t	ill_token;
	uint_t		ill_token_length;
	uint32_t	ill_xmit_count;		/* ndp max multicast xmits */
	mib2_ipIfStatsEntry_t	*ill_ip_mib;	/* ver indep. interface mib */
	mib2_ipv6IfIcmpEntry_t	*ill_icmp6_mib;	/* Per interface mib */
	/*
	 * Following two mblks are allocated common to all
	 * the ipifs when the first interface is coming up.
	 * It is sent up to arp when the last ipif is coming
	 * down.
	 */
	mblk_t			*ill_arp_down_mp;
	mblk_t			*ill_arp_del_mapping_mp;
	/*
	 * Used for implementing IFF_NOARP. As IFF_NOARP is used
	 * to turn off for all the logicals, it is here instead
	 * of the ipif.
	 */
	mblk_t			*ill_arp_on_mp;

	phyint_t		*ill_phyint;
	uint64_t		ill_flags;

	kmutex_t	ill_lock;	/* Please see table below */
	/*
	 * The ill_nd_lla* fields handle the link layer address option
	 * from neighbor discovery. This is used for external IPv6
	 * address resolution.
	 */
	mblk_t		*ill_nd_lla_mp;	/* mblk which holds ill_nd_lla */
	uint8_t		*ill_nd_lla;	/* Link Layer Address */
	uint_t		ill_nd_lla_len;	/* Link Layer Address length */
	/*
	 * We now have 3 phys_addr_req's sent down. This field keeps track
	 * of which one is pending.
	 */
	t_uscalar_t	ill_phys_addr_pend; /* which dl_phys_addr_req pending */
	/*
	 * Used to save errors that occur during plumbing
	 */
	uint_t		ill_ifname_pending_err;
	avl_node_t	ill_avl_byppa; /* avl node based on ppa */
	void		*ill_fastpath_list; /* both ire and nce hang off this */
	uint_t		ill_refcnt;	/* active refcnt by threads */
	uint_t		ill_ire_cnt;	/* ires associated with this ill */
	kcondvar_t	ill_cv;
	uint_t		ill_ilm_walker_cnt;	/* snmp ilm walkers */
	uint_t		ill_nce_cnt;	/* nces associated with this ill */
	uint_t		ill_waiters;	/* threads waiting in ipsq_enter */
	/*
	 * Contains the upper read queue pointer of the module immediately
	 * beneath IP.  This field allows IP to validate sub-capability
	 * acknowledgments coming up from downstream.
	 */
	queue_t		*ill_lmod_rq;	/* read queue pointer of module below */
	uint_t		ill_lmod_cnt;	/* number of modules beneath IP */
	ip_m_t		*ill_media;	/* media specific params/functions */
	t_uscalar_t	ill_dlpi_pending; /* Last DLPI primitive issued */
	uint_t		ill_usesrc_ifindex; /* use src addr from this ILL */
	struct ill_s	*ill_usesrc_grp_next; /* Next ILL in the usesrc group */
	boolean_t	ill_trace_disable;	/* True when alloc fails */
	zoneid_t	ill_zoneid;
	ip_stack_t	*ill_ipst;	/* Corresponds to a netstack_hold */
	uint32_t	ill_dhcpinit;	/* IP_DHCPINIT_IFs for ill */
	void		*ill_flownotify_mh; /* Tx flow ctl, mac cb handle */
	uint_t		ill_ilm_cnt;    /* ilms referencing this ill */
	uint_t		ill_ipallmulti_cnt; /* ip_join_allmulti() calls */
	/*
	 * IPMP fields.
	 */
	ipmp_illgrp_t	*ill_grp;	/* IPMP group information */
	list_node_t	ill_actnode; 	/* next active ill in group */
	list_node_t	ill_grpnode;	/* next ill in group */
	ipif_t		*ill_src_ipif;	/* source address selection rotor */
	ipif_t		*ill_move_ipif;	/* ipif awaiting move to new ill */
	boolean_t	ill_nom_cast;	/* nominated for mcast/bcast */
	uint_t		ill_bound_cnt;	/* # of data addresses bound to ill */
	ipif_t		*ill_bound_ipif; /* ipif chain bound to ill */
	timeout_id_t	ill_refresh_tid; /* ill refresh retry timeout id */
} ill_t;

/*
 * ILL_FREE_OK() means that there are no incoming pointer references
 * to the ill.
 */
#define	ILL_FREE_OK(ill)					\
	((ill)->ill_ire_cnt == 0 && (ill)->ill_ilm_cnt == 0 &&	\
	(ill)->ill_nce_cnt == 0)

/*
 * An ipif/ill can be marked down only when the ire and nce references
 * to that ipif/ill goes to zero. ILL_DOWN_OK() is a necessary condition
 * quiescence checks. See comments above IPIF_DOWN_OK for details
 * on why ires and nces are selectively considered for this macro.
 */
#define	ILL_DOWN_OK(ill)	(ill->ill_ire_cnt == 0 && ill->ill_nce_cnt == 0)

/*
 * The following table lists the protection levels of the various members
 * of the ill_t. Same notation as that used for ipif_t above is used.
 *
 *				Write			Read
 *
 * ill_ifptr			ill_g_lock + s		Write once
 * ill_rq			ipsq			Write once
 * ill_wq			ipsq			Write once
 *
 * ill_error			ipsq			None
 * ill_ipif			ill_g_lock + ipsq	ill_g_lock OR ipsq
 * ill_ipif_up_count		ill_lock + ipsq		ill_lock OR ipsq
 * ill_max_frag			ipsq			Write once
 *
 * ill_name			ill_g_lock + ipsq	Write once
 * ill_name_length		ill_g_lock + ipsq	Write once
 * ill_ndd_name			ipsq			Write once
 * ill_net_type			ipsq			Write once
 * ill_ppa			ill_g_lock + ipsq	Write once
 * ill_sap			ipsq + down ill		Write once
 * ill_sap_length		ipsq + down ill		Write once
 * ill_phys_addr_length		ipsq + down ill		Write once
 *
 * ill_bcast_addr_length	ipsq			ipsq
 * ill_mactype			ipsq			ipsq
 * ill_frag_ptr			ipsq			ipsq
 *
 * ill_frag_timer_id		ill_lock		ill_lock
 * ill_frag_hash_tbl		ipsq			up ill
 * ill_ilm			ipsq + ill_lock		ill_lock
 * ill_mcast_type		ill_lock		ill_lock
 * ill_mcast_v1_time		ill_lock		ill_lock
 * ill_mcast_v2_time		ill_lock		ill_lock
 * ill_mcast_v1_tset		ill_lock		ill_lock
 * ill_mcast_v2_tset		ill_lock		ill_lock
 * ill_mcast_rv			ill_lock		ill_lock
 * ill_mcast_qi			ill_lock		ill_lock
 * ill_pending_mp		ill_lock		ill_lock
 *
 * ill_bcast_mp			ipsq			ipsq
 * ill_resolver_mp		ipsq			only when ill is up
 * ill_down_mp			ipsq			ipsq
 * ill_dlpi_deferred		ill_lock		ill_lock
 * ill_dlpi_pending		ill_lock		ill_lock
 * ill_phys_addr_mp		ipsq + down ill		only when ill is up
 * ill_phys_addr		ipsq + down ill		only when ill is up
 *
 * ill_state_flags		ill_lock		ill_lock
 * exclusive bit flags		ipsq_t			ipsq_t
 * shared bit flags		ill_lock		ill_lock
 *
 * ill_arp_muxid		ipsq			Not atomic
 * ill_ip_muxid			ipsq			Not atomic
 *
 * ill_ipf_gen			Not atomic
 * ill_frag_count		atomics			atomics
 * ill_type			ipsq + down ill		only when ill is up
 * ill_dlpi_multicast_state	ill_lock		ill_lock
 * ill_dlpi_fastpath_state	ill_lock		ill_lock
 * ill_dlpi_capab_state		ipsq			ipsq
 * ill_max_hops			ipsq			Not atomic
 *
 * ill_max_mtu
 *
 * ill_user_mtu			ipsq + ill_lock		ill_lock
 * ill_reachable_time		ipsq + ill_lock		ill_lock
 * ill_reachable_retrans_time	ipsq + ill_lock		ill_lock
 * ill_max_buf			ipsq + ill_lock		ill_lock
 *
 * Next 2 fields need ill_lock because of the get ioctls. They should not
 * report partially updated results without executing in the ipsq.
 * ill_token			ipsq + ill_lock		ill_lock
 * ill_token_length		ipsq + ill_lock		ill_lock
 * ill_xmit_count		ipsq + down ill		write once
 * ill_ip6_mib			ipsq + down ill		only when ill is up
 * ill_icmp6_mib		ipsq + down ill		only when ill is up
 * ill_arp_down_mp		ipsq			ipsq
 * ill_arp_del_mapping_mp	ipsq			ipsq
 * ill_arp_on_mp		ipsq			ipsq
 *
 * ill_phyint			ipsq, ill_g_lock, ill_lock	Any of them
 * ill_flags			ill_lock		ill_lock
 * ill_nd_lla_mp		ipsq + down ill		only when ill is up
 * ill_nd_lla			ipsq + down ill		only when ill is up
 * ill_nd_lla_len		ipsq + down ill		only when ill is up
 * ill_phys_addr_pend		ipsq + down ill		only when ill is up
 * ill_ifname_pending_err	ipsq			ipsq
 * ill_avl_byppa		ipsq, ill_g_lock	write once
 *
 * ill_fastpath_list		ill_lock		ill_lock
 * ill_refcnt			ill_lock		ill_lock
 * ill_ire_cnt			ill_lock		ill_lock
 * ill_cv			ill_lock		ill_lock
 * ill_ilm_walker_cnt		ill_lock		ill_lock
 * ill_nce_cnt			ill_lock		ill_lock
 * ill_ilm_cnt			ill_lock		ill_lock
 * ill_src_ipif			ill_g_lock		ill_g_lock
 * ill_trace			ill_lock		ill_lock
 * ill_usesrc_grp_next		ill_g_usesrc_lock	ill_g_usesrc_lock
 * ill_dhcpinit			atomics			atomics
 * ill_flownotify_mh		write once		write once
 * ill_capab_pending_cnt	ipsq			ipsq
 *
 * ill_bound_cnt		ipsq			ipsq
 * ill_bound_ipif		ipsq			ipsq
 * ill_actnode			ipsq + ipmp_lock	ipsq OR ipmp_lock
 * ill_grpnode			ipsq + ill_g_lock	ipsq OR ill_g_lock
 * ill_src_ipif			ill_g_lock		ill_g_lock
 * ill_move_ipif		ipsq			ipsq
 * ill_nom_cast			ipsq			ipsq OR advisory
 * ill_refresh_tid		ill_lock		ill_lock
 * ill_grp (for IPMP ill)	write once		write once
 * ill_grp (for underlying ill)	ipsq + ill_g_lock	ipsq OR ill_g_lock
 *
 * NOTE: It's OK to make heuristic decisions on an underlying interface
 *	 by using IS_UNDER_IPMP() or comparing ill_grp's raw pointer value.
 */

/*
 * For ioctl restart mechanism see ip_reprocess_ioctl()
 */
struct ip_ioctl_cmd_s;

typedef	int (*ifunc_t)(ipif_t *, struct sockaddr_in *, queue_t *, mblk_t *,
    struct ip_ioctl_cmd_s *, void *);

typedef struct ip_ioctl_cmd_s {
	int	ipi_cmd;
	size_t	ipi_copyin_size;
	uint_t	ipi_flags;
	uint_t	ipi_cmd_type;
	ifunc_t	ipi_func;
	ifunc_t	ipi_func_restart;
} ip_ioctl_cmd_t;

/*
 * ipi_cmd_type:
 *
 * IF_CMD		1	old style ifreq cmd
 * LIF_CMD		2	new style lifreq cmd
 * TUN_CMD		3	tunnel related
 * ARP_CMD		4	arpreq cmd
 * XARP_CMD		5	xarpreq cmd
 * MSFILT_CMD		6	multicast source filter cmd
 * MISC_CMD		7	misc cmd (not a more specific one above)
 */

enum { IF_CMD = 1, LIF_CMD, TUN_CMD, ARP_CMD, XARP_CMD, MSFILT_CMD, MISC_CMD };

#define	IPI_DONTCARE	0	/* For ioctl encoded values that don't matter */

/* Flag values in ipi_flags */
#define	IPI_PRIV	0x1	/* Root only command */
#define	IPI_MODOK	0x2	/* Permitted on mod instance of IP */
#define	IPI_WR		0x4	/* Need to grab writer access */
#define	IPI_GET_CMD	0x8	/* branch to mi_copyout on success */
/*	unused		0x10	*/
#define	IPI_NULL_BCONT	0x20	/* ioctl has not data and hence no b_cont */
#define	IPI_PASS_DOWN	0x40	/* pass this ioctl down when a module only */

extern ip_ioctl_cmd_t	ip_ndx_ioctl_table[];
extern ip_ioctl_cmd_t	ip_misc_ioctl_table[];
extern int ip_ndx_ioctl_count;
extern int ip_misc_ioctl_count;

/* Passed down by ARP to IP during I_PLINK/I_PUNLINK */
typedef struct ipmx_s {
	char	ipmx_name[LIFNAMSIZ];		/* if name */
	uint_t
		ipmx_arpdev_stream : 1,		/* This is the arp stream */
		ipmx_notused : 31;
} ipmx_t;

/*
 * State for detecting if a driver supports certain features.
 * Support for DL_ENABMULTI_REQ uses ill_dlpi_multicast_state.
 * Support for DLPI M_DATA fastpath uses ill_dlpi_fastpath_state.
 */
#define	IDS_UNKNOWN	0	/* No DLPI request sent */
#define	IDS_INPROGRESS	1	/* DLPI request sent */
#define	IDS_OK		2	/* DLPI request completed successfully */
#define	IDS_FAILED	3	/* DLPI request failed */

/* Support for DL_CAPABILITY_REQ uses ill_dlpi_capab_state. */
enum {
	IDCS_UNKNOWN,
	IDCS_PROBE_SENT,
	IDCS_OK,
	IDCS_RESET_SENT,
	IDCS_RENEG,
	IDCS_FAILED
};

/* Named Dispatch Parameter Management Structure */
typedef struct ipparam_s {
	uint_t	ip_param_min;
	uint_t	ip_param_max;
	uint_t	ip_param_value;
	char	*ip_param_name;
} ipparam_t;

/* Extended NDP Management Structure */
typedef struct ipndp_s {
	ndgetf_t	ip_ndp_getf;
	ndsetf_t	ip_ndp_setf;
	caddr_t		ip_ndp_data;
	char		*ip_ndp_name;
} ipndp_t;

/*
 * The kernel stores security attributes of all gateways in a database made
 * up of one or more tsol_gcdb_t elements.  Each tsol_gcdb_t contains the
 * security-related credentials of the gateway.  More than one gateways may
 * share entries in the database.
 *
 * The tsol_gc_t structure represents the gateway to credential association,
 * and refers to an entry in the database.  One or more tsol_gc_t entities are
 * grouped together to form one or more tsol_gcgrp_t, each representing the
 * list of security attributes specific to the gateway.  A gateway may be
 * associated with at most one credentials group.
 */
struct tsol_gcgrp_s;

extern uchar_t	ip6opt_ls;	/* TX IPv6 enabler */

/*
 * Gateway security credential record.
 */
typedef struct tsol_gcdb_s {
	uint_t		gcdb_refcnt;	/* reference count */
	struct rtsa_s	gcdb_attr;	/* security attributes */
#define	gcdb_mask	gcdb_attr.rtsa_mask
#define	gcdb_doi	gcdb_attr.rtsa_doi
#define	gcdb_slrange	gcdb_attr.rtsa_slrange
} tsol_gcdb_t;

/*
 * Gateway to credential association.
 */
typedef struct tsol_gc_s {
	uint_t		gc_refcnt;	/* reference count */
	struct tsol_gcgrp_s *gc_grp;	/* pointer to group */
	struct tsol_gc_s *gc_prev;	/* previous in list */
	struct tsol_gc_s *gc_next;	/* next in list */
	tsol_gcdb_t	*gc_db;		/* pointer to actual credentials */
} tsol_gc_t;

/*
 * Gateway credentials group address.
 */
typedef struct tsol_gcgrp_addr_s {
	int		ga_af;		/* address family */
	in6_addr_t	ga_addr;	/* IPv4 mapped or IPv6 address */
} tsol_gcgrp_addr_t;

/*
 * Gateway credentials group.
 */
typedef struct tsol_gcgrp_s {
	uint_t		gcgrp_refcnt;	/* reference count */
	krwlock_t	gcgrp_rwlock;	/* lock to protect following */
	uint_t		gcgrp_count;	/* number of credentials */
	tsol_gc_t	*gcgrp_head;	/* first credential in list */
	tsol_gc_t	*gcgrp_tail;	/* last credential in list */
	tsol_gcgrp_addr_t gcgrp_addr;	/* next-hop gateway address */
} tsol_gcgrp_t;

extern kmutex_t gcgrp_lock;

#define	GC_REFRELE(p) {				\
	ASSERT((p)->gc_grp != NULL);		\
	rw_enter(&(p)->gc_grp->gcgrp_rwlock, RW_WRITER); \
	ASSERT((p)->gc_refcnt > 0);		\
	if (--((p)->gc_refcnt) == 0)		\
		gc_inactive(p);			\
	else					\
		rw_exit(&(p)->gc_grp->gcgrp_rwlock); \
}

#define	GCGRP_REFHOLD(p) {			\
	mutex_enter(&gcgrp_lock);		\
	++((p)->gcgrp_refcnt);			\
	ASSERT((p)->gcgrp_refcnt != 0);		\
	mutex_exit(&gcgrp_lock);		\
}

#define	GCGRP_REFRELE(p) {			\
	mutex_enter(&gcgrp_lock);		\
	ASSERT((p)->gcgrp_refcnt > 0);		\
	if (--((p)->gcgrp_refcnt) == 0)		\
		gcgrp_inactive(p);		\
	ASSERT(MUTEX_HELD(&gcgrp_lock));	\
	mutex_exit(&gcgrp_lock);		\
}

/*
 * IRE gateway security attributes structure, pointed to by tsol_ire_gw_secattr
 */
struct tsol_tnrhc;

typedef struct tsol_ire_gw_secattr_s {
	kmutex_t	igsa_lock;	/* lock to protect following */
	struct tsol_tnrhc *igsa_rhc;	/* host entry for gateway */
	tsol_gc_t	*igsa_gc;	/* for prefix IREs */
	tsol_gcgrp_t	*igsa_gcgrp;	/* for cache IREs */
} tsol_ire_gw_secattr_t;

/*
 * Following are the macros to increment/decrement the reference
 * count of the IREs and IRBs (ire bucket).
 *
 * 1) We bump up the reference count of an IRE to make sure that
 *    it does not get deleted and freed while we are using it.
 *    Typically all the lookup functions hold the bucket lock,
 *    and look for the IRE. If it finds an IRE, it bumps up the
 *    reference count before dropping the lock. Sometimes we *may* want
 *    to bump up the reference count after we *looked* up i.e without
 *    holding the bucket lock. So, the IRE_REFHOLD macro does not assert
 *    on the bucket lock being held. Any thread trying to delete from
 *    the hash bucket can still do so but cannot free the IRE if
 *    ire_refcnt is not 0.
 *
 * 2) We bump up the reference count on the bucket where the IRE resides
 *    (IRB), when we want to prevent the IREs getting deleted from a given
 *    hash bucket. This makes life easier for ire_walk type functions which
 *    wants to walk the IRE list, call a function, but needs to drop
 *    the bucket lock to prevent recursive rw_enters. While the
 *    lock is dropped, the list could be changed by other threads or
 *    the same thread could end up deleting the ire or the ire pointed by
 *    ire_next. IRE_REFHOLDing the ire or ire_next is not sufficient as
 *    a delete will still remove the ire from the bucket while we have
 *    dropped the lock and hence the ire_next would be NULL. Thus, we
 *    need a mechanism to prevent deletions from a given bucket.
 *
 *    To prevent deletions, we bump up the reference count on the
 *    bucket. If the bucket is held, ire_delete just marks IRE_MARK_CONDEMNED
 *    both on the ire's ire_marks and the bucket's irb_marks. When the
 *    reference count on the bucket drops to zero, all the CONDEMNED ires
 *    are deleted. We don't have to bump up the reference count on the
 *    bucket if we are walking the bucket and never have to drop the bucket
 *    lock. Note that IRB_REFHOLD does not prevent addition of new ires
 *    in the list. It is okay because addition of new ires will not cause
 *    ire_next to point to freed memory. We do IRB_REFHOLD only when
 *    all of the 3 conditions are true :
 *
 *    1) The code needs to walk the IRE bucket from start to end.
 *    2) It may have to drop the bucket lock sometimes while doing (1)
 *    3) It does not want any ires to be deleted meanwhile.
 */

/*
 * Bump up the reference count on the IRE. We cannot assert that the
 * bucket lock is being held as it is legal to bump up the reference
 * count after the first lookup has returned the IRE without
 * holding the lock. Currently ip_wput does this for caching IRE_CACHEs.
 */

#ifdef DEBUG
#define	IRE_UNTRACE_REF(ire)	ire_untrace_ref(ire);
#define	IRE_TRACE_REF(ire)	ire_trace_ref(ire);
#else
#define	IRE_UNTRACE_REF(ire)
#define	IRE_TRACE_REF(ire)
#endif

#define	IRE_REFHOLD_NOTR(ire) {				\
	atomic_add_32(&(ire)->ire_refcnt, 1);		\
	ASSERT((ire)->ire_refcnt != 0);			\
}

#define	IRE_REFHOLD(ire) {				\
	IRE_REFHOLD_NOTR(ire);				\
	IRE_TRACE_REF(ire);				\
}

#define	IRE_REFHOLD_LOCKED(ire)	{			\
	IRE_TRACE_REF(ire);				\
	(ire)->ire_refcnt++;				\
}

/*
 * Decrement the reference count on the IRE.
 * In architectures e.g sun4u, where atomic_add_32_nv is just
 * a cas, we need to maintain the right memory barrier semantics
 * as that of mutex_exit i.e all the loads and stores should complete
 * before the cas is executed. membar_exit() does that here.
 *
 * NOTE : This macro is used only in places where we want performance.
 *	  To avoid bloating the code, we use the function "ire_refrele"
 *	  which essentially calls the macro.
 */
#define	IRE_REFRELE_NOTR(ire) {					\
	ASSERT((ire)->ire_refcnt != 0);				\
	membar_exit();						\
	if (atomic_add_32_nv(&(ire)->ire_refcnt, -1) == 0)	\
		ire_inactive(ire);				\
}

#define	IRE_REFRELE(ire) {					\
	if (ire->ire_bucket != NULL) {				\
		IRE_UNTRACE_REF(ire);				\
	}							\
	IRE_REFRELE_NOTR(ire);					\
}

/*
 * Bump up the reference count on the hash bucket - IRB to
 * prevent ires from being deleted in this bucket.
 */
#define	IRB_REFHOLD(irb) {				\
	rw_enter(&(irb)->irb_lock, RW_WRITER);		\
	(irb)->irb_refcnt++;				\
	ASSERT((irb)->irb_refcnt != 0);			\
	rw_exit(&(irb)->irb_lock);			\
}
#define	IRB_REFHOLD_LOCKED(irb) {			\
	ASSERT(RW_WRITE_HELD(&(irb)->irb_lock));	\
	(irb)->irb_refcnt++;				\
	ASSERT((irb)->irb_refcnt != 0);			\
}

void irb_refrele_ftable(irb_t *);
/*
 * Note: when IRB_MARK_FTABLE (i.e., IRE_CACHETABLE entry), the irb_t
 * is statically allocated, so that when the irb_refcnt goes to 0,
 * we simply clean up the ire list and continue.
 */
#define	IRB_REFRELE(irb) {				\
	if ((irb)->irb_marks & IRB_MARK_FTABLE) {	\
		irb_refrele_ftable((irb));		\
	} else {					\
		rw_enter(&(irb)->irb_lock, RW_WRITER);		\
		ASSERT((irb)->irb_refcnt != 0);			\
		if (--(irb)->irb_refcnt	== 0 &&			\
		    ((irb)->irb_marks & IRE_MARK_CONDEMNED)) {	\
			ire_t *ire_list;			\
								\
			ire_list = ire_unlink(irb);		\
			rw_exit(&(irb)->irb_lock);		\
			ASSERT(ire_list != NULL);		\
			ire_cleanup(ire_list);			\
		} else {					\
			rw_exit(&(irb)->irb_lock);		\
		}						\
	}							\
}

extern struct kmem_cache *rt_entry_cache;

/*
 * Lock the fast path mp for access, since the fp_mp can be deleted
 * due a DL_NOTE_FASTPATH_FLUSH in the case of IRE_BROADCAST
 */

#define	LOCK_IRE_FP_MP(ire) {				\
		if ((ire)->ire_type == IRE_BROADCAST)	\
			mutex_enter(&ire->ire_nce->nce_lock);	\
	}
#define	UNLOCK_IRE_FP_MP(ire) {				\
		if ((ire)->ire_type == IRE_BROADCAST)	\
			mutex_exit(&ire->ire_nce->nce_lock);	\
	}

typedef struct ire4 {
	ipaddr_t ire4_src_addr;		/* Source address to use. */
	ipaddr_t ire4_mask;		/* Mask for matching this IRE. */
	ipaddr_t ire4_addr;		/* Address this IRE represents. */
	ipaddr_t ire4_gateway_addr;	/* Gateway if IRE_CACHE/IRE_OFFSUBNET */
	ipaddr_t ire4_cmask;		/* Mask from parent prefix route */
} ire4_t;

typedef struct ire6 {
	in6_addr_t ire6_src_addr;	/* Source address to use. */
	in6_addr_t ire6_mask;		/* Mask for matching this IRE. */
	in6_addr_t ire6_addr;		/* Address this IRE represents. */
	in6_addr_t ire6_gateway_addr;	/* Gateway if IRE_CACHE/IRE_OFFSUBNET */
	in6_addr_t ire6_cmask;		/* Mask from parent prefix route */
} ire6_t;

typedef union ire_addr {
	ire6_t	ire6_u;
	ire4_t	ire4_u;
} ire_addr_u_t;

/* Internet Routing Entry */
typedef struct ire_s {
	struct	ire_s	*ire_next;	/* The hash chain must be first. */
	struct	ire_s	**ire_ptpn;	/* Pointer to previous next. */
	uint32_t	ire_refcnt;	/* Number of references */
	mblk_t		*ire_mp;	/* Non-null if allocated as mblk */
	queue_t		*ire_rfq;	/* recv from this queue */
	queue_t		*ire_stq;	/* send to this queue */
	union {
		uint_t	*max_fragp;	/* Used only during ire creation */
		uint_t	max_frag;	/* MTU (next hop or path). */
	} imf_u;
#define	ire_max_frag	imf_u.max_frag
#define	ire_max_fragp	imf_u.max_fragp
	uint32_t	ire_frag_flag;	/* IPH_DF or zero. */
	uint32_t	ire_ident;	/* Per IRE IP ident. */
	uint32_t	ire_tire_mark;	/* Used for reclaim of unused. */
	uchar_t		ire_ipversion;	/* IPv4/IPv6 version */
	uchar_t		ire_marks;	/* IRE_MARK_CONDEMNED etc. */
	ushort_t	ire_type;	/* Type of IRE */
	uint_t	ire_ib_pkt_count;	/* Inbound packets for ire_addr */
	uint_t	ire_ob_pkt_count;	/* Outbound packets to ire_addr */
	uint_t	ire_ll_hdr_length;	/* Non-zero if we do M_DATA prepends */
	time_t	ire_create_time;	/* Time (in secs) IRE was created. */
	uint32_t	ire_phandle;	/* Associate prefix IREs to cache */
	uint32_t	ire_ihandle;	/* Associate interface IREs to cache */
	ipif_t		*ire_ipif;	/* the interface that this ire uses */
	uint32_t	ire_flags;	/* flags related to route (RTF_*) */
	uint_t ire_ipsec_overhead;	/* IPSEC overhead */
	/*
	 * Neighbor Cache Entry for IPv6; arp info for IPv4
	 */
	struct	nce_s	*ire_nce;
	uint_t		ire_masklen;	/* # bits in ire_mask{,_v6} */
	ire_addr_u_t	ire_u;		/* IPv4/IPv6 address info. */

	irb_t		*ire_bucket;	/* Hash bucket when ire_ptphn is set */
	iulp_t		ire_uinfo;	/* Upper layer protocol info. */
	/*
	 * Protects ire_uinfo, ire_max_frag, and ire_frag_flag.
	 */
	kmutex_t	ire_lock;
	uint_t		ire_ipif_seqid; /* ipif_seqid of ire_ipif */
	uint_t		ire_ipif_ifindex; /* ifindex associated with ipif */
	clock_t		ire_last_used_time;	/* Last used time */
	tsol_ire_gw_secattr_t *ire_gw_secattr; /* gateway security attributes */
	zoneid_t	ire_zoneid;	/* for local address discrimination */
	/*
	 * ire's that are embedded inside mblk_t and sent to the external
	 * resolver use the ire_stq_ifindex to track the ifindex of the
	 * ire_stq, so that the ill (if it exists) can be correctly recovered
	 * for cleanup in the esbfree routine when arp failure occurs.
	 * Similarly, the ire_stackid is used to recover the ip_stack_t.
	 */
	uint_t		ire_stq_ifindex;
	netstackid_t	ire_stackid;
	uint_t		ire_defense_count;	/* number of ARP conflicts */
	uint_t		ire_defense_time;	/* last time defended (secs) */
	boolean_t	ire_trace_disable;	/* True when alloc fails */
	ip_stack_t	*ire_ipst;	/* Does not have a netstack_hold */
} ire_t;

/* IPv4 compatibility macros */
#define	ire_src_addr		ire_u.ire4_u.ire4_src_addr
#define	ire_mask		ire_u.ire4_u.ire4_mask
#define	ire_addr		ire_u.ire4_u.ire4_addr
#define	ire_gateway_addr	ire_u.ire4_u.ire4_gateway_addr
#define	ire_cmask		ire_u.ire4_u.ire4_cmask

#define	ire_src_addr_v6		ire_u.ire6_u.ire6_src_addr
#define	ire_mask_v6		ire_u.ire6_u.ire6_mask
#define	ire_addr_v6		ire_u.ire6_u.ire6_addr
#define	ire_gateway_addr_v6	ire_u.ire6_u.ire6_gateway_addr
#define	ire_cmask_v6		ire_u.ire6_u.ire6_cmask

/* Convenient typedefs for sockaddrs */
typedef	struct sockaddr_in	sin_t;
typedef	struct sockaddr_in6	sin6_t;

/* Address structure used for internal bind with IP */
typedef struct ipa_conn_s {
	ipaddr_t	ac_laddr;
	ipaddr_t	ac_faddr;
	uint16_t	ac_fport;
	uint16_t	ac_lport;
} ipa_conn_t;

typedef struct ipa6_conn_s {
	in6_addr_t	ac6_laddr;
	in6_addr_t	ac6_faddr;
	uint16_t	ac6_fport;
	uint16_t	ac6_lport;
} ipa6_conn_t;

/*
 * Using ipa_conn_x_t or ipa6_conn_x_t allows us to modify the behavior of IP's
 * bind handler.
 */
typedef struct ipa_conn_extended_s {
	uint64_t	acx_flags;
	ipa_conn_t	acx_conn;
} ipa_conn_x_t;

typedef struct ipa6_conn_extended_s {
	uint64_t	ac6x_flags;
	ipa6_conn_t	ac6x_conn;
} ipa6_conn_x_t;

/* flag values for ipa_conn_x_t and ipa6_conn_x_t. */
#define	ACX_VERIFY_DST	0x1ULL	/* verify destination address is reachable */

/* Name/Value Descriptor. */
typedef struct nv_s {
	uint64_t nv_value;
	char	*nv_name;
} nv_t;

#define	ILL_FRAG_HASH(s, i) \
	((ntohl(s) ^ ((i) ^ ((i) >> 8))) % ILL_FRAG_HASH_TBL_COUNT)

/*
 * The MAX number of allowed fragmented packets per hash bucket
 * calculation is based on the most common mtu size of 1500. This limit
 * will work well for other mtu sizes as well.
 */
#define	COMMON_IP_MTU 1500
#define	MAX_FRAG_MIN 10
#define	MAX_FRAG_PKTS(ipst)	\
	MAX(MAX_FRAG_MIN, (2 * (ipst->ips_ip_reass_queue_bytes / \
	    (COMMON_IP_MTU * ILL_FRAG_HASH_TBL_COUNT))))

/*
 * Maximum dups allowed per packet.
 */
extern uint_t ip_max_frag_dups;

/*
 * Per-packet information for received packets and transmitted.
 * Used by the transport protocols when converting between the packet
 * and ancillary data and socket options.
 *
 * Note: This private data structure and related IPPF_* constant
 * definitions are exposed to enable compilation of some debugging tools
 * like lsof which use struct tcp_t in <inet/tcp.h>. This is intended to be
 * a temporary hack and long term alternate interfaces should be defined
 * to support the needs of such tools and private definitions moved to
 * private headers.
 */
struct ip6_pkt_s {
	uint_t		ipp_fields;		/* Which fields are valid */
	uint_t		ipp_sticky_ignored;	/* sticky fields to ignore */
	uint_t		ipp_ifindex;		/* pktinfo ifindex */
	in6_addr_t	ipp_addr;		/* pktinfo src/dst addr */
	uint_t		ipp_unicast_hops;	/* IPV6_UNICAST_HOPS */
	uint_t		ipp_multicast_hops;	/* IPV6_MULTICAST_HOPS */
	uint_t		ipp_hoplimit;		/* IPV6_HOPLIMIT */
	uint_t		ipp_hopoptslen;
	uint_t		ipp_rtdstoptslen;
	uint_t		ipp_rthdrlen;
	uint_t		ipp_dstoptslen;
	uint_t		ipp_pathmtulen;
	uint_t		ipp_fraghdrlen;
	ip6_hbh_t	*ipp_hopopts;
	ip6_dest_t	*ipp_rtdstopts;
	ip6_rthdr_t	*ipp_rthdr;
	ip6_dest_t	*ipp_dstopts;
	ip6_frag_t	*ipp_fraghdr;
	struct ip6_mtuinfo *ipp_pathmtu;
	in6_addr_t	ipp_nexthop;		/* Transmit only */
	uint8_t		ipp_tclass;
	int8_t		ipp_use_min_mtu;
};
typedef struct ip6_pkt_s ip6_pkt_t;

extern void ip6_pkt_free(ip6_pkt_t *);	/* free storage inside ip6_pkt_t */

/*
 * This struct is used by ULP_opt_set() functions to return value of IPv4
 * ancillary options. Currently this is only used by udp and icmp and only
 * IP_PKTINFO option is supported.
 */
typedef struct ip4_pkt_s {
	uint_t		ip4_ill_index;	/* interface index */
	ipaddr_t	ip4_addr;	/* source address */
} ip4_pkt_t;

/*
 * Used by ULP's to pass options info to ip_output
 * currently only IP_PKTINFO is supported.
 */
typedef struct ip_opt_info_s {
	uint_t ip_opt_ill_index;
	uint_t ip_opt_flags;
} ip_opt_info_t;

/*
 * value for ip_opt_flags
 */
#define	IP_VERIFY_SRC	0x1

/*
 * This structure is used to convey information from IP and the ULP.
 * Currently used for the IP_RECVSLLA, IP_RECVIF and IP_RECVPKTINFO options.
 * The type of information field is set to IN_PKTINFO (i.e inbound pkt info)
 */
typedef struct ip_pktinfo {
	uint32_t		ip_pkt_ulp_type;	/* type of info sent */
	uint32_t		ip_pkt_flags;	/* what is sent up by IP */
	uint32_t		ip_pkt_ifindex;	/* inbound interface index */
	struct sockaddr_dl	ip_pkt_slla;	/* has source link layer addr */
	struct in_addr		ip_pkt_match_addr; /* matched address */
} ip_pktinfo_t;

/*
 * flags to tell UDP what IP is sending; in_pkt_flags
 */
#define	IPF_RECVIF	0x01	/* inbound interface index */
#define	IPF_RECVSLLA	0x02	/* source link layer address */
/*
 * Inbound interface index + matched address.
 * Used only by IPV4.
 */
#define	IPF_RECVADDR	0x04

/* ipp_fields values */
#define	IPPF_IFINDEX	0x0001	/* Part of in6_pktinfo: ifindex */
#define	IPPF_ADDR	0x0002	/* Part of in6_pktinfo: src/dst addr */
#define	IPPF_SCOPE_ID	0x0004	/* Add xmit ip6i_t for sin6_scope_id */
#define	IPPF_NO_CKSUM	0x0008	/* Add xmit ip6i_t for IP6I_NO_*_CKSUM */

#define	IPPF_RAW_CKSUM	0x0010	/* Add xmit ip6i_t for IP6I_RAW_CHECKSUM */
#define	IPPF_HOPLIMIT	0x0020
#define	IPPF_HOPOPTS	0x0040
#define	IPPF_RTHDR	0x0080

#define	IPPF_RTDSTOPTS	0x0100
#define	IPPF_DSTOPTS	0x0200
#define	IPPF_NEXTHOP	0x0400
#define	IPPF_PATHMTU	0x0800

#define	IPPF_TCLASS	0x1000
#define	IPPF_DONTFRAG	0x2000
#define	IPPF_USE_MIN_MTU	0x04000
#define	IPPF_MULTICAST_HOPS	0x08000

#define	IPPF_UNICAST_HOPS	0x10000
#define	IPPF_FRAGHDR		0x20000

#define	IPPF_HAS_IP6I \
	(IPPF_IFINDEX|IPPF_ADDR|IPPF_NEXTHOP|IPPF_SCOPE_ID| \
	IPPF_NO_CKSUM|IPPF_RAW_CKSUM|IPPF_HOPLIMIT|IPPF_DONTFRAG| \
	IPPF_USE_MIN_MTU|IPPF_MULTICAST_HOPS|IPPF_UNICAST_HOPS)

#define	TCP_PORTS_OFFSET	0
#define	UDP_PORTS_OFFSET	0

/*
 * lookups return the ill/ipif only if the flags are clear OR Iam writer.
 * ill / ipif lookup functions increment the refcnt on the ill / ipif only
 * after calling these macros. This ensures that the refcnt on the ipif or
 * ill will eventually drop down to zero.
 */
#define	ILL_LOOKUP_FAILED	1	/* Used as error code */
#define	IPIF_LOOKUP_FAILED	2	/* Used as error code */

#define	ILL_CAN_LOOKUP(ill)						\
	(!((ill)->ill_state_flags & (ILL_CONDEMNED | ILL_CHANGING)) ||	\
	IAM_WRITER_ILL(ill))

#define	ILL_CAN_WAIT(ill, q)	\
	(((q) != NULL) && !((ill)->ill_state_flags & (ILL_CONDEMNED)))

#define	IPIF_CAN_LOOKUP(ipif)	\
	(!((ipif)->ipif_state_flags & (IPIF_CONDEMNED | IPIF_CHANGING)) || \
	IAM_WRITER_IPIF(ipif))

/*
 * If the parameter 'q' is NULL, the caller is not interested in wait and
 * restart of the operation if the ILL or IPIF cannot be looked up when it is
 * marked as 'CHANGING'. Typically a thread that tries to send out data  will
 * end up passing NULLs as the last 4 parameters to ill_lookup_on_ifindex and
 * in this case 'q' is NULL
 */
#define	IPIF_CAN_WAIT(ipif, q)	\
	(((q) != NULL) && !((ipif)->ipif_state_flags & (IPIF_CONDEMNED)))

#define	IPIF_CAN_LOOKUP_WALKER(ipif)					\
	(!((ipif)->ipif_state_flags & (IPIF_CONDEMNED)) ||		\
	IAM_WRITER_IPIF(ipif))

#define	ILL_UNMARK_CHANGING(ill)                                \
	(ill)->ill_state_flags &= ~ILL_CHANGING;

/* Macros used to assert that this thread is a writer */
#define	IAM_WRITER_IPSQ(ipsq)	((ipsq)->ipsq_xop->ipx_writer == curthread)
#define	IAM_WRITER_ILL(ill)	IAM_WRITER_IPSQ((ill)->ill_phyint->phyint_ipsq)
#define	IAM_WRITER_IPIF(ipif)	IAM_WRITER_ILL((ipif)->ipif_ill)

/*
 * Grab ill locks in the proper order. The order is highest addressed
 * ill is locked first.
 */
#define	GRAB_ILL_LOCKS(ill_1, ill_2)				\
{								\
	if ((ill_1) > (ill_2)) {				\
		if (ill_1 != NULL)				\
			mutex_enter(&(ill_1)->ill_lock);	\
		if (ill_2 != NULL)				\
			mutex_enter(&(ill_2)->ill_lock);	\
	} else {						\
		if (ill_2 != NULL)				\
			mutex_enter(&(ill_2)->ill_lock);	\
		if (ill_1 != NULL && ill_1 != ill_2)		\
			mutex_enter(&(ill_1)->ill_lock);	\
	}							\
}

#define	RELEASE_ILL_LOCKS(ill_1, ill_2)		\
{						\
	if (ill_1 != NULL)			\
		mutex_exit(&(ill_1)->ill_lock); \
	if (ill_2 != NULL && ill_2 != ill_1)	\
		mutex_exit(&(ill_2)->ill_lock); \
}

/* Get the other protocol instance ill */
#define	ILL_OTHER(ill)						\
	((ill)->ill_isv6 ? (ill)->ill_phyint->phyint_illv4 :	\
	    (ill)->ill_phyint->phyint_illv6)

/* ioctl command info: Ioctl properties extracted and stored in here */
typedef struct cmd_info_s
{
	ipif_t  *ci_ipif;	/* ipif associated with [l]ifreq ioctl's */
	sin_t	*ci_sin;	/* the sin struct passed down */
	sin6_t	*ci_sin6;	/* the sin6_t struct passed down */
	struct lifreq *ci_lifr;	/* the lifreq struct passed down */
} cmd_info_t;

/*
 * List of AH and ESP IPsec acceleration capable ills
 */
typedef struct ipsec_capab_ill_s {
	uint_t ill_index;
	boolean_t ill_isv6;
	struct ipsec_capab_ill_s *next;
} ipsec_capab_ill_t;

extern struct kmem_cache *ire_cache;

extern ipaddr_t	ip_g_all_ones;

extern	uint_t	ip_loopback_mtu;	/* /etc/system */

extern vmem_t *ip_minor_arena_sa;
extern vmem_t *ip_minor_arena_la;

/*
 * ip_g_forward controls IP forwarding.  It takes two values:
 *	0: IP_FORWARD_NEVER	Don't forward packets ever.
 *	1: IP_FORWARD_ALWAYS	Forward packets for elsewhere.
 *
 * RFC1122 says there must be a configuration switch to control forwarding,
 * but that the default MUST be to not forward packets ever.  Implicit
 * control based on configuration of multiple interfaces MUST NOT be
 * implemented (Section 3.1).  SunOS 4.1 did provide the "automatic" capability
 * and, in fact, it was the default.  That capability is now provided in the
 * /etc/rc2.d/S69inet script.
 */

#define	ips_ip_respond_to_address_mask_broadcast ips_param_arr[0].ip_param_value
#define	ips_ip_g_resp_to_echo_bcast	ips_param_arr[1].ip_param_value
#define	ips_ip_g_resp_to_echo_mcast	ips_param_arr[2].ip_param_value
#define	ips_ip_g_resp_to_timestamp	ips_param_arr[3].ip_param_value
#define	ips_ip_g_resp_to_timestamp_bcast ips_param_arr[4].ip_param_value
#define	ips_ip_g_send_redirects		ips_param_arr[5].ip_param_value
#define	ips_ip_g_forward_directed_bcast	ips_param_arr[6].ip_param_value
#define	ips_ip_mrtdebug			ips_param_arr[7].ip_param_value
#define	ips_ip_timer_interval		ips_param_arr[8].ip_param_value
#define	ips_ip_ire_arp_interval		ips_param_arr[9].ip_param_value
#define	ips_ip_ire_redir_interval	ips_param_arr[10].ip_param_value
#define	ips_ip_def_ttl			ips_param_arr[11].ip_param_value
#define	ips_ip_forward_src_routed	ips_param_arr[12].ip_param_value
#define	ips_ip_wroff_extra		ips_param_arr[13].ip_param_value
#define	ips_ip_ire_pathmtu_interval	ips_param_arr[14].ip_param_value
#define	ips_ip_icmp_return		ips_param_arr[15].ip_param_value
#define	ips_ip_path_mtu_discovery	ips_param_arr[16].ip_param_value
#define	ips_ip_ignore_delete_time	ips_param_arr[17].ip_param_value
#define	ips_ip_ignore_redirect		ips_param_arr[18].ip_param_value
#define	ips_ip_output_queue		ips_param_arr[19].ip_param_value
#define	ips_ip_broadcast_ttl		ips_param_arr[20].ip_param_value
#define	ips_ip_icmp_err_interval	ips_param_arr[21].ip_param_value
#define	ips_ip_icmp_err_burst		ips_param_arr[22].ip_param_value
#define	ips_ip_reass_queue_bytes	ips_param_arr[23].ip_param_value
#define	ips_ip_strict_dst_multihoming	ips_param_arr[24].ip_param_value
#define	ips_ip_addrs_per_if		ips_param_arr[25].ip_param_value
#define	ips_ipsec_override_persocket_policy ips_param_arr[26].ip_param_value
#define	ips_icmp_accept_clear_messages	ips_param_arr[27].ip_param_value
#define	ips_igmp_accept_clear_messages	ips_param_arr[28].ip_param_value

/* IPv6 configuration knobs */
#define	ips_delay_first_probe_time	ips_param_arr[29].ip_param_value
#define	ips_max_unicast_solicit		ips_param_arr[30].ip_param_value
#define	ips_ipv6_def_hops		ips_param_arr[31].ip_param_value
#define	ips_ipv6_icmp_return		ips_param_arr[32].ip_param_value
#define	ips_ipv6_forward_src_routed	ips_param_arr[33].ip_param_value
#define	ips_ipv6_resp_echo_mcast	ips_param_arr[34].ip_param_value
#define	ips_ipv6_send_redirects		ips_param_arr[35].ip_param_value
#define	ips_ipv6_ignore_redirect	ips_param_arr[36].ip_param_value
#define	ips_ipv6_strict_dst_multihoming	ips_param_arr[37].ip_param_value
#define	ips_ip_ire_reclaim_fraction	ips_param_arr[38].ip_param_value
#define	ips_ipsec_policy_log_interval	ips_param_arr[39].ip_param_value
#define	ips_pim_accept_clear_messages	ips_param_arr[40].ip_param_value
#define	ips_ip_ndp_unsolicit_interval	ips_param_arr[41].ip_param_value
#define	ips_ip_ndp_unsolicit_count	ips_param_arr[42].ip_param_value
#define	ips_ipv6_ignore_home_address_opt ips_param_arr[43].ip_param_value

/* Misc IP configuration knobs */
#define	ips_ip_policy_mask		ips_param_arr[44].ip_param_value
#define	ips_ip_multirt_resolution_interval ips_param_arr[45].ip_param_value
#define	ips_ip_multirt_ttl  		ips_param_arr[46].ip_param_value
#define	ips_ip_multidata_outbound	ips_param_arr[47].ip_param_value
#define	ips_ip_ndp_defense_interval	ips_param_arr[48].ip_param_value
#define	ips_ip_max_temp_idle		ips_param_arr[49].ip_param_value
#define	ips_ip_max_temp_defend		ips_param_arr[50].ip_param_value
#define	ips_ip_max_defend		ips_param_arr[51].ip_param_value
#define	ips_ip_defend_interval		ips_param_arr[52].ip_param_value
#define	ips_ip_dup_recovery		ips_param_arr[53].ip_param_value
#define	ips_ip_restrict_interzone_loopback ips_param_arr[54].ip_param_value
#define	ips_ip_lso_outbound		ips_param_arr[55].ip_param_value
#define	ips_igmp_max_version		ips_param_arr[56].ip_param_value
#define	ips_mld_max_version		ips_param_arr[57].ip_param_value
#define	ips_ip_pmtu_min			ips_param_arr[58].ip_param_value
#define	ips_ipv6_drop_inbound_icmpv6	ips_param_arr[59].ip_param_value

extern int	dohwcksum;	/* use h/w cksum if supported by the h/w */
#ifdef ZC_TEST
extern int	noswcksum;
#endif

extern char	ipif_loopback_name[];

extern nv_t	*ire_nv_tbl;

extern struct module_info ip_mod_info;

#define	HOOKS4_INTERESTED_PHYSICAL_IN(ipst)	\
	((ipst)->ips_ip4_physical_in_event.he_interested)
#define	HOOKS6_INTERESTED_PHYSICAL_IN(ipst)	\
	((ipst)->ips_ip6_physical_in_event.he_interested)
#define	HOOKS4_INTERESTED_PHYSICAL_OUT(ipst)	\
	((ipst)->ips_ip4_physical_out_event.he_interested)
#define	HOOKS6_INTERESTED_PHYSICAL_OUT(ipst)	\
	((ipst)->ips_ip6_physical_out_event.he_interested)
#define	HOOKS4_INTERESTED_FORWARDING(ipst)	\
	((ipst)->ips_ip4_forwarding_event.he_interested)
#define	HOOKS6_INTERESTED_FORWARDING(ipst)	\
	((ipst)->ips_ip6_forwarding_event.he_interested)
#define	HOOKS4_INTERESTED_LOOPBACK_IN(ipst)	\
	((ipst)->ips_ip4_loopback_in_event.he_interested)
#define	HOOKS6_INTERESTED_LOOPBACK_IN(ipst)	\
	((ipst)->ips_ip6_loopback_in_event.he_interested)
#define	HOOKS4_INTERESTED_LOOPBACK_OUT(ipst)	\
	((ipst)->ips_ip4_loopback_out_event.he_interested)
#define	HOOKS6_INTERESTED_LOOPBACK_OUT(ipst)	\
	((ipst)->ips_ip6_loopback_out_event.he_interested)

/*
 * Hooks macros used inside of ip
 */
#define	FW_HOOKS(_hook, _event, _ilp, _olp, _iph, _fm, _m, _llm, ipst)	\
									\
	if ((_hook).he_interested) {	\
		hook_pkt_event_t info;					\
									\
		_NOTE(CONSTCOND)					\
		ASSERT((_ilp != NULL) || (_olp != NULL));		\
									\
		FW_SET_ILL_INDEX(info.hpe_ifp, (ill_t *)_ilp);		\
		FW_SET_ILL_INDEX(info.hpe_ofp, (ill_t *)_olp);		\
		info.hpe_protocol = ipst->ips_ipv4_net_data;		\
		info.hpe_hdr = _iph;					\
		info.hpe_mp = &(_fm);					\
		info.hpe_mb = _m;					\
		info.hpe_flags = _llm;					\
		if (hook_run(ipst->ips_ipv4_net_data->netd_hooks,	\
		    _event, (hook_data_t)&info) != 0) {			\
			ip2dbg(("%s hook dropped mblk chain %p hdr %p\n",\
			    (_hook).he_name, (void *)_fm, (void *)_m));	\
			freemsg(_fm);					\
			_fm = NULL;					\
			_iph = NULL;					\
			_m = NULL;					\
		} else {						\
			_iph = info.hpe_hdr;				\
			_m = info.hpe_mb;				\
		}							\
	}

#define	FW_HOOKS6(_hook, _event, _ilp, _olp, _iph, _fm, _m, _llm, ipst)	\
									\
	if ((_hook).he_interested) {	\
		hook_pkt_event_t info;					\
									\
		_NOTE(CONSTCOND)					\
		ASSERT((_ilp != NULL) || (_olp != NULL));		\
									\
		FW_SET_ILL_INDEX(info.hpe_ifp, (ill_t *)_ilp);		\
		FW_SET_ILL_INDEX(info.hpe_ofp, (ill_t *)_olp);		\
		info.hpe_protocol = ipst->ips_ipv6_net_data;		\
		info.hpe_hdr = _iph;					\
		info.hpe_mp = &(_fm);					\
		info.hpe_mb = _m;					\
		info.hpe_flags = _llm;					\
		if (hook_run(ipst->ips_ipv6_net_data->netd_hooks,	\
		    _event, (hook_data_t)&info) != 0) {			\
			ip2dbg(("%s hook dropped mblk chain %p hdr %p\n",\
			    (_hook).he_name, (void *)_fm, (void *)_m));	\
			freemsg(_fm);					\
			_fm = NULL;					\
			_iph = NULL;					\
			_m = NULL;					\
		} else {						\
			_iph = info.hpe_hdr;				\
			_m = info.hpe_mb;				\
		}							\
	}

#define	FW_SET_ILL_INDEX(fp, ill)					\
	_NOTE(CONSTCOND)						\
	if ((ill) == NULL || (ill)->ill_phyint == NULL) {		\
		(fp) = 0;						\
		_NOTE(CONSTCOND)					\
	} else if (IS_UNDER_IPMP(ill)) {				\
		(fp) = ipmp_ill_get_ipmp_ifindex(ill);			\
	} else {							\
		(fp) = (ill)->ill_phyint->phyint_ifindex;		\
	}

/*
 * Network byte order macros
 */
#ifdef	_BIG_ENDIAN
#define	N_IN_CLASSA_NET		IN_CLASSA_NET
#define	N_IN_CLASSD_NET		IN_CLASSD_NET
#define	N_INADDR_UNSPEC_GROUP	INADDR_UNSPEC_GROUP
#define	N_IN_LOOPBACK_NET	(ipaddr_t)0x7f000000U
#else /* _BIG_ENDIAN */
#define	N_IN_CLASSA_NET		(ipaddr_t)0x000000ffU
#define	N_IN_CLASSD_NET		(ipaddr_t)0x000000f0U
#define	N_INADDR_UNSPEC_GROUP	(ipaddr_t)0x000000e0U
#define	N_IN_LOOPBACK_NET	(ipaddr_t)0x0000007fU
#endif /* _BIG_ENDIAN */
#define	CLASSD(addr)	(((addr) & N_IN_CLASSD_NET) == N_INADDR_UNSPEC_GROUP)
#define	CLASSE(addr)	(((addr) & N_IN_CLASSD_NET) == N_IN_CLASSD_NET)
#define	IP_LOOPBACK_ADDR(addr)			\
	(((addr) & N_IN_CLASSA_NET == N_IN_LOOPBACK_NET))

#ifdef DEBUG
/* IPsec HW acceleration debugging support */

#define	IPSECHW_CAPAB		0x0001	/* capability negotiation */
#define	IPSECHW_SADB		0x0002	/* SADB exchange */
#define	IPSECHW_PKT		0x0004	/* general packet flow */
#define	IPSECHW_PKTIN		0x0008	/* driver in pkt processing details */
#define	IPSECHW_PKTOUT		0x0010	/* driver out pkt processing details */

#define	IPSECHW_DEBUG(f, x)	if (ipsechw_debug & (f)) { (void) printf x; }
#define	IPSECHW_CALL(f, r, x)	if (ipsechw_debug & (f)) { (void) r x; }

extern uint32_t ipsechw_debug;
#else
#define	IPSECHW_DEBUG(f, x)	{}
#define	IPSECHW_CALL(f, r, x)	{}
#endif

extern int	ip_debug;
extern uint_t	ip_thread_data;
extern krwlock_t ip_thread_rwlock;
extern list_t	ip_thread_list;

#ifdef IP_DEBUG
#include <sys/debug.h>
#include <sys/promif.h>

#define	ip0dbg(a)	printf a
#define	ip1dbg(a)	if (ip_debug > 2) printf a
#define	ip2dbg(a)	if (ip_debug > 3) printf a
#define	ip3dbg(a)	if (ip_debug > 4) printf a
#else
#define	ip0dbg(a)	/* */
#define	ip1dbg(a)	/* */
#define	ip2dbg(a)	/* */
#define	ip3dbg(a)	/* */
#endif	/* IP_DEBUG */

/* Default MAC-layer address string length for mac_colon_addr */
#define	MAC_STR_LEN	128

struct	ipsec_out_s;

struct	mac_header_info_s;

extern void	ill_frag_timer(void *);
extern ill_t	*ill_first(int, int, ill_walk_context_t *, ip_stack_t *);
extern ill_t	*ill_next(ill_walk_context_t *, ill_t *);
extern void	ill_frag_timer_start(ill_t *);
extern void	ill_nic_event_dispatch(ill_t *, lif_if_t, nic_event_t,
    nic_event_data_t, size_t);
extern mblk_t	*ip_carve_mp(mblk_t **, ssize_t);
extern mblk_t	*ip_dlpi_alloc(size_t, t_uscalar_t);
extern mblk_t	*ip_dlnotify_alloc(uint_t, uint_t);
extern char	*ip_dot_addr(ipaddr_t, char *);
extern const char *mac_colon_addr(const uint8_t *, size_t, char *, size_t);
extern void	ip_lwput(queue_t *, mblk_t *);
extern boolean_t icmp_err_rate_limit(ip_stack_t *);
extern void	icmp_time_exceeded(queue_t *, mblk_t *, uint8_t, zoneid_t,
    ip_stack_t *);
extern void	icmp_unreachable(queue_t *, mblk_t *, uint8_t, zoneid_t,
    ip_stack_t *);
extern mblk_t	*ip_add_info(mblk_t *, ill_t *, uint_t, zoneid_t, ip_stack_t *);
cred_t		*ip_best_cred(mblk_t *, conn_t *, pid_t *);
extern mblk_t	*ip_bind_v4(queue_t *, mblk_t *, conn_t *);
extern	boolean_t ip_bind_ipsec_policy_set(conn_t *, mblk_t *);
extern	int	ip_bind_laddr_v4(conn_t *, mblk_t **, uint8_t, ipaddr_t,
    uint16_t, boolean_t);
extern	int	ip_proto_bind_laddr_v4(conn_t *, mblk_t **, uint8_t, ipaddr_t,
    uint16_t, boolean_t);
extern	int	ip_proto_bind_connected_v4(conn_t *, mblk_t **,
    uint8_t, ipaddr_t *, uint16_t, ipaddr_t, uint16_t, boolean_t, boolean_t,
    cred_t *);
extern	int	ip_bind_connected_v4(conn_t *, mblk_t **, uint8_t, ipaddr_t *,
    uint16_t, ipaddr_t, uint16_t, boolean_t, boolean_t, cred_t *);
extern uint_t	ip_cksum(mblk_t *, int, uint32_t);
extern int	ip_close(queue_t *, int);
extern uint16_t	ip_csum_hdr(ipha_t *);
extern void	ip_proto_not_sup(queue_t *, mblk_t *, uint_t, zoneid_t,
    ip_stack_t *);
extern void	ip_ire_g_fini(void);
extern void	ip_ire_g_init(void);
extern void	ip_ire_fini(ip_stack_t *);
extern void	ip_ire_init(ip_stack_t *);
extern int	ip_openv4(queue_t *q, dev_t *devp, int flag, int sflag,
		    cred_t *credp);
extern int	ip_openv6(queue_t *q, dev_t *devp, int flag, int sflag,
		    cred_t *credp);
extern int	ip_reassemble(mblk_t *, ipf_t *, uint_t, boolean_t, ill_t *,
    size_t);
extern int	ip_opt_set_ill(conn_t *, int, boolean_t, boolean_t,
    int, int, mblk_t *);
extern void	ip_rput(queue_t *, mblk_t *);
extern void	ip_input(ill_t *, ill_rx_ring_t *, mblk_t *,
    struct mac_header_info_s *);
extern mblk_t	*ip_accept_tcp(ill_t *, ill_rx_ring_t *, squeue_t *,
    mblk_t *, mblk_t **, uint_t *cnt);
extern void	ip_rput_dlpi(queue_t *, mblk_t *);
extern void	ip_rput_forward(ire_t *, ipha_t *, mblk_t *, ill_t *);
extern void	ip_rput_forward_multicast(ipaddr_t, mblk_t *, ipif_t *);

extern void	ip_mib2_add_ip_stats(mib2_ipIfStatsEntry_t *,
		    mib2_ipIfStatsEntry_t *);
extern void	ip_mib2_add_icmp6_stats(mib2_ipv6IfIcmpEntry_t *,
		    mib2_ipv6IfIcmpEntry_t *);
extern void	ip_udp_input(queue_t *, mblk_t *, ipha_t *, ire_t *, ill_t *);
extern void	ip_proto_input(queue_t *, mblk_t *, ipha_t *, ire_t *, ill_t *,
    uint32_t);
extern void	ip_rput_other(ipsq_t *, queue_t *, mblk_t *, void *);
extern ire_t	*ip_check_multihome(void *, ire_t *, ill_t *);
extern void	ip_setpktversion(conn_t *, boolean_t, boolean_t, ip_stack_t *);
extern void	ip_trash_ire_reclaim(void *);
extern void	ip_trash_timer_expire(void *);
extern void	ip_wput(queue_t *, mblk_t *);
extern void	ip_output(void *, mblk_t *, void *, int);
extern void	ip_output_options(void *, mblk_t *, void *, int,
    ip_opt_info_t *);

extern void	ip_wput_ire(queue_t *, mblk_t *, ire_t *, conn_t *, int,
		    zoneid_t);
extern void	ip_wput_local(queue_t *, ill_t *, ipha_t *, mblk_t *, ire_t *,
		    int, zoneid_t);
extern void	ip_wput_multicast(queue_t *, mblk_t *, ipif_t *, zoneid_t);
extern void	ip_wput_nondata(ipsq_t *, queue_t *, mblk_t *, void *);
extern void	ip_wsrv(queue_t *);
extern char	*ip_nv_lookup(nv_t *, int);
extern boolean_t ip_local_addr_ok_v6(const in6_addr_t *, const in6_addr_t *);
extern boolean_t ip_remote_addr_ok_v6(const in6_addr_t *, const in6_addr_t *);
extern ipaddr_t ip_massage_options(ipha_t *, netstack_t *);
extern ipaddr_t ip_net_mask(ipaddr_t);
extern void	ip_newroute(queue_t *, mblk_t *, ipaddr_t, conn_t *, zoneid_t,
		    ip_stack_t *);
extern ipxmit_state_t	ip_xmit_v4(mblk_t *, ire_t *, struct ipsec_out_s *,
    boolean_t, conn_t *);
extern int	ip_hdr_complete(ipha_t *, zoneid_t, ip_stack_t *);

extern struct qinit iprinitv6;
extern struct qinit ipwinitv6;

extern void	ipmp_init(ip_stack_t *);
extern void	ipmp_destroy(ip_stack_t *);
extern ipmp_grp_t *ipmp_grp_create(const char *, phyint_t *);
extern void	ipmp_grp_destroy(ipmp_grp_t *);
extern void	ipmp_grp_info(const ipmp_grp_t *, lifgroupinfo_t *);
extern int	ipmp_grp_rename(ipmp_grp_t *, const char *);
extern ipmp_grp_t *ipmp_grp_lookup(const char *, ip_stack_t *);
extern int	ipmp_grp_vet_phyint(ipmp_grp_t *, phyint_t *);
extern ipmp_illgrp_t *ipmp_illgrp_create(ill_t *);
extern void	ipmp_illgrp_destroy(ipmp_illgrp_t *);
extern ill_t	*ipmp_illgrp_add_ipif(ipmp_illgrp_t *, ipif_t *);
extern void	ipmp_illgrp_del_ipif(ipmp_illgrp_t *, ipif_t *);
extern ill_t	*ipmp_illgrp_next_ill(ipmp_illgrp_t *);
extern ill_t	*ipmp_illgrp_hold_next_ill(ipmp_illgrp_t *);
extern ill_t	*ipmp_illgrp_cast_ill(ipmp_illgrp_t *);
extern ill_t	*ipmp_illgrp_hold_cast_ill(ipmp_illgrp_t *);
extern ill_t	*ipmp_illgrp_ipmp_ill(ipmp_illgrp_t *);
extern void	ipmp_illgrp_refresh_mtu(ipmp_illgrp_t *);
extern ipmp_arpent_t *ipmp_illgrp_create_arpent(ipmp_illgrp_t *, mblk_t *,
    boolean_t);
extern void	ipmp_illgrp_destroy_arpent(ipmp_illgrp_t *, ipmp_arpent_t *);
extern ipmp_arpent_t *ipmp_illgrp_lookup_arpent(ipmp_illgrp_t *, ipaddr_t *);
extern void	ipmp_illgrp_refresh_arpent(ipmp_illgrp_t *);
extern void	ipmp_illgrp_mark_arpent(ipmp_illgrp_t *, ipmp_arpent_t *);
extern ill_t	*ipmp_illgrp_find_ill(ipmp_illgrp_t *, uchar_t *, uint_t);
extern void	ipmp_illgrp_link_grp(ipmp_illgrp_t *, ipmp_grp_t *);
extern int	ipmp_illgrp_unlink_grp(ipmp_illgrp_t *);
extern uint_t	ipmp_ill_get_ipmp_ifindex(const ill_t *);
extern void	ipmp_ill_join_illgrp(ill_t *, ipmp_illgrp_t *);
extern void	ipmp_ill_leave_illgrp(ill_t *);
extern ill_t	*ipmp_ill_hold_ipmp_ill(ill_t *);
extern boolean_t ipmp_ill_is_active(ill_t *);
extern void	ipmp_ill_refresh_active(ill_t *);
extern void	ipmp_phyint_join_grp(phyint_t *, ipmp_grp_t *);
extern void	ipmp_phyint_leave_grp(phyint_t *);
extern void	ipmp_phyint_refresh_active(phyint_t *);
extern ill_t	*ipmp_ipif_bound_ill(const ipif_t *);
extern ill_t	*ipmp_ipif_hold_bound_ill(const ipif_t *);
extern boolean_t ipmp_ipif_is_dataaddr(const ipif_t *);
extern boolean_t ipmp_ipif_is_stubaddr(const ipif_t *);

extern void	conn_drain_insert(conn_t *, idl_tx_list_t *);
extern int	conn_ipsec_length(conn_t *);
extern void	ip_wput_ipsec_out(queue_t *, mblk_t *, ipha_t *, ill_t *,
    ire_t *);
extern ipaddr_t	ip_get_dst(ipha_t *);
extern int	ipsec_out_extra_length(mblk_t *);
extern int	ipsec_in_extra_length(mblk_t *);
extern mblk_t	*ipsec_in_alloc(boolean_t, netstack_t *);
extern boolean_t ipsec_in_is_secure(mblk_t *);
extern void	ipsec_out_process(queue_t *, mblk_t *, ire_t *, uint_t);
extern void	ipsec_out_to_in(mblk_t *);
extern void	ip_fanout_proto_again(mblk_t *, ill_t *, ill_t *, ire_t *);

extern void	ire_cleanup(ire_t *);
extern void	ire_inactive(ire_t *);
extern boolean_t irb_inactive(irb_t *);
extern ire_t	*ire_unlink(irb_t *);

#ifdef DEBUG
extern	boolean_t th_trace_ref(const void *, ip_stack_t *);
extern	void	th_trace_unref(const void *);
extern	void	th_trace_cleanup(const void *, boolean_t);
extern	void	ire_trace_ref(ire_t *);
extern	void	ire_untrace_ref(ire_t *);
#endif

extern int	ip_srcid_insert(const in6_addr_t *, zoneid_t, ip_stack_t *);
extern int	ip_srcid_remove(const in6_addr_t *, zoneid_t, ip_stack_t *);
extern void	ip_srcid_find_id(uint_t, in6_addr_t *, zoneid_t, netstack_t *);
extern uint_t	ip_srcid_find_addr(const in6_addr_t *, zoneid_t, netstack_t *);

extern uint8_t	ipoptp_next(ipoptp_t *);
extern uint8_t	ipoptp_first(ipoptp_t *, ipha_t *);
extern int	ip_opt_get_user(const ipha_t *, uchar_t *);
extern int	ipsec_req_from_conn(conn_t *, ipsec_req_t *, int);
extern int	ip_snmp_get(queue_t *q, mblk_t *mctl, int level);
extern int	ip_snmp_set(queue_t *q, int, int, uchar_t *, int);
extern void	ip_process_ioctl(ipsq_t *, queue_t *, mblk_t *, void *);
extern void	ip_quiesce_conn(conn_t *);
extern  void    ip_reprocess_ioctl(ipsq_t *, queue_t *, mblk_t *, void *);
extern void	ip_restart_optmgmt(ipsq_t *, queue_t *, mblk_t *, void *);
extern void	ip_ioctl_finish(queue_t *, mblk_t *, int, int, ipsq_t *);

extern boolean_t ip_cmpbuf(const void *, uint_t, boolean_t, const void *,
    uint_t);
extern boolean_t ip_allocbuf(void **, uint_t *, boolean_t, const void *,
    uint_t);
extern void	ip_savebuf(void **, uint_t *, boolean_t, const void *, uint_t);

extern boolean_t	ipsq_pending_mp_cleanup(ill_t *, conn_t *);
extern void	conn_ioctl_cleanup(conn_t *);
extern ill_t	*conn_get_held_ill(conn_t *, ill_t **, int *);

struct tcp_stack;
extern void ip_xmit_reset_serialize(mblk_t *, int, zoneid_t, struct tcp_stack *,
    conn_t *);

struct multidata_s;
struct pdesc_s;

extern mblk_t	*ip_mdinfo_alloc(ill_mdt_capab_t *);
extern mblk_t	*ip_mdinfo_return(ire_t *, conn_t *, char *, ill_mdt_capab_t *);
extern mblk_t	*ip_lsoinfo_alloc(ill_lso_capab_t *);
extern mblk_t	*ip_lsoinfo_return(ire_t *, conn_t *, char *,
    ill_lso_capab_t *);
extern uint_t	ip_md_cksum(struct pdesc_s *, int, uint_t);
extern boolean_t ip_md_addr_attr(struct multidata_s *, struct pdesc_s *,
			const mblk_t *);
extern boolean_t ip_md_hcksum_attr(struct multidata_s *, struct pdesc_s *,
			uint32_t, uint32_t, uint32_t, uint32_t);
extern boolean_t ip_md_zcopy_attr(struct multidata_s *, struct pdesc_s *,
			uint_t);
extern	void	ip_unbind(conn_t *connp);

extern void tnet_init(void);
extern void tnet_fini(void);

/* Hooks for CGTP (multirt routes) filtering module */
#define	CGTP_FILTER_REV_1	1
#define	CGTP_FILTER_REV_2	2
#define	CGTP_FILTER_REV_3	3
#define	CGTP_FILTER_REV		CGTP_FILTER_REV_3

/* cfo_filter and cfo_filter_v6 hooks return values */
#define	CGTP_IP_PKT_NOT_CGTP	0
#define	CGTP_IP_PKT_PREMIUM	1
#define	CGTP_IP_PKT_DUPLICATE	2

/* Version 3 of the filter interface */
typedef struct cgtp_filter_ops {
	int	cfo_filter_rev;			/* CGTP_FILTER_REV_3 */
	int	(*cfo_change_state)(netstackid_t, int);
	int	(*cfo_add_dest_v4)(netstackid_t, ipaddr_t, ipaddr_t,
		    ipaddr_t, ipaddr_t);
	int	(*cfo_del_dest_v4)(netstackid_t, ipaddr_t, ipaddr_t);
	int	(*cfo_add_dest_v6)(netstackid_t, in6_addr_t *, in6_addr_t *,
		    in6_addr_t *, in6_addr_t *);
	int	(*cfo_del_dest_v6)(netstackid_t, in6_addr_t *, in6_addr_t *);
	int	(*cfo_filter)(netstackid_t, uint_t, mblk_t *);
	int	(*cfo_filter_v6)(netstackid_t, uint_t, ip6_t *,
		    ip6_frag_t *);
} cgtp_filter_ops_t;

#define	CGTP_MCAST_SUCCESS	1

/*
 * The separate CGTP module needs this global symbol so that it
 * can check the version and determine whether to use the old or the new
 * version of the filtering interface.
 */
extern int	ip_cgtp_filter_rev;

extern int	ip_cgtp_filter_supported(void);
extern int	ip_cgtp_filter_register(netstackid_t, cgtp_filter_ops_t *);
extern int	ip_cgtp_filter_unregister(netstackid_t);
extern int	ip_cgtp_filter_is_registered(netstackid_t);

/* Flags for ire_multirt_lookup() */

#define	MULTIRT_USESTAMP	0x0001
#define	MULTIRT_SETSTAMP	0x0002
#define	MULTIRT_CACHEGW		0x0004

/* Debug stuff for multirt route resolution. */
#if defined(DEBUG) && !defined(__lint)
/* Our "don't send, rather drop" flag. */
#define	MULTIRT_DEBUG_FLAG	0x8000

#define	MULTIRT_TRACE(x)	ip2dbg(x)

#define	MULTIRT_DEBUG_TAG(mblk)	\
	do { \
		ASSERT(mblk != NULL); \
		MULTIRT_TRACE(("%s[%d]: tagging mblk %p, tag was %d\n", \
		__FILE__, __LINE__, \
		(void *)(mblk), (mblk)->b_flag & MULTIRT_DEBUG_FLAG)); \
		(mblk)->b_flag |= MULTIRT_DEBUG_FLAG; \
	} while (0)

#define	MULTIRT_DEBUG_UNTAG(mblk) \
	do { \
		ASSERT(mblk != NULL); \
		MULTIRT_TRACE(("%s[%d]: untagging mblk %p, tag was %d\n", \
		__FILE__, __LINE__, \
		(void *)(mblk), (mblk)->b_flag & MULTIRT_DEBUG_FLAG)); \
		(mblk)->b_flag &= ~MULTIRT_DEBUG_FLAG; \
	} while (0)

#define	MULTIRT_DEBUG_TAGGED(mblk) \
	(((mblk)->b_flag & MULTIRT_DEBUG_FLAG) ? B_TRUE : B_FALSE)
#else
#define	MULTIRT_DEBUG_TAG(mblk)		ASSERT(mblk != NULL)
#define	MULTIRT_DEBUG_UNTAG(mblk)	ASSERT(mblk != NULL)
#define	MULTIRT_DEBUG_TAGGED(mblk)	B_FALSE
#endif

/*
 * IP observability hook support
 */

/*
 * ipobs_hooktype_t describes the hook types supported
 * by the ip module. IPOBS_HOOK_LOCAL refers to packets
 * which are looped back internally within the ip module.
 */

typedef enum ipobs_hook_type {
	IPOBS_HOOK_LOCAL,
	IPOBS_HOOK_OUTBOUND,
	IPOBS_HOOK_INBOUND
} ipobs_hook_type_t;

typedef void ipobs_cbfunc_t(mblk_t *);

typedef struct ipobs_cb {
	ipobs_cbfunc_t	*ipobs_cbfunc;
	list_node_t	ipobs_cbnext;
} ipobs_cb_t;

/*
 * This structure holds the data passed back from the ip module to
 * observability consumers.
 *
 * ihd_mp	  Pointer to the IP packet.
 * ihd_zsrc	  Source zoneid; set to ALL_ZONES when unknown.
 * ihd_zdst	  Destination zoneid; set to ALL_ZONES when unknown.
 * ihd_htype	  IPobs hook type, see above for the defined types.
 * ihd_ipver	  IP version of the packet.
 * ihd_ifindex	  Interface index that the packet was received/sent over.
 *		  For local packets, this is the index of the interface
 *		  associated with the local destination address.
 * ihd_grifindex  IPMP group interface index (zero unless ihd_ifindex
 *		  is an IPMP underlying interface).
 * ihd_stack	  Netstack the packet is from.
 */
typedef struct ipobs_hook_data {
	mblk_t			*ihd_mp;
	zoneid_t		ihd_zsrc;
	zoneid_t		ihd_zdst;
	ipobs_hook_type_t	ihd_htype;
	uint16_t		ihd_ipver;
	uint64_t		ihd_ifindex;
	uint64_t 		ihd_grifindex;
	netstack_t		*ihd_stack;
} ipobs_hook_data_t;

/*
 * Per-ILL Multidata Transmit capabilities.
 */
struct ill_mdt_capab_s {
	uint_t ill_mdt_version;  /* interface version */
	uint_t ill_mdt_on;	 /* on/off switch for MDT on this ILL */
	uint_t ill_mdt_hdr_head; /* leading header fragment extra space */
	uint_t ill_mdt_hdr_tail; /* trailing header fragment extra space */
	uint_t ill_mdt_max_pld;	 /* maximum payload buffers per Multidata */
	uint_t ill_mdt_span_limit; /* maximum payload span per packet */
};

struct ill_hcksum_capab_s {
	uint_t	ill_hcksum_version;	/* interface version */
	uint_t	ill_hcksum_txflags;	/* capabilities on transmit */
};

struct ill_zerocopy_capab_s {
	uint_t	ill_zerocopy_version;	/* interface version */
	uint_t	ill_zerocopy_flags;	/* capabilities */
};

struct ill_lso_capab_s {
	uint_t	ill_lso_on;		/* on/off switch for LSO on this ILL */
	uint_t	ill_lso_flags;		/* capabilities */
	uint_t	ill_lso_max;		/* maximum size of payload */
};

/*
 * rr_ring_state cycles in the order shown below from RR_FREE through
 * RR_FREE_IN_PROG and  back to RR_FREE.
 */
typedef enum {
	RR_FREE,			/* Free slot */
	RR_SQUEUE_UNBOUND,		/* Ring's squeue is unbound */
	RR_SQUEUE_BIND_INPROG,		/* Ring's squeue bind in progress */
	RR_SQUEUE_BOUND,		/* Ring's squeue bound to cpu */
	RR_FREE_INPROG			/* Ring is being freed */
} ip_ring_state_t;

#define	ILL_MAX_RINGS		256	/* Max num of rx rings we can manage */
#define	ILL_POLLING		0x01	/* Polling in use */

/*
 * These functions pointer types are exported by the mac/dls layer.
 * we need to duplicate the definitions here because we cannot
 * include mac/dls header files here.
 */
typedef void			(*ip_mac_intr_disable_t)(void *);
typedef void			(*ip_mac_intr_enable_t)(void *);
typedef ip_mac_tx_cookie_t	(*ip_dld_tx_t)(void *, mblk_t *,
    uint64_t, uint16_t);
typedef	void			(*ip_flow_enable_t)(void *, ip_mac_tx_cookie_t);
typedef void			*(*ip_dld_callb_t)(void *,
    ip_flow_enable_t, void *);
typedef boolean_t		(*ip_dld_fctl_t)(void *, ip_mac_tx_cookie_t);
typedef int			(*ip_capab_func_t)(void *, uint_t,
    void *, uint_t);

/*
 * POLLING README
 * sq_get_pkts() is called to pick packets from softring in poll mode. It
 * calls rr_rx to get the chain and process it with rr_ip_accept.
 * rr_rx = mac_soft_ring_poll() to pick packets
 * rr_ip_accept = ip_accept_tcp() to process packets
 */

/*
 * XXX: With protocol, service specific squeues, they will have
 * specific acceptor functions.
 */
typedef	mblk_t *(*ip_mac_rx_t)(void *, size_t);
typedef mblk_t *(*ip_accept_t)(ill_t *, ill_rx_ring_t *,
    squeue_t *, mblk_t *, mblk_t **, uint_t *);

/*
 * rr_intr_enable, rr_intr_disable, rr_rx_handle, rr_rx:
 * May be accessed while in the squeue AND after checking that SQS_POLL_CAPAB
 * is set.
 *
 * rr_ring_state: Protected by ill_lock.
 */
struct ill_rx_ring {
	ip_mac_intr_disable_t	rr_intr_disable; /* Interrupt disabling func */
	ip_mac_intr_enable_t	rr_intr_enable;	/* Interrupt enabling func */
	void			*rr_intr_handle; /* Handle interrupt funcs */
	ip_mac_rx_t		rr_rx;		/* Driver receive function */
	ip_accept_t		rr_ip_accept;	/* IP accept function */
	void			*rr_rx_handle;	/* Handle for Rx ring */
	squeue_t		*rr_sqp; /* Squeue the ring is bound to */
	ill_t			*rr_ill;	/* back pointer to ill */
	ip_ring_state_t		rr_ring_state;	/* State of this ring */
};

/*
 * IP - DLD direct function call capability
 * Suffixes, df - dld function, dh - dld handle,
 * cf - client (IP) function, ch - client handle
 */
typedef struct ill_dld_direct_s {		/* DLD provided driver Tx */
	ip_dld_tx_t		idd_tx_df;	/* str_mdata_fastpath_put */
	void			*idd_tx_dh;	/* dld_str_t *dsp */
	ip_dld_callb_t		idd_tx_cb_df;	/* mac_tx_srs_notify */
	void			*idd_tx_cb_dh;	/* mac_client_handle_t *mch */
	ip_dld_fctl_t		idd_tx_fctl_df;	/* mac_tx_is_flow_blocked */
	void			*idd_tx_fctl_dh;	/* mac_client_handle */
} ill_dld_direct_t;

/* IP - DLD polling capability */
typedef struct ill_dld_poll_s {
	ill_rx_ring_t		idp_ring_tbl[ILL_MAX_RINGS];
} ill_dld_poll_t;

/* Describes ill->ill_dld_capab */
struct ill_dld_capab_s {
	ip_capab_func_t		idc_capab_df;	/* dld_capab_func */
	void			*idc_capab_dh;	/* dld_str_t *dsp */
	ill_dld_direct_t	idc_direct;
	ill_dld_poll_t		idc_poll;
};

/*
 * IP squeues exports
 */
extern boolean_t 	ip_squeue_fanout;

#define	IP_SQUEUE_GET(hint) ip_squeue_random(hint)

extern void ip_squeue_init(void (*)(squeue_t *));
extern squeue_t	*ip_squeue_random(uint_t);
extern squeue_t *ip_squeue_get(ill_rx_ring_t *);
extern squeue_t *ip_squeue_getfree(pri_t);
extern int ip_squeue_cpu_move(squeue_t *, processorid_t);
extern void *ip_squeue_add_ring(ill_t *, void *);
extern void ip_squeue_bind_ring(ill_t *, ill_rx_ring_t *, processorid_t);
extern void ip_squeue_clean_ring(ill_t *, ill_rx_ring_t *);
extern void ip_squeue_quiesce_ring(ill_t *, ill_rx_ring_t *);
extern void ip_squeue_restart_ring(ill_t *, ill_rx_ring_t *);
extern void ip_squeue_clean_all(ill_t *);

extern void tcp_wput(queue_t *, mblk_t *);

extern int	ip_fill_mtuinfo(struct in6_addr *, in_port_t,
	struct ip6_mtuinfo *, netstack_t *);
extern	ipif_t *conn_get_held_ipif(conn_t *, ipif_t **, int *);
extern void ipobs_register_hook(netstack_t *, ipobs_cbfunc_t *);
extern void ipobs_unregister_hook(netstack_t *, ipobs_cbfunc_t *);
extern void ipobs_hook(mblk_t *, int, zoneid_t, zoneid_t, const ill_t *, int,
    uint32_t, ip_stack_t *);
typedef void    (*ipsq_func_t)(ipsq_t *, queue_t *, mblk_t *, void *);

/*
 * Squeue tags. Tags only need to be unique when the callback function is the
 * same to distinguish between different calls, but we use unique tags for
 * convenience anyway.
 */
#define	SQTAG_IP_INPUT			1
#define	SQTAG_TCP_INPUT_ICMP_ERR	2
#define	SQTAG_TCP6_INPUT_ICMP_ERR	3
#define	SQTAG_IP_TCP_INPUT		4
#define	SQTAG_IP6_TCP_INPUT		5
#define	SQTAG_IP_TCP_CLOSE		6
#define	SQTAG_TCP_OUTPUT		7
#define	SQTAG_TCP_TIMER			8
#define	SQTAG_TCP_TIMEWAIT		9
#define	SQTAG_TCP_ACCEPT_FINISH		10
#define	SQTAG_TCP_ACCEPT_FINISH_Q0	11
#define	SQTAG_TCP_ACCEPT_PENDING	12
#define	SQTAG_TCP_LISTEN_DISCON		13
#define	SQTAG_TCP_CONN_REQ_1		14
#define	SQTAG_TCP_EAGER_BLOWOFF		15
#define	SQTAG_TCP_EAGER_CLEANUP		16
#define	SQTAG_TCP_EAGER_CLEANUP_Q0	17
#define	SQTAG_TCP_CONN_IND		18
#define	SQTAG_TCP_RSRV			19
#define	SQTAG_TCP_ABORT_BUCKET		20
#define	SQTAG_TCP_REINPUT		21
#define	SQTAG_TCP_REINPUT_EAGER		22
#define	SQTAG_TCP_INPUT_MCTL		23
#define	SQTAG_TCP_RPUTOTHER		24
#define	SQTAG_IP_PROTO_AGAIN		25
#define	SQTAG_IP_FANOUT_TCP		26
#define	SQTAG_IPSQ_CLEAN_RING		27
#define	SQTAG_TCP_WPUT_OTHER		28
#define	SQTAG_TCP_CONN_REQ_UNBOUND	29
#define	SQTAG_TCP_SEND_PENDING		30
#define	SQTAG_BIND_RETRY		31
#define	SQTAG_UDP_FANOUT		32
#define	SQTAG_UDP_INPUT			33
#define	SQTAG_UDP_WPUT			34
#define	SQTAG_UDP_OUTPUT		35
#define	SQTAG_TCP_KSSL_INPUT		36
#define	SQTAG_TCP_DROP_Q0		37
#define	SQTAG_TCP_CONN_REQ_2		38
#define	SQTAG_IP_INPUT_RX_RING		39
#define	SQTAG_SQUEUE_CHANGE		40
#define	SQTAG_CONNECT_FINISH		41
#define	SQTAG_SYNCHRONOUS_OP		42
#define	SQTAG_TCP_SHUTDOWN_OUTPUT	43
#define	SQTAG_XMIT_EARLY_RESET		44

#define	NOT_OVER_IP(ip_wq)	\
	(ip_wq->q_next != NULL ||	\
	    (ip_wq->q_qinfo->qi_minfo->mi_idname) == NULL ||	\
	    strcmp(ip_wq->q_qinfo->qi_minfo->mi_idname,	\
	    IP_MOD_NAME) != 0 ||	\
	    ip_wq->q_qinfo->qi_minfo->mi_idnum != IP_MOD_ID)

#define	PROTO_FLOW_CNTRLD(connp)	(connp->conn_flow_cntrld)
#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _INET_IP_H */
