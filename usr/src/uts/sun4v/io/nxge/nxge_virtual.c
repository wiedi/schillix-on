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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/nxge/nxge_impl.h>
#include <sys/nxge/nxge_mac.h>

static void nxge_get_niu_property(dev_info_t *, niu_type_t *);
static nxge_status_t nxge_get_mac_addr_properties(p_nxge_t);
static nxge_status_t nxge_use_cfg_n2niu_properties(p_nxge_t);
static void nxge_use_cfg_neptune_properties(p_nxge_t);
static void nxge_use_cfg_dma_config(p_nxge_t);
static void nxge_use_cfg_vlan_class_config(p_nxge_t);
static void nxge_use_cfg_mac_class_config(p_nxge_t);
static void nxge_use_cfg_class_config(p_nxge_t);
static void nxge_use_cfg_link_cfg(p_nxge_t);
static void nxge_set_hw_dma_config(p_nxge_t);
static void nxge_set_hw_vlan_class_config(p_nxge_t);
static void nxge_set_hw_mac_class_config(p_nxge_t);
static void nxge_set_hw_class_config(p_nxge_t);
static nxge_status_t nxge_use_default_dma_config_n2(p_nxge_t);
static void nxge_ldgv_setup(p_nxge_ldg_t *, p_nxge_ldv_t *, uint8_t,
	uint8_t, int *);
static void nxge_init_mmac(p_nxge_t);

uint32_t nxge_use_hw_property = 1;
uint32_t nxge_groups_per_port = 2;

extern uint32_t nxge_use_partition;
extern uint32_t nxge_dma_obp_props_only;

extern uint16_t nxge_rcr_timeout;
extern uint16_t nxge_rcr_threshold;

extern uint_t nxge_rx_intr(void *, void *);
extern uint_t nxge_tx_intr(void *, void *);
extern uint_t nxge_mif_intr(void *, void *);
extern uint_t nxge_mac_intr(void *, void *);
extern uint_t nxge_syserr_intr(void *, void *);
extern void *nxge_list;

#define	NXGE_SHARED_REG_SW_SIM

#ifdef NXGE_SHARED_REG_SW_SIM
uint64_t global_dev_ctrl = 0;
#endif

#define	MAX_SIBLINGS	NXGE_MAX_PORTS

extern uint32_t nxge_rbr_size;
extern uint32_t nxge_rcr_size;
extern uint32_t nxge_tx_ring_size;
extern uint32_t nxge_rbr_spare_size;

extern npi_status_t npi_mac_altaddr_disable(npi_handle_t, uint8_t, uint8_t);

static uint8_t p2_tx_fair[2] = {12, 12};
static uint8_t p2_tx_equal[2] = {12, 12};
static uint8_t p4_tx_fair[4] = {6, 6, 6, 6};
static uint8_t p4_tx_equal[4] = {6, 6, 6, 6};
static uint8_t p2_rx_fair[2] = {8, 8};
static uint8_t p2_rx_equal[2] = {8, 8};

static uint8_t p4_rx_fair[4] = {4, 4, 4, 4};
static uint8_t p4_rx_equal[4] = {4, 4, 4, 4};

static uint8_t p2_rdcgrp_fair[2] = {4, 4};
static uint8_t p2_rdcgrp_equal[2] = {4, 4};
static uint8_t p4_rdcgrp_fair[4] = {2, 2, 1, 1};
static uint8_t p4_rdcgrp_equal[4] = {2, 2, 2, 2};
static uint8_t p2_rdcgrp_cls[2] = {1, 1};
static uint8_t p4_rdcgrp_cls[4] = {1, 1, 1, 1};

typedef enum {
	DEFAULT = 0,
	EQUAL,
	FAIR,
	CUSTOM,
	CLASSIFY,
	L2_CLASSIFY,
	L3_DISTRIBUTE,
	L3_CLASSIFY,
	L3_TCAM,
	CONFIG_TOKEN_NONE
} config_token_t;

static char *token_names[] = {
	"default",
	"equal",
	"fair",
	"custom",
	"classify",
	"l2_classify",
	"l3_distribute",
	"l3_classify",
	"l3_tcam",
	"none",
};

void nxge_virint_regs_dump(p_nxge_t nxgep);

void
nxge_virint_regs_dump(p_nxge_t nxgep)
{
	npi_handle_t handle;

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_virint_regs_dump"));
	handle = NXGE_DEV_NPI_HANDLE(nxgep);
	(void) npi_vir_dump_pio_fzc_regs_one(handle);
	(void) npi_vir_dump_ldgnum(handle);
	(void) npi_vir_dump_ldsv(handle);
	(void) npi_vir_dump_imask0(handle);
	(void) npi_vir_dump_sid(handle);
	(void) npi_mac_dump_regs(handle, nxgep->function_num);
	(void) npi_ipp_dump_regs(handle, nxgep->function_num);
	(void) npi_fflp_dump_regs(handle);
	NXGE_DEBUG_MSG((nxgep, INT_CTL, "<== nxge_virint_regs_dump"));
}

/*
 * For now: we hard coded the DMA configurations.
 *	    and assume for one partition only.
 *
 *       OBP. Then OBP will pass this partition's
 *	 Neptune configurations to fcode to create
 *	 properties for them.
 *
 *	Since Neptune(PCI-E) and NIU (Niagara-2) has
 *	different bus interfaces, the driver needs
 *	to know which bus it is connected to.
 *  	Ravinder suggested: create a device property.
 *	In partitioning environment, we cannot
 *	use .conf file (need to check). If conf changes,
 *	need to reboot the system.
 *	The following function assumes that we will
 *	retrieve its properties from a virtualized nexus driver.
 */

nxge_status_t
nxge_cntlops(dev_info_t *dip, nxge_ctl_enum_t ctlop, void *arg, void *result)
{
	nxge_status_t status = NXGE_OK;
	int instance;
	p_nxge_t nxgep;

#ifndef NXGE_SHARED_REG_SW_SIM
	npi_handle_t handle;
	uint16_t sr16, cr16;
#endif
	instance = ddi_get_instance(dip);
	NXGE_DEBUG_MSG((NULL, VIR_CTL, "Instance %d ", instance));

	if (nxge_list == NULL) {
		NXGE_ERROR_MSG((NULL, NXGE_ERR_CTL,
				"nxge_cntlops: nxge_list null"));
		return (NXGE_ERROR);
	}
	nxgep = (p_nxge_t)ddi_get_soft_state(nxge_list, instance);
	if (nxgep == NULL) {
		NXGE_ERROR_MSG((NULL, NXGE_ERR_CTL,
				"nxge_cntlops: nxgep null"));
		return (NXGE_ERROR);
	}
#ifndef NXGE_SHARED_REG_SW_SIM
	handle = nxgep->npi_reg_handle;
#endif
	switch (ctlop) {
	case NXGE_CTLOPS_NIUTYPE:
		nxge_get_niu_property(dip, (niu_type_t *)result);
		return (status);

	case NXGE_CTLOPS_GET_SHARED_REG:
#ifdef NXGE_SHARED_REG_SW_SIM
		*(uint64_t *)result = global_dev_ctrl;
		return (0);
#else
		status = npi_dev_func_sr_sr_get(handle, &sr16);
		*(uint16_t *)result = sr16;
		NXGE_DEBUG_MSG((NULL, VIR_CTL,
			"nxge_cntlops: NXGE_CTLOPS_GET_SHARED_REG"));
		return (0);
#endif

	case NXGE_CTLOPS_SET_SHARED_REG_LOCK:
#ifdef NXGE_SHARED_REG_SW_SIM
		global_dev_ctrl = *(uint64_t *)arg;
		return (0);
#else
		status = NPI_FAILURE;
		while (status != NPI_SUCCESS)
			status = npi_dev_func_sr_lock_enter(handle);

		sr16 = *(uint16_t *)arg;
		status = npi_dev_func_sr_sr_set_only(handle, &sr16);
		status = npi_dev_func_sr_lock_free(handle);
		NXGE_DEBUG_MSG((NULL, VIR_CTL,
			"nxge_cntlops: NXGE_CTLOPS_SET_SHARED_REG"));
		return (0);
#endif

	case NXGE_CTLOPS_UPDATE_SHARED_REG:
#ifdef NXGE_SHARED_REG_SW_SIM
		global_dev_ctrl |= *(uint64_t *)arg;
		return (0);
#else
		status = NPI_FAILURE;
		while (status != NPI_SUCCESS)
			status = npi_dev_func_sr_lock_enter(handle);
		status = npi_dev_func_sr_sr_get(handle, &sr16);
		sr16 |= *(uint16_t *)arg;
		status = npi_dev_func_sr_sr_set_only(handle, &sr16);
		status = npi_dev_func_sr_lock_free(handle);
		NXGE_DEBUG_MSG((NULL, VIR_CTL,
			"nxge_cntlops: NXGE_CTLOPS_SET_SHARED_REG"));
		return (0);
#endif

	case NXGE_CTLOPS_CLEAR_BIT_SHARED_REG_UL:
#ifdef NXGE_SHARED_REG_SW_SIM
		global_dev_ctrl |= *(uint64_t *)arg;
		return (0);
#else
		status = npi_dev_func_sr_sr_get(handle, &sr16);
		cr16 = *(uint16_t *)arg;
		sr16 &= ~cr16;
		status = npi_dev_func_sr_sr_set_only(handle, &sr16);
		NXGE_DEBUG_MSG((NULL, VIR_CTL,
			"nxge_cntlops: NXGE_CTLOPS_SET_SHARED_REG"));
		return (0);
#endif

	case NXGE_CTLOPS_CLEAR_BIT_SHARED_REG:
#ifdef NXGE_SHARED_REG_SW_SIM
		global_dev_ctrl |= *(uint64_t *)arg;
		return (0);
#else
		status = NPI_FAILURE;
		while (status != NPI_SUCCESS)
			status = npi_dev_func_sr_lock_enter(handle);
		status = npi_dev_func_sr_sr_get(handle, &sr16);
		cr16 = *(uint16_t *)arg;
		sr16 &= ~cr16;
		status = npi_dev_func_sr_sr_set_only(handle, &sr16);
		status = npi_dev_func_sr_lock_free(handle);
		NXGE_DEBUG_MSG((NULL, VIR_CTL,
			"nxge_cntlops: NXGE_CTLOPS_SET_SHARED_REG"));
		return (0);
#endif

	case NXGE_CTLOPS_GET_LOCK_BLOCK:
#ifdef NXGE_SHARED_REG_SW_SIM
		global_dev_ctrl |= *(uint64_t *)arg;
		return (0);
#else
		status = NPI_FAILURE;
		while (status != NPI_SUCCESS)
			status = npi_dev_func_sr_lock_enter(handle);
		NXGE_DEBUG_MSG((NULL, VIR_CTL,
			"nxge_cntlops: NXGE_CTLOPS_GET_LOCK_BLOCK"));
		return (0);
#endif
	case NXGE_CTLOPS_GET_LOCK_TRY:
#ifdef NXGE_SHARED_REG_SW_SIM
		global_dev_ctrl |= *(uint64_t *)arg;
		return (0);
#else
		status = npi_dev_func_sr_lock_enter(handle);
		NXGE_DEBUG_MSG((NULL, VIR_CTL,
			"nxge_cntlops: NXGE_CTLOPS_GET_LOCK_TRY"));
		if (status == NPI_SUCCESS)
			return (NXGE_OK);
		else
			return (NXGE_ERROR);
#endif
	case NXGE_CTLOPS_FREE_LOCK:
#ifdef NXGE_SHARED_REG_SW_SIM
		global_dev_ctrl |= *(uint64_t *)arg;
		return (0);
#else
		status = npi_dev_func_sr_lock_free(handle);
		NXGE_DEBUG_MSG((NULL, VIR_CTL,
			"nxge_cntlops: NXGE_CTLOPS_GET_LOCK_FREE"));
		if (status == NPI_SUCCESS)
			return (NXGE_OK);
		else
			return (NXGE_ERROR);
#endif

	default:
		status = NXGE_ERROR;
	}

	return (status);
}

void
nxge_common_lock_get(p_nxge_t nxgep)
{
	uint32_t status = NPI_FAILURE;
	npi_handle_t handle;

#if	defined(NXGE_SHARE_REG_SW_SIM)
	return;
#endif
	handle = nxgep->npi_reg_handle;
	while (status != NPI_SUCCESS)
		status = npi_dev_func_sr_lock_enter(handle);
}

void
nxge_common_lock_free(p_nxge_t nxgep)
{
	npi_handle_t handle;

#if	defined(NXGE_SHARE_REG_SW_SIM)
	return;
#endif
	handle = nxgep->npi_reg_handle;
	(void) npi_dev_func_sr_lock_free(handle);
}

static void
nxge_get_niu_property(dev_info_t *dip, niu_type_t *niu_type)
{
	uchar_t *prop_val;
	uint_t prop_len;

	*niu_type = NEPTUNE;
	if (ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, dip, 0,
			"niu-type", (uchar_t **)&prop_val,
			&prop_len) == DDI_PROP_SUCCESS) {
		if (strncmp("niu", (caddr_t)prop_val, (size_t)prop_len) == 0) {
			*niu_type = N2_NIU;
		}
		ddi_prop_free(prop_val);
	}
}

static config_token_t
nxge_get_config_token(char *prop)
{
	config_token_t token = DEFAULT;

	while (token < CONFIG_TOKEN_NONE) {
		if (strncmp(prop, token_names[token], 4) == 0)
			break;
		token++;
	}
	return (token);
}

/* per port */

static nxge_status_t
nxge_update_rxdma_grp_properties(p_nxge_t nxgep, config_token_t token,
	dev_info_t *s_dip[])
{
	nxge_status_t status = NXGE_OK;
	int ddi_status;
	int num_ports = nxgep->nports;
	int port, bits, j;
	uint8_t start_grp = 0, num_grps = 0;
	p_nxge_param_t param_arr;
	uint32_t grp_bitmap[MAX_SIBLINGS];
	int custom_start_grp[MAX_SIBLINGS];
	int custom_num_grp[MAX_SIBLINGS];
	uint8_t bad_config = B_FALSE;
	char *start_prop, *num_prop, *cfg_prop;

	start_grp = 0;
	param_arr = nxgep->param_arr;
	start_prop = param_arr[param_rdc_grps_start].fcode_name;
	num_prop = param_arr[param_rx_rdc_grps].fcode_name;

	switch (token) {
	case FAIR:
		cfg_prop = "fair";
		for (port = 0; port < num_ports; port++) {
			custom_num_grp[port] =
				(num_ports == 4) ?
				p4_rdcgrp_fair[port] :
				p2_rdcgrp_fair[port];
			custom_start_grp[port] = start_grp;
			start_grp += custom_num_grp[port];
		}
		break;

	case EQUAL:
		cfg_prop = "equal";
		for (port = 0; port < num_ports; port++) {
			custom_num_grp[port] =
				(num_ports == 4) ?
				p4_rdcgrp_equal[port] :
				p2_rdcgrp_equal[port];
			custom_start_grp[port] = start_grp;
			start_grp += custom_num_grp[port];
		}
		break;


	case CLASSIFY:
		cfg_prop = "classify";
		for (port = 0; port < num_ports; port++) {
			custom_num_grp[port] = (num_ports == 4) ?
				p4_rdcgrp_cls[port] : p2_rdcgrp_cls[port];
			custom_start_grp[port] = start_grp;
			start_grp += custom_num_grp[port];
		}
		break;

	case CUSTOM:
		cfg_prop = "custom";
		/* See if it is good config */
		num_grps = 0;
		for (port = 0; port < num_ports; port++) {
			custom_start_grp[port] =
				ddi_prop_get_int(DDI_DEV_T_NONE, s_dip[port],
				DDI_PROP_DONTPASS, start_prop, -1);
			if ((custom_start_grp[port] == -1) ||
				(custom_start_grp[port] >=
					NXGE_MAX_RDC_GRPS)) {
				bad_config = B_TRUE;
				break;
			}
			custom_num_grp[port] = ddi_prop_get_int(
				DDI_DEV_T_NONE,
				s_dip[port],
				DDI_PROP_DONTPASS,
				num_prop, -1);

			if ((custom_num_grp[port] == -1) ||
				(custom_num_grp[port] >
					NXGE_MAX_RDC_GRPS) ||
				((custom_num_grp[port] +
						custom_start_grp[port]) >=
					NXGE_MAX_RDC_GRPS)) {
				bad_config = B_TRUE;
				break;
			}
			num_grps += custom_num_grp[port];
			if (num_grps > NXGE_MAX_RDC_GRPS) {
				bad_config = B_TRUE;
				break;
			}
			grp_bitmap[port] = 0;
			for (bits = 0;
				bits < custom_num_grp[port];
				bits++) {
				grp_bitmap[port] |=
					(1 << (bits + custom_start_grp[port]));
			}

		}

		if (bad_config == B_FALSE) {
			/* check for overlap */
			for (port = 0; port < num_ports - 1; port++) {
				for (j = port + 1; j < num_ports; j++) {
					if (grp_bitmap[port] &
						grp_bitmap[j]) {
						bad_config = B_TRUE;
						break;
					}
				}
				if (bad_config == B_TRUE)
					break;
			}
		}
		if (bad_config == B_TRUE) {
			/* use default config */
			for (port = 0; port < num_ports; port++) {
				custom_num_grp[port] =
					(num_ports == 4) ?
					p4_rx_fair[port] : p2_rx_fair[port];
				custom_start_grp[port] = start_grp;
				start_grp += custom_num_grp[port];
			}
		}
		break;

	default:
		/* use default config */
		cfg_prop = "fair";
		for (port = 0; port < num_ports; port++) {
			custom_num_grp[port] = (num_ports == 4) ?
				p4_rx_fair[port] : p2_rx_fair[port];
			custom_start_grp[port] = start_grp;
			start_grp += custom_num_grp[port];
		}
		break;
	}

	/* Now Update the rx properties */
	for (port = 0; port < num_ports; port++) {
		ddi_status = ddi_prop_update_string(DDI_DEV_T_NONE, s_dip[port],
			"rxdma-grp-cfg", cfg_prop);
		if (ddi_status != DDI_PROP_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
					" property %s not updating",
					cfg_prop));
			status |= NXGE_DDI_FAILED;
		}
		ddi_status = ddi_prop_update_int(DDI_DEV_T_NONE, s_dip[port],
			num_prop, custom_num_grp[port]);

		if (ddi_status != DDI_PROP_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
					" property %s not updating",
					num_prop));
			status |= NXGE_DDI_FAILED;
		}
		ddi_status = ddi_prop_update_int(DDI_DEV_T_NONE, s_dip[port],
			start_prop, custom_start_grp[port]);

		if (ddi_status != DDI_PROP_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
					" property %s not updating",
					start_prop));
			status |= NXGE_DDI_FAILED;
		}
	}
	if (status & NXGE_DDI_FAILED)
		status |= NXGE_ERROR;

	return (status);
}

static nxge_status_t
nxge_update_rxdma_properties(p_nxge_t nxgep, config_token_t token,
	dev_info_t *s_dip[])
{
	nxge_status_t status = NXGE_OK;
	int ddi_status;
	int num_ports = nxgep->nports;
	int port, bits, j;
	uint8_t start_rdc = 0, num_rdc = 0;
	p_nxge_param_t param_arr;
	uint32_t rdc_bitmap[MAX_SIBLINGS];
	int custom_start_rdc[MAX_SIBLINGS];
	int custom_num_rdc[MAX_SIBLINGS];
	uint8_t bad_config = B_FALSE;
	int *prop_val;
	uint_t prop_len;
	char *start_rdc_prop, *num_rdc_prop, *cfg_prop;

	start_rdc = 0;
	param_arr = nxgep->param_arr;
	start_rdc_prop = param_arr[param_rxdma_channels_begin].fcode_name;
	num_rdc_prop = param_arr[param_rxdma_channels].fcode_name;

	switch (token) {
	case FAIR:
		cfg_prop = "fair";
		for (port = 0; port < num_ports; port++) {
			custom_num_rdc[port] = (num_ports == 4) ?
				p4_rx_fair[port] : p2_rx_fair[port];
			custom_start_rdc[port] = start_rdc;
			start_rdc += custom_num_rdc[port];
		}
		break;

	case EQUAL:
		cfg_prop = "equal";
		for (port = 0; port < num_ports; port++) {
			custom_num_rdc[port] = (num_ports == 4) ?
				p4_rx_equal[port] :
				p2_rx_equal[port];
			custom_start_rdc[port] = start_rdc;
			start_rdc += custom_num_rdc[port];
		}
		break;

	case CUSTOM:
		cfg_prop = "custom";
		/* See if it is good config */
		num_rdc = 0;
		for (port = 0; port < num_ports; port++) {
			ddi_status = ddi_prop_lookup_int_array(
				DDI_DEV_T_ANY,
				s_dip[port], 0,
				start_rdc_prop,
				&prop_val,
				&prop_len);
			if (ddi_status == DDI_SUCCESS)
				custom_start_rdc[port] = *prop_val;
			else {
				NXGE_DEBUG_MSG((nxgep, CFG_CTL,
						" %s custom start port %d"
						" read failed ",
						" rxdma-cfg", port));
				bad_config = B_TRUE;
				status |= NXGE_DDI_FAILED;
			}
			if ((custom_start_rdc[port] == -1) ||
				(custom_start_rdc[port] >=
					NXGE_MAX_RDCS)) {
				NXGE_DEBUG_MSG((nxgep, CFG_CTL,
						" %s custom start %d"
						" out of range %x ",
						" rxdma-cfg",
						port,
						custom_start_rdc[port]));
				bad_config = B_TRUE;
				break;
			}
			ddi_status = ddi_prop_lookup_int_array(
				DDI_DEV_T_ANY,
				s_dip[port],
				0,
				num_rdc_prop,
				&prop_val,
				&prop_len);

			if (ddi_status == DDI_SUCCESS)
				custom_num_rdc[port] = *prop_val;
			else {
				NXGE_DEBUG_MSG((nxgep, CFG_CTL,
					" %s custom num port %d"
					" read failed ",
					"rxdma-cfg", port));
				bad_config = B_TRUE;
				status |= NXGE_DDI_FAILED;
			}

			if ((custom_num_rdc[port] == -1) ||
					(custom_num_rdc[port] >
						NXGE_MAX_RDCS) ||
					((custom_num_rdc[port] +
						custom_start_rdc[port]) >
					NXGE_MAX_RDCS)) {
				NXGE_DEBUG_MSG((nxgep, CFG_CTL,
					" %s custom num %d"
					" out of range %x ",
					" rxdma-cfg",
					port, custom_num_rdc[port]));
				bad_config = B_TRUE;
				break;
			}
			num_rdc += custom_num_rdc[port];
			if (num_rdc > NXGE_MAX_RDCS) {
				bad_config = B_TRUE;
				break;
			}
			rdc_bitmap[port] = 0;
			for (bits = 0;
				bits < custom_num_rdc[port]; bits++) {
				rdc_bitmap[port] |=
					(1 << (bits + custom_start_rdc[port]));
			}
		}

		if (bad_config == B_FALSE) {
			/* check for overlap */
			for (port = 0; port < num_ports - 1; port++) {
				for (j = port + 1; j < num_ports; j++) {
					if (rdc_bitmap[port] &
						rdc_bitmap[j]) {
						NXGE_DEBUG_MSG((nxgep,
							CFG_CTL,
							" rxdma-cfg"
							" property custom"
							" bit overlap"
							" %d %d ",
							port, j));
						bad_config = B_TRUE;
						break;
					}
				}
				if (bad_config == B_TRUE)
					break;
			}
		}
		if (bad_config == B_TRUE) {
			/* use default config */
			NXGE_DEBUG_MSG((nxgep, CFG_CTL,
				" rxdma-cfg property:"
				" bad custom config:"
				" use default"));
			for (port = 0; port < num_ports; port++) {
				custom_num_rdc[port] =
					(num_ports == 4) ?
					p4_rx_fair[port] :
					p2_rx_fair[port];
				custom_start_rdc[port] = start_rdc;
				start_rdc += custom_num_rdc[port];
			}
		}
		break;

	default:
		/* use default config */
		cfg_prop = "fair";
		for (port = 0; port < num_ports; port++) {
			custom_num_rdc[port] = (num_ports == 4) ?
				p4_rx_fair[port] : p2_rx_fair[port];
			custom_start_rdc[port] = start_rdc;
			start_rdc += custom_num_rdc[port];
		}
		break;
	}

	/* Now Update the rx properties */
	for (port = 0; port < num_ports; port++) {
		NXGE_DEBUG_MSG((nxgep, CFG_CTL,
			" update property rxdma-cfg with %s ", cfg_prop));
		ddi_status = ddi_prop_update_string(DDI_DEV_T_NONE, s_dip[port],
			"rxdma-cfg", cfg_prop);
		if (ddi_status != DDI_PROP_SUCCESS) {
			NXGE_DEBUG_MSG((nxgep, CFG_CTL,
				" property rxdma-cfg is not updating to %s",
				cfg_prop));
			status |= NXGE_DDI_FAILED;
		}
		NXGE_DEBUG_MSG((nxgep, CFG_CTL, " update property %s with %d ",
			num_rdc_prop, custom_num_rdc[port]));

		ddi_status = ddi_prop_update_int(DDI_DEV_T_NONE, s_dip[port],
			num_rdc_prop, custom_num_rdc[port]);

		if (ddi_status != DDI_PROP_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
				" property %s not updating with %d",
				num_rdc_prop, custom_num_rdc[port]));
			status |= NXGE_DDI_FAILED;
		}
		NXGE_DEBUG_MSG((nxgep, CFG_CTL, " update property %s with %d ",
			start_rdc_prop, custom_start_rdc[port]));
		ddi_status = ddi_prop_update_int(DDI_DEV_T_NONE, s_dip[port],
			start_rdc_prop, custom_start_rdc[port]);

		if (ddi_status != DDI_PROP_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
				" property %s not updating with %d ",
				start_rdc_prop, custom_start_rdc[port]));
			status |= NXGE_DDI_FAILED;
		}
	}
	if (status & NXGE_DDI_FAILED)
		status |= NXGE_ERROR;
	return (status);
}

static nxge_status_t
nxge_update_txdma_properties(p_nxge_t nxgep, config_token_t token,
	dev_info_t *s_dip[])
{
	nxge_status_t status = NXGE_OK;
	int ddi_status = DDI_SUCCESS;
	int num_ports = nxgep->nports;
	int port, bits, j;
	uint8_t start_tdc = 0, num_tdc = 0;
	p_nxge_param_t param_arr;
	uint32_t tdc_bitmap[MAX_SIBLINGS];
	int custom_start_tdc[MAX_SIBLINGS];
	int custom_num_tdc[MAX_SIBLINGS];
	uint8_t bad_config = B_FALSE;
	int *prop_val;
	uint_t prop_len;
	char *start_tdc_prop, *num_tdc_prop, *cfg_prop;

	start_tdc = 0;
	param_arr = nxgep->param_arr;
	start_tdc_prop = param_arr[param_txdma_channels_begin].fcode_name;
	num_tdc_prop = param_arr[param_txdma_channels].fcode_name;

	switch (token) {
	case FAIR:
		cfg_prop = "fair";
		for (port = 0; port < num_ports; port++) {
			custom_num_tdc[port] = (num_ports == 4) ?
				p4_tx_fair[port] : p2_tx_fair[port];
			custom_start_tdc[port] = start_tdc;
			start_tdc += custom_num_tdc[port];
		}
		break;

	case EQUAL:
		cfg_prop = "equal";
		for (port = 0; port < num_ports; port++) {
			custom_num_tdc[port] = (num_ports == 4) ?
				p4_tx_equal[port] : p2_tx_equal[port];
			custom_start_tdc[port] = start_tdc;
			start_tdc += custom_num_tdc[port];
		}
		break;

	case CUSTOM:
		cfg_prop = "custom";
		/* See if it is good config */
		num_tdc = 0;
		for (port = 0; port < num_ports; port++) {
			ddi_status = ddi_prop_lookup_int_array(
				DDI_DEV_T_ANY, s_dip[port], 0, start_tdc_prop,
				&prop_val, &prop_len);
			if (ddi_status == DDI_SUCCESS)
				custom_start_tdc[port] = *prop_val;
			else {
				NXGE_DEBUG_MSG((nxgep, CFG_CTL,
					" %s custom start port %d"
					" read failed ", " txdma-cfg", port));
				bad_config = B_TRUE;
				status |= NXGE_DDI_FAILED;
			}

			if ((custom_start_tdc[port] == -1) ||
					(custom_start_tdc[port] >=
					NXGE_MAX_RDCS)) {
				NXGE_DEBUG_MSG((nxgep, CFG_CTL,
					" %s custom start %d"
					" out of range %x ", " txdma-cfg",
					port, custom_start_tdc[port]));
				bad_config = B_TRUE;
				break;
			}

			ddi_status = ddi_prop_lookup_int_array(
				DDI_DEV_T_ANY, s_dip[port], 0, num_tdc_prop,
				&prop_val, &prop_len);
			if (ddi_status == DDI_SUCCESS)
				custom_num_tdc[port] = *prop_val;
			else {
				NXGE_DEBUG_MSG((nxgep, CFG_CTL,
					" %s custom num port %d"
					" read failed ", " txdma-cfg", port));
				bad_config = B_TRUE;
				status |= NXGE_DDI_FAILED;
			}

			if ((custom_num_tdc[port] == -1) ||
					(custom_num_tdc[port] >
						NXGE_MAX_TDCS) ||
					((custom_num_tdc[port] +
						custom_start_tdc[port]) >
					NXGE_MAX_TDCS)) {
				NXGE_DEBUG_MSG((nxgep, CFG_CTL,
					" %s custom num %d"
					" out of range %x ", " rxdma-cfg",
					port, custom_num_tdc[port]));
				bad_config = B_TRUE;
				break;
			}
			num_tdc += custom_num_tdc[port];
			if (num_tdc > NXGE_MAX_TDCS) {
				bad_config = B_TRUE;
				break;
			}
			tdc_bitmap[port] = 0;
			for (bits = 0;
				bits < custom_num_tdc[port]; bits++) {
				tdc_bitmap[port] |=
					(1 <<
					(bits + custom_start_tdc[port]));
			}

		}

		if (bad_config == B_FALSE) {
			/* check for overlap */
			for (port = 0; port < num_ports - 1; port++) {
				for (j = port + 1; j < num_ports; j++) {
					if (tdc_bitmap[port] &
						tdc_bitmap[j]) {
						NXGE_DEBUG_MSG((nxgep, CFG_CTL,
							" rxdma-cfg"
							" property custom"
							" bit overlap"
							" %d %d ",
							port, j));
						bad_config = B_TRUE;
						break;
					}
				}
				if (bad_config == B_TRUE)
					break;
			}
		}
		if (bad_config == B_TRUE) {
			/* use default config */
			NXGE_DEBUG_MSG((nxgep, CFG_CTL,
				" txdma-cfg property:"
				" bad custom config:" " use default"));

			for (port = 0; port < num_ports; port++) {
				custom_num_tdc[port] = (num_ports == 4) ?
					p4_tx_fair[port] : p2_tx_fair[port];
				custom_start_tdc[port] = start_tdc;
				start_tdc += custom_num_tdc[port];
			}
		}
		break;

	default:
		/* use default config */
		cfg_prop = "fair";
		for (port = 0; port < num_ports; port++) {
			custom_num_tdc[port] = (num_ports == 4) ?
				p4_tx_fair[port] : p2_tx_fair[port];
			custom_start_tdc[port] = start_tdc;
			start_tdc += custom_num_tdc[port];
		}
		break;
	}

	/* Now Update the tx properties */
	for (port = 0; port < num_ports; port++) {
		NXGE_DEBUG_MSG((nxgep, CFG_CTL,
			" update property txdma-cfg with %s ", cfg_prop));
		ddi_status = ddi_prop_update_string(DDI_DEV_T_NONE, s_dip[port],
			"txdma-cfg", cfg_prop);
		if (ddi_status != DDI_PROP_SUCCESS) {
			NXGE_DEBUG_MSG((nxgep, CFG_CTL,
				" property txdma-cfg is not updating to %s",
				cfg_prop));
			status |= NXGE_DDI_FAILED;
		}
		NXGE_DEBUG_MSG((nxgep, CFG_CTL, " update property %s with %d ",
			num_tdc_prop, custom_num_tdc[port]));

		ddi_status = ddi_prop_update_int(DDI_DEV_T_NONE, s_dip[port],
			num_tdc_prop, custom_num_tdc[port]);

		if (ddi_status != DDI_PROP_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
				" property %s not updating with %d",
				num_tdc_prop,
				custom_num_tdc[port]));
			status |= NXGE_DDI_FAILED;
		}

		NXGE_DEBUG_MSG((nxgep, CFG_CTL, " update property %s with %d ",
			start_tdc_prop, custom_start_tdc[port]));

		ddi_status = ddi_prop_update_int(DDI_DEV_T_NONE, s_dip[port],
			start_tdc_prop, custom_start_tdc[port]);
		if (ddi_status != DDI_PROP_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
				" property %s not updating with %d ",
				start_tdc_prop, custom_start_tdc[port]));
			status |= NXGE_DDI_FAILED;
		}
	}
	if (status & NXGE_DDI_FAILED)
		status |= NXGE_ERROR;
	return (status);
}

static nxge_status_t
nxge_update_cfg_properties(p_nxge_t nxgep, uint32_t flags,
	config_token_t token, dev_info_t *s_dip[])
{
	nxge_status_t status = NXGE_OK;

	switch (flags) {
	case COMMON_TXDMA_CFG:
		if (nxge_dma_obp_props_only == 0)
			status = nxge_update_txdma_properties(nxgep,
				token, s_dip);
		break;
	case COMMON_RXDMA_CFG:
		if (nxge_dma_obp_props_only == 0)
			status = nxge_update_rxdma_properties(nxgep,
				token, s_dip);

		break;
	case COMMON_RXDMA_GRP_CFG:
		status = nxge_update_rxdma_grp_properties(nxgep,
			token, s_dip);
		break;
	default:
		return (NXGE_ERROR);
	}
	return (status);
}

/*
 * verify consistence.
 * (May require publishing the properties on all the ports.
 *
 * What if properties are published on function 0 device only?
 *
 *
 * rxdma-cfg, txdma-cfg, rxdma-grp-cfg (required )
 * What about class configs?
 *
 * If consistent, update the property on all the siblings.
 * set  a flag on hardware shared register
 * The rest of the siblings will check the flag
 * if the flag is set, they will use the updated property
 * without doing any validation.
 */

nxge_status_t
nxge_cfg_verify_set_classify_prop(p_nxge_t nxgep, char *prop,
	uint64_t known_cfg, uint32_t override, dev_info_t *c_dip[])
{
	nxge_status_t status = NXGE_OK;
	int ddi_status = DDI_SUCCESS;
	int i = 0, found = 0, update_prop = B_TRUE;
	int *cfg_val;
	uint_t new_value, cfg_value[MAX_SIBLINGS];
	uint_t prop_len;
	uint_t known_cfg_value;

	known_cfg_value = (uint_t)known_cfg;

	if (override == B_TRUE) {
		new_value = known_cfg_value;
		for (i = 0; i < nxgep->nports; i++) {
			ddi_status = ddi_prop_update_int(DDI_DEV_T_NONE,
				c_dip[i], prop, new_value);
#ifdef NXGE_DEBUG_ERROR
			if (ddi_status != DDI_PROP_SUCCESS)
				NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
					" property %s failed update ", prop));
#endif
		}
		if (ddi_status != DDI_PROP_SUCCESS)
			return (NXGE_ERROR | NXGE_DDI_FAILED);
	}
	for (i = 0; i < nxgep->nports; i++) {
		cfg_value[i] = known_cfg_value;
		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, c_dip[i], 0,
				prop, &cfg_val,
				&prop_len) == DDI_PROP_SUCCESS) {
			cfg_value[i] = *cfg_val;
			ddi_prop_free(cfg_val);
			found++;
		}
	}

	if (found != i) {
		NXGE_DEBUG_MSG((nxgep, CFG_CTL,
			" property %s not specified on all ports", prop));
		if (found == 0) {
			/* not specified: Use default */
			NXGE_DEBUG_MSG((nxgep, CFG_CTL,
				" property %s not specified on any port:"
				" Using default", prop));
			new_value = known_cfg_value;
		} else {
			/* specified on some */
			NXGE_DEBUG_MSG((nxgep, CFG_CTL,
				" property %s not specified"
				" on some ports: Using default", prop));
			/* ? use p0 value instead ? */
			new_value = known_cfg_value;
		}
	} else {
		/* check type and consistence */
		/* found on all devices */
		for (i = 1; i < found; i++) {
			if (cfg_value[i] != cfg_value[i - 1]) {
				NXGE_DEBUG_MSG((nxgep, CFG_CTL,
					" property %s inconsistent:"
					" Using default", prop));
				new_value = known_cfg_value;
				break;
			}
			/*
			 * Found on all the ports and consistent. Nothing to
			 * do.
			 */
			update_prop = B_FALSE;
		}
	}

	if (update_prop == B_TRUE) {
		for (i = 0; i < nxgep->nports; i++) {
			ddi_status = ddi_prop_update_int(DDI_DEV_T_NONE,
				c_dip[i], prop, new_value);
#ifdef NXGE_DEBUG_ERROR
			if (ddi_status != DDI_SUCCESS)
				NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
					" property %s not updating with %d"
					" Using default",
					prop, new_value));
#endif
			if (ddi_status != DDI_PROP_SUCCESS)
				status |= NXGE_DDI_FAILED;
		}
	}
	if (status & NXGE_DDI_FAILED)
		status |= NXGE_ERROR;

	return (status);
}

static uint64_t
nxge_class_get_known_cfg(p_nxge_t nxgep, int class_prop, int rx_quick_cfg)
{
	int start_prop;
	uint64_t cfg_value;
	p_nxge_param_t param_arr;

	param_arr = nxgep->param_arr;
	cfg_value = param_arr[class_prop].value;
	start_prop = param_h1_init_value;

	/* update the properties per quick config */
	switch (rx_quick_cfg) {
	case CFG_L3_WEB:
	case CFG_L3_DISTRIBUTE:
		cfg_value = nxge_classify_get_cfg_value(nxgep,
			rx_quick_cfg, class_prop - start_prop);
		break;
	default:
		cfg_value = param_arr[class_prop].value;
		break;
	}
	return (cfg_value);
}

static nxge_status_t
nxge_cfg_verify_set_classify(p_nxge_t nxgep, dev_info_t *c_dip[])
{
	nxge_status_t status = NXGE_OK;
	int rx_quick_cfg, class_prop, start_prop, end_prop;
	char *prop_name;
	int override = B_TRUE;
	uint64_t cfg_value;
	p_nxge_param_t param_arr;

	param_arr = nxgep->param_arr;
	rx_quick_cfg = param_arr[param_rx_quick_cfg].value;
	start_prop = param_h1_init_value;
	end_prop = param_class_opt_ipv6_sctp;

	/* update the properties per quick config */
	if (rx_quick_cfg == CFG_NOT_SPECIFIED)
		override = B_FALSE;

	/*
	 * these parameter affect the classification outcome.
	 * these parameters are used to configure the Flow key and
	 * the TCAM key for each of the IP classes.
	 * Includee here are also the H1 and H2 initial values
	 * which affect the distribution as well as final hash value
	 * (hence the offset into RDC table and FCRAM bucket location)
	 *
	 */
	for (class_prop = start_prop; class_prop <= end_prop; class_prop++) {
		prop_name = param_arr[class_prop].fcode_name;
		cfg_value = nxge_class_get_known_cfg(nxgep,
			class_prop, rx_quick_cfg);
		status = nxge_cfg_verify_set_classify_prop(nxgep, prop_name,
			cfg_value, override, c_dip);
	}

	/*
	 * these properties do not affect the actual classification outcome.
	 * used to enable/disable or tune the fflp hardware
	 *
	 * fcram_access_ratio, tcam_access_ratio, tcam_enable, llc_snap_enable
	 *
	 */
	override = B_FALSE;
	for (class_prop = param_fcram_access_ratio;
			class_prop <= param_llc_snap_enable; class_prop++) {
		prop_name = param_arr[class_prop].fcode_name;
		cfg_value = param_arr[class_prop].value;
		status = nxge_cfg_verify_set_classify_prop(nxgep, prop_name,
			cfg_value, override, c_dip);
	}

	return (status);
}

nxge_status_t
nxge_cfg_verify_set(p_nxge_t nxgep, uint32_t flag)
{
	nxge_status_t status = NXGE_OK;
	int i = 0, found = 0;
	int num_siblings;
	dev_info_t *c_dip[MAX_SIBLINGS + 1];
	char *prop_val[MAX_SIBLINGS];
	config_token_t c_token[MAX_SIBLINGS];
	char *prop;

	if (nxge_dma_obp_props_only)
		return (NXGE_OK);

	num_siblings = 0;
	c_dip[num_siblings] = ddi_get_child(nxgep->p_dip);
	while (c_dip[num_siblings]) {
		c_dip[num_siblings + 1] =
			ddi_get_next_sibling(c_dip[num_siblings]);
		num_siblings++;
	}

	switch (flag) {
	case COMMON_TXDMA_CFG:
		prop = "txdma-cfg";
		break;
	case COMMON_RXDMA_CFG:
		prop = "rxdma-cfg";
		break;
	case COMMON_RXDMA_GRP_CFG:
		prop = "rxdma-grp-cfg";
		break;
	case COMMON_CLASS_CFG:
		status = nxge_cfg_verify_set_classify(nxgep, c_dip);
		return (status);
	default:
		return (NXGE_ERROR);
	}

	i = 0;
	while (i < num_siblings) {
		if (ddi_prop_lookup_string(DDI_DEV_T_ANY, c_dip[i], 0, prop,
				(char **)&prop_val[i]) == DDI_PROP_SUCCESS) {
			c_token[i] = nxge_get_config_token(prop_val[i]);
			ddi_prop_free(prop_val[i]);
			found++;
		} else
			c_token[i] = CONFIG_TOKEN_NONE;
		i++;
	}

	if (found != i) {
		if (found == 0) {
			/* not specified: Use default */
			NXGE_DEBUG_MSG((nxgep, CFG_CTL,
				" property %s not specified on any port:"
					" Using default", prop));

			status = nxge_update_cfg_properties(nxgep,
				flag, FAIR, c_dip);
			return (status);
		} else {
			/*
			 * if  the convention is to use function 0 device then
			 * populate the other devices with this configuration.
			 *
			 * The other alternative is to use the default config.
			 */
			/* not specified: Use default */
			NXGE_DEBUG_MSG((nxgep, CFG_CTL,
				" property %s not specified on some ports:"
				" Using default", prop));
			status = nxge_update_cfg_properties(nxgep,
				flag, FAIR, c_dip);
			return (status);
		}
	}

	/* check type and consistence */
	/* found on all devices */
	for (i = 1; i < found; i++) {
		if (c_token[i] != c_token[i - 1]) {
			NXGE_DEBUG_MSG((nxgep, CFG_CTL,
				" property %s inconsistent:"
				" Using default", prop));
			status = nxge_update_cfg_properties(nxgep,
				flag, FAIR, c_dip);
			return (status);
		}
	}

	/*
	 * Found on all the ports check if it is custom configuration. if
	 * custom, then verify consistence
	 *
	 * finally create soft properties
	 */
	status = nxge_update_cfg_properties(nxgep, flag, c_token[0], c_dip);
	return (status);
}

nxge_status_t
nxge_cfg_verify_set_quick_config(p_nxge_t nxgep)
{
	nxge_status_t status = NXGE_OK;
	int ddi_status = DDI_SUCCESS;
	char *prop_val;
	char *rx_prop;
	char *prop;
	uint32_t cfg_value = CFG_NOT_SPECIFIED;
	p_nxge_param_t param_arr;

	param_arr = nxgep->param_arr;
	rx_prop = param_arr[param_rx_quick_cfg].fcode_name;

	prop = "rx-quick-cfg";

	/*
	 * good value are
	 *
	 * "web-server" "generic-server" "l3-classify" "flow-classify"
	 */
	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, nxgep->dip, 0,
			prop, (char **)&prop_val) != DDI_PROP_SUCCESS) {
		NXGE_DEBUG_MSG((nxgep, VPD_CTL,
			" property %s not specified: using default ", prop));
		cfg_value = CFG_NOT_SPECIFIED;
	} else {
		cfg_value = CFG_L3_DISTRIBUTE;
		if (strncmp("web-server", (caddr_t)prop_val, 8) == 0) {
			cfg_value = CFG_L3_WEB;
			NXGE_DEBUG_MSG((nxgep, CFG_CTL,
				" %s: web server ", prop));
		}
		if (strncmp("generic-server", (caddr_t)prop_val, 8) == 0) {
			cfg_value = CFG_L3_DISTRIBUTE;
			NXGE_DEBUG_MSG((nxgep, CFG_CTL,
				" %s: distribute ", prop));
		}
		/* more */
		ddi_prop_free(prop_val);
	}

	ddi_status = ddi_prop_update_int(DDI_DEV_T_NONE, nxgep->dip,
		rx_prop, cfg_value);
	if (ddi_status != DDI_PROP_SUCCESS)
		status |= NXGE_DDI_FAILED;

	/* now handle specified cases: */
	if (status & NXGE_DDI_FAILED)
		status |= NXGE_ERROR;
	return (status);
}

static void
nxge_use_cfg_link_cfg(p_nxge_t nxgep)
{
	int *prop_val;
	uint_t prop_len;
	dev_info_t *dip;
	int speed;
	int duplex;
	int adv_autoneg_cap;
	int adv_10gfdx_cap;
	int adv_10ghdx_cap;
	int adv_1000fdx_cap;
	int adv_1000hdx_cap;
	int adv_100fdx_cap;
	int adv_100hdx_cap;
	int adv_10fdx_cap;
	int adv_10hdx_cap;
	int status = DDI_SUCCESS;

	dip = nxgep->dip;

	/*
	 * first find out the card type and the supported link speeds and
	 * features
	 */
	/* add code for card type */
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, "adv-autoneg-cap",
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		ddi_prop_free(prop_val);
		return;
	}

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, "adv-10gfdx-cap",
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		ddi_prop_free(prop_val);
		return;
	}

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, "adv-1000hdx-cap",
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		ddi_prop_free(prop_val);
		return;
	}

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, "adv-1000fdx-cap",
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		ddi_prop_free(prop_val);
		return;
	}

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, "adv-100fdx-cap",
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		ddi_prop_free(prop_val);
		return;
	}

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, "adv-100hdx-cap",
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		ddi_prop_free(prop_val);
		return;
	}

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, "adv-10fdx-cap",
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		ddi_prop_free(prop_val);
		return;
	}

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, "adv-10hdx-cap",
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		ddi_prop_free(prop_val);
		return;
	}

	if (ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, dip, 0, "speed",
			(uchar_t **)&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		if (strncmp("10000", (caddr_t)prop_val,
				(size_t)prop_len) == 0) {
			speed = 10000;
		} else if (strncmp("1000", (caddr_t)prop_val,
				(size_t)prop_len) == 0) {
			speed = 1000;
		} else if (strncmp("100", (caddr_t)prop_val,
				(size_t)prop_len) == 0) {
			speed = 100;
		} else if (strncmp("10", (caddr_t)prop_val,
				(size_t)prop_len) == 0) {
			speed = 10;
		} else if (strncmp("auto", (caddr_t)prop_val,
				(size_t)prop_len) == 0) {
			speed = 0;
		} else {
			NXGE_ERROR_MSG((nxgep, NXGE_NOTE,
				"speed property is invalid reverting to auto"));
			speed = 0;
		}
		ddi_prop_free(prop_val);
	} else
		speed = 0;

	if (ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, dip, 0, "duplex",
			(uchar_t **)&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		if (strncmp("full", (caddr_t)prop_val,
				(size_t)prop_len) == 0) {
			duplex = 2;
		} else if (strncmp("half", (caddr_t)prop_val,
				(size_t)prop_len) == 0) {
			duplex = 1;
		} else if (strncmp("auto", (caddr_t)prop_val,
				(size_t)prop_len) == 0) {
			duplex = 0;
		} else {
			NXGE_ERROR_MSG((nxgep, NXGE_NOTE,
				"duplex property is invalid"
				" reverting to auto"));
			duplex = 0;
		}
		ddi_prop_free(prop_val);
	} else
		duplex = 0;

	adv_autoneg_cap = (speed == 0) || (duplex == 0);
	if (adv_autoneg_cap == 0) {
		adv_10gfdx_cap = ((speed == 10000) && (duplex == 2));
		adv_10ghdx_cap = adv_10gfdx_cap;
		adv_10ghdx_cap |= ((speed == 10000) && (duplex == 1));
		adv_1000fdx_cap = adv_10ghdx_cap;
		adv_1000fdx_cap |= ((speed == 1000) && (duplex == 2));
		adv_1000hdx_cap = adv_1000fdx_cap;
		adv_1000hdx_cap |= ((speed == 1000) && (duplex == 1));
		adv_100fdx_cap = adv_1000hdx_cap;
		adv_100fdx_cap |= ((speed == 100) && (duplex == 2));
		adv_100hdx_cap = adv_100fdx_cap;
		adv_100hdx_cap |= ((speed == 100) && (duplex == 1));
		adv_10fdx_cap = adv_100hdx_cap;
		adv_10fdx_cap |= ((speed == 10) && (duplex == 2));
		adv_10hdx_cap = adv_10fdx_cap;
		adv_10hdx_cap |= ((speed == 10) && (duplex == 1));
	} else if (speed == 0) {
		adv_10gfdx_cap = (duplex == 2);
		adv_10ghdx_cap = (duplex == 1);
		adv_1000fdx_cap = (duplex == 2);
		adv_1000hdx_cap = (duplex == 1);
		adv_100fdx_cap = (duplex == 2);
		adv_100hdx_cap = (duplex == 1);
		adv_10fdx_cap = (duplex == 2);
		adv_10hdx_cap = (duplex == 1);
	}
	if (duplex == 0) {
		adv_10gfdx_cap = (speed == 0);
		adv_10gfdx_cap |= (speed == 10000);
		adv_10ghdx_cap = adv_10gfdx_cap;
		adv_10ghdx_cap |= (speed == 10000);
		adv_1000fdx_cap = adv_10ghdx_cap;
		adv_1000fdx_cap |= (speed == 1000);
		adv_1000hdx_cap = adv_1000fdx_cap;
		adv_1000hdx_cap |= (speed == 1000);
		adv_100fdx_cap = adv_1000hdx_cap;
		adv_100fdx_cap |= (speed == 100);
		adv_100hdx_cap = adv_100fdx_cap;
		adv_100hdx_cap |= (speed == 100);
		adv_10fdx_cap = adv_100hdx_cap;
		adv_10fdx_cap |= (speed == 10);
		adv_10hdx_cap = adv_10fdx_cap;
		adv_10hdx_cap |= (speed == 10);
	}
	status = ddi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		"adv-autoneg-cap", &adv_autoneg_cap, 1);
	if (status)
		return;

	status = ddi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		"adv-10gfdx-cap", &adv_10gfdx_cap, 1);
	if (status)
		goto nxge_map_myargs_to_gmii_fail1;

	status = ddi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		"adv-10ghdx-cap", &adv_10ghdx_cap, 1);
	if (status)
		goto nxge_map_myargs_to_gmii_fail2;

	status = ddi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		"adv-1000fdx-cap", &adv_1000fdx_cap, 1);
	if (status)
		goto nxge_map_myargs_to_gmii_fail3;

	status = ddi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		"adv-1000hdx-cap", &adv_1000hdx_cap, 1);
	if (status)
		goto nxge_map_myargs_to_gmii_fail4;

	status = ddi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		"adv-100fdx-cap", &adv_100fdx_cap, 1);
	if (status)
		goto nxge_map_myargs_to_gmii_fail5;

	status = ddi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		"adv-100hdx-cap", &adv_100hdx_cap, 1);
	if (status)
		goto nxge_map_myargs_to_gmii_fail6;

	status = ddi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		"adv-10fdx-cap", &adv_10fdx_cap, 1);
	if (status)
		goto nxge_map_myargs_to_gmii_fail7;

	status = ddi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		"adv-10hdx-cap", &adv_10hdx_cap, 1);
	if (status)
		goto nxge_map_myargs_to_gmii_fail8;

	return;

nxge_map_myargs_to_gmii_fail9:
	(void) ddi_prop_remove(DDI_DEV_T_NONE, dip, "adv-10hdx-cap");

nxge_map_myargs_to_gmii_fail8:
	(void) ddi_prop_remove(DDI_DEV_T_NONE, dip, "adv-10fdx-cap");

nxge_map_myargs_to_gmii_fail7:
	(void) ddi_prop_remove(DDI_DEV_T_NONE, dip, "adv-100hdx-cap");

nxge_map_myargs_to_gmii_fail6:
	(void) ddi_prop_remove(DDI_DEV_T_NONE, dip, "adv-100fdx-cap");

nxge_map_myargs_to_gmii_fail5:
	(void) ddi_prop_remove(DDI_DEV_T_NONE, dip, "adv-1000hdx-cap");

nxge_map_myargs_to_gmii_fail4:
	(void) ddi_prop_remove(DDI_DEV_T_NONE, dip, "adv-1000fdx-cap");

nxge_map_myargs_to_gmii_fail3:
	(void) ddi_prop_remove(DDI_DEV_T_NONE, dip, "adv-10ghdx-cap");

nxge_map_myargs_to_gmii_fail2:
	(void) ddi_prop_remove(DDI_DEV_T_NONE, dip, "adv-10gfdx-cap");

nxge_map_myargs_to_gmii_fail1:
	(void) ddi_prop_remove(DDI_DEV_T_NONE, dip, "adv-autoneg-cap");
}

nxge_status_t
nxge_get_config_properties(p_nxge_t nxgep)
{
	nxge_status_t status = NXGE_OK;
	p_nxge_hw_list_t hw_p;
	uint_t prop_len;
	uchar_t *prop_val8;

	NXGE_DEBUG_MSG((nxgep, VPD_CTL, " ==> nxge_get_config_properties"));

	if ((hw_p = nxgep->nxge_hw_p) == NULL) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			" nxge_get_config_properties:"
			" common hardware not set", nxgep->niu_type));
		return (NXGE_ERROR);
	}

	/*
	 * Get info on how many ports Neptune card has.
	 */
	switch (nxgep->niu_type) {
	case N2_NIU:
		nxgep->nports = 2;
		nxgep->classifier.tcam_size = TCAM_NIU_TCAM_MAX_ENTRY;
		if (nxgep->function_num > 1) {
			return (NXGE_ERROR);
		}
		break;
	case NEPTUNE_2:
		if (nxgep->function_num > 1)
			return (NXGE_ERROR);

		/* Set Board Version Number */
		nxgep->board_ver = 0;
		if (ddi_prop_lookup_byte_array(DDI_DEV_T_ANY,
				nxgep->dip, 0, "board-model", &prop_val8,
				&prop_len) == DDI_PROP_SUCCESS) {
			if (prop_len > 9) {
				if ((prop_val8[9] == '0') &&
						(prop_val8[10] == '4'))
					nxgep->board_ver = 4;
			}
			ddi_prop_free(prop_val8);
		}
		status = nxge_espc_num_ports_get(nxgep);
		if (status != NXGE_OK)
			return (NXGE_ERROR);

		nxgep->classifier.tcam_size = TCAM_NXGE_TCAM_MAX_ENTRY;
		break;

	case NEPTUNE:
	default:
		status = nxge_espc_num_ports_get(nxgep);
		if (status != NXGE_OK)
			return (NXGE_ERROR);
		nxgep->classifier.tcam_size = TCAM_NXGE_TCAM_MAX_ENTRY;
		break;
	}

	status = nxge_get_mac_addr_properties(nxgep);
	if (status != NXGE_OK)
		return (NXGE_ERROR);

	/*
	 * read the configuration type. If none is specified, used default.
	 * Config types: equal: (default) DMA channels, RDC groups, TCAM, FCRAM
	 * are shared equally across all the ports.
	 *
	 * Fair: DMA channels, RDC groups, TCAM, FCRAM are shared proprtional to
	 * te port speed.
	 *
	 *
	 * custom: DMA channels, RDC groups, TCAM, FCRAM partition is
	 * specified in nxge.conf. Need to read each parameter and set
	 * up the parameters in nxge structures.
	 *
	 */
	switch (nxgep->niu_type) {
	case N2_NIU:
		NXGE_DEBUG_MSG((nxgep, VPD_CTL,
			" ==> nxge_get_config_properties: N2"));
		MUTEX_ENTER(&hw_p->nxge_cfg_lock);
		if ((hw_p->flags & COMMON_CFG_VALID) !=
			COMMON_CFG_VALID) {
			status = nxge_cfg_verify_set(nxgep,
				COMMON_RXDMA_GRP_CFG);
			status = nxge_cfg_verify_set(nxgep,
				COMMON_CLASS_CFG);
			hw_p->flags |= COMMON_CFG_VALID;
		}
		MUTEX_EXIT(&hw_p->nxge_cfg_lock);
		status = nxge_use_cfg_n2niu_properties(nxgep);
		break;

	case NEPTUNE:
		NXGE_DEBUG_MSG((nxgep, VPD_CTL,
			" ==> nxge_get_config_properties: Neptune"));
		status = nxge_cfg_verify_set_quick_config(nxgep);
		MUTEX_ENTER(&hw_p->nxge_cfg_lock);
		if ((hw_p->flags & COMMON_CFG_VALID) !=
			COMMON_CFG_VALID) {
			status = nxge_cfg_verify_set(nxgep,
				COMMON_TXDMA_CFG);
			status = nxge_cfg_verify_set(nxgep,
				COMMON_RXDMA_CFG);
			status = nxge_cfg_verify_set(nxgep,
				COMMON_RXDMA_GRP_CFG);
			status = nxge_cfg_verify_set(nxgep,
				COMMON_CLASS_CFG);
			hw_p->flags |= COMMON_CFG_VALID;
		}
		MUTEX_EXIT(&hw_p->nxge_cfg_lock);
		nxge_use_cfg_neptune_properties(nxgep);
		status = NXGE_OK;
		break;

	case NEPTUNE_2:
		NXGE_DEBUG_MSG((nxgep, VPD_CTL,
			" ==> nxge_get_config_properties: Neptune-2"));
		if (nxgep->function_num > 1)
			return (NXGE_ERROR);
		status = nxge_cfg_verify_set_quick_config(nxgep);
		MUTEX_ENTER(&hw_p->nxge_cfg_lock);

		if ((hw_p->flags & COMMON_CFG_VALID) !=
			COMMON_CFG_VALID) {
			status = nxge_cfg_verify_set(nxgep,
				COMMON_TXDMA_CFG);
			status = nxge_cfg_verify_set(nxgep,
				COMMON_RXDMA_CFG);
			status = nxge_cfg_verify_set(nxgep,
				COMMON_RXDMA_GRP_CFG);
			status = nxge_cfg_verify_set(nxgep,
				COMMON_CLASS_CFG);
			hw_p->flags |= COMMON_CFG_VALID;
		}
		MUTEX_EXIT(&hw_p->nxge_cfg_lock);

		nxge_use_cfg_neptune_properties(nxgep);
		status = NXGE_OK;
		break;

	default:
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			" nxge_get_config_properties:"
			" unknown NIU type %x", nxgep->niu_type));
		return (NXGE_ERROR);
	}

	NXGE_DEBUG_MSG((nxgep, VPD_CTL, " <== nxge_get_config_properties"));
	return (status);
}

static nxge_status_t
nxge_use_cfg_n2niu_properties(p_nxge_t nxgep)
{
	nxge_status_t status = NXGE_OK;

	NXGE_DEBUG_MSG((nxgep, CFG_CTL, " ==> nxge_use_cfg_n2niu_properties"));

	status = nxge_use_default_dma_config_n2(nxgep);
	if (status != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			" ==> nxge_use_cfg_n2niu_properties (err 0x%x)",
			status));
		return (status | NXGE_ERROR);
	}

	(void) nxge_use_cfg_vlan_class_config(nxgep);
	(void) nxge_use_cfg_mac_class_config(nxgep);
	(void) nxge_use_cfg_class_config(nxgep);
	(void) nxge_use_cfg_link_cfg(nxgep);

	/*
	 * Read in the hardware (fcode) properties. Use the ndd array to read
	 * each property.
	 */
	(void) nxge_get_param_soft_properties(nxgep);
	NXGE_DEBUG_MSG((nxgep, CFG_CTL, " <== nxge_use_cfg_n2niu_properties"));

	return (status);
}

static void
nxge_use_cfg_neptune_properties(p_nxge_t nxgep)
{
	NXGE_DEBUG_MSG((nxgep, CFG_CTL, "==> nxge_use_cfg_neptune_properties"));

	(void) nxge_use_cfg_dma_config(nxgep);
	(void) nxge_use_cfg_vlan_class_config(nxgep);
	(void) nxge_use_cfg_mac_class_config(nxgep);
	(void) nxge_use_cfg_class_config(nxgep);
	(void) nxge_use_cfg_link_cfg(nxgep);

	/*
	 * Read in the hardware (fcode) properties. Use the ndd array to read
	 * each property.
	 */
	(void) nxge_get_param_soft_properties(nxgep);
	NXGE_DEBUG_MSG((nxgep, CFG_CTL, "<== nxge_use_cfg_neptune_properties"));
}

/*
 * FWARC 2006/556
 */

static nxge_status_t
nxge_use_default_dma_config_n2(p_nxge_t nxgep)
{
	int ndmas;
	int nrxgp;
	uint8_t func;
	p_nxge_dma_pt_cfg_t p_dma_cfgp;
	p_nxge_hw_pt_cfg_t p_cfgp;
	int *prop_val;
	uint_t prop_len;
	int i;
	nxge_status_t status = NXGE_OK;

	NXGE_DEBUG_MSG((nxgep, OBP_CTL, "==> nxge_use_default_dma_config_n2"));

	p_dma_cfgp = (p_nxge_dma_pt_cfg_t)&nxgep->pt_config;
	p_cfgp = (p_nxge_hw_pt_cfg_t)&p_dma_cfgp->hw_config;

	func = nxgep->function_num;
	p_cfgp->function_number = func;
	ndmas = NXGE_TDMA_PER_NIU_PORT;
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, nxgep->dip, 0,
			"tx-dma-channels", (int **)&prop_val,
			&prop_len) == DDI_PROP_SUCCESS) {
		p_cfgp->start_tdc = prop_val[0];
		NXGE_DEBUG_MSG((nxgep, OBP_CTL,
			"==> nxge_use_default_dma_config_n2: tdc starts %d "
			"(#%d)", p_cfgp->start_tdc, prop_len));

		ndmas = prop_val[1];
		NXGE_DEBUG_MSG((nxgep, OBP_CTL,
			"==> nxge_use_default_dma_config_n2: #tdc %d (#%d)",
			ndmas, prop_len));
		ddi_prop_free(prop_val);
	} else {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			"==> nxge_use_default_dma_config_n2: "
			"get tx-dma-channels failed"));
		return (NXGE_DDI_FAILED);
	}

	p_cfgp->max_tdcs = nxgep->max_tdcs = ndmas;
	nxgep->tdc_mask = (ndmas - 1);

	NXGE_DEBUG_MSG((nxgep, OBP_CTL, "==> nxge_use_default_dma_config_n2: "
		"p_cfgp 0x%llx max_tdcs %d nxgep->max_tdcs %d start %d",
		p_cfgp, p_cfgp->max_tdcs, nxgep->max_tdcs, p_cfgp->start_tdc));

	/* Receive DMA */
	ndmas = NXGE_RDMA_PER_NIU_PORT;
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, nxgep->dip, 0,
			"rx-dma-channels", (int **)&prop_val,
			&prop_len) == DDI_PROP_SUCCESS) {
		p_cfgp->start_rdc = prop_val[0];
		NXGE_DEBUG_MSG((nxgep, OBP_CTL,
			"==> nxge_use_default_dma_config_n2(obp): rdc start %d"
			" (#%d)", p_cfgp->start_rdc, prop_len));
		ndmas = prop_val[1];
		NXGE_DEBUG_MSG((nxgep, OBP_CTL,
			"==> nxge_use_default_dma_config_n2(obp):#rdc %d (#%d)",
			ndmas, prop_len));
		ddi_prop_free(prop_val);
	} else {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			"==> nxge_use_default_dma_config_n2: "
			"get rx-dma-channel failed"));
		return (NXGE_DDI_FAILED);
	}

	p_cfgp->max_rdcs = nxgep->max_rdcs = ndmas;
	nxgep->rdc_mask = (ndmas - 1);

	/* Hypervisor: rdc # and group # use the same # !! */
	p_cfgp->max_grpids = p_cfgp->max_rdcs + p_cfgp->max_tdcs;
	p_cfgp->start_grpid = 0;
	p_cfgp->mif_ldvid = p_cfgp->mac_ldvid = p_cfgp->ser_ldvid = 0;

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, nxgep->dip, 0,
			"interrupts", (int **)&prop_val,
			&prop_len) == DDI_PROP_SUCCESS) {
		/*
		 * For each device assigned, the content of each interrupts
		 * property is its logical device group.
		 *
		 * Assignment of interrupts property is in the the following
		 * order:
		 *
		 * MAC MIF (if configured) SYSTEM ERROR (if configured) first
		 * receive channel next channel...... last receive channel
		 * first transmit channel next channel...... last transmit
		 * channel
		 *
		 * prop_len should be at least for one mac and total # of rx and
		 * tx channels. Function 0 owns MIF and ERROR
		 */
		NXGE_DEBUG_MSG((nxgep, OBP_CTL,
			"==> nxge_use_default_dma_config_n2(obp): "
			"# interrupts %d", prop_len));

		switch (func) {
		case 0:
			p_cfgp->ldg_chn_start = 3;
			p_cfgp->mac_ldvid = NXGE_MAC_LD_PORT0;
			p_cfgp->mif_ldvid = NXGE_MIF_LD;
			p_cfgp->ser_ldvid = NXGE_SYS_ERROR_LD;

			break;
		case 1:
			p_cfgp->ldg_chn_start = 1;
			p_cfgp->mac_ldvid = NXGE_MAC_LD_PORT1;

			break;
		default:
			status = NXGE_DDI_FAILED;
			break;
		}

		if (status != NXGE_OK)
			return (status);

		for (i = 0; i < prop_len; i++) {
			p_cfgp->ldg[i] = prop_val[i];
			NXGE_DEBUG_MSG((nxgep, OBP_CTL,
				"==> nxge_use_default_dma_config_n2(obp): "
				"interrupt #%d, ldg %d",
				i, p_cfgp->ldg[i]));
		}

		p_cfgp->max_grpids = prop_len;
		NXGE_DEBUG_MSG((nxgep, OBP_CTL,
			"==> nxge_use_default_dma_config_n2(obp): %d "
			"(#%d) maxgrpids %d channel starts %d",
			p_cfgp->mac_ldvid, i, p_cfgp->max_grpids,
			p_cfgp->ldg_chn_start));
		ddi_prop_free(prop_val);
	} else {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			"==> nxge_use_default_dma_config_n2: "
			"get interrupts failed"));
		return (NXGE_DDI_FAILED);
	}

	p_cfgp->max_ldgs = p_cfgp->max_grpids;
	NXGE_DEBUG_MSG((nxgep, OBP_CTL,
		"==> nxge_use_default_dma_config_n2: "
		"p_cfgp 0x%llx max_rdcs %d nxgep->max_rdcs %d max_grpids %d"
		"start_grpid %d macid %d mifid %d serrid %d",
		p_cfgp, p_cfgp->max_rdcs, nxgep->max_rdcs, p_cfgp->max_grpids,
		p_cfgp->start_grpid,
		p_cfgp->mac_ldvid, p_cfgp->mif_ldvid, p_cfgp->ser_ldvid));

	NXGE_DEBUG_MSG((nxgep, OBP_CTL, "==> nxge_use_default_dma_config_n2: "
		"p_cfgp p%p start_ldg %d nxgep->max_ldgs %d",
		p_cfgp, p_cfgp->start_ldg, p_cfgp->max_ldgs));

	/*
	 * RDC groups and the beginning RDC group assigned to this function.
	 */
	nrxgp = 2;
	p_cfgp->max_rdc_grpids = nrxgp;
	p_cfgp->start_rdc_grpid = (nxgep->function_num * nrxgp);

	status = ddi_prop_update_int(DDI_DEV_T_NONE, nxgep->dip,
		"rx-rdc-grps", nrxgp);
	if (status) {
		return (NXGE_DDI_FAILED);
	}
	status = ddi_prop_update_int(DDI_DEV_T_NONE, nxgep->dip,
		"rx-rdc-grps-begin", p_cfgp->start_rdc_grpid);
	if (status) {
		(void) ddi_prop_remove(DDI_DEV_T_NONE, nxgep->dip,
			"rx-rdc-grps");
		return (NXGE_DDI_FAILED);
	}
	NXGE_DEBUG_MSG((nxgep, OBP_CTL, "==> nxge_use_default_dma_config_n2: "
		"p_cfgp $%p # rdc groups %d start rdc group id %d",
		p_cfgp, p_cfgp->max_rdc_grpids,
		p_cfgp->start_rdc_grpid));

	nxge_set_hw_dma_config(nxgep);
	NXGE_DEBUG_MSG((nxgep, OBP_CTL, "<== nxge_use_default_dma_config_n2"));
	return (status);
}

static void
nxge_use_cfg_dma_config(p_nxge_t nxgep)
{
	int tx_ndmas, rx_ndmas, nrxgp;
	p_nxge_dma_pt_cfg_t p_dma_cfgp;
	p_nxge_hw_pt_cfg_t p_cfgp;
	dev_info_t *dip;
	p_nxge_param_t param_arr;
	char *prop;
	int *prop_val;
	uint_t prop_len;

	NXGE_DEBUG_MSG((nxgep, CFG_CTL, " ==> nxge_use_cfg_dma_config"));
	param_arr = nxgep->param_arr;

	p_dma_cfgp = (p_nxge_dma_pt_cfg_t)&nxgep->pt_config;
	p_cfgp = (p_nxge_hw_pt_cfg_t)&p_dma_cfgp->hw_config;
	dip = nxgep->dip;
	p_cfgp->function_number = nxgep->function_num;
	prop = param_arr[param_txdma_channels_begin].fcode_name;

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, prop,
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		p_cfgp->start_tdc = *prop_val;
		ddi_prop_free(prop_val);
	} else {
		if (nxgep->nports == 2) {
			tx_ndmas = (nxgep->function_num * p2_tx_equal[0]);
		} else {
			tx_ndmas = (nxgep->function_num * p4_tx_equal[0]);
		}
		(void) ddi_prop_update_int(DDI_DEV_T_NONE, nxgep->dip,
			prop, tx_ndmas);
		p_cfgp->start_tdc = tx_ndmas;
	}

	prop = param_arr[param_txdma_channels].fcode_name;
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, prop,
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		tx_ndmas = *prop_val;
		ddi_prop_free(prop_val);
	} else {
		if (nxgep->nports == 2) {
			tx_ndmas = p2_tx_equal[0];
		} else {
			tx_ndmas = p4_tx_equal[0];
		}
		(void) ddi_prop_update_int(DDI_DEV_T_NONE, nxgep->dip,
			prop, tx_ndmas);
	}

	p_cfgp->max_tdcs = nxgep->max_tdcs = tx_ndmas;
	nxgep->tdc_mask = (tx_ndmas - 1);
	NXGE_DEBUG_MSG((nxgep, CFG_CTL, "==> nxge_use_cfg_dma_config: "
		"p_cfgp 0x%llx max_tdcs %d nxgep->max_tdcs %d",
		p_cfgp, p_cfgp->max_tdcs, nxgep->max_tdcs));

	prop = param_arr[param_rxdma_channels_begin].fcode_name;

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, prop,
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		p_cfgp->start_rdc = *prop_val;
		ddi_prop_free(prop_val);
	} else {
		if (nxgep->nports == 2) {
			rx_ndmas = (nxgep->function_num * p2_rx_equal[0]);
		} else {
			rx_ndmas = (nxgep->function_num * p4_rx_equal[0]);
		}
		(void) ddi_prop_update_int(DDI_DEV_T_NONE, nxgep->dip,
			prop, rx_ndmas);
		p_cfgp->start_rdc = rx_ndmas;
	}

	prop = param_arr[param_rxdma_channels].fcode_name;

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, prop,
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		rx_ndmas = *prop_val;
		ddi_prop_free(prop_val);
	} else {
		if (nxgep->nports == 2) {
			rx_ndmas = p2_rx_equal[0];
		} else {
			rx_ndmas = p4_rx_equal[0];
		}
		(void) ddi_prop_update_int(DDI_DEV_T_NONE, nxgep->dip,
			prop, rx_ndmas);
	}

	p_cfgp->max_rdcs = nxgep->max_rdcs = rx_ndmas;

	prop = param_arr[param_rdc_grps_start].fcode_name;
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, prop,
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		p_cfgp->start_rdc_grpid = *prop_val;
		ddi_prop_free(prop_val);
		NXGE_DEBUG_MSG((nxgep, CFG_CTL,
			"==> nxge_use_default_dma_config: "
			"use property " "start_grpid %d ",
			p_cfgp->start_grpid));
	} else {
		p_cfgp->start_rdc_grpid = nxgep->function_num;
		(void) ddi_prop_update_int(DDI_DEV_T_NONE, nxgep->dip,
			prop, p_cfgp->start_rdc_grpid);

		NXGE_DEBUG_MSG((nxgep, CFG_CTL,
			"==> nxge_use_default_dma_config: "
			"use default "
			"start_grpid %d (same as function #)",
			p_cfgp->start_grpid));
	}

	prop = param_arr[param_rx_rdc_grps].fcode_name;
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, prop,
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		nrxgp = *prop_val;
		ddi_prop_free(prop_val);
	} else {
		nrxgp = 1;
		(void) ddi_prop_update_int(DDI_DEV_T_NONE, nxgep->dip,
			prop, nrxgp);
		NXGE_DEBUG_MSG((nxgep, CFG_CTL,
			"==> nxge_use_default_dma_config: "
			"num_rdc_grpid not found: use def:# of "
			"rdc groups %d\n", nrxgp));
	}

	p_cfgp->max_rdc_grpids = nrxgp;

	/*
	 * 2/4 ports have the same hard-wired logical groups assigned.
	 */
	p_cfgp->start_ldg = nxgep->function_num * NXGE_LDGRP_PER_4PORTS;
	p_cfgp->max_ldgs = NXGE_LDGRP_PER_4PORTS;

	NXGE_DEBUG_MSG((nxgep, CFG_CTL, "==> nxge_use_default_dma_config: "
		"p_cfgp 0x%llx max_rdcs %d nxgep->max_rdcs %d max_grpids %d"
		"start_grpid %d",
		p_cfgp, p_cfgp->max_rdcs, nxgep->max_rdcs, p_cfgp->max_grpids,
		p_cfgp->start_grpid));

	NXGE_DEBUG_MSG((nxgep, CFG_CTL, "==> nxge_use_cfg_dma_config: "
		"p_cfgp 0x%016llx start_ldg %d nxgep->max_ldgs %d "
		"start_rdc_grpid %d",
		p_cfgp, p_cfgp->start_ldg, p_cfgp->max_ldgs,
		p_cfgp->start_rdc_grpid));

	prop = param_arr[param_rxdma_intr_time].fcode_name;

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, prop,
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		if ((prop_len > 0) && (prop_len <= p_cfgp->max_rdcs)) {
			(void) ddi_prop_update_int_array(DDI_DEV_T_NONE,
				nxgep->dip, prop, prop_val, prop_len);
		}
		ddi_prop_free(prop_val);
	}
	prop = param_arr[param_rxdma_intr_pkts].fcode_name;

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, 0, prop,
			&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		if ((prop_len > 0) && (prop_len <= p_cfgp->max_rdcs)) {
			(void) ddi_prop_update_int_array(DDI_DEV_T_NONE,
				nxgep->dip, prop, prop_val, prop_len);
		}
		ddi_prop_free(prop_val);
	}
	nxge_set_hw_dma_config(nxgep);

	NXGE_DEBUG_MSG((nxgep, CFG_CTL, "<== nxge_use_cfg_dma_config"));
}

static void
nxge_use_cfg_vlan_class_config(p_nxge_t nxgep)
{
	uint_t vlan_cnt;
	int *vlan_cfg_val;
	int status;
	p_nxge_param_t param_arr;
	char *prop;

	NXGE_DEBUG_MSG((nxgep, CFG_CTL, " ==> nxge_use_cfg_vlan_config"));
	param_arr = nxgep->param_arr;
	prop = param_arr[param_vlan_2rdc_grp].fcode_name;

	status = ddi_prop_lookup_int_array(DDI_DEV_T_ANY, nxgep->dip, 0, prop,
		&vlan_cfg_val, &vlan_cnt);
	if (status == DDI_PROP_SUCCESS) {
		status = ddi_prop_update_int_array(DDI_DEV_T_NONE,
			nxgep->dip, prop, vlan_cfg_val, vlan_cnt);
		ddi_prop_free(vlan_cfg_val);
	}
	nxge_set_hw_vlan_class_config(nxgep);
	NXGE_DEBUG_MSG((nxgep, CFG_CTL, " <== nxge_use_cfg_vlan_config"));
}

static void
nxge_use_cfg_mac_class_config(p_nxge_t nxgep)
{
	p_nxge_dma_pt_cfg_t p_dma_cfgp;
	p_nxge_hw_pt_cfg_t p_cfgp;
	uint_t mac_cnt;
	int *mac_cfg_val;
	int status;
	p_nxge_param_t param_arr;
	char *prop;

	NXGE_DEBUG_MSG((nxgep, CFG_CTL, "==> nxge_use_cfg_mac_class_config"));
	p_dma_cfgp = (p_nxge_dma_pt_cfg_t)&nxgep->pt_config;
	p_cfgp = (p_nxge_hw_pt_cfg_t)&p_dma_cfgp->hw_config;
	p_cfgp->start_mac_entry = 0;
	param_arr = nxgep->param_arr;
	prop = param_arr[param_mac_2rdc_grp].fcode_name;

	switch (nxgep->function_num) {
	case 0:
	case 1:
		/* 10G ports */
		p_cfgp->max_macs = NXGE_MAX_MACS_XMACS;
		break;
	case 2:
	case 3:
		/* 1G ports */
	default:
		p_cfgp->max_macs = NXGE_MAX_MACS_BMACS;
		break;
	}

	p_cfgp->mac_pref = 1;
	p_cfgp->def_mac_rxdma_grpid = p_cfgp->start_rdc_grpid;

	NXGE_DEBUG_MSG((nxgep, OBP_CTL,
		"== nxge_use_cfg_mac_class_config: "
		" mac_pref bit set def_mac_rxdma_grpid %d",
		p_cfgp->def_mac_rxdma_grpid));

	status = ddi_prop_lookup_int_array(DDI_DEV_T_ANY, nxgep->dip, 0, prop,
		&mac_cfg_val, &mac_cnt);
	if (status == DDI_PROP_SUCCESS) {
		if (mac_cnt <= p_cfgp->max_macs)
			status = ddi_prop_update_int_array(DDI_DEV_T_NONE,
				nxgep->dip, prop, mac_cfg_val, mac_cnt);
		ddi_prop_free(mac_cfg_val);
	}
	nxge_set_hw_mac_class_config(nxgep);
	NXGE_DEBUG_MSG((nxgep, CFG_CTL, " <== nxge_use_cfg_mac_class_config"));
}

static void
nxge_use_cfg_class_config(p_nxge_t nxgep)
{
	nxge_set_hw_class_config(nxgep);
}

static void
nxge_set_rdc_intr_property(p_nxge_t nxgep)
{
	int i;
	p_nxge_dma_pt_cfg_t p_dma_cfgp;

	NXGE_DEBUG_MSG((nxgep, CFG_CTL, " ==> nxge_set_rdc_intr_property"));
	p_dma_cfgp = (p_nxge_dma_pt_cfg_t)&nxgep->pt_config;

	for (i = 0; i < NXGE_MAX_RDCS; i++) {
		p_dma_cfgp->rcr_timeout[i] = nxge_rcr_timeout;
		p_dma_cfgp->rcr_threshold[i] = nxge_rcr_threshold;
	}

	NXGE_DEBUG_MSG((nxgep, CFG_CTL, " <== nxge_set_rdc_intr_property"));
}

static void
nxge_set_hw_dma_config(p_nxge_t nxgep)
{
	int i, j, rdc, ndmas, ngrps, bitmap, end, st_rdc;
	int32_t status;
	uint8_t rdcs_per_grp;
	p_nxge_dma_pt_cfg_t p_dma_cfgp;
	p_nxge_hw_pt_cfg_t p_cfgp;
	p_nxge_rdc_grp_t rdc_grp_p;
	int rdcgrp_cfg = CFG_NOT_SPECIFIED, rx_quick_cfg;
	char *prop, *prop_val;
	p_nxge_param_t param_arr;
	config_token_t token;

	NXGE_DEBUG_MSG((nxgep, CFG_CTL, "==> nxge_set_hw_dma_config"));

	p_dma_cfgp = (p_nxge_dma_pt_cfg_t)&nxgep->pt_config;
	p_cfgp = (p_nxge_hw_pt_cfg_t)&p_dma_cfgp->hw_config;
	rdc_grp_p = p_dma_cfgp->rdc_grps;

	/* Transmit DMA Channels */
	bitmap = 0;
	end = p_cfgp->start_tdc + p_cfgp->max_tdcs;
	nxgep->ntdc = p_cfgp->max_tdcs;
	p_dma_cfgp->tx_dma_map = 0;
	for (i = p_cfgp->start_tdc; i < end; i++) {
		bitmap |= (1 << i);
		nxgep->tdc[i - p_cfgp->start_tdc] = (uint8_t)i;
	}

	p_dma_cfgp->tx_dma_map = bitmap;
	param_arr = nxgep->param_arr;

	/* Assume RDCs are evenly distributed */
	rx_quick_cfg = param_arr[param_rx_quick_cfg].value;
	switch (rx_quick_cfg) {
	case CFG_NOT_SPECIFIED:
		prop = "rxdma-grp-cfg";
		status = ddi_prop_lookup_string(DDI_DEV_T_NONE,
			nxgep->dip, 0, prop, (char **)&prop_val);
		if (status != DDI_PROP_SUCCESS) {
			NXGE_DEBUG_MSG((nxgep, CFG_CTL,
				" property %s not found", prop));
			rdcgrp_cfg = CFG_L3_DISTRIBUTE;
		} else {
			token = nxge_get_config_token(prop_val);
			switch (token) {
			case L2_CLASSIFY:
				break;
			case CLASSIFY:
			case L3_CLASSIFY:
			case L3_DISTRIBUTE:
			case L3_TCAM:
				rdcgrp_cfg = CFG_L3_DISTRIBUTE;
				break;
			default:
				rdcgrp_cfg = CFG_L3_DISTRIBUTE;
				break;
			}
			ddi_prop_free(prop_val);
		}
		break;
	case CFG_L3_WEB:
	case CFG_L3_DISTRIBUTE:
	case CFG_L2_CLASSIFY:
	case CFG_L3_TCAM:
		rdcgrp_cfg = rx_quick_cfg;
		break;
	default:
		rdcgrp_cfg = CFG_L3_DISTRIBUTE;
		break;
	}

	/* Receive DMA Channels */
	st_rdc = p_cfgp->start_rdc;
	nxgep->nrdc = p_cfgp->max_rdcs;

	for (i = 0; i < p_cfgp->max_rdcs; i++) {
		nxgep->rdc[i] = i + p_cfgp->start_rdc;
	}

	switch (rdcgrp_cfg) {
	case CFG_L3_DISTRIBUTE:
	case CFG_L3_WEB:
	case CFG_L3_TCAM:
		ndmas = p_cfgp->max_rdcs;
		ngrps = 1;
		rdcs_per_grp = ndmas / ngrps;
		break;
	case CFG_L2_CLASSIFY:
		ndmas = p_cfgp->max_rdcs / 2;
		if (p_cfgp->max_rdcs < 2)
			ndmas = 1;
		ngrps = 1;
		rdcs_per_grp = ndmas / ngrps;
		break;
	default:
		ngrps = p_cfgp->max_rdc_grpids;
		ndmas = p_cfgp->max_rdcs;
		rdcs_per_grp = ndmas / ngrps;
		break;
	}

	for (i = 0; i < ngrps; i++) {
		rdc_grp_p = &p_dma_cfgp->rdc_grps[i];
		rdc_grp_p->start_rdc = st_rdc + i * rdcs_per_grp;
		rdc_grp_p->max_rdcs = rdcs_per_grp;

		/* default to: 0, 1, 2, 3, ...., 0, 1, 2, 3.... */
		rdc_grp_p->config_method = RDC_TABLE_ENTRY_METHOD_SEQ;
		rdc = rdc_grp_p->start_rdc;
		for (j = 0; j < NXGE_MAX_RDCS; j++) {
			rdc_grp_p->rdc[j] = rdc++;
			if (rdc == (rdc_grp_p->start_rdc + rdcs_per_grp)) {
				rdc = rdc_grp_p->start_rdc;
			}
		}
		rdc_grp_p->def_rdc = rdc_grp_p->rdc[0];
		rdc_grp_p->flag = 1;	/* configured */
	}

	/* default RDC */
	p_cfgp->def_rdc = p_cfgp->start_rdc;
	nxgep->def_rdc = p_cfgp->start_rdc;

	/* full 18 byte header ? */
	p_dma_cfgp->rcr_full_header = NXGE_RCR_FULL_HEADER;
	p_dma_cfgp->rx_drr_weight = PT_DRR_WT_DEFAULT_10G;
	if (nxgep->function_num > 1)
		p_dma_cfgp->rx_drr_weight = PT_DRR_WT_DEFAULT_1G;
	p_dma_cfgp->rbr_size = nxge_rbr_size;
	p_dma_cfgp->rcr_size = nxge_rcr_size;

	nxge_set_rdc_intr_property(nxgep);
	NXGE_DEBUG_MSG((nxgep, CFG_CTL, " <== nxge_set_hw_dma_config"));
}

boolean_t
nxge_check_rxdma_port_member(p_nxge_t nxgep, uint8_t rdc)
{
	p_nxge_dma_pt_cfg_t p_dma_cfgp;
	p_nxge_hw_pt_cfg_t p_cfgp;
	int status = B_TRUE;

	NXGE_DEBUG_MSG((nxgep, CFG2_CTL, "==> nxge_check_rxdma_port_member"));

	p_dma_cfgp = (p_nxge_dma_pt_cfg_t)&nxgep->pt_config;
	p_cfgp = (p_nxge_hw_pt_cfg_t)&p_dma_cfgp->hw_config;

	/* Receive DMA Channels */
	if (rdc < p_cfgp->max_rdcs)
		status = B_TRUE;
	NXGE_DEBUG_MSG((nxgep, CFG2_CTL, " <== nxge_check_rxdma_port_member"));
	return (status);
}

boolean_t
nxge_check_txdma_port_member(p_nxge_t nxgep, uint8_t tdc)
{
	p_nxge_dma_pt_cfg_t p_dma_cfgp;
	p_nxge_hw_pt_cfg_t p_cfgp;
	int status = B_FALSE;

	NXGE_DEBUG_MSG((nxgep, CFG2_CTL, "==> nxge_check_rxdma_port_member"));

	p_dma_cfgp = (p_nxge_dma_pt_cfg_t)&nxgep->pt_config;
	p_cfgp = (p_nxge_hw_pt_cfg_t)&p_dma_cfgp->hw_config;

	/* Receive DMA Channels */
	if (tdc < p_cfgp->max_tdcs)
		status = B_TRUE;
	NXGE_DEBUG_MSG((nxgep, CFG2_CTL, " <== nxge_check_rxdma_port_member"));
	return (status);
}

boolean_t
nxge_check_rxdma_rdcgrp_member(p_nxge_t nxgep, uint8_t rdc_grp, uint8_t rdc)
{
	p_nxge_dma_pt_cfg_t p_dma_cfgp;
	int status = B_TRUE;
	p_nxge_rdc_grp_t rdc_grp_p;

	NXGE_DEBUG_MSG((nxgep, CFG2_CTL,
		" ==> nxge_check_rxdma_rdcgrp_member"));
	NXGE_DEBUG_MSG((nxgep, CFG2_CTL, "  nxge_check_rxdma_rdcgrp_member"
		" rdc  %d group %d", rdc, rdc_grp));
	p_dma_cfgp = (p_nxge_dma_pt_cfg_t)&nxgep->pt_config;

	rdc_grp_p = &p_dma_cfgp->rdc_grps[rdc_grp];
	NXGE_DEBUG_MSG((nxgep, CFG2_CTL, "  max  %d ", rdc_grp_p->max_rdcs));
	if (rdc >= rdc_grp_p->max_rdcs) {
		status = B_FALSE;
	}
	NXGE_DEBUG_MSG((nxgep, CFG2_CTL,
		" <== nxge_check_rxdma_rdcgrp_member"));
	return (status);
}

boolean_t
nxge_check_rdcgrp_port_member(p_nxge_t nxgep, uint8_t rdc_grp)
{
	p_nxge_dma_pt_cfg_t p_dma_cfgp;
	p_nxge_hw_pt_cfg_t p_cfgp;
	int status = B_TRUE;

	NXGE_DEBUG_MSG((nxgep, CFG2_CTL, "==> nxge_check_rdcgrp_port_member"));

	p_dma_cfgp = (p_nxge_dma_pt_cfg_t)&nxgep->pt_config;
	p_cfgp = (p_nxge_hw_pt_cfg_t)&p_dma_cfgp->hw_config;

	if (rdc_grp >= p_cfgp->max_rdc_grpids)
		status = B_FALSE;
	NXGE_DEBUG_MSG((nxgep, CFG2_CTL, " <== nxge_check_rdcgrp_port_member"));
	return (status);
}

static void
nxge_set_hw_vlan_class_config(p_nxge_t nxgep)
{
	int i;
	p_nxge_dma_pt_cfg_t p_dma_cfgp;
	p_nxge_hw_pt_cfg_t p_cfgp;
	p_nxge_param_t param_arr;
	uint_t vlan_cnt;
	int *vlan_cfg_val;
	nxge_param_map_t *vmap;
	char *prop;
	p_nxge_class_pt_cfg_t p_class_cfgp;
	uint32_t good_cfg[32];
	int good_count = 0;
	nxge_mv_cfg_t *vlan_tbl;

	NXGE_DEBUG_MSG((nxgep, CFG_CTL, " ==> nxge_set_hw_vlan_config"));
	p_dma_cfgp = (p_nxge_dma_pt_cfg_t)&nxgep->pt_config;
	p_cfgp = (p_nxge_hw_pt_cfg_t)&p_dma_cfgp->hw_config;
	p_class_cfgp = (p_nxge_class_pt_cfg_t)&nxgep->class_config;

	param_arr = nxgep->param_arr;
	prop = param_arr[param_vlan_2rdc_grp].fcode_name;

	/*
	 * By default, VLAN to RDC group mapping is disabled Need to read HW or
	 * .conf properties to find out if mapping is required
	 *
	 * Format
	 *
	 * uint32_t array, each array entry specifying the VLAN id and the
	 * mapping
	 *
	 * bit[30] = add bit[29] = remove bit[28]  = preference bits[23-16] =
	 * rdcgrp bits[15-0] = VLAN ID ( )
	 */

	for (i = 0; i < NXGE_MAX_VLANS; i++) {
		p_class_cfgp->vlan_tbl[i].flag = 0;
	}

	vlan_tbl = (nxge_mv_cfg_t *)&p_class_cfgp->vlan_tbl[0];
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, nxgep->dip, 0, prop,
			&vlan_cfg_val, &vlan_cnt) == DDI_PROP_SUCCESS) {
		for (i = 0; i < vlan_cnt; i++) {
			vmap = (nxge_param_map_t *)&vlan_cfg_val[i];
			if ((vmap->param_id) &&
					(vmap->param_id < NXGE_MAX_VLANS) &&
					(vmap->map_to <
						p_cfgp->max_rdc_grpids) &&
					(vmap->map_to >= (uint8_t)0)) {
				NXGE_DEBUG_MSG((nxgep, CFG2_CTL,
					" nxge_vlan_config mapping"
					" id %d grp %d",
					vmap->param_id, vmap->map_to));
				good_cfg[good_count] = vlan_cfg_val[i];
				if (vlan_tbl[vmap->param_id].flag == 0)
					good_count++;
				vlan_tbl[vmap->param_id].flag = 1;
				vlan_tbl[vmap->param_id].rdctbl =
					vmap->map_to + p_cfgp->start_rdc_grpid;
				vlan_tbl[vmap->param_id].mpr_npr = vmap->pref;
			}
		}
		ddi_prop_free(vlan_cfg_val);
		if (good_count != vlan_cnt) {
			(void) ddi_prop_update_int_array(DDI_DEV_T_NONE,
				nxgep->dip, prop, (int *)good_cfg, good_count);
		}
	}
	NXGE_DEBUG_MSG((nxgep, CFG_CTL, "<== nxge_set_hw_vlan_config"));
}

static void
nxge_set_hw_mac_class_config(p_nxge_t nxgep)
{
	int i;
	p_nxge_dma_pt_cfg_t p_dma_cfgp;
	p_nxge_hw_pt_cfg_t p_cfgp;
	p_nxge_param_t param_arr;
	uint_t mac_cnt;
	int *mac_cfg_val;
	nxge_param_map_t *mac_map;
	char *prop;
	p_nxge_class_pt_cfg_t p_class_cfgp;
	int good_count = 0;
	int good_cfg[NXGE_MAX_MACS];
	nxge_mv_cfg_t *mac_host_info;

	NXGE_DEBUG_MSG((nxgep, CFG_CTL, "==> nxge_set_hw_mac_config"));

	p_dma_cfgp = (p_nxge_dma_pt_cfg_t)&nxgep->pt_config;
	p_cfgp = (p_nxge_hw_pt_cfg_t)&p_dma_cfgp->hw_config;
	p_class_cfgp = (p_nxge_class_pt_cfg_t)&nxgep->class_config;
	mac_host_info = (nxge_mv_cfg_t *)&p_class_cfgp->mac_host_info[0];

	param_arr = nxgep->param_arr;
	prop = param_arr[param_mac_2rdc_grp].fcode_name;

	for (i = 0; i < NXGE_MAX_MACS; i++) {
		p_class_cfgp->mac_host_info[i].flag = 0;
	}

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, nxgep->dip, 0, prop,
			&mac_cfg_val, &mac_cnt) == DDI_PROP_SUCCESS) {
		for (i = 0; i < mac_cnt; i++) {
			mac_map = (nxge_param_map_t *)&mac_cfg_val[i];
			if ((mac_map->param_id < p_cfgp->max_macs) &&
					(mac_map->map_to <
						p_cfgp->max_rdc_grpids) &&
					(mac_map->map_to >= (uint8_t)0)) {
				NXGE_DEBUG_MSG((nxgep, CFG2_CTL,
					" nxge_mac_config mapping"
					" id %d grp %d",
					mac_map->param_id, mac_map->map_to));
				mac_host_info[mac_map->param_id].mpr_npr =
					mac_map->pref;
				mac_host_info[mac_map->param_id].rdctbl =
					mac_map->map_to +
					p_cfgp->start_rdc_grpid;
				good_cfg[good_count] = mac_cfg_val[i];
				if (mac_host_info[mac_map->param_id].flag == 0)
					good_count++;
				mac_host_info[mac_map->param_id].flag = 1;
			}
		}
		ddi_prop_free(mac_cfg_val);
		if (good_count != mac_cnt) {
			(void) ddi_prop_update_int_array(DDI_DEV_T_NONE,
				nxgep->dip, prop, good_cfg, good_count);
		}
	}
	NXGE_DEBUG_MSG((nxgep, CFG_CTL, "<== nxge_set_hw_mac_config"));
}

static void
nxge_set_hw_class_config(p_nxge_t nxgep)
{
	int i;
	p_nxge_param_t param_arr;
	int *int_prop_val;
	uint32_t cfg_value;
	char *prop;
	p_nxge_class_pt_cfg_t p_class_cfgp;
	int start_prop, end_prop;
	uint_t prop_cnt;

	NXGE_DEBUG_MSG((nxgep, CFG_CTL, " ==> nxge_set_hw_class_config"));

	p_class_cfgp = (p_nxge_class_pt_cfg_t)&nxgep->class_config;
	param_arr = nxgep->param_arr;
	start_prop = param_class_opt_ip_usr4;
	end_prop = param_class_opt_ipv6_sctp;

	for (i = start_prop; i <= end_prop; i++) {
		prop = param_arr[i].fcode_name;
		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, nxgep->dip,
				0, prop, &int_prop_val,
				&prop_cnt) == DDI_PROP_SUCCESS) {
			cfg_value = (uint32_t)*int_prop_val;
			ddi_prop_free(int_prop_val);
		} else {
			cfg_value = (uint32_t)param_arr[i].value;
		}
		p_class_cfgp->class_cfg[i - start_prop] = cfg_value;
	}

	prop = param_arr[param_h1_init_value].fcode_name;

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, nxgep->dip, 0, prop,
			&int_prop_val, &prop_cnt) == DDI_PROP_SUCCESS) {
		cfg_value = (uint32_t)*int_prop_val;
		ddi_prop_free(int_prop_val);
	} else {
		cfg_value = (uint32_t)param_arr[param_h1_init_value].value;
	}

	p_class_cfgp->init_h1 = (uint32_t)cfg_value;
	prop = param_arr[param_h2_init_value].fcode_name;

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, nxgep->dip, 0, prop,
			&int_prop_val, &prop_cnt) == DDI_PROP_SUCCESS) {
		cfg_value = (uint32_t)*int_prop_val;
		ddi_prop_free(int_prop_val);
	} else {
		cfg_value = (uint32_t)param_arr[param_h2_init_value].value;
	}

	p_class_cfgp->init_h2 = (uint16_t)cfg_value;
	NXGE_DEBUG_MSG((nxgep, CFG_CTL, " <== nxge_set_hw_class_config"));
}

nxge_status_t
nxge_ldgv_init_n2(p_nxge_t nxgep, int *navail_p, int *nrequired_p)
{
	int i, maxldvs, maxldgs, start, end, nldvs;
	int ldv, endldg;
	uint8_t func;
	uint8_t channel;
	uint8_t chn_start;
	boolean_t own_sys_err = B_FALSE, own_fzc = B_FALSE;
	p_nxge_dma_pt_cfg_t p_dma_cfgp;
	p_nxge_hw_pt_cfg_t p_cfgp;
	p_nxge_ldgv_t ldgvp;
	p_nxge_ldg_t ldgp, ptr;
	p_nxge_ldv_t ldvp;
	nxge_status_t status = NXGE_OK;

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_ldgv_init_n2"));
	if (!*navail_p) {
		*nrequired_p = 0;
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			"<== nxge_ldgv_init:no avail"));
		return (NXGE_ERROR);
	}
	/*
	 * N2/NIU: one logical device owns one logical group. and each
	 * device/group will be assigned one vector by Hypervisor.
	 */
	p_dma_cfgp = (p_nxge_dma_pt_cfg_t)&nxgep->pt_config;
	p_cfgp = (p_nxge_hw_pt_cfg_t)&p_dma_cfgp->hw_config;
	maxldgs = p_cfgp->max_ldgs;
	if (!maxldgs) {
		/* No devices configured. */
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "<== nxge_ldgv_init_n2: "
			"no logical groups configured."));
		return (NXGE_ERROR);
	} else {
		maxldvs = maxldgs + 1;
	}

	/*
	 * If function zero instance, it needs to handle the system and MIF
	 * error interrupts. MIF interrupt may not be needed for N2/NIU.
	 */
	func = nxgep->function_num;
	if (func == 0) {
		own_sys_err = B_TRUE;
		if (!p_cfgp->ser_ldvid) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
				"nxge_ldgv_init_n2: func 0, ERR ID not set!"));
		}
		/* MIF interrupt */
		if (!p_cfgp->mif_ldvid) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
				"nxge_ldgv_init_n2: func 0, MIF ID not set!"));
		}
	}

	/*
	 * Assume single partition, each function owns mac.
	 */
	if (!nxge_use_partition)
		own_fzc = B_TRUE;

	ldgvp = nxgep->ldgvp;
	if (ldgvp == NULL) {
		ldgvp = KMEM_ZALLOC(sizeof (nxge_ldgv_t), KM_SLEEP);
		nxgep->ldgvp = ldgvp;
		ldgvp->maxldgs = (uint8_t)maxldgs;
		ldgvp->maxldvs = (uint8_t)maxldvs;
		ldgp = ldgvp->ldgp = KMEM_ZALLOC(sizeof (nxge_ldg_t) * maxldgs,
			KM_SLEEP);
		ldvp = ldgvp->ldvp = KMEM_ZALLOC(sizeof (nxge_ldv_t) * maxldvs,
			KM_SLEEP);
	} else {
		ldgp = ldgvp->ldgp;
		ldvp = ldgvp->ldvp;
	}

	ldgvp->ndma_ldvs = p_cfgp->max_tdcs + p_cfgp->max_rdcs;
	ldgvp->tmres = NXGE_TIMER_RESO;

	NXGE_DEBUG_MSG((nxgep, INT_CTL,
		"==> nxge_ldgv_init_n2: maxldvs %d maxldgs %d",
		maxldvs, maxldgs));

	/* logical start_ldg is ldv */
	ptr = ldgp;
	for (i = 0; i < maxldgs; i++) {
		ptr->func = func;
		ptr->arm = B_TRUE;
		ptr->vldg_index = (uint8_t)i;
		ptr->ldg_timer = NXGE_TIMER_LDG;
		ptr->ldg = p_cfgp->ldg[i];
		ptr->sys_intr_handler = nxge_intr;
		ptr->nldvs = 0;
		ptr->ldvp = NULL;
		ptr->nxgep = nxgep;
		NXGE_DEBUG_MSG((nxgep, INT_CTL,
			"==> nxge_ldgv_init_n2: maxldvs %d maxldgs %d "
			"ldg %d ldgptr $%p",
			maxldvs, maxldgs, ptr->ldg, ptr));
		ptr++;
	}

	endldg = NXGE_INT_MAX_LDG;
	nldvs = 0;
	ldgvp->nldvs = 0;
	ldgp->ldvp = NULL;
	*nrequired_p = 0;

	/*
	 * logical device group table is organized in the following order (same
	 * as what interrupt property has). function 0: owns MAC, MIF, error,
	 * rx, tx. function 1: owns MAC, rx, tx.
	 */

	if (own_fzc && p_cfgp->mac_ldvid) {
		/* Each function should own MAC interrupt */
		ldv = p_cfgp->mac_ldvid;
		ldvp->ldv = (uint8_t)ldv;
		ldvp->is_mac = B_TRUE;
		ldvp->ldv_intr_handler = nxge_mac_intr;
		ldvp->ldv_ldf_masks = 0;
		ldvp->nxgep = nxgep;
		NXGE_DEBUG_MSG((nxgep, INT_CTL,
			"==> nxge_ldgv_init_n2(mac): maxldvs %d ldv %d "
			"ldg %d ldgptr $%p ldvptr $%p",
			maxldvs, ldv, ldgp->ldg, ldgp, ldvp));
		nxge_ldgv_setup(&ldgp, &ldvp, ldv, endldg, nrequired_p);
		nldvs++;
	}

	if (own_fzc && p_cfgp->mif_ldvid) {
		ldv = p_cfgp->mif_ldvid;
		ldvp->ldv = (uint8_t)ldv;
		ldvp->is_mif = B_TRUE;
		ldvp->ldv_intr_handler = nxge_mif_intr;
		ldvp->ldv_ldf_masks = 0;
		ldvp->nxgep = nxgep;
		NXGE_DEBUG_MSG((nxgep, INT_CTL,
			"==> nxge_ldgv_init_n2(mif): maxldvs %d ldv %d "
			"ldg %d ldgptr $%p ldvptr $%p",
			maxldvs, ldv, ldgp->ldg, ldgp, ldvp));
		nxge_ldgv_setup(&ldgp, &ldvp, ldv, endldg, nrequired_p);
		nldvs++;
	}

	ldv = NXGE_SYS_ERROR_LD;
	ldvp->use_timer = B_TRUE;
	if (own_sys_err && p_cfgp->ser_ldvid) {
		ldv = p_cfgp->ser_ldvid;
		/*
		 * Unmask the system interrupt states.
		 */
		(void) nxge_fzc_sys_err_mask_set(nxgep, SYS_ERR_SMX_MASK |
			SYS_ERR_IPP_MASK | SYS_ERR_TXC_MASK |
			SYS_ERR_ZCP_MASK);
	}
	ldvp->ldv = (uint8_t)ldv;
	ldvp->is_syserr = B_TRUE;
	ldvp->ldv_intr_handler = nxge_syserr_intr;
	ldvp->ldv_ldf_masks = 0;
	ldvp->nxgep = nxgep;
	ldgvp->ldvp_syserr = ldvp;

	NXGE_DEBUG_MSG((nxgep, INT_CTL,
		"==> nxge_ldgv_init_n2(syserr): maxldvs %d ldv %d "
		"ldg %d ldgptr $%p ldvptr p%p",
		maxldvs, ldv, ldgp->ldg, ldgp, ldvp));

	if (own_sys_err && p_cfgp->ser_ldvid) {
		(void) nxge_ldgv_setup(&ldgp, &ldvp, ldv, endldg, nrequired_p);
	} else {
		ldvp++;
	}

	nldvs++;

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_ldgv_init_n2: "
		"(before rx) func %d nldvs %d navail %d nrequired %d",
		func, nldvs, *navail_p, *nrequired_p));

	/*
	 * Receive DMA channels.
	 */
	channel = p_cfgp->start_rdc;
	start = p_cfgp->start_rdc + NXGE_RDMA_LD_START;
	end = start + p_cfgp->max_rdcs;
	chn_start = p_cfgp->ldg_chn_start;
	/*
	 * Start with RDC to configure logical devices for each group.
	 */
	for (i = 0, ldv = start; ldv < end; i++, ldv++, chn_start++) {
		ldvp->is_rxdma = B_TRUE;
		ldvp->ldv = (uint8_t)ldv;
		ldvp->channel = channel++;
		ldvp->vdma_index = (uint8_t)i;
		ldvp->ldv_intr_handler = nxge_rx_intr;
		ldvp->ldv_ldf_masks = 0;
		ldvp->nxgep = nxgep;
		ldgp->ldg = p_cfgp->ldg[chn_start];

		NXGE_DEBUG_MSG((nxgep, INT_CTL,
			"==> nxge_ldgv_init_n2(rx%d): maxldvs %d ldv %d "
			"ldg %d ldgptr 0x%016llx ldvptr 0x%016llx",
			i, maxldvs, ldv, ldgp->ldg, ldgp, ldvp));
		nxge_ldgv_setup(&ldgp, &ldvp, ldv, endldg, nrequired_p);
		nldvs++;
	}

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_ldgv_init_n2: "
		"func %d nldvs %d navail %d nrequired %d",
		func, nldvs, *navail_p, *nrequired_p));

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_ldgv_init_n2: "
		"func %d nldvs %d navail %d nrequired %d ldgp 0x%llx "
		"ldvp 0x%llx",
		func, nldvs, *navail_p, *nrequired_p, ldgp, ldvp));
	/*
	 * Transmit DMA channels.
	 */
	channel = p_cfgp->start_tdc;
	start = p_cfgp->start_tdc + NXGE_TDMA_LD_START;
	end = start + p_cfgp->max_tdcs;
	for (i = 0, ldv = start; ldv < end; i++, ldv++, chn_start++) {
		ldvp->is_txdma = B_TRUE;
		ldvp->ldv = (uint8_t)ldv;
		ldvp->channel = channel++;
		ldvp->vdma_index = (uint8_t)i;
		ldvp->ldv_intr_handler = nxge_tx_intr;
		ldvp->ldv_ldf_masks = 0;
		ldgp->ldg = p_cfgp->ldg[chn_start];
		ldvp->nxgep = nxgep;
		NXGE_DEBUG_MSG((nxgep, INT_CTL,
			"==> nxge_ldgv_init_n2(tx%d): maxldvs %d ldv %d "
			"ldg %d ldgptr 0x%016llx ldvptr 0x%016llx",
			i, maxldvs, ldv, ldgp->ldg, ldgp, ldvp));
		nxge_ldgv_setup(&ldgp, &ldvp, ldv, endldg, nrequired_p);
		nldvs++;
	}

	ldgvp->ldg_intrs = *nrequired_p;
	ldgvp->nldvs = (uint8_t)nldvs;

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_ldgv_init_n2: "
		"func %d nldvs %d maxgrps %d navail %d nrequired %d",
		func, nldvs, maxldgs, *navail_p, *nrequired_p));

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "<== nxge_ldgv_init_n2"));
	return (status);
}

/*
 * Interrupts related interface functions.
 */

nxge_status_t
nxge_ldgv_init(p_nxge_t nxgep, int *navail_p, int *nrequired_p)
{
	int i, maxldvs, maxldgs, start, end, nldvs;
	int ldv, ldg, endldg, ngrps;
	uint8_t func;
	uint8_t channel;
	boolean_t own_sys_err = B_FALSE, own_fzc = B_FALSE;
	p_nxge_dma_pt_cfg_t p_dma_cfgp;
	p_nxge_hw_pt_cfg_t p_cfgp;
	p_nxge_ldgv_t ldgvp;
	p_nxge_ldg_t ldgp, ptr;
	p_nxge_ldv_t ldvp;
	nxge_status_t status = NXGE_OK;

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_ldgv_init"));
	if (!*navail_p) {
		*nrequired_p = 0;
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			"<== nxge_ldgv_init:no avail"));
		return (NXGE_ERROR);
	}
	p_dma_cfgp = (p_nxge_dma_pt_cfg_t)&nxgep->pt_config;
	p_cfgp = (p_nxge_hw_pt_cfg_t)&p_dma_cfgp->hw_config;

	nldvs = p_cfgp->max_tdcs + p_cfgp->max_rdcs;

	/*
	 * If function zero instance, it needs to handle the system error
	 * interrupts.
	 */
	func = nxgep->function_num;
	if (func == 0) {
		nldvs++;
		own_sys_err = B_TRUE;
	} else {
		/* use timer */
		nldvs++;
	}

	/*
	 * Assume single partition, each function owns mac.
	 */
	if (!nxge_use_partition) {
		/* mac */
		nldvs++;
		/* MIF */
		nldvs++;
		own_fzc = B_TRUE;
	}
	maxldvs = nldvs;
	maxldgs = p_cfgp->max_ldgs;
	if (!maxldvs || !maxldgs) {
		/* No devices configured. */
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "<== nxge_ldgv_init: "
			"no logical devices or groups configured."));
		return (NXGE_ERROR);
	}
	ldgvp = nxgep->ldgvp;
	if (ldgvp == NULL) {
		ldgvp = KMEM_ZALLOC(sizeof (nxge_ldgv_t), KM_SLEEP);
		nxgep->ldgvp = ldgvp;
		ldgvp->maxldgs = (uint8_t)maxldgs;
		ldgvp->maxldvs = (uint8_t)maxldvs;
		ldgp = ldgvp->ldgp = KMEM_ZALLOC(sizeof (nxge_ldg_t) * maxldgs,
			KM_SLEEP);
		ldvp = ldgvp->ldvp = KMEM_ZALLOC(sizeof (nxge_ldv_t) * maxldvs,
			KM_SLEEP);
	}
	ldgvp->ndma_ldvs = p_cfgp->max_tdcs + p_cfgp->max_rdcs;
	ldgvp->tmres = NXGE_TIMER_RESO;

	NXGE_DEBUG_MSG((nxgep, INT_CTL,
		"==> nxge_ldgv_init: maxldvs %d maxldgs %d nldvs %d",
		maxldvs, maxldgs, nldvs));
	ldg = p_cfgp->start_ldg;
	ptr = ldgp;
	for (i = 0; i < maxldgs; i++) {
		ptr->func = func;
		ptr->arm = B_TRUE;
		ptr->vldg_index = (uint8_t)i;
		ptr->ldg_timer = NXGE_TIMER_LDG;
		ptr->ldg = ldg++;
		ptr->sys_intr_handler = nxge_intr;
		ptr->nldvs = 0;
		ptr->nxgep = nxgep;
		NXGE_DEBUG_MSG((nxgep, INT_CTL,
			"==> nxge_ldgv_init: maxldvs %d maxldgs %d ldg %d",
			maxldvs, maxldgs, ptr->ldg));
		ptr++;
	}

	ldg = p_cfgp->start_ldg;
	if (maxldgs > *navail_p) {
		ngrps = *navail_p;
	} else {
		ngrps = maxldgs;
	}
	endldg = ldg + ngrps;

	/*
	 * Receive DMA channels.
	 */
	channel = p_cfgp->start_rdc;
	start = p_cfgp->start_rdc + NXGE_RDMA_LD_START;
	end = start + p_cfgp->max_rdcs;
	nldvs = 0;
	ldgvp->nldvs = 0;
	ldgp->ldvp = NULL;
	*nrequired_p = 0;

	/*
	 * Start with RDC to configure logical devices for each group.
	 */
	for (i = 0, ldv = start; ldv < end; i++, ldv++) {
		ldvp->is_rxdma = B_TRUE;
		ldvp->ldv = (uint8_t)ldv;
		/* If non-seq needs to change the following code */
		ldvp->channel = channel++;
		ldvp->vdma_index = (uint8_t)i;
		ldvp->ldv_intr_handler = nxge_rx_intr;
		ldvp->ldv_ldf_masks = 0;
		ldvp->use_timer = B_FALSE;
		ldvp->nxgep = nxgep;
		nxge_ldgv_setup(&ldgp, &ldvp, ldv, endldg, nrequired_p);
		nldvs++;
	}

	/*
	 * Transmit DMA channels.
	 */
	channel = p_cfgp->start_tdc;
	start = p_cfgp->start_tdc + NXGE_TDMA_LD_START;
	end = start + p_cfgp->max_tdcs;
	for (i = 0, ldv = start; ldv < end; i++, ldv++) {
		ldvp->is_txdma = B_TRUE;
		ldvp->ldv = (uint8_t)ldv;
		ldvp->channel = channel++;
		ldvp->vdma_index = (uint8_t)i;
		ldvp->ldv_intr_handler = nxge_tx_intr;
		ldvp->ldv_ldf_masks = 0;
		ldvp->use_timer = B_FALSE;
		ldvp->nxgep = nxgep;
		nxge_ldgv_setup(&ldgp, &ldvp, ldv, endldg, nrequired_p);
		nldvs++;
	}

	if (own_fzc) {
		ldv = NXGE_MIF_LD;
		ldvp->ldv = (uint8_t)ldv;
		ldvp->is_mif = B_TRUE;
		ldvp->ldv_intr_handler = nxge_mif_intr;
		ldvp->ldv_ldf_masks = 0;
		ldvp->use_timer = B_FALSE;
		ldvp->nxgep = nxgep;
		nxge_ldgv_setup(&ldgp, &ldvp, ldv, endldg, nrequired_p);
		nldvs++;
	}
	/*
	 * MAC port (function zero control)
	 */
	if (own_fzc) {
		ldvp->is_mac = B_TRUE;
		ldvp->ldv_intr_handler = nxge_mac_intr;
		ldvp->ldv_ldf_masks = 0;
		ldv = func + NXGE_MAC_LD_START;
		ldvp->ldv = (uint8_t)ldv;
		ldvp->use_timer = B_FALSE;
		ldvp->nxgep = nxgep;
		nxge_ldgv_setup(&ldgp, &ldvp, ldv, endldg, nrequired_p);
		nldvs++;
	}
	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_ldgv_init: "
		"func %d nldvs %d navail %d nrequired %d",
		func, nldvs, *navail_p, *nrequired_p));
	/*
	 * Function 0 owns system error interrupts.
	 */
	ldvp->use_timer = B_TRUE;
	if (own_sys_err) {
		ldv = NXGE_SYS_ERROR_LD;
		ldvp->ldv = (uint8_t)ldv;
		ldvp->is_syserr = B_TRUE;
		ldvp->ldv_intr_handler = nxge_syserr_intr;
		ldvp->ldv_ldf_masks = 0;
		ldvp->nxgep = nxgep;
		ldgvp->ldvp_syserr = ldvp;
		/*
		 * Unmask the system interrupt states.
		 */
		(void) nxge_fzc_sys_err_mask_set(nxgep, SYS_ERR_SMX_MASK |
			SYS_ERR_IPP_MASK | SYS_ERR_TXC_MASK |
			SYS_ERR_ZCP_MASK);

		(void) nxge_ldgv_setup(&ldgp, &ldvp, ldv, endldg, nrequired_p);
		nldvs++;
	} else {
		ldv = NXGE_SYS_ERROR_LD;
		ldvp->ldv = (uint8_t)ldv;
		ldvp->is_syserr = B_TRUE;
		ldvp->ldv_intr_handler = nxge_syserr_intr;
		ldvp->nxgep = nxgep;
		ldvp->ldv_ldf_masks = 0;
		ldgvp->ldvp_syserr = ldvp;
	}

	ldgvp->ldg_intrs = *nrequired_p;

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_ldgv_init: "
		"func %d nldvs %d navail %d nrequired %d",
		func, nldvs, *navail_p, *nrequired_p));

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "<== nxge_ldgv_init"));
	return (status);
}

nxge_status_t
nxge_ldgv_uninit(p_nxge_t nxgep)
{
	p_nxge_ldgv_t ldgvp;

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_ldgv_uninit"));
	ldgvp = nxgep->ldgvp;
	if (ldgvp == NULL) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "<== nxge_ldgv_uninit: "
				"no logical group configured."));
		return (NXGE_OK);
	}
	if (ldgvp->ldgp) {
		KMEM_FREE(ldgvp->ldgp, sizeof (nxge_ldg_t) * ldgvp->maxldgs);
	}
	if (ldgvp->ldvp) {
		KMEM_FREE(ldgvp->ldvp, sizeof (nxge_ldv_t) * ldgvp->maxldvs);
	}
	KMEM_FREE(ldgvp, sizeof (nxge_ldgv_t));
	nxgep->ldgvp = NULL;

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "<== nxge_ldgv_uninit"));
	return (NXGE_OK);
}

nxge_status_t
nxge_intr_ldgv_init(p_nxge_t nxgep)
{
	nxge_status_t status = NXGE_OK;

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_intr_ldgv_init"));
	/*
	 * Configure the logical device group numbers, state vectors and
	 * interrupt masks for each logical device.
	 */
	status = nxge_fzc_intr_init(nxgep);

	/*
	 * Configure logical device masks and timers.
	 */
	status = nxge_intr_mask_mgmt(nxgep);

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "<== nxge_intr_ldgv_init"));
	return (status);
}

nxge_status_t
nxge_intr_mask_mgmt(p_nxge_t nxgep)
{
	p_nxge_ldgv_t ldgvp;
	p_nxge_ldg_t ldgp;
	p_nxge_ldv_t ldvp;
	npi_handle_t handle;
	int i, j;
	npi_status_t rs = NPI_SUCCESS;

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_intr_mask_mgmt"));

	if ((ldgvp = nxgep->ldgvp) == NULL) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			"<== nxge_intr_mask_mgmt: Null ldgvp"));
		return (NXGE_ERROR);
	}
	handle = NXGE_DEV_NPI_HANDLE(nxgep);
	ldgp = ldgvp->ldgp;
	ldvp = ldgvp->ldvp;
	if (ldgp == NULL || ldvp == NULL) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			"<== nxge_intr_mask_mgmt: Null ldgp or ldvp"));
		return (NXGE_ERROR);
	}
	NXGE_DEBUG_MSG((nxgep, INT_CTL,
		"==> nxge_intr_mask_mgmt: # of intrs %d ", ldgvp->ldg_intrs));
	/* Initialize masks. */
	if (nxgep->niu_type != N2_NIU) {
		NXGE_DEBUG_MSG((nxgep, INT_CTL,
			"==> nxge_intr_mask_mgmt(Neptune): # intrs %d ",
			ldgvp->ldg_intrs));
		for (i = 0; i < ldgvp->ldg_intrs; i++, ldgp++) {
			NXGE_DEBUG_MSG((nxgep, INT_CTL,
				"==> nxge_intr_mask_mgmt(Neptune): # ldv %d "
				"in group %d", ldgp->nldvs, ldgp->ldg));
			for (j = 0; j < ldgp->nldvs; j++, ldvp++) {
				NXGE_DEBUG_MSG((nxgep, INT_CTL,
					"==> nxge_intr_mask_mgmt: set ldv # %d "
					"for ldg %d", ldvp->ldv, ldgp->ldg));
				rs = npi_intr_mask_set(handle, ldvp->ldv,
					ldvp->ldv_ldf_masks);
				if (rs != NPI_SUCCESS) {
					NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
						"<== nxge_intr_mask_mgmt: "
						"set mask failed "
						" rs 0x%x ldv %d mask 0x%x",
						rs, ldvp->ldv,
						ldvp->ldv_ldf_masks));
					return (NXGE_ERROR | rs);
				}
				NXGE_DEBUG_MSG((nxgep, INT_CTL,
					"==> nxge_intr_mask_mgmt: "
					"set mask OK "
					" rs 0x%x ldv %d mask 0x%x",
					rs, ldvp->ldv,
					ldvp->ldv_ldf_masks));
			}
		}
	}
	ldgp = ldgvp->ldgp;
	/* Configure timer and arm bit */
	for (i = 0; i < nxgep->ldgvp->ldg_intrs; i++, ldgp++) {
		rs = npi_intr_ldg_mgmt_set(handle, ldgp->ldg,
			ldgp->arm, ldgp->ldg_timer);
		if (rs != NPI_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
				"<== nxge_intr_mask_mgmt: "
				"set timer failed "
				" rs 0x%x dg %d timer 0x%x",
				rs, ldgp->ldg, ldgp->ldg_timer));
			return (NXGE_ERROR | rs);
		}
		NXGE_DEBUG_MSG((nxgep, INT_CTL,
			"==> nxge_intr_mask_mgmt: "
			"set timer OK "
			" rs 0x%x ldg %d timer 0x%x",
			rs, ldgp->ldg, ldgp->ldg_timer));
	}

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "<== nxge_fzc_intr_mask_mgmt"));
	return (NXGE_OK);
}

nxge_status_t
nxge_intr_mask_mgmt_set(p_nxge_t nxgep, boolean_t on)
{
	p_nxge_ldgv_t ldgvp;
	p_nxge_ldg_t ldgp;
	p_nxge_ldv_t ldvp;
	npi_handle_t handle;
	int i, j;
	npi_status_t rs = NPI_SUCCESS;

	NXGE_DEBUG_MSG((nxgep, INT_CTL,
		"==> nxge_intr_mask_mgmt_set (%d)", on));

	if (nxgep->niu_type == N2_NIU) {
		NXGE_DEBUG_MSG((nxgep, INT_CTL,
			"<== nxge_intr_mask_mgmt_set (%d) not set (N2/NIU)",
			on));
		return (NXGE_ERROR);
	}

	if ((ldgvp = nxgep->ldgvp) == NULL) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			"==> nxge_intr_mask_mgmt_set: Null ldgvp"));
		return (NXGE_ERROR);
	}

	handle = NXGE_DEV_NPI_HANDLE(nxgep);
	ldgp = ldgvp->ldgp;
	ldvp = ldgvp->ldvp;
	if (ldgp == NULL || ldvp == NULL) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			"<== nxge_intr_mask_mgmt_set: Null ldgp or ldvp"));
		return (NXGE_ERROR);
	}
	/* set masks. */
	for (i = 0; i < ldgvp->ldg_intrs; i++, ldgp++) {
		NXGE_DEBUG_MSG((nxgep, INT_CTL,
			"==> nxge_intr_mask_mgmt_set: flag %d ldg %d"
			"set mask nldvs %d", on, ldgp->ldg, ldgp->nldvs));
		for (j = 0; j < ldgp->nldvs; j++, ldvp++) {
			NXGE_DEBUG_MSG((nxgep, INT_CTL,
				"==> nxge_intr_mask_mgmt_set: "
				"for %d %d flag %d", i, j, on));
			if (on) {
				ldvp->ldv_ldf_masks = 0;
				NXGE_DEBUG_MSG((nxgep, INT_CTL,
					"==> nxge_intr_mask_mgmt_set: "
					"ON mask off"));
			} else if (!on) {
				ldvp->ldv_ldf_masks = (uint8_t)LD_IM1_MASK;
				NXGE_DEBUG_MSG((nxgep, INT_CTL,
					"==> nxge_intr_mask_mgmt_set:mask on"));
			}
			rs = npi_intr_mask_set(handle, ldvp->ldv,
				ldvp->ldv_ldf_masks);
			if (rs != NPI_SUCCESS) {
				NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
					"==> nxge_intr_mask_mgmt_set: "
					"set mask failed "
					" rs 0x%x ldv %d mask 0x%x",
					rs, ldvp->ldv, ldvp->ldv_ldf_masks));
				return (NXGE_ERROR | rs);
			}
			NXGE_DEBUG_MSG((nxgep, INT_CTL,
				"==> nxge_intr_mask_mgmt_set: flag %d"
				"set mask OK "
				" ldv %d mask 0x%x",
				on, ldvp->ldv, ldvp->ldv_ldf_masks));
		}
	}

	ldgp = ldgvp->ldgp;
	/* set the arm bit */
	for (i = 0; i < nxgep->ldgvp->ldg_intrs; i++, ldgp++) {
		if (on && !ldgp->arm) {
			ldgp->arm = B_TRUE;
		} else if (!on && ldgp->arm) {
			ldgp->arm = B_FALSE;
		}
		rs = npi_intr_ldg_mgmt_set(handle, ldgp->ldg,
			ldgp->arm, ldgp->ldg_timer);
		if (rs != NPI_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
				"<== nxge_intr_mask_mgmt_set: "
				"set timer failed "
				" rs 0x%x ldg %d timer 0x%x",
				rs, ldgp->ldg, ldgp->ldg_timer));
			return (NXGE_ERROR | rs);
		}
		NXGE_DEBUG_MSG((nxgep, INT_CTL,
			"==> nxge_intr_mask_mgmt_set: OK (flag %d) "
			"set timer "
			" ldg %d timer 0x%x",
			on, ldgp->ldg, ldgp->ldg_timer));
	}

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "<== nxge_intr_mask_mgmt_set"));
	return (NXGE_OK);
}

static nxge_status_t
nxge_get_mac_addr_properties(p_nxge_t nxgep)
{
	uchar_t *prop_val;
	uint_t prop_len;
	uint_t i;
	uint8_t func_num;
	uint8_t num_macs;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_get_mac_addr_properties "));

#if defined(_BIG_ENDIAN)
	/*
	 * Get the ethernet address.
	 */
	(void) localetheraddr((struct ether_addr *)NULL, &nxgep->ouraddr);

	/*
	 * Check if it is an adapter with its own local mac address If it is
	 * present, override the system mac address.
	 */
	if (ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, nxgep->dip, 0,
			"local-mac-address", &prop_val,
			&prop_len) == DDI_PROP_SUCCESS) {
		if (prop_len == ETHERADDRL) {
			nxgep->factaddr = *(p_ether_addr_t)prop_val;
			NXGE_DEBUG_MSG((nxgep, DDI_CTL, "Local mac address = "
				"%02x:%02x:%02x:%02x:%02x:%02x",
				prop_val[0], prop_val[1], prop_val[2],
				prop_val[3], prop_val[4], prop_val[5]));
		}
		ddi_prop_free(prop_val);
	}
	if (ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, nxgep->dip, 0,
			"local-mac-address?", &prop_val,
			&prop_len) == DDI_PROP_SUCCESS) {
		if (strncmp("true", (caddr_t)prop_val, (size_t)prop_len) == 0) {
			nxgep->ouraddr = nxgep->factaddr;
			NXGE_DEBUG_MSG((nxgep, DDI_CTL,
				"Using local MAC address"));
		}
		ddi_prop_free(prop_val);
	} else {
		nxgep->ouraddr = nxgep->factaddr;
	}
#else
	(void) nxge_espc_mac_addrs_get(nxgep);
	nxgep->ouraddr = nxgep->factaddr;
#endif

	func_num = nxgep->function_num;

	/* NIU does not need max_num_mmac */
	if (nxgep->niu_type == NEPTUNE || nxgep->niu_type == NEPTUNE_2) {
		if (nxge_espc_num_macs_get(nxgep, &num_macs) == NXGE_OK) {
			nxgep->nxge_mmac_info.max_num_mmac = num_macs;
		} else {
			NXGE_ERROR_MSG((NULL, NXGE_ERR_CTL,
				"nxge_espc_num_macs_get: espc access failed"));
			return (NXGE_ERROR);
		}
	}

	/*
	 * Note: mac-addresses of n2-niu is the list of mac addresses for a
	 * port. #mac-addresses stored in Neptune's SEEPROM is the total number
	 * of MAC addresses allocated for a board.
	 */
	if (nxgep->niu_type == N2_NIU) {
		if (ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, nxgep->dip, 0,
				"mac-addresses", &prop_val, &prop_len) ==
			DDI_PROP_SUCCESS) {
			/*
			 * XAUI may have up to 18 MACs, more than the XMAC can
			 * use (1 unique MAC plus 16 alternate MACs)
			 */
			nxgep->nxge_mmac_info.num_mmac = prop_len / 6;
			if (nxgep->nxge_mmac_info.num_mmac >
				XMAC_MAX_ALT_ADDR_ENTRY + 1) {
				nxgep->nxge_mmac_info.num_mmac =
					XMAC_MAX_ALT_ADDR_ENTRY + 1;
			}
			ddi_prop_free(prop_val);
		}
	} else {
		nxgep->nxge_mmac_info.num_mmac
			= nxgep->nxge_mmac_info.max_num_mmac >>
			(nxgep->nports >> 1);
	}

	for (i = 0; i < nxgep->nxge_mmac_info.num_mmac - 1; ++i) {
		/* Initialze all mac addr. to "AVAILABLE" state */
		nxgep->nxge_mmac_info.rsv_mmac[i] = B_FALSE;
		/*
		 * XMAC: Disable alter MAC address comparison only (XMAC's
		 * unique MAC comparison is always enabled. BMAC: (Neptune
		 * only) Disable both unique and alter MAC address comparison
		 */
		(void) npi_mac_altaddr_disable(nxgep->npi_handle,
			NXGE_GET_PORT_NUM(func_num), i);
	}

	/*
	 * Initialize alt. mac addr. in the mac pool
	 */
	(void) nxge_init_mmac(nxgep);
	return (NXGE_OK);
}

void
nxge_get_xcvr_properties(p_nxge_t nxgep)
{
	uchar_t *prop_val;
	uint_t prop_len;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_get_xcvr_properties"));

	/*
	 * Read the type of physical layer interface being used.
	 */
	nxgep->statsp->mac_stats.xcvr_inuse = INT_MII_XCVR;
	if (ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, nxgep->dip, 0,
			"phy-type", &prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		if (strncmp("pcs", (caddr_t)prop_val,
				(size_t)prop_len) == 0) {
			nxgep->statsp->mac_stats.xcvr_inuse = PCS_XCVR;
		} else {
			nxgep->statsp->mac_stats.xcvr_inuse = INT_MII_XCVR;
		}
		ddi_prop_free(prop_val);
	} else if (ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, nxgep->dip, 0,
			"phy-interface", &prop_val,
			&prop_len) == DDI_PROP_SUCCESS) {
		if (strncmp("pcs", (caddr_t)prop_val, (size_t)prop_len) == 0) {
			nxgep->statsp->mac_stats.xcvr_inuse = PCS_XCVR;
		} else {
			nxgep->statsp->mac_stats.xcvr_inuse = INT_MII_XCVR;
		}
		ddi_prop_free(prop_val);
	}
}

/*
 * Static functions start here.
 */

static void
nxge_ldgv_setup(p_nxge_ldg_t *ldgp, p_nxge_ldv_t *ldvp, uint8_t ldv,
	uint8_t endldg, int *ngrps)
{
	NXGE_DEBUG_MSG((NULL, INT_CTL, "==> nxge_ldgv_setup"));
	/* Assign the group number for each device. */
	(*ldvp)->ldg_assigned = (*ldgp)->ldg;
	(*ldvp)->ldgp = *ldgp;
	(*ldvp)->ldv = ldv;

	NXGE_DEBUG_MSG((NULL, INT_CTL, "==> nxge_ldgv_setup: "
		"ldv %d endldg %d ldg %d, ldvp $%p",
		ldv, endldg, (*ldgp)->ldg, (*ldgp)->ldvp));

	(*ldgp)->nldvs++;
	if ((*ldgp)->ldg == (endldg - 1)) {
		if ((*ldgp)->ldvp == NULL) {
			(*ldgp)->ldvp = *ldvp;
			*ngrps += 1;
			NXGE_DEBUG_MSG((NULL, INT_CTL,
				"==> nxge_ldgv_setup: ngrps %d", *ngrps));
		}
		NXGE_DEBUG_MSG((NULL, INT_CTL,
			"==> nxge_ldgv_setup: ldvp $%p ngrps %d",
			*ldvp, *ngrps));
		++*ldvp;
	} else {
		(*ldgp)->ldvp = *ldvp;
		*ngrps += 1;
		NXGE_DEBUG_MSG((NULL, INT_CTL, "==> nxge_ldgv_setup(done): "
			"ldv %d endldg %d ldg %d, ldvp $%p",
			ldv, endldg, (*ldgp)->ldg, (*ldgp)->ldvp));
		(*ldvp) = ++*ldvp;
		(*ldgp) = ++*ldgp;
		NXGE_DEBUG_MSG((NULL, INT_CTL,
			"==> nxge_ldgv_setup: new ngrps %d", *ngrps));
	}

	NXGE_DEBUG_MSG((NULL, INT_CTL, "==> nxge_ldgv_setup: "
		"ldv %d ldvp $%p endldg %d ngrps %d",
		ldv, ldvp, endldg, *ngrps));

	NXGE_DEBUG_MSG((NULL, INT_CTL, "<== nxge_ldgv_setup"));
}

/*
 * Note: This function assume the following distribution of mac
 * addresses among 4 ports in neptune:
 *
 *      -------------
 *    0|            |0 - local-mac-address for fn 0
 *      -------------
 *    1|            |1 - local-mac-address for fn 1
 *      -------------
 *    2|            |2 - local-mac-address for fn 2
 *      -------------
 *    3|            |3 - local-mac-address for fn 3
 *      -------------
 *     |            |4 - Start of alt. mac addr. for fn 0
 *     |            |
 *     |            |
 *     |            |10
 *     --------------
 *     |            |11 - Start of alt. mac addr. for fn 1
 *     |            |
 *     |            |
 *     |            |17
 *     --------------
 *     |            |18 - Start of alt. mac addr. for fn 2
 *     |            |
 *     |            |
 *     |            |24
 *     --------------
 *     |            |25 - Start of alt. mac addr. for fn 3
 *     |            |
 *     |            |
 *     |            |31
 *     --------------
 *
 * For N2/NIU the mac addresses is from XAUI card.
 */

static void
nxge_init_mmac(p_nxge_t nxgep)
{
	int i;
	uint8_t func_num;
	uint16_t *base_mmac_addr;
	uint32_t first_alt_mac_ls4b;
	uint16_t *mmac_addr;
	uint32_t base_mac_ls4b;	/* least significant 4 bytes */
	nxge_mmac_t *mac_poolp;
	npi_mac_addr_t mac_addr;

	func_num = nxgep->function_num;
	base_mmac_addr = (uint16_t *)&nxgep->factaddr;
	mac_poolp = (nxge_mmac_t *)&nxgep->nxge_mmac_info;

	base_mac_ls4b = ((uint32_t)base_mmac_addr[1]) << 16 |
		base_mmac_addr[2];

	if (nxgep->niu_type == N2_NIU)
		first_alt_mac_ls4b = base_mac_ls4b + 1;
	else			/* Neptune */
		first_alt_mac_ls4b = base_mac_ls4b + (nxgep->nports - func_num)
			+ (func_num * (nxgep->nxge_mmac_info.num_mmac - 1));

	for (i = 0; i < nxgep->nxge_mmac_info.num_mmac - 1; ++i) {
		/*
		 * Populate shadow mac pool w/ available mac. so we dont have
		 * to read the h/w to search for a mac addr.
		 */
		mmac_addr = (uint16_t *)&mac_poolp->mmac_pool[i];
		mmac_addr[0] = base_mmac_addr[0];
		mac_addr.w0 = mmac_addr[0];

		mmac_addr[1] = (first_alt_mac_ls4b >> 16) & 0x0FFFF;
		mac_addr.w1 = mmac_addr[1];

		mmac_addr[2] = first_alt_mac_ls4b & 0x0FFFF;
		mac_addr.w2 = mmac_addr[2];

		/*
		 * Program the h/w alt. mac address, starting from reg1(reg0
		 * corr. to unique mac addr)
		 */
		(void) npi_mac_altaddr_entry(nxgep->npi_handle, OP_SET,
			NXGE_GET_PORT_NUM(func_num), i, &mac_addr);
		first_alt_mac_ls4b++;
	}
}
