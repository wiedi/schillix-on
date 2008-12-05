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
 * Copyright(c) 2007-2008 Intel Corporation. All rights reserved.
 */

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_IGB_OSDEP_H
#define	_IGB_OSDEP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/debug.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strlog.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/kstat.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/ddi.h>
#include <sys/dditypes.h>
#include <sys/sunddi.h>
#include <sys/pci.h>
#include <sys/atomic.h>
#include <sys/note.h>
#include "igb_debug.h"

#define	usec_delay(x)		drv_usecwait(x)
#define	msec_delay(x)		drv_usecwait(x * 1000)

#ifdef IGB_DEBUG
#define	DEBUGOUT(S)		IGB_DEBUGLOG_0(NULL, S)
#define	DEBUGOUT1(S, A)		IGB_DEBUGLOG_1(NULL, S, A)
#define	DEBUGOUT2(S, A, B)	IGB_DEBUGLOG_2(NULL, S, A, B)
#define	DEBUGOUT3(S, A, B, C)	IGB_DEBUGLOG_3(NULL, S, A, B, C)
#define	DEBUGFUNC(F)
#else
#define	DEBUGOUT(S)
#define	DEBUGOUT1(S, A)
#define	DEBUGOUT2(S, A, B)
#define	DEBUGOUT3(S, A, B, C)
#define	DEBUGFUNC(F)
#endif

#define	OS_DEP(hw)		((struct igb_osdep *)((hw)->back))

#define	FALSE		0
#define	TRUE		1

#define	CMD_MEM_WRT_INVALIDATE	0x0010	/* BIT_4 */
#define	PCI_COMMAND_REGISTER	0x04
#define	PCI_EX_CONF_CAP		0xE0


/*
 * Constants used in setting flow control thresholds
 */
#define	E1000_PBA_MASK		0xffff
#define	E1000_PBA_SHIFT		10
#define	E1000_FC_HIGH_DIFF	0x1638 /* High: 5688 bytes below Rx FIFO size */
#define	E1000_FC_LOW_DIFF	0x1640 /* Low: 5696 bytes below Rx FIFO size */
#define	E1000_FC_PAUSE_TIME	0x0680 /* 858 usec */

/* PHY Extended Status Register */
#define	IEEE_ESR_1000T_HD_CAPS	0x1000	/* 1000T HD capable */
#define	IEEE_ESR_1000T_FD_CAPS	0x2000	/* 1000T FD capable */
#define	IEEE_ESR_1000X_HD_CAPS	0x4000	/* 1000X HD capable */
#define	IEEE_ESR_1000X_FD_CAPS	0x8000	/* 1000X FD capable */

/* VMDq MODE supported by hardware */
#define	E1000_VMDQ_OFF		0
#define	E1000_VMDQ_MAC		1
#define	E1000_VMDQ_MAC_RSS	2

/* VMDq based on packet destination MAC address */
#define	E1000_MRQC_ENABLE_VMDQ_MAC_GROUP	0x00000003
/* VMDq based on packet destination MAC address and RSS */
#define	E1000_MRQC_ENABLE_VMDQ_MAC_RSS_GROUP	0x00000005
/* The default queue in each VMDqs */
#define	E1000_VMDQ_MAC_GROUP_DEFAULT_QUEUE	0x100

#define	E1000_WRITE_FLUSH(a)	(void) E1000_READ_REG(a, E1000_STATUS)

#define	E1000_WRITE_REG(hw, reg, value)	\
	ddi_put32((OS_DEP(hw))->reg_handle, \
	    (uint32_t *)((uintptr_t)(hw)->hw_addr + reg), (value))

#define	E1000_READ_REG(hw, reg)	\
	ddi_get32((OS_DEP(hw))->reg_handle, \
	    (uint32_t *)((uintptr_t)(hw)->hw_addr + reg))

#define	E1000_WRITE_REG_ARRAY(hw, reg, offset, value)	\
	ddi_put32((OS_DEP(hw))->reg_handle, \
	    (uint32_t *)((uintptr_t)(hw)->hw_addr + reg + ((offset) << 2)), \
	    (value))

#define	E1000_READ_REG_ARRAY(hw, reg, offset)	\
	ddi_get32((OS_DEP(hw))->reg_handle, \
	    (uint32_t *)((uintptr_t)(hw)->hw_addr + reg + ((offset) << 2)))

#define	E1000_WRITE_REG_ARRAY_DWORD(a, reg, offset, value)	\
	E1000_WRITE_REG_ARRAY(a, reg, offset, value)
#define	E1000_READ_REG_ARRAY_DWORD(a, reg, offset)		\
	E1000_READ_REG_ARRAY(a, reg, offset)

#define	msec_delay_irq	msec_delay

#define	UNREFERENCED_PARAMETER(x)	_NOTE(ARGUNUSED(x))

typedef	int8_t		s8;
typedef	int16_t		s16;
typedef	int32_t		s32;
typedef	int64_t		s64;
typedef uint8_t		u8;
typedef	uint16_t 	u16;
typedef	uint32_t	u32;
typedef	uint64_t	u64;
typedef	boolean_t	bool;

struct igb_osdep {
	ddi_acc_handle_t reg_handle;
	ddi_acc_handle_t cfg_handle;
	struct igb *igb;
};


#ifdef __cplusplus
}
#endif

#endif	/* _IGB_OSDEP_H */
