/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at src/OPENSOLARIS.LICENSE.
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

#ifndef	_ETHERADDR_H
#define	_ETHERADDR_H

/*
 * Module:	etheraddr
 * Description:	This is the solaris-specific interface for retrieving
 *		the MAC (IEEE 802.3) node identifier, a.k.a. the ethernet
 *		address of the system.  Note that this can only get the
 *		ethernet address if the process running the code can open
 *		/dev/[whatever] read/write, e.g. you must be root.
 */

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h> /* For lots of types */
#include <sys/socket.h> /* For socket ops */
#include <sys/dlpi.h> /* For the DLPI stuff */
#include <net/if.h> /* For if ops */
#include <netinet/in.h> /* For if ops */
#include <netinet/if_ether.h> /* For ether structs */

/* max modules that can be pushed on intr */
#define	MAX_MODS	9

/* used to avoid getting errors from lo0 */
#define	LOOPBACK_IF	"lo0"

/*
 * Local structure encapsulating an interface
 * device and its associated modules
 */
typedef struct dev_att {
	char	ifname[LIFNAMSIZ];	/* interface name, such as "le0" */
	int	style;			/* DLPI message style */
	int	ppa;			/* Physical point of attachment */
	int	lun;			/* logical unit number */
	int	mod_cnt; 		/* # modules to push onto stream */
	char	devname[LIFNAMSIZ];	/* device name, such as "/dev/le0" */
	char	modlist[MAX_MODS][LIFNAMSIZ]; /* modules to push onto stream */
} dev_att_t;


/* where devices are located */
#define	DEVDIR		"/dev"

/* how long to wait for dlpi requests to succeed */
#define	DLPI_TIMEOUT	60

/*
 * Structure encapsulating the real physical MAC address as well as the
 * SAP address
 */
struct	etherdladdr {
	struct ether_addr	dl_phys;
	ushort_t		dl_sap;
};

/*
 * Global functions
 */
int	dlpi_get_address(char *, struct ether_addr *);
int	get_net_if_names(char ***);
void	free_net_if_names(char **);

#ifdef __cplusplus
}
#endif

#endif /* _ETHERADDR_H */
