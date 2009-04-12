/*
 * CDDL HEADER START
 *
 * Copyright(c) 2007-2009 Intel Corporation. All rights reserved.
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at:
 *      http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When using or redistributing this file, you may do so under the
 * License only. No other modification of this header is permitted.
 *
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

/* IntelVersion: 1.32 v2-7-8_2009-4-7 */

#ifndef _IXGBE_PHY_H
#define	_IXGBE_PHY_H

#include "ixgbe_type.h"

#define	IXGBE_I2C_EEPROM_DEV_ADDR	0xA0

/* EEPROM byte offsets */
#define	IXGBE_SFF_IDENTIFIER		0x0
#define	IXGBE_SFF_IDENTIFIER_SFP	0x3
#define	IXGBE_SFF_VENDOR_OUI_BYTE0	0x25
#define	IXGBE_SFF_VENDOR_OUI_BYTE1	0x26
#define	IXGBE_SFF_VENDOR_OUI_BYTE2	0x27
#define	IXGBE_SFF_1GBE_COMP_CODES	0x6
#define	IXGBE_SFF_10GBE_COMP_CODES	0x3
#define	IXGBE_SFF_TRANSMISSION_MEDIA	0x9

/* Bitmasks */
#define	IXGBE_SFF_TWIN_AX_CAPABLE	0x80
#define	IXGBE_SFF_1GBASESX_CAPABLE	0x1
#define	IXGBE_SFF_1GBASELX_CAPABLE	0x2
#define	IXGBE_SFF_10GBASESR_CAPABLE	0x10
#define	IXGBE_SFF_10GBASELR_CAPABLE	0x20
#define	IXGBE_I2C_EEPROM_READ_MASK	0x100
#define	IXGBE_I2C_EEPROM_STATUS_MASK	0x3
#define	IXGBE_I2C_EEPROM_STATUS_NO_OPERATION	0x0
#define	IXGBE_I2C_EEPROM_STATUS_PASS	0x1
#define	IXGBE_I2C_EEPROM_STATUS_FAIL	0x2
#define	IXGBE_I2C_EEPROM_STATUS_IN_PROGRESS	0x3

/* Bit-shift macros */
#define	IXGBE_SFF_VENDOR_OUI_BYTE0_SHIFT	24
#define	IXGBE_SFF_VENDOR_OUI_BYTE1_SHIFT	16
#define	IXGBE_SFF_VENDOR_OUI_BYTE2_SHIFT	8

/* Vendor OUIs: format of OUI is 0x[byte0][byte1][byte2][00] */
#define	IXGBE_SFF_VENDOR_OUI_TYCO	0x00407600
#define	IXGBE_SFF_VENDOR_OUI_FTL	0x00906500
#define	IXGBE_SFF_VENDOR_OUI_AVAGO	0x00176A00
#define	IXGBE_SFF_VENDOR_OUI_INTEL	0x001B2100

/* I2C SDA and SCL timing parameters for standard mode */
#define	IXGBE_I2C_T_HD_STA	4
#define	IXGBE_I2C_T_LOW		5
#define	IXGBE_I2C_T_HIGH	4
#define	IXGBE_I2C_T_SU_STA	5
#define	IXGBE_I2C_T_HD_DATA	5
#define	IXGBE_I2C_T_SU_DATA	1
#define	IXGBE_I2C_T_RISE	1
#define	IXGBE_I2C_T_FALL	1
#define	IXGBE_I2C_T_SU_STO	4
#define	IXGBE_I2C_T_BUF		5

s32 ixgbe_init_phy_ops_generic(struct ixgbe_hw *hw);
bool ixgbe_validate_phy_addr(struct ixgbe_hw *hw, u32 phy_addr);
enum ixgbe_phy_type ixgbe_get_phy_type_from_id(u32 phy_id);
s32 ixgbe_get_phy_id(struct ixgbe_hw *hw);
s32 ixgbe_identify_phy_generic(struct ixgbe_hw *hw);
s32 ixgbe_reset_phy_generic(struct ixgbe_hw *hw);
s32 ixgbe_read_phy_reg_generic(struct ixgbe_hw *hw, u32 reg_addr,
    u32 device_type, u16 *phy_data);
s32 ixgbe_write_phy_reg_generic(struct ixgbe_hw *hw, u32 reg_addr,
    u32 device_type, u16 phy_data);
s32 ixgbe_setup_phy_link_generic(struct ixgbe_hw *hw);
s32 ixgbe_setup_phy_link_speed_generic(struct ixgbe_hw *hw,
    ixgbe_link_speed speed, bool autoneg, bool autoneg_wait_to_complete);
s32 ixgbe_get_copper_link_capabilities_generic(struct ixgbe_hw *hw,
    ixgbe_link_speed *speed, bool *autoneg);

/* PHY specific */
s32 ixgbe_check_phy_link_tnx(struct ixgbe_hw *hw,
    ixgbe_link_speed *speed, bool *link_up);
s32 ixgbe_get_phy_firmware_version_tnx(struct ixgbe_hw *hw,
    u16 *firmware_version);

s32 ixgbe_reset_phy_nl(struct ixgbe_hw *hw);
s32 ixgbe_identify_sfp_module_generic(struct ixgbe_hw *hw);
s32 ixgbe_get_sfp_init_sequence_offsets(struct ixgbe_hw *hw,
    u16 *list_offset, u16 *data_offset);
s32 ixgbe_read_i2c_byte_generic(struct ixgbe_hw *hw, u8 byte_offset,
    u8 dev_addr, u8 *data);
s32 ixgbe_write_i2c_byte_generic(struct ixgbe_hw *hw, u8 byte_offset,
    u8 dev_addr, u8 data);
s32 ixgbe_read_i2c_eeprom_generic(struct ixgbe_hw *hw, u8 byte_offset,
    u8 *eeprom_data);
s32 ixgbe_write_i2c_eeprom_generic(struct ixgbe_hw *hw, u8 byte_offset,
    u8 eeprom_data);

#endif /* _IXGBE_PHY_H */
