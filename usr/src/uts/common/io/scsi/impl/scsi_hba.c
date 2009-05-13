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

#include <sys/note.h>

/*
 * Generic SCSI Host Bus Adapter interface implementation
 */
#include <sys/scsi/scsi.h>
#include <sys/file.h>
#include <sys/ddi_impldefs.h>
#include <sys/ndi_impldefs.h>
#include <sys/ddi.h>
#include <sys/sunmdi.h>
#include <sys/epm.h>

extern struct scsi_pkt *scsi_init_cache_pkt(struct scsi_address *,
		    struct scsi_pkt *, struct buf *, int, int, int, int,
		    int (*)(caddr_t), caddr_t);
extern void scsi_free_cache_pkt(struct scsi_address *, struct scsi_pkt *);
extern void scsi_cache_dmafree(struct scsi_address *, struct scsi_pkt *);
extern void scsi_sync_cache_pkt(struct scsi_address *, struct scsi_pkt *);

/*
 * Round up all allocations so that we can guarantee
 * long-long alignment.  This is the same alignment
 * provided by kmem_alloc().
 */
#define	ROUNDUP(x)	(((x) + 0x07) & ~0x07)

/* Magic number to track correct allocations in wrappers */
#define	PKT_WRAPPER_MAGIC	0xa110ced	/* alloced correctly */

kmutex_t	scsi_flag_nointr_mutex;
kcondvar_t	scsi_flag_nointr_cv;
kmutex_t	scsi_log_mutex;

/*
 * Prototypes for static functions
 */
static int	scsi_hba_bus_ctl(
			dev_info_t		*self,
			dev_info_t		*child,
			ddi_ctl_enum_t		op,
			void			*arg,
			void			*result);

static int	scsi_hba_map_fault(
			dev_info_t		*self,
			dev_info_t		*child,
			struct hat		*hat,
			struct seg		*seg,
			caddr_t			addr,
			struct devpage		*dp,
			pfn_t			pfn,
			uint_t			prot,
			uint_t			lock);

static int	scsi_hba_get_eventcookie(
			dev_info_t		*self,
			dev_info_t		*child,
			char			*name,
			ddi_eventcookie_t	*eventp);

static int	scsi_hba_add_eventcall(
			dev_info_t		*self,
			dev_info_t		*child,
			ddi_eventcookie_t	event,
			void			(*callback)(
				dev_info_t		*dip,
				ddi_eventcookie_t	event,
				void			*arg,
				void			*bus_impldata),
			void			*arg,
			ddi_callback_id_t	*cb_id);

static int	scsi_hba_remove_eventcall(
			dev_info_t		*self,
			ddi_callback_id_t	id);

static int	scsi_hba_post_event(
			dev_info_t		*self,
			dev_info_t		*child,
			ddi_eventcookie_t	event,
			void			*bus_impldata);

static int	scsi_hba_info(
			dev_info_t		*self,
			ddi_info_cmd_t		infocmd,
			void			*arg,
			void			**result);

static int	scsi_hba_bus_config(
			dev_info_t		*self,
			uint_t			flags,
			ddi_bus_config_op_t	op,
			void			*arg,
			dev_info_t		**childp);

static int	scsi_hba_bus_unconfig(
			dev_info_t		*self,
			uint_t			flags,
			ddi_bus_config_op_t	op,
			void			*arg);

static int	scsi_hba_fm_init_child(
			dev_info_t		*self,
			dev_info_t		*child,
			int			cap,
			ddi_iblock_cookie_t	*ibc);

static int	scsi_hba_bus_power(
			dev_info_t		*self,
			void			*impl_arg,
			pm_bus_power_op_t	op,
			void			*arg,
			void			*result);

/* busops vector for SCSI HBA's. */
static struct bus_ops scsi_hba_busops = {
	BUSO_REV,
	nullbusmap,			/* bus_map */
	NULL,				/* bus_get_intrspec */
	NULL,				/* bus_add_intrspec */
	NULL,				/* bus_remove_intrspec */
	scsi_hba_map_fault,		/* bus_map_fault */
	ddi_dma_map,			/* bus_dma_map */
	ddi_dma_allochdl,		/* bus_dma_allochdl */
	ddi_dma_freehdl,		/* bus_dma_freehdl */
	ddi_dma_bindhdl,		/* bus_dma_bindhdl */
	ddi_dma_unbindhdl,		/* bus_unbindhdl */
	ddi_dma_flush,			/* bus_dma_flush */
	ddi_dma_win,			/* bus_dma_win */
	ddi_dma_mctl,			/* bus_dma_ctl */
	scsi_hba_bus_ctl,		/* bus_ctl */
	ddi_bus_prop_op,		/* bus_prop_op */
	scsi_hba_get_eventcookie,	/* bus_get_eventcookie */
	scsi_hba_add_eventcall,		/* bus_add_eventcall */
	scsi_hba_remove_eventcall,	/* bus_remove_eventcall */
	scsi_hba_post_event,		/* bus_post_event */
	NULL,				/* bus_intr_ctl */
	scsi_hba_bus_config,		/* bus_config */
	scsi_hba_bus_unconfig,		/* bus_unconfig */
	scsi_hba_fm_init_child,		/* bus_fm_init */
	NULL,				/* bus_fm_fini */
	NULL,				/* bus_fm_access_enter */
	NULL,				/* bus_fm_access_exit */
	scsi_hba_bus_power		/* bus_power */
};

/* cb_ops for hotplug :devctl and :scsi support */
static struct cb_ops scsi_hba_cbops = {
	scsi_hba_open,
	scsi_hba_close,
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	nodev,			/* read */
	nodev,			/* write */
	scsi_hba_ioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* prop_op */
	NULL,			/* stream */
	D_NEW|D_MP|D_HOTPLUG,	/* cb_flag */
	CB_REV,			/* rev */
	nodev,			/* int (*cb_aread)() */
	nodev			/* int (*cb_awrite)() */
};

/*
 * SCSI_HBA_LOG is used for all messages. A logging level is specified when
 * generating a message. Some levels correspond directly to cmn_err levels,
 * the others are associated with increasing levels diagnostic/debug output.
 * For _LOG() messages, a __func__ prefix will identify the function origin
 * of the message. For _LOG_NF messages, there is no function prefix or
 * self/child context. Filtering of messages is provided based on logging
 * level, but messages with cmn_err logging level and messages generated
 * generated with _LOG_NF() are never filtered.
 *
 * For debugging, more complete information can be displayed with each message
 * (full device path and pointer values) by adjusting scsi_hba_log_info.
 */
/* logging levels */
#define	SCSI_HBA_LOGCONT	CE_CONT
#define	SCSI_HBA_LOGNOTE	CE_NOTE
#define	SCSI_HBA_LOGWARN	CE_WARN
#define	SCSI_HBA_LOGPANIC	CE_PANIC
#define	SCSI_HBA_LOGIGNORE	CE_IGNORE
#define	SCSI_HBA_LOG_CE_MASK	0x0000000F	/* no filter for these levels */
#define	SCSI_HBA_LOG1		0x00000010	/* DIAG1 level enable */
#define	SCSI_HBA_LOG2		0x00000020	/* DIAG2 level enable */
#define	SCSI_HBA_LOG3		0x00000040	/* DIAG3 level enable */
#define	SCSI_HBA_LOG4		0x00000080	/* DIAG4 level enable */
#define	SCSI_HBA_LOGTRACE	0x00000100	/* TRACE enable */
#if (CE_CONT | CE_NOTE | CE_WARN | CE_PANIC | CE_IGNORE) > SCSI_HBA_LOG_CE_MASK
Error, problem with CE_ definitions
#endif

/*
 * Tunable log message augmentation and filters: filters do not apply to
 * SCSI_HBA_LOG_CE_MASK level messages or LOG_NF() messages.
 *
 * An example set of /etc/system tunings to simplify debug a SCSA pHCI HBA
 * driver called "pmcs", including "scsi_vhci" operation, might be:
 *
 * echo "set scsi:scsi_hba_log_filter_level=0xf0"		>> /etc/system
 * echo "set scsi:scsi_hba_log_filter_phci=\"pmcs\""		>> /etc/system
 * echo "set scsi:scsi_hba_log_filter_vhci=\"scsi_vhci\""	>> /etc/system
 * echo "set scsi:scsi_hba_log_align=1"				>> /etc/system
 * echo "set scsi:scsi_hba_log_fcif=0x21"			>> /etc/system
 * echo "set scsi:scsi_hba_log_mt_disable=0x6"			>> /etc/system
 * echo "set mtc_off=1"						>> /etc/system
 * echo "set mdi_mtc_off=1"					>> /etc/system
 */
int		scsi_hba_log_filter_level =
			SCSI_HBA_LOG1 |
			0;
char		*scsi_hba_log_filter_phci = "\0\0\0\0\0\0\0\0\0\0\0\0";
char		*scsi_hba_log_filter_vhci = "\0\0\0\0\0\0\0\0\0\0\0\0";
int		scsi_hba_log_align = 0;	/* NOTE: will not cause truncation */
int		scsi_hba_log_fcif = '\0';	/* "^!?" first char in format */
						/* ^==0x5e, !==0x21, ?==0x3F */
						/* See cmn_err(9F) */
int		scsi_hba_log_info =	/* augmentation: extra info output */
			(0 << 0) |	/* 0x0001: process information */
			(0 << 1) |	/* 0x0002: full /devices path */
			(0 << 2);	/* 0x0004: devinfo pointer */

int		scsi_hba_log_mt_disable =
			/* SCSI_ENUMERATION_MT_LUN_DISABLE |	(ie 0x02) */
			/* SCSI_ENUMERATION_MT_TARGET_DISABLE |	(ie 0x04) */
			0;

/* static data for HBA logging subsystem */
static kmutex_t	scsi_hba_log_mutex;
static char	scsi_hba_log_i[512];
static char	scsi_hba_log_buf[512];
static char	scsi_hba_fmt[512];

/* Macros to use in scsi_hba.c source code below */
#define	SCSI_HBA_LOG(x)	scsi_hba_log x
#define	_LOG(level)	SCSI_HBA_LOG##level, __func__
#define	_LOG_NF(level)	SCSI_HBA_LOG##level, NULL, NULL, NULL
#define	_LOG_TRACE	_LOG(TRACE)

/*PRINTFLIKE5*/
void
scsi_hba_log(int level, const char *func, dev_info_t *self, dev_info_t *child,
    const char *fmt, ...)
{
	va_list		ap;
	int		clevel;
	int		align;
	char		*info;
	char		*f;
	char		*ua;

	/* derive self from child's parent */
	if ((self == NULL) && child)
		self = ddi_get_parent(child);

	/* no filtering of SCSI_HBA_LOG_CE_MASK or LOG_NF messages */
	if (((level & SCSI_HBA_LOG_CE_MASK) != level) && (func != NULL)) {
		/* scsi_hba_log_filter_level: filter on level as bitmask */
		if ((level & scsi_hba_log_filter_level) == 0)
			return;

		/* scsi_hba_log_filter_phci/vhci: on name of driver */
		if (*scsi_hba_log_filter_phci &&
		    ((self == NULL) ||
		    (ddi_driver_name(self) == NULL) ||
		    strcmp(ddi_driver_name(self), scsi_hba_log_filter_phci))) {
			/* does not match pHCI, check vHCI */
			if (*scsi_hba_log_filter_vhci &&
			    ((self == NULL) ||
			    (ddi_driver_name(self) == NULL) ||
			    strcmp(ddi_driver_name(self),
			    scsi_hba_log_filter_vhci))) {
				/* does not match vHCI */
				return;
			}
		}


		/* passed filters, determine align */
		align = scsi_hba_log_align;

		/* shorten func for filtered output */
		if (strncmp(func, "scsi_hba_", 9) == 0)
			func += 9;
		if (strncmp(func, "scsi_", 5) == 0)
			func += 5;
	} else {
		/* don't align output that is never filtered */
		align = 0;
	}

	/* determine the cmn_err form from the level */
	clevel = ((level & SCSI_HBA_LOG_CE_MASK) == level) ? level : CE_CONT;

	/* protect common buffers used to format output */
	mutex_enter(&scsi_hba_log_mutex);

	/* skip special first characters, we add them back below */
	f = (char *)fmt;
	if (*f && strchr("^!?", *f))
		f++;
	va_start(ap, fmt);
	(void) vsprintf(scsi_hba_log_buf, f, ap);
	va_end(ap);

	/* augment message with 'information' */
	info = scsi_hba_log_i;
	*info = '\0';
	if ((scsi_hba_log_info & 0x0001) && curproc && PTOU(curproc)->u_comm) {
		(void) sprintf(info, "%s[%d]%p ",
		    PTOU(curproc)->u_comm, curproc->p_pid, (void *)curthread);
		info += strlen(info);
	}
	if (self) {
		if ((scsi_hba_log_info & 0x0004) && (child || self)) {
			(void) sprintf(info, "%p ",
			    (void *)(child ? child : self));
			info += strlen(info);
		}
		if (scsi_hba_log_info & 0x0002)	{
			(void) ddi_pathname(child ? child : self, info);
			(void) strcat(info, " ");
			info += strlen(info);
		}

		/* always provide 'default' information about self &child */
		(void) sprintf(info, "%s%d ", ddi_driver_name(self),
		    ddi_get_instance(self));
		info += strlen(info);
		if (child) {
			ua = ddi_get_name_addr(child);
			(void) sprintf(info, "%s@%s ",
			    ddi_node_name(child), (ua && *ua) ? ua : "");
			info += strlen(info);
		}
	}

	/* turn off alignment if truncation would occur */
	if (align && ((strlen(func) > 18) || (strlen(scsi_hba_log_i) > 36)))
		align = 0;

	/* adjust for aligned output */
	if (align) {
		if (func == NULL)
			func = "";
		/* remove trailing blank with align output */
		if ((info != scsi_hba_log_i) && (*(info -1) == '\b'))
			*(info - 1) = '\0';
	}

	/* special "first character in format" must be in format itself */
	f = scsi_hba_fmt;
	if (fmt[0] && strchr("^!?", fmt[0]))
		*f++ = fmt[0];
	else if (scsi_hba_log_fcif)
		*f++ = (char)scsi_hba_log_fcif;		/* add global fcif */
	if (align)
		(void) sprintf(f, "%s", "%-18.18s: %36.36s: %s%s");
	else
		(void) sprintf(f, "%s", func ? "%s: %s%s%s" : "%s%s%s");

	if (func)
		cmn_err(clevel, scsi_hba_fmt, func, scsi_hba_log_i,
		    scsi_hba_log_buf, clevel == CE_CONT ? "\n" : "");
	else
		cmn_err(clevel, scsi_hba_fmt, scsi_hba_log_i,
		    scsi_hba_log_buf, clevel == CE_CONT ? "\n" : "");
	mutex_exit(&scsi_hba_log_mutex);
}

/*
 * Called from _init() when loading "scsi" module
 */
void
scsi_initialize_hba_interface()
{
	SCSI_HBA_LOG((_LOG_TRACE, NULL, NULL, __func__));

	mutex_init(&scsi_log_mutex, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&scsi_flag_nointr_mutex, NULL, MUTEX_DRIVER, NULL);
	cv_init(&scsi_flag_nointr_cv, NULL, CV_DRIVER, NULL);
	mutex_init(&scsi_hba_log_mutex, NULL, MUTEX_DRIVER, NULL);
}

int
scsi_hba_pkt_constructor(void *buf, void *arg, int kmflag)
{
	struct scsi_pkt_cache_wrapper *pktw;
	struct scsi_pkt		*pkt;
	scsi_hba_tran_t		*tran = (scsi_hba_tran_t *)arg;
	int			pkt_len;
	char			*ptr;

	/*
	 * allocate a chunk of memory for the following:
	 * scsi_pkt
	 * pcw_* fields
	 * pkt_ha_private
	 * pkt_cdbp, if needed
	 * (pkt_private always null)
	 * pkt_scbp, if needed
	 */
	pkt_len = tran->tran_hba_len + sizeof (struct scsi_pkt_cache_wrapper);
	if (tran->tran_hba_flags & SCSI_HBA_TRAN_CDB)
		pkt_len += DEFAULT_CDBLEN;
	if (tran->tran_hba_flags & SCSI_HBA_TRAN_SCB)
		pkt_len += DEFAULT_SCBLEN;
	bzero(buf, pkt_len);

	ptr = buf;
	pktw = buf;
	ptr += sizeof (struct scsi_pkt_cache_wrapper);
	pkt = &(pktw->pcw_pkt);
	pkt->pkt_ha_private = (opaque_t)ptr;

	pktw->pcw_magic = PKT_WRAPPER_MAGIC;	/* alloced correctly */
	/*
	 * keep track of the granularity at the time this handle was
	 * allocated
	 */
	pktw->pcw_granular = tran->tran_dma_attr.dma_attr_granular;

	if (ddi_dma_alloc_handle(tran->tran_hba_dip,
	    &tran->tran_dma_attr,
	    kmflag == KM_SLEEP ? SLEEP_FUNC: NULL_FUNC, NULL,
	    &pkt->pkt_handle) != DDI_SUCCESS) {

		return (-1);
	}
	ptr += tran->tran_hba_len;
	if (tran->tran_hba_flags & SCSI_HBA_TRAN_CDB) {
		pkt->pkt_cdbp = (opaque_t)ptr;
		ptr += DEFAULT_CDBLEN;
	}
	pkt->pkt_private = NULL;
	if (tran->tran_hba_flags & SCSI_HBA_TRAN_SCB)
		pkt->pkt_scbp = (opaque_t)ptr;
	if (tran->tran_pkt_constructor)
		return ((*tran->tran_pkt_constructor)(pkt, arg, kmflag));
	else
		return (0);
}

#define	P_TO_TRAN(pkt)	((pkt)->pkt_address.a_hba_tran)

void
scsi_hba_pkt_destructor(void *buf, void *arg)
{
	struct scsi_pkt_cache_wrapper *pktw = buf;
	struct scsi_pkt		*pkt = &(pktw->pcw_pkt);
	scsi_hba_tran_t		*tran = (scsi_hba_tran_t *)arg;

	ASSERT(pktw->pcw_magic == PKT_WRAPPER_MAGIC);
	ASSERT((pktw->pcw_flags & PCW_BOUND) == 0);
	if (tran->tran_pkt_destructor)
		(*tran->tran_pkt_destructor)(pkt, arg);

	/* make sure nobody messed with our pointers */
	ASSERT(pkt->pkt_ha_private == (opaque_t)((char *)pkt +
	    sizeof (struct scsi_pkt_cache_wrapper)));
	ASSERT(((tran->tran_hba_flags & SCSI_HBA_TRAN_SCB) == 0) ||
	    (pkt->pkt_scbp == (opaque_t)((char *)pkt +
	    tran->tran_hba_len +
	    (((tran->tran_hba_flags & SCSI_HBA_TRAN_CDB) == 0) ?
	    0 : DEFAULT_CDBLEN) +
	    DEFAULT_PRIVLEN + sizeof (struct scsi_pkt_cache_wrapper))));
	ASSERT(((tran->tran_hba_flags & SCSI_HBA_TRAN_CDB) == 0) ||
	    (pkt->pkt_cdbp == (opaque_t)((char *)pkt +
	    tran->tran_hba_len +
	    sizeof (struct scsi_pkt_cache_wrapper))));
	ASSERT(pkt->pkt_handle);
	ddi_dma_free_handle(&pkt->pkt_handle);
	pkt->pkt_handle = NULL;
	pkt->pkt_numcookies = 0;
	pktw->pcw_total_xfer = 0;
	pktw->pcw_totalwin = 0;
	pktw->pcw_curwin = 0;
}

/*
 * Called by an HBA from _init() to plumb in common SCSA bus_ops and
 * cb_ops for the HBA's :devctl and :scsi minor nodes.
 */
int
scsi_hba_init(struct modlinkage *modlp)
{
	struct dev_ops *hba_dev_ops;

	SCSI_HBA_LOG((_LOG_TRACE, NULL, NULL, __func__));

	/*
	 * Get a pointer to the dev_ops structure of the HBA and plumb our
	 * bus_ops vector into the HBA's dev_ops structure.
	 */
	hba_dev_ops = ((struct modldrv *)(modlp->ml_linkage[0]))->drv_dev_ops;
	ASSERT(hba_dev_ops->devo_bus_ops == NULL);
	hba_dev_ops->devo_bus_ops = &scsi_hba_busops;

	/*
	 * Plumb our cb_ops vector into the HBA's dev_ops structure to
	 * provide getinfo and hotplugging ioctl support if the HBA driver
	 * does not already provide this support.
	 */
	if (hba_dev_ops->devo_cb_ops == NULL) {
		hba_dev_ops->devo_cb_ops = &scsi_hba_cbops;
	}
	if (hba_dev_ops->devo_cb_ops->cb_open == scsi_hba_open) {
		ASSERT(hba_dev_ops->devo_cb_ops->cb_close == scsi_hba_close);
		hba_dev_ops->devo_getinfo = scsi_hba_info;
	}
	return (0);
}

/*
 * Called by an HBA attach(9E) to allocate a scsi_hba_tran structure. An HBA
 * driver will then initialize the structure and then call
 * scsi_hba_attach_setup.
 */
/*ARGSUSED*/
scsi_hba_tran_t *
scsi_hba_tran_alloc(
	dev_info_t		*self,
	int			flags)
{
	scsi_hba_tran_t		*tran;

	SCSI_HBA_LOG((_LOG_TRACE, self, NULL, __func__));

	/* allocate SCSA flavors for self */
	ndi_flavorv_alloc(self, SCSA_NFLAVORS);

	tran = kmem_zalloc(sizeof (scsi_hba_tran_t),
	    (flags & SCSI_HBA_CANSLEEP) ? KM_SLEEP : KM_NOSLEEP);

	if (tran) {
		tran->tran_interconnect_type = INTERCONNECT_PARALLEL;
		tran->tran_hba_flags |= SCSI_HBA_SCSA_TA;
	}

	return (tran);
}

/*
 * Called by an HBA to free a scsi_hba_tran structure
 */
void
scsi_hba_tran_free(
	scsi_hba_tran_t		*tran)
{
	SCSI_HBA_LOG((_LOG_TRACE, tran->tran_hba_dip, NULL, __func__));

	kmem_free(tran, sizeof (scsi_hba_tran_t));
}

int
scsi_tran_ext_alloc(
	scsi_hba_tran_t		*tran,
	size_t			length,
	int			flags)
{
	void	*tran_ext;
	int	ret = DDI_FAILURE;

	tran_ext = kmem_zalloc(length,
	    (flags & SCSI_HBA_CANSLEEP) ? KM_SLEEP : KM_NOSLEEP);
	if (tran_ext != NULL) {
		tran->tran_extension = tran_ext;
		ret = DDI_SUCCESS;
	}
	return (ret);
}

void
scsi_tran_ext_free(
	scsi_hba_tran_t		*tran,
	size_t			length)
{
	if (tran->tran_extension != NULL) {
		kmem_free(tran->tran_extension, length);
		tran->tran_extension = NULL;
	}
}

/*
 * Return the unit-address of an 'iport' node, or NULL for non-iport node.
 */
char *
scsi_hba_iport_unit_address(dev_info_t *self)
{
	/*
	 * NOTE: Since 'self' could be a SCSA iport node or a SCSA HBA node,
	 * we can't use SCSA flavors: the flavor of a SCSA HBA node is not
	 * established/owned by SCSA, it is established by the nexus that
	 * created the SCSA HBA node (PCI) as a child.
	 *
	 * NOTE: If we want to support a node_name other than "iport" for
	 * an iport node then we can add support for a "scsa-iport-node-name"
	 * property on the SCSA HBA node.  A SCSA HBA driver would set this
	 * property on the SCSA HBA node prior to using the iport API.
	 */
	if (strcmp(ddi_node_name(self), "iport") == 0)
		return (ddi_get_name_addr(self));
	else
		return (NULL);
}

/*
 * Define a SCSI initiator port (bus/channel) for an HBA card that needs to
 * support multiple SCSI ports, but only has a single HBA devinfo node. This
 * function should be called from the HBA's attach(9E) implementation (when
 * processing the HBA devinfo node attach) after the number of SCSI ports on
 * the card is known or DR handler once DR handler detects a new port added.
 * The function returns 0 on failure and 1 on success.
 *
 * The implementation will add the port value into the "scsi-iports" property
 * value maintained on the HBA node as. These properties are used by the generic
 * scsi bus_config implementation to dynamicaly enumerate the specified iport
 * children. The enumeration code will, on demand, create the appropriate
 * iport children with a "scsi-iport" unit address. This node will bind to the
 * same driver as the HBA node itself. This means that an HBA driver that
 * uses iports should expect probe(9E), attach(9E), and detach(9E) calls on
 * the iport children of the HBA.  If configuration for all ports was already
 * done during HBA node attach, the driver should just return DDI_SUCCESS when
 * confronted with an iport node.
 *
 * A maximum of 32 iport ports are supported per HBA devinfo node.
 *
 * A NULL "port" can be used to indicate that the framework should enumerate
 * target children on the HBA node itself, in addition to enumerating target
 * children on any iport nodes declared. There are two reasons that an HBA may
 * wish to have target children enumerated on both the HBA node and iport
 * node(s):
 *
 *   o  If, in the past, HBA hardware had only a single physical port but now
 *      supports multiple physical ports, the updated driver that supports
 *      multiple physical ports may want to avoid /devices path upgrade issues
 *      by enumerating the first physical port under the HBA instead of as a
 *      iport.
 *
 *   o  Some hardware RAID HBA controllers (mlx, chs, etc) support multiple
 *      SCSI physical ports configured so that various physical devices on
 *      the physical ports are amalgamated into virtual devices on a virtual
 *      port.  Amalgamated physical devices no longer appear to the host OS
 *      on the physical ports, but other non-amalgamated devices may still be
 *      visible on the physical ports.  These drivers use a model where the
 *      physical ports are iport nodes and the HBA node is the virtual port to
 *      the configured virtual devices.
 *
 */

int
scsi_hba_iport_register(dev_info_t *self, char *port)
{
	unsigned int ports = 0;
	int rval, i;
	char **iports, **newiports;

	ASSERT(self);
	if (self == NULL)
		return (DDI_FAILURE);

	rval = ddi_prop_lookup_string_array(DDI_DEV_T_ANY, self,
	    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM, "scsi-iports", &iports,
	    &ports);

	if (ports == SCSI_HBA_MAX_IPORTS) {
		ddi_prop_free(iports);
		return (DDI_FAILURE);
	}

	if (rval == DDI_PROP_SUCCESS) {
		for (i = 0; i < ports; i++) {
			if (strcmp(port, iports[i]) == 0) {
				ddi_prop_free(iports);
				return (DDI_SUCCESS);
			}
		}
	}

	newiports = kmem_alloc((sizeof (char *) * (ports + 1)), KM_SLEEP);

	for (i = 0; i < ports; i++) {
		newiports[i] = strdup(iports[i]);
	}
	newiports[ports] = strdup(port);
	ports++;

	rval = 1;

	if (ddi_prop_update_string_array(DDI_DEV_T_NONE, self,
	    "scsi-iports", newiports, ports) != DDI_PROP_SUCCESS) {
		SCSI_HBA_LOG((_LOG(WARN), self, NULL,
		    "Failed to establish scsi-iport %s", port));
		rval = DDI_FAILURE;
	} else {
		rval = DDI_SUCCESS;
	}

	/* If there is iport exist, free property */
	if (ports > 1)
		ddi_prop_free(iports);
	for (i = 0; i < ports; i++) {
		strfree(newiports[i]);
	}
	kmem_free(newiports, (sizeof (char *)) * ports);

	return (rval);
}

/*
 * Check if the HBA is with scsi-iport under it
 */
int
scsi_hba_iport_exist(dev_info_t *self)
{
	unsigned int ports = 0;
	char **iports;
	int rval;

	rval = ddi_prop_lookup_string_array(DDI_DEV_T_ANY, self,
	    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM, "scsi-iports", &iports,
	    &ports);

	if (rval != DDI_PROP_SUCCESS)
		return (0);

	/* If there is now at least 1 iport, then iports is valid */
	if (ports > 0) {
		rval = 1;
	} else
		rval = 0;
	ddi_prop_free(iports);

	return (rval);
}

dev_info_t *
scsi_hba_iport_find(dev_info_t *self, char *portnm)
{
	char		*addr = NULL;
	char		**iports;
	unsigned int	num_iports = 0;
	int		rval = DDI_FAILURE;
	int		i = 0;
	dev_info_t	*child = NULL;

	/* check to see if this is an HBA that defined scsi iports */
	rval = ddi_prop_lookup_string_array(DDI_DEV_T_ANY, self,
	    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM, "scsi-iports", &iports,
	    &num_iports);

	if (rval != DDI_SUCCESS) {
		return (NULL);
	}
	ASSERT(num_iports > 0);

	/* check to see if this port was registered */
	for (i = 0; i < num_iports; i++) {
		if (strcmp(iports[i], portnm) == 0)
			break;
	}

	if (i == num_iports) {
		child = NULL;
		goto out;
	}

	addr = kmem_zalloc(SCSI_MAXNAMELEN, KM_SLEEP);
	(void) sprintf(addr, "iport@%s", portnm);
	rval = ndi_devi_config_one(self, addr, &child, NDI_NO_EVENT);
	kmem_free(addr, SCSI_MAXNAMELEN);

	if (rval != DDI_SUCCESS) {
		child = NULL;
	}
out:
	ddi_prop_free(iports);
	return (child);
}

/*
 * Common nexus teardown code: used by both scsi_hba_detach() on SCSA HBA node
 * and iport_devctl_uninitchild() on a SCSA HBA iport node (and for failure
 * cleanup). Undo scsa_nexus_setup in reverse order.
 */
static void
scsa_nexus_teardown(dev_info_t *self, scsi_hba_tran_t	*tran)
{
	if ((self == NULL) || (tran == NULL))
		return;
	/* Teardown FMA. */
	if (tran->tran_hba_flags & SCSI_HBA_SCSA_FM) {
		ddi_fm_fini(self);
		tran->tran_hba_flags &= SCSI_HBA_SCSA_FM;
	}
}

/*
 * Common nexus setup code: used by both scsi_hba_attach_setup() on SCSA HBA
 * node and iport_devctl_initchild() on a SCSA HBA iport node.
 *
 * This code makes no assumptions about tran use by scsi_device children.
 */
static int
scsa_nexus_setup(dev_info_t *self, scsi_hba_tran_t *tran)
{
	int		capable;
	int		scsa_minor;

	/*
	 * NOTE: SCSA maintains an 'fm-capable' domain, in tran_fm_capable,
	 * that is not dependent (limited by) the capabilities of its parents.
	 * For example a devinfo node in a branch that is not
	 * DDI_FM_EREPORT_CAPABLE may report as capable, via tran_fm_capable,
	 * to its scsi_device children.
	 *
	 * Get 'fm-capable' property from driver.conf, if present. If not
	 * present, default to the scsi_fm_capable global (which has
	 * DDI_FM_EREPORT_CAPABLE set by default).
	 */
	if (tran->tran_fm_capable == DDI_FM_NOT_CAPABLE)
		tran->tran_fm_capable = ddi_prop_get_int(DDI_DEV_T_ANY, self,
		    DDI_PROP_NOTPROM | DDI_PROP_DONTPASS,
		    "fm-capable", scsi_fm_capable);
	/*
	 * If an HBA is *not* doing its own fma support by calling
	 * ddi_fm_init() prior to scsi_hba_attach_setup(), we provide a
	 * minimal common SCSA implementation so that scsi_device children
	 * can generate ereports via scsi_fm_ereport_post().  We use
	 * ddi_fm_capable() to detect an HBA calling ddi_fm_init() prior to
	 * scsi_hba_attach_setup().
	 */
	if (tran->tran_fm_capable &&
	    (ddi_fm_capable(self) == DDI_FM_NOT_CAPABLE)) {
		/*
		 * We are capable of something, pass our capabilities up
		 * the tree, but use a local variable so our parent can't
		 * limit our capabilities (we don't want our parent to
		 * clear DDI_FM_EREPORT_CAPABLE).
		 *
		 * NOTE: iblock cookies are not important because scsi
		 * HBAs always interrupt below LOCK_LEVEL.
		 */
		capable = tran->tran_fm_capable;
		ddi_fm_init(self, &capable, NULL);

		/*
		 * Set SCSI_HBA_SCSA_FM bit to mark us as usiung the
		 * common minimal SCSA fm implementation -  we called
		 * ddi_fm_init(), so we are responsible for calling
		 * ddi_fm_fini() in scsi_hba_detach().
		 * NOTE: if ddi_fm_init fails in any reason, SKIP.
		 */
		if (DEVI(self)->devi_fmhdl)
			tran->tran_hba_flags |= SCSI_HBA_SCSA_FM;
	}

	/* If SCSA responsible for for minor nodes, create :devctl minor. */
	scsa_minor = (ddi_get_driver(self)->devo_cb_ops->cb_open ==
	    scsi_hba_open) ? 1 : 0;
	if (scsa_minor && ((ddi_create_minor_node(self, "devctl", S_IFCHR,
	    INST2DEVCTL(ddi_get_instance(self)), DDI_NT_SCSI_NEXUS, 0) !=
	    DDI_SUCCESS))) {
		SCSI_HBA_LOG((_LOG(WARN), self, NULL,
		    "can't create devctl minor nodes"));
		scsa_nexus_teardown(self, tran);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * Common tran teardown code: used by iport_devctl_uninitchild() on a SCSA HBA
 * iport node and (possibly) by scsi_hba_detach() on SCSA HBA node (and for
 * failure cleanup). Undo scsa_tran_setup in reverse order.
 */
/*ARGSUSED*/
static void
scsa_tran_teardown(dev_info_t *self, scsi_hba_tran_t *tran)
{
	if (tran == NULL)
		return;
	tran->tran_iport_dip = NULL;

	/*
	 * In the future, if PHCI was registered in the SCSA
	 * scsa_tran_teardown is able to unregiter PHCI
	 */
}

static int
scsa_tran_setup(dev_info_t *self, scsi_hba_tran_t *tran)
{
	int			scsa_minor;
	int			id;
	static const char	*interconnect[] = INTERCONNECT_TYPE_ASCII;

	/* If SCSA responsible for for minor nodes, create ":scsi" */
	scsa_minor = (ddi_get_driver(self)->devo_cb_ops->cb_open ==
	    scsi_hba_open) ? 1 : 0;
	if (scsa_minor && (ddi_create_minor_node(self, "scsi", S_IFCHR,
	    INST2SCSI(ddi_get_instance(self)),
	    DDI_NT_SCSI_ATTACHMENT_POINT, 0) != DDI_SUCCESS)) {
		SCSI_HBA_LOG((_LOG(WARN), self, NULL,
		    "can't create scsi minor nodes"));
		scsa_nexus_teardown(self, tran);
		goto fail;
	}

	/*
	 * If the property does not already exist on self then see if we can
	 * pull it from further up the tree and define it on self. If the
	 * property does not exist above (including options.conf) then use the
	 * default value specified (global variable).
	 */
#define	CONFIG_INT_PROP(s, p, dv)	{				\
	if ((ddi_prop_exists(DDI_DEV_T_ANY, s,				\
	    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM, p) == 0) &&		\
	    (ndi_prop_update_int(DDI_DEV_T_NONE, s, p,			\
	    ddi_prop_get_int(DDI_DEV_T_ANY, ddi_get_parent(s),		\
	    DDI_PROP_NOTPROM, p, dv)) != DDI_PROP_SUCCESS))		\
		SCSI_HBA_LOG((_LOG(WARN), NULL, s,	\
		    "can't create property '%s'", p));			\
	}

	/*
	 * Attach scsi configuration property parameters not already defined
	 * via driver.conf to this instance of the HBA using global variable
	 * value.  Pulling things down from above us to use
	 * "DDI_PROP_NOTPROM | DDI_PROP_DONTPASS" for faster access.
	 */
	CONFIG_INT_PROP(self, "scsi-options", scsi_options);
	CONFIG_INT_PROP(self, "scsi-reset-delay", scsi_reset_delay);
	CONFIG_INT_PROP(self, "scsi-tag-age-limit", scsi_tag_age_limit);
	CONFIG_INT_PROP(self, "scsi-watchdog-tick", scsi_watchdog_tick);
	CONFIG_INT_PROP(self, "scsi-selection-timeout", scsi_selection_timeout);

	/*
	 * cache the scsi-initiator-id as an property defined further up
	 * the tree or defined by OBP on the HBA node so can use
	 * "DDI_PROP_NOTPROM | DDI_PROP_DONTPASS" during enumeration.
	 * We perform the same type of operation that an HBA driver would
	 * use to obtain the 'initiator-id' capability.
	 */
	id = ddi_prop_get_int(DDI_DEV_T_ANY, self, 0, "initiator-id", -1);
	if (id == -1)
		id = ddi_prop_get_int(DDI_DEV_T_ANY, self, 0,
		    "scsi-initiator-id", -1);
	if (id != -1)
		CONFIG_INT_PROP(self, "scsi-initiator-id", id);

	/* Establish 'initiator-interconnect-type' */
	if ((tran->tran_hba_flags & SCSI_HBA_SCSA_TA) &&
	    (tran->tran_interconnect_type > 0) &&
	    (tran->tran_interconnect_type < INTERCONNECT_MAX)) {
		if (ndi_prop_update_string(DDI_DEV_T_NONE, self,
		    "initiator-interconnect-type",
		    (char *)interconnect[tran->tran_interconnect_type])
		    != DDI_PROP_SUCCESS) {
			SCSI_HBA_LOG((_LOG(WARN), self, NULL,
			    "failed to establish "
			    "'initiator-interconnect-type'"));
			return (DDI_FAILURE);
		}
	}

	tran->tran_iport_dip = self;
	/*
	 * In the future SCSA v3, PHCI could be registered in the SCSA
	 * here.
	 */
	return (DDI_SUCCESS);
fail:
	scsa_tran_teardown(self, tran);
	return (DDI_FAILURE);
}

/*
 * Obsolete: Called by an HBA to attach an instance of the driver
 * Implement this older interface in terms of the new.
 */
/*ARGSUSED*/
int
scsi_hba_attach(
	dev_info_t		*self,
	ddi_dma_lim_t		*hba_lim,
	scsi_hba_tran_t		*tran,
	int			flags,
	void			*hba_options)
{
	ddi_dma_attr_t		hba_dma_attr;

	bzero(&hba_dma_attr, sizeof (ddi_dma_attr_t));

	hba_dma_attr.dma_attr_burstsizes = hba_lim->dlim_burstsizes;
	hba_dma_attr.dma_attr_minxfer = hba_lim->dlim_minxfer;

	return (scsi_hba_attach_setup(self, &hba_dma_attr, tran, flags));
}

/*
 * Called by an HBA to attach an instance of the driver.
 */
int
scsi_hba_attach_setup(
	dev_info_t		*self,
	ddi_dma_attr_t		*hba_dma_attr,
	scsi_hba_tran_t		*tran,
	int			flags)
{
	SCSI_HBA_LOG((_LOG_TRACE, self, NULL, __func__));

	/* Verify that we are a driver */
	if (ddi_get_driver(self) == NULL)
		return (DDI_FAILURE);

	ASSERT(scsi_hba_iport_unit_address(self) == NULL);
	if (scsi_hba_iport_unit_address(self))
		return (DDI_FAILURE);

	ASSERT(tran);
	if (tran == NULL)
		return (DDI_FAILURE);

	/*
	 * Verify correct scsi_hba_tran_t form:
	 *   o	both or none of tran_get_name/tran_get_addr.
	 */
	if ((tran->tran_get_name == NULL) ^
	    (tran->tran_get_bus_addr == NULL)) {
		return (DDI_FAILURE);
	}

	/*
	 * Save all the important HBA information that must be accessed
	 * later by scsi_hba_bus_ctl(), and scsi_hba_map().
	 */
	tran->tran_hba_dip = self;
	tran->tran_hba_flags &= SCSI_HBA_SCSA_TA;
	tran->tran_hba_flags |= (flags & ~SCSI_HBA_SCSA_TA);

	/* Establish flavor of transport (and ddi_get_driver_private()) */
	ndi_flavorv_set(self, SCSA_FLAVOR_SCSI_DEVICE, tran);

	/*
	 * Note: we only need dma_attr_minxfer and dma_attr_burstsizes
	 * from the DMA attributes. scsi_hba_attach(9f) only
	 * guarantees that these two fields are initialized properly.
	 * If this changes, be sure to revisit the implementation
	 * of scsi_hba_attach(9F).
	 */
	(void) memcpy(&tran->tran_dma_attr, hba_dma_attr,
	    sizeof (ddi_dma_attr_t));

	/* create kmem_cache, if needed */
	if (tran->tran_setup_pkt) {
		char tmp[96];
		int hbalen;
		int cmdlen = 0;
		int statuslen = 0;

		ASSERT(tran->tran_init_pkt == NULL);
		ASSERT(tran->tran_destroy_pkt == NULL);

		tran->tran_init_pkt = scsi_init_cache_pkt;
		tran->tran_destroy_pkt = scsi_free_cache_pkt;
		tran->tran_sync_pkt = scsi_sync_cache_pkt;
		tran->tran_dmafree = scsi_cache_dmafree;

		hbalen = ROUNDUP(tran->tran_hba_len);
		if (flags & SCSI_HBA_TRAN_CDB)
			cmdlen = ROUNDUP(DEFAULT_CDBLEN);
		if (flags & SCSI_HBA_TRAN_SCB)
			statuslen = ROUNDUP(DEFAULT_SCBLEN);

		(void) snprintf(tmp, sizeof (tmp), "pkt_cache_%s_%d",
		    ddi_driver_name(self), ddi_get_instance(self));
		tran->tran_pkt_cache_ptr = kmem_cache_create(tmp,
		    sizeof (struct scsi_pkt_cache_wrapper) +
		    hbalen + cmdlen + statuslen, 8,
		    scsi_hba_pkt_constructor, scsi_hba_pkt_destructor,
		    NULL, tran, NULL, 0);
	}

	/* Perform node setup independent of initiator role */
	if (scsa_nexus_setup(self, tran) != DDI_SUCCESS)
		goto fail;

	/*
	 * The SCSI_HBA_HBA flag is passed to scsi_hba_attach_setup when the
	 * HBA driver knows that *all* children of the SCSA HBA node will be
	 * 'iports'. If the SCSA HBA node can have iport children and also
	 * function as an initiator for xxx_device children then it should
	 * not specify SCSI_HBA_HBA in its scsi_hba_attach_setup call. An
	 * HBA driver that does not manage iports should not set SCSA_HBA_HBA.
	 */
	if (tran->tran_hba_flags & SCSI_HBA_HBA) {
		/*
		 * Set the 'ddi-config-driver-node' property on the nexus
		 * node that notify attach_driver_nodes() to configure all
		 * immediate children so that nodes which bind to the
		 * same driver as parent are able to be added into per-driver
		 * list.
		 */
		if (ndi_prop_create_boolean(DDI_DEV_T_NONE,
		    self, "ddi-config-driver-node") != DDI_PROP_SUCCESS)
			goto fail;
	} else {
		if (scsa_tran_setup(self, tran) != DDI_SUCCESS)
			goto fail;
	}

	return (DDI_SUCCESS);

fail:
	(void) scsi_hba_detach(self);
	return (DDI_FAILURE);
}

/*
 * Called by an HBA to detach an instance of the driver
 */
int
scsi_hba_detach(dev_info_t *self)
{
	scsi_hba_tran_t	*tran;

	SCSI_HBA_LOG((_LOG_TRACE, self, NULL, __func__));

	ASSERT(scsi_hba_iport_unit_address(self) == NULL);
	if (scsi_hba_iport_unit_address(self))
		return (DDI_FAILURE);

	tran = ndi_flavorv_get(self, SCSA_FLAVOR_SCSI_DEVICE);
	ASSERT(tran);
	if (tran == NULL)
		return (DDI_FAILURE);

	ASSERT(tran->tran_open_flag == 0);
	if (tran->tran_open_flag)
		return (DDI_FAILURE);

	if (!(tran->tran_hba_flags & SCSI_HBA_HBA))
		scsa_tran_teardown(self, tran);
	scsa_nexus_teardown(self, tran);

	/*
	 * XXX - scsi_transport.h states that these data fields should not be
	 * referenced by the HBA. However, to be consistent with
	 * scsi_hba_attach(), they are being reset.
	 */
	tran->tran_hba_dip = (dev_info_t *)NULL;
	tran->tran_hba_flags = 0;
	(void) memset(&tran->tran_dma_attr, 0, sizeof (ddi_dma_attr_t));

	if (tran->tran_pkt_cache_ptr != NULL) {
		kmem_cache_destroy(tran->tran_pkt_cache_ptr);
		tran->tran_pkt_cache_ptr = NULL;
	}

	/* Teardown flavor of transport (and ddi_get_driver_private()) */
	ndi_flavorv_set(self, SCSA_FLAVOR_SCSI_DEVICE, NULL);

	return (DDI_SUCCESS);
}

/*
 * Called by an HBA from _fini()
 */
void
scsi_hba_fini(struct modlinkage *modlp)
{
	struct dev_ops *hba_dev_ops;

	SCSI_HBA_LOG((_LOG_TRACE, NULL, NULL, __func__));

	/* Get the devops structure of this module and clear bus_ops vector. */
	hba_dev_ops = ((struct modldrv *)(modlp->ml_linkage[0]))->drv_dev_ops;

	if (hba_dev_ops->devo_cb_ops == &scsi_hba_cbops)
		hba_dev_ops->devo_cb_ops = NULL;

	if (hba_dev_ops->devo_getinfo == scsi_hba_info)
		hba_dev_ops->devo_getinfo = NULL;

	hba_dev_ops->devo_bus_ops = (struct bus_ops *)NULL;
}

/*
 * SAS specific functions
 */
/*ARGSUSED*/
sas_hba_tran_t *
sas_hba_tran_alloc(
	dev_info_t		*self,
	int			flags)
{
	/* allocate SCSA flavors for self */
	ndi_flavorv_alloc(self, SCSA_NFLAVORS);
	return (kmem_zalloc(sizeof (sas_hba_tran_t), KM_SLEEP));
}

void
sas_hba_tran_free(
	sas_hba_tran_t		*tran)
{
	kmem_free(tran, sizeof (sas_hba_tran_t));
}

int
sas_hba_attach_setup(
	dev_info_t		*self,
	sas_hba_tran_t		*tran)
{
	ASSERT(scsi_hba_iport_unit_address(self) == NULL);
	if (scsi_hba_iport_unit_address(self))
		return (DDI_FAILURE);
	/*
	 * The owner of the this devinfo_t was responsible
	 * for informing the framework already about
	 * additional flavors.
	 */
	ndi_flavorv_set(self, SCSA_FLAVOR_SMP, tran);
	return (DDI_SUCCESS);
}

int
sas_hba_detach(dev_info_t *self)
{
	ASSERT(scsi_hba_iport_unit_address(self) == NULL);
	if (scsi_hba_iport_unit_address(self))
		return (DDI_FAILURE);

	ndi_flavorv_set(self, SCSA_FLAVOR_SMP, NULL);
	return (DDI_SUCCESS);
}

/*
 * SMP child flavored functions
 */

static int
smp_busctl_reportdev(dev_info_t *child)
{
	dev_info_t		*self = ddi_get_parent(child);
	char			*smp_wwn;

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, child,
	    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
	    SMP_WWN, &smp_wwn) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	SCSI_HBA_LOG((_LOG_NF(CONT), "?%s%d at %s%d: wwn %s\n",
	    ddi_driver_name(child), ddi_get_instance(child),
	    ddi_driver_name(self), ddi_get_instance(self), smp_wwn));
	ddi_prop_free(smp_wwn);
	return (DDI_SUCCESS);
}

static int
smp_busctl_initchild(dev_info_t *child)
{
	dev_info_t		*self = ddi_get_parent(child);
	sas_hba_tran_t		*tran = ndi_flavorv_get(self, SCSA_FLAVOR_SMP);
	struct smp_device	*smp;
	char			addr[SCSI_MAXNAMELEN];
	dev_info_t		*ndip;
	char			*smp_wwn = NULL;
	uint64_t		wwn;

	ASSERT(tran);
	if (tran == NULL)
		return (DDI_FAILURE);

	smp = kmem_zalloc(sizeof (struct smp_device), KM_SLEEP);
	smp->dip = child;
	smp->smp_addr.a_hba_tran = tran;

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, child,
	    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
	    SMP_WWN, &smp_wwn) != DDI_SUCCESS) {
		goto failure;
	}

	if (scsi_wwnstr_to_wwn(smp_wwn, &wwn)) {
		goto failure;
	}

	bcopy(&wwn, smp->smp_addr.a_wwn, SAS_WWN_BYTE_SIZE);
	(void) snprintf(addr, SCSI_MAXNAMELEN, "w%s", smp_wwn);

	/* Prevent duplicate nodes.  */
	ndip = ndi_devi_find(self, ddi_node_name(child), addr);
	if (ndip && (ndip != child)) {
		goto failure;
	}

	ddi_set_name_addr(child, addr);
	ddi_set_driver_private(child, smp);
	ddi_prop_free(smp_wwn);
	return (DDI_SUCCESS);

failure:
	kmem_free(smp, sizeof (struct smp_device));
	if (smp_wwn) {
		ddi_prop_free(smp_wwn);
	}
	return (DDI_FAILURE);
}

/*ARGSUSED*/
static int
smp_busctl_uninitchild(dev_info_t *child)
{
	struct smp_device	*smp = ddi_get_driver_private(child);

	ASSERT(smp);
	if (smp == NULL)
		return (DDI_FAILURE);
	kmem_free(smp, sizeof (*smp));
	ddi_set_driver_private(child, NULL);
	ddi_set_name_addr(child, NULL);
	return (DDI_SUCCESS);
}

/*
 * Wrapper to scsi_get_name which takes a devinfo argument instead of a
 * scsi_device structure.
 */
static int
scsi_hba_name_child(dev_info_t *child, char *addr, int maxlen)
{
	struct scsi_device	*sd = ddi_get_driver_private(child);

	/* nodes are named by tran_get_name or default "tgt,lun" */
	if (sd && (scsi_get_name(sd, addr, maxlen) == 1))
		return (DDI_SUCCESS);

	return (DDI_FAILURE);
}

static int
scsi_busctl_reportdev(dev_info_t *child)
{
	dev_info_t		*self = ddi_get_parent(child);
	scsi_hba_tran_t		*tran = ddi_get_driver_private(self);
	struct scsi_device	*sd = ddi_get_driver_private(child);
	char			ua[SCSI_MAXNAMELEN];
	char			ba[SCSI_MAXNAMELEN];

	SCSI_HBA_LOG((_LOG_TRACE, NULL, child, __func__));

	ASSERT(tran && sd);
	if ((tran == NULL) || (sd == NULL))
		return (DDI_FAILURE);

	/* get the unit_address and bus_addr information */
	if ((scsi_get_name(sd, ua, sizeof (ua)) == 0) ||
	    (scsi_get_bus_addr(sd, ba, sizeof (ba)) == 0)) {
		SCSI_HBA_LOG((_LOG(WARN), NULL, child, "REPORTDEV failure"));
		return (DDI_FAILURE);
	}

	if (tran->tran_get_name == NULL)
		SCSI_HBA_LOG((_LOG_NF(CONT), "?%s%d at %s%d: %s",
		    ddi_driver_name(child), ddi_get_instance(child),
		    ddi_driver_name(self), ddi_get_instance(self), ba));
	else
		SCSI_HBA_LOG((_LOG_NF(CONT),
		    "?%s%d at %s%d: unit-address %s: %s",
		    ddi_driver_name(child), ddi_get_instance(child),
		    ddi_driver_name(self), ddi_get_instance(self), ua, ba));
	return (DDI_SUCCESS);
}

/*
 * scsi_busctl_initchild is called to initialize the SCSA transport for
 * communication with a particular child scsi target device. Successful
 * initialization requires properties on the node which describe the address
 * of the target device. If the address of the target device can't be
 * determined from properties then DDI_NOT_WELL_FORMED is returned. Nodes that
 * are DDI_NOT_WELL_FORMED are considered an implementation artifact.
 * The child may be one of the following types of devinfo nodes:
 *
 * OBP node:
 *	OBP does not enumerate target devices attached a SCSI bus. These
 *	template/stub/wildcard nodes are a legacy artifact for support of old
 *	driver loading methods. Since they have no properties,
 *	DDI_NOT_WELL_FORMED will be returned.
 *
 * SID node:
 *	The node may be either a:
 *	    o	probe/barrier SID node
 *	    o	a dynamic SID target node
 *	    o	a dynamic SID mscsi node
 *
 * driver.conf node: The situation for this nexus is different than most.
 *	Typically a driver.conf node definition is used to either define a
 *	new child devinfo node or to further decorate (via merge) a SID
 *	child with properties. In our case we use the nodes for *both*
 *	purposes.
 *
 * In both the SID node and driver.conf node cases we must form the nodes
 * "@addr" from the well-known scsi(9P) device unit-address properties on
 * the node.
 *
 * For HBA drivers that implement the deprecated tran_get_name interface,
 * "@addr" construction involves having that driver interpret properties via
 * scsi_hba_name_child -> scsi_get_name -> tran_get_name: there is no
 * requiremnt for the property names to be well-known.
 */
static int
scsi_busctl_initchild(dev_info_t *child)
{
	dev_info_t		*self = ddi_get_parent(child);
	dev_info_t		*dup;
	scsi_hba_tran_t		*tran;
	struct scsi_device	*sd;
	scsi_hba_tran_t		*tran_clone;
	int			tgt = 0;
	int			lun = 0;
	int			sfunc = 0;
	int			err = DDI_FAILURE;
	char			addr[SCSI_MAXNAMELEN];

	ASSERT(DEVI_BUSY_OWNED(self));
	SCSI_HBA_LOG((_LOG(4), NULL, child, "init begin"));

	/*
	 * For a driver like fp with multiple upper-layer-protocols
	 * it is possible for scsi_hba_init in _init to plumb SCSA
	 * and have the load of fcp (which does scsi_hba_attach_setup)
	 * to fail.  In this case we may get here with a NULL hba.
	 */
	tran = ddi_get_driver_private(self);
	if (tran == NULL)
		return (DDI_NOT_WELL_FORMED);

	/*
	 * OBP may create template/stub/wildcard nodes for legacy driver
	 * loading methods. These nodes have no properties, so we lack the
	 * addressing properties to initchild them. Hide the node and return
	 * DDI_NOT_WELL_FORMED.
	 *
	 * XXX need ndi_devi_has_properties(dip) type interface?
	 *
	 * XXX It would be nice if we could delete these ill formed nodes by
	 * implementing a DDI_NOT_WELL_FORMED_DELETE return code. This can't
	 * be done until leadville debug code removes its dependencies
	 * on the devinfo still being present after a failed ndi_devi_online.
	 */
	if ((DEVI(child)->devi_hw_prop_ptr == NULL) &&
	    (DEVI(child)->devi_drv_prop_ptr == NULL) &&
	    (DEVI(child)->devi_sys_prop_ptr == NULL)) {
		SCSI_HBA_LOG((_LOG(4), NULL, child,
		    "init failed: no properties"));
		return (DDI_NOT_WELL_FORMED);
	}

	/* get legacy SPI addressing properties */
	sfunc = ddi_prop_get_int(DDI_DEV_T_ANY, child,
	    DDI_PROP_NOTPROM | DDI_PROP_DONTPASS, SCSI_ADDR_PROP_SFUNC, -1);
	lun = ddi_prop_get_int(DDI_DEV_T_ANY, child,
	    DDI_PROP_NOTPROM | DDI_PROP_DONTPASS, SCSI_ADDR_PROP_LUN, 0);
	if ((tgt = ddi_prop_get_int(DDI_DEV_T_ANY, child,
	    DDI_PROP_NOTPROM | DDI_PROP_DONTPASS,
	    SCSI_ADDR_PROP_TARGET, -1)) == -1) {
		tgt = 0;
		/*
		 * A driver.conf node for merging always has a target= property,
		 * even if it is just a dummy that does not contain the real
		 * target address. However drivers that register devids may
		 * create stub driver.conf nodes without a target= property so
		 * that pathological devid resolution works.
		 */
		if (ndi_dev_is_persistent_node(child) == 0) {
			SCSI_HBA_LOG((_LOG(4), NULL, child,
			    "init failed: stub .conf node"));
			return (DDI_NOT_WELL_FORMED);
		}
	}

	/*
	 * The scsi_address structure may not specify all the addressing
	 * information. For an old HBA that doesn't support tran_get_name
	 * (most pre-SCSI-3 HBAs) the scsi_address structure is still used,
	 * so the target property must exist and the LUN must be < 256.
	 */
	if ((tran->tran_get_name == NULL) &&
	    ((tgt >= USHRT_MAX) || (lun >= 256))) {
		SCSI_HBA_LOG((_LOG(1), NULL, child,
		    "init failed: illegal/missing properties"));
		return (DDI_NOT_WELL_FORMED);
	}

	/*
	 * We need to initialize a fair amount of our environment to invoke
	 * tran_get_name (via scsi_hba_name_child and scsi_get_name) to
	 * produce the "@addr" name from addressing properties. Allocate and
	 * initialize scsi device structure.
	 */
	sd = kmem_zalloc(sizeof (struct scsi_device), KM_SLEEP);
	mutex_init(&sd->sd_mutex, NULL, MUTEX_DRIVER, NULL);
	sd->sd_dev = child;
	sd->sd_pathinfo = NULL;
	ddi_set_driver_private(child, sd);

	if (tran->tran_hba_flags & SCSI_HBA_ADDR_COMPLEX) {
		/*
		 * For a SCSI_HBA_ADDR_COMPLEX transport we store a pointer to
		 * scsi_device in the scsi_address structure.  This allows an
		 * HBA driver to find its per-scsi_device private data
		 * (accessable to the HBA given just the scsi_address by using
		 *  scsi_address_device(9F)/scsi_device_hba_private_get(9F)).
		 */
		sd->sd_address.a.a_sd = sd;
		tran_clone = NULL;
	} else {
		/*
		 * Initialize the scsi_address so that a SCSI-2 target driver
		 * talking to a SCSI-2 device on a SCSI-3 bus (spi) continues
		 * to work. We skew the secondary function value so that we
		 * can tell from the address structure if we are processing
		 * a secondary function request.
		 */
		sd->sd_address.a_target = (ushort_t)tgt;
		sd->sd_address.a_lun = (uchar_t)lun;
		if (sfunc == -1)
			sd->sd_address.a_sublun = (uchar_t)0;
		else
			sd->sd_address.a_sublun = (uchar_t)sfunc + 1;

		/*
		 * XXX TODO: apply target/lun limitation logic for SPI
		 * binding_set. If spi this is based on scsi_options WIDE
		 * NLUNS some forms of lun limitation are based on the
		 * device @lun 0
		 */

		/*
		 * Deprecated: Use SCSI_HBA_ADDR_COMPLEX:
		 *   Clone transport structure if requested. Cloning allows
		 *   an HBA to maintain target-specific information if
		 *   necessary, such as target addressing information that
		 *   does not adhere to the scsi_address structure format.
		 */
		if (tran->tran_hba_flags & SCSI_HBA_TRAN_CLONE) {
			tran_clone = kmem_alloc(
			    sizeof (scsi_hba_tran_t), KM_SLEEP);
			bcopy((caddr_t)tran,
			    (caddr_t)tran_clone, sizeof (scsi_hba_tran_t));
			tran = tran_clone;
			tran->tran_sd = sd;
		} else {
			tran_clone = NULL;
			ASSERT(tran->tran_sd == NULL);
		}
	}

	/* establish scsi_address pointer to the HBA's tran structure */
	sd->sd_address.a_hba_tran = tran;

	/*
	 * This is a grotty hack that allows direct-access (non-scsa) drivers
	 * (like chs, ata, and mlx which all make cmdk children) to put its
	 * own vector in the 'a_hba_tran' field. When all the drivers that do
	 * this are fixed, please remove this hack.
	 *
	 * NOTE: This hack is also shows up in the DEVP_TO_TRAN implementation
	 * in scsi_confsubr.c.
	 */
	sd->sd_tran_safe = tran;

	/* Establish the @addr name of the child. */
	*addr = '\0';
	if (scsi_hba_name_child(child, addr, SCSI_MAXNAMELEN) != DDI_SUCCESS) {
		/*
		 * Some driver.conf files add bogus target properties (relative
		 * to their nexus representation of target) to their stub
		 * nodes, causing the check above to not filter them.
		 */
		SCSI_HBA_LOG((_LOG(3), NULL, child,
		    "init failed: scsi_busctl_ua call"));
		err = DDI_NOT_WELL_FORMED;
		goto failure;
	}
	if (*addr == '\0') {
		SCSI_HBA_LOG((_LOG(2), NULL, child, "init failed: ua"));
		ndi_devi_set_hidden(child);
		err = DDI_NOT_WELL_FORMED;
		goto failure;
	}

	/* set the node @addr string */
	ddi_set_name_addr(child, addr);

	/* prevent sibling duplicates */
	dup = ndi_devi_find(self, ddi_node_name(child), addr);
	if (dup && (dup != child)) {
		SCSI_HBA_LOG((_LOG(4), NULL, child,
		    "init failed: detected duplicate %p", (void *)dup));
		goto failure;
	}

	/* call HBA's target init entry point if it exists */
	if (tran->tran_tgt_init != NULL) {
		SCSI_HBA_LOG((_LOG(4), NULL, child, "init tran_tgt_init"));
		if ((*tran->tran_tgt_init)
		    (self, child, tran, sd) != DDI_SUCCESS) {
			SCSI_HBA_LOG((_LOG(2), NULL, child,
			    "init failed: tran_tgt_init failed"));
			goto failure;
		}
	}

	SCSI_HBA_LOG((_LOG(3), NULL, child, "init successful"));
	return (DDI_SUCCESS);

failure:
	if (tran_clone)
		kmem_free(tran_clone, sizeof (scsi_hba_tran_t));
	mutex_destroy(&sd->sd_mutex);
	kmem_free(sd, sizeof (*sd));
	ddi_set_driver_private(child, NULL);
	ddi_set_name_addr(child, NULL);

	return (err);		/* remove the node */
}

static int
scsi_busctl_uninitchild(dev_info_t *child)
{
	dev_info_t		*self = ddi_get_parent(child);
	scsi_hba_tran_t		*tran = ddi_get_driver_private(self);
	struct scsi_device	*sd = ddi_get_driver_private(child);
	scsi_hba_tran_t		*tran_clone;

	ASSERT(DEVI_BUSY_OWNED(self));

	ASSERT(tran && sd);
	if ((tran == NULL) || (sd == NULL))
		return (DDI_FAILURE);


	SCSI_HBA_LOG((_LOG(3), NULL, child, "uninit begin"));

	if (tran->tran_hba_flags & SCSI_HBA_TRAN_CLONE) {
		tran_clone = sd->sd_address.a_hba_tran;

		/* ... grotty hack, involving sd_tran_safe, continued. */
		if (tran_clone != sd->sd_tran_safe) {
			tran_clone = sd->sd_tran_safe;
#ifdef	DEBUG
			/*
			 * Complain so things get fixed and hack can, at
			 * some point in time, be removed.
			 */
			SCSI_HBA_LOG((_LOG(WARN), self, NULL,
			    "'%s' is corrupting a_hba_tran", sd->sd_dev ?
			    ddi_driver_name(sd->sd_dev) : "unknown_driver"));
#endif	/* DEBUG */
		}

		ASSERT(tran_clone->tran_hba_flags & SCSI_HBA_TRAN_CLONE);
		ASSERT(tran_clone->tran_sd == sd);
		tran = tran_clone;
	} else {
		tran_clone = NULL;
		ASSERT(tran->tran_sd == NULL);
	}

	/*
	 * To simplify host adapter drivers we guarantee that multiple
	 * tran_tgt_init(9E) calls of the same unit address are never
	 * active at the same time.
	 */
	if (tran->tran_tgt_free)
		(*tran->tran_tgt_free) (self, child, tran, sd);

	/*
	 * If a inquiry data is still allocated (by scsi_probe()) we
	 * free the allocation here. This keeps scsi_inq valid for the
	 * same duration as the corresponding inquiry properties. It
	 * also allows a tran_tgt_init() implementation that establishes
	 * sd_inq (common/io/dktp/controller/ata/ata_disk.c) to deal
	 * with deallocation in its tran_tgt_free (setting sd_inq back
	 * to NULL) without upsetting the framework.
	 */
	if (sd->sd_inq) {
		kmem_free(sd->sd_inq, SUN_INQSIZE);
		sd->sd_inq = (struct scsi_inquiry *)NULL;
	}

	mutex_destroy(&sd->sd_mutex);
	if (tran_clone)
		kmem_free(tran_clone, sizeof (scsi_hba_tran_t));
	kmem_free(sd, sizeof (*sd));

	ddi_set_driver_private(child, NULL);
	SCSI_HBA_LOG((_LOG(3), NULL, child, "uninit complete"));
	ddi_set_name_addr(child, NULL);
	return (DDI_SUCCESS);
}

static int
iport_busctl_reportdev(dev_info_t *child)
{
	dev_info_t	*self = ddi_get_parent(child);
	char		*iport;


	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, child,
	    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
	    "scsi-iport", &iport) != DDI_SUCCESS)
		return (DDI_FAILURE);

	SCSI_HBA_LOG((_LOG_NF(CONT), "?%s%d at %s%d: iport %s\n",
	    ddi_driver_name(child), ddi_get_instance(child),
	    ddi_driver_name(self), ddi_get_instance(self),
	    iport));

	ddi_prop_free(iport);
	return (DDI_SUCCESS);
}

/* uninitchild SCSA iport 'child' node */
static int
iport_busctl_uninitchild(dev_info_t *child)
{
	ddi_set_name_addr(child, NULL);
	return (DDI_SUCCESS);
}

/* initchild SCSA iport 'child' node */
static int
iport_busctl_initchild(dev_info_t *child)
{
	dev_info_t	*self = ddi_get_parent(child);
	dev_info_t	*dup = NULL;
	char		addr[SCSI_MAXNAMELEN];
	char		*iport;


	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, child,
	    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
	    "scsi-iport", &iport) != DDI_SUCCESS) {
		return (DDI_NOT_WELL_FORMED);
	}

	(void) snprintf(addr, SCSI_MAXNAMELEN, "%s", iport);
	ddi_prop_free(iport);


	/* set the node @addr string */
	ddi_set_name_addr(child, addr);

	/* Prevent duplicate nodes.  */
	dup = ndi_devi_find(self, ddi_node_name(child), addr);
	if (dup && (dup != child)) {
		ddi_set_name_addr(child, NULL);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/* Uninitialize scsi_device flavor of transport on SCSA iport 'child' node. */
static void
iport_postdetach_tran_scsi_device(dev_info_t *child)
{
	scsi_hba_tran_t		*tran;

	tran = ndi_flavorv_get(child, SCSA_FLAVOR_SCSI_DEVICE);
	if (tran == NULL)
		return;

	scsa_tran_teardown(child, tran);
	scsa_nexus_teardown(child, tran);

	ndi_flavorv_set(child, SCSA_FLAVOR_SCSI_DEVICE, NULL);
	scsi_hba_tran_free(tran);
}

/*
 * Initialize scsi_device flavor of transport on SCSA iport 'child' node.
 *
 * NOTE: Given our past history with SCSI_HBA_TRAN_CLONE (structure-copy tran
 * per scsi_device), using structure-copy of tran at the iport level should
 * not be a problem (the most risky thing is likely tran_hba_dip).
 */
static void
iport_preattach_tran_scsi_device(dev_info_t *child)
{
	dev_info_t	*hba = ddi_get_parent(child);
	scsi_hba_tran_t	*htran;
	scsi_hba_tran_t	*tran;

	/* parent HBA node scsi_device tran is required */
	htran = ndi_flavorv_get(hba, SCSA_FLAVOR_SCSI_DEVICE);
	ASSERT(htran != NULL);
	if (htran == NULL)
		return;

	/* Allocate iport child's scsi_device transport vector */
	tran = scsi_hba_tran_alloc(child, SCSI_HBA_CANSLEEP);
	if (tran == NULL)
		return;

	/* Structure-copy scsi_device transport of HBA to iport. */
	*tran = *htran;

	/*
	 * Reset scsi_device transport fields not shared with the
	 * parent, and not established below.
	 */
	tran->tran_open_flag = 0;
	tran->tran_hba_private = NULL;
	tran->tran_iport_dip = child;

	/* Clear SCSI_HBA_SCSA flags (except TA) */
	tran->tran_hba_flags &= ~SCSI_HBA_SCSA_FM;	/* clear parent state */
	tran->tran_hba_flags |= SCSI_HBA_SCSA_TA;
	tran->tran_hba_flags &= ~SCSI_HBA_HBA;		/* never HBA */

	/* Establish flavor of transport (and ddi_get_driver_private()) */
	ndi_flavorv_set(child, SCSA_FLAVOR_SCSI_DEVICE, tran);

	/* Setup iport node */
	if ((scsa_nexus_setup(child, tran) != DDI_SUCCESS) ||
	    (scsa_tran_setup(child, tran) != DDI_SUCCESS)) {
		iport_postdetach_tran_scsi_device(child);
	}
}

/* Uninitialize smp_device flavor of transport on SCSA iport 'child' node. */
static void
iport_postdetach_tran_smp_device(dev_info_t *child)
{
	sas_hba_tran_t	*tran;

	tran = ndi_flavorv_get(child, SCSA_FLAVOR_SMP);
	if (tran == NULL)
		return;

	ndi_flavorv_set(child, SCSA_FLAVOR_SMP, NULL);
	sas_hba_tran_free(tran);
}

/* Initialize smp_device flavor of transport on SCSA iport 'child' node. */
static void
iport_preattach_tran_smp_device(dev_info_t *child)
{
	dev_info_t	*hba = ddi_get_parent(child);
	sas_hba_tran_t	*htran;
	sas_hba_tran_t	*tran;

	/* parent HBA node smp_device tran is optional */
	htran = ndi_flavorv_get(hba, SCSA_FLAVOR_SMP);
	if (htran == NULL) {
		ndi_flavorv_set(child, SCSA_FLAVOR_SMP, NULL);
		return;
	}

	/* Allocate iport child's smp_device transport vector */
	tran = sas_hba_tran_alloc(child, 0);

	/* Structure-copy smp_device transport of HBA to iport. */
	*tran = *htran;

	/* Establish flavor of transport */
	ndi_flavorv_set(child, SCSA_FLAVOR_SMP, tran);
}


/*
 * Generic bus_ctl operations for SCSI HBA's,
 * hiding the busctl interface from the HBA.
 */
/*ARGSUSED*/
static int
scsi_hba_bus_ctl(
	dev_info_t		*self,
	dev_info_t		*child,
	ddi_ctl_enum_t		op,
	void			*arg,
	void			*result)
{
	int			child_flavor = 0;
	int			val;
	ddi_dma_attr_t		*attr;
	scsi_hba_tran_t		*tran;
	struct attachspec	*as;
	struct detachspec	*ds;

	/* For some ops, child is 'arg'. */
	if ((op == DDI_CTLOPS_INITCHILD) || (op == DDI_CTLOPS_UNINITCHILD))
		child = (dev_info_t *)arg;

	/* Determine the flavor of the child: smp .vs. scsi */
	child_flavor = ndi_flavor_get(child);

	switch (op) {
	case DDI_CTLOPS_INITCHILD:
		switch (child_flavor) {
		case SCSA_FLAVOR_IPORT:
			return (iport_busctl_initchild(child));
		case SCSA_FLAVOR_SMP:
			return (smp_busctl_initchild(child));
		default:
			return (scsi_busctl_initchild(child));
		}
	case DDI_CTLOPS_UNINITCHILD:
		switch (child_flavor) {
		case SCSA_FLAVOR_IPORT:
			return (iport_busctl_uninitchild(child));
		case SCSA_FLAVOR_SMP:
			return (smp_busctl_uninitchild(child));
		default:
			return (scsi_busctl_uninitchild(child));
		}
	case DDI_CTLOPS_REPORTDEV:
		switch (child_flavor) {
		case SCSA_FLAVOR_IPORT:
			return (iport_busctl_reportdev(child));
		case SCSA_FLAVOR_SMP:
			return (smp_busctl_reportdev(child));
		default:
			return (scsi_busctl_reportdev(child));
		}
	case DDI_CTLOPS_ATTACH:
		as = (struct attachspec *)arg;

		if (child_flavor != SCSA_FLAVOR_IPORT)
			return (DDI_SUCCESS);

		/* iport processing */
		if (as->when == DDI_PRE) {
			/* setup pre attach(9E) */
			iport_preattach_tran_scsi_device(child);
			iport_preattach_tran_smp_device(child);
		} else if ((as->when == DDI_POST) &&
		    (as->result != DDI_SUCCESS)) {
			/* cleanup if attach(9E) failed */
			iport_postdetach_tran_scsi_device(child);
			iport_postdetach_tran_smp_device(child);
		}
		return (DDI_SUCCESS);
	case DDI_CTLOPS_DETACH:
		ds = (struct detachspec *)arg;

		if (child_flavor != SCSA_FLAVOR_IPORT)
			return (DDI_SUCCESS);

		/* iport processing */
		if ((ds->when == DDI_POST) &&
		    (ds->result == DDI_SUCCESS)) {
			/* cleanup if detach(9E) was successful */
			iport_postdetach_tran_scsi_device(child);
			iport_postdetach_tran_smp_device(child);
		}
		return (DDI_SUCCESS);
	case DDI_CTLOPS_IOMIN:
		tran = ddi_get_driver_private(self);
		ASSERT(tran);
		if (tran == NULL)
			return (DDI_FAILURE);

		/*
		 * The 'arg' value of nonzero indicates 'streaming'
		 * mode. If in streaming mode, pick the largest
		 * of our burstsizes available and say that that
		 * is our minimum value (modulo what minxfer is).
		 */
		attr = &tran->tran_dma_attr;
		val = *((int *)result);
		val = maxbit(val, attr->dma_attr_minxfer);
		*((int *)result) = maxbit(val, ((intptr_t)arg ?
		    (1<<ddi_ffs(attr->dma_attr_burstsizes)-1) :
		    (1<<(ddi_fls(attr->dma_attr_burstsizes)-1))));

		return (ddi_ctlops(self, child, op, arg, result));

	case DDI_CTLOPS_SIDDEV:
		return (ndi_dev_is_persistent_node(child) ?
		    DDI_SUCCESS : DDI_FAILURE);

	/* XXX these should be handled */
	case DDI_CTLOPS_POWER:
		return (DDI_SUCCESS);

	/*
	 * These ops correspond to functions that "shouldn't" be called
	 * by a SCSI target driver. So we whine when we're called.
	 */
	case DDI_CTLOPS_DMAPMAPC:
	case DDI_CTLOPS_REPORTINT:
	case DDI_CTLOPS_REGSIZE:
	case DDI_CTLOPS_NREGS:
	case DDI_CTLOPS_SLAVEONLY:
	case DDI_CTLOPS_AFFINITY:
	case DDI_CTLOPS_POKE:
	case DDI_CTLOPS_PEEK:
		SCSI_HBA_LOG((_LOG(WARN), self, NULL, "invalid op (%d)", op));
		return (DDI_FAILURE);

	/* Everything else we pass up */
	case DDI_CTLOPS_PTOB:
	case DDI_CTLOPS_BTOP:
	case DDI_CTLOPS_BTOPR:
	case DDI_CTLOPS_DVMAPAGESIZE:
	default:
		return (ddi_ctlops(self, child, op, arg, result));
	}
}

/*
 * Private wrapper for scsi_pkt's allocated via scsi_hba_pkt_alloc()
 */
struct scsi_pkt_wrapper {
	struct scsi_pkt		scsi_pkt;
	int			pkt_wrapper_magic;
	int			pkt_wrapper_len;
};

#if !defined(lint)
_NOTE(SCHEME_PROTECTS_DATA("unique per thread", scsi_pkt_wrapper))
_NOTE(SCHEME_PROTECTS_DATA("Unshared Data", dev_ops))
#endif

/*
 * Called by an HBA to allocate a scsi_pkt
 */
/*ARGSUSED*/
struct scsi_pkt *
scsi_hba_pkt_alloc(
	dev_info_t		*dip,
	struct scsi_address	*ap,
	int			cmdlen,
	int			statuslen,
	int			tgtlen,
	int			hbalen,
	int			(*callback)(caddr_t arg),
	caddr_t			arg)
{
	struct scsi_pkt		*pkt;
	struct scsi_pkt_wrapper	*hba_pkt;
	caddr_t			p;
	int			acmdlen, astatuslen, atgtlen, ahbalen;
	int			pktlen;

	/* Sanity check */
	if (callback != SLEEP_FUNC && callback != NULL_FUNC)
		SCSI_HBA_LOG((_LOG(WARN), dip, NULL,
		    "callback must be SLEEP_FUNC or NULL_FUNC"));

	/*
	 * Round up so everything gets allocated on long-word boundaries
	 */
	acmdlen = ROUNDUP(cmdlen);
	astatuslen = ROUNDUP(statuslen);
	atgtlen = ROUNDUP(tgtlen);
	ahbalen = ROUNDUP(hbalen);
	pktlen = sizeof (struct scsi_pkt_wrapper) +
	    acmdlen + astatuslen + atgtlen + ahbalen;

	hba_pkt = kmem_zalloc(pktlen,
	    (callback == SLEEP_FUNC) ? KM_SLEEP : KM_NOSLEEP);
	if (hba_pkt == NULL) {
		ASSERT(callback == NULL_FUNC);
		return (NULL);
	}

	/*
	 * Set up our private info on this pkt
	 */
	hba_pkt->pkt_wrapper_len = pktlen;
	hba_pkt->pkt_wrapper_magic = PKT_WRAPPER_MAGIC;	/* alloced correctly */
	pkt = &hba_pkt->scsi_pkt;

	/*
	 * Set up pointers to private data areas, cdb, and status.
	 */
	p = (caddr_t)(hba_pkt + 1);
	if (hbalen > 0) {
		pkt->pkt_ha_private = (opaque_t)p;
		p += ahbalen;
	}
	if (tgtlen > 0) {
		pkt->pkt_private = (opaque_t)p;
		p += atgtlen;
	}
	if (statuslen > 0) {
		pkt->pkt_scbp = (uchar_t *)p;
		p += astatuslen;
	}
	if (cmdlen > 0) {
		pkt->pkt_cdbp = (uchar_t *)p;
	}

	/*
	 * Initialize the pkt's scsi_address
	 */
	pkt->pkt_address = *ap;

	/*
	 * NB: It may not be safe for drivers, esp target drivers, to depend
	 * on the following fields being set until all the scsi_pkt
	 * allocation violations discussed in scsi_pkt.h are all resolved.
	 */
	pkt->pkt_cdblen = cmdlen;
	pkt->pkt_tgtlen = tgtlen;
	pkt->pkt_scblen = statuslen;

	return (pkt);
}

/*
 * Called by an HBA to free a scsi_pkt
 */
/*ARGSUSED*/
void
scsi_hba_pkt_free(
	struct scsi_address	*ap,
	struct scsi_pkt		*pkt)
{
	kmem_free(pkt, ((struct scsi_pkt_wrapper *)pkt)->pkt_wrapper_len);
}

/*
 * Return 1 if the scsi_pkt used a proper allocator.
 *
 * The DDI does not allow a driver to allocate it's own scsi_pkt(9S), a
 * driver should not have *any* compiled in dependencies on "sizeof (struct
 * scsi_pkt)". While this has been the case for many years, a number of
 * drivers have still not been fixed. This function can be used to detect
 * improperly allocated scsi_pkt structures, and produce messages identifying
 * drivers that need to be fixed.
 *
 * While drivers in violation are being fixed, this function can also
 * be used by the framework to detect packets that violated allocation
 * rules.
 *
 * NB: It is possible, but very unlikely, for this code to return a false
 * positive (finding correct magic, but for wrong reasons).  Careful
 * consideration is needed for callers using this interface to condition
 * access to newer scsi_pkt fields (those after pkt_reason).
 *
 * NB: As an aid to minimizing the amount of work involved in 'fixing' legacy
 * drivers that violate scsi_*(9S) allocation rules, private
 * scsi_pkt_size()/scsi_size_clean() functions are available (see their
 * implementation for details).
 *
 * *** Non-legacy use of scsi_pkt_size() is discouraged. ***
 *
 * NB: When supporting broken HBA drivers is not longer a concern, this
 * code should be removed.
 */
int
scsi_pkt_allocated_correctly(struct scsi_pkt *pkt)
{
	struct scsi_pkt_wrapper	*hba_pkt = (struct scsi_pkt_wrapper *)pkt;
	int	magic;
	major_t	major;
#ifdef	DEBUG
	int	*pspwm, *pspcwm;

	/*
	 * We are getting scsi packets from two 'correct' wrapper schemes,
	 * make sure we are looking at the same place in both to detect
	 * proper allocation.
	 */
	pspwm = &((struct scsi_pkt_wrapper *)0)->pkt_wrapper_magic;
	pspcwm = &((struct scsi_pkt_cache_wrapper *)0)->pcw_magic;
	ASSERT(pspwm == pspcwm);
#endif	/* DEBUG */


	/*
	 * Check to see if driver is scsi_size_clean(), assume it
	 * is using the scsi_pkt_size() interface everywhere it needs to
	 * if the driver indicates it is scsi_size_clean().
	 */
	major = ddi_driver_major(P_TO_TRAN(pkt)->tran_hba_dip);
	if (devnamesp[major].dn_flags & DN_SCSI_SIZE_CLEAN)
		return (1);		/* ok */

	/*
	 * Special case crossing a page boundary. If the scsi_pkt was not
	 * allocated correctly, then across a page boundary we have a
	 * fault hazard.
	 */
	if ((((uintptr_t)(&hba_pkt->scsi_pkt)) & MMU_PAGEMASK) ==
	    (((uintptr_t)(&hba_pkt->pkt_wrapper_magic)) & MMU_PAGEMASK)) {
		/* fastpath, no cross-page hazard */
		magic = hba_pkt->pkt_wrapper_magic;
	} else {
		/* add protection for cross-page hazard */
		if (ddi_peek32((dev_info_t *)NULL,
		    &hba_pkt->pkt_wrapper_magic, &magic) == DDI_FAILURE) {
			return (0);	/* violation */
		}
	}

	/* properly allocated packet always has correct magic */
	return ((magic == PKT_WRAPPER_MAGIC) ? 1 : 0);
}

/*
 * Private interfaces to simplify conversion of legacy drivers so they don't
 * depend on scsi_*(9S) size. Instead of using these private interface, HBA
 * drivers should use DDI sanctioned allocation methods:
 *
 *	scsi_pkt	Use scsi_hba_pkt_alloc(9F), or implement
 *			tran_setup_pkt(9E).
 *
 *	scsi_device	You are doing something strange/special, a scsi_device
 *			structure should only be allocated by scsi_hba.c
 *			initchild code or scsi_vhci.c code.
 *
 *	scsi_hba_tran	Use scsi_hba_tran_alloc(9F).
 */
size_t
scsi_pkt_size()
{
	return (sizeof (struct scsi_pkt));
}

size_t
scsi_hba_tran_size()
{
	return (sizeof (scsi_hba_tran_t));
}

size_t
scsi_device_size()
{
	return (sizeof (struct scsi_device));
}

/*
 * Legacy compliance to scsi_pkt(9S) allocation rules through use of
 * scsi_pkt_size() is detected by the 'scsi-size-clean' driver.conf property
 * or an HBA driver calling to scsi_size_clean() from attach(9E).  A driver
 * developer should only indicate that a legacy driver is clean after using
 * SCSI_SIZE_CLEAN_VERIFY to ensure compliance (see scsi_pkt.h).
 */
void
scsi_size_clean(dev_info_t *dip)
{
	major_t		major;
	struct devnames	*dnp;

	ASSERT(dip);
	major = ddi_driver_major(dip);
	ASSERT(major < devcnt);
	if (major >= devcnt) {
		SCSI_HBA_LOG((_LOG(WARN), dip, NULL,
		    "scsi_pkt_size: bogus major: %d", major));
		return;
	}

	/* Set DN_SCSI_SIZE_CLEAN flag in dn_flags. */
	dnp = &devnamesp[major];
	if ((dnp->dn_flags & DN_SCSI_SIZE_CLEAN) == 0) {
		LOCK_DEV_OPS(&dnp->dn_lock);
		dnp->dn_flags |= DN_SCSI_SIZE_CLEAN;
		UNLOCK_DEV_OPS(&dnp->dn_lock);
	}
}


/*
 * Called by an HBA to map strings to capability indices
 */
int
scsi_hba_lookup_capstr(
	char			*capstr)
{
	/*
	 * Capability strings: only add entries to mask the legacy
	 * '_' vs. '-' misery.  All new capabilities should use '-',
	 * and be captured be added to SCSI_CAP_ASCII.
	 */
	static struct cap_strings {
		char	*cap_string;
		int	cap_index;
	} cap_strings[] = {
		{ "dma_max",		SCSI_CAP_DMA_MAX		},
		{ "msg_out",		SCSI_CAP_MSG_OUT		},
		{ "wide_xfer",		SCSI_CAP_WIDE_XFER		},
		{ NULL,			0				}
	};
	static char		*cap_ascii[] = SCSI_CAP_ASCII;
	char			**cap;
	int			i;
	struct cap_strings	*cp;

	for (cap = cap_ascii, i = 0; *cap != NULL; cap++, i++)
		if (strcmp(*cap, capstr) == 0)
			return (i);

	for (cp = cap_strings; cp->cap_string != NULL; cp++)
		if (strcmp(cp->cap_string, capstr) == 0)
			return (cp->cap_index);

	return (-1);
}

/*
 * Called by an HBA to determine if the system is in 'panic' state.
 */
int
scsi_hba_in_panic()
{
	return (panicstr != NULL);
}

/*
 * If a SCSI target driver attempts to mmap memory,
 * the buck stops here.
 */
/*ARGSUSED*/
static int
scsi_hba_map_fault(
	dev_info_t		*dip,
	dev_info_t		*child,
	struct hat		*hat,
	struct seg		*seg,
	caddr_t			addr,
	struct devpage		*dp,
	pfn_t			pfn,
	uint_t			prot,
	uint_t			lock)
{
	return (DDI_FAILURE);
}

static int
scsi_hba_get_eventcookie(
	dev_info_t		*self,
	dev_info_t		*child,
	char			*name,
	ddi_eventcookie_t	*eventp)
{
	scsi_hba_tran_t		*tran;

	tran = ddi_get_driver_private(self);
	if (tran->tran_get_eventcookie &&
	    ((*tran->tran_get_eventcookie)(self,
	    child, name, eventp) == DDI_SUCCESS)) {
		return (DDI_SUCCESS);
	}

	return (ndi_busop_get_eventcookie(self, child, name, eventp));
}

static int
scsi_hba_add_eventcall(
	dev_info_t		*self,
	dev_info_t		*child,
	ddi_eventcookie_t	event,
	void			(*callback)(
					dev_info_t *self,
					ddi_eventcookie_t event,
					void *arg,
					void *bus_impldata),
	void			*arg,
	ddi_callback_id_t	*cb_id)
{
	scsi_hba_tran_t		*tran;

	tran = ddi_get_driver_private(self);
	if (tran->tran_add_eventcall &&
	    ((*tran->tran_add_eventcall)(self, child,
	    event, callback, arg, cb_id) == DDI_SUCCESS)) {
		return (DDI_SUCCESS);
	}

	return (DDI_FAILURE);
}

static int
scsi_hba_remove_eventcall(dev_info_t *self, ddi_callback_id_t cb_id)
{
	scsi_hba_tran_t		*tran;
	ASSERT(cb_id);

	tran = ddi_get_driver_private(self);
	if (tran->tran_remove_eventcall &&
	    ((*tran->tran_remove_eventcall)(
	    self, cb_id) == DDI_SUCCESS)) {
		return (DDI_SUCCESS);
	}

	return (DDI_FAILURE);
}

static int
scsi_hba_post_event(
	dev_info_t		*self,
	dev_info_t		*child,
	ddi_eventcookie_t	event,
	void			*bus_impldata)
{
	scsi_hba_tran_t		*tran;

	tran = ddi_get_driver_private(self);
	if (tran->tran_post_event &&
	    ((*tran->tran_post_event)(self,
	    child, event, bus_impldata) == DDI_SUCCESS)) {
		return (DDI_SUCCESS);
	}

	return (DDI_FAILURE);
}

/*
 * Default getinfo(9e) for scsi_hba
 */
/* ARGSUSED */
static int
scsi_hba_info(dev_info_t *self, ddi_info_cmd_t infocmd, void *arg,
    void **result)
{
	int error = DDI_SUCCESS;

	switch (infocmd) {
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)(intptr_t)(MINOR2INST(getminor((dev_t)arg)));
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}

/*
 * Default open and close routine for scsi_hba
 */
/* ARGSUSED */
int
scsi_hba_open(dev_t *devp, int flags, int otyp, cred_t *credp)
{
	dev_info_t	*self;
	scsi_hba_tran_t	*tran;
	int		rv = 0;

	if (otyp != OTYP_CHR)
		return (EINVAL);

	if ((self = e_ddi_hold_devi_by_dev(*devp, 0)) == NULL)
		return (ENXIO);

	tran = ddi_get_driver_private(self);
	if (tran == NULL) {
		ddi_release_devi(self);
		return (ENXIO);
	}

	/*
	 * tran_open_flag bit field:
	 *	0:	closed
	 *	1:	shared open by minor at bit position
	 *	1 at 31st bit:	exclusive open
	 */
	mutex_enter(&(tran->tran_open_lock));
	if (flags & FEXCL) {
		if (tran->tran_open_flag != 0) {
			rv = EBUSY;		/* already open */
		} else {
			tran->tran_open_flag = TRAN_OPEN_EXCL;
		}
	} else {
		if (tran->tran_open_flag == TRAN_OPEN_EXCL) {
			rv = EBUSY;		/* already excl. open */
		} else {
			int minor = getminor(*devp) & TRAN_MINOR_MASK;
			tran->tran_open_flag |= (1 << minor);
			/*
			 * Ensure that the last framework reserved minor
			 * is unused. Otherwise, the exclusive open
			 * mechanism may break.
			 */
			ASSERT(minor != 31);
		}
	}
	mutex_exit(&(tran->tran_open_lock));

	ddi_release_devi(self);
	return (rv);
}

/* ARGSUSED */
int
scsi_hba_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	dev_info_t	*self;
	scsi_hba_tran_t	*tran;

	if (otyp != OTYP_CHR)
		return (EINVAL);

	if ((self = e_ddi_hold_devi_by_dev(dev, 0)) == NULL)
		return (ENXIO);

	tran = ddi_get_driver_private(self);
	if (tran == NULL) {
		ddi_release_devi(self);
		return (ENXIO);
	}

	mutex_enter(&(tran->tran_open_lock));
	if (tran->tran_open_flag == TRAN_OPEN_EXCL) {
		tran->tran_open_flag = 0;
	} else {
		int minor = getminor(dev) & TRAN_MINOR_MASK;
		tran->tran_open_flag &= ~(1 << minor);
	}
	mutex_exit(&(tran->tran_open_lock));

	ddi_release_devi(self);
	return (0);
}

/*
 * standard ioctl commands for SCSI hotplugging
 */
/* ARGSUSED */
int
scsi_hba_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
	int *rvalp)
{
	dev_info_t		*self;
	struct devctl_iocdata	*dcp = NULL;
	dev_info_t		*child = NULL;
	struct scsi_device	*sd;
	scsi_hba_tran_t		*tran;
	uint_t			bus_state;
	int			rv = 0;
	int			circ;

	if ((self = e_ddi_hold_devi_by_dev(dev, 0)) == NULL) {
		rv = ENXIO;
		goto out;
	}

	if ((tran = ddi_get_driver_private(self)) == NULL) {
		rv = ENXIO;
		goto out;
	}

	/* Ioctls for which the generic implementation suffices. */
	switch (cmd) {
	case DEVCTL_DEVICE_GETSTATE:
	case DEVCTL_DEVICE_ONLINE:
	case DEVCTL_DEVICE_OFFLINE:
	case DEVCTL_DEVICE_REMOVE:
	case DEVCTL_BUS_GETSTATE:
		rv = ndi_devctl_ioctl(self, cmd, arg, mode, 0);
		goto out;
	}

	/* read devctl ioctl data */
	if (ndi_dc_allochdl((void *)arg, &dcp) != NDI_SUCCESS) {
		rv = EFAULT;
		goto out;
	}

	/* Ioctls that require child identification */
	switch (cmd) {
	case DEVCTL_DEVICE_RESET:
		/* child identification from unit-address */
		if (ndi_dc_getname(dcp) == NULL ||
		    ndi_dc_getaddr(dcp) == NULL) {
			rv = EINVAL;
			goto out;
		}

		ndi_devi_enter(self, &circ);
		child = ndi_devi_find(self,
		    ndi_dc_getname(dcp), ndi_dc_getaddr(dcp));
		if (child == NULL) {
			ndi_devi_exit(self, circ);
			rv = ENXIO;
			goto out;
		}
		ndi_hold_devi(child);
		ndi_devi_exit(self, circ);
		break;

	case DEVCTL_BUS_RESETALL:
		/*
		 * Find a child's scsi_address so we can invoke tran_reset
		 * below.
		 *
		 * XXX If no child exists, one may to able to fake a child.
		 *	This will be a enhancement for the future.
		 *	For now, we fall back to BUS_RESET.
		 * XXX We sould be looking at node state to get one
		 *	that is initialized...
		 */
		ndi_devi_enter(self, &circ);
		child = ddi_get_child(self);
		sd = NULL;
		while (child) {
			/* XXX verify scsi_device 'flavor' of child */
			if ((sd = ddi_get_driver_private(child)) != NULL) {
				ndi_hold_devi(child);
				break;
			}
			child = ddi_get_next_sibling(child);
		}
		ndi_devi_exit(self, circ);
		break;
	}

	switch (cmd) {
	case DEVCTL_DEVICE_RESET:
		ASSERT(child);
		if (tran->tran_reset == NULL)
			rv = ENOTSUP;
		else {
			sd = ddi_get_driver_private(child);
			/* XXX verify scsi_device 'flavor' of child */
			if ((sd == NULL) ||
			    (tran->tran_reset(&sd->sd_address,
			    RESET_TARGET) != 1))
				rv = EIO;
		}
		break;

	case DEVCTL_BUS_QUIESCE:
		if ((ndi_get_bus_state(self, &bus_state) == NDI_SUCCESS) &&
		    (bus_state == BUS_QUIESCED))
			rv = EALREADY;
		else if (tran->tran_quiesce == NULL)
			rv = ENOTSUP;
		else if ((*tran->tran_quiesce)(self) != 0)
			rv = EIO;
		else
			(void) ndi_set_bus_state(self, BUS_QUIESCED);
		break;

	case DEVCTL_BUS_UNQUIESCE:
		if ((ndi_get_bus_state(self, &bus_state) == NDI_SUCCESS) &&
		    (bus_state == BUS_ACTIVE))
			rv = EALREADY;
		else if (tran->tran_unquiesce == NULL)
			rv = ENOTSUP;
		else if ((*tran->tran_unquiesce)(self) != 0)
			rv = EIO;
		else
			(void) ndi_set_bus_state(self, BUS_ACTIVE);
		break;

	case DEVCTL_BUS_RESET:
		if (tran->tran_bus_reset == NULL)
			rv = ENOTSUP;
		else if ((*tran->tran_bus_reset)(self, RESET_BUS) != 1)
			rv = EIO;
		break;

	case DEVCTL_BUS_RESETALL:
		if (tran->tran_reset == NULL) {
			rv = ENOTSUP;
		} else {
			if (sd) {
				if ((*tran->tran_reset)
				    (&sd->sd_address, RESET_ALL) != 1)
					rv = EIO;
			} else {
				if ((tran->tran_bus_reset == NULL) ||
				    ((*tran->tran_bus_reset)
				    (self, RESET_BUS) != 1))
					rv = EIO;
			}
		}
		break;

	case DEVCTL_BUS_CONFIGURE:
		if (ndi_devi_config(self, NDI_DEVFS_CLEAN|
		    NDI_DEVI_PERSIST|NDI_CONFIG_REPROBE) != NDI_SUCCESS) {
			rv = EIO;
		}
		break;

	case DEVCTL_BUS_UNCONFIGURE:
		if (ndi_devi_unconfig(self,
		    NDI_DEVI_REMOVE|NDI_DEVFS_CLEAN) != NDI_SUCCESS) {
			rv = EBUSY;
		}
		break;

	default:
		rv = ENOTTY;
	}

out:	if (child)
		ndi_rele_devi(child);
	if (dcp)
		ndi_dc_freehdl(dcp);
	if (self)
		ddi_release_devi(self);
	return (rv);
}

/*ARGSUSED*/
static int
scsi_hba_fm_init_child(dev_info_t *self, dev_info_t *child, int cap,
    ddi_iblock_cookie_t *ibc)
{
	scsi_hba_tran_t	*tran = ddi_get_driver_private(self);

	return (tran ? tran->tran_fm_capable : scsi_fm_capable);
}

static int
scsi_hba_bus_power(dev_info_t *self, void *impl_arg, pm_bus_power_op_t op,
    void *arg, void *result)
{
	scsi_hba_tran_t	*tran;

	tran = ddi_get_driver_private(self);
	if (tran && tran->tran_bus_power) {
		return (tran->tran_bus_power(self, impl_arg,
		    op, arg, result));
	}

	return (pm_busop_bus_power(self, impl_arg, op, arg, result));
}

/*
 * Return the lun from an address string. Either the lun is after the
 * first ',' or the entire addr is the lun. Return SCSI_LUN64_ILLEGAL
 * if the format is incorrect.
 *
 * If the addr specified has incorrect syntax (busconfig one of
 * bogus /devices path) then scsi_addr_to_lun64 can return SCSI_LUN64_ILLEGAL.
 */
scsi_lun64_t
scsi_addr_to_lun64(char *addr)
{
	scsi_lun64_t	lun64;
	char		*s;
	int		i;

	if (addr) {
		s = strchr(addr, ',');			/* "addr,lun[,sfunc]" */
		if (s)
			s++;				/* skip ',' */
		else
			s = addr;			/* "lun" */

		for (lun64 = 0, i = 0; *s && (i < 16); s++, i++) {
			if (*s >= '0' && *s <= '9')
				lun64 = (lun64 << 4) + (*s - '0');
			else if (*s >= 'A' && *s <= 'F')
				lun64 = (lun64 << 4) + 10 + (*s - 'A');
			else if (*s >= 'a' && *s <= 'f')
				lun64 = (lun64 << 4) + 10 + (*s - 'a');
			else
				break;
		}
		if (*s && (*s != ','))		/* addr,lun[,sfunc] is OK */
			lun64 = SCSI_LUN64_ILLEGAL;
	} else
		lun64 = SCSI_LUN64_ILLEGAL;

	if (lun64 == SCSI_LUN64_ILLEGAL)
		SCSI_HBA_LOG((_LOG(2), NULL, NULL,
		    "addr_to_lun64 %s lun %" PRIlun64,
		    addr ? addr : "NULL", lun64));
	return (lun64);
}

/*
 * Convert scsi ascii string data to NULL terminated (semi) legal IEEE 1275
 * "compatible" (name) property form.
 *
 * For ASCII INQUIRY data, a one-way conversion algorithm is needed to take
 * SCSI_ASCII (20h - 7Eh) to a 1275-like compatible form. The 1275 spec allows
 * letters, digits, one ",", and ". _ + -", all limited by a maximum 31
 * character length. Since ", ." are used as separators in the compatible
 * string itself, they are converted to "_". All SCSI_ASCII characters that
 * are illegal in 1275, as well as any illegal SCSI_ASCII characters
 * encountered, are converted to "_". To reduce length, trailing blanks are
 * trimmed from SCSI_ASCII fields prior to conversion.
 *
 * Example: SCSI_ASCII "ST32550W SUN2.1G" -> "ST32550W_SUN2_1G"
 *
 * NOTE: the 1275 string form is always less than or equal to the scsi form.
 */
static char *
string_scsi_to_1275(char *s_1275, char *s_scsi, int len)
{
	(void) strncpy(s_1275, s_scsi, len);
	s_1275[len--] = '\0';

	while (len >= 0) {
		if (s_1275[len] == ' ')
			s_1275[len--] = '\0';	/* trim trailing " " */
		else
			break;
	}

	while (len >= 0) {
		if (((s_1275[len] >= 'a') && (s_1275[len] <= 'z')) ||
		    ((s_1275[len] >= 'A') && (s_1275[len] <= 'Z')) ||
		    ((s_1275[len] >= '0') && (s_1275[len] <= '9')) ||
		    (s_1275[len] == '_') ||
		    (s_1275[len] == '+') ||
		    (s_1275[len] == '-'))
			len--;			/* legal 1275  */
		else
			s_1275[len--] = '_';	/* illegal SCSI_ASCII | 1275 */
	}

	return (s_1275);
}

/*
 * Given the inquiry data, binding_set, and dtype_node for a scsi device,
 * return the nodename and compatible property for the device. The "compatible"
 * concept comes from IEEE-1275. The compatible information is returned is in
 * the correct form for direct use defining the "compatible" string array
 * property. Internally, "compatible" is also used to determine the nodename
 * to return.
 *
 * This function is provided as a separate entry point for use by drivers that
 * currently issue their own non-SCSA inquiry command and perform their own
 * node creation based their own private compiled in tables. Converting these
 * drivers to use this interface provides a quick easy way of obtaining
 * consistency as well as the flexibility associated with the 1275 techniques.
 *
 * The dtype_node is passed as a separate argument (instead of having the
 * implementation use inq_dtype). It indicates that information about
 * a secondary function embedded service should be produced.
 *
 * Callers must always use scsi_hba_nodename_compatible_free, even if
 * *nodenamep is null, to free the nodename and compatible information
 * when done.
 *
 * If a nodename can't be determined then **compatiblep will point to a
 * diagnostic string containing all the compatible forms.
 *
 * NOTE: some compatible strings may violate the 31 character restriction
 * imposed by IEEE-1275. This is not a problem because Solaris does not care
 * about this 31 character limit.
 *
 * Each compatible form belongs to a form-group.  The form-groups currently
 * defined are generic ("scsiclass"), binding-set ("scsa.b"), and failover
 * ("scsa.f").
 *
 * The following compatible forms, in high to low precedence
 * order, are defined for SCSI target device nodes.
 *
 *  scsiclass,DDEEFFF.vVVVVVVVV.pPPPPPPPPPPPPPPPP.rRRRR	(1 *1&2)
 *  scsiclass,DDEE.vVVVVVVVV.pPPPPPPPPPPPPPPPP.rRRRR	(2 *1)
 *  scsiclass,DDFFF.vVVVVVVVV.pPPPPPPPPPPPPPPPP.rRRRR	(3 *2)
 *  scsiclass,DD.vVVVVVVVV.pPPPPPPPPPPPPPPPP.rRRRR	(4)
 *  scsiclass,DDEEFFF.vVVVVVVVV.pPPPPPPPPPPPPPPPP	(5 *1&2)
 *  scsiclass,DDEE.vVVVVVVVV.pPPPPPPPPPPPPPPPP		(6 *1)
 *  scsiclass,DDFFF.vVVVVVVVV.pPPPPPPPPPPPPPPPP		(7 *2)
 *  scsiclass,DD.vVVVVVVVV.pPPPPPPPPPPPPPPPP		(8)
 *  scsa,DD.bBBBBBBBB					(8.5 *3)
 *  scsiclass,DDEEFFF					(9 *1&2)
 *  scsiclass,DDEE					(10 *1)
 *  scsiclass,DDFFF					(11 *2)
 *  scsiclass,DD					(12)
 *  scsa.fFFF						(12.5 *4)
 *  scsiclass						(13)
 *
 *	  *1 only produced on a secondary function node
 *	  *2 only produced when generic form-group flags exist.
 *	  *3 only produced when binding-set form-group legacy support is needed
 *	  *4 only produced when failover form-group flags exist.
 *
 *	where:
 *
 *	v                       is the letter 'v'. Denotest the
 *				beginning of VVVVVVVV.
 *
 *	VVVVVVVV                Translated scsi_vendor.
 *
 *	p                       is the letter 'p'. Denotes the
 *				beginning of PPPPPPPPPPPPPPPP.
 *
 *	PPPPPPPPPPPPPPPP	Translated scsi_product.
 *
 *	r                       is the letter 'r'. Denotes the
 *				beginning of RRRR.
 *
 *	RRRR                    Translated scsi_revision.
 *
 *	DD                      is a two digit ASCII hexadecimal
 *				number. The value of the two digits is
 *				based one the SCSI "Peripheral device
 *				type" command set associated with the
 *				node. On a primary node this is the
 *				scsi_dtype of the primary command set,
 *				on a secondary node this is the
 *				scsi_dtype associated with the embedded
 *				function command set.
 *
 *	EE                      Same encoding used for DD. This form is
 *				only generated on secondary function
 *				nodes. The DD function is embedded in
 *				an EE device.
 *
 *	FFF                     Concatenation, in alphabetical order,
 *				of the flag characters within a form-group.
 *				For a given form-group, the following
 *				flags are defined.
 *
 *				scsiclass: (generic form-group):
 *				  R	Removable_Media: Used when
 *					inq_rmb is set.
 *
 *				scsa.f:	(failover form-group):
 *				  E	Explicit Target_Port_Group: Used
 *					when inq_tpgse is set and 'G' is
 *					alse present.
 *				  G	GUID: Used when a GUID can be
 *					generated for the device.
 *				  I	Implicit Target_Port_Group: Used
 *					when inq_tpgs is set and 'G' is
 *					also present.
 *
 *				Forms using FFF are only be generated
 *				if there are applicable flag
 *				characters.
 *
 *	b                       is the letter 'b'. Denotes the
 *				beginning of BBBBBBBB.
 *
 *	BBBBBBBB                Binding-set. Operating System Specific:
 *				scsi-binding-set property of HBA.
 */
#define	NCOMPAT		(1 + (13 + 2) + 1)
#define	COMPAT_LONGEST	(strlen( \
	"scsiclass,DDEEFFF.vVVVVVVVV.pPPPPPPPPPPPPPPPP.rRRRR" + 1))

/*
 * Private version with extra device 'identity' arguments to allow code
 * to determine GUID FFF support.
 */
static void
scsi_hba_identity_nodename_compatible_get(struct scsi_inquiry *inq,
    uchar_t *inq80, size_t inq80len, uchar_t *inq83, size_t inq83len,
    char *binding_set, int dtype_node, char *compat0,
    char **nodenamep, char ***compatiblep, int *ncompatiblep)
{
	char		vid[sizeof (inq->inq_vid) + 1 ];
	char		pid[sizeof (inq->inq_pid) + 1];
	char		rev[sizeof (inq->inq_revision) + 1];
	char		gf[sizeof ("R\0")];
	char		ff[sizeof ("EGI\0")];
	int		dtype_device;
	int		ncompat;		/* number of compatible */
	char		**compatp;		/* compatible ptrs */
	int		i;
	char		*nname;			/* nodename */
	char		*dname;			/* driver name */
	char		**csp;
	char		*p;
	int		tlen;
	int		len;
	major_t		major;
	ddi_devid_t	devid;
	char		*guid;

	/*
	 * Nodename_aliases: This table was originally designed to be
	 * implemented via a new nodename_aliases file - a peer to the
	 * driver_aliases that selects a nodename based on compatible
	 * forms in much the same say driver_aliases is used to select
	 * driver bindings from compatible forms. Each compatible form
	 * is an 'alias'. Until a more general need for a
	 * nodename_aliases file exists, which may never occur, the
	 * scsi mappings are described here via a compiled in table.
	 *
	 * This table contains nodename mappings for self-identifying
	 * scsi devices enumerated by the Solaris kernel. For a given
	 * device, the highest precedence "compatible" form with a
	 * mapping is used to select the nodename for the device. This
	 * will typically be a generic nodename, however in some legacy
	 * compatibility cases a driver nodename mapping may be selected.
	 *
	 * Because of possible breakage associated with switching SCSI
	 * target devices from driver nodenames to generic nodenames,
	 * we are currently unable to support generic nodenames for all
	 * SCSI devices (binding-sets). Although /devices paths are
	 * defined as unstable, avoiding possible breakage is
	 * important. Some of the newer SCSI transports (USB) already
	 * use generic nodenames. All new SCSI transports and target
	 * devices should use generic nodenames. At times this decision
	 * may be architecture dependent (sparc .vs. intel) based on when
	 * a transport was supported on a particular architecture.
	 *
	 * We provide a base set of generic nodename mappings based on
	 * scsiclass dtype and higher-precedence driver nodename
	 * mappings based on scsa "binding-set" to cover legacy
	 * issues. The binding-set is typically associated with
	 * "scsi-binding-set" property value of the HBA. The legacy
	 * mappings are provided independent of whether the driver they
	 * refer to is installed. This allows a correctly named node
	 * be created at discovery time, and binding to occur when/if
	 * an add_drv of the legacy driver occurs.
	 *
	 * We also have mappings for legacy SUN hardware that
	 * misidentifies itself (enclosure services which identify
	 * themselves as processors). All future hardware should use
	 * the correct dtype.
	 *
	 * As SCSI HBAs are modified to use the SCSA interfaces for
	 * self-identifying SCSI target devices (PSARC/2004/116) the
	 * nodename_aliases table (PSARC/2004/420) should be augmented
	 * with legacy mappings in order to maintain compatibility with
	 * existing /devices paths, especially for devices that house
	 * an OS. Failure to do this may cause upgrade problems.
	 * Additions for new target devices or transports should not
	 * add scsa binding-set compatible mappings.
	 */
	static struct nodename_aliases {
		char	*na_nodename;		/* nodename */
		char	*na_alias;		/* compatible form match */
	} na[] = {
	/* # mapping to generic nodenames based on scsi dtype */
		{"disk",		"scsiclass,00"},
		{"tape",		"scsiclass,01"},
		{"printer",		"scsiclass,02"},
		{"processor",		"scsiclass,03"},
		{"worm",		"scsiclass,04"},
		{"cdrom",		"scsiclass,05"},
		{"scanner",		"scsiclass,06"},
		{"optical-disk",	"scsiclass,07"},
		{"medium-changer",	"scsiclass,08"},
		{"obsolete",		"scsiclass,09"},
		{"prepress-a",		"scsiclass,0a"},
		{"prepress-b",		"scsiclass,0b"},
		{"array-controller",	"scsiclass,0c"},
		{"enclosure",		"scsiclass,0d"},
		{"disk",		"scsiclass,0e"},
		{"card-reader",		"scsiclass,0f"},
		{"bridge",		"scsiclass,10"},
		{"object-store",	"scsiclass,11"},
		{"reserved",		"scsiclass,12"},
		{"reserved",		"scsiclass,13"},
		{"reserved",		"scsiclass,14"},
		{"reserved",		"scsiclass,15"},
		{"reserved",		"scsiclass,16"},
		{"reserved",		"scsiclass,17"},
		{"reserved",		"scsiclass,18"},
		{"reserved",		"scsiclass,19"},
		{"reserved",		"scsiclass,1a"},
		{"reserved",		"scsiclass,1b"},
		{"reserved",		"scsiclass,1c"},
		{"reserved",		"scsiclass,1d"},
		{"well-known-lun",	"scsiclass,1e"},
		{"unknown",		"scsiclass,1f"},

#ifdef	sparc
	/* # legacy mapping to driver nodenames for fcp binding-set */
		{"ssd",			"scsa,00.bfcp"},
		{"st",			"scsa,01.bfcp"},
		{"sgen",		"scsa,08.bfcp"},
		{"ses",			"scsa,0d.bfcp"},

	/* # legacy mapping to driver nodenames for vhci binding-set */
		{"ssd",			"scsa,00.bvhci"},
		{"st",			"scsa,01.bvhci"},
		{"sgen",		"scsa,08.bvhci"},
		{"ses",			"scsa,0d.bvhci"},
#else	/* sparc */
	/* # for x86 fcp and vhci use generic nodenames */
#endif	/* sparc */

#ifdef  notdef
	/*
	 * The following binding-set specific mappings are not being
	 * delivered at this time, but are listed here as an examples of
	 * the type of mappings needed.
	 */

	/* # legacy mapping to driver nodenames for spi binding-set */
		{"sd",			"scsa,00.bspi"},
		{"sd",			"scsa,05.bspi"},
		{"sd",			"scsa,07.bspi"},
		{"st",			"scsa,01.bspi"},
		{"ses",			"scsa,0d.bspi"},

	/* #				SUN misidentified spi hardware */
		{"ses",			"scsiclass,03.vSUN.pD2"},
		{"ses",			"scsiclass,03.vSYMBIOS.pD1000"},

	/* # legacy mapping to driver nodenames for atapi binding-set */
		{"sd",			"scsa,00.batapi"},
		{"sd",			"scsa,05.batapi"},
		{"sd",			"scsa,07.batapi"},
		{"st",			"scsa,01.batapi"},
		{"unknown",		"scsa,0d.batapi"},

	/* # legacy mapping to generic nodenames for usb binding-set */
		{"disk",		"scsa,05.busb"},
		{"disk",		"scsa,07.busb"},
		{"changer",		"scsa,08.busb"},
		{"comm",		"scsa,09.busb"},
		{"array_ctlr",		"scsa,0c.busb"},
		{"esi",			"scsa,0d.busb"},
#endif  /* notdef */

	/*
	 * mapping nodenames for mpt based on scsi dtype
	 * for being compatible with the original node names
	 * under mpt controller
	 */
		{"sd",			"scsa,00.bmpt"},
		{"sd",			"scsa,05.bmpt"},
		{"sd",			"scsa,07.bmpt"},
		{"st",			"scsa,01.bmpt"},
		{"ses",			"scsa,0d.bmpt"},
		{"sgen",		"scsa,08.bmpt"},
		{NULL,		NULL}
	};
	struct nodename_aliases *nap;

	ASSERT(nodenamep && compatiblep && ncompatiblep &&
	    (binding_set == NULL || (strlen(binding_set) <= 8)));
	if ((nodenamep == NULL) || (compatiblep == NULL) ||
	    (ncompatiblep == NULL))
		return;

	/*
	 * In order to reduce runtime we allocate one block of memory that
	 * contains both the NULL terminated array of pointers to compatible
	 * forms and the individual compatible strings. This block is
	 * somewhat larger than needed, but is short lived - it only exists
	 * until the caller can transfer the information into the "compatible"
	 * string array property and call scsi_hba_nodename_compatible_free.
	 */
	tlen = NCOMPAT * COMPAT_LONGEST;
	compatp = kmem_alloc((NCOMPAT * sizeof (char *)) + tlen, KM_SLEEP);

	/* convert inquiry data from SCSI ASCII to 1275 string */
	(void) string_scsi_to_1275(vid, inq->inq_vid,
	    sizeof (inq->inq_vid));
	(void) string_scsi_to_1275(pid, inq->inq_pid,
	    sizeof (inq->inq_pid));
	(void) string_scsi_to_1275(rev, inq->inq_revision,
	    sizeof (inq->inq_revision));
	ASSERT((strlen(vid) <= sizeof (inq->inq_vid)) &&
	    (strlen(pid) <= sizeof (inq->inq_pid)) &&
	    (strlen(rev) <= sizeof (inq->inq_revision)));

	/*
	 * Form flags in ***ALPHABETICAL*** order within form-group:
	 *
	 * NOTE: When adding a new flag to an existing form-group, carefull
	 * consideration must be given to not breaking existing bindings
	 * based on that form-group.
	 */

	/*
	 * generic form-group flags
	 *   R	removable:
	 *	Set when inq_rmb is set and for well known scsi dtypes. For a
	 *	bus where the entire device is removable (like USB), we expect
	 *	the HBA to intercept the inquiry data and set inq_rmb.
	 *	Since OBP does not distinguish removable media in its generic
	 *	name selection we avoid setting the 'R' flag if the root is not
	 *	yet mounted.
	 */
	i = 0;
	dtype_device = inq->inq_dtype & DTYPE_MASK;
	if (rootvp && (inq->inq_rmb ||
	    (dtype_device == DTYPE_WORM) ||
	    (dtype_device == DTYPE_RODIRECT) ||
	    (dtype_device == DTYPE_OPTICAL)))
		gf[i++] = 'R';			/* removable */
	gf[i] = '\0';

	/*
	 * failover form-group flags
	 *   E	Explicit Target_Port_Group_Supported:
	 *	Set for a device that has a GUID if inq_tpgse also set.
	 *   G	GUID:
	 *	Set when we have identity information, can determine a devid
	 *	from the identity information, and can generate a guid from
	 *	that devid.
	 *   I	Implicit Target_Port_Group_Supported:
	 *	Set for a device that has a GUID if inq_tpgs also set.
	 */
	i = 0;
	if ((inq80 || inq83) &&
	    (ddi_devid_scsi_encode(DEVID_SCSI_ENCODE_VERSION_LATEST, NULL,
	    (uchar_t *)inq, sizeof (*inq), inq80, inq80len, inq83, inq83len,
	    &devid) == DDI_SUCCESS)) {
		guid = ddi_devid_to_guid(devid);
		ddi_devid_free(devid);
	} else
		guid = NULL;
	if (guid && (inq->inq_tpgs & TPGS_FAILOVER_EXPLICIT))
		ff[i++] = 'E';			/* EXPLICIT TPGS */
	if (guid)
		ff[i++] = 'G';			/* GUID */
	if (guid && (inq->inq_tpgs & TPGS_FAILOVER_IMPLICIT))
		ff[i++] = 'I';			/* IMPLICIT TPGS */
	ff[i] = '\0';
	if (guid)
		ddi_devid_free_guid(guid);

	/*
	 * Construct all applicable compatible forms. See comment at the
	 * head of the function for a description of the compatible forms.
	 */
	csp = compatp;
	p = (char *)(compatp + NCOMPAT);

	/* ( 0) driver (optional, not documented in scsi(4)) */
	if (compat0) {
		*csp++ = p;
		(void) snprintf(p, tlen, "%s", compat0);
		len = strlen(p) + 1;
		p += len;
		tlen -= len;
	}

	/* ( 1) scsiclass,DDEEFFF.vV.pP.rR */
	if ((dtype_device != dtype_node) && *gf && *vid && *pid && *rev) {
		*csp++ = p;
		(void) snprintf(p, tlen, "scsiclass,%02x%02x%s.v%s.p%s.r%s",
		    dtype_node, dtype_device, gf, vid, pid, rev);
		len = strlen(p) + 1;
		p += len;
		tlen -= len;
	}

	/* ( 2) scsiclass,DDEE.vV.pP.rR */
	if ((dtype_device != dtype_node) && *vid && *pid && *rev) {
		*csp++ = p;
		(void) snprintf(p, tlen, "scsiclass,%02x%02x.v%s.p%s.r%s",
		    dtype_node, dtype_device, vid, pid, rev);
		len = strlen(p) + 1;
		p += len;
		tlen -= len;
	}

	/* ( 3) scsiclass,DDFFF.vV.pP.rR */
	if (*gf && *vid && *pid && *rev) {
		*csp++ = p;
		(void) snprintf(p, tlen, "scsiclass,%02x%s.v%s.p%s.r%s",
		    dtype_node, gf, vid, pid, rev);
		len = strlen(p) + 1;
		p += len;
		tlen -= len;
	}

	/* ( 4) scsiclass,DD.vV.pP.rR */
	if (*vid && *pid && rev) {
		*csp++ = p;
		(void) snprintf(p, tlen, "scsiclass,%02x.v%s.p%s.r%s",
		    dtype_node, vid, pid, rev);
		len = strlen(p) + 1;
		p += len;
		tlen -= len;
	}

	/* ( 5) scsiclass,DDEEFFF.vV.pP */
	if ((dtype_device != dtype_node) && *gf && *vid && *pid) {
		*csp++ = p;
		(void) snprintf(p, tlen, "scsiclass,%02x%02x%s.v%s.p%s",
		    dtype_node, dtype_device, gf, vid, pid);
		len = strlen(p) + 1;
		p += len;
		tlen -= len;
	}

	/* ( 6) scsiclass,DDEE.vV.pP */
	if ((dtype_device != dtype_node) && *vid && *pid) {
		*csp++ = p;
		(void) snprintf(p, tlen, "scsiclass,%02x%02x.v%s.p%s",
		    dtype_node, dtype_device, vid, pid);
		len = strlen(p) + 1;
		p += len;
		tlen -= len;
	}

	/* ( 7) scsiclass,DDFFF.vV.pP */
	if (*gf && *vid && *pid) {
		*csp++ = p;
		(void) snprintf(p, tlen, "scsiclass,%02x%s.v%s.p%s",
		    dtype_node, gf, vid, pid);
		len = strlen(p) + 1;
		p += len;
		tlen -= len;
	}

	/* ( 8) scsiclass,DD.vV.pP */
	if (*vid && *pid) {
		*csp++ = p;
		(void) snprintf(p, tlen, "scsiclass,%02x.v%s.p%s",
		    dtype_node, vid, pid);
		len = strlen(p) + 1;
		p += len;
		tlen -= len;
	}

	/* (8.5) scsa,DD.bB (not documented in scsi(4)) */
	if (binding_set) {
		*csp++ = p;
		(void) snprintf(p, tlen, "scsa,%02x.b%s",
		    dtype_node, binding_set);
		len = strlen(p) + 1;
		p += len;
		tlen -= len;
	}

	/* ( 9) scsiclass,DDEEFFF */
	if ((dtype_device != dtype_node) && *gf) {
		*csp++ = p;
		(void) snprintf(p, tlen, "scsiclass,%02x%02x%s",
		    dtype_node, dtype_device, gf);
		len = strlen(p) + 1;
		p += len;
		tlen -= len;
	}

	/* (10) scsiclass,DDEE */
	if (dtype_device != dtype_node) {
		*csp++ = p;
		(void) snprintf(p, tlen, "scsiclass,%02x%02x",
		    dtype_node, dtype_device);
		len = strlen(p) + 1;
		p += len;
		tlen -= len;
	}

	/* (11) scsiclass,DDFFF */
	if (*gf) {
		*csp++ = p;
		(void) snprintf(p, tlen, "scsiclass,%02x%s",
		    dtype_node, gf);
		len = strlen(p) + 1;
		p += len;
		tlen -= len;
	}

	/* (12) scsiclass,DD */
	*csp++ = p;
	(void) snprintf(p, tlen, "scsiclass,%02x", dtype_node);
	len = strlen(p) + 1;
	p += len;
	tlen -= len;

	/* (12.5) scsa.fFFF */
	if (*ff) {
		*csp++ = p;
		(void) snprintf(p, tlen, "scsa.f%s", ff);
		len = strlen(p) + 1;
		p += len;
		tlen -= len;
	}

	/* (13) scsiclass */
	*csp++ = p;
	(void) snprintf(p, tlen, "scsiclass");
	len = strlen(p) + 1;
	p += len;
	tlen -= len;
	ASSERT(tlen >= 0);

	*csp = NULL;			/* NULL terminate array of pointers */
	ncompat = csp - compatp;

	/*
	 * When determining a nodename, a nodename_aliases specified
	 * mapping has precedence over using a driver_aliases specified
	 * driver binding as a nodename.
	 *
	 * See if any of the compatible forms have a nodename_aliases
	 * specified nodename. These mappings are described by
	 * nodename_aliases entries like:
	 *
	 *	disk		"scsiclass,00"
	 *	enclosure	"scsiclass,03.vSYMBIOS.pD1000"
	 *	ssd		"scsa,00.bfcp"
	 *
	 * All nodename_aliases mappings should idealy be to generic
	 * names, however a higher precedence legacy mapping to a
	 * driver name may exist. The highest precedence mapping
	 * provides the nodename, so legacy driver nodename mappings
	 * (if they exist) take precedence over generic nodename
	 * mappings.
	 */
	for (nname = NULL, csp = compatp; (nname == NULL) && *csp; csp++) {
		for (nap = na; nap->na_nodename; nap++) {
			if (strcmp(*csp, nap->na_alias) == 0) {
				nname = nap->na_nodename;
				break;
			}
		}
	}

	/*
	 * If no nodename_aliases mapping exists then use the
	 * driver_aliases specified driver binding as a nodename.
	 * Determine the driver based on compatible (which may
	 * have the passed in compat0 as the first item). The
	 * driver_aliases file has entries like
	 *
	 *	sd	"scsiclass,00"
	 *
	 * that map compatible forms to specific drivers. These
	 * entries are established by add_drv. We use the most specific
	 * driver binding as the nodename. This matches the eventual
	 * ddi_driver_compatible_major() binding that will be
	 * established by bind_node()
	 */
	if (nname == NULL) {
		for (dname = NULL, csp = compatp; *csp; csp++) {
			major = ddi_name_to_major(*csp);
			if ((major == (major_t)-1) ||
			    (devnamesp[major].dn_flags & DN_DRIVER_REMOVED))
				continue;
			if (dname = ddi_major_to_name(major))
				break;
		}
		nname = dname;
	}

	/* return results */
	if (nname) {
		*nodenamep = kmem_alloc(strlen(nname) + 1, KM_SLEEP);
		(void) strcpy(*nodenamep, nname);
	} else {
		*nodenamep = NULL;

		/*
		 * If no nodename could be determined return a special
		 * 'compatible' to be used for a diagnostic message. This
		 * compatible contains all compatible forms concatenated
		 * into a single string pointed to by the first element.
		 */
		if (nname == NULL) {
			for (csp = compatp; *(csp + 1); csp++)
				*((*csp) + strlen(*csp)) = ' ';
			*(compatp + 1) = NULL;
			ncompat = 1;
		}

	}
	*compatiblep = compatp;
	*ncompatiblep = ncompat;
}

void
scsi_hba_nodename_compatible_get(struct scsi_inquiry *inq,
    char *binding_set, int dtype_node, char *compat0,
    char **nodenamep, char ***compatiblep, int *ncompatiblep)
{
	scsi_hba_identity_nodename_compatible_get(inq,
	    NULL, 0, NULL, 0, binding_set, dtype_node, compat0, nodenamep,
	    compatiblep, ncompatiblep);
}

/*
 * Free allocations associated with scsi_hba_nodename_compatible_get or
 * scsi_hba_identity_nodename_compatible_get use.
 */
void
scsi_hba_nodename_compatible_free(char *nodename, char **compatible)
{
	if (nodename)
		kmem_free(nodename, strlen(nodename) + 1);

	if (compatible)
		kmem_free(compatible, (NCOMPAT * sizeof (char *)) +
		    (NCOMPAT * COMPAT_LONGEST));
}

/* scsi_device property interfaces */
#define	_TYPE_DEFINED(flags)						\
	(((flags & SCSI_DEVICE_PROP_TYPE_MSK) == SCSI_DEVICE_PROP_PATH) || \
	((flags & SCSI_DEVICE_PROP_TYPE_MSK) == SCSI_DEVICE_PROP_DEVICE))

#define	_DEVICE_PIP(sd, flags)						\
	((((flags & SCSI_DEVICE_PROP_TYPE_MSK) == SCSI_DEVICE_PROP_PATH) && \
	sd->sd_pathinfo) ? (mdi_pathinfo_t *)sd->sd_pathinfo : NULL)

/* return the unit_address associated with a scsi_device */
char *
scsi_device_unit_address(struct scsi_device *sd)
{
	mdi_pathinfo_t	*pip;

	ASSERT(sd && sd->sd_dev);
	if ((sd == NULL) || (sd->sd_dev == NULL))
		return (NULL);

	pip = _DEVICE_PIP(sd, SCSI_DEVICE_PROP_PATH);
	if (pip)
		return (mdi_pi_get_addr(pip));
	else
		return (ddi_get_name_addr(sd->sd_dev));
}

int
scsi_device_prop_get_int(struct scsi_device *sd, uint_t flags,
    char *name, int defval)
{
	mdi_pathinfo_t	*pip;
	int		v = defval;
	int		data;
	int		rv;

	ASSERT(sd && name && sd->sd_dev && _TYPE_DEFINED(flags));
	if ((sd == NULL) || (name == NULL) || (sd->sd_dev == NULL) ||
	    !_TYPE_DEFINED(flags))
		return (v);

	pip = _DEVICE_PIP(sd, flags);
	if (pip) {
		rv = mdi_prop_lookup_int(pip, name, &data);
		if (rv == DDI_PROP_SUCCESS)
			v = data;
	} else
		v = ddi_prop_get_int(DDI_DEV_T_ANY, sd->sd_dev,
		    DDI_PROP_NOTPROM | DDI_PROP_DONTPASS, name, v);
	return (v);
}


int64_t
scsi_device_prop_get_int64(struct scsi_device *sd, uint_t flags,
    char *name, int64_t defval)
{
	mdi_pathinfo_t	*pip;
	int64_t		v = defval;
	int64_t		data;
	int		rv;

	ASSERT(sd && name && sd->sd_dev && _TYPE_DEFINED(flags));
	if ((sd == NULL) || (name == NULL) || (sd->sd_dev == NULL) ||
	    !_TYPE_DEFINED(flags))
		return (v);

	pip = _DEVICE_PIP(sd, flags);
	if (pip) {
		rv = mdi_prop_lookup_int64(pip, name, &data);
		if (rv == DDI_PROP_SUCCESS)
			v = data;
	} else
		v = ddi_prop_get_int64(DDI_DEV_T_ANY, sd->sd_dev,
		    DDI_PROP_NOTPROM | DDI_PROP_DONTPASS, name, v);
	return (v);
}

int
scsi_device_prop_lookup_byte_array(struct scsi_device *sd, uint_t flags,
    char *name, uchar_t **data, uint_t *nelements)
{
	mdi_pathinfo_t	*pip;
	int		rv;

	ASSERT(sd && name && sd->sd_dev && _TYPE_DEFINED(flags));
	if ((sd == NULL) || (name == NULL) || (sd->sd_dev == NULL) ||
	    !_TYPE_DEFINED(flags))
		return (DDI_PROP_INVAL_ARG);

	pip = _DEVICE_PIP(sd, flags);
	if (pip)
		rv = mdi_prop_lookup_byte_array(pip, name, data, nelements);
	else
		rv = ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, sd->sd_dev,
		    DDI_PROP_NOTPROM | DDI_PROP_DONTPASS,
		    name, data, nelements);
	return (rv);
}

int
scsi_device_prop_lookup_int_array(struct scsi_device *sd, uint_t flags,
    char *name, int **data, uint_t *nelements)
{
	mdi_pathinfo_t	*pip;
	int		rv;

	ASSERT(sd && name && sd->sd_dev && _TYPE_DEFINED(flags));
	if ((sd == NULL) || (name == NULL) || (sd->sd_dev == NULL) ||
	    !_TYPE_DEFINED(flags))
		return (DDI_PROP_INVAL_ARG);

	pip = _DEVICE_PIP(sd, flags);
	if (pip)
		rv = mdi_prop_lookup_int_array(pip, name, data, nelements);
	else
		rv = ddi_prop_lookup_int_array(DDI_DEV_T_ANY, sd->sd_dev,
		    DDI_PROP_NOTPROM | DDI_PROP_DONTPASS,
		    name, data, nelements);
	return (rv);
}


int
scsi_device_prop_lookup_string(struct scsi_device *sd, uint_t flags,
    char *name, char **data)
{
	mdi_pathinfo_t	*pip;
	int		rv;

	ASSERT(sd && name && sd->sd_dev && _TYPE_DEFINED(flags));
	if ((sd == NULL) || (name == NULL) || (sd->sd_dev == NULL) ||
	    !_TYPE_DEFINED(flags))
		return (DDI_PROP_INVAL_ARG);

	pip = _DEVICE_PIP(sd, flags);
	if (pip)
		rv = mdi_prop_lookup_string(pip, name, data);
	else
		rv = ddi_prop_lookup_string(DDI_DEV_T_ANY, sd->sd_dev,
		    DDI_PROP_NOTPROM | DDI_PROP_DONTPASS,
		    name, data);
	return (rv);
}

int
scsi_device_prop_lookup_string_array(struct scsi_device *sd, uint_t flags,
    char *name, char ***data, uint_t *nelements)
{
	mdi_pathinfo_t	*pip;
	int		rv;

	ASSERT(sd && name && sd->sd_dev && _TYPE_DEFINED(flags));
	if ((sd == NULL) || (name == NULL) || (sd->sd_dev == NULL) ||
	    !_TYPE_DEFINED(flags))
		return (DDI_PROP_INVAL_ARG);

	pip = _DEVICE_PIP(sd, flags);
	if (pip)
		rv = mdi_prop_lookup_string_array(pip, name, data, nelements);
	else
		rv = ddi_prop_lookup_string_array(DDI_DEV_T_ANY, sd->sd_dev,
		    DDI_PROP_NOTPROM | DDI_PROP_DONTPASS,
		    name, data, nelements);
	return (rv);
}

int
scsi_device_prop_update_byte_array(struct scsi_device *sd, uint_t flags,
    char *name, uchar_t *data, uint_t nelements)
{
	mdi_pathinfo_t	*pip;
	int		rv;

	ASSERT(sd && name && sd->sd_dev && _TYPE_DEFINED(flags));
	if ((sd == NULL) || (name == NULL) || (sd->sd_dev == NULL) ||
	    !_TYPE_DEFINED(flags))
		return (DDI_PROP_INVAL_ARG);

	pip = _DEVICE_PIP(sd, flags);
	if (pip)
		rv = mdi_prop_update_byte_array(pip, name, data, nelements);
	else
		rv = ndi_prop_update_byte_array(DDI_DEV_T_NONE, sd->sd_dev,
		    name, data, nelements);
	return (rv);
}

int
scsi_device_prop_update_int(struct scsi_device *sd, uint_t flags,
    char *name, int data)
{
	mdi_pathinfo_t	*pip;
	int		rv;

	ASSERT(sd && name && sd->sd_dev && _TYPE_DEFINED(flags));
	if ((sd == NULL) || (name == NULL) || (sd->sd_dev == NULL) ||
	    !_TYPE_DEFINED(flags))
		return (DDI_PROP_INVAL_ARG);

	pip = _DEVICE_PIP(sd, flags);
	if (pip)
		rv = mdi_prop_update_int(pip, name, data);
	else
		rv = ndi_prop_update_int(DDI_DEV_T_NONE, sd->sd_dev,
		    name, data);
	return (rv);
}

int
scsi_device_prop_update_int64(struct scsi_device *sd, uint_t flags,
    char *name, int64_t data)
{
	mdi_pathinfo_t	*pip;
	int		rv;

	ASSERT(sd && name && sd->sd_dev && _TYPE_DEFINED(flags));
	if ((sd == NULL) || (name == NULL) || (sd->sd_dev == NULL) ||
	    !_TYPE_DEFINED(flags))
		return (DDI_PROP_INVAL_ARG);

	pip = _DEVICE_PIP(sd, flags);
	if (pip)
		rv = mdi_prop_update_int64(pip, name, data);
	else
		rv = ndi_prop_update_int64(DDI_DEV_T_NONE, sd->sd_dev,
		    name, data);
	return (rv);
}

int
scsi_device_prop_update_int_array(struct scsi_device *sd, uint_t flags,
    char *name, int *data, uint_t nelements)
{
	mdi_pathinfo_t	*pip;
	int		rv;

	ASSERT(sd && name && sd->sd_dev && _TYPE_DEFINED(flags));
	if ((sd == NULL) || (name == NULL) || (sd->sd_dev == NULL) ||
	    !_TYPE_DEFINED(flags))
		return (DDI_PROP_INVAL_ARG);

	pip = _DEVICE_PIP(sd, flags);
	if (pip)
		rv = mdi_prop_update_int_array(pip, name, data, nelements);
	else
		rv = ndi_prop_update_int_array(DDI_DEV_T_NONE, sd->sd_dev,
		    name, data, nelements);
	return (rv);
}

int
scsi_device_prop_update_string(struct scsi_device *sd, uint_t flags,
    char *name, char *data)
{
	mdi_pathinfo_t	*pip;
	int		rv;

	ASSERT(sd && name && sd->sd_dev && _TYPE_DEFINED(flags));
	if ((sd == NULL) || (name == NULL) || (sd->sd_dev == NULL) ||
	    !_TYPE_DEFINED(flags))
		return (DDI_PROP_INVAL_ARG);

	pip = _DEVICE_PIP(sd, flags);
	if (pip)
		rv = mdi_prop_update_string(pip, name, data);
	else
		rv = ndi_prop_update_string(DDI_DEV_T_NONE, sd->sd_dev,
		    name, data);
	return (rv);
}

int
scsi_device_prop_update_string_array(struct scsi_device *sd, uint_t flags,
    char *name, char **data, uint_t nelements)
{
	mdi_pathinfo_t	*pip;
	int		rv;

	ASSERT(sd && name && sd->sd_dev && _TYPE_DEFINED(flags));
	if ((sd == NULL) || (name == NULL) || (sd->sd_dev == NULL) ||
	    !_TYPE_DEFINED(flags))
		return (DDI_PROP_INVAL_ARG);

	pip = _DEVICE_PIP(sd, flags);
	if (pip)
		rv = mdi_prop_update_string_array(pip, name, data, nelements);
	else
		rv = ndi_prop_update_string_array(DDI_DEV_T_NONE, sd->sd_dev,
		    name, data, nelements);
	return (rv);
}

int
scsi_device_prop_remove(struct scsi_device *sd, uint_t flags, char *name)
{
	mdi_pathinfo_t	*pip;
	int		rv;

	ASSERT(sd && name && sd->sd_dev && _TYPE_DEFINED(flags));
	if ((sd == NULL) || (name == NULL) || (sd->sd_dev == NULL) ||
	    !_TYPE_DEFINED(flags))
		return (DDI_PROP_INVAL_ARG);

	pip = _DEVICE_PIP(sd, flags);
	if (pip)
		rv = mdi_prop_remove(pip, name);
	else
		rv = ndi_prop_remove(DDI_DEV_T_NONE, sd->sd_dev, name);
	return (rv);
}

void
scsi_device_prop_free(struct scsi_device *sd, uint_t flags, void *data)
{
	mdi_pathinfo_t	*pip;

	ASSERT(sd && data && sd->sd_dev && _TYPE_DEFINED(flags));
	if ((sd == NULL) || (data == NULL) || (sd->sd_dev == NULL) ||
	    !_TYPE_DEFINED(flags))
		return;

	pip = _DEVICE_PIP(sd, flags);
	if (pip)
		(void) mdi_prop_free(data);
	else
		ddi_prop_free(data);
}

/*ARGSUSED*/
/*
 * Search/create the specified iport node
 */
static dev_info_t *
scsi_hba_bus_config_port(dev_info_t *self, char *nameaddr)
{
	dev_info_t	*child;
	char		*mcompatible, *addr;

	/*
	 * See if the iport node already exists.
	 */

	if (child = ndi_devi_findchild(self, nameaddr)) {
		return (child);
	}

	/* allocate and initialize a new "iport" node */
	ndi_devi_alloc_sleep(self, "iport", DEVI_SID_NODEID, &child);
	ASSERT(child);
	/*
	 * Set the flavor of the child to be IPORT flavored
	 */
	ndi_flavor_set(child, SCSA_FLAVOR_IPORT);

	/*
	 * Add the "scsi-iport" addressing property for this child. This
	 * property is used to identify a iport node, and to represent the
	 * nodes @addr form via node properties.
	 *
	 * Add "compatible" property to the "scsi-iport" node to cause it bind
	 * to the same driver as the HBA  driver.
	 *
	 * Give the HBA a chance, via tran_set_name_prop, to set additional
	 * iport node properties or to change the "compatible" binding
	 * prior to init_child.
	 *
	 * NOTE: the order of these operations is important so that
	 * scsi_hba_iport works when called.
	 */
	mcompatible = ddi_binding_name(self);
	addr = nameaddr + strlen("iport@");

	if ((ndi_prop_update_string(DDI_DEV_T_NONE, child,
	    "scsi-iport", addr) != DDI_PROP_SUCCESS) ||
	    (ndi_prop_update_string_array(DDI_DEV_T_NONE, child,
	    "compatible", &mcompatible, 1) != DDI_PROP_SUCCESS) ||
	    ddi_pathname_obp_set(child, NULL) != DDI_SUCCESS) {
		SCSI_HBA_LOG((_LOG(WARN), self, NULL,
		    "scsi_hba_bus_config_port:%s failed dynamic decoration",
		    nameaddr));
		(void) ddi_remove_child(child, 0);
		child = NULL;
	} else {
		if (ddi_initchild(self, child) != DDI_SUCCESS) {
			ndi_prop_remove_all(child);
			(void) ndi_devi_free(child);
			child = NULL;
		}
	}

	return (child);
}

#ifdef	sparc
/* ARGSUSED */
static int
scsi_hba_bus_config_prom_node(dev_info_t *self, uint_t flags,
    void *arg, dev_info_t **childp)
{
	char		**iports;
	int		circ, i;
	int		ret = NDI_FAILURE;
	unsigned int	num_iports = 0;
	dev_info_t	*pdip = NULL;
	char		*addr = NULL;

	/* check to see if this is an HBA that defined scsi iports */
	ret = ddi_prop_lookup_string_array(DDI_DEV_T_ANY, self,
	    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM, "scsi-iports", &iports,
	    &num_iports);

	if (ret != DDI_SUCCESS) {
		return (ret);
	}

	ASSERT(num_iports > 0);

	addr = kmem_zalloc(SCSI_MAXNAMELEN, KM_SLEEP);

	ret = NDI_FAILURE;

	ndi_devi_enter(self, &circ);

	/* create iport nodes for each scsi port/bus */
	for (i = 0; i < num_iports; i++) {
		bzero(addr, SCSI_MAXNAMELEN);
		/* Prepend the iport name */
		(void) snprintf(addr, SCSI_MAXNAMELEN, "iport@%s",
		    iports[i]);
		if (pdip = scsi_hba_bus_config_port(self, addr)) {
			if (ndi_busop_bus_config(self, NDI_NO_EVENT,
			    BUS_CONFIG_ONE, addr, &pdip, 0) !=
			    NDI_SUCCESS) {
				continue;
			}
			/*
			 * Try to configure child under iport see wehter
			 * request node is the child of the iport node
			 */
			if (ndi_devi_config_one(pdip, arg, childp,
			    NDI_NO_EVENT) == NDI_SUCCESS) {
				ret = NDI_SUCCESS;
				break;
			}
		}
	}

	ndi_devi_exit(self, circ);

	kmem_free(addr, SCSI_MAXNAMELEN);

	ddi_prop_free(iports);

	return (ret);
}
#endif

/*
 * Perform iport port/bus bus_config.
 */
static int
scsi_hba_bus_config_iports(dev_info_t *self, uint_t flags,
    ddi_bus_config_op_t op, void *arg, dev_info_t **childp)
{
	char		*nameaddr, *addr;
	char		**iports;
	int		circ, i;
	int		ret = NDI_FAILURE;
	unsigned int	num_iports = 0;

	/* check to see if this is an HBA that defined scsi iports */
	ret = ddi_prop_lookup_string_array(DDI_DEV_T_ANY, self,
	    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM, "scsi-iports", &iports,
	    &num_iports);

	if (ret != DDI_SUCCESS) {
		return (ret);
	}

	ASSERT(num_iports > 0);

	ndi_devi_enter(self, &circ);

	switch (op) {
	case BUS_CONFIG_ONE:
		/* return if this operation is not against an iport node */
		nameaddr = (char *)arg;
		if ((nameaddr == NULL) ||
		    (strncmp(nameaddr, "iport@", strlen("iport@")) != 0)) {
			ret = NDI_FAILURE;
			ndi_devi_exit(self, circ);
			return (ret);
		}

		/*
		 * parse the port number from "iport@%x"
		 * XXX use atoi (hex)
		 */
		addr = nameaddr + strlen("iport@");

		/* check to see if this port was registered */
		for (i = 0; i < num_iports; i++) {
			if (strcmp((iports[i]), addr) == 0)
				break;
		}

		if (i == num_iports) {
			ret = NDI_FAILURE;
			break;
		}

		/* create the iport node */
		if (scsi_hba_bus_config_port(self, nameaddr)) {
			ret = NDI_SUCCESS;
		}
		break;
	case BUS_CONFIG_ALL:
	case BUS_CONFIG_DRIVER:
		addr = kmem_zalloc(SCSI_MAXNAMELEN, KM_SLEEP);
		/* create iport nodes for each scsi port/bus */
		for (i = 0; i < num_iports; i++) {
			bzero(addr, SCSI_MAXNAMELEN);
			/* Prepend the iport name */
			(void) snprintf(addr, SCSI_MAXNAMELEN, "iport@%s",
			    iports[i]);
			(void) scsi_hba_bus_config_port(self, addr);
		}

		kmem_free(addr, SCSI_MAXNAMELEN);
		ret = NDI_SUCCESS;
		break;
	}
	if (ret == NDI_SUCCESS) {
#ifdef sparc
		/*
		 * Mask NDI_PROMNAME since PROM doesn't have iport
		 * node at all.
		 */
		flags &= (~NDI_PROMNAME);
#endif
		ret = ndi_busop_bus_config(self, flags, op,
		    arg, childp, 0);
	}
	ndi_devi_exit(self, circ);

	ddi_prop_free(iports);

	return (ret);
}


/*ARGSUSED*/
static int
scsi_hba_bus_config(dev_info_t *self, uint_t flag, ddi_bus_config_op_t op,
    void *arg, dev_info_t **childp)
{
	scsi_hba_tran_t	*tran = NULL;

	tran = ddi_get_driver_private(self);

	if (tran && (tran->tran_hba_flags & SCSI_HBA_HBA)) {
#ifdef	sparc
		char *nameaddr = NULL;
		nameaddr = (char *)arg;
		switch (op) {
		case BUS_CONFIG_ONE:
			if (nameaddr == NULL)
				return (NDI_FAILURE);
			if (strncmp(nameaddr, "iport", strlen("iport")) == 0) {
				break;
			}
			/*
			 * If this operation is not against an iport node, it's
			 * possible the operation is requested to configure
			 * root disk by OBP. Unfortunately, prom path is without
			 * iport string in the boot path.
			 */
			if (strncmp(nameaddr, "disk@", strlen("disk@")) == 0) {
				return (scsi_hba_bus_config_prom_node(self,
				    flag, arg, childp));
			}
			break;
		default:
			break;
		}
#endif
		/*
		 * The request is to configure multi-port HBA.
		 * Now start to configure iports, for the end
		 * devices attached to iport, should be configured
		 * by bus_configure routine of iport
		 */
		return (scsi_hba_bus_config_iports(self, flag, op, arg,
		    childp));
	}

#ifdef sparc
	if (scsi_hba_iport_unit_address(self)) {
		flag &= (~NDI_PROMNAME);
	}
#endif
	if (tran && tran->tran_bus_config) {
		return (tran->tran_bus_config(self, flag, op, arg, childp));
	}

	/*
	 * Force reprobe for BUS_CONFIG_ONE or when manually reconfiguring
	 * via devfsadm(1m) to emulate deferred attach.
	 * Reprobe only discovers driver.conf enumerated nodes, more
	 * dynamic implementations probably require their own bus_config.
	 */
	if ((op == BUS_CONFIG_ONE) || (flag & NDI_DRV_CONF_REPROBE))
		flag |= NDI_CONFIG_REPROBE;

	return (ndi_busop_bus_config(self, flag, op, arg, childp, 0));
}

static int
scsi_hba_bus_unconfig(dev_info_t *self, uint_t flag, ddi_bus_config_op_t op,
    void *arg)
{
	scsi_hba_tran_t	*tran = NULL;

	tran = ddi_get_driver_private(self);

	if (tran && tran->tran_bus_unconfig) {
		return (tran->tran_bus_unconfig(self, flag, op, arg));
	}
	return (ndi_busop_bus_unconfig(self, flag, op, arg));
}

void
scsi_hba_pkt_comp(struct scsi_pkt *pkt)
{
	ASSERT(pkt);
	if (pkt->pkt_comp == NULL)
		return;

	/*
	 * For HBA drivers that implement tran_setup_pkt(9E), if we are
	 * completing a 'consistent' mode DMA operation then we must
	 * perform dma_sync prior to calling pkt_comp to ensure that
	 * the target driver sees the correct data in memory.
	 */
	ASSERT((pkt->pkt_flags & FLAG_NOINTR) == 0);
	if (((pkt->pkt_dma_flags & DDI_DMA_CONSISTENT) &&
	    (pkt->pkt_dma_flags & DDI_DMA_READ)) &&
	    ((P_TO_TRAN(pkt)->tran_setup_pkt) != NULL)) {
		scsi_sync_pkt(pkt);
	}
	(*pkt->pkt_comp)(pkt);
}
