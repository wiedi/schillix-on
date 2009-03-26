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

/*
 *	Library file that has miscellaneous support for npe(7d)
 */

#include <sys/conf.h>
#include <sys/pci.h>
#include <sys/sunndi.h>
#include <sys/acpi/acpi.h>
#include <sys/acpi/acpi_pci.h>
#include <sys/acpica.h>
#include <sys/pci_cap.h>
#include <sys/pcie_impl.h>
#include <sys/x86_archext.h>
#include <io/pciex/pcie_nvidia.h>
#include <io/pciex/pcie_nb5000.h>

/*
 * Prototype declaration
 */
void	npe_query_acpi_mcfg(dev_info_t *dip);
void	npe_ck804_fix_aer_ptr(ddi_acc_handle_t cfg_hdl);
int	npe_disable_empty_bridges_workaround(dev_info_t *child);
void	npe_nvidia_error_mask(ddi_acc_handle_t cfg_hdl);
void	npe_intel_error_mask(ddi_acc_handle_t cfg_hdl);
boolean_t npe_is_child_pci(dev_info_t *dip);
boolean_t check_and_set_mmcfg(dev_info_t *dip);

/*
 * Default ecfga base address
 */
int64_t npe_default_ecfga_base = 0xE0000000;

extern uint32_t	npe_aer_uce_mask;
extern boolean_t pcie_full_scan;

/* AMD's northbridges vendor-id and device-ids */
#define	AMD_NTBRDIGE_VID		0x1022	/* AMD vendor-id */
#define	AMD_HT_NTBRIDGE_DID		0x1100	/* HT Configuration */
#define	AMD_AM_NTBRIDGE_DID		0x1101	/* Address Map */
#define	AMD_DC_NTBRIDGE_DID		0x1102	/* DRAM Controller */
#define	AMD_MC_NTBRIDGE_DID		0x1103	/* Misc Controller */
#define	AMD_K10_NTBRIDGE_DID_0		0x1200
#define	AMD_K10_NTBRIDGE_DID_1		0x1201
#define	AMD_K10_NTBRIDGE_DID_2		0x1202
#define	AMD_K10_NTBRIDGE_DID_3		0x1203
#define	AMD_K10_NTBRIDGE_DID_4		0x1204

/*
 * Check if the given device is an AMD northbridge
 */
#define	IS_BAD_AMD_NTBRIDGE(vid, did) \
	    (((vid) == AMD_NTBRDIGE_VID) && \
	    (((did) == AMD_HT_NTBRIDGE_DID) || \
	    ((did) == AMD_AM_NTBRIDGE_DID) || \
	    ((did) == AMD_DC_NTBRIDGE_DID) || \
	    ((did) == AMD_MC_NTBRIDGE_DID)))

#define	IS_K10_AMD_NTBRIDGE(vid, did) \
	    (((vid) == AMD_NTBRDIGE_VID) && \
	    (((did) == AMD_K10_NTBRIDGE_DID_0) || \
	    ((did) == AMD_K10_NTBRIDGE_DID_1) || \
	    ((did) == AMD_K10_NTBRIDGE_DID_2) || \
	    ((did) == AMD_K10_NTBRIDGE_DID_3) || \
	    ((did) == AMD_K10_NTBRIDGE_DID_4)))

#define	MSR_AMD_NB_MMIO_CFG_BADDR	0xc0010058
#define	AMD_MMIO_CFG_BADDR_ADDR_MASK	0xFFFFFFF00000ULL
#define	AMD_MMIO_CFG_BADDR_ENA_MASK	0x000000000001ULL
#define	AMD_MMIO_CFG_BADDR_ENA_ON	0x000000000001ULL
#define	AMD_MMIO_CFG_BADDR_ENA_OFF	0x000000000000ULL


/*
 * Query the MCFG table using ACPI.  If MCFG is found, setup the
 * 'ecfg' property accordingly.  Otherwise, set the values
 * to the default values.
 */
void
npe_query_acpi_mcfg(dev_info_t *dip)
{
	MCFG_TABLE *mcfgp;
	CFG_BASE_ADDR_ALLOC *cfg_baap;
	char *cfg_baa_endp;
	int64_t ecfginfo[4];
	int ecfg_found = 0;

	/* Query the MCFG table using ACPI */
	if ((acpica_init() == AE_OK) && (AcpiGetTable(ACPI_SIG_MCFG, 1,
	    (ACPI_TABLE_HEADER **)&mcfgp) == AE_OK)) {

		cfg_baap = (CFG_BASE_ADDR_ALLOC *)mcfgp->CfgBaseAddrAllocList;
		cfg_baa_endp = ((char *)mcfgp) + mcfgp->Length;

		while ((char *)cfg_baap < cfg_baa_endp) {
			if (cfg_baap->base_addr != (uint64_t)0 &&
			    cfg_baap->segment == 0) {
				/*
				 * Set up the 'ecfg' property to hold
				 * base_addr, segment, and first/last bus.
				 * We only do the first entry that maps
				 * segment 0; nonzero segments are not yet
				 * known, or handled.  If they appear,
				 * we'll need to figure out which bus node
				 * should have which entry by examining the
				 * ACPI _SEG method on each bus node.
				 */
				ecfginfo[0] = cfg_baap->base_addr;
				ecfginfo[1] = cfg_baap->segment;
				ecfginfo[2] = cfg_baap->start_bno;
				ecfginfo[3] = cfg_baap->end_bno;
				(void) ndi_prop_update_int64_array(
				    DDI_DEV_T_NONE, dip, "ecfg",
				    ecfginfo, 4);
				ecfg_found = 1;
				break;
			}
			cfg_baap++;
		}
	}
	if (ecfg_found)
		return;
	/*
	 * If MCFG is not found or ecfga_base is not found in MCFG table,
	 * set the property to the default values.
	 */
	ecfginfo[0] = npe_default_ecfga_base;
	ecfginfo[1] = 0;		/* segment 0 */
	ecfginfo[2] = 0;		/* first bus 0 */
	ecfginfo[3] = 0xff;		/* last bus ff */
	(void) ndi_prop_update_int64_array(DDI_DEV_T_NONE, dip,
	    "ecfg", ecfginfo, 4);
}


/*
 * Enable reporting of AER capability next pointer.
 * This needs to be done only for CK8-04 devices
 * by setting NV_XVR_VEND_CYA1 (offset 0xf40) bit 13
 * NOTE: BIOS is disabling this, it needs to be enabled temporarily
 */
void
npe_ck804_fix_aer_ptr(ddi_acc_handle_t cfg_hdl)
{
	ushort_t cya1;

	if ((pci_config_get16(cfg_hdl, PCI_CONF_VENID) == NVIDIA_VENDOR_ID) &&
	    (pci_config_get16(cfg_hdl, PCI_CONF_DEVID) ==
	    NVIDIA_CK804_DEVICE_ID) &&
	    (pci_config_get8(cfg_hdl, PCI_CONF_REVID) >=
	    NVIDIA_CK804_AER_VALID_REVID)) {
		cya1 =  pci_config_get16(cfg_hdl, NVIDIA_CK804_VEND_CYA1_OFF);
		if (!(cya1 & ~NVIDIA_CK804_VEND_CYA1_ERPT_MASK))
			(void) pci_config_put16(cfg_hdl,
			    NVIDIA_CK804_VEND_CYA1_OFF,
			    cya1 | NVIDIA_CK804_VEND_CYA1_ERPT_VAL);
	}
}

/*
 * If the bridge is empty, disable it
 */
int
npe_disable_empty_bridges_workaround(dev_info_t *child)
{
	/*
	 * Do not bind drivers to empty bridges.
	 * Fail above, if the bridge is found to be hotplug capable
	 */
	if (ddi_driver_major(child) == ddi_name_to_major("pcie_pci") &&
	    ddi_get_child(child) == NULL &&
	    ddi_prop_get_int(DDI_DEV_T_ANY, child, DDI_PROP_DONTPASS,
	    "pci-hotplug-type", INBAND_HPC_NONE) == INBAND_HPC_NONE)
		return (1);

	return (0);
}

void
npe_nvidia_error_mask(ddi_acc_handle_t cfg_hdl) {
	uint32_t regs;
	uint16_t vendor_id = pci_config_get16(cfg_hdl, PCI_CONF_VENID);
	uint16_t dev_id = pci_config_get16(cfg_hdl, PCI_CONF_DEVID);

	if ((vendor_id == NVIDIA_VENDOR_ID) && NVIDIA_PCIE_RC_DEV_ID(dev_id)) {
		/* Disable ECRC for all devices */
		regs = pcie_get_aer_uce_mask() | npe_aer_uce_mask |
		    PCIE_AER_UCE_ECRC;
		pcie_set_aer_uce_mask(regs);

		/*
		 * Turn full scan on since the Error Source ID register may not
		 * have the correct ID.
		 */
		pcie_full_scan = B_TRUE;
	}
}

void
npe_intel_error_mask(ddi_acc_handle_t cfg_hdl) {
	uint32_t regs;
	uint16_t vendor_id = pci_config_get16(cfg_hdl, PCI_CONF_VENID);

	if (vendor_id == INTEL_VENDOR_ID) {
		/*
		 * Due to an errata in Intel's ESB2 southbridge, all ECRCs
		 * generation/checking need to be disabled.  There is a
		 * workaround by setting a proprietary bit in the ESB2, but it
		 * is not well documented or understood.  If that bit is set in
		 * the future, then ECRC generation/checking should be enabled
		 * again.
		 *
		 * Disable ECRC generation/checking by masking ECRC in the AER
		 * UE Mask.  The pcie misc module would then automatically
		 * disable ECRC generation/checking in the AER Control register.
		 */
		regs = pcie_get_aer_uce_mask() | PCIE_AER_UCE_ECRC;
		pcie_set_aer_uce_mask(regs);
	}
}

/*
 * Check's if this child is a PCI device.
 * Child is a PCI device if:
 * parent has a dev_type of "pci"
 * -and-
 * child does not have a dev_type of "pciex"
 *
 * If the parent is not of dev_type "pci", then assume it is "pciex" and all
 * children should support using PCIe style MMCFG access.
 *
 * If parent's dev_type is "pci" and child is "pciex", then also enable using
 * PCIe style MMCFG access.  This covers the case where NPE is "pci" and a PCIe
 * RP is beneath.
 */
boolean_t
npe_child_is_pci(dev_info_t *dip) {
	char *dev_type;
	boolean_t parent_is_pci, child_is_pciex;

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, ddi_get_parent(dip),
	    DDI_PROP_DONTPASS, "device_type", &dev_type) ==
	    DDI_PROP_SUCCESS) {
		parent_is_pci = (strcmp(dev_type, "pci") == 0);
		ddi_prop_free(dev_type);
	} else {
		parent_is_pci = B_FALSE;
	}

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "device_type", &dev_type) == DDI_PROP_SUCCESS) {
		child_is_pciex = (strcmp(dev_type, "pciex") == 0);
		ddi_prop_free(dev_type);
	} else {
		child_is_pciex = B_FALSE;
	}

	return (parent_is_pci && !child_is_pciex);
}

/*
 * Checks to see if MMCFG is supported and enables it if necessary.
 * Returns: TRUE is MMCFG is support, FLASE is not.
 *
 * In general if a device sits below a parent who's "dev_type" is "pciex" the
 * support MMCFG.  Otherwise, default back to legacy IOCFG access.
 *
 * Enable Legacy PCI config space access for AMD K8 north bridges.
 *	Host bridge: AMD HyperTransport Technology Configuration
 *	Host bridge: AMD Address Map
 *	Host bridge: AMD DRAM Controller
 *	Host bridge: AMD Miscellaneous Control
 * These devices do not support MMCFG access.
 *
 * Enable MMCFG via msr for AMD K10 north bridges
 */
boolean_t
npe_check_and_set_mmcfg(dev_info_t *dip)
{
	int vendor_id, device_id;
	int64_t data;

	vendor_id = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "vendor-id", -1);
	device_id = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "device-id", -1);

	if (IS_K10_AMD_NTBRIDGE(vendor_id, device_id)) {
		data = ddi_prop_get_int64(DDI_DEV_T_ANY, dip, 0,
		    "ecfga-base-address", 0);
		data &= AMD_MMIO_CFG_BADDR_ADDR_MASK;
		data |= AMD_MMIO_CFG_BADDR_ENA_ON;
		wrmsr(MSR_AMD_NB_MMIO_CFG_BADDR, data);
	}

	return !(npe_child_is_pci(dip) ||
	    IS_BAD_AMD_NTBRIDGE(vendor_id, device_id));
}
