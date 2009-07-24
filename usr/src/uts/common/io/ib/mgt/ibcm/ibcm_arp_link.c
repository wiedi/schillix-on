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

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/dlpi.h>
#include <sys/stropts.h>
#include <sys/strsun.h>
#include <sys/sysmacros.h>
#include <sys/strlog.h>
#include <sys/ddi.h>
#include <sys/cmn_err.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <sys/ethernet.h>
#include <inet/arp.h>
#include <inet/ip.h>
#include <inet/ip_ire.h>
#include <inet/ip_if.h>
#include <sys/ib/mgt/ibcm/ibcm_arp.h>
#include <inet/ip_ftable.h>

static areq_t ibcm_arp_areq_template = {
	AR_ENTRY_QUERY,	/* cmd */
	sizeof (areq_t) + (2 * IP_ADDR_LEN),	/* name offset */
	sizeof (areq_t),	/* name len */
	IP_ARP_PROTO_TYPE,	/* protocol, from arps perspective */
	sizeof (areq_t),	/* target addr offset */
	IP_ADDR_LEN,	/* target ADDR_length */
	0,	/* flags */
	sizeof (areq_t) + IP_ADDR_LEN,	/* sender addr offset */
	IP_ADDR_LEN,	/* sender addr length */
	IBCM_ARP_XMIT_COUNT,	/* xmit_count */
	IBCM_ARP_XMIT_INTERVAL,	/* (re)xmit_interval in milliseconds */
	4	/* max # of requests to buffer */
		/*
		 * anything else filled in by the code
		 */
};

static area_t ibcm_arp_area_template = {
	AR_ENTRY_ADD,			/* cmd */
	sizeof (area_t) + IPOIB_ADDRL + (2 * IP_ADDR_LEN), /* name offset */
	sizeof (area_t),		/* name len */
	IP_ARP_PROTO_TYPE,		/* protocol, from arps perspective */
	sizeof (area_t),		/* proto addr offset */
	IP_ADDR_LEN,			/* proto ADDR_length */
	sizeof (area_t) + (IP_ADDR_LEN),	/* proto mask offset */
	0,				/* flags */
	sizeof (area_t) + (2 * IP_ADDR_LEN),	/* hw addr offset */
	IPOIB_ADDRL				/* hw addr length */
};

extern char cmlog[];

_NOTE(SCHEME_PROTECTS_DATA("Unshared data", msgb))
_NOTE(SCHEME_PROTECTS_DATA("Unshared data", area_t))
_NOTE(SCHEME_PROTECTS_DATA("Unshared data", ibcm_arp_streams_t))

static void ibcm_arp_timeout(void *arg);
static void ibcm_arp_pr_callback(ibcm_arp_prwqn_t *wqnp, int status);
static void ibcm_ipv6_resolver_ack(ip2mac_t *, void *);
static int ibcm_ipv6_lookup(ibcm_arp_prwqn_t *wqnp, ill_t *ill, zoneid_t zid);

/*
 * issue a AR_ENTRY_QUERY to arp driver and schedule a timeout.
 */
static int
ibcm_arp_query_arp(ibcm_arp_prwqn_t *wqnp)
{
	int len;
	int name_len;
	int name_offset;
	char *cp;
	mblk_t *mp;
	mblk_t *mp1;
	areq_t *areqp;
	ibcm_arp_streams_t *ib_s = (ibcm_arp_streams_t *)wqnp->arg;

	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_query_arp(ib_s: %p wqnp: %p)",
	    ib_s, wqnp);

	name_offset = ibcm_arp_areq_template.areq_name_offset;

	/*
	 * allocate mblk for AR_ENTRY_QUERY
	 */
	name_len = strlen(wqnp->ifname) + 1;
	len = name_len + name_offset;
	if ((mp = allocb(len, BPRI_HI)) == NULL) {
		return (ENOMEM);
	}
	bzero(mp->b_rptr, len);
	mp->b_wptr += len;

	/*
	 * allocate a mblk and set wqnp in the data
	 */
	if ((mp1 = allocb(sizeof (void *), BPRI_HI)) == NULL) {
		freeb(mp);
		return (ENOMEM);
	}

	mp1->b_wptr += sizeof (void *);
	*(uintptr_t *)(void *)mp1->b_rptr = (uintptr_t)wqnp;	/* store wqnp */

	cp = (char *)mp->b_rptr;
	bcopy(&ibcm_arp_areq_template, cp, sizeof (areq_t));
	areqp = (void *)cp;
	areqp->areq_name_length = name_len;

	cp = (char *)areqp + areqp->areq_name_offset;
	bcopy(wqnp->ifname, cp, name_len);

	areqp->areq_proto = wqnp->ifproto;
	bcopy(&wqnp->ifproto, areqp->areq_sap, 2);
	cp = (char *)areqp + areqp->areq_target_addr_offset;
	bcopy(&wqnp->dst_addr.un.ip4addr, cp, IP_ADDR_LEN);
	cp = (char *)areqp + areqp->areq_sender_addr_offset;
	bcopy(&wqnp->src_addr.un.ip4addr, cp, IP_ADDR_LEN);

	mp->b_cont = mp1;

	DB_TYPE(mp) = M_PROTO;

	/*
	 * issue the request to arp
	 */
	wqnp->flags |= IBCM_ARP_PR_RESOLVE_PENDING;
	wqnp->timeout_id = timeout(ibcm_arp_timeout, wqnp,
	    drv_usectohz(IBCM_ARP_TIMEOUT * 1000));
	if (canputnext(ib_s->arpqueue)) {
		putnext(ib_s->arpqueue, mp);
	} else {
		(void) putq(ib_s->arpqueue, mp);
		qenable(ib_s->arpqueue);
	}

	return (0);
}

/*
 * issue AR_ENTRY_SQUERY to arp driver
 */
static int
ibcm_arp_squery_arp(ibcm_arp_prwqn_t *wqnp)
{
	int len;
	int name_len;
	char *cp;
	mblk_t *mp;
	mblk_t *mp1;
	area_t *areap;
	uint32_t  proto_mask = 0xffffffff;
	struct iocblk *ioc;
	ibcm_arp_streams_t *ib_s = (ibcm_arp_streams_t *)wqnp->arg;

	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_squery_arp(ib_s: %p wqnp: %p)",
	    ib_s, wqnp);

	/*
	 * allocate mblk for AR_ENTRY_SQUERY
	 */
	name_len = strlen(wqnp->ifname) + 1;
	len = ibcm_arp_area_template.area_name_offset + name_len +
	    sizeof (uintptr_t);
	if ((mp = allocb(len, BPRI_HI)) == NULL) {
		return (ENOMEM);
	}
	bzero(mp->b_rptr, len);
	mp->b_wptr += len + sizeof (uintptr_t);

	*(uintptr_t *)(void *)mp->b_rptr = (uintptr_t)wqnp;	/* store wqnp */
	mp->b_rptr += sizeof (uintptr_t);


	cp = (char *)mp->b_rptr;
	bcopy(&ibcm_arp_area_template, cp, sizeof (area_t));

	areap = (void *)cp;
	areap->area_cmd = AR_ENTRY_SQUERY;
	areap->area_name_length = name_len;
	cp = (char *)areap + areap->area_name_offset;
	bcopy(wqnp->ifname, cp, name_len);

	cp = (char *)areap + areap->area_proto_addr_offset;
	bcopy(&wqnp->dst_addr.un.ip4addr, cp, IP_ADDR_LEN);

	cp = (char *)areap + areap->area_proto_mask_offset;
	bcopy(&proto_mask, cp, IP_ADDR_LEN);

	mp1 = allocb(sizeof (struct iocblk), BPRI_HI);
	if (mp1 == NULL) {
		freeb(mp);
		return (ENOMEM);
	}
	ioc = (void *)mp1->b_rptr;
	ioc->ioc_cmd = AR_ENTRY_SQUERY;
	ioc->ioc_error = 0;
	ioc->ioc_cr = NULL;
	ioc->ioc_count = msgdsize(mp);
	mp1->b_wptr += sizeof (struct iocblk);
	mp1->b_cont = mp;

	DB_TYPE(mp1) = M_IOCTL;

	if (canputnext(ib_s->arpqueue)) {
		putnext(ib_s->arpqueue, mp1);
	} else {
		(void) putq(ib_s->arpqueue, mp1);
		qenable(ib_s->arpqueue);
	}
	return (0);
}

/*
 * issue a AR_ENTRY_ADD to arp driver
 * This is required as arp driver does not maintain a cache.
 */
static int
ibcm_arp_add(ibcm_arp_prwqn_t *wqnp)
{
	int len;
	int name_len;
	char *cp;
	mblk_t *mp;
	area_t *areap;
	uint32_t  proto_mask = 0xffffffff;
	ibcm_arp_streams_t *ib_s = (ibcm_arp_streams_t *)wqnp->arg;

	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_add(ib_s: %p wqnp: %p)", ib_s, wqnp);

	/*
	 * allocate mblk for AR_ENTRY_ADD
	 */

	name_len = strlen(wqnp->ifname) + 1;
	len = ibcm_arp_area_template.area_name_offset + name_len;
	if ((mp = allocb(len, BPRI_HI)) == NULL) {
		return (ENOMEM);
	}
	bzero(mp->b_rptr, len);
	mp->b_wptr += len;

	cp = (char *)mp->b_rptr;
	bcopy(&ibcm_arp_area_template, cp, sizeof (area_t));

	areap = (void *)mp->b_rptr;
	areap->area_name_length = name_len;
	cp = (char *)areap + areap->area_name_offset;
	bcopy(wqnp->ifname, cp, name_len);

	cp = (char *)areap + areap->area_proto_addr_offset;
	bcopy(&wqnp->dst_addr.un.ip4addr, cp, IP_ADDR_LEN);

	cp = (char *)areap + areap->area_proto_mask_offset;
	bcopy(&proto_mask, cp, IP_ADDR_LEN);

	cp = (char *)areap + areap->area_hw_addr_offset;
	bcopy(&wqnp->dst_mac, cp, IPOIB_ADDRL);

	DB_TYPE(mp) = M_PROTO;

	if (canputnext(ib_s->arpqueue)) {
		putnext(ib_s->arpqueue, mp);
	} else {
		(void) putq(ib_s->arpqueue, mp);
		qenable(ib_s->arpqueue);
	}
	return (0);
}


/*
 * timeout routine when there is no response to AR_ENTRY_QUERY
 */
static void
ibcm_arp_timeout(void *arg)
{
	ibcm_arp_prwqn_t *wqnp = (ibcm_arp_prwqn_t *)arg;
	ibcm_arp_streams_t *ib_s = (ibcm_arp_streams_t *)wqnp->arg;

	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_timeout(ib_s: %p wqnp: %p)",
	    ib_s, wqnp);
	wqnp->flags &= ~IBCM_ARP_PR_RESOLVE_PENDING;
	cv_broadcast(&ib_s->cv);

	/*
	 * indicate to user
	 */
	ibcm_arp_pr_callback(wqnp, EHOSTUNREACH);
}

/*
 * delete a wait queue node from the list.
 * assumes mutex is acquired
 */
void
ibcm_arp_prwqn_delete(ibcm_arp_prwqn_t *wqnp)
{
	ibcm_arp_streams_t *ib_s;

	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_prwqn_delete(%p)", wqnp);

	ib_s = (ibcm_arp_streams_t *)wqnp->arg;
	ib_s->wqnp = NULL;
	kmem_free(wqnp, sizeof (ibcm_arp_prwqn_t));
}

/*
 * allocate a wait queue node, and insert it in the list
 */
static ibcm_arp_prwqn_t *
ibcm_arp_create_prwqn(ibcm_arp_streams_t *ib_s, ibt_ip_addr_t *dst_addr,
    ibt_ip_addr_t *src_addr, ibcm_arp_pr_comp_func_t func)
{
	ibcm_arp_prwqn_t *wqnp;

	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_create_prwqn(ib_s: 0x%p)", ib_s);

	if (dst_addr == NULL) {
		return (NULL);
	}
	if ((wqnp = kmem_zalloc(sizeof (ibcm_arp_prwqn_t), KM_NOSLEEP)) ==
	    NULL) {
		return (NULL);
	}
	wqnp->dst_addr = *dst_addr;

	if (src_addr) {
		wqnp->usrc_addr = *src_addr;
	}
	wqnp->func = func;
	wqnp->arg = ib_s;
	wqnp->ifproto = (dst_addr->family == AF_INET) ?
	    ETHERTYPE_IP : ETHERTYPE_IPV6;

	ib_s->wqnp = wqnp;

	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_create_prwqn: Return wqnp: %p", wqnp);

	return (wqnp);
}

/*
 * call the user function
 * called with lock held
 */
static void
ibcm_arp_pr_callback(ibcm_arp_prwqn_t *wqnp, int status)
{
	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_pr_callback(%p, %d)", wqnp, status);

	wqnp->func((void *)wqnp, status);
}

/*
 * Check if the interface is loopback or IB.
 */
static int
ibcm_arp_check_interface(ill_t *ill)
{
	if (IS_LOOPBACK(ill) || ill->ill_type == IFT_IB)
		return (0);

	return (ETIMEDOUT);
}

int
ibcm_arp_pr_lookup(ibcm_arp_streams_t *ib_s, ibt_ip_addr_t *dst_addr,
    ibt_ip_addr_t *src_addr, ibcm_arp_pr_comp_func_t func)
{
	ibcm_arp_prwqn_t *wqnp;
	ire_t	*ire = NULL;
	ire_t	*src_ire = NULL;
	ipif_t	*ipif;
	ill_t	*ill, *hwaddr_ill = NULL;
	ip_stack_t *ipst;
	int		len;

	IBCM_PRINT_IP("ibcm_arp_pr_lookup: SRC", src_addr);
	IBCM_PRINT_IP("ibcm_arp_pr_lookup: DST", dst_addr);

	if ((wqnp = ibcm_arp_create_prwqn(ib_s, dst_addr,
	    src_addr, func)) == NULL) {
		IBTF_DPRINTF_L2(cmlog, "ibcm_arp_pr_lookup: "
		    "ibcm_arp_create_prwqn failed");
		ib_s->status = ENOMEM;
		return (1);
	}

	ipst = netstack_find_by_zoneid(GLOBAL_ZONEID)->netstack_ip;
	if (dst_addr->family == AF_INET) {
		/*
		 * Get the ire for the local address
		 */
		IBTF_DPRINTF_L5(cmlog, "ibcm_arp_pr_lookup: ire_ctable_lookup");
		src_ire = ire_ctable_lookup(src_addr->un.ip4addr, NULL,
		    IRE_LOCAL, NULL, ALL_ZONES, NULL, MATCH_IRE_TYPE, ipst);
		if (src_ire == NULL) {
			IBTF_DPRINTF_L2(cmlog, "ibcm_arp_pr_lookup: "
			    "ire_ctable_lookup failed");
			ib_s->status = EFAULT;
			goto fail;
		}
		IBTF_DPRINTF_L5(cmlog, "ibcm_arp_pr_lookup: ire_ctable_lookup");

		/*
		 * get an ire for the destination address with the matching
		 * source address
		 */
		ire = ire_ftable_lookup(dst_addr->un.ip4addr, 0, 0, 0,
		    src_ire->ire_ipif, 0, src_ire->ire_zoneid, 0, NULL,
		    MATCH_IRE_SRC, ipst);
		if (ire == NULL) {
			IBTF_DPRINTF_L2(cmlog, "ibcm_arp_pr_lookup: "
			    "ire_ftable_lookup failed");
			ib_s->status = EFAULT;
			goto fail;
		}

		IBTF_DPRINTF_L5(cmlog, "ibcm_arp_pr_lookup: ire_ftable_lookup:"
		    "done");

		wqnp->gateway.un.ip4addr =
		    ((ire->ire_gateway_addr == INADDR_ANY) ?
		    ire->ire_addr : ire->ire_gateway_addr);
		wqnp->netmask.un.ip4addr = ire->ire_mask;
		wqnp->src_addr.un.ip4addr = ire->ire_src_addr;
		wqnp->src_addr.family = wqnp->gateway.family =
		    wqnp->netmask.family = AF_INET;

	} else if (dst_addr->family == AF_INET6) {
		/*
		 * Get the ire for the local address
		 */
		src_ire = ire_ctable_lookup_v6(&src_addr->un.ip6addr, NULL,
		    IRE_LOCAL, NULL, ALL_ZONES, NULL, MATCH_IRE_TYPE, ipst);
		if (src_ire == NULL) {
			IBTF_DPRINTF_L2(cmlog, "ibcm_arp_pr_lookup: "
			    "ire_ctable_lookup_v6 failed");
			ib_s->status = EFAULT;
			goto fail;
		}
		IBTF_DPRINTF_L5(cmlog, "ibcm_arp_pr_lookup: "
		    "ire_ctable_lookup_v6: done");

		/*
		 * get an ire for the destination address with the matching
		 * source address
		 */
		ire = ire_ftable_lookup_v6(&dst_addr->un.ip6addr, 0, 0, 0,
		    src_ire->ire_ipif, 0, src_ire->ire_zoneid, 0, NULL,
		    MATCH_IRE_SRC, ipst);
		if (ire == NULL) {
			IBTF_DPRINTF_L2(cmlog, "ibcm_arp_pr_lookup: "
			    "ire_ftable_lookup_v6 failed");
			ib_s->status = EFAULT;
			goto fail;
		}
		IBTF_DPRINTF_L5(cmlog, "ibcm_arp_pr_lookup: "
		    "ire_ftable_lookup_v6: done");

		wqnp->gateway.un.ip6addr =
		    (IN6_IS_ADDR_UNSPECIFIED(&ire->ire_gateway_addr_v6) ?
		    ire->ire_addr_v6 : ire->ire_gateway_addr_v6);
		wqnp->netmask.un.ip6addr = ire->ire_mask_v6;
		wqnp->src_addr.un.ip6addr = ire->ire_src_addr_v6;
		wqnp->src_addr.family = wqnp->gateway.family =
		    wqnp->netmask.family = AF_INET6;
	}

	ipif = src_ire->ire_ipif;
	ill = ipif->ipif_ill;
	(void) strlcpy(wqnp->ifname, ill->ill_name, sizeof (wqnp->ifname));

	/*
	 * For IPMP data addresses, we need to use the hardware address of the
	 * interface bound to the given address.
	 */
	if (IS_IPMP(ill)) {
		if ((hwaddr_ill = ipmp_ipif_hold_bound_ill(ipif)) == NULL) {
			IBTF_DPRINTF_L2(cmlog, "ibcm_arp_pr_lookup: no bound "
			    "ill for IPMP interface %s", ill->ill_name);
			ib_s->status = EFAULT;
			goto fail;
		}
	} else {
		hwaddr_ill = ill;
		ill_refhold(hwaddr_ill); 	/* for symmetry */
	}

	if ((ib_s->status = ibcm_arp_check_interface(hwaddr_ill)) != 0) {
		IBTF_DPRINTF_L2(cmlog, "ibcm_arp_pr_lookup: "
		    "ibcm_arp_check_interface failed");
		goto fail;
	}

	bcopy(hwaddr_ill->ill_phys_addr, &wqnp->src_mac,
	    hwaddr_ill->ill_phys_addr_length);

	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_pr_lookup: outgoing if:%s",
	    wqnp->ifname);

	/*
	 * if the user supplied a address, then verify rts returned
	 * the same address
	 */
	if (wqnp->usrc_addr.family) {
		len = (wqnp->usrc_addr.family == AF_INET) ?
		    IP_ADDR_LEN : sizeof (in6_addr_t);
		if (bcmp(&wqnp->usrc_addr.un, &wqnp->src_addr.un, len)) {
			IBTF_DPRINTF_L2(cmlog, "ibcm_arp_pr_lookup: srcaddr "
			    "mismatch:%d", ENETUNREACH);
			goto fail;
		}
	}

	/*
	 * at this stage, we have the source address and the IB
	 * interface, now get the destination mac address from
	 * arp or ipv6 drivers
	 */
	if (wqnp->dst_addr.family == AF_INET) {
		if ((ib_s->status = ibcm_arp_squery_arp(wqnp)) != 0) {
			IBTF_DPRINTF_L2(cmlog, "ibcm_arp_pr_lookup: "
			    "ibcm_arp_squery_arp failed: %d", ib_s->status);
			goto fail;
		}
	} else {
		if ((ib_s->status = ibcm_ipv6_lookup(wqnp, ill, getzoneid())) !=
		    0) {
			IBTF_DPRINTF_L2(cmlog, "ibcm_arp_pr_lookup: "
			    "ibcm_ipv6_lookup failed: %d", ib_s->status);
			goto fail;
		}
	}

	ill_refrele(hwaddr_ill);
	IRE_REFRELE(ire);
	IRE_REFRELE(src_ire);
	netstack_rele(ipst->ips_netstack);

	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_pr_lookup: Return: 0x%p", wqnp);
	return (0);
fail:
	if (hwaddr_ill != NULL)
		ill_refrele(hwaddr_ill);
	if (ire != NULL)
		IRE_REFRELE(ire);
	if (src_ire != NULL)
		IRE_REFRELE(src_ire);
	ibcm_arp_prwqn_delete(wqnp);
	netstack_rele(ipst->ips_netstack);
	return (1);
}

/*
 * called from lrsrv.
 * process a AR_ENTRY_QUERY reply from arp
 * the message should be M_DATA -->> dl_unitdata_req
 */
static void
ibcm_arp_pr_arp_query_ack(mblk_t *mp)
{
	ibcm_arp_prwqn_t 	*wqnp;
	dl_unitdata_req_t *dlreq;
	ibcm_arp_streams_t *ib_s;
	char *cp;
	int rc;

	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_pr_arp_query_ack(%p)", mp);

	/*
	 * the first mblk contains the wqnp pointer for the request
	 */
	if (MBLKL(mp) != sizeof (void *)) {
		freemsg(mp);
		return;
	}

	wqnp = *(ibcm_arp_prwqn_t **)(void *)mp->b_rptr; /* retrieve wqnp */
	ib_s = (ibcm_arp_streams_t *)wqnp->arg;

	mutex_enter(&ib_s->lock);

	/*
	 * cancel the timeout for this request
	 */
	(void) untimeout(wqnp->timeout_id);

	/*
	 * sanity checks on the dl_unitdata_req block
	 */
	if (!mp->b_cont) {
		IBTF_DPRINTF_L2(cmlog, "areq_ack: b_cont = NULL\n");
		rc = EPROTO;
		goto user_callback;
	}
	if (MBLKL(mp->b_cont) < (sizeof (dl_unitdata_req_t) + IPOIB_ADDRL)) {
		IBTF_DPRINTF_L2(cmlog, "areq_ack: invalid len in "
		    "dl_unitdatareq_t block\n");
		rc = EPROTO;
		goto user_callback;
	}
	dlreq = (void *)mp->b_cont->b_rptr;
	if (dlreq->dl_primitive != DL_UNITDATA_REQ) {
		IBTF_DPRINTF_L2(cmlog, "areq_ack: invalid dl_primitive "
		    "in dl_unitdatareq_t block\n");
		rc = EPROTO;
		goto user_callback;
	}
	if (dlreq->dl_dest_addr_length != (IPOIB_ADDRL + 2)) {
		IBTF_DPRINTF_L2(cmlog, "areq_ack: invalid hw len in "
		    "dl_unitdatareq_t block %d\n", dlreq->dl_dest_addr_length);
		rc = EPROTO;
		goto user_callback;
	}
	cp = (char *)mp->b_cont->b_rptr + dlreq->dl_dest_addr_offset;
	bcopy(cp, &wqnp->dst_mac, IPOIB_ADDRL);

	/*
	 * at this point we have src/dst gid's derived from the mac addresses
	 * now get the hca, port
	 */
	bcopy(&wqnp->src_mac.ipoib_gidpref, &wqnp->sgid, sizeof (ib_gid_t));
	bcopy(&wqnp->dst_mac.ipoib_gidpref, &wqnp->dgid, sizeof (ib_gid_t));
	freemsg(mp);

	IBCM_H2N_GID(wqnp->sgid);
	IBCM_H2N_GID(wqnp->dgid);

	(void) ibcm_arp_add(wqnp);

	mutex_exit(&ib_s->lock);
	ibcm_arp_pr_callback(wqnp, 0);

	return;
user_callback:
	freemsg(mp);
	mutex_exit(&ib_s->lock);

	/*
	 * indicate to user
	 */
	ibcm_arp_pr_callback(wqnp, rc);
}

/*
 * process a AR_ENTRY_SQUERY reply from arp
 * the message should be M_IOCACK -->> area_t
 */
static void
ibcm_arp_pr_arp_squery_ack(mblk_t *mp)
{
	struct iocblk *ioc;
	mblk_t	*mp1;
	ibcm_arp_prwqn_t 	*wqnp;
	ibcm_arp_streams_t *ib_s;
	area_t *areap;
	char *cp;

	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_pr_arp_squery_ack(%p)", mp);

	if (MBLKL(mp) < sizeof (struct iocblk)) {
		freemsg(mp);
		return;
	}

	ioc = (void *)mp->b_rptr;
	if ((ioc->ioc_cmd != AR_ENTRY_SQUERY) || (mp->b_cont == NULL)) {
		freemsg(mp);
		return;
	}

	mp1 = mp->b_cont;

	wqnp = *(ibcm_arp_prwqn_t **)((uintptr_t)mp1->b_rptr -
	    sizeof (uintptr_t));
	ib_s = (ibcm_arp_streams_t *)wqnp->arg;

	mutex_enter(&ib_s->lock);

	/*
	 * cancel the timeout for this request
	 */
	(void) untimeout(wqnp->timeout_id);

	/* If the entry was not in arp cache, ioc_error is set */
	if (ioc->ioc_error) {

		/*
		 * send out AR_ENTRY_QUERY which would send
		 * arp-request on wire
		 */
		IBTF_DPRINTF_L3(cmlog, "Sending a Query_ARP");

		(void) ibcm_arp_query_arp(wqnp);
		freemsg(mp);
		mutex_exit(&ib_s->lock);
		return;
	}

	areap = (void *)mp1->b_rptr;
	cp = (char *)areap + areap->area_hw_addr_offset;
	bcopy(cp, &wqnp->dst_mac, IPOIB_ADDRL);

	/*
	 * at this point we have src/dst gid's derived from the mac addresses
	 * now get the hca, port
	 */
	bcopy(&wqnp->src_mac.ipoib_gidpref, &wqnp->sgid, sizeof (ib_gid_t));
	bcopy(&wqnp->dst_mac.ipoib_gidpref, &wqnp->dgid, sizeof (ib_gid_t));
	freemsg(mp);

	IBCM_H2N_GID(wqnp->sgid);
	IBCM_H2N_GID(wqnp->dgid);

	mutex_exit(&ib_s->lock);
	ibcm_arp_pr_callback(wqnp, 0);
}

/*
 * Process arp ack's.
 */
void
ibcm_arp_pr_arp_ack(mblk_t *mp)
{
	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_pr_arp_ack(0x%p, DB_TYPE %lX)",
	    mp, DB_TYPE(mp));

	if (DB_TYPE(mp) == M_DATA) {
		ibcm_arp_pr_arp_query_ack(mp);
	} else if ((DB_TYPE(mp) == M_IOCACK) ||
	    (DB_TYPE(mp) == M_IOCNAK)) {
		ibcm_arp_pr_arp_squery_ack(mp);
	} else {
		freemsg(mp);
	}
}

/*
 * query the ipv6 driver cache for ipv6 to mac address mapping.
 */
static int
ibcm_ipv6_lookup(ibcm_arp_prwqn_t *wqnp, ill_t *ill, zoneid_t zoneid)
{
	ip2mac_t ip2m;
	sin6_t *sin6;
	ip2mac_id_t ip2mid;
	int err;

	if (wqnp->src_addr.family != AF_INET6) {
		IBTF_DPRINTF_L2(cmlog, "ibcm_ipv6_lookup: SRC_ADDR NOT INET6: "
		    "%d", wqnp->src_addr.family);
		return (1);
	}

	bzero(&ip2m, sizeof (ip2m));
	sin6 = (sin6_t *)&ip2m.ip2mac_pa;
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = wqnp->dst_addr.un.ip6addr;
	ip2m.ip2mac_ifindex = ill->ill_phyint->phyint_ifindex;

	wqnp->flags |= IBCM_ARP_PR_RESOLVE_PENDING;
	/*
	 * XXX XTBD set the scopeid?
	 * issue the request to IP for Neighbor Discovery
	 */
	ip2mid = ip2mac(IP2MAC_RESOLVE, &ip2m, ibcm_ipv6_resolver_ack, wqnp,
	    zoneid);
	err = ip2m.ip2mac_err;
	if (err == EINPROGRESS) {
		wqnp->ip2mac_id = ip2mid;
		wqnp->flags |= IBCM_ARP_PR_RESOLVE_PENDING;
		err = 0;
	} else if (err == 0) {
		ibcm_ipv6_resolver_ack(&ip2m, wqnp);
	}
	return (err);
}

/*
 * do sanity checks on the link-level sockaddr
 */
static boolean_t
ibcm_check_sockdl(struct sockaddr_dl *sdl)
{

	if (sdl->sdl_type != IFT_IB || sdl->sdl_alen != IPOIB_ADDRL)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * callback for resolver lookups, both for success and failure.
 * If Address resolution was succesful: return GID info.
 */
static void
ibcm_ipv6_resolver_ack(ip2mac_t *ip2macp, void *arg)
{
	ibcm_arp_prwqn_t *wqnp = (ibcm_arp_prwqn_t *)arg;
	ibcm_arp_streams_t *ib_s;
	uchar_t *cp;
	int err = 0;

	IBTF_DPRINTF_L4(cmlog, "ibcm_ipv6_resolver_ack(%p, %p)", ip2macp, wqnp);

	ib_s = (ibcm_arp_streams_t *)wqnp->arg;
	mutex_enter(&ib_s->lock);

	if (ip2macp->ip2mac_err != 0) {
		wqnp->flags &= ~IBCM_ARP_PR_RESOLVE_PENDING;
		cv_broadcast(&ib_s->cv);
		err = EHOSTUNREACH;
		goto user_callback;
	}

	if (!ibcm_check_sockdl(&ip2macp->ip2mac_ha)) {
		IBTF_DPRINTF_L2(cmlog, "ibcm_ipv6_resolver_ack: Error: "
		    "interface %s is not IB\n", wqnp->ifname);
		err = EHOSTUNREACH;
		goto user_callback;
	}

	cp = (uchar_t *)LLADDR(&ip2macp->ip2mac_ha);
	bcopy(cp, &wqnp->dst_mac, IPOIB_ADDRL);

	/*
	 * at this point we have src/dst gid's derived from the mac addresses
	 * now get the hca, port
	 */
	bcopy(&wqnp->src_mac.ipoib_gidpref, &wqnp->sgid, sizeof (ib_gid_t));
	bcopy(&wqnp->dst_mac.ipoib_gidpref, &wqnp->dgid, sizeof (ib_gid_t));

	IBCM_H2N_GID(wqnp->sgid);
	IBCM_H2N_GID(wqnp->dgid);

user_callback:
	mutex_exit(&ib_s->lock);
	ibcm_arp_pr_callback(wqnp, err);
}
