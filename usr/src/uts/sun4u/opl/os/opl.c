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

#include <sys/cpuvar.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/promif.h>
#include <sys/platform_module.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/machsystm.h>
#include <sys/bootconf.h>
#include <sys/nvpair.h>
#include <sys/kobj.h>
#include <sys/mem_cage.h>
#include <sys/opl.h>
#include <sys/scfd/scfostoescf.h>
#include <sys/cpu_sgnblk_defs.h>
#include <sys/utsname.h>
#include <sys/ddi.h>
#include <sys/sunndi.h>
#include <sys/lgrp.h>
#include <sys/memnode.h>
#include <sys/sysmacros.h>
#include <vm/vm_dep.h>

int (*opl_get_mem_unum)(int, uint64_t, char *, int, int *);
int (*opl_get_mem_sid)(char *unum, char *buf, int buflen, int *lenp);
int (*opl_get_mem_offset)(uint64_t paddr, uint64_t *offp);
int (*opl_get_mem_addr)(char *unum, char *sid,
    uint64_t offset, uint64_t *paddr);

/* Memory for fcode claims.  16k times # maximum possible IO units */
#define	EFCODE_SIZE	(OPL_MAX_BOARDS * OPL_MAX_IO_UNITS_PER_BOARD * 0x4000)
int efcode_size = EFCODE_SIZE;

#define	OPL_MC_MEMBOARD_SHIFT 38	/* Boards on 256BG boundary */

/* Set the maximum number of boards for DR */
int opl_boards = OPL_MAX_BOARDS;

void sgn_update_all_cpus(ushort_t, uchar_t, uchar_t);

extern int tsb_lgrp_affinity;

int opl_tsb_spares = (OPL_MAX_BOARDS) * (OPL_MAX_PCICH_UNITS_PER_BOARD) *
	(OPL_MAX_TSBS_PER_PCICH);

pgcnt_t opl_startup_cage_size = 0;

static struct memlist *opl_memlist_per_board(struct memlist *ml);

static enum {
	MODEL_FF1 = 0,
	MODEL_FF2 = 1,
	MODEL_DC = 2
} plat_model = -1;

int
set_platform_max_ncpus(void)
{
	return (OPL_MAX_CPU_PER_BOARD * OPL_MAX_BOARDS);
}

int
set_platform_tsb_spares(void)
{
	return (MIN(opl_tsb_spares, MAX_UPA));
}

#pragma weak mmu_init_large_pages

void
set_platform_defaults(void)
{
	extern char *tod_module_name;
	extern void cpu_sgn_update(ushort_t, uchar_t, uchar_t, int);
	extern int ts_dispatch_extended;
	extern void mmu_init_large_pages(size_t);

	/* Set the CPU signature function pointer */
	cpu_sgn_func = cpu_sgn_update;

	/* Set appropriate tod module for OPL platform */
	ASSERT(tod_module_name == NULL);
	tod_module_name = "todopl";

	/*
	 * Use the alternate TS dispatch table, which is better tuned
	 * for large servers.
	 */
	if (ts_dispatch_extended == -1)
		ts_dispatch_extended = 1;

	if ((mmu_page_sizes == max_mmu_page_sizes) &&
	    (mmu_ism_pagesize != MMU_PAGESIZE32M)) {
		if (&mmu_init_large_pages)
			mmu_init_large_pages(mmu_ism_pagesize);
	}

	tsb_lgrp_affinity = 1;
}

/*
 * Convert logical a board number to a physical one.
 */

#define	LSBPROP		"board#"
#define	PSBPROP		"physical-board#"

int
opl_get_physical_board(int id)
{
	dev_info_t	*root_dip, *dip = NULL;
	char		*dname = NULL;
	int		circ;

	pnode_t		pnode;
	char		pname[MAXSYSNAME] = {0};

	int		lsb_id;	/* Logical System Board ID */
	int		psb_id;	/* Physical System Board ID */


	/*
	 * This function is called on early stage of bootup when the
	 * kernel device tree is not initialized yet, and also
	 * later on when the device tree is up. We want to try
	 * the fast track first.
	 */
	root_dip = ddi_root_node();
	if (root_dip) {
		/* Get from devinfo node */
		ndi_devi_enter(root_dip, &circ);
		for (dip = ddi_get_child(root_dip); dip;
		    dip = ddi_get_next_sibling(dip)) {

			dname = ddi_node_name(dip);
			if (strncmp(dname, "pseudo-mc", 9) != 0)
				continue;

			if ((lsb_id = (int)ddi_getprop(DDI_DEV_T_ANY, dip,
			    DDI_PROP_DONTPASS, LSBPROP, -1)) == -1)
				continue;

			if (id == lsb_id) {
				if ((psb_id = (int)ddi_getprop(DDI_DEV_T_ANY,
				    dip, DDI_PROP_DONTPASS, PSBPROP, -1))
				    == -1) {
					ndi_devi_exit(root_dip, circ);
					return (-1);
				} else {
					ndi_devi_exit(root_dip, circ);
					return (psb_id);
				}
			}
		}
		ndi_devi_exit(root_dip, circ);
	}

	/*
	 * We do not have the kernel device tree, or we did not
	 * find the node for some reason (let's say the kernel
	 * device tree was modified), let's try the OBP tree.
	 */
	pnode = prom_rootnode();
	for (pnode = prom_childnode(pnode); pnode;
	    pnode = prom_nextnode(pnode)) {

		if ((prom_getprop(pnode, "name", (caddr_t)pname) == -1) ||
		    (strncmp(pname, "pseudo-mc", 9) != 0))
			continue;

		if (prom_getprop(pnode, LSBPROP, (caddr_t)&lsb_id) == -1)
			continue;

		if (id == lsb_id) {
			if (prom_getprop(pnode, PSBPROP,
			    (caddr_t)&psb_id) == -1) {
				return (-1);
			} else {
				return (psb_id);
			}
		}
	}

	return (-1);
}

/*
 * For OPL it's possible that memory from two or more successive boards
 * will be contiguous across the boards, and therefore represented as a
 * single chunk.
 * This function splits such chunks down the board boundaries.
 */
static struct memlist *
opl_memlist_per_board(struct memlist *ml)
{
	uint64_t ssize, low, high, boundary;
	struct memlist *head, *tail, *new;

	ssize = (1ull << OPL_MC_MEMBOARD_SHIFT);

	head = tail = NULL;

	for (; ml; ml = ml->next) {
		low  = (uint64_t)ml->address;
		high = low+(uint64_t)(ml->size);
		while (low < high) {
			boundary = roundup(low+1, ssize);
			boundary = MIN(high, boundary);
			new = kmem_zalloc(sizeof (struct memlist), KM_SLEEP);
			new->address = low;
			new->size = boundary - low;
			if (head == NULL)
				head = new;
			if (tail) {
				tail->next = new;
				new->prev = tail;
			}
			tail = new;
			low = boundary;
		}
	}
	return (head);
}

void
set_platform_cage_params(void)
{
	extern pgcnt_t total_pages;
	extern struct memlist *phys_avail;
	struct memlist *ml, *tml;
	int ret;

	if (kernel_cage_enable) {
		pgcnt_t preferred_cage_size;

		preferred_cage_size =
			MAX(opl_startup_cage_size, total_pages / 256);

		ml = opl_memlist_per_board(phys_avail);

		kcage_range_lock();
		/*
		 * Note: we are assuming that post has load the
		 * whole show in to the high end of memory. Having
		 * taken this leap, we copy the whole of phys_avail
		 * the glist and arrange for the cage to grow
		 * downward (descending pfns).
		 */
		ret = kcage_range_init(ml, 1);

		/* free the memlist */
		do {
			tml = ml->next;
			kmem_free(ml, sizeof (struct memlist));
			ml = tml;
		} while (ml != NULL);

		if (ret == 0)
			kcage_init(preferred_cage_size);
		kcage_range_unlock();
	}

	if (kcage_on)
		cmn_err(CE_NOTE, "!DR Kernel Cage is ENABLED");
	else
		cmn_err(CE_NOTE, "!DR Kernel Cage is DISABLED");
}

/*ARGSUSED*/
int
plat_cpu_poweron(struct cpu *cp)
{
	int (*opl_cpu_poweron)(struct cpu *) = NULL;

	opl_cpu_poweron =
	    (int (*)(struct cpu *))kobj_getsymvalue("drmach_cpu_poweron", 0);

	if (opl_cpu_poweron == NULL)
		return (ENOTSUP);
	else
		return ((opl_cpu_poweron)(cp));

}

/*ARGSUSED*/
int
plat_cpu_poweroff(struct cpu *cp)
{
	int (*opl_cpu_poweroff)(struct cpu *) = NULL;

	opl_cpu_poweroff =
	    (int (*)(struct cpu *))kobj_getsymvalue("drmach_cpu_poweroff", 0);

	if (opl_cpu_poweroff == NULL)
		return (ENOTSUP);
	else
		return ((opl_cpu_poweroff)(cp));

}

int
plat_max_boards(void)
{
	return (OPL_MAX_BOARDS);
}

int
plat_max_cpu_units_per_board(void)
{
	return (OPL_MAX_CPU_PER_BOARD);
}

int
plat_max_mem_units_per_board(void)
{
	return (OPL_MAX_MEM_UNITS_PER_BOARD);
}

int
plat_max_io_units_per_board(void)
{
	return (OPL_MAX_IO_UNITS_PER_BOARD);
}

int
plat_max_cmp_units_per_board(void)
{
	return (OPL_MAX_CMP_UNITS_PER_BOARD);
}

int
plat_max_core_units_per_board(void)
{
	return (OPL_MAX_CORE_UNITS_PER_BOARD);
}

int
plat_pfn_to_mem_node(pfn_t pfn)
{
	return (pfn >> mem_node_pfn_shift);
}

/* ARGSUSED */
void
plat_build_mem_nodes(u_longlong_t *list, size_t nelems)
{
	size_t	elem;
	pfn_t	basepfn;
	pgcnt_t	npgs;
	uint64_t	boundary, ssize;
	uint64_t	low, high;

	/*
	 * OPL mem slices are always aligned on a 256GB boundary.
	 */
	mem_node_pfn_shift = OPL_MC_MEMBOARD_SHIFT - MMU_PAGESHIFT;
	mem_node_physalign = 0;

	/*
	 * Boot install lists are arranged <addr, len>, <addr, len>, ...
	 */
	ssize = (1ull << OPL_MC_MEMBOARD_SHIFT);
	for (elem = 0; elem < nelems; elem += 2) {
		low  = (uint64_t)list[elem];
		high = low+(uint64_t)(list[elem+1]);
		while (low < high) {
			boundary = roundup(low+1, ssize);
			boundary = MIN(high, boundary);
			basepfn = btop(low);
			npgs = btop(boundary - low);
			mem_node_add_slice(basepfn, basepfn + npgs - 1);
			low = boundary;
		}
	}
}

/*
 * Find the CPU associated with a slice at boot-time.
 */
void
plat_fill_mc(pnode_t nodeid)
{
	int board;
	int memnode;
	struct {
		uint64_t	addr;
		uint64_t	size;
	} mem_range;

	if (prom_getprop(nodeid, "board#", (caddr_t)&board) < 0) {
		panic("Can not find board# property in mc node %x", nodeid);
	}
	if (prom_getprop(nodeid, "sb-mem-ranges", (caddr_t)&mem_range) < 0) {
		panic("Can not find sb-mem-ranges property in mc node %x",
			nodeid);
	}
	memnode = mem_range.addr >> OPL_MC_MEMBOARD_SHIFT;
	plat_assign_lgrphand_to_mem_node(board, memnode);
}

/*
 * Return the platform handle for the lgroup containing the given CPU
 *
 * For OPL, lgroup platform handle == board #.
 */

extern int mpo_disabled;
extern lgrp_handle_t lgrp_default_handle;

lgrp_handle_t
plat_lgrp_cpu_to_hand(processorid_t id)
{
	lgrp_handle_t plathand;

	/*
	 * Return the real platform handle for the CPU until
	 * such time as we know that MPO should be disabled.
	 * At that point, we set the "mpo_disabled" flag to true,
	 * and from that point on, return the default handle.
	 *
	 * By the time we know that MPO should be disabled, the
	 * first CPU will have already been added to a leaf
	 * lgroup, but that's ok. The common lgroup code will
	 * double check that the boot CPU is in the correct place,
	 * and in the case where mpo should be disabled, will move
	 * it to the root if necessary.
	 */
	if (mpo_disabled) {
		/* If MPO is disabled, return the default (UMA) handle */
		plathand = lgrp_default_handle;
	} else
		plathand = (lgrp_handle_t)LSB_ID(id);
	return (plathand);
}

/*
 * Platform specific lgroup initialization
 */
void
plat_lgrp_init(void)
{
	extern uint32_t lgrp_expand_proc_thresh;
	extern uint32_t lgrp_expand_proc_diff;

	/*
	 * Set tuneables for the OPL architecture
	 *
	 * lgrp_expand_proc_thresh is the minimum load on the lgroups
	 * this process is currently running on before considering
	 * expanding threads to another lgroup.
	 *
	 * lgrp_expand_proc_diff determines how much less the remote lgroup
	 * must be loaded before expanding to it.
	 *
	 * Since remote latencies can be costly, attempt to keep 3 threads
	 * within the same lgroup before expanding to the next lgroup.
	 */
	lgrp_expand_proc_thresh = LGRP_LOADAVG_THREAD_MAX * 3;
	lgrp_expand_proc_diff = LGRP_LOADAVG_THREAD_MAX;
}

/*
 * Platform notification of lgroup (re)configuration changes
 */
/*ARGSUSED*/
void
plat_lgrp_config(lgrp_config_flag_t evt, uintptr_t arg)
{
	update_membounds_t *umb;
	lgrp_config_mem_rename_t lmr;
	int sbd, tbd;
	lgrp_handle_t hand, shand, thand;
	int mnode, snode, tnode;
	pfn_t start, end;

	if (mpo_disabled)
		return;

	switch (evt) {

	case LGRP_CONFIG_MEM_ADD:
		/*
		 * Establish the lgroup handle to memnode translation.
		 */
		umb = (update_membounds_t *)arg;

		hand = umb->u_board;
		mnode = plat_pfn_to_mem_node(umb->u_base >> MMU_PAGESHIFT);
		plat_assign_lgrphand_to_mem_node(hand, mnode);

		break;

	case LGRP_CONFIG_MEM_DEL:
		/*
		 * Special handling for possible memory holes.
		 */
		umb = (update_membounds_t *)arg;
		hand = umb->u_board;
		if ((mnode = plat_lgrphand_to_mem_node(hand)) != -1) {
			if (mem_node_config[mnode].exists) {
				start = mem_node_config[mnode].physbase;
				end = mem_node_config[mnode].physmax;
				mem_node_pre_del_slice(start, end);
				mem_node_post_del_slice(start, end, 0);
			}
		}

		break;

	case LGRP_CONFIG_MEM_RENAME:
		/*
		 * During a DR copy-rename operation, all of the memory
		 * on one board is moved to another board -- but the
		 * addresses/pfns and memnodes don't change. This means
		 * the memory has changed locations without changing identity.
		 *
		 * Source is where we are copying from and target is where we
		 * are copying to.  After source memnode is copied to target
		 * memnode, the physical addresses of the target memnode are
		 * renamed to match what the source memnode had.  Then target
		 * memnode can be removed and source memnode can take its
		 * place.
		 *
		 * To do this, swap the lgroup handle to memnode mappings for
		 * the boards, so target lgroup will have source memnode and
		 * source lgroup will have empty target memnode which is where
		 * its memory will go (if any is added to it later).
		 *
		 * Then source memnode needs to be removed from its lgroup
		 * and added to the target lgroup where the memory was living
		 * but under a different name/memnode.  The memory was in the
		 * target memnode and now lives in the source memnode with
		 * different physical addresses even though it is the same
		 * memory.
		 */
		sbd = arg & 0xffff;
		tbd = (arg & 0xffff0000) >> 16;
		shand = sbd;
		thand = tbd;
		snode = plat_lgrphand_to_mem_node(shand);
		tnode = plat_lgrphand_to_mem_node(thand);

		/*
		 * Special handling for possible memory holes.
		 */
		if (tnode != -1 && mem_node_config[tnode].exists) {
			start = mem_node_config[mnode].physbase;
			end = mem_node_config[mnode].physmax;
			mem_node_pre_del_slice(start, end);
			mem_node_post_del_slice(start, end, 0);
		}

		plat_assign_lgrphand_to_mem_node(thand, snode);
		plat_assign_lgrphand_to_mem_node(shand, tnode);

		lmr.lmem_rename_from = shand;
		lmr.lmem_rename_to = thand;

		/*
		 * Remove source memnode of copy rename from its lgroup
		 * and add it to its new target lgroup
		 */
		lgrp_config(LGRP_CONFIG_MEM_RENAME, (uintptr_t)snode,
		    (uintptr_t)&lmr);

		break;

	default:
		break;
	}
}

/*
 * Return latency between "from" and "to" lgroups
 *
 * This latency number can only be used for relative comparison
 * between lgroups on the running system, cannot be used across platforms,
 * and may not reflect the actual latency.  It is platform and implementation
 * specific, so platform gets to decide its value.  It would be nice if the
 * number was at least proportional to make comparisons more meaningful though.
 * NOTE: The numbers below are supposed to be load latencies for uncached
 * memory divided by 10.
 *
 * XXX latency values for Columbus, not Columbus2. Should be fixed later when
 *	we know the actual numbers for Columbus2.
 */
int
plat_lgrp_latency(lgrp_handle_t from, lgrp_handle_t to)
{
	/*
	 * Return min remote latency when there are more than two lgroups
	 * (root and child) and getting latency between two different lgroups
	 * or root is involved
	 */
	if (lgrp_optimizations() && (from != to ||
	    from == LGRP_DEFAULT_HANDLE || to == LGRP_DEFAULT_HANDLE))
		return (27);
	else
		return (25);
}

/*
 * Return platform handle for root lgroup
 */
lgrp_handle_t
plat_lgrp_root_hand(void)
{
	if (mpo_disabled)
		return (lgrp_default_handle);

	return (LGRP_DEFAULT_HANDLE);
}

/*ARGSUSED*/
void
plat_freelist_process(int mnode)
{
}

void
load_platform_drivers(void)
{
	(void) i_ddi_attach_pseudo_node("dr");
}

/*
 * No platform drivers on this platform
 */
char *platform_module_list[] = {
	(char *)0
};

/*ARGSUSED*/
void
plat_tod_fault(enum tod_fault_type tod_bad)
{
}

/*ARGSUSED*/
void
cpu_sgn_update(ushort_t sgn, uchar_t state, uchar_t sub_state, int cpuid)
{
	static void (*scf_panic_callback)(int);
	static void (*scf_shutdown_callback)(int);

	/*
	 * This is for notifing system panic/shutdown to SCF.
	 * In case of shutdown and panic, SCF call back
	 * function should be called.
	 *  <SCF call back functions>
	 *   scf_panic_callb()   : panicsys()->panic_quiesce_hw()
	 *   scf_shutdown_callb(): halt() or power_down() or reboot_machine()
	 * cpuid should be -1 and state should be SIGST_EXIT.
	 */
	if (state == SIGST_EXIT && cpuid == -1) {

		/*
		 * find the symbol for the SCF panic callback routine in driver
		 */
		if (scf_panic_callback == NULL)
			scf_panic_callback = (void (*)(int))
				modgetsymvalue("scf_panic_callb", 0);
		if (scf_shutdown_callback == NULL)
			scf_shutdown_callback = (void (*)(int))
				modgetsymvalue("scf_shutdown_callb", 0);

		switch (sub_state) {
		case SIGSUBST_PANIC:
			if (scf_panic_callback == NULL) {
				cmn_err(CE_NOTE, "!cpu_sgn_update: "
				    "scf_panic_callb not found\n");
				return;
			}
			scf_panic_callback(SIGSUBST_PANIC);
			break;

		case SIGSUBST_HALT:
			if (scf_shutdown_callback == NULL) {
				cmn_err(CE_NOTE, "!cpu_sgn_update: "
				    "scf_shutdown_callb not found\n");
				return;
			}
			scf_shutdown_callback(SIGSUBST_HALT);
			break;

		case SIGSUBST_ENVIRON:
			if (scf_shutdown_callback == NULL) {
				cmn_err(CE_NOTE, "!cpu_sgn_update: "
				    "scf_shutdown_callb not found\n");
				return;
			}
			scf_shutdown_callback(SIGSUBST_ENVIRON);
			break;

		case SIGSUBST_REBOOT:
			if (scf_shutdown_callback == NULL) {
				cmn_err(CE_NOTE, "!cpu_sgn_update: "
				    "scf_shutdown_callb not found\n");
				return;
			}
			scf_shutdown_callback(SIGSUBST_REBOOT);
			break;
		}
	}
}

/*ARGSUSED*/
int
plat_get_mem_unum(int synd_code, uint64_t flt_addr, int flt_bus_id,
	int flt_in_memory, ushort_t flt_status,
	char *buf, int buflen, int *lenp)
{
	/*
	 * check if it's a Memory error.
	 */
	if (flt_in_memory) {
		if (opl_get_mem_unum != NULL) {
			return (opl_get_mem_unum(synd_code, flt_addr,
				buf, buflen, lenp));
		} else {
			return (ENOTSUP);
		}
	} else {
		return (ENOTSUP);
	}
}

/*ARGSUSED*/
int
plat_get_cpu_unum(int cpuid, char *buf, int buflen, int *lenp)
{
	int	plen;
	int	ret = 0;
	char	model[20];
	uint_t	sb;
	pnode_t	node;

	/* determine the platform model once */
	if (plat_model == -1) {
		plat_model = MODEL_DC; /* Default model */
		node = prom_rootnode();
		plen = prom_getproplen(node, "model");
		if (plen > 0 && plen < sizeof (model)) {
			(void) prom_getprop(node, "model", model);
			model[plen] = '\0';
			if (strcmp(model, "FF1") == 0)
				plat_model = MODEL_FF1;
			else if (strcmp(model, "FF2") == 0)
				plat_model = MODEL_FF2;
			else if (strncmp(model, "DC", 2) == 0)
				plat_model = MODEL_DC;
		}
	}

	sb = opl_get_physical_board(LSB_ID(cpuid));
	if (sb == -1) {
		return (ENXIO);
	}

	switch (plat_model) {
	case MODEL_FF1:
		plen = snprintf(buf, buflen, "/%s/CPUM%d", "MBU_A",
		    CHIP_ID(cpuid) / 2);
		break;

	case MODEL_FF2:
		plen = snprintf(buf, buflen, "/%s/CPUM%d", "MBU_B",
		    CHIP_ID(cpuid) / 2);
		break;

	case MODEL_DC:
		plen = snprintf(buf, buflen, "/%s%02d/CPUM%d", "CMU", sb,
		    CHIP_ID(cpuid));
		break;

	default:
		/* This should never happen */
		return (ENODEV);
	}

	if (plen >= buflen) {
		ret = ENOSPC;
	} else {
		if (lenp)
			*lenp = strlen(buf);
	}
	return (ret);
}

#define	SCF_PUTINFO(f, s, p)	\
	f(KEY_ESCF, 0x01, 0, s, p)
void
plat_nodename_set(void)
{
	void *datap;
	static int (*scf_service_function)(uint32_t, uint8_t,
	    uint32_t, uint32_t, void *);
	int counter = 5;

	/*
	 * find the symbol for the SCF put routine in driver
	 */
	if (scf_service_function == NULL)
		scf_service_function =
			(int (*)(uint32_t, uint8_t, uint32_t, uint32_t, void *))
			modgetsymvalue("scf_service_putinfo", 0);

	/*
	 * If the symbol was found, call it.  Otherwise, log a note (but not to
	 * the console).
	 */

	if (scf_service_function == NULL) {
		cmn_err(CE_NOTE,
		    "!plat_nodename_set: scf_service_putinfo not found\n");
		return;
	}

	datap =
	    (struct utsname *)kmem_zalloc(sizeof (struct utsname), KM_SLEEP);

	if (datap == NULL) {
		return;
	}

	bcopy((struct utsname *)&utsname,
	    (struct utsname *)datap, sizeof (struct utsname));

	while ((SCF_PUTINFO(scf_service_function,
		sizeof (struct utsname), datap) == EBUSY) && (counter-- > 0)) {
		delay(10 * drv_usectohz(1000000));
	}
	if (counter == 0)
		cmn_err(CE_NOTE,
			"!plat_nodename_set: "
			"scf_service_putinfo not responding\n");

	kmem_free(datap, sizeof (struct utsname));
}

caddr_t	efcode_vaddr = NULL;

/*
 * Preallocate enough memory for fcode claims.
 */

caddr_t
efcode_alloc(caddr_t alloc_base)
{
	caddr_t efcode_alloc_base = (caddr_t)roundup((uintptr_t)alloc_base,
	    MMU_PAGESIZE);
	caddr_t vaddr;

	/*
	 * allocate the physical memory for the Oberon fcode.
	 */
	if ((vaddr = (caddr_t)BOP_ALLOC(bootops, efcode_alloc_base,
	    efcode_size, MMU_PAGESIZE)) == NULL)
		cmn_err(CE_PANIC, "Cannot allocate Efcode Memory");

	efcode_vaddr = vaddr;

	return (efcode_alloc_base + efcode_size);
}

caddr_t
plat_startup_memlist(caddr_t alloc_base)
{
	caddr_t tmp_alloc_base;

	tmp_alloc_base = efcode_alloc(alloc_base);
	tmp_alloc_base =
	    (caddr_t)roundup((uintptr_t)tmp_alloc_base, ecache_alignsize);
	return (tmp_alloc_base);
}

void
startup_platform(void)
{
}

int
plat_get_mem_sid(char *unum, char *buf, int buflen, int *lenp)
{
	if (opl_get_mem_sid == NULL) {
		return (ENOTSUP);
	}
	return (opl_get_mem_sid(unum, buf, buflen, lenp));
}

int
plat_get_mem_offset(uint64_t paddr, uint64_t *offp)
{
	if (opl_get_mem_offset == NULL) {
		return (ENOTSUP);
	}
	return (opl_get_mem_offset(paddr, offp));
}

int
plat_get_mem_addr(char *unum, char *sid, uint64_t offset, uint64_t *addrp)
{
	if (opl_get_mem_addr == NULL) {
		return (ENOTSUP);
	}
	return (opl_get_mem_addr(unum, sid, offset, addrp));
}
