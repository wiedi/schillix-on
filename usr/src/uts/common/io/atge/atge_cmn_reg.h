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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _ATGE_CMN_REG_H
#define	_ATGE_CMN_REG_H

#ifdef __cplusplus
	extern "C" {
#endif

#define	ATGE_SPI_CTRL	0x200
#define	SPI_VPD_ENB	0x00002000

#define	PCIE_DEVCTRL	0x0060	/* L1 */

/*
 * Station Address
 */
#define	ATGE_PAR0		0x1488
#define	ATGE_PAR1		0x148C

#define	ATGE_MASTER_CFG		0x1400
#define	MASTER_RESET		0x00000001
#define	MASTER_MTIMER_ENB	0x00000002
#define	MASTER_ITIMER_ENB	0x00000004	/* L1 */
#define	MASTER_IM_TX_TIMER_ENB	0x00000004	/* L1E */
#define	MASTER_MANUAL_INT_ENB	0x00000008
#define	MASTER_IM_RX_TIMER_ENB	0x00000020
#define	MASTER_INT_RDCLR	0x00000040
#define	MASTER_LED_MODE		0x00000200
#define	MASTER_CHIP_REV_MASK	0x00FF0000
#define	MASTER_CHIP_ID_MASK	0xFF000000
#define	MASTER_CHIP_REV_SHIFT	16
#define	MASTER_CHIP_ID_SHIFT	24

#define	ATGE_RESET_TIMEOUT	100

#define	ATGE_IDLE_STATUS	0x1410
#define	IDLE_STATUS_RXMAC	0x00000001
#define	IDLE_STATUS_TXMAC	0x00000002
#define	IDLE_STATUS_RXQ		0x00000004
#define	IDLE_STATUS_TXQ		0x00000008
#define	IDLE_STATUS_DMARD	0x00000010
#define	IDLE_STATUS_DMAWR	0x00000020
#define	IDLE_STATUS_SMB		0x00000040
#define	IDLE_STATUS_CMB		0x00000080

#define	ATGE_MAC_CFG			0x1480
#define	ATGE_CFG_TX_ENB			0x00000001
#define	ATGE_CFG_RX_ENB			0x00000002
#define	ATGE_CFG_TX_FC			0x00000004
#define	ATGE_CFG_RX_FC			0x00000008
#define	ATGE_CFG_LOOP			0x00000010
#define	ATGE_CFG_FULL_DUPLEX		0x00000020
#define	ATGE_CFG_TX_CRC_ENB		0x00000040
#define	ATGE_CFG_TX_AUTO_PAD		0x00000080
#define	ATGE_CFG_TX_LENCHK		0x00000100
#define	ATGE_CFG_RX_JUMBO_ENB		0x00000200
#define	ATGE_CFG_PREAMBLE_MASK		0x00003C00
#define	ATGE_CFG_VLAN_TAG_STRIP		0x00004000
#define	ATGE_CFG_PROMISC		0x00008000
#define	ATGE_CFG_TX_PAUSE		0x00010000
#define	ATGE_CFG_SCNT			0x00020000
#define	ATGE_CFG_SYNC_RST_TX		0x00040000
#define	ATGE_CFG_SPEED_MASK		0x00300000
#define	ATGE_CFG_SPEED_10_100		0x00100000
#define	ATGE_CFG_SPEED_1000		0x00200000
#define	ATGE_CFG_DBG_TX_BACKOFF		0x00400000
#define	ATGE_CFG_TX_JUMBO_ENB		0x00800000
#define	ATGE_CFG_RXCSUM_ENB		0x01000000
#define	ATGE_CFG_ALLMULTI		0x02000000
#define	ATGE_CFG_BCAST			0x04000000
#define	ATGE_CFG_DBG			0x08000000
#define	ATGE_CFG_PREAMBLE_SHIFT		10
#define	ATGE_CFG_PREAMBLE_DEFAULT	7

/*
 * Interrupt related registers.
 */
#define	ATGE_INTR_MASK			0x1604
#define	ATGE_INTR_STATUS		0x1600
#define	INTR_SMB			0x00000001
#define	INTR_MOD_TIMER			0x00000002
#define	INTR_MANUAL_TIMER		0x00000004
#define	INTR_RX_FIFO_OFLOW		0x00000008
#define	INTR_RD_UNDERRUN		0x00000010
#define	INTR_RRD_OFLOW			0x00000020
#define	INTR_TX_FIFO_UNDERRUN		0x00000040
#define	INTR_LINK_CHG			0x00000080
#define	INTR_HOST_RD_UNDERRUN		0x00000100
#define	INTR_HOST_RRD_OFLOW		0x00000200
#define	INTR_DMA_RD_TO_RST		0x00000400
#define	INTR_DMA_WR_TO_RST		0x00000800
#define	INTR_GPHY			0x00001000
#define	INTR_RX_PKT			0x00010000
#define	INTR_TX_PKT			0x00020000
#define	INTR_TX_DMA			0x00040000
#define	INTR_MAC_RX			0x00400000
#define	INTR_MAC_TX			0x00800000
#define	INTR_UNDERRUN			0x01000000
#define	INTR_FRAME_ERROR		0x02000000
#define	INTR_FRAME_OK			0x04000000
#define	INTR_CSUM_ERROR			0x08000000
#define	INTR_PHY_LINK_DOWN		0x10000000
#define	INTR_DIS_INT			0x80000000

/* L1E intr status */
#define	INTR_RX_PKT1			0x00080000
#define	INTR_RX_PKT2			0x00100000
#define	INTR_RX_PKT3			0x00200000

/* L1 intr status */
#define	INTR_TX_DMA			0x00040000

/*
 * L1E specific errors. We keep it here since some errors are common for
 * both L1 and L1E chip.
 *
 */
#define	L1E_INTR_ERRORS		\
	(INTR_DMA_RD_TO_RST | INTR_DMA_WR_TO_RST)

/*
 * TXQ CFG registers.
 */
#define	ATGE_TXQ_CFG			0x1580
#define	TXQ_CFG_TPD_BURST_MASK		0x0000000F
#define	TXQ_CFG_ENB			0x00000020
#define	TXQ_CFG_ENHANCED_MODE		0x00000040
#define	TXQ_CFG_TX_FIFO_BURST_MASK	0xFFFF0000
#define	TXQ_CFG_TPD_BURST_SHIFT		0
#define	TXQ_CFG_TPD_BURST_DEFAULT	4
#define	TXQ_CFG_TX_FIFO_BURST_SHIFT	16
#define	TXQ_CFG_TX_FIFO_BURST_DEFAULT	256

/*
 * RXQ CFG register.
 */
#define	ATGE_RXQ_CFG			0x15A0

/*
 * Common registers for DMA CFG.
 */
#define	ATGE_DMA_CFG			0x15C0
#define	DMA_CFG_IN_ORDER		0x00000001
#define	DMA_CFG_ENH_ORDER		0x00000002
#define	DMA_CFG_OUT_ORDER		0x00000004
#define	DMA_CFG_RCB_64			0x00000000
#define	DMA_CFG_RCB_128			0x00000008
#define	DMA_CFG_RD_BURST_128		0x00000000
#define	DMA_CFG_RD_BURST_256		0x00000010
#define	DMA_CFG_RD_BURST_512		0x00000020
#define	DMA_CFG_RD_BURST_1024		0x00000030
#define	DMA_CFG_RD_BURST_2048		0x00000040
#define	DMA_CFG_RD_BURST_4096		0x00000050
#define	DMA_CFG_WR_BURST_128		0x00000000
#define	DMA_CFG_WR_BURST_256		0x00000080
#define	DMA_CFG_WR_BURST_512		0x00000100
#define	DMA_CFG_WR_BURST_1024		0x00000180
#define	DMA_CFG_WR_BURST_2048		0x00000200
#define	DMA_CFG_WR_BURST_4096		0x00000280

/* L1E specific but can go into common regs */
#define	DMA_CFG_RXCMB_ENB		0x00200000
#define	DMA_CFG_RD_DELAY_CNT_DEFAULT	15
#define	DMA_CFG_RD_DELAY_CNT_SHIFT	11
#define	DMA_CFG_RD_DELAY_CNT_MASK	0x0000F800
#define	DMA_CFG_WR_DELAY_CNT_DEFAULT	4
#define	DMA_CFG_WR_DELAY_CNT_SHIFT	16
#define	DMA_CFG_WR_DELAY_CNT_MASK	0x000F0000

/*
 * Common PHY registers.
 */
#define	ATGE_MDIO			0x1414
#define	MDIO_DATA_MASK			0x0000FFFF
#define	MDIO_REG_ADDR_MASK		0x001F0000
#define	MDIO_OP_READ			0x00200000
#define	MDIO_OP_WRITE			0x00000000
#define	MDIO_SUP_PREAMBLE		0x00400000
#define	MDIO_OP_EXECUTE			0x00800000
#define	MDIO_CLK_25_4			0x00000000
#define	MDIO_CLK_25_6			0x02000000
#define	MDIO_CLK_25_8			0x03000000
#define	MDIO_CLK_25_10			0x04000000
#define	MDIO_CLK_25_14			0x05000000
#define	MDIO_CLK_25_20			0x06000000
#define	MDIO_CLK_25_28			0x07000000
#define	MDIO_OP_BUSY			0x08000000
#define	MDIO_DATA_SHIFT			0
#define	MDIO_REG_ADDR_SHIFT		16

#define	MDIO_REG_ADDR(x)	\
	(((x) << MDIO_REG_ADDR_SHIFT) & MDIO_REG_ADDR_MASK)


#define	ATGE_PHY_ADDR			0
#define	ATGE_PHY_STATUS			0x1418
#define	PHY_TIMEOUT			1000

#define	ATGE_DESC_TPD_CNT		0x155C
#define	DESC_TPD_CNT_MASK		0x00003FF
#define	DESC_TPD_CNT_SHIFT		0

#define	ATGE_DMA_BLOCK			0x1534
#define	DMA_BLOCK_LOAD			0x00000001

#define	ATGE_MBOX			0x15F0
#define	MBOX_RD_PROD_IDX_MASK		0x000007FF
#define	MBOX_RRD_CONS_IDX_MASK		0x003FF800
#define	MBOX_TD_PROD_IDX_MASK		0xFFC00000
#define	MBOX_RD_PROD_IDX_SHIFT		0
#define	MBOX_RRD_CONS_IDX_SHIFT		11
#define	MBOX_TD_PROD_IDX_SHIFT		22


#define	ATGE_IPG_IFG_CFG		0x1484
#define	IPG_IFG_IPGT_MASK		0x0000007F
#define	IPG_IFG_MIFG_MASK		0x0000FF00
#define	IPG_IFG_IPG1_MASK		0x007F0000
#define	IPG_IFG_IPG2_MASK		0x7F000000
#define	IPG_IFG_IPGT_SHIFT		0
#define	IPG_IFG_IPGT_DEFAULT		0x60
#define	IPG_IFG_MIFG_SHIFT		8
#define	IPG_IFG_MIFG_DEFAULT		0x50
#define	IPG_IFG_IPG1_SHIFT		16
#define	IPG_IFG_IPG1_DEFAULT		0x40
#define	IPG_IFG_IPG2_SHIFT		24
#define	IPG_IFG_IPG2_DEFAULT		0x60

/* half-duplex parameter configuration. */
#define	ATGE_HDPX_CFG			0x1498
#define	HDPX_CFG_LCOL_MASK		0x000003FF
#define	HDPX_CFG_RETRY_MASK		0x0000F000
#define	HDPX_CFG_EXC_DEF_EN		0x00010000
#define	HDPX_CFG_NO_BACK_C		0x00020000
#define	HDPX_CFG_NO_BACK_P		0x00040000
#define	HDPX_CFG_ABEBE			0x00080000
#define	HDPX_CFG_ABEBT_MASK		0x00F00000
#define	HDPX_CFG_JAMIPG_MASK		0x0F000000
#define	HDPX_CFG_LCOL_SHIFT		0
#define	HDPX_CFG_LCOL_DEFAULT		0x37
#define	HDPX_CFG_RETRY_SHIFT		12
#define	HDPX_CFG_RETRY_DEFAULT		0x0F
#define	HDPX_CFG_ABEBT_SHIFT		20
#define	HDPX_CFG_ABEBT_DEFAULT		0x0A
#define	HDPX_CFG_JAMIPG_SHIFT		24
#define	HDPX_CFG_JAMIPG_DEFAULT		0x07

#define	ATGE_FRAME_SIZE			0x149C

#define	ATGE_WOL_CFG			0x14A0
#define	WOL_CFG_PATTERN			0x00000001
#define	WOL_CFG_PATTERN_ENB		0x00000002
#define	WOL_CFG_MAGIC			0x00000004
#define	WOL_CFG_MAGIC_ENB		0x00000008
#define	WOL_CFG_LINK_CHG		0x00000010
#define	WOL_CFG_LINK_CHG_ENB		0x00000020
#define	WOL_CFG_PATTERN_DET		0x00000100
#define	WOL_CFG_MAGIC_DET		0x00000200
#define	WOL_CFG_LINK_CHG_DET		0x00000400
#define	WOL_CFG_CLK_SWITCH_ENB		0x00008000
#define	WOL_CFG_PATTERN0		0x00010000
#define	WOL_CFG_PATTERN1		0x00020000
#define	WOL_CFG_PATTERN2		0x00040000
#define	WOL_CFG_PATTERN3		0x00080000
#define	WOL_CFG_PATTERN4		0x00100000
#define	WOL_CFG_PATTERN5		0x00200000
#define	WOL_CFG_PATTERN6		0x00400000

/* WOL pattern length. */
#define	ATGE_PATTERN_CFG0		0x14A4
#define	PATTERN_CFG_0_LEN_MASK		0x0000007F
#define	PATTERN_CFG_1_LEN_MASK		0x00007F00
#define	PATTERN_CFG_2_LEN_MASK		0x007F0000
#define	PATTERN_CFG_3_LEN_MASK		0x7F000000

#define	ATGE_PATTERN_CFG1		0x14A8
#define	PATTERN_CFG_4_LEN_MASK		0x0000007F
#define	PATTERN_CFG_5_LEN_MASK		0x00007F00
#define	PATTERN_CFG_6_LEN_MASK		0x007F0000

#define	ATGE_TICK_USECS			2
#define	ATGE_USECS(x)			((x) / ATGE_TICK_USECS)

#define	ATGE_INTR_CLR_TIMER		0x140E	/* 16-bits */
#define	ATGE_IM_TIMER			0x1408
#define	ATGE_IM_TIMER2			0x140A
#define	ATGE_IM_TIMER_MIN		0
#define	ATGE_IM_TIMER_MAX		130000	/* 130 ms */
#define	ATGE_IM_TIMER_DEFAULT		100
#define	ATGE_IM_RX_TIMER_DEFAULT	1
#define	ATGE_IM_TX_TIMER_DEFAULT	1
#define	IM_TIMER_TX_SHIFT		0
#define	IM_TIMER_RX_SHIFT		16

#define	ATGE_DESC_ADDR_HI		0x1540
#define	ATGE_DESC_RD_ADDR_LO		0x1544
#define	ATGE_DESC_RRD_ADDR_LO		0x1548
#define	ATGE_DESC_TPD_ADDR_LO		0x154C
#define	ATGE_DESC_CMB_ADDR_LO		0x1550
#define	ATGE_DESC_SMB_ADDR_LO		0x1554
#define	ATGE_DESC_RRD_RD_CNT		0x1558

#define	ATGE_RXQ_JUMBO_CFG		0x15A4
#define	RXQ_JUMBO_CFG_SZ_THRESH_SHIFT	0
#define	RXQ_JUMBO_CFG_SZ_THRESH_MASK	0x000007FF
#define	RXQ_JUMBO_CFG_LKAH_DEFAULT	0x01
#define	RXQ_JUMBO_CFG_LKAH_SHIFT	11
#define	RXQ_JUMBO_CFG_LKAH_MASK		0x00007800
#define	RXQ_JUMBO_CFG_RRD_TIMER_SHIFT	16
#define	RXQ_JUMBO_CFG_RRD_TIMER_MASK	0xFFFF0000

#define	ATGE_RXQ_FIFO_PAUSE_THRESH	0x15A8
#define	RXQ_FIFO_PAUSE_THRESH_LO_MASK	0x00000FFF
#define	RXQ_FIFO_PAUSE_THRESH_HI_MASK	0x0FFF000
#define	RXQ_FIFO_PAUSE_THRESH_LO_SHIFT	0
#define	RXQ_FIFO_PAUSE_THRESH_HI_SHIFT	16

#define	ATGE_RXQ_CFG			0x15A0
#define	ATGE_TXQ_CFG			0x1580
#define	RXQ_CFG_ALIGN_32		0x00000000
#define	RXQ_CFG_ALIGN_64		0x00000001
#define	RXQ_CFG_ALIGN_128		0x00000002
#define	RXQ_CFG_ALIGN_256		0x00000003
#define	RXQ_CFG_QUEUE1_ENB		0x00000010
#define	RXQ_CFG_QUEUE2_ENB		0x00000020
#define	RXQ_CFG_QUEUE3_ENB		0x00000040
#define	RXQ_CFG_IPV6_CSUM_VERIFY	0x00000080
#define	RXQ_CFG_RSS_HASH_TBL_LEN_MASK	0x0000FF00
#define	RXQ_CFG_RSS_HASH_IPV4		0x00010000
#define	RXQ_CFG_RSS_HASH_IPV4_TCP	0x00020000
#define	RXQ_CFG_RSS_HASH_IPV6		0x00040000
#define	RXQ_CFG_RSS_HASH_IPV6_TCP	0x00080000
#define	RXQ_CFG_RSS_MODE_DIS		0x00000000
#define	RXQ_CFG_RSS_MODE_SQSINT		0x04000000
#define	RXQ_CFG_RSS_MODE_MQUESINT	0x08000000
#define	RXQ_CFG_RSS_MODE_MQUEMINT	0x0C000000
#define	RXQ_CFG_NIP_QUEUE_SEL_TBL	0x10000000
#define	RXQ_CFG_RSS_HASH_ENB		0x20000000
#define	RXQ_CFG_CUT_THROUGH_ENB		0x40000000
#define	RXQ_CFG_ENB			0x80000000
#define	RXQ_CFG_RSS_HASH_TBL_LEN_SHIFT	8

/* 64bit multicast hash register. */
#define	ATGE_MAR0			0x1490
#define	ATGE_MAR1			0x1494

#define	ATPHY_DBG_ADDR			0x1D
#define	ATPHY_DBG_DATA			0x1E

#ifdef __cplusplus
}
#endif

#endif	/* _ATGE_CMN_REG_H */
