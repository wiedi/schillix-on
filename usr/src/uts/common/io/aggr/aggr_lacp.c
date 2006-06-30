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
 * IEEE 802.3ad Link Aggregation - LACP & Marker Protocol processing.
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
#include <sys/byteorder.h>
#include <sys/strsun.h>
#include <sys/isa_defs.h>

#include <sys/aggr.h>
#include <sys/aggr_impl.h>

static struct ether_addr	etherzeroaddr = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*
 * Slow_Protocol_Multicast address, as per IEEE 802.3ad spec.
 */
static struct ether_addr   slow_multicast_addr = {
	0x01, 0x80, 0xc2, 0x00, 0x00, 0x02
};

#ifdef DEBUG
/* LACP state machine debugging support */
static uint32_t aggr_lacp_debug = 0;
#define	AGGR_LACP_DBG(x)	if (aggr_lacp_debug) { (void) printf x; }
#else
#define	AGGR_LACP_DBG(x)	{}
#endif /* DEBUG */

#define	NSECS_PER_SEC   1000000000ll

/* used by lacp_misconfig_walker() */
typedef struct lacp_misconfig_check_state_s {
	aggr_port_t *cs_portp;
	boolean_t cs_found;
} lacp_misconfig_check_state_t;

static const char *lacp_receive_str[] = LACP_RECEIVE_STATE_STRINGS;
static const char *lacp_periodic_str[] = LACP_PERIODIC_STRINGS;
static const char *lacp_mux_str[] = LACP_MUX_STRINGS;

static uint16_t lacp_port_priority = 0x1000;
static uint16_t lacp_system_priority = 0x1000;

/*
 * Maintains a list of all ports in ATTACHED state. This information
 * is used to detect misconfiguration.
 */
typedef struct lacp_sel_ports {
	uint16_t sp_key;
	char sp_devname[MAXNAMELEN + 1];
	struct ether_addr sp_partner_system;
	uint32_t sp_partner_key;
	struct lacp_sel_ports *sp_next;
} lacp_sel_ports_t;

static lacp_sel_ports_t *sel_ports = NULL;
static kmutex_t lacp_sel_lock;

static void periodic_timer_pop_locked(aggr_port_t *);
static void periodic_timer_pop(void *);
static void lacp_xmit_sm(aggr_port_t *);
static void lacp_periodic_sm(aggr_port_t *);
static void fill_lacp_pdu(aggr_port_t *, lacp_t *);
static void fill_lacp_ether(aggr_port_t *, struct ether_header *);
static void lacp_on(aggr_port_t *);
static void lacp_off(aggr_port_t *);
static boolean_t valid_lacp_pdu(aggr_port_t *, lacp_t *);
static void lacp_receive_sm(aggr_port_t *, lacp_t *);
static void aggr_set_coll_dist(aggr_port_t *, boolean_t);
static void aggr_set_coll_dist_locked(aggr_port_t *, boolean_t);
static void start_wait_while_timer(aggr_port_t *);
static void stop_wait_while_timer(aggr_port_t *);
static void lacp_reset_port(aggr_port_t *);
static void stop_current_while_timer(aggr_port_t *);
static void current_while_timer_pop(void *);
static void update_default_selected(aggr_port_t *);
static boolean_t update_selected(aggr_port_t *, lacp_t *);
static boolean_t lacp_sel_ports_add(aggr_port_t *);
static void lacp_sel_ports_del(aggr_port_t *);

void
aggr_lacp_init(void)
{
	mutex_init(&lacp_sel_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
aggr_lacp_fini(void)
{
	mutex_destroy(&lacp_sel_lock);
}

static int
inst_num(char *devname)
{
	int inst = 0;
	int fact = 1;
	char *p = &devname[strlen(devname)-1];

	while (*p >= '0' && *p <= '9' && p >= devname) {
		inst += (*p - '0') * fact;
		fact *= 10;
		p--;
	}

	return (inst);
}

/*
 * Set the port LACP state to SELECTED. Returns B_FALSE if the operation
 * could not be performed due to a memory allocation error, B_TRUE otherwise.
 */
static boolean_t
lacp_port_select(aggr_port_t *portp)
{
	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	if (!lacp_sel_ports_add(portp))
		return (B_FALSE);
	portp->lp_lacp.sm.selected = AGGR_SELECTED;
	return (B_TRUE);
}

/*
 * Set the port LACP state to UNSELECTED.
 */
static void
lacp_port_unselect(aggr_port_t *portp)
{
	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	lacp_sel_ports_del(portp);
	portp->lp_lacp.sm.selected = AGGR_UNSELECTED;
}

/*
 * Initialize group specific LACP state and parameters.
 */
void
aggr_lacp_init_grp(aggr_grp_t *aggrp)
{
	aggrp->aggr.PeriodicTimer = AGGR_LACP_TIMER_SHORT;
	aggrp->aggr.ActorSystemPriority = (uint16_t)lacp_system_priority;
	aggrp->aggr.CollectorMaxDelay = 10;
	aggrp->lg_lacp_mode = AGGR_LACP_OFF;
	aggrp->aggr.ready = B_FALSE;
}

/*
 * Complete LACP info initialization at port creation time.
 */
void
aggr_lacp_init_port(aggr_port_t *portp)
{
	aggr_grp_t *aggrp = portp->lp_grp;
	aggr_lacp_port_t *pl = &portp->lp_lacp;
	uint16_t offset;
	uint32_t instance;

	ASSERT(AGGR_LACP_LOCK_HELD(aggrp));
	ASSERT(RW_LOCK_HELD(&aggrp->lg_lock));
	ASSERT(RW_LOCK_HELD(&portp->lp_lock));

	/*
	 * Port numbers must be unique. For now, we encode the first two
	 * characters into the top byte of the port number. This will work
	 * with multiple types of NICs provided that the first two
	 * characters are unique.
	 */
	offset = ((portp->lp_devname[0] + portp->lp_devname[1]) << 8);
	instance = inst_num(portp->lp_devname);
	/* actor port # */
	pl->ActorPortNumber = offset + instance;
	AGGR_LACP_DBG(("aggr_lacp_init_port(%s): "
	    "ActorPortNumber = 0x%x\n", portp->lp_devname,
	    pl->ActorPortNumber));

	pl->ActorPortPriority = (uint16_t)lacp_port_priority;
	pl->ActorPortAggrId = 0;	/* aggregator id - not used */
	pl->NTT = B_FALSE;			/* need to transmit */

	pl->ActorAdminPortKey = aggrp->lg_key;
	pl->ActorOperPortKey = pl->ActorAdminPortKey;
	AGGR_LACP_DBG(("aggr_lacp_init_port(%s) "
	    "ActorAdminPortKey = 0x%x, ActorAdminPortKey = 0x%x\n",
	    portp->lp_devname, pl->ActorAdminPortKey, pl->ActorOperPortKey));

	/* Actor admin. port state */
	pl->ActorAdminPortState.bit.activity = B_FALSE;
	pl->ActorAdminPortState.bit.timeout = B_TRUE;
	pl->ActorAdminPortState.bit.aggregation = B_TRUE;
	pl->ActorAdminPortState.bit.sync = B_FALSE;
	pl->ActorAdminPortState.bit.collecting = B_FALSE;
	pl->ActorAdminPortState.bit.distributing = B_FALSE;
	pl->ActorAdminPortState.bit.defaulted = B_FALSE;
	pl->ActorAdminPortState.bit.expired = B_FALSE;
	pl->ActorOperPortState = pl->ActorAdminPortState;

	/*
	 * Partner Administrative Information
	 * (All initialized to zero except for the following)
	 * Fast Timeouts.
	 */
	pl->PartnerAdminPortState.bit.timeout =
	    pl->PartnerOperPortState.bit.timeout = B_TRUE;

	pl->PartnerCollectorMaxDelay = 0; /* tens of microseconds */

	/*
	 * State machine information.
	 */
	pl->sm.lacp_on = B_FALSE;		/* LACP Off default */
	pl->sm.begin = B_TRUE;		/* Prevents transmissions */
	pl->sm.lacp_enabled = B_FALSE;
	pl->sm.port_enabled = B_FALSE;		/* Link Down */
	pl->sm.actor_churn = B_FALSE;
	pl->sm.partner_churn = B_FALSE;
	pl->sm.ready_n = B_FALSE;
	pl->sm.port_moved = B_FALSE;

	lacp_port_unselect(portp);

	pl->sm.periodic_state = LACP_NO_PERIODIC;
	pl->sm.receive_state = LACP_INITIALIZE;
	pl->sm.mux_state = LACP_DETACHED;
	pl->sm.churn_state = LACP_NO_ACTOR_CHURN;

	/*
	 * Timer information.
	 */
	pl->current_while_timer.id = 0;
	pl->current_while_timer.val = SHORT_TIMEOUT_TIME;

	pl->periodic_timer.id = 0;
	pl->periodic_timer.val = FAST_PERIODIC_TIME;

	pl->wait_while_timer.id = 0;
	pl->wait_while_timer.val = AGGREGATE_WAIT_TIME;
}

/*
 * Port initialization when we need to
 * turn LACP on/off, etc. Not everything is
 * reset like in the above routine.
 *		Do NOT modify things like link status.
 */
static void
lacp_reset_port(aggr_port_t *portp)
{
	aggr_lacp_port_t *pl = &portp->lp_lacp;

	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	pl->NTT = B_FALSE;			/* need to transmit */

	/* reset operational port state */
	pl->ActorOperPortState.bit.timeout =
		pl->ActorAdminPortState.bit.timeout;

	pl->ActorOperPortState.bit.sync = B_FALSE;
	pl->ActorOperPortState.bit.collecting = B_FALSE;
	pl->ActorOperPortState.bit.distributing = B_FALSE;
	pl->ActorOperPortState.bit.defaulted = B_TRUE;
	pl->ActorOperPortState.bit.expired = B_FALSE;

	pl->PartnerOperPortState.bit.timeout = B_TRUE;	/* fast t/o */
	pl->PartnerCollectorMaxDelay = 0; /* tens of microseconds */

	/*
	 * State machine information.
	 */
	pl->sm.begin = B_TRUE;		/* Prevents transmissions */
	pl->sm.actor_churn = B_FALSE;
	pl->sm.partner_churn = B_FALSE;
	pl->sm.ready_n = B_FALSE;

	lacp_port_unselect(portp);

	pl->sm.periodic_state = LACP_NO_PERIODIC;
	pl->sm.receive_state = LACP_INITIALIZE;
	pl->sm.mux_state = LACP_DETACHED;
	pl->sm.churn_state = LACP_NO_ACTOR_CHURN;

	/*
	 * Timer information.
	 */
	pl->current_while_timer.val = SHORT_TIMEOUT_TIME;
	pl->periodic_timer.val = FAST_PERIODIC_TIME;
}

static void
aggr_lacp_mcast_on(aggr_port_t *port)
{
	ASSERT(AGGR_LACP_LOCK_HELD(port->lp_grp));
	ASSERT(RW_WRITE_HELD(&port->lp_lock));

	if (port->lp_state != AGGR_PORT_STATE_ATTACHED)
		return;

	(void) aggr_port_multicst(port, B_TRUE,
	    (uchar_t *)&slow_multicast_addr);
}

static void
aggr_lacp_mcast_off(aggr_port_t *port)
{
	ASSERT(AGGR_LACP_LOCK_HELD(port->lp_grp));
	ASSERT(RW_WRITE_HELD(&port->lp_lock));

	if (port->lp_state != AGGR_PORT_STATE_ATTACHED)
		return;

	(void) aggr_port_multicst(port, B_FALSE,
	    (uchar_t *)&slow_multicast_addr);
}

static void
start_periodic_timer(aggr_port_t *portp)
{
	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	if (portp->lp_lacp.periodic_timer.id == 0) {
		portp->lp_lacp.periodic_timer.id =
		    timeout(periodic_timer_pop, portp,
		    drv_usectohz(1000000 * portp->lp_lacp.periodic_timer.val));
	}
}

static void
stop_periodic_timer(aggr_port_t *portp)
{
	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	if (portp->lp_lacp.periodic_timer.id != 0) {
		AGGR_LACP_UNLOCK(portp->lp_grp);
		(void) untimeout(portp->lp_lacp.periodic_timer.id);
		AGGR_LACP_LOCK(portp->lp_grp);
		portp->lp_lacp.periodic_timer.id = 0;
	}
}

/*
 * When the timer pops, we arrive here to
 * clear out LACPDU count as well as transmit an
 * LACPDU. We then set the periodic state and let
 * the periodic state machine restart the timer.
 */

static void
periodic_timer_pop_locked(aggr_port_t *portp)
{
	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	portp->lp_lacp.periodic_timer.id = NULL;
	portp->lp_lacp_stats.LACPDUsTx = 0;

	/* current timestamp */
	portp->lp_lacp.time = gethrtime();
	portp->lp_lacp.NTT = B_TRUE;
	lacp_xmit_sm(portp);

	/*
	 * Set Periodic State machine state based on the
	 * value of the Partner Operation Port State timeout
	 * bit.
	 */
	if (portp->lp_lacp.PartnerOperPortState.bit.timeout) {
		portp->lp_lacp.periodic_timer.val = FAST_PERIODIC_TIME;
		portp->lp_lacp.sm.periodic_state = LACP_FAST_PERIODIC;
	} else {
		portp->lp_lacp.periodic_timer.val = SLOW_PERIODIC_TIME;
		portp->lp_lacp.sm.periodic_state = LACP_SLOW_PERIODIC;
	}

	lacp_periodic_sm(portp);
}

static void
periodic_timer_pop(void *data)
{
	aggr_port_t *portp = data;

	if (portp->lp_closing)
		return;

	AGGR_LACP_LOCK(portp->lp_grp);
	periodic_timer_pop_locked(portp);
	AGGR_LACP_UNLOCK(portp->lp_grp);
}

/*
 * Invoked from:
 *	- startup upon aggregation
 *	- when the periodic timer pops
 *	- when the periodic timer value is changed
 *	- when the port is attached or detached
 *	- when LACP mode is changed.
 */
static void
lacp_periodic_sm(aggr_port_t *portp)
{
	lacp_periodic_state_t oldstate = portp->lp_lacp.sm.periodic_state;
	aggr_lacp_port_t *pl = &portp->lp_lacp;

	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	/* LACP_OFF state not in specification so check here.  */
	if (!pl->sm.lacp_on) {
		/* Stop timer whether it is running or not */
		stop_periodic_timer(portp);
		pl->sm.periodic_state = LACP_NO_PERIODIC;
		pl->NTT = B_FALSE;
		AGGR_LACP_DBG(("lacp_periodic_sm(%s):NO LACP "
		    "%s--->%s\n", portp->lp_devname,
		    lacp_periodic_str[oldstate],
		    lacp_periodic_str[pl->sm.periodic_state]));
		return;
	}

	if (pl->sm.begin || !pl->sm.lacp_enabled ||
	    !pl->sm.port_enabled ||
	    !pl->ActorOperPortState.bit.activity &&
	    !pl->PartnerOperPortState.bit.activity) {

		/* Stop timer whether it is running or not */
		stop_periodic_timer(portp);
		pl->sm.periodic_state = LACP_NO_PERIODIC;
		pl->NTT = B_FALSE;
		AGGR_LACP_DBG(("lacp_periodic_sm(%s):STOP %s--->%s\n",
		    portp->lp_devname, lacp_periodic_str[oldstate],
		    lacp_periodic_str[pl->sm.periodic_state]));
		return;
	}

	/*
	 * Startup with FAST_PERIODIC_TIME if no previous LACPDU
	 * has been received. Then after we timeout, then it is
	 * possible to go to SLOW_PERIODIC_TIME.
	 */
	if (pl->sm.periodic_state == LACP_NO_PERIODIC) {
		pl->periodic_timer.val = FAST_PERIODIC_TIME;
		pl->sm.periodic_state = LACP_FAST_PERIODIC;
	} else if ((pl->sm.periodic_state == LACP_SLOW_PERIODIC) &&
	    pl->PartnerOperPortState.bit.timeout) {
		/*
		 * If we receive a bit indicating we are going to
		 * fast periodic from slow periodic, stop the timer
		 * and let the periodic_timer_pop routine deal
		 * with reseting the periodic state and transmitting
		 * a LACPDU.
		 */
		stop_periodic_timer(portp);
		periodic_timer_pop_locked(portp);
	}

	/* Rearm timer with value provided by partner */
	start_periodic_timer(portp);
}

/*
 * This routine transmits an LACPDU if lacp_enabled
 * is TRUE and if NTT is set.
 */
static void
lacp_xmit_sm(aggr_port_t *portp)
{
	aggr_lacp_port_t *pl = &portp->lp_lacp;
	size_t	len;
	mblk_t  *mp;
	hrtime_t now, elapsed;
	const mac_txinfo_t *mtp;

	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	/* LACP_OFF state not in specification so check here.  */
	if (!pl->sm.lacp_on || !pl->NTT || !portp->lp_started)
		return;

	/*
	 * Do nothing if LACP has been turned off or if the
	 * periodic state machine is not enabled.
	 */
	if ((pl->sm.periodic_state == LACP_NO_PERIODIC) ||
	    !pl->sm.lacp_enabled || pl->sm.begin) {
		pl->NTT = B_FALSE;
		return;
	}

	/*
	 * If we have sent 5 Slow packets in the last second, avoid
	 * sending any more here. No more than three LACPDUs may be transmitted
	 * in any Fast_Periodic_Time interval.
	 */
	if (portp->lp_lacp_stats.LACPDUsTx >= 3) {
		/*
		 * Grab the current time value and see if
		 * more than 1 second has passed. If so,
		 * reset the timestamp and clear the count.
		 */
		now = gethrtime();
		elapsed = now - pl->time;
		if (elapsed > NSECS_PER_SEC) {
			portp->lp_lacp_stats.LACPDUsTx = 0;
			pl->time = now;
		} else {
			return;
		}
	}

	len = sizeof (lacp_t) + sizeof (struct ether_header);
	mp = allocb(len, BPRI_MED);
	if (mp == NULL)
		return;

	mp->b_wptr = mp->b_rptr + len;
	bzero(mp->b_rptr, len);

	fill_lacp_ether(portp, (struct ether_header *)mp->b_rptr);
	fill_lacp_pdu(portp,
	    (lacp_t *)(mp->b_rptr + sizeof (struct ether_header)));

	/*
	 * Store the transmit info pointer locally in case it changes between
	 * loading mt_fn and mt_arg.
	 */
	mtp = portp->lp_txinfo;
	mtp->mt_fn(mtp->mt_arg, mp);

	pl->NTT = B_FALSE;
	portp->lp_lacp_stats.LACPDUsTx++;
}

/*
 * Initialize the ethernet header of a LACP packet sent from the specified
 * port.
 */
static void
fill_lacp_ether(aggr_port_t *port, struct ether_header *ether)
{
	bcopy(port->lp_addr, (uint8_t *)&(ether->ether_shost), ETHERADDRL);
	bcopy(&slow_multicast_addr, (uint8_t *)&(ether->ether_dhost),
	    ETHERADDRL);
	ether->ether_type = htons(ETHERTYPE_SLOW);
}

static void
fill_lacp_pdu(aggr_port_t *portp, lacp_t *lacp)
{
	aggr_lacp_port_t *pl = &portp->lp_lacp;
	aggr_grp_t *aggrp = portp->lp_grp;

	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	lacp->subtype = LACP_SUBTYPE;
	lacp->version = LACP_VERSION;

	rw_enter(&aggrp->lg_lock, RW_READER);
	rw_enter(&portp->lp_lock, RW_READER);

	/*
	 * Actor Information
	 */
	lacp->actor_info.tlv_type = ACTOR_TLV;
	lacp->actor_info.information_len = sizeof (link_info_t);
	lacp->actor_info.system_priority =
	    htons(aggrp->aggr.ActorSystemPriority);
	bcopy(aggrp->lg_addr, (uchar_t *)&lacp->actor_info.system_id,
	    ETHERADDRL);
	lacp->actor_info.key = htons(pl->ActorOperPortKey);
	lacp->actor_info.port_priority = htons(pl->ActorPortPriority);
	lacp->actor_info.port = htons(pl->ActorPortNumber);
	lacp->actor_info.state.state = pl->ActorOperPortState.state;

	/*
	 * Partner Information
	 */
	lacp->partner_info.tlv_type = PARTNER_TLV;
	lacp->partner_info.information_len = sizeof (link_info_t);
	lacp->partner_info.system_priority =
	    htons(pl->PartnerOperSysPriority);
	lacp->partner_info.system_id = pl->PartnerOperSystem;
	lacp->partner_info.key = htons(pl->PartnerOperKey);
	lacp->partner_info.port_priority =
	    htons(pl->PartnerOperPortPriority);
	lacp->partner_info.port = htons(pl->PartnerOperPortNum);
	lacp->partner_info.state.state = pl->PartnerOperPortState.state;

	/* Collector Information */
	lacp->tlv_collector = COLLECTOR_TLV;
	lacp->collector_len = 0x10;
	lacp->collector_max_delay = htons(aggrp->aggr.CollectorMaxDelay);

	/* Termination Information */
	lacp->tlv_terminator = TERMINATOR_TLV;
	lacp->terminator_len = 0x0;

	rw_exit(&portp->lp_lock);
	rw_exit(&aggrp->lg_lock);
}

/*
 * lacp_mux_sm - LACP mux state machine
 *		This state machine is invoked from:
 *			- startup upon aggregation
 *			- from the Selection logic
 *			- when the wait_while_timer pops
 *			- when the aggregation MAC address is changed
 *			- when receiving DL_NOTE_LINK_UP/DOWN
 *			- when receiving DL_NOTE_AGGR_AVAIL/UNAVAIL
 *			- when LACP mode is changed.
 *			- when a DL_NOTE_SPEED is received
 */
static void
lacp_mux_sm(aggr_port_t *portp)
{
	aggr_grp_t *aggrp = portp->lp_grp;
	boolean_t NTT_updated = B_FALSE;
	aggr_lacp_port_t *pl = &portp->lp_lacp;
	lacp_mux_state_t oldstate = pl->sm.mux_state;

	ASSERT(AGGR_LACP_LOCK_HELD(aggrp));

	/* LACP_OFF state not in specification so check here.  */
	if (!pl->sm.lacp_on) {
		pl->sm.mux_state = LACP_DETACHED;
		pl->ActorOperPortState.bit.sync = B_FALSE;

		if (pl->ActorOperPortState.bit.collecting ||
		    pl->ActorOperPortState.bit.distributing) {
			AGGR_LACP_DBG(("trunk link: (%s): "
			    "Collector_Distributor Disabled.\n",
			    portp->lp_devname));
		}

		pl->ActorOperPortState.bit.collecting =
		    pl->ActorOperPortState.bit.distributing = B_FALSE;
		return;
	}

	if (pl->sm.begin || !pl->sm.lacp_enabled)
		pl->sm.mux_state = LACP_DETACHED;

again:
	/* determine next state, or return if state unchanged */
	switch (pl->sm.mux_state) {
	case LACP_DETACHED:
		if (pl->sm.begin) {
			break;
		}

		if ((pl->sm.selected == AGGR_SELECTED) ||
		    (pl->sm.selected == AGGR_STANDBY)) {
			pl->sm.mux_state = LACP_WAITING;
			break;
		}
		return;

	case LACP_WAITING:
		if (pl->sm.selected == AGGR_UNSELECTED) {
			pl->sm.mux_state = LACP_DETACHED;
			break;
		}

		if ((pl->sm.selected == AGGR_SELECTED) && aggrp->aggr.ready) {
			pl->sm.mux_state = LACP_ATTACHED;
			break;
		}
		return;

	case LACP_ATTACHED:
		if ((pl->sm.selected == AGGR_UNSELECTED) ||
		    (pl->sm.selected == AGGR_STANDBY)) {
			pl->sm.mux_state = LACP_DETACHED;
			break;
		}

		if ((pl->sm.selected == AGGR_SELECTED) &&
		    pl->PartnerOperPortState.bit.sync) {
			pl->sm.mux_state = LACP_COLLECTING_DISTRIBUTING;
			break;
		}
		return;

	case LACP_COLLECTING_DISTRIBUTING:
		if ((pl->sm.selected == AGGR_UNSELECTED) ||
		    (pl->sm.selected == AGGR_STANDBY) ||
		    !pl->PartnerOperPortState.bit.sync) {
			pl->sm.mux_state = LACP_ATTACHED;
			break;
		}
		return;
	}

	AGGR_LACP_DBG(("lacp_mux_sm(%s):%s--->%s\n",
	    portp->lp_devname, lacp_mux_str[oldstate],
	    lacp_mux_str[pl->sm.mux_state]));

	/* perform actions on entering a new state */
	switch (pl->sm.mux_state) {
	case LACP_DETACHED:
		if (pl->ActorOperPortState.bit.collecting ||
		    pl->ActorOperPortState.bit.distributing) {
			AGGR_LACP_DBG(("trunk link: (%s): "
			    "Collector_Distributor Disabled.\n",
			    portp->lp_devname));
		}

		pl->ActorOperPortState.bit.sync =
		    pl->ActorOperPortState.bit.collecting = B_FALSE;

		/* Turn OFF Collector_Distributor */
		aggr_set_coll_dist(portp, B_FALSE);

		pl->ActorOperPortState.bit.distributing = B_FALSE;
		NTT_updated = B_TRUE;
		break;

	case LACP_WAITING:
		start_wait_while_timer(portp);
		break;

	case LACP_ATTACHED:
		if (pl->ActorOperPortState.bit.collecting ||
		    pl->ActorOperPortState.bit.distributing) {
			AGGR_LACP_DBG(("trunk link: (%s): "
			    "Collector_Distributor Disabled.\n",
			    portp->lp_devname));
		}

		pl->ActorOperPortState.bit.sync = B_TRUE;
		pl->ActorOperPortState.bit.collecting = B_FALSE;

		/* Turn OFF Collector_Distributor */
		aggr_set_coll_dist(portp, B_FALSE);

		pl->ActorOperPortState.bit.distributing = B_FALSE;
		NTT_updated = B_TRUE;
		if (pl->PartnerOperPortState.bit.sync) {
			/*
			 * We had already received an updated sync from
			 * the partner. Attempt to transition to
			 * collecting/distributing now.
			 */
			goto again;
		}
		break;

	case LACP_COLLECTING_DISTRIBUTING:
		if (!pl->ActorOperPortState.bit.collecting &&
		    !pl->ActorOperPortState.bit.distributing) {
			AGGR_LACP_DBG(("trunk link: (%s): "
			    "Collector_Distributor Enabled.\n",
			    portp->lp_devname));
		}
		pl->ActorOperPortState.bit.distributing = B_TRUE;

		/* Turn Collector_Distributor back ON */
		aggr_set_coll_dist(portp, B_TRUE);

		pl->ActorOperPortState.bit.collecting = B_TRUE;
		NTT_updated = B_TRUE;
		break;
	}

	/*
	 * If we updated the state of the NTT variable, then
	 * initiate a LACPDU transmission.
	 */
	if (NTT_updated) {
		pl->NTT = B_TRUE;
		lacp_xmit_sm(portp);
	}
} /* lacp_mux_sm */


static void
receive_marker_pdu(aggr_port_t *portp, mblk_t *mp)
{
	marker_pdu_t		*markerp = (marker_pdu_t *)mp->b_rptr;
	const mac_txinfo_t	*mtp;

	AGGR_LACP_LOCK(portp->lp_grp);

	AGGR_LACP_DBG(("trunk link: (%s): MARKER PDU received:\n",
	    portp->lp_devname));

	/* LACP_OFF state not in specification so check here.  */
	if (!portp->lp_lacp.sm.lacp_on)
		goto bail;

	if (MBLKL(mp) < sizeof (marker_pdu_t))
		goto bail;

	if (markerp->version != MARKER_VERSION) {
		AGGR_LACP_DBG(("trunk link (%s): Malformed MARKER PDU: "
		    "version = %d does not match s/w version %d\n",
		    portp->lp_devname, markerp->version, MARKER_VERSION));
		goto bail;
	}

	if (markerp->tlv_marker == MARKER_RESPONSE_TLV) {
		/* We do not yet send out MARKER info PDUs */
		AGGR_LACP_DBG(("trunk link (%s): MARKER RESPONSE PDU: "
		    " MARKER TLV = %d - We don't send out info type!\n",
		    portp->lp_devname, markerp->tlv_marker));
		goto bail;
	}

	if (markerp->tlv_marker != MARKER_INFO_TLV) {
		AGGR_LACP_DBG(("trunk link (%s): Malformed MARKER PDU: "
		    " MARKER TLV = %d \n", portp->lp_devname,
		    markerp->tlv_marker));
		goto bail;
	}

	if (markerp->marker_len != MARKER_INFO_RESPONSE_LENGTH) {
		AGGR_LACP_DBG(("trunk link (%s): Malformed MARKER PDU: "
		    " MARKER length = %d \n", portp->lp_devname,
		    markerp->marker_len));
		goto bail;
	}

	if (markerp->requestor_port != portp->lp_lacp.PartnerOperPortNum) {
		AGGR_LACP_DBG(("trunk link (%s): MARKER PDU: "
		    " MARKER Port %d not equal to Partner port %d\n",
		    portp->lp_devname, markerp->requestor_port,
		    portp->lp_lacp.PartnerOperPortNum));
		goto bail;
	}

	if (ether_cmp(&markerp->system_id,
	    &portp->lp_lacp.PartnerOperSystem) != 0) {
		AGGR_LACP_DBG(("trunk link (%s): MARKER PDU: "
		    " MARKER MAC not equal to Partner MAC\n",
		    portp->lp_devname));
		goto bail;
	}

	/*
	 * Turn into Marker Response PDU
	 * and return mblk to sending system
	 */
	markerp->tlv_marker = MARKER_RESPONSE_TLV;

	/* reuse the space that was used by received ethernet header */
	ASSERT(MBLKHEAD(mp) >= sizeof (struct ether_header));
	mp->b_rptr -= sizeof (struct ether_header);
	fill_lacp_ether(portp, (struct ether_header *)mp->b_rptr);
	AGGR_LACP_UNLOCK(portp->lp_grp);

	/*
	 * Store the transmit info pointer locally in case it changes between
	 * loading mt_fn and mt_arg.
	 */
	mtp = portp->lp_txinfo;
	mtp->mt_fn(mtp->mt_arg, mp);
	return;

bail:
	AGGR_LACP_UNLOCK(portp->lp_grp);
	freemsg(mp);
}


/*
 * Update the LACP mode (off, active, or passive) of the specified group.
 */
void
aggr_lacp_update_mode(aggr_grp_t *grp, aggr_lacp_mode_t mode)
{
	aggr_lacp_mode_t old_mode = grp->lg_lacp_mode;
	aggr_port_t *port;

	ASSERT(AGGR_LACP_LOCK_HELD(grp));
	ASSERT(RW_WRITE_HELD(&grp->lg_lock));

	if (mode == old_mode)
		return;

	grp->lg_lacp_mode = mode;

	for (port = grp->lg_ports; port != NULL; port = port->lp_next) {
		port->lp_lacp.ActorAdminPortState.bit.activity =
		    port->lp_lacp.ActorOperPortState.bit.activity =
		    (mode == AGGR_LACP_ACTIVE);

		if (old_mode == AGGR_LACP_OFF) {
			/* OFF -> {PASSIVE,ACTIVE} */
			/* turn OFF Collector_Distributor */
			aggr_set_coll_dist(port, B_FALSE);
			rw_enter(&port->lp_lock, RW_WRITER);
			lacp_on(port);
			if (port->lp_state == AGGR_PORT_STATE_ATTACHED)
				aggr_lacp_port_attached(port);
			rw_exit(&port->lp_lock);
		} else if (mode == AGGR_LACP_OFF) {
			/* {PASSIVE,ACTIVE} -> OFF */
			rw_enter(&port->lp_lock, RW_WRITER);
			lacp_off(port);
			rw_exit(&port->lp_lock);
			if (!grp->lg_closing) {
				/* Turn ON Collector_Distributor */
				aggr_set_coll_dist(port, B_TRUE);
			}
		} else {
			/* PASSIVE->ACTIVE or ACTIVE->PASSIVE */
			port->lp_lacp.sm.begin = B_TRUE;
			lacp_mux_sm(port);
			lacp_periodic_sm(port);

			/* kick off state machines */
			lacp_receive_sm(port, NULL);
			lacp_mux_sm(port);
		}

		if (grp->lg_closing)
			break;
	}
}


/*
 * Update the LACP timer (short or long) of the specified group.
 */
void
aggr_lacp_update_timer(aggr_grp_t *grp, aggr_lacp_timer_t timer)
{
	aggr_port_t *port;

	ASSERT(AGGR_LACP_LOCK_HELD(grp));
	ASSERT(RW_WRITE_HELD(&grp->lg_lock));

	if (timer == grp->aggr.PeriodicTimer)
		return;

	grp->aggr.PeriodicTimer = timer;

	for (port = grp->lg_ports; port != NULL; port = port->lp_next) {
		port->lp_lacp.ActorAdminPortState.bit.timeout =
		    port->lp_lacp.ActorOperPortState.bit.timeout =
		    (timer == AGGR_LACP_TIMER_SHORT);
	}
}


/*
 * Sets the initial LACP mode (off, active, passive) and LACP timer
 * (short, long) of the specified group.
 */
void
aggr_lacp_set_mode(aggr_grp_t *grp, aggr_lacp_mode_t mode,
    aggr_lacp_timer_t timer)
{
	aggr_port_t *port;

	ASSERT(AGGR_LACP_LOCK_HELD(grp));
	ASSERT(RW_WRITE_HELD(&grp->lg_lock));

	grp->lg_lacp_mode = mode;
	grp->aggr.PeriodicTimer = timer;

	for (port = grp->lg_ports; port != NULL; port = port->lp_next) {
		port->lp_lacp.ActorAdminPortState.bit.activity =
		    port->lp_lacp.ActorOperPortState.bit.activity =
		    (mode == AGGR_LACP_ACTIVE);

		port->lp_lacp.ActorAdminPortState.bit.timeout =
			port->lp_lacp.ActorOperPortState.bit.timeout =
			(timer == AGGR_LACP_TIMER_SHORT);

		if (grp->lg_lacp_mode == AGGR_LACP_OFF) {
			/* Turn ON Collector_Distributor */
			aggr_set_coll_dist(port, B_TRUE);
		} else { /* LACP_ACTIVE/PASSIVE */
			rw_enter(&port->lp_lock, RW_WRITER);
			lacp_on(port);
			rw_exit(&port->lp_lock);
		}
	}
}

/*
 * Verify that the Partner MAC and Key recorded by the specified
 * port are not found in other ports that are not part of our
 * aggregation. Returns B_TRUE if such a port is found, B_FALSE
 * otherwise.
 */
static boolean_t
lacp_misconfig_check(aggr_port_t *portp)
{
	aggr_grp_t *grp = portp->lp_grp;
	lacp_sel_ports_t *cport;

	mutex_enter(&lacp_sel_lock);

	for (cport = sel_ports; cport != NULL; cport = cport->sp_next) {

		/* skip entries of the group of the port being checked */
		if (cport->sp_key == grp->lg_key)
			continue;

		if ((ether_cmp(&cport->sp_partner_system,
		    &grp->aggr.PartnerSystem) == 0) &&
		    (cport->sp_partner_key == grp->aggr.PartnerOperAggrKey)) {
			char mac_str[ETHERADDRL*3];
			struct ether_addr *mac = &cport->sp_partner_system;

			/*
			 * The Partner port information is already in use
			 * by ports in another aggregation so disable this
			 * port.
			 */

			(void) snprintf(mac_str, sizeof (mac_str),
			    "%x:%x:%x:%x:%x:%x",
			    mac->ether_addr_octet[0], mac->ether_addr_octet[1],
			    mac->ether_addr_octet[2], mac->ether_addr_octet[3],
			    mac->ether_addr_octet[4], mac->ether_addr_octet[5]);

			portp->lp_lacp.sm.selected = AGGR_UNSELECTED;
			cmn_err(CE_NOTE, "aggr key %d port %s: Port Partner "
			    "MAC %s and key %d in use on aggregation "
			    "key %d port %s\n", grp->lg_key,
			    portp->lp_devname, mac_str,
			    portp->lp_lacp.PartnerOperKey, cport->sp_key,
			    cport->sp_devname);
			break;
		}
	}

	mutex_exit(&lacp_sel_lock);
	return (cport != NULL);
}

/*
 * Remove the specified port from the list of selected ports.
 */
static void
lacp_sel_ports_del(aggr_port_t *portp)
{
	lacp_sel_ports_t *cport, **prev = NULL;

	mutex_enter(&lacp_sel_lock);

	prev = &sel_ports;
	for (cport = sel_ports; cport != NULL; prev = &cport->sp_next,
	    cport = cport->sp_next) {
		if (bcmp(portp->lp_devname, cport->sp_devname,
		    MAXNAMELEN + 1) == 0) {
			break;
		}
	}

	if (cport == NULL) {
		mutex_exit(&lacp_sel_lock);
		return;
	}

	*prev = cport->sp_next;
	kmem_free(cport, sizeof (*cport));

	mutex_exit(&lacp_sel_lock);
}

/*
 * Add the specified port to the list of selected ports. Returns B_FALSE
 * if the operation could not be performed due to an memory allocation
 * error.
 */
static boolean_t
lacp_sel_ports_add(aggr_port_t *portp)
{
	lacp_sel_ports_t *new_port;
	lacp_sel_ports_t *cport, **last;

	mutex_enter(&lacp_sel_lock);

	/* check if port is already in the list */
	last = &sel_ports;
	for (cport = sel_ports; cport != NULL;
	    last = &cport->sp_next, cport = cport->sp_next) {
		if (bcmp(portp->lp_devname, cport->sp_devname,
		    MAXNAMELEN + 1) == 0) {
			ASSERT(cport->sp_partner_key ==
			    portp->lp_lacp.PartnerOperKey);
			ASSERT(ether_cmp(&cport->sp_partner_system,
			    &portp->lp_lacp.PartnerOperSystem) == 0);

			mutex_exit(&lacp_sel_lock);
			return (B_TRUE);
		}
	}

	/* create and initialize new entry */
	new_port = kmem_zalloc(sizeof (lacp_sel_ports_t), KM_NOSLEEP);
	if (new_port == NULL) {
		mutex_exit(&lacp_sel_lock);
		return (B_FALSE);
	}

	new_port->sp_key = portp->lp_grp->lg_key;
	bcopy(&portp->lp_lacp.PartnerOperSystem,
	    &new_port->sp_partner_system, sizeof (new_port->sp_partner_system));
	new_port->sp_partner_key = portp->lp_lacp.PartnerOperKey;
	bcopy(portp->lp_devname, new_port->sp_devname, MAXNAMELEN + 1);

	*last = new_port;

	mutex_exit(&lacp_sel_lock);
	return (B_TRUE);
}

/*
 * lacp_selection_logic - LACP selection logic
 *		Sets the selected variable on a per port basis
 *		and sets Ready when all waiting ports are ready
 *		to go online.
 *
 * parameters:
 *      - portp - instance this applies to.
 *
 * invoked:
 *    - when initialization is needed
 *    - when UNSELECTED is set from the lacp_receive_sm() in LACP_CURRENT state
 *    - When the lacp_receive_sm goes to the LACP_DEFAULTED state
 *    - every time the wait_while_timer pops
 *    - everytime we turn LACP on/off
 */
static void
lacp_selection_logic(aggr_port_t *portp)
{
	aggr_port_t *tpp;
	aggr_grp_t *aggrp = portp->lp_grp;
	int ports_waiting;
	boolean_t reset_mac = B_FALSE;
	aggr_lacp_port_t *pl = &portp->lp_lacp;

	ASSERT(AGGR_LACP_LOCK_HELD(aggrp));

	/* LACP_OFF state not in specification so check here.  */
	if (!pl->sm.lacp_on) {
		lacp_port_unselect(portp);
		aggrp->aggr.ready = B_FALSE;
		lacp_mux_sm(portp);
		return;
	}

	if (pl->sm.begin || !pl->sm.lacp_enabled ||
	    (portp->lp_state != AGGR_PORT_STATE_ATTACHED)) {

		AGGR_LACP_DBG(("lacp_selection_logic:(%s): "
		    "selected %d-->%d (begin=%d, lacp_enabled = %d, "
		    "lp_state=%d)\n", portp->lp_devname, pl->sm.selected,
		    AGGR_UNSELECTED, pl->sm.begin, pl->sm.lacp_enabled,
		    portp->lp_state));

		lacp_port_unselect(portp);
		aggrp->aggr.ready = B_FALSE;
		lacp_mux_sm(portp);
		return;
	}

	/*
	 * If LACP is not enabled then selected is never set.
	 */
	if (!pl->sm.lacp_enabled) {
		AGGR_LACP_DBG(("lacp_selection_logic:(%s): selected %d-->%d\n",
		    portp->lp_devname, pl->sm.selected, AGGR_UNSELECTED));

		lacp_port_unselect(portp);
		lacp_mux_sm(portp);
		return;
	}

	/*
	 * Check if the Partner MAC or Key are zero. If so, we have
	 * not received any LACP info or it has expired and the
	 * receive machine is in the LACP_DEFAULTED state.
	 */
	if (ether_cmp(&pl->PartnerOperSystem, &etherzeroaddr) == 0 ||
	    (pl->PartnerOperKey == 0)) {

		for (tpp = aggrp->lg_ports; tpp; tpp = tpp->lp_next) {
			if (ether_cmp(&tpp->lp_lacp.PartnerOperSystem,
			    &etherzeroaddr) != 0 &&
			    (tpp->lp_lacp.PartnerOperKey != 0))
				break;
		}

		/*
		 * If all ports have no key or aggregation address,
		 * then clear the negotiated Partner MAC and key.
		 */
		if (tpp == NULL) {
			/* Clear the aggregation Partner MAC and key */
			aggrp->aggr.PartnerSystem = etherzeroaddr;
			aggrp->aggr.PartnerOperAggrKey = 0;
		}

		return;
	}

	/*
	 * Insure that at least one port in the aggregation
	 * matches the Partner aggregation MAC and key. If not,
	 * then clear the aggregation MAC and key. Later we will
	 * set the Partner aggregation MAC and key to that of the
	 * current port's Partner MAC and key.
	 */
	if (ether_cmp(&pl->PartnerOperSystem,
	    &aggrp->aggr.PartnerSystem) != 0 ||
	    (pl->PartnerOperKey != aggrp->aggr.PartnerOperAggrKey)) {

		for (tpp = aggrp->lg_ports; tpp; tpp = tpp->lp_next) {
			if (ether_cmp(&tpp->lp_lacp.PartnerOperSystem,
			    &aggrp->aggr.PartnerSystem) == 0 &&
			    (tpp->lp_lacp.PartnerOperKey ==
			    aggrp->aggr.PartnerOperAggrKey))
				break;
		}

		if (tpp == NULL) {
			/* Clear the aggregation Partner MAC and key */
			aggrp->aggr.PartnerSystem = etherzeroaddr;
			aggrp->aggr.PartnerOperAggrKey = 0;
			reset_mac = B_TRUE;
		}
	}

	/*
	 * If our Actor MAC is found in the Partner MAC
	 * on this port then we have a loopback misconfiguration.
	 */
	if (ether_cmp(&pl->PartnerOperSystem,
	    (struct ether_addr *)&aggrp->lg_addr) == 0) {
		cmn_err(CE_NOTE, "trunk link: (%s): Loopback condition.\n",
		    portp->lp_devname);

		lacp_port_unselect(portp);
		lacp_mux_sm(portp);
		return;
	}

	/*
	 * If our Partner MAC and Key are found on any other
	 * ports that are not in our aggregation, we have
	 * a misconfiguration.
	 */
	if (lacp_misconfig_check(portp)) {
		lacp_mux_sm(portp);
		return;
	}

	/*
	 * If the Aggregation Partner MAC and Key have not been
	 * set, then this is either the first port or the aggregation
	 * MAC and key have been reset. In either case we must set
	 * the values of the Partner MAC and key.
	 */
	if (ether_cmp(&aggrp->aggr.PartnerSystem, &etherzeroaddr) == 0 &&
	    (aggrp->aggr.PartnerOperAggrKey == 0)) {
		/* Set aggregation Partner MAC and key */
		aggrp->aggr.PartnerSystem = pl->PartnerOperSystem;
		aggrp->aggr.PartnerOperAggrKey = pl->PartnerOperKey;

		/*
		 * If we reset Partner aggregation MAC, then restart
		 * selection_logic on ports that match new MAC address.
		 */
		if (reset_mac) {
			for (tpp = aggrp->lg_ports; tpp; tpp =
			    tpp->lp_next) {
				if (tpp == portp)
					continue;
				if (ether_cmp(&tpp->lp_lacp.PartnerOperSystem,
				    &aggrp->aggr.PartnerSystem) == 0 &&
				    (tpp->lp_lacp.PartnerOperKey ==
				    aggrp->aggr.PartnerOperAggrKey))
					lacp_selection_logic(tpp);
			}
		}
	} else if (ether_cmp(&pl->PartnerOperSystem,
	    &aggrp->aggr.PartnerSystem) != 0 ||
	    (pl->PartnerOperKey != aggrp->aggr.PartnerOperAggrKey)) {
		/*
		 * The Partner port information does not match
		 * that of the other ports in the aggregation
		 * so disable this port.
		 */
		lacp_port_unselect(portp);

		cmn_err(CE_NOTE, "trunk link: (%s): Port Partner MAC or"
		    " key (%d) incompatible with Aggregation Partner "
		    "MAC or key (%d)\n",
		    portp->lp_devname, pl->PartnerOperKey,
		    aggrp->aggr.PartnerOperAggrKey);

		lacp_mux_sm(portp);
		return;
	}

	/* If we get to here, automatically set selected */
	if (pl->sm.selected != AGGR_SELECTED) {
		AGGR_LACP_DBG(("lacp_selection_logic:(%s): "
		    "selected %d-->%d\n", portp->lp_devname,
		    pl->sm.selected, AGGR_SELECTED));
		if (!lacp_port_select(portp))
			return;
		lacp_mux_sm(portp);
	}

	/*
	 * From this point onward we have selected the port
	 * and are simply checking if the Ready flag should
	 * be set.
	 */

	/*
	 * If at least two ports are waiting to aggregate
	 * and ready_n is set on all ports waiting to aggregate
	 * then set READY for the aggregation.
	 */

	ports_waiting = 0;

	if (!aggrp->aggr.ready) {
		/*
		 * If all ports in the aggregation have received compatible
		 * partner information and they match up correctly with the
		 * switch, there is no need to wait for all the
		 * wait_while_timers to pop.
		 */
		for (tpp = aggrp->lg_ports; tpp; tpp = tpp->lp_next) {
			if (((tpp->lp_lacp.sm.mux_state == LACP_WAITING) ||
			    tpp->lp_lacp.sm.begin) &&
			    !pl->PartnerOperPortState.bit.sync) {
				/* Add up ports uninitialized or waiting */
				ports_waiting++;
				if (!tpp->lp_lacp.sm.ready_n)
					return;
			}
		}
	}

	if (aggrp->aggr.ready) {
		AGGR_LACP_DBG(("lacp_selection_logic:(%s): "
		    "aggr.ready already set\n", portp->lp_devname));
		lacp_mux_sm(portp);
	} else {
		AGGR_LACP_DBG(("lacp_selection_logic:(%s): Ready %d-->%d\n",
		    portp->lp_devname, aggrp->aggr.ready, B_TRUE));
		aggrp->aggr.ready = B_TRUE;

		for (tpp = aggrp->lg_ports; tpp; tpp = tpp->lp_next)
			lacp_mux_sm(tpp);
	}

}

/*
 * wait_while_timer_pop - When the timer pops, we arrive here to
 *			set ready_n and trigger the selection logic.
 */
static void
wait_while_timer_pop(void *data)
{
	aggr_port_t *portp = data;

	if (portp->lp_closing)
		return;

	AGGR_LACP_LOCK(portp->lp_grp);

	AGGR_LACP_DBG(("trunk link:(%s): wait_while_timer pop \n",
	    portp->lp_devname));
	portp->lp_lacp.wait_while_timer.id = 0;
	portp->lp_lacp.sm.ready_n = B_TRUE;

	lacp_selection_logic(portp);
	AGGR_LACP_UNLOCK(portp->lp_grp);
}

static void
start_wait_while_timer(aggr_port_t *portp)
{
	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	if (portp->lp_lacp.wait_while_timer.id == 0) {
		portp->lp_lacp.wait_while_timer.id =
		    timeout(wait_while_timer_pop, portp,
		    drv_usectohz(1000000 *
		    portp->lp_lacp.wait_while_timer.val));
	}
}


static void
stop_wait_while_timer(portp)
aggr_port_t *portp;
{
	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	if (portp->lp_lacp.wait_while_timer.id != 0) {
		AGGR_LACP_UNLOCK(portp->lp_grp);
		(void) untimeout(portp->lp_lacp.wait_while_timer.id);
		AGGR_LACP_LOCK(portp->lp_grp);
		portp->lp_lacp.wait_while_timer.id = 0;
	}
}

/*
 * Invoked when a port has been attached to a group.
 * Complete the processing that couldn't be finished from lacp_on()
 * because the port was not started. We know that the link is full
 * duplex and ON, otherwise it wouldn't be attached.
 */
void
aggr_lacp_port_attached(aggr_port_t *portp)
{
	aggr_grp_t *grp = portp->lp_grp;
	aggr_lacp_port_t *pl = &portp->lp_lacp;

	ASSERT(AGGR_LACP_LOCK_HELD(grp));
	ASSERT(portp->lp_state == AGGR_PORT_STATE_ATTACHED);
	ASSERT(RW_WRITE_HELD(&portp->lp_lock));

	AGGR_LACP_DBG(("aggr_lacp_port_attached: port %s\n",
	    portp->lp_devname));

	portp->lp_lacp.sm.port_enabled = B_TRUE;	/* link on */

	if (grp->lg_lacp_mode == AGGR_LACP_OFF) {
		pl->ActorAdminPortState.bit.activity =
		    pl->ActorOperPortState.bit.activity = B_FALSE;

		/* Turn ON Collector_Distributor */
		aggr_set_coll_dist_locked(portp, B_TRUE);

		return;
	}

	pl->ActorAdminPortState.bit.activity =
	    pl->ActorOperPortState.bit.activity =
	    (grp->lg_lacp_mode == AGGR_LACP_ACTIVE);

	pl->ActorAdminPortState.bit.timeout =
	    pl->ActorOperPortState.bit.timeout =
	    (grp->aggr.PeriodicTimer == AGGR_LACP_TIMER_SHORT);

	pl->sm.lacp_enabled = B_TRUE;
	pl->ActorOperPortState.bit.aggregation = B_TRUE;
	pl->sm.begin = B_TRUE;

	if (!pl->sm.lacp_on) {
		/* Turn OFF Collector_Distributor */
		aggr_set_coll_dist_locked(portp, B_FALSE);

		lacp_on(portp);
	} else {
		lacp_receive_sm(portp, NULL);
		lacp_mux_sm(portp);

		/* Enable Multicast Slow Protocol address */
		aggr_lacp_mcast_on(portp);

		/* periodic_sm is started up from the receive machine */
		lacp_selection_logic(portp);
	}
}

/*
 * Invoked when a port has been detached from a group. Turn off
 * LACP processing if it was enabled.
 */
void
aggr_lacp_port_detached(aggr_port_t *portp)
{
	aggr_grp_t *grp = portp->lp_grp;

	ASSERT(AGGR_LACP_LOCK_HELD(grp));
	ASSERT(RW_WRITE_HELD(&portp->lp_lock));

	AGGR_LACP_DBG(("aggr_lacp_port_detached: port %s\n",
	    portp->lp_devname));

	portp->lp_lacp.sm.port_enabled = B_FALSE;

	if (grp->lg_lacp_mode == AGGR_LACP_OFF)
		return;

	/* Disable Slow Protocol PDUs */
	lacp_off(portp);
}


/*
 * Invoked after the outbound port selection policy has been changed.
 */
void
aggr_lacp_policy_changed(aggr_grp_t *grp)
{
	ASSERT(AGGR_LACP_LOCK_HELD(grp));
	ASSERT(RW_WRITE_HELD(&grp->lg_lock));

	/* suspend transmission for CollectorMaxDelay time */
	delay(grp->aggr.CollectorMaxDelay * 10);
}


/*
 * Enable Slow Protocol LACP and Marker PDUs.
 */
static void
lacp_on(aggr_port_t *portp)
{
	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));
	ASSERT(RW_WRITE_HELD(&portp->lp_grp->lg_lock));
	ASSERT(RW_WRITE_HELD(&portp->lp_lock));

	/*
	 * Reset the state machines and Partner operational
	 * information. Careful to not reset things like
	 * our link state.
	 */
	lacp_reset_port(portp);
	portp->lp_lacp.sm.lacp_on = B_TRUE;

	AGGR_LACP_DBG(("lacp_on:(%s): \n", portp->lp_devname));

	lacp_receive_sm(portp, NULL);
	lacp_mux_sm(portp);

	if (portp->lp_state != AGGR_PORT_STATE_ATTACHED)
		return;

	/* Enable Multicast Slow Protocol address */
	aggr_lacp_mcast_on(portp);

	/* periodic_sm is started up from the receive machine */
	lacp_selection_logic(portp);
} /* lacp_on */


/* Disable Slow Protocol LACP and Marker PDUs */
static void
lacp_off(aggr_port_t *portp)
{
	aggr_grp_t *grp = portp->lp_grp;

	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));
	ASSERT(RW_WRITE_HELD(&grp->lg_lock));
	ASSERT(RW_WRITE_HELD(&portp->lp_lock));

	portp->lp_lacp.sm.lacp_on = B_FALSE;

	AGGR_LACP_DBG(("lacp_off:(%s): \n", portp->lp_devname));

	/*
	 * Disable Slow Protocol Timers. We must temporarely release
	 * the group and port locks in order to avod deadlocks. Make
	 * sure that the port nor the group are closing after re-acquiring
	 * their locks.
	 */
	rw_exit(&portp->lp_lock);
	rw_exit(&grp->lg_lock);

	stop_periodic_timer(portp);
	stop_current_while_timer(portp);
	stop_wait_while_timer(portp);

	rw_enter(&grp->lg_lock, RW_WRITER);
	rw_enter(&portp->lp_lock, RW_WRITER);

	if (!portp->lp_closing && !grp->lg_closing) {
		lacp_mux_sm(portp);
		lacp_periodic_sm(portp);
		lacp_selection_logic(portp);
	}

	/* Turn OFF Collector_Distributor */
	aggr_set_coll_dist_locked(portp, B_FALSE);

	/* Disable Multicast Slow Protocol address */
	aggr_lacp_mcast_off(portp);

	lacp_reset_port(portp);
}


static boolean_t
valid_lacp_pdu(aggr_port_t *portp, lacp_t *lacp)
{
	/*
	 * 43.4.12 - "a Receive machine shall not validate
	 * the Version Number, TLV_type, or Reserved fields in received
	 * LACPDUs."
	 * ... "a Receive machine may validate the Actor_Information_Length,
	 * Partner_Information_Length, Collector_Information_Length,
	 * or Terminator_Length fields."
	 */
	if ((lacp->actor_info.information_len != sizeof (link_info_t)) ||
	    (lacp->partner_info.information_len != sizeof (link_info_t)) ||
	    (lacp->collector_len != LACP_COLLECTOR_INFO_LEN) ||
	    (lacp->terminator_len != LACP_TERMINATOR_INFO_LEN)) {
		AGGR_LACP_DBG(("trunk link (%s): Malformed LACPDU: "
		    " Terminator Length = %d \n", portp->lp_devname,
		    lacp->terminator_len));
		return (B_FALSE);
	}

	return (B_TRUE);
}


static void
start_current_while_timer(aggr_port_t *portp, uint_t time)
{
	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	if (portp->lp_lacp.current_while_timer.id == 0) {
		if (time > 0) {
			portp->lp_lacp.current_while_timer.val = time;
		} else if (portp->lp_lacp.ActorOperPortState.bit.timeout) {
			portp->lp_lacp.current_while_timer.val =
			    SHORT_TIMEOUT_TIME;
		} else {
			portp->lp_lacp.current_while_timer.val =
			    LONG_TIMEOUT_TIME;
		}

		portp->lp_lacp.current_while_timer.id =
		    timeout(current_while_timer_pop, portp,
		    drv_usectohz((clock_t)1000000 *
		    (clock_t)portp->lp_lacp.current_while_timer.val));
	}
}


static void
stop_current_while_timer(aggr_port_t *portp)
{
	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	if (portp->lp_lacp.current_while_timer.id != 0) {
		AGGR_LACP_UNLOCK(portp->lp_grp);
		(void) untimeout(portp->lp_lacp.current_while_timer.id);
		AGGR_LACP_LOCK(portp->lp_grp);
		portp->lp_lacp.current_while_timer.id = 0;
	}
}


static void
current_while_timer_pop(void *data)
{
	aggr_port_t *portp = (aggr_port_t *)data;

	if (portp->lp_closing)
		return;

	AGGR_LACP_LOCK(portp->lp_grp);

	AGGR_LACP_DBG(("trunk link:(%s): current_while_timer "
	    "pop id=%p\n", portp->lp_devname,
	    portp->lp_lacp.current_while_timer.id));

	portp->lp_lacp.current_while_timer.id = 0;
	lacp_receive_sm(portp, NULL);
	AGGR_LACP_UNLOCK(portp->lp_grp);
}


/*
 * record_Default - Simply copies over administrative values
 * to the partner operational values, and sets our state to indicate we
 * are using defaulted values.
 */
static void
record_Default(aggr_port_t *portp)
{
	aggr_lacp_port_t *pl = &portp->lp_lacp;

	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	pl->PartnerOperPortNum = pl->PartnerAdminPortNum;
	pl->PartnerOperPortPriority = pl->PartnerAdminPortPriority;
	pl->PartnerOperSystem = pl->PartnerAdminSystem;
	pl->PartnerOperSysPriority = pl->PartnerAdminSysPriority;
	pl->PartnerOperKey = pl->PartnerAdminKey;
	pl->PartnerOperPortState.state = pl->PartnerAdminPortState.state;

	pl->ActorOperPortState.bit.defaulted = B_TRUE;
}


/* Returns B_TRUE on sync value changing */
static boolean_t
record_PDU(aggr_port_t *portp, lacp_t *lacp)
{
	aggr_grp_t *aggrp = portp->lp_grp;
	aggr_lacp_port_t *pl = &portp->lp_lacp;
	uint8_t save_sync;

	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	/*
	 * Partner Information
	 */
	pl->PartnerOperPortNum = ntohs(lacp->actor_info.port);
	pl->PartnerOperPortPriority =
	    ntohs(lacp->actor_info.port_priority);
	pl->PartnerOperSystem = lacp->actor_info.system_id;
	pl->PartnerOperSysPriority =
	    htons(lacp->actor_info.system_priority);
	pl->PartnerOperKey = ntohs(lacp->actor_info.key);

	/* All state info except for Synchronization */
	save_sync = pl->PartnerOperPortState.bit.sync;
	pl->PartnerOperPortState.state = lacp->actor_info.state.state;

	/* Defaulted set to FALSE */
	pl->ActorOperPortState.bit.defaulted = B_FALSE;

	/*
	 * 43.4.9 - (Partner_Port, Partner_Port_Priority, Partner_system,
	 *		Partner_System_Priority, Partner_Key, and
	 *		Partner_State.Aggregation) are compared to the
	 *		corresponding operations paramters values for
	 *		the Actor. If these are equal, or if this is
	 *		an individual link, we are synchronized.
	 */
	if (((ntohs(lacp->partner_info.port) == pl->ActorPortNumber) &&
	    (ntohs(lacp->partner_info.port_priority) ==
	    pl->ActorPortPriority) &&
	    (ether_cmp(&lacp->partner_info.system_id,
		(struct ether_addr *)&aggrp->lg_addr) == 0) &&
	    (ntohs(lacp->partner_info.system_priority) ==
	    aggrp->aggr.ActorSystemPriority) &&
	    (ntohs(lacp->partner_info.key) == pl->ActorOperPortKey) &&
	    (lacp->partner_info.state.bit.aggregation ==
	    pl->ActorOperPortState.bit.aggregation)) ||
	    (!lacp->actor_info.state.bit.aggregation)) {

		pl->PartnerOperPortState.bit.sync =
		    lacp->actor_info.state.bit.sync;
	} else {
		pl->PartnerOperPortState.bit.sync = B_FALSE;
	}

	if (save_sync != pl->PartnerOperPortState.bit.sync) {
		AGGR_LACP_DBG(("record_PDU:(%s): partner sync "
		    "%d -->%d\n", portp->lp_devname, save_sync,
		    pl->PartnerOperPortState.bit.sync));
		return (B_TRUE);
	} else {
		return (B_FALSE);
	}
}


/*
 * update_selected - If any of the Partner parameters has
 *			changed from a previous value, then
 *			unselect the link from the aggregator.
 */
static boolean_t
update_selected(aggr_port_t *portp, lacp_t *lacp)
{
	aggr_lacp_port_t *pl = &portp->lp_lacp;

	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	if ((pl->PartnerOperPortNum != ntohs(lacp->actor_info.port)) ||
	    (pl->PartnerOperPortPriority !=
	    ntohs(lacp->actor_info.port_priority)) ||
	    (ether_cmp(&pl->PartnerOperSystem,
	    &lacp->actor_info.system_id) != 0) ||
	    (pl->PartnerOperSysPriority !=
	    ntohs(lacp->actor_info.system_priority)) ||
	    (pl->PartnerOperKey != ntohs(lacp->actor_info.key)) ||
	    (pl->PartnerOperPortState.bit.aggregation !=
	    lacp->actor_info.state.bit.aggregation)) {
		AGGR_LACP_DBG(("update_selected:(%s): "
		    "selected  %d-->%d\n", portp->lp_devname, pl->sm.selected,
		    AGGR_UNSELECTED));

		lacp_port_unselect(portp);
		return (B_TRUE);
	} else {
		return (B_FALSE);
	}
}


/*
 * update_default_selected - If any of the operational Partner parameters
 *			is different than that of the administrative values
 *			then unselect the link from the aggregator.
 */
static void
update_default_selected(aggr_port_t *portp)
{
	aggr_lacp_port_t *pl = &portp->lp_lacp;

	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	if ((pl->PartnerAdminPortNum != pl->PartnerOperPortNum) ||
	    (pl->PartnerOperPortPriority != pl->PartnerAdminPortPriority) ||
	    (ether_cmp(&pl->PartnerOperSystem, &pl->PartnerAdminSystem) != 0) ||
	    (pl->PartnerOperSysPriority != pl->PartnerAdminSysPriority) ||
	    (pl->PartnerOperKey != pl->PartnerAdminKey) ||
	    (pl->PartnerOperPortState.bit.aggregation !=
	    pl->PartnerAdminPortState.bit.aggregation)) {

		AGGR_LACP_DBG(("update_default_selected:(%s): "
		    "selected  %d-->%d\n", portp->lp_devname,
		    pl->sm.selected, AGGR_UNSELECTED));

		lacp_port_unselect(portp);
	}
}


/*
 * update_NTT - If any of the Partner values in the received LACPDU
 *			are different than that of the Actor operational
 *			values then set NTT to true.
 */
static void
update_NTT(aggr_port_t *portp, lacp_t *lacp)
{
	aggr_grp_t *aggrp = portp->lp_grp;
	aggr_lacp_port_t *pl = &portp->lp_lacp;

	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	if ((pl->ActorPortNumber != ntohs(lacp->partner_info.port)) ||
	    (pl->ActorPortPriority !=
	    ntohs(lacp->partner_info.port_priority)) ||
	    (ether_cmp(&aggrp->lg_addr,
	    &lacp->partner_info.system_id) != 0) ||
	    (aggrp->aggr.ActorSystemPriority !=
	    ntohs(lacp->partner_info.system_priority)) ||
	    (pl->ActorOperPortKey != ntohs(lacp->partner_info.key)) ||
	    (pl->ActorOperPortState.bit.activity !=
	    lacp->partner_info.state.bit.activity) ||
	    (pl->ActorOperPortState.bit.timeout !=
	    lacp->partner_info.state.bit.timeout) ||
	    (pl->ActorOperPortState.bit.sync !=
	    lacp->partner_info.state.bit.sync) ||
	    (pl->ActorOperPortState.bit.aggregation !=
	    lacp->partner_info.state.bit.aggregation)) {

		AGGR_LACP_DBG(("update_NTT:(%s): NTT  %d-->%d\n",
		    portp->lp_devname, pl->NTT, B_TRUE));

		pl->NTT = B_TRUE;
	}
}

/*
 * lacp_receive_sm - LACP receive state machine
 *
 * parameters:
 *      - portp - instance this applies to.
 *      - lacp - pointer in the case of a received LACPDU.
 *                This value is NULL if there is no LACPDU.
 *
 * invoked:
 *    - when initialization is needed
 *    - upon reception of an LACPDU. This is the common case.
 *    - every time the current_while_timer pops
 */
static void
lacp_receive_sm(aggr_port_t *portp, lacp_t *lacp)
{
	boolean_t sync_updated, selected_updated, save_activity;
	aggr_lacp_port_t *pl = &portp->lp_lacp;
	lacp_receive_state_t oldstate = pl->sm.receive_state;

	ASSERT(AGGR_LACP_LOCK_HELD(portp->lp_grp));

	/* LACP_OFF state not in specification so check here.  */
	if (!pl->sm.lacp_on)
		return;

	/* figure next state */
	if (pl->sm.begin || pl->sm.port_moved) {
		pl->sm.receive_state = LACP_INITIALIZE;
	} else if (!pl->sm.port_enabled) {	/* DL_NOTE_LINK_DOWN */
		pl->sm.receive_state = LACP_PORT_DISABLED;
	} else if (!pl->sm.lacp_enabled) { /* DL_NOTE_AGGR_UNAVAIL */
		pl->sm.receive_state =
		    (pl->sm.receive_state == LACP_PORT_DISABLED) ?
		    LACP_DISABLED : LACP_PORT_DISABLED;
	} else if (lacp != NULL) {
		if ((pl->sm.receive_state == LACP_EXPIRED) ||
		    (pl->sm.receive_state == LACP_DEFAULTED)) {
			pl->sm.receive_state = LACP_CURRENT;
		}
	} else if ((pl->sm.receive_state == LACP_CURRENT) &&
	    (pl->current_while_timer.id == 0)) {
		pl->sm.receive_state = LACP_EXPIRED;
	} else if ((pl->sm.receive_state == LACP_EXPIRED) &&
	    (pl->current_while_timer.id == 0)) {
		pl->sm.receive_state = LACP_DEFAULTED;
	}


	if (!((lacp && (oldstate == LACP_CURRENT) &&
	    (pl->sm.receive_state == LACP_CURRENT)))) {
		AGGR_LACP_DBG(("lacp_receive_sm(%s):%s--->%s\n",
		    portp->lp_devname, lacp_receive_str[oldstate],
		    lacp_receive_str[pl->sm.receive_state]));
	}

	switch (pl->sm.receive_state) {
	case LACP_INITIALIZE:
		lacp_port_unselect(portp);
		record_Default(portp);
		pl->ActorOperPortState.bit.expired = B_FALSE;
		pl->sm.port_moved = B_FALSE;
		pl->sm.receive_state = LACP_PORT_DISABLED;
		pl->sm.begin = B_FALSE;
		lacp_receive_sm(portp, NULL);
		break;

	case LACP_PORT_DISABLED:
		pl->PartnerOperPortState.bit.sync = B_FALSE;
		/*
		 * Stop current_while_timer in case
		 * we got here from link down
		 */
		stop_current_while_timer(portp);

		if (pl->sm.port_enabled && !pl->sm.lacp_enabled) {
			pl->sm.receive_state = LACP_DISABLED;
			lacp_receive_sm(portp, lacp);
			/* We goto LACP_DISABLED state */
			break;
		} else if (pl->sm.port_enabled && pl->sm.lacp_enabled) {
			pl->sm.receive_state = LACP_EXPIRED;
			/*
			 * FALL THROUGH TO LACP_EXPIRED CASE:
			 * We have no way of knowing if we get into
			 * lacp_receive_sm() from a  current_while_timer
			 * expiring as it has never been kicked off yet!
			 */
		} else {
			/* We stay in LACP_PORT_DISABLED state */
			break;
		}
		/* LACP_PORT_DISABLED -> LACP_EXPIRED */
		/* FALLTHROUGH */

	case LACP_EXPIRED:
		/*
		 * Arrives here from LACP_PORT_DISABLED state as well as
		 * as well as current_while_timer expiring.
		 */
		pl->PartnerOperPortState.bit.sync = B_FALSE;
		pl->PartnerOperPortState.bit.timeout = B_TRUE;

		pl->ActorOperPortState.bit.expired = B_TRUE;
		start_current_while_timer(portp, SHORT_TIMEOUT_TIME);
		lacp_periodic_sm(portp);
		break;

	case LACP_DISABLED:
		/*
		 * This is the normal state for recv_sm when LACP_OFF
		 * is set or the NIC is in half duplex mode.
		 */
		lacp_port_unselect(portp);
		record_Default(portp);
		pl->PartnerOperPortState.bit.aggregation = B_FALSE;
		pl->ActorOperPortState.bit.expired = B_FALSE;
		break;

	case LACP_DEFAULTED:
		/*
		 * Current_while_timer expired a second time.
		 */
		update_default_selected(portp);
		record_Default(portp);	/* overwrite Partner Oper val */
		pl->ActorOperPortState.bit.expired = B_FALSE;
		pl->PartnerOperPortState.bit.sync = B_TRUE;

		lacp_selection_logic(portp);
		lacp_mux_sm(portp);
		break;

	case LACP_CURRENT:
		/*
		 * Reception of LACPDU
		 */

		if (!lacp) /* no LACPDU so current_while_timer popped */
			break;

		AGGR_LACP_DBG(("lacp_receive_sm: (%s): LACPDU received:\n",
		    portp->lp_devname));

		/*
		 * Validate Actor_Information_Length,
		 * Partner_Information_Length, Collector_Information_Length,
		 * and Terminator_Length fields.
		 */
		if (!valid_lacp_pdu(portp, lacp)) {
			AGGR_LACP_DBG(("lacp_receive_sm (%s): "
			    "Invalid LACPDU received\n",
			    portp->lp_devname));
			break;
		}

		save_activity = pl->PartnerOperPortState.bit.activity;
		selected_updated = update_selected(portp, lacp);
		update_NTT(portp, lacp);
		sync_updated = record_PDU(portp, lacp);

		pl->ActorOperPortState.bit.expired = B_FALSE;

		if (selected_updated) {
			lacp_selection_logic(portp);
			lacp_mux_sm(portp);
		} else if (sync_updated) {
			lacp_mux_sm(portp);
		}

		/*
		 * If the periodic timer value bit has been modified
		 * or the partner activity bit has been changed then
		 * we need to respectively:
		 *  - restart the timer with the proper timeout value.
		 *  - possibly enable/disable transmission of LACPDUs.
		 */
		if ((pl->PartnerOperPortState.bit.timeout &&
		    (pl->periodic_timer.val != FAST_PERIODIC_TIME)) ||
		    (!pl->PartnerOperPortState.bit.timeout &&
		    (pl->periodic_timer.val != SLOW_PERIODIC_TIME)) ||
		    (pl->PartnerOperPortState.bit.activity !=
		    save_activity)) {
			lacp_periodic_sm(portp);
		}

		stop_current_while_timer(portp);
		/* Check if we need to transmit an LACPDU */
		if (pl->NTT)
			lacp_xmit_sm(portp);
		start_current_while_timer(portp, 0);

		break;
	}
}

static void
aggr_set_coll_dist(aggr_port_t *portp, boolean_t enable)
{
	rw_enter(&portp->lp_lock, RW_WRITER);
	aggr_set_coll_dist_locked(portp, enable);
	rw_exit(&portp->lp_lock);
}

static void
aggr_set_coll_dist_locked(aggr_port_t *portp, boolean_t enable)
{
	ASSERT(RW_WRITE_HELD(&portp->lp_lock));

	AGGR_LACP_DBG(("AGGR_SET_COLL_DIST_TYPE: (%s) %s\n",
	    portp->lp_devname, enable ? "ENABLED" : "DISABLED"));

	if (!enable) {
		/*
		 * Turn OFF Collector_Distributor.
		 */
		portp->lp_collector_enabled = B_FALSE;
		aggr_send_port_disable(portp);
		return;
	}

	/*
	 * Turn ON Collector_Distributor.
	 */

	if (!portp->lp_lacp.sm.lacp_on || (portp->lp_lacp.sm.lacp_on &&
	    (portp->lp_lacp.sm.mux_state == LACP_COLLECTING_DISTRIBUTING))) {
		/* Port is compatible and can be aggregated */
		portp->lp_collector_enabled = B_TRUE;
		aggr_send_port_enable(portp);
	}
}

/*
 * Process a received Marker or LACPDU.
 */
void
aggr_lacp_rx(aggr_port_t *portp, mblk_t *dmp)
{
	lacp_t	*lacp;

	dmp->b_rptr += sizeof (struct ether_header);

	if (MBLKL(dmp) < sizeof (lacp_t)) {
		freemsg(dmp);
		return;
	}

	lacp = (lacp_t *)dmp->b_rptr;

	switch (lacp->subtype) {
	case LACP_SUBTYPE:
		AGGR_LACP_DBG(("aggr_lacp_rx:(%s): LACPDU received.\n",
		    portp->lp_devname));

		AGGR_LACP_LOCK(portp->lp_grp);
		if (!portp->lp_lacp.sm.lacp_on) {
			AGGR_LACP_UNLOCK(portp->lp_grp);
			break;
		}
		lacp_receive_sm(portp, lacp);
		AGGR_LACP_UNLOCK(portp->lp_grp);
		break;

	case MARKER_SUBTYPE:
		AGGR_LACP_DBG(("aggr_lacp_rx:(%s): Marker Packet received.\n",
		    portp->lp_devname));

		(void) receive_marker_pdu(portp, dmp);
		break;

	default:
		AGGR_LACP_DBG(("aggr_lacp_rx: (%s): "
		    "Unknown Slow Protocol type %d\n",
		    portp->lp_devname, lacp->subtype));
		break;
	}

	freemsg(dmp);
}
