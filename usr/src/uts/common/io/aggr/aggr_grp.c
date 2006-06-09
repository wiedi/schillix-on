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

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * IEEE 802.3ad Link Aggregation -- Link Aggregation Groups.
 *
 * An instance of the structure aggr_grp_t is allocated for each
 * link aggregation group. When created, aggr_grp_t objects are
 * entered into the aggr_grp_hash hash table maintained by the modhash
 * module. The hash key is the port number associated with the link
 * aggregation group. The port number associated with a group corresponds
 * the key associated with the group.
 *
 * A set of MAC ports are associated with each association group.
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/list.h>
#include <sys/ksynch.h>
#include <sys/kmem.h>
#include <sys/stream.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/atomic.h>
#include <sys/stat.h>
#include <sys/modhash.h>
#include <sys/strsun.h>
#include <sys/dlpi.h>

#include <sys/aggr.h>
#include <sys/aggr_impl.h>

static void aggr_m_info(void *, mac_info_t *);
static int aggr_m_start(void *);
static void aggr_m_stop(void *);
static int aggr_m_promisc(void *, boolean_t);
static int aggr_m_multicst(void *, boolean_t, const uint8_t *);
static int aggr_m_unicst(void *, const uint8_t *);
static uint64_t aggr_m_stat(void *, enum mac_stat);
static void aggr_m_resources(void *);
static void aggr_m_ioctl(void *, queue_t *, mblk_t *);

static aggr_port_t *aggr_grp_port_lookup(aggr_grp_t *, const char *, uint32_t);
static int aggr_grp_rem_port(aggr_grp_t *, aggr_port_t *, boolean_t *,
    boolean_t *);
static void aggr_stats_op(enum mac_stat, uint64_t *, uint64_t *, boolean_t);
static void aggr_grp_capab_set(aggr_grp_t *);
static boolean_t aggr_grp_capab_check(aggr_grp_t *, aggr_port_t *);

static kmem_cache_t	*aggr_grp_cache;
static mod_hash_t	*aggr_grp_hash;
static krwlock_t	aggr_grp_lock;
static uint_t		aggr_grp_cnt;

#define	GRP_HASHSZ		64
#define	GRP_HASH_KEY(key)	((mod_hash_key_t)(uintptr_t)key)

static uchar_t aggr_zero_mac[] = {0, 0, 0, 0, 0, 0};
static uchar_t aggr_brdcst_mac[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/* used by grp_info_walker */
typedef struct aggr_grp_info_state {
	uint32_t	ls_group_key;
	boolean_t	ls_group_found;
	aggr_grp_info_new_grp_fn_t ls_new_grp_fn;
	aggr_grp_info_new_port_fn_t ls_new_port_fn;
	void		*ls_fn_arg;
	int		ls_rc;
} aggr_grp_info_state_t;

/*ARGSUSED*/
static int
aggr_grp_constructor(void *buf, void *arg, int kmflag)
{
	aggr_grp_t *grp = buf;

	bzero(grp, sizeof (*grp));
	rw_init(&grp->lg_lock, NULL, RW_DRIVER, NULL);
	mutex_init(&grp->aggr.gl_lock, NULL, MUTEX_DEFAULT, NULL);

	grp->lg_link_state = LINK_STATE_UNKNOWN;

	return (0);
}

/*ARGSUSED*/
static void
aggr_grp_destructor(void *buf, void *arg)
{
	aggr_grp_t *grp = buf;

	if (grp->lg_tx_ports != NULL) {
		kmem_free(grp->lg_tx_ports,
		    grp->lg_tx_ports_size * sizeof (aggr_port_t *));
	}

	mutex_destroy(&grp->aggr.gl_lock);
	rw_destroy(&grp->lg_lock);
}

void
aggr_grp_init(void)
{
	aggr_grp_cache = kmem_cache_create("aggr_grp_cache",
	    sizeof (aggr_grp_t), 0, aggr_grp_constructor,
	    aggr_grp_destructor, NULL, NULL, NULL, 0);

	aggr_grp_hash = mod_hash_create_idhash("aggr_grp_hash",
	    GRP_HASHSZ, mod_hash_null_valdtor);
	rw_init(&aggr_grp_lock, NULL, RW_DEFAULT, NULL);
	aggr_grp_cnt = 0;
}

void
aggr_grp_fini(void)
{
	rw_destroy(&aggr_grp_lock);
	mod_hash_destroy_idhash(aggr_grp_hash);
	kmem_cache_destroy(aggr_grp_cache);
}

uint_t
aggr_grp_count(void)
{
	uint_t	count;

	rw_enter(&aggr_grp_lock, RW_READER);
	count = aggr_grp_cnt;
	rw_exit(&aggr_grp_lock);
	return (count);
}

/*
 * Attach a port to a link aggregation group.
 *
 * A port is attached to a link aggregation group once its speed
 * and link state have been verified.
 *
 * Returns B_TRUE if the group link state or speed has changed. If
 * it's the case, the caller must notify the MAC layer via a call
 * to mac_link().
 */
boolean_t
aggr_grp_attach_port(aggr_grp_t *grp, aggr_port_t *port)
{
	boolean_t link_state_changed = B_FALSE;

	ASSERT(AGGR_LACP_LOCK_HELD(grp));
	ASSERT(RW_WRITE_HELD(&grp->lg_lock));
	ASSERT(RW_WRITE_HELD(&port->lp_lock));

	if (port->lp_state == AGGR_PORT_STATE_ATTACHED)
		return (B_FALSE);

	/*
	 * Validate the MAC port link speed and update the group
	 * link speed if needed.
	 */
	if (port->lp_ifspeed == 0 ||
	    port->lp_link_state != LINK_STATE_UP ||
	    port->lp_link_duplex != LINK_DUPLEX_FULL) {
		/*
		 * Can't attach a MAC port with unknown link speed,
		 * down link, or not in full duplex mode.
		 */
		return (B_FALSE);
	}

	if (grp->lg_ifspeed == 0) {
		/*
		 * The group inherits the speed of the first link being
		 * attached.
		 */
		grp->lg_ifspeed = port->lp_ifspeed;
		link_state_changed = B_TRUE;
	} else if (grp->lg_ifspeed != port->lp_ifspeed) {
		/*
		 * The link speed of the MAC port must be the same as
		 * the group link speed, as per 802.3ad. Since it is
		 * not, the attach is cancelled.
		 */
		return (B_FALSE);
	}

	grp->lg_nattached_ports++;

	/*
	 * Update the group link state.
	 */
	if (grp->lg_link_state != LINK_STATE_UP) {
		grp->lg_link_state = LINK_STATE_UP;
		grp->lg_link_duplex = LINK_DUPLEX_FULL;
		link_state_changed = B_TRUE;
	}

	aggr_grp_multicst_port(port, B_TRUE);

	/*
	 * Update port's state.
	 */
	port->lp_state = AGGR_PORT_STATE_ATTACHED;

	/*
	 * Set port's receive callback
	 */
	port->lp_mrh = mac_rx_add(port->lp_mh, aggr_recv_cb, (void *)port);

	/*
	 * If LACP is OFF, the port can be used to send data as soon
	 * as its link is up and verified to be compatible with the
	 * aggregation.
	 *
	 * If LACP is active or passive, notify the LACP subsystem, which
	 * will enable sending on the port following the LACP protocol.
	 */
	if (grp->lg_lacp_mode == AGGR_LACP_OFF)
		aggr_send_port_enable(port);
	else
		aggr_lacp_port_attached(port);

	return (link_state_changed);
}

boolean_t
aggr_grp_detach_port(aggr_grp_t *grp, aggr_port_t *port)
{
	boolean_t link_state_changed = B_FALSE;

	ASSERT(RW_WRITE_HELD(&grp->lg_lock));
	ASSERT(RW_WRITE_HELD(&port->lp_lock));
	ASSERT(AGGR_LACP_LOCK_HELD(grp));

	/* update state */
	if (port->lp_state != AGGR_PORT_STATE_ATTACHED)
		return (B_FALSE);

	mac_rx_remove(port->lp_mh, port->lp_mrh);
	port->lp_state = AGGR_PORT_STATE_STANDBY;

	aggr_grp_multicst_port(port, B_FALSE);

	if (grp->lg_lacp_mode == AGGR_LACP_OFF)
		aggr_send_port_disable(port);
	else
		aggr_lacp_port_detached(port);

	grp->lg_nattached_ports--;
	if (grp->lg_nattached_ports == 0) {
		/* the last attached MAC port of the group is being detached */
		grp->lg_ifspeed = 0;
		grp->lg_link_state = LINK_STATE_DOWN;
		grp->lg_link_duplex = LINK_DUPLEX_UNKNOWN;
		link_state_changed = B_TRUE;
	}

	return (link_state_changed);
}

/*
 * Update the MAC addresses of the constituent ports of the specified
 * group. This function is invoked:
 * - after creating a new aggregation group.
 * - after adding new ports to an aggregation group.
 * - after removing a port from a group when the MAC address of
 *   that port was used for the MAC address of the group.
 * - after the MAC address of a port changed when the MAC address
 *   of that port was used for the MAC address of the group.
 *
 * Return true if the link state of the aggregation changed, for example
 * as a result of a failure changing the MAC address of one of the
 * constituent ports.
 */
boolean_t
aggr_grp_update_ports_mac(aggr_grp_t *grp)
{
	aggr_port_t *cport;
	boolean_t link_state_changed = B_FALSE;

	ASSERT(RW_WRITE_HELD(&grp->lg_lock));

	if (grp->lg_closing)
		return (link_state_changed);

	for (cport = grp->lg_ports; cport != NULL;
	    cport = cport->lp_next) {
		rw_enter(&cport->lp_lock, RW_WRITER);
		if (aggr_port_unicst(cport, grp->lg_addr) != 0) {
			link_state_changed = link_state_changed ||
			    aggr_grp_detach_port(grp, cport);
		} else {
			/*
			 * If a port was detached because of a previous
			 * failure changing the MAC address, the port is
			 * reattached when it successfully changes the MAC
			 * address now, and this might cause the link state
			 * of the aggregation to change.
			 */
			link_state_changed = link_state_changed ||
			    aggr_grp_attach_port(grp, cport);
		}
		rw_exit(&cport->lp_lock);
	}
	return (link_state_changed);
}

/*
 * Invoked when the MAC address of a port has changed. If the port's
 * MAC address was used for the group MAC address, set mac_addr_changedp
 * to B_TRUE to indicate to the caller that it should send a MAC_NOTE_UNICST
 * notification. If the link state changes due to detach/attach of
 * the constituent port, set link_state_changedp to B_TRUE to indicate
 * to the caller that it should send a MAC_NOTE_LINK notification. In both
 * cases, it is the responsibility of the caller to invoke notification
 * functions after releasing the the port lock.
 */
void
aggr_grp_port_mac_changed(aggr_grp_t *grp, aggr_port_t *port,
    boolean_t *mac_addr_changedp, boolean_t *link_state_changedp)
{
	ASSERT(AGGR_LACP_LOCK_HELD(grp));
	ASSERT(RW_WRITE_HELD(&grp->lg_lock));
	ASSERT(RW_WRITE_HELD(&port->lp_lock));
	ASSERT(mac_addr_changedp != NULL);
	ASSERT(link_state_changedp != NULL);

	*mac_addr_changedp = B_FALSE;
	*link_state_changedp = B_FALSE;

	if (grp->lg_addr_fixed) {
		/*
		 * The group is using a fixed MAC address or an automatic
		 * MAC address has not been set.
		 */
		return;
	}

	if (grp->lg_mac_addr_port == port) {
		/*
		 * The MAC address of the port was assigned to the group
		 * MAC address. Update the group MAC address.
		 */
		bcopy(port->lp_addr, grp->lg_addr, ETHERADDRL);
		*mac_addr_changedp = B_TRUE;
	} else {
		/*
		 * Update the actual port MAC address to the MAC address
		 * of the group.
		 */
		if (aggr_port_unicst(port, grp->lg_addr) != 0) {
			*link_state_changedp = aggr_grp_detach_port(grp, port);
		} else {
			/*
			 * If a port was detached because of a previous
			 * failure changing the MAC address, the port is
			 * reattached when it successfully changes the MAC
			 * address now, and this might cause the link state
			 * of the aggregation to change.
			 */
			*link_state_changedp = aggr_grp_attach_port(grp, port);
		}
	}
}

/*
 * Add a port to a link aggregation group.
 */
static int
aggr_grp_add_port(aggr_grp_t *grp, const char *name, uint_t portnum,
    aggr_port_t **pp)
{
	aggr_port_t *port, **cport;
	int err;

	ASSERT(AGGR_LACP_LOCK_HELD(grp));
	ASSERT(RW_WRITE_HELD(&grp->lg_lock));

	/* create new port */
	err = aggr_port_create(name, portnum, &port);
	if (err != 0)
		return (err);

	rw_enter(&port->lp_lock, RW_WRITER);

	/* add port to list of group constituent ports */
	cport = &grp->lg_ports;
	while (*cport != NULL)
		cport = &((*cport)->lp_next);
	*cport = port;

	/*
	 * Back reference to the group it is member of. A port always
	 * holds a reference to its group to ensure that the back
	 * reference is always valid.
	 */
	port->lp_grp = grp;
	AGGR_GRP_REFHOLD(grp);
	grp->lg_nports++;

	aggr_lacp_init_port(port);

	/*
	 * Initialize the callback functions for this port. Note that this
	 * can only be done after the lp_grp field is set.
	 */
	aggr_port_init_callbacks(port);

	rw_exit(&port->lp_lock);

	if (pp != NULL)
		*pp = port;

	return (0);
}

/*
 * Add one or more ports to an existing link aggregation group.
 */
int
aggr_grp_add_ports(uint32_t key, uint_t nports, laioc_port_t *ports)
{
	int rc, i, nadded = 0;
	aggr_grp_t *grp = NULL;
	aggr_port_t *port;
	boolean_t link_state_changed = B_FALSE;

	/* get group corresponding to key */
	rw_enter(&aggr_grp_lock, RW_READER);
	if (mod_hash_find(aggr_grp_hash, GRP_HASH_KEY(key),
	    (mod_hash_val_t *)&grp) != 0) {
		rw_exit(&aggr_grp_lock);
		return (ENOENT);
	}
	AGGR_GRP_REFHOLD(grp);
	rw_exit(&aggr_grp_lock);

	AGGR_LACP_LOCK(grp);
	rw_enter(&grp->lg_lock, RW_WRITER);

	/* add the specified ports to group */
	for (i = 0; i < nports; i++) {
		/* add port to group */
		if ((rc = aggr_grp_add_port(grp, ports[i].lp_devname,
		    ports[i].lp_port, &port)) != 0)
			goto bail;
		ASSERT(port != NULL);
		nadded++;

		/* check capabilities */
		if (!aggr_grp_capab_check(grp, port)) {
			rc = ENOTSUP;
			goto bail;
		}

		/* start port if group has already been started */
		if (grp->lg_started) {
			rw_enter(&port->lp_lock, RW_WRITER);
			rc = aggr_port_start(port);
			if (rc != 0) {
				rw_exit(&port->lp_lock);
				goto bail;
			}

			/* set port promiscuous mode */
			rc = aggr_port_promisc(port, grp->lg_promisc);
			if (rc != 0) {
				rw_exit(&port->lp_lock);
				goto bail;
			}
			rw_exit(&port->lp_lock);
		}

		/*
		 * Attach each port if necessary.
		 */
		link_state_changed = link_state_changed ||
		    aggr_port_notify_link(grp, port, B_FALSE);
	}

	/* update the MAC address of the constituent ports */
	link_state_changed = link_state_changed ||
	    aggr_grp_update_ports_mac(grp);

	if (link_state_changed)
		mac_link_update(&grp->lg_mac, grp->lg_link_state);

bail:
	if (rc != 0) {
		/* stop and remove ports that have been added */
		for (i = 0; i < nadded && !grp->lg_closing; i++) {
			port = aggr_grp_port_lookup(grp, ports[i].lp_devname,
			    ports[i].lp_port);
			ASSERT(port != NULL);
			if (grp->lg_started) {
				rw_enter(&port->lp_lock, RW_WRITER);
				aggr_port_stop(port);
				rw_exit(&port->lp_lock);
			}
			(void) aggr_grp_rem_port(grp, port, NULL, NULL);
		}
	}

	rw_exit(&grp->lg_lock);
	AGGR_LACP_UNLOCK(grp);
	if (rc == 0 && !grp->lg_closing)
		mac_resource_update(&grp->lg_mac);
	AGGR_GRP_REFRELE(grp);
	return (rc);
}

/*
 * Update properties of an existing link aggregation group.
 */
int
aggr_grp_modify(uint32_t key, aggr_grp_t *grp_arg, uint8_t update_mask,
    uint32_t policy, boolean_t mac_fixed, const uchar_t *mac_addr,
    aggr_lacp_mode_t lacp_mode, aggr_lacp_timer_t lacp_timer)
{
	int rc = 0;
	aggr_grp_t *grp = NULL;
	boolean_t mac_addr_changed = B_FALSE;
	boolean_t link_state_changed = B_FALSE;

	if (grp_arg == NULL) {
		/* get group corresponding to key */
		rw_enter(&aggr_grp_lock, RW_READER);
		if (mod_hash_find(aggr_grp_hash, GRP_HASH_KEY(key),
		    (mod_hash_val_t *)&grp) != 0) {
			rc = ENOENT;
			goto bail;
		}
		AGGR_LACP_LOCK(grp);
		rw_enter(&grp->lg_lock, RW_WRITER);
	} else {
		grp = grp_arg;
		ASSERT(AGGR_LACP_LOCK_HELD(grp));
		ASSERT(RW_WRITE_HELD(&grp->lg_lock));
	}

	ASSERT(RW_WRITE_HELD(&grp->lg_lock) || RW_READ_HELD(&grp->lg_lock));
	AGGR_GRP_REFHOLD(grp);

	/* validate fixed address if specified */
	if ((update_mask & AGGR_MODIFY_MAC) && mac_fixed &&
	    ((bcmp(aggr_zero_mac, mac_addr, ETHERADDRL) == 0) ||
	    (mac_addr[0] & 0x01))) {
		rc = EINVAL;
		goto bail;
	}

	/* update policy if requested */
	if (update_mask & AGGR_MODIFY_POLICY)
		aggr_send_update_policy(grp, policy);

	/* update unicast MAC address if requested */
	if (update_mask & AGGR_MODIFY_MAC) {
		if (mac_fixed) {
			/* user-supplied MAC address */
			grp->lg_mac_addr_port = NULL;
			if (bcmp(mac_addr, grp->lg_addr, ETHERADDRL) != 0) {
				bcopy(mac_addr, grp->lg_addr, ETHERADDRL);
				mac_addr_changed = B_TRUE;
			}
		} else if (grp->lg_addr_fixed) {
			/* switch from user-supplied to automatic */
			aggr_port_t *port = grp->lg_ports;

			rw_enter(&port->lp_lock, RW_WRITER);
			bcopy(port->lp_addr, grp->lg_addr, ETHERADDRL);
			grp->lg_mac_addr_port = port;
			mac_addr_changed = B_TRUE;
			rw_exit(&port->lp_lock);
		}
		grp->lg_addr_fixed = mac_fixed;
	}

	if (mac_addr_changed)
		link_state_changed = aggr_grp_update_ports_mac(grp);

	if (update_mask & AGGR_MODIFY_LACP_MODE)
		aggr_lacp_update_mode(grp, lacp_mode);

	if ((update_mask & AGGR_MODIFY_LACP_TIMER) && !grp->lg_closing)
		aggr_lacp_update_timer(grp, lacp_timer);

bail:
	if (grp != NULL && !grp->lg_closing) {
		/*
		 * If grp_arg is non-NULL, this function is called from
		 * mac_unicst_set(), and the MAC_NOTE_UNICST notification
		 * will be sent there.
		 */
		if ((grp_arg == NULL) && mac_addr_changed)
			mac_unicst_update(&grp->lg_mac, grp->lg_addr);

		if (link_state_changed)
			mac_link_update(&grp->lg_mac, grp->lg_link_state);

	}

	if (grp_arg == NULL) {
		if (grp != NULL) {
			rw_exit(&grp->lg_lock);
			AGGR_LACP_UNLOCK(grp);
		}
		rw_exit(&aggr_grp_lock);
	}

	if (grp != NULL)
		AGGR_GRP_REFRELE(grp);

	return (rc);
}

/*
 * Create a new link aggregation group upon request from administrator.
 * Returns 0 on success, an errno on failure.
 */
int
aggr_grp_create(uint32_t key, uint_t nports, laioc_port_t *ports,
    uint32_t policy, boolean_t mac_fixed, uchar_t *mac_addr,
    aggr_lacp_mode_t lacp_mode, aggr_lacp_timer_t lacp_timer)
{
	aggr_grp_t *grp = NULL;
	aggr_port_t *port;
	mac_t *mac;
	mac_info_t *mip;
	boolean_t link_state_changed;
	int err;
	int i;

	/* need at least one port */
	if (nports == 0)
		return (EINVAL);

	rw_enter(&aggr_grp_lock, RW_WRITER);

	/* does a group with the same key already exist? */
	err = mod_hash_find(aggr_grp_hash, GRP_HASH_KEY(key),
	    (mod_hash_val_t *)&grp);
	if (err == 0) {
		rw_exit(&aggr_grp_lock);
		return (EEXIST);
	}

	grp = kmem_cache_alloc(aggr_grp_cache, KM_SLEEP);

	AGGR_LACP_LOCK(grp);
	rw_enter(&grp->lg_lock, RW_WRITER);

	grp->lg_refs = 1;
	grp->lg_closing = B_FALSE;
	grp->lg_key = key;

	grp->lg_ifspeed = 0;
	grp->lg_link_state = LINK_STATE_UNKNOWN;
	grp->lg_link_duplex = LINK_DUPLEX_UNKNOWN;
	grp->lg_started = B_FALSE;
	grp->lg_promisc = B_FALSE;
	aggr_lacp_init_grp(grp);

	/* add MAC ports to group */
	grp->lg_ports = NULL;
	grp->lg_nports = 0;
	grp->lg_nattached_ports = 0;
	grp->lg_ntx_ports = 0;

	for (i = 0; i < nports; i++) {
		err = aggr_grp_add_port(grp, ports[i].lp_devname,
		    ports[i].lp_port, NULL);
		if (err != 0)
			goto bail;
	}

	/*
	 * If no explicit MAC address was specified by the administrator,
	 * set it to the MAC address of the first port.
	 */
	grp->lg_addr_fixed = mac_fixed;
	if (grp->lg_addr_fixed) {
		/* validate specified address */
		if (bcmp(aggr_zero_mac, mac_addr, ETHERADDRL) == 0) {
			err = EINVAL;
			goto bail;
		}
		bcopy(mac_addr, grp->lg_addr, ETHERADDRL);
	} else {
		bcopy(grp->lg_ports->lp_addr, grp->lg_addr, ETHERADDRL);
		grp->lg_mac_addr_port = grp->lg_ports;
	}

	/*
	 * Update the MAC address of the constituent ports.
	 * None of the port is attached at this time, the link state of the
	 * aggregation will not change.
	 */
	link_state_changed = aggr_grp_update_ports_mac(grp);
	ASSERT(!link_state_changed);

	/* update outbound load balancing policy */
	aggr_send_update_policy(grp, policy);

	/* register with the MAC module */
	mac = &grp->lg_mac;
	bzero(mac, sizeof (*mac));

	mac->m_ident = MAC_IDENT;

	mac->m_driver = grp;
	mac->m_dip = aggr_dip;
	mac->m_port = key;

	mip = &(mac->m_info);
	mip->mi_media = DL_ETHER;
	mip->mi_sdu_min = 0;
	mip->mi_sdu_max = ETHERMTU;

	MAC_STAT_MIB(mip->mi_stat);
	MAC_STAT_ETHER(mip->mi_stat);
	mip->mi_stat[MAC_STAT_LINK_DUPLEX] = B_TRUE;

	mip->mi_addr_length = ETHERADDRL;
	bcopy(aggr_brdcst_mac, mip->mi_brdcst_addr, ETHERADDRL);
	bcopy(grp->lg_addr, mip->mi_unicst_addr, ETHERADDRL);

	mac->m_stat = aggr_m_stat;
	mac->m_start = aggr_m_start;
	mac->m_stop = aggr_m_stop;
	mac->m_promisc = aggr_m_promisc;
	mac->m_multicst = aggr_m_multicst;
	mac->m_unicst = aggr_m_unicst;
	mac->m_tx = aggr_m_tx;
	mac->m_resources = aggr_m_resources;
	mac->m_ioctl = aggr_m_ioctl;

	/* set the initial group capabilities */
	aggr_grp_capab_set(grp);

	if ((err = mac_register(mac)) != 0)
		goto bail;

	/* set LACP mode */
	aggr_lacp_set_mode(grp, lacp_mode, lacp_timer);

	/*
	 * Attach each port if necessary.
	 */
	for (port = grp->lg_ports; port != NULL; port = port->lp_next)
		(void) aggr_port_notify_link(grp, port, B_FALSE);

	/* add new group to hash table */
	err = mod_hash_insert(aggr_grp_hash, GRP_HASH_KEY(key),
	    (mod_hash_val_t)grp);
	ASSERT(err == 0);
	aggr_grp_cnt++;

	rw_exit(&grp->lg_lock);
	AGGR_LACP_UNLOCK(grp);
	rw_exit(&aggr_grp_lock);
	return (0);

bail:
	if (grp != NULL) {
		aggr_port_t *cport;

		grp->lg_closing = B_TRUE;

		port = grp->lg_ports;
		while (port != NULL) {
			cport = port->lp_next;
			aggr_port_delete(port);
			port = cport;
		}

		rw_exit(&grp->lg_lock);
		AGGR_LACP_UNLOCK(grp);

		kmem_cache_free(aggr_grp_cache, grp);
	}

	rw_exit(&aggr_grp_lock);
	return (err);
}

/*
 * Return a pointer to the member of a group with specified device name
 * and port number.
 */
static aggr_port_t *
aggr_grp_port_lookup(aggr_grp_t *grp, const char *devname, uint32_t portnum)
{
	aggr_port_t *port;

	ASSERT(RW_WRITE_HELD(&grp->lg_lock) || RW_READ_HELD(&grp->lg_lock));

	for (port = grp->lg_ports; port != NULL; port = port->lp_next) {
		if ((strcmp(port->lp_devname, devname) == 0) &&
		    (port->lp_port == portnum))
			break;
	}

	return (port);
}

/*
 * Stop, detach and remove a port from a link aggregation group.
 */
static int
aggr_grp_rem_port(aggr_grp_t *grp, aggr_port_t *port,
    boolean_t *mac_addr_changedp, boolean_t *link_state_changedp)
{
	int rc = 0;
	aggr_port_t **pport;
	boolean_t mac_addr_changed = B_FALSE;
	boolean_t link_state_changed = B_FALSE;
	uint64_t val;
	uint_t i;

	ASSERT(AGGR_LACP_LOCK_HELD(grp));
	ASSERT(RW_WRITE_HELD(&grp->lg_lock));
	ASSERT(grp->lg_nports > 1);
	ASSERT(!grp->lg_closing);

	/* unlink port */
	for (pport = &grp->lg_ports; *pport != port;
	    pport = &(*pport)->lp_next) {
		if (*pport == NULL) {
			rc = ENOENT;
			goto done;
		}
	}
	*pport = port->lp_next;

	atomic_add_32(&port->lp_closing, 1);

	rw_enter(&port->lp_lock, RW_WRITER);

	/*
	 * If the MAC address of the port being removed was assigned
	 * to the group, update the group MAC address
	 * using the MAC address of a different port.
	 */
	if (!grp->lg_addr_fixed && grp->lg_mac_addr_port == port) {
		/*
		 * Set the MAC address of the group to the
		 * MAC address of its first port.
		 */
		bcopy(grp->lg_ports->lp_addr, grp->lg_addr, ETHERADDRL);
		grp->lg_mac_addr_port = grp->lg_ports;
		mac_addr_changed = B_TRUE;
	}

	link_state_changed = aggr_grp_detach_port(grp, port);

	/*
	 * Add the statistics of the ports while it was aggregated
	 * to the group's residual statistics.
	 */
	for (i = 0; i < MAC_NSTAT && !grp->lg_closing; i++) {
		/* avoid stats that are not counters */
		if (i == MAC_STAT_IFSPEED || i == MAC_STAT_LINK_DUPLEX)
			continue;

		/* get current value */
		val = aggr_port_stat(port, i);
		/* subtract value at the point of aggregation */
		val -= port->lp_stat[i];
		/* add to the residual stat */
		grp->lg_stat[i] += val;
	}

	grp->lg_nports--;

	rw_exit(&port->lp_lock);

	aggr_port_delete(port);

	/*
	 * If the group MAC address has changed, update the MAC address of
	 * the remaining consistuent ports according to the new MAC
	 * address of the group.
	 */
	if (mac_addr_changed)
		link_state_changed = link_state_changed ||
		    aggr_grp_update_ports_mac(grp);

done:
	if (mac_addr_changedp != NULL)
		*mac_addr_changedp = mac_addr_changed;
	if (link_state_changedp != NULL)
		*link_state_changedp = link_state_changed;

	return (rc);
}

/*
 * Remove one or more ports from an existing link aggregation group.
 */
int
aggr_grp_rem_ports(uint32_t key, uint_t nports, laioc_port_t *ports)
{
	int rc = 0, i;
	aggr_grp_t *grp = NULL;
	aggr_port_t *port;
	boolean_t mac_addr_update = B_FALSE, mac_addr_changed;
	boolean_t link_state_update = B_FALSE, link_state_changed;

	/* get group corresponding to key */
	rw_enter(&aggr_grp_lock, RW_READER);
	if (mod_hash_find(aggr_grp_hash, GRP_HASH_KEY(key),
	    (mod_hash_val_t *)&grp) != 0) {
		rw_exit(&aggr_grp_lock);
		return (ENOENT);
	}
	AGGR_GRP_REFHOLD(grp);
	rw_exit(&aggr_grp_lock);

	AGGR_LACP_LOCK(grp);
	rw_enter(&grp->lg_lock, RW_WRITER);

	/* we need to keep at least one port per group */
	if (nports >= grp->lg_nports) {
		rc = EINVAL;
		goto bail;
	}

	/* first verify that all the groups are valid */
	for (i = 0; i < nports; i++) {
		if (aggr_grp_port_lookup(grp, ports[i].lp_devname,
		    ports[i].lp_port) == NULL) {
			/* port not found */
			rc = ENOENT;
			goto bail;
		}
	}

	/* remove the specified ports from group */
	for (i = 0; i < nports && !grp->lg_closing; i++) {
		/* lookup port */
		port = aggr_grp_port_lookup(grp, ports[i].lp_devname,
		    ports[i].lp_port);
		ASSERT(port != NULL);

		/* stop port if group has already been started */
		if (grp->lg_started) {
			rw_enter(&port->lp_lock, RW_WRITER);
			aggr_port_stop(port);
			rw_exit(&port->lp_lock);
		}

		/* remove port from group */
		rc = aggr_grp_rem_port(grp, port, &mac_addr_changed,
		    &link_state_changed);
		ASSERT(rc == 0);
		mac_addr_update = mac_addr_update || mac_addr_changed;
		link_state_update = link_state_update || link_state_changed;
	}

bail:
	rw_exit(&grp->lg_lock);
	AGGR_LACP_UNLOCK(grp);
	if (mac_addr_update)
		mac_unicst_update(&grp->lg_mac, grp->lg_addr);
	if (link_state_update)
		mac_link_update(&grp->lg_mac, grp->lg_link_state);
	if (rc == 0)
		mac_resource_update(&grp->lg_mac);
	AGGR_GRP_REFRELE(grp);

	return (rc);
}

int
aggr_grp_delete(uint32_t key)
{
	aggr_grp_t *grp = NULL;
	aggr_port_t *port, *cport;
	mod_hash_val_t val;

	rw_enter(&aggr_grp_lock, RW_WRITER);

	if (mod_hash_find(aggr_grp_hash, GRP_HASH_KEY(key),
	    (mod_hash_val_t *)&grp) != 0) {
		rw_exit(&aggr_grp_lock);
		return (ENOENT);
	}

	AGGR_LACP_LOCK(grp);
	rw_enter(&grp->lg_lock, RW_WRITER);

	grp->lg_closing = B_TRUE;

	/*
	 * Unregister from the MAC service module. Since this can
	 * fail if a client hasn't closed the MAC port, we gracefully
	 * fail the operation.
	 */
	if (mac_unregister(&grp->lg_mac)) {
		rw_exit(&grp->lg_lock);
		AGGR_LACP_UNLOCK(grp);
		rw_exit(&aggr_grp_lock);
		return (EBUSY);
	}

	/* detach and free MAC ports associated with group */
	port = grp->lg_ports;
	while (port != NULL) {
		cport = port->lp_next;
		rw_enter(&port->lp_lock, RW_WRITER);
		if (grp->lg_started)
			aggr_port_stop(port);
		(void) aggr_grp_detach_port(grp, port);
		rw_exit(&port->lp_lock);
		aggr_port_delete(port);
		port = cport;
	}

	rw_exit(&grp->lg_lock);
	AGGR_LACP_UNLOCK(grp);

	(void) mod_hash_remove(aggr_grp_hash, GRP_HASH_KEY(key), &val);
	ASSERT(grp == (aggr_grp_t *)val);

	ASSERT(aggr_grp_cnt > 0);
	aggr_grp_cnt--;

	rw_exit(&aggr_grp_lock);
	AGGR_GRP_REFRELE(grp);

	return (0);
}

void
aggr_grp_free(aggr_grp_t *grp)
{
	ASSERT(grp->lg_refs == 0);
	kmem_cache_free(aggr_grp_cache, grp);
}

/*
 * Walker invoked when building the list of configured groups and
 * their ports that must be passed up to user-space.
 */

/*ARGSUSED*/
static uint_t
aggr_grp_info_walker(mod_hash_key_t key, mod_hash_val_t *val, void *arg)
{
	aggr_grp_t *grp;
	aggr_port_t *port;
	aggr_grp_info_state_t *state = arg;

	if (state->ls_rc != 0)
		return (MH_WALK_TERMINATE);	/* terminate walk */

	grp = (aggr_grp_t *)val;

	rw_enter(&grp->lg_lock, RW_READER);

	if (state->ls_group_key != 0 && grp->lg_key != state->ls_group_key)
		goto bail;

	state->ls_group_found = B_TRUE;

	state->ls_rc = state->ls_new_grp_fn(state->ls_fn_arg, grp->lg_key,
	    grp->lg_addr, grp->lg_addr_fixed, grp->lg_tx_policy,
	    grp->lg_nports, grp->lg_lacp_mode, grp->aggr.PeriodicTimer);

	if (state->ls_rc != 0)
		goto bail;

	for (port = grp->lg_ports; port != NULL; port = port->lp_next) {

		rw_enter(&port->lp_lock, RW_READER);

		state->ls_rc = state->ls_new_port_fn(state->ls_fn_arg,
		    port->lp_devname, port->lp_port, port->lp_addr,
		    port->lp_state, &port->lp_lacp.ActorOperPortState);

		rw_exit(&port->lp_lock);

		if (state->ls_rc != 0)
			goto bail;
	}

bail:
	rw_exit(&grp->lg_lock);
	return ((state->ls_rc == 0) ? MH_WALK_CONTINUE : MH_WALK_TERMINATE);
}

int
aggr_grp_info(uint_t *ngroups, uint32_t group_key, void *fn_arg,
    aggr_grp_info_new_grp_fn_t new_grp_fn,
    aggr_grp_info_new_port_fn_t new_port_fn)
{
	aggr_grp_info_state_t state;
	int rc = 0;

	rw_enter(&aggr_grp_lock, RW_READER);

	*ngroups = aggr_grp_cnt;

	bzero(&state, sizeof (state));
	state.ls_group_key = group_key;
	state.ls_new_grp_fn = new_grp_fn;
	state.ls_new_port_fn = new_port_fn;
	state.ls_fn_arg = fn_arg;

	mod_hash_walk(aggr_grp_hash, aggr_grp_info_walker, &state);

	if ((rc = state.ls_rc) == 0 && group_key != 0 &&
	    !state.ls_group_found)
		rc = ENOENT;

	rw_exit(&aggr_grp_lock);
	return (rc);
}

static void
aggr_m_resources(void *arg)
{
	aggr_grp_t *grp = arg;
	aggr_port_t *port;

	/* Call each port's m_resources function */
	for (port = grp->lg_ports; port != NULL; port = port->lp_next)
		mac_resources(port->lp_mh);
}

/*ARGSUSED*/
static void
aggr_m_ioctl(void *arg, queue_t *q, mblk_t *mp)
{
	miocnak(q, mp, 0, ENOTSUP);
}

static uint64_t
aggr_m_stat(void *arg, enum mac_stat stat)
{
	aggr_grp_t *grp = arg;
	aggr_port_t *port;
	uint64_t val;

	rw_enter(&grp->lg_lock, RW_READER);

	switch (stat) {
	case MAC_STAT_IFSPEED:
		val = grp->lg_ifspeed;
		break;
	case MAC_STAT_LINK_DUPLEX:
		val = grp->lg_link_duplex;
		break;
	default:
		/*
		 * The remaining statistics are counters. They are computed
		 * by aggregating the counters of the members MACs while they
		 * were aggregated, plus the residual counter of the group
		 * itself, which is updated each time a MAC is removed from
		 * the group.
		 */
		val = 0;
		for (port = grp->lg_ports; port != NULL; port = port->lp_next) {
			/* actual port statistic */
			val += aggr_port_stat(port, stat);
			/* minus the port stat when it was added */
			val -= port->lp_stat[stat];
			/* plus any residual amount for the group */
			val += grp->lg_stat[stat];
		}
	}

	rw_exit(&grp->lg_lock);
	return (val);
}

static int
aggr_m_start(void *arg)
{
	aggr_grp_t *grp = arg;
	aggr_port_t *port;

	AGGR_LACP_LOCK(grp);
	rw_enter(&grp->lg_lock, RW_WRITER);

	/*
	 * Attempts to start all configured members of the group.
	 * Group members will be attached when their link-up notification
	 * is received.
	 */
	for (port = grp->lg_ports; port != NULL; port = port->lp_next) {
		rw_enter(&port->lp_lock, RW_WRITER);
		if (aggr_port_start(port) != 0) {
			rw_exit(&port->lp_lock);
			continue;
		}

		/* set port promiscuous mode */
		if (aggr_port_promisc(port, grp->lg_promisc) != 0)
			aggr_port_stop(port);
		rw_exit(&port->lp_lock);
	}

	grp->lg_started = B_TRUE;

	rw_exit(&grp->lg_lock);
	AGGR_LACP_UNLOCK(grp);

	return (0);
}

static void
aggr_m_stop(void *arg)
{
	aggr_grp_t *grp = arg;
	aggr_port_t *port;

	rw_enter(&grp->lg_lock, RW_WRITER);

	for (port = grp->lg_ports; port != NULL; port = port->lp_next) {
		rw_enter(&port->lp_lock, RW_WRITER);
		aggr_port_stop(port);
		rw_exit(&port->lp_lock);
	}

	grp->lg_started = B_FALSE;

	rw_exit(&grp->lg_lock);
}

static int
aggr_m_promisc(void *arg, boolean_t on)
{
	aggr_grp_t *grp = arg;
	aggr_port_t *port;
	boolean_t link_state_changed = B_FALSE;

	AGGR_LACP_LOCK(grp);
	rw_enter(&grp->lg_lock, RW_WRITER);
	AGGR_GRP_REFHOLD(grp);

	ASSERT(!grp->lg_closing);

	if (on == grp->lg_promisc)
		goto bail;

	for (port = grp->lg_ports; port != NULL; port = port->lp_next) {
		rw_enter(&port->lp_lock, RW_WRITER);
		AGGR_PORT_REFHOLD(port);
		if (port->lp_started) {
			if (aggr_port_promisc(port, on) != 0) {
				link_state_changed = link_state_changed ||
				    aggr_grp_detach_port(grp, port);
			} else {
				/*
				 * If a port was detached because of a previous
				 * failure changing the promiscuity, the port
				 * is reattached when it successfully changes
				 * the promiscuity now, and this might cause
				 * the link state of the aggregation to change.
				 */
				link_state_changed = link_state_changed ||
				    aggr_grp_attach_port(grp, port);
			}
		}
		rw_exit(&port->lp_lock);
		AGGR_PORT_REFRELE(port);
	}

	grp->lg_promisc = on;

	if (link_state_changed)
		mac_link_update(&grp->lg_mac, grp->lg_link_state);

bail:
	rw_exit(&grp->lg_lock);
	AGGR_LACP_UNLOCK(grp);
	AGGR_GRP_REFRELE(grp);

	return (0);
}

/*
 * Add or remove the multicast addresses that are defined for the group
 * to or from the specified port.
 * This function is called before stopping a port, before a port
 * is detached from a group, and when attaching a port to a group.
 */
void
aggr_grp_multicst_port(aggr_port_t *port, boolean_t add)
{
	aggr_grp_t *grp = port->lp_grp;

	ASSERT(RW_WRITE_HELD(&port->lp_lock));
	ASSERT(RW_WRITE_HELD(&grp->lg_lock) || RW_READ_HELD(&grp->lg_lock));

	if (!port->lp_started)
		return;

	mac_multicst_refresh(&grp->lg_mac, aggr_port_multicst, port,
	    add);
}

static int
aggr_m_multicst(void *arg, boolean_t add, const uint8_t *addrp)
{
	aggr_grp_t *grp = arg;
	aggr_port_t *port = NULL;
	int err = 0, cerr;

	rw_enter(&grp->lg_lock, RW_WRITER);
	for (port = grp->lg_ports; port != NULL; port = port->lp_next) {
		if (port->lp_state != AGGR_PORT_STATE_ATTACHED)
			continue;
		cerr = aggr_port_multicst(port, add, addrp);
		if (cerr != 0 && err == 0)
			err = cerr;
	}
	rw_exit(&grp->lg_lock);
	return (err);
}

static int
aggr_m_unicst(void *arg, const uint8_t *macaddr)
{
	aggr_grp_t *grp = arg;
	int rc;

	AGGR_LACP_LOCK(grp);
	rw_enter(&grp->lg_lock, RW_WRITER);
	rc = aggr_grp_modify(0, grp, AGGR_MODIFY_MAC, 0, B_TRUE, macaddr,
	    0, 0);
	rw_exit(&grp->lg_lock);
	AGGR_LACP_UNLOCK(grp);

	return (rc);
}

/*
 * Initialize the capabilities that are advertised for the group
 * according to the capabilities of the constituent ports.
 */
static void
aggr_grp_capab_set(aggr_grp_t *grp)
{
	uint32_t cksum = (uint32_t)-1;
	uint32_t poll = DL_CAPAB_POLL;
	aggr_port_t *port;
	const mac_info_t *port_mi;

	ASSERT(RW_WRITE_HELD(&grp->lg_lock));

	ASSERT(grp->lg_ports != NULL);
	for (port = grp->lg_ports; port != NULL; port = port->lp_next) {
		port_mi = mac_info(port->lp_mh);
		cksum &= port_mi->mi_cksum;
		poll &= port_mi->mi_poll;
	}

	grp->lg_mac.m_info.mi_cksum = cksum;
	grp->lg_mac.m_info.mi_poll = poll;
}

/*
 * Checks whether the capabilities of the ports being added are compatible
 * with the current capabilities of the aggregation.
 */
static boolean_t
aggr_grp_capab_check(aggr_grp_t *grp, aggr_port_t *port)
{
	const mac_info_t *port_mi = mac_info(port->lp_mh);
	uint32_t grp_cksum = grp->lg_mac.m_info.mi_cksum;

	ASSERT(grp->lg_ports != NULL);

	return (((grp_cksum & port_mi->mi_cksum) == grp_cksum) &&
	    (grp->lg_mac.m_info.mi_poll == port_mi->mi_poll));
}
