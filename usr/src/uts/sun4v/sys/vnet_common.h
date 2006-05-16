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

#ifndef _VNET_COMMON_H
#define	_VNET_COMMON_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/vio_common.h>
#include <sys/vio_mailbox.h>
#include <sys/ethernet.h>

/*
 * This header file contains definitions common to LDoms Virtual Network
 * server (vsw) and client (vnet).
 */

/* max # of cookies per frame size */
#define	MAX_COOKIES	 ((ETHERMAX >> MMU_PAGESHIFT) + 2)

/* initial send sequence number */
#define	VNET_ISS		0x1

/* vnet descriptor */
typedef struct vnet_public_desc {
	vio_dring_entry_hdr_t	hdr;		/* descriptor header */
	uint32_t		nbytes;		/* data length */
	uint32_t		ncookies;	/* number of data cookies */
	ldc_mem_cookie_t	memcookie[MAX_COOKIES]; /* data cookies */
} vnet_public_desc_t;

/*
 * VIO in-band descriptor. Used by those vio clients
 * such as OBP who do not use descriptor rings.
 */
typedef struct vio_ibnd_desc {
	vio_inband_desc_msg_hdr_t	hdr;

	/* payload */
	uint32_t			nbytes;
	uint32_t			ncookies;
	ldc_mem_cookie_t		memcookie[MAX_COOKIES];
} vio_ibnd_desc_t;

#ifdef __cplusplus
}
#endif

#endif	/* _VNET_COMMON_H */
