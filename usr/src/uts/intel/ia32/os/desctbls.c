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
 * Copyright (c) 1992 Terrence R. Lambert.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)machdep.c	7.4 (Berkeley) 6/3/91
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/tss.h>
#include <sys/segments.h>
#include <sys/trap.h>
#include <sys/cpuvar.h>
#include <sys/bootconf.h>
#include <sys/x86_archext.h>
#include <sys/controlregs.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <sys/kobj.h>
#include <sys/cmn_err.h>
#include <sys/reboot.h>
#include <sys/kdi.h>
#include <sys/mach_mmu.h>
#include <sys/systm.h>
#include <sys/promif.h>
#include <sys/bootinfo.h>
#include <vm/kboot_mmu.h>

/*
 * cpu0 and default tables and structures.
 */
user_desc_t	*gdt0;
desctbr_t	gdt0_default_r;

#pragma	align	16(idt0)
gate_desc_t	idt0[NIDT]; 		/* interrupt descriptor table */
#if defined(__i386)
desctbr_t	idt0_default_r;		/* describes idt0 in IDTR format */
#endif

#pragma align	16(ktss0)
struct tss	ktss0;			/* kernel task state structure */

#if defined(__i386)
#pragma align	16(dftss0)
struct tss	dftss0;			/* #DF double-fault exception */
#endif	/* __i386 */

user_desc_t	zero_udesc;		/* base zero user desc native procs */
system_desc_t	zero_sdesc;

#if defined(__amd64)
user_desc_t	zero_u32desc;		/* 32-bit compatibility procs */
#endif	/* __amd64 */

#pragma	align	16(dblfault_stack0)
char		dblfault_stack0[DEFAULTSTKSZ];

extern void	fast_null(void);
extern hrtime_t	get_hrtime(void);
extern hrtime_t	gethrvtime(void);
extern hrtime_t	get_hrestime(void);
extern uint64_t	getlgrp(void);

void (*(fasttable[]))(void) = {
	fast_null,			/* T_FNULL routine */
	fast_null,			/* T_FGETFP routine (initially null) */
	fast_null,			/* T_FSETFP routine (initially null) */
	(void (*)())get_hrtime,		/* T_GETHRTIME */
	(void (*)())gethrvtime,		/* T_GETHRVTIME */
	(void (*)())get_hrestime,	/* T_GETHRESTIME */
	(void (*)())getlgrp		/* T_GETLGRP */
};

/*
 * Structure containing pre-computed descriptors to allow us to temporarily
 * interpose on a standard handler.
 */
struct interposing_handler {
	int ih_inum;
	gate_desc_t ih_interp_desc;
	gate_desc_t ih_default_desc;
};

/*
 * The brand infrastructure interposes on two handlers, and we use one as a
 * NULL signpost.
 */
static struct interposing_handler brand_tbl[3];

/*
 * software prototypes for default local descriptor table
 */

/*
 * Routines for loading segment descriptors in format the hardware
 * can understand.
 */

#if defined(__amd64)

/*
 * In long mode we have the new L or long mode attribute bit
 * for code segments. Only the conforming bit in type is used along
 * with descriptor priority and present bits. Default operand size must
 * be zero when in long mode. In 32-bit compatibility mode all fields
 * are treated as in legacy mode. For data segments while in long mode
 * only the present bit is loaded.
 */
void
set_usegd(user_desc_t *dp, uint_t lmode, void *base, size_t size,
    uint_t type, uint_t dpl, uint_t gran, uint_t defopsz)
{
	ASSERT(lmode == SDP_SHORT || lmode == SDP_LONG);

	/*
	 * 64-bit long mode.
	 */
	if (lmode == SDP_LONG)
		dp->usd_def32 = 0;		/* 32-bit operands only */
	else
		/*
		 * 32-bit compatibility mode.
		 */
		dp->usd_def32 = defopsz;	/* 0 = 16, 1 = 32-bit ops */

	dp->usd_long = lmode;	/* 64-bit mode */
	dp->usd_type = type;
	dp->usd_dpl = dpl;
	dp->usd_p = 1;
	dp->usd_gran = gran;		/* 0 = bytes, 1 = pages */

	dp->usd_lobase = (uintptr_t)base;
	dp->usd_midbase = (uintptr_t)base >> 16;
	dp->usd_hibase = (uintptr_t)base >> (16 + 8);
	dp->usd_lolimit = size;
	dp->usd_hilimit = (uintptr_t)size >> 16;
}

#elif defined(__i386)

/*
 * Install user segment descriptor for code and data.
 */
void
set_usegd(user_desc_t *dp, void *base, size_t size, uint_t type,
    uint_t dpl, uint_t gran, uint_t defopsz)
{
	dp->usd_lolimit = size;
	dp->usd_hilimit = (uintptr_t)size >> 16;

	dp->usd_lobase = (uintptr_t)base;
	dp->usd_midbase = (uintptr_t)base >> 16;
	dp->usd_hibase = (uintptr_t)base >> (16 + 8);

	dp->usd_type = type;
	dp->usd_dpl = dpl;
	dp->usd_p = 1;
	dp->usd_def32 = defopsz;	/* 0 = 16, 1 = 32 bit operands */
	dp->usd_gran = gran;		/* 0 = bytes, 1 = pages */
}

#endif	/* __i386 */

/*
 * Install system segment descriptor for LDT and TSS segments.
 */

#if defined(__amd64)

void
set_syssegd(system_desc_t *dp, void *base, size_t size, uint_t type,
    uint_t dpl)
{
	dp->ssd_lolimit = size;
	dp->ssd_hilimit = (uintptr_t)size >> 16;

	dp->ssd_lobase = (uintptr_t)base;
	dp->ssd_midbase = (uintptr_t)base >> 16;
	dp->ssd_hibase = (uintptr_t)base >> (16 + 8);
	dp->ssd_hi64base = (uintptr_t)base >> (16 + 8 + 8);

	dp->ssd_type = type;
	dp->ssd_zero1 = 0;	/* must be zero */
	dp->ssd_zero2 = 0;
	dp->ssd_dpl = dpl;
	dp->ssd_p = 1;
	dp->ssd_gran = 0;	/* force byte units */
}

#elif defined(__i386)

void
set_syssegd(system_desc_t *dp, void *base, size_t size, uint_t type,
    uint_t dpl)
{
	dp->ssd_lolimit = size;
	dp->ssd_hilimit = (uintptr_t)size >> 16;

	dp->ssd_lobase = (uintptr_t)base;
	dp->ssd_midbase = (uintptr_t)base >> 16;
	dp->ssd_hibase = (uintptr_t)base >> (16 + 8);

	dp->ssd_type = type;
	dp->ssd_zero = 0;	/* must be zero */
	dp->ssd_dpl = dpl;
	dp->ssd_p = 1;
	dp->ssd_gran = 0;	/* force byte units */
}

#endif	/* __i386 */

/*
 * Install gate segment descriptor for interrupt, trap, call and task gates.
 */

#if defined(__amd64)

void
set_gatesegd(gate_desc_t *dp, void (*func)(void), selector_t sel,
    uint_t type, uint_t dpl)
{
	dp->sgd_looffset = (uintptr_t)func;
	dp->sgd_hioffset = (uintptr_t)func >> 16;
	dp->sgd_hi64offset = (uintptr_t)func >> (16 + 16);

	dp->sgd_selector =  (uint16_t)sel;

	/*
	 * For 64 bit native we use the IST stack mechanism
	 * for double faults. All other traps use the CPL = 0
	 * (tss_rsp0) stack.
	 */
	if (type == T_DBLFLT)
		dp->sgd_ist = 1;
	else
		dp->sgd_ist = 0;

	dp->sgd_type = type;
	dp->sgd_dpl = dpl;
	dp->sgd_p = 1;
}

#elif defined(__i386)

void
set_gatesegd(gate_desc_t *dp, void (*func)(void), selector_t sel,
    uint_t type, uint_t dpl)
{
	dp->sgd_looffset = (uintptr_t)func;
	dp->sgd_hioffset = (uintptr_t)func >> 16;

	dp->sgd_selector =  (uint16_t)sel;
	dp->sgd_stkcpy = 0;	/* always zero bytes */
	dp->sgd_type = type;
	dp->sgd_dpl = dpl;
	dp->sgd_p = 1;
}

#endif	/* __i386 */

#if defined(__amd64)

/*
 * Build kernel GDT.
 */

static void
init_gdt_common(user_desc_t *gdt)
{
	int i;

	/*
	 * 64-bit kernel code segment.
	 */
	set_usegd(&gdt[GDT_KCODE], SDP_LONG, NULL, 0, SDT_MEMERA, SEL_KPL,
	    SDP_PAGES, SDP_OP32);

	/*
	 * 64-bit kernel data segment. The limit attribute is ignored in 64-bit
	 * mode, but we set it here to 0xFFFF so that we can use the SYSRET
	 * instruction to return from system calls back to 32-bit applications.
	 * SYSRET doesn't update the base, limit, or attributes of %ss or %ds
	 * descriptors. We therefore must ensure that the kernel uses something,
	 * though it will be ignored by hardware, that is compatible with 32-bit
	 * apps. For the same reason we must set the default op size of this
	 * descriptor to 32-bit operands.
	 */
	set_usegd(&gdt[GDT_KDATA], SDP_LONG, NULL, -1, SDT_MEMRWA,
	    SEL_KPL, SDP_PAGES, SDP_OP32);
	gdt[GDT_KDATA].usd_def32 = 1;

	/*
	 * 64-bit user code segment.
	 */
	set_usegd(&gdt[GDT_UCODE], SDP_LONG, NULL, 0, SDT_MEMERA, SEL_UPL,
	    SDP_PAGES, SDP_OP32);

	/*
	 * 32-bit user code segment.
	 */
	set_usegd(&gdt[GDT_U32CODE], SDP_SHORT, NULL, -1, SDT_MEMERA,
	    SEL_UPL, SDP_PAGES, SDP_OP32);

	/*
	 * 32 and 64 bit data segments can actually share the same descriptor.
	 * In long mode only the present bit is checked but all other fields
	 * are loaded. But in compatibility mode all fields are interpreted
	 * as in legacy mode so they must be set correctly for a 32-bit data
	 * segment.
	 */
	set_usegd(&gdt[GDT_UDATA], SDP_SHORT, NULL, -1, SDT_MEMRWA, SEL_UPL,
	    SDP_PAGES, SDP_OP32);

	/*
	 * The 64-bit kernel has no default LDT. By default, the LDT descriptor
	 * in the GDT is 0.
	 */

	/*
	 * Kernel TSS
	 */
	set_syssegd((system_desc_t *)&gdt[GDT_KTSS], &ktss0,
	    sizeof (ktss0) - 1, SDT_SYSTSS, SEL_KPL);

	/*
	 * Initialize fs and gs descriptors for 32 bit processes.
	 * Only attributes and limits are initialized, the effective
	 * base address is programmed via fsbase/gsbase.
	 */
	set_usegd(&gdt[GDT_LWPFS], SDP_SHORT, NULL, -1, SDT_MEMRWA,
	    SEL_UPL, SDP_PAGES, SDP_OP32);
	set_usegd(&gdt[GDT_LWPGS], SDP_SHORT, NULL, -1, SDT_MEMRWA,
	    SEL_UPL, SDP_PAGES, SDP_OP32);

	/*
	 * Initialize the descriptors set aside for brand usage.
	 * Only attributes and limits are initialized.
	 */
	for (i = GDT_BRANDMIN; i <= GDT_BRANDMAX; i++)
		set_usegd(&gdt0[i], SDP_SHORT, NULL, -1, SDT_MEMRWA,
		    SEL_UPL, SDP_PAGES, SDP_OP32);

	/*
	 * Initialize convenient zero base user descriptors for clearing
	 * lwp private %fs and %gs descriptors in GDT. See setregs() for
	 * an example.
	 */
	set_usegd(&zero_udesc, SDP_LONG, 0, 0, SDT_MEMRWA, SEL_UPL,
	    SDP_BYTES, SDP_OP32);
	set_usegd(&zero_u32desc, SDP_SHORT, 0, -1, SDT_MEMRWA, SEL_UPL,
	    SDP_PAGES, SDP_OP32);
}

static user_desc_t *
init_gdt(void)
{
	desctbr_t	r_bgdt, r_gdt;
	user_desc_t	*bgdt;

#if !defined(__lint)
	/*
	 * Our gdt is never larger than a single page.
	 */
	ASSERT((sizeof (*gdt0) * NGDT) <= PAGESIZE);
#endif
	gdt0 = (user_desc_t *)BOP_ALLOC(bootops, (caddr_t)GDT_VA,
	    PAGESIZE, PAGESIZE);
	if (gdt0 == NULL)
		panic("init_gdt: BOP_ALLOC failed");
	bzero(gdt0, PAGESIZE);

	init_gdt_common(gdt0);

	/*
	 * Copy in from boot's gdt to our gdt.
	 * Entry 0 is the null descriptor by definition.
	 */
	rd_gdtr(&r_bgdt);
	bgdt = (user_desc_t *)r_bgdt.dtr_base;
	if (bgdt == NULL)
		panic("null boot gdt");

	gdt0[GDT_B32DATA] = bgdt[GDT_B32DATA];
	gdt0[GDT_B32CODE] = bgdt[GDT_B32CODE];
	gdt0[GDT_B16CODE] = bgdt[GDT_B16CODE];
	gdt0[GDT_B16DATA] = bgdt[GDT_B16DATA];
	gdt0[GDT_B64CODE] = bgdt[GDT_B64CODE];

	/*
	 * Install our new GDT
	 */
	r_gdt.dtr_limit = (sizeof (*gdt0) * NGDT) - 1;
	r_gdt.dtr_base = (uintptr_t)gdt0;
	wr_gdtr(&r_gdt);

	/*
	 * Reload the segment registers to use the new GDT
	 */
	load_segment_registers(KCS_SEL, KFS_SEL, KGS_SEL, KDS_SEL);

	/*
	 *  setup %gs for kernel
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
	wrmsr(MSR_AMD_FSBASE, 0x200000000ul);
	wrmsr(MSR_AMD_KGSBASE, 0x200000000ul);
	return (gdt0);
}

#elif defined(__i386)

static void
init_gdt_common(user_desc_t *gdt)
{
	int i;

	/*
	 * Text and data for both kernel and user span entire 32 bit
	 * address space.
	 */

	/*
	 * kernel code segment.
	 */
	set_usegd(&gdt[GDT_KCODE], NULL, -1, SDT_MEMERA, SEL_KPL, SDP_PAGES,
	    SDP_OP32);

	/*
	 * kernel data segment.
	 */
	set_usegd(&gdt[GDT_KDATA], NULL, -1, SDT_MEMRWA, SEL_KPL, SDP_PAGES,
	    SDP_OP32);

	/*
	 * user code segment.
	 */
	set_usegd(&gdt[GDT_UCODE], NULL, -1, SDT_MEMERA, SEL_UPL, SDP_PAGES,
	    SDP_OP32);

	/*
	 * user data segment.
	 */
	set_usegd(&gdt[GDT_UDATA], NULL, -1, SDT_MEMRWA, SEL_UPL, SDP_PAGES,
	    SDP_OP32);

	/*
	 * TSS for T_DBLFLT (double fault) handler
	 */
	set_syssegd((system_desc_t *)&gdt[GDT_DBFLT], &dftss0,
	    sizeof (dftss0) - 1, SDT_SYSTSS, SEL_KPL);

	/*
	 * TSS for kernel
	 */
	set_syssegd((system_desc_t *)&gdt[GDT_KTSS], &ktss0,
	    sizeof (ktss0) - 1, SDT_SYSTSS, SEL_KPL);

	/*
	 * %gs selector for kernel
	 */
	set_usegd(&gdt[GDT_GS], &cpus[0], sizeof (struct cpu) -1, SDT_MEMRWA,
	    SEL_KPL, SDP_BYTES, SDP_OP32);

	/*
	 * Initialize lwp private descriptors.
	 * Only attributes and limits are initialized, the effective
	 * base address is programmed via fsbase/gsbase.
	 */
	set_usegd(&gdt[GDT_LWPFS], NULL, (size_t)-1, SDT_MEMRWA, SEL_UPL,
	    SDP_PAGES, SDP_OP32);
	set_usegd(&gdt[GDT_LWPGS], NULL, (size_t)-1, SDT_MEMRWA, SEL_UPL,
	    SDP_PAGES, SDP_OP32);

	/*
	 * Initialize the descriptors set aside for brand usage.
	 * Only attributes and limits are initialized.
	 */
	for (i = GDT_BRANDMIN; i <= GDT_BRANDMAX; i++)
		set_usegd(&gdt0[i], NULL, (size_t)-1, SDT_MEMRWA, SEL_UPL,
		    SDP_PAGES, SDP_OP32);
	/*
	 * Initialize convenient zero base user descriptor for clearing
	 * lwp  private %fs and %gs descriptors in GDT. See setregs() for
	 * an example.
	 */
	set_usegd(&zero_udesc, NULL, -1, SDT_MEMRWA, SEL_UPL,
	    SDP_BYTES, SDP_OP32);
}

static user_desc_t *
init_gdt(void)
{
	desctbr_t	r_bgdt, r_gdt;
	user_desc_t	*bgdt;

#if !defined(__lint)
	/*
	 * Our gdt is never larger than a single page.
	 */
	ASSERT((sizeof (*gdt0) * NGDT) <= PAGESIZE);
#endif
	/*
	 * XXX this allocation belongs in our caller, not here.
	 */
	gdt0 = (user_desc_t *)BOP_ALLOC(bootops, (caddr_t)GDT_VA,
	    PAGESIZE, PAGESIZE);
	if (gdt0 == NULL)
		panic("init_gdt: BOP_ALLOC failed");
	bzero(gdt0, PAGESIZE);

	init_gdt_common(gdt0);

	/*
	 * Copy in from boot's gdt to our gdt entries.
	 * Entry 0 is null descriptor by definition.
	 */
	rd_gdtr(&r_bgdt);
	bgdt = (user_desc_t *)r_bgdt.dtr_base;
	if (bgdt == NULL)
		panic("null boot gdt");

	gdt0[GDT_B32DATA] = bgdt[GDT_B32DATA];
	gdt0[GDT_B32CODE] = bgdt[GDT_B32CODE];
	gdt0[GDT_B16CODE] = bgdt[GDT_B16CODE];
	gdt0[GDT_B16DATA] = bgdt[GDT_B16DATA];

	/*
	 * Install our new GDT
	 */
	r_gdt.dtr_limit = (sizeof (*gdt0) * NGDT) - 1;
	r_gdt.dtr_base = (uintptr_t)gdt0;
	wr_gdtr(&r_gdt);

	/*
	 * Reload the segment registers to use the new GDT
	 */
	load_segment_registers(
	    KCS_SEL, KDS_SEL, KDS_SEL, KFS_SEL, KGS_SEL, KDS_SEL);

	return (gdt0);
}

#endif	/* __i386 */

/*
 * Build kernel IDT.
 *
 * Note that for amd64 we pretty much require every gate to be an interrupt
 * gate which blocks interrupts atomically on entry; that's because of our
 * dependency on using 'swapgs' every time we come into the kernel to find
 * the cpu structure. If we get interrupted just before doing that, %cs could
 * be in kernel mode (so that the trap prolog doesn't do a swapgs), but
 * %gsbase is really still pointing at something in userland. Bad things will
 * ensue. We also use interrupt gates for i386 as well even though this is not
 * required for some traps.
 *
 * Perhaps they should have invented a trap gate that does an atomic swapgs?
 */
static void
init_idt_common(gate_desc_t *idt)
{
	set_gatesegd(&idt[T_ZERODIV], &div0trap, KCS_SEL, SDT_SYSIGT, SEL_KPL);
	set_gatesegd(&idt[T_SGLSTP], &dbgtrap, KCS_SEL, SDT_SYSIGT, SEL_KPL);
	set_gatesegd(&idt[T_NMIFLT], &nmiint, KCS_SEL, SDT_SYSIGT, SEL_KPL);
	set_gatesegd(&idt[T_BPTFLT], &brktrap, KCS_SEL, SDT_SYSIGT, SEL_UPL);
	set_gatesegd(&idt[T_OVFLW], &ovflotrap, KCS_SEL, SDT_SYSIGT, SEL_UPL);
	set_gatesegd(&idt[T_BOUNDFLT], &boundstrap, KCS_SEL, SDT_SYSIGT,
	    SEL_KPL);
	set_gatesegd(&idt[T_ILLINST], &invoptrap, KCS_SEL, SDT_SYSIGT, SEL_KPL);
	set_gatesegd(&idt[T_NOEXTFLT], &ndptrap,  KCS_SEL, SDT_SYSIGT, SEL_KPL);

	/*
	 * double fault handler.
	 */
#if defined(__amd64)
	set_gatesegd(&idt[T_DBLFLT], &syserrtrap, KCS_SEL, SDT_SYSIGT, SEL_KPL);
#elif defined(__i386)
	/*
	 * task gate required.
	 */
	set_gatesegd(&idt[T_DBLFLT], NULL, DFTSS_SEL, SDT_SYSTASKGT, SEL_KPL);

#endif	/* __i386 */

	/*
	 * T_EXTOVRFLT coprocessor-segment-overrun not supported.
	 */

	set_gatesegd(&idt[T_TSSFLT], &invtsstrap, KCS_SEL, SDT_SYSIGT, SEL_KPL);
	set_gatesegd(&idt[T_SEGFLT], &segnptrap, KCS_SEL, SDT_SYSIGT, SEL_KPL);
	set_gatesegd(&idt[T_STKFLT], &stktrap, KCS_SEL, SDT_SYSIGT, SEL_KPL);
	set_gatesegd(&idt[T_GPFLT], &gptrap, KCS_SEL, SDT_SYSIGT, SEL_KPL);
	set_gatesegd(&idt[T_PGFLT], &pftrap, KCS_SEL, SDT_SYSIGT, SEL_KPL);
	set_gatesegd(&idt[T_EXTERRFLT], &ndperr, KCS_SEL, SDT_SYSIGT, SEL_KPL);
	set_gatesegd(&idt[T_ALIGNMENT], &achktrap, KCS_SEL, SDT_SYSIGT,
	    SEL_KPL);
	set_gatesegd(&idt[T_MCE], &mcetrap, KCS_SEL, SDT_SYSIGT, SEL_KPL);
	set_gatesegd(&idt[T_SIMDFPE], &xmtrap, KCS_SEL, SDT_SYSIGT, SEL_KPL);

	/*
	 * install "int80" handler at, well, 0x80.
	 */
	set_gatesegd(&idt0[T_INT80], &sys_int80, KCS_SEL, SDT_SYSIGT, SEL_UPL);

	/*
	 * install fast trap handler at 210.
	 */
	set_gatesegd(&idt[T_FASTTRAP], &fasttrap, KCS_SEL, SDT_SYSIGT, SEL_UPL);

	/*
	 * System call handler.
	 */
#if defined(__amd64)
	set_gatesegd(&idt[T_SYSCALLINT], &sys_syscall_int, KCS_SEL, SDT_SYSIGT,
	    SEL_UPL);

#elif defined(__i386)
	set_gatesegd(&idt[T_SYSCALLINT], &sys_call, KCS_SEL, SDT_SYSIGT,
	    SEL_UPL);
#endif	/* __i386 */

	/*
	 * Install the DTrace interrupt handler for the pid provider.
	 */
	set_gatesegd(&idt[T_DTRACE_RET], &dtrace_ret, KCS_SEL,
	    SDT_SYSIGT, SEL_UPL);

	/*
	 * Prepare interposing descriptors for the branded "int80"
	 * and syscall handlers and cache copies of the default
	 * descriptors.
	 */
	brand_tbl[0].ih_inum = T_INT80;
	brand_tbl[0].ih_default_desc = idt0[T_INT80];
	set_gatesegd(&(brand_tbl[0].ih_interp_desc), &brand_sys_int80, KCS_SEL,
	    SDT_SYSIGT, SEL_UPL);

	brand_tbl[1].ih_inum = T_SYSCALLINT;
	brand_tbl[1].ih_default_desc = idt0[T_SYSCALLINT];

#if defined(__amd64)
	set_gatesegd(&(brand_tbl[1].ih_interp_desc), &brand_sys_syscall_int,
	    KCS_SEL, SDT_SYSIGT, SEL_UPL);
#elif defined(__i386)
	set_gatesegd(&(brand_tbl[1].ih_interp_desc), &brand_sys_call,
	    KCS_SEL, SDT_SYSIGT, SEL_UPL);
#endif	/* __i386 */

	brand_tbl[2].ih_inum = 0;
}

static void
init_idt(gate_desc_t *idt)
{
	char	ivctname[80];
	void	(*ivctptr)(void);
	int	i;

	/*
	 * Initialize entire table with 'reserved' trap and then overwrite
	 * specific entries. T_EXTOVRFLT (9) is unsupported and reserved
	 * since it can only be generated on a 386 processor. 15 is also
	 * unsupported and reserved.
	 */
	for (i = 0; i < NIDT; i++)
		set_gatesegd(&idt[i], &resvtrap, KCS_SEL, SDT_SYSIGT, SEL_KPL);

	/*
	 * 20-31 reserved
	 */
	for (i = 20; i < 32; i++)
		set_gatesegd(&idt[i], &invaltrap, KCS_SEL, SDT_SYSIGT, SEL_KPL);

	/*
	 * interrupts 32 - 255
	 */
	for (i = 32; i < 256; i++) {
		(void) snprintf(ivctname, sizeof (ivctname), "ivct%d", i);
		ivctptr = (void (*)(void))kobj_getsymvalue(ivctname, 0);
		if (ivctptr == NULL)
			panic("kobj_getsymvalue(%s) failed", ivctname);

		set_gatesegd(&idt[i], ivctptr, KCS_SEL, SDT_SYSIGT, SEL_KPL);
	}

	/*
	 * Now install the common ones. Note that it will overlay some
	 * entries installed above like T_SYSCALLINT, T_FASTTRAP etc.
	 */
	init_idt_common(idt);
}

/*
 * The kernel does not deal with LDTs unless a user explicitly creates
 * one. Under normal circumstances, the LDTR contains 0. Any process attempting
 * to reference the LDT will therefore cause a #gp. System calls made via the
 * obsolete lcall mechanism are emulated by the #gp fault handler.
 */
static void
init_ldt(void)
{
	wr_ldtr(0);
}

#if defined(__amd64)

static void
init_tss(void)
{
	/*
	 * tss_rsp0 is dynamically filled in by resume() on each context switch.
	 * All exceptions but #DF will run on the thread stack.
	 * Set up the double fault stack here.
	 */
	ktss0.tss_ist1 =
	    (uint64_t)&dblfault_stack0[sizeof (dblfault_stack0)];

	/*
	 * Set I/O bit map offset equal to size of TSS segment limit
	 * for no I/O permission map. This will force all user I/O
	 * instructions to generate #gp fault.
	 */
	ktss0.tss_bitmapbase = sizeof (ktss0);

	/*
	 * Point %tr to descriptor for ktss0 in gdt.
	 */
	wr_tsr(KTSS_SEL);
}

#elif defined(__i386)

static void
init_tss(void)
{
	/*
	 * ktss0.tss_esp dynamically filled in by resume() on each
	 * context switch.
	 */
	ktss0.tss_ss0	= KDS_SEL;
	ktss0.tss_eip	= (uint32_t)_start;
	ktss0.tss_ds	= ktss0.tss_es = ktss0.tss_ss = KDS_SEL;
	ktss0.tss_cs	= KCS_SEL;
	ktss0.tss_fs	= KFS_SEL;
	ktss0.tss_gs	= KGS_SEL;
	ktss0.tss_ldt	= ULDT_SEL;

	/*
	 * Initialize double fault tss.
	 */
	dftss0.tss_esp0	= (uint32_t)&dblfault_stack0[sizeof (dblfault_stack0)];
	dftss0.tss_ss0	= KDS_SEL;

	/*
	 * tss_cr3 will get initialized in hat_kern_setup() once our page
	 * tables have been setup.
	 */
	dftss0.tss_eip	= (uint32_t)syserrtrap;
	dftss0.tss_esp	= (uint32_t)&dblfault_stack0[sizeof (dblfault_stack0)];
	dftss0.tss_cs	= KCS_SEL;
	dftss0.tss_ds	= KDS_SEL;
	dftss0.tss_es	= KDS_SEL;
	dftss0.tss_ss	= KDS_SEL;
	dftss0.tss_fs	= KFS_SEL;
	dftss0.tss_gs	= KGS_SEL;

	/*
	 * Set I/O bit map offset equal to size of TSS segment limit
	 * for no I/O permission map. This will force all user I/O
	 * instructions to generate #gp fault.
	 */
	ktss0.tss_bitmapbase = sizeof (ktss0);

	/*
	 * Point %tr to descriptor for ktss0 in gdt.
	 */
	wr_tsr(KTSS_SEL);
}

#endif	/* __i386 */

void
init_desctbls(void)
{
	user_desc_t *gdt;
	desctbr_t idtr;

	/*
	 * Setup and install our GDT.
	 */
	gdt = init_gdt();
	ASSERT(IS_P2ALIGNED((uintptr_t)gdt, PAGESIZE));
	CPU->cpu_m.mcpu_gdt = gdt;

	/*
	 * Setup and install our IDT.
	 */
	init_idt(&idt0[0]);

	idtr.dtr_base = (uintptr_t)idt0;
	idtr.dtr_limit = sizeof (idt0) - 1;
	wr_idtr(&idtr);
	CPU->cpu_m.mcpu_idt = idt0;

#if defined(__i386)
	/*
	 * We maintain a description of idt0 in convenient IDTR format
	 * for #pf's on some older pentium processors. See pentium_pftrap().
	 */
	idt0_default_r = idtr;
#endif	/* __i386 */

	init_tss();
	CPU->cpu_tss = &ktss0;
	init_ldt();
}

/*
 * In the early kernel, we need to set up a simple GDT to run on.
 */
void
init_boot_gdt(user_desc_t *bgdt)
{
#if defined(__amd64)
	set_usegd(&bgdt[GDT_B32DATA], SDP_LONG, NULL, -1, SDT_MEMRWA, SEL_KPL,
	    SDP_PAGES, SDP_OP32);
	set_usegd(&bgdt[GDT_B64CODE], SDP_LONG, NULL, -1, SDT_MEMERA, SEL_KPL,
	    SDP_PAGES, SDP_OP32);
#elif defined(__i386)
	set_usegd(&bgdt[GDT_B32DATA], NULL, -1, SDT_MEMRWA, SEL_KPL,
	    SDP_PAGES, SDP_OP32);
	set_usegd(&bgdt[GDT_B32CODE], NULL, -1, SDT_MEMERA, SEL_KPL,
	    SDP_PAGES, SDP_OP32);
#endif	/* __i386 */
}

/*
 * Enable interpositioning on the system call path by rewriting the
 * sys{call|enter} MSRs and the syscall-related entries in the IDT to use
 * the branded entry points.
 */
void
brand_interpositioning_enable(void)
{
	int i;

	for (i = 0; brand_tbl[i].ih_inum; i++)
		CPU->cpu_idt[brand_tbl[i].ih_inum] =
		    brand_tbl[i].ih_interp_desc;

#if defined(__amd64)
	wrmsr(MSR_AMD_LSTAR, (uintptr_t)brand_sys_syscall);
	wrmsr(MSR_AMD_CSTAR, (uintptr_t)brand_sys_syscall32);
#endif

	if (x86_feature & X86_SEP)
		wrmsr(MSR_INTC_SEP_EIP, (uintptr_t)brand_sys_sysenter);
}

/*
 * Disable interpositioning on the system call path by rewriting the
 * sys{call|enter} MSRs and the syscall-related entries in the IDT to use
 * the standard entry points, which bypass the interpositioning hooks.
 */
void
brand_interpositioning_disable(void)
{
	int i;

	for (i = 0; brand_tbl[i].ih_inum; i++)
		CPU->cpu_idt[brand_tbl[i].ih_inum] =
		    brand_tbl[i].ih_default_desc;

#if defined(__amd64)
	wrmsr(MSR_AMD_LSTAR, (uintptr_t)sys_syscall);
	wrmsr(MSR_AMD_CSTAR, (uintptr_t)sys_syscall32);
#endif

	if (x86_feature & X86_SEP)
		wrmsr(MSR_INTC_SEP_EIP, (uintptr_t)sys_sysenter);
}
