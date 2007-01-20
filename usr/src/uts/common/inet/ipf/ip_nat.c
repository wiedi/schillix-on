/*
 * Copyright (C) 1995-2003 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#if defined(KERNEL) || defined(_KERNEL)
# undef KERNEL
# undef _KERNEL
# define        KERNEL	1
# define        _KERNEL	1
#endif
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/file.h>
#if defined(__NetBSD__) && (NetBSD >= 199905) && !defined(IPFILTER_LKM) && \
    defined(_KERNEL)
# include "opt_ipfilter_log.h"
#endif
#if !defined(_KERNEL)
# include <stdio.h>
# include <string.h>
# include <stdlib.h>
# define _KERNEL
# ifdef __OpenBSD__
struct file;
# endif
# include <sys/uio.h>
# undef _KERNEL
#endif
#if defined(_KERNEL) && (__FreeBSD_version >= 220000)
# include <sys/filio.h>
# include <sys/fcntl.h>
#else
# include <sys/ioctl.h>
#endif
#if !defined(AIX)
# include <sys/fcntl.h>
#endif
#if !defined(linux)
# include <sys/protosw.h>
#endif
#include <sys/socket.h>
#if defined(_KERNEL)
# include <sys/systm.h>
# if !defined(__SVR4) && !defined(__svr4__)
#  include <sys/mbuf.h>
# endif
#endif
#if defined(__SVR4) || defined(__svr4__)
# include <sys/filio.h>
# include <sys/byteorder.h>
# ifdef _KERNEL
#  include <sys/dditypes.h>
# endif
# include <sys/stream.h>
# include <sys/kmem.h>
#endif
#if __FreeBSD_version >= 300000
# include <sys/queue.h>
#endif
#include <net/if.h>
#if __FreeBSD_version >= 300000
# include <net/if_var.h>
# if defined(_KERNEL) && !defined(IPFILTER_LKM)
#  include "opt_ipfilter.h"
# endif
#endif
#ifdef sun
# include <net/af.h>
#endif
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>

#ifdef RFC1825
# include <vpn/md5.h>
# include <vpn/ipsec.h>
extern struct ifnet vpnif;
#endif

#if !defined(linux)
# include <netinet/ip_var.h>
#endif
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include "netinet/ip_compat.h"
#include <netinet/tcpip.h>
#include "netinet/ip_fil.h"
#include "netinet/ip_nat.h"
#include "netinet/ip_frag.h"
#include "netinet/ip_state.h"
#include "netinet/ip_proxy.h"
#include "netinet/ipf_stack.h"
#ifdef	IPFILTER_SYNC
#include "netinet/ip_sync.h"
#endif
#if (__FreeBSD_version >= 300000)
# include <sys/malloc.h>
#endif
/* END OF INCLUDES */

#undef	SOCKADDR_IN
#define	SOCKADDR_IN	struct sockaddr_in

#if !defined(lint)
static const char sccsid[] = "@(#)ip_nat.c	1.11 6/5/96 (C) 1995 Darren Reed";
static const char rcsid[] = "@(#)$Id: ip_nat.c,v 2.195.2.42 2005/08/11 19:51:36 darrenr Exp $";
#endif


/* ======================================================================== */
/* How the NAT is organised and works.                                      */
/*                                                                          */
/* Inside (interface y) NAT       Outside (interface x)                     */
/* -------------------- -+- -------------------------------------           */
/* Packet going          |   out, processsed by fr_checknatout() for x      */
/* ------------>         |   ------------>                                  */
/* src=10.1.1.1          |   src=192.1.1.1                                  */
/*                       |                                                  */
/*                       |   in, processed by fr_checknatin() for x         */
/* <------------         |   <------------                                  */
/* dst=10.1.1.1          |   dst=192.1.1.1                                  */
/* -------------------- -+- -------------------------------------           */
/* fr_checknatout() - changes ip_src and if required, sport                 */
/*             - creates a new mapping, if required.                        */
/* fr_checknatin()  - changes ip_dst and if required, dport                 */
/*                                                                          */
/* In the NAT table, internal source is recorded as "in" and externally     */
/* seen as "out".                                                           */
/* ======================================================================== */



static	int	nat_flushtable __P((ipf_stack_t *));
static	int	nat_clearlist __P((ipf_stack_t *));
static	void	nat_addnat __P((struct ipnat *, ipf_stack_t *));
static	void	nat_addrdr __P((struct ipnat *, ipf_stack_t *));
static	void	nat_delete __P((struct nat *, int, ipf_stack_t *));
static	void	nat_delrdr __P((struct ipnat *));
static	void	nat_delnat __P((struct ipnat *));
static	int	fr_natgetent __P((caddr_t, ipf_stack_t *));
static	int	fr_natgetsz __P((caddr_t, ipf_stack_t *));
static	int	fr_natputent __P((caddr_t, int, ipf_stack_t *));
static	void	nat_tabmove __P((nat_t *, ipf_stack_t *));
static	int	nat_match __P((fr_info_t *, ipnat_t *));
static	INLINE	int nat_newmap __P((fr_info_t *, nat_t *, natinfo_t *));
static	INLINE	int nat_newrdr __P((fr_info_t *, nat_t *, natinfo_t *));
static	hostmap_t *nat_hostmap __P((ipnat_t *, struct in_addr,
				    struct in_addr, struct in_addr, u_32_t, 
				    ipf_stack_t *));
static	void	nat_hostmapdel __P((struct hostmap *));
static	INLINE	int nat_icmpquerytype4 __P((int));
static	int	nat_siocaddnat __P((ipnat_t *, ipnat_t **, int,
				    ipf_stack_t *));
static	void	nat_siocdelnat __P((ipnat_t *, ipnat_t **, int,
				    ipf_stack_t *));
static	INLINE	int nat_icmperrortype4 __P((int));
static	INLINE	int nat_finalise __P((fr_info_t *, nat_t *, natinfo_t *,
				      tcphdr_t *, nat_t **, int));
static	INLINE	void nat_resolverule __P((ipnat_t *, ipf_stack_t *));
static	nat_t	*fr_natclone __P((fr_info_t *, nat_t *));
static	void	nat_mssclamp __P((tcphdr_t *, u_32_t, u_short *));
static	INLINE	int nat_wildok __P((nat_t *, int, int, int, int));
static	int	nat_getnext __P((ipftoken_t *, ipfgeniter_t *, ipf_stack_t *));
static	int	nat_iterator __P((ipftoken_t *, ipfgeniter_t *, ipf_stack_t *));


/* ------------------------------------------------------------------------ */
/* Function:    fr_natinit                                                  */
/* Returns:     int - 0 == success, -1 == failure                           */
/* Parameters:  Nil                                                         */
/*                                                                          */
/* Initialise all of the NAT locks, tables and other structures.            */
/* ------------------------------------------------------------------------ */
int fr_natinit(ifs)
ipf_stack_t *ifs;
{
	int i;

	ifs->ifs_ipf_nattable_sz = NAT_TABLE_SZ;
	ifs->ifs_ipf_nattable_max = NAT_TABLE_MAX;
	ifs->ifs_ipf_natrules_sz = NAT_SIZE;
	ifs->ifs_ipf_rdrrules_sz = RDR_SIZE;
	ifs->ifs_ipf_hostmap_sz = HOSTMAP_SIZE;
	ifs->ifs_fr_nat_maxbucket_reset = 1;
#ifdef  IPFILTER_LOG
	ifs->ifs_nat_logging = 1;
#else
	ifs->ifs_nat_logging = 0;
#endif
	ifs->ifs_fr_defnatage = DEF_NAT_AGE;
	ifs->ifs_fr_defnatipage = 120;		/* 60 seconds */
	ifs->ifs_fr_defnaticmpage = 6;		/* 3 seconds */

	KMALLOCS(ifs->ifs_nat_table[0], nat_t **,
		 sizeof(nat_t *) * ifs->ifs_ipf_nattable_sz);
	if (ifs->ifs_nat_table[0] != NULL)
		bzero((char *)ifs->ifs_nat_table[0],
		      ifs->ifs_ipf_nattable_sz * sizeof(nat_t *));
	else
		return -1;

	KMALLOCS(ifs->ifs_nat_table[1], nat_t **,
		 sizeof(nat_t *) * ifs->ifs_ipf_nattable_sz);
	if (ifs->ifs_nat_table[1] != NULL)
		bzero((char *)ifs->ifs_nat_table[1],
		      ifs->ifs_ipf_nattable_sz * sizeof(nat_t *));
	else
		return -2;

	KMALLOCS(ifs->ifs_nat_rules, ipnat_t **,
		 sizeof(ipnat_t *) * ifs->ifs_ipf_natrules_sz);
	if (ifs->ifs_nat_rules != NULL)
		bzero((char *)ifs->ifs_nat_rules,
		      ifs->ifs_ipf_natrules_sz * sizeof(ipnat_t *));
	else
		return -3;

	KMALLOCS(ifs->ifs_rdr_rules, ipnat_t **,
		 sizeof(ipnat_t *) * ifs->ifs_ipf_rdrrules_sz);
	if (ifs->ifs_rdr_rules != NULL)
		bzero((char *)ifs->ifs_rdr_rules,
		      ifs->ifs_ipf_rdrrules_sz * sizeof(ipnat_t *));
	else
		return -4;

	KMALLOCS(ifs->ifs_maptable, hostmap_t **,
		 sizeof(hostmap_t *) * ifs->ifs_ipf_hostmap_sz);
	if (ifs->ifs_maptable != NULL)
		bzero((char *)ifs->ifs_maptable,
		      sizeof(hostmap_t *) * ifs->ifs_ipf_hostmap_sz);
	else
		return -5;

	ifs->ifs_ipf_hm_maplist = NULL;

	KMALLOCS(ifs->ifs_nat_stats.ns_bucketlen[0], u_long *,
		 ifs->ifs_ipf_nattable_sz * sizeof(u_long));
	if (ifs->ifs_nat_stats.ns_bucketlen[0] == NULL)
		return -1;
	bzero((char *)ifs->ifs_nat_stats.ns_bucketlen[0],
	      ifs->ifs_ipf_nattable_sz * sizeof(u_long));

	KMALLOCS(ifs->ifs_nat_stats.ns_bucketlen[1], u_long *,
		 ifs->ifs_ipf_nattable_sz * sizeof(u_long));
	if (ifs->ifs_nat_stats.ns_bucketlen[1] == NULL)
		return -1;
	bzero((char *)ifs->ifs_nat_stats.ns_bucketlen[1],
	      ifs->ifs_ipf_nattable_sz * sizeof(u_long));

	if (ifs->ifs_fr_nat_maxbucket == 0) {
		for (i = ifs->ifs_ipf_nattable_sz; i > 0; i >>= 1)
			ifs->ifs_fr_nat_maxbucket++;
		ifs->ifs_fr_nat_maxbucket *= 2;
	}

	fr_sttab_init(ifs->ifs_nat_tqb, ifs);
	/*
	 * Increase this because we may have "keep state" following this too
	 * and packet storms can occur if this is removed too quickly.
	 */
	ifs->ifs_nat_tqb[IPF_TCPS_CLOSED].ifq_ttl = ifs->ifs_fr_tcplastack;
	ifs->ifs_nat_tqb[IPF_TCP_NSTATES - 1].ifq_next = &ifs->ifs_nat_udptq;
	ifs->ifs_nat_udptq.ifq_ttl = ifs->ifs_fr_defnatage;
	ifs->ifs_nat_udptq.ifq_ref = 1;
	ifs->ifs_nat_udptq.ifq_head = NULL;
	ifs->ifs_nat_udptq.ifq_tail = &ifs->ifs_nat_udptq.ifq_head;
	MUTEX_INIT(&ifs->ifs_nat_udptq.ifq_lock, "nat ipftq udp tab");
	ifs->ifs_nat_udptq.ifq_next = &ifs->ifs_nat_icmptq;
	ifs->ifs_nat_icmptq.ifq_ttl = ifs->ifs_fr_defnaticmpage;
	ifs->ifs_nat_icmptq.ifq_ref = 1;
	ifs->ifs_nat_icmptq.ifq_head = NULL;
	ifs->ifs_nat_icmptq.ifq_tail = &ifs->ifs_nat_icmptq.ifq_head;
	MUTEX_INIT(&ifs->ifs_nat_icmptq.ifq_lock, "nat icmp ipftq tab");
	ifs->ifs_nat_icmptq.ifq_next = &ifs->ifs_nat_iptq;
	ifs->ifs_nat_iptq.ifq_ttl = ifs->ifs_fr_defnatipage;
	ifs->ifs_nat_iptq.ifq_ref = 1;
	ifs->ifs_nat_iptq.ifq_head = NULL;
	ifs->ifs_nat_iptq.ifq_tail = &ifs->ifs_nat_iptq.ifq_head;
	MUTEX_INIT(&ifs->ifs_nat_iptq.ifq_lock, "nat ip ipftq tab");
	ifs->ifs_nat_iptq.ifq_next = NULL;

	for (i = 0; i < IPF_TCP_NSTATES; i++) {
		if (ifs->ifs_nat_tqb[i].ifq_ttl < ifs->ifs_fr_defnaticmpage)
			ifs->ifs_nat_tqb[i].ifq_ttl = ifs->ifs_fr_defnaticmpage;
#ifdef LARGE_NAT
		else if (ifs->ifs_nat_tqb[i].ifq_ttl > ifs->ifs_fr_defnatage)
			ifs->ifs_nat_tqb[i].ifq_ttl = ifs->ifs_fr_defnatage;
#endif
	}

	/*
	 * Increase this because we may have "keep state" following
	 * this too and packet storms can occur if this is removed
	 * too quickly.
	 */
	ifs->ifs_nat_tqb[IPF_TCPS_CLOSED].ifq_ttl =
	    ifs->ifs_nat_tqb[IPF_TCPS_LAST_ACK].ifq_ttl;

	RWLOCK_INIT(&ifs->ifs_ipf_nat, "ipf IP NAT rwlock");
	RWLOCK_INIT(&ifs->ifs_ipf_natfrag, "ipf IP NAT-Frag rwlock");
	MUTEX_INIT(&ifs->ifs_ipf_nat_new, "ipf nat new mutex");
	MUTEX_INIT(&ifs->ifs_ipf_natio, "ipf nat io mutex");

	ifs->ifs_fr_nat_init = 1;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_addrdr                                                  */
/* Returns:     Nil                                                         */
/* Parameters:  n(I) - pointer to NAT rule to add                           */
/*                                                                          */
/* Adds a redirect rule to the hash table of redirect rules and the list of */
/* loaded NAT rules.  Updates the bitmask indicating which netmasks are in  */
/* use by redirect rules.                                                   */
/* ------------------------------------------------------------------------ */
static void nat_addrdr(n, ifs)
ipnat_t *n;
ipf_stack_t *ifs;
{
	ipnat_t **np;
	u_32_t j;
	u_int hv;
	int k;

	k = count4bits(n->in_outmsk);
	if ((k >= 0) && (k != 32))
		ifs->ifs_rdr_masks |= 1 << k;
	j = (n->in_outip & n->in_outmsk);
	hv = NAT_HASH_FN(j, 0, ifs->ifs_ipf_rdrrules_sz);
	np = ifs->ifs_rdr_rules + hv;
	while (*np != NULL)
		np = &(*np)->in_rnext;
	n->in_rnext = NULL;
	n->in_prnext = np;
	n->in_hv = hv;
	*np = n;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_addnat                                                  */
/* Returns:     Nil                                                         */
/* Parameters:  n(I) - pointer to NAT rule to add                           */
/*                                                                          */
/* Adds a NAT map rule to the hash table of rules and the list of  loaded   */
/* NAT rules.  Updates the bitmask indicating which netmasks are in use by  */
/* redirect rules.                                                          */
/* ------------------------------------------------------------------------ */
static void nat_addnat(n, ifs)
ipnat_t *n;
ipf_stack_t *ifs;
{
	ipnat_t **np;
	u_32_t j;
	u_int hv;
	int k;

	k = count4bits(n->in_inmsk);
	if ((k >= 0) && (k != 32))
		ifs->ifs_nat_masks |= 1 << k;
	j = (n->in_inip & n->in_inmsk);
	hv = NAT_HASH_FN(j, 0, ifs->ifs_ipf_natrules_sz);
	np = ifs->ifs_nat_rules + hv;
	while (*np != NULL)
		np = &(*np)->in_mnext;
	n->in_mnext = NULL;
	n->in_pmnext = np;
	n->in_hv = hv;
	*np = n;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_delrdr                                                  */
/* Returns:     Nil                                                         */
/* Parameters:  n(I) - pointer to NAT rule to delete                        */
/*                                                                          */
/* Removes a redirect rule from the hash table of redirect rules.           */
/* ------------------------------------------------------------------------ */
static void nat_delrdr(n)
ipnat_t *n;
{
	if (n->in_rnext)
		n->in_rnext->in_prnext = n->in_prnext;
	*n->in_prnext = n->in_rnext;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_delnat                                                  */
/* Returns:     Nil                                                         */
/* Parameters:  n(I) - pointer to NAT rule to delete                        */
/*                                                                          */
/* Removes a NAT map rule from the hash table of NAT map rules.             */
/* ------------------------------------------------------------------------ */
static void nat_delnat(n)
ipnat_t *n;
{
	if (n->in_mnext != NULL)
		n->in_mnext->in_pmnext = n->in_pmnext;
	*n->in_pmnext = n->in_mnext;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_hostmap                                                 */
/* Returns:     struct hostmap* - NULL if no hostmap could be created,      */
/*                                else a pointer to the hostmapping to use  */
/* Parameters:  np(I)   - pointer to NAT rule                               */
/*              real(I) - real IP address                                   */
/*              map(I)  - mapped IP address                                 */
/*              port(I) - destination port number                           */
/* Write Locks: ipf_nat                                                     */
/*                                                                          */
/* Check if an ip address has already been allocated for a given mapping    */
/* that is not doing port based translation.  If is not yet allocated, then */
/* create a new entry if a non-NULL NAT rule pointer has been supplied.     */
/* ------------------------------------------------------------------------ */
static struct hostmap *nat_hostmap(np, src, dst, map, port, ifs)
ipnat_t *np;
struct in_addr src;
struct in_addr dst;
struct in_addr map;
u_32_t port;
ipf_stack_t *ifs;
{
	hostmap_t *hm;
	u_int hv;

	hv = (src.s_addr ^ dst.s_addr);
	hv += src.s_addr;
	hv += dst.s_addr;
	hv %= HOSTMAP_SIZE;
	for (hm = ifs->ifs_maptable[hv]; hm; hm = hm->hm_next)
		if ((hm->hm_srcip.s_addr == src.s_addr) &&
		    (hm->hm_dstip.s_addr == dst.s_addr) &&
		    ((np == NULL) || (np == hm->hm_ipnat)) &&
		    ((port == 0) || (port == hm->hm_port))) {
			hm->hm_ref++;
			return hm;
		}

	if (np == NULL)
		return NULL;

	KMALLOC(hm, hostmap_t *);
	if (hm) {
		hm->hm_hnext = ifs->ifs_ipf_hm_maplist;
		hm->hm_phnext = &ifs->ifs_ipf_hm_maplist;
		if (ifs->ifs_ipf_hm_maplist != NULL)
			ifs->ifs_ipf_hm_maplist->hm_phnext = &hm->hm_hnext;
		ifs->ifs_ipf_hm_maplist = hm;

		hm->hm_next = ifs->ifs_maptable[hv];
		hm->hm_pnext = ifs->ifs_maptable + hv;
		if (ifs->ifs_maptable[hv] != NULL)
			ifs->ifs_maptable[hv]->hm_pnext = &hm->hm_next;
		ifs->ifs_maptable[hv] = hm;
		hm->hm_ipnat = np;
		hm->hm_srcip = src;
		hm->hm_dstip = dst;
		hm->hm_mapip = map;
		hm->hm_ref = 1;
		hm->hm_port = port;
	}
	return hm;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_hostmapdel                                              */
/* Returns:     Nil                                                         */
/* Parameters:  hm(I) - pointer to hostmap structure                        */
/* Write Locks: ipf_nat                                                     */
/*                                                                          */
/* Decrement the references to this hostmap structure by one.  If this      */
/* reaches zero then remove it and free it.                                 */
/* ------------------------------------------------------------------------ */
static void nat_hostmapdel(hm)
struct hostmap *hm;
{
	hm->hm_ref--;
	if (hm->hm_ref == 0) {
		if (hm->hm_next)
			hm->hm_next->hm_pnext = hm->hm_pnext;
		*hm->hm_pnext = hm->hm_next;
		if (hm->hm_hnext)
			hm->hm_hnext->hm_phnext = hm->hm_phnext;
		*hm->hm_phnext = hm->hm_hnext;
		KFREE(hm);
	}
}

void fr_hostmapderef(hmp)
struct hostmap **hmp;
{
	struct hostmap *hm;

	hm = *hmp;
	*hmp = NULL;
	hm->hm_ref--;
	if (hm->hm_ref == 0)
		nat_hostmapdel(hm);
}


/* ------------------------------------------------------------------------ */
/* Function:    fix_outcksum                                                */
/* Returns:     Nil                                                         */
/* Parameters:  sp(I)  - location of 16bit checksum to update               */
/*              n((I)  - amount to adjust checksum by                       */
/*                                                                          */
/* Adjusts the 16bit checksum by "n" for packets going out.                 */
/* ------------------------------------------------------------------------ */
void fix_outcksum(sp, n)
u_short *sp;
u_32_t n;
{
	u_short sumshort;
	u_32_t sum1;

	if (n == 0)
		return;

	sum1 = (~ntohs(*sp)) & 0xffff;
	sum1 += (n);
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	/* Again */
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	sumshort = ~(u_short)sum1;
	*(sp) = htons(sumshort);
}


/* ------------------------------------------------------------------------ */
/* Function:    fix_incksum                                                 */
/* Returns:     Nil                                                         */
/* Parameters:  sp(I)  - location of 16bit checksum to update               */
/*              n((I)  - amount to adjust checksum by                       */
/*                                                                          */
/* Adjusts the 16bit checksum by "n" for packets going in.                  */
/* ------------------------------------------------------------------------ */
void fix_incksum(sp, n)
u_short *sp;
u_32_t n;
{
	u_short sumshort;
	u_32_t sum1;

	if (n == 0)
		return;

	sum1 = (~ntohs(*sp)) & 0xffff;
	sum1 += ~(n) & 0xffff;
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	/* Again */
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	sumshort = ~(u_short)sum1;
	*(sp) = htons(sumshort);
}


/* ------------------------------------------------------------------------ */
/* Function:    fix_datacksum                                               */
/* Returns:     Nil                                                         */
/* Parameters:  sp(I)  - location of 16bit checksum to update               */
/*              n((I)  - amount to adjust checksum by                       */
/*                                                                          */
/* Fix_datacksum is used *only* for the adjustments of checksums in the     */
/* data section of an IP packet.                                            */
/*                                                                          */
/* The only situation in which you need to do this is when NAT'ing an       */
/* ICMP error message. Such a message, contains in its body the IP header   */
/* of the original IP packet, that causes the error.                        */
/*                                                                          */
/* You can't use fix_incksum or fix_outcksum in that case, because for the  */
/* kernel the data section of the ICMP error is just data, and no special   */
/* processing like hardware cksum or ntohs processing have been done by the */
/* kernel on the data section.                                              */
/* ------------------------------------------------------------------------ */
void fix_datacksum(sp, n)
u_short *sp;
u_32_t n;
{
	u_short sumshort;
	u_32_t sum1;

	if (n == 0)
		return;

	sum1 = (~ntohs(*sp)) & 0xffff;
	sum1 += (n);
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	/* Again */
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	sumshort = ~(u_short)sum1;
	*(sp) = htons(sumshort);
}


/* ------------------------------------------------------------------------ */
/* Function:    fr_nat_ioctl                                                */
/* Returns:     int - 0 == success, != 0 == failure                         */
/* Parameters:  data(I) - pointer to ioctl data                             */
/*              cmd(I)  - ioctl command integer                             */
/*              mode(I) - file mode bits used with open                     */
/*                                                                          */
/* Processes an ioctl call made to operate on the IP Filter NAT device.     */
/* ------------------------------------------------------------------------ */
int fr_nat_ioctl(data, cmd, mode, uid, ctx, ifs)
ioctlcmd_t cmd;
caddr_t data;
int mode, uid;
void *ctx;
ipf_stack_t *ifs;
{
	ipnat_t *nat, *nt, *n = NULL, **np = NULL;
	int error = 0, ret, arg, getlock;
	ipnat_t natd;

#if (BSD >= 199306) && defined(_KERNEL)
	if ((securelevel >= 2) && (mode & FWRITE))
		return EPERM;
#endif

#if defined(__osf__) && defined(_KERNEL)
	getlock = 0;
#else
	getlock = (mode & NAT_LOCKHELD) ? 0 : 1;
#endif

	nat = NULL;     /* XXX gcc -Wuninitialized */
	if (cmd == (ioctlcmd_t)SIOCADNAT) {
		KMALLOC(nt, ipnat_t *);
	} else {
		nt = NULL;
	}

	if ((cmd == (ioctlcmd_t)SIOCADNAT) || (cmd == (ioctlcmd_t)SIOCRMNAT)) {
		if (mode & NAT_SYSSPACE) {
			bcopy(data, (char *)&natd, sizeof(natd));
			error = 0;
		} else {
			error = fr_inobj(data, &natd, IPFOBJ_IPNAT);
		}

	} else if (cmd == (ioctlcmd_t)SIOCIPFFL) { /* SIOCFLNAT & SIOCCNATL */
		BCOPYIN(data, &arg, sizeof(arg));
	}

	if (error != 0)
		goto done;

	/*
	 * For add/delete, look to see if the NAT entry is already present
	 */
	if ((cmd == (ioctlcmd_t)SIOCADNAT) || (cmd == (ioctlcmd_t)SIOCRMNAT)) {
		nat = &natd;
		if (nat->in_v == 0)	/* For backward compat. */
			nat->in_v = 4;
		nat->in_flags &= IPN_USERFLAGS;
		if ((nat->in_redir & NAT_MAPBLK) == 0) {
			if ((nat->in_flags & IPN_SPLIT) == 0)
				nat->in_inip &= nat->in_inmsk;
			if ((nat->in_flags & IPN_IPRANGE) == 0)
				nat->in_outip &= nat->in_outmsk;
		}
		MUTEX_ENTER(&ifs->ifs_ipf_natio);
		for (np = &ifs->ifs_nat_list; ((n = *np) != NULL);
		     np = &n->in_next)
			if (!bcmp((char *)&nat->in_flags, (char *)&n->in_flags,
					IPN_CMPSIZ))
				break;
	}

	switch (cmd)
	{
	case SIOCGENITER :
	    {
		ipfgeniter_t iter;
		ipftoken_t *token;

		error = fr_inobj(data, &iter, IPFOBJ_GENITER);
		if (error != 0)
			break;

		token = ipf_findtoken(iter.igi_type, uid, ctx, ifs);
		if (token != NULL)
			error  = nat_iterator(token, &iter, ifs);
		else
			error = ESRCH;
		RWLOCK_EXIT(&ifs->ifs_ipf_tokens);
		break;
	    }
#ifdef  IPFILTER_LOG
	case SIOCIPFFB :
	{
		int tmp;

		if (!(mode & FWRITE))
			error = EPERM;
		else {
			tmp = ipflog_clear(IPL_LOGNAT, ifs);
			BCOPYOUT((char *)&tmp, (char *)data, sizeof(tmp));
		}
		break;
	}
	case SIOCSETLG :
		if (!(mode & FWRITE))
			error = EPERM;
		else {
			BCOPYIN((char *)data,
				       (char *)&ifs->ifs_nat_logging,
				sizeof(ifs->ifs_nat_logging));
		}
		break;
	case SIOCGETLG :
		BCOPYOUT((char *)&ifs->ifs_nat_logging, (char *)data,
			sizeof(ifs->ifs_nat_logging));
		break;
	case FIONREAD :
		arg = ifs->ifs_iplused[IPL_LOGNAT];
		BCOPYOUT(&arg, data, sizeof(arg));
		break;
#endif
	case SIOCADNAT :
		if (!(mode & FWRITE)) {
			error = EPERM;
		} else if (n != NULL) {
			error = EEXIST;
		} else if (nt == NULL) {
			error = ENOMEM;
		}
		if (error != 0) {
			MUTEX_EXIT(&ifs->ifs_ipf_natio);
			break;
		}
		bcopy((char *)nat, (char *)nt, sizeof(*n));
		error = nat_siocaddnat(nt, np, getlock, ifs);
		MUTEX_EXIT(&ifs->ifs_ipf_natio);
		if (error == 0)
			nt = NULL;
		break;
	case SIOCRMNAT :
		if (!(mode & FWRITE)) {
			error = EPERM;
			n = NULL;
		} else if (n == NULL) {
			error = ESRCH;
		}

		if (error != 0) {
			MUTEX_EXIT(&ifs->ifs_ipf_natio);
			break;
		}
		nat_siocdelnat(n, np, getlock, ifs);

		MUTEX_EXIT(&ifs->ifs_ipf_natio);
		n = NULL;
		break;
	case SIOCGNATS :
		ifs->ifs_nat_stats.ns_table[0] = ifs->ifs_nat_table[0];
		ifs->ifs_nat_stats.ns_table[1] = ifs->ifs_nat_table[1];
		ifs->ifs_nat_stats.ns_list = ifs->ifs_nat_list;
		ifs->ifs_nat_stats.ns_maptable = ifs->ifs_maptable;
		ifs->ifs_nat_stats.ns_maplist = ifs->ifs_ipf_hm_maplist;
		ifs->ifs_nat_stats.ns_nattab_max = ifs->ifs_ipf_nattable_max;
		ifs->ifs_nat_stats.ns_nattab_sz = ifs->ifs_ipf_nattable_sz;
		ifs->ifs_nat_stats.ns_rultab_sz = ifs->ifs_ipf_natrules_sz;
		ifs->ifs_nat_stats.ns_rdrtab_sz = ifs->ifs_ipf_rdrrules_sz;
		ifs->ifs_nat_stats.ns_hostmap_sz = ifs->ifs_ipf_hostmap_sz;
		ifs->ifs_nat_stats.ns_instances = ifs->ifs_nat_instances;
		ifs->ifs_nat_stats.ns_apslist = ifs->ifs_ap_sess_list;
		error = fr_outobj(data, &ifs->ifs_nat_stats, IPFOBJ_NATSTAT);
		break;
	case SIOCGNATL :
	    {
		natlookup_t nl;

		if (getlock) {
			READ_ENTER(&ifs->ifs_ipf_nat);
		}
		error = fr_inobj(data, &nl, IPFOBJ_NATLOOKUP);
		if (error == 0) {
			if (nat_lookupredir(&nl, ifs) != NULL) {
				error = fr_outobj(data, &nl, IPFOBJ_NATLOOKUP);
			} else {
				error = ESRCH;
			}
		}
		if (getlock) {
			RWLOCK_EXIT(&ifs->ifs_ipf_nat);
		}
		break;
	    }
	case SIOCIPFFL :	/* old SIOCFLNAT & SIOCCNATL */
		if (!(mode & FWRITE)) {
			error = EPERM;
			break;
		}
		if (getlock) {
			WRITE_ENTER(&ifs->ifs_ipf_nat);
		}
		error = 0;
		if (arg == 0)
			ret = nat_flushtable(ifs);
		else if (arg == 1)
			ret = nat_clearlist(ifs);
		else
			error = EINVAL;
		if (getlock) {
			RWLOCK_EXIT(&ifs->ifs_ipf_nat);
		}
		if (error == 0) {
			BCOPYOUT(&ret, data, sizeof(ret));
		}
		break;
	case SIOCPROXY :
		error = appr_ioctl(data, cmd, mode, ifs);
		break;
	case SIOCSTLCK :
		if (!(mode & FWRITE)) {
			error = EPERM;
		} else {
			fr_lock(data, &ifs->ifs_fr_nat_lock);
		}
		break;
	case SIOCSTPUT :
		if ((mode & FWRITE) != 0) {
			error = fr_natputent(data, getlock, ifs);
		} else {
			error = EACCES;
		}
		break;
	case SIOCSTGSZ :
		if (ifs->ifs_fr_nat_lock) {
			if (getlock) {
				READ_ENTER(&ifs->ifs_ipf_nat);
			}
			error = fr_natgetsz(data, ifs);
			if (getlock) {
				RWLOCK_EXIT(&ifs->ifs_ipf_nat);
			}
		} else
			error = EACCES;
		break;
	case SIOCSTGET :
		if (ifs->ifs_fr_nat_lock) {
			if (getlock) {
				READ_ENTER(&ifs->ifs_ipf_nat);
			}
			error = fr_natgetent(data, ifs);
			if (getlock) {
				RWLOCK_EXIT(&ifs->ifs_ipf_nat);
			}
		} else
			error = EACCES;
		break;
	case SIOCIPFDELTOK :
		(void) BCOPYIN((caddr_t)data, (caddr_t)&arg, sizeof(arg));
		error = ipf_deltoken(arg, uid, ctx, ifs);
		break;
	default :
		error = EINVAL;
		break;
	}
done:
	if (nt)
		KFREE(nt);
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_siocaddnat                                              */
/* Returns:     int - 0 == success, != 0 == failure                         */
/* Parameters:  n(I)       - pointer to new NAT rule                        */
/*              np(I)      - pointer to where to insert new NAT rule        */
/*              getlock(I) - flag indicating if lock on ipf_nat is held     */
/* Mutex Locks: ipf_natio                                                   */
/*                                                                          */
/* Handle SIOCADNAT.  Resolve and calculate details inside the NAT rule     */
/* from information passed to the kernel, then add it  to the appropriate   */
/* NAT rule table(s).                                                       */
/* ------------------------------------------------------------------------ */
static int nat_siocaddnat(n, np, getlock, ifs)
ipnat_t *n, **np;
int getlock;
ipf_stack_t *ifs;
{
	int error = 0, i, j;

	nat_resolverule(n, ifs);
	if (n->in_plabel[0] != '\0') {
		if (n->in_apr == NULL)
			return ENOENT;
	}

	if ((n->in_age[0] == 0) && (n->in_age[1] != 0))
		return EINVAL;

	n->in_use = 0;
	if (n->in_redir & NAT_MAPBLK)
		n->in_space = USABLE_PORTS * ~ntohl(n->in_outmsk);
	else if (n->in_flags & IPN_AUTOPORTMAP)
		n->in_space = USABLE_PORTS * ~ntohl(n->in_inmsk);
	else if (n->in_flags & IPN_IPRANGE)
		n->in_space = ntohl(n->in_outmsk) - ntohl(n->in_outip);
	else if (n->in_flags & IPN_SPLIT)
		n->in_space = 2;
	else if (n->in_outmsk != 0)
		n->in_space = ~ntohl(n->in_outmsk);
	else
		n->in_space = 1;

	/*
	 * Calculate the number of valid IP addresses in the output
	 * mapping range.  In all cases, the range is inclusive of
	 * the start and ending IP addresses.
	 * If to a CIDR address, lose 2: broadcast + network address
	 *                               (so subtract 1)
	 * If to a range, add one.
	 * If to a single IP address, set to 1.
	 */
	if (n->in_space) {
		if ((n->in_flags & IPN_IPRANGE) != 0)
			n->in_space += 1;
		else
			n->in_space -= 1;
	} else
		n->in_space = 1;

	if ((n->in_outmsk != 0xffffffff) && (n->in_outmsk != 0) &&
	    ((n->in_flags & (IPN_IPRANGE|IPN_SPLIT)) == 0))
		n->in_nip = ntohl(n->in_outip) + 1;
	else if ((n->in_flags & IPN_SPLIT) &&
		 (n->in_redir & NAT_REDIRECT))
		n->in_nip = ntohl(n->in_inip);
	else
		n->in_nip = ntohl(n->in_outip);
	if (n->in_redir & NAT_MAP) {
		n->in_pnext = ntohs(n->in_pmin);
		/*
		 * Multiply by the number of ports made available.
		 */
		if (ntohs(n->in_pmax) >= ntohs(n->in_pmin)) {
			n->in_space *= (ntohs(n->in_pmax) -
					ntohs(n->in_pmin) + 1);
			/*
			 * Because two different sources can map to
			 * different destinations but use the same
			 * local IP#/port #.
			 * If the result is smaller than in_space, then
			 * we may have wrapped around 32bits.
			 */
			i = n->in_inmsk;
			if ((i != 0) && (i != 0xffffffff)) {
				j = n->in_space * (~ntohl(i) + 1);
				if (j >= n->in_space)
					n->in_space = j;
				else
					n->in_space = 0xffffffff;
			}
		}
		/*
		 * If no protocol is specified, multiple by 256 to allow for
		 * at least one IP:IP mapping per protocol.
		 */
		if ((n->in_flags & IPN_TCPUDPICMP) == 0) {
				j = n->in_space * 256;
				if (j >= n->in_space)
					n->in_space = j;
				else
					n->in_space = 0xffffffff;
		}
	}

	/* Otherwise, these fields are preset */

	if (getlock) {
		WRITE_ENTER(&ifs->ifs_ipf_nat);
	}
	n->in_next = NULL;
	*np = n;

	if (n->in_age[0] != 0)
	    n->in_tqehead[0] = fr_addtimeoutqueue(&ifs->ifs_nat_utqe,
						  n->in_age[0], ifs);

	if (n->in_age[1] != 0)
	    n->in_tqehead[1] = fr_addtimeoutqueue(&ifs->ifs_nat_utqe,
						  n->in_age[1], ifs);

	if (n->in_redir & NAT_REDIRECT) {
		n->in_flags &= ~IPN_NOTDST;
		nat_addrdr(n, ifs);
	}
	if (n->in_redir & (NAT_MAP|NAT_MAPBLK)) {
		n->in_flags &= ~IPN_NOTSRC;
		nat_addnat(n, ifs);
	}
	n = NULL;
	ifs->ifs_nat_stats.ns_rules++;
	if (getlock) {
		RWLOCK_EXIT(&ifs->ifs_ipf_nat);			/* WRITE */
	}

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_resolvrule                                              */
/* Returns:     Nil                                                         */
/* Parameters:  n(I)  - pointer to NAT rule                                 */
/*                                                                          */
/* Handle SIOCADNAT.  Resolve and calculate details inside the NAT rule     */
/* from information passed to the kernel, then add it  to the appropriate   */
/* NAT rule table(s).                                                       */
/* ------------------------------------------------------------------------ */
static void nat_resolverule(n, ifs)
ipnat_t *n;
ipf_stack_t *ifs;
{
	n->in_ifnames[0][LIFNAMSIZ - 1] = '\0';
	n->in_ifps[0] = fr_resolvenic(n->in_ifnames[0], 4, ifs);

	n->in_ifnames[1][LIFNAMSIZ - 1] = '\0';
	if (n->in_ifnames[1][0] == '\0') {
		(void) strncpy(n->in_ifnames[1], n->in_ifnames[0], LIFNAMSIZ);
		n->in_ifps[1] = n->in_ifps[0];
	} else {
		n->in_ifps[1] = fr_resolvenic(n->in_ifnames[0], 4, ifs);
	}

	if (n->in_plabel[0] != '\0') {
		n->in_apr = appr_lookup(n->in_p, n->in_plabel, ifs);
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_siocdelnat                                              */
/* Returns:     int - 0 == success, != 0 == failure                         */
/* Parameters:  n(I)       - pointer to new NAT rule                        */
/*              np(I)      - pointer to where to insert new NAT rule        */
/*              getlock(I) - flag indicating if lock on ipf_nat is held     */
/* Mutex Locks: ipf_natio                                                   */
/*                                                                          */
/* Handle SIOCADNAT.  Resolve and calculate details inside the NAT rule     */
/* from information passed to the kernel, then add it  to the appropriate   */
/* NAT rule table(s).                                                       */
/* ------------------------------------------------------------------------ */
static void nat_siocdelnat(n, np, getlock, ifs)
ipnat_t *n, **np;
int getlock;
ipf_stack_t *ifs;
{
	if (getlock) {
		WRITE_ENTER(&ifs->ifs_ipf_nat);
	}
	if (n->in_redir & NAT_REDIRECT)
		nat_delrdr(n);
	if (n->in_redir & (NAT_MAPBLK|NAT_MAP))
		nat_delnat(n);
	if (ifs->ifs_nat_list == NULL) {
		ifs->ifs_nat_masks = 0;
		ifs->ifs_rdr_masks = 0;
	}

	if (n->in_tqehead[0] != NULL) {
		if (fr_deletetimeoutqueue(n->in_tqehead[0]) == 0) {
			fr_freetimeoutqueue(n->in_tqehead[1], ifs);
		}
	}

	if (n->in_tqehead[1] != NULL) {
		if (fr_deletetimeoutqueue(n->in_tqehead[1]) == 0) {
			fr_freetimeoutqueue(n->in_tqehead[1], ifs);
		}
	}

	*np = n->in_next;

	if (n->in_use == 0) {
		if (n->in_apr)
			appr_free(n->in_apr);
		KFREE(n);
		ifs->ifs_nat_stats.ns_rules--;
	} else {
		n->in_flags |= IPN_DELETE;
		n->in_next = NULL;
	}
	if (getlock) {
		RWLOCK_EXIT(&ifs->ifs_ipf_nat);			/* READ/WRITE */
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    fr_natgetsz                                                 */
/* Returns:     int - 0 == success, != 0 is the error value.                */
/* Parameters:  data(I) - pointer to natget structure with kernel pointer   */
/*                        get the size of.                                  */
/*                                                                          */
/* Handle SIOCSTGSZ.                                                        */
/* Return the size of the nat list entry to be copied back to user space.   */
/* The size of the entry is stored in the ng_sz field and the enture natget */
/* structure is copied back to the user.                                    */
/* ------------------------------------------------------------------------ */
static int fr_natgetsz(data, ifs)
caddr_t data;
ipf_stack_t *ifs;
{
	ap_session_t *aps;
	nat_t *nat, *n;
	natget_t ng;

	BCOPYIN(data, &ng, sizeof(ng));

	nat = ng.ng_ptr;
	if (!nat) {
		nat = ifs->ifs_nat_instances;
		ng.ng_sz = 0;
		/*
		 * Empty list so the size returned is 0.  Simple.
		 */
		if (nat == NULL) {
			BCOPYOUT(&ng, data, sizeof(ng));
			return 0;
		}
	} else {
		/*
		 * Make sure the pointer we're copying from exists in the
		 * current list of entries.  Security precaution to prevent
		 * copying of random kernel data.
		 */
		for (n = ifs->ifs_nat_instances; n; n = n->nat_next)
			if (n == nat)
				break;
		if (!n)
			return ESRCH;
	}

	/*
	 * Incluse any space required for proxy data structures.
	 */
	ng.ng_sz = sizeof(nat_save_t);
	aps = nat->nat_aps;
	if (aps != NULL) {
		ng.ng_sz += sizeof(ap_session_t) - 4;
		if (aps->aps_data != 0)
			ng.ng_sz += aps->aps_psiz;
	}

	BCOPYOUT(&ng, data, sizeof(ng));
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    fr_natgetent                                                */
/* Returns:     int - 0 == success, != 0 is the error value.                */
/* Parameters:  data(I) - pointer to natget structure with kernel pointer   */
/*                        to NAT structure to copy out.                     */
/*                                                                          */
/* Handle SIOCSTGET.                                                        */
/* Copies out NAT entry to user space.  Any additional data held for a      */
/* proxy is also copied, as to is the NAT rule which was responsible for it */
/* ------------------------------------------------------------------------ */
static int fr_natgetent(data, ifs)
caddr_t data;
ipf_stack_t *ifs;
{
	int error, outsize;
	ap_session_t *aps;
	nat_save_t *ipn, ipns;
	nat_t *n, *nat;

	error = fr_inobj(data, &ipns, IPFOBJ_NATSAVE);
	if (error != 0)
		return error;

	if ((ipns.ipn_dsize < sizeof(ipns)) || (ipns.ipn_dsize > 81920))
		return EINVAL;

	KMALLOCS(ipn, nat_save_t *, ipns.ipn_dsize);
	if (ipn == NULL)
		return ENOMEM;

	ipn->ipn_dsize = ipns.ipn_dsize;
	nat = ipns.ipn_next;
	if (nat == NULL) {
		nat = ifs->ifs_nat_instances;
		if (nat == NULL) {
			if (ifs->ifs_nat_instances == NULL)
				error = ENOENT;
			goto finished;
		}
	} else {
		/*
		 * Make sure the pointer we're copying from exists in the
		 * current list of entries.  Security precaution to prevent
		 * copying of random kernel data.
		 */
		for (n = ifs->ifs_nat_instances; n; n = n->nat_next)
			if (n == nat)
				break;
		if (n == NULL) {
			error = ESRCH;
			goto finished;
		}
	}
	ipn->ipn_next = nat->nat_next;

	/*
	 * Copy the NAT structure.
	 */
	bcopy((char *)nat, &ipn->ipn_nat, sizeof(*nat));

	/*
	 * If we have a pointer to the NAT rule it belongs to, save that too.
	 */
	if (nat->nat_ptr != NULL)
		bcopy((char *)nat->nat_ptr, (char *)&ipn->ipn_ipnat,
		      sizeof(ipn->ipn_ipnat));

	/*
	 * If we also know the NAT entry has an associated filter rule,
	 * save that too.
	 */
	if (nat->nat_fr != NULL)
		bcopy((char *)nat->nat_fr, (char *)&ipn->ipn_fr,
		      sizeof(ipn->ipn_fr));

	/*
	 * Last but not least, if there is an application proxy session set
	 * up for this NAT entry, then copy that out too, including any
	 * private data saved along side it by the proxy.
	 */
	aps = nat->nat_aps;
	outsize = ipn->ipn_dsize - sizeof(*ipn) + sizeof(ipn->ipn_data);
	if (aps != NULL) {
		char *s;

		if (outsize < sizeof(*aps)) {
			error = ENOBUFS;
			goto finished;
		}

		s = ipn->ipn_data;
		bcopy((char *)aps, s, sizeof(*aps));
		s += sizeof(*aps);
		outsize -= sizeof(*aps);
		if ((aps->aps_data != NULL) && (outsize >= aps->aps_psiz))
			bcopy(aps->aps_data, s, aps->aps_psiz);
		else
			error = ENOBUFS;
	}
	if (error == 0) {
		error = fr_outobjsz(data, ipn, IPFOBJ_NATSAVE, ipns.ipn_dsize);
	}

finished:
	if (ipn != NULL) {
		KFREES(ipn, ipns.ipn_dsize);
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    fr_natputent                                                */
/* Returns:     int - 0 == success, != 0 is the error value.                */
/* Parameters:  data(I) -     pointer to natget structure with NAT          */
/*                            structure information to load into the kernel */
/*              getlock(I) - flag indicating whether or not a write lock    */
/*                           on ipf_nat is already held.                    */
/*                                                                          */
/* Handle SIOCSTPUT.                                                        */
/* Loads a NAT table entry from user space, including a NAT rule, proxy and */
/* firewall rule data structures, if pointers to them indicate so.          */
/* ------------------------------------------------------------------------ */
static int fr_natputent(data, getlock, ifs)
caddr_t data;
int getlock;
ipf_stack_t *ifs;
{
	nat_save_t ipn, *ipnn;
	ap_session_t *aps;
	nat_t *n, *nat;
	frentry_t *fr;
	fr_info_t fin;
	ipnat_t *in;
	int error;

	error = fr_inobj(data, &ipn, IPFOBJ_NATSAVE);
	if (error != 0)
		return error;

	/*
	 * Initialise early because of code at junkput label.
	 */
	in = NULL;
	aps = NULL;
	nat = NULL;
	ipnn = NULL;

	/*
	 * New entry, copy in the rest of the NAT entry if it's size is more
	 * than just the nat_t structure.
	 */
	fr = NULL;
	if (ipn.ipn_dsize > sizeof(ipn)) {
		if (ipn.ipn_dsize > 81920) {
			error = ENOMEM;
			goto junkput;
		}

		KMALLOCS(ipnn, nat_save_t *, ipn.ipn_dsize);
		if (ipnn == NULL)
			return ENOMEM;

		error = fr_inobjsz(data, ipnn, IPFOBJ_NATSAVE, ipn.ipn_dsize);
		if (error != 0) {
			error = EFAULT;
			goto junkput;
		}
	} else
		ipnn = &ipn;

	KMALLOC(nat, nat_t *);
	if (nat == NULL) {
		error = ENOMEM;
		goto junkput;
	}

	bcopy((char *)&ipnn->ipn_nat, (char *)nat, sizeof(*nat));
	/*
	 * Initialize all these so that nat_delete() doesn't cause a crash.
	 */
	bzero((char *)nat, offsetof(struct nat, nat_tqe));
	nat->nat_tqe.tqe_pnext = NULL;
	nat->nat_tqe.tqe_next = NULL;
	nat->nat_tqe.tqe_ifq = NULL;
	nat->nat_tqe.tqe_parent = nat;

	/*
	 * Restore the rule associated with this nat session
	 */
	in = ipnn->ipn_nat.nat_ptr;
	if (in != NULL) {
		KMALLOC(in, ipnat_t *);
		nat->nat_ptr = in;
		if (in == NULL) {
			error = ENOMEM;
			goto junkput;
		}
		bzero((char *)in, offsetof(struct ipnat, in_next6));
		bcopy((char *)&ipnn->ipn_ipnat, (char *)in, sizeof(*in));
		in->in_use = 1;
		in->in_flags |= IPN_DELETE;

		ATOMIC_INC(ifs->ifs_nat_stats.ns_rules);

		nat_resolverule(in, ifs);
	}

	/*
	 * Check that the NAT entry doesn't already exist in the kernel.
	 */
	bzero((char *)&fin, sizeof(fin));
	fin.fin_p = nat->nat_p;
	if (nat->nat_dir == NAT_OUTBOUND) {
		fin.fin_data[0] = ntohs(nat->nat_oport);
		fin.fin_data[1] = ntohs(nat->nat_outport);
		fin.fin_ifp = nat->nat_ifps[0];
		if (getlock) {
			READ_ENTER(&ifs->ifs_ipf_nat);
		}
		n = nat_inlookup(&fin, nat->nat_flags, fin.fin_p,
			nat->nat_oip, nat->nat_outip);
		if (getlock) {
			RWLOCK_EXIT(&ifs->ifs_ipf_nat);
		}
		if (n != NULL) {
			error = EEXIST;
			goto junkput;
		}
	} else if (nat->nat_dir == NAT_INBOUND) {
		fin.fin_data[0] = ntohs(nat->nat_inport);
		fin.fin_data[1] = ntohs(nat->nat_oport);
		fin.fin_ifp = nat->nat_ifps[1];
		if (getlock) {
			READ_ENTER(&ifs->ifs_ipf_nat);
		}
		n = nat_outlookup(&fin, nat->nat_flags, fin.fin_p,
			nat->nat_inip, nat->nat_oip);
		if (getlock) {
			RWLOCK_EXIT(&ifs->ifs_ipf_nat);
		}
		if (n != NULL) {
			error = EEXIST;
			goto junkput;
		}
	} else {
		error = EINVAL;
		goto junkput;
	}

	/*
	 * Restore ap_session_t structure.  Include the private data allocated
	 * if it was there.
	 */
	aps = nat->nat_aps;
	if (aps != NULL) {
		KMALLOC(aps, ap_session_t *);
		nat->nat_aps = aps;
		if (aps == NULL) {
			error = ENOMEM;
			goto junkput;
		}
		bcopy(ipnn->ipn_data, (char *)aps, sizeof(*aps));
		if (in != NULL)
			aps->aps_apr = in->in_apr;
		else
			aps->aps_apr = NULL;
		if (aps->aps_psiz != 0) {
			if (aps->aps_psiz > 81920) {
				error = ENOMEM;
				goto junkput;
			}
			KMALLOCS(aps->aps_data, void *, aps->aps_psiz);
			if (aps->aps_data == NULL) {
				error = ENOMEM;
				goto junkput;
			}
			bcopy(ipnn->ipn_data + sizeof(*aps), aps->aps_data,
			      aps->aps_psiz);
		} else {
			aps->aps_psiz = 0;
			aps->aps_data = NULL;
		}
	}

	/*
	 * If there was a filtering rule associated with this entry then
	 * build up a new one.
	 */
	fr = nat->nat_fr;
	if (fr != NULL) {
		if ((nat->nat_flags & SI_NEWFR) != 0) {
			KMALLOC(fr, frentry_t *);
			nat->nat_fr = fr;
			if (fr == NULL) {
				error = ENOMEM;
				goto junkput;
			}
			ipnn->ipn_nat.nat_fr = fr;
			fr->fr_ref = 1;
			(void) fr_outobj(data, ipnn, IPFOBJ_NATSAVE);
			bcopy((char *)&ipnn->ipn_fr, (char *)fr, sizeof(*fr));
			MUTEX_NUKE(&fr->fr_lock);
			MUTEX_INIT(&fr->fr_lock, "nat-filter rule lock");
		} else {
			READ_ENTER(&ifs->ifs_ipf_nat);
			for (n = ifs->ifs_nat_instances; n; n = n->nat_next)
				if (n->nat_fr == fr)
					break;

			if (n != NULL) {
				MUTEX_ENTER(&fr->fr_lock);
				fr->fr_ref++;
				MUTEX_EXIT(&fr->fr_lock);
			}
			RWLOCK_EXIT(&ifs->ifs_ipf_nat);

			if (!n) {
				error = ESRCH;
				goto junkput;
			}
		}
	}

	if (ipnn != &ipn) {
		KFREES(ipnn, ipn.ipn_dsize);
		ipnn = NULL;
	}

	if (getlock) {
		WRITE_ENTER(&ifs->ifs_ipf_nat);
	}
	error = nat_insert(nat, nat->nat_rev, ifs);
	if ((error == 0) && (aps != NULL)) {
		aps->aps_next = ifs->ifs_ap_sess_list;
		ifs->ifs_ap_sess_list = aps;
	}
	if (getlock) {
		RWLOCK_EXIT(&ifs->ifs_ipf_nat);
	}

	if (error == 0)
		return 0;

	error = ENOMEM;

junkput:
	if (fr != NULL)
		(void) fr_derefrule(&fr, ifs);

	if ((ipnn != NULL) && (ipnn != &ipn)) {
		KFREES(ipnn, ipn.ipn_dsize);
	}
	if (nat != NULL) {
		if (aps != NULL) {
			if (aps->aps_data != NULL) {
				KFREES(aps->aps_data, aps->aps_psiz);
			}
			KFREE(aps);
		}
		if (in != NULL) {
			if (in->in_apr)
				appr_free(in->in_apr);
			KFREE(in);
		}
		KFREE(nat);
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_delete                                                  */
/* Returns:     Nil                                                         */
/* Parameters:  natd(I)    - pointer to NAT structure to delete             */
/*              logtype(I) - type of LOG record to create before deleting   */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* Delete a nat entry from the various lists and table.  If NAT logging is  */
/* enabled then generate a NAT log record for this event.                   */
/* ------------------------------------------------------------------------ */
static void nat_delete(nat, logtype, ifs)
struct nat *nat;
int logtype;
ipf_stack_t *ifs;
{
	struct ipnat *ipn;

	if (logtype != 0 && ifs->ifs_nat_logging != 0)
		nat_log(nat, logtype, ifs);

	MUTEX_ENTER(&ifs->ifs_ipf_nat_new);

	/*
	 * Take it as a general indication that all the pointers are set if
	 * nat_pnext is set.
	 */
	if (nat->nat_pnext != NULL) {
		ifs->ifs_nat_stats.ns_bucketlen[0][nat->nat_hv[0]]--;
		ifs->ifs_nat_stats.ns_bucketlen[1][nat->nat_hv[1]]--;

		*nat->nat_pnext = nat->nat_next;
		if (nat->nat_next != NULL) {
			nat->nat_next->nat_pnext = nat->nat_pnext;
			nat->nat_next = NULL;
		}
		nat->nat_pnext = NULL;

		*nat->nat_phnext[0] = nat->nat_hnext[0];
		if (nat->nat_hnext[0] != NULL) {
			nat->nat_hnext[0]->nat_phnext[0] = nat->nat_phnext[0];
			nat->nat_hnext[0] = NULL;
		}
		nat->nat_phnext[0] = NULL;

		*nat->nat_phnext[1] = nat->nat_hnext[1];
		if (nat->nat_hnext[1] != NULL) {
			nat->nat_hnext[1]->nat_phnext[1] = nat->nat_phnext[1];
			nat->nat_hnext[1] = NULL;
		}
		nat->nat_phnext[1] = NULL;

		if ((nat->nat_flags & SI_WILDP) != 0)
			ifs->ifs_nat_stats.ns_wilds--;
	}

	if (nat->nat_me != NULL) {
		*nat->nat_me = NULL;
		nat->nat_me = NULL;
	}

	fr_deletequeueentry(&nat->nat_tqe);

	nat->nat_ref--;
	if (nat->nat_ref > 0) {
		MUTEX_EXIT(&ifs->ifs_ipf_nat_new);
		return;
	}

#ifdef	IPFILTER_SYNC
	if (nat->nat_sync)
		ipfsync_del(nat->nat_sync);
#endif

	if (nat->nat_fr != NULL)
		(void)fr_derefrule(&nat->nat_fr, ifs);

	if (nat->nat_hm != NULL)
		nat_hostmapdel(nat->nat_hm);

	/*
	 * If there is an active reference from the nat entry to its parent
	 * rule, decrement the rule's reference count and free it too if no
	 * longer being used.
	 */
	ipn = nat->nat_ptr;
	if (ipn != NULL) {
		ipn->in_space++;
		ipn->in_use--;
		if (ipn->in_use == 0 && (ipn->in_flags & IPN_DELETE)) {
			if (ipn->in_apr)
				appr_free(ipn->in_apr);
			KFREE(ipn);
			ifs->ifs_nat_stats.ns_rules--;
		}
	}

	MUTEX_DESTROY(&nat->nat_lock);

	aps_free(nat->nat_aps, ifs);
	ifs->ifs_nat_stats.ns_inuse--;
	MUTEX_EXIT(&ifs->ifs_ipf_nat_new);

	/*
	 * If there's a fragment table entry too for this nat entry, then
	 * dereference that as well.  This is after nat_lock is released
	 * because of Tru64.
	 */
	fr_forgetnat((void *)nat, ifs);

	KFREE(nat);
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_flushtable                                              */
/* Returns:     int - number of NAT rules deleted                           */
/* Parameters:  Nil                                                         */
/*                                                                          */
/* Deletes all currently active NAT sessions.  In deleting each NAT entry a */
/* log record should be emitted in nat_delete() if NAT logging is enabled.  */
/* ------------------------------------------------------------------------ */
/*
 * nat_flushtable - clear the NAT table of all mapping entries.
 */
static int nat_flushtable(ifs)
ipf_stack_t *ifs;
{
	nat_t *nat;
	int j = 0;

	/*
	 * ALL NAT mappings deleted, so lets just make the deletions
	 * quicker.
	 */
	if (ifs->ifs_nat_table[0] != NULL)
		bzero((char *)ifs->ifs_nat_table[0],
		      sizeof(ifs->ifs_nat_table[0]) * ifs->ifs_ipf_nattable_sz);
	if (ifs->ifs_nat_table[1] != NULL)
		bzero((char *)ifs->ifs_nat_table[1],
		      sizeof(ifs->ifs_nat_table[1]) * ifs->ifs_ipf_nattable_sz);

	while ((nat = ifs->ifs_nat_instances) != NULL) {
		nat_delete(nat, NL_FLUSH, ifs);
		j++;
	}

	ifs->ifs_nat_stats.ns_inuse = 0;
	return j;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_clearlist                                               */
/* Returns:     int - number of NAT/RDR rules deleted                       */
/* Parameters:  Nil                                                         */
/*                                                                          */
/* Delete all rules in the current list of rules.  There is nothing elegant */
/* about this cleanup: simply free all entries on the list of rules and     */
/* clear out the tables used for hashed NAT rule lookups.                   */
/* ------------------------------------------------------------------------ */
static int nat_clearlist(ifs)
ipf_stack_t *ifs;
{
	ipnat_t *n, **np = &ifs->ifs_nat_list;
	int i = 0;

	if (ifs->ifs_nat_rules != NULL)
		bzero((char *)ifs->ifs_nat_rules,
		      sizeof(*ifs->ifs_nat_rules) * ifs->ifs_ipf_natrules_sz);
	if (ifs->ifs_rdr_rules != NULL)
		bzero((char *)ifs->ifs_rdr_rules,
		      sizeof(*ifs->ifs_rdr_rules) * ifs->ifs_ipf_rdrrules_sz);

	while ((n = *np) != NULL) {
		*np = n->in_next;
		if (n->in_use == 0) {
			if (n->in_apr != NULL)
				appr_free(n->in_apr);
			KFREE(n);
			ifs->ifs_nat_stats.ns_rules--;
		} else {
			n->in_flags |= IPN_DELETE;
			n->in_next = NULL;
		}
		i++;
	}
	ifs->ifs_nat_masks = 0;
	ifs->ifs_rdr_masks = 0;
	return i;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_newmap                                                  */
/* Returns:     int - -1 == error, 0 == success                             */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to NAT entry                               */
/*              ni(I)  - pointer to structure with misc. information needed */
/*                       to create new NAT entry.                           */
/*                                                                          */
/* Given an empty NAT structure, populate it with new information about a   */
/* new NAT session, as defined by the matching NAT rule.                    */
/* ni.nai_ip is passed in uninitialised and must be set, in host byte order,*/
/* to the new IP address for the translation.                               */
/* ------------------------------------------------------------------------ */
static INLINE int nat_newmap(fin, nat, ni)
fr_info_t *fin;
nat_t *nat;
natinfo_t *ni;
{
	u_short st_port, dport, sport, port, sp, dp;
	struct in_addr in, inb;
	hostmap_t *hm;
	u_32_t flags;
	u_32_t st_ip;
	ipnat_t *np;
	nat_t *natl;
	int l;
	ipf_stack_t *ifs = fin->fin_ifs;

	/*
	 * If it's an outbound packet which doesn't match any existing
	 * record, then create a new port
	 */
	l = 0;
	hm = NULL;
	np = ni->nai_np;
	st_ip = np->in_nip;
	st_port = np->in_pnext;
	flags = ni->nai_flags;
	sport = ni->nai_sport;
	dport = ni->nai_dport;

	/*
	 * Do a loop until we either run out of entries to try or we find
	 * a NAT mapping that isn't currently being used.  This is done
	 * because the change to the source is not (usually) being fixed.
	 */
	do {
		port = 0;
		in.s_addr = htonl(np->in_nip);
		if (l == 0) {
			/*
			 * Check to see if there is an existing NAT
			 * setup for this IP address pair.
			 */
			hm = nat_hostmap(np, fin->fin_src, fin->fin_dst,
					 in, 0, ifs);
			if (hm != NULL)
				in.s_addr = hm->hm_mapip.s_addr;
		} else if ((l == 1) && (hm != NULL)) {
			nat_hostmapdel(hm);
			hm = NULL;
		}
		in.s_addr = ntohl(in.s_addr);

		nat->nat_hm = hm;

		if ((np->in_outmsk == 0xffffffff) && (np->in_pnext == 0)) {
			if (l > 0)
				return -1;
		}

		if (np->in_redir == NAT_BIMAP &&
		    np->in_inmsk == np->in_outmsk) {
			/*
			 * map the address block in a 1:1 fashion
			 */
			in.s_addr = np->in_outip;
			in.s_addr |= fin->fin_saddr & ~np->in_inmsk;
			in.s_addr = ntohl(in.s_addr);

		} else if (np->in_redir & NAT_MAPBLK) {
			if ((l >= np->in_ppip) || ((l > 0) &&
			     !(flags & IPN_TCPUDP)))
				return -1;
			/*
			 * map-block - Calculate destination address.
			 */
			in.s_addr = ntohl(fin->fin_saddr);
			in.s_addr &= ntohl(~np->in_inmsk);
			inb.s_addr = in.s_addr;
			in.s_addr /= np->in_ippip;
			in.s_addr &= ntohl(~np->in_outmsk);
			in.s_addr += ntohl(np->in_outip);
			/*
			 * Calculate destination port.
			 */
			if ((flags & IPN_TCPUDP) &&
			    (np->in_ppip != 0)) {
				port = ntohs(sport) + l;
				port %= np->in_ppip;
				port += np->in_ppip *
					(inb.s_addr % np->in_ippip);
				port += MAPBLK_MINPORT;
				port = htons(port);
			}

		} else if ((np->in_outip == 0) &&
			   (np->in_outmsk == 0xffffffff)) {
			/*
			 * 0/32 - use the interface's IP address.
			 */
			if ((l > 0) ||
			    fr_ifpaddr(4, FRI_NORMAL, fin->fin_ifp,
				       &in, NULL, fin->fin_ifs) == -1)
				return -1;
			in.s_addr = ntohl(in.s_addr);

		} else if ((np->in_outip == 0) && (np->in_outmsk == 0)) {
			/*
			 * 0/0 - use the original source address/port.
			 */
			if (l > 0)
				return -1;
			in.s_addr = ntohl(fin->fin_saddr);

		} else if ((np->in_outmsk != 0xffffffff) &&
			   (np->in_pnext == 0) && ((l > 0) || (hm == NULL)))
			np->in_nip++;

		natl = NULL;

		if ((flags & IPN_TCPUDP) &&
		    ((np->in_redir & NAT_MAPBLK) == 0) &&
		    (np->in_flags & IPN_AUTOPORTMAP)) {
			/*
			 * "ports auto" (without map-block)
			 */
			if ((l > 0) && (l % np->in_ppip == 0)) {
				if (l > np->in_space) {
					return -1;
				} else if ((l > np->in_ppip) &&
					   np->in_outmsk != 0xffffffff)
					np->in_nip++;
			}
			if (np->in_ppip != 0) {
				port = ntohs(sport);
				port += (l % np->in_ppip);
				port %= np->in_ppip;
				port += np->in_ppip *
					(ntohl(fin->fin_saddr) %
					 np->in_ippip);
				port += MAPBLK_MINPORT;
				port = htons(port);
			}

		} else if (((np->in_redir & NAT_MAPBLK) == 0) &&
			   (flags & IPN_TCPUDPICMP) && (np->in_pnext != 0)) {
			/*
			 * Standard port translation.  Select next port.
			 */
			port = htons(np->in_pnext++);

			if (np->in_pnext > ntohs(np->in_pmax)) {
				np->in_pnext = ntohs(np->in_pmin);
				if (np->in_outmsk != 0xffffffff)
					np->in_nip++;
			}
		}

		if (np->in_flags & IPN_IPRANGE) {
			if (np->in_nip > ntohl(np->in_outmsk))
				np->in_nip = ntohl(np->in_outip);
		} else {
			if ((np->in_outmsk != 0xffffffff) &&
			    ((np->in_nip + 1) & ntohl(np->in_outmsk)) >
			    ntohl(np->in_outip))
				np->in_nip = ntohl(np->in_outip) + 1;
		}

		if ((port == 0) && (flags & (IPN_TCPUDPICMP|IPN_ICMPQUERY)))
			port = sport;

		/*
		 * Here we do a lookup of the connection as seen from
		 * the outside.  If an IP# pair already exists, try
		 * again.  So if you have A->B becomes C->B, you can
		 * also have D->E become C->E but not D->B causing
		 * another C->B.  Also take protocol and ports into
		 * account when determining whether a pre-existing
		 * NAT setup will cause an external conflict where
		 * this is appropriate.
		 */
		inb.s_addr = htonl(in.s_addr);
		sp = fin->fin_data[0];
		dp = fin->fin_data[1];
		fin->fin_data[0] = fin->fin_data[1];
		fin->fin_data[1] = htons(port);
		natl = nat_inlookup(fin, flags & ~(SI_WILDP|NAT_SEARCH),
				    (u_int)fin->fin_p, fin->fin_dst, inb);
		fin->fin_data[0] = sp;
		fin->fin_data[1] = dp;

		/*
		 * Has the search wrapped around and come back to the
		 * start ?
		 */
		if ((natl != NULL) &&
		    (np->in_pnext != 0) && (st_port == np->in_pnext) &&
		    (np->in_nip != 0) && (st_ip == np->in_nip))
			return -1;
		l++;
	} while (natl != NULL);

	if (np->in_space > 0)
		np->in_space--;

	/* Setup the NAT table */
	nat->nat_inip = fin->fin_src;
	nat->nat_outip.s_addr = htonl(in.s_addr);
	nat->nat_oip = fin->fin_dst;
	if (nat->nat_hm == NULL)
		nat->nat_hm = nat_hostmap(np, fin->fin_src, fin->fin_dst,
					  nat->nat_outip, 0, ifs);

	/*
	 * The ICMP checksum does not have a pseudo header containing
	 * the IP addresses
	 */
	ni->nai_sum1 = LONG_SUM(ntohl(fin->fin_saddr));
	ni->nai_sum2 = LONG_SUM(in.s_addr);
	if ((flags & IPN_TCPUDP)) {
		ni->nai_sum1 += ntohs(sport);
		ni->nai_sum2 += ntohs(port);
	}

	if (flags & IPN_TCPUDP) {
		nat->nat_inport = sport;
		nat->nat_outport = port;	/* sport */
		nat->nat_oport = dport;
		((tcphdr_t *)fin->fin_dp)->th_sport = port;
	} else if (flags & IPN_ICMPQUERY) {
		((icmphdr_t *)fin->fin_dp)->icmp_id = port;
		nat->nat_inport = port;
		nat->nat_outport = port;
	}
	
	ni->nai_ip.s_addr = in.s_addr;
	ni->nai_port = port;
	ni->nai_nport = dport;
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_newrdr                                                  */
/* Returns:     int - -1 == error, 0 == success (no move), 1 == success and */
/*                    allow rule to be moved if IPN_ROUNDR is set.          */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to NAT entry                               */
/*              ni(I)  - pointer to structure with misc. information needed */
/*                       to create new NAT entry.                           */
/*                                                                          */
/* ni.nai_ip is passed in uninitialised and must be set, in host byte order,*/
/* to the new IP address for the translation.                               */
/* ------------------------------------------------------------------------ */
static INLINE int nat_newrdr(fin, nat, ni)
fr_info_t *fin;
nat_t *nat;
natinfo_t *ni;
{
	u_short nport, dport, sport;
	struct in_addr in;
	hostmap_t *hm;
	u_32_t flags;
	ipnat_t *np;
	int move;
	ipf_stack_t *ifs = fin->fin_ifs;

	move = 1;
	hm = NULL;
	in.s_addr = 0;
	np = ni->nai_np;
	flags = ni->nai_flags;
	sport = ni->nai_sport;
	dport = ni->nai_dport;

	/*
	 * If the matching rule has IPN_STICKY set, then we want to have the
	 * same rule kick in as before.  Why would this happen?  If you have
	 * a collection of rdr rules with "round-robin sticky", the current
	 * packet might match a different one to the previous connection but
	 * we want the same destination to be used.
	 */
	if ((np->in_flags & (IPN_ROUNDR|IPN_STICKY)) ==
	    (IPN_ROUNDR|IPN_STICKY)) {
		hm = nat_hostmap(NULL, fin->fin_src, fin->fin_dst, in,
				 (u_32_t)dport, ifs);
		if (hm != NULL) {
			in.s_addr = ntohl(hm->hm_mapip.s_addr);
			np = hm->hm_ipnat;
			ni->nai_np = np;
			move = 0;
		}
	}

	/*
	 * Otherwise, it's an inbound packet. Most likely, we don't
	 * want to rewrite source ports and source addresses. Instead,
	 * we want to rewrite to a fixed internal address and fixed
	 * internal port.
	 */
	if (np->in_flags & IPN_SPLIT) {
		in.s_addr = np->in_nip;

		if ((np->in_flags & (IPN_ROUNDR|IPN_STICKY)) == IPN_STICKY) {
			hm = nat_hostmap(np, fin->fin_src, fin->fin_dst,
					 in, (u_32_t)dport, ifs);
			if (hm != NULL) {
				in.s_addr = hm->hm_mapip.s_addr;
				move = 0;
			}
		}

		if (hm == NULL || hm->hm_ref == 1) {
			if (np->in_inip == htonl(in.s_addr)) {
				np->in_nip = ntohl(np->in_inmsk);
				move = 0;
			} else {
				np->in_nip = ntohl(np->in_inip);
			}
		}

	} else if ((np->in_inip == 0) && (np->in_inmsk == 0xffffffff)) {
		/*
		 * 0/32 - use the interface's IP address.
		 */
		if (fr_ifpaddr(4, FRI_NORMAL, fin->fin_ifp, &in, NULL,
			   fin->fin_ifs) == -1)
			return -1;
		in.s_addr = ntohl(in.s_addr);

	} else if ((np->in_inip == 0) && (np->in_inmsk== 0)) {
		/*
		 * 0/0 - use the original destination address/port.
		 */
		in.s_addr = ntohl(fin->fin_daddr);

	} else if (np->in_redir == NAT_BIMAP &&
		   np->in_inmsk == np->in_outmsk) {
		/*
		 * map the address block in a 1:1 fashion
		 */
		in.s_addr = np->in_inip;
		in.s_addr |= fin->fin_daddr & ~np->in_inmsk;
		in.s_addr = ntohl(in.s_addr);
	} else {
		in.s_addr = ntohl(np->in_inip);
	}

	if ((np->in_pnext == 0) || ((flags & NAT_NOTRULEPORT) != 0))
		nport = dport;
	else {
		/*
		 * Whilst not optimized for the case where
		 * pmin == pmax, the gain is not significant.
		 */
		if (((np->in_flags & IPN_FIXEDDPORT) == 0) &&
		    (np->in_pmin != np->in_pmax)) {
			nport = ntohs(dport) - ntohs(np->in_pmin) +
				ntohs(np->in_pnext);
			nport = htons(nport);
		} else
			nport = np->in_pnext;
	}

	/*
	 * When the redirect-to address is set to 0.0.0.0, just
	 * assume a blank `forwarding' of the packet.  We don't
	 * setup any translation for this either.
	 */
	if (in.s_addr == 0) {
		if (nport == dport)
			return -1;
		in.s_addr = ntohl(fin->fin_daddr);
	}

	nat->nat_inip.s_addr = htonl(in.s_addr);
	nat->nat_outip = fin->fin_dst;
	nat->nat_oip = fin->fin_src;

	ni->nai_sum1 = LONG_SUM(ntohl(fin->fin_daddr)) + ntohs(dport);
	ni->nai_sum2 = LONG_SUM(in.s_addr) + ntohs(nport);

	ni->nai_ip.s_addr = in.s_addr;
	ni->nai_nport = nport;
	ni->nai_port = sport;

	if (flags & IPN_TCPUDP) {
		nat->nat_inport = nport;
		nat->nat_outport = dport;
		nat->nat_oport = sport;
		((tcphdr_t *)fin->fin_dp)->th_dport = nport;
	} else if (flags & IPN_ICMPQUERY) {
		((icmphdr_t *)fin->fin_dp)->icmp_id = nport;
		nat->nat_inport = nport;
		nat->nat_outport = nport;
	}

	return move;
}

/* ------------------------------------------------------------------------ */
/* Function:    nat_new                                                     */
/* Returns:     nat_t* - NULL == failure to create new NAT structure,       */
/*                       else pointer to new NAT structure                  */
/* Parameters:  fin(I)       - pointer to packet information                */
/*              np(I)        - pointer to NAT rule                          */
/*              natsave(I)   - pointer to where to store NAT struct pointer */
/*              flags(I)     - flags describing the current packet          */
/*              direction(I) - direction of packet (in/out)                 */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* Attempts to create a new NAT entry.  Does not actually change the packet */
/* in any way.                                                              */
/*                                                                          */
/* This fucntion is in three main parts: (1) deal with creating a new NAT   */
/* structure for a "MAP" rule (outgoing NAT translation); (2) deal with     */
/* creating a new NAT structure for a "RDR" rule (incoming NAT translation) */
/* and (3) building that structure and putting it into the NAT table(s).    */
/* ------------------------------------------------------------------------ */
nat_t *nat_new(fin, np, natsave, flags, direction)
fr_info_t *fin;
ipnat_t *np;
nat_t **natsave;
u_int flags;
int direction;
{
	u_short port = 0, sport = 0, dport = 0, nport = 0;
	tcphdr_t *tcp = NULL;
	hostmap_t *hm = NULL;
	struct in_addr in;
	nat_t *nat, *natl;
	u_int nflags;
	natinfo_t ni;
	u_32_t sumd;
	int move;
	ipf_stack_t *ifs = fin->fin_ifs;

	if (ifs->ifs_nat_stats.ns_inuse >= ifs->ifs_ipf_nattable_max) {
		ifs->ifs_nat_stats.ns_memfail++;
		return NULL;
	}

	move = 1;
	nflags = np->in_flags & flags;
	nflags &= NAT_FROMRULE;

	ni.nai_np = np;
	ni.nai_nflags = nflags;
	ni.nai_flags = flags;

	/* Give me a new nat */
	KMALLOC(nat, nat_t *);
	if (nat == NULL) {
		ifs->ifs_nat_stats.ns_memfail++;
		/*
		 * Try to automatically tune the max # of entries in the
		 * table allowed to be less than what will cause kmem_alloc()
		 * to fail and try to eliminate panics due to out of memory
		 * conditions arising.
		 */
		if (ifs->ifs_ipf_nattable_max > ifs->ifs_ipf_nattable_sz) {
			ifs->ifs_ipf_nattable_max = ifs->ifs_nat_stats.ns_inuse - 100;
			printf("ipf_nattable_max reduced to %d\n",
				ifs->ifs_ipf_nattable_max);
		}
		return NULL;
	}

	if (flags & IPN_TCPUDP) {
		tcp = fin->fin_dp;
		ni.nai_sport = htons(fin->fin_sport);
		ni.nai_dport = htons(fin->fin_dport);
	} else if (flags & IPN_ICMPQUERY) {
		/*
		 * In the ICMP query NAT code, we translate the ICMP id fields
		 * to make them unique. This is indepedent of the ICMP type
		 * (e.g. in the unlikely event that a host sends an echo and
		 * an tstamp request with the same id, both packets will have
		 * their ip address/id field changed in the same way).
		 */
		/* The icmp_id field is used by the sender to identify the
		 * process making the icmp request. (the receiver justs
		 * copies it back in its response). So, it closely matches
		 * the concept of source port. We overlay sport, so we can
		 * maximally reuse the existing code.
		 */
		ni.nai_sport = ((icmphdr_t *)fin->fin_dp)->icmp_id;
		ni.nai_dport = ni.nai_sport;
	}

	bzero((char *)nat, sizeof(*nat));
	nat->nat_flags = flags;
	nat->nat_redir = np->in_redir;

	if ((flags & NAT_SLAVE) == 0) {
		MUTEX_ENTER(&ifs->ifs_ipf_nat_new);
	}

	/*
	 * Search the current table for a match.
	 */
	if (direction == NAT_OUTBOUND) {
		/*
		 * We can now arrange to call this for the same connection
		 * because ipf_nat_new doesn't protect the code path into
		 * this function.
		 */
		natl = nat_outlookup(fin, nflags, (u_int)fin->fin_p,
				     fin->fin_src, fin->fin_dst);
		if (natl != NULL) {
			nat = natl;
			goto done;
		}

		move = nat_newmap(fin, nat, &ni);
		if (move == -1)
			goto badnat;

		np = ni.nai_np;
		in = ni.nai_ip;
	} else {
		/*
		 * NAT_INBOUND is used only for redirects rules
		 */
		natl = nat_inlookup(fin, nflags, (u_int)fin->fin_p,
				    fin->fin_src, fin->fin_dst);
		if (natl != NULL) {
			nat = natl;
			goto done;
		}

		move = nat_newrdr(fin, nat, &ni);
		if (move == -1)
			goto badnat;

		np = ni.nai_np;
		in = ni.nai_ip;
	}
	port = ni.nai_port;
	nport = ni.nai_nport;

	if ((move == 1) && (np->in_flags & IPN_ROUNDR)) {
		if (np->in_redir == NAT_REDIRECT) {
			nat_delrdr(np);
			nat_addrdr(np, ifs);
		} else if (np->in_redir == NAT_MAP) {
			nat_delnat(np);
			nat_addnat(np, ifs);
		}
	}

	if (flags & IPN_TCPUDP) {
		sport = ni.nai_sport;
		dport = ni.nai_dport;
	} else if (flags & IPN_ICMPQUERY) {
		sport = ni.nai_sport;
		dport = 0;
	}

	/*
	 * nat_sumd[0] stores adjustment value including both IP address and
	 * port number changes. nat_sumd[1] stores adjustment value only for
	 * IP address changes, to be used for pseudo header adjustment, in
	 * case hardware partial checksum offload is offered.
	 */
	CALC_SUMD(ni.nai_sum1, ni.nai_sum2, sumd);
	nat->nat_sumd[0] = (sumd & 0xffff) + (sumd >> 16);
#if SOLARIS && defined(_KERNEL) && (SOLARIS2 >= 6)
	if (flags & IPN_TCPUDP) {
		ni.nai_sum1 = LONG_SUM(in.s_addr);
		if (direction == NAT_OUTBOUND)
			ni.nai_sum2 = LONG_SUM(ntohl(fin->fin_saddr));
		else
			ni.nai_sum2 = LONG_SUM(ntohl(fin->fin_daddr));

		CALC_SUMD(ni.nai_sum1, ni.nai_sum2, sumd);
		nat->nat_sumd[1] = (sumd & 0xffff) + (sumd >> 16);
	} else
#endif
		nat->nat_sumd[1] = nat->nat_sumd[0];

	if ((flags & IPN_TCPUDPICMP) && ((sport != port) || (dport != nport))) {
		if (direction == NAT_OUTBOUND)
			ni.nai_sum1 = LONG_SUM(ntohl(fin->fin_saddr));
		else
			ni.nai_sum1 = LONG_SUM(ntohl(fin->fin_daddr));

		ni.nai_sum2 = LONG_SUM(in.s_addr);

		CALC_SUMD(ni.nai_sum1, ni.nai_sum2, sumd);
		nat->nat_ipsumd = (sumd & 0xffff) + (sumd >> 16);
	} else {
		nat->nat_ipsumd = nat->nat_sumd[0];
		if (!(flags & IPN_TCPUDPICMP)) {
			nat->nat_sumd[0] = 0;
			nat->nat_sumd[1] = 0;
		}
	}

	if (nat_finalise(fin, nat, &ni, tcp, natsave, direction) == -1) {
		goto badnat;
	}
	if (flags & SI_WILDP)
		ifs->ifs_nat_stats.ns_wilds++;
	goto done;
badnat:
	ifs->ifs_nat_stats.ns_badnat++;
	if ((hm = nat->nat_hm) != NULL)
		nat_hostmapdel(hm);
	KFREE(nat);
	nat = NULL;
done:
	if ((flags & NAT_SLAVE) == 0) {
		MUTEX_EXIT(&ifs->ifs_ipf_nat_new);
	}
	return nat;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_finalise                                                */
/* Returns:     int - 0 == sucess, -1 == failure                            */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to NAT entry                               */
/*              ni(I)  - pointer to structure with misc. information needed */
/*                       to create new NAT entry.                           */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* This is the tail end of constructing a new NAT entry and is the same     */
/* for both IPv4 and IPv6.                                                  */
/* ------------------------------------------------------------------------ */
/*ARGSUSED*/
static INLINE int nat_finalise(fin, nat, ni, tcp, natsave, direction)
fr_info_t *fin;
nat_t *nat;
natinfo_t *ni;
tcphdr_t *tcp;
nat_t **natsave;
int direction;
{
	frentry_t *fr;
	ipnat_t *np;
	ipf_stack_t *ifs = fin->fin_ifs;

	np = ni->nai_np;

	COPYIFNAME(fin->fin_ifp, nat->nat_ifnames[0], fin->fin_v);

#ifdef	IPFILTER_SYNC
	if ((nat->nat_flags & SI_CLONE) == 0)
		nat->nat_sync = ipfsync_new(SMC_NAT, fin, nat);
#endif

	nat->nat_me = natsave;
	nat->nat_dir = direction;
	nat->nat_ifps[0] = np->in_ifps[0];
	nat->nat_ifps[1] = np->in_ifps[1];
	nat->nat_ptr = np;
	nat->nat_p = fin->fin_p;
	nat->nat_mssclamp = np->in_mssclamp;
	fr = fin->fin_fr;
	nat->nat_fr = fr;

	if ((np->in_apr != NULL) && ((ni->nai_flags & NAT_SLAVE) == 0))
		if (appr_new(fin, nat) == -1)
			return -1;

	if (nat_insert(nat, fin->fin_rev, ifs) == 0) {
		if (ifs->ifs_nat_logging)
			nat_log(nat, (u_int)np->in_redir, ifs);
		np->in_use++;
		if (fr != NULL) {
			MUTEX_ENTER(&fr->fr_lock);
			fr->fr_ref++;
			MUTEX_EXIT(&fr->fr_lock);
		}
		return 0;
	}

	/*
	 * nat_insert failed, so cleanup time...
	 */
	return -1;
}


/* ------------------------------------------------------------------------ */
/* Function:   nat_insert                                                   */
/* Returns:    int - 0 == sucess, -1 == failure                             */
/* Parameters: nat(I) - pointer to NAT structure                            */
/*             rev(I) - flag indicating forward/reverse direction of packet */
/* Write Lock: ipf_nat                                                      */
/*                                                                          */
/* Insert a NAT entry into the hash tables for searching and add it to the  */
/* list of active NAT entries.  Adjust global counters when complete.       */
/* ------------------------------------------------------------------------ */
int	nat_insert(nat, rev, ifs)
nat_t	*nat;
int	rev;
ipf_stack_t *ifs;
{
	u_int hv1, hv2;
	nat_t **natp;

	/*
	 * Try and return an error as early as possible, so calculate the hash
	 * entry numbers first and then proceed.
	 */
	if ((nat->nat_flags & (SI_W_SPORT|SI_W_DPORT)) == 0) {
		hv1 = NAT_HASH_FN(nat->nat_inip.s_addr, nat->nat_inport,
				  0xffffffff);
		hv1 = NAT_HASH_FN(nat->nat_oip.s_addr, hv1 + nat->nat_oport,
				  ifs->ifs_ipf_nattable_sz);
		hv2 = NAT_HASH_FN(nat->nat_outip.s_addr, nat->nat_outport,
				  0xffffffff);
		hv2 = NAT_HASH_FN(nat->nat_oip.s_addr, hv2 + nat->nat_oport,
				  ifs->ifs_ipf_nattable_sz);
	} else {
		hv1 = NAT_HASH_FN(nat->nat_inip.s_addr, 0, 0xffffffff);
		hv1 = NAT_HASH_FN(nat->nat_oip.s_addr, hv1,
				  ifs->ifs_ipf_nattable_sz);
		hv2 = NAT_HASH_FN(nat->nat_outip.s_addr, 0, 0xffffffff);
		hv2 = NAT_HASH_FN(nat->nat_oip.s_addr, hv2,
				  ifs->ifs_ipf_nattable_sz);
	}

	if (ifs->ifs_nat_stats.ns_bucketlen[0][hv1] >= ifs->ifs_fr_nat_maxbucket ||
	    ifs->ifs_nat_stats.ns_bucketlen[1][hv2] >= ifs->ifs_fr_nat_maxbucket) {
		return -1;
	}

	nat->nat_hv[0] = hv1;
	nat->nat_hv[1] = hv2;

	MUTEX_INIT(&nat->nat_lock, "nat entry lock");

	nat->nat_rev = rev;
	nat->nat_ref = 1;
	nat->nat_bytes[0] = 0;
	nat->nat_pkts[0] = 0;
	nat->nat_bytes[1] = 0;
	nat->nat_pkts[1] = 0;

	nat->nat_ifnames[0][LIFNAMSIZ - 1] = '\0';
	nat->nat_ifps[0] = fr_resolvenic(nat->nat_ifnames[0], 4, ifs);

	if (nat->nat_ifnames[1][0] !='\0') {
		nat->nat_ifnames[1][LIFNAMSIZ - 1] = '\0';
		nat->nat_ifps[1] = fr_resolvenic(nat->nat_ifnames[1], 4, ifs);
	} else {
		(void) strncpy(nat->nat_ifnames[1], nat->nat_ifnames[0],
			       LIFNAMSIZ);
		nat->nat_ifnames[1][LIFNAMSIZ - 1] = '\0';
		nat->nat_ifps[1] = nat->nat_ifps[0];
	}

	nat->nat_next = ifs->ifs_nat_instances;
	nat->nat_pnext = &ifs->ifs_nat_instances;
	if (ifs->ifs_nat_instances)
		ifs->ifs_nat_instances->nat_pnext = &nat->nat_next;
	ifs->ifs_nat_instances = nat;

	natp = &ifs->ifs_nat_table[0][hv1];
	if (*natp)
		(*natp)->nat_phnext[0] = &nat->nat_hnext[0];
	nat->nat_phnext[0] = natp;
	nat->nat_hnext[0] = *natp;
	*natp = nat;
	ifs->ifs_nat_stats.ns_bucketlen[0][hv1]++;

	natp = &ifs->ifs_nat_table[1][hv2];
	if (*natp)
		(*natp)->nat_phnext[1] = &nat->nat_hnext[1];
	nat->nat_phnext[1] = natp;
	nat->nat_hnext[1] = *natp;
	*natp = nat;
	ifs->ifs_nat_stats.ns_bucketlen[1][hv2]++;

	fr_setnatqueue(nat, rev, ifs);

	ifs->ifs_nat_stats.ns_added++;
	ifs->ifs_nat_stats.ns_inuse++;
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_icmperrorlookup                                         */
/* Returns:     nat_t* - point to matching NAT structure                    */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              dir(I) - direction of packet (in/out)                       */
/*                                                                          */
/* Check if the ICMP error message is related to an existing TCP, UDP or    */
/* ICMP query nat entry.  It is assumed that the packet is already of the   */
/* the required length.                                                     */
/* ------------------------------------------------------------------------ */
nat_t *nat_icmperrorlookup(fin, dir)
fr_info_t *fin;
int dir;
{
	int flags = 0, minlen;
	icmphdr_t *orgicmp;
	tcphdr_t *tcp = NULL;
	u_short data[2];
	nat_t *nat;
	ip_t *oip;
	u_int p;

	/*
	 * Does it at least have the return (basic) IP header ?
	 * Only a basic IP header (no options) should be with an ICMP error
	 * header.  Also, if it's not an error type, then return.
	 */
	if ((fin->fin_hlen != sizeof(ip_t)) || !(fin->fin_flx & FI_ICMPERR))
		return NULL;

	/*
	 * Check packet size
	 */
	oip = (ip_t *)((char *)fin->fin_dp + 8);
	minlen = IP_HL(oip) << 2;
	if ((minlen < sizeof(ip_t)) ||
	    (fin->fin_plen < ICMPERR_IPICMPHLEN + minlen))
		return NULL;
	/*
	 * Is the buffer big enough for all of it ?  It's the size of the IP
	 * header claimed in the encapsulated part which is of concern.  It
	 * may be too big to be in this buffer but not so big that it's
	 * outside the ICMP packet, leading to TCP deref's causing problems.
	 * This is possible because we don't know how big oip_hl is when we
	 * do the pullup early in fr_check() and thus can't gaurantee it is
	 * all here now.
	 */
#ifdef  _KERNEL
	{
	mb_t *m;

	m = fin->fin_m;
# if defined(MENTAT)
	if ((char *)oip + fin->fin_dlen - ICMPERR_ICMPHLEN > (char *)m->b_wptr)
		return NULL;
# else
	if ((char *)oip + fin->fin_dlen - ICMPERR_ICMPHLEN >
	    (char *)fin->fin_ip + M_LEN(m))
		return NULL;
# endif
	}
#endif

	if (fin->fin_daddr != oip->ip_src.s_addr)
		return NULL;

	p = oip->ip_p;
	if (p == IPPROTO_TCP)
		flags = IPN_TCP;
	else if (p == IPPROTO_UDP)
		flags = IPN_UDP;
	else if (p == IPPROTO_ICMP) {
		orgicmp = (icmphdr_t *)((char *)oip + (IP_HL(oip) << 2));

		/* see if this is related to an ICMP query */
		if (nat_icmpquerytype4(orgicmp->icmp_type)) {
			data[0] = fin->fin_data[0];
			data[1] = fin->fin_data[1];
			fin->fin_data[0] = 0;
			fin->fin_data[1] = orgicmp->icmp_id;

			flags = IPN_ICMPERR|IPN_ICMPQUERY;
			/*
			 * NOTE : dir refers to the direction of the original
			 *        ip packet. By definition the icmp error
			 *        message flows in the opposite direction.
			 */
			if (dir == NAT_INBOUND)
				nat = nat_inlookup(fin, flags, p, oip->ip_dst,
						   oip->ip_src);
			else
				nat = nat_outlookup(fin, flags, p, oip->ip_dst,
						    oip->ip_src);
			fin->fin_data[0] = data[0];
			fin->fin_data[1] = data[1];
			return nat;
		}
	}
		
	if (flags & IPN_TCPUDP) {
		minlen += 8;		/* + 64bits of data to get ports */
		if (fin->fin_plen < ICMPERR_IPICMPHLEN + minlen)
			return NULL;

		data[0] = fin->fin_data[0];
		data[1] = fin->fin_data[1];
		tcp = (tcphdr_t *)((char *)oip + (IP_HL(oip) << 2));
		fin->fin_data[0] = ntohs(tcp->th_dport);
		fin->fin_data[1] = ntohs(tcp->th_sport);

		if (dir == NAT_INBOUND) {
			nat = nat_inlookup(fin, flags, p, oip->ip_dst,
					   oip->ip_src);
		} else {
			nat = nat_outlookup(fin, flags, p, oip->ip_dst,
					    oip->ip_src);
		}
		fin->fin_data[0] = data[0];
		fin->fin_data[1] = data[1];
		return nat;
	}
	if (dir == NAT_INBOUND)
		return nat_inlookup(fin, 0, p, oip->ip_dst, oip->ip_src);
	else
		return nat_outlookup(fin, 0, p, oip->ip_dst, oip->ip_src);
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_icmperror                                               */
/* Returns:     nat_t* - point to matching NAT structure                    */
/* Parameters:  fin(I)    - pointer to packet information                   */
/*              nflags(I) - NAT flags for this packet                       */
/*              dir(I)    - direction of packet (in/out)                    */
/*                                                                          */
/* Fix up an ICMP packet which is an error message for an existing NAT      */
/* session.  This will correct both packet header data and checksums.       */
/*                                                                          */
/* This should *ONLY* be used for incoming ICMP error packets to make sure  */
/* a NAT'd ICMP packet gets correctly recognised.                           */
/* ------------------------------------------------------------------------ */
nat_t *nat_icmperror(fin, nflags, dir)
fr_info_t *fin;
u_int *nflags;
int dir;
{
	u_32_t sum1, sum2, sumd, psum1, psum2, psumd, sumd2;
	struct in_addr in;
	icmphdr_t *icmp, *orgicmp;
	int dlen;
	udphdr_t *udp;
	tcphdr_t *tcp;
	nat_t *nat;
	ip_t *oip;
	if ((fin->fin_flx & (FI_SHORT|FI_FRAGBODY)))
		return NULL;

	/*
	 * nat_icmperrorlookup() looks up nat entry associated with the
	 * offending IP packet and returns pointer to the entry, or NULL
	 * if packet wasn't natted or for `defective' packets.
	 */

	if ((fin->fin_v != 4) || !(nat = nat_icmperrorlookup(fin, dir)))
		return NULL;

	sumd2 = 0;
	*nflags = IPN_ICMPERR;
	icmp = fin->fin_dp;
	oip = (ip_t *)&icmp->icmp_ip;
	udp = (udphdr_t *)((((char *)oip) + (IP_HL(oip) << 2)));
	tcp = (tcphdr_t *)udp;
	dlen = fin->fin_plen - ((char *)udp - (char *)fin->fin_ip);

	/*
	 * Need to adjust ICMP header to include the real IP#'s and
	 * port #'s.  There are three steps required.
	 *
	 * Step 1
	 * Fix the IP addresses in the offending IP packet and update
	 * ip header checksum to compensate for the change.
	 *
	 * No update needed here for icmp_cksum because the ICMP checksum
	 * is calculated over the complete ICMP packet, which includes the
	 * changed oip IP addresses and oip->ip_sum.  These two changes
	 * cancel each other out (if the delta for the IP address is x,
	 * then the delta for ip_sum is minus x).
	 */

	if (oip->ip_dst.s_addr == nat->nat_oip.s_addr) {
		sum1 = LONG_SUM(ntohl(oip->ip_src.s_addr));
		in = nat->nat_inip;
		oip->ip_src = in;
	} else {
		sum1 = LONG_SUM(ntohl(oip->ip_dst.s_addr));
		in = nat->nat_outip;
		oip->ip_dst = in;
	}

	sum2 = LONG_SUM(ntohl(in.s_addr));
	CALC_SUMD(sum1, sum2, sumd);
	fix_datacksum(&oip->ip_sum, sumd);

	/*
	 * Step 2
	 * Perform other adjustments based on protocol of offending packet.
	 */

	switch (oip->ip_p) {
		case IPPROTO_TCP :
		case IPPROTO_UDP :

			/*
			* For offending TCP/UDP IP packets, translate the ports
			* based on the NAT specification.
			*
			* Advance notice : Now it becomes complicated :-)
			*
			* Since the port and IP addresse fields are both part
			* of the TCP/UDP checksum of the offending IP packet,
			* we need to adjust that checksum as well.
			*
			* To further complicate things, the TCP/UDP checksum
			* may not be present.  We must check to see if the
			* length of the data portion is big enough to hold
			* the checksum.  In the UDP case, a test to determine
			* if the checksum is even set is also required.
			*
			* Any changes to an IP address, port or checksum within
			* the ICMP packet requires a change to icmp_cksum.
			*
			* Be extremely careful here ... The change is dependent
			* upon whether or not the TCP/UPD checksum is present.
			*
			* If TCP/UPD checksum is present, the icmp_cksum must
			* compensate for checksum modification resulting from
			* IP address change only.  Port change and resulting
			* data checksum adjustments cancel each other out.
			*
			* If TCP/UDP checksum is not present, icmp_cksum must
			* compensate for port change only.  The IP address
			* change does not modify anything else in this case.
			*/

			psum1 = 0;
			psum2 = 0;
			psumd = 0;

			if ((tcp->th_dport == nat->nat_oport) &&
			    (tcp->th_sport != nat->nat_inport)) {

				/*
				 * Translate the source port.
				 */

				psum1 = ntohs(tcp->th_sport);
				psum2 = ntohs(nat->nat_inport);
				tcp->th_sport = nat->nat_inport;

			} else if ((tcp->th_sport == nat->nat_oport) &&
				    (tcp->th_dport != nat->nat_outport)) {

				/*
				 * Translate the destination port.
				 */

				psum1 = ntohs(tcp->th_dport);
				psum2 = ntohs(nat->nat_outport);
				tcp->th_dport = nat->nat_outport;
			}

			if ((oip->ip_p == IPPROTO_TCP) && (dlen >= 18)) {

				/*
				 * TCP checksum present.
				 *
				 * Adjust data checksum and icmp checksum to
				 * compensate for any IP address change.
				 */

				sum1 = ntohs(tcp->th_sum);
				fix_datacksum(&tcp->th_sum, sumd);
				sum2 = ntohs(tcp->th_sum);
				sumd2 = sumd << 1;
				CALC_SUMD(sum1, sum2, sumd);
				sumd2 += sumd;

				/*
				 * Also make data checksum adjustment to
				 * compensate for any port change.
				 */

				if (psum1 != psum2) {
					CALC_SUMD(psum1, psum2, psumd);
					fix_datacksum(&tcp->th_sum, psumd);
				}

			} else if ((oip->ip_p == IPPROTO_UDP) &&
				   (dlen >= 8) && (udp->uh_sum != 0)) {

				/*
				 * The UDP checksum is present and set.
				 *
				 * Adjust data checksum and icmp checksum to
				 * compensate for any IP address change.
				 */

				sum1 = ntohs(udp->uh_sum);
				fix_datacksum(&udp->uh_sum, sumd);
				sum2 = ntohs(udp->uh_sum);
				sumd2 = sumd << 1;
				CALC_SUMD(sum1, sum2, sumd);
				sumd2 += sumd;

				/*
				 * Also make data checksum adjustment to
				 * compensate for any port change.
				 */

				if (psum1 != psum2) {
					CALC_SUMD(psum1, psum2, psumd);
					fix_datacksum(&udp->uh_sum, psumd);
				}

			} else {

				/*
				 * Data checksum was not present.
				 *
				 * Compensate for any port change.
				 */

				CALC_SUMD(psum2, psum1, psumd);
				sumd2 += psumd;
			}
			break;

		case IPPROTO_ICMP :

			orgicmp = (icmphdr_t *)udp;

			if ((nat->nat_dir == NAT_OUTBOUND) &&
			    (orgicmp->icmp_id != nat->nat_inport) &&
			    (dlen >= 8)) {

				/*
				 * Fix ICMP checksum (of the offening ICMP
				 * query packet) to compensate the change
				 * in the ICMP id of the offending ICMP
				 * packet.
				 *
				 * Since you modify orgicmp->icmp_id with
				 * a delta (say x) and you compensate that
				 * in origicmp->icmp_cksum with a delta
				 * minus x, you don't have to adjust the
				 * overall icmp->icmp_cksum
				 */

				sum1 = ntohs(orgicmp->icmp_id);
				sum2 = ntohs(nat->nat_inport);
				CALC_SUMD(sum1, sum2, sumd);
				orgicmp->icmp_id = nat->nat_inport;
				fix_datacksum(&orgicmp->icmp_cksum, sumd);

			} /* nat_dir can't be NAT_INBOUND for icmp queries */

			break;

		default :

			break;

	} /* switch (oip->ip_p) */

	/*
	 * Step 3
	 * Make the adjustments to icmp checksum.
	 */

	if (sumd2 != 0) {
		sumd2 = (sumd2 & 0xffff) + (sumd2 >> 16);
		sumd2 = (sumd2 & 0xffff) + (sumd2 >> 16);
		fix_incksum(&icmp->icmp_cksum, sumd2);
	}
	return nat;
}


/*
 * NB: these lookups don't lock access to the list, it assumed that it has
 * already been done!
 */

/* ------------------------------------------------------------------------ */
/* Function:    nat_inlookup                                                */
/* Returns:     nat_t* - NULL == no match,                                  */
/*                       else pointer to matching NAT entry                 */
/* Parameters:  fin(I)    - pointer to packet information                   */
/*              flags(I)  - NAT flags for this packet                       */
/*              p(I)      - protocol for this packet                        */
/*              src(I)    - source IP address                               */
/*              mapdst(I) - destination IP address                          */
/*                                                                          */
/* Lookup a nat entry based on the mapped destination ip address/port and   */
/* real source address/port.  We use this lookup when receiving a packet,   */
/* we're looking for a table entry, based on the destination address.       */
/*                                                                          */
/* NOTE: THE PACKET BEING CHECKED (IF FOUND) HAS A MAPPING ALREADY.         */
/*                                                                          */
/* NOTE: IT IS ASSUMED THAT ipf_nat IS ONLY HELD WITH A READ LOCK WHEN      */
/*       THIS FUNCTION IS CALLED WITH NAT_SEARCH SET IN nflags.             */
/*                                                                          */
/* flags   -> relevant are IPN_UDP/IPN_TCP/IPN_ICMPQUERY that indicate if   */
/*            the packet is of said protocol                                */
/* ------------------------------------------------------------------------ */
nat_t *nat_inlookup(fin, flags, p, src, mapdst)
fr_info_t *fin;
u_int flags, p;
struct in_addr src , mapdst;
{
	u_short sport, dport;
	ipnat_t *ipn;
	u_int sflags;
	nat_t *nat;
	int nflags;
	u_32_t dst;
	void *ifp;
	u_int hv;
	ipf_stack_t *ifs = fin->fin_ifs;

	if (fin != NULL)
		ifp = fin->fin_ifp;
	else
		ifp = NULL;
	sport = 0;
	dport = 0;
	dst = mapdst.s_addr;
	sflags = flags & NAT_TCPUDPICMP;

	switch (p)
	{
	case IPPROTO_TCP :
	case IPPROTO_UDP :
		sport = htons(fin->fin_data[0]);
		dport = htons(fin->fin_data[1]);
		break;
	case IPPROTO_ICMP :
		if (flags & IPN_ICMPERR)
			sport = fin->fin_data[1];
		else
			dport = fin->fin_data[1];
		break;
	default :
		break;
	}


	if ((flags & SI_WILDP) != 0)
		goto find_in_wild_ports;

	hv = NAT_HASH_FN(dst, dport, 0xffffffff);
	hv = NAT_HASH_FN(src.s_addr, hv + sport, ifs->ifs_ipf_nattable_sz);
	nat = ifs->ifs_nat_table[1][hv];
	for (; nat; nat = nat->nat_hnext[1]) {
		if (nat->nat_ifps[0] != NULL) {
			if ((ifp != NULL) && (ifp != nat->nat_ifps[0]))
				continue;
		} else if (ifp != NULL)
			nat->nat_ifps[0] = ifp;

		nflags = nat->nat_flags;

		if (nat->nat_oip.s_addr == src.s_addr &&
		    nat->nat_outip.s_addr == dst &&
		    (((p == 0) &&
		      (sflags == (nat->nat_flags & IPN_TCPUDPICMP)))
		     || (p == nat->nat_p))) {
			switch (p)
			{
#if 0
			case IPPROTO_GRE :
				if (nat->nat_call[1] != fin->fin_data[0])
					continue;
				break;
#endif
			case IPPROTO_ICMP :
				if ((flags & IPN_ICMPERR) != 0) {
					if (nat->nat_outport != sport)
						continue;
				} else {
					if (nat->nat_outport != dport)
						continue;
				}
				break;
			case IPPROTO_TCP :
			case IPPROTO_UDP :
				if (nat->nat_oport != sport)
					continue;
				if (nat->nat_outport != dport)
					continue;
				break;
			default :
				break;
			}

			ipn = nat->nat_ptr;
			if ((ipn != NULL) && (nat->nat_aps != NULL))
				if (appr_match(fin, nat) != 0)
					continue;
			return nat;
		}
	}

	/*
	 * So if we didn't find it but there are wildcard members in the hash
	 * table, go back and look for them.  We do this search and update here
	 * because it is modifying the NAT table and we want to do this only
	 * for the first packet that matches.  The exception, of course, is
	 * for "dummy" (FI_IGNORE) lookups.
	 */
find_in_wild_ports:
	if (!(flags & NAT_TCPUDP) || !(flags & NAT_SEARCH))
		return NULL;
	if (ifs->ifs_nat_stats.ns_wilds == 0)
		return NULL;

	RWLOCK_EXIT(&ifs->ifs_ipf_nat);

	hv = NAT_HASH_FN(dst, 0, 0xffffffff);
	hv = NAT_HASH_FN(src.s_addr, hv, ifs->ifs_ipf_nattable_sz);

	WRITE_ENTER(&ifs->ifs_ipf_nat);

	nat = ifs->ifs_nat_table[1][hv];
	for (; nat; nat = nat->nat_hnext[1]) {
		if (nat->nat_ifps[0] != NULL) {
			if ((ifp != NULL) && (ifp != nat->nat_ifps[0]))
				continue;
		} else if (ifp != NULL)
			nat->nat_ifps[0] = ifp;

		if (nat->nat_p != fin->fin_p)
			continue;
		if (nat->nat_oip.s_addr != src.s_addr ||
		    nat->nat_outip.s_addr != dst)
			continue;

		nflags = nat->nat_flags;
		if (!(nflags & (NAT_TCPUDP|SI_WILDP)))
			continue;

		if (nat_wildok(nat, (int)sport, (int)dport, nflags,
			       NAT_INBOUND) == 1) {
			if ((fin->fin_flx & FI_IGNORE) != 0)
				break;
			if ((nflags & SI_CLONE) != 0) {
				nat = fr_natclone(fin, nat);
				if (nat == NULL)
					break;
			} else {
				MUTEX_ENTER(&ifs->ifs_ipf_nat_new);
				ifs->ifs_nat_stats.ns_wilds--;
				MUTEX_EXIT(&ifs->ifs_ipf_nat_new);
			}
			nat->nat_oport = sport;
			nat->nat_outport = dport;
			nat->nat_flags &= ~(SI_W_DPORT|SI_W_SPORT);
			nat_tabmove(nat, ifs);
			break;
		}
	}

	MUTEX_DOWNGRADE(&ifs->ifs_ipf_nat);

	return nat;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_tabmove                                                 */
/* Returns:     Nil                                                         */
/* Parameters:  nat(I) - pointer to NAT structure                           */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* This function is only called for TCP/UDP NAT table entries where the     */
/* original was placed in the table without hashing on the ports and we now */
/* want to include hashing on port numbers.                                 */
/* ------------------------------------------------------------------------ */
static void nat_tabmove(nat, ifs)
nat_t *nat;
ipf_stack_t *ifs;
{
	nat_t **natp;
	u_int hv;

	if (nat->nat_flags & SI_CLONE)
		return;

	/*
	 * Remove the NAT entry from the old location
	 */
	if (nat->nat_hnext[0])
		nat->nat_hnext[0]->nat_phnext[0] = nat->nat_phnext[0];
	*nat->nat_phnext[0] = nat->nat_hnext[0];
	ifs->ifs_nat_stats.ns_bucketlen[0][nat->nat_hv[0]]--;

	if (nat->nat_hnext[1])
		nat->nat_hnext[1]->nat_phnext[1] = nat->nat_phnext[1];
	*nat->nat_phnext[1] = nat->nat_hnext[1];
	ifs->ifs_nat_stats.ns_bucketlen[1][nat->nat_hv[1]]--;

	/*
	 * Add into the NAT table in the new position
	 */
	hv = NAT_HASH_FN(nat->nat_inip.s_addr, nat->nat_inport, 0xffffffff);
	hv = NAT_HASH_FN(nat->nat_oip.s_addr, hv + nat->nat_oport,
			 ifs->ifs_ipf_nattable_sz);
	nat->nat_hv[0] = hv;
	natp = &ifs->ifs_nat_table[0][hv];
	if (*natp)
		(*natp)->nat_phnext[0] = &nat->nat_hnext[0];
	nat->nat_phnext[0] = natp;
	nat->nat_hnext[0] = *natp;
	*natp = nat;
	ifs->ifs_nat_stats.ns_bucketlen[0][hv]++;

	hv = NAT_HASH_FN(nat->nat_outip.s_addr, nat->nat_outport, 0xffffffff);
	hv = NAT_HASH_FN(nat->nat_oip.s_addr, hv + nat->nat_oport,
			 ifs->ifs_ipf_nattable_sz);
	nat->nat_hv[1] = hv;
	natp = &ifs->ifs_nat_table[1][hv];
	if (*natp)
		(*natp)->nat_phnext[1] = &nat->nat_hnext[1];
	nat->nat_phnext[1] = natp;
	nat->nat_hnext[1] = *natp;
	*natp = nat;
	ifs->ifs_nat_stats.ns_bucketlen[1][hv]++;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_outlookup                                               */
/* Returns:     nat_t* - NULL == no match,                                  */
/*                       else pointer to matching NAT entry                 */
/* Parameters:  fin(I)   - pointer to packet information                    */
/*              flags(I) - NAT flags for this packet                        */
/*              p(I)     - protocol for this packet                         */
/*              src(I)   - source IP address                                */
/*              dst(I)   - destination IP address                           */
/*              rw(I)    - 1 == write lock on ipf_nat held, 0 == read lock. */
/*                                                                          */
/* Lookup a nat entry based on the source 'real' ip address/port and        */
/* destination address/port.  We use this lookup when sending a packet out, */
/* we're looking for a table entry, based on the source address.            */
/*                                                                          */
/* NOTE: THE PACKET BEING CHECKED (IF FOUND) HAS A MAPPING ALREADY.         */
/*                                                                          */
/* NOTE: IT IS ASSUMED THAT ipf_nat IS ONLY HELD WITH A READ LOCK WHEN      */
/*       THIS FUNCTION IS CALLED WITH NAT_SEARCH SET IN nflags.             */
/*                                                                          */
/* flags   -> relevant are IPN_UDP/IPN_TCP/IPN_ICMPQUERY that indicate if   */
/*            the packet is of said protocol                                */
/* ------------------------------------------------------------------------ */
nat_t *nat_outlookup(fin, flags, p, src, dst)
fr_info_t *fin;
u_int flags, p;
struct in_addr src , dst;
{
	u_short sport, dport;
	u_int sflags;
	ipnat_t *ipn;
	u_32_t srcip;
	nat_t *nat;
	int nflags;
	void *ifp;
	u_int hv;
	frentry_t *fr;
	ipf_stack_t *ifs = fin->fin_ifs;

	fr = fin->fin_fr;

	if ((fr != NULL) && !(fr->fr_flags & FR_DUP) &&
	    fr->fr_tif.fd_ifp && fr->fr_tif.fd_ifp != (void *)-1)
		ifp = fr->fr_tif.fd_ifp;
	else
		ifp = fin->fin_ifp;

	srcip = src.s_addr;
	sflags = flags & IPN_TCPUDPICMP;
	sport = 0;
	dport = 0;

	switch (p)
	{
	case IPPROTO_TCP :
	case IPPROTO_UDP :
		sport = htons(fin->fin_data[0]);
		dport = htons(fin->fin_data[1]);
		break;
	case IPPROTO_ICMP :
		if (flags & IPN_ICMPERR)
			sport = fin->fin_data[1];
		else
			dport = fin->fin_data[1];
		break;
	default :
		break;
	}

	if ((flags & SI_WILDP) != 0)
		goto find_out_wild_ports;

	hv = NAT_HASH_FN(srcip, sport, 0xffffffff);
	hv = NAT_HASH_FN(dst.s_addr, hv + dport, ifs->ifs_ipf_nattable_sz);
	nat = ifs->ifs_nat_table[0][hv];
	for (; nat; nat = nat->nat_hnext[0]) {
		if (nat->nat_ifps[1] != NULL) {
			if ((ifp != NULL) && (ifp != nat->nat_ifps[1]))
				continue;
		} else if (ifp != NULL)
			nat->nat_ifps[1] = ifp;

		nflags = nat->nat_flags;
 
		if (nat->nat_inip.s_addr == srcip &&
		    nat->nat_oip.s_addr == dst.s_addr &&
		    (((p == 0) && (sflags == (nflags & NAT_TCPUDPICMP)))
		     || (p == nat->nat_p))) {
			switch (p)
			{
#if 0
			case IPPROTO_GRE :
				if (nat->nat_call[1] != fin->fin_data[0])
					continue;
				break;
#endif
			case IPPROTO_TCP :
			case IPPROTO_UDP :
				if (nat->nat_oport != dport)
					continue;
				if (nat->nat_inport != sport)
					continue;
				break;
			default :
				break;
			}

			ipn = nat->nat_ptr;
			if ((ipn != NULL) && (nat->nat_aps != NULL))
				if (appr_match(fin, nat) != 0)
					continue;
			return nat;
		}
	}

	/*
	 * So if we didn't find it but there are wildcard members in the hash
	 * table, go back and look for them.  We do this search and update here
	 * because it is modifying the NAT table and we want to do this only
	 * for the first packet that matches.  The exception, of course, is
	 * for "dummy" (FI_IGNORE) lookups.
	 */
find_out_wild_ports:
	if (!(flags & NAT_TCPUDP) || !(flags & NAT_SEARCH))
		return NULL;
	if (ifs->ifs_nat_stats.ns_wilds == 0)
		return NULL;

	RWLOCK_EXIT(&ifs->ifs_ipf_nat);

	hv = NAT_HASH_FN(srcip, 0, 0xffffffff);
	hv = NAT_HASH_FN(dst.s_addr, hv, ifs->ifs_ipf_nattable_sz);

	WRITE_ENTER(&ifs->ifs_ipf_nat);

	nat = ifs->ifs_nat_table[0][hv];
	for (; nat; nat = nat->nat_hnext[0]) {
		if (nat->nat_ifps[1] != NULL) {
			if ((ifp != NULL) && (ifp != nat->nat_ifps[1]))
				continue;
		} else if (ifp != NULL)
			nat->nat_ifps[1] = ifp;

		if (nat->nat_p != fin->fin_p)
			continue;
		if ((nat->nat_inip.s_addr != srcip) ||
		    (nat->nat_oip.s_addr != dst.s_addr))
			continue;

		nflags = nat->nat_flags;
		if (!(nflags & (NAT_TCPUDP|SI_WILDP)))
			continue;

		if (nat_wildok(nat, (int)sport, (int)dport, nflags,
			       NAT_OUTBOUND) == 1) {
			if ((fin->fin_flx & FI_IGNORE) != 0)
				break;
			if ((nflags & SI_CLONE) != 0) {
				nat = fr_natclone(fin, nat);
				if (nat == NULL)
					break;
			} else {
				MUTEX_ENTER(&ifs->ifs_ipf_nat_new);
				ifs->ifs_nat_stats.ns_wilds--;
				MUTEX_EXIT(&ifs->ifs_ipf_nat_new);
			}
			nat->nat_inport = sport;
			nat->nat_oport = dport;
			if (nat->nat_outport == 0)
				nat->nat_outport = sport;
			nat->nat_flags &= ~(SI_W_DPORT|SI_W_SPORT);
			nat_tabmove(nat, ifs);
			break;
		}
	}

	MUTEX_DOWNGRADE(&ifs->ifs_ipf_nat);

	return nat;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_lookupredir                                             */
/* Returns:     nat_t* - NULL == no match,                                  */
/*                       else pointer to matching NAT entry                 */
/* Parameters:  np(I) - pointer to description of packet to find NAT table  */
/*                      entry for.                                          */
/*                                                                          */
/* Lookup the NAT tables to search for a matching redirect                  */
/* ------------------------------------------------------------------------ */
nat_t *nat_lookupredir(np, ifs)
natlookup_t *np;
ipf_stack_t *ifs;
{
	fr_info_t fi;
	nat_t *nat;

	bzero((char *)&fi, sizeof(fi));
	if (np->nl_flags & IPN_IN) {
		fi.fin_data[0] = ntohs(np->nl_realport);
		fi.fin_data[1] = ntohs(np->nl_outport);
	} else {
		fi.fin_data[0] = ntohs(np->nl_inport);
		fi.fin_data[1] = ntohs(np->nl_outport);
	}
	if (np->nl_flags & IPN_TCP)
		fi.fin_p = IPPROTO_TCP;
	else if (np->nl_flags & IPN_UDP)
		fi.fin_p = IPPROTO_UDP;
	else if (np->nl_flags & (IPN_ICMPERR|IPN_ICMPQUERY))
		fi.fin_p = IPPROTO_ICMP;

	fi.fin_ifs = ifs;
	/*
	 * We can do two sorts of lookups:
	 * - IPN_IN: we have the `real' and `out' address, look for `in'.
	 * - default: we have the `in' and `out' address, look for `real'.
	 */
	if (np->nl_flags & IPN_IN) {
		if ((nat = nat_inlookup(&fi, np->nl_flags, fi.fin_p,
					np->nl_realip, np->nl_outip))) {
			np->nl_inip = nat->nat_inip;
			np->nl_inport = nat->nat_inport;
		}
	} else {
		/*
		 * If nl_inip is non null, this is a lookup based on the real
		 * ip address. Else, we use the fake.
		 */
		if ((nat = nat_outlookup(&fi, np->nl_flags, fi.fin_p,
					 np->nl_inip, np->nl_outip))) {

			if ((np->nl_flags & IPN_FINDFORWARD) != 0) {
				fr_info_t fin;
				bzero((char *)&fin, sizeof(fin));
				fin.fin_p = nat->nat_p;
				fin.fin_data[0] = ntohs(nat->nat_outport);
				fin.fin_data[1] = ntohs(nat->nat_oport);
				fin.fin_ifs = ifs;
				if (nat_inlookup(&fin, np->nl_flags, fin.fin_p,
						 nat->nat_outip,
						 nat->nat_oip) != NULL) {
					np->nl_flags &= ~IPN_FINDFORWARD;
				}
			}

			np->nl_realip = nat->nat_outip;
			np->nl_realport = nat->nat_outport;
		}
 	}

	return nat;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_match                                                   */
/* Returns:     int - 0 == no match, 1 == match                             */
/* Parameters:  fin(I)   - pointer to packet information                    */
/*              np(I)    - pointer to NAT rule                              */
/*                                                                          */
/* Pull the matching of a packet against a NAT rule out of that complex     */
/* loop inside fr_checknatin() and lay it out properly in its own function. */
/* ------------------------------------------------------------------------ */
static int nat_match(fin, np)
fr_info_t *fin;
ipnat_t *np;
{
	frtuc_t *ft;

	if (fin->fin_v != 4)
		return 0;

	if (np->in_p && fin->fin_p != np->in_p)
		return 0;

	if (fin->fin_out) {
		if (!(np->in_redir & (NAT_MAP|NAT_MAPBLK)))
			return 0;
		if (((fin->fin_fi.fi_saddr & np->in_inmsk) != np->in_inip)
		    ^ ((np->in_flags & IPN_NOTSRC) != 0))
			return 0;
		if (((fin->fin_fi.fi_daddr & np->in_srcmsk) != np->in_srcip)
		    ^ ((np->in_flags & IPN_NOTDST) != 0))
			return 0;
	} else {
		if (!(np->in_redir & NAT_REDIRECT))
			return 0;
		if (((fin->fin_fi.fi_saddr & np->in_srcmsk) != np->in_srcip)
		    ^ ((np->in_flags & IPN_NOTSRC) != 0))
			return 0;
		if (((fin->fin_fi.fi_daddr & np->in_outmsk) != np->in_outip)
		    ^ ((np->in_flags & IPN_NOTDST) != 0))
			return 0;
	}

	ft = &np->in_tuc;
	if (!(fin->fin_flx & FI_TCPUDP) ||
	    (fin->fin_flx & (FI_SHORT|FI_FRAGBODY))) {
		if (ft->ftu_scmp || ft->ftu_dcmp)
			return 0;
		return 1;
	}

	return fr_tcpudpchk(fin, ft);
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_update                                                  */
/* Returns:     Nil                                                         */
/* Parameters:  nat(I)    - pointer to NAT structure                        */
/*              np(I)     - pointer to NAT rule                             */
/*                                                                          */
/* Updates the lifetime of a NAT table entry for non-TCP packets.  Must be  */
/* called with fin_rev updated - i.e. after calling nat_proto().            */
/* ------------------------------------------------------------------------ */
void nat_update(fin, nat, np)
fr_info_t *fin;
nat_t *nat;
ipnat_t *np;
{
	ipftq_t *ifq, *ifq2;
	ipftqent_t *tqe;
	ipf_stack_t *ifs = fin->fin_ifs;

	MUTEX_ENTER(&nat->nat_lock);
	tqe = &nat->nat_tqe;
	ifq = tqe->tqe_ifq;

	/*
	 * We allow over-riding of NAT timeouts from NAT rules, even for
	 * TCP, however, if it is TCP and there is no rule timeout set,
	 * then do not update the timeout here.
	 */
	if (np != NULL)
		ifq2 = np->in_tqehead[fin->fin_rev];
	else
		ifq2 = NULL;

	if (nat->nat_p == IPPROTO_TCP && ifq2 == NULL) {
		(void) fr_tcp_age(&nat->nat_tqe, fin, ifs->ifs_nat_tqb, 0);
	} else {
		if (ifq2 == NULL) {
			if (nat->nat_p == IPPROTO_UDP)
				ifq2 = &ifs->ifs_nat_udptq;
			else if (nat->nat_p == IPPROTO_ICMP)
				ifq2 = &ifs->ifs_nat_icmptq;
			else
				ifq2 = &ifs->ifs_nat_iptq;
		}

		fr_movequeue(tqe, ifq, ifq2, ifs);
	}
	MUTEX_EXIT(&nat->nat_lock);
}


/* ------------------------------------------------------------------------ */
/* Function:    fr_checknatout                                              */
/* Returns:     int - -1 == packet failed NAT checks so block it,           */
/*                     0 == no packet translation occurred,                 */
/*                     1 == packet was successfully translated.             */
/* Parameters:  fin(I)   - pointer to packet information                    */
/*              passp(I) - pointer to filtering result flags                */
/*                                                                          */
/* Check to see if an outcoming packet should be changed.  ICMP packets are */
/* first checked to see if they match an existing entry (if an error),      */
/* otherwise a search of the current NAT table is made.  If neither results */
/* in a match then a search for a matching NAT rule is made.  Create a new  */
/* NAT entry if a we matched a NAT rule.  Lastly, actually change the       */
/* packet header(s) as required.                                            */
/* ------------------------------------------------------------------------ */
int fr_checknatout(fin, passp)
fr_info_t *fin;
u_32_t *passp;
{
	struct ifnet *ifp, *sifp;
	icmphdr_t *icmp = NULL;
	tcphdr_t *tcp = NULL;
	int rval, natfailed;
	ipnat_t *np = NULL;
	u_int nflags = 0;
	u_32_t ipa, iph;
	int natadd = 1;
	frentry_t *fr;
	nat_t *nat;
	ipf_stack_t *ifs = fin->fin_ifs;

	if (ifs->ifs_nat_stats.ns_rules == 0 || ifs->ifs_fr_nat_lock != 0)
		return 0;

	natfailed = 0;
	fr = fin->fin_fr;
	sifp = fin->fin_ifp;
	if ((fr != NULL) && !(fr->fr_flags & FR_DUP) &&
	    fr->fr_tif.fd_ifp && fr->fr_tif.fd_ifp != (void *)-1)
		fin->fin_ifp = fr->fr_tif.fd_ifp;
	ifp = fin->fin_ifp;

	if (!(fin->fin_flx & FI_SHORT) && (fin->fin_off == 0)) {
		switch (fin->fin_p)
		{
		case IPPROTO_TCP :
			nflags = IPN_TCP;
			break;
		case IPPROTO_UDP :
			nflags = IPN_UDP;
			break;
		case IPPROTO_ICMP :
			icmp = fin->fin_dp;

			/*
			 * This is an incoming packet, so the destination is
			 * the icmp_id and the source port equals 0
			 */
			if (nat_icmpquerytype4(icmp->icmp_type))
				nflags = IPN_ICMPQUERY;
			break;
		default :
			break;
		}
		
		if ((nflags & IPN_TCPUDP))
			tcp = fin->fin_dp;
	}

	ipa = fin->fin_saddr;

	READ_ENTER(&ifs->ifs_ipf_nat);

	if ((fin->fin_p == IPPROTO_ICMP) && !(nflags & IPN_ICMPQUERY) &&
	    (nat = nat_icmperror(fin, &nflags, NAT_OUTBOUND)))
		/*EMPTY*/;
	else if ((fin->fin_flx & FI_FRAG) && (nat = fr_nat_knownfrag(fin)))
		natadd = 0;
	else if ((nat = nat_outlookup(fin, nflags|NAT_SEARCH, (u_int)fin->fin_p,
				      fin->fin_src, fin->fin_dst))) {
		nflags = nat->nat_flags;
	} else {
		u_32_t hv, msk, nmsk;

		/*
		 * If there is no current entry in the nat table for this IP#,
		 * create one for it (if there is a matching rule).
		 */
		RWLOCK_EXIT(&ifs->ifs_ipf_nat);
		msk = 0xffffffff;
		nmsk = ifs->ifs_nat_masks;
		WRITE_ENTER(&ifs->ifs_ipf_nat);
maskloop:
		iph = ipa & htonl(msk);
		hv = NAT_HASH_FN(iph, 0, ifs->ifs_ipf_natrules_sz);
		for (np = ifs->ifs_nat_rules[hv]; np; np = np->in_mnext)
		{
			if ((np->in_ifps[1] && (np->in_ifps[1] != ifp)))
				continue;
			if (np->in_v != fin->fin_v)
				continue;
			if (np->in_p && (np->in_p != fin->fin_p))
				continue;
			if ((np->in_flags & IPN_RF) && !(np->in_flags & nflags))
				continue;
			if (np->in_flags & IPN_FILTER) {
				if (!nat_match(fin, np))
					continue;
			} else if ((ipa & np->in_inmsk) != np->in_inip)
				continue;

			if ((fr != NULL) &&
			    !fr_matchtag(&np->in_tag, &fr->fr_nattag))
				continue;

			if (*np->in_plabel != '\0') {
				if (((np->in_flags & IPN_FILTER) == 0) &&
				    (np->in_dport != tcp->th_dport))
					continue;
				if (appr_ok(fin, tcp, np) == 0)
					continue;
			}

			if ((nat = nat_new(fin, np, NULL, nflags,
					   NAT_OUTBOUND))) {
				np->in_hits++;
				break;
			} else
				natfailed = -1;
		}
		if ((np == NULL) && (nmsk != 0)) {
			while (nmsk) {
				msk <<= 1;
				if (nmsk & 0x80000000)
					break;
				nmsk <<= 1;
			}
			if (nmsk != 0) {
				nmsk <<= 1;
				goto maskloop;
			}
		}
		MUTEX_DOWNGRADE(&ifs->ifs_ipf_nat);
	}

	if (nat != NULL) {
		rval = fr_natout(fin, nat, natadd, nflags);
		if (rval == 1) {
			MUTEX_ENTER(&nat->nat_lock);
			nat->nat_ref++;
			MUTEX_EXIT(&nat->nat_lock);
			fin->fin_nat = nat;
		}
	} else
		rval = natfailed;
	RWLOCK_EXIT(&ifs->ifs_ipf_nat);

	if (rval == -1) {
		if (passp != NULL)
			*passp = FR_BLOCK;
		fin->fin_flx |= FI_BADNAT;
	}
	fin->fin_ifp = sifp;
	return rval;
}

/* ------------------------------------------------------------------------ */
/* Function:    fr_natout                                                   */
/* Returns:     int - -1 == packet failed NAT checks so block it,           */
/*                     1 == packet was successfully translated.             */
/* Parameters:  fin(I)    - pointer to packet information                   */
/*              nat(I)    - pointer to NAT structure                        */
/*              natadd(I) - flag indicating if it is safe to add frag cache */
/*              nflags(I) - NAT flags set for this packet                   */
/*                                                                          */
/* Translate a packet coming "out" on an interface.                         */
/* ------------------------------------------------------------------------ */
int fr_natout(fin, nat, natadd, nflags)
fr_info_t *fin;
nat_t *nat;
int natadd;
u_32_t nflags;
{
	icmphdr_t *icmp;
	u_short *csump;
	u_32_t sumd;
	tcphdr_t *tcp;
	ipnat_t *np;
	int i;
	ipf_stack_t *ifs = fin->fin_ifs;

#if SOLARIS && defined(_KERNEL)
	net_data_t net_data_p;
	if (fin->fin_v == 4)
		net_data_p = ifs->ifs_ipf_ipv4;
	else
		net_data_p = ifs->ifs_ipf_ipv6;
#endif

	tcp = NULL;
	icmp = NULL;
	csump = NULL;
	np = nat->nat_ptr;

	if ((natadd != 0) && (fin->fin_flx & FI_FRAG) && (np != NULL))
		(void) fr_nat_newfrag(fin, 0, nat);

	MUTEX_ENTER(&nat->nat_lock);
	nat->nat_bytes[1] += fin->fin_plen;
	nat->nat_pkts[1]++;
	MUTEX_EXIT(&nat->nat_lock);
	
	/*
	 * Fix up checksums, not by recalculating them, but
	 * simply computing adjustments.
	 * This is only done for STREAMS based IP implementations where the
	 * checksum has already been calculated by IP.  In all other cases,
	 * IPFilter is called before the checksum needs calculating so there
	 * is no call to modify whatever is in the header now.
	 */
	ASSERT(fin->fin_m != NULL);
	if (fin->fin_v == 4 && !NET_IS_HCK_L3_FULL(net_data_p, fin->fin_m)) {
		if (nflags == IPN_ICMPERR) {
			u_32_t s1, s2;

			s1 = LONG_SUM(ntohl(fin->fin_saddr));
			s2 = LONG_SUM(ntohl(nat->nat_outip.s_addr));
			CALC_SUMD(s1, s2, sumd);

			fix_outcksum(&fin->fin_ip->ip_sum, sumd);
		}
#if !defined(_KERNEL) || defined(MENTAT) || defined(__sgi) || \
    defined(linux) || defined(BRIDGE_IPF)
		else {
			/*
			 * Strictly speaking, this isn't necessary on BSD
			 * kernels because they do checksum calculation after
			 * this code has run BUT if ipfilter is being used
			 * to do NAT as a bridge, that code doesn't exist.
			 */
			if (nat->nat_dir == NAT_OUTBOUND)
				fix_outcksum(&fin->fin_ip->ip_sum,
					    nat->nat_ipsumd);
			else
				fix_incksum(&fin->fin_ip->ip_sum,
				 	   nat->nat_ipsumd);
		}
#endif
	}

	if (!(fin->fin_flx & FI_SHORT) && (fin->fin_off == 0)) {
		if ((nat->nat_outport != 0) && (nflags & IPN_TCPUDP)) {
			tcp = fin->fin_dp;

			tcp->th_sport = nat->nat_outport;
			fin->fin_data[0] = ntohs(nat->nat_outport);
		}

		if ((nat->nat_outport != 0) && (nflags & IPN_ICMPQUERY)) {
			icmp = fin->fin_dp;
			icmp->icmp_id = nat->nat_outport;
		}

		csump = nat_proto(fin, nat, nflags);
	}

	fin->fin_ip->ip_src = nat->nat_outip;

	nat_update(fin, nat, np);

	/*
	 * The above comments do not hold for layer 4 (or higher) checksums...
	 */
	if (csump != NULL && !NET_IS_HCK_L4_FULL(net_data_p, fin->fin_m)) {
		if (nflags & IPN_TCPUDP &&
	   	    NET_IS_HCK_L4_PART(net_data_p, fin->fin_m))
			sumd = nat->nat_sumd[1];
		else
			sumd = nat->nat_sumd[0];

		if (nat->nat_dir == NAT_OUTBOUND)
			fix_outcksum(csump, sumd);
		else
			fix_incksum(csump, sumd);
	}
#ifdef	IPFILTER_SYNC
	ipfsync_update(SMC_NAT, fin, nat->nat_sync);
#endif
	/* ------------------------------------------------------------- */
	/* A few quick notes:						 */
	/*	Following are test conditions prior to calling the 	 */
	/*	appr_check routine.					 */
	/*								 */
	/* 	A NULL tcp indicates a non TCP/UDP packet.  When dealing */
	/*	with a redirect rule, we attempt to match the packet's	 */
	/*	source port against in_dport, otherwise	we'd compare the */
	/*	packet's destination.			 		 */
	/* ------------------------------------------------------------- */
	if ((np != NULL) && (np->in_apr != NULL)) {
		i = appr_check(fin, nat);
		if (i == 0)
			i = 1;
	} else
		i = 1;
	ATOMIC_INCL(ifs->ifs_nat_stats.ns_mapped[1]);
	fin->fin_flx |= FI_NATED;
	return i;
}


/* ------------------------------------------------------------------------ */
/* Function:    fr_checknatin                                               */
/* Returns:     int - -1 == packet failed NAT checks so block it,           */
/*                     0 == no packet translation occurred,                 */
/*                     1 == packet was successfully translated.             */
/* Parameters:  fin(I)   - pointer to packet information                    */
/*              passp(I) - pointer to filtering result flags                */
/*                                                                          */
/* Check to see if an incoming packet should be changed.  ICMP packets are  */
/* first checked to see if they match an existing entry (if an error),      */
/* otherwise a search of the current NAT table is made.  If neither results */
/* in a match then a search for a matching NAT rule is made.  Create a new  */
/* NAT entry if a we matched a NAT rule.  Lastly, actually change the       */
/* packet header(s) as required.                                            */
/* ------------------------------------------------------------------------ */
int fr_checknatin(fin, passp)
fr_info_t *fin;
u_32_t *passp;
{
	u_int nflags, natadd;
	int rval, natfailed;
	struct ifnet *ifp;
	struct in_addr in;
	icmphdr_t *icmp;
	tcphdr_t *tcp;
	u_short dport;
	ipnat_t *np;
	nat_t *nat;
	u_32_t iph;
	ipf_stack_t *ifs = fin->fin_ifs;

	if (ifs->ifs_nat_stats.ns_rules == 0 || ifs->ifs_fr_nat_lock != 0)
		return 0;

	tcp = NULL;
	icmp = NULL;
	dport = 0;
	natadd = 1;
	nflags = 0;
	natfailed = 0;
	ifp = fin->fin_ifp;

	if (!(fin->fin_flx & FI_SHORT) && (fin->fin_off == 0)) {
		switch (fin->fin_p)
		{
		case IPPROTO_TCP :
			nflags = IPN_TCP;
			break;
		case IPPROTO_UDP :
			nflags = IPN_UDP;
			break;
		case IPPROTO_ICMP :
			icmp = fin->fin_dp;

			/*
			 * This is an incoming packet, so the destination is
			 * the icmp_id and the source port equals 0
			 */
			if (nat_icmpquerytype4(icmp->icmp_type)) {
				nflags = IPN_ICMPQUERY;
				dport = icmp->icmp_id;	
			} break;
		default :
			break;
		}
		
		if ((nflags & IPN_TCPUDP)) {
			tcp = fin->fin_dp;
			dport = tcp->th_dport;
		}
	}

	in = fin->fin_dst;

	READ_ENTER(&ifs->ifs_ipf_nat);

	if ((fin->fin_p == IPPROTO_ICMP) && !(nflags & IPN_ICMPQUERY) &&
	    (nat = nat_icmperror(fin, &nflags, NAT_INBOUND)))
		/*EMPTY*/;
	else if ((fin->fin_flx & FI_FRAG) && (nat = fr_nat_knownfrag(fin)))
		natadd = 0;
	else if ((nat = nat_inlookup(fin, nflags|NAT_SEARCH, (u_int)fin->fin_p,
				     fin->fin_src, in))) {
		nflags = nat->nat_flags;
	} else {
		u_32_t hv, msk, rmsk;

		RWLOCK_EXIT(&ifs->ifs_ipf_nat);
		rmsk = ifs->ifs_rdr_masks;
		msk = 0xffffffff;
		WRITE_ENTER(&ifs->ifs_ipf_nat);
		/*
		 * If there is no current entry in the nat table for this IP#,
		 * create one for it (if there is a matching rule).
		 */
maskloop:
		iph = in.s_addr & htonl(msk);
		hv = NAT_HASH_FN(iph, 0, ifs->ifs_ipf_rdrrules_sz);
		for (np = ifs->ifs_rdr_rules[hv]; np; np = np->in_rnext) {
			if (np->in_ifps[0] && (np->in_ifps[0] != ifp))
				continue;
			if (np->in_v != fin->fin_v)
				continue;
			if (np->in_p && (np->in_p != fin->fin_p))
				continue;
			if ((np->in_flags & IPN_RF) && !(np->in_flags & nflags))
				continue;
			if (np->in_flags & IPN_FILTER) {
				if (!nat_match(fin, np))
					continue;
			} else {
				if ((in.s_addr & np->in_outmsk) != np->in_outip)
					continue;
				if (np->in_pmin &&
				    ((ntohs(np->in_pmax) < ntohs(dport)) ||
				     (ntohs(dport) < ntohs(np->in_pmin))))
					continue;
			}

			if (*np->in_plabel != '\0') {
				if (!appr_ok(fin, tcp, np)) {
					continue;
				}
			}

			nat = nat_new(fin, np, NULL, nflags, NAT_INBOUND);
			if (nat != NULL) {
				np->in_hits++;
				break;
			} else
				natfailed = -1;
		}

		if ((np == NULL) && (rmsk != 0)) {
			while (rmsk) {
				msk <<= 1;
				if (rmsk & 0x80000000)
					break;
				rmsk <<= 1;
			}
			if (rmsk != 0) {
				rmsk <<= 1;
				goto maskloop;
			}
		}
		MUTEX_DOWNGRADE(&ifs->ifs_ipf_nat);
	}
	if (nat != NULL) {
		rval = fr_natin(fin, nat, natadd, nflags);
		if (rval == 1) {
			MUTEX_ENTER(&nat->nat_lock);
			nat->nat_ref++;
			MUTEX_EXIT(&nat->nat_lock);
			fin->fin_nat = nat;
			fin->fin_state = nat->nat_state;
		}
	} else
		rval = natfailed;
	RWLOCK_EXIT(&ifs->ifs_ipf_nat);

	if (rval == -1) {
		if (passp != NULL)
			*passp = FR_BLOCK;
		fin->fin_flx |= FI_BADNAT;
	}
	return rval;
}


/* ------------------------------------------------------------------------ */
/* Function:    fr_natin                                                    */
/* Returns:     int - -1 == packet failed NAT checks so block it,           */
/*                     1 == packet was successfully translated.             */
/* Parameters:  fin(I)    - pointer to packet information                   */
/*              nat(I)    - pointer to NAT structure                        */
/*              natadd(I) - flag indicating if it is safe to add frag cache */
/*              nflags(I) - NAT flags set for this packet                   */
/* Locks Held:  ipf_nat (READ)                                              */
/*                                                                          */
/* Translate a packet coming "in" on an interface.                          */
/* ------------------------------------------------------------------------ */
int fr_natin(fin, nat, natadd, nflags)
fr_info_t *fin;
nat_t *nat;
int natadd;
u_32_t nflags;
{
	icmphdr_t *icmp;
	u_short *csump, *csump1;
	u_32_t sumd;
	tcphdr_t *tcp;
	ipnat_t *np;
	int i;
	ipf_stack_t *ifs = fin->fin_ifs;

#if SOLARIS && defined(_KERNEL)
	net_data_t net_data_p;
	if (fin->fin_v == 4)
		net_data_p = ifs->ifs_ipf_ipv4;
	else
		net_data_p = ifs->ifs_ipf_ipv6;
#endif

	tcp = NULL;
	csump = NULL;
	np = nat->nat_ptr;
	fin->fin_fr = nat->nat_fr;

	if (np != NULL) {
		if ((natadd != 0) && (fin->fin_flx & FI_FRAG))
			(void) fr_nat_newfrag(fin, 0, nat);

	/* ------------------------------------------------------------- */
	/* A few quick notes:						 */
	/*	Following are test conditions prior to calling the 	 */
	/*	appr_check routine.					 */
	/*								 */
	/* 	A NULL tcp indicates a non TCP/UDP packet.  When dealing */
	/*	with a map rule, we attempt to match the packet's	 */
	/*	source port against in_dport, otherwise	we'd compare the */
	/*	packet's destination.			 		 */
	/* ------------------------------------------------------------- */
		if (np->in_apr != NULL) {
			i = appr_check(fin, nat);
			if (i == -1) {
				return -1;
			}
		}
	}

#ifdef	IPFILTER_SYNC
	ipfsync_update(SMC_NAT, fin, nat->nat_sync);
#endif

	MUTEX_ENTER(&nat->nat_lock);
	nat->nat_bytes[0] += fin->fin_plen;
	nat->nat_pkts[0]++;
	MUTEX_EXIT(&nat->nat_lock);

	fin->fin_ip->ip_dst = nat->nat_inip;
	fin->fin_fi.fi_daddr = nat->nat_inip.s_addr;
	if (nflags & IPN_TCPUDP)
		tcp = fin->fin_dp;

	/*
	 * Fix up checksums, not by recalculating them, but
	 * simply computing adjustments.
	 * Why only do this for some platforms on inbound packets ?
	 * Because for those that it is done, IP processing is yet to happen
	 * and so the IPv4 header checksum has not yet been evaluated.
	 * Perhaps it should always be done for the benefit of things like
	 * fast forwarding (so that it doesn't need to be recomputed) but with
	 * header checksum offloading, perhaps it is a moot point.
	 */
#if !defined(_KERNEL) || defined(MENTAT) || defined(__sgi) || \
     defined(__osf__) || defined(linux)
	if (nat->nat_dir == NAT_OUTBOUND)
		fix_incksum(&fin->fin_ip->ip_sum, nat->nat_ipsumd);
	else
		fix_outcksum(&fin->fin_ip->ip_sum, nat->nat_ipsumd);
#endif

	if (!(fin->fin_flx & FI_SHORT) && (fin->fin_off == 0)) {
		if ((nat->nat_inport != 0) && (nflags & IPN_TCPUDP)) {
			tcp->th_dport = nat->nat_inport;
			fin->fin_data[1] = ntohs(nat->nat_inport);
		}


		if ((nat->nat_inport != 0) && (nflags & IPN_ICMPQUERY)) {
			icmp = fin->fin_dp;

			icmp->icmp_id = nat->nat_inport;
		}

		csump = nat_proto(fin, nat, nflags);
	}

	nat_update(fin, nat, np);

#if SOLARIS && defined(_KERNEL)
	if (nflags & IPN_TCPUDP &&
	    NET_IS_HCK_L4_PART(net_data_p, fin->fin_m)) {
		sumd = nat->nat_sumd[1];
		csump1 = &(fin->fin_m->b_datap->db_struioun.cksum.cksum_val.u16);
		if (csump1 != NULL) {
			if (nat->nat_dir == NAT_OUTBOUND)
				fix_incksum(csump1, sumd);
			else
				fix_outcksum(csump1, sumd);
		}
	} else
#endif
		sumd = nat->nat_sumd[0];

	/*
	 * Inbound packets always need to have their address adjusted in case	
	 * code following this validates it.
	 */
	if (csump != NULL) {
		if (nat->nat_dir == NAT_OUTBOUND)
			fix_incksum(csump, sumd);
		else
			fix_outcksum(csump, sumd);
	}
	ATOMIC_INCL(ifs->ifs_nat_stats.ns_mapped[0]);
	fin->fin_flx |= FI_NATED;
	if (np != NULL && np->in_tag.ipt_num[0] != 0)
		fin->fin_nattag = &np->in_tag;
	return 1;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_proto                                                   */
/* Returns:     u_short* - pointer to transport header checksum to update,  */
/*                         NULL if the transport protocol is not recognised */
/*                         as needing a checksum update.                    */
/* Parameters:  fin(I)    - pointer to packet information                   */
/*              nat(I)    - pointer to NAT structure                        */
/*              nflags(I) - NAT flags set for this packet                   */
/*                                                                          */
/* Return the pointer to the checksum field for each protocol so understood.*/
/* If support for making other changes to a protocol header is required,    */
/* that is not strictly 'address' translation, such as clamping the MSS in  */
/* TCP down to a specific value, then do it from here.                      */
/* ------------------------------------------------------------------------ */
u_short *nat_proto(fin, nat, nflags)
fr_info_t *fin;
nat_t *nat;
u_int nflags;
{
	icmphdr_t *icmp;
	u_short *csump;
	tcphdr_t *tcp;
	udphdr_t *udp;

	csump = NULL;
	if (fin->fin_out == 0) {
		fin->fin_rev = (nat->nat_dir == NAT_OUTBOUND);
	} else {
		fin->fin_rev = (nat->nat_dir == NAT_INBOUND);
	}

	switch (fin->fin_p)
	{
	case IPPROTO_TCP :
		tcp = fin->fin_dp;

		csump = &tcp->th_sum;

		/*
		 * Do a MSS CLAMPING on a SYN packet,
		 * only deal IPv4 for now.
		 */
		if ((nat->nat_mssclamp != 0) && (tcp->th_flags & TH_SYN) != 0)
			nat_mssclamp(tcp, nat->nat_mssclamp, csump);

		break;

	case IPPROTO_UDP :
		udp = fin->fin_dp;

		if (udp->uh_sum)
			csump = &udp->uh_sum;
		break;

	case IPPROTO_ICMP :
		icmp = fin->fin_dp;

		if ((nflags & IPN_ICMPQUERY) != 0) {
			if (icmp->icmp_cksum != 0)
				csump = &icmp->icmp_cksum;
		}
		break;
	}
	return csump;
}


/* ------------------------------------------------------------------------ */
/* Function:    fr_natunload                                                */
/* Returns:     Nil                                                         */
/* Parameters:  Nil                                                         */
/*                                                                          */
/* Free all memory used by NAT structures allocated at runtime.             */
/* ------------------------------------------------------------------------ */
void fr_natunload(ifs)
ipf_stack_t *ifs;
{
	ipftq_t *ifq, *ifqnext;

	(void) nat_clearlist(ifs);
	(void) nat_flushtable(ifs);

	/*
	 * Proxy timeout queues are not cleaned here because although they
	 * exist on the NAT list, appr_unload is called after fr_natunload
	 * and the proxies actually are responsible for them being created.
	 * Should the proxy timeouts have their own list?  There's no real
	 * justification as this is the only complication.
	 */
	for (ifq = ifs->ifs_nat_utqe; ifq != NULL; ifq = ifqnext) {
		ifqnext = ifq->ifq_next;
		if (((ifq->ifq_flags & IFQF_PROXY) == 0) &&
		    (fr_deletetimeoutqueue(ifq) == 0))
			fr_freetimeoutqueue(ifq, ifs);
	}

	if (ifs->ifs_nat_table[0] != NULL) {
		KFREES(ifs->ifs_nat_table[0],
		       sizeof(nat_t *) * ifs->ifs_ipf_nattable_sz);
		ifs->ifs_nat_table[0] = NULL;
	}
	if (ifs->ifs_nat_table[1] != NULL) {
		KFREES(ifs->ifs_nat_table[1],
		       sizeof(nat_t *) * ifs->ifs_ipf_nattable_sz);
		ifs->ifs_nat_table[1] = NULL;
	}
	if (ifs->ifs_nat_rules != NULL) {
		KFREES(ifs->ifs_nat_rules, 
		       sizeof(ipnat_t *) * ifs->ifs_ipf_natrules_sz);
		ifs->ifs_nat_rules = NULL;
	}
	if (ifs->ifs_rdr_rules != NULL) {
		KFREES(ifs->ifs_rdr_rules,
		       sizeof(ipnat_t *) * ifs->ifs_ipf_rdrrules_sz);
		ifs->ifs_rdr_rules = NULL;
	}
	if (ifs->ifs_maptable != NULL) {
		KFREES(ifs->ifs_maptable, 
		       sizeof(hostmap_t *) * ifs->ifs_ipf_hostmap_sz);
		ifs->ifs_maptable = NULL;
	}
	if (ifs->ifs_nat_stats.ns_bucketlen[0] != NULL) {
		KFREES(ifs->ifs_nat_stats.ns_bucketlen[0],
		       sizeof(u_long *) * ifs->ifs_ipf_nattable_sz);
		ifs->ifs_nat_stats.ns_bucketlen[0] = NULL;
	}
	if (ifs->ifs_nat_stats.ns_bucketlen[1] != NULL) {
		KFREES(ifs->ifs_nat_stats.ns_bucketlen[1],
		       sizeof(u_long *) * ifs->ifs_ipf_nattable_sz);
		ifs->ifs_nat_stats.ns_bucketlen[1] = NULL;
	}

	if (ifs->ifs_fr_nat_maxbucket_reset == 1)
		ifs->ifs_fr_nat_maxbucket = 0;

	if (ifs->ifs_fr_nat_init == 1) {
		ifs->ifs_fr_nat_init = 0;
		fr_sttab_destroy(ifs->ifs_nat_tqb);

		RW_DESTROY(&ifs->ifs_ipf_natfrag);
		RW_DESTROY(&ifs->ifs_ipf_nat);

		MUTEX_DESTROY(&ifs->ifs_ipf_nat_new);
		MUTEX_DESTROY(&ifs->ifs_ipf_natio);

		MUTEX_DESTROY(&ifs->ifs_nat_udptq.ifq_lock);
		MUTEX_DESTROY(&ifs->ifs_nat_icmptq.ifq_lock);
		MUTEX_DESTROY(&ifs->ifs_nat_iptq.ifq_lock);
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    fr_natexpire                                                */
/* Returns:     Nil                                                         */
/* Parameters:  Nil                                                         */
/*                                                                          */
/* Check all of the timeout queues for entries at the top which need to be  */
/* expired.                                                                 */
/* ------------------------------------------------------------------------ */
void fr_natexpire(ifs)
ipf_stack_t *ifs;
{
	ipftq_t *ifq, *ifqnext;
	ipftqent_t *tqe, *tqn;
	int i;
	SPL_INT(s);

	SPL_NET(s);
	WRITE_ENTER(&ifs->ifs_ipf_nat);
	for (ifq = ifs->ifs_nat_tqb, i = 0; ifq != NULL; ifq = ifq->ifq_next) {
		for (tqn = ifq->ifq_head; ((tqe = tqn) != NULL); i++) {
			if (tqe->tqe_die > ifs->ifs_fr_ticks)
				break;
			tqn = tqe->tqe_next;
			nat_delete(tqe->tqe_parent, NL_EXPIRE, ifs);
		}
	}

	for (ifq = ifs->ifs_nat_utqe; ifq != NULL; ifq = ifqnext) {
		ifqnext = ifq->ifq_next;

		for (tqn = ifq->ifq_head; ((tqe = tqn) != NULL); i++) {
			if (tqe->tqe_die > ifs->ifs_fr_ticks)
				break;
			tqn = tqe->tqe_next;
			nat_delete(tqe->tqe_parent, NL_EXPIRE, ifs);
		}
	}

	for (ifq = ifs->ifs_nat_utqe; ifq != NULL; ifq = ifqnext) {
		ifqnext = ifq->ifq_next;

		if (((ifq->ifq_flags & IFQF_DELETE) != 0) &&
		    (ifq->ifq_ref == 0)) {
			fr_freetimeoutqueue(ifq, ifs);
		}
	}

	RWLOCK_EXIT(&ifs->ifs_ipf_nat);
	SPL_X(s);
}


/* ------------------------------------------------------------------------ */
/* Function:    fr_nataddrsync                                              */
/* Returns:     Nil                                                         */
/* Parameters:  ifp(I) -  pointer to network interface                      */
/*              addr(I) - pointer to new network address                    */
/*                                                                          */
/* Walk through all of the currently active NAT sessions, looking for those */
/* which need to have their translated address updated (where the interface */
/* matches the one passed in) and change it, recalculating the checksum sum */
/* difference too.                                                          */
/* ------------------------------------------------------------------------ */
void fr_nataddrsync(ifp, addr, ifs)
void *ifp;
struct in_addr *addr;
ipf_stack_t *ifs;
{
	u_32_t sum1, sum2, sumd;
	nat_t *nat;
	ipnat_t *np;
	SPL_INT(s);

	if (ifs->ifs_fr_running <= 0)
		return;

	SPL_NET(s);
	WRITE_ENTER(&ifs->ifs_ipf_nat);

	if (ifs->ifs_fr_running <= 0) {
		RWLOCK_EXIT(&ifs->ifs_ipf_nat);
		return;
	}

	/*
	 * Change IP addresses for NAT sessions for any protocol except TCP
	 * since it will break the TCP connection anyway.  The only rules
	 * which will get changed are those which are "map ... -> 0/32",
	 * where the rule specifies the address is taken from the interface.
	 */
	for (nat = ifs->ifs_nat_instances; nat; nat = nat->nat_next) {
		if (addr != NULL) {
			if (((ifp != NULL) && ifp != (nat->nat_ifps[0])) ||
			    ((nat->nat_flags & IPN_TCP) != 0))
				continue;
			if (((np = nat->nat_ptr) == NULL) ||
			    (np->in_nip || (np->in_outmsk != 0xffffffff)))
				continue;

			/*
			 * Change the map-to address to be the same as the
			 * new one.
			 */
			sum1 = nat->nat_outip.s_addr;
			nat->nat_outip = *addr;
			sum2 = nat->nat_outip.s_addr;

		} else if (((ifp == NULL) || (ifp == nat->nat_ifps[0])) &&
		    !(nat->nat_flags & IPN_TCP) && (np = nat->nat_ptr) &&
		    (np->in_outmsk == 0xffffffff) && !np->in_nip) {
			struct in_addr in;

			/*
			 * Change the map-to address to be the same as the
			 * new one.
			 */
			sum1 = nat->nat_outip.s_addr;
			if (fr_ifpaddr(4, FRI_NORMAL, nat->nat_ifps[0],
				       &in, NULL, ifs) != -1)
				nat->nat_outip = in;
			sum2 = nat->nat_outip.s_addr;
		} else {
			continue;
		}

		if (sum1 == sum2)
			continue;
		/*
		 * Readjust the checksum adjustment to take into
		 * account the new IP#.
		 */
		CALC_SUMD(sum1, sum2, sumd);
		/* XXX - dont change for TCP when solaris does
		 * hardware checksumming.
		 */
		sumd += nat->nat_sumd[0];
		nat->nat_sumd[0] = (sumd & 0xffff) + (sumd >> 16);
		nat->nat_sumd[1] = nat->nat_sumd[0];
	}

	RWLOCK_EXIT(&ifs->ifs_ipf_nat);
	SPL_X(s);
}


/* ------------------------------------------------------------------------ */
/* Function:    fr_natifpsync                                               */
/* Returns:     Nil                                                         */
/* Parameters:  action(I) - how we are syncing                              */
/*              ifp(I)    - pointer to network interface                    */
/*              name(I)   - name of interface to sync to                    */
/*                                                                          */
/* This function is used to resync the mapping of interface names and their */
/* respective 'pointers'.  For "action == IPFSYNC_RESYNC", resync all       */
/* interfaces by doing a new lookup of name to 'pointer'.  For "action ==   */
/* IPFSYNC_NEWIFP", treat ifp as the new pointer value associated with      */
/* "name" and for "action == IPFSYNC_OLDIFP", ifp is a pointer for which    */
/* there is no longer any interface associated with it.                     */
/* ------------------------------------------------------------------------ */
void fr_natifpsync(action, ifp, name, ifs)
int action;
void *ifp;
char *name;
ipf_stack_t *ifs;
{
#if defined(_KERNEL) && !defined(MENTAT) && defined(USE_SPL)
	int s;
#endif
	nat_t *nat;
	ipnat_t *n;

	if (ifs->ifs_fr_running <= 0)
		return;

	SPL_NET(s);
	WRITE_ENTER(&ifs->ifs_ipf_nat);

	if (ifs->ifs_fr_running <= 0) {
		RWLOCK_EXIT(&ifs->ifs_ipf_nat);
		return;
	}

	switch (action)
	{
	case IPFSYNC_RESYNC :
		for (nat = ifs->ifs_nat_instances; nat; nat = nat->nat_next) {
			if ((ifp == nat->nat_ifps[0]) ||
			    (nat->nat_ifps[0] == (void *)-1)) {
				nat->nat_ifps[0] =
				    fr_resolvenic(nat->nat_ifnames[0], 4, ifs);
			}

			if ((ifp == nat->nat_ifps[1]) ||
			    (nat->nat_ifps[1] == (void *)-1)) {
				nat->nat_ifps[1] =
				    fr_resolvenic(nat->nat_ifnames[1], 4, ifs);
			}
		}

		for (n = ifs->ifs_nat_list; (n != NULL); n = n->in_next) {
			if (n->in_ifps[0] == ifp ||
			    n->in_ifps[0] == (void *)-1) {
				n->in_ifps[0] =
				    fr_resolvenic(n->in_ifnames[0], 4, ifs);
			}
			if (n->in_ifps[1] == ifp ||
			    n->in_ifps[1] == (void *)-1) {
				n->in_ifps[1] =
				    fr_resolvenic(n->in_ifnames[1], 4, ifs);
			}
		}
		break;
	case IPFSYNC_NEWIFP :
		for (nat = ifs->ifs_nat_instances; nat; nat = nat->nat_next) {
			if (!strncmp(name, nat->nat_ifnames[0],
				     sizeof(nat->nat_ifnames[0])))
				nat->nat_ifps[0] = ifp;
			if (!strncmp(name, nat->nat_ifnames[1],
				     sizeof(nat->nat_ifnames[1])))
				nat->nat_ifps[1] = ifp;
		}
		for (n = ifs->ifs_nat_list; (n != NULL); n = n->in_next) {
			if (!strncmp(name, n->in_ifnames[0],
				     sizeof(n->in_ifnames[0])))
				n->in_ifps[0] = ifp;
			if (!strncmp(name, n->in_ifnames[1],
				     sizeof(n->in_ifnames[1])))
				n->in_ifps[1] = ifp;
		}
		break;
	case IPFSYNC_OLDIFP :
		for (nat = ifs->ifs_nat_instances; nat; nat = nat->nat_next) {
			if (ifp == nat->nat_ifps[0])
				nat->nat_ifps[0] = (void *)-1;
			if (ifp == nat->nat_ifps[1])
				nat->nat_ifps[1] = (void *)-1;
		}
		for (n = ifs->ifs_nat_list; (n != NULL); n = n->in_next) {
			if (n->in_ifps[0] == ifp)
				n->in_ifps[0] = (void *)-1;
			if (n->in_ifps[1] == ifp)
				n->in_ifps[1] = (void *)-1;
		}
		break;
	}
	RWLOCK_EXIT(&ifs->ifs_ipf_nat);
	SPL_X(s);
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_icmpquerytype4                                          */
/* Returns:     int - 1 == success, 0 == failure                            */
/* Parameters:  icmptype(I) - ICMP type number                              */
/*                                                                          */
/* Tests to see if the ICMP type number passed is a query/response type or  */
/* not.                                                                     */
/* ------------------------------------------------------------------------ */
static INLINE int nat_icmpquerytype4(icmptype)
int icmptype;
{

	/*
	 * For the ICMP query NAT code, it is essential that both the query
	 * and the reply match on the NAT rule. Because the NAT structure
	 * does not keep track of the icmptype, and a single NAT structure
	 * is used for all icmp types with the same src, dest and id, we
	 * simply define the replies as queries as well. The funny thing is,
	 * altough it seems silly to call a reply a query, this is exactly
	 * as it is defined in the IPv4 specification
	 */
	
	switch (icmptype)
	{
	
	case ICMP_ECHOREPLY:
	case ICMP_ECHO:
	/* route aedvertisement/solliciation is currently unsupported: */
	/* it would require rewriting the ICMP data section            */
	case ICMP_TSTAMP:
	case ICMP_TSTAMPREPLY:
	case ICMP_IREQ:
	case ICMP_IREQREPLY:
	case ICMP_MASKREQ:
	case ICMP_MASKREPLY:
		return 1;
	default:
		return 0;
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_log                                                     */
/* Returns:     Nil                                                         */
/* Parameters:  nat(I)  - pointer to NAT structure                          */
/*              type(I) - type of log entry to create                       */
/*                                                                          */
/* Creates a NAT log entry.                                                 */
/* ------------------------------------------------------------------------ */
void nat_log(nat, type, ifs)
struct nat *nat;
u_int type;
ipf_stack_t *ifs;
{
#ifdef	IPFILTER_LOG
# ifndef LARGE_NAT
	struct ipnat *np;
	int rulen;
# endif
	struct natlog natl;
	void *items[1];
	size_t sizes[1];
	int types[1];

	natl.nl_inip = nat->nat_inip;
	natl.nl_outip = nat->nat_outip;
	natl.nl_origip = nat->nat_oip;
	natl.nl_bytes[0] = nat->nat_bytes[0];
	natl.nl_bytes[1] = nat->nat_bytes[1];
	natl.nl_pkts[0] = nat->nat_pkts[0];
	natl.nl_pkts[1] = nat->nat_pkts[1];
	natl.nl_origport = nat->nat_oport;
	natl.nl_inport = nat->nat_inport;
	natl.nl_outport = nat->nat_outport;
	natl.nl_p = nat->nat_p;
	natl.nl_type = type;
	natl.nl_rule = -1;
# ifndef LARGE_NAT
	if (nat->nat_ptr != NULL) {
		for (rulen = 0, np = ifs->ifs_nat_list; np;
		     np = np->in_next, rulen++)
			if (np == nat->nat_ptr) {
				natl.nl_rule = rulen;
				break;
			}
	}
# endif
	items[0] = &natl;
	sizes[0] = sizeof(natl);
	types[0] = 0;

	(void) ipllog(IPL_LOGNAT, NULL, items, sizes, types, 1, ifs);
#endif
}


#if defined(__OpenBSD__)
/* ------------------------------------------------------------------------ */
/* Function:    nat_ifdetach                                                */
/* Returns:     Nil                                                         */
/* Parameters:  ifp(I) - pointer to network interface                       */
/*                                                                          */
/* Compatibility interface for OpenBSD to trigger the correct updating of   */
/* interface references within IPFilter.                                    */
/* ------------------------------------------------------------------------ */
void nat_ifdetach(ifp, ifs)
void *ifp;
ipf_stack_t *ifs;
{
	frsync(ifp, ifs);
	return;
}
#endif


/* ------------------------------------------------------------------------ */
/* Function:    fr_ipnatderef                                               */
/* Returns:     Nil                                                         */
/* Parameters:  isp(I) - pointer to pointer to NAT rule                     */
/* Write Locks: ipf_nat                                                     */
/*                                                                          */
/* ------------------------------------------------------------------------ */
void fr_ipnatderef(inp, ifs)
ipnat_t **inp;
ipf_stack_t *ifs;
{
	ipnat_t *in;

	in = *inp;
	*inp = NULL;
	in->in_space++;
	in->in_use--;
	if (in->in_use == 0 && (in->in_flags & IPN_DELETE)) {
		if (in->in_apr)
			appr_free(in->in_apr);
		KFREE(in);
		ifs->ifs_nat_stats.ns_rules--;
#ifdef notdef
#if SOLARIS
		if (ifs->ifs_nat_stats.ns_rules == 0)
			ifs->ifs_pfil_delayed_copy = 1;
#endif
#endif
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    fr_natderef                                                 */
/* Returns:     Nil                                                         */
/* Parameters:  isp(I) - pointer to pointer to NAT table entry              */
/*                                                                          */
/* Decrement the reference counter for this NAT table entry and free it if  */
/* there are no more things using it.                                       */
/* ------------------------------------------------------------------------ */
void fr_natderef(natp, ifs)
nat_t **natp;
ipf_stack_t *ifs;
{
	nat_t *nat;

	nat = *natp;
	*natp = NULL;
	WRITE_ENTER(&ifs->ifs_ipf_nat);
	nat->nat_ref--;
	if (nat->nat_ref == 0)
	    nat_delete(nat, NL_EXPIRE, ifs);
	RWLOCK_EXIT(&ifs->ifs_ipf_nat);
}


/* ------------------------------------------------------------------------ */
/* Function:    fr_natclone                                                 */
/* Returns:     ipstate_t* - NULL == cloning failed,                        */
/*                           else pointer to new state structure            */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              is(I)  - pointer to master state structure                  */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* Create a "duplcate" state table entry from the master.                   */
/* ------------------------------------------------------------------------ */
static nat_t *fr_natclone(fin, nat)
fr_info_t *fin;
nat_t *nat;
{
	frentry_t *fr;
	nat_t *clone;
	ipnat_t *np;
	ipf_stack_t *ifs = fin->fin_ifs;

	KMALLOC(clone, nat_t *);
	if (clone == NULL)
		return NULL;
	bcopy((char *)nat, (char *)clone, sizeof(*clone));

	MUTEX_NUKE(&clone->nat_lock);

	clone->nat_aps = NULL;
	/*
	 * Initialize all these so that nat_delete() doesn't cause a crash.
	 */
	clone->nat_tqe.tqe_pnext = NULL;
	clone->nat_tqe.tqe_next = NULL;
	clone->nat_tqe.tqe_ifq = NULL;
	clone->nat_tqe.tqe_parent = clone;

	clone->nat_flags &= ~SI_CLONE;
	clone->nat_flags |= SI_CLONED;

	if (clone->nat_hm)
		clone->nat_hm->hm_ref++;

	if (nat_insert(clone, fin->fin_rev, ifs) == -1) {
		KFREE(clone);
		return NULL;
	}
	np = clone->nat_ptr;
	if (np != NULL) {
		if (ifs->ifs_nat_logging)
			nat_log(clone, (u_int)np->in_redir, ifs);
		np->in_use++;
	}
	fr = clone->nat_fr;
	if (fr != NULL) {
		MUTEX_ENTER(&fr->fr_lock);
		fr->fr_ref++;
		MUTEX_EXIT(&fr->fr_lock);
	}

	/*
	 * Because the clone is created outside the normal loop of things and
	 * TCP has special needs in terms of state, initialise the timeout
	 * state of the new NAT from here.
	 */
	if (clone->nat_p == IPPROTO_TCP) {
		(void) fr_tcp_age(&clone->nat_tqe, fin, ifs->ifs_nat_tqb,
				  clone->nat_flags);
	}
#ifdef	IPFILTER_SYNC
	clone->nat_sync = ipfsync_new(SMC_NAT, fin, clone);
#endif
	if (ifs->ifs_nat_logging)
		nat_log(clone, NL_CLONE, ifs);
	return clone;
}


/* ------------------------------------------------------------------------ */
/* Function:   nat_wildok                                                   */
/* Returns:    int - 1 == packet's ports match wildcards                    */
/*                   0 == packet's ports don't match wildcards              */
/* Parameters: nat(I)   - NAT entry                                         */
/*             sport(I) - source port                                       */
/*             dport(I) - destination port                                  */
/*             flags(I) - wildcard flags                                    */
/*             dir(I)   - packet direction                                  */
/*                                                                          */
/* Use NAT entry and packet direction to determine which combination of     */
/* wildcard flags should be used.                                           */
/* ------------------------------------------------------------------------ */
static INLINE int nat_wildok(nat, sport, dport, flags, dir)
nat_t *nat;
int sport;
int dport;
int flags;
int dir;
{
	/*
	 * When called by       dir is set to
	 * nat_inlookup         NAT_INBOUND (0)
	 * nat_outlookup        NAT_OUTBOUND (1)
	 *
	 * We simply combine the packet's direction in dir with the original
	 * "intended" direction of that NAT entry in nat->nat_dir to decide
	 * which combination of wildcard flags to allow.
	 */

	switch ((dir << 1) | nat->nat_dir)
	{
	case 3: /* outbound packet / outbound entry */
		if (((nat->nat_inport == sport) ||
		    (flags & SI_W_SPORT)) &&
		    ((nat->nat_oport == dport) ||
		    (flags & SI_W_DPORT)))
			return 1;
		break;
	case 2: /* outbound packet / inbound entry */
		if (((nat->nat_outport == sport) ||
		    (flags & SI_W_DPORT)) &&
		    ((nat->nat_oport == dport) ||
		    (flags & SI_W_SPORT)))
			return 1;
		break;
	case 1: /* inbound packet / outbound entry */
		if (((nat->nat_oport == sport) ||
		    (flags & SI_W_DPORT)) &&
		    ((nat->nat_outport == dport) ||
		    (flags & SI_W_SPORT)))
			return 1;
		break;
	case 0: /* inbound packet / inbound entry */
		if (((nat->nat_oport == sport) ||
		    (flags & SI_W_SPORT)) &&
		    ((nat->nat_outport == dport) ||
		    (flags & SI_W_DPORT)))
			return 1;
		break;
	default:
		break;
	}

	return(0);
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_mssclamp                                                */
/* Returns:     Nil                                                         */
/* Parameters:  tcp(I)    - pointer to TCP header                           */
/*              maxmss(I) - value to clamp the TCP MSS to                   */
/*              csump(I)  - pointer to TCP checksum                         */
/*                                                                          */
/* Check for MSS option and clamp it if necessary.  If found and changed,   */
/* then the TCP header checksum will be updated to reflect the change in    */
/* the MSS.                                                                 */
/* ------------------------------------------------------------------------ */
static void nat_mssclamp(tcp, maxmss, csump)
tcphdr_t *tcp;
u_32_t maxmss;
u_short *csump;
{
	u_char *cp, *ep, opt;
	int hlen, advance;
	u_32_t mss, sumd;

	hlen = TCP_OFF(tcp) << 2;
	if (hlen > sizeof(*tcp)) {
		cp = (u_char *)tcp + sizeof(*tcp);
		ep = (u_char *)tcp + hlen;

		while (cp < ep) {
			opt = cp[0];
			if (opt == TCPOPT_EOL)
				break;
			else if (opt == TCPOPT_NOP) {
				cp++;
				continue;
			}

			if (cp + 1 >= ep)
				break;
			advance = cp[1];
			if ((cp + advance > ep) || (advance <= 0))
				break;
			switch (opt)
			{
			case TCPOPT_MAXSEG:
				if (advance != 4)
					break;
				mss = cp[2] * 256 + cp[3];
				if (mss > maxmss) {
					cp[2] = maxmss / 256;
					cp[3] = maxmss & 0xff;
					CALC_SUMD(mss, maxmss, sumd);
					fix_outcksum(csump, sumd);
				}
				break;
			default:
				/* ignore unknown options */
				break;
			}

			cp += advance;
		}
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    fr_setnatqueue                                              */
/* Returns:     Nil                                                         */
/* Parameters:  nat(I)- pointer to NAT structure                            */
/*              rev(I) - forward(0) or reverse(1) direction                 */
/* Locks:       ipf_nat (read or write)                                     */
/*                                                                          */
/* Put the NAT entry on its default queue entry, using rev as a helped in   */
/* determining which queue it should be placed on.                          */
/* ------------------------------------------------------------------------ */
void fr_setnatqueue(nat, rev, ifs)
nat_t *nat;
int rev;
ipf_stack_t *ifs;
{
	ipftq_t *oifq, *nifq;

	if (nat->nat_ptr != NULL)
		nifq = nat->nat_ptr->in_tqehead[rev];
	else
		nifq = NULL;

	if (nifq == NULL) {
		switch (nat->nat_p)
		{
		case IPPROTO_UDP :
			nifq = &ifs->ifs_nat_udptq;
			break;
		case IPPROTO_ICMP :
			nifq = &ifs->ifs_nat_icmptq;
			break;
		case IPPROTO_TCP :
			nifq = ifs->ifs_nat_tqb + nat->nat_tqe.tqe_state[rev];
			break;
		default :
			nifq = &ifs->ifs_nat_iptq;
			break;
		}
	}

	oifq = nat->nat_tqe.tqe_ifq;
	/*
	 * If it's currently on a timeout queue, move it from one queue to
	 * another, else put it on the end of the newly determined queue.
	 */
	if (oifq != NULL)
		fr_movequeue(&nat->nat_tqe, oifq, nifq, ifs);
	else
		fr_queueappend(&nat->nat_tqe, nifq, nat, ifs);
	return;
}

/* Function:    nat_getnext                                                 */
/* Returns:     int - 0 == ok, else error                                   */
/* Parameters:  t(I)   - pointer to ipftoken structure                      */
/*              itp(I) - pointer to ipfgeniter_t structure                  */
/*                                                                          */
/* Fetch the next nat/ipnat structure pointer from the linked list and      */
/* copy it out to the storage space pointed to by itp_data.  The next item  */
/* in the list to look at is put back in the ipftoken struture.             */
/* If we call ipf_freetoken, the accompanying pointer is set to NULL because*/
/* ipf_freetoken will call a deref function for us and we dont want to call */
/* that twice (second time would be in the second switch statement below.   */
/* ------------------------------------------------------------------------ */
static int nat_getnext(t, itp, ifs)
ipftoken_t *t;
ipfgeniter_t *itp;
ipf_stack_t *ifs;
{
	hostmap_t *hm, *nexthm = NULL, zerohm;
	ipnat_t *ipn, *nextipnat = NULL, zeroipn;
	nat_t *nat, *nextnat = NULL, zeronat;
	int error = 0;

	READ_ENTER(&ifs->ifs_ipf_nat);
	switch (itp->igi_type)
	{
	case IPFGENITER_HOSTMAP :
		hm = t->ipt_data;
		if (hm == NULL) {
			nexthm = ifs->ifs_ipf_hm_maplist;
		} else {
			nexthm = hm->hm_hnext;
		}
		if (nexthm != NULL) {
			if (nexthm->hm_hnext == NULL) {
				t->ipt_alive = 0;
				/* ipf_freetoken(t, ifs);
				hm = NULL; */
			} else {
				/*MUTEX_ENTER(&nexthm->hm_lock);*/
				nexthm->hm_ref++;
				/*MUTEX_EXIT(&nextipnat->hm_lock);*/
			}
				
		} else {
			bzero(&zerohm, sizeof(zerohm));
			nexthm = &zerohm;
		}
		break;

	case IPFGENITER_IPNAT :
		ipn = t->ipt_data;
		if (ipn == NULL) {
			nextipnat = ifs->ifs_nat_list;
		} else {
			nextipnat = ipn->in_next;
		}
		if (nextipnat != NULL) {
			if (nextipnat->in_next == NULL) {
				t->ipt_alive = 0;
				/*ipf_freetoken(t, ifs);
				ipn = NULL;*/
			} else {
				/* MUTEX_ENTER(&nextipnat->in_lock); */
				nextipnat->in_use++;
				/* MUTEX_EXIT(&nextipnat->in_lock); */
			}
		} else {
			bzero(&zeroipn, sizeof(zeroipn));
			nextipnat = &zeroipn;
		}
		break;

	case IPFGENITER_NAT :
		nat = t->ipt_data;
		if (nat == NULL) {
			nextnat = ifs->ifs_nat_instances;
		} else {
			nextnat = nat->nat_next;
		}
		if (nextnat != NULL) {
			if (nextnat->nat_next == NULL) {
				t->ipt_alive = 0;
				/*ipf_freetoken(t, ifs);
				nat = NULL;*/
			} else {
				MUTEX_ENTER(&nextnat->nat_lock);
				nextnat->nat_ref++;
				MUTEX_EXIT(&nextnat->nat_lock);
			}
		} else {
			bzero(&zeronat, sizeof(zeronat));
			nextnat = &zeronat;
		}
		break;
	}

	RWLOCK_EXIT(&ifs->ifs_ipf_nat);

	switch (itp->igi_type)
	{
	case IPFGENITER_HOSTMAP :
		if (hm != NULL) {
			WRITE_ENTER(&ifs->ifs_ipf_nat);
			fr_hostmapderef(&hm);
			RWLOCK_EXIT(&ifs->ifs_ipf_nat);
		}
		t->ipt_data = nexthm;
		error = COPYOUT(nexthm, itp->igi_data, sizeof(*nexthm));
		if (error != 0)
			error = EFAULT;
		break;

	case IPFGENITER_IPNAT :
		if (ipn != NULL)
			fr_ipnatderef(&ipn, ifs);
		t->ipt_data = nextipnat;
		error = COPYOUT(nextipnat, itp->igi_data, sizeof(*nextipnat));
		if (error != 0)
			error = EFAULT;
		break;

	case IPFGENITER_NAT :
		if (nat != NULL)
			fr_natderef(&nat, ifs);
		t->ipt_data = nextnat;
		error = COPYOUT(nextnat, itp->igi_data, sizeof(*nextnat));
		if (error != 0)
			error = EFAULT;
		break;
	}

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_iterator                                                */
/* Returns:     int - 0 == ok, else error                                   */
/* Parameters:  token(I) - pointer to ipftoken structure                    */
/*              itp(I) - pointer to ipfgeniter_t structure                  */
/*                                                                          */
/* This function acts as a handler for the SIOCGENITER ioctls that use a    */
/* generic structure to iterate through a list.  There are three different  */
/* linked lists of NAT related information to go through: NAT rules, active */
/* NAT mappings and the NAT fragment cache.                                 */
/* ------------------------------------------------------------------------ */
static int nat_iterator(token, itp, ifs)
ipftoken_t *token;
ipfgeniter_t *itp;
ipf_stack_t *ifs;
{
	int error;

	if (itp->igi_data == NULL)
		return EFAULT;

	token->ipt_subtype = itp->igi_type;

	switch (itp->igi_type)
	{
	case IPFGENITER_HOSTMAP :
	case IPFGENITER_IPNAT :
	case IPFGENITER_NAT :
		error = nat_getnext(token, itp, ifs);
		break;
	case IPFGENITER_NATFRAG :
		error = fr_nextfrag(token, itp, &ifs->ifs_ipfr_natlist,
				    &ifs->ifs_ipfr_nattail,
				    &ifs->ifs_ipf_natfrag, ifs);
		break;
	default :
		error = EINVAL;
		break;
	}

	return error;
}
