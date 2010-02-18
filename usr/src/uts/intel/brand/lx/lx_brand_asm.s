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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#if defined(__lint)

#include <sys/systm.h>

#else	/* __lint */

#include "genassym.h"
#include "../common/brand_asm.h"

#endif	/* __lint */

#ifdef	__lint

void
lx_brand_int80_callback(void)
{
}

#else	/* __lint */

#if defined(__amd64)

/*
 * See "64-BIT INTERPOSITION STACK" in brand_asm.h.
 */
ENTRY(lx_brand_int80_callback)
	GET_PROCP(SP_REG, 0, %r15)
	movq	P_ZONE(%r15), %r15		/* grab the zone pointer */
	/* grab the 'max syscall num' for this process from 'zone brand data' */
	movq	ZONE_BRAND_DATA(%r15), %r15	/* grab the zone brand ptr */
	movl	LXZD_MAX_SYSCALL(%r15), %r15d	/* get the 'max sysnum' word */
	cmpq	%r15, %rax			/* is 0 <= syscall <= MAX? */
	jbe	0f				/* yes, syscall is OK */
	xorl    %eax, %eax			/* no, zero syscall number */
0:

.lx_brand_int80_patch_point:
	jmp	.lx_brand_int80_notrace

.lx_brand_int80_notrace:
	CALC_TABLE_ADDR(%r15, L_HANDLER)
1:
	movq	%r15, %rax
	GET_V(%rsp, 0, V_SSP, %rsp)	/* restore intr. stack pointer */
	xchgq	(%rsp), %rax		/* swap %rax and return addr */
	jmp	sys_sysint_swapgs_iret

.lx_brand_int80_trace:
	/*
	 * If tracing is active, we vector to an alternate trace-enabling
	 * handler table instead.
	 */
	CALC_TABLE_ADDR(%r15, L_TRACEHANDLER)
	jmp	1b
SET_SIZE(lx_brand_int80_callback)

#define	PATCH_POINT	_CONST(.lx_brand_int80_patch_point + 1)
#define	PATCH_VAL	_CONST(.lx_brand_int80_trace - .lx_brand_int80_notrace)

ENTRY(lx_brand_int80_enable)
	movl	$1, lx_systrace_brand_enabled(%rip)
	movq	$PATCH_POINT, %r8
	movb	$PATCH_VAL, (%r8)
	ret
SET_SIZE(lx_brand_int80_enable)

ENTRY(lx_brand_int80_disable)
	movq	$PATCH_POINT, %r8
	movb	$0, (%r8)
	movl	$0, lx_systrace_brand_enabled(%rip)
	ret
SET_SIZE(lx_brand_int80_disable)


#elif defined(__i386)

/*
 * See "32-BIT INTERPOSITION STACK" in brand_asm.h.
 */
ENTRY(lx_brand_int80_callback)
	GET_PROCP(SP_REG, 0, %ebx)
	movl	P_ZONE(%ebx), %ebx		/* grab the zone pointer */
	/* grab the 'max syscall num' for this process from 'zone brand data' */
	movl	ZONE_BRAND_DATA(%ebx), %ebx	/* grab the zone brand data */
	movl	LXZD_MAX_SYSCALL(%ebx), %ebx	/* get the max sysnum */

	cmpl	%ebx, %eax 			/* is 0 <= syscall <= MAX? */
	jbe	0f				/* yes, syscall is OK */
	xorl    %eax, %eax		     	/* no, zero syscall number */	
0:

.lx_brand_int80_patch_point:
	jmp	.lx_brand_int80_notrace

.lx_brand_int80_notrace:
	CALC_TABLE_ADDR(%ebx, L_HANDLER)

1:
	movl	%ebx, %eax
	GET_V(%esp, 0, V_U_EBX, %ebx)		/* restore scratch register */
	addl	$V_END, %esp		/* restore intr. stack ptr */
	xchgl	(%esp), %eax		/* swap new and orig. return addrs */
	jmp	nopop_sys_rtt_syscall

.lx_brand_int80_trace:
	CALC_TABLE_ADDR(%ebx, L_TRACEHANDLER)
	jmp	1b
SET_SIZE(lx_brand_int80_callback)


#define	PATCH_POINT	_CONST(.lx_brand_int80_patch_point + 1)
#define	PATCH_VAL	_CONST(.lx_brand_int80_trace - .lx_brand_int80_notrace)

ENTRY(lx_brand_int80_enable)
	pushl	%ebx
	pushl	%eax
	movl	$1, lx_systrace_brand_enabled
	movl	$PATCH_POINT, %ebx
	movl	$PATCH_VAL, %eax
	movb	%al, (%ebx)
	popl	%eax
	popl	%ebx
	ret
SET_SIZE(lx_brand_int80_enable)

ENTRY(lx_brand_int80_disable)
	pushl	%ebx
	movl	$PATCH_POINT, %ebx
	movb	$0, (%ebx)
	movl	$0, lx_systrace_brand_enabled
	popl	%ebx
	ret
SET_SIZE(lx_brand_int80_disable)

#endif	/* __i386 */
#endif	/* __lint */
