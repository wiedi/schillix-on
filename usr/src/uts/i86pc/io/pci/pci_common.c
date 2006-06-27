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
 *	File that has code which is common between pci(7d) and npe(7d)
 *	It shares the following:
 *	- interrupt code
 *	- pci_tools ioctl code
 *	- name_child code
 *	- set_parent_private_data code
 */

#include <sys/conf.h>
#include <sys/pci.h>
#include <sys/sunndi.h>
#include <sys/mach_intr.h>
#include <sys/hotplug/pci/pcihp.h>
#include <sys/pci_intr_lib.h>
#include <sys/psm.h>
#include <sys/policy.h>
#include <sys/sysmacros.h>
#include <sys/clock.h>
#include <io/pcplusmp/apic.h>
#include <sys/pci_tools.h>
#include <io/pci/pci_var.h>
#include <io/pci/pci_tools_ext.h>
#include <io/pci/pci_common.h>
#include <sys/pci_cfgspace.h>
#include <sys/pci_impl.h>

/*
 * Function prototypes
 */
static int	pci_get_priority(dev_info_t *, ddi_intr_handle_impl_t *, int *);
static int	pci_get_nintrs(dev_info_t *, int, int *);
static int	pci_enable_intr(dev_info_t *, dev_info_t *,
		    ddi_intr_handle_impl_t *, uint32_t);
static void	pci_disable_intr(dev_info_t *, dev_info_t *,
		    ddi_intr_handle_impl_t *, uint32_t);

/* Extern decalration for pcplusmp module */
extern int	(*psm_intr_ops)(dev_info_t *, ddi_intr_handle_impl_t *,
		    psm_intr_op_t, int *);


/*
 * pci_name_child:
 *
 *	Assign the address portion of the node name
 */
int
pci_common_name_child(dev_info_t *child, char *name, int namelen)
{
	int		dev, func, length;
	char		**unit_addr;
	uint_t		n;
	pci_regspec_t	*pci_rp;

	if (ndi_dev_is_persistent_node(child) == 0) {
		/*
		 * For .conf node, use "unit-address" property
		 */
		if (ddi_prop_lookup_string_array(DDI_DEV_T_ANY, child,
		    DDI_PROP_DONTPASS, "unit-address", &unit_addr, &n) !=
		    DDI_PROP_SUCCESS) {
			cmn_err(CE_WARN, "cannot find unit-address in %s.conf",
			    ddi_get_name(child));
			return (DDI_FAILURE);
		}
		if (n != 1 || *unit_addr == NULL || **unit_addr == 0) {
			cmn_err(CE_WARN, "unit-address property in %s.conf"
			    " not well-formed", ddi_get_name(child));
			ddi_prop_free(unit_addr);
			return (DDI_FAILURE);
		}
		(void) snprintf(name, namelen, "%s", *unit_addr);
		ddi_prop_free(unit_addr);
		return (DDI_SUCCESS);
	}

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, child, DDI_PROP_DONTPASS,
	    "reg", (int **)&pci_rp, (uint_t *)&length) != DDI_PROP_SUCCESS) {
		cmn_err(CE_WARN, "cannot find reg property in %s",
		    ddi_get_name(child));
		return (DDI_FAILURE);
	}

	/* copy the device identifications */
	dev = PCI_REG_DEV_G(pci_rp->pci_phys_hi);
	func = PCI_REG_FUNC_G(pci_rp->pci_phys_hi);

	/*
	 * free the memory allocated by ddi_prop_lookup_int_array
	 */
	ddi_prop_free(pci_rp);

	if (func != 0) {
		(void) snprintf(name, namelen, "%x,%x", dev, func);
	} else {
		(void) snprintf(name, namelen, "%x", dev);
	}

	return (DDI_SUCCESS);
}

/*
 * Interrupt related code:
 *
 * The following busop is common to npe and pci drivers
 *	bus_introp
 */

/*
 * Create the ddi_parent_private_data for a pseudo child.
 */
void
pci_common_set_parent_private_data(dev_info_t *dip)
{
	struct ddi_parent_private_data *pdptr;

	pdptr = (struct ddi_parent_private_data *)kmem_zalloc(
	    (sizeof (struct ddi_parent_private_data) +
	sizeof (struct intrspec)), KM_SLEEP);
	pdptr->par_intr = (struct intrspec *)(pdptr + 1);
	pdptr->par_nintr = 1;
	ddi_set_parent_data(dip, pdptr);
}

/*
 * pci_get_priority:
 *	Figure out the priority of the device
 */
static int
pci_get_priority(dev_info_t *dip, ddi_intr_handle_impl_t *hdlp, int *pri)
{
	struct intrspec *ispec;

	DDI_INTR_NEXDBG((CE_CONT, "pci_get_priority: dip = 0x%p, hdlp = %p\n",
	    (void *)dip, (void *)hdlp));

	if ((ispec = (struct intrspec *)pci_intx_get_ispec(dip, dip,
	    hdlp->ih_inum)) == NULL) {
		if (DDI_INTR_IS_MSI_OR_MSIX(hdlp->ih_type)) {
			int class = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
			    DDI_PROP_DONTPASS, "class-code", -1);

			*pri = (class == -1) ? 1 : pci_devclass_to_ipl(class);
			pci_common_set_parent_private_data(hdlp->ih_dip);
			ispec = (struct intrspec *)pci_intx_get_ispec(dip, dip,
			    hdlp->ih_inum);
			return (DDI_SUCCESS);
		}
		return (DDI_FAILURE);
	}

	*pri = ispec->intrspec_pri;
	return (DDI_SUCCESS);
}


/*
 * pci_get_nintrs:
 *	Figure out how many interrupts the device supports
 */
static int
pci_get_nintrs(dev_info_t *dip, int type, int *nintrs)
{
	int	ret;

	*nintrs = 0;

	if (DDI_INTR_IS_MSI_OR_MSIX(type))
		ret = pci_msi_get_nintrs(dip, type, nintrs);
	else {
		ret = DDI_FAILURE;
		if (ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		    "interrupts", -1) != -1) {
			*nintrs = 1;
			ret = DDI_SUCCESS;
		}
	}

	return (ret);
}

static int pcie_pci_intr_pri_counter = 0;

/*
 * pci_common_intr_ops: bus_intr_op() function for interrupt support
 */
int
pci_common_intr_ops(dev_info_t *pdip, dev_info_t *rdip, ddi_intr_op_t intr_op,
    ddi_intr_handle_impl_t *hdlp, void *result)
{
	int			priority = 0;
	int			psm_status = 0;
	int			pci_status = 0;
	int			pci_rval, psm_rval = PSM_FAILURE;
	int			types = 0;
	int			pciepci = 0;
	int			i, j, count;
	int			behavior;
	int			cap_ptr;
	ddi_intrspec_t		isp;
	struct intrspec		*ispec;
	ddi_intr_handle_impl_t	tmp_hdl;
	ddi_intr_msix_t		*msix_p;
	ihdl_plat_t		*ihdl_plat_datap;
	ddi_intr_handle_t	*h_array;
	ddi_acc_handle_t	handle;

	DDI_INTR_NEXDBG((CE_CONT,
	    "pci_common_intr_ops: pdip 0x%p, rdip 0x%p, op %x handle 0x%p\n",
	    (void *)pdip, (void *)rdip, intr_op, (void *)hdlp));

	/* Process the request */
	switch (intr_op) {
	case DDI_INTROP_SUPPORTED_TYPES:
		/* Fixed supported by default */
		*(int *)result = DDI_INTR_TYPE_FIXED;

		/* Figure out if MSI or MSI-X is supported? */
		if (pci_msi_get_supported_type(rdip, &types) != DDI_SUCCESS)
			return (DDI_SUCCESS);

		if (psm_intr_ops != NULL) {
			/*
			 * Only support MSI for now, OR it in
			 */
			*(int *)result |= (types & DDI_INTR_TYPE_MSI);

			tmp_hdl.ih_type = *(int *)result;
			(void) (*psm_intr_ops)(rdip, &tmp_hdl,
			    PSM_INTR_OP_CHECK_MSI, result);
			DDI_INTR_NEXDBG((CE_CONT, "pci_common_intr_ops: "
			    "rdip: 0x%p supported types: 0x%x\n", (void *)rdip,
			    *(int *)result));
		}
		break;
	case DDI_INTROP_NINTRS:
		if (pci_get_nintrs(rdip, hdlp->ih_type, result) != DDI_SUCCESS)
			return (DDI_FAILURE);
		break;
	case DDI_INTROP_ALLOC:
		/*
		 * MSI or MSIX (figure out number of vectors available)
		 * FIXED interrupts: just return available interrupts
		 */
		if (DDI_INTR_IS_MSI_OR_MSIX(hdlp->ih_type) &&
		    (psm_intr_ops != NULL) &&
		    (pci_get_priority(rdip, hdlp, &priority) == DDI_SUCCESS)) {
			/*
			 * Following check is a special case for 'pcie_pci'.
			 * This makes sure vectors with the right priority
			 * are allocated for pcie_pci during ALLOC time.
			 */
			if (strcmp(ddi_driver_name(rdip), "pcie_pci") == 0) {
				hdlp->ih_pri =
				    (pcie_pci_intr_pri_counter % 2) ? 4 : 7;
				pciepci = 1;
			} else
				hdlp->ih_pri = priority;
			behavior = (int)(uintptr_t)hdlp->ih_scratch2;

			/*
			 * Cache in the config handle and cap_ptr
			 */
			if (i_ddi_get_pci_config_handle(rdip) == NULL) {
				if (pci_config_setup(rdip, &handle) !=
				    DDI_SUCCESS)
					return (DDI_FAILURE);
				i_ddi_set_pci_config_handle(rdip, handle);
			}

			if (i_ddi_get_msi_msix_cap_ptr(rdip) == 0) {
				char *prop =
				    (hdlp->ih_type == DDI_INTR_TYPE_MSI) ?
				    "pci-msi-capid-pointer" :
				    "pci-msix-capid-pointer";

				cap_ptr = ddi_prop_get_int(DDI_DEV_T_ANY, rdip,
				    DDI_PROP_DONTPASS, prop,
				    PCI_CAP_NEXT_PTR_NULL);
				i_ddi_set_msi_msix_cap_ptr(rdip, cap_ptr);
			}


			(void) (*psm_intr_ops)(rdip, hdlp,
			    PSM_INTR_OP_ALLOC_VECTORS, result);

			/* verify behavior flag and take appropriate action */
			if ((behavior == DDI_INTR_ALLOC_STRICT) &&
			    (*(int *)result < hdlp->ih_scratch1)) {
				DDI_INTR_NEXDBG((CE_CONT,
				    "pci_common_intr_ops: behavior %x, "
				    "couldn't get enough intrs\n", behavior));
				hdlp->ih_scratch1 = *(int *)result;
				(void) (*psm_intr_ops)(rdip, hdlp,
				    PSM_INTR_OP_FREE_VECTORS, NULL);
				return (DDI_EAGAIN);
			}

			if (hdlp->ih_type == DDI_INTR_TYPE_MSIX) {
				if (!(msix_p = i_ddi_get_msix(hdlp->ih_dip))) {
					msix_p = pci_msix_init(hdlp->ih_dip);
					if (msix_p)
						i_ddi_set_msix(hdlp->ih_dip,
						    msix_p);
				}
			}

			if (pciepci) {
				/* update priority in ispec */
				isp = pci_intx_get_ispec(pdip, rdip,
					(int)hdlp->ih_inum);
				ispec = (struct intrspec *)isp;
				if (ispec)
					ispec->intrspec_pri = hdlp->ih_pri;
				++pcie_pci_intr_pri_counter;
			}

		} else if (hdlp->ih_type == DDI_INTR_TYPE_FIXED) {
			/* Figure out if this device supports MASKING */
			pci_rval = pci_intx_get_cap(rdip, &pci_status);
			if (pci_rval == DDI_SUCCESS && pci_status)
				hdlp->ih_cap |= pci_status;
			*(int *)result = 1;	/* DDI_INTR_TYPE_FIXED */
		} else
			return (DDI_FAILURE);
		break;
	case DDI_INTROP_FREE:
		if (DDI_INTR_IS_MSI_OR_MSIX(hdlp->ih_type) &&
		    (psm_intr_ops != NULL)) {
			if (i_ddi_intr_get_current_nintrs(hdlp->ih_dip) - 1 ==
			    0) {
				if (handle = i_ddi_get_pci_config_handle(
				    rdip)) {
					(void) pci_config_teardown(&handle);
					i_ddi_set_pci_config_handle(rdip, NULL);
				}
				if (cap_ptr = i_ddi_get_msi_msix_cap_ptr(rdip))
					i_ddi_set_msi_msix_cap_ptr(rdip, 0);
			}

			(void) (*psm_intr_ops)(rdip, hdlp,
			    PSM_INTR_OP_FREE_VECTORS, NULL);

			if (hdlp->ih_type == DDI_INTR_TYPE_MSIX) {
				msix_p = i_ddi_get_msix(hdlp->ih_dip);
				if (msix_p &&
				    (i_ddi_intr_get_current_nintrs(
					hdlp->ih_dip) - 1) == 0) {
					pci_msix_fini(msix_p);
					i_ddi_set_msix(hdlp->ih_dip, NULL);
				}
			}
		}
		break;
	case DDI_INTROP_GETPRI:
		/* Get the priority */
		if (pci_get_priority(rdip, hdlp, &priority) != DDI_SUCCESS)
			return (DDI_FAILURE);
		DDI_INTR_NEXDBG((CE_CONT, "pci_common_intr_ops: "
		    "priority = 0x%x\n", priority));
		*(int *)result = priority;
		break;
	case DDI_INTROP_SETPRI:
		/* Validate the interrupt priority passed */
		if (*(int *)result > LOCK_LEVEL)
			return (DDI_FAILURE);

		/* Ensure that PSM is all initialized */
		if (psm_intr_ops == NULL)
			return (DDI_FAILURE);

		/* Change the priority */
		if ((*psm_intr_ops)(rdip, hdlp, PSM_INTR_OP_SET_PRI, result) ==
		    PSM_FAILURE)
			return (DDI_FAILURE);

		/* update ispec */
		isp = pci_intx_get_ispec(pdip, rdip, (int)hdlp->ih_inum);
		ispec = (struct intrspec *)isp;
		if (ispec)
			ispec->intrspec_pri = *(int *)result;
		break;
	case DDI_INTROP_ADDISR:
		/* update ispec */
		isp = pci_intx_get_ispec(pdip, rdip, (int)hdlp->ih_inum);
		ispec = (struct intrspec *)isp;
		if (ispec) {
			ispec->intrspec_func = hdlp->ih_cb_func;
			ihdl_plat_datap = (ihdl_plat_t *)hdlp->ih_private;
			pci_kstat_create(&ihdl_plat_datap->ip_ksp, pdip, hdlp);
		}
		break;
	case DDI_INTROP_REMISR:
		/* Get the interrupt structure pointer */
		isp = pci_intx_get_ispec(pdip, rdip, (int)hdlp->ih_inum);
		ispec = (struct intrspec *)isp;
		if (ispec) {
			ispec->intrspec_func = (uint_t (*)()) 0;
			ihdl_plat_datap = (ihdl_plat_t *)hdlp->ih_private;
			if (ihdl_plat_datap->ip_ksp != NULL)
				pci_kstat_delete(ihdl_plat_datap->ip_ksp);
		}
		break;
	case DDI_INTROP_GETCAP:
		/*
		 * First check the config space and/or
		 * MSI capability register(s)
		 */
		if (DDI_INTR_IS_MSI_OR_MSIX(hdlp->ih_type))
			pci_rval = pci_msi_get_cap(rdip, hdlp->ih_type,
			    &pci_status);
		else if (hdlp->ih_type == DDI_INTR_TYPE_FIXED)
			pci_rval = pci_intx_get_cap(rdip, &pci_status);

		/* next check with pcplusmp */
		if (psm_intr_ops != NULL)
			psm_rval = (*psm_intr_ops)(rdip, hdlp,
			    PSM_INTR_OP_GET_CAP, &psm_status);

		DDI_INTR_NEXDBG((CE_CONT, "pci: GETCAP returned psm_rval = %x, "
		    "psm_status = %x, pci_rval = %x, pci_status = %x\n",
		    psm_rval, psm_status, pci_rval, pci_status));

		if (psm_rval == PSM_FAILURE && pci_rval == DDI_FAILURE) {
			*(int *)result = 0;
			return (DDI_FAILURE);
		}

		if (psm_rval == PSM_SUCCESS)
			*(int *)result = psm_status;

		if (pci_rval == DDI_SUCCESS)
			*(int *)result |= pci_status;

		DDI_INTR_NEXDBG((CE_CONT, "pci: GETCAP returned = %x\n",
		    *(int *)result));
		break;
	case DDI_INTROP_SETCAP:
		DDI_INTR_NEXDBG((CE_CONT, "pci_common_intr_ops: "
		    "SETCAP cap=0x%x\n", *(int *)result));
		if (psm_intr_ops == NULL)
			return (DDI_FAILURE);

		if ((*psm_intr_ops)(rdip, hdlp, PSM_INTR_OP_SET_CAP, result)) {
			DDI_INTR_NEXDBG((CE_CONT, "GETCAP: psm_intr_ops"
			    " returned failure\n"));
			return (DDI_FAILURE);
		}
		break;
	case DDI_INTROP_ENABLE:
		DDI_INTR_NEXDBG((CE_CONT, "pci_common_intr_ops: ENABLE\n"));
		if (psm_intr_ops == NULL)
			return (DDI_FAILURE);

		if (pci_enable_intr(pdip, rdip, hdlp, hdlp->ih_inum) !=
		    DDI_SUCCESS)
			return (DDI_FAILURE);

		DDI_INTR_NEXDBG((CE_CONT, "pci_common_intr_ops: ENABLE "
		    "vector=0x%x\n", hdlp->ih_vector));
		break;
	case DDI_INTROP_DISABLE:
		DDI_INTR_NEXDBG((CE_CONT, "pci_common_intr_ops: DISABLE\n"));
		if (psm_intr_ops == NULL)
			return (DDI_FAILURE);

		pci_disable_intr(pdip, rdip, hdlp, hdlp->ih_inum);
		DDI_INTR_NEXDBG((CE_CONT, "pci_common_intr_ops: DISABLE "
		    "vector = %x\n", hdlp->ih_vector));
		break;
	case DDI_INTROP_BLOCKENABLE:
		DDI_INTR_NEXDBG((CE_CONT, "pci_common_intr_ops: "
		    "BLOCKENABLE\n"));
		if (hdlp->ih_type != DDI_INTR_TYPE_MSI) {
			DDI_INTR_NEXDBG((CE_CONT, "BLOCKENABLE: not MSI\n"));
			return (DDI_FAILURE);
		}

		/* Check if psm_intr_ops is NULL? */
		if (psm_intr_ops == NULL)
			return (DDI_FAILURE);

		count = hdlp->ih_scratch1;
		h_array = (ddi_intr_handle_t *)hdlp->ih_scratch2;
		for (i = 0; i < count; i++) {
			hdlp = (ddi_intr_handle_impl_t *)h_array[i];
			if (pci_enable_intr(pdip, rdip, hdlp,
			    hdlp->ih_inum) != DDI_SUCCESS) {
				DDI_INTR_NEXDBG((CE_CONT, "BLOCKENABLE: "
				    "pci_enable_intr failed for %d\n", i));
				for (j = 0; j < i; j++) {
				    hdlp = (ddi_intr_handle_impl_t *)h_array[j];
				    pci_disable_intr(pdip, rdip, hdlp,
					    hdlp->ih_inum);
				}
				return (DDI_FAILURE);
			}
			DDI_INTR_NEXDBG((CE_CONT, "pci_common_intr_ops: "
			    "BLOCKENABLE inum %x done\n", hdlp->ih_inum));
		}
		break;
	case DDI_INTROP_BLOCKDISABLE:
		DDI_INTR_NEXDBG((CE_CONT, "pci_common_intr_ops: "
		    "BLOCKDISABLE\n"));
		if (hdlp->ih_type != DDI_INTR_TYPE_MSI) {
			DDI_INTR_NEXDBG((CE_CONT, "BLOCKDISABLE: not MSI\n"));
			return (DDI_FAILURE);
		}

		/* Check if psm_intr_ops is present */
		if (psm_intr_ops == NULL)
			return (DDI_FAILURE);

		count = hdlp->ih_scratch1;
		h_array = (ddi_intr_handle_t *)hdlp->ih_scratch2;
		for (i = 0; i < count; i++) {
			hdlp = (ddi_intr_handle_impl_t *)h_array[i];
			pci_disable_intr(pdip, rdip, hdlp, hdlp->ih_inum);
			DDI_INTR_NEXDBG((CE_CONT, "pci_common_intr_ops: "
			    "BLOCKDISABLE inum %x done\n", hdlp->ih_inum));
		}
		break;
	case DDI_INTROP_SETMASK:
	case DDI_INTROP_CLRMASK:
		/*
		 * First handle in the config space
		 */
		if (intr_op == DDI_INTROP_SETMASK) {
			if (DDI_INTR_IS_MSI_OR_MSIX(hdlp->ih_type))
				pci_status = pci_msi_set_mask(rdip,
				    hdlp->ih_type, hdlp->ih_inum);
			else if (hdlp->ih_type == DDI_INTR_TYPE_FIXED)
				pci_status = pci_intx_set_mask(rdip);
		} else {
			if (DDI_INTR_IS_MSI_OR_MSIX(hdlp->ih_type))
				pci_status = pci_msi_clr_mask(rdip,
				    hdlp->ih_type, hdlp->ih_inum);
			else if (hdlp->ih_type == DDI_INTR_TYPE_FIXED)
				pci_status = pci_intx_clr_mask(rdip);
		}

		/* For MSI/X; no need to check with pcplusmp */
		if (hdlp->ih_type != DDI_INTR_TYPE_FIXED)
			return (pci_status);

		/* For fixed interrupts only: handle config space first */
		if (hdlp->ih_type == DDI_INTR_TYPE_FIXED &&
		    pci_status == DDI_SUCCESS)
			break;

		/* For fixed interrupts only: confer with pcplusmp next */
		if (psm_intr_ops != NULL) {
			/* If interrupt is shared; do nothing */
			psm_rval = (*psm_intr_ops)(rdip, hdlp,
			    PSM_INTR_OP_GET_SHARED, &psm_status);

			if (psm_rval == PSM_FAILURE || psm_status == 1)
				return (pci_status);

			/* Now, pcplusmp should try to set/clear the mask */
			if (intr_op == DDI_INTROP_SETMASK)
				psm_rval = (*psm_intr_ops)(rdip, hdlp,
				    PSM_INTR_OP_SET_MASK, NULL);
			else
				psm_rval = (*psm_intr_ops)(rdip, hdlp,
				    PSM_INTR_OP_CLEAR_MASK, NULL);
		}
		return ((psm_rval == PSM_FAILURE) ? DDI_FAILURE : DDI_SUCCESS);
	case DDI_INTROP_GETPENDING:
		/*
		 * First check the config space and/or
		 * MSI capability register(s)
		 */
		if (DDI_INTR_IS_MSI_OR_MSIX(hdlp->ih_type))
			pci_rval = pci_msi_get_pending(rdip, hdlp->ih_type,
			    hdlp->ih_inum, &pci_status);
		else if (hdlp->ih_type == DDI_INTR_TYPE_FIXED)
			pci_rval = pci_intx_get_pending(rdip, &pci_status);

		/* On failure; next try with pcplusmp */
		if (pci_rval != DDI_SUCCESS && psm_intr_ops != NULL)
			psm_rval = (*psm_intr_ops)(rdip, hdlp,
			    PSM_INTR_OP_GET_PENDING, &psm_status);

		DDI_INTR_NEXDBG((CE_CONT, "pci: GETPENDING returned "
		    "psm_rval = %x, psm_status = %x, pci_rval = %x, "
		    "pci_status = %x\n", psm_rval, psm_status, pci_rval,
		    pci_status));
		if (psm_rval == PSM_FAILURE && pci_rval == DDI_FAILURE) {
			*(int *)result = 0;
			return (DDI_FAILURE);
		}

		if (psm_rval != PSM_FAILURE)
			*(int *)result = psm_status;
		else if (pci_rval != DDI_FAILURE)
			*(int *)result = pci_status;
		DDI_INTR_NEXDBG((CE_CONT, "pci: GETPENDING returned = %x\n",
		    *(int *)result));
		break;
	case DDI_INTROP_NAVAIL:
		if ((psm_intr_ops != NULL) && (pci_get_priority(rdip,
		    hdlp, &priority) == DDI_SUCCESS)) {
			/* Priority in the handle not initialized yet */
			hdlp->ih_pri = priority;
			(void) (*psm_intr_ops)(rdip, hdlp,
			    PSM_INTR_OP_NAVAIL_VECTORS, result);
		} else {
			*(int *)result = 1;
		}
		DDI_INTR_NEXDBG((CE_CONT, "pci: NAVAIL returned = %x\n",
		    *(int *)result));
		break;
	default:
		return (i_ddi_intr_ops(pdip, rdip, intr_op, hdlp, result));
	}

	return (DDI_SUCCESS);
}

int
pci_get_intr_from_vecirq(apic_get_intr_t *intrinfo_p,
    int vecirq, boolean_t is_irq)
{
	ddi_intr_handle_impl_t	get_info_ii_hdl;

	if (is_irq)
		intrinfo_p->avgi_req_flags |= PSMGI_INTRBY_IRQ;

	/*
	 * For this locally-declared and used handle, ih_private will contain a
	 * pointer to apic_get_intr_t, not an ihdl_plat_t as used for
	 * global interrupt handling.
	 */
	get_info_ii_hdl.ih_private = intrinfo_p;
	get_info_ii_hdl.ih_vector = (ushort_t)vecirq;

	if ((*psm_intr_ops)(NULL, &get_info_ii_hdl,
	    PSM_INTR_OP_GET_INTR, NULL) == PSM_FAILURE)
		return (DDI_FAILURE);

	return (DDI_SUCCESS);
}


int
pci_get_cpu_from_vecirq(int vecirq, boolean_t is_irq)
{
	int rval;

	apic_get_intr_t	intrinfo;
	intrinfo.avgi_req_flags = PSMGI_REQ_CPUID;
	rval = pci_get_intr_from_vecirq(&intrinfo, vecirq, is_irq);

	if (rval == DDI_SUCCESS)
		return (intrinfo.avgi_cpu_id);
	else
		return (-1);
}


static int
pci_enable_intr(dev_info_t *pdip, dev_info_t *rdip,
    ddi_intr_handle_impl_t *hdlp, uint32_t inum)
{
	struct intrspec	*ispec;
	int		irq;
	ihdl_plat_t	*ihdl_plat_datap = (ihdl_plat_t *)hdlp->ih_private;

	DDI_INTR_NEXDBG((CE_CONT, "pci_enable_intr: hdlp %p inum %x\n",
	    (void *)hdlp, inum));

	/* Translate the interrupt if needed */
	ispec = (struct intrspec *)pci_intx_get_ispec(pdip, rdip, (int)inum);
	if (ispec == NULL)
		return (DDI_FAILURE);
	if (DDI_INTR_IS_MSI_OR_MSIX(hdlp->ih_type))
		ispec->intrspec_vec = inum;
	ihdl_plat_datap->ip_ispecp = ispec;

	/* translate the interrupt if needed */
	(void) (*psm_intr_ops)(rdip, hdlp, PSM_INTR_OP_XLATE_VECTOR, &irq);
	DDI_INTR_NEXDBG((CE_CONT, "pci_enable_intr: priority=%x irq=%x\n",
	    hdlp->ih_pri, irq));

	/* Add the interrupt handler */
	if (!add_avintr((void *)hdlp, hdlp->ih_pri, hdlp->ih_cb_func,
	    DEVI(rdip)->devi_name, irq, hdlp->ih_cb_arg1,
	    hdlp->ih_cb_arg2, &ihdl_plat_datap->ip_ticks, rdip))
		return (DDI_FAILURE);

	/* Note this really is an irq. */
	hdlp->ih_vector = (ushort_t)irq;

	return (DDI_SUCCESS);
}


static void
pci_disable_intr(dev_info_t *pdip, dev_info_t *rdip,
    ddi_intr_handle_impl_t *hdlp, uint32_t inum)
{
	int		irq;
	struct intrspec	*ispec;
	ihdl_plat_t	*ihdl_plat_datap = (ihdl_plat_t *)hdlp->ih_private;

	DDI_INTR_NEXDBG((CE_CONT, "pci_disable_intr: \n"));
	ispec = (struct intrspec *)pci_intx_get_ispec(pdip, rdip, (int)inum);
	if (ispec == NULL)
		return;
	if (DDI_INTR_IS_MSI_OR_MSIX(hdlp->ih_type))
		ispec->intrspec_vec = inum;
	ihdl_plat_datap->ip_ispecp = ispec;

	/* translate the interrupt if needed */
	(void) (*psm_intr_ops)(rdip, hdlp, PSM_INTR_OP_XLATE_VECTOR, &irq);

	/* Disable the interrupt handler */
	rem_avintr((void *)hdlp, hdlp->ih_pri, hdlp->ih_cb_func, irq);
	ihdl_plat_datap->ip_ispecp = NULL;
}

/*
 * Miscellaneous library function
 */
int
pci_common_get_reg_prop(dev_info_t *dip, pci_regspec_t *pci_rp)
{
	int		i;
	int 		number;
	int		assigned_addr_len;
	uint_t		phys_hi = pci_rp->pci_phys_hi;
	pci_regspec_t	*assigned_addr;

	if (((phys_hi & PCI_REG_ADDR_M) == PCI_ADDR_CONFIG) ||
	    (phys_hi & PCI_RELOCAT_B))
		return (DDI_SUCCESS);

	/*
	 * the "reg" property specifies relocatable, get and interpret the
	 * "assigned-addresses" property.
	 */
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "assigned-addresses", (int **)&assigned_addr,
	    (uint_t *)&assigned_addr_len) != DDI_PROP_SUCCESS)
		return (DDI_FAILURE);

	/*
	 * Scan the "assigned-addresses" for one that matches the specified
	 * "reg" property entry.
	 */
	phys_hi &= PCI_CONF_ADDR_MASK;
	number = assigned_addr_len / (sizeof (pci_regspec_t) / sizeof (int));
	for (i = 0; i < number; i++) {
		if ((assigned_addr[i].pci_phys_hi & PCI_CONF_ADDR_MASK) ==
		    phys_hi) {
			pci_rp->pci_phys_mid = assigned_addr[i].pci_phys_mid;
			pci_rp->pci_phys_low = assigned_addr[i].pci_phys_low;
			ddi_prop_free(assigned_addr);
			return (DDI_SUCCESS);
		}
	}

	ddi_prop_free(assigned_addr);
	return (DDI_FAILURE);
}


/*
 * For pci_tools
 */

int
pci_common_ioctl(dev_info_t *dip, dev_t dev, int cmd, intptr_t arg,
    int mode, cred_t *credp, int *rvalp)
{
	int rv = ENOTTY;

	minor_t minor = getminor(dev);

	switch (PCIHP_AP_MINOR_NUM_TO_PCI_DEVNUM(minor)) {
	case PCI_TOOL_REG_MINOR_NUM:

		switch (cmd) {
		case PCITOOL_DEVICE_SET_REG:
		case PCITOOL_DEVICE_GET_REG:

			/* Require full privileges. */
			if (secpolicy_kmdb(credp))
				rv = EPERM;
			else
				rv = pcitool_dev_reg_ops(dip, (void *)arg,
				    cmd, mode);
			break;

		case PCITOOL_NEXUS_SET_REG:
		case PCITOOL_NEXUS_GET_REG:

			/* Require full privileges. */
			if (secpolicy_kmdb(credp))
				rv = EPERM;
			else
				rv = pcitool_bus_reg_ops(dip, (void *)arg,
				    cmd, mode);
			break;
		}
		break;

	case PCI_TOOL_INTR_MINOR_NUM:

		switch (cmd) {
		case PCITOOL_DEVICE_SET_INTR:

			/* Require PRIV_SYS_RES_CONFIG, same as psradm */
			if (secpolicy_ponline(credp)) {
				rv = EPERM;
				break;
			}

		/*FALLTHRU*/
		/* These require no special privileges. */
		case PCITOOL_DEVICE_GET_INTR:
		case PCITOOL_DEVICE_NUM_INTR:
			rv = pcitool_intr_admn(dip, (void *)arg, cmd, mode);
			break;
		}
		break;

	/*
	 * All non-PCItool ioctls go through here, including:
	 *   devctl ioctls with minor number PCIHP_DEVCTL_MINOR and
	 *   those for attachment points with where minor number is the
	 *   device number.
	 */
	default:
		rv = (pcihp_get_cb_ops())->cb_ioctl(dev, cmd, arg, mode,
		    credp, rvalp);
		break;
	}

	return (rv);
}


int
pci_common_ctlops_poke(peekpoke_ctlops_t *in_args)
{
	size_t size = in_args->size;
	uintptr_t dev_addr = in_args->dev_addr;
	uintptr_t host_addr = in_args->host_addr;
	ddi_acc_impl_t *hp = (ddi_acc_impl_t *)in_args->handle;
	ddi_acc_hdl_t *hdlp = (ddi_acc_hdl_t *)in_args->handle;
	size_t repcount = in_args->repcount;
	uint_t flags = in_args->flags;
	int err = DDI_SUCCESS;

	/*
	 * if no handle then this is a poke. We have to return failure here
	 * as we have no way of knowing whether this is a MEM or IO space access
	 */
	if (in_args->handle == NULL)
		return (DDI_FAILURE);

	/*
	 * rest of this function is actually for cautious puts
	 */
	for (; repcount; repcount--) {
		if (hp->ahi_acc_attr == DDI_ACCATTR_CONFIG_SPACE) {
			switch (size) {
			case sizeof (uint8_t):
				pci_config_wr8(hp, (uint8_t *)dev_addr,
				    *(uint8_t *)host_addr);
				break;
			case sizeof (uint16_t):
				pci_config_wr16(hp, (uint16_t *)dev_addr,
				    *(uint16_t *)host_addr);
				break;
			case sizeof (uint32_t):
				pci_config_wr32(hp, (uint32_t *)dev_addr,
				    *(uint32_t *)host_addr);
				break;
			case sizeof (uint64_t):
				pci_config_wr64(hp, (uint64_t *)dev_addr,
				    *(uint64_t *)host_addr);
				break;
			default:
				err = DDI_FAILURE;
				break;
			}
		} else if (hp->ahi_acc_attr & DDI_ACCATTR_IO_SPACE) {
			if (hdlp->ah_acc.devacc_attr_endian_flags ==
			    DDI_STRUCTURE_BE_ACC) {
				switch (size) {
				case sizeof (uint8_t):
					i_ddi_io_put8(hp,
					    (uint8_t *)dev_addr,
					    *(uint8_t *)host_addr);
					break;
				case sizeof (uint16_t):
					i_ddi_io_swap_put16(hp,
					    (uint16_t *)dev_addr,
					    *(uint16_t *)host_addr);
					break;
				case sizeof (uint32_t):
					i_ddi_io_swap_put32(hp,
					    (uint32_t *)dev_addr,
					    *(uint32_t *)host_addr);
					break;
				/*
				 * note the 64-bit case is a dummy
				 * function - so no need to swap
				 */
				case sizeof (uint64_t):
					i_ddi_io_put64(hp,
					    (uint64_t *)dev_addr,
					    *(uint64_t *)host_addr);
					break;
				default:
					err = DDI_FAILURE;
					break;
				}
			} else {
				switch (size) {
				case sizeof (uint8_t):
					i_ddi_io_put8(hp,
					    (uint8_t *)dev_addr,
					    *(uint8_t *)host_addr);
					break;
				case sizeof (uint16_t):
					i_ddi_io_put16(hp,
					    (uint16_t *)dev_addr,
					    *(uint16_t *)host_addr);
					break;
				case sizeof (uint32_t):
					i_ddi_io_put32(hp,
					    (uint32_t *)dev_addr,
					    *(uint32_t *)host_addr);
					break;
				case sizeof (uint64_t):
					i_ddi_io_put64(hp,
					    (uint64_t *)dev_addr,
					    *(uint64_t *)host_addr);
					break;
				default:
					err = DDI_FAILURE;
					break;
				}
			}
		} else {
			if (hdlp->ah_acc.devacc_attr_endian_flags ==
			    DDI_STRUCTURE_BE_ACC) {
				switch (size) {
				case sizeof (uint8_t):
					*(uint8_t *)dev_addr =
					    *(uint8_t *)host_addr;
					break;
				case sizeof (uint16_t):
					*(uint16_t *)dev_addr =
					    ddi_swap16(*(uint16_t *)host_addr);
					break;
				case sizeof (uint32_t):
					*(uint32_t *)dev_addr =
					    ddi_swap32(*(uint32_t *)host_addr);
					break;
				case sizeof (uint64_t):
					*(uint64_t *)dev_addr =
					    ddi_swap64(*(uint64_t *)host_addr);
					break;
				default:
					err = DDI_FAILURE;
					break;
				}
			} else {
				switch (size) {
				case sizeof (uint8_t):
					*(uint8_t *)dev_addr =
					    *(uint8_t *)host_addr;
					break;
				case sizeof (uint16_t):
					*(uint16_t *)dev_addr =
					    *(uint16_t *)host_addr;
					break;
				case sizeof (uint32_t):
					*(uint32_t *)dev_addr =
					    *(uint32_t *)host_addr;
					break;
				case sizeof (uint64_t):
					*(uint64_t *)dev_addr =
					    *(uint64_t *)host_addr;
					break;
				default:
					err = DDI_FAILURE;
					break;
				}
			}
		}
		host_addr += size;
		if (flags == DDI_DEV_AUTOINCR)
			dev_addr += size;
	}
	return (err);
}


int
pci_fm_acc_setup(ddi_acc_hdl_t *hp, off_t offset, off_t len)
{
	ddi_acc_impl_t	*ap = (ddi_acc_impl_t *)hp->ah_platform_private;

	/* endian-ness check */
	if (hp->ah_acc.devacc_attr_endian_flags == DDI_STRUCTURE_BE_ACC)
		return (DDI_FAILURE);

	/*
	 * range check
	 */
	if ((offset >= PCI_CONF_HDR_SIZE) ||
	    (len > PCI_CONF_HDR_SIZE) ||
	    (offset + len > PCI_CONF_HDR_SIZE))
		return (DDI_FAILURE);

	ap->ahi_acc_attr |= DDI_ACCATTR_CONFIG_SPACE;
	/*
	 * always use cautious mechanism for config space gets
	 */
	ap->ahi_get8 = i_ddi_caut_get8;
	ap->ahi_get16 = i_ddi_caut_get16;
	ap->ahi_get32 = i_ddi_caut_get32;
	ap->ahi_get64 = i_ddi_caut_get64;
	ap->ahi_rep_get8 = i_ddi_caut_rep_get8;
	ap->ahi_rep_get16 = i_ddi_caut_rep_get16;
	ap->ahi_rep_get32 = i_ddi_caut_rep_get32;
	ap->ahi_rep_get64 = i_ddi_caut_rep_get64;
	if (hp->ah_acc.devacc_attr_access == DDI_CAUTIOUS_ACC) {
		ap->ahi_put8 = i_ddi_caut_put8;
		ap->ahi_put16 = i_ddi_caut_put16;
		ap->ahi_put32 = i_ddi_caut_put32;
		ap->ahi_put64 = i_ddi_caut_put64;
		ap->ahi_rep_put8 = i_ddi_caut_rep_put8;
		ap->ahi_rep_put16 = i_ddi_caut_rep_put16;
		ap->ahi_rep_put32 = i_ddi_caut_rep_put32;
		ap->ahi_rep_put64 = i_ddi_caut_rep_put64;
	} else {
		ap->ahi_put8 = pci_config_wr8;
		ap->ahi_put16 = pci_config_wr16;
		ap->ahi_put32 = pci_config_wr32;
		ap->ahi_put64 = pci_config_wr64;
		ap->ahi_rep_put8 = pci_config_rep_wr8;
		ap->ahi_rep_put16 = pci_config_rep_wr16;
		ap->ahi_rep_put32 = pci_config_rep_wr32;
		ap->ahi_rep_put64 = pci_config_rep_wr64;
	}

	/* Initialize to default check/notify functions */
	ap->ahi_fault_check = i_ddi_acc_fault_check;
	ap->ahi_fault_notify = i_ddi_acc_fault_notify;
	ap->ahi_fault = 0;
	impl_acc_err_init(hp);
	return (DDI_SUCCESS);
}


int
pci_common_ctlops_peek(peekpoke_ctlops_t *in_args)
{
	size_t size = in_args->size;
	uintptr_t dev_addr = in_args->dev_addr;
	uintptr_t host_addr = in_args->host_addr;
	ddi_acc_impl_t *hp = (ddi_acc_impl_t *)in_args->handle;
	ddi_acc_hdl_t *hdlp = (ddi_acc_hdl_t *)in_args->handle;
	size_t repcount = in_args->repcount;
	uint_t flags = in_args->flags;
	int err = DDI_SUCCESS;

	/*
	 * if no handle then this is a peek. We have to return failure here
	 * as we have no way of knowing whether this is a MEM or IO space access
	 */
	if (in_args->handle == NULL)
		return (DDI_FAILURE);

	for (; repcount; repcount--) {
		if (hp->ahi_acc_attr == DDI_ACCATTR_CONFIG_SPACE) {
			switch (size) {
			case sizeof (uint8_t):
				*(uint8_t *)host_addr = pci_config_rd8(hp,
				    (uint8_t *)dev_addr);
				break;
			case sizeof (uint16_t):
				*(uint16_t *)host_addr = pci_config_rd16(hp,
				    (uint16_t *)dev_addr);
				break;
			case sizeof (uint32_t):
				*(uint32_t *)host_addr = pci_config_rd32(hp,
				    (uint32_t *)dev_addr);
				break;
			case sizeof (uint64_t):
				*(uint64_t *)host_addr = pci_config_rd64(hp,
				    (uint64_t *)dev_addr);
				break;
			default:
				err = DDI_FAILURE;
				break;
			}
		} else if (hp->ahi_acc_attr & DDI_ACCATTR_IO_SPACE) {
			if (hdlp->ah_acc.devacc_attr_endian_flags ==
			    DDI_STRUCTURE_BE_ACC) {
				switch (size) {
				case sizeof (uint8_t):
					*(uint8_t *)host_addr =
					    i_ddi_io_get8(hp,
					    (uint8_t *)dev_addr);
					break;
				case sizeof (uint16_t):
					*(uint16_t *)host_addr =
					    i_ddi_io_swap_get16(hp,
					    (uint16_t *)dev_addr);
					break;
				case sizeof (uint32_t):
					*(uint32_t *)host_addr =
					    i_ddi_io_swap_get32(hp,
					    (uint32_t *)dev_addr);
					break;
				/*
				 * note the 64-bit case is a dummy
				 * function - so no need to swap
				 */
				case sizeof (uint64_t):
					*(uint64_t *)host_addr =
					    i_ddi_io_get64(hp,
					    (uint64_t *)dev_addr);
					break;
				default:
					err = DDI_FAILURE;
					break;
				}
			} else {
				switch (size) {
				case sizeof (uint8_t):
					*(uint8_t *)host_addr =
					    i_ddi_io_get8(hp,
					    (uint8_t *)dev_addr);
					break;
				case sizeof (uint16_t):
					*(uint16_t *)host_addr =
					    i_ddi_io_get16(hp,
					    (uint16_t *)dev_addr);
					break;
				case sizeof (uint32_t):
					*(uint32_t *)host_addr =
					    i_ddi_io_get32(hp,
					    (uint32_t *)dev_addr);
					break;
				case sizeof (uint64_t):
					*(uint64_t *)host_addr =
					    i_ddi_io_get64(hp,
					    (uint64_t *)dev_addr);
					break;
				default:
					err = DDI_FAILURE;
					break;
				}
			}
		} else {
			if (hdlp->ah_acc.devacc_attr_endian_flags ==
			    DDI_STRUCTURE_BE_ACC) {
				switch (in_args->size) {
				case sizeof (uint8_t):
					*(uint8_t *)host_addr =
					    *(uint8_t *)dev_addr;
					break;
				case sizeof (uint16_t):
					*(uint16_t *)host_addr =
					    ddi_swap16(*(uint16_t *)dev_addr);
					break;
				case sizeof (uint32_t):
					*(uint32_t *)host_addr =
					    ddi_swap32(*(uint32_t *)dev_addr);
					break;
				case sizeof (uint64_t):
					*(uint64_t *)host_addr =
					    ddi_swap64(*(uint64_t *)dev_addr);
					break;
				default:
					err = DDI_FAILURE;
					break;
				}
			} else {
				switch (in_args->size) {
				case sizeof (uint8_t):
					*(uint8_t *)host_addr =
					    *(uint8_t *)dev_addr;
					break;
				case sizeof (uint16_t):
					*(uint16_t *)host_addr =
					    *(uint16_t *)dev_addr;
					break;
				case sizeof (uint32_t):
					*(uint32_t *)host_addr =
					    *(uint32_t *)dev_addr;
					break;
				case sizeof (uint64_t):
					*(uint64_t *)host_addr =
					    *(uint64_t *)dev_addr;
					break;
				default:
					err = DDI_FAILURE;
					break;
				}
			}
		}
		host_addr += size;
		if (flags == DDI_DEV_AUTOINCR)
			dev_addr += size;
	}
	return (err);
}

/*ARGSUSED*/
int
pci_common_peekpoke(dev_info_t *dip, dev_info_t *rdip,
	ddi_ctl_enum_t ctlop, void *arg, void *result)
{
	if (ctlop == DDI_CTLOPS_PEEK)
		return (pci_common_ctlops_peek((peekpoke_ctlops_t *)arg));
	else
		return (pci_common_ctlops_poke((peekpoke_ctlops_t *)arg));
}

/*
 * These are the get and put functions to be shared with drivers. The
 * mutex locking is done inside the functions referenced, rather than
 * here, and is thus shared across PCI child drivers and any other
 * consumers of PCI config space (such as the ACPI subsystem).
 *
 * The configuration space addresses come in as pointers.  This is fine on
 * a 32-bit system, where the VM space and configuration space are the same
 * size.  It's not such a good idea on a 64-bit system, where memory
 * addresses are twice as large as configuration space addresses.  At some
 * point in the call tree we need to take a stand and say "you are 32-bit
 * from this time forth", and this seems like a nice self-contained place.
 */

uint8_t
pci_config_rd8(ddi_acc_impl_t *hdlp, uint8_t *addr)
{
	pci_acc_cfblk_t *cfp;
	uint8_t	rval;
	int reg;

	ASSERT64(((uintptr_t)addr >> 32) == 0);

	reg = (int)(uintptr_t)addr;

	cfp = (pci_acc_cfblk_t *)&hdlp->ahi_common.ah_bus_private;

	rval = (*pci_getb_func)(cfp->c_busnum, cfp->c_devnum, cfp->c_funcnum,
	    reg);

	return (rval);
}

void
pci_config_rep_rd8(ddi_acc_impl_t *hdlp, uint8_t *host_addr,
	uint8_t *dev_addr, size_t repcount, uint_t flags)
{
	uint8_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_AUTOINCR)
		for (; repcount; repcount--)
			*h++ = pci_config_rd8(hdlp, d++);
	else
		for (; repcount; repcount--)
			*h++ = pci_config_rd8(hdlp, d);
}

uint16_t
pci_config_rd16(ddi_acc_impl_t *hdlp, uint16_t *addr)
{
	pci_acc_cfblk_t *cfp;
	uint16_t rval;
	int reg;

	ASSERT64(((uintptr_t)addr >> 32) == 0);

	reg = (int)(uintptr_t)addr;

	cfp = (pci_acc_cfblk_t *)&hdlp->ahi_common.ah_bus_private;

	rval = (*pci_getw_func)(cfp->c_busnum, cfp->c_devnum, cfp->c_funcnum,
	    reg);

	return (rval);
}

void
pci_config_rep_rd16(ddi_acc_impl_t *hdlp, uint16_t *host_addr,
	uint16_t *dev_addr, size_t repcount, uint_t flags)
{
	uint16_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_AUTOINCR)
		for (; repcount; repcount--)
			*h++ = pci_config_rd16(hdlp, d++);
	else
		for (; repcount; repcount--)
			*h++ = pci_config_rd16(hdlp, d);
}

uint32_t
pci_config_rd32(ddi_acc_impl_t *hdlp, uint32_t *addr)
{
	pci_acc_cfblk_t *cfp;
	uint32_t rval;
	int reg;

	ASSERT64(((uintptr_t)addr >> 32) == 0);

	reg = (int)(uintptr_t)addr;

	cfp = (pci_acc_cfblk_t *)&hdlp->ahi_common.ah_bus_private;

	rval = (*pci_getl_func)(cfp->c_busnum, cfp->c_devnum,
	    cfp->c_funcnum, reg);

	return (rval);
}

void
pci_config_rep_rd32(ddi_acc_impl_t *hdlp, uint32_t *host_addr,
	uint32_t *dev_addr, size_t repcount, uint_t flags)
{
	uint32_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_AUTOINCR)
		for (; repcount; repcount--)
			*h++ = pci_config_rd32(hdlp, d++);
	else
		for (; repcount; repcount--)
			*h++ = pci_config_rd32(hdlp, d);
}


void
pci_config_wr8(ddi_acc_impl_t *hdlp, uint8_t *addr, uint8_t value)
{
	pci_acc_cfblk_t *cfp;
	int reg;

	ASSERT64(((uintptr_t)addr >> 32) == 0);

	reg = (int)(uintptr_t)addr;

	cfp = (pci_acc_cfblk_t *)&hdlp->ahi_common.ah_bus_private;

	(*pci_putb_func)(cfp->c_busnum, cfp->c_devnum,
	    cfp->c_funcnum, reg, value);
}

void
pci_config_rep_wr8(ddi_acc_impl_t *hdlp, uint8_t *host_addr,
	uint8_t *dev_addr, size_t repcount, uint_t flags)
{
	uint8_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_AUTOINCR)
		for (; repcount; repcount--)
			pci_config_wr8(hdlp, d++, *h++);
	else
		for (; repcount; repcount--)
			pci_config_wr8(hdlp, d, *h++);
}

void
pci_config_wr16(ddi_acc_impl_t *hdlp, uint16_t *addr, uint16_t value)
{
	pci_acc_cfblk_t *cfp;
	int reg;

	ASSERT64(((uintptr_t)addr >> 32) == 0);

	reg = (int)(uintptr_t)addr;

	cfp = (pci_acc_cfblk_t *)&hdlp->ahi_common.ah_bus_private;

	(*pci_putw_func)(cfp->c_busnum, cfp->c_devnum,
	    cfp->c_funcnum, reg, value);
}

void
pci_config_rep_wr16(ddi_acc_impl_t *hdlp, uint16_t *host_addr,
	uint16_t *dev_addr, size_t repcount, uint_t flags)
{
	uint16_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_AUTOINCR)
		for (; repcount; repcount--)
			pci_config_wr16(hdlp, d++, *h++);
	else
		for (; repcount; repcount--)
			pci_config_wr16(hdlp, d, *h++);
}

void
pci_config_wr32(ddi_acc_impl_t *hdlp, uint32_t *addr, uint32_t value)
{
	pci_acc_cfblk_t *cfp;
	int reg;

	ASSERT64(((uintptr_t)addr >> 32) == 0);

	reg = (int)(uintptr_t)addr;

	cfp = (pci_acc_cfblk_t *)&hdlp->ahi_common.ah_bus_private;

	(*pci_putl_func)(cfp->c_busnum, cfp->c_devnum,
	    cfp->c_funcnum, reg, value);
}

void
pci_config_rep_wr32(ddi_acc_impl_t *hdlp, uint32_t *host_addr,
	uint32_t *dev_addr, size_t repcount, uint_t flags)
{
	uint32_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_AUTOINCR)
		for (; repcount; repcount--)
			pci_config_wr32(hdlp, d++, *h++);
	else
		for (; repcount; repcount--)
			pci_config_wr32(hdlp, d, *h++);
}

uint64_t
pci_config_rd64(ddi_acc_impl_t *hdlp, uint64_t *addr)
{
	uint32_t lw_val;
	uint32_t hi_val;
	uint32_t *dp;
	uint64_t val;

	dp = (uint32_t *)addr;
	lw_val = pci_config_rd32(hdlp, dp);
	dp++;
	hi_val = pci_config_rd32(hdlp, dp);
	val = ((uint64_t)hi_val << 32) | lw_val;
	return (val);
}

void
pci_config_wr64(ddi_acc_impl_t *hdlp, uint64_t *addr, uint64_t value)
{
	uint32_t lw_val;
	uint32_t hi_val;
	uint32_t *dp;

	dp = (uint32_t *)addr;
	lw_val = (uint32_t)(value & 0xffffffff);
	hi_val = (uint32_t)(value >> 32);
	pci_config_wr32(hdlp, dp, lw_val);
	dp++;
	pci_config_wr32(hdlp, dp, hi_val);
}

void
pci_config_rep_rd64(ddi_acc_impl_t *hdlp, uint64_t *host_addr,
	uint64_t *dev_addr, size_t repcount, uint_t flags)
{
	if (flags == DDI_DEV_AUTOINCR) {
		for (; repcount; repcount--)
			*host_addr++ = pci_config_rd64(hdlp, dev_addr++);
	} else {
		for (; repcount; repcount--)
			*host_addr++ = pci_config_rd64(hdlp, dev_addr);
	}
}

void
pci_config_rep_wr64(ddi_acc_impl_t *hdlp, uint64_t *host_addr,
	uint64_t *dev_addr, size_t repcount, uint_t flags)
{
	if (flags == DDI_DEV_AUTOINCR) {
		for (; repcount; repcount--)
			pci_config_wr64(hdlp, host_addr++, *dev_addr++);
	} else {
		for (; repcount; repcount--)
			pci_config_wr64(hdlp, host_addr++, *dev_addr);
	}
}


/*
 * Enable Legacy PCI config space access for the following four north bridges
 *	Host bridge: AMD HyperTransport Technology Configuration
 *	Host bridge: AMD Address Map
 *	Host bridge: AMD DRAM Controller
 *	Host bridge: AMD Miscellaneous Control
 */
int
is_amd_northbridge(dev_info_t *dip)
{
	int vendor_id, device_id;

	vendor_id = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
			"vendor-id", -1);
	device_id = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
			"device-id", -1);

	if (IS_AMD_NTBRIDGE(vendor_id, device_id))
		return (0);

	return (1);
}
