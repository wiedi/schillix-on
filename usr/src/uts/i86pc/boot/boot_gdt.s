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

#if defined(__lint)

int global_descriptor_table[0x10];

#else	/* __lint */

	.align 16

	/*
	 * This must remain in sync with the entries in intel/sys/gdt.h; in
	 * particular kmdb uses B64CODE_SEL or B32CODE_SEL in perpetuity for
	 * its IDT entries (they're copied to the kernel's GDT in init_idt()).
	 */

global_descriptor_table:
	.long   0
	.long   0

	/* GDT_B32DATA: 32 bit flat data descriptor */
	.value  0xFFFF	/* segment limit 0..15 */
	.value  0x0000	/* segment base 0..15 */
	.byte   0x0	/* segment base 16..23 */
	.byte   0x92	/* P = 1, read/write data */
	.byte   0xCF	/* G=1, B=1, Limit (16..19)=1111 */
	.byte   0x0	/* segment base 24..32 */

	/* GDT_B32CODE 32 bit flat code descriptor */
	.value  0xFFFF	/* segment limit 0..15 */
	.value  0x0000	/* segment base 0..15 */
	.byte   0x0	/* segment base 16..23 */
	.byte   0x9E	/* P=1, code, exec, readable */
	.byte   0xCF	/* G=1, D=1, Limit (16..19)=1111 */
	.byte   0x0	/* segment base 24..32 */

	/*
	 * GDT_B16CODE 16 bit code descriptor for doing BIOS calls
	 */
	.value  0xFFFF	/* segment limit 0..15 */
	.value  0x0000	/* segment base 0..15 */
	.byte   0x0	/* segment base 16..23 */
	.byte   0x9E	/* P=1, code, exec, readable */
	.byte   0x0F	/* G=0, D=0, Limit (16..19)=1111 */
	.byte   0x0	/* segment base 24..32 */

	/*
	 * GDT_B16DATA 16 bit data descriptor for doing BIOS calls
	 */
	.value  0xFFFF	/* segment limit 0..15 */
	.value  0x0000	/* segment base 0..15 */
	.byte   0x0	/* segment base 16..23 */
	.byte   0x92	/* P = 1, read/write data */
	.byte   0x4F	/* G=0, D=1, Limit (16..19)=1111 */
	.byte   0x0	/* segment base 24..32 */

	/* GDT_B64CODE: 64 bit flat code descriptor - only L bit has meaning */
	.value  0xFFFF	/* segment limit 0..15 */
	.value  0x0000	/* segment base 0..15 */
	.byte   0x0	/* segment base 16..23 */
	.byte   0x9E	/* P=1, code, exec, readable */
	.byte   0xAF	/* G=1, D=0, L=1, Limit (16..19)=1111 */
	.byte   0x0	/* segment base 24..32 */

	/*
	 * unused
	 */
	.long	0
	.long	0

	/*
	 * GDT_BGSTMP -- an entry for kmdb to use during boot
	 */
	.long	0
	.long	0

gdt_info:
	.value	gdt_info - global_descriptor_table - 1
	.long	global_descriptor_table
	.long   0		/* needed for 64 bit */

#endif	/* __lint */
