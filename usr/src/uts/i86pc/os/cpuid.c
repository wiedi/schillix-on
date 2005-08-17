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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Various routines to handle identification
 * and classification of x86 processors.
 */

#include <sys/types.h>
#include <sys/archsystm.h>
#include <sys/x86_archext.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/cpuvar.h>
#include <sys/processor.h>
#include <sys/chip.h>
#include <sys/fp.h>
#include <sys/controlregs.h>
#include <sys/auxv_386.h>
#include <sys/bitmap.h>
#include <sys/controlregs.h>
#include <sys/memnode.h>

/*
 * Pass 0 of cpuid feature analysis happens in locore. It contains special code
 * to recognize Cyrix processors that are not cpuid-compliant, and to deal with
 * them accordingly. For most modern processors, feature detection occurs here
 * in pass 1.
 *
 * Pass 1 of cpuid feature analysis happens just at the beginning of mlsetup()
 * for the boot CPU and does the basic analysis that the early kernel needs.
 * x86_feature is set based on the return value of cpuid_pass1() of the boot
 * CPU.
 *
 * Pass 1 includes:
 *
 *	o Determining vendor/model/family/stepping and setting x86_type and
 *	  x86_vendor accordingly.
 *	o Processing the feature flags returned by the cpuid instruction while
 *	  applying any workarounds or tricks for the specific processor.
 *	o Mapping the feature flags into Solaris feature bits (X86_*).
 *	o Processing extended feature flags if supported by the processor,
 *	  again while applying specific processor knowledge.
 *	o Determining the CMT characteristics of the system.
 *
 * Pass 1 is done on non-boot CPUs during their initialization and the results
 * are used only as a meager attempt at ensuring that all processors within the
 * system support the same features.
 *
 * Pass 2 of cpuid feature analysis happens just at the beginning
 * of startup().  It just copies in and corrects the remainder
 * of the cpuid data we depend on: standard cpuid functions that we didn't
 * need for pass1 feature analysis, and extended cpuid functions beyond the
 * simple feature processing done in pass1.
 *
 * Pass 3 of cpuid analysis is invoked after basic kernel services; in
 * particular kernel memory allocation has been made available. It creates a
 * readable brand string based on the data collected in the first two passes.
 *
 * Pass 4 of cpuid analysis is invoked after post_startup() when all
 * the support infrastructure for various hardware features has been
 * initialized. It determines which processor features will be reported
 * to userland via the aux vector.
 *
 * All passes are executed on all CPUs, but only the boot CPU determines what
 * features the kernel will use.
 *
 * Much of the worst junk in this file is for the support of processors
 * that didn't really implement the cpuid instruction properly.
 *
 * NOTE: The accessor functions (cpuid_get*) are aware of, and ASSERT upon,
 * the pass numbers.  Accordingly, changes to the pass code may require changes
 * to the accessor code.
 */

uint_t x86_feature = 0;
uint_t x86_vendor = X86_VENDOR_IntelClone;
uint_t x86_type = X86_TYPE_OTHER;

ulong_t cr4_value;
uint_t pentiumpro_bug4046376;
uint_t pentiumpro_bug4064495;

uint_t enable486;

/*
 * This set of strings are for processors rumored to support the cpuid
 * instruction, and is used by locore.s to figure out how to set x86_vendor
 */
const char CyrixInstead[] = "CyrixInstead";

struct cpuidr {
	uint32_t	cp_eax;
	uint32_t	cp_ebx;
	uint32_t	cp_ecx;
	uint32_t	cp_edx;
};

/*
 * These constants determine how many of the elements of the
 * cpuid we cache in the cpuid_info data structure; the
 * remaining elements are accessible via the cpuid instruction.
 */

#define	NMAX_CPI_STD	6		/* eax = 0 .. 5 */
#define	NMAX_CPI_EXTD	9		/* eax = 0x80000000 .. 0x80000008 */

struct cpuid_info {
	uint_t cpi_pass;		/* last pass completed */
	/*
	 * standard function information
	 */
	uint_t cpi_maxeax;		/* fn 0: %eax */
	char cpi_vendorstr[13];		/* fn 0: %ebx:%ecx:%edx */
	uint_t cpi_vendor;		/* enum of cpi_vendorstr */

	uint_t cpi_family;		/* fn 1: extended family */
	uint_t cpi_model;		/* fn 1: extended model */
	uint_t cpi_step;		/* fn 1: stepping */
	chipid_t cpi_chipid;		/* fn 1: %ebx: chip # on ht cpus */
	uint_t cpi_brandid;		/* fn 1: %ebx: brand ID */
	int cpi_clogid;			/* fn 1: %ebx: thread # */
	uint_t cpi_ncpu_per_chip;

	uint8_t cpi_cacheinfo[16];	/* fn 2: intel-style cache desc */
	uint_t cpi_ncache;		/* fn 2: number of elements */
	struct cpuidr cpi_std[NMAX_CPI_STD];	/* 0 .. 5 */
	/*
	 * extended function information
	 */
	uint_t cpi_xmaxeax;		/* fn 0x80000000: %eax */
	char cpi_brandstr[49];		/* fn 0x8000000[234] */
	uint8_t cpi_pabits;		/* fn 0x80000006: %eax */
	uint8_t cpi_vabits;		/* fn 0x80000006: %eax */
	struct cpuidr cpi_extd[NMAX_CPI_EXTD];	/* 0x80000000 .. 0x80000008 */
	/*
	 * supported feature information
	 */
	uint32_t cpi_support[4];
#define	STD_EDX_FEATURES	0
#define	AMD_EDX_FEATURES	1
#define	TM_EDX_FEATURES		2
#define	STD_ECX_FEATURES	3

};


static struct cpuid_info cpuid_info0;

/*
 * These bit fields are defined by the Intel Application Note AP-485
 * "Intel Processor Identification and the CPUID Instruction"
 */
#define	CPI_FAMILY_XTD(cpi)	BITX((cpi)->cpi_std[1].cp_eax, 27, 20)
#define	CPI_MODEL_XTD(cpi)	BITX((cpi)->cpi_std[1].cp_eax, 19, 16)
#define	CPI_TYPE(cpi)		BITX((cpi)->cpi_std[1].cp_eax, 13, 12)
#define	CPI_FAMILY(cpi)		BITX((cpi)->cpi_std[1].cp_eax, 11, 8)
#define	CPI_STEP(cpi)		BITX((cpi)->cpi_std[1].cp_eax, 3, 0)
#define	CPI_MODEL(cpi)		BITX((cpi)->cpi_std[1].cp_eax, 7, 4)

#define	CPI_FEATURES_EDX(cpi)		((cpi)->cpi_std[1].cp_edx)
#define	CPI_FEATURES_ECX(cpi)		((cpi)->cpi_std[1].cp_ecx)
#define	CPI_FEATURES_XTD_EDX(cpi)	((cpi)->cpi_extd[1].cp_edx)
#define	CPI_FEATURES_XTD_ECX(cpi)	((cpi)->cpi_extd[1].cp_ecx)

#define	CPI_BRANDID(cpi)	BITX((cpi)->cpi_std[1].cp_ebx, 7, 0)
#define	CPI_CHUNKS(cpi)		BITX((cpi)->cpi_std[1].cp_ebx, 15, 7)
#define	CPI_CPU_COUNT(cpi)	BITX((cpi)->cpi_std[1].cp_ebx, 23, 16)
#define	CPI_APIC_ID(cpi)	BITX((cpi)->cpi_std[1].cp_ebx, 31, 24)

#define	CPI_MAXEAX_MAX		0x100		/* sanity control */
#define	CPI_XMAXEAX_MAX		0x80000100

/*
 *  Some undocumented ways of patching the results of the cpuid
 *  instruction to permit running Solaris 10 on future cpus that
 *  we don't currently support.  Could be set to non-zero values
 *  via settings in eeprom.
 */

uint32_t cpuid_feature_ecx_include;
uint32_t cpuid_feature_ecx_exclude;
uint32_t cpuid_feature_edx_include;
uint32_t cpuid_feature_edx_exclude;

uint_t
cpuid_pass1(cpu_t *cpu)
{
	uint32_t mask_ecx, mask_edx;
	uint_t feature = X86_CPUID;
	struct cpuid_info *cpi;
	struct cpuidr *cp;
	int xcpuid;

	/*
	 * By convention, cpu0 is the boot cpu, which is called
	 * before memory allocation is available.  Other cpus are
	 * initialized when memory becomes available.
	 */
	if (cpu->cpu_id == 0)
		cpu->cpu_m.mcpu_cpi = cpi = &cpuid_info0;
	else
		cpu->cpu_m.mcpu_cpi = cpi =
		    kmem_zalloc(sizeof (*cpi), KM_SLEEP);

	cp = &cpi->cpi_std[0];
	cp->cp_eax = __cpuid_insn(0, &cp->cp_ebx, &cp->cp_ecx, &cp->cp_edx);
	cpi->cpi_maxeax = cp->cp_eax;
	{
		uint32_t *iptr = (uint32_t *)cpi->cpi_vendorstr;
		*iptr++ = cp->cp_ebx;
		*iptr++ = cp->cp_edx;
		*iptr++ = cp->cp_ecx;
		*(char *)&cpi->cpi_vendorstr[12] = '\0';
	}

	/*
	 * Map the vendor string to a type code
	 */
	if (strcmp(cpi->cpi_vendorstr, "GenuineIntel") == 0)
		cpi->cpi_vendor = X86_VENDOR_Intel;
	else if (strcmp(cpi->cpi_vendorstr, "AuthenticAMD") == 0)
		cpi->cpi_vendor = X86_VENDOR_AMD;
	else if (strcmp(cpi->cpi_vendorstr, "GenuineTMx86") == 0)
		cpi->cpi_vendor = X86_VENDOR_TM;
	else if (strcmp(cpi->cpi_vendorstr, CyrixInstead) == 0)
		/*
		 * CyrixInstead is a variable used by the Cyrix detection code
		 * in locore.
		 */
		cpi->cpi_vendor = X86_VENDOR_Cyrix;
	else if (strcmp(cpi->cpi_vendorstr, "UMC UMC UMC ") == 0)
		cpi->cpi_vendor = X86_VENDOR_UMC;
	else if (strcmp(cpi->cpi_vendorstr, "NexGenDriven") == 0)
		cpi->cpi_vendor = X86_VENDOR_NexGen;
	else if (strcmp(cpi->cpi_vendorstr, "CentaurHauls") == 0)
		cpi->cpi_vendor = X86_VENDOR_Centaur;
	else if (strcmp(cpi->cpi_vendorstr, "RiseRiseRise") == 0)
		cpi->cpi_vendor = X86_VENDOR_Rise;
	else if (strcmp(cpi->cpi_vendorstr, "SiS SiS SiS ") == 0)
		cpi->cpi_vendor = X86_VENDOR_SiS;
	else if (strcmp(cpi->cpi_vendorstr, "Geode by NSC") == 0)
		cpi->cpi_vendor = X86_VENDOR_NSC;
	else
		cpi->cpi_vendor = X86_VENDOR_IntelClone;

	x86_vendor = cpi->cpi_vendor; /* for compatibility */

	/*
	 * Limit the range in case of weird hardware
	 */
	if (cpi->cpi_maxeax > CPI_MAXEAX_MAX)
		cpi->cpi_maxeax = CPI_MAXEAX_MAX;
	if (cpi->cpi_maxeax < 1)
		goto pass1_done;

	cp = &cpi->cpi_std[1];
	cp->cp_eax = __cpuid_insn(1, &cp->cp_ebx, &cp->cp_ecx, &cp->cp_edx);

	/*
	 * Extract identifying constants for easy access.
	 */
	cpi->cpi_model = CPI_MODEL(cpi);
	cpi->cpi_family = CPI_FAMILY(cpi);

	if (cpi->cpi_family == 0xf) {
		cpi->cpi_family += CPI_FAMILY_XTD(cpi);
		cpi->cpi_model += CPI_MODEL_XTD(cpi) << 4;
	}

	cpi->cpi_step = CPI_STEP(cpi);
	cpi->cpi_brandid = CPI_BRANDID(cpi);

	/*
	 * *default* assumptions:
	 * - believe %edx feature word
	 * - ignore %ecx feature word
	 * - 32-bit virtual and physical addressing
	 */
	mask_edx = 0xffffffff;
	mask_ecx = 0;

	cpi->cpi_pabits = cpi->cpi_vabits = 32;

	switch (cpi->cpi_vendor) {
	case X86_VENDOR_Intel:
		if (cpi->cpi_family == 5)
			x86_type = X86_TYPE_P5;
		else if (cpi->cpi_family == 6) {
			x86_type = X86_TYPE_P6;
			pentiumpro_bug4046376 = 1;
			pentiumpro_bug4064495 = 1;
			/*
			 * Clear the SEP bit when it was set erroneously
			 */
			if (cpi->cpi_model < 3 && cpi->cpi_step < 3)
				cp->cp_edx &= ~CPUID_INTC_EDX_SEP;
		} else if (cpi->cpi_family == 0xf) {
			x86_type = X86_TYPE_P4;
			/*
			 * We don't currently depend on any of the %ecx
			 * features until Prescott, so we'll only check
			 * this from P4 onwards.  We might want to revisit
			 * that idea later.
			 */
			mask_ecx = 0xffffffff;
		} else if (cpi->cpi_family > 0xf)
			mask_ecx = 0xffffffff;
		break;
	case X86_VENDOR_IntelClone:
	default:
		break;
	case X86_VENDOR_AMD:
#if defined(OPTERON_ERRATUM_108)
		if (cpi->cpi_family == 0xf && cpi->cpi_model == 0xe) {
			cp->cp_eax = (0xf0f & cp->cp_eax) | 0xc0;
			cpi->cpi_model = 0xc;
		} else
#endif
		if (cpi->cpi_family == 5) {
			/*
			 * AMD K5 and K6
			 *
			 * These CPUs have an incomplete implementation
			 * of MCA/MCE which we mask away.
			 */
			mask_edx =
			    CPUID_INTC_EDX_DE |
			    CPUID_INTC_EDX_PSE |
			    CPUID_INTC_EDX_TSC |
			    CPUID_INTC_EDX_MSR |
			    CPUID_INTC_EDX_CX8 |
			    CPUID_INTC_EDX_PGE;
			if (cpi->cpi_model == 0) {
				/*
				 * Model 0 uses the wrong (APIC) bit
				 * to indicate PGE.  Fix it here.
				 */
				if (cp->cp_edx & 0x200) {
					cp->cp_edx &= ~0x200;
					cp->cp_edx |= CPUID_INTC_EDX_PGE;
				}
			} else if (cpi->cpi_model >= 6)
				mask_edx |= CPUID_INTC_EDX_MMX;
		}
		break;
	case X86_VENDOR_TM:
		/*
		 * workaround the NT workaround in CMS 4.1
		 */
		if (cpi->cpi_family == 5 && cpi->cpi_model == 4 &&
		    (cpi->cpi_step == 2 || cpi->cpi_step == 3))
			cp->cp_edx |= CPUID_INTC_EDX_CX8;
		break;
	case X86_VENDOR_Centaur:
		/*
		 * workaround the NT workarounds again
		 */
		if (cpi->cpi_family == 6)
			cp->cp_edx |= CPUID_INTC_EDX_CX8;
		break;
	case X86_VENDOR_Cyrix:
		/*
		 * We rely heavily on the probing in locore
		 * to actually figure out what parts, if any,
		 * of the Cyrix cpuid instruction to believe.
		 */
		switch (x86_type) {
		case X86_TYPE_CYRIX_486:
			mask_edx = 0;
			break;
		case X86_TYPE_CYRIX_6x86:
			mask_edx = 0;
			break;
		case X86_TYPE_CYRIX_6x86L:
			mask_edx =
			    CPUID_INTC_EDX_DE |
			    CPUID_INTC_EDX_CX8;
			break;
		case X86_TYPE_CYRIX_6x86MX:
			mask_edx =
			    CPUID_INTC_EDX_DE |
			    CPUID_INTC_EDX_MSR |
			    CPUID_INTC_EDX_CX8 |
			    CPUID_INTC_EDX_PGE |
			    CPUID_INTC_EDX_CMOV |
			    CPUID_INTC_EDX_MMX;
			break;
		case X86_TYPE_CYRIX_GXm:
			mask_edx =
			    CPUID_INTC_EDX_MSR |
			    CPUID_INTC_EDX_CX8 |
			    CPUID_INTC_EDX_CMOV |
			    CPUID_INTC_EDX_MMX;
			break;
		case X86_TYPE_CYRIX_MediaGX:
			break;
		case X86_TYPE_CYRIX_MII:
		case X86_TYPE_VIA_CYRIX_III:
			mask_edx =
			    CPUID_INTC_EDX_DE |
			    CPUID_INTC_EDX_TSC |
			    CPUID_INTC_EDX_MSR |
			    CPUID_INTC_EDX_CX8 |
			    CPUID_INTC_EDX_PGE |
			    CPUID_INTC_EDX_CMOV |
			    CPUID_INTC_EDX_MMX;
			break;
		default:
			break;
		}
		break;
	}

	/*
	 * Now we've figured out the masks that determine
	 * which bits we choose to believe, apply the masks
	 * to the feature words, then map the kernel's view
	 * of these feature words into its feature word.
	 */
	cp->cp_edx &= mask_edx;
	cp->cp_ecx &= mask_ecx;

	/*
	 * fold in fix ups
	 */

	cp->cp_edx |= cpuid_feature_edx_include;
	cp->cp_edx &= ~cpuid_feature_edx_exclude;


	cp->cp_ecx |= cpuid_feature_ecx_include;
	cp->cp_ecx &= ~cpuid_feature_ecx_exclude;

	if (cp->cp_edx & CPUID_INTC_EDX_PSE)
		feature |= X86_LARGEPAGE;
	if (cp->cp_edx & CPUID_INTC_EDX_TSC)
		feature |= X86_TSC;
	if (cp->cp_edx & CPUID_INTC_EDX_MSR)
		feature |= X86_MSR;
	if (cp->cp_edx & CPUID_INTC_EDX_MTRR)
		feature |= X86_MTRR;
	if (cp->cp_edx & CPUID_INTC_EDX_PGE)
		feature |= X86_PGE;
	if (cp->cp_edx & CPUID_INTC_EDX_CMOV)
		feature |= X86_CMOV;
	if (cp->cp_edx & CPUID_INTC_EDX_MMX)
		feature |= X86_MMX;
	if ((cp->cp_edx & CPUID_INTC_EDX_MCE) != 0 &&
	    (cp->cp_edx & CPUID_INTC_EDX_MCA) != 0)
		feature |= X86_MCA;
	if (cp->cp_edx & CPUID_INTC_EDX_PAE)
		feature |= X86_PAE;
	if (cp->cp_edx & CPUID_INTC_EDX_CX8)
		feature |= X86_CX8;
#if defined(CPUID_INTC_ECX_CX16)
	/*
	 * In the July 16 2004 edition of AN 485, Intel rescinded
	 * the definition of this bit -- it's now in the "Do not count on
	 * their value" category.
	 */
	if (cp->cp_ecx & CPUID_INTC_ECX_CX16)
		feature |= X86_CX16;
#endif
	if (cp->cp_edx & CPUID_INTC_EDX_PAT)
		feature |= X86_PAT;
	if (cp->cp_edx & CPUID_INTC_EDX_SEP)
		feature |= X86_SEP;
	if (cp->cp_edx & CPUID_INTC_EDX_FXSR) {
		/*
		 * In our implementation, fxsave/fxrstor
		 * are prerequisites before we'll even
		 * try and do SSE things.
		 */
		if (cp->cp_edx & CPUID_INTC_EDX_SSE)
			feature |= X86_SSE;
		if (cp->cp_edx & CPUID_INTC_EDX_SSE2)
			feature |= X86_SSE2;
		if (cp->cp_ecx & CPUID_INTC_ECX_SSE3)
			feature |= X86_SSE3;
	}
	if (cp->cp_edx & CPUID_INTC_EDX_DE)
		cr4_value |= CR4_DE;

	if (feature & X86_PAE)
		cpi->cpi_pabits = 36;

	/*
	 * Hyperthreading configuration is slightly tricky on Intel
	 * and pure clones, and even trickier on AMD.
	 *
	 * (AMD chose to set the HTT bit on their CMP processors,
	 * even though they're not actually hyperthreaded.  Thus it
	 * takes a bit more work to figure out what's really going
	 * on ... see the handling of the HTvalid bit below)
	 */
	if (cp->cp_edx & CPUID_INTC_EDX_HTT) {
		cpi->cpi_ncpu_per_chip = CPI_CPU_COUNT(cpi);
		if (cpi->cpi_ncpu_per_chip > 1)
			feature |= X86_HTT;
	}

	/*
	 * Work on the "extended" feature information, doing
	 * some basic initialization for cpuid_pass2()
	 */
	xcpuid = 0;
	switch (cpi->cpi_vendor) {
	case X86_VENDOR_Intel:
		if (cpi->cpi_family >= 0xf)
			xcpuid++;
		break;
	case X86_VENDOR_AMD:
		if (cpi->cpi_family > 5 ||
		    (cpi->cpi_family == 5 && cpi->cpi_model >= 1))
			xcpuid++;
		break;
	case X86_VENDOR_Cyrix:
		/*
		 * Only these Cyrix CPUs are -known- to support
		 * extended cpuid operations.
		 */
		if (x86_type == X86_TYPE_VIA_CYRIX_III ||
		    x86_type == X86_TYPE_CYRIX_GXm)
			xcpuid++;
		break;
	case X86_VENDOR_Centaur:
	case X86_VENDOR_TM:
	default:
		xcpuid++;
		break;
	}

	if (xcpuid) {
		cp = &cpi->cpi_extd[0];
		cpi->cpi_xmaxeax = cp->cp_eax = __cpuid_insn(0x80000000,
		    &cp->cp_ebx, &cp->cp_ecx, &cp->cp_edx);
	}

	if (cpi->cpi_xmaxeax & 0x80000000) {

		if (cpi->cpi_xmaxeax > CPI_XMAXEAX_MAX)
			cpi->cpi_xmaxeax = CPI_XMAXEAX_MAX;

		switch (cpi->cpi_vendor) {
		case X86_VENDOR_Intel:
		case X86_VENDOR_AMD:
			if (cpi->cpi_xmaxeax < 0x80000001)
				break;
			cp = &cpi->cpi_extd[1];
			cp->cp_eax = __cpuid_insn(0x80000001,
			    &cp->cp_ebx, &cp->cp_ecx, &cp->cp_edx);
			if (cpi->cpi_vendor == X86_VENDOR_AMD &&
			    cpi->cpi_family == 5 &&
			    cpi->cpi_model == 6 &&
			    cpi->cpi_step == 6) {
				/*
				 * K6 model 6 uses bit 10 to indicate SYSC
				 * Later models use bit 11. Fix it here.
				 */
				if (cp->cp_edx & 0x400) {
					cp->cp_edx &= ~0x400;
					cp->cp_edx |= CPUID_AMD_EDX_SYSC;
				}
			}

			/*
			 * Compute the additions to the kernel's feature word.
			 */
			if (cp->cp_edx & CPUID_AMD_EDX_NX)
				feature |= X86_NX;

			/*
			 * Unless both the HTT bit is set, and the
			 * HTvalid bit is set, then we're not actually
			 * HyperThreaded at all..
			 */
			if (cpi->cpi_vendor == X86_VENDOR_AMD &&
			    (feature & X86_HTT) == X86_HTT &&
			    (cp->cp_ecx & CPUID_AMD_ECX_HTvalid) == 0)
				feature &= ~X86_HTT;
#if defined(_LP64)
			/*
			 * It's really tricky to support syscall/sysret in
			 * the i386 kernel; we rely on sysenter/sysexit
			 * instead.  In the amd64 kernel, things are -way-
			 * better.
			 */
			if (cp->cp_edx & CPUID_AMD_EDX_SYSC)
				feature |= X86_ASYSC;

			/*
			 * While we're thinking about system calls, note
			 * that AMD processors don't support sysenter
			 * in long mode at all, so don't try to program them.
			 */
			if (x86_vendor == X86_VENDOR_AMD)
				feature &= ~X86_SEP;
#endif
			break;
		default:
			break;
		}

		switch (cpi->cpi_vendor) {
		case X86_VENDOR_Intel:
		case X86_VENDOR_AMD:
			if (cpi->cpi_xmaxeax < 0x80000008)
				break;
			cp = &cpi->cpi_extd[8];
			cp->cp_eax = __cpuid_insn(0x80000008,
			    &cp->cp_ebx, &cp->cp_ecx, &cp->cp_edx);
			/*
			 * Virtual and physical address limits from
			 * cpuid override previously guessed values.
			 */
			cpi->cpi_pabits = BITX(cp->cp_eax, 7, 0);
			cpi->cpi_vabits = BITX(cp->cp_eax, 15, 8);

			/*
			 * This -might- be a CMP processor?
			 */
			if (cpi->cpi_vendor == X86_VENDOR_AMD) {
				cpi->cpi_ncpu_per_chip =
				    1 + BITX(cp->cp_ecx, 7, 0);
				if (cpi->cpi_ncpu_per_chip > 1)
					feature |= X86_CMP;
			}
			break;
		default:
			break;
		}
	}

	if ((feature & (X86_HTT | X86_CMP)) == 0) {
		cpi->cpi_chipid = -1;
		cpi->cpi_clogid = 0;
	} else if (cpi->cpi_ncpu_per_chip > 1) {
		uint_t i, cid_shift, apic_id;

		for (i = 1, cid_shift = 0;
		    i < cpi->cpi_ncpu_per_chip; i <<= 1)
			cid_shift++;
		apic_id = CPI_APIC_ID(cpi);
		cpi->cpi_chipid = apic_id >> cid_shift;
		cpi->cpi_clogid = apic_id & ((1 << cid_shift) - 1);
	}

pass1_done:
	cpi->cpi_pass = 1;
	return (feature);
}

/*
 * Make copies of the cpuid table entries we depend on, in
 * part for ease of parsing now, in part so that we have only
 * one place to correct any of it, in part for ease of
 * later export to userland, and in part so we can look at
 * this stuff in a crash dump.
 */

/*ARGSUSED*/
void
cpuid_pass2(cpu_t *cpu)
{
	uint_t n, nmax;
	int i;
	struct cpuidr *cp;
	uint8_t *dp;
	uint32_t *iptr;
	struct cpuid_info *cpi = cpu->cpu_m.mcpu_cpi;

	ASSERT(cpi->cpi_pass == 1);

	if (cpi->cpi_maxeax < 1)
		goto pass2_done;

	if ((nmax = cpi->cpi_maxeax + 1) > NMAX_CPI_STD)
		nmax = NMAX_CPI_STD;
	/*
	 * (We already handled n == 0 and n == 1 in pass 1)
	 */
	for (n = 2, cp = &cpi->cpi_std[2]; n < nmax; n++, cp++) {
		cp->cp_eax = __cpuid_insn(n,
		    &cp->cp_ebx, &cp->cp_ecx, &cp->cp_edx);
		switch (n) {
		case 2:
			/*
			 * "the lower 8 bits of the %eax register
			 * contain a value that identifies the number
			 * of times the cpuid [instruction] has to be
			 * executed to obtain a complete image of the
			 * processor's caching systems."
			 *
			 * How *do* they make this stuff up?
			 */
			cpi->cpi_ncache = sizeof (*cp) *
			    BITX(cp->cp_eax, 7, 0);
			if (cpi->cpi_ncache == 0)
				break;
			cpi->cpi_ncache--;	/* skip count byte */

			/*
			 * Well, for now, rather than attempt to implement
			 * this slightly dubious algorithm, we just look
			 * at the first 15 ..
			 */
			if (cpi->cpi_ncache > (sizeof (*cp) - 1))
				cpi->cpi_ncache = sizeof (*cp) - 1;

			dp = cpi->cpi_cacheinfo;
			if (BITX(cp->cp_eax, 31, 31) == 0) {
				uint8_t *p = (void *)&cp->cp_eax;
				for (i = 1; i < 3; i++)
					if (p[i] != 0)
						*dp++ = p[i];
			}
			if (BITX(cp->cp_ebx, 31, 31) == 0) {
				uint8_t *p = (void *)&cp->cp_ebx;
				for (i = 0; i < 4; i++)
					if (p[i] != 0)
						*dp++ = p[i];
			}
			if (BITX(cp->cp_ecx, 31, 31) == 0) {
				uint8_t *p = (void *)&cp->cp_ecx;
				for (i = 0; i < 4; i++)
					if (p[i] != 0)
						*dp++ = p[i];
			}
			if (BITX(cp->cp_edx, 31, 31) == 0) {
				uint8_t *p = (void *)&cp->cp_edx;
				for (i = 0; i < 4; i++)
					if (p[i] != 0)
						*dp++ = p[i];
			}
			break;
		case 3:	/* Processor serial number, if PSN supported */
		case 4:	/* Deterministic cache parameters */
		case 5:	/* Monitor/Mwait parameters */
		default:
			break;
		}
	}

	if ((cpi->cpi_xmaxeax & 0x80000000) == 0)
		goto pass2_done;

	if ((nmax = cpi->cpi_xmaxeax - 0x80000000 + 1) > NMAX_CPI_EXTD)
		nmax = NMAX_CPI_EXTD;
	/*
	 * Copy the extended properties, fixing them as we go.
	 * (We already handled n == 0 and n == 1 in pass 1)
	 */
	iptr = (void *)cpi->cpi_brandstr;
	for (n = 2, cp = &cpi->cpi_extd[2]; n < nmax; cp++, n++) {
		cp->cp_eax = __cpuid_insn(n + 0x80000000,
		    &cp->cp_ebx, &cp->cp_ecx, &cp->cp_edx);
		switch (n) {
		case 2:
		case 3:
		case 4:
			/*
			 * Extract the brand string
			 */
			*iptr++ = cp->cp_eax;
			*iptr++ = cp->cp_ebx;
			*iptr++ = cp->cp_ecx;
			*iptr++ = cp->cp_edx;
			break;
		case 5:
			switch (cpi->cpi_vendor) {
			case X86_VENDOR_AMD:
				/*
				 * The Athlon and Duron were the first
				 * parts to report the sizes of the
				 * TLB for large pages. Before then,
				 * we don't trust the data.
				 */
				if (cpi->cpi_family < 6 ||
				    (cpi->cpi_family == 6 &&
				    cpi->cpi_model < 1))
					cp->cp_eax = 0;
				break;
			default:
				break;
			}
			break;
		case 6:
			switch (cpi->cpi_vendor) {
			case X86_VENDOR_AMD:
				/*
				 * The Athlon and Duron were the first
				 * AMD parts with L2 TLB's.
				 * Before then, don't trust the data.
				 */
				if (cpi->cpi_family < 6 ||
				    cpi->cpi_family == 6 &&
				    cpi->cpi_model < 1)
					cp->cp_eax = cp->cp_ebx = 0;
				/*
				 * AMD Duron rev A0 reports L2
				 * cache size incorrectly as 1K
				 * when it is really 64K
				 */
				if (cpi->cpi_family == 6 &&
				    cpi->cpi_model == 3 &&
				    cpi->cpi_step == 0) {
					cp->cp_ecx &= 0xffff;
					cp->cp_ecx |= 0x400000;
				}
				break;
			case X86_VENDOR_Cyrix:	/* VIA C3 */
				/*
				 * VIA C3 processors are a bit messed
				 * up w.r.t. encoding cache sizes in %ecx
				 */
				if (cpi->cpi_family != 6)
					break;
				/*
				 * model 7 and 8 were incorrectly encoded
				 *
				 * xxx is model 8 really broken?
				 */
				if (cpi->cpi_model == 7 ||
				    cpi->cpi_model == 8)
					cp->cp_ecx =
					    BITX(cp->cp_ecx, 31, 24) << 16 |
					    BITX(cp->cp_ecx, 23, 16) << 12 |
					    BITX(cp->cp_ecx, 15, 8) << 8 |
					    BITX(cp->cp_ecx, 7, 0);
				/*
				 * model 9 stepping 1 has wrong associativity
				 */
				if (cpi->cpi_model == 9 && cpi->cpi_step == 1)
					cp->cp_ecx |= 8 << 12;
				break;
			case X86_VENDOR_Intel:
				/*
				 * Extended L2 Cache features function.
				 * First appeared on Prescott.
				 */
			default:
				break;
			}
			break;
		default:
			break;
		}
	}

pass2_done:
	cpi->cpi_pass = 2;
}

static const char *
intel_cpubrand(const struct cpuid_info *cpi)
{
	int i;

	if ((x86_feature & X86_CPUID) == 0 ||
	    cpi->cpi_maxeax < 1 || cpi->cpi_family < 5)
		return ("i486");

	switch (cpi->cpi_family) {
	case 5:
		return ("Intel Pentium(r)");
	case 6:
		switch (cpi->cpi_model) {
			uint_t celeron, xeon;
			const struct cpuidr *cp;
		case 0:
		case 1:
		case 2:
			return ("Intel Pentium(r) Pro");
		case 3:
		case 4:
			return ("Intel Pentium(r) II");
		case 6:
			return ("Intel Celeron(r)");
		case 5:
		case 7:
			celeron = xeon = 0;
			cp = &cpi->cpi_std[2];	/* cache info */

			for (i = 1; i < 3; i++) {
				uint_t tmp;

				tmp = (cp->cp_eax >> (8 * i)) & 0xff;
				if (tmp == 0x40)
					celeron++;
				if (tmp >= 0x44 && tmp <= 0x45)
					xeon++;
			}

			for (i = 0; i < 2; i++) {
				uint_t tmp;

				tmp = (cp->cp_ebx >> (8 * i)) & 0xff;
				if (tmp == 0x40)
					celeron++;
				else if (tmp >= 0x44 && tmp <= 0x45)
					xeon++;
			}

			for (i = 0; i < 4; i++) {
				uint_t tmp;

				tmp = (cp->cp_ecx >> (8 * i)) & 0xff;
				if (tmp == 0x40)
					celeron++;
				else if (tmp >= 0x44 && tmp <= 0x45)
					xeon++;
			}

			for (i = 0; i < 4; i++) {
				uint_t tmp;

				tmp = (cp->cp_edx >> (8 * i)) & 0xff;
				if (tmp == 0x40)
					celeron++;
				else if (tmp >= 0x44 && tmp <= 0x45)
					xeon++;
			}

			if (celeron)
				return ("Intel Celeron(r)");
			if (xeon)
				return (cpi->cpi_model == 5 ?
				    "Intel Pentium(r) II Xeon(tm)" :
				    "Intel Pentium(r) III Xeon(tm)");
			return (cpi->cpi_model == 5 ?
			    "Intel Pentium(r) II or Pentium(r) II Xeon(tm)" :
			    "Intel Pentium(r) III or Pentium(r) III Xeon(tm)");
		default:
			break;
		}
	default:
		break;
	}

	if (cpi->cpi_family <= 0xf && cpi->cpi_model <= 0xf &&
	    cpi->cpi_brandid != 0) {
		static const struct {
			uint_t bt_bid;
			const char *bt_str;
		} brand_tbl[] = {
			{ 0x1,	"Intel(r) Celeron(r)" },
			{ 0x2,	"Intel(r) Pentium(r) III" },
			{ 0x3,	"Intel(r) Pentium(r) III Xeon(tm)" },
			{ 0x4,	"Intel(r) Pentium(r) III" },
			{ 0x6,	"Mobile Intel(r) Pentium(r) III" },
			{ 0x7,	"Mobile Intel(r) Celeron(r)" },
			{ 0x8,	"Intel(r) Pentium(r) 4" },
			{ 0x9,	"Intel(r) Pentium(r) 4" },
			{ 0xa,	"Intel(r) Celeron(r)" },
			{ 0xb,	"Intel(r) Xeon(tm)" },
			{ 0xc,	"Intel(r) Xeon(tm) MP" },
			{ 0xe,	"Mobile Intel(r) Pentium(r) 4" },
			{ 0xf,	"Mobile Intel(r) Celeron(r)" }
		};
		uint_t btblmax = sizeof (brand_tbl) / sizeof (brand_tbl[0]);
		uint_t sgn;

		sgn = (cpi->cpi_family << 8) |
		    (cpi->cpi_model << 4) | cpi->cpi_step;

		for (i = 0; i < btblmax; i++)
			if (brand_tbl[i].bt_bid == cpi->cpi_brandid)
				break;
		if (i < btblmax) {
			if (sgn == 0x6b1 && cpi->cpi_brandid == 3)
				return ("Intel(r) Celeron(r)");
			if (sgn < 0xf13 && cpi->cpi_brandid == 0xb)
				return ("Intel(r) Xeon(tm) MP");
			if (sgn < 0xf13 && cpi->cpi_brandid == 0xe)
				return ("Intel(r) Xeon(tm)");
			return (brand_tbl[i].bt_str);
		}
	}

	return (NULL);
}

static const char *
amd_cpubrand(const struct cpuid_info *cpi)
{
	if ((x86_feature & X86_CPUID) == 0 ||
	    cpi->cpi_maxeax < 1 || cpi->cpi_family < 5)
		return ("i486 compatible");

	switch (cpi->cpi_family) {
	case 5:
		switch (cpi->cpi_model) {
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
			return ("AMD-K5(r)");
		case 6:
		case 7:
			return ("AMD-K6(r)");
		case 8:
			return ("AMD-K6(r)-2");
		case 9:
			return ("AMD-K6(r)-III");
		default:
			return ("AMD (family 5)");
		}
	case 6:
		switch (cpi->cpi_model) {
		case 1:
			return ("AMD-K7(tm)");
		case 0:
		case 2:
		case 4:
			return ("AMD Athlon(tm)");
		case 3:
		case 7:
			return ("AMD Duron(tm)");
		case 6:
		case 8:
		case 10:
			/*
			 * Use the L2 cache size to distinguish
			 */
			return ((cpi->cpi_extd[6].cp_ecx >> 16) >= 256 ?
			    "AMD Athlon(tm)" : "AMD Duron(tm)");
		default:
			return ("AMD (family 6)");
		}
	default:
		break;
	}

	if (cpi->cpi_family == 0xf && cpi->cpi_model == 5 &&
	    cpi->cpi_brandid != 0) {
		switch (BITX(cpi->cpi_brandid, 7, 5)) {
		case 3:
			return ("AMD Opteron(tm) UP 1xx");
		case 4:
			return ("AMD Opteron(tm) DP 2xx");
		case 5:
			return ("AMD Opteron(tm) MP 8xx");
		default:
			return ("AMD Opteron(tm)");
		}
	}

	return (NULL);
}

static const char *
cyrix_cpubrand(struct cpuid_info *cpi, uint_t type)
{
	if ((x86_feature & X86_CPUID) == 0 ||
	    cpi->cpi_maxeax < 1 || cpi->cpi_family < 5 ||
	    type == X86_TYPE_CYRIX_486)
		return ("i486 compatible");

	switch (type) {
	case X86_TYPE_CYRIX_6x86:
		return ("Cyrix 6x86");
	case X86_TYPE_CYRIX_6x86L:
		return ("Cyrix 6x86L");
	case X86_TYPE_CYRIX_6x86MX:
		return ("Cyrix 6x86MX");
	case X86_TYPE_CYRIX_GXm:
		return ("Cyrix GXm");
	case X86_TYPE_CYRIX_MediaGX:
		return ("Cyrix MediaGX");
	case X86_TYPE_CYRIX_MII:
		return ("Cyrix M2");
	case X86_TYPE_VIA_CYRIX_III:
		return ("VIA Cyrix M3");
	default:
		/*
		 * Have another wild guess ..
		 */
		if (cpi->cpi_family == 4 && cpi->cpi_model == 9)
			return ("Cyrix 5x86");
		else if (cpi->cpi_family == 5) {
			switch (cpi->cpi_model) {
			case 2:
				return ("Cyrix 6x86");	/* Cyrix M1 */
			case 4:
				return ("Cyrix MediaGX");
			default:
				break;
			}
		} else if (cpi->cpi_family == 6) {
			switch (cpi->cpi_model) {
			case 0:
				return ("Cyrix 6x86MX"); /* Cyrix M2? */
			case 5:
			case 6:
			case 7:
			case 8:
			case 9:
				return ("VIA C3");
			default:
				break;
			}
		}
		break;
	}
	return (NULL);
}

/*
 * This only gets called in the case that the CPU extended
 * feature brand string (0x80000002, 0x80000003, 0x80000004)
 * aren't available, or contain null bytes for some reason.
 */
static void
fabricate_brandstr(struct cpuid_info *cpi)
{
	const char *brand = NULL;

	switch (cpi->cpi_vendor) {
	case X86_VENDOR_Intel:
		brand = intel_cpubrand(cpi);
		break;
	case X86_VENDOR_AMD:
		brand = amd_cpubrand(cpi);
		break;
	case X86_VENDOR_Cyrix:
		brand = cyrix_cpubrand(cpi, x86_type);
		break;
	case X86_VENDOR_NexGen:
		if (cpi->cpi_family == 5 && cpi->cpi_model == 0)
			brand = "NexGen Nx586";
		break;
	case X86_VENDOR_Centaur:
		if (cpi->cpi_family == 5)
			switch (cpi->cpi_model) {
			case 4:
				brand = "Centaur C6";
				break;
			case 8:
				brand = "Centaur C2";
				break;
			case 9:
				brand = "Centaur C3";
				break;
			default:
				break;
			}
		break;
	case X86_VENDOR_Rise:
		if (cpi->cpi_family == 5 &&
		    (cpi->cpi_model == 0 || cpi->cpi_model == 2))
			brand = "Rise mP6";
		break;
	case X86_VENDOR_SiS:
		if (cpi->cpi_family == 5 && cpi->cpi_model == 0)
			brand = "SiS 55x";
		break;
	case X86_VENDOR_TM:
		if (cpi->cpi_family == 5 && cpi->cpi_model == 4)
			brand = "Transmeta Crusoe TM3x00 or TM5x00";
		break;
	case X86_VENDOR_NSC:
	case X86_VENDOR_UMC:
	default:
		break;
	}
	if (brand) {
		(void) strcpy((char *)cpi->cpi_brandstr, brand);
		return;
	}

	/*
	 * If all else fails ...
	 */
	(void) snprintf(cpi->cpi_brandstr, sizeof (cpi->cpi_brandstr),
	    "%s %d.%d.%d", cpi->cpi_vendorstr, cpi->cpi_family,
	    cpi->cpi_model, cpi->cpi_step);
}

/*
 * This routine is called just after kernel memory allocation
 * becomes available on cpu0, and as part of mp_startup() on
 * the other cpus.
 *
 * Fixup the brand string.
 */
/*ARGSUSED*/
void
cpuid_pass3(cpu_t *cpu)
{
	struct cpuid_info *cpi = cpu->cpu_m.mcpu_cpi;

	ASSERT(cpi->cpi_pass == 2);

	if ((cpi->cpi_xmaxeax & 0x80000000) == 0) {
		fabricate_brandstr(cpi);
		goto pass3_done;
	}

	/*
	 * If we successfully extracted a brand string from the cpuid
	 * instruction, clean it up by removing leading spaces and
	 * similar junk.
	 */
	if (cpi->cpi_brandstr[0]) {
		size_t maxlen = sizeof (cpi->cpi_brandstr);
		char *src, *dst;

		dst = src = (char *)cpi->cpi_brandstr;
		src[maxlen - 1] = '\0';
		/*
		 * strip leading spaces
		 */
		while (*src == ' ')
			src++;
		/*
		 * Remove any 'Genuine' or "Authentic" prefixes
		 */
		if (strncmp(src, "Genuine ", 8) == 0)
			src += 8;
		if (strncmp(src, "Authentic ", 10) == 0)
			src += 10;

		/*
		 * Now do an in-place copy.
		 * Map (R) to (r) and (TM) to (tm).
		 * The era of teletypes is long gone, and there's
		 * -really- no need to shout.
		 */
		while (*src != '\0') {
			if (src[0] == '(') {
				if (strncmp(src + 1, "R)", 2) == 0) {
					(void) strncpy(dst, "(r)", 3);
					src += 3;
					dst += 3;
					continue;
				}
				if (strncmp(src + 1, "TM)", 3) == 0) {
					(void) strncpy(dst, "(tm)", 4);
					src += 4;
					dst += 4;
					continue;
				}
			}
			*dst++ = *src++;
		}
		*dst = '\0';

		/*
		 * Finally, remove any trailing spaces
		 */
		while (--dst > cpi->cpi_brandstr)
			if (*dst == ' ')
				*dst = '\0';
			else
				break;
	} else
		fabricate_brandstr(cpi);

pass3_done:
	cpi->cpi_pass = 3;
}

/*
 * This routine is called out of bind_hwcap() much later in the life
 * of the kernel (post_startup()).  The job of this routine is to resolve
 * the hardware feature support and kernel support for those features into
 * what we're actually going to tell applications via the aux vector.
 */
uint_t
cpuid_pass4(cpu_t *cpu)
{
	struct cpuid_info *cpi;
	uint_t hwcap_flags = 0;

	if (cpu == NULL)
		cpu = CPU;
	cpi = cpu->cpu_m.mcpu_cpi;

	ASSERT(cpi->cpi_pass == 3);

	if (cpi->cpi_maxeax >= 1) {
		uint32_t *edx = &cpi->cpi_support[STD_EDX_FEATURES];
		uint32_t *ecx = &cpi->cpi_support[STD_ECX_FEATURES];

		*edx = CPI_FEATURES_EDX(cpi);
		*ecx = CPI_FEATURES_ECX(cpi);

		/*
		 * [these require explicit kernel support]
		 */
		if ((x86_feature & X86_SEP) == 0)
			*edx &= ~CPUID_INTC_EDX_SEP;

		if ((x86_feature & X86_SSE) == 0)
			*edx &= ~(CPUID_INTC_EDX_FXSR|CPUID_INTC_EDX_SSE);
		if ((x86_feature & X86_SSE2) == 0)
			*edx &= ~CPUID_INTC_EDX_SSE2;

		if ((x86_feature & X86_HTT) == 0)
			*edx &= ~CPUID_INTC_EDX_HTT;

		if ((x86_feature & X86_SSE3) == 0)
			*ecx &= ~CPUID_INTC_ECX_SSE3;

		/*
		 * [no explicit support required beyond x87 fp context]
		 */
		if (!fpu_exists)
			*edx &= ~(CPUID_INTC_EDX_FPU | CPUID_INTC_EDX_MMX);

		/*
		 * Now map the supported feature vector to things that we
		 * think userland will care about.
		 */
		if (*edx & CPUID_INTC_EDX_SEP)
			hwcap_flags |= AV_386_SEP;
		if (*edx & CPUID_INTC_EDX_SSE)
			hwcap_flags |= AV_386_FXSR | AV_386_SSE;
		if (*edx & CPUID_INTC_EDX_SSE2)
			hwcap_flags |= AV_386_SSE2;
		if (*ecx & CPUID_INTC_ECX_SSE3)
			hwcap_flags |= AV_386_SSE3;

		if (*edx & CPUID_INTC_EDX_FPU)
			hwcap_flags |= AV_386_FPU;
		if (*edx & CPUID_INTC_EDX_MMX)
			hwcap_flags |= AV_386_MMX;

		if (*edx & CPUID_INTC_EDX_TSC)
			hwcap_flags |= AV_386_TSC;
		if (*edx & CPUID_INTC_EDX_CX8)
			hwcap_flags |= AV_386_CX8;
		if (*edx & CPUID_INTC_EDX_CMOV)
			hwcap_flags |= AV_386_CMOV;
		if (*ecx & CPUID_INTC_ECX_MON)
			hwcap_flags |= AV_386_MON;
#if defined(CPUID_INTC_ECX_CX16)
		if (*ecx & CPUID_INTC_ECX_CX16)
			hwcap_flags |= AV_386_CX16;
#endif
	}

	if (cpuid_is_ht(cpu))
		hwcap_flags |= AV_386_PAUSE;

	if (cpi->cpi_xmaxeax < 0x80000001)
		goto pass4_done;

	switch (cpi->cpi_vendor) {
		uint32_t junk, *edx;

	case X86_VENDOR_Intel:	/* sigh */
	case X86_VENDOR_AMD:
		edx = &cpi->cpi_support[AMD_EDX_FEATURES];

		*edx = CPI_FEATURES_XTD_EDX(cpi);

		/*
		 * [no explicit support required beyond
		 * x87 fp context and exception handlers]
		 */
		if (!fpu_exists)
			*edx &= ~(CPUID_AMD_EDX_MMXamd |
			    CPUID_AMD_EDX_3DNow | CPUID_AMD_EDX_3DNowx);

		if ((x86_feature & X86_ASYSC) == 0)
			*edx &= ~CPUID_AMD_EDX_SYSC;
		if ((x86_feature & X86_NX) == 0)
			*edx &= ~CPUID_AMD_EDX_NX;
#if !defined(_LP64)
		*edx &= ~CPUID_AMD_EDX_LM;
#endif
		/*
		 * Now map the supported feature vector to
		 * things that we think userland will care about.
		 */
		if (*edx & CPUID_AMD_EDX_SYSC)
			hwcap_flags |= AV_386_AMD_SYSC;
		if (*edx & CPUID_AMD_EDX_MMXamd)
			hwcap_flags |= AV_386_AMD_MMX;
		if (*edx & CPUID_AMD_EDX_3DNow)
			hwcap_flags |= AV_386_AMD_3DNow;
		if (*edx & CPUID_AMD_EDX_3DNowx)
			hwcap_flags |= AV_386_AMD_3DNowx;
		break;

	case X86_VENDOR_TM:
		edx = &cpi->cpi_support[TM_EDX_FEATURES];
		(void) __cpuid_insn(0x80860001, &junk, &junk, edx);
		break;

	default:
		break;
	}

pass4_done:
	cpi->cpi_pass = 4;
	return (hwcap_flags);
}


/*
 * Simulate the cpuid instruction using the data we previously
 * captured about this CPU.  We try our best to return the truth
 * about the hardware, independently of kernel support.
 */
uint32_t
cpuid_insn(cpu_t *cpu,
    uint32_t eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	struct cpuid_info *cpi;
	struct cpuidr *cp;

	if (cpu == NULL)
		cpu = CPU;
	cpi = cpu->cpu_m.mcpu_cpi;

	ASSERT(cpuid_checkpass(cpu, 3));

	/*
	 * CPUID data is cached in two separate places: cpi_std for standard
	 * CPUID functions, and cpi_extd for extended CPUID functions.
	 */
	if (eax <= cpi->cpi_maxeax && eax < NMAX_CPI_STD)
		cp = &cpi->cpi_std[eax];
	else if (eax >= 0x80000000 && eax <= cpi->cpi_xmaxeax &&
	    eax < 0x80000000 + NMAX_CPI_EXTD)
		cp = &cpi->cpi_extd[eax - 0x80000000];
	else
		/*
		 * The caller is asking for data from an input parameter which
		 * the kernel has not cached.  In this case we go fetch from
		 * the hardware and return the data directly to the user.
		 */
		return (__cpuid_insn(eax, ebx, ecx, edx));
	*ebx = cp->cp_ebx;
	*ecx = cp->cp_ecx;
	*edx = cp->cp_edx;
	return (cp->cp_eax);
}

int
cpuid_checkpass(cpu_t *cpu, int pass)
{
	return (cpu != NULL && cpu->cpu_m.mcpu_cpi != NULL &&
	    cpu->cpu_m.mcpu_cpi->cpi_pass >= pass);
}

int
cpuid_getbrandstr(cpu_t *cpu, char *s, size_t n)
{
	ASSERT(cpuid_checkpass(cpu, 3));

	return (snprintf(s, n, "%s", cpu->cpu_m.mcpu_cpi->cpi_brandstr));
}

int
cpuid_is_ht(cpu_t *cpu)
{
	if (cpu == NULL)
		cpu = CPU;

	ASSERT(cpuid_checkpass(cpu, 1));

	return (cpu->cpu_m.mcpu_cpi->cpi_chipid >= 0);
}

/*
 * AMD and Intel both implement the 64-bit variant of the syscall
 * instruction (syscallq), so if there's -any- support for syscall,
 * cpuid currently says "yes, we support this".
 *
 * However, Intel decided to -not- implement the 32-bit variant of the
 * syscall instruction, so we provide a predicate to allow our caller
 * to test that subtlety here.
 */
/*ARGSUSED*/
int
cpuid_syscall32_insn(cpu_t *cpu)
{
	ASSERT(cpuid_checkpass((cpu == NULL ? CPU : cpu), 1));

	if (x86_feature & X86_ASYSC)
		return (x86_vendor != X86_VENDOR_Intel);
	return (0);
}

int
cpuid_getidstr(cpu_t *cpu, char *s, size_t n)
{
	struct cpuid_info *cpi = cpu->cpu_m.mcpu_cpi;

	static const char fmt[] =
	    "x86 (%s family %d model %d step %d clock %d MHz)";
	static const char fmt_ht[] =
	    "x86 (chipid 0x%x %s family %d model %d step %d clock %d MHz)";

	ASSERT(cpuid_checkpass(cpu, 1));

	if (cpuid_is_ht(cpu))
		return (snprintf(s, n, fmt_ht, cpi->cpi_chipid,
		    cpi->cpi_vendorstr, cpi->cpi_family, cpi->cpi_model,
		    cpi->cpi_step, cpu->cpu_type_info.pi_clock));
	return (snprintf(s, n, fmt,
	    cpi->cpi_vendorstr, cpi->cpi_family, cpi->cpi_model,
	    cpi->cpi_step, cpu->cpu_type_info.pi_clock));
}

const char *
cpuid_getvendorstr(cpu_t *cpu)
{
	ASSERT(cpuid_checkpass(cpu, 1));
	return ((const char *)cpu->cpu_m.mcpu_cpi->cpi_vendorstr);
}

uint_t
cpuid_getvendor(cpu_t *cpu)
{
	ASSERT(cpuid_checkpass(cpu, 1));
	return (cpu->cpu_m.mcpu_cpi->cpi_vendor);
}

uint_t
cpuid_getfamily(cpu_t *cpu)
{
	ASSERT(cpuid_checkpass(cpu, 1));
	return (cpu->cpu_m.mcpu_cpi->cpi_family);
}

uint_t
cpuid_getmodel(cpu_t *cpu)
{
	ASSERT(cpuid_checkpass(cpu, 1));
	return (cpu->cpu_m.mcpu_cpi->cpi_model);
}

uint_t
cpuid_get_ncpu_per_chip(cpu_t *cpu)
{
	ASSERT(cpuid_checkpass(cpu, 1));
	return (cpu->cpu_m.mcpu_cpi->cpi_ncpu_per_chip);
}

uint_t
cpuid_getstep(cpu_t *cpu)
{
	ASSERT(cpuid_checkpass(cpu, 1));
	return (cpu->cpu_m.mcpu_cpi->cpi_step);
}

chipid_t
chip_plat_get_chipid(cpu_t *cpu)
{
	ASSERT(cpuid_checkpass(cpu, 1));

	if (cpuid_is_ht(cpu))
		return (cpu->cpu_m.mcpu_cpi->cpi_chipid);
	return (cpu->cpu_id);
}

int
chip_plat_get_clogid(cpu_t *cpu)
{
	ASSERT(cpuid_checkpass(cpu, 1));
	return (cpu->cpu_m.mcpu_cpi->cpi_clogid);
}

void
cpuid_get_addrsize(cpu_t *cpu, uint_t *pabits, uint_t *vabits)
{
	struct cpuid_info *cpi;

	if (cpu == NULL)
		cpu = CPU;
	cpi = cpu->cpu_m.mcpu_cpi;

	ASSERT(cpuid_checkpass(cpu, 1));

	if (pabits)
		*pabits = cpi->cpi_pabits;
	if (vabits)
		*vabits = cpi->cpi_vabits;
}

/*
 * Returns the number of data TLB entries for a corresponding
 * pagesize.  If it can't be computed, or isn't known, the
 * routine returns zero.  If you ask about an architecturally
 * impossible pagesize, the routine will panic (so that the
 * hat implementor knows that things are inconsistent.)
 */
uint_t
cpuid_get_dtlb_nent(cpu_t *cpu, size_t pagesize)
{
	struct cpuid_info *cpi;
	uint_t dtlb_nent = 0;

	if (cpu == NULL)
		cpu = CPU;
	cpi = cpu->cpu_m.mcpu_cpi;

	ASSERT(cpuid_checkpass(cpu, 1));

	/*
	 * Check the L2 TLB info
	 */
	if (cpi->cpi_xmaxeax >= 0x80000006) {
		struct cpuidr *cp = &cpi->cpi_extd[6];

		switch (pagesize) {

		case 4 * 1024:
			/*
			 * All zero in the top 16 bits of the register
			 * indicates a unified TLB. Size is in low 16 bits.
			 */
			if ((cp->cp_ebx & 0xffff0000) == 0)
				dtlb_nent = cp->cp_ebx & 0x0000ffff;
			else
				dtlb_nent = BITX(cp->cp_ebx, 27, 16);
			break;

		case 2 * 1024 * 1024:
			if ((cp->cp_eax & 0xffff0000) == 0)
				dtlb_nent = cp->cp_eax & 0x0000ffff;
			else
				dtlb_nent = BITX(cp->cp_eax, 27, 16);
			break;

		default:
			panic("unknown L2 pagesize");
			/*NOTREACHED*/
		}
	}

	if (dtlb_nent != 0)
		return (dtlb_nent);

	/*
	 * No L2 TLB support for this size, try L1.
	 */
	if (cpi->cpi_xmaxeax >= 0x80000005) {
		struct cpuidr *cp = &cpi->cpi_extd[5];

		switch (pagesize) {
		case 4 * 1024:
			dtlb_nent = BITX(cp->cp_ebx, 23, 16);
			break;
		case 2 * 1024 * 1024:
			dtlb_nent = BITX(cp->cp_eax, 23, 16);
			break;
		default:
			panic("unknown L1 d-TLB pagesize");
			/*NOTREACHED*/
		}
	}

	return (dtlb_nent);
}

/*
 * Return 0 if the erratum is not present or not applicable, positive
 * if it is, and negative if the status of the erratum is unknown.
 *
 * See "Revision Guide for AMD Athlon(tm) 64 and AMD Opteron(tm)
 * Processors" #25759, Rev 3.57, August 2005
 */
int
cpuid_opteron_erratum(cpu_t *cpu, uint_t erratum)
{
	struct cpuid_info *cpi = cpu->cpu_m.mcpu_cpi;
	uint_t eax;

	if (cpi->cpi_vendor != X86_VENDOR_AMD)
		return (0);

	eax = cpi->cpi_std[1].cp_eax;

#define	SH_B0(eax)	(eax == 0xf40 || eax == 0xf50)
#define	SH_B3(eax) 	(eax == 0xf51)
#define	B(eax)		(SH_B0(eax) | SH_B3(eax))

#define	SH_C0(eax)	(eax == 0xf48 || eax == 0xf58)

#define	SH_CG(eax)	(eax == 0xf4a || eax == 0xf5a || eax == 0xf7a)
#define	DH_CG(eax)	(eax == 0xfc0 || eax == 0xfe0 || eax == 0xff0)
#define	CH_CG(eax)	(eax == 0xf82 || eax == 0xfb2)
#define	CG(eax)		(SH_CG(eax) | DH_CG(eax) | CH_CG(eax))

#define	SH_D0(eax)	(eax == 0x10f40 || eax == 0x10f50 || eax == 0x10f70)
#define	DH_D0(eax)	(eax == 0x10fc0 || eax == 0x10ff0)
#define	CH_D0(eax)	(eax == 0x10f80 || eax == 0x10fb0)
#define	D0(eax)		(SH_D0(eax) | DH_D0(eax) | CH_D0(eax))

#define	SH_E0(eax)	(eax == 0x20f50 || eax == 0x20f40 || eax == 0x20f70)
#define	JH_E1(eax)	(eax == 0x20f10)	/* JH8_E0 had 0x20f30 */
#define	DH_E3(eax)	(eax == 0x20fc0 || eax == 0x20ff0)
#define	SH_E4(eax)	(eax == 0x20f51 || eax == 0x20f71)
#define	BH_E4(eax)	(eax == 0x20fb1)
#define	SH_E5(eax)	(eax == 0x20f42)
#define	DH_E6(eax)	(eax == 0x20ff2 || eax == 0x20fc2)
#define	JH_E6(eax)	(eax == 0x20f12 || eax == 0x20f32)
#define	EX(eax)		(SH_E0(eax) | JH_E1(eax) | DH_E3(eax) | SH_E4(eax) | \
			    BH_E4(eax) | SH_E5(eax) | DH_E6(eax) | JH_E6(eax))

	switch (erratum) {
	case 1:
		return (1);
	case 51:	/* what does the asterisk mean? */
		return (B(eax) || SH_C0(eax) || CG(eax));
	case 52:
		return (B(eax));
	case 57:
		return (1);
	case 58:
		return (B(eax));
	case 60:
		return (1);
	case 61:
	case 62:
	case 63:
	case 64:
	case 65:
	case 66:
	case 68:
	case 69:
	case 70:
	case 71:
		return (B(eax));
	case 72:
		return (SH_B0(eax));
	case 74:
		return (B(eax));
	case 75:
		return (1);
	case 76:
		return (B(eax));
	case 77:
		return (1);
	case 78:
		return (B(eax) || SH_C0(eax));
	case 79:
		return (B(eax) || SH_C0(eax) || CG(eax) || D0(eax) || EX(eax));
	case 80:
	case 81:
	case 82:
		return (B(eax));
	case 83:
		return (B(eax) || SH_C0(eax) || CG(eax));
	case 85:
		return (1);
	case 86:
		return (SH_C0(eax) || CG(eax));
	case 88:
#if !defined(__amd64)
		return (0);
#else
		return (B(eax) || SH_C0(eax));
#endif
	case 89:
		return (1);
	case 90:
		return (B(eax) || SH_C0(eax) || CG(eax));
	case 91:
	case 92:
		return (B(eax) || SH_C0(eax));
	case 93:
		return (SH_C0(eax));
	case 94:
		return (B(eax) || SH_C0(eax) || CG(eax));
	case 95:
#if !defined(__amd64)
		return (0);
#else
		return (B(eax) || SH_C0(eax));
#endif
	case 96:
		return (B(eax) || SH_C0(eax) || CG(eax));
	case 97:
	case 98:
		return (SH_C0(eax) || CG(eax));
	case 99:
		return (B(eax) || SH_C0(eax) || CG(eax) || D0(eax));
	case 100:
		return (B(eax) || SH_C0(eax));
	case 101:
	case 103:
		return (B(eax) || SH_C0(eax) || CG(eax) || D0(eax));
	case 104:
		return (SH_C0(eax) || CG(eax) || D0(eax));
	case 105:
	case 106:
	case 107:
		return (B(eax) || SH_C0(eax) || CG(eax) || D0(eax));
	case 108:
		return (DH_CG(eax));
	case 109:
		return (SH_C0(eax) || CG(eax) || D0(eax));
	case 110:
		return (D0(eax) || EX(eax));
	case 111:
		return (CG(eax));
	case 112:
		return (B(eax) || SH_C0(eax) || CG(eax) || D0(eax) || EX(eax));
	case 113:
		return (eax == 0x20fc0);
	case 114:
		return (SH_E0(eax) || JH_E1(eax) || DH_E3(eax));
	case 115:
		return (SH_E0(eax) || JH_E1(eax));
	case 116:
		return (SH_E0(eax) || JH_E1(eax) || DH_E3(eax));
	case 117:
		return (B(eax) || SH_C0(eax) || CG(eax) || D0(eax));
	case 118:
		return (SH_E0(eax) || JH_E1(eax) || SH_E4(eax) || BH_E4(eax) ||
		    JH_E6(eax));
	case 121:
		return (B(eax) || SH_C0(eax) || CG(eax) || D0(eax) || EX(eax));
	case 122:
		return (SH_C0(eax) || CG(eax) || D0(eax) || EX(eax));
	case 123:
		return (JH_E1(eax) || BH_E4(eax) || JH_E6(eax));
	case 131:
		return (1);

	default:
		return (-1);
	}
}

static const char assoc_str[] = "associativity";
static const char line_str[] = "line-size";
static const char size_str[] = "size";

static void
add_cache_prop(dev_info_t *devi, const char *label, const char *type,
    uint32_t val)
{
	char buf[128];

	/*
	 * ndi_prop_update_int() is used because it is desirable for
	 * DDI_PROP_HW_DEF and DDI_PROP_DONTSLEEP to be set.
	 */
	if (snprintf(buf, sizeof (buf), "%s-%s", label, type) < sizeof (buf))
		(void) ndi_prop_update_int(DDI_DEV_T_NONE, devi, buf, val);
}

/*
 * Intel-style cache/tlb description
 *
 * Standard cpuid level 2 gives a randomly ordered
 * selection of tags that index into a table that describes
 * cache and tlb properties.
 */

static const char l1_icache_str[] = "l1-icache";
static const char l1_dcache_str[] = "l1-dcache";
static const char l2_cache_str[] = "l2-cache";
static const char itlb4k_str[] = "itlb-4K";
static const char dtlb4k_str[] = "dtlb-4K";
static const char itlb4M_str[] = "itlb-4M";
static const char dtlb4M_str[] = "dtlb-4M";
static const char itlb424_str[] = "itlb-4K-2M-4M";
static const char dtlb44_str[] = "dtlb-4K-4M";
static const char sl1_dcache_str[] = "sectored-l1-dcache";
static const char sl2_cache_str[] = "sectored-l2-cache";
static const char itrace_str[] = "itrace-cache";
static const char sl3_cache_str[] = "sectored-l3-cache";

static const struct cachetab {
	uint8_t 	ct_code;
	uint8_t		ct_assoc;
	uint16_t 	ct_line_size;
	size_t		ct_size;
	const char	*ct_label;
} intel_ctab[] = {
	/* maintain descending order! */
	{ 0xb3, 4, 0, 128, dtlb4k_str },
	{ 0xb0, 4, 0, 128, itlb4k_str },
	{ 0x87, 8, 64, 1024*1024, l2_cache_str},
	{ 0x86, 4, 64, 512*1024, l2_cache_str},
	{ 0x85, 8, 32, 2*1024*1024, l2_cache_str},
	{ 0x84, 8, 32, 1024*1024, l2_cache_str},
	{ 0x83, 8, 32, 512*1024, l2_cache_str},
	{ 0x82, 8, 32, 256*1024, l2_cache_str},
	{ 0x81, 8, 32, 128*1024, l2_cache_str},		/* suspect! */
	{ 0x7f, 2, 64, 512*1024, l2_cache_str},
	{ 0x7d, 8, 64, 2*1024*1024, sl2_cache_str},
	{ 0x7c, 8, 64, 1024*1024, sl2_cache_str},
	{ 0x7b, 8, 64, 512*1024, sl2_cache_str},
	{ 0x7a, 8, 64, 256*1024, sl2_cache_str},
	{ 0x79, 8, 64, 128*1024, sl2_cache_str},
	{ 0x78, 8, 64, 1024*1024, l2_cache_str},
	{ 0x72, 8, 0, 32*1024, itrace_str},
	{ 0x71, 8, 0, 16*1024, itrace_str},
	{ 0x70, 8, 0, 12*1024, itrace_str},
	{ 0x68, 4, 64, 32*1024, sl1_dcache_str},
	{ 0x67, 4, 64, 16*1024, sl1_dcache_str},
	{ 0x66, 4, 64, 8*1024, sl1_dcache_str},
	{ 0x60, 8, 64, 16*1024, sl1_dcache_str},
	{ 0x5d, 0, 0, 256, dtlb44_str},
	{ 0x5c, 0, 0, 128, dtlb44_str},
	{ 0x5b, 0, 0, 64, dtlb44_str},
	{ 0x52, 0, 0, 256, itlb424_str},
	{ 0x51, 0, 0, 128, itlb424_str},
	{ 0x50, 0, 0, 64, itlb424_str},
	{ 0x45, 4, 32, 2*1024*1024, l2_cache_str},
	{ 0x44, 4, 32, 1024*1024, l2_cache_str},
	{ 0x43, 4, 32, 512*1024, l2_cache_str},
	{ 0x42, 4, 32, 256*1024, l2_cache_str},
	{ 0x41, 4, 32, 128*1024, l2_cache_str},
	{ 0x3c, 4, 64, 256*1024, sl2_cache_str},
	{ 0x3b, 2, 64, 128*1024, sl2_cache_str},
	{ 0x39, 4, 64, 128*1024, sl2_cache_str},
	{ 0x30, 8, 64, 32*1024, l1_icache_str},
	{ 0x2c, 8, 64, 32*1024, l1_dcache_str},
	{ 0x29, 8, 64, 4096*1024, sl3_cache_str},
	{ 0x25, 8, 64, 2048*1024, sl3_cache_str},
	{ 0x23, 8, 64, 1024*1024, sl3_cache_str},
	{ 0x22, 4, 64, 512*1024, sl3_cache_str},
	{ 0x0c, 4, 32, 16*1024, l1_dcache_str},
	{ 0x0a, 2, 32, 8*1024, l1_dcache_str},
	{ 0x08, 4, 32, 16*1024, l1_icache_str},
	{ 0x06, 4, 32, 8*1024, l1_icache_str},
	{ 0x04, 4, 0, 8, dtlb4M_str},
	{ 0x03, 4, 0, 64, dtlb4k_str},
	{ 0x02, 4, 0, 2, itlb4M_str},
	{ 0x01, 4, 0, 32, itlb4k_str},
	{ 0 }
};

static const struct cachetab cyrix_ctab[] = {
	{ 0x70, 4, 0, 32, "tlb-4K" },
	{ 0x80, 4, 16, 16*1024, "l1-cache" },
	{ 0 }
};

/*
 * Search a cache table for a matching entry
 */
static const struct cachetab *
find_cacheent(const struct cachetab *ct, uint_t code)
{
	if (code != 0) {
		for (; ct->ct_code != 0; ct++)
			if (ct->ct_code <= code)
				break;
		if (ct->ct_code == code)
			return (ct);
	}
	return (NULL);
}

/*
 * Walk the cacheinfo descriptor, applying 'func' to every valid element
 * The walk is terminated if the walker returns non-zero.
 */
static void
intel_walk_cacheinfo(struct cpuid_info *cpi,
    void *arg, int (*func)(void *, const struct cachetab *))
{
	const struct cachetab *ct;
	uint8_t *dp;
	int i;

	if ((dp = cpi->cpi_cacheinfo) == NULL)
		return;
	for (i = 0; i < cpi->cpi_ncache; i++, dp++)
		if ((ct = find_cacheent(intel_ctab, *dp)) != NULL) {
			if (func(arg, ct) != 0)
				break;
		}
}

/*
 * (Like the Intel one, except for Cyrix CPUs)
 */
static void
cyrix_walk_cacheinfo(struct cpuid_info *cpi,
    void *arg, int (*func)(void *, const struct cachetab *))
{
	const struct cachetab *ct;
	uint8_t *dp;
	int i;

	if ((dp = cpi->cpi_cacheinfo) == NULL)
		return;
	for (i = 0; i < cpi->cpi_ncache; i++, dp++) {
		/*
		 * Search Cyrix-specific descriptor table first ..
		 */
		if ((ct = find_cacheent(cyrix_ctab, *dp)) != NULL) {
			if (func(arg, ct) != 0)
				break;
			continue;
		}
		/*
		 * .. else fall back to the Intel one
		 */
		if ((ct = find_cacheent(intel_ctab, *dp)) != NULL) {
			if (func(arg, ct) != 0)
				break;
			continue;
		}
	}
}

/*
 * A cacheinfo walker that adds associativity, line-size, and size properties
 * to the devinfo node it is passed as an argument.
 */
static int
add_cacheent_props(void *arg, const struct cachetab *ct)
{
	dev_info_t *devi = arg;

	add_cache_prop(devi, ct->ct_label, assoc_str, ct->ct_assoc);
	if (ct->ct_line_size != 0)
		add_cache_prop(devi, ct->ct_label, line_str,
		    ct->ct_line_size);
	add_cache_prop(devi, ct->ct_label, size_str, ct->ct_size);
	return (0);
}

static const char fully_assoc[] = "fully-associative?";

/*
 * AMD style cache/tlb description
 *
 * Extended functions 5 and 6 directly describe properties of
 * tlbs and various cache levels.
 */
static void
add_amd_assoc(dev_info_t *devi, const char *label, uint_t assoc)
{
	switch (assoc) {
	case 0:	/* reserved; ignore */
		break;
	default:
		add_cache_prop(devi, label, assoc_str, assoc);
		break;
	case 0xff:
		add_cache_prop(devi, label, fully_assoc, 1);
		break;
	}
}

static void
add_amd_tlb(dev_info_t *devi, const char *label, uint_t assoc, uint_t size)
{
	if (size == 0)
		return;
	add_cache_prop(devi, label, size_str, size);
	add_amd_assoc(devi, label, assoc);
}

static void
add_amd_cache(dev_info_t *devi, const char *label,
    uint_t size, uint_t assoc, uint_t lines_per_tag, uint_t line_size)
{
	if (size == 0 || line_size == 0)
		return;
	add_amd_assoc(devi, label, assoc);
	/*
	 * Most AMD parts have a sectored cache. Multiple cache lines are
	 * associated with each tag. A sector consists of all cache lines
	 * associated with a tag. For example, the AMD K6-III has a sector
	 * size of 2 cache lines per tag.
	 */
	if (lines_per_tag != 0)
		add_cache_prop(devi, label, "lines-per-tag", lines_per_tag);
	add_cache_prop(devi, label, line_str, line_size);
	add_cache_prop(devi, label, size_str, size * 1024);
}

static void
add_amd_l2_assoc(dev_info_t *devi, const char *label, uint_t assoc)
{
	switch (assoc) {
	case 0:	/* off */
		break;
	case 1:
	case 2:
	case 4:
		add_cache_prop(devi, label, assoc_str, assoc);
		break;
	case 6:
		add_cache_prop(devi, label, assoc_str, 8);
		break;
	case 8:
		add_cache_prop(devi, label, assoc_str, 16);
		break;
	case 0xf:
		add_cache_prop(devi, label, fully_assoc, 1);
		break;
	default: /* reserved; ignore */
		break;
	}
}

static void
add_amd_l2_tlb(dev_info_t *devi, const char *label, uint_t assoc, uint_t size)
{
	if (size == 0 || assoc == 0)
		return;
	add_amd_l2_assoc(devi, label, assoc);
	add_cache_prop(devi, label, size_str, size);
}

static void
add_amd_l2_cache(dev_info_t *devi, const char *label,
    uint_t size, uint_t assoc, uint_t lines_per_tag, uint_t line_size)
{
	if (size == 0 || assoc == 0 || line_size == 0)
		return;
	add_amd_l2_assoc(devi, label, assoc);
	if (lines_per_tag != 0)
		add_cache_prop(devi, label, "lines-per-tag", lines_per_tag);
	add_cache_prop(devi, label, line_str, line_size);
	add_cache_prop(devi, label, size_str, size * 1024);
}

static void
amd_cache_info(struct cpuid_info *cpi, dev_info_t *devi)
{
	struct cpuidr *cp;

	if (cpi->cpi_xmaxeax < 0x80000005)
		return;
	cp = &cpi->cpi_extd[5];

	/*
	 * 4M/2M L1 TLB configuration
	 *
	 * We report the size for 2M pages because AMD uses two
	 * TLB entries for one 4M page.
	 */
	add_amd_tlb(devi, "dtlb-2M",
	    BITX(cp->cp_eax, 31, 24), BITX(cp->cp_eax, 23, 16));
	add_amd_tlb(devi, "itlb-2M",
	    BITX(cp->cp_eax, 15, 8), BITX(cp->cp_eax, 7, 0));

	/*
	 * 4K L1 TLB configuration
	 */

	switch (cpi->cpi_vendor) {
		uint_t nentries;
	case X86_VENDOR_TM:
		if (cpi->cpi_family >= 5) {
			/*
			 * Crusoe processors have 256 TLB entries, but
			 * cpuid data format constrains them to only
			 * reporting 255 of them.
			 */
			if ((nentries = BITX(cp->cp_ebx, 23, 16)) == 255)
				nentries = 256;
			/*
			 * Crusoe processors also have a unified TLB
			 */
			add_amd_tlb(devi, "tlb-4K", BITX(cp->cp_ebx, 31, 24),
			    nentries);
			break;
		}
		/*FALLTHROUGH*/
	default:
		add_amd_tlb(devi, itlb4k_str,
		    BITX(cp->cp_ebx, 31, 24), BITX(cp->cp_ebx, 23, 16));
		add_amd_tlb(devi, dtlb4k_str,
		    BITX(cp->cp_ebx, 15, 8), BITX(cp->cp_ebx, 7, 0));
		break;
	}

	/*
	 * data L1 cache configuration
	 */

	add_amd_cache(devi, l1_dcache_str,
	    BITX(cp->cp_ecx, 31, 24), BITX(cp->cp_ecx, 23, 16),
	    BITX(cp->cp_ecx, 15, 8), BITX(cp->cp_ecx, 7, 0));

	/*
	 * code L1 cache configuration
	 */

	add_amd_cache(devi, l1_icache_str,
	    BITX(cp->cp_edx, 31, 24), BITX(cp->cp_edx, 23, 16),
	    BITX(cp->cp_edx, 15, 8), BITX(cp->cp_edx, 7, 0));

	if (cpi->cpi_xmaxeax < 0x80000006)
		return;
	cp = &cpi->cpi_extd[6];

	/* Check for a unified L2 TLB for large pages */

	if (BITX(cp->cp_eax, 31, 16) == 0)
		add_amd_l2_tlb(devi, "l2-tlb-2M",
		    BITX(cp->cp_eax, 15, 12), BITX(cp->cp_eax, 11, 0));
	else {
		add_amd_l2_tlb(devi, "l2-dtlb-2M",
		    BITX(cp->cp_eax, 31, 28), BITX(cp->cp_eax, 27, 16));
		add_amd_l2_tlb(devi, "l2-itlb-2M",
		    BITX(cp->cp_eax, 15, 12), BITX(cp->cp_eax, 11, 0));
	}

	/* Check for a unified L2 TLB for 4K pages */

	if (BITX(cp->cp_ebx, 31, 16) == 0) {
		add_amd_l2_tlb(devi, "l2-tlb-4K",
		    BITX(cp->cp_eax, 15, 12), BITX(cp->cp_eax, 11, 0));
	} else {
		add_amd_l2_tlb(devi, "l2-dtlb-4K",
		    BITX(cp->cp_eax, 31, 28), BITX(cp->cp_eax, 27, 16));
		add_amd_l2_tlb(devi, "l2-itlb-4K",
		    BITX(cp->cp_eax, 15, 12), BITX(cp->cp_eax, 11, 0));
	}

	add_amd_l2_cache(devi, l2_cache_str,
	    BITX(cp->cp_ecx, 31, 16), BITX(cp->cp_ecx, 15, 12),
	    BITX(cp->cp_ecx, 11, 8), BITX(cp->cp_ecx, 7, 0));
}

/*
 * There are two basic ways that the x86 world describes it cache
 * and tlb architecture - Intel's way and AMD's way.
 *
 * Return which flavor of cache architecture we should use
 */
static int
x86_which_cacheinfo(struct cpuid_info *cpi)
{
	switch (cpi->cpi_vendor) {
	case X86_VENDOR_Intel:
		if (cpi->cpi_maxeax >= 2)
			return (X86_VENDOR_Intel);
		break;
	case X86_VENDOR_AMD:
		/*
		 * The K5 model 1 was the first part from AMD that reported
		 * cache sizes via extended cpuid functions.
		 */
		if (cpi->cpi_family > 5 ||
		    (cpi->cpi_family == 5 && cpi->cpi_model >= 1))
			return (X86_VENDOR_AMD);
		break;
	case X86_VENDOR_TM:
		if (cpi->cpi_family >= 5)
			return (X86_VENDOR_AMD);
		/*FALLTHROUGH*/
	default:
		/*
		 * If they have extended CPU data for 0x80000005
		 * then we assume they have AMD-format cache
		 * information.
		 *
		 * If not, and the vendor happens to be Cyrix,
		 * then try our-Cyrix specific handler.
		 *
		 * If we're not Cyrix, then assume we're using Intel's
		 * table-driven format instead.
		 */
		if (cpi->cpi_xmaxeax >= 0x80000005)
			return (X86_VENDOR_AMD);
		else if (cpi->cpi_vendor == X86_VENDOR_Cyrix)
			return (X86_VENDOR_Cyrix);
		else if (cpi->cpi_maxeax >= 2)
			return (X86_VENDOR_Intel);
		break;
	}
	return (-1);
}

/*
 * create a node for the given cpu under the prom root node.
 * Also, create a cpu node in the device tree.
 */
static dev_info_t *cpu_nex_devi = NULL;
static kmutex_t cpu_node_lock;

/*
 * Called from post_startup() and mp_startup()
 */
void
add_cpunode2devtree(processorid_t cpu_id, struct cpuid_info *cpi)
{
	dev_info_t *cpu_devi;
	int create;

	mutex_enter(&cpu_node_lock);

	/*
	 * create a nexus node for all cpus identified as 'cpu_id' under
	 * the root node.
	 */
	if (cpu_nex_devi == NULL) {
		if (ndi_devi_alloc(ddi_root_node(), "cpus",
		    (dnode_t)DEVI_SID_NODEID, &cpu_nex_devi) != NDI_SUCCESS) {
			mutex_exit(&cpu_node_lock);
			return;
		}
		(void) ndi_devi_online(cpu_nex_devi, 0);
	}

	/*
	 * create a child node for cpu identified as 'cpu_id'
	 */
	cpu_devi = ddi_add_child(cpu_nex_devi, "cpu", DEVI_SID_NODEID,
		cpu_id);
	if (cpu_devi == NULL) {
		mutex_exit(&cpu_node_lock);
		return;
	}

	/* device_type */

	(void) ndi_prop_update_string(DDI_DEV_T_NONE, cpu_devi,
	    "device_type", "cpu");

	/* reg */

	(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
	    "reg", cpu_id);

	/* cpu-mhz, and clock-frequency */

	if (cpu_freq > 0) {
		long long mul;

		(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
		    "cpu-mhz", cpu_freq);

		if ((mul = cpu_freq * 1000000LL) <= INT_MAX)
			(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
			    "clock-frequency", (int)mul);
	}

	(void) ndi_devi_online(cpu_devi, 0);

	if ((x86_feature & X86_CPUID) == 0) {
		mutex_exit(&cpu_node_lock);
		return;
	}

	/* vendor-id */

	(void) ndi_prop_update_string(DDI_DEV_T_NONE, cpu_devi,
		"vendor-id", cpi->cpi_vendorstr);

	if (cpi->cpi_maxeax == 0) {
		mutex_exit(&cpu_node_lock);
		return;
	}

	/*
	 * family, model, and step
	 */
	(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
		"family", CPI_FAMILY(cpi));
	(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
		"cpu-model", CPI_MODEL(cpi));
	(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
		"stepping-id", CPI_STEP(cpi));

	/* type */

	switch (cpi->cpi_vendor) {
	case X86_VENDOR_Intel:
		create = 1;
		break;
	default:
		create = 0;
		break;
	}
	if (create)
		(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
			"type", CPI_TYPE(cpi));

	/* ext-family */

	switch (cpi->cpi_vendor) {
	case X86_VENDOR_Intel:
	case X86_VENDOR_AMD:
		create = cpi->cpi_family >= 0xf;
		break;
	default:
		create = 0;
		break;
	}
	if (create)
		(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
		    "ext-family", CPI_FAMILY_XTD(cpi));

	/* ext-model */

	switch (cpi->cpi_vendor) {
	case X86_VENDOR_Intel:
	case X86_VENDOR_AMD:
		create = CPI_MODEL(cpi) == 0xf;
		break;
	default:
		create = 0;
		break;
	}
	if (create)
		(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
			"ext-model", CPI_MODEL_XTD(cpi));

	/* generation */

	switch (cpi->cpi_vendor) {
	case X86_VENDOR_AMD:
		/*
		 * AMD K5 model 1 was the first part to support this
		 */
		create = cpi->cpi_xmaxeax >= 0x80000001;
		break;
	default:
		create = 0;
		break;
	}
	if (create)
		(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
		    "generation", BITX((cpi)->cpi_extd[1].cp_eax, 11, 8));

	/* brand-id */

	switch (cpi->cpi_vendor) {
	case X86_VENDOR_Intel:
		/*
		 * brand id first appeared on Pentium III Xeon model 8,
		 * and Celeron model 8 processors and Opteron
		 */
		create = cpi->cpi_family > 6 ||
		    (cpi->cpi_family == 6 && cpi->cpi_model >= 8);
		break;
	case X86_VENDOR_AMD:
		create = cpi->cpi_family >= 0xf;
		break;
	default:
		create = 0;
		break;
	}
	if (create && cpi->cpi_brandid != 0) {
		(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
		    "brand-id", cpi->cpi_brandid);
	}

	/* chunks, and apic-id */

	switch (cpi->cpi_vendor) {
	case X86_VENDOR_Intel:
	case X86_VENDOR_AMD:
		/*
		 * first available on Pentium IV and Opteron (K8)
		 */
		create = cpi->cpi_family >= 0xf;
		break;
	default:
		create = 0;
		break;
	}
	if (create) {
		(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
			"chunks", CPI_CHUNKS(cpi));
		(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
			"apic-id", CPI_APIC_ID(cpi));
		if (cpi->cpi_chipid >= 0)
			(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
			    "chip#", cpi->cpi_chipid);
	}

	/* cpuid-features */

	(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
	    "cpuid-features", CPI_FEATURES_EDX(cpi));


	/* cpuid-features-ecx */

	switch (cpi->cpi_vendor) {
	case X86_VENDOR_Intel:
		create = cpi->cpi_family >= 0xf;
		break;
	default:
		create = 0;
		break;
	}
	if (create)
		(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
		    "cpuid-features-ecx", CPI_FEATURES_ECX(cpi));

	/* ext-cpuid-features */

	switch (cpi->cpi_vendor) {
	case X86_VENDOR_AMD:
	case X86_VENDOR_Cyrix:
	case X86_VENDOR_TM:
	case X86_VENDOR_Centaur:
		/*
		 * The extended cpuid features are not relevant on
		 * Intel but are available from the AMD K5 model 1
		 * and most Cyrix GXm and later.
		 */
		create = cpi->cpi_xmaxeax >= 0x80000001;
		break;
	default:
		create = 0;
		break;
	}
	if (create)
		(void) ndi_prop_update_int(DDI_DEV_T_NONE, cpu_devi,
			"ext-cpuid-features", CPI_FEATURES_XTD_EDX(cpi));

	/*
	 * Brand String first appeared in Intel Pentium IV, AMD K5
	 * model 1, and Cyrix GXm.  On earlier models we try and
	 * simulate something similar .. so this string should always
	 * same -something- about the processor, however lame.
	 */
	(void) ndi_prop_update_string(DDI_DEV_T_NONE, cpu_devi,
	    "brand-string", cpi->cpi_brandstr);

	/*
	 * Finally, cache and tlb information
	 */
	switch (x86_which_cacheinfo(cpi)) {
	case X86_VENDOR_Intel:
		intel_walk_cacheinfo(cpi, cpu_devi, add_cacheent_props);
		break;
	case X86_VENDOR_Cyrix:
		cyrix_walk_cacheinfo(cpi, cpu_devi, add_cacheent_props);
		break;
	case X86_VENDOR_AMD:
		amd_cache_info(cpi, cpu_devi);
		break;
	default:
		break;
	}

	mutex_exit(&cpu_node_lock);
}

struct l2info {
	int *l2i_csz;
	int *l2i_lsz;
	int *l2i_assoc;
	int l2i_ret;
};

/*
 * A cacheinfo walker that fetches the size, line-size and associativity
 * of the L2 cache
 */
static int
intel_l2cinfo(void *arg, const struct cachetab *ct)
{
	struct l2info *l2i = arg;
	int *ip;

	if (ct->ct_label != l2_cache_str &&
	    ct->ct_label != sl2_cache_str)
		return (0);	/* not an L2 -- keep walking */

	if ((ip = l2i->l2i_csz) != NULL)
		*ip = ct->ct_size;
	if ((ip = l2i->l2i_lsz) != NULL)
		*ip = ct->ct_line_size;
	if ((ip = l2i->l2i_assoc) != NULL)
		*ip = ct->ct_assoc;
	l2i->l2i_ret = ct->ct_size;
	return (1);		/* was an L2 -- terminate walk */
}

static void
amd_l2cacheinfo(struct cpuid_info *cpi, struct l2info *l2i)
{
	struct cpuidr *cp;
	uint_t size, assoc;
	int *ip;

	if (cpi->cpi_xmaxeax < 0x80000006)
		return;
	cp = &cpi->cpi_extd[6];

	if ((assoc = BITX(cp->cp_ecx, 15, 12)) != 0 &&
	    (size = BITX(cp->cp_ecx, 31, 16)) != 0) {
		uint_t cachesz = size * 1024;


		if ((ip = l2i->l2i_csz) != NULL)
			*ip = cachesz;
		if ((ip = l2i->l2i_lsz) != NULL)
			*ip = BITX(cp->cp_ecx, 7, 0);
		if ((ip = l2i->l2i_assoc) != NULL)
			*ip = assoc;
		l2i->l2i_ret = cachesz;
	}
}

int
getl2cacheinfo(cpu_t *cpu, int *csz, int *lsz, int *assoc)
{
	struct cpuid_info *cpi = cpu->cpu_m.mcpu_cpi;
	struct l2info __l2info, *l2i = &__l2info;

	l2i->l2i_csz = csz;
	l2i->l2i_lsz = lsz;
	l2i->l2i_assoc = assoc;
	l2i->l2i_ret = -1;

	switch (x86_which_cacheinfo(cpi)) {
	case X86_VENDOR_Intel:
		intel_walk_cacheinfo(cpi, l2i, intel_l2cinfo);
		break;
	case X86_VENDOR_Cyrix:
		cyrix_walk_cacheinfo(cpi, l2i, intel_l2cinfo);
		break;
	case X86_VENDOR_AMD:
		amd_l2cacheinfo(cpi, l2i);
		break;
	default:
		break;
	}
	return (l2i->l2i_ret);
}
