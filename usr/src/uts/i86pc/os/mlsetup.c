/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
#include <sys/disp.h>
#include <sys/promif.h>
#include <sys/clock.h>
#include <sys/cpuvar.h>
#include <sys/stack.h>
#include <vm/as.h>
#include <vm/hat.h>
#include <sys/reboot.h>
#include <sys/avintr.h>
#include <sys/vtrace.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/cpupart.h>
#include <sys/pset.h>
#include <sys/copyops.h>
#include <sys/chip.h>
#include <sys/disp.h>
#include <sys/debug.h>
#include <sys/sunddi.h>
#include <sys/x86_archext.h>
#include <sys/privregs.h>
#include <sys/machsystm.h>
#include <sys/ontrap.h>
#include <sys/bootconf.h>
#include <sys/kdi.h>
#include <sys/archsystm.h>
#include <sys/promif.h>
#include <sys/bootconf.h>
#include <sys/kobj.h>
#include <sys/kobj_lex.h>
#include <sys/pci_cfgspace.h>
#if defined(__amd64)
#include <sys/bootsvcs.h>

/*
 * XX64	This stuff deals with switching stacks in case a trapping
 *	thread wants to call back into boot -after- boot has lost track
 *	of the mappings but before the kernel owns the console.
 *
 *	(A better way to hide this would be to add a 'this' pointer to
 *	every boot syscall so that vmx could get at the resulting save
 *	area.)
 */

struct boot_syscalls *_vmx_sysp;
static struct boot_syscalls __kbootsvcs;
extern struct boot_syscalls *sysp;
extern void _stack_safe_putchar(int c);
#endif

/*
 * some globals for patching the result of cpuid
 * to solve problems w/ creative cpu vendors
 */

extern uint32_t cpuid_feature_ecx_include;
extern uint32_t cpuid_feature_ecx_exclude;
extern uint32_t cpuid_feature_edx_include;
extern uint32_t cpuid_feature_edx_exclude;

/*
 * Dummy spl priority masks
 */
static unsigned char	dummy_cpu_pri[MAXIPL + 1] = {
	0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf,
	0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf
};

/*
 * External Routines:
 */

extern void init_tables(void);


static uint32_t
cpuid_getval(char *name)
{
	char prop[32];
	u_longlong_t ll;
	extern struct bootops *bootops;
	if ((BOP_GETPROPLEN(bootops, name) > sizeof (prop)) ||
	    (BOP_GETPROP(bootops, name, prop) < 0) ||
	    (kobj_getvalue(prop, &ll) == -1))
		return (0);
	return ((uint32_t)ll);
}

/*
 * Setup routine called right before main(). Interposing this function
 * before main() allows us to call it in a machine-independent fashion.
 */
void
mlsetup(struct regs *rp)
{
	extern struct classfuncs sys_classfuncs;
	extern struct chip cpu0_chip;
	extern disp_t cpu0_disp;
	extern char t0stack[];

	ASSERT_STACK_ALIGNED();

#if defined(__amd64)

#if (BS_VERSION > 4)
	/*
	 * When new boot_syscalls are added to the vector, this routine
	 * must be modified to copy them into the kernel's copy of the
	 * vector.
	 */
#error mlsetup() must be updated for amd64 to support new boot_syscalls
#endif	/* (BS_VERSION > 4) */

	/*
	 * XX64	This remaps vmx's putchar to use the kernel's version
	 *	that switches stacks before diving into vmx
	 *	See explanation/complaints in commentary above.
	 */
	_vmx_sysp = sysp;
	sysp = &__kbootsvcs;

	sysp->bsvc_getchar = _vmx_sysp->bsvc_getchar;
	sysp->bsvc_putchar = _stack_safe_putchar;
	sysp->bsvc_ischar = _vmx_sysp->bsvc_ischar;
#endif
	/*
	 * initialize cpu_self
	 */
	cpu[0]->cpu_self = cpu[0];

	/*
	 * Set up dummy cpu_pri_data values till psm spl code is
	 * installed.  This allows splx() to work on amd64.
	 */

	cpu[0]->cpu_pri_data = dummy_cpu_pri;

	/*
	 * check if we've got special bits to clear or set
	 * when checking cpu features
	 */

	cpuid_feature_ecx_include =
	    cpuid_getval("cpuid_feature_ecx_include");
	cpuid_feature_ecx_exclude =
	    cpuid_getval("cpuid_feature_ecx_exclude");
	cpuid_feature_edx_include =
	    cpuid_getval("cpuid_feature_edx_include");
	cpuid_feature_edx_exclude =
	    cpuid_getval("cpuid_feature_edx_exclude");

	/*
	 * The first lightweight pass (pass0) through the cpuid data
	 * was done in locore before mlsetup was called.  Do the next
	 * pass in C code.
	 *
	 * The x86_feature bits are set here on the basis of the capabilities
	 * of the boot CPU.  Note that if we choose to support CPUs that have
	 * different feature sets (at which point we would almost certainly
	 * want to set the feature bits to correspond to the feature
	 * minimum) this value may be altered.
	 */

	x86_feature = cpuid_pass1(cpu[0]);

	/*
	 * Initialize idt0, gdt0, ldt0_default, ktss0 and dftss.
	 */
	init_tables();

#if defined(__amd64)
	/*CSTYLED*/
	{
		/*
		 * setup %gs for the kernel
		 */
		wrmsr(MSR_AMD_GSBASE, (uint64_t)&cpus[0]);
		/*
		 * XX64 We should never dereference off "other gsbase" or
		 * "fsbase".  So, we should arrange to point FSBASE and
		 * KGSBASE somewhere truly awful e.g. point it at the last
		 * valid address below the hole so that any attempts to index
		 * off them cause an exception.
		 *
		 * For now, point it at 8G -- at least it should be unmapped
		 * until some 64-bit processes run.
		 */
		wrmsr(MSR_AMD_FSBASE, 0x200000000UL);
		wrmsr(MSR_AMD_KGSBASE, 0x200000000UL);
	}

#elif defined(__i386)
	/*
	 * enable large page support right here.
	 */
	if (x86_feature & X86_LARGEPAGE) {
		cr4_value |= CR4_PSE;
		if (x86_feature & X86_PGE)
			cr4_value |= CR4_PGE;
		setup_121_andcall(enable_big_page_support, cr4_value);
	}

	/*
	 * Some i386 processors do not implement the rdtsc instruction,
	 * or at least they do not implement it correctly.
	 *
	 * For those that do, patch in the rdtsc instructions in
	 * various parts of the kernel right now while the text is
	 * still writable.
	 */
	if (x86_feature & X86_TSC)
		patch_tsc();
#endif

	/*
	 * initialize t0
	 */
	t0.t_stk = (caddr_t)rp - MINFRAME;
	t0.t_stkbase = t0stack;
	t0.t_pri = maxclsyspri - 3;
	t0.t_schedflag = TS_LOAD | TS_DONT_SWAP;
	t0.t_procp = &p0;
	t0.t_plockp = &p0lock.pl_lock;
	t0.t_lwp = &lwp0;
	t0.t_forw = &t0;
	t0.t_back = &t0;
	t0.t_next = &t0;
	t0.t_prev = &t0;
	t0.t_cpu = cpu[0];
	t0.t_disp_queue = &cpu0_disp;
	t0.t_bind_cpu = PBIND_NONE;
	t0.t_bind_pset = PS_NONE;
	t0.t_cpupart = &cp_default;
	t0.t_clfuncs = &sys_classfuncs.thread;
	t0.t_copyops = NULL;
	THREAD_ONPROC(&t0, CPU);

	lwp0.lwp_thread = &t0;
	lwp0.lwp_regs = (void *) rp;
	lwp0.lwp_procp = &p0;
	t0.t_tid = p0.p_lwpcnt = p0.p_lwprcnt = p0.p_lwpid = 1;

	p0.p_exec = NULL;
	p0.p_stat = SRUN;
	p0.p_flag = SSYS;
	p0.p_tlist = &t0;
	p0.p_stksize = 2*PAGESIZE;
	p0.p_stkpageszc = 0;
	p0.p_as = &kas;
	p0.p_lockp = &p0lock;
	p0.p_brkpageszc = 0;
	sigorset(&p0.p_ignore, &ignoredefault);

	CPU->cpu_thread = &t0;
	bzero(&cpu0_disp, sizeof (disp_t));
	CPU->cpu_disp = &cpu0_disp;
	CPU->cpu_disp->disp_cpu = CPU;
	CPU->cpu_dispthread = &t0;
	CPU->cpu_idle_thread = &t0;
	CPU->cpu_flags = CPU_READY | CPU_RUNNING | CPU_EXISTS | CPU_ENABLE;
	CPU->cpu_dispatch_pri = t0.t_pri;

	CPU->cpu_mask = 1;
	CPU->cpu_id = 0;

	CPU->cpu_tss = &ktss0;

	CPU->cpu_pri = 12;		/* initial PIL for the boot CPU */

	CPU->cpu_gdt = gdt0;

	/*
	 * The kernel doesn't use LDTs unless a process explicitly requests one.
	 */
	p0.p_ldt_desc = zero_sdesc;

	/*
	 * Kernel IDT.
	 */
	CPU->cpu_idt = idt0;

	/*
	 * Initialize thread/cpu microstate accounting here
	 */
	init_mstate(&t0, LMS_SYSTEM);
	init_cpu_mstate(CPU, CMS_SYSTEM);

	/*
	 * Initialize lists of available and active CPUs.
	 */
	cpu_list_init(CPU);

	cpu_vm_data_init(CPU);

	/* lgrp_init() needs PCI config space access */
	pci_cfgspace_init();

	/*
	 * Initialize the lgrp framework
	 */
	lgrp_init();

	/*
	 * The lgroup code needs to at least know about a CPU's
	 * chip association, but it's too early to fully initialize
	 * cpu0_chip, since the device node for the boot CPU doesn't
	 * exist yet. Initialize enough of it to get by until formal
	 * initialization.
	 */
	CPU->cpu_rechoose = rechoose_interval;
	CPU->cpu_chip = &cpu0_chip;

	rp->r_fp = 0;	/* terminate kernel stack traces! */

	prom_init("kernel", (void *)NULL);

	if (boothowto & RB_HALT) {
		prom_printf("unix: kernel halted by -h flag\n");
		prom_enter_mon();
	}

	ASSERT_STACK_ALIGNED();

	if (workaround_errata(CPU) != 0)
		panic("critical workaround(s) missing for boot cpu");
}
