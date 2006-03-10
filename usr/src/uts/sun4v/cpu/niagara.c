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

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/archsystm.h>
#include <sys/machparam.h>
#include <sys/machsystm.h>
#include <sys/cpu.h>
#include <sys/elf_SPARC.h>
#include <vm/hat_sfmmu.h>
#include <vm/page.h>
#include <sys/cpuvar.h>
#include <sys/async.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/dditypes.h>
#include <sys/sunddi.h>
#include <sys/cpu_module.h>
#include <sys/prom_debug.h>
#include <sys/vmsystm.h>
#include <sys/prom_plat.h>
#include <sys/sysmacros.h>
#include <sys/intreg.h>
#include <sys/machtrap.h>
#include <sys/ontrap.h>
#include <sys/ivintr.h>
#include <sys/atomic.h>
#include <sys/panic.h>
#include <sys/dtrace.h>
#include <sys/simulate.h>
#include <sys/fault.h>
#include <sys/niagararegs.h>
#include <sys/trapstat.h>
#include <sys/hsvc.h>

#define	S_VAC_SIZE	MMU_PAGESIZE /* XXXQ? */

/*
 * Maximum number of contexts
 */
#define	MAX_NCTXS	(1 << 13)

uint_t root_phys_addr_lo_mask = 0xffffffffU;
static niagara_mmustat_t *cpu_tstat_va;		/* VA of mmustat buffer */
static uint64_t cpu_tstat_pa;			/* PA of mmustat buffer */
char cpu_module_name[] = "SUNW,UltraSPARC-T1";

/*
 * Hypervisor services information for the NIAGARA CPU module
 */
static boolean_t niagara_hsvc_available = B_TRUE;
static uint64_t niagara_sup_minor;		/* Supported minor number */
static hsvc_info_t niagara_hsvc = {
	HSVC_REV_1, NULL, HSVC_GROUP_NIAGARA_CPU, 1, 0, cpu_module_name
};

void
cpu_setup(void)
{
	extern int at_flags;
	extern int disable_delay_tlb_flush, delay_tlb_flush;
	extern int mmu_exported_pagesize_mask;
	extern int get_cpu_pagesizes(void);
	extern int cpc_has_overflow_intr;
	int status;

	/*
	 * Negotiate the API version for Niagara specific hypervisor
	 * services.
	 */
	status = hsvc_register(&niagara_hsvc, &niagara_sup_minor);
	if (status != 0) {
		cmn_err(CE_WARN, "%s: cannot negotiate hypervisor services "
		    "group: 0x%x major: 0x%x minor: 0x%x errno: %d\n",
		    niagara_hsvc.hsvc_modname, niagara_hsvc.hsvc_group,
		    niagara_hsvc.hsvc_major, niagara_hsvc.hsvc_minor, status);
		niagara_hsvc_available = B_FALSE;
	}

	cache |= (CACHE_PTAG | CACHE_IOCOHERENT);
	at_flags = EF_SPARC_SUN_US3 | EF_SPARC_32PLUS | EF_SPARC_SUN_US1;

	/*
	 * Use the maximum number of contexts available for Spitfire unless
	 * it has been tuned for debugging.
	 * We are checking against 0 here since this value can be patched
	 * while booting.  It can not be patched via /etc/system since it
	 * will be patched too late and thus cause the system to panic.
	 */
	if (nctxs == 0)
		nctxs = MAX_NCTXS;

	if (use_page_coloring) {
		do_pg_coloring = 1;
		if (use_virtual_coloring)
			do_virtual_coloring = 1;
	}
	/*
	 * Initalize supported page sizes information before the PD.
	 * If no information is available, then initialize the
	 * mmu_exported_pagesize_mask to a reasonable value for that processor.
	 */
	mmu_exported_pagesize_mask = get_cpu_pagesizes();
	if (mmu_exported_pagesize_mask <= 0) {
		mmu_exported_pagesize_mask = (1 << TTE8K) | (1 << TTE64K) |
		    (1 << TTE4M) | (1 << TTE256M);
	}

	/*
	 * Tune pp_slots to use up to 1/8th of the tlb entries.
	 */
	pp_slots = MIN(8, MAXPP_SLOTS);

	/*
	 * Block stores invalidate all pages of the d$ so pagecopy
	 * et. al. do not need virtual translations with virtual
	 * coloring taken into consideration.
	 */
	pp_consistent_coloring = 0;
	isa_list =
	    "sparcv9 sparcv8plus sparcv8 sparcv8-fsmuld sparcv7 "
	    "sparc sparcv9+vis sparcv9+vis2 sparcv8plus+vis sparcv8plus+vis2";

	cpu_hwcap_flags |= AV_SPARC_ASI_BLK_INIT;

	/*
	 * Niagara supports a 48-bit subset of the full 64-bit virtual
	 * address space. Virtual addresses between 0x0000800000000000
	 * and 0xffff.7fff.ffff.ffff inclusive lie within a "VA Hole"
	 * and must never be mapped. In addition, software must not use
	 * pages within 4GB of the VA hole as instruction pages to
	 * avoid problems with prefetching into the VA hole.
	 *
	 * VA hole information should be obtained from the machine
	 * description.
	 */
	hole_start = (caddr_t)(0x800000000000ul - (1ul << 32));
	hole_end = (caddr_t)(0xffff800000000000ul + (1ul << 32));

	/*
	 * The kpm mapping window.
	 * kpm_size:
	 *	The size of a single kpm range.
	 *	The overall size will be: kpm_size * vac_colors.
	 * kpm_vbase:
	 *	The virtual start address of the kpm range within the kernel
	 *	virtual address space. kpm_vbase has to be kpm_size aligned.
	 */
	kpm_size = (size_t)(2ull * 1024 * 1024 * 1024 * 1024); /* 2TB */
	kpm_size_shift = 41;
	kpm_vbase = (caddr_t)0xfffffa0000000000ull; /* 16EB - 6TB */

	/*
	 * The traptrace code uses either %tick or %stick for
	 * timestamping.  We have %stick so we can use it.
	 */
	traptrace_use_stick = 1;

	/*
	 * sun4v provides demap_all
	 */
	if (!disable_delay_tlb_flush)
		delay_tlb_flush = 1;
	/*
	 * Niagara has a performance counter overflow interrupt
	 */
	cpc_has_overflow_intr = 1;
}

#define	MB	 * 1024 * 1024
/*
 * Set the magic constants of the implementation.
 */
void
cpu_fiximp(struct cpu_node *cpunode)
{
	extern int vac_size, vac_shift;
	extern uint_t vac_mask;
	int i, a;

	/*
	 * The assumption here is that fillsysinfo will eventually
	 * have code to fill this info in from the PD.
	 * We hard code this for niagara now.
	 * Once the PD access library is done this code
	 * might need to be changed to get the info from the PD
	 */
	if (cpunode->ecache_size == 0)
		cpunode->ecache_size = 3 MB;
	if (cpunode->ecache_linesize == 0)
		cpunode->ecache_linesize = 64;
	if (cpunode->ecache_associativity == 0)
		cpunode->ecache_associativity = 12;

	cpunode->ecache_setsize =
	    cpunode->ecache_size / cpunode->ecache_associativity;

	if (ecache_setsize == 0)
		ecache_setsize = cpunode->ecache_setsize;
	if (ecache_alignsize == 0)
		ecache_alignsize = cpunode->ecache_linesize;

	vac_size = S_VAC_SIZE;
	vac_mask = MMU_PAGEMASK & (vac_size - 1);
	i = 0; a = vac_size;
	while (a >>= 1)
		++i;
	vac_shift = i;
	shm_alignment = vac_size;
	vac = 0;
}

static int niagara_cpucnt;

void
cpu_init_private(struct cpu *cp)
{
	extern int niagara_kstat_init(void);

	/*
	 * This code change assumes that the virtual cpu ids are identical
	 * to the physical cpu ids which is true for ontario but not for
	 * niagara in general.
	 * This is a temporary fix which will later be modified to obtain
	 * the execution unit sharing information from MD table.
	 */
	cp->cpu_m.cpu_ipipe = (id_t)(cp->cpu_id / 4);

	ASSERT(MUTEX_HELD(&cpu_lock));
	if (niagara_cpucnt++ == 0 && niagara_hsvc_available == B_TRUE) {
		(void) niagara_kstat_init();
	}
}

void
cpu_uninit_private(struct cpu *cp)
{
	extern int niagara_kstat_fini(void);

	ASSERT(MUTEX_HELD(&cpu_lock));
	if (--niagara_cpucnt == 0 && niagara_hsvc_available == B_TRUE) {
		(void) niagara_kstat_fini();
	}
}

/*
 * On Niagara, any flush will cause all preceding stores to be
 * synchronized wrt the i$, regardless of address or ASI.  In fact,
 * the address is ignored, so we always flush address 0.
 */
void
dtrace_flush_sec(uintptr_t addr)
{
	doflush(0);
}

#define	IS_FLOAT(i) (((i) & 0x1000000) != 0)
#define	IS_IBIT_SET(x)	(x & 0x2000)
#define	IS_VIS1(op, op3)(op == 2 && op3 == 0x36)
#define	IS_PARTIAL_OR_SHORT_FLOAT_LD_ST(op, op3, asi)		\
		(op == 3 && (op3 == IOP_V8_LDDFA ||		\
		op3 == IOP_V8_STDFA) &&	asi > ASI_SNFL)
int
vis1_partial_support(struct regs *rp, k_siginfo_t *siginfo, uint_t *fault)
{
	char *badaddr;
	int instr;
	uint_t	optype, op3, asi;
	uint_t	rd, ignor;

	if (!USERMODE(rp->r_tstate))
		return (-1);

	instr = fetch_user_instr((caddr_t)rp->r_pc);

	rd = (instr >> 25) & 0x1f;
	optype = (instr >> 30) & 0x3;
	op3 = (instr >> 19) & 0x3f;
	ignor = (instr >> 5) & 0xff;
	if (IS_IBIT_SET(instr)) {
		asi = (uint32_t)((rp->r_tstate >> TSTATE_ASI_SHIFT) &
		    TSTATE_ASI_MASK);
	} else {
		asi = ignor;
	}

	if (!IS_VIS1(optype, op3) &&
	    !IS_PARTIAL_OR_SHORT_FLOAT_LD_ST(optype, op3, asi)) {
		return (-1);
	}
	switch (simulate_unimp(rp, &badaddr)) {
	case SIMU_RETRY:
		break;	/* regs are already set up */
		/*NOTREACHED*/

	case SIMU_SUCCESS:
		/*
		 * skip the successfully
		 * simulated instruction
		 */
		rp->r_pc = rp->r_npc;
		rp->r_npc += 4;
		break;
		/*NOTREACHED*/

	case SIMU_FAULT:
		siginfo->si_signo = SIGSEGV;
		siginfo->si_code = SEGV_MAPERR;
		siginfo->si_addr = badaddr;
		*fault = FLTBOUNDS;
		break;

	case SIMU_DZERO:
		siginfo->si_signo = SIGFPE;
		siginfo->si_code = FPE_INTDIV;
		siginfo->si_addr = (caddr_t)rp->r_pc;
		*fault = FLTIZDIV;
		break;

	case SIMU_UNALIGN:
		siginfo->si_signo = SIGBUS;
		siginfo->si_code = BUS_ADRALN;
		siginfo->si_addr = badaddr;
		*fault = FLTACCESS;
		break;

	case SIMU_ILLEGAL:
	default:
		siginfo->si_signo = SIGILL;
		op3 = (instr >> 19) & 0x3F;
		if ((IS_FLOAT(instr) && (op3 == IOP_V8_STQFA) ||
		    (op3 == IOP_V8_STDFA)))
			siginfo->si_code = ILL_ILLADR;
		else
			siginfo->si_code = ILL_ILLOPC;
		siginfo->si_addr = (caddr_t)rp->r_pc;
		*fault = FLTILL;
		break;
	}
	return (0);
}

/*
 * Trapstat support for Niagara processor
 */
int
cpu_trapstat_conf(int cmd)
{
	size_t len;
	uint64_t mmustat_pa, hvret;
	int status = 0;

	if (niagara_hsvc_available == B_FALSE)
		return (ENOTSUP);

	switch (cmd) {
	case CPU_TSTATCONF_INIT:
		ASSERT(cpu_tstat_va == NULL);
		len = (NCPU+1) * sizeof (niagara_mmustat_t);
		cpu_tstat_va = contig_mem_alloc_align(len,
		    sizeof (niagara_mmustat_t));
		if (cpu_tstat_va == NULL)
			status = EAGAIN;
		else {
			bzero(cpu_tstat_va, len);
			cpu_tstat_pa = va_to_pa(cpu_tstat_va);
		}
		break;

	case CPU_TSTATCONF_FINI:
		if (cpu_tstat_va) {
			len = (NCPU+1) * sizeof (niagara_mmustat_t);
			contig_mem_free(cpu_tstat_va, len);
			cpu_tstat_va = NULL;
			cpu_tstat_pa = 0;
		}
		break;

	case CPU_TSTATCONF_ENABLE:
		hvret = hv_niagara_mmustat_conf((cpu_tstat_pa +
		    (CPU->cpu_id+1) * sizeof (niagara_mmustat_t)),
		    (uint64_t *)&mmustat_pa);
		if (hvret != H_EOK)
			status = EINVAL;
		break;

	case CPU_TSTATCONF_DISABLE:
		hvret = hv_niagara_mmustat_conf(0, (uint64_t *)&mmustat_pa);
		if (hvret != H_EOK)
			status = EINVAL;
		break;

	default:
		status = EINVAL;
		break;
	}
	return (status);
}

void
cpu_trapstat_data(void *buf, uint_t tstat_pgszs)
{
	niagara_mmustat_t	*mmustatp;
	tstat_pgszdata_t	*tstatp = (tstat_pgszdata_t *)buf;
	int	i, pgcnt;

	if (cpu_tstat_va == NULL)
		return;

	mmustatp = &((niagara_mmustat_t *)cpu_tstat_va)[CPU->cpu_id+1];
	if (tstat_pgszs > NIAGARA_MMUSTAT_PGSZS)
		tstat_pgszs = NIAGARA_MMUSTAT_PGSZS;

	for (i = 0; i < tstat_pgszs; i++, tstatp++) {
		tstatp->tpgsz_kernel.tmode_itlb.ttlb_tlb.tmiss_count =
		    mmustatp->kitsb[i].tsbhit_count;
		tstatp->tpgsz_kernel.tmode_itlb.ttlb_tlb.tmiss_time =
		    mmustatp->kitsb[i].tsbhit_time;
		tstatp->tpgsz_user.tmode_itlb.ttlb_tlb.tmiss_count =
		    mmustatp->uitsb[i].tsbhit_count;
		tstatp->tpgsz_user.tmode_itlb.ttlb_tlb.tmiss_time =
		    mmustatp->uitsb[i].tsbhit_time;
		tstatp->tpgsz_kernel.tmode_dtlb.ttlb_tlb.tmiss_count =
		    mmustatp->kdtsb[i].tsbhit_count;
		tstatp->tpgsz_kernel.tmode_dtlb.ttlb_tlb.tmiss_time =
		    mmustatp->kdtsb[i].tsbhit_time;
		tstatp->tpgsz_user.tmode_dtlb.ttlb_tlb.tmiss_count =
		    mmustatp->udtsb[i].tsbhit_count;
		tstatp->tpgsz_user.tmode_dtlb.ttlb_tlb.tmiss_time =
		    mmustatp->udtsb[i].tsbhit_time;
	}
}
