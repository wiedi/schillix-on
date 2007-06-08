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

/*
 *	Niagara2 Network Interface Unit (NIU) Nexus Driver
 */

#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddi_subrdefs.h>
#include <sys/ddi.h>
#include <sys/sunndi.h>
#include <sys/sunddi.h>
#include <sys/open.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/machsystm.h>
#include <sys/hsvc.h>
#include <sys/sdt.h>
#include <sys/hypervisor_api.h>
#include "niumx_var.h"

static int niumx_fm_init_child(dev_info_t *, dev_info_t *, int,
	ddi_iblock_cookie_t *);
static int niumx_intr_ops(dev_info_t *dip, dev_info_t *rdip,
	ddi_intr_op_t intr_op, ddi_intr_handle_impl_t *hdlp, void *result);
static int niumx_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);
static int niumx_detach(dev_info_t *devi, ddi_detach_cmd_t cmd);
static int niumx_set_intr(dev_info_t *dip, dev_info_t *rdip,
	ddi_intr_handle_impl_t *hdlp, int valid);
static int niumx_add_intr(dev_info_t *dip, dev_info_t *rdip,
	ddi_intr_handle_impl_t *hdlp);
static int niumx_rem_intr(dev_info_t *dip, dev_info_t *rdip,
	ddi_intr_handle_impl_t *hdlp);
static uint_t niumx_intr_hdlr(void *arg);
static int niumx_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
	off_t offset, off_t len, caddr_t *addrp);
static int niumx_dma_allochdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_attr_t *attrp,
	int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *handlep);
static int niumx_dma_freehdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handlep);
static int niumx_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle, ddi_dma_req_t *dmareq,
	ddi_dma_cookie_t *cookiep, uint_t *ccountp);
static int niumx_dma_unbindhdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle);
static int niumx_ctlops(dev_info_t *dip, dev_info_t *rdip,
	ddi_ctl_enum_t op, void *arg, void *result);

static struct bus_ops niumx_bus_ops = {
	BUSO_REV,
	niumx_map,
	0,
	0,
	0,
	i_ddi_map_fault,
	0,
	niumx_dma_allochdl,
	niumx_dma_freehdl,
	niumx_dma_bindhdl,
	niumx_dma_unbindhdl,
	0,
	0,
	0,
	niumx_ctlops,
	ddi_bus_prop_op,
	0,				/* (*bus_get_eventcookie)();    */
	0,				/* (*bus_add_eventcall)();	*/
	0,				/* (*bus_remove_eventcall)();   */
	0,				/* (*bus_post_event)();		*/
	0,				/* (*bus_intr_ctl)();		*/
	0,				/* (*bus_config)(); 		*/
	0,				/* (*bus_unconfig)(); 		*/
	niumx_fm_init_child,		/* (*bus_fm_init)(); 		*/
	0,				/* (*bus_fm_fini)(); 		*/
	0,				/* (*bus_enter)()		*/
	0,				/* (*bus_exit)()		*/
	0,				/* (*bus_power)()		*/
	niumx_intr_ops			/* (*bus_intr_op)(); 		*/
};

static struct dev_ops niumx_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* refcnt  */
	ddi_no_info,		/* info */
	nulldev,		/* identify */
	0,			/* probe */
	niumx_attach,		/* attach */
	niumx_detach,		/* detach */
	nulldev,		/* reset */
	(struct cb_ops *)0,	/* driver operations */
	&niumx_bus_ops,		/* bus operations */
	0
};

/* Module linkage information for the kernel. */
static struct modldrv modldrv = {
	&mod_driverops, /* Type of module */
	"NIU Nexus Driver %I%",
	&niumx_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modldrv,
	NULL
};

static void *niumx_state;
static niumx_ih_t niumx_ihtable[NIUMX_MAX_INTRS];

/*
 * forward function declarations:
 */
static void niumx_removechild(dev_info_t *);
static int niumx_initchild(dev_info_t *child);

int
_init(void)
{
	int e;
	uint64_t mjrnum;
	uint64_t mnrnum;

	/*
	 * Check HV intr group api versioning.
	 * This driver uses the old interrupt routines which are supported
	 * in old firmware in the CORE API group and in newer firmware in
	 * the INTR API group.  Support for these calls will be dropped
	 * once the INTR API group major goes to 2.
	 */
	if ((hsvc_version(HSVC_GROUP_INTR, &mjrnum, &mnrnum) == 0) &&
	    (mjrnum > NIUMX_INTR_MAJOR_VER)) {
		cmn_err(CE_WARN, "niumx: unsupported intr api group: "
		    "maj:0x%lx, min:0x%lx", mjrnum, mnrnum);
		return (ENOTSUP);
	}

	if ((e = ddi_soft_state_init(&niumx_state, sizeof (niumx_devstate_t),
	    1)) == 0 && (e = mod_install(&modlinkage)) != 0)
		ddi_soft_state_fini(&niumx_state);
	return (e);
}

int
_fini(void)
{
	int e;
	if ((e = mod_remove(&modlinkage)) == 0)
		ddi_soft_state_fini(&niumx_state);
	return (e);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


hrtime_t niumx_intr_timeout = 2ull * NANOSEC; /* 2 seconds in nanoseconds */

void
niumx_intr_dist(void *arg)
{
	kmutex_t 	*lock_p = (kmutex_t *)arg;
	int		i = NIUMX_RSVD_INTRS;
	niumx_ih_t	*ih_p = niumx_ihtable + i;

	DBG(DBG_A_INTX, NULL, "niumx_intr_dist entered\n");
	mutex_enter(lock_p);
	for (; i < NIUMX_MAX_INTRS; i++, ih_p++) {
		sysino_t sysino = ih_p->ih_sysino;
		cpuid_t	cpuid;
		int	intr_state, state;
		hrtime_t	start;
		dev_info_t	*dip = ih_p->ih_dip;
		if (!sysino ||	/* sequence is significant */
		    (hvio_intr_getvalid(sysino, &intr_state) != H_EOK) ||
		    (intr_state == HV_INTR_NOTVALID) ||
		    (cpuid = intr_dist_cpuid()) == ih_p->ih_cpuid)
			continue;

		(void) hvio_intr_setvalid(sysino, HV_INTR_NOTVALID);

		/* check for pending interrupts, busy wait if so */
		for (start = gethrtime(); !panicstr &&
		    (hvio_intr_getstate(sysino, &state) == H_EOK) &&
		    (state == HV_INTR_DELIVERED_STATE); /* */) {
			if (gethrtime() - start > niumx_intr_timeout) {
				cmn_err(CE_WARN, "%s%d: niumx_intr_dist: "
				    "pending interrupt (%x,%lx) timedout\n",
				    ddi_driver_name(dip), ddi_get_instance(dip),
				    ih_p->ih_inum, sysino);
				(void) hvio_intr_setstate(sysino,
					HV_INTR_IDLE_STATE);
				break;
			}
		}
		(void) hvio_intr_settarget(sysino, cpuid);
		(void) hvio_intr_setvalid(sysino, HV_INTR_VALID);
		ih_p->ih_cpuid = cpuid;
	}
	mutex_exit(lock_p);
}

static int
niumx_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int instance = ddi_get_instance(dip);
	niumx_devstate_t *niumxds_p;	/* devstate pointer */
	niu_regspec_t	*reg_p;
	uint_t		reglen;
	int		ret = DDI_SUCCESS;

	switch (cmd) {
	case DDI_ATTACH:
		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip,
			DDI_PROP_DONTPASS, "reg", (int **)&reg_p, &reglen)
				!= DDI_PROP_SUCCESS) {
			DBG(DBG_ATTACH, dip, "reg lookup failed\n");
			ret = DDI_FAILURE;
			goto done;
		}

		/*
		 * Allocate and get soft state structure.
		 */
		if (ddi_soft_state_zalloc(niumx_state, instance)
			!= DDI_SUCCESS) {
			ret = DDI_FAILURE;
			goto prop_free;
		}
		niumxds_p = (niumx_devstate_t *)ddi_get_soft_state(niumx_state,
							instance);
		niumxds_p->dip = dip;
		mutex_init(&niumxds_p->niumx_mutex, NULL, MUTEX_DRIVER, NULL);

		DBG(DBG_ATTACH, dip, "soft state alloc'd instance = %d, "
			"niumxds_p = %p\n", instance, niumxds_p);

		/* hv devhdl: low 28-bit of 1st "reg" entry's addr.hi */
		niumxds_p->niumx_dev_hdl = (devhandle_t)(reg_p->addr_high &
			NIUMX_DEVHDLE_MASK);

		/* add interrupt redistribution callback */
		intr_dist_add(niumx_intr_dist, &niumxds_p->niumx_mutex);

		niumxds_p->niumx_fm_cap = DDI_FM_EREPORT_CAPABLE;

		ddi_fm_init(niumxds_p->dip, &niumxds_p->niumx_fm_cap,
			&niumxds_p->niumx_fm_ibc);

		ret = DDI_SUCCESS;
		goto prop_free;
cleanup:
		mutex_destroy(&niumxds_p->niumx_mutex);
		ddi_soft_state_free(niumx_state, ddi_get_instance(dip));
prop_free:
		ddi_prop_free(reg_p);
done:
		return (ret);

	case DDI_RESUME:
	default:
		break;
	}
	return (ret);
}

static int
niumx_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	niumx_devstate_t *niumxds_p;

	switch (cmd) {
	case DDI_DETACH:

		niumxds_p = (niumx_devstate_t *)
		    ddi_get_soft_state(niumx_state, ddi_get_instance(dip));

		intr_dist_rem(niumx_intr_dist, &niumxds_p->niumx_mutex);
		ddi_fm_fini(dip);
		mutex_destroy(&niumxds_p->niumx_mutex);
		ddi_soft_state_free(niumx_state, ddi_get_instance(dip));
		return (DDI_SUCCESS);

	case DDI_SUSPEND:
	default:
		break;
	}
	return (DDI_FAILURE);
}


/*
 * Function used to initialize FMA for our children nodes. Called
 * through pci busops when child node calls ddi_fm_init.
 */
/*ARGSUSED*/
int
niumx_fm_init_child(dev_info_t *dip, dev_info_t *cdip, int cap,
    ddi_iblock_cookie_t *ibc_p)
{
	niumx_devstate_t	*niumxds_p = DIP_TO_STATE(dip);

	ASSERT(ibc_p != NULL);
	*ibc_p = niumxds_p->niumx_fm_ibc;

	return (niumxds_p->niumx_fm_cap);
}


/*ARGSUSED*/
int
niumx_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
	off_t offset, off_t len, caddr_t *vaddrp)
{
	struct regspec p_regspec;
	ddi_map_req_t p_mapreq;
	niu_regspec_t	*reg_p;
	int 	i, rn = mp->map_obj.rnumber, reglen, rnglen, rngnum, ret;
	niumx_ranges_t	*rng_p;

	uint32_t	reg_begin, rng_begin;

	DBG(DBG_MAP, dip, "%s%d: mapping %s%d reg %d\n", NAMEINST(dip),
		NAMEINST(rdip), rn);

	if (ddi_getlongprop(DDI_DEV_T_ANY, rdip, DDI_PROP_DONTPASS,
		"reg", (caddr_t)&reg_p, &reglen) != DDI_SUCCESS)
		return (DDI_FAILURE);

	if (rn < 0 || (rn >= reglen / sizeof (niu_regspec_t))) {
		DBG(DBG_MAP, dip,  "rnumber out of range: %d\n", rn);
		kmem_free(reg_p, reglen);
		return (DDI_ME_RNUMBER_RANGE);
	}

	/* build regspec up for parent */
	p_mapreq = *mp;		/* dup the whole structure */
	p_mapreq.map_type = DDI_MT_REGSPEC;
	p_mapreq.map_obj.rp = &p_regspec;

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS, "ranges",
		(caddr_t)&rng_p, &rnglen) != DDI_SUCCESS) {
			DBG(DBG_MAP,  dip, "%s%d: no ranges property\n",
				ddi_driver_name(dip), ddi_get_instance(dip));
			kmem_free(reg_p, reglen);
			return (DDI_FAILURE);
	}

	/* locate matching ranges record */
	rngnum = rnglen / sizeof (niumx_ranges_t);
	for (i = 0, reg_p += rn; i < rngnum; rng_p++, i++) {
		if (reg_p->addr_high == rng_p->child_hi)
			break;
	}

	if (i >= rngnum) {
		DBG(DBG_MAP, dip, "ranges record for reg[%d] not found.\n", rn);
		ret = DDI_ME_REGSPEC_RANGE;
		goto err;
	}

	/*
	 * validate request has matching bus type and within 4G
	 * limit by comparing addr.hi of "ranges" and child "reg".
	 */

	ASSERT(reg_p->size_high == 0);

	rng_begin = rng_p->child_lo;
	reg_begin = reg_p->addr_low;
	/* check to verify reg bounds are within rng bounds */
	if (reg_begin < rng_begin || (reg_begin + (reg_p->size_low - 1)) >
			(rng_begin + (rng_p->size_lo - 1))) {
		DBG(DBG_MAP, dip, "size out of range for reg[%d].\n", rn);
		ret = DDI_ME_REGSPEC_RANGE;
		goto err;
	}

	p_regspec.regspec_bustype = rng_p->parent_hi;
	p_regspec.regspec_addr = reg_begin - rng_begin + rng_p->parent_lo;
	p_regspec.regspec_size = reg_p->size_low;
	DBG(DBG_MAP, dip, "regspec:bus,addr,size = (%x,%x,%x)\n",
		p_regspec.regspec_bustype, p_regspec.regspec_addr,
		p_regspec.regspec_size);
	ret = ddi_map(dip, &p_mapreq, 0, 0, vaddrp);
	DBG(DBG_MAP, dip, "niumx_map: ret %d.\n", ret);
err:
	kmem_free(rng_p - i, rnglen);
	kmem_free(reg_p - rn, reglen);
	return (ret);
}

/*
 * niumx_ctlops
 */
int
niumx_ctlops(dev_info_t *dip, dev_info_t *rdip,
	ddi_ctl_enum_t ctlop, void *arg, void *result)
{
	niu_regspec_t *reg_p;
	int	reglen, totreg;

	DBG(DBG_CTLOPS, dip, "niumx_ctlops ctlop=%d.\n", ctlop);
	if (rdip == (dev_info_t *)0)
		return (DDI_FAILURE);

	switch (ctlop) {
	case DDI_CTLOPS_REPORTDEV:
		cmn_err(CE_NOTE, "device: %s@%s, %s%d\n",
		    ddi_node_name(rdip), ddi_get_name_addr(rdip),
		    NAMEINST(rdip));
		return (DDI_SUCCESS);

	case DDI_CTLOPS_INITCHILD:
		return (niumx_initchild((dev_info_t *)arg));

	case DDI_CTLOPS_UNINITCHILD:
		niumx_removechild((dev_info_t *)arg);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_REGSIZE:
	case DDI_CTLOPS_NREGS:
		/* fall through */
		break;
	default:
		DBG(DBG_CTLOPS, dip, "just pass to ddi_cltops.\n");
		return (ddi_ctlops(dip, rdip, ctlop, arg, result));
	}

	/* REGSIZE/NREGS */

	*(int *)result = 0;

	if (ddi_getlongprop(DDI_DEV_T_NONE, rdip, DDI_PROP_DONTPASS |
		DDI_PROP_CANSLEEP, "reg", (caddr_t)&reg_p, &reglen)
			!= DDI_SUCCESS)
		return (DDI_FAILURE);

	totreg = reglen / sizeof (niu_regspec_t);
	if (ctlop == DDI_CTLOPS_NREGS) {
		DBG(DBG_CTLOPS, (dev_info_t *)dip, "niumx_ctlops NREGS=%d.\n",
				totreg);
		*(int *)result = totreg;
	} else if (ctlop == DDI_CTLOPS_REGSIZE) {
		int	rn;
		rn = *(int *)arg;
		if (rn >= totreg) {
			kmem_free(reg_p, reglen);
			return (DDI_FAILURE);
		}
		*(off_t *)result = (reg_p + rn)->size_low;
		DBG(DBG_CTLOPS, (dev_info_t *)dip, "rn = %d, REGSIZE=%x.\n",
				rn, *(off_t *)result);
	}

	kmem_free(reg_p, reglen);
	return (DDI_SUCCESS);
}

static int
niumx_initchild(dev_info_t *child)
{
	char name[MAXNAMELEN];
	niu_regspec_t *r;
	uint_t n;

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, child, DDI_PROP_DONTPASS,
	    "reg", (int **)&r, &n) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	(void) snprintf(name, MAXNAMELEN, "%x", (r[0].addr_high &
		NIUMX_FUNC_NUM_MASK));
	ddi_prop_free(r);
	ddi_set_name_addr(child, name);
	return (DDI_SUCCESS);
}

static void
niumx_removechild(dev_info_t *dip)
{
	ddi_set_name_addr(dip, NULL);
	ddi_remove_minor_node(dip, NULL);
	impl_rem_dev_props(dip);
}



/*
 * bus dma alloc handle entry point:
 */
/*ARGSUSED*/
int
niumx_dma_allochdl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_attr_t *attrp,
	int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *handlep)
{
	ddi_dma_impl_t *mp;
	int sleep = (waitfp == DDI_DMA_SLEEP) ? KM_SLEEP : KM_NOSLEEP;

	DBG(DBG_DMA_ALLOCH, dip, "rdip=%s%d\n", NAMEINST(rdip));

	if (attrp->dma_attr_version != DMA_ATTR_V0) {
		DBG(DBG_DMA_ALLOCH, (dev_info_t *)dip, "DDI_DMA_BADATTR\n");
		return (DDI_DMA_BADATTR);
	}

	/* Caution: we don't use zalloc to enhance performance! */
	if ((mp = kmem_alloc(sizeof (ddi_dma_impl_t), sleep)) == 0) {
		DBG(DBG_DMA_ALLOCH, dip, "can't alloc ddi_dma_impl_t\n");
		return (DDI_FAILURE);
	}
	mp->dmai_rdip = rdip;
	mp->dmai_pfnlst = NULL;
	mp->dmai_cookie = NULL;
	mp->dmai_fault = 0;
	mp->dmai_fault_check = NULL;
	mp->dmai_fault_notify = NULL;

	mp->dmai_attr = *attrp; 	/* set requestors attr info */

	DBG(DBG_DMA_ALLOCH, dip, "mp=%p\n", mp);

	*handlep = (ddi_dma_handle_t)mp;
	return (DDI_SUCCESS);
}


/*
 * bus dma free handle entry point:
 */
/*ARGSUSED*/
int
niumx_dma_freehdl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handle)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;

	if (mp->dmai_cookie)
		kmem_free(mp->dmai_cookie, sizeof (ddi_dma_cookie_t));
	kmem_free(mp, sizeof (ddi_dma_impl_t));

	return (DDI_SUCCESS);
}


/*
 * bus dma bind handle entry point:
 *
 *	check/enforce DMA type, setup pfn0 and some other key pieces
 *	of this dma request.
 * Note: this only works with DMA_OTYP_VADDR, and makes use of the known
 *	fact that only contiguous memory blocks will be passed in.
 *	Therefore only one cookie will ever be returned.
 *
 *	return values:
 *		DDI_DMA_NOMAPPING - can't get valid pfn0, or bad dma type
 *		DDI_DMA_NORESOURCES
 *		DDI_SUCCESS
 *
 *	dma handle members affected (set on exit):
 *	mp->dmai_object		- dmareq->dmar_object
 *	mp->dmai_rflags		- dmareq->dmar_flags
 *	mp->dmai_pfn0   	- 1st page pfn (if va/size pair and not shadow)
 *	mp->dmai_roffset 	- initialized to starting page offset
 *	mp->dmai_size		- # of total pages of entire object
 *	mp->dmai_cookie		- new cookie alloc'd
 */
/*ARGSUSED*/
int
niumx_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle, ddi_dma_req_t *dmareq,
	ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	int (*waitfp)(caddr_t) = dmareq->dmar_fp;
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	ddi_dma_obj_t *dobj_p = &dmareq->dmar_object;
	uint32_t offset;
	pfn_t pfn0;
	int ret;

	DBG(DBG_DMA_BINDH, dip, "rdip=%s%d mp=%p dmareq=%p\n", NAMEINST(rdip),
		mp, dmareq);

	/* first check dma type */
	mp->dmai_rflags = dmareq->dmar_flags & DMP_DDIFLAGS | DMP_NOSYNC;
	switch (dobj_p->dmao_type) {
	case DMA_OTYP_VADDR: {
		caddr_t vaddr = dobj_p->dmao_obj.virt_obj.v_addr;
		struct as *as_p = dobj_p->dmao_obj.virt_obj.v_as;
		struct hat *hat_p = as_p ? as_p->a_hat : kas.a_hat;
		offset = (ulong_t)vaddr & NIUMX_PAGE_OFFSET;
		pfn0 = hat_getpfnum(hat_p, vaddr);
		}
		break;

	case DMA_OTYP_BUFVADDR:
	case DMA_OTYP_PAGES:
	case DMA_OTYP_PADDR:
	default:
		cmn_err(CE_WARN, "%s%d requested unsupported dma type %x",
			NAMEINST(mp->dmai_rdip), dobj_p->dmao_type);
		ret = DDI_DMA_NOMAPPING;
		goto err;
	}
	if (pfn0 == PFN_INVALID) {
		cmn_err(CE_WARN, "%s%d: invalid pfn0 for DMA object %p",
			NAMEINST(dip), (void *)dobj_p);
		ret = DDI_DMA_NOMAPPING;
		goto err;
	}
	mp->dmai_object	 = *dobj_p;			/* whole object */
	mp->dmai_pfn0	 = (void *)pfn0;		/* cache pfn0   */
	mp->dmai_roffset = offset;			/* pg0 offset   */
	mp->dmai_mapping = mp->dmai_roffset | NIUMX_PTOB(pfn0);
	mp->dmai_size = mp->dmai_object.dmao_size;

	DBG(DBG_DMA_BINDH, dip, "check pfn: mp=%p pfn0=%x\n",
		mp, mp->dmai_pfn0);
	if (!(mp->dmai_cookie = kmem_zalloc(sizeof (ddi_dma_cookie_t),
		waitfp == DDI_DMA_SLEEP ? KM_SLEEP : KM_NOSLEEP))) {
			ret = DDI_DMA_NORESOURCES;
			goto err;
		}
	mp->dmai_cookie->dmac_laddress = mp->dmai_mapping;
	mp->dmai_cookie->dmac_size = mp->dmai_size;
	*ccountp = 1;
	*cookiep = *mp->dmai_cookie;
	DBG(DBG_DMA_BINDH, dip, "cookie %" PRIx64 "+%x, count=%d\n",
		cookiep->dmac_address, cookiep->dmac_size, *ccountp);
	return (DDI_DMA_MAPPED);

err:
	DBG(DBG_DMA_BINDH, (dev_info_t *)dip,
			"niumx_dma_bindhdl error ret=%d\n", ret);
	return (ret);
}

/*
 * bus dma unbind handle entry point:
 */
/*ARGSUSED*/
int
niumx_dma_unbindhdl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handle)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;

	DBG(DBG_DMA_UNBINDH, dip, "rdip=%s%d, mp=%p\n",
		ddi_driver_name(rdip), ddi_get_instance(rdip), handle);
	if (mp->dmai_cookie) {
		kmem_free(mp->dmai_cookie, sizeof (ddi_dma_cookie_t));
		mp->dmai_cookie = NULL;
	}

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
int
niumx_intr_ops(dev_info_t *dip, dev_info_t *rdip, ddi_intr_op_t intr_op,
    ddi_intr_handle_impl_t *hdlp, void *result)
{

	int	ret = DDI_SUCCESS;

	DBG(DBG_INTROPS, dip, "niumx_intr_ops: dip=%p rdip=%p intr_op=%x "
	    "handle=%p\n", dip, rdip, intr_op, hdlp);

	switch (intr_op) {

	case DDI_INTROP_SUPPORTED_TYPES:
		*(int *)result = DDI_INTR_TYPE_FIXED;
		break;
	case DDI_INTROP_GETCAP:
		*(int *)result =  DDI_INTR_FLAG_LEVEL;
		break;
	case DDI_INTROP_SETCAP:
		ret = DDI_ENOTSUP;
		break;
	case DDI_INTROP_ALLOC:
		/*  scratch1 = count,  # of intrs from DDI framework */
		*(int *)result = hdlp->ih_scratch1;
		break;
	case DDI_INTROP_FREE:
		/* Do we need to do anything here?  */
		break;
	case DDI_INTROP_GETPRI:
		*(int *)result = NIUMX_DEFAULT_PIL;
		break;
	case DDI_INTROP_SETPRI:
		ret = DDI_ENOTSUP;
		break;
	case DDI_INTROP_ADDISR:
		ret = niumx_add_intr(dip, rdip, hdlp);
		break;
	case DDI_INTROP_REMISR:
		ret = niumx_rem_intr(dip, rdip, hdlp);
		break;
	case DDI_INTROP_ENABLE:
		ret = niumx_set_intr(dip, rdip, hdlp, HV_INTR_VALID);
		break;
	case DDI_INTROP_DISABLE:
		ret = niumx_set_intr(dip, rdip, hdlp, HV_INTR_NOTVALID);
		break;
	case DDI_INTROP_SETMASK:
		ret = DDI_ENOTSUP;
		break;
	case DDI_INTROP_CLRMASK:
		ret = DDI_ENOTSUP;
		break;
	case DDI_INTROP_GETPENDING:
		ret = DDI_ENOTSUP;
		break;
	case DDI_INTROP_NINTRS:
	case DDI_INTROP_NAVAIL: {
		devino_t	*inos_p;
		int		inoslen;
		if (ddi_getlongprop(DDI_DEV_T_ANY, rdip, DDI_PROP_DONTPASS,
			"interrupts", (caddr_t)&inos_p, &inoslen)
			!= DDI_SUCCESS) {
				ret = DDI_FAILURE;
				break;
			}
		*(int *)result = inoslen / sizeof (uint32_t);
		kmem_free(inos_p, inoslen);
		}
		break;
	default:
		ret = DDI_ENOTSUP;
		break;
	}

	DBG(DBG_INTROPS, dip, "niumx_intr_ops: ret=%d\n", ret);
	return (ret);
}

int
niumx_set_intr(dev_info_t *dip, dev_info_t *rdip,
    ddi_intr_handle_impl_t *hdlp, int valid)
{
	niumx_ih_t	*ih_p;
	devino_t	*inos_p;
	int		inoslen, ret = DDI_SUCCESS;
	uint64_t	hvret;

	ASSERT(hdlp->ih_inum < NIUMX_MAX_INTRS);

	/* find the appropriate slot from the fixed table */
	if (ddi_getlongprop(DDI_DEV_T_ANY, rdip, DDI_PROP_DONTPASS,
		"interrupts", (caddr_t)&inos_p, &inoslen) != DDI_SUCCESS) {
		ret = DDI_FAILURE;
		goto fail;
	}
	ih_p = niumx_ihtable + inos_p[hdlp->ih_inum];
	DBG(DBG_A_INTX, dip, "niumx_set_intr: rdip=%s%d, valid=%d %s (%x,%x)\n",
		NAMEINST(rdip), valid, valid ? "enabling" : "disabling",
		ih_p->ih_inum, ih_p->ih_sysino);

	if (valid == HV_INTR_VALID)
		(void) hvio_intr_setstate(ih_p->ih_sysino, HV_INTR_IDLE_STATE);
	if ((hvret = hvio_intr_setvalid(ih_p->ih_sysino, valid))
		!= H_EOK) {
		DBG(DBG_A_INTX, dip, "hvio_intr_setvalid failed, ret 0x%x\n",
			hvret);
		ret = DDI_FAILURE;
	}
	kmem_free(inos_p, inoslen);
fail:
	return (ret);
}



/*
 * niumx_add_intr:
 *
 * This is the leaf/nexus/HV mapping, now read from "interrupts":
 *
 * we have a range of 64 to work with:
 *   [0-15]  - reserved
 *   [16]    - mac0
 *   [17]    - MIF
 *   [18]    - SYSERR
 *   [19-26] - func0 Rx (qty. 8)
 *   [27-34] - func0 Tx (qty. 8)
 *   [35]    - mac1
 *   [36-43] - func1 Rx (qty. 8)
 *   [44-51] - func1 Tx (qty. 8)
 */
int
niumx_add_intr(dev_info_t *dip, dev_info_t *rdip,
    ddi_intr_handle_impl_t *hdlp)
{
	niumx_ih_t	*ih_p;
	int		inoslen, ret = DDI_SUCCESS;
	uint64_t	hvret;
	devino_t	*inos_p, ino; /* INO numbers, from "interrupts" prop */
	sysino_t	sysino;

	/* get new ino */
	if (hdlp->ih_inum >= NIUMX_MAX_INTRS) {
		DBG(DBG_INTR, dip, "error: inum %d out of range\n",
			hdlp->ih_inum);
		ret = DDI_FAILURE;
		goto done;
	}
	if (ddi_getlongprop(DDI_DEV_T_ANY, rdip, DDI_PROP_DONTPASS,
		"interrupts", (caddr_t)&inos_p, &inoslen) != DDI_SUCCESS) {
		ret = DDI_FAILURE;
		goto done;
	}
	ih_p = niumx_ihtable + inos_p[hdlp->ih_inum];
	ino = inos_p[hdlp->ih_inum];
	kmem_free(inos_p, inoslen);
	if ((hvret = hvio_intr_devino_to_sysino(DIP_TO_HANDLE(dip), ino,
		&sysino)) != H_EOK) {
		DBG(DBG_INTR, dip, "hvio_intr_devino_to_sysino failed, "
			"ret 0x%x\n", hvret);
		ret = DDI_FAILURE;
		goto done;
	}
	ih_p->ih_sysino = sysino;
	ih_p->ih_dip = dip;
	ih_p->ih_inum = hdlp->ih_inum;
	ih_p->ih_hdlr = hdlp->ih_cb_func;
	ih_p->ih_arg1 = hdlp->ih_cb_arg1;
	ih_p->ih_arg2 = hdlp->ih_cb_arg2;

	DBG(DBG_A_INTX, dip, "niumx_add_intr: rdip=%s%d inum=0x%x "
		"handler=%p arg1=%p arg2=%p, new ih_p = %p\n", NAMEINST(rdip),
		hdlp->ih_inum, hdlp->ih_cb_func, hdlp->ih_cb_arg1,
		hdlp->ih_cb_arg2, ih_p);

	if (hdlp->ih_pri == 0)
		hdlp->ih_pri = NIUMX_DEFAULT_PIL;

	/* Save sysino value in hdlp */
	hdlp->ih_vector = ih_p->ih_sysino;

	/* swap in our handler & arg */
	DDI_INTR_ASSIGN_HDLR_N_ARGS(hdlp, (ddi_intr_handler_t *)niumx_intr_hdlr,
			(void *)ih_p, NULL);

	DBG(DBG_A_INTX, dip, "for ino %x adding (%x,%x)\n", ino, ih_p->ih_inum,
			ih_p->ih_sysino);
	ret = i_ddi_add_ivintr(hdlp);

	/* Restore orig. interrupt handler & args in handle. */
	DDI_INTR_ASSIGN_HDLR_N_ARGS(hdlp, ih_p->ih_hdlr, ih_p->ih_arg1,
		ih_p->ih_arg2);

	if (ret != DDI_SUCCESS) {
		DBG(DBG_A_INTX, dip, "i_ddi_add_ivintr error ret=%x\n", ret);
		goto done;
	}

	/* select cpu, saving it for removal */
	ih_p->ih_cpuid = intr_dist_cpuid();

	if ((hvret = hvio_intr_settarget(ih_p->ih_sysino, ih_p->ih_cpuid))
		!= H_EOK) {
		DBG(DBG_A_INTX, dip, "hvio_intr_settarget failed, ret 0x%x\n",
			hvret);
		ret = DDI_FAILURE;
	}
done:
	DBG(DBG_A_INTX, dip, "done, ret = %d, ih_p 0x%p, hdlp 0x%p\n", ih_p,
		hdlp, ret);
	return (ret);
}

/*
 * niumx_rem_intr:
 *
 * This function is called to unregister interrupts.
 */
int
niumx_rem_intr(dev_info_t *dip, dev_info_t *rdip,
    ddi_intr_handle_impl_t *hdlp)
{
	niumx_ih_t	*ih_p;
	devino_t	*inos_p;
	int		inoslen, ret = DDI_SUCCESS, state;
	hrtime_t	start;
	sysino_t 	sysino;

	ASSERT(hdlp->ih_inum < NIUMX_MAX_INTRS);

	/* find the appropriate slot from the fixed table */
	if (ddi_getlongprop(DDI_DEV_T_ANY, rdip, DDI_PROP_DONTPASS,
		"interrupts", (caddr_t)&inos_p, &inoslen) != DDI_SUCCESS) {
		ret = DDI_FAILURE;
		goto fail1;
	}
	ih_p = niumx_ihtable + inos_p[hdlp->ih_inum];
	sysino = ih_p->ih_sysino;
	DBG(DBG_R_INTX, dip, "removing (%x,%x)\n", ih_p->ih_inum, sysino);

	(void) hvio_intr_setvalid(sysino, HV_INTR_NOTVALID);

	/* check for pending interrupts, busy wait if so */
	for (start = gethrtime(); !panicstr &&
	    (hvio_intr_getstate(sysino, &state) == H_EOK) &&
	    (state == HV_INTR_DELIVERED_STATE); /* */) {
		if (gethrtime() - start > niumx_intr_timeout) {
			cmn_err(CE_WARN, "%s%d: niumx_intr_dist: "
			    "pending interrupt (%x,%lx) timedout\n",
			    ddi_driver_name(dip), ddi_get_instance(dip),
			    ih_p->ih_inum, sysino);
			ret = DDI_FAILURE;
			goto fail2;
		}
	}

	hdlp->ih_vector = (uint32_t)sysino;
	if (hdlp->ih_vector !=  NULL) i_ddi_rem_ivintr(hdlp);

fail2:
	kmem_free(inos_p, inoslen);
fail1:
	return (ret);
}

/*
 * niumx_intr_hdlr (our interrupt handler)
 */
uint_t
niumx_intr_hdlr(void *arg)
{
	niumx_ih_t *ih_p = (niumx_ih_t *)arg;
	uint_t		r;

	DTRACE_PROBE4(interrupt__start, dev_info_t, ih_p->ih_dip, void *,
		ih_p->ih_hdlr, caddr_t, ih_p->ih_arg1, caddr_t, ih_p->ih_arg2);

	r = (*ih_p->ih_hdlr)(ih_p->ih_arg1, ih_p->ih_arg2);

	DTRACE_PROBE4(interrupt__complete, dev_info_t, ih_p->ih_dip, void *,
		ih_p->ih_hdlr, caddr_t, ih_p->ih_arg1, int, r);

	(void) hvio_intr_setstate(ih_p->ih_sysino, HV_INTR_IDLE_STATE);
	return (r);
}

#ifdef	DEBUG
uint64_t niumx_debug_flags = 0;

static char *niumx_debug_sym [] = {	/* same sequence as niumx_debug_bit */
	/*  0 */ "attach",
	/*  1 */ "map",
	/*  2 */ "nex-ctlops",
	/*  3 */ "introps",
	/*  4 */ "intr-add",
	/*  5 */ "intr-rem",
	/*  6 */ "intr",
	/*  7 */ "dma-alloc",
	/*  8 */ "dma-bind",
	/*  9 */ "dma-unbind",
	/* 10 */ "chk-dma-mode"
};

/*ARGSUSED*/
void
niumx_dbg(niumx_debug_bit_t bit, dev_info_t *dip, char *fmt, ...)
{
	va_list ap;
	char msgbuf[1024];

	if (!(1ull << bit & niumx_debug_flags))
		return;
	va_start(ap, fmt);
	(void) vsprintf(msgbuf, fmt, ap);
	va_end(ap);
	cmn_err(CE_NOTE, "%s: %s", niumx_debug_sym[bit], msgbuf);
}

#endif	/* DEBUG */
