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
 * Copyright(c) 2007-2008 Intel Corporation. All rights reserved.
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "igb_sw.h"

int
igb_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	igb_t *igb = (igb_t *)arg;
	struct e1000_hw *hw = &igb->hw;
	igb_stat_t *igb_ks;
	uint32_t low_val, high_val;

	igb_ks = (igb_stat_t *)igb->igb_ks->ks_data;

	mutex_enter(&igb->gen_lock);

	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		return (ECANCELED);
	}

	switch (stat) {
	case MAC_STAT_IFSPEED:
		*val = igb->link_speed * 1000000ull;
		break;

	case MAC_STAT_MULTIRCV:
		igb_ks->mprc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_MPRC);
		*val = igb_ks->mprc.value.ui64;
		break;

	case MAC_STAT_BRDCSTRCV:
		igb_ks->bprc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_BPRC);
		*val = igb_ks->bprc.value.ui64;
		break;

	case MAC_STAT_MULTIXMT:
		igb_ks->mptc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_MPTC);
		*val = igb_ks->mptc.value.ui64;
		break;

	case MAC_STAT_BRDCSTXMT:
		igb_ks->bptc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_BPTC);
		*val = igb_ks->bptc.value.ui64;
		break;

	case MAC_STAT_NORCVBUF:
		igb_ks->rnbc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RNBC);
		*val = igb_ks->rnbc.value.ui64;
		break;

	case MAC_STAT_IERRORS:
		igb_ks->rxerrc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RXERRC);
		igb_ks->algnerrc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ALGNERRC);
		igb_ks->rlec.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RLEC);
		igb_ks->crcerrs.value.ui64 +=
		    E1000_READ_REG(hw, E1000_CRCERRS);
		igb_ks->cexterr.value.ui64 +=
		    E1000_READ_REG(hw, E1000_CEXTERR);
		*val = igb_ks->rxerrc.value.ui64 +
		    igb_ks->algnerrc.value.ui64 +
		    igb_ks->rlec.value.ui64 +
		    igb_ks->crcerrs.value.ui64 +
		    igb_ks->cexterr.value.ui64;
		break;

	case MAC_STAT_NOXMTBUF:
		*val = 0;
		break;

	case MAC_STAT_OERRORS:
		igb_ks->ecol.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ECOL);
		*val = igb_ks->ecol.value.ui64;
		break;

	case MAC_STAT_COLLISIONS:
		igb_ks->colc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_COLC);
		*val = igb_ks->colc.value.ui64;
		break;

	case MAC_STAT_RBYTES:
		/*
		 * The 64-bit register will reset whenever the upper
		 * 32 bits are read. So we need to read the lower
		 * 32 bits first, then read the upper 32 bits.
		 */
		low_val = E1000_READ_REG(hw, E1000_TORL);
		high_val = E1000_READ_REG(hw, E1000_TORH);
		igb_ks->tor.value.ui64 +=
		    (uint64_t)high_val << 32 | (uint64_t)low_val;
		*val = igb_ks->tor.value.ui64;
		break;

	case MAC_STAT_IPACKETS:
		igb_ks->tpr.value.ui64 +=
		    E1000_READ_REG(hw, E1000_TPR);
		*val = igb_ks->tpr.value.ui64;
		break;

	case MAC_STAT_OBYTES:
		/*
		 * The 64-bit register will reset whenever the upper
		 * 32 bits are read. So we need to read the lower
		 * 32 bits first, then read the upper 32 bits.
		 */
		low_val = E1000_READ_REG(hw, E1000_TOTL);
		high_val = E1000_READ_REG(hw, E1000_TOTH);
		igb_ks->tot.value.ui64 +=
		    (uint64_t)high_val << 32 | (uint64_t)low_val;
		*val = igb_ks->tot.value.ui64;
		break;

	case MAC_STAT_OPACKETS:
		igb_ks->tpt.value.ui64 +=
		    E1000_READ_REG(hw, E1000_TPT);
		*val = igb_ks->tpt.value.ui64;
		break;

	/* RFC 1643 stats */
	case ETHER_STAT_ALIGN_ERRORS:
		igb_ks->algnerrc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ALGNERRC);
		*val = igb_ks->algnerrc.value.ui64;
		break;

	case ETHER_STAT_FCS_ERRORS:
		igb_ks->crcerrs.value.ui64 +=
		    E1000_READ_REG(hw, E1000_CRCERRS);
		*val = igb_ks->crcerrs.value.ui64;
		break;

	case ETHER_STAT_FIRST_COLLISIONS:
		igb_ks->scc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_SCC);
		*val = igb_ks->scc.value.ui64;
		break;

	case ETHER_STAT_MULTI_COLLISIONS:
		igb_ks->mcc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_MCC);
		*val = igb_ks->mcc.value.ui64;
		break;

	case ETHER_STAT_SQE_ERRORS:
		igb_ks->sec.value.ui64 +=
		    E1000_READ_REG(hw, E1000_SEC);
		*val = igb_ks->sec.value.ui64;
		break;

	case ETHER_STAT_DEFER_XMTS:
		igb_ks->dc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_DC);
		*val = igb_ks->dc.value.ui64;
		break;

	case ETHER_STAT_TX_LATE_COLLISIONS:
		igb_ks->latecol.value.ui64 +=
		    E1000_READ_REG(hw, E1000_LATECOL);
		*val = igb_ks->latecol.value.ui64;
		break;

	case ETHER_STAT_EX_COLLISIONS:
		igb_ks->ecol.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ECOL);
		*val = igb_ks->ecol.value.ui64;
		break;

	case ETHER_STAT_MACXMT_ERRORS:
		igb_ks->ecol.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ECOL);
		*val = igb_ks->ecol.value.ui64;
		break;

	case ETHER_STAT_CARRIER_ERRORS:
		igb_ks->cexterr.value.ui64 +=
		    E1000_READ_REG(hw, E1000_CEXTERR);
		*val = igb_ks->cexterr.value.ui64;
		break;

	case ETHER_STAT_TOOLONG_ERRORS:
		igb_ks->roc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ROC);
		*val = igb_ks->roc.value.ui64;
		break;

	case ETHER_STAT_MACRCV_ERRORS:
		igb_ks->rxerrc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RXERRC);
		*val = igb_ks->rxerrc.value.ui64;
		break;

	/* MII/GMII stats */
	case ETHER_STAT_XCVR_ADDR:
		/* The Internal PHY's MDI address for each MAC is 1 */
		*val = 1;
		break;

	case ETHER_STAT_XCVR_ID:
		*val = hw->phy.id | hw->phy.revision;
		break;

	case ETHER_STAT_XCVR_INUSE:
		switch (igb->link_speed) {
		case SPEED_1000:
			*val =
			    (hw->phy.media_type == e1000_media_type_copper) ?
			    XCVR_1000T : XCVR_1000X;
			break;
		case SPEED_100:
			*val =
			    (hw->phy.media_type == e1000_media_type_copper) ?
			    (igb->param_100t4_cap == 1) ?
			    XCVR_100T4 : XCVR_100T2 : XCVR_100X;
			break;
		case SPEED_10:
			*val = XCVR_10;
			break;
		default:
			*val = XCVR_NONE;
			break;
		}
		break;

	case ETHER_STAT_CAP_1000FDX:
		*val = igb->param_1000fdx_cap;
		break;

	case ETHER_STAT_CAP_1000HDX:
		*val = igb->param_1000hdx_cap;
		break;

	case ETHER_STAT_CAP_100FDX:
		*val = igb->param_100fdx_cap;
		break;

	case ETHER_STAT_CAP_100HDX:
		*val = igb->param_100hdx_cap;
		break;

	case ETHER_STAT_CAP_10FDX:
		*val = igb->param_10fdx_cap;
		break;

	case ETHER_STAT_CAP_10HDX:
		*val = igb->param_10hdx_cap;
		break;

	case ETHER_STAT_CAP_ASMPAUSE:
		*val = igb->param_asym_pause_cap;
		break;

	case ETHER_STAT_CAP_PAUSE:
		*val = igb->param_pause_cap;
		break;

	case ETHER_STAT_CAP_AUTONEG:
		*val = igb->param_autoneg_cap;
		break;

	case ETHER_STAT_ADV_CAP_1000FDX:
		*val = igb->param_adv_1000fdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_1000HDX:
		*val = igb->param_adv_1000hdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_100FDX:
		*val = igb->param_adv_100fdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_100HDX:
		*val = igb->param_adv_100hdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_10FDX:
		*val = igb->param_adv_10fdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_10HDX:
		*val = igb->param_adv_10hdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_ASMPAUSE:
		*val = igb->param_adv_asym_pause_cap;
		break;

	case ETHER_STAT_ADV_CAP_PAUSE:
		*val = igb->param_adv_pause_cap;
		break;

	case ETHER_STAT_ADV_CAP_AUTONEG:
		*val = hw->mac.autoneg;
		break;

	case ETHER_STAT_LP_CAP_1000FDX:
		*val = igb->param_lp_1000fdx_cap;
		break;

	case ETHER_STAT_LP_CAP_1000HDX:
		*val = igb->param_lp_1000hdx_cap;
		break;

	case ETHER_STAT_LP_CAP_100FDX:
		*val = igb->param_lp_100fdx_cap;
		break;

	case ETHER_STAT_LP_CAP_100HDX:
		*val = igb->param_lp_100hdx_cap;
		break;

	case ETHER_STAT_LP_CAP_10FDX:
		*val = igb->param_lp_10fdx_cap;
		break;

	case ETHER_STAT_LP_CAP_10HDX:
		*val = igb->param_lp_10hdx_cap;
		break;

	case ETHER_STAT_LP_CAP_ASMPAUSE:
		*val = igb->param_lp_asym_pause_cap;
		break;

	case ETHER_STAT_LP_CAP_PAUSE:
		*val = igb->param_lp_pause_cap;
		break;

	case ETHER_STAT_LP_CAP_AUTONEG:
		*val = igb->param_lp_autoneg_cap;
		break;

	case ETHER_STAT_LINK_ASMPAUSE:
		*val = igb->param_asym_pause_cap;
		break;

	case ETHER_STAT_LINK_PAUSE:
		*val = igb->param_pause_cap;
		break;

	case ETHER_STAT_LINK_AUTONEG:
		*val = hw->mac.autoneg;
		break;

	case ETHER_STAT_LINK_DUPLEX:
		*val = (igb->link_duplex == FULL_DUPLEX) ?
		    LINK_DUPLEX_FULL : LINK_DUPLEX_HALF;
		break;

	case ETHER_STAT_TOOSHORT_ERRORS:
		igb_ks->ruc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RUC);
		*val = igb_ks->ruc.value.ui64;
		break;

	case ETHER_STAT_CAP_REMFAULT:
		*val = igb->param_rem_fault;
		break;

	case ETHER_STAT_ADV_REMFAULT:
		*val = igb->param_adv_rem_fault;
		break;

	case ETHER_STAT_LP_REMFAULT:
		*val = igb->param_lp_rem_fault;
		break;

	case ETHER_STAT_JABBER_ERRORS:
		igb_ks->rjc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RJC);
		*val = igb_ks->rjc.value.ui64;
		break;

	case ETHER_STAT_CAP_100T4:
		*val = igb->param_100t4_cap;
		break;

	case ETHER_STAT_ADV_CAP_100T4:
		*val = igb->param_adv_100t4_cap;
		break;

	case ETHER_STAT_LP_CAP_100T4:
		*val = igb->param_lp_100t4_cap;
		break;

	default:
		mutex_exit(&igb->gen_lock);
		return (ENOTSUP);
	}

	mutex_exit(&igb->gen_lock);

	if (igb_check_acc_handle(igb->osdep.reg_handle) != DDI_FM_OK)
		ddi_fm_service_impact(igb->dip, DDI_SERVICE_UNAFFECTED);

	return (0);
}

/*
 * Bring the device out of the reset/quiesced state that it
 * was in when the interface was registered.
 */
int
igb_m_start(void *arg)
{
	igb_t *igb = (igb_t *)arg;

	mutex_enter(&igb->gen_lock);

	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		return (ECANCELED);
	}

	if (igb_start(igb) != IGB_SUCCESS) {
		mutex_exit(&igb->gen_lock);
		return (EIO);
	}

	igb->igb_state |= IGB_STARTED;

	mutex_exit(&igb->gen_lock);

	/*
	 * Enable and start the watchdog timer
	 */
	igb_enable_watchdog_timer(igb);

	return (0);
}

/*
 * Stop the device and put it in a reset/quiesced state such
 * that the interface can be unregistered.
 */
void
igb_m_stop(void *arg)
{
	igb_t *igb = (igb_t *)arg;

	mutex_enter(&igb->gen_lock);

	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		return;
	}

	igb->igb_state &= ~IGB_STARTED;

	igb_stop(igb);

	mutex_exit(&igb->gen_lock);

	/*
	 * Disable and stop the watchdog timer
	 */
	igb_disable_watchdog_timer(igb);
}

/*
 * Set the promiscuity of the device.
 */
int
igb_m_promisc(void *arg, boolean_t on)
{
	igb_t *igb = (igb_t *)arg;
	uint32_t reg_val;

	mutex_enter(&igb->gen_lock);

	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		return (ECANCELED);
	}

	reg_val = E1000_READ_REG(&igb->hw, E1000_RCTL);

	if (on)
		reg_val |= (E1000_RCTL_UPE | E1000_RCTL_MPE);
	else
		reg_val &= (~(E1000_RCTL_UPE | E1000_RCTL_MPE));

	E1000_WRITE_REG(&igb->hw, E1000_RCTL, reg_val);

	mutex_exit(&igb->gen_lock);

	if (igb_check_acc_handle(igb->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igb->dip, DDI_SERVICE_DEGRADED);
		return (EIO);
	}

	return (0);
}

/*
 * Add/remove the addresses to/from the set of multicast
 * addresses for which the device will receive packets.
 */
int
igb_m_multicst(void *arg, boolean_t add, const uint8_t *mcst_addr)
{
	igb_t *igb = (igb_t *)arg;
	int result;

	mutex_enter(&igb->gen_lock);

	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		return (ECANCELED);
	}

	result = (add) ? igb_multicst_add(igb, mcst_addr)
	    : igb_multicst_remove(igb, mcst_addr);

	mutex_exit(&igb->gen_lock);

	return (result);
}

/*
 * Pass on M_IOCTL messages passed to the DLD, and support
 * private IOCTLs for debugging and ndd.
 */
void
igb_m_ioctl(void *arg, queue_t *q, mblk_t *mp)
{
	igb_t *igb = (igb_t *)arg;
	struct iocblk *iocp;
	enum ioc_reply status;

	iocp = (struct iocblk *)(uintptr_t)mp->b_rptr;
	iocp->ioc_error = 0;

	switch (iocp->ioc_cmd) {
	case LB_GET_INFO_SIZE:
	case LB_GET_INFO:
	case LB_GET_MODE:
	case LB_SET_MODE:
		status = igb_loopback_ioctl(igb, iocp, mp);
		break;

	case ND_GET:
	case ND_SET:
		status = igb_nd_ioctl(igb, q, mp, iocp);
		break;

	default:
		status = IOC_INVAL;
		break;
	}

	/*
	 * Decide how to reply
	 */
	switch (status) {
	default:
	case IOC_INVAL:
		/*
		 * Error, reply with a NAK and EINVAL or the specified error
		 */
		miocnak(q, mp, 0, iocp->ioc_error == 0 ?
		    EINVAL : iocp->ioc_error);
		break;

	case IOC_DONE:
		/*
		 * OK, reply already sent
		 */
		break;

	case IOC_ACK:
		/*
		 * OK, reply with an ACK
		 */
		miocack(q, mp, 0, 0);
		break;

	case IOC_REPLY:
		/*
		 * OK, send prepared reply as ACK or NAK
		 */
		mp->b_datap->db_type = iocp->ioc_error == 0 ?
		    M_IOCACK : M_IOCNAK;
		qreply(q, mp);
		break;
	}
}

/*
 * Add a MAC address to the target RX group.
 */
static int
igb_addmac(void *arg, const uint8_t *mac_addr)
{
	igb_rx_group_t *rx_group = (igb_rx_group_t *)arg;
	igb_t *igb = rx_group->igb;
	struct e1000_hw *hw = &igb->hw;
	int i, slot;

	mutex_enter(&igb->gen_lock);

	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		return (ECANCELED);
	}

	if (igb->unicst_avail == 0) {
		/* no slots available */
		mutex_exit(&igb->gen_lock);
		return (ENOSPC);
	}

	/*
	 * The slots from 0 to igb->num_rx_groups are reserved slots which
	 * are 1 to 1 mapped with group index directly. The other slots are
	 * shared between the all of groups. While adding a MAC address,
	 * it will try to set the reserved slots first, then the shared slots.
	 */
	slot = -1;
	if (igb->unicst_addr[rx_group->index].mac.set == 1) {
		/*
		 * The reserved slot for current group is used, find the free
		 * slots in the shared slots.
		 */
		for (i = igb->num_rx_groups; i < igb->unicst_total; i++) {
			if (igb->unicst_addr[i].mac.set == 0) {
				slot = i;
				break;
			}
		}
	} else
		slot = rx_group->index;

	if (slot == -1) {
		/* no slots available in the shared slots */
		mutex_exit(&igb->gen_lock);
		return (ENOSPC);
	}

	/* Set VMDq according to the mode supported by hardware. */
	e1000_rar_set_vmdq(hw, mac_addr, slot, igb->vmdq_mode, rx_group->index);

	bcopy(mac_addr, igb->unicst_addr[slot].mac.addr, ETHERADDRL);
	igb->unicst_addr[slot].mac.group_index = rx_group->index;
	igb->unicst_addr[slot].mac.set = 1;
	igb->unicst_avail--;

	mutex_exit(&igb->gen_lock);

	return (0);
}

/*
 * Remove a MAC address from the specified RX group.
 */
static int
igb_remmac(void *arg, const uint8_t *mac_addr)
{
	igb_rx_group_t *rx_group = (igb_rx_group_t *)arg;
	igb_t *igb = rx_group->igb;
	struct e1000_hw *hw = &igb->hw;
	int slot;

	mutex_enter(&igb->gen_lock);

	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		return (ECANCELED);
	}

	slot = igb_unicst_find(igb, mac_addr);
	if (slot == -1) {
		mutex_exit(&igb->gen_lock);
		return (EINVAL);
	}

	if (igb->unicst_addr[slot].mac.set == 0) {
		mutex_exit(&igb->gen_lock);
		return (EINVAL);
	}

	/* Clear the MAC ddress in the slot */
	e1000_rar_clear(hw, slot);
	igb->unicst_addr[slot].mac.set = 0;
	igb->unicst_avail++;

	mutex_exit(&igb->gen_lock);

	return (0);
}

/*
 * Enable interrupt on the specificed rx ring.
 */
int
igb_rx_ring_intr_enable(mac_intr_handle_t intrh)
{
	igb_rx_ring_t *rx_ring = (igb_rx_ring_t *)intrh;
	igb_t *igb = rx_ring->igb;
	struct e1000_hw *hw = &igb->hw;
	uint32_t index = rx_ring->index;

	if (igb->intr_type == DDI_INTR_TYPE_MSIX) {
		/* Interrupt enabling for MSI-X */
		igb->eims_mask |= (E1000_EICR_RX_QUEUE0 << index);
		E1000_WRITE_REG(hw, E1000_EIMS, igb->eims_mask);
		E1000_WRITE_REG(hw, E1000_EIAC, igb->eims_mask);
	} else {
		ASSERT(index == 0);
		/* Interrupt enabling for MSI and legacy */
		igb->ims_mask |= E1000_IMS_RXT0;
		E1000_WRITE_REG(hw, E1000_IMS, igb->ims_mask);
	}

	E1000_WRITE_FLUSH(hw);

	return (0);
}

/*
 * Disable interrupt on the specificed rx ring.
 */
int
igb_rx_ring_intr_disable(mac_intr_handle_t intrh)
{
	igb_rx_ring_t *rx_ring = (igb_rx_ring_t *)intrh;
	igb_t *igb = rx_ring->igb;
	struct e1000_hw *hw = &igb->hw;
	uint32_t index = rx_ring->index;

	if (igb->intr_type == DDI_INTR_TYPE_MSIX) {
		/* Interrupt disabling for MSI-X */
		igb->eims_mask &= ~(E1000_EICR_RX_QUEUE0 << index);
		E1000_WRITE_REG(hw, E1000_EIMC,
		    (E1000_EICR_RX_QUEUE0 << index));
		E1000_WRITE_REG(hw, E1000_EIAC, igb->eims_mask);
	} else {
		ASSERT(index == 0);
		/* Interrupt disabling for MSI and legacy */
		igb->ims_mask &= ~E1000_IMS_RXT0;
		E1000_WRITE_REG(hw, E1000_IMC, E1000_IMS_RXT0);
	}

	E1000_WRITE_FLUSH(hw);

	return (0);
}

/*
 * Get the global ring index by a ring index within a group.
 */
int
igb_get_rx_ring_index(igb_t *igb, int gindex, int rindex)
{
	igb_rx_ring_t *rx_ring;
	int i;

	for (i = 0; i < igb->num_rx_rings; i++) {
		rx_ring = &igb->rx_rings[i];
		if (rx_ring->group_index == gindex)
			rindex--;
		if (rindex < 0)
			return (i);
	}

	return (-1);
}

static int
igb_ring_start(mac_ring_driver_t rh, uint64_t mr_gen_num)
{
	igb_rx_ring_t *rx_ring = (igb_rx_ring_t *)rh;

	mutex_enter(&rx_ring->rx_lock);
	rx_ring->ring_gen_num = mr_gen_num;
	mutex_exit(&rx_ring->rx_lock);
	return (0);
}

/*
 * Callback funtion for MAC layer to register all rings.
 */
/* ARGSUSED */
void
igb_fill_ring(void *arg, mac_ring_type_t rtype, const int rg_index,
    const int index, mac_ring_info_t *infop, mac_ring_handle_t rh)
{
	igb_t *igb = (igb_t *)arg;
	mac_intr_t *mintr = &infop->mri_intr;

	switch (rtype) {
	case MAC_RING_TYPE_RX: {
		igb_rx_ring_t *rx_ring;
		int global_index;

		/*
		 * 'index' is the ring index within the group.
		 * We need the global ring index by searching in group.
		 */
		global_index = igb_get_rx_ring_index(igb, rg_index, index);

		ASSERT(global_index >= 0);

		rx_ring = &igb->rx_rings[global_index];
		rx_ring->ring_handle = rh;

		infop->mri_driver = (mac_ring_driver_t)rx_ring;
		infop->mri_start = igb_ring_start;
		infop->mri_stop = NULL;
		infop->mri_poll = (mac_ring_poll_t)igb_rx_ring_poll;

		mintr->mi_handle = (mac_intr_handle_t)rx_ring;
		mintr->mi_enable = igb_rx_ring_intr_enable;
		mintr->mi_disable = igb_rx_ring_intr_disable;

		break;
	}
	case MAC_RING_TYPE_TX: {
		ASSERT(index < igb->num_tx_rings);

		igb_tx_ring_t *tx_ring = &igb->tx_rings[index];
		tx_ring->ring_handle = rh;

		infop->mri_driver = (mac_ring_driver_t)tx_ring;
		infop->mri_start = NULL;
		infop->mri_stop = NULL;
		infop->mri_tx = igb_tx_ring_send;

		break;
	}
	default:
		break;
	}
}

void
igb_fill_group(void *arg, mac_ring_type_t rtype, const int index,
    mac_group_info_t *infop, mac_group_handle_t gh)
{
	igb_t *igb = (igb_t *)arg;

	switch (rtype) {
	case MAC_RING_TYPE_RX: {
		igb_rx_group_t *rx_group;

		ASSERT((index >= 0) && (index < igb->num_rx_groups));

		rx_group = &igb->rx_groups[index];
		rx_group->group_handle = gh;

		infop->mgi_driver = (mac_group_driver_t)rx_group;
		infop->mgi_start = NULL;
		infop->mgi_stop = NULL;
		infop->mgi_addmac = igb_addmac;
		infop->mgi_remmac = igb_remmac;
		infop->mgi_count = (igb->num_rx_rings / igb->num_rx_groups);

		break;
	}
	case MAC_RING_TYPE_TX:
		break;
	default:
		break;
	}
}

/*
 * Obtain the MAC's capabilities and associated data from
 * the driver.
 */
boolean_t
igb_m_getcapab(void *arg, mac_capab_t cap, void *cap_data)
{
	igb_t *igb = (igb_t *)arg;

	switch (cap) {
	case MAC_CAPAB_HCKSUM: {
		uint32_t *tx_hcksum_flags = cap_data;

		/*
		 * We advertise our capabilities only if tx hcksum offload is
		 * enabled.  On receive, the stack will accept checksummed
		 * packets anyway, even if we haven't said we can deliver
		 * them.
		 */
		if (!igb->tx_hcksum_enable)
			return (B_FALSE);

		*tx_hcksum_flags = HCKSUM_INET_PARTIAL | HCKSUM_IPHDRCKSUM;
		break;
	}
	case MAC_CAPAB_LSO: {
		mac_capab_lso_t *cap_lso = cap_data;

		if (igb->lso_enable) {
			cap_lso->lso_flags = LSO_TX_BASIC_TCP_IPV4;
			cap_lso->lso_basic_tcp_ipv4.lso_max = IGB_LSO_MAXLEN;
			break;
		} else {
			return (B_FALSE);
		}
	}
	case MAC_CAPAB_RINGS: {
		mac_capab_rings_t *cap_rings = cap_data;

		switch (cap_rings->mr_type) {
		case MAC_RING_TYPE_RX:
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = igb->num_rx_rings;
			cap_rings->mr_gnum = igb->num_rx_groups;
			cap_rings->mr_rget = igb_fill_ring;
			cap_rings->mr_gget = igb_fill_group;
			cap_rings->mr_gaddring = NULL;
			cap_rings->mr_gremring = NULL;

			break;
		case MAC_RING_TYPE_TX:
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = igb->num_tx_rings;
			cap_rings->mr_gnum = 0;
			cap_rings->mr_rget = igb_fill_ring;
			cap_rings->mr_gget = NULL;

			break;
		default:
			break;
		}
		break;
	}

	default:
		return (B_FALSE);
	}
	return (B_TRUE);
}
