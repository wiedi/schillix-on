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

#ifndef _HB_SUN4_H
#define	_HB_SUN4_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <fm/topo_mod.h>
#include <sys/types.h>
#include <libdevinfo.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct busorrc {
	struct busorrc *br_nextbus; /* next bus or root complex */
	struct busorrc *br_prevbus; /* previous bus or root complex */
	ulong_t br_ba_ac;	  /* bus addr, after the comma */
	ulong_t br_ba_bc;	  /* bus addr, before the comma */
	di_node_t br_din;	  /* devinfo node */
} busorrc_t;

struct did_hash;

extern busorrc_t *busorrc_new(const char *, di_node_t, topo_mod_t *);
extern void busorrc_insert(busorrc_t **, busorrc_t *, topo_mod_t *);
extern int busorrc_add(busorrc_t **, di_node_t, topo_mod_t *);
extern void busorrc_free(busorrc_t *, topo_mod_t *);

extern tnode_t *hb_process(tnode_t *,
    topo_instance_t, topo_instance_t, di_node_t, struct did_hash *,
    di_prom_handle_t, topo_mod_t *);
extern tnode_t *rc_process(tnode_t *, topo_instance_t, di_node_t,
    struct did_hash *, di_prom_handle_t, topo_mod_t *);
extern int declare_buses(busorrc_t *, tnode_t *, int,
    struct did_hash *, di_prom_handle_t, topo_mod_t *);
extern int declare_exbuses(busorrc_t *, tnode_t *, int, int,
    struct did_hash *, di_prom_handle_t, topo_mod_t *);

#ifdef __cplusplus
}
#endif

#endif	/* _HB_SUN4_H */
