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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _VNET_GEN_H
#define	_VNET_GEN_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef __cplusplus
extern "C" {
#endif

#define	VGEN_SUCCESS		(0)	/* successful return */
#define	VGEN_FAILURE		(-1)	/* unsuccessful return */

#define	VGEN_NUM_VER		1	/* max # of vgen versions */

#define	VGEN_LOCAL	1	/* local ldc end-point */
#define	VGEN_PEER	2	/* peer ldc end-point */

/* vgen_t flags */
#define	VGEN_STOPPED		0x0
#define	VGEN_STARTED		0x1

#define	KMEM_FREE(_p)		kmem_free((_p), sizeof (*(_p)))

#define	VGEN_INIT_MCTAB_SIZE	16	/* initial size of multicast table */

#define	READ_ENTER(x)	rw_enter(x, RW_READER)
#define	WRITE_ENTER(x)	rw_enter(x, RW_WRITER)
#define	RW_EXIT(x)	rw_exit(x)

/* channel flags */
#define	CHANNEL_ATTACHED	0x1
#define	CHANNEL_STARTED		0x2

/* transmit return values */
#define	VGEN_TX_SUCCESS		0	/* transmit success */
#define	VGEN_TX_FAILURE		1	/* transmit failure */
#define	VGEN_TX_NORESOURCES	2	/* out of tbufs/txds */

/* private descriptor flags */
#define	VGEN_PRIV_DESC_FREE	0x0	/* desc is available */
#define	VGEN_PRIV_DESC_BUSY	0x1	/* desc in use */

#define	LDC_TO_VNET(ldcp)  ((ldcp)->portp->vgenp->vnetp)
#define	LDC_TO_VGEN(ldcp)  ((ldcp)->portp->vgenp)

#define	VGEN_DBLK_SZ		2048	/* data buffer size */
#define	VGEN_LDC_UP_DELAY	100	/* usec delay between ldc_up retries */

/* get the address of next tbuf */
#define	NEXTTBUF(ldcp, tbufp)	(((tbufp) + 1) == (ldcp)->tbufendp    \
		? (ldcp)->tbufp : ((tbufp) + 1))

/* increment recv index */
#define	INCR_RXI(i, ldcp)	\
		((i) = (((i) + 1) & ((ldcp)->num_rxds - 1)))

/* decrement recv index */
#define	DECR_RXI(i, ldcp)	\
		((i) = (((i) - 1) & ((ldcp)->num_rxds - 1)))

/* increment tx index */
#define	INCR_TXI(i, ldcp)	\
		((i) = (((i) + 1) & ((ldcp)->num_txds - 1)))

/* decrement tx index */
#define	DECR_TXI(i, ldcp)	\
		((i) = (((i) - 1) & ((ldcp)->num_txds - 1)))

/* bounds check rx index */
#define	CHECK_RXI(i, ldcp)	\
		(((i) >= 0) && ((i) < (ldcp)->num_rxds))

/* bounds check tx index */
#define	CHECK_TXI(i, ldcp)	\
		(((i) >= 0) && ((i) < (ldcp)->num_txds))

/* private descriptor */
typedef struct vgen_priv_desc {
	uint64_t		flags;		/* flag bits */
	vnet_public_desc_t	*descp;		/* associated public desc */
	ldc_mem_handle_t	memhandle;	/* mem handle for data */
	caddr_t			datap;		/* prealloc'd tx data buffer */
	uint64_t		datalen;	/* total actual datalen */
	uint64_t		ncookies;	/* num ldc_mem_cookies */
	ldc_mem_cookie_t	memcookie[MAX_COOKIES];	/* data cookies */
} vgen_private_desc_t;

/*
 * Handshake parameters (per vio_mailbox.h) of each ldc end point, used
 * during handshake negotiation.
 */
typedef struct vgen_handshake_params {
	/* version specific params */
	uint32_t	ver_major:16,
			ver_minor:16;		/* major, minor version */
	uint8_t		dev_class;		/* device class */

	/* attributes specific params */
	uint64_t		mtu;		/* max transfer unit size */
	uint64_t		addr;		/* address of the device */
	uint8_t			addr_type;	/* type of address */
	uint8_t			xfer_mode;	/* SHM or PKT */
	uint16_t		ack_freq;	/* dring data ack freq */

	/* descriptor ring params */
	uint32_t		num_desc;	/* # of descriptors in ring */
	uint32_t		desc_size;	/* size of descriptor */
	ldc_mem_cookie_t	dring_cookie;	/* desc ring cookie */
	uint32_t		num_dcookies;	/* # of dring cookies */
	uint64_t		dring_ident;	/* ident=0 for INFO msg */
	boolean_t		dring_ready;   /* dring ready flag */
} vgen_hparams_t;

/* version info */
typedef struct vgen_ver {
	uint32_t	ver_major:16,
			ver_minor:16;
} vgen_ver_t;

typedef struct vgen_stats {

	/* Link Input/Output stats */
	uint64_t	ipackets;	/* # rx packets */
	uint64_t	ierrors;	/* # rx error */
	uint64_t	opackets;	/* # tx packets */
	uint64_t	oerrors;	/* # tx error */

	/* MIB II variables */
	uint64_t	rbytes;		/* # bytes received */
	uint64_t	obytes;		/* # bytes transmitted */
	uint32_t	multircv;	/* # multicast packets received */
	uint32_t	multixmt;	/* # multicast packets for xmit */
	uint32_t	brdcstrcv;	/* # broadcast packets received */
	uint32_t	brdcstxmt;	/* # broadcast packets for xmit */
	uint32_t	norcvbuf;	/* # rcv packets discarded */
	uint32_t	noxmtbuf;	/* # xmit packets discarded */

	/* Tx Statistics */
	uint32_t	tx_no_desc;	/* # out of transmit descriptors */

	/* Rx Statistics */
	uint32_t	rx_allocb_fail;	/* # rx buf allocb() failures */
	uint32_t	rx_vio_allocb_fail; /* # vio_allocb() failures */
	uint32_t	rx_lost_pkts;	/* # rx lost packets */

	/* Callback statistics */
	uint32_t	callbacks;		/* # callbacks */
	uint32_t	dring_data_acks;	/* # dring data acks recvd  */
	uint32_t	dring_stopped_acks;	/* # dring stopped acks recvd */
	uint32_t	dring_data_msgs;	/* # dring data msgs sent */

} vgen_stats_t;

typedef struct vgen_kstats {
	/*
	 * Link Input/Output stats
	 */
	kstat_named_t	ipackets;
	kstat_named_t	ipackets64;
	kstat_named_t	ierrors;
	kstat_named_t	opackets;
	kstat_named_t	opackets64;
	kstat_named_t	oerrors;

	/*
	 * required by kstat for MIB II objects(RFC 1213)
	 */
	kstat_named_t	rbytes; 	/* MIB - ifInOctets */
	kstat_named_t	rbytes64;
	kstat_named_t	obytes; 	/* MIB - ifOutOctets */
	kstat_named_t	obytes64;
	kstat_named_t	multircv; 	/* MIB - ifInNUcastPkts */
	kstat_named_t	multixmt; 	/* MIB - ifOutNUcastPkts */
	kstat_named_t	brdcstrcv;	/* MIB - ifInNUcastPkts */
	kstat_named_t	brdcstxmt;	/* MIB - ifOutNUcastPkts */
	kstat_named_t	norcvbuf; 	/* MIB - ifInDiscards */
	kstat_named_t	noxmtbuf; 	/* MIB - ifOutDiscards */

	/* Tx Statistics */
	kstat_named_t	tx_no_desc;	/* # out of transmit descriptors */

	/* Rx Statistics */
	kstat_named_t	rx_allocb_fail;	/* # rx buf allocb failures */
	kstat_named_t	rx_vio_allocb_fail; /* # vio_allocb() failures */
	kstat_named_t	rx_lost_pkts;	/* # rx lost packets */

	/* Callback statistics */
	kstat_named_t	callbacks;		/* # callbacks */
	kstat_named_t	dring_data_acks;	/* # dring data acks recvd  */
	kstat_named_t	dring_stopped_acks;	/* # dring stopped acks recvd */
	kstat_named_t	dring_data_msgs;	/* # dring data msgs sent */

} vgen_kstats_t;

/* Channel information associated with a vgen-port */
typedef struct vgen_ldc {

	struct vgen_ldc		*nextp;		/* next ldc in the list */
	struct vgen_port	*portp;		/* associated port */

	/*
	 * Locks:
	 * locking hierarchy when more than one lock is held concurrently:
	 * cblock > txlock > tclock.
	 */
	kmutex_t		cblock;		/* sync callback processing */
	kmutex_t		txlock;		/* sync transmits */
	kmutex_t		tclock;		/* tx reclaim lock */

	/* channel info from ldc layer */
	uint64_t		ldc_id;		/* channel number */
	uint64_t		ldc_handle;	/* channel handle */
	ldc_status_t		ldc_status;	/* channel status */

	/* handshake info */
	vgen_ver_t		vgen_versions[VGEN_NUM_VER]; /* versions */
	int			hphase;		/* handshake phase */
	int			hstate;		/* handshake state bits */
	uint32_t		local_sid;	/* local session id */
	uint32_t		peer_sid;	/* session id of peer */
	vgen_hparams_t		local_hparams;	/* local handshake params */
	vgen_hparams_t		peer_hparams;	/* peer's handshake params */
	timeout_id_t		htid;		/* handshake wd timeout id */

	/* transmit and receive descriptor ring info */
	ldc_dring_handle_t	tx_dhandle;	/* tx descriptor ring handle */
	ldc_mem_cookie_t	tx_dcookie;	/* tx descriptor ring cookie */
	ldc_dring_handle_t	rx_dhandle;	/* mapped rx dhandle */
	ldc_mem_cookie_t	rx_dcookie;	/* rx descriptor ring cookie */
	vnet_public_desc_t	*txdp;		/* transmit frame descriptors */
	vnet_public_desc_t	*txdendp;	/* txd ring end */
	vgen_private_desc_t	*tbufp;		/* associated tx resources */
	vgen_private_desc_t	*tbufendp;	/* tbuf ring end */
	vgen_private_desc_t	*next_tbufp;	/* next free tbuf */
	vgen_private_desc_t	*cur_tbufp;	/* next reclaim tbuf */
	uint64_t		next_txseq;	/* next tx sequence number */
	uint32_t		num_txdcookies;	/* # of tx dring cookies */
	uint32_t		num_rxdcookies;	/* # of rx dring cookies */
	uint32_t		next_txi;	/* next tx descriptor index */
	uint32_t		num_txds;	/* number of tx descriptors */
	uint32_t		reclaim_lowat;	/* lowat for tx reclaim */
	uint32_t		reclaim_hiwat;	/* hiwat for tx reclaim */
	clock_t			reclaim_lbolt;	/* time of last tx reclaim */
	timeout_id_t		wd_tid;		/* tx watchdog timeout id */
	vnet_public_desc_t	*rxdp;		/* receive frame descriptors */
	uint64_t		next_rxseq;	/* next expected recv seqnum */
	uint32_t		next_rxi;	/* next expected recv index */
	uint32_t		num_rxds;	/* number of rx descriptors */
	caddr_t			tx_datap;	/* prealloc'd tx data area */
	vio_mblk_pool_t		*rmp;		/* rx mblk pool */
	uint32_t		num_rbufs;	/* number of rx bufs */

	/* misc */
	uint32_t		flags;		/* flags */
	boolean_t		need_resched;	/* reschedule tx */
	boolean_t		need_ldc_reset; /* ldc_reset needed */
	boolean_t		need_mcast_sync; /* sync mcast table with vsw */
	uint32_t		hretries;	/* handshake retry count */
	boolean_t		resched_peer;	/* send tx msg to peer */

	/* channel statistics */
	vgen_stats_t		*statsp;	/* channel statistics */
	kstat_t			*ksp;		/* channel kstats */

} vgen_ldc_t;

/* Channel list structure */
typedef struct vgen_ldclist_s {
	vgen_ldc_t	*headp;		/* head of the list */
	krwlock_t	rwlock;		/* sync access to the list */
	int		num_ldcs;	/* number of channels in the list */
} vgen_ldclist_t;

/* port information  structure */
typedef struct vgen_port {
	struct vgen_port	*nextp;		/* next port in the list */
	struct vgen		*vgenp;		/* associated vgen_t */
	int			port_num;	/* port number */
	vgen_ldclist_t		ldclist;	/* list of ldcs for this port */
	struct ether_addr	macaddr;	/* mac address of peer */
} vgen_port_t;

/* port list structure */
typedef struct vgen_portlist {
	vgen_port_t	*headp;		/* head of ports */
	vgen_port_t	*tailp;		/* tail */
	krwlock_t	rwlock;		/* sync access to the port list */
} vgen_portlist_t;

/* vgen instance information  */
typedef struct vgen {
	void			*vnetp;		/* associated vnet instance */
	dev_info_t		*vnetdip;	/* dip of vnet */
	uint8_t			macaddr[ETHERADDRL];	/* mac addr of vnet */
	kmutex_t		lock;		/* synchornize ops */
	int			flags;		/* flags */
	vgen_portlist_t		vgenports;	/* Port List */
	mdeg_node_spec_t	*mdeg_parentp;
	mdeg_handle_t		mdeg_hdl;
	vgen_port_t		*vsw_portp;	/* port connected to vsw */
	mac_register_t		*macp;		/* vgen mac ops */
	struct ether_addr	*mctab;		/* multicast addr table */
	uint32_t		mcsize;		/* allocated size of mctab */
	uint32_t		mccount;	/* # of valid addrs in mctab */
	vio_mblk_pool_t		*rmp;		/* rx mblk pools to be freed */
} vgen_t;

#ifdef __cplusplus
}
#endif

#endif	/* _VNET_GEN_H */
