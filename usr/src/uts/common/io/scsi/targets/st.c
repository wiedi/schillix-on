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

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * SCSI	 SCSA-compliant and not-so-DDI-compliant Tape Driver
 */

#if defined(lint) && !defined(DEBUG)
#define	DEBUG	1
#endif

#include <sys/modctl.h>
#include <sys/scsi/scsi.h>
#include <sys/mtio.h>
#include <sys/scsi/targets/stdef.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/kstat.h>
#include <sys/ddidmareq.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#define	IOSP	KSTAT_IO_PTR(un->un_stats)
/*
 * stats maintained only for reads/writes as commands
 * like rewind etc skew the wait/busy times
 */
#define	IS_RW(bp) 	((bp)->b_bcount > 0)
#define	ST_DO_KSTATS(bp, kstat_function) \
	if ((bp != un->un_sbufp) && un->un_stats && IS_RW(bp)) { \
		kstat_function(IOSP); \
	}

#define	ST_DO_ERRSTATS(un, x)  \
	if (un->un_errstats) { \
		struct st_errstats *stp; \
		stp = (struct st_errstats *)un->un_errstats->ks_data; \
		stp->x.value.ul++; \
	}

#define	FILL_SCSI1_LUN(devp, pkt) 					\
	if ((devp)->sd_inq->inq_ansi == 0x1) {				\
		int _lun;						\
		_lun = ddi_prop_get_int(DDI_DEV_T_ANY, (devp)->sd_dev,	\
		    DDI_PROP_DONTPASS, SCSI_ADDR_PROP_LUN, 0);		\
		if (_lun > 0) {						\
			((union scsi_cdb *)(pkt)->pkt_cdbp)->scc_lun =	\
			    _lun;					\
		}							\
	}

/*
 * get an available contig mem header, cp.
 * when big_enough is true, we will return NULL, if no big enough
 * contig mem is found.
 * when big_enough is false, we will try to find cp containing big
 * enough contig mem. if not found, we will ruturn the last cp available.
 *
 * used by st_get_contig_mem()
 */
#define	ST_GET_CONTIG_MEM_HEAD(un, cp, len, big_enough) {		\
	struct contig_mem *tmp_cp = NULL;				\
	for ((cp) = (un)->un_contig_mem;				\
	    (cp) != NULL;						\
	    tmp_cp = (cp), (cp) = (cp)->cm_next) { 			\
		if (((cp)->cm_len >= (len)) || 				\
		    (!(big_enough) && ((cp)->cm_next == NULL))) { 	\
			if (tmp_cp == NULL) { 				\
				(un)->un_contig_mem = (cp)->cm_next; 	\
			} else { 					\
				tmp_cp->cm_next = (cp)->cm_next; 	\
			} 						\
			(cp)->cm_next = NULL; 				\
			(un)->un_contig_mem_available_num--; 		\
			break; 						\
		} 							\
	} 								\
}

#define	ST_NUM_MEMBERS(array)	(sizeof (array) / sizeof (array[0]))

/*
 * Global External Data Definitions
 */
extern struct scsi_key_strings scsi_cmds[];
extern uchar_t	scsi_cdb_size[];

/*
 * Local Static Data
 */
static void *st_state;
static char *st_label = "st";

#if defined(__i386) || defined(__amd64)
/*
 * We need to use below DMA attr to alloc physically contiguous
 * memory to do I/O in big block size
 */
static ddi_dma_attr_t st_contig_mem_dma_attr = {
	DMA_ATTR_V0,    /* version number */
	0x0,		/* lowest usable address */
	0xFFFFFFFFull,  /* high DMA address range */
	0xFFFFFFFFull,  /* DMA counter register */
	1,		/* DMA address alignment */
	1,		/* DMA burstsizes */
	1,		/* min effective DMA size */
	0xFFFFFFFFull,  /* max DMA xfer size */
	0xFFFFFFFFull,  /* segment boundary */
	1,		/* s/g list length */
	1,		/* granularity of device */
	0		/* DMA transfer flags */
};

static ddi_device_acc_attr_t st_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC
};

/* set limitation for the number of contig_mem */
static int st_max_contig_mem_num = ST_MAX_CONTIG_MEM_NUM;
#endif

/*
 * Tunable parameters
 *
 * DISCLAIMER
 * ----------
 * These parameters are intended for use only in system testing; if you use
 * them in production systems, you do so at your own risk. Altering any
 * variable not listed below may cause unpredictable system behavior.
 *
 * st_check_media_time
 *
 *   Three second state check
 *
 * st_allow_large_xfer
 *
 *   Gated with ST_NO_RECSIZE_LIMIT
 *
 *   0 - Transfers larger than 64KB will not be allowed
 *       regardless of the setting of ST_NO_RECSIZE_LIMIT
 *   1 - Transfers larger than 64KB will be allowed
 *       if ST_NO_RECSIZE_LIMIT is TRUE for the drive
 *
 * st_report_soft_errors_on_close
 *
 *  Gated with ST_SOFT_ERROR_REPORTING
 *
 *  0 - Errors will not be reported on close regardless
 *      of the setting of ST_SOFT_ERROR_REPORTING
 *
 *  1 - Errors will be reported on close if
 *      ST_SOFT_ERROR_REPORTING is TRUE for the drive
 */
static int st_selection_retry_count = ST_SEL_RETRY_COUNT;
static int st_retry_count	= ST_RETRY_COUNT;

static int st_io_time		= ST_IO_TIME;
static int st_long_timeout_x	= ST_LONG_TIMEOUT_X;

static int st_space_time	= ST_SPACE_TIME;
static int st_long_space_time_x	= ST_LONG_SPACE_TIME_X;

static int st_error_level	= SCSI_ERR_RETRYABLE;
static int st_check_media_time	= 3000000;	/* 3 Second State Check */

static int st_max_throttle	= ST_MAX_THROTTLE;

static clock_t st_wait_cmds_complete = ST_WAIT_CMDS_COMPLETE;

static int st_allow_large_xfer = 1;
static int st_report_soft_errors_on_close = 1;

/*
 * End of tunable parameters list
 */



/*
 * Asynchronous I/O and persistent errors, refer to PSARC/1995/228
 *
 * Asynchronous I/O's main offering is that it is a non-blocking way to do
 * reads and writes.  The driver will queue up all the requests it gets and
 * have them ready to transport to the HBA.  Unfortunately, we cannot always
 * just ship the I/O requests to the HBA, as there errors and exceptions
 * that may happen when we don't want the HBA to continue.  Therein comes
 * the flush-on-errors capability.  If the HBA supports it, then st will
 * send in st_max_throttle I/O requests at the same time.
 *
 * Persistent errors : This was also reasonably simple.  In the interrupt
 * routines, if there was an error or exception (FM, LEOT, media error,
 * transport error), the persistent error bits are set and shuts everything
 * down, but setting the throttle to zero.  If we hit and exception in the
 * HBA, and flush-on-errors were set, we wait for all outstanding I/O's to
 * come back (with CMD_ABORTED), then flush all bp's in the wait queue with
 * the appropriate error, and this will preserve order. Of course, depending
 * on the exception we have to show a zero read or write before we show
 * errors back to the application.
 */

extern const int st_ndrivetypes;	/* defined in st_conf.c */
extern const struct st_drivetype st_drivetypes[];

#ifdef STDEBUG
static int st_soft_error_report_debug = 0;
volatile int st_debug = 0;
#endif

#define	ST_MT02_NAME	"Emulex  MT02 QIC-11/24  "

static const struct driver_minor_data {
	char	*name;
	int	minor;
} st_minor_data[] = {
	/*
	 * The top 4 entries are for the default densities,
	 * don't alter their position.
	 */
	{"",	0},
	{"n",	MT_NOREWIND},
	{"b",	MT_BSD},
	{"bn",	MT_NOREWIND | MT_BSD},
	{"l",	MT_DENSITY1},
	{"m",	MT_DENSITY2},
	{"h",	MT_DENSITY3},
	{"c",	MT_DENSITY4},
	{"u",	MT_DENSITY4},
	{"ln",	MT_DENSITY1 | MT_NOREWIND},
	{"mn",	MT_DENSITY2 | MT_NOREWIND},
	{"hn",	MT_DENSITY3 | MT_NOREWIND},
	{"cn",	MT_DENSITY4 | MT_NOREWIND},
	{"un",	MT_DENSITY4 | MT_NOREWIND},
	{"lb",	MT_DENSITY1 | MT_BSD},
	{"mb",	MT_DENSITY2 | MT_BSD},
	{"hb",	MT_DENSITY3 | MT_BSD},
	{"cb",	MT_DENSITY4 | MT_BSD},
	{"ub",	MT_DENSITY4 | MT_BSD},
	{"lbn",	MT_DENSITY1 | MT_NOREWIND | MT_BSD},
	{"mbn",	MT_DENSITY2 | MT_NOREWIND | MT_BSD},
	{"hbn",	MT_DENSITY3 | MT_NOREWIND | MT_BSD},
	{"cbn",	MT_DENSITY4 | MT_NOREWIND | MT_BSD},
	{"ubn",	MT_DENSITY4 | MT_NOREWIND | MT_BSD}
};

/* strings used in many debug and warning messages */
static const char wr_str[]  = "write";
static const char rd_str[]  = "read";
static const char wrg_str[] = "writing";
static const char rdg_str[] = "reading";

/* default density offsets in the table above */
#define	DEF_BLANK	0
#define	DEF_NOREWIND	1
#define	DEF_BSD		2
#define	DEF_BSD_NR	3

/* Sense Key, ASC/ASCQ for which tape ejection is needed */

static struct tape_failure_code {
	uchar_t key;
	uchar_t add_code;
	uchar_t qual_code;
} st_tape_failure_code[] = {
	{ KEY_HARDWARE_ERROR, 0x15, 0x01},
	{ KEY_HARDWARE_ERROR, 0x44, 0x00},
	{ KEY_HARDWARE_ERROR, 0x53, 0x00},
	{ KEY_HARDWARE_ERROR, 0x53, 0x01},
	{ KEY_NOT_READY, 0x53, 0x00},
	{ 0xff}
};

/*  clean bit position and mask */

static struct cln_bit_position {
	ushort_t cln_bit_byte;
	uchar_t cln_bit_mask;
} st_cln_bit_position[] = {
	{ 21, 0x08},
	{ 70, 0xc0},
	{ 18, 0x81}  /* 80 bit indicates in bit mode, 1 bit clean light is on */
};

/*
 * architecture dependent allocation restrictions. For x86, we'll set
 * dma_attr_addr_hi to st_max_phys_addr and dma_attr_sgllen to
 * st_sgl_size during _init().
 */
#if defined(__sparc)
static ddi_dma_attr_t st_alloc_attr = {
	DMA_ATTR_V0,	/* version number */
	0x0,		/* lowest usable address */
	0xFFFFFFFFull,	/* high DMA address range */
	0xFFFFFFFFull,	/* DMA counter register */
	1,		/* DMA address alignment */
	1,		/* DMA burstsizes */
	1,		/* min effective DMA size */
	0xFFFFFFFFull,	/* max DMA xfer size */
	0xFFFFFFFFull,	/* segment boundary */
	1,		/* s/g list length */
	512,		/* granularity of device */
	0		/* DMA transfer flags */
};
#elif defined(__x86)
static ddi_dma_attr_t st_alloc_attr = {
	DMA_ATTR_V0,	/* version number */
	0x0,		/* lowest usable address */
	0x0,		/* high DMA address range [set in _init()] */
	0xFFFFull,	/* DMA counter register */
	512,		/* DMA address alignment */
	1,		/* DMA burstsizes */
	1,		/* min effective DMA size */
	0xFFFFFFFFull,	/* max DMA xfer size */
	0xFFFFFFFFull,  /* segment boundary */
	0,		/* s/g list length */
	512,		/* granularity of device [set in _init()] */
	0		/* DMA transfer flags */
};
uint64_t st_max_phys_addr = 0xFFFFFFFFull;
int st_sgl_size = 0xF;

#endif

/*
 * Configuration Data:
 *
 * Device driver ops vector
 */
static int st_aread(dev_t dev, struct aio_req *aio, cred_t *cred_p);
static int st_awrite(dev_t dev, struct aio_req *aio, cred_t *cred_p);
static int st_read(dev_t  dev,  struct   uio   *uio_p,   cred_t *cred_p);
static int st_write(dev_t  dev,  struct  uio   *uio_p,   cred_t *cred_p);
static int st_open(dev_t  *devp,  int  flag,  int  otyp,  cred_t *cred_p);
static int st_close(dev_t  dev,  int  flag,  int  otyp,  cred_t *cred_p);
static int st_strategy(struct buf *bp);
static int st_ioctl(dev_t dev, int cmd, intptr_t arg, int  flag,
	cred_t *cred_p, int *rval_p);
extern int nulldev(), nodev();

static struct cb_ops st_cb_ops = {
	st_open,			/* open */
	st_close,		/* close */
	st_strategy,		/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	st_read,		/* read */
	st_write,		/* write */
	st_ioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* cb_prop_op */
	0,			/* streamtab  */
	D_64BIT | D_MP | D_NEW | D_HOTPLUG,	/* Driver compatibility flag */
	CB_REV,			/* cb_rev */
	st_aread, 		/* async I/O read entry point */
	st_awrite		/* async I/O write entry point */

};

static int stinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **result);
static int st_probe(dev_info_t *dev);
static int st_attach(dev_info_t *dev, ddi_attach_cmd_t cmd);
static int st_detach(dev_info_t *dev, ddi_detach_cmd_t cmd);

static struct dev_ops st_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	stinfo,			/* info */
	nulldev,		/* identify */
	st_probe,		/* probe */
	st_attach,		/* attach */
	st_detach,		/* detach */
	nodev,			/* reset */
	&st_cb_ops,		/* driver operations */
	(struct bus_ops *)0,	/* bus operations */
	nulldev			/* power */
};

/*
 * Local Function Declarations
 */
static void st_clean_print(dev_info_t *dev, char *label, uint_t level,
	char *title, char *data, int len);
static int st_doattach(struct scsi_device *devp, int (*canwait)());
static void st_known_tape_type(struct scsi_tape *un);
static int st_get_conf_from_st_dot_conf(struct scsi_tape *, char *,
    struct st_drivetype *);
static int st_get_conf_from_st_conf_dot_c(struct scsi_tape *, char *,
    struct st_drivetype *);
static int st_get_default_conf(struct scsi_tape *, char *,
    struct st_drivetype *);
static int st_rw(dev_t dev, struct uio *uio, int flag);
static int st_arw(dev_t dev, struct aio_req *aio, int flag);
static int st_find_eom(dev_t dev);
static int st_check_density_or_wfm(dev_t dev, int wfm, int mode, int stepflag);
static int st_ioctl_cmd(dev_t dev, struct uscsi_cmd *,
	enum uio_seg, enum uio_seg, enum uio_seg);
static int st_mtioctop(struct scsi_tape *un, intptr_t arg, int flag);
static void st_start(struct scsi_tape *un);
static int st_handle_start_busy(struct scsi_tape *un, struct buf *bp,
    clock_t timeout_interval);
static int st_handle_intr_busy(struct scsi_tape *un, struct buf *bp,
    clock_t timeout_interval);
static int st_handle_intr_retry_lcmd(struct scsi_tape *un, struct buf *bp);
static void st_done_and_mutex_exit(struct scsi_tape *un, struct buf *bp);
static void st_init(struct scsi_tape *un);
static void st_make_cmd(struct scsi_tape *un, struct buf *bp,
    int (*func)(caddr_t));
static void st_make_uscsi_cmd(struct scsi_tape *, struct uscsi_cmd *,
    struct buf *bp, int (*func)(caddr_t));
static void st_intr(struct scsi_pkt *pkt);
static void st_set_state(struct scsi_tape *un);
static void st_test_append(struct buf *bp);
static int st_runout(caddr_t);
static int st_cmd(dev_t dev, int com, int count, int wait);
static int st_set_compression(struct scsi_tape *un);
static int st_write_fm(dev_t dev, int wfm);
static int st_determine_generic(dev_t dev);
static int st_determine_density(dev_t dev, int rw);
static int st_get_density(dev_t dev);
static int st_set_density(dev_t dev);
static int st_loadtape(dev_t dev);
static int st_modesense(struct scsi_tape *un);
static int st_modeselect(struct scsi_tape *un);
static int st_handle_incomplete(struct scsi_tape *un, struct buf *bp);
static int st_wrongtapetype(struct scsi_tape *un);
static int st_check_error(struct scsi_tape *un, struct scsi_pkt *pkt);
static int st_handle_sense(struct scsi_tape *un, struct buf *bp);
static int st_handle_autosense(struct scsi_tape *un, struct buf *bp);
static int st_decode_sense(struct scsi_tape *un, struct buf *bp, int amt,
	struct scsi_status *);
static int st_report_soft_errors(dev_t dev, int flag);
static void st_delayed_cv_broadcast(void *arg);
static int st_check_media(dev_t dev, enum mtio_state state);
static int st_media_watch_cb(caddr_t arg, struct scsi_watch_result *resultp);
static void st_intr_restart(void *arg);
static void st_start_restart(void *arg);
static int st_gen_mode_sense(struct scsi_tape *un, int page,
    struct seq_mode *page_data, int page_size);
static int st_change_block_size(dev_t dev, uint32_t nblksz);
static int st_gen_mode_select(struct scsi_tape *un, struct seq_mode *page_data,
    int page_size);
static int st_tape_init(dev_t dev);
static void st_flush(struct scsi_tape *un);
static void st_set_pe_errno(struct scsi_tape *un);
static void st_hba_unflush(struct scsi_tape *un);
static void st_turn_pe_on(struct scsi_tape *un);
static void st_turn_pe_off(struct scsi_tape *un);
static void st_set_pe_flag(struct scsi_tape *un);
static void st_clear_pe(struct scsi_tape *un);
static void st_wait_for_io(struct scsi_tape *un);
static int st_set_devconfig_page(struct scsi_tape *un, int compression_on);
static int st_set_datacomp_page(struct scsi_tape *un, int compression_on);
static int st_reserve_release(struct scsi_tape *un, int command);
static int st_check_cdb_for_need_to_reserve(struct scsi_tape *un, caddr_t cdb);
static int st_check_cmd_for_need_to_reserve(struct scsi_tape *un, uchar_t cmd,
    int count);
static int st_take_ownership(dev_t dev);
static int st_check_asc_ascq(struct scsi_tape *un);
static int st_check_clean_bit(dev_t dev);
static int st_check_alert_flags(dev_t dev);
static int st_check_sequential_clean_bit(dev_t dev);
static int st_check_sense_clean_bit(dev_t dev);
static int st_clear_unit_attentions(dev_t dev_instance, int max_trys);
static void st_calculate_timeouts(struct scsi_tape *un);
static writablity st_is_drive_worm(struct scsi_tape *un);
static int st_read_attributes(struct scsi_tape *un, uint16_t attribute,
    caddr_t buf, size_t size);
static int st_get_special_inquiry(struct scsi_tape *un, uchar_t size,
    caddr_t dest, uchar_t page);

#if defined(__i386) || defined(__amd64)
/*
 * routines for I/O in big block size
 */
static void st_release_contig_mem(struct scsi_tape *un, struct contig_mem *cp);
static struct contig_mem *st_get_contig_mem(struct scsi_tape *un, size_t len,
    int alloc_flags);
static int st_bigblk_xfer_done(struct buf *bp);
static struct buf *st_get_bigblk_bp(struct buf *bp);
#endif

/*
 * error statistics create/update functions
 */
static int st_create_errstats(struct scsi_tape *, int);
static void st_uscsi_minphys(struct buf *bp);
static int st_validate_tapemarks(struct scsi_tape *un, int fileno, daddr_t bn);

#ifdef STDEBUG
static void st_debug_cmds(struct scsi_tape *un, int com, int count, int wait);
static char *st_dev_name(dev_t dev);
#endif /* STDEBUG */

#if !defined(lint)
_NOTE(SCHEME_PROTECTS_DATA("unique per pkt", scsi_pkt buf uio scsi_cdb))
_NOTE(SCHEME_PROTECTS_DATA("unique per pkt", scsi_extended_sense scsi_status))
_NOTE(SCHEME_PROTECTS_DATA("stable data", scsi_device))
_NOTE(DATA_READABLE_WITHOUT_LOCK(st_drivetype scsi_address))
#endif

/*
 * autoconfiguration routines.
 */
char _depends_on[] = "misc/scsi";

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module. This one is a driver */
	"SCSI tape Driver %I%", /* Name of the module. */
	&st_ops			/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};

/*
 * Notes on Post Reset Behavior in the tape driver:
 *
 * When the tape drive is opened, the driver  attempts  to make sure that
 * the tape head is positioned exactly where it was left when it was last
 * closed  provided  the  medium  is not  changed.  If the tape  drive is
 * opened in O_NDELAY mode, the repositioning  (if necessary for any loss
 * of position due to reset) will happen when the first tape operation or
 * I/O occurs.  The repositioning (if required) may not be possible under
 * certain situations such as when the device firmware not able to report
 * the medium  change in the REQUEST  SENSE data  because of a reset or a
 * misbehaving  bus  not  allowing  the  reposition  to  happen.  In such
 * extraordinary  situations, where the driver fails to position the head
 * at its  original  position,  it will fail the open the first  time, to
 * save the applications from overwriting the data.  All further attempts
 * to open the tape device will result in the driver  attempting  to load
 * the  tape at BOT  (beginning  of  tape).  Also a  warning  message  to
 * indicate  that further  attempts to open the tape device may result in
 * the tape being  loaded at BOT will be printed on the  console.  If the
 * tape  device is opened  in  O_NDELAY  mode,  failure  to  restore  the
 * original tape head  position,  will result in the failure of the first
 * tape  operation  or I/O,  Further,  the  driver  will  invalidate  its
 * internal tape position  which will  necessitate  the  applications  to
 * validate the position by using either a tape  positioning  ioctl (such
 * as MTREW) or closing and reopening the tape device.
 *
 */

int
_init(void)
{
	int e;

	if (((e = ddi_soft_state_init(&st_state,
	    sizeof (struct scsi_tape), ST_MAXUNIT)) != 0)) {
		return (e);
	}

	if ((e = mod_install(&modlinkage)) != 0) {
		ddi_soft_state_fini(&st_state);
	}

#if defined(__x86)
	/* set the max physical address for iob allocs on x86 */
	st_alloc_attr.dma_attr_addr_hi = st_max_phys_addr;

	/*
	 * set the sgllen for iob allocs on x86. If this is set less than
	 * the number of pages the buffer will take (taking into account
	 * alignment), it would force the allocator to try and allocate
	 * contiguous pages.
	 */
	st_alloc_attr.dma_attr_sgllen = st_sgl_size;
#endif

	return (e);
}

int
_fini(void)
{
	int e;

	if ((e = mod_remove(&modlinkage)) != 0) {
		return (e);
	}

	ddi_soft_state_fini(&st_state);

	return (e);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


static int
st_probe(dev_info_t *devi)
{
	int instance;
	struct scsi_device *devp;
	int rval;

#if !defined(__sparc)
	char    *tape_prop;
	int	tape_prop_len;
#endif

	/* If self identifying device */
	if (ddi_dev_is_sid(devi) == DDI_SUCCESS) {
		return (DDI_PROBE_DONTCARE);
	}

#if !defined(__sparc)
	/*
	 * Since some x86 HBAs have devnodes that look like SCSI as
	 * far as we can tell but aren't really SCSI (DADK, like mlx)
	 * we check for the presence of the "tape" property.
	 */
	if (ddi_prop_op(DDI_DEV_T_NONE, devi, PROP_LEN_AND_VAL_ALLOC,
	    DDI_PROP_CANSLEEP, "tape",
	    (caddr_t)&tape_prop, &tape_prop_len) != DDI_PROP_SUCCESS) {
		return (DDI_PROBE_FAILURE);
	}
	if (strncmp(tape_prop, "sctp", tape_prop_len) != 0) {
		kmem_free(tape_prop, tape_prop_len);
		return (DDI_PROBE_FAILURE);
	}
	kmem_free(tape_prop, tape_prop_len);
#endif

	devp = ddi_get_driver_private(devi);
	instance = ddi_get_instance(devi);

	if (ddi_get_soft_state(st_state, instance) != NULL) {
		return (DDI_PROBE_PARTIAL);
	}


	/*
	 * Turn around and call probe routine to see whether
	 * we actually have a tape at this SCSI nexus.
	 */
	if (scsi_probe(devp, NULL_FUNC) == SCSIPROBE_EXISTS) {

		/*
		 * In checking the whole inq_dtype byte we are looking at both
		 * the Peripheral Qualifier and the Peripheral Device Type.
		 * For this driver we are only interested in sequential devices
		 * that are connected or capable if connecting to this logical
		 * unit.
		 */
		if (devp->sd_inq->inq_dtype ==
		    (DTYPE_SEQUENTIAL | DPQ_POSSIBLE)) {
			ST_DEBUG6(devi, st_label, SCSI_DEBUG,
			    "probe exists\n");
			rval = DDI_PROBE_SUCCESS;
		} else {
			rval = DDI_PROBE_FAILURE;
		}
	} else {
		ST_DEBUG6(devi, st_label, SCSI_DEBUG,
		    "probe failure: nothing there\n");
		rval = DDI_PROBE_FAILURE;
	}
	scsi_unprobe(devp);
	return (rval);
}

static int
st_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	int 	instance;
	int	wide;
	int 	dev_instance;
	int	ret_status;
	struct	scsi_device *devp;
	int	node_ix;
	struct	scsi_tape *un;

	devp = ddi_get_driver_private(devi);
	instance = ddi_get_instance(devi);

	switch (cmd) {
		case DDI_ATTACH:
			if (st_doattach(devp, SLEEP_FUNC) == DDI_FAILURE) {
				return (DDI_FAILURE);
			}
			break;
		case DDI_RESUME:
			/*
			 * Suspend/Resume
			 *
			 * When the driver suspended, there might be
			 * outstanding cmds and therefore we need to
			 * reset the suspended flag and resume the scsi
			 * watch thread and restart commands and timeouts
			 */

			if (!(un = ddi_get_soft_state(st_state, instance))) {
				return (DDI_FAILURE);
			}
			dev_instance = ((un->un_dev == 0) ? MTMINOR(instance) :
			    un->un_dev);

			mutex_enter(ST_MUTEX);

			un->un_throttle = un->un_max_throttle;
			un->un_tids_at_suspend = 0;
			un->un_pwr_mgmt = ST_PWR_NORMAL;

			if (un->un_swr_token) {
				scsi_watch_resume(un->un_swr_token);
			}

			/*
			 * Restart timeouts
			 */
			if ((un->un_tids_at_suspend & ST_DELAY_TID) != 0) {
				mutex_exit(ST_MUTEX);
				un->un_delay_tid =
				    timeout(st_delayed_cv_broadcast, un,
					drv_usectohz((clock_t)
					MEDIA_ACCESS_DELAY));
				mutex_enter(ST_MUTEX);
			}

			if (un->un_tids_at_suspend & ST_HIB_TID) {
				mutex_exit(ST_MUTEX);
				un->un_hib_tid = timeout(st_intr_restart, un,
				    ST_STATUS_BUSY_TIMEOUT);
				mutex_enter(ST_MUTEX);
			}

			ret_status = st_clear_unit_attentions(dev_instance, 5);

			/*
			 * now check if we need to restore the tape position
			 */
			if ((un->un_suspend_fileno > 0) ||
			    (un->un_suspend_blkno > 0)) {
				if (ret_status != 0) {
					/*
					 * tape didn't get good TUR
					 * just print out error messages
					 */
					scsi_log(ST_DEVINFO, st_label, CE_WARN,
					    "st_attach-RESUME: tape failure "
					    " tape position will be lost");
				} else {
					/* this prints errors */
					(void) st_validate_tapemarks(un,
					    un->un_suspend_fileno,
					    un->un_suspend_blkno);
				}
				/*
				 * there are no retries, if there is an error
				 * we don't know if the tape has changed
				 */
				un->un_suspend_fileno = 0;
				un->un_suspend_blkno = 0;
			}

			/* now we are ready to start up any queued I/Os */
			if (un->un_ncmds || un->un_quef) {
				st_start(un);
			}

			cv_broadcast(&un->un_suspend_cv);
			mutex_exit(ST_MUTEX);
			return (DDI_SUCCESS);

		default:
			return (DDI_FAILURE);
	}

	un = ddi_get_soft_state(st_state, instance);

	ST_DEBUG(devi, st_label, SCSI_DEBUG,
	    "st_attach: instance=%x\n", instance);

	/*
	 * find the drive type for this target
	 */
	st_known_tape_type(un);

	for (node_ix = 0; node_ix < ST_NUM_MEMBERS(st_minor_data); node_ix++) {
		int minor;
		char *name;

		name  = st_minor_data[node_ix].name;
		minor = st_minor_data[node_ix].minor;

		/*
		 * For default devices set the density to the
		 * preferred default density for this device.
		 */
		if (node_ix <= DEF_BSD_NR) {
			minor |= un->un_dp->default_density;
		}
		minor |= MTMINOR(instance);

		if (ddi_create_minor_node(devi, name, S_IFCHR, minor,
		    DDI_NT_TAPE, NULL) == DDI_SUCCESS) {
			continue;
		}

		ddi_remove_minor_node(devi, NULL);
		if (un) {
			cv_destroy(&un->un_clscv);
			cv_destroy(&un->un_sbuf_cv);
			cv_destroy(&un->un_queue_cv);
			cv_destroy(&un->un_state_cv);
			cv_destroy(&un->un_suspend_cv);
			cv_destroy(&un->un_tape_busy_cv);

			if (un->un_sbufp) {
				freerbuf(un->un_sbufp);
			}
			if (un->un_uscsi_rqs_buf) {
				kmem_free(un->un_uscsi_rqs_buf, SENSE_LENGTH);
			}
			if (un->un_mspl) {
				i_ddi_mem_free((caddr_t)un->un_mspl, NULL);
			}
			scsi_destroy_pkt(un->un_rqs);
			scsi_free_consistent_buf(un->un_rqs_bp);
			ddi_soft_state_free(st_state, instance);
			devp->sd_private = NULL;
			devp->sd_sense = NULL;

		}
		ddi_prop_remove_all(devi);
		return (DDI_FAILURE);
	}

	/*
	 * Add a zero-length attribute to tell the world we support
	 * kernel ioctls (for layered drivers)
	 */
	(void) ddi_prop_create(DDI_DEV_T_NONE, devi, DDI_PROP_CANSLEEP,
	    DDI_KERNEL_IOCTL, NULL, 0);

	ddi_report_dev((dev_info_t *)devi);

	/*
	 * If it's a SCSI-2 tape drive which supports wide,
	 * tell the host adapter to use wide.
	 */
	wide = ((devp->sd_inq->inq_rdf == RDF_SCSI2) &&
	    (devp->sd_inq->inq_wbus16 || devp->sd_inq->inq_wbus32)) ?
		1 : 0;

	if (scsi_ifsetcap(ROUTE, "wide-xfer", wide, 1) == 1) {
		ST_DEBUG(devi, st_label, SCSI_DEBUG,
		    "Wide Transfer %s\n", wide ? "enabled" : "disabled");
	}

	/*
	 * enable autorequest sense; keep the rq packet around in case
	 * the autorequest sense fails because of a busy condition
	 * do a getcap first in case the capability is not variable
	 */
	if (scsi_ifgetcap(ROUTE, "auto-rqsense", 1) == 1) {
		un->un_arq_enabled = 1;
	} else {
		un->un_arq_enabled =
		    ((scsi_ifsetcap(ROUTE, "auto-rqsense", 1, 1) == 1) ? 1 : 0);
	}


	ST_DEBUG(devi, st_label, SCSI_DEBUG, "auto request sense %s\n",
		(un->un_arq_enabled ? "enabled" : "disabled"));

	un->un_untagged_qing =
	    (scsi_ifgetcap(ROUTE, "untagged-qing", 0) == 1);

	/*
	 * XXX - This is just for 2.6.  to tell users that write buffering
	 *	has gone away.
	 */
	if (un->un_arq_enabled && un->un_untagged_qing) {
		if (ddi_getprop(DDI_DEV_T_ANY, devi, DDI_PROP_DONTPASS,
		    "tape-driver-buffering", 0) != 0) {
			scsi_log(ST_DEVINFO, st_label, CE_NOTE,
			    "Write Data Buffering has been depricated. Your "
			    "applications should continue to work normally.\n"
			    " But, they should  ported to use Asynchronous "
			    " I/O\n"
			    " For more information, read about "
			    " tape-driver-buffering "
			    "property in the st(7d) man page\n");
		}
	}

	un->un_max_throttle = un->un_throttle = un->un_last_throttle = 1;
	un->un_flush_on_errors = 0;
	un->un_mkr_pkt = (struct scsi_pkt *)NULL;

	ST_DEBUG(devi, st_label, SCSI_DEBUG,
	    "throttle=%x, max_throttle = %x\n",
	    un->un_throttle, un->un_max_throttle);

	/* initialize persistent errors to nil */
	un->un_persistence = 0;
	un->un_persist_errors = 0;

	/*
	 * Get dma-max from HBA driver. If it is not defined, use 64k
	 */
	un->un_maxdma	= scsi_ifgetcap(&devp->sd_address, "dma-max", 1);
	if (un->un_maxdma == -1) {
		ST_DEBUG(devi, st_label, SCSI_DEBUG,
		    "Receaved a value that looked like -1. Using 64k maxdma");
		un->un_maxdma = (64 * 1024);
	}

	/*
	 * Get the max allowable cdb size
	 */
	un->un_max_cdb_sz =
	    scsi_ifgetcap(&devp->sd_address, "max-cdb-length", 1);
	if (un->un_max_cdb_sz < CDB_GROUP0) {
		ST_DEBUG(devi, st_label, SCSI_DEBUG,
		    "HBA reported max-cdb-length as %d\n", un->un_max_cdb_sz);
		un->un_max_cdb_sz = CDB_GROUP4; /* optimistic default */
	}

	un->un_maxbsize = MAXBSIZE_UNKNOWN;

	un->un_mediastate = MTIO_NONE;
	un->un_HeadClean  = TAPE_ALERT_SUPPORT_UNKNOWN;

	/*
	 * initialize kstats
	 */
	un->un_stats = kstat_create("st", instance, NULL, "tape",
			KSTAT_TYPE_IO, 1, KSTAT_FLAG_PERSISTENT);
	if (un->un_stats) {
		un->un_stats->ks_lock = ST_MUTEX;
		kstat_install(un->un_stats);
	}
	(void) st_create_errstats(un, instance);

	return (DDI_SUCCESS);
}

/*
 * st_detach:
 *
 * we allow a detach if and only if:
 *	- no tape is currently inserted
 *	- tape position is at BOT or unknown
 *		(if it is not at BOT then a no rewind
 *		device was opened and we have to preserve state)
 *	- it must be in a closed state : no timeouts or scsi_watch requests
 *		will exist if it is closed, so we don't need to check for
 *		them here.
 */
/*ARGSUSED*/
static int
st_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	int 	instance;
	int 	dev_instance;
	struct scsi_device *devp;
	struct scsi_tape *un;
	clock_t wait_cmds_complete;

	instance = ddi_get_instance(devi);

	if (!(un = ddi_get_soft_state(st_state, instance))) {
		return (DDI_FAILURE);
	}

	switch (cmd) {

	case DDI_DETACH:
		/*
		 * Undo what we did in st_attach & st_doattach,
		 * freeing resources and removing things we installed.
		 * The system framework guarantees we are not active
		 * with this devinfo node in any other entry points at
		 * this time.
		 */

		ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_detach: instance=%x, un=%p\n", instance,
		    (void *)un);

		if (((un->un_dp->options & ST_UNLOADABLE) == 0) ||
		    (un->un_ncmds != 0) || (un->un_quef != NULL) ||
		    (un->un_state != ST_STATE_CLOSED)) {
			/*
			 * we cannot unload some targets because the
			 * inquiry returns junk unless immediately
			 * after a reset
			 */
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "cannot unload instance %x\n", instance);
			return (DDI_FAILURE);
		}

		/*
		 * if the tape has been removed then we may unload;
		 * do a test unit ready and if it returns NOT READY
		 * then we assume that it is safe to unload.
		 * as a side effect, fileno may be set to -1 if the
		 * the test unit ready fails;
		 * also un_state may be set to non-closed, so reset it
		 */
		if ((un->un_dev) &&		/* Been opened since attach */
		    ((un->un_fileno > 0) ||	/* Known position not rewound */
		    (un->un_blkno != 0))) {	/* Or within first file */
			mutex_enter(ST_MUTEX);
			/*
			 * Send Test Unit Ready in the hopes that if
			 * the drive is not in the state we think it is.
			 * And the state will be changed so it can be detached.
			 * If the command fails to reach the device and
			 * the drive was not rewound or unloaded we want
			 * to fail the detach till a user command fails
			 * where after the detach will succead.
			 */
			(void) st_cmd(un->un_dev, SCMD_TEST_UNIT_READY,
			    0, SYNC_CMD);
			/*
			 * After TUR un_state may be set to non-closed,
			 * so reset it back.
			 */
			un->un_state = ST_STATE_CLOSED;
			mutex_exit(ST_MUTEX);
		}
		ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "un_status=%x, fileno=%x, blkno=%lx\n",
		    un->un_status, un->un_fileno, un->un_blkno);

		/*
		 * check again:
		 * if we are not at BOT then it is not safe to unload
		 */
		if ((un->un_dev) &&		/* Been opened since attach */
		    ((un->un_fileno > 0) ||	/* Known position not rewound */
		    (un->un_blkno != 0))) {	/* Or within first file */
			ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "cannot detach: fileno=%x, blkno=%lx\n",
			    un->un_fileno, un->un_blkno);
			return (DDI_FAILURE);
		}

		/*
		 * Just To make sure that we have released the
		 * tape unit .
		 */
		if (un->un_dev && (un->un_rsvd_status & ST_RESERVE) &&
		    !DEVI_IS_DEVICE_REMOVED(devi)) {
			mutex_enter(ST_MUTEX);
			(void) st_reserve_release(un, ST_RELEASE);
			mutex_exit(ST_MUTEX);
		}

		/*
		 * now remove other data structures allocated in st_doattach()
		 */
		ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "destroying/freeing\n");
		cv_destroy(&un->un_clscv);
		cv_destroy(&un->un_sbuf_cv);
		cv_destroy(&un->un_queue_cv);
		cv_destroy(&un->un_suspend_cv);
		cv_destroy(&un->un_tape_busy_cv);

		if (un->un_hib_tid) {
			(void) untimeout(un->un_hib_tid);
			un->un_hib_tid = 0;
		}

		if (un->un_delay_tid) {
			(void) untimeout(un->un_delay_tid);
			un->un_delay_tid = 0;
		}
		cv_destroy(&un->un_state_cv);

#if defined(__i386) || defined(__amd64)
		if (un->un_contig_mem_hdl != NULL) {
			ddi_dma_free_handle(&un->un_contig_mem_hdl);
		}
#endif
		if (un->un_sbufp) {
			freerbuf(un->un_sbufp);
		}
		if (un->un_uscsi_rqs_buf) {
			kmem_free(un->un_uscsi_rqs_buf, SENSE_LENGTH);
		}
		if (un->un_mspl) {
			i_ddi_mem_free((caddr_t)un->un_mspl, NULL);
		}
		if (un->un_rqs) {
			scsi_destroy_pkt(un->un_rqs);
			scsi_free_consistent_buf(un->un_rqs_bp);
		}
		if (un->un_mkr_pkt) {
			scsi_destroy_pkt(un->un_mkr_pkt);
		}
		if (un->un_arq_enabled) {
			(void) scsi_ifsetcap(ROUTE, "auto-rqsense", 0, 1);
		}
		if (un->un_dp_size) {
			kmem_free(un->un_dp, un->un_dp_size);
		}
		if (un->un_stats) {
			kstat_delete(un->un_stats);
			un->un_stats = (kstat_t *)0;
		}
		if (un->un_errstats) {
			kstat_delete(un->un_errstats);
			un->un_errstats = (kstat_t *)0;
		}
		devp = ST_SCSI_DEVP;
		ddi_soft_state_free(st_state, instance);
		devp->sd_private = NULL;
		devp->sd_sense = NULL;
		scsi_unprobe(devp);
		ddi_prop_remove_all(devi);
		ddi_remove_minor_node(devi, NULL);
		ST_DEBUG(0, st_label, SCSI_DEBUG, "st_detach done\n");
		return (DDI_SUCCESS);

	case DDI_SUSPEND:

		/*
		 * Suspend/Resume
		 *
		 * To process DDI_SUSPEND, we must do the following:
		 *
		 *  - check ddi_removing_power to see if power will be turned
		 *    off. if so, return DDI_FAILURE
		 *  - check if we are already suspended,
		 *    if so, return DDI_FAILURE
		 *  - check if device state is CLOSED,
		 *    if not, return DDI_FAILURE.
		 *  - wait until outstanding operations complete
		 *  - save tape state
		 *  - block new operations
		 *  - cancel pending timeouts
		 *
		 */

		if (ddi_removing_power(devi))
			return (DDI_FAILURE);

		mutex_enter(ST_MUTEX);

		/*
		 * Shouldn't already be suspended, if so return failure
		 */
		if (un->un_pwr_mgmt == ST_PWR_SUSPENDED) {
			mutex_exit(ST_MUTEX);
			return (DDI_FAILURE);
		}
		if (un->un_state != ST_STATE_CLOSED) {
			mutex_exit(ST_MUTEX);
			return (DDI_FAILURE);
		}

		/*
		 * Wait for all outstanding I/O's to complete
		 *
		 * we wait on both ncmds and the wait queue for times
		 * when we are flushing after persistent errors are
		 * flagged, which is when ncmds can be 0, and the
		 * queue can still have I/O's.  This way we preserve
		 * order of biodone's.
		 */
		wait_cmds_complete = ddi_get_lbolt();
		wait_cmds_complete +=
		    st_wait_cmds_complete * drv_usectohz(1000000);
		while (un->un_ncmds || un->un_quef ||
		    (un->un_state == ST_STATE_RESOURCE_WAIT)) {

			if (cv_timedwait(&un->un_tape_busy_cv, ST_MUTEX,
			    wait_cmds_complete) == -1) {
				/*
				 * Time expired then cancel the command
				 */
				mutex_exit(ST_MUTEX);
				if (scsi_reset(ROUTE, RESET_TARGET) == 0) {
					mutex_enter(ST_MUTEX);
					if (un->un_last_throttle) {
						un->un_throttle =
						    un->un_last_throttle;
					}
					mutex_exit(ST_MUTEX);
					return (DDI_FAILURE);
				} else {
					mutex_enter(ST_MUTEX);
					break;
				}
			}
		}

		/*
		 * DDI_SUSPEND says that the system "may" power down, we
		 * remember the file and block number before rewinding.
		 * we also need to save state before issuing
		 * any WRITE_FILE_MARK command.
		 */
		if (un->un_fileno < 0) {
			un->un_suspend_fileno = 0;
			un->un_suspend_blkno  = 0;
		} else {
			un->un_suspend_fileno = un->un_fileno;
			un->un_suspend_blkno = un->un_blkno;
		}
		dev_instance = ((un->un_dev == 0) ? MTMINOR(instance) :
		    un->un_dev);

		/*
		 * Issue a zero write file fmk command to tell the drive to
		 * flush any buffered tape marks
		 */
		(void) st_cmd(dev_instance, SCMD_WRITE_FILE_MARK, 0, SYNC_CMD);

		/*
		 * Because not all tape drives correctly implement buffer
		 * flushing with the zero write file fmk command, issue a
		 * synchronous rewind command to force data flushing.
		 * st_validate_tapemarks() will do a rewind during DDI_RESUME
		 * anyway.
		 */
		(void) st_cmd(dev_instance, SCMD_REWIND, 0, SYNC_CMD);

		/* stop any new operations */
		un->un_pwr_mgmt = ST_PWR_SUSPENDED;
		un->un_throttle = 0;

		/*
		 * cancel any outstanding timeouts
		 */
		if (un->un_delay_tid) {
			timeout_id_t temp_id = un->un_delay_tid;
			un->un_delay_tid = 0;
			un->un_tids_at_suspend |= ST_DELAY_TID;
			mutex_exit(ST_MUTEX);
			(void) untimeout(temp_id);
			mutex_enter(ST_MUTEX);
		}

		if (un->un_hib_tid) {
			timeout_id_t temp_id = un->un_hib_tid;
			un->un_hib_tid = 0;
			un->un_tids_at_suspend |= ST_HIB_TID;
			mutex_exit(ST_MUTEX);
			(void) untimeout(temp_id);
			mutex_enter(ST_MUTEX);
		}

		/*
		 * Suspend the scsi_watch_thread
		 */
		if (un->un_swr_token) {
			opaque_t temp_token = un->un_swr_token;
			mutex_exit(ST_MUTEX);
			scsi_watch_suspend(temp_token);
		} else {
			mutex_exit(ST_MUTEX);
		}

		return (DDI_SUCCESS);

	default:
		ST_DEBUG(0, st_label, SCSI_DEBUG, "st_detach failed\n");
		return (DDI_FAILURE);
	}
}


/* ARGSUSED */
static int
stinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	dev_t dev;
	struct scsi_tape *un;
	int instance, error;
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		dev = (dev_t)arg;
		instance = MTUNIT(dev);
		if ((un = ddi_get_soft_state(st_state, instance)) == NULL)
			return (DDI_FAILURE);
		*result = (void *) ST_DEVINFO;
		error = DDI_SUCCESS;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		dev = (dev_t)arg;
		instance = MTUNIT(dev);
		*result = (void *)(uintptr_t)instance;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}

static int
st_doattach(struct scsi_device *devp, int (*canwait)())
{
	struct scsi_pkt *rqpkt = NULL;
	struct scsi_tape *un = NULL;
	int km_flags = (canwait != NULL_FUNC) ? KM_SLEEP : KM_NOSLEEP;
	int instance;
	struct buf *bp;
	size_t rlen;

	/*
	 * Call the routine scsi_probe to do some of the dirty work.
	 * If the INQUIRY command succeeds, the field sd_inq in the
	 * device structure will be filled in.
	 */
	ST_DEBUG(devp->sd_dev, st_label, SCSI_DEBUG,
		"st_doattach(): probing\n");

	if (scsi_probe(devp, canwait) == SCSIPROBE_EXISTS) {

		/*
		 * In checking the whole inq_dtype byte we are looking at both
		 * the Peripheral Qualifier and the Peripheral Device Type.
		 * For this driver we are only interested in sequential devices
		 * that are connected or capable if connecting to this logical
		 * unit.
		 */
		if (devp->sd_inq->inq_dtype ==
		    (DTYPE_SEQUENTIAL | DPQ_POSSIBLE)) {
			ST_DEBUG(devp->sd_dev, st_label, SCSI_DEBUG,
			    "probe exists\n");
		} else {
			/* Something there but not a tape device */
			scsi_unprobe(devp);
			return (DDI_FAILURE);
		}
	} else {
		/* Nothing there */
		ST_DEBUG(devp->sd_dev, st_label, SCSI_DEBUG,
		    "probe failure: nothing there\n");
		scsi_unprobe(devp);
		return (DDI_FAILURE);
	}

	bp = scsi_alloc_consistent_buf(&devp->sd_address, (struct buf *)NULL,
	    SENSE_LENGTH, B_READ, canwait, NULL);
	if (!bp) {
		goto error;
	}
	rqpkt = scsi_init_pkt(&devp->sd_address,
	    (struct scsi_pkt *)NULL, bp, CDB_GROUP0, 1, 0,
	    PKT_CONSISTENT, canwait, NULL);
	if (!rqpkt) {
		goto error;
	}
	devp->sd_sense = (struct scsi_extended_sense *)bp->b_un.b_addr;
	ASSERT(geterror(bp) == NULL);

	(void) scsi_setup_cdb((union scsi_cdb *)rqpkt->pkt_cdbp,
	    SCMD_REQUEST_SENSE, 0, SENSE_LENGTH, 0);
	FILL_SCSI1_LUN(devp, rqpkt);

	/*
	 * The actual unit is present.
	 * Now is the time to fill in the rest of our info..
	 */
	instance = ddi_get_instance(devp->sd_dev);

	if (ddi_soft_state_zalloc(st_state, instance) != DDI_SUCCESS) {
		goto error;
	}
	un = ddi_get_soft_state(st_state, instance);

	ASSERT(un != NULL);

	un->un_sbufp = getrbuf(km_flags);

	un->un_uscsi_rqs_buf = kmem_alloc(SENSE_LENGTH, KM_SLEEP);

	/*
	 * use i_ddi_mem_alloc() for now until we have an interface to allocate
	 * memory for DMA which doesn't require a DMA handle. ddi_iopb_alloc()
	 * is obsolete and we want more flexibility in controlling the DMA
	 * address constraints.
	 */
	(void) i_ddi_mem_alloc(devp->sd_dev, &st_alloc_attr,
	    sizeof (struct seq_mode), ((km_flags == KM_SLEEP) ? 1 : 0), 0,
	    NULL, (caddr_t *)&un->un_mspl, &rlen, NULL);

	if (!un->un_sbufp || !un->un_mspl) {
		if (un->un_mspl) {
			i_ddi_mem_free((caddr_t)un->un_mspl, NULL);
		}
		ST_DEBUG6(devp->sd_dev, st_label, SCSI_DEBUG,
		    "probe partial failure: no space\n");
		goto error;
	}

	bzero(un->un_mspl, sizeof (struct seq_mode));

	cv_init(&un->un_sbuf_cv, NULL, CV_DRIVER, NULL);
	cv_init(&un->un_queue_cv, NULL, CV_DRIVER, NULL);
	cv_init(&un->un_clscv, NULL, CV_DRIVER, NULL);
	cv_init(&un->un_state_cv, NULL, CV_DRIVER, NULL);
#if defined(__i386) || defined(__amd64)
	cv_init(&un->un_contig_mem_cv, NULL, CV_DRIVER, NULL);
#endif

	/* Initialize power managemnet condition variable */
	cv_init(&un->un_suspend_cv, NULL, CV_DRIVER, NULL);
	cv_init(&un->un_tape_busy_cv, NULL, CV_DRIVER, NULL);

	rqpkt->pkt_flags |= (FLAG_SENSING | FLAG_HEAD | FLAG_NODISCON);

	un->un_fileno	= -1;
	rqpkt->pkt_time = st_io_time;
	rqpkt->pkt_comp = st_intr;
	un->un_rqs	= rqpkt;
	un->un_sd	= devp;
	un->un_rqs_bp	= bp;
	un->un_swr_token = (opaque_t)NULL;
	un->un_comp_page = ST_DEV_DATACOMP_PAGE | ST_DEV_CONFIG_PAGE;
	un->un_wormable = st_is_drive_worm;

	un->un_suspend_fileno 	= 0;
	un->un_suspend_blkno 	= 0;

#if defined(__i386) || defined(__amd64)
	if (ddi_dma_alloc_handle(ST_DEVINFO, &st_contig_mem_dma_attr,
		DDI_DMA_SLEEP, NULL, &un->un_contig_mem_hdl) != DDI_SUCCESS) {
		ST_DEBUG6(devp->sd_dev, st_label, SCSI_DEBUG,
		    "allocation of contiguous memory dma handle failed!");
		un->un_contig_mem_hdl = NULL;
		goto error;
	}
#endif

	/*
	 * Since this driver manages devices with "remote" hardware,
	 * i.e. the devices themselves have no "reg" properties,
	 * the SUSPEND/RESUME commands in detach/attach will not be
	 * called by the power management framework unless we request
	 * it by creating a "pm-hardware-state" property and setting it
	 * to value "needs-suspend-resume".
	 */
	if (ddi_prop_update_string(DDI_DEV_T_NONE, devp->sd_dev,
	    "pm-hardware-state", "needs-suspend-resume") !=
	    DDI_PROP_SUCCESS) {

		ST_DEBUG(devp->sd_dev, st_label, SCSI_DEBUG,
		    "ddi_prop_update(\"pm-hardware-state\") failed\n");
		goto error;
	}

	if (ddi_prop_create(DDI_DEV_T_NONE, devp->sd_dev, DDI_PROP_CANSLEEP,
	    "no-involuntary-power-cycles", NULL, 0) != DDI_PROP_SUCCESS) {

		ST_DEBUG(devp->sd_dev, st_label, SCSI_DEBUG,
		    "ddi_prop_create(\"no-involuntary-power-cycles\") "
		    "failed\n");
		goto error;
	}

	ST_DEBUG6(devp->sd_dev, st_label, SCSI_DEBUG, "probe success\n");
	return (DDI_SUCCESS);

error:
	devp->sd_sense = NULL;

	ddi_remove_minor_node(devp->sd_dev, NULL);
	if (un) {
		if (un->un_mspl) {
			i_ddi_mem_free((caddr_t)un->un_mspl, NULL);
		}
		if (un->un_sbufp) {
			freerbuf(un->un_sbufp);
		}
		if (un->un_uscsi_rqs_buf) {
			kmem_free(un->un_uscsi_rqs_buf, SENSE_LENGTH);
		}
#if defined(__i386) || defined(__amd64)
		if (un->un_contig_mem_hdl != NULL) {
			ddi_dma_free_handle(&un->un_contig_mem_hdl);
		}
#endif
		ddi_soft_state_free(st_state, instance);
		devp->sd_private = NULL;
	}

	if (rqpkt) {
		scsi_destroy_pkt(rqpkt);
	}

	if (bp) {
		scsi_free_consistent_buf(bp);
	}

	if (devp->sd_inq) {
		scsi_unprobe(devp);
	}
	return (DDI_FAILURE);
}

typedef int
(*cfg_functp)(struct scsi_tape *, char *vidpid, struct st_drivetype *);

static cfg_functp config_functs[] = {
	st_get_conf_from_st_dot_conf,
	st_get_conf_from_st_conf_dot_c,
	st_get_default_conf
};


/*
 * determine tape type, using tape-config-list or built-in table or
 * use a generic tape config entry
 */
static void
st_known_tape_type(struct scsi_tape *un)
{
	struct st_drivetype *dp;
	cfg_functp *config_funct;

	/*
	 * XXX:  Emulex MT-02 (and emulators) predates SCSI-1 and has
	 *	 no vid & pid inquiry data.  So, we provide one.
	 */
	if (ST_INQUIRY->inq_len == 0 ||
		(bcmp("\0\0\0\0\0\0\0\0", ST_INQUIRY->inq_vid, 8) == 0)) {
		(void) strcpy((char *)ST_INQUIRY->inq_vid, ST_MT02_NAME);
	}

	un->un_dp_size = sizeof (struct st_drivetype);
	dp = kmem_zalloc((size_t)un->un_dp_size, KM_SLEEP);
	un->un_dp = dp;

	/*
	 * Loop through the configuration methods till one works.
	 */
	for (config_funct = &config_functs[0]; ; config_funct++) {
		if ((*config_funct)(un, ST_INQUIRY->inq_vid, dp)) {
			break;
		}
	}

	/*
	 * If we didn't just make up this configuration and
	 * all the density codes are the same..
	 * Set Auto Density over ride.
	 */
	if (*config_funct != st_get_default_conf) {
		/*
		 * If this device is one that is configured and all
		 * densities are the same, This saves doing gets and set
		 * that yield nothing.
		 */
		if ((dp->densities[0]) == (dp->densities[1]) &&
		    (dp->densities[0]) == (dp->densities[2]) &&
		    (dp->densities[0]) == (dp->densities[3])) {

			dp->options |= ST_AUTODEN_OVERRIDE;
		}
	}


	/*
	 * Store tape drive characteristics.
	 */
	un->un_status = 0;
	un->un_attached = 1;
	un->un_init_options = dp->options;

	/* setup operation time-outs based on options */
	st_calculate_timeouts(un);

	/* make sure if we are supposed to be variable, make it variable */
	if (dp->options & ST_VARIABLE) {
		dp->bsize = 0;
	}

	scsi_log(ST_DEVINFO, st_label, CE_NOTE, "?<%s>\n", dp->name);
}


typedef struct {
	int mask;
	int bottom;
	int top;
	char *name;
} conf_limit;

static const conf_limit conf_limits[] = {

	-1,		1,		2,		"conf version",
	-1,		MT_ISTS,	ST_LAST_TYPE,	"drive type",
	-1,		0,		0xffffff,	"block size",
	ST_VALID_OPTS,	0,		ST_VALID_OPTS,	"options",
	-1,		0,		4,		"number of densities",
	-1,		0,		UINT8_MAX,	"density code",
	-1,		0,		3,		"default density",
	-1,		0,		UINT16_MAX,	"non motion timeout",
	-1,		0,		UINT16_MAX,	"I/O timeout",
	-1,		0,		UINT16_MAX,	"space timeout",
	-1,		0,		UINT16_MAX,	"load timeout",
	-1,		0,		UINT16_MAX,	"unload timeout",
	-1,		0,		UINT16_MAX,	"erase timeout",
	0,		0,		0,		NULL
};

static int
st_validate_conf_data(struct scsi_tape *un, int *list, int list_len,
    const char *conf_name)
{
	int dens;
	int ndens;
	int value;
	int type;
	int count;
	const conf_limit *limit = &conf_limits[0];

	ST_DEBUG3(ST_DEVINFO, st_label, CE_NOTE,
	    "Checking %d entrys total with %d densities\n", list_len, list[4]);

	count = list_len;
	type = *list;
	for (;  count && limit->name; count--, list++, limit++) {

		value = *list;
		if (value & ~limit->mask) {
			scsi_log(ST_DEVINFO, st_label, CE_NOTE,
			    "%s %s value invalid bits set: 0x%X\n",
			    conf_name, limit->name, value & ~limit->mask);
			*list &= limit->mask;
		} else if (value < limit->bottom) {
			scsi_log(ST_DEVINFO, st_label, CE_NOTE,
			    "%s %s value too low: value = %d limit %d\n",
			    conf_name, limit->name, value, limit->bottom);
		} else if (value > limit->top) {
			scsi_log(ST_DEVINFO, st_label, CE_NOTE,
			    "%s %s value too high: value = %d limit %d\n",
			    conf_name, limit->name, value, limit->top);
		} else {
			ST_DEBUG3(ST_DEVINFO, st_label, CE_CONT,
			    "%s %s value = 0x%X\n",
			    conf_name, limit->name, value);
		}

		/* If not the number of densities continue */
		if (limit != &conf_limits[4]) {
			continue;
		}

		/* If number of densities is not in range can't use config */
		if (value < limit->bottom || value > limit->top) {
			return (-1);
		}

		ndens = min(value, NDENSITIES);
		if ((type == 1) && (list_len - ndens) != 6) {
			scsi_log(ST_DEVINFO, st_label, CE_NOTE,
			    "%s conf version 1 with %d densities has %d items"
			    " should have %d",
			    conf_name, ndens, list_len, 6 + ndens);
		} else if ((type == 2) && (list_len - ndens) != 13) {
			scsi_log(ST_DEVINFO, st_label, CE_NOTE,
			    "%s conf version 2 with %d densities has %d items"
			    " should have %d",
			    conf_name, ndens, list_len, 13 + ndens);
		}

		limit++;
		for (dens = 0; dens < ndens && count; dens++) {
			count--;
			list++;
			value = *list;
			if (value < limit->bottom) {
				scsi_log(ST_DEVINFO, st_label, CE_NOTE,
				    "%s density[%d] value too low: value ="
				    " 0x%X limit 0x%X\n",
				    conf_name, dens, value, limit->bottom);
			} else if (value > limit->top) {
				scsi_log(ST_DEVINFO, st_label, CE_NOTE,
				    "%s density[%d] value too high: value ="
				    " 0x%X limit 0x%X\n",
				    conf_name, dens, value, limit->top);
			} else {
				ST_DEBUG3(ST_DEVINFO, st_label, CE_CONT,
				    "%s density[%d] value = 0x%X\n",
				    conf_name, dens, value);
			}
		}
	}

	return (0);
}

static int
st_get_conf_from_st_dot_conf(struct scsi_tape *un, char *vidpid,
    struct st_drivetype *dp)
{
	caddr_t config_list = NULL;
	caddr_t data_list = NULL;
	int	*data_ptr;
	caddr_t vidptr, prettyptr, datanameptr;
	size_t	vidlen, prettylen, datanamelen, tripletlen = 0;
	int config_list_len, data_list_len, len, i;
	int version;
	int found = 0;


	/*
	 * Determine type of tape controller. Type is determined by
	 * checking the vendor ids of the earlier inquiry command and
	 * comparing those with vids in tape-config-list defined in st.conf
	 */
	if (ddi_getlongprop(DDI_DEV_T_ANY, ST_DEVINFO, DDI_PROP_DONTPASS,
	    "tape-config-list", (caddr_t)&config_list, &config_list_len)
	    != DDI_PROP_SUCCESS) {
		return (found);
	}

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_get_conf_from_st_dot_conf(): st.conf has tape-config-list\n");

	/*
	 * Compare vids in each triplet - if it matches, get value for
	 * data_name and contruct a st_drivetype struct
	 * tripletlen is not set yet!
	 */
	for (len = config_list_len, vidptr = config_list;
	    len > 0;
	    vidptr += tripletlen, len -= tripletlen) {

		vidlen = strlen(vidptr);
		prettyptr = vidptr + vidlen + 1;
		prettylen = strlen(prettyptr);
		datanameptr = prettyptr + prettylen + 1;
		datanamelen = strlen(datanameptr);
		tripletlen = vidlen + prettylen + datanamelen + 3;

		if (vidlen == 0) {
			continue;
		}

		/*
		 * If inquiry vid dosen't match this triplets vid,
		 * try the next.
		 */
		if (strncasecmp(vidpid, vidptr, vidlen)) {
			continue;
		}

		/*
		 * if prettylen is zero then use the vid string
		 */
		if (prettylen == 0) {
			prettyptr = vidptr;
			prettylen = vidlen;
		}

		ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "vid = %s, pretty=%s, dataname = %s\n",
		    vidptr, prettyptr, datanameptr);

		/*
		 * get the data list
		 */
		if (ddi_getlongprop(DDI_DEV_T_ANY, ST_DEVINFO, 0,
		    datanameptr, (caddr_t)&data_list,
		    &data_list_len) != DDI_PROP_SUCCESS) {
			/*
			 * Error in getting property value
			 * print warning!
			 */
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			    "data property (%s) has no value\n",
			    datanameptr);
			continue;
		}

		/*
		 * now initialize the st_drivetype struct
		 */
		(void) strncpy(dp->name, prettyptr, ST_NAMESIZE - 1);
		dp->length = (int)min(vidlen, (VIDPIDLEN - 1));
		(void) strncpy(dp->vid, vidptr, dp->length);
		data_ptr = (int *)data_list;
		/*
		 * check if data is enough for version, type,
		 * bsize, options, # of densities, density1,
		 * density2, ..., default_density
		 */
		if ((data_list_len < 5 * sizeof (int)) ||
		    (data_list_len < 6 * sizeof (int) +
		    *(data_ptr + 4) * sizeof (int))) {
			/*
			 * print warning and skip to next triplet.
			 */
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			    "data property (%s) incomplete\n",
			    datanameptr);
			kmem_free(data_list, data_list_len);
			continue;
		}

		if (st_validate_conf_data(un, data_ptr,
		    data_list_len / sizeof (int), datanameptr)) {
			kmem_free(data_list, data_list_len);
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			    "data property (%s) rejected\n",
			    datanameptr);
			continue;
		}

		/*
		 * check version
		 */
		version = *data_ptr++;
		if (version != 1 && version != 2) {
			/* print warning but accept it */
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			    "Version # for data property (%s) "
			    "not set to 1 or 2\n", datanameptr);
		}

		dp->type    = *data_ptr++;
		dp->bsize   = *data_ptr++;
		dp->options = *data_ptr++;
		dp->options |= ST_DYNAMIC;
		len = *data_ptr++;
		for (i = 0; i < NDENSITIES; i++) {
			if (i < len) {
				dp->densities[i] = *data_ptr++;
			}
		}
		dp->default_density = *data_ptr << 3;
		if (version == 2 &&
		    data_list_len >= (13 + len) * sizeof (int)) {
			data_ptr++;
			dp->non_motion_timeout	= *data_ptr++;
			dp->io_timeout		= *data_ptr++;
			dp->rewind_timeout	= *data_ptr++;
			dp->space_timeout	= *data_ptr++;
			dp->load_timeout	= *data_ptr++;
			dp->unload_timeout	= *data_ptr++;
			dp->erase_timeout	= *data_ptr++;
		}
		kmem_free(data_list, data_list_len);
		found = 1;
		ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "found in st.conf: vid = %s, pretty=%s\n",
		    dp->vid, dp->name);
		break;
	}

	/*
	 * free up the memory allocated by ddi_getlongprop
	 */
	if (config_list) {
		kmem_free(config_list, config_list_len);
	}
	return (found);
}

static int
st_get_conf_from_st_conf_dot_c(struct scsi_tape *un, char *vidpid,
    struct st_drivetype *dp)
{
	int i;

	/*
	 * Determine type of tape controller.  Type is determined by
	 * checking the result of the earlier inquiry command and
	 * comparing vendor ids with strings in a table declared in st_conf.c.
	 */
	ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_get_conf_from_st_conf_dot_c(): looking at st_drivetypes\n");

	for (i = 0; i < st_ndrivetypes; i++) {
		if (st_drivetypes[i].length == 0) {
			continue;
		}
		if (strncasecmp(vidpid, st_drivetypes[i].vid,
		    st_drivetypes[i].length)) {
			continue;
		}
		bcopy(&st_drivetypes[i], dp, sizeof (st_drivetypes[i]));
		return (1);
	}
	return (0);
}

static int
st_get_default_conf(struct scsi_tape *un, char *vidpid, struct st_drivetype *dp)
{
	int i;

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_get_default_conf(): making drivetype from INQ cmd\n");


	/*
	 * Make up a name
	 */
	bcopy("Vendor '", dp->name, 8);
	bcopy(vidpid, &dp->name[8], VIDLEN);
	bcopy("' Product '", &dp->name[16], 11);
	bcopy(&vidpid[8], &dp->name[27], PIDLEN);
	dp->name[ST_NAMESIZE - 2] = '\'';
	dp->name[ST_NAMESIZE - 1] = '\0';
	dp->length = min(strlen(ST_INQUIRY->inq_vid), (VIDPIDLEN - 1));
	(void) strncpy(dp->vid, ST_INQUIRY->inq_vid, dp->length);
	/*
	 * 'clean' vendor and product strings of non-printing chars
	 */
	for (i = 0; i < ST_NAMESIZE - 2; i++) {
		if (dp->name[i] < ' ' || dp->name[i] > '~') {
			dp->name[i] = '.';
		}
	}
	dp->type = ST_TYPE_INVALID;
	dp->options |= (ST_DYNAMIC | ST_UNLOADABLE | ST_MODE_SEL_COMP);

	return (1); /* Can Not Fail */
}

/*
 * Regular Unix Entry points
 */



/* ARGSUSED */
static int
st_open(dev_t *dev_p, int flag, int otyp, cred_t *cred_p)
{
	dev_t dev = *dev_p;
	int rval = 0;

	GET_SOFT_STATE(dev);

	/*
	 * validate that we are addressing a sensible unit
	 */
	mutex_enter(ST_MUTEX);

#ifdef	STDEBUG
	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_open(node = %s dev = 0x%lx, flag = %d, otyp = %d)\n",
	    st_dev_name(dev), *dev_p, flag, otyp);
#endif

	/*
	 * All device accesss go thru st_strategy() where we check
	 * suspend status
	 */

	if (!un->un_attached) {
		st_known_tape_type(un);
		if (!un->un_attached) {
			rval = ENXIO;
			goto exit;
		}

	}

	/*
	 * Check for the case of the tape in the middle of closing.
	 * This isn't simply a check of the current state, because
	 * we could be in state of sensing with the previous state
	 * that of closing.
	 *
	 * And don't allow multiple opens.
	 */
	if (!(flag & (FNDELAY | FNONBLOCK)) && IS_CLOSING(un)) {
		un->un_laststate = un->un_state;
		un->un_state = ST_STATE_CLOSE_PENDING_OPEN;
		while (IS_CLOSING(un) ||
		    un->un_state == ST_STATE_CLOSE_PENDING_OPEN) {
			if (cv_wait_sig(&un->un_clscv, ST_MUTEX) == 0) {
				rval = EINTR;
				un->un_state = un->un_laststate;
				goto exit;
			}
		}
	} else if (un->un_state != ST_STATE_CLOSED) {
		rval = EBUSY;
		goto busy;
	}

	/*
	 * record current dev
	 */
	un->un_dev = dev;
	un->un_oflags = flag;	/* save for use in st_tape_init() */
	un->un_errno = 0;	/* no errors yet */
	un->un_restore_pos = 0;
	un->un_rqs_state = 0;

	/*
	 * If we are opening O_NDELAY, or O_NONBLOCK, we don't check for
	 * anything, leave internal states alone, if fileno >= 0
	 */
	if (flag & (FNDELAY | FNONBLOCK)) {
		if (un->un_fileno < 0 || (un->un_fileno == 0 &&
		    un->un_blkno == 0)) {
			un->un_state = ST_STATE_OFFLINE;
		} else if (un->un_fileno > 0 ||
		    (un->un_fileno == 0 && un->un_blkno != 0)) {
			/*
			 * set un_read_only/write-protect status.
			 *
			 * If the tape is not bot we can assume
			 * that mspl->wp_status is set properly.
			 * else
			 * we need to do a mode sense/Tur once
			 * again to get the actual tape status.(since
			 * user might have replaced the tape)
			 * Hence make the st state OFFLINE so that
			 * we re-intialize the tape once again.
			 */
			un->un_read_only =
			    (un->un_oflags & FWRITE) ? RDWR : RDONLY;
			un->un_state = ST_STATE_OPEN_PENDING_IO;
		} else {
			un->un_state = ST_STATE_OFFLINE;
		}
		rval = 0;
	} else {
		/*
		 * Not opening O_NDELAY.
		 */
		un->un_state = ST_STATE_OPENING;

		rval = st_tape_init(dev);
		if ((rval == EACCES) && (un->un_read_only & WORM)) {
			un->un_state = ST_STATE_OPEN_PENDING_IO;
			rval = 0; /* so open doesn't fail */
		} else if (rval) {
			/*
			 * Release the tape unit, if reserved and not
			 * preserve reserve.
			 */
			if ((un->un_rsvd_status &
			    (ST_RESERVE | ST_PRESERVE_RESERVE)) == ST_RESERVE) {
				(void) st_reserve_release(un, ST_RELEASE);
			}
		} else {
			un->un_state = ST_STATE_OPEN_PENDING_IO;
		}
	}

exit:
	/*
	 * we don't want any uninvited guests scrogging our data when we're
	 * busy with something, so for successful opens or failed opens
	 * (except for EBUSY), reset these counters and state appropriately.
	 */
	if (rval != EBUSY) {
		if (rval) {
			un->un_state = ST_STATE_CLOSED;
		}
		un->un_err_resid = 0;
		un->un_retry_ct = 0;
		un->un_tran_retry_ct = 0;
	}
busy:
	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_open: return val = %x, state = %d\n", rval, un->un_state);
	mutex_exit(ST_MUTEX);
	return (rval);

}

static int
st_tape_init(dev_t dev)
{
	int err;
	int rval = 0;

	GET_SOFT_STATE(dev);

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_tape_init(dev = 0x%lx, oflags = %d)\n", dev, un->un_oflags);

	/*
	 * Clean up after any errors left by 'last' close.
	 * This also handles the case of the initial open.
	 */
	if (un->un_state != ST_STATE_INITIALIZING) {
		un->un_laststate = un->un_state;
		un->un_state = ST_STATE_OPENING;
	}

	un->un_kbytes_xferred = 0;

	/*
	 * do a throw away TUR to clear check condition
	 */
	err = st_cmd(dev, SCMD_TEST_UNIT_READY, 0, SYNC_CMD);

	/*
	 * If test unit ready fails because the drive is reserved
	 * by another host fail the open for no access.
	 */
	if (err) {
		if (un->un_rsvd_status & ST_RESERVATION_CONFLICT) {
			un->un_state = ST_STATE_CLOSED;
			ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_tape_init: RESERVATION CONFLICT\n");
			rval = EACCES;
			goto exit;
		}
	}

	/*
	 * See whether this is a generic device that we haven't figured
	 * anything out about yet.
	 */
	if (un->un_dp->type == ST_TYPE_INVALID) {
		rval = st_determine_generic(dev);
		if (rval) {
			if (rval != EACCES) {
				rval = EIO;
			}
			un->un_state = ST_STATE_CLOSED;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_tape_init: %s invalid type\n",
			    rval == EACCES ? "EACCES" : "EIO");
			goto exit;
		}
		/*
		 * If this is a Unknown Type drive,
		 * Use the READ BLOCK LIMITS to determine if
		 * allow large xfer is approprate if not globally
		 * disabled with st_allow_large_xfer.
		 */
		un->un_allow_large_xfer = (uchar_t)st_allow_large_xfer;
	} else {

		/*
		 * If we allow_large_xfer (ie >64k) and have not yet found out
		 * the max block size supported by the drive,
		 * find it by issueing a READ_BLKLIM command.
		 * if READ_BLKLIM cmd fails, assume drive doesn't
		 * allow_large_xfer and min/max block sizes as 1 byte and 63k.
		 */
		un->un_allow_large_xfer = st_allow_large_xfer &&
		    (un->un_dp->options & ST_NO_RECSIZE_LIMIT);
	}
	/*
	 * if maxbsize is unknown, set the maximum block size.
	 */
	if (un->un_maxbsize == MAXBSIZE_UNKNOWN) {

		/*
		 * Get the Block limits of the tape drive.
		 * if un->un_allow_large_xfer = 0 , then make sure
		 * that maxbsize is <= ST_MAXRECSIZE_FIXED.
		 */
		un->un_rbl = kmem_zalloc(RBLSIZE, KM_SLEEP);

		err = st_cmd(dev, SCMD_READ_BLKLIM, RBLSIZE, SYNC_CMD);
		if (err) {
			/* Retry */
			err = st_cmd(dev, SCMD_READ_BLKLIM, RBLSIZE, SYNC_CMD);
		}
		if (!err) {

			/*
			 * if cmd successful, use limit returned
			 */
			un->un_maxbsize = (un->un_rbl->max_hi << 16) +
					(un->un_rbl->max_mid << 8) +
					un->un_rbl->max_lo;
			un->un_minbsize = (un->un_rbl->min_hi << 8) +
					un->un_rbl->min_lo;
			un->un_data_mod = 1 << un->un_rbl->granularity;
			if ((un->un_maxbsize == 0) ||
			    (un->un_allow_large_xfer == 0 &&
			    un->un_maxbsize > ST_MAXRECSIZE_FIXED)) {
				un->un_maxbsize = ST_MAXRECSIZE_FIXED;

			} else if (un->un_dp->type == ST_TYPE_DEFAULT) {
				/*
				 * Drive is not one that is configured, But the
				 * READ BLOCK LIMITS tells us it can do large
				 * xfers.
				 */
				if (un->un_maxbsize > ST_MAXRECSIZE_FIXED) {
					un->un_dp->options |=
					    ST_NO_RECSIZE_LIMIT;
				}
				/*
				 * If max and mimimum block limits are the
				 * same this is a fixed block size device.
				 */
				if (un->un_maxbsize == un->un_minbsize) {
					un->un_dp->options &= ~ST_VARIABLE;
				}
			}

			if (un->un_minbsize == 0) {
				un->un_minbsize = 1;
			}

		} else { /* error on read block limits */

			scsi_log(ST_DEVINFO, st_label, CE_NOTE,
				"!st_tape_init: Error on READ BLOCK LIMITS,"
				" errno = %d un_rsvd_status = 0x%X\n",
				err, un->un_rsvd_status);

			/*
			 * since read block limits cmd failed,
			 * do not allow large xfers.
			 * use old values in st_minphys
			 */
			if (un->un_rsvd_status & ST_RESERVATION_CONFLICT) {
				rval = EACCES;
			} else {
				un->un_allow_large_xfer = 0;
				scsi_log(ST_DEVINFO, st_label, CE_NOTE,
					"!Disabling large transfers\n");

				/*
				 * we guess maxbsize and minbsize
				 */
				if (un->un_bsize) {
					un->un_maxbsize = un->un_minbsize =
						un->un_bsize;
				} else {
					un->un_maxbsize = ST_MAXRECSIZE_FIXED;
					un->un_minbsize = 1;
				}
				/*
				 * Data Mod must be set,
				 * Even if read block limits fails.
				 * Prevents Divide By Zero in st_rw().
				 */
				un->un_data_mod = 1;
			}
		}
		if (un->un_rbl) {
			kmem_free(un->un_rbl, RBLSIZE);
			un->un_rbl = NULL;
		}

		if (rval) {
			goto exit;
		}
	}

	ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "maxdma = %d, maxbsize = %d, minbsize = %d, %s large xfer\n",
	    un->un_maxdma, un->un_maxbsize, un->un_minbsize,
	    (un->un_allow_large_xfer ? "ALLOW": "DON'T ALLOW"));

	err = st_cmd(dev, SCMD_TEST_UNIT_READY, 0, SYNC_CMD);

	if (err != 0) {
		if (err == EINTR) {
			un->un_laststate = un->un_state;
			un->un_state = ST_STATE_CLOSED;
			rval = EINTR;
			goto exit;
		}
		/*
		 * Make sure the tape is ready
		 */
		un->un_fileno = -1;
		if (un->un_status != KEY_UNIT_ATTENTION) {
			/*
			 * allow open no media.  Subsequent MTIOCSTATE
			 * with media present will complete the open
			 * logic.
			 */
			un->un_laststate = un->un_state;
			if (un->un_oflags & (FNONBLOCK|FNDELAY)) {
				un->un_mediastate = MTIO_EJECTED;
				un->un_state = ST_STATE_OFFLINE;
				rval = 0;
				goto exit;
			} else {
				un->un_state = ST_STATE_CLOSED;
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_tape_init EIO no media, not opened "
				    "O_NONBLOCK|O_EXCL\n");
				rval = EIO;
				goto exit;
			}
		}
	}

	/*
	 * On each open, initialize block size from drivetype struct,
	 * as it could have been changed by MTSRSZ ioctl.
	 * Now, ST_VARIABLE simply means drive is capable of variable
	 * mode. All drives are assumed to support fixed records.
	 * Hence, un_bsize tells what mode the drive is in.
	 *	un_bsize	= 0	- variable record length
	 *			= x	- fixed record length is x
	 */
	un->un_bsize = un->un_dp->bsize;

	if (un->un_restore_pos) {
		rval = st_validate_tapemarks(un, un->un_save_fileno,
		    un->un_save_blkno);
		if (rval != 0) {
			if (rval != EACCES) {
				rval = EIO;
			}
			un->un_restore_pos = 0;
			un->un_laststate = un->un_state;
			un->un_state = ST_STATE_CLOSED;
			goto exit;
		}
		un->un_restore_pos = 0;
	}

	if (un->un_fileno < 0) {
		rval = st_loadtape(dev);
		if (rval) {
			if (rval != EACCES) {
				rval = EIO;
			}
			un->un_laststate = un->un_state;
			un->un_state = ST_STATE_CLOSED;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_tape_init: %s can't open tape\n",
			    rval == EACCES ? "EACCES" : "EIO");
			goto exit;
		}
	}

	/*
	 * do a mode sense to pick up state of current write-protect,
	 * Could cause reserve and fail due to conflict.
	 */
	rval = st_modesense(un);
	if (rval == EACCES) {
		goto exit;
	}

	/*
	 * If we are opening the tape for writing, check
	 * to make sure that the tape can be written.
	 */
	if (un->un_oflags & FWRITE) {
		err = 0;
		if (un->un_mspl->wp) {
			un->un_status = KEY_WRITE_PROTECT;
			un->un_laststate = un->un_state;
			un->un_state = ST_STATE_CLOSED;
			rval = EACCES;
			/*
			 * STK sets the wp bit if volsafe tape is loaded.
			 */
			if ((un->un_dp->type == MT_ISSTK9840) &&
			    (un->un_dp->options & ST_WORMABLE)) {
				un->un_read_only = RDONLY;
			} else {
				goto exit;
			}
		} else {
			un->un_read_only = RDWR;
		}
	} else {
		un->un_read_only = RDONLY;
	}

	if (un->un_dp->options & ST_WORMABLE) {
		un->un_read_only |= un->un_wormable(un);

		if (((un->un_read_only == WORM) ||
		    (un->un_read_only == RDWORM)) &&
		    ((un->un_oflags & FWRITE) == FWRITE)) {
			un->un_status = KEY_DATA_PROTECT;
			rval = EACCES;
			ST_DEBUG4(ST_DEVINFO, st_label, CE_NOTE,
			    "read_only = %d eof = %d oflag = %d\n",
			    un->un_read_only, un->un_eof, un->un_oflags);
		}
	}

	/*
	 * If we're opening the tape write-only, we need to
	 * write 2 filemarks on the HP 1/2 inch drive, to
	 * create a null file.
	 */
	if ((un->un_read_only == RDWR) ||
	    (un->un_read_only == WORM) && (un->un_oflags & FWRITE)) {
		if (un->un_dp->options & ST_REEL) {
			un->un_fmneeded = 2;
		} else {
			un->un_fmneeded = 1;
		}
	} else {
		un->un_fmneeded = 0;
	}

	ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "fmneeded = %x\n", un->un_fmneeded);

	/*
	 * Make sure the density can be selected correctly.
	 * If WORM can only write at the append point which in most cases
	 * isn't BOP. st_determine_density() with a B_WRITE only attempts
	 * to set and try densities if a BOP.
	 */
	if (st_determine_density(dev,
	    un->un_read_only == RDWR ? B_WRITE : B_READ)) {
		un->un_status = KEY_ILLEGAL_REQUEST;
		un->un_laststate = un->un_state;
		un->un_state = ST_STATE_CLOSED;
		ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_tape_init: EIO can't determine density\n");
		rval = EIO;
		goto exit;
	}

	/*
	 * Destroy the knowledge that we have 'determined'
	 * density so that a later read at BOT comes along
	 * does the right density determination.
	 */

	un->un_density_known = 0;


	/*
	 * Okay, the tape is loaded and either at BOT or somewhere past.
	 * Mark the state such that any I/O or tape space operations
	 * will get/set the right density, etc..
	 */
	un->un_laststate = un->un_state;
	un->un_lastop = ST_OP_NIL;
	un->un_mediastate = MTIO_INSERTED;
	cv_broadcast(&un->un_state_cv);

	/*
	 *  Set test append flag if writing.
	 *  First write must check that tape is positioned correctly.
	 */
	un->un_test_append = (un->un_oflags & FWRITE);

exit:
	un->un_err_resid = 0;
	un->un_last_resid = 0;
	un->un_last_count = 0;

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_tape_init: return val = %x\n", rval);
	return (rval);

}



/* ARGSUSED */
static int
st_close(dev_t dev, int flag, int otyp, cred_t *cred_p)
{
	int err = 0;
	int norew, count, last_state;
#if defined(__i386) || defined(__amd64)
	struct contig_mem *cp, *cp_temp;
#endif

	GET_SOFT_STATE(dev);

	/*
	 * wait till all cmds in the pipeline have been completed
	 */
	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_close(dev = 0x%lx, flag = %d, otyp = %d)\n", dev, flag, otyp);

	st_wait_for_io(un);

	/* turn off persistent errors on close, as we want close to succeed */
	TURN_PE_OFF(un);

	/*
	 * set state to indicate that we are in process of closing
	 */
	last_state = un->un_laststate = un->un_state;
	un->un_state = ST_STATE_CLOSING;

	/*
	 * BSD behavior:
	 * a close always causes a silent span to the next file if we've hit
	 * an EOF (but not yet read across it).
	 */
	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_close1: fileno=%x, blkno=%lx, un_eof=%x\n", un->un_fileno,
	    un->un_blkno, un->un_eof);


	if (BSD_BEHAVIOR && (un->un_eof == ST_EOF)) {
		if (un->un_fileno >= 0) {
			un->un_fileno++;
			un->un_blkno = 0;
		}
		un->un_eof = ST_NO_EOF;
	}

	/*
	 * rewinding?
	 */
	norew = (getminor(dev) & MT_NOREWIND);

	/*
	 * SVR4 behavior for skipping to next file:
	 *
	 * If we have not seen a filemark, space to the next file
	 *
	 * If we have already seen the filemark we are physically in the next
	 * file and we only increment the filenumber
	 */

	if (norew && SVR4_BEHAVIOR && (flag & FREAD) && (un->un_blkno != 0) &&
	    (un->un_lastop != ST_OP_WRITE)) {
		switch (un->un_eof) {
		case ST_NO_EOF:
			/*
			 * if we were reading and did not read the complete file
			 * skip to the next file, leaving the tape correctly
			 * positioned to read the first record of the next file
			 * Check first for REEL if we are at EOT by trying to
			 * read a block
			 */
			if ((un->un_dp->options & ST_REEL) &&
				(!(un->un_dp->options & ST_READ_IGNORE_EOFS)) &&
			    (un->un_blkno == 0)) {
				if (st_cmd(dev, SCMD_SPACE, Blk(1), SYNC_CMD)) {
					ST_DEBUG2(ST_DEVINFO, st_label,
					    SCSI_DEBUG,
					    "st_close : EIO can't space\n");
					err = EIO;
					break;
				}
				if (un->un_eof >= ST_EOF_PENDING) {
					un->un_eof = ST_EOT_PENDING;
					un->un_fileno += 1;
					un->un_blkno   = 0;
					break;
				}
			}
			if (st_cmd(dev, SCMD_SPACE, Fmk(1), SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_close: EIO can't space #2\n");
				err = EIO;
			} else {
				ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_close2: fileno=%x,blkno=%lx,"
				    "un_eof=%x\n",
				    un->un_fileno, un->un_blkno, un->un_eof);
				un->un_eof = ST_NO_EOF;
			}
			break;

		case ST_EOF_PENDING:
		case ST_EOF:
			un->un_fileno += 1;
			un->un_blkno   = 0;
			un->un_eof = ST_NO_EOF;
			break;

		case ST_EOT:
		case ST_EOT_PENDING:
			/* nothing to do */
			break;
		default:
			scsi_log(ST_DEVINFO, st_label, CE_PANIC,
			    "Undefined state 0x%x", un->un_eof);

		}
	}


	/*
	 * For performance reasons (HP 88780), the driver should
	 * postpone writing the second tape mark until just before a file
	 * positioning ioctl is issued (e.g., rewind).	This means that
	 * the user must not manually rewind the tape because the tape will
	 * be missing the second tape mark which marks EOM.
	 * However, this small performance improvement is not worth the risk.
	 */

	/*
	 * We need to back up over the filemark we inadvertently popped
	 * over doing a read in between the two filemarks that constitute
	 * logical eot for 1/2" tapes. Note that ST_EOT_PENDING is only
	 * set while reading.
	 *
	 * If we happen to be at physical eot (ST_EOM) (writing case),
	 * the writing of filemark(s) will clear the ST_EOM state, which
	 * we don't want, so we save this state and restore it later.
	 */

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "flag=%x, fmneeded=%x, lastop=%x, eof=%x\n",
		flag, un->un_fmneeded, un->un_lastop, un->un_eof);

	if (un->un_eof == ST_EOT_PENDING) {
		if (norew) {
			if (st_cmd(dev, SCMD_SPACE, Fmk((-1)), SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_close: EIO can't space #3\n");
				err = EIO;
			} else {
				un->un_blkno = 0;
				un->un_eof = ST_EOT;
			}
		} else {
			un->un_eof = ST_NO_EOF;
		}

	/*
	 * Do we need to write a file mark?
	 *
	 * only write filemarks if there are fmks to be written and
	 *   - open for write (possibly read/write)
	 *   - the last operation was a write
	 * or:
	 *   -	opened for wronly
	 *   -	no data was written
	 */
	} else if ((un->un_fileno >= 0) && (un->un_fmneeded > 0) &&
	    (((flag & FWRITE) && (un->un_lastop == ST_OP_WRITE)) ||
	    ((flag & FWRITE) && (un->un_lastop == ST_OP_WEOF)) ||
	    ((flag == FWRITE) && (un->un_lastop == ST_OP_NIL)))) {

		/* save ST_EOM state */
		int was_at_eom = (un->un_eof == ST_EOM) ? 1 : 0;

		/*
		 * Note that we will write a filemark if we had opened
		 * the tape write only and no data was written, thus
		 * creating a null file.
		 *
		 * If the user already wrote one, we only have to write 1 more.
		 * If they wrote two, we don't have to write any.
		 */

		count = un->un_fmneeded;
		if (count > 0) {
			if (st_cmd(dev, SCMD_WRITE_FILE_MARK,
			    count, SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_close : EIO can't wfm\n");
				err = EIO;
			}
			if ((un->un_dp->options & ST_REEL) && norew) {
				if (st_cmd(dev, SCMD_SPACE, Fmk((-1)),
				    SYNC_CMD)) {
					ST_DEBUG2(ST_DEVINFO, st_label,
					    SCSI_DEBUG,
					    "st_close : EIO space fmk(-1)\n");
					err = EIO;
				}
				un->un_eof = ST_NO_EOF;
				/* fix up block number */
				un->un_blkno = 0;
			}
		}

		/*
		 * If we aren't going to be rewinding, and we were at
		 * physical eot, restore the state that indicates we
		 * are at physical eot. Once you have reached physical
		 * eot, and you close the tape, the only thing you can
		 * do on the next open is to rewind. Access to trailer
		 * records is only allowed without closing the device.
		 */
		if (norew == 0 && was_at_eom)
			un->un_eof = ST_EOM;
	}

	/*
	 * report soft errors if enabled and available, if we never accessed
	 * the drive, don't get errors. This will prevent some DAT error
	 * messages upon LOG SENSE.
	 */
	if (st_report_soft_errors_on_close &&
	    (un->un_dp->options & ST_SOFT_ERROR_REPORTING) &&
	    (last_state != ST_STATE_OFFLINE)) {
		(void) st_report_soft_errors(dev, flag);
	}


	/*
	 * Do we need to rewind? Can we rewind?
	 */
	if (norew == 0 && un->un_fileno >= 0 && err == 0) {
		/*
		 * We'd like to rewind with the
		 * 'immediate' bit set, but this
		 * causes problems on some drives
		 * where subsequent opens get a
		 * 'NOT READY' error condition
		 * back while the tape is rewinding,
		 * which is impossible to distinguish
		 * from the condition of 'no tape loaded'.
		 *
		 * Also, for some targets, if you disconnect
		 * with the 'immediate' bit set, you don't
		 * actually return right away, i.e., the
		 * target ignores your request for immediate
		 * return.
		 *
		 * Instead, we'll fire off an async rewind
		 * command. We'll mark the device as closed,
		 * and any subsequent open will stall on
		 * the first TEST_UNIT_READY until the rewind
		 * completes.
		 */

		/*
		 * Used to be if reserve was not supported we'd send an
		 * asynchronious rewind. Comments above may be slightly invalid
		 * as the immediate bit was never set. Doing an immedate rewind
		 * makes sense, I think fixes to not ready status might handle
		 * the problems described above.
		 */
		if (un->un_sd->sd_inq->inq_ansi < 2) {
			(void) st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD);
		} else {
			(void) st_cmd(dev, SCMD_REWIND, 0, ASYNC_CMD);
		}
	}

	/*
	 * eject tape if necessary
	 */
	if (un->un_eject_tape_on_failure) {
		un->un_eject_tape_on_failure = 0;
		if (st_cmd(dev, SCMD_LOAD, 0, SYNC_CMD)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_close : can't unload tape\n");
		} else {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_close : tape unloaded \n");
			un->un_eof = ST_NO_EOF;
			un->un_mediastate = MTIO_EJECTED;
		}
	}
	/*
	 * Release the tape unit, if default reserve/release
	 * behaviour.
	 */
	if ((un->un_rsvd_status &
	    (ST_RESERVE | ST_PRESERVE_RESERVE)) == ST_RESERVE) {
		(void) st_reserve_release(un, ST_RELEASE);
	}

	/*
	 * clear up state
	 */
	un->un_laststate = un->un_state;
	un->un_state = ST_STATE_CLOSED;
	un->un_lastop = ST_OP_NIL;
	un->un_throttle = 1;	/* assume one request at time, for now */
	un->un_retry_ct = 0;
	un->un_tran_retry_ct = 0;
	un->un_errno = 0;
	un->un_swr_token = (opaque_t)NULL;
	un->un_rsvd_status &= ~(ST_INIT_RESERVE);

	/* Restore the options to the init time settings */
	if (un->un_init_options & ST_READ_IGNORE_ILI) {
		un->un_dp->options |= ST_READ_IGNORE_ILI;
	} else {
		un->un_dp->options &= ~ST_READ_IGNORE_ILI;
	}

	if (un->un_init_options & ST_READ_IGNORE_EOFS) {
		un->un_dp->options |= ST_READ_IGNORE_EOFS;
	} else {
		un->un_dp->options &= ~ST_READ_IGNORE_EOFS;
	}

	if (un->un_init_options & ST_SHORT_FILEMARKS) {
		un->un_dp->options |= ST_SHORT_FILEMARKS;
	} else {
		un->un_dp->options &= ~ST_SHORT_FILEMARKS;
	}

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * Signal anyone awaiting a close operation to complete.
	 */
	cv_signal(&un->un_clscv);

	/*
	 * any kind of error on closing causes all state to be tossed
	 */
	if (err && un->un_status != KEY_ILLEGAL_REQUEST) {
		un->un_density_known = 0;
		/*
		 * note that st_intr has already set un_fileno to -1
		 */
	}

#if defined(__i386) || defined(__amd64)
	/*
	 * free any contiguous mem alloc'ed for big block I/O
	 */
	cp = un->un_contig_mem;
	while (cp) {
		if (cp->cm_addr) {
			ddi_dma_mem_free(&cp->cm_acc_hdl);
		}
		cp_temp = cp;
		cp = cp->cm_next;
		kmem_free(cp_temp,
		    sizeof (struct contig_mem) + biosize());
	}
	un->un_contig_mem_total_num = 0;
	un->un_contig_mem_available_num = 0;
	un->un_contig_mem = NULL;
	un->un_max_contig_mem_len = 0;
#endif

	ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_close3: return val = %x, fileno=%x, blkno=%lx, un_eof=%x\n",
			    err, un->un_fileno, un->un_blkno, un->un_eof);

	mutex_exit(ST_MUTEX);
	return (err);
}

/*
 * These routines perform raw i/o operations.
 */

/* ARGSUSED2 */
static int
st_aread(dev_t dev, struct aio_req *aio, cred_t *cred_p)
{
	return (st_arw(dev, aio, B_READ));
}


/* ARGSUSED2 */
static int
st_awrite(dev_t dev, struct aio_req *aio, cred_t *cred_p)
{
	return (st_arw(dev, aio, B_WRITE));
}



/* ARGSUSED */
static int
st_read(dev_t dev, struct uio *uiop, cred_t *cred_p)
{
	return (st_rw(dev, uiop, B_READ));
}

/* ARGSUSED */
static int
st_write(dev_t dev, struct uio *uiop, cred_t *cred_p)
{
	return (st_rw(dev, uiop, B_WRITE));
}

/*
 * Due to historical reasons, old limits are: For variable-length devices:
 * if greater than 64KB - 1 (ST_MAXRECSIZE_VARIABLE), block into 64 KB - 2
 * ST_MAXRECSIZE_VARIABLE_LIMIT) requests; otherwise,
 * (let it through unmodified. For fixed-length record devices:
 * 63K (ST_MAXRECSIZE_FIXED) is max (default minphys).
 *
 * The new limits used are un_maxdma (retrieved using scsi_ifgetcap()
 * from the HBA) and un_maxbsize (retrieved by sending SCMD_READ_BLKLIM
 * command to the drive).
 *
 */
static void
st_minphys(struct buf *bp)
{
	struct scsi_tape *un;

	un = ddi_get_soft_state(st_state, MTUNIT(bp->b_edev));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_minphys(bp = 0x%p): b_bcount = 0x%lx\n", (void *)bp,
	    bp->b_bcount);

	if (un->un_allow_large_xfer) {

		/*
		 * check un_maxbsize for variable length devices only
		 */
		if (un->un_bsize == 0 && bp->b_bcount > un->un_maxbsize) {
			bp->b_bcount = un->un_maxbsize;
		}
		/*
		 * can't go more that HBA maxdma limit in either fixed-length
		 * or variable-length tape drives.
		 */
		if (bp->b_bcount > un->un_maxdma) {
			bp->b_bcount = un->un_maxdma;
		}
	} else {

		/*
		 *  use old fixed limits
		 */
		if (un->un_bsize == 0) {
			if (bp->b_bcount > ST_MAXRECSIZE_VARIABLE) {
				bp->b_bcount = ST_MAXRECSIZE_VARIABLE_LIMIT;
			}
		} else {
			if (bp->b_bcount > ST_MAXRECSIZE_FIXED) {
				bp->b_bcount = ST_MAXRECSIZE_FIXED;
			}
		}
	}

	/*
	 * For regular raw I/O and Fixed Block length devices, make sure
	 * the adjusted block count is a whole multiple of the device
	 * block size.
	 */
	if (bp != un->un_sbufp && un->un_bsize) {
		bp->b_bcount -= (bp->b_bcount % un->un_bsize);
	}
}

/*ARGSUSED*/
static void
st_uscsi_minphys(struct buf *bp)
{
	/*
	 * do not break up because the CDB count would then be
	 * incorrect and create spurious data underrun errors.
	 */
}

static int
st_rw(dev_t dev, struct uio *uio, int flag)
{
	int rval = 0;
	long len;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_rw(dev = 0x%lx, flag = %s)\n", dev,
	    (flag == B_READ ? rd_str: wr_str));

	/* get local copy of transfer length */
	len = uio->uio_iov->iov_len;

	mutex_enter(ST_MUTEX);

	/*
	 * If in fixed block size mode and requested read or write
	 * is not an even multiple of that block size.
	 */
	if ((un->un_bsize != 0) && (len % un->un_bsize != 0)) {
		scsi_log(ST_DEVINFO, st_label, CE_WARN,
		    "%s: not modulo %d block size\n",
		    (flag == B_WRITE) ? wr_str : rd_str, un->un_bsize);
		rval = EINVAL;
	}

	/* If device has set granularity in the READ_BLKLIM we honor it. */
	if ((un->un_data_mod != 0) && (len % un->un_data_mod != 0)) {
		scsi_log(ST_DEVINFO, st_label, CE_WARN,
		    "%s: not modulo %d device granularity\n",
		    (flag == B_WRITE) ? wr_str : rd_str, un->un_data_mod);
		rval = EINVAL;
	}

	if (rval != 0) {
		un->un_errno = rval;
		mutex_exit(ST_MUTEX);
		return (rval);
	}

	un->un_silent_skip = 0;
	mutex_exit(ST_MUTEX);

	len = uio->uio_resid;

	rval = physio(st_strategy, (struct buf *)NULL,
		dev, flag, st_minphys, uio);
	/*
	 * if we have hit logical EOT during this xfer and there is not a
	 * full residue, then set un_eof back  to ST_EOM to make sure that
	 * the user will see at least one zero write
	 * after this short write
	 */
	mutex_enter(ST_MUTEX);
	if (un->un_eof > ST_NO_EOF) {
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		"un_eof=%d resid=%lx\n", un->un_eof, uio->uio_resid);
	}
	if (un->un_eof >= ST_EOM && (flag == B_WRITE)) {
		if ((uio->uio_resid != len) && (uio->uio_resid != 0)) {
			un->un_eof = ST_EOM;
		} else if (uio->uio_resid == len) {
			un->un_eof = ST_NO_EOF;
		}
	}

	if (un->un_silent_skip && uio->uio_resid != len) {
		un->un_eof = ST_EOF;
		un->un_blkno = un->un_save_blkno;
		un->un_fileno--;
	}

	un->un_errno = rval;

	mutex_exit(ST_MUTEX);

	return (rval);
}

static int
st_arw(dev_t dev, struct aio_req *aio, int flag)
{
	struct uio *uio = aio->aio_uio;
	int rval = 0;
	long len;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_rw(dev = 0x%lx, flag = %s)\n", dev,
	    (flag == B_READ ? rd_str: wr_str));

	/* get local copy of transfer length */
	len = uio->uio_iov->iov_len;

	mutex_enter(ST_MUTEX);

	/*
	 * If in fixed block size mode and requested read or write
	 * is not an even multiple of that block size.
	 */
	if ((un->un_bsize != 0) && (len % un->un_bsize != 0)) {
		scsi_log(ST_DEVINFO, st_label, CE_WARN,
		    "%s: not modulo %d block size\n",
		    (flag == B_WRITE) ? wr_str : rd_str, un->un_bsize);
		rval = EINVAL;
	}

	/* If device has set granularity in the READ_BLKLIM we honor it. */
	if ((un->un_data_mod != 0) && (len % un->un_data_mod != 0)) {
		scsi_log(ST_DEVINFO, st_label, CE_WARN,
		    "%s: not modulo %d device granularity\n",
		    (flag == B_WRITE) ? wr_str : rd_str, un->un_data_mod);
		rval = EINVAL;
	}

	if (rval != 0) {
		un->un_errno = rval;
		mutex_exit(ST_MUTEX);
		return (rval);
	}

	mutex_exit(ST_MUTEX);

	len = uio->uio_resid;

	rval = aphysio(st_strategy, anocancel, dev, flag, st_minphys, aio);

	/*
	 * if we have hit logical EOT during this xfer and there is not a
	 * full residue, then set un_eof back  to ST_EOM to make sure that
	 * the user will see at least one zero write
	 * after this short write
	 *
	 * we keep this here just in case the application is not using
	 * persistent errors
	 */
	mutex_enter(ST_MUTEX);
	if (un->un_eof > ST_NO_EOF) {
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "un_eof=%d resid=%lx\n", un->un_eof, uio->uio_resid);
	}
	if (un->un_eof >= ST_EOM && (flag == B_WRITE)) {
		if ((uio->uio_resid != len) && (uio->uio_resid != 0)) {
			un->un_eof = ST_EOM;
		} else if (uio->uio_resid == len && !IS_PE_FLAG_SET(un)) {
			un->un_eof = ST_NO_EOF;
		}
	}
	un->un_errno = rval;
	mutex_exit(ST_MUTEX);

	return (rval);
}



static int
st_strategy(struct buf *bp)
{
	struct scsi_tape *un;
	dev_t dev = bp->b_edev;

	/*
	 * validate arguments
	 */
	if ((un = ddi_get_soft_state(st_state, MTUNIT(bp->b_edev))) == NULL) {
		bp->b_resid = bp->b_bcount;
		mutex_enter(ST_MUTEX);
		st_bioerror(bp, ENXIO);
		mutex_exit(ST_MUTEX);
		goto error;
	}

	mutex_enter(ST_MUTEX);

	while (un->un_pwr_mgmt == ST_PWR_SUSPENDED) {
		cv_wait(&un->un_suspend_cv, ST_MUTEX);
	}

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_strategy(): bcount=0x%lx, fileno=%d, blkno=%lx, eof=%d\n",
	    bp->b_bcount, un->un_fileno, un->un_blkno, un->un_eof);

	/*
	 * If persistent errors have been flagged, just nix this one. We wait
	 * for any outstanding I/O's below, so we will be in order.
	 */
	if (IS_PE_FLAG_SET(un))
		goto exit;

	if (bp != un->un_sbufp) {
		char reading = bp->b_flags & B_READ;
		int wasopening = 0;

		/*
		 * If we haven't done/checked reservation on the tape unit
		 * do it now.
		 */
		if ((un->un_rsvd_status &
		    (ST_RESERVE | ST_APPLICATION_RESERVATIONS)) == 0) {
			if ((un->un_dp->options & ST_NO_RESERVE_RELEASE) == 0) {
				if (st_reserve_release(un, ST_RESERVE)) {
					st_bioerror(bp, un->un_errno);
					goto exit;
				}
			} else if (un->un_state == ST_STATE_OPEN_PENDING_IO) {
				/*
				 * Enter here to restore position for possible
				 * resets when the device was closed and opened
				 * in O_NDELAY mode subsequently
				 */
				un->un_state = ST_STATE_INITIALIZING;
				(void) st_cmd(dev, SCMD_TEST_UNIT_READY,
				    0, SYNC_CMD);
				un->un_state = ST_STATE_OPEN_PENDING_IO;
			}
			un->un_rsvd_status |= ST_INIT_RESERVE;
		}

		/*
		 * If we are offline, we have to initialize everything first.
		 * This is to handle either when opened with O_NDELAY, or
		 * we just got a new tape in the drive, after an offline.
		 * We don't observe O_NDELAY past the open,
		 * as it will not make sense for tapes.
		 */
		if (un->un_state == ST_STATE_OFFLINE || un->un_restore_pos) {
			/* reset state to avoid recursion */
			un->un_state = ST_STATE_INITIALIZING;
			if (st_tape_init(dev)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "stioctl : OFFLINE init failure ");
				un->un_state = ST_STATE_OFFLINE;
				un->un_fileno = -1;
				goto b_done_err;
			}
			un->un_state = ST_STATE_OPEN_PENDING_IO;
		}
		/*
		 * Check for legal operations
		 */
		if (un->un_fileno < 0) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "strategy with un->un_fileno < 0\n");
			goto b_done_err;
		}

		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_strategy(): regular io\n");

		/*
		 * Process this first. If we were reading, and we're pending
		 * logical eot, that means we've bumped one file mark too far.
		 */

		/*
		 * Recursion warning: st_cmd will route back through here.
		 */
		if (un->un_eof == ST_EOT_PENDING) {
			if (st_cmd(dev, SCMD_SPACE, Fmk((-1)), SYNC_CMD)) {
				un->un_fileno = -1;
				un->un_density_known = 0;
				goto b_done_err;
			}
			un->un_blkno = 0; /* fix up block number.. */
			un->un_eof = ST_EOT;
		}

		/*
		 * If we are in the process of opening, we may have to
		 * determine/set the correct density. We also may have
		 * to do a test_append (if QIC) to see whether we are
		 * in a position to append to the end of the tape.
		 *
		 * If we're already at logical eot, we transition
		 * to ST_NO_EOF. If we're at physical eot, we punt
		 * to the switch statement below to handle.
		 */
		if ((un->un_state == ST_STATE_OPEN_PENDING_IO) ||
		    (un->un_test_append && (un->un_dp->options & ST_QIC))) {

			if (un->un_state == ST_STATE_OPEN_PENDING_IO) {
				if (st_determine_density(dev, (int)reading)) {
					goto b_done_err;
				}
			}

			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "pending_io@fileno %d rw %d qic %d eof %d\n",
			    un->un_fileno, (int)reading,
			    (un->un_dp->options & ST_QIC) ? 1 : 0,
			    un->un_eof);

			if (!reading && un->un_eof != ST_EOM) {
				if (un->un_eof == ST_EOT) {
					un->un_eof = ST_NO_EOF;
				} else if (un->un_fileno > 0 &&
				    (un->un_dp->options & ST_QIC)) {
					/*
					 * st_test_append() will do it all
					 */
					st_test_append(bp);
					goto done;
				}
			}
			if (un->un_state == ST_STATE_OPEN_PENDING_IO) {
				wasopening = 1;
			}
			un->un_laststate = un->un_state;
			un->un_state = ST_STATE_OPEN;
		}


		/*
		 * Process rest of END OF FILE and END OF TAPE conditions
		 */

		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "un_eof=%x, wasopening=%x\n",
		    un->un_eof, wasopening);

		switch (un->un_eof) {
		case ST_EOM:
			/*
			 * This allows writes to proceed past physical
			 * eot. We'll *really* be in trouble if the
			 * user continues blindly writing data too
			 * much past this point (unwind the tape).
			 * Physical eot really means 'early warning
			 * eot' in this context.
			 *
			 * Every other write from now on will succeed
			 * (if sufficient  tape left).
			 * This write will return with resid == count
			 * but the next one should be successful
			 *
			 * Note that we only transition to logical EOT
			 * if the last state wasn't the OPENING state.
			 * We explicitly prohibit running up to physical
			 * eot, closing the device, and then re-opening
			 * to proceed. Trailer records may only be gotten
			 * at by keeping the tape open after hitting eot.
			 *
			 * Also note that ST_EOM cannot be set by reading-
			 * this can only be set during writing. Reading
			 * up to the end of the tape gets a blank check
			 * or a double-filemark indication (ST_EOT_PENDING),
			 * and we prohibit reading after that point.
			 *
			 */
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "EOM\n");
			if (wasopening == 0) {
				/*
				 * this allows st_rw() to reset it back to
				 * ST_EOM to make sure that the application
				 * will see a zero write
				 */
				un->un_eof = ST_WRITE_AFTER_EOM;
			}
			un->un_status = SUN_KEY_EOT;
			goto b_done;

		case ST_WRITE_AFTER_EOM:
		case ST_EOT:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "EOT\n");
			un->un_status = SUN_KEY_EOT;
			if (SVR4_BEHAVIOR && reading) {
				goto b_done_err;
			}

			if (reading) {
				goto b_done;
			}
			un->un_eof = ST_NO_EOF;
			break;

		case ST_EOF_PENDING:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "EOF PENDING\n");
			un->un_status = SUN_KEY_EOF;
			if (SVR4_BEHAVIOR) {
				un->un_eof = ST_EOF;
				goto b_done;
			}
			/* FALLTHROUGH */
		case ST_EOF:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "EOF\n");
			un->un_status = SUN_KEY_EOF;
			if (SVR4_BEHAVIOR) {
				goto b_done_err;
			}

			if (BSD_BEHAVIOR) {
				un->un_eof = ST_NO_EOF;
				un->un_fileno += 1;
				un->un_blkno   = 0;
			}

			if (reading) {
				ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "now file %d (read)\n",
				    un->un_fileno);
				goto b_done;
			}
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "now file %d (write)\n", un->un_fileno);
			break;
		default:
			un->un_status = 0;
			break;
		}
	}

	bp->b_flags &= ~(B_DONE);
	st_bioerror(bp, 0);
	bp->av_forw = NULL;
	bp->b_resid = 0;
	SET_BP_PKT(bp, 0);


	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_strategy: cmd=0x%p  count=%ld  resid=%ld flags=0x%x"
	    " pkt=0x%p\n",
	    (void *)bp->b_forw, bp->b_bcount,
	    bp->b_resid, bp->b_flags, (void *)BP_PKT(bp));

#if defined(__i386) || defined(__amd64)
	/*
	 * We will replace bp with a new bp that can do big blk xfer
	 * if the requested xfer size is bigger than ST_BIGBLK_XFER
	 *
	 * Also, we need to make sure that we're handling real I/O
	 * by checking group 0/1 SCSI I/O commands, if needed
	 */
	if (bp->b_bcount > ST_BIGBLK_XFER &&
	    (bp != un->un_sbufp					||
	    (uchar_t)(uintptr_t)bp->b_forw == SCMD_READ		||
	    (uchar_t)(uintptr_t)bp->b_forw == SCMD_READ_G1	||
	    (uchar_t)(uintptr_t)bp->b_forw == SCMD_WRITE	||
	    (uchar_t)(uintptr_t)bp->b_forw == SCMD_WRITE_G1)) {
		mutex_exit(ST_MUTEX);
		bp = st_get_bigblk_bp(bp);
		mutex_enter(ST_MUTEX);
	}
#endif

	/* put on wait queue */
	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_strategy: un->un_quef = 0x%p, bp = 0x%p\n",
	    (void *)un->un_quef, (void *)bp);

	if (un->un_quef) {
		un->un_quel->b_actf = bp;
	} else {
		un->un_quef = bp;
	}
	un->un_quel = bp;

	ST_DO_KSTATS(bp, kstat_waitq_enter);

	st_start(un);

done:
	mutex_exit(ST_MUTEX);
	return (0);


error:
	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_strategy: error exit\n");

	biodone(bp);
	return (0);

b_done_err:
	st_bioerror(bp, EIO);
	ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_strategy : EIO b_done_err\n");

b_done:
	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_strategy: b_done\n");

exit:
	/*
	 * make sure no commands are outstanding or waiting before closing,
	 * so we can guarantee order
	 */
	st_wait_for_io(un);
	un->un_err_resid = bp->b_resid = bp->b_bcount;

	/* override errno here, if persistent errors were flagged */
	if (IS_PE_FLAG_SET(un))
		bioerror(bp, un->un_errno);

	mutex_exit(ST_MUTEX);

	biodone(bp);
	ASSERT(mutex_owned(ST_MUTEX) == 0);
	return (0);
}



/*
 * this routine spaces forward over filemarks
 */
static int
st_space_fmks(dev_t dev, int count)
{
	int rval = 0;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_space_fmks(dev = 0x%lx, count = %d)\n", dev, count);

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * the risk with doing only one space operation is that we
	 * may accidentily jump in old data
	 * the exabyte 8500 reading 8200 tapes cannot use KNOWS_EOD
	 * because the 8200 does not append a marker; in order not to
	 * sacrifice the fast file skip, we do a slow skip if the low
	 * density device has been opened
	 */

	if ((un->un_dp->options & ST_KNOWS_EOD) &&
	    !((un->un_dp->type == ST_TYPE_EXB8500 && MT_DENSITY(dev) == 0))) {
		if (st_cmd(dev, SCMD_SPACE, Fmk(count), SYNC_CMD)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "space_fmks : EIO can't do space cmd #1\n");
			rval = EIO;
		}
	} else {
		while (count > 0) {
			if (st_cmd(dev, SCMD_SPACE, Fmk(1), SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "space_fmks : EIO can't do space cmd #2\n");
				rval = EIO;
				break;
			}
			count -= 1;
			/*
			 * read a block to see if we have reached
			 * end of medium (double filemark for reel or
			 * medium error for others)
			 */
			if (count > 0) {
				if (st_cmd(dev, SCMD_SPACE, Blk(1),
				    SYNC_CMD)) {
					ST_DEBUG2(ST_DEVINFO, st_label,
					    SCSI_DEBUG,
					    "space_fmks : EIO can't do "
					    "space cmd #3\n");
					rval = EIO;
					break;
				}
				if ((un->un_eof >= ST_EOF_PENDING) &&
				    (un->un_dp->options & ST_REEL)) {
					un->un_status = SUN_KEY_EOT;
					ST_DEBUG2(ST_DEVINFO, st_label,
					    SCSI_DEBUG,
					    "space_fmks : EIO ST_REEL\n");
					rval = EIO;
					break;
				} else if (IN_EOF(un)) {
					un->un_eof = ST_NO_EOF;
					un->un_fileno++;
					un->un_blkno = 0;
					count--;
				} else if (un->un_eof > ST_EOF) {
					ST_DEBUG2(ST_DEVINFO, st_label,
					    SCSI_DEBUG,
					    "space_fmks, EIO > ST_EOF\n");
					rval = EIO;
					break;
				}

			}
		}
		un->un_err_resid = count;
		un->un_err_fileno = un->un_fileno;
		un->un_err_blkno = un->un_blkno;
	}
	ASSERT(mutex_owned(ST_MUTEX));
	return (rval);
}

/*
 * this routine spaces to EOM
 *
 * it keeps track of the current filenumber and returns the filenumber after
 * the last successful space operation, we keep the number high because as
 * tapes are getting larger, the possibility of more and more files exist,
 * 0x100000 (1 Meg of files) probably will never have to be changed any time
 * soon
 */
#define	MAX_SKIP	0x100000 /* somewhat arbitrary */

static int
st_find_eom(dev_t dev)
{
	int count, savefile;
	struct scsi_tape *un;
	int instance;

	instance = MTUNIT(dev);
	if ((un = ddi_get_soft_state(st_state, instance)) == NULL)
		return (-1);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_find_eom(dev = 0x%lx): fileno = %d\n", dev, un->un_fileno);

	ASSERT(mutex_owned(ST_MUTEX));

	savefile = un->un_fileno;

	/*
	 * see if the drive is smart enough to do the skips in
	 * one operation; 1/2" use two filemarks
	 * the exabyte 8500 reading 8200 tapes cannot use KNOWS_EOD
	 * because the 8200 does not append a marker; in order not to
	 * sacrifice the fast file skip, we do a slow skip if the low
	 * density device has been opened
	 */
	if ((un->un_dp->options & ST_KNOWS_EOD) &&
	    !((un->un_dp->type == ST_TYPE_EXB8500 && MT_DENSITY(dev) == 0))) {
		count = MAX_SKIP;
	} else {
		count = 1;
	}

	while (st_cmd(dev, SCMD_SPACE, Fmk(count), SYNC_CMD) == 0) {

		savefile = un->un_fileno;
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			"count=%x, eof=%x, status=%x\n", count,  un->un_eof,
			un->un_status);

		/*
		 * If we're not EOM smart,  space a record
		 * to see whether we're now in the slot between
		 * the two sequential filemarks that logical
		 * EOM consists of (REEL) or hit nowhere land
		 * (8mm).
		 */
		if (count == 1) {
			/*
			 * no fast skipping, check a record
			 */
			if (st_cmd(dev, SCMD_SPACE, Blk((1)), SYNC_CMD))
				break;
			else if ((un->un_eof >= ST_EOF_PENDING) &&
			    (un->un_dp->options & ST_REEL)) {
				un->un_status = KEY_BLANK_CHECK;
				un->un_fileno++;
				un->un_blkno = 0;
				break;
			} else if (IN_EOF(un)) {
				un->un_eof = ST_NO_EOF;
				un->un_fileno++;
				un->un_blkno = 0;
			} else if (un->un_eof > ST_EOF) {
				break;
			}
		} else {
			if (un->un_eof >  ST_EOF) {
				break;
			}
		}
	}

	if (un->un_dp->options & ST_KNOWS_EOD) {
		savefile = un->un_fileno;
	}
	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_find_eom: %x\n", savefile);
	ASSERT(mutex_owned(ST_MUTEX));
	return (savefile);
}


/*
 * this routine is frequently used in ioctls below;
 * it determines whether we know the density and if not will
 * determine it
 * if we have written the tape before, one or more filemarks are written
 *
 * depending on the stepflag, the head is repositioned to where it was before
 * the filemarks were written in order not to confuse step counts
 */
#define	STEPBACK    0
#define	NO_STEPBACK 1

static int
st_check_density_or_wfm(dev_t dev, int wfm, int mode, int stepflag)
{

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	"st_check_density_or_wfm(dev= 0x%lx, wfm= %d, mode= %d, stpflg= %d)\n",
	dev, wfm, mode, stepflag);

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * If we don't yet know the density of the tape we have inserted,
	 * we have to either unconditionally set it (if we're 'writing'),
	 * or we have to determine it. As side effects, check for any
	 * write-protect errors, and for the need to put out any file-marks
	 * before positioning a tape.
	 *
	 * If we are going to be spacing forward, and we haven't determined
	 * the tape density yet, we have to do so now...
	 */
	if (un->un_state == ST_STATE_OPEN_PENDING_IO) {
		if (st_determine_density(dev, mode)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "check_density_or_wfm : EIO can't determine "
			    "density\n");
			un->un_errno = EIO;
			return (EIO);
		}
		/*
		 * Presumably we are at BOT. If we attempt to write, it will
		 * either work okay, or bomb. We don't do a st_test_append
		 * unless we're past BOT.
		 */
		un->un_laststate = un->un_state;
		un->un_state = ST_STATE_OPEN;

	} else if (un->un_fileno >= 0 && un->un_fmneeded > 0 &&
	    ((un->un_lastop == ST_OP_WEOF && wfm) ||
	    (un->un_lastop == ST_OP_WRITE && wfm))) {

		daddr_t blkno = un->un_blkno;
		int fileno = un->un_fileno;

		/*
		 * We need to write one or two filemarks.
		 * In the case of the HP, we need to
		 * position the head between the two
		 * marks.
		 */
		if ((un->un_fmneeded > 0) || (un->un_lastop == ST_OP_WEOF)) {
			wfm = un->un_fmneeded;
			un->un_fmneeded = 0;
		}

		if (st_write_fm(dev, wfm)) {
			un->un_fileno = -1;
			un->un_density_known = 0;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "check_density_or_wfm : EIO can't write fm\n");
			un->un_errno = EIO;
			return (EIO);
		}

		if (stepflag == STEPBACK) {
			if (st_cmd(dev, SCMD_SPACE, Fmk((-wfm)), SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "check_density_or_wfm : EIO can't space "
				    "(-wfm)\n");
				un->un_errno = EIO;
				return (EIO);
			}
			un->un_blkno = blkno;
			un->un_fileno = fileno;
		}
	}

	/*
	 * Whatever we do at this point clears the state of the eof flag.
	 */

	un->un_eof = ST_NO_EOF;

	/*
	 * If writing, let's check that we're positioned correctly
	 * at the end of tape before issuing the next write.
	 */
	if (un->un_read_only == RDWR) {
		un->un_test_append = 1;
	}

	ASSERT(mutex_owned(ST_MUTEX));
	return (0);
}


/*
 * Wait for all outstaning I/O's to complete
 *
 * we wait on both ncmds and the wait queue for times when we are flushing
 * after persistent errors are flagged, which is when ncmds can be 0, and the
 * queue can still have I/O's.  This way we preserve order of biodone's.
 */
static void
st_wait_for_io(struct scsi_tape *un)
{
	ASSERT(mutex_owned(ST_MUTEX));
	while (un->un_ncmds || un->un_quef) {
		cv_wait(&un->un_queue_cv, ST_MUTEX);
	}
}

/*
 * This routine implements the ioctl calls.  It is called
 * from the device switch at normal priority.
 */
/*ARGSUSED*/
static int
st_ioctl(dev_t dev, int cmd, intptr_t arg, int flag, cred_t *cred_p,
    int *rval_p)
{
	int tmp, rval = 0;

	GET_SOFT_STATE(dev);

	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_ioctl(): fileno=%x, blkno=%lx, un_eof=%x, state = %d, "
	    "pe_flag = %d\n",
	    un->un_fileno, un->un_blkno, un->un_eof, un->un_state,
	    IS_PE_FLAG_SET(un));

	/*
	 * We don't want to block on these, so let them through
	 * and we don't care about setting driver states here.
	 */
	if ((cmd == MTIOCGETDRIVETYPE) ||
	    (cmd == MTIOCGUARANTEEDORDER) ||
	    (cmd == MTIOCPERSISTENTSTATUS)) {
		goto check_commands;
	}

	/*
	 * wait for all outstanding commands to complete, or be dequeued.
	 * And because ioctl's are synchronous commands, any return value
	 * after this,  will be in order
	 */
	st_wait_for_io(un);

	/*
	 * allow only a through clear errors and persistent status, and
	 * status
	 */
	if (IS_PE_FLAG_SET(un)) {
		if ((cmd == MTIOCLRERR) ||
		    (cmd == MTIOCPERSISTENT) ||
		    (cmd == MTIOCGET) ||
		    (cmd == USCSIGETRQS)) {
			goto check_commands;
		} else {
			rval = un->un_errno;
			goto exit;
		}
	}

	un->un_throttle = 1;	/* > 1 will never happen here */
	un->un_errno = 0;	/* start clean from here */

	/*
	 * first and foremost, handle any ST_EOT_PENDING cases.
	 * That is, if a logical eot is pending notice, notice it.
	 */
	if (un->un_eof == ST_EOT_PENDING) {
		int resid = un->un_err_resid;
		uchar_t status = un->un_status;
		uchar_t lastop = un->un_lastop;

		if (st_cmd(dev, SCMD_SPACE, Fmk((-1)), SYNC_CMD)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO can't space fmk(-1)\n");
			rval = EIO;
			goto exit;
		}
		un->un_lastop = lastop; /* restore last operation */
		if (status == SUN_KEY_EOF) {
			un->un_status = SUN_KEY_EOT;
		} else {
			un->un_status = status;
		}
		un->un_err_resid  = resid;
		un->un_err_blkno = un->un_blkno = 0; /* fix up block number */
		un->un_eof = ST_EOT;	/* now we're at logical eot */
	}

	/*
	 * now, handle the rest of the situations
	 */
check_commands:
	switch (cmd) {
	case MTIOCGET:
		{
#ifdef _MULTI_DATAMODEL
			/*
			 * For use when a 32 bit app makes a call into a
			 * 64 bit ioctl
			 */
			struct mtget32		mtg_local32;
			struct mtget32 		*mtget_32 = &mtg_local32;
#endif /* _MULTI_DATAMODEL */

			/* Get tape status */
			struct mtget mtg_local;
			struct mtget *mtget = &mtg_local;
			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
				"st_ioctl: MTIOCGET\n");

			bzero(mtget, sizeof (struct mtget));
			mtget->mt_erreg = un->un_status;
			mtget->mt_resid = un->un_err_resid;
			mtget->mt_dsreg = un->un_retry_ct;
			mtget->mt_fileno = un->un_err_fileno;
			mtget->mt_blkno = un->un_err_blkno;
			mtget->mt_type = un->un_dp->type;
			mtget->mt_flags = MTF_SCSI | MTF_ASF;
			if (un->un_dp->options & ST_REEL) {
				mtget->mt_flags |= MTF_REEL;
				mtget->mt_bf = 20;
			} else {		/* 1/4" cartridges */
				switch (mtget->mt_type) {
				/* Emulex cartridge tape */
				case MT_ISMT02:
					mtget->mt_bf = 40;
					break;
				default:
					mtget->mt_bf = 126;
					break;
				}
			}

			/*
			 * If large transfers are allowed and drive options
			 * has no record size limit set. Calculate blocking
			 * factor from the lesser of maxbsize and maxdma.
			 */
			if ((un->un_allow_large_xfer) &&
			    (un->un_dp->options & ST_NO_RECSIZE_LIMIT)) {
				mtget->mt_bf = min(un->un_maxbsize,
				    un->un_maxdma) / SECSIZE;
			}

			if (un->un_read_only == WORM ||
			    un->un_read_only == RDWORM) {
				mtget->mt_flags |= MTF_WORM_MEDIA;
			}

			rval = st_check_clean_bit(dev);
			if (rval == -1) {
				rval = EIO;
				goto exit;
			} else {
				mtget->mt_flags |= (ushort_t)rval;
				rval = 0;
			}

			un->un_status = 0;		/* Reset status */
			un->un_err_resid = 0;
			tmp = sizeof (struct mtget);

#ifdef _MULTI_DATAMODEL

		switch (ddi_model_convert_from(flag & FMODELS)) {
		case DDI_MODEL_ILP32:
			/*
			 * Convert 64 bit back to 32 bit before doing
			 * copyout. This is what the ILP32 app expects.
			 */
			mtget_32->mt_erreg = 	mtget->mt_erreg;
			mtget_32->mt_resid = 	mtget->mt_resid;
			mtget_32->mt_dsreg = 	mtget->mt_dsreg;
			mtget_32->mt_fileno = 	(daddr32_t)mtget->mt_fileno;
			mtget_32->mt_blkno = 	(daddr32_t)mtget->mt_blkno;
			mtget_32->mt_type =  	mtget->mt_type;
			mtget_32->mt_flags = 	mtget->mt_flags;
			mtget_32->mt_bf = 	mtget->mt_bf;

			if (ddi_copyout(mtget_32, (void *)arg,
			    sizeof (struct mtget32), flag)) {
				rval = EFAULT;
			}
			break;

		case DDI_MODEL_NONE:
			if (ddi_copyout(mtget, (void *)arg, tmp, flag)) {
				rval = EFAULT;
			}
			break;
		}
#else /* ! _MULTI_DATAMODE */
		if (ddi_copyout(mtget, (void *)arg, tmp, flag)) {
			rval = EFAULT;
		}
#endif /* _MULTI_DATAMODE */

			break;
		}
	case MTIOCSTATE:
		{
			/*
			 * return when media presence matches state
			 */
			enum mtio_state state;

			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
				"st_ioctl: MTIOCSTATE\n");

			if (ddi_copyin((void *)arg, &state, sizeof (int), flag))
				rval = EFAULT;

			mutex_exit(ST_MUTEX);

			rval = st_check_media(dev, state);

			mutex_enter(ST_MUTEX);

			if (rval != 0) {
				break;
			}

			if (ddi_copyout(&un->un_mediastate, (void *)arg,
			    sizeof (int), flag))
				rval = EFAULT;
			break;

		}

	case MTIOCGETDRIVETYPE:
		{
#ifdef _MULTI_DATAMODEL
		/*
		 * For use when a 32 bit app makes a call into a
		 * 64 bit ioctl
		 */
		struct mtdrivetype_request32	mtdtrq32;
#endif /* _MULTI_DATAMODEL */

			/*
			 * return mtdrivetype
			 */
			struct mtdrivetype_request mtdtrq;
			struct mtdrivetype mtdrtyp;
			struct mtdrivetype *mtdt = &mtdrtyp;
			struct st_drivetype *stdt = un->un_dp;

			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
				"st_ioctl: MTIOCGETDRIVETYPE\n");

#ifdef _MULTI_DATAMODEL
		switch (ddi_model_convert_from(flag & FMODELS)) {
		case DDI_MODEL_ILP32:
		{
			if (ddi_copyin((void *)arg, &mtdtrq32,
			    sizeof (struct mtdrivetype_request32), flag)) {
				rval = EFAULT;
				break;
			}
			mtdtrq.size = mtdtrq32.size;
			mtdtrq.mtdtp =
			    (struct  mtdrivetype *)(uintptr_t)mtdtrq32.mtdtp;
			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
				"st_ioctl: size 0x%x\n", mtdtrq.size);
			break;
		}
		case DDI_MODEL_NONE:
			if (ddi_copyin((void *)arg, &mtdtrq,
			    sizeof (struct mtdrivetype_request), flag)) {
				rval = EFAULT;
				break;
			}
			break;
		}

#else /* ! _MULTI_DATAMODEL */
		if (ddi_copyin((void *)arg, &mtdtrq,
		    sizeof (struct mtdrivetype_request), flag)) {
			rval = EFAULT;
			break;
		}
#endif /* _MULTI_DATAMODEL */

			/*
			 * if requested size is < 0 then return
			 * error.
			 */
			if (mtdtrq.size < 0) {
				rval = EINVAL;
				break;
			}
			bzero(mtdt, sizeof (struct mtdrivetype));
			(void) strncpy(mtdt->name, stdt->name, ST_NAMESIZE);
			(void) strncpy(mtdt->vid, stdt->vid, VIDPIDLEN - 1);
			mtdt->type = stdt->type;
			mtdt->bsize = stdt->bsize;
			mtdt->options = stdt->options;
			mtdt->max_rretries = stdt->max_rretries;
			mtdt->max_wretries = stdt->max_wretries;
			for (tmp = 0; tmp < NDENSITIES; tmp++)
				mtdt->densities[tmp] = stdt->densities[tmp];
			mtdt->default_density = stdt->default_density;
			/*
			 * Speed hasn't been used since the hayday of reel tape.
			 * For all drives not setting the option ST_KNOWS_MEDIA
			 * the speed member renamed to mediatype are zeros.
			 * Those drives that have ST_KNOWS_MEDIA set use the
			 * new mediatype member which is used to figure the
			 * type of media loaded.
			 *
			 * So as to not break applications speed in the
			 * mtdrivetype structure is not renamed.
			 */
			for (tmp = 0; tmp < NDENSITIES; tmp++) {
				mtdt->speeds[tmp] = stdt->mediatype[tmp];
			}
			mtdt->non_motion_timeout = stdt->non_motion_timeout;
			mtdt->io_timeout = stdt->io_timeout;
			mtdt->rewind_timeout = stdt->rewind_timeout;
			mtdt->space_timeout = stdt->space_timeout;
			mtdt->load_timeout = stdt->load_timeout;
			mtdt->unload_timeout = stdt->unload_timeout;
			mtdt->erase_timeout = stdt->erase_timeout;

			/*
			 * Limit the maximum length of the result to
			 * sizeof (struct mtdrivetype).
			 */
			tmp = sizeof (struct mtdrivetype);
			if (mtdtrq.size < tmp)
				tmp = mtdtrq.size;
			if (ddi_copyout(mtdt, mtdtrq.mtdtp, tmp, flag)) {
				rval = EFAULT;
			}
			break;
		}
	case MTIOCPERSISTENT:
		{
			int persistence = 0;

			if (ddi_copyin((void *)arg, &persistence,
			    sizeof (int), flag)) {
				rval = EFAULT;
				break;
			}

			/* non zero sets it, only 0 turns it off */
			un->un_persistence = (uchar_t)persistence ? 1 : 0;

			if (un->un_persistence)
				TURN_PE_ON(un);
			else
				TURN_PE_OFF(un);

			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl: MTIOCPERSISTENT : persistence = %d\n",
			    un->un_persistence);

			break;
		}
	case MTIOCPERSISTENTSTATUS:
		{
			int persistence = (int)un->un_persistence;

			if (ddi_copyout(&persistence, (void *)arg,
			    sizeof (int), flag)) {
				rval = EFAULT;
			}
			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl: MTIOCPERSISTENTSTATUS:persistece = %d\n",
			    un->un_persistence);

			break;
		}


	case MTIOCLRERR:
		{
			/* clear persistent errors */

			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl: MTIOCLRERR\n");

			CLEAR_PE(un);

			break;
		}

	case MTIOCGUARANTEEDORDER:
		{
			/*
			 * this is just a holder to make a valid ioctl and
			 * it won't be in any earlier release
			 */
			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl: MTIOCGUARANTEEDORDER\n");

			break;
		}

	case MTIOCRESERVE:
		{
			ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl: MTIOCRESERVE\n");

			/*
			 * Check if Reserve/Release is supported.
			 */
			if (un->un_dp->options & ST_NO_RESERVE_RELEASE) {
				rval = ENOTTY;
				break;
			}

			rval = st_reserve_release(un, ST_RESERVE);

			if (rval == 0) {
				un->un_rsvd_status |= ST_PRESERVE_RESERVE;
			}
			break;
		}

	case MTIOCRELEASE:
		{
			ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl: MTIOCRELEASE\n");

			/*
			 * Check if Reserve/Release is supported.
			 */
			if (un->un_dp->options & ST_NO_RESERVE_RELEASE) {
				rval = ENOTTY;
				break;
			}

			/*
			 * Used to just clear ST_PRESERVE_RESERVE which
			 * made the reservation release at next close.
			 * As the user may have opened and then done a
			 * persistant reservation we now need to drop
			 * the reservation without closing if the user
			 * attempts to do this.
			 */
			rval = st_reserve_release(un, ST_RELEASE);

			un->un_rsvd_status &= ~ST_PRESERVE_RESERVE;

			break;
		}

	case MTIOCFORCERESERVE:
		{
			ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl: MTIOCFORCERESERVE\n");

			/*
			 * Check if Reserve/Release is supported.
			 */
			if (un->un_dp->options & ST_NO_RESERVE_RELEASE) {
				rval = ENOTTY;
				break;
			}
			/*
			 * allow only super user to run this.
			 */
			if (drv_priv(cred_p) != 0) {
				rval = EPERM;
				break;
			}
			/*
			 * Throw away reserve,
			 * not using test-unit-ready
			 * since reserve can succeed without tape being
			 * present in the drive.
			 */
			(void) st_reserve_release(un, ST_RESERVE);

			rval = st_take_ownership(dev);

			break;
		}
	case USCSIGETRQS:
		{
#ifdef _MULTI_DATAMODEL
		/*
		 * For use when a 32 bit app makes a call into a
		 * 64 bit ioctl
		 */
		struct uscsi_rqs32	urqs_32;
		struct uscsi_rqs32	*urqs_32_ptr = &urqs_32;
#endif /* _MULTI_DATAMODEL */
			struct uscsi_rqs	urqs;
			struct uscsi_rqs	*urqs_ptr = &urqs;
			ushort_t		len;
#ifdef _MULTI_DATAMODEL
		switch (ddi_model_convert_from(flag & FMODELS)) {
		case DDI_MODEL_ILP32:
		{
			if (ddi_copyin((void *)arg, urqs_32_ptr,
			    sizeof (struct uscsi_rqs32), flag)) {
				rval = EFAULT;
				break;
			}
			urqs_ptr->rqs_buflen = urqs_32_ptr->rqs_buflen;
			urqs_ptr->rqs_bufaddr =
			    (caddr_t)(uintptr_t)urqs_32_ptr->rqs_bufaddr;
			break;
		}
		case DDI_MODEL_NONE:
			if (ddi_copyin((void *)arg, urqs_ptr,
			    sizeof (struct uscsi_rqs), flag)) {
				rval = EFAULT;
				break;
			}
		}
#else /* ! _MULTI_DATAMODEL */
		if (ddi_copyin((void *)arg, urqs_ptr, sizeof (urqs), flag)) {
			rval = EFAULT;
			break;
		}
#endif /* _MULTI_DATAMODEL */

			urqs_ptr->rqs_flags = (int)un->un_rqs_state &
			    (ST_RQS_OVR | ST_RQS_VALID);
			if (urqs_ptr->rqs_buflen <= SENSE_LENGTH) {
			    len = urqs_ptr->rqs_buflen;
			    urqs_ptr->rqs_resid = 0;
			} else {
			    len = SENSE_LENGTH;
			    urqs_ptr->rqs_resid = urqs_ptr->rqs_buflen
				    - SENSE_LENGTH;
			}
			if (!(un->un_rqs_state & ST_RQS_VALID)) {
			    urqs_ptr->rqs_resid = urqs_ptr->rqs_buflen;
			}
			un->un_rqs_state |= ST_RQS_READ;
			un->un_rqs_state &= ~(ST_RQS_OVR);

#ifdef _MULTI_DATAMODEL
		switch (ddi_model_convert_from(flag & FMODELS)) {
		case DDI_MODEL_ILP32:
			urqs_32_ptr->rqs_flags = urqs_ptr->rqs_flags;
			urqs_32_ptr->rqs_resid = urqs_ptr->rqs_resid;
			if (ddi_copyout(&urqs_32, (void *)arg,
			    sizeof (urqs_32), flag)) {
				rval = EFAULT;
			}
			break;
		case DDI_MODEL_NONE:
			if (ddi_copyout(&urqs, (void *)arg, sizeof (urqs),
			    flag)) {
				rval = EFAULT;
			}
			break;
		}

		if (un->un_rqs_state & ST_RQS_VALID) {
			if (ddi_copyout(un->un_uscsi_rqs_buf,
			    urqs_ptr->rqs_bufaddr, len, flag)) {
				rval = EFAULT;
		    }
		}
#else /* ! _MULTI_DATAMODEL */
		if (ddi_copyout(&urqs, (void *)arg, sizeof (urqs), flag)) {
			rval = EFAULT;
		}
		if (un->un_rqs_state & ST_RQS_VALID) {
			if (ddi_copyout(un->un_uscsi_rqs_buf,
			    urqs_ptr->rqs_bufaddr, len, flag)) {
				rval = EFAULT;
		    }
		}
#endif /* _MULTI_DATAMODEL */
			break;
		}

	case USCSICMD:
	{
		cred_t	*cr;
#ifdef _MULTI_DATAMODEL
		/*
		 * For use when a 32 bit app makes a call into a
		 * 64 bit ioctl
		 */
		struct uscsi_cmd32	ucmd_32;
		struct uscsi_cmd32	*ucmd_32_ptr = &ucmd_32;
#endif /* _MULTI_DATAMODEL */

		/*
		 * Run a generic USCSI command
		 */
		struct uscsi_cmd ucmd;
		struct uscsi_cmd *ucmd_ptr = &ucmd;
		enum uio_seg uioseg;

		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			"st_ioctl: USCSICMD\n");

		cr = ddi_get_cred();
		if ((drv_priv(cred_p) != 0) && (drv_priv(cr) != 0)) {
			rval = EPERM;
			break;
		}

#ifdef _MULTI_DATAMODEL
		switch (ddi_model_convert_from(flag & FMODELS)) {
		case DDI_MODEL_ILP32:
		{
			if (ddi_copyin((void *)arg, ucmd_32_ptr,
			    sizeof (struct uscsi_cmd32), flag)) {
				rval = EFAULT;
				break;
			}
			uscsi_cmd32touscsi_cmd(ucmd_32_ptr, ucmd_ptr);
			break;
		}
		case DDI_MODEL_NONE:
			if (ddi_copyin((void *)arg, ucmd_ptr, sizeof (ucmd),
			    flag)) {
				rval = EFAULT;
				break;
			}
		}

#else /* ! _MULTI_DATAMODEL */
		if (ddi_copyin((void *)arg, ucmd_ptr, sizeof (ucmd), flag)) {
			rval = EFAULT;
			break;
		}
#endif /* _MULTI_DATAMODEL */


		/*
		 * although st_ioctl_cmd() never makes use of these
		 * now, we are just being safe and consistent
		 */
		ucmd.uscsi_flags &= ~(USCSI_NOINTR | USCSI_NOPARITY |
		    USCSI_OTAG | USCSI_HTAG | USCSI_HEAD);


		uioseg = (flag & FKIOCTL) ?  UIO_SYSSPACE : UIO_USERSPACE;

		rval = st_ioctl_cmd(dev, &ucmd, uioseg, uioseg, uioseg);


#ifdef _MULTI_DATAMODEL
		switch (ddi_model_convert_from(flag & FMODELS)) {
		case DDI_MODEL_ILP32:
			/*
			 * Convert 64 bit back to 32 bit before doing
			 * copyout. This is what the ILP32 app expects.
			 */
			uscsi_cmdtouscsi_cmd32(ucmd_ptr, ucmd_32_ptr);

			if (ddi_copyout(&ucmd_32, (void *)arg,
			    sizeof (ucmd_32), flag)) {
				if (rval != 0)
					rval = EFAULT;
				}
			break;

		case DDI_MODEL_NONE:
			if (ddi_copyout(&ucmd, (void *)arg,
			    sizeof (ucmd), flag)) {
				if (rval != 0)
					rval = EFAULT;
				}
			break;
		}
#else /* ! _MULTI_DATAMODEL */
		if (ddi_copyout(&ucmd, (void *)arg, sizeof (ucmd), flag)) {
			if (rval != 0)
				rval = EFAULT;
		}
#endif /* _MULTI_DATAMODEL */

		break;
	}

	case MTIOCTOP:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			"st_ioctl: MTIOCTOP\n");
		rval = st_mtioctop(un, arg, flag);
		break;

	case MTIOCREADIGNOREILI:
		{
			int set_ili;

			if (ddi_copyin((void *)arg, &set_ili,
			    sizeof (set_ili), flag)) {
				rval = EFAULT;
				break;
			}

			if (un->un_bsize) {
				rval = ENOTTY;
				break;
			}

			switch (set_ili) {
			case 0:
				un->un_dp->options &= ~ST_READ_IGNORE_ILI;
				break;

			case 1:
				un->un_dp->options |= ST_READ_IGNORE_ILI;
				break;

			default:
				rval = EINVAL;
				break;
			}
			break;
		}

	case MTIOCREADIGNOREEOFS:
		{
			int ignore_eof;

			if (ddi_copyin((void *)arg, &ignore_eof,
			    sizeof (ignore_eof), flag)) {
				rval = EFAULT;
				break;
			}

			if (!(un->un_dp->options & ST_REEL)) {
				rval = ENOTTY;
				break;
			}

			switch (ignore_eof) {
			case 0:
				un->un_dp->options &= ~ST_READ_IGNORE_EOFS;
				break;

			case 1:
				un->un_dp->options |= ST_READ_IGNORE_EOFS;
				break;

			default:
				rval = EINVAL;
				break;
			}
			break;
		}

	case MTIOCSHORTFMK:
		{
			int short_fmk;

			if (ddi_copyin((void *)arg, &short_fmk,
			    sizeof (short_fmk), flag)) {
				rval = EFAULT;
				break;
			}

			switch (un->un_dp->type) {
			case ST_TYPE_EXB8500:
			case ST_TYPE_EXABYTE:
				if (!short_fmk) {
					un->un_dp->options &=
						~ST_SHORT_FILEMARKS;
				} else if (short_fmk == 1) {
					un->un_dp->options |=
						ST_SHORT_FILEMARKS;
				} else {
					rval = EINVAL;
				}
				break;

			default:
				rval = ENOTTY;
				break;
			}
			break;
		}

	default:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			"st_ioctl: unknown ioctl\n");
		rval = ENOTTY;
	}

exit:
	if (!IS_PE_FLAG_SET(un))
		un->un_errno = rval;

	mutex_exit(ST_MUTEX);

	return (rval);
}


/*
 * do some MTIOCTOP tape operations
 */
static int
st_mtioctop(struct scsi_tape *un, intptr_t arg, int flag)
{
	struct mtop *mtop, local;
	int savefile, tmp, rval = 0;
	dev_t dev = un->un_dev;
#ifdef _MULTI_DATAMODEL
	/*
	 * For use when a 32 bit app makes a call into a
	 * 64 bit ioctl
	 */
	struct mtop32	mtop_32_for_64;
#endif /* _MULTI_DATAMODEL */


	ASSERT(mutex_owned(ST_MUTEX));

	mtop = &local;
#ifdef _MULTI_DATAMODEL
		switch (ddi_model_convert_from(flag & FMODELS)) {
		case DDI_MODEL_ILP32:
		{
			if (ddi_copyin((void *)arg, &mtop_32_for_64,
			    sizeof (struct mtop32), flag)) {
				return (EFAULT);
			}
			mtop->mt_op = mtop_32_for_64.mt_op;
			mtop->mt_count =  (daddr_t)mtop_32_for_64.mt_count;
			break;
		}
		case DDI_MODEL_NONE:
			if (ddi_copyin((void *)arg, mtop,
			    sizeof (struct mtop), flag)) {
				return (EFAULT);
			}
			break;
		}

#else /* ! _MULTI_DATAMODEL */
		if (ddi_copyin((void *)arg, mtop, sizeof (struct mtop), flag)) {
			return (EFAULT);
		}
#endif /* _MULTI_DATAMODEL */

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_mtioctop(): mt_op=%x\n", mtop->mt_op);
	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "fileno=%x, blkno=%lx, un_eof=%x\n", un->un_fileno, un->un_blkno,
	    un->un_eof);

	rval = 0;
	un->un_status = 0;

	/*
	 * if we are going to mess with a tape, we have to make sure we have
	 * one and are not offline (i.e. no tape is initialized).  We let
	 * commands pass here that don't actually touch the tape, except for
	 * loading and initialization (rewinding).
	 */
	if (un->un_state == ST_STATE_OFFLINE) {
		switch (mtop->mt_op) {
		case MTLOAD:
		case MTNOP:
			/*
			 * We don't want strategy calling st_tape_init here,
			 * so, change state
			 */
			un->un_state = ST_STATE_INITIALIZING;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop : OFFLINE state = %d\n",
			    un->un_state);
			break;
		default:
			/*
			 * reinitialize by normal means
			 */
			rval = st_tape_init(dev);
			if (rval) {
				un->un_state = ST_STATE_INITIALIZING;
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_mtioctop : OFFLINE init failure ");
				un->un_state = ST_STATE_OFFLINE;
				un->un_fileno = -1;
				if (rval != EACCES) {
					rval = EIO;
				}
				return (rval);
			}
			un->un_state = ST_STATE_OPEN_PENDING_IO;
			break;
		}
	}

	/*
	 * If the file position is invalid, allow only those
	 * commands that properly position the tape and fail
	 * the rest with EIO
	 */
	if (un->un_fileno < 0) {
		switch (mtop->mt_op) {
		case MTWEOF:
		case MTRETEN:
		case MTERASE:
		case MTEOM:
		case MTFSF:
		case MTFSR:
		case MTBSF:
		case MTNBSF:
		case MTBSR:
		case MTSRSZ:
		case MTGRSZ:
			return (EIO);
			/* NOTREACHED */
		case MTREW:
		case MTLOAD:
		case MTOFFL:
		case MTNOP:
			break;

		default:
			return (ENOTTY);
			/* NOTREACHED */
		}
	}

	switch (mtop->mt_op) {
	case MTERASE:
		/*
		 * MTERASE rewinds the tape, erase it completely, and returns
		 * to the beginning of the tape
		 */
		if (un->un_mspl->wp || un->un_read_only & WORM) {
			un->un_status = KEY_WRITE_PROTECT;
			un->un_err_resid = mtop->mt_count;
			un->un_err_fileno = un->un_fileno;
			un->un_err_blkno = un->un_blkno;
			return (EACCES);
		}
		if (un->un_dp->options & ST_REEL) {
			un->un_fmneeded = 2;
		} else {
			un->un_fmneeded = 1;
		}
		if (st_check_density_or_wfm(dev, 1, B_WRITE, NO_STEPBACK) ||
		    st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD) ||
		    st_cmd(dev, SCMD_ERASE, 0, SYNC_CMD)) {
			un->un_fileno = -1;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop : EIO space or erase or check den)\n");
			rval = EIO;
		} else {
			/* QIC and helical scan rewind after erase */
			if (un->un_dp->options & ST_REEL) {
				(void) st_cmd(dev, SCMD_REWIND, 0, ASYNC_CMD);
			}
		}
		break;

	case MTWEOF:
		/*
		 * write an end-of-file record
		 */
		if (un->un_mspl->wp || un->un_read_only & RDONLY) {
			un->un_status = KEY_WRITE_PROTECT;
			un->un_err_resid = mtop->mt_count;
			un->un_err_fileno = un->un_fileno;
			un->un_err_blkno = un->un_blkno;
			return (EACCES);
		}

		/*
		 * zero count means just flush buffers
		 * negative count is not permitted
		 */
		if (mtop->mt_count < 0)
			return (EINVAL);

		/* Not on worm */
		if (un->un_read_only == RDWR) {
			un->un_test_append = 1;
		}

		if (un->un_state == ST_STATE_OPEN_PENDING_IO) {
			if (st_determine_density(dev, B_WRITE)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_mtioctop : EIO : MTWEOF can't determine"
				    "density");
				return (EIO);
			}
		}

		rval = st_write_fm(dev, (int)mtop->mt_count);
		if ((rval != 0) && (rval != EACCES)) {
			/*
			 * Failure due to something other than illegal
			 * request results in loss of state (st_intr).
			 */
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop : EIO : MTWEOF can't write file mark");
			rval = EIO;
		}
		break;

	case MTRETEN:
		/*
		 * retension the tape
		 */
		if (st_check_density_or_wfm(dev, 1, 0, NO_STEPBACK) ||
		    st_cmd(dev, SCMD_LOAD, 3, SYNC_CMD)) {
			un->un_fileno = -1;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop : EIO : MTRETEN ");
			rval = EIO;
		}
		break;

	case MTREW:
		/*
		 * rewind  the tape
		 */
		if (st_check_density_or_wfm(dev, 1, 0, NO_STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop : EIO:MTREW check density/wfm failed");
			return (EIO);
		}
		if (st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop : EIO : MTREW ");
			rval = EIO;
		}
		break;

	case MTOFFL:
		/*
		 * rewinds, and, if appropriate, takes the device offline by
		 * unloading the tape
		 */
		if (st_check_density_or_wfm(dev, 1, 0, NO_STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop :EIO:MTOFFL check density/wfm failed");
			return (EIO);
		}
		(void) st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD);
		if (st_cmd(dev, SCMD_LOAD, 0, SYNC_CMD)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop : EIO : MTOFFL");
			return (EIO);
		}
		un->un_eof = ST_NO_EOF;
		un->un_laststate = un->un_state;
		un->un_state = ST_STATE_OFFLINE;
		un->un_mediastate = MTIO_EJECTED;
		break;

	case MTLOAD:
		/*
		 * This is to load a tape into the drive
		 * Note that if the tape is not loaded, the device will have
		 * to be opened via O_NDELAY or O_NONBLOCK.
		 */
		/*
		 * Let's try and clean things up, if we are not
		 * initializing, and then send in the load command, no
		 * matter what.
		 *
		 * load after a media change by the user.
		 */

		if (un->un_state > ST_STATE_INITIALIZING)
			(void) st_check_density_or_wfm(dev, 1, 0, NO_STEPBACK);
		rval = st_cmd(dev, SCMD_LOAD, 1, SYNC_CMD);
		if (rval) {
			if (rval != EACCES) {
				rval = EIO;
			}
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop : %s : MTLOAD\n",
			    rval == EACCES ? "EACCES" : "EIO");
			/*
			 * If load tape fails, who knows what happened...
			 */
			un->un_fileno = -1;
			break;
		}

		/*
		 * reset all counters appropriately using rewind, as if LOAD
		 * succeeds, we are at BOT
		 */
		un->un_state = ST_STATE_INITIALIZING;

		rval = st_tape_init(dev);
		if ((rval == EACCES) && (un->un_read_only & WORM)) {
			rval = 0;
			break;
		}

		if (rval != 0) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop : EIO : MTLOAD calls st_tape_init\n");
			rval = EIO;
			un->un_state = ST_STATE_OFFLINE;
		}

		break;

	case MTNOP:
		un->un_status = 0;		/* Reset status */
		un->un_err_resid = 0;
		break;

	case MTEOM:
		/*
		 * positions the tape at a location just after the last file
		 * written on the tape. For cartridge and 8 mm, this after
		 * the last file mark; for reel, this is inbetween the two
		 * last 2 file marks
		 */
		if ((un->un_eof >= ST_EOT) ||
		    (un->un_lastop == ST_OP_WRITE) ||
		    (un->un_lastop == ST_OP_WEOF)) {
			/*
			 * If the command wants to move to logical end
			 * of media, and we're already there, we're done.
			 * If we were at logical eot, we reset the state
			 * to be *not* at logical eot.
			 *
			 * If we're at physical or logical eot, we prohibit
			 * forward space operations (unconditionally).
			 *
			 * Also if the last operation was a write of any
			 * kind the tape is at EOD.
			 */
			return (0);
		}
		/*
		 * physical tape position may not be what we've been
		 * telling the user; adjust the request accordingly
		 */
		if (IN_EOF(un)) {
			un->un_fileno++;
			un->un_blkno = 0;
		}

		if (st_check_density_or_wfm(dev, 1, B_READ, NO_STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop : EIO:MTEOM check density/wfm failed");
			return (EIO);
		}

		/*
		 * st_find_eom() returns the last fileno we knew about;
		 */
		savefile = st_find_eom(dev);

		if ((un->un_status != KEY_BLANK_CHECK) &&
		    (un->un_status != SUN_KEY_EOT)) {
			un->un_fileno = -1;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop : EIO : MTEOM status check failed");
			rval = EIO;
		} else {
			/*
			 * For 1/2" reel tapes assume logical EOT marked
			 * by two file marks or we don't care that we may
			 * be extending the last file on the tape.
			 */
			if (un->un_dp->options & ST_REEL) {
				if (st_cmd(dev, SCMD_SPACE, Fmk((-1)),
				    SYNC_CMD)) {
					un->un_fileno = -1;
					ST_DEBUG2(ST_DEVINFO, st_label,
					    SCSI_DEBUG,
					    "st_mtioctop : EIO : MTEOM space "
					    "cmd failed");
					rval = EIO;
					break;
				}
				/*
				 * Fix up the block number.
				 */
				un->un_blkno = 0;
				un->un_err_blkno = 0;
			}
			un->un_err_resid = 0;
			un->un_fileno = savefile;
			un->un_eof = ST_EOT;
		}
		un->un_status = 0;
		break;

	case MTFSF:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "fsf: count=%lx, eof=%x\n", mtop->mt_count,
			un->un_eof);
		/*
		 * forward space over filemark
		 *
		 * For ASF we allow a count of 0 on fsf which means
		 * we just want to go to beginning of current file.
		 * Equivalent to "nbsf(0)" or "bsf(1) + fsf".
		 * Allow stepping over double fmk with reel
		 */
		if ((un->un_eof >= ST_EOT) && (mtop->mt_count > 0) &&
		    ((un->un_dp->options & ST_REEL) == 0)) {
			/* we're at EOM */
			un->un_err_resid = mtop->mt_count;
			un->un_status = KEY_BLANK_CHECK;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop : EIO : MTFSF at EOM");
			return (EIO);
		}

		/*
		 * physical tape position may not be what we've been
		 * telling the user; adjust the request accordingly
		 */
		if (IN_EOF(un)) {
			un->un_fileno++;
			un->un_blkno = 0;
			/*
			 * For positive direction case, we're now covered.
			 * For zero or negative direction, we're covered
			 * (almost)
			 */
			mtop->mt_count--;
		}

		if (st_check_density_or_wfm(dev, 1, B_READ, STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop : EIO : MTFSF density/wfm failed");
			return (EIO);
		}


		/*
		 * Forward space file marks.
		 * We leave ourselves at block zero
		 * of the target file number.
		 */
		if (mtop->mt_count < 0) {
			mtop->mt_count = -mtop->mt_count;
			mtop->mt_op = MTNBSF;
			goto bspace;
		}
fspace:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "fspace: count=%lx, eof=%x\n", mtop->mt_count,
			un->un_eof);
		if ((tmp = mtop->mt_count) == 0) {
			if (un->un_blkno == 0) {
				un->un_err_resid = 0;
				un->un_err_fileno = un->un_fileno;
				un->un_err_blkno = un->un_blkno;
				break;
			} else if (un->un_fileno == 0) {
				rval = st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD);
			} else if (un->un_dp->options & ST_BSF) {
				rval = (st_cmd(dev, SCMD_SPACE, Fmk((-1)),
				    SYNC_CMD) ||
				    st_cmd(dev, SCMD_SPACE, Fmk(1), SYNC_CMD));
			} else {
				tmp = un->un_fileno;
				rval = (st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD) ||
				    st_cmd(dev, SCMD_SPACE, (int)Fmk(tmp),
				    SYNC_CMD));
			}
			if (rval != 0) {
				un->un_fileno = -1;
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_mtioctop : EIO : fspace fileno = -1");

				rval = EIO;
			}
		} else {
			rval = st_space_fmks(dev, tmp);
		}

		if (mtop->mt_op == MTBSF && rval != EIO) {
			/*
			 * we came here with a count < 0; we now need
			 * to skip back to end up before the filemark
			 */
			mtop->mt_count = 1;
			goto bspace;
		}
		break;


	case MTFSR:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "fsr: count=%lx, eof=%x\n", mtop->mt_count,
			un->un_eof);
		/*
		 * forward space to inter-record gap
		 *
		 */
		if ((un->un_eof >= ST_EOT) && (mtop->mt_count > 0)) {
			/* we're at EOM */
			un->un_err_resid = mtop->mt_count;
			un->un_status = KEY_BLANK_CHECK;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop : EIO : MTFSR un_eof > ST_EOT");
			return (EIO);
		}

		if (mtop->mt_count == 0) {
			un->un_err_fileno = un->un_fileno;
			un->un_err_blkno = un->un_blkno;
			un->un_err_resid = 0;
			if (IN_EOF(un) && SVR4_BEHAVIOR) {
				un->un_status = SUN_KEY_EOF;
			}
			return (0);
		}

		/*
		 * physical tape position may not be what we've been
		 * telling the user; adjust the position accordingly
		 */
		if (IN_EOF(un)) {
			daddr_t blkno = un->un_blkno;
			int fileno = un->un_fileno;
			uchar_t lastop = un->un_lastop;
			if (st_cmd(dev, SCMD_SPACE, Fmk((-1)), SYNC_CMD)
			    == -1) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_mtioctop : EIO :MTFSR count && IN_EOF");
				return (EIO);
			}

			un->un_blkno = blkno;
			un->un_fileno = fileno;
			un->un_lastop = lastop;
		}

		if (st_check_density_or_wfm(dev, 1, B_READ, STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mtioctop : EIO : MTFSR st_check_den");
			return (EIO);
		}

space_records:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "space_records: count=%lx, eof=%x\n", mtop->mt_count,
			un->un_eof);
		tmp = un->un_blkno + mtop->mt_count;
		if (tmp == un->un_blkno) {
			un->un_err_resid = 0;
			un->un_err_fileno = un->un_fileno;
			un->un_err_blkno = un->un_blkno;
			break;
		} else if (un->un_blkno < tmp ||
		    (un->un_dp->options & ST_BSR)) {
			/*
			 * If we're spacing forward, or the device can
			 * backspace records, we can just use the SPACE
			 * command.
			 */
			tmp = tmp - un->un_blkno;
			if (st_cmd(dev, SCMD_SPACE, Blk(tmp), SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_mtioctop :EIO:space_records can't spc");
				rval = EIO;
			} else if (un->un_eof >= ST_EOF_PENDING) {
				/*
				 * check if we hit BOT/EOT
				 */
				if (tmp < 0 && un->un_eof == ST_EOM) {
					un->un_status = SUN_KEY_BOT;
					un->un_eof = ST_NO_EOF;
				} else if (tmp < 0 && un->un_eof ==
				    ST_EOF_PENDING) {
					int residue = un->un_err_resid;
					/*
					 * we skipped over a filemark
					 * and need to go forward again
					 */
					if (st_cmd(dev, SCMD_SPACE, Fmk(1),
					    SYNC_CMD)) {
						ST_DEBUG2(ST_DEVINFO,
						    st_label, SCSI_DEBUG,
						    "st_mtioctop : EIO : "
						    "space_records can't "
						    "space #2");
						rval = EIO;
					}
					un->un_err_resid = residue;
				}
				if (rval == 0) {
					ST_DEBUG2(ST_DEVINFO, st_label,
					    SCSI_DEBUG,
					    "st_mtioctop : EIO : space_rec rval"
					    " == 0");
					rval = EIO;
				}
			}
		} else {
			/*
			 * else we rewind, space forward across filemarks to
			 * the desired file, and then space records to the
			 * desired block.
			 */

			int t = un->un_fileno;	/* save current file */

			if (tmp < 0) {
				/*
				 * Wups - we're backing up over a filemark
				 */
				if (un->un_blkno != 0 &&
				    (st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD) ||
				    st_cmd(dev, SCMD_SPACE, Fmk(t), SYNC_CMD)))
					un->un_fileno = -1;
				un->un_err_resid = -tmp;
				if (un->un_fileno == 0 && un->un_blkno == 0) {
					un->un_status = SUN_KEY_BOT;
					un->un_eof = ST_NO_EOF;
				} else if (un->un_fileno > 0) {
					un->un_status = SUN_KEY_EOF;
					un->un_eof = ST_NO_EOF;
				}
				un->un_err_fileno = un->un_fileno;
				un->un_err_blkno = un->un_blkno;
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_mtioctop :EIO:space_records : tmp < 0");
				rval = EIO;
			} else if (st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD) ||
				    st_cmd(dev, SCMD_SPACE, Fmk(t), SYNC_CMD) ||
				    st_cmd(dev, SCMD_SPACE, Blk(tmp),
					SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_mtioctop : EIO :space_records : rewind "
				    "and space failed");
				un->un_fileno = -1;
				rval = EIO;
			}
		}
		break;


	case MTBSF:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "bsf: count=%lx, eof=%x\n", mtop->mt_count,
			un->un_eof);
		/*
		 * backward space of file filemark (1/2" and 8mm)
		 * tape position will end on the beginning of tape side
		 * of the desired file mark
		 */
		if ((un->un_dp->options & ST_BSF) == 0) {
			return (ENOTTY);
		}

		/*
		 * If a negative count (which implies a forward space op)
		 * is specified, and we're at logical or physical eot,
		 * bounce the request.
		 */

		if (un->un_eof >= ST_EOT && mtop->mt_count < 0) {
			un->un_err_resid = mtop->mt_count;
			un->un_status = SUN_KEY_EOT;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl : EIO : MTBSF : un_eof > ST_EOF");
			return (EIO);
		}
		/*
		 * physical tape position may not be what we've been
		 * telling the user; adjust the request accordingly
		 */
		if (IN_EOF(un)) {
			un->un_fileno++;
			un->un_blkno = 0;
			mtop->mt_count++;
			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			"bsf in eof: count=%ld, op=%x\n",
			mtop->mt_count, mtop->mt_op);

		}

		if (st_check_density_or_wfm(dev, 1, 0, STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl : EIO : MTBSF : check den wfm");
			return (EIO);
		}

		if (mtop->mt_count <= 0) {
			/*
			 * for a negative count, we need to step forward
			 * first and then step back again
			 */
			mtop->mt_count = -mtop->mt_count+1;
			goto fspace;
		}

bspace:
	{
		int skip_cnt, end_at_eof;

		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "bspace: count=%lx, eof=%x\n", mtop->mt_count,
			un->un_eof);
		/*
		 * Backspace files (MTNBSF):
		 *
		 *	For tapes that can backspace, backspace
		 *	count+1 filemarks and then run forward over
		 *	a filemark
		 *
		 *	For tapes that can't backspace,
		 *		calculate desired filenumber
		 *		(un->un_fileno - count), rewind,
		 *		and then space forward this amount
		 *
		 * Backspace filemarks (MTBSF)
		 *
		 *	For tapes that can backspace, backspace count
		 *	filemarks
		 *
		 *	For tapes that can't backspace, calculate
		 *	desired filenumber (un->un_fileno - count),
		 *	add 1, rewind, space forward this amount,
		 *	and mark state as ST_EOF_PENDING appropriately.
		 */

		if (mtop->mt_op == MTBSF) {
			end_at_eof = 1;
		} else {
			end_at_eof = 0;
		}

		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "bspace: mt_op=%x, count=%lx, fileno=%x, blkno=%lx\n",
		    mtop->mt_op, mtop->mt_count, un->un_fileno, un->un_blkno);

		/*
		 * Handle the simple case of BOT
		 * playing a role in these cmds.
		 * We do this by calculating the
		 * ending file number. If the ending
		 * file is < BOT, rewind and set an
		 * error and mark resid appropriately.
		 * If we're backspacing a file (not a
		 * filemark) and the target file is
		 * the first file on the tape, just
		 * rewind.
		 */

		tmp = un->un_fileno - mtop->mt_count;
		if ((end_at_eof && tmp < 0) || (end_at_eof == 0 && tmp <= 0)) {
			if (st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_ioctl : EIO : bspace : end_at_eof && "
				    "tmp < 0");
				rval = EIO;
			}
			if (tmp < 0) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_ioctl : EIO : bspace : tmp < 0");
				rval = EIO;
				un->un_err_resid = -tmp;
				un->un_status = SUN_KEY_BOT;
			}
			break;
		}

		if (un->un_dp->options & ST_BSF) {
			skip_cnt = 1 - end_at_eof;
			/*
			 * If we are going to end up at the beginning
			 * of the file, we have to space one extra file
			 * first, and then space forward later.
			 */
			tmp = -(mtop->mt_count + skip_cnt);
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "skip_cnt=%x, tmp=%x\n", skip_cnt, tmp);
			if (st_cmd(dev, SCMD_SPACE, Fmk(tmp), SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_ioctl : EIO : bspace : can't space "
				    "tmp");
				rval = EIO;
			}
		} else {
			if (st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD)) {
				rval = EIO;
			} else {
				skip_cnt = tmp + end_at_eof;
			}
		}

		/*
		 * If we have to space forward, do so...
		 */
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "space forward skip_cnt=%x, rval=%x\n", skip_cnt, rval);
		if (rval == 0 && skip_cnt) {
			if (st_cmd(dev, SCMD_SPACE, Fmk(skip_cnt), SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_ioctl : EIO : bspace : can't space "
				    "skip_cnt");
				rval = EIO;
			} else if (end_at_eof) {
				/*
				 * If we had to space forward, and we're
				 * not a tape that can backspace, mark state
				 * as if we'd just seen a filemark during a
				 * a read.
				 */
				if ((un->un_dp->options & ST_BSF) == 0) {
					un->un_eof = ST_EOF_PENDING;
					un->un_fileno -= 1;
					un->un_blkno = INF;
				}
			}
		}

		if (rval != 0) {
			un->un_fileno = -1;
		}
		break;
	}

	case MTNBSF:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "nbsf: count=%lx, eof=%x\n", mtop->mt_count,
			un->un_eof);
		/*
		 * backward space file to beginning of file
		 *
		 * If a negative count (which implies a forward space op)
		 * is specified, and we're at logical or physical eot,
		 * bounce the request.
		 */

		if (un->un_eof >= ST_EOT && mtop->mt_count < 0) {
			un->un_err_resid = mtop->mt_count;
			un->un_status = SUN_KEY_EOT;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl : EIO : > EOT and count < 0");
			return (EIO);
		}
		/*
		 * physical tape position may not be what we've been
		 * telling the user; adjust the request accordingly
		 */
		if (IN_EOF(un)) {
			un->un_fileno++;
			un->un_blkno = 0;
			mtop->mt_count++;
		}

		if (st_check_density_or_wfm(dev, 1, 0, STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl : EIO : MTNBSF check den and wfm");
			return (EIO);
		}

mtnbsf:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "mtnbsf: count=%lx, eof=%x\n", mtop->mt_count,
			un->un_eof);
		if (mtop->mt_count <= 0) {
			mtop->mt_op = MTFSF;
			mtop->mt_count = -mtop->mt_count;
			goto fspace;
		}
		goto bspace;

	case MTBSR:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "bsr: count=%lx, eof=%x\n", mtop->mt_count,
			un->un_eof);
		/*
		 * backward space into inter-record gap
		 *
		 * If a negative count (which implies a forward space op)
		 * is specified, and we're at logical or physical eot,
		 * bounce the request.
		 */
		if (un->un_eof >= ST_EOT && mtop->mt_count < 0) {
			un->un_err_resid = mtop->mt_count;
			un->un_status = SUN_KEY_EOT;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl : EIO : MTBSR > EOT");
			return (EIO);
		}

		if (mtop->mt_count == 0) {
			un->un_err_fileno = un->un_fileno;
			un->un_err_blkno = un->un_blkno;
			un->un_err_resid = 0;
			if (IN_EOF(un) && SVR4_BEHAVIOR) {
				un->un_status = SUN_KEY_EOF;
			}
			return (0);
		}

		/*
		 * physical tape position may not be what we've been
		 * telling the user; adjust the position accordingly.
		 * bsr can not skip filemarks and continue to skip records
		 * therefore if we are logically before the filemark but
		 * physically at the EOT side of the filemark, we need to step
		 * back; this allows fsr N where N > number of blocks in file
		 * followed by bsr 1 to position at the beginning of last block
		 */
		if (IN_EOF(un)) {
			int blkno = un->un_blkno;
			int fileno = un->un_fileno;
			uchar_t lastop = un->un_lastop;
			if (st_cmd(dev, SCMD_SPACE, Fmk((-1)), SYNC_CMD)
			    == -1) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_write_fm : EIO : MTBSR can't space");
				return (EIO);
			}

			un->un_blkno = blkno;
			un->un_fileno = fileno;
			un->un_lastop = lastop;
		}

		un->un_eof = ST_NO_EOF;

		if (st_check_density_or_wfm(dev, 1, 0, STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl : EIO : MTBSR : can't set density or "
			    "wfm");
			return (EIO);
		}

		mtop->mt_count = -mtop->mt_count;
		goto space_records;

	case MTSRSZ:

		/*
		 * Set record-size to that sent by user
		 * Check to see if there is reason that the requested
		 * block size should not be set.
		 */

		/* If requesting variable block size is it ok? */
		if ((mtop->mt_count == 0) &&
		    ((un->un_dp->options & ST_VARIABLE) == 0)) {
			return (ENOTTY);
		}

		/*
		 * If requested block size is not variable "0",
		 * is it less then minimum.
		 */
		if ((mtop->mt_count != 0) &&
		    (mtop->mt_count < un->un_minbsize)) {
			return (EINVAL);
		}

		/* Is the requested block size more then maximum */
		if ((mtop->mt_count > min(un->un_maxbsize, un->un_maxdma)) &&
		    (un->un_maxbsize != 0)) {
			return (EINVAL);
		}

		/* Is requested block size a modulus the device likes */
		if ((mtop->mt_count % un->un_data_mod) != 0) {
			return (EINVAL);
		}

		if (st_change_block_size(dev, (uint32_t)mtop->mt_count) != 0) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl : MTSRSZ : EIO : cant set block size");
			return (EIO);
		}

		return (0);

	case MTGRSZ:
	{
#ifdef _MULTI_DATAMODEL
	/*
	 * For use when a 32 bit app makes a call into a
	 * 64 bit ioctl
	 */
	struct mtop32	mtop_32_for_64;
#endif /* _MULTI_DATAMODEL */


		/*
		 * Get record-size to the user
		 */
		mtop->mt_count = un->un_bsize;

#ifdef _MULTI_DATAMODEL
		switch (ddi_model_convert_from(flag & FMODELS)) {
		case DDI_MODEL_ILP32:
			/*
			 * Convert 64 bit back to 32 bit before doing
			 * copyout. This is what the ILP32 app expects.
			 */
			mtop_32_for_64.mt_op = mtop->mt_op;
			mtop_32_for_64.mt_count = mtop->mt_count;

			if (ddi_copyout(&mtop_32_for_64, (void *)arg,
			    sizeof (struct mtop32), flag)) {
				return (EFAULT);
			}
			break;

		case DDI_MODEL_NONE:
			if (ddi_copyout(mtop, (void *)arg,
			    sizeof (struct mtop), flag)) {
				return (EFAULT);
			}
			break;
		}
#else /* ! _MULTI_DATAMODE */
		if (ddi_copyout(mtop, (void *)arg, sizeof (struct mtop), flag))
			return (EFAULT);

#endif /* _MULTI_DATAMODE */

		return (0);
	}
	default:
		rval = ENOTTY;
	}

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_ioctl: fileno=%x, blkno=%lx, un_eof=%x\n", un->un_fileno,
	    un->un_blkno, un->un_eof);

	if (un->un_fileno < 0) {
		un->un_density_known = 0;
	}

	ASSERT(mutex_owned(ST_MUTEX));
	return (rval);
}


/*
 * Run a command for uscsi ioctl.
 * cdbspace is address space of cdb.
 * dataspace is address space of the uscsi data buffer.
 */
static int
st_ioctl_cmd(dev_t dev, struct uscsi_cmd *ucmd,
	enum uio_seg cdbspace, enum uio_seg dataspace,
	enum uio_seg rqbufspace)
{
	struct buf *bp;
	struct uscsi_cmd *kcmd;
	caddr_t kcdb;
	int flag;
	int err;
	int rqlen;
	int offline_state = 0;
	char *krqbuf = NULL;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_ioctl_cmd(dev = 0x%lx)\n", dev);

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * We really don't know what commands are coming in here and
	 * we don't want to limit the commands coming in.
	 *
	 * If st_tape_init() gets called from st_strategy(), then we
	 * will hang the process waiting for un->un_sbuf_busy to be cleared,
	 * which it never will, as we set it below.  To prevent
	 * st_tape_init() from getting called, we have to set state to other
	 * than ST_STATE_OFFLINE, so we choose ST_STATE_INITIALIZING, which
	 * achieves this purpose already
	 *
	 * We use offline_state to preserve the OFFLINE state, if it exists,
	 * so other entry points to the driver might have the chance to call
	 * st_tape_init().
	 */
	if (un->un_state == ST_STATE_OFFLINE) {
		un->un_laststate = ST_STATE_OFFLINE;
		un->un_state = ST_STATE_INITIALIZING;
		offline_state = 1;
	}
	/*
	 * Is this a request to reset the bus?
	 * If so, we need go no further.
	 */
	if (ucmd->uscsi_flags & (USCSI_RESET|USCSI_RESET_ALL)) {
		flag = ((ucmd->uscsi_flags & USCSI_RESET_ALL)) ?
			RESET_ALL : RESET_TARGET;

		mutex_exit(ST_MUTEX);
		err = (scsi_reset(ROUTE, flag)) ? 0 : EIO;
		mutex_enter(ST_MUTEX);

		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			"reset %s %s\n",
			(flag == RESET_ALL) ? "all" : "target",
			(err == 0) ? "ok" : "failed");
		/*
		 * If scsi reset successful, don't write any filemarks.
		 */
		if (err == 0) {
			un->un_fmneeded = 0;
		} else {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl_cmd : EIO : scsi_reset failed");
		}
		goto exit;
	}

	/*
	 * First do some sanity checks for USCSI commands.
	 */
	if ((ucmd->uscsi_cdblen <= 0) ||
	    (ucmd->uscsi_cdblen > un->un_max_cdb_sz)) {
		if (cdbspace != UIO_SYSSPACE) {
			scsi_log(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "USCSICMD with cdb larger then transport supports");
		}
		return (EINVAL);
	}

	/*
	 * In order to not worry about where the uscsi structure
	 * or cdb it points to came from, we kmem_alloc copies
	 * of them here.  This will allow reference to the data
	 * they contain long after this process has gone to
	 * sleep and its kernel stack has been unmapped, etc.
	 */

	kcdb = kmem_alloc((size_t)ucmd->uscsi_cdblen, KM_SLEEP);
	if (cdbspace == UIO_SYSSPACE) {
		bcopy(ucmd->uscsi_cdb, kcdb, ucmd->uscsi_cdblen);
	} else {
		if (ddi_copyin(ucmd->uscsi_cdb, kcdb,
		    (size_t)ucmd->uscsi_cdblen, 0)) {
			kmem_free(kcdb, (size_t)ucmd->uscsi_cdblen);
			err = EFAULT;
			goto exit;
		}
	}

	/*
	 * can't peek at the cdb till is copied into kernal space.
	 */
	if (scsi_cdb_size[CDB_GROUPID(kcdb[0])] > un->un_max_cdb_sz) {
		if (cdbspace != UIO_SYSSPACE) {
			scsi_log(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "USCSICMD with cdb larger then transport supports");
		}
		kmem_free(kcdb, ucmd->uscsi_cdblen);
		return (EINVAL);
	}
	kcmd = kmem_alloc(sizeof (struct uscsi_cmd), KM_SLEEP);
	bcopy(ucmd, kcmd, sizeof (struct uscsi_cmd));
	kcmd->uscsi_cdb = kcdb;

	flag = (kcmd->uscsi_flags & USCSI_READ) ? B_READ : B_WRITE;

	/* check to see if this command requires the drive to be reserved */
	err = st_check_cdb_for_need_to_reserve(un, &kcdb[0]);

	if (err) {
		goto exit_free;
	}

	/*
	 * Get buffer resources...
	 */
	while (un->un_sbuf_busy)
		cv_wait(&un->un_sbuf_cv, ST_MUTEX);
	un->un_sbuf_busy = 1;

#ifdef STDEBUG
	if (st_debug > 6) {
		st_clean_print(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "uscsi cdb", kcdb, kcmd->uscsi_cdblen);
		if (kcmd->uscsi_buflen) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			"uscsi %s of %ld bytes %s %s space\n",
			(flag == B_READ) ? rd_str : wr_str,
			kcmd->uscsi_buflen,
			(flag == B_READ) ? "to" : "from",
			(dataspace == UIO_SYSSPACE) ? "system" : "user");
		}
	}
#endif /* ST_DEBUG */

	/*
	 * Initialize Request Sense buffering, if requested.
	 * For user processes, allocate a kernel copy of the sense buffer
	 */
	if ((kcmd->uscsi_flags & USCSI_RQENABLE) &&
			kcmd->uscsi_rqlen && kcmd->uscsi_rqbuf) {
		if (rqbufspace == UIO_USERSPACE) {
			krqbuf = kmem_alloc(SENSE_LENGTH, KM_SLEEP);
		}
		kcmd->uscsi_rqlen = SENSE_LENGTH;
		kcmd->uscsi_rqresid = SENSE_LENGTH;
	} else {
		kcmd->uscsi_rqlen = 0;
		kcmd->uscsi_rqresid = 0;
	}

	un->un_srqbufp = krqbuf;
	bp = un->un_sbufp;
	bzero(bp, sizeof (buf_t));

	/*
	 * Force asynchronous mode, if necessary.
	 */
	if (ucmd->uscsi_flags & USCSI_ASYNC) {
		mutex_exit(ST_MUTEX);
		if (scsi_ifgetcap(ROUTE, "synchronous", 1) == 1) {
			if (scsi_ifsetcap(ROUTE, "synchronous", 0, 1) == 1) {
				ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "forced async ok\n");
			} else {
				ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "forced async failed\n");
				err = EINVAL;
				mutex_enter(ST_MUTEX);
				goto done;
			}
		}
		mutex_enter(ST_MUTEX);
	}

	/*
	 * Re-enable synchronous mode, if requested
	 */
	if (ucmd->uscsi_flags & USCSI_SYNC) {
		mutex_exit(ST_MUTEX);
		if (scsi_ifgetcap(ROUTE, "synchronous", 1) == 0) {
			int i = scsi_ifsetcap(ROUTE, "synchronous", 1, 1);
			ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
				"re-enabled sync %s\n",
				(i == 1) ? "ok" : "failed");
		}
		mutex_enter(ST_MUTEX);
	}

	if (kcmd->uscsi_buflen) {
		/*
		 * We're going to do actual I/O.
		 * Set things up for physio.
		 */
		struct iovec aiov;
		struct uio auio;
		struct uio *uio = &auio;

		bzero(&auio, sizeof (struct uio));
		bzero(&aiov, sizeof (struct iovec));
		aiov.iov_base = kcmd->uscsi_bufaddr;
		aiov.iov_len = kcmd->uscsi_buflen;

		uio->uio_iov = &aiov;
		uio->uio_iovcnt = 1;
		uio->uio_resid = aiov.iov_len;
		uio->uio_segflg = dataspace;

		/*
		 * Let physio do the rest...
		 */
		bp->b_forw = (struct buf *)(uintptr_t)kcdb[0];
		bp->b_back = (struct buf *)kcmd;

		mutex_exit(ST_MUTEX);
		err = physio(st_strategy, bp, dev, flag, st_uscsi_minphys, uio);
		mutex_enter(ST_MUTEX);
	} else {
		/*
		 * Mimic physio
		 */
		bp->b_forw = (struct buf *)(uintptr_t)kcdb[0];
		bp->b_back = (struct buf *)kcmd;
		bp->b_flags = B_BUSY | flag;
		bp->b_edev = dev;
		bp->b_dev = cmpdev(dev);
		bp->b_bcount = 0;
		bp->b_blkno = 0;
		bp->b_resid = 0;
		mutex_exit(ST_MUTEX);
		(void) st_strategy(bp);

		/*
		 * BugTraq #4260046
		 * ----------------
		 * See comments in st_cmd.
		 */

		err = biowait(bp);
		mutex_enter(ST_MUTEX);
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_ioctl_cmd: biowait returns %d\n", err);
	}

	/*
	 * Copy status from kernel copy of uscsi_cmd to user copy
	 * of uscsi_cmd - this was saved in st_done_and_mutex_exit()
	 */
	ucmd->uscsi_status = kcmd->uscsi_status;

done:
	ucmd->uscsi_resid = bp->b_resid;

	/*
	 * Update the Request Sense status and resid
	 */
	rqlen = kcmd->uscsi_rqlen - kcmd->uscsi_rqresid;
	rqlen = min(((int)ucmd->uscsi_rqlen), rqlen);
	ucmd->uscsi_rqresid = ucmd->uscsi_rqlen - rqlen;
	ucmd->uscsi_rqstatus = kcmd->uscsi_rqstatus;
	/*
	 * Copy out the sense data for user processes
	 */
	if (ucmd->uscsi_rqbuf && rqlen && rqbufspace == UIO_USERSPACE) {
		if (copyout(krqbuf, ucmd->uscsi_rqbuf, rqlen)) {
			err = EFAULT;
		}
	}

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_ioctl_cmd status is 0x%x, resid is 0x%lx\n",
	    ucmd->uscsi_status, ucmd->uscsi_resid);
	if (DEBUGGING && (rqlen != 0)) {
		int i, n, len;
		char *data = krqbuf;
		scsi_log(ST_DEVINFO, st_label, SCSI_DEBUG,
			"rqstatus=0x%x	rqlen=0x%x  rqresid=0x%x\n",
			ucmd->uscsi_rqstatus, ucmd->uscsi_rqlen,
			ucmd->uscsi_rqresid);
		len = (int)ucmd->uscsi_rqlen - ucmd->uscsi_rqresid;
		for (i = 0; i < len; i += 16) {
			n = min(16, len-1);
			st_clean_print(ST_DEVINFO, st_label, CE_NOTE,
				"  ", &data[i], n);
		}
	}

exit_free:
	/*
	 * Free resources
	 */
	un->un_sbuf_busy = 0;
	un->un_srqbufp = NULL;
	cv_signal(&un->un_sbuf_cv);

	if (krqbuf) {
		kmem_free(krqbuf, SENSE_LENGTH);
	}
	kmem_free(kcdb, kcmd->uscsi_cdblen);
	kmem_free(kcmd, sizeof (struct uscsi_cmd));

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_ioctl_cmd returns 0x%x\n", err);


exit:
	/* don't lose offline state */
	if (offline_state)
		un->un_state = ST_STATE_OFFLINE;

	ASSERT(mutex_owned(ST_MUTEX));
	return (err);
}

static int
st_write_fm(dev_t dev, int wfm)
{
	int i;
	int rval;

	GET_SOFT_STATE(dev);

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_write_fm(dev = 0x%lx, wfm = %d)\n", dev, wfm);

	/*
	 * write one filemark at the time after EOT
	 */
	if (un->un_eof >= ST_EOT) {
		for (i = 0; i < wfm; i++) {
			rval = st_cmd(dev, SCMD_WRITE_FILE_MARK, 1, SYNC_CMD);
			if (rval == EACCES) {
				return (rval);
			}
			if (rval != 0) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_write_fm : EIO : write EOT file mark");
				return (EIO);
			}
		}
	} else {
		rval = st_cmd(dev, SCMD_WRITE_FILE_MARK, wfm, SYNC_CMD);
		if (rval == EACCES) {
			return (rval);
		}
		if (rval) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_write_fm : EIO : write file mark");
			return (EIO);
		}
	}

	ASSERT(mutex_owned(ST_MUTEX));
	return (0);
}

#ifdef STDEBUG
static void
start_dump(struct scsi_tape *un, struct buf *bp)
{
	struct scsi_pkt *pkt = BP_PKT(bp);
	uchar_t *cdbp = (uchar_t *)pkt->pkt_cdbp;

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_start: cmd=0x%p count=%ld resid=%ld flags=0x%x pkt=0x%p\n",
	    (void *)bp->b_forw, bp->b_bcount,
	    bp->b_resid, bp->b_flags, (void *)BP_PKT(bp));

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_start: cdb %x %x %x %x %x %x, fileno=%d, blk=%ld\n",
	    cdbp[0], cdbp[1], cdbp[2],
	    cdbp[3], cdbp[4], cdbp[5], un->un_fileno,
	    un->un_blkno);
}
#endif


/*
 * Command start && done functions
 */

/*
 * st_start()
 *
 * Called from:
 *  st_strategy() to start a command.
 *  st_runout() to retry when scsi_pkt allocation fails on previous attempt(s).
 *  st_attach() when resuming from power down state.
 *  st_start_restart() to retry transport when device was previously busy.
 *  st_done_and_mutex_exit() to start the next command when previous is done.
 *
 * On entry:
 *  scsi_pkt may or may not be allocated.
 *
 */
static void
st_start(struct scsi_tape *un)
{
	struct buf *bp;
	int status;

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_start(): dev = 0x%lx\n", un->un_dev);

	if ((bp = un->un_quef) == NULL) {
		return;
	}

	ASSERT((bp->b_flags & B_DONE) == 0);

	/*
	 * Don't send more than un_throttle commands to the HBA
	 */
	if ((un->un_throttle <= 0) || (un->un_ncmds >= un->un_throttle)) {
		return;
	}

	/*
	 * If the buf has no scsi_pkt call st_make_cmd() to get one and
	 * build the command.
	 */
	if (BP_PKT(bp) == NULL) {
		ASSERT((bp->b_flags & B_DONE) == 0);
		st_make_cmd(un, bp, st_runout);
		ASSERT((bp->b_flags & B_DONE) == 0);
		status = geterror(bp);

		/*
		 * Some HBA's don't call bioerror() to set an error.
		 * And geterror() returns zero if B_ERROR is not set.
		 * So if we get zero we must check b_error.
		 */
		if (status == 0 && bp->b_error != 0) {
			status = bp->b_error;
			bioerror(bp, status);
		}

		/*
		 * Some HBA's convert DDI_DMA_NORESOURCES into ENOMEM.
		 * In tape ENOMEM has special meaning so we'll change it.
		 */
		if (status == ENOMEM) {
			status = 0;
			bioerror(bp, status);
		}

		/*
		 * Did it fail and is it retryable?
		 * If so return and wait for the callback through st_runout.
		 * Also looks like scsi_init_pkt() will setup a callback even
		 * if it isn't retryable.
		 */
		if (BP_PKT(bp) == NULL) {
			if (status == 0) {
				/*
				 * If first attempt save state.
				 */
				if (un->un_state != ST_STATE_RESOURCE_WAIT) {
					un->un_laststate = un->un_state;
					un->un_state = ST_STATE_RESOURCE_WAIT;
				}
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "temp no resources for pkt\n");
			} else {
				/*
				 * Unlikely that it would be retryable then not.
				 */
				if (un->un_state == ST_STATE_RESOURCE_WAIT) {
					un->un_state = un->un_laststate;
				}
				scsi_log(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "perm no resources for pkt errno = 0x%x\n",
				    status);
			}
			return;
		}
		/*
		 * Worked this time set the state back.
		 */
		if (un->un_state == ST_STATE_RESOURCE_WAIT) {
			un->un_state = un->un_laststate;
		}
	}

	/*
	 * move from waitq to runq
	 */
	un->un_quef = bp->b_actf;
	if (un->un_quel == bp) {
		/*
		 *  For the case of queue having one
		 *  element, set the tail pointer to
		 *  point to the element.
		 */
		un->un_quel = bp->b_actf;
	}

	bp->b_actf = NULL;

	if (un->un_runqf) {
		un->un_runql->b_actf = bp;
	} else {
		un->un_runqf = bp;
	}
	un->un_runql = bp;


#ifdef STDEBUG
	start_dump(un, bp);
#endif

	/* could not get here if throttle was zero */
	un->un_last_throttle = un->un_throttle;
	un->un_throttle = 0;	/* so nothing else will come in here */
	un->un_ncmds++;

	ST_DO_KSTATS(bp, kstat_waitq_to_runq);

	mutex_exit(ST_MUTEX);

	status = scsi_transport(BP_PKT(bp));

	mutex_enter(ST_MUTEX);

	if (un->un_last_throttle) {
		un->un_throttle = un->un_last_throttle;
	}

	if (status != TRAN_ACCEPT) {
		ST_DO_KSTATS(bp, kstat_runq_back_to_waitq);
		mutex_exit(ST_MUTEX);

		if (status == TRAN_BUSY) {
			/* if too many retries, fail the transport */
			if (st_handle_start_busy(un, bp,
			    ST_TRAN_BUSY_TIMEOUT) == 0)
				goto done;
		}
		scsi_log(ST_DEVINFO, st_label, CE_WARN,
		    "transport rejected\n");
		bp->b_resid = bp->b_bcount;


#ifndef __lock_lint
		/*
		 * warlock doesn't understand this potential
		 * recursion?
		 */
		mutex_enter(ST_MUTEX);
		ST_DO_KSTATS(bp, kstat_waitq_exit);
		ST_DO_ERRSTATS(un, st_transerrs);
		st_bioerror(bp, EIO);
		SET_PE_FLAG(un);
		st_done_and_mutex_exit(un, bp);
#endif
	} else {
		un->un_tran_retry_ct = 0;
		mutex_exit(ST_MUTEX);
	}

done:

	mutex_enter(ST_MUTEX);
}

/*
 * if the transport is busy, then put this bp back on the waitq
 */
static int
st_handle_start_busy(struct scsi_tape *un, struct buf *bp,
    clock_t timeout_interval)
{
	struct buf *last_quef, *runq_bp;
	int rval = 0;

	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_handle_start_busy()\n");

	/*
	 * Check to see if we hit the retry timeout and one last check for
	 * making sure this is the last on the runq, if it is not, we have
	 * to fail
	 */
	if (((int)un->un_tran_retry_ct++ > st_retry_count) ||
	    (un->un_runql != bp)) {
		rval = -1;
		goto exit;
	}

	/* put the bp back on the waitq */
	if (un->un_quef) {
		last_quef = un->un_quef;
		un->un_quef = bp;
		bp->b_actf = last_quef;
	} else  {
		bp->b_actf = NULL;
		un->un_quef = bp;
		un->un_quel = bp;
	}

	/*
	 * Decrement un_ncmds so that this
	 * gets thru' st_start() again.
	 */
	un->un_ncmds--;

	/*
	 * since this is an error case, we won't have to do
	 * this list walking much.  We've already made sure this bp was the
	 * last on the runq
	 */
	runq_bp = un->un_runqf;

	if (un->un_runqf == bp) {
		un->un_runqf = NULL;
		un->un_runql = NULL;
	} else {
		while (runq_bp) {
			if (runq_bp->b_actf == bp) {
				runq_bp->b_actf = NULL;
				un->un_runql = runq_bp;
				break;
			}
			runq_bp = runq_bp->b_actf;
		}
	}


	/*
	 * send a marker pkt, if appropriate
	 */
	st_hba_unflush(un);

	/*
	 * all queues are aligned, we are just waiting to
	 * transport, don't alloc any more buf p's, when
	 * st_start is reentered.
	 */
	(void) timeout(st_start_restart, un, timeout_interval);

exit:
	mutex_exit(ST_MUTEX);
	return (rval);
}


/*
 * st_runout a callback that is called what a resource allocatation failed
 */
static int
st_runout(caddr_t arg)
{
	struct scsi_tape *un = (struct scsi_tape *)arg;
	struct buf *bp;
	ASSERT(un != NULL);

	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG, "st_runout()\n");

	bp = un->un_quef;

	/*
	 * failed scsi_init_pkt(). If errno is zero its retryable.
	 */
	if ((bp != NULL) && (geterror(bp) != 0)) {

		scsi_log(ST_DEVINFO, st_label, CE_WARN,
		    "errors after pkt alloc (b_flags=0x%x, b_error=0x%x)\n",
		    bp->b_flags, geterror(bp));
		ASSERT((bp->b_flags & B_DONE) == 0);

		un->un_quef = bp->b_actf;
		if (un->un_quel == bp) {
			/*
			 *  For the case of queue having one
			 *  element, set the tail pointer to
			 *  point to the element.
			 */
			un->un_quel = bp->b_actf;
		}
		mutex_exit(ST_MUTEX);
		bp->b_actf = NULL;

		ASSERT((bp->b_flags & B_DONE) == 0);

		/*
		 * Set resid, Error already set, then unblock calling thread.
		 */
		bp->b_resid = bp->b_bcount;
		biodone(bp);
	} else {
		/*
		 * Try Again
		 */
		st_start(un);
		mutex_exit(ST_MUTEX);
	}

	/*
	 * Comments courtesy of sd.c
	 * The scsi_init_pkt routine allows for the callback function to
	 * return a 0 indicating the callback should be rescheduled or a 1
	 * indicating not to reschedule. This routine always returns 1
	 * because the driver always provides a callback function to
	 * scsi_init_pkt. This results in a callback always being scheduled
	 * (via the scsi_init_pkt callback implementation) if a resource
	 * failure occurs.
	 */

	return (1);
}

/*
 * st_done_and_mutex_exit()
 *	- remove bp from runq
 *	- start up the next request
 *	- if this was an asynch bp, clean up
 *	- exit with released mutex
 */
static void
st_done_and_mutex_exit(struct scsi_tape *un, struct buf *bp)
{
	struct buf *runqbp, *prevbp;
	int	pe_flagged = 0;

	ASSERT(MUTEX_HELD(&un->un_sd->sd_mutex));
#if !defined(lint)
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(&un->un_sd->sd_mutex))
#endif
	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * if bp is still on the runq (anywhere), then remove it
	 */
	prevbp = NULL;
	for (runqbp = un->un_runqf; runqbp != 0; runqbp = runqbp->b_actf) {
		if (runqbp == bp) {
			if (runqbp == un->un_runqf) {
				un->un_runqf = bp->b_actf;
			} else {
				prevbp->b_actf = bp->b_actf;
			}
			if (un->un_runql == bp) {
				un->un_runql = prevbp;
			}
			break;
		}
		prevbp = runqbp;
	}
	bp->b_actf = NULL;

	un->un_ncmds--;
	cv_signal(&un->un_queue_cv);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	"st_done_and_mutex_exit(): cmd=0x%x count=%ld resid=%ld  flags=0x%x\n",
		(uchar_t)*((caddr_t)(BP_PKT(bp))->pkt_cdbp),
		bp->b_bcount, bp->b_resid, bp->b_flags);


	/*
	 * update kstats with transfer count info
	 */
	if (un->un_stats && (bp != un->un_sbufp) && IS_RW(bp)) {
		uint32_t n_done =  bp->b_bcount - bp->b_resid;
		if (bp->b_flags & B_READ) {
			IOSP->reads++;
			IOSP->nread += n_done;
		} else {
			IOSP->writes++;
			IOSP->nwritten += n_done;
		}
	}

	/*
	 * Start the next one before releasing resources on this one, if
	 * there is something on the queue and persistent errors has not been
	 * flagged
	 */

	if ((pe_flagged = IS_PE_FLAG_SET(un)) != 0) {
		un->un_last_resid = bp->b_resid;
		un->un_last_count = bp->b_bcount;
	}

	if (un->un_pwr_mgmt == ST_PWR_SUSPENDED) {
		cv_broadcast(&un->un_tape_busy_cv);
	} else if (un->un_quef && un->un_throttle && !pe_flagged) {
		st_start(un);
	}

	if (bp == un->un_sbufp && (bp->b_flags & B_ASYNC)) {
		/*
		 * Since we marked this ourselves as ASYNC,
		 * there isn't anybody around waiting for
		 * completion any more.
		 */
		uchar_t com = (uchar_t)(uintptr_t)bp->b_forw;
		if (com == SCMD_READ || com == SCMD_WRITE) {
			bp->b_un.b_addr = (caddr_t)0;
		}
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_done_and_mutex_exit(async): freeing pkt\n");
		scsi_destroy_pkt(BP_PKT(bp));
		un->un_sbuf_busy = 0;
		cv_signal(&un->un_sbuf_cv);
		mutex_exit(ST_MUTEX);
		return;
	}

	if (bp == un->un_sbufp && BP_UCMD(bp)) {
		/*
		 * Copy status from scsi_pkt to uscsi_cmd
		 * since st_ioctl_cmd needs it
		 */
		BP_UCMD(bp)->uscsi_status = SCBP_C(BP_PKT(bp));
	}


#ifdef STDEBUG
	if ((st_debug >= 4) &&
	    (((un->un_blkno % 100) == 0) || IS_PE_FLAG_SET(un))) {

		ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_d_a_m_exit(): ncmds = %d, thr = %d, "
		    "un_errno = %d, un_pe = %d\n",
		    un->un_ncmds, un->un_throttle, un->un_errno,
		    un->un_persist_errors);
	}

#endif

	mutex_exit(ST_MUTEX);
	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_done_and_mutex_exit: freeing pkt\n");

	scsi_destroy_pkt(BP_PKT(bp));

	biodone(bp);

	/*
	 * now that we biodoned that command, if persistent errors have been
	 * flagged, flush the waitq
	 */
	if (pe_flagged)
		st_flush(un);
}


/*
 * Tape error, flush tape driver queue.
 */
static void
st_flush(struct scsi_tape *un)
{
	struct buf *bp;

	mutex_enter(ST_MUTEX);

	ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_flush(), ncmds = %d, quef = 0x%p\n",
	    un->un_ncmds, (void *)un->un_quef);

	/*
	 * if we still have commands outstanding, wait for them to come in
	 * before flushing the queue, and make sure there is a queue
	 */
	if (un->un_ncmds || !un->un_quef)
		goto exit;

	/*
	 * we have no more commands outstanding, so let's deal with special
	 * cases in the queue for EOM and FM. If we are here, and un_errno
	 * is 0, then we know there was no error and we return a 0 read or
	 * write before showing errors
	 */

	/* Flush the wait queue. */
	while ((bp = un->un_quef) != NULL) {
		un->un_quef = bp->b_actf;

		bp->b_resid = bp->b_bcount;

		ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_flush() : blkno=%ld, err=%d, b_bcount=%ld\n",
		    un->un_blkno, un->un_errno, bp->b_bcount);

		st_set_pe_errno(un);

		bioerror(bp, un->un_errno);

		mutex_exit(ST_MUTEX);
		/* it should have one, but check anyway */
		if (BP_PKT(bp)) {
			scsi_destroy_pkt(BP_PKT(bp));
		}
		biodone(bp);
		mutex_enter(ST_MUTEX);
	}

	/*
	 * It's not a bad practice to reset the
	 * waitq tail pointer to NULL.
	 */
	un->un_quel = NULL;

exit:
	/* we mucked with the queue, so let others know about it */
	cv_signal(&un->un_queue_cv);
	mutex_exit(ST_MUTEX);
}


/*
 * Utility functions
 */
static int
st_determine_generic(dev_t dev)
{
	int bsize;
	static char *cart = "0.25 inch cartridge";
	char *sizestr;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_determine_generic(dev = 0x%lx)\n", dev);

	ASSERT(mutex_owned(ST_MUTEX));

	if (st_modesense(un)) {
		return (-1);
	}

	bsize = (un->un_mspl->high_bl << 16)	|
		(un->un_mspl->mid_bl << 8)	|
		(un->un_mspl->low_bl);

	if (bsize == 0) {
		un->un_dp->options |= ST_VARIABLE;
		un->un_dp->bsize = 0;
		un->un_bsize = 0;
	} else if (bsize > ST_MAXRECSIZE_FIXED) {
		/*
		 * record size of this device too big.
		 * try and convert it to variable record length.
		 *
		 */
		un->un_dp->options |= ST_VARIABLE;
		if (st_change_block_size(dev, 0) != 0) {
			ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
			    "Fixed Record Size %d is too large\n", bsize);
			ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
			    "Cannot switch to variable record size\n");
			un->un_dp->options &= ~ST_VARIABLE;
			return (-1);
		}
	} else if (st_change_block_size(dev, 0) == 0) {
		/*
		 * If the drive was set to a non zero block size,
		 * See if it can be set to a zero block size.
		 * If it works, ST_VARIABLE so user can set it as they want.
		 */
		un->un_dp->options |= ST_VARIABLE;
		un->un_dp->bsize = 0;
		un->un_bsize = 0;
	} else {
		un->un_dp->bsize = bsize;
		un->un_bsize = bsize;
	}


	switch (un->un_mspl->density) {
	default:
	case 0x0:
		/*
		 * default density, cannot determine any other
		 * information.
		 */
		sizestr = "Unknown type- assuming 0.25 inch cartridge";
		un->un_dp->type = ST_TYPE_DEFAULT;
		un->un_dp->options |= (ST_AUTODEN_OVERRIDE|ST_QIC);
		break;
	case 0x1:
	case 0x2:
	case 0x3:
	case 0x6:
		/*
		 * 1/2" reel
		 */
		sizestr = "0.50 inch reel";
		un->un_dp->type = ST_TYPE_REEL;
		un->un_dp->options |= ST_REEL;
		un->un_dp->densities[0] = 0x1;
		un->un_dp->densities[1] = 0x2;
		un->un_dp->densities[2] = 0x6;
		un->un_dp->densities[3] = 0x3;
		break;
	case 0x4:
	case 0x5:
	case 0x7:
	case 0x0b:

		/*
		 * Quarter inch.
		 */
		sizestr = cart;
		un->un_dp->type = ST_TYPE_DEFAULT;
		un->un_dp->options |= ST_QIC;

		un->un_dp->densities[1] = 0x4;
		un->un_dp->densities[2] = 0x5;
		un->un_dp->densities[3] = 0x7;
		un->un_dp->densities[0] = 0x0b;
		break;

	case 0x0f:
	case 0x10:
	case 0x11:
	case 0x12:
		/*
		 * QIC-120, QIC-150, QIC-320, QIC-600
		 */
		sizestr = cart;
		un->un_dp->type = ST_TYPE_DEFAULT;
		un->un_dp->options |= ST_QIC;
		un->un_dp->densities[0] = 0x0f;
		un->un_dp->densities[1] = 0x10;
		un->un_dp->densities[2] = 0x11;
		un->un_dp->densities[3] = 0x12;
		break;

	case 0x09:
	case 0x0a:
	case 0x0c:
	case 0x0d:
		/*
		 * 1/2" cartridge tapes. Include HI-TC.
		 */
		sizestr = cart;
		sizestr[2] = '5';
		sizestr[3] = '0';
		un->un_dp->type = ST_TYPE_HIC;
		un->un_dp->densities[0] = 0x09;
		un->un_dp->densities[1] = 0x0a;
		un->un_dp->densities[2] = 0x0c;
		un->un_dp->densities[3] = 0x0d;
		break;

	case 0x13:
			/* DDS-2/DDS-3 scsi spec densities */
	case 0x24:
	case 0x25:
	case 0x26:
		sizestr = "DAT Data Storage (DDS)";
		un->un_dp->type = ST_TYPE_DAT;
		un->un_dp->options |= ST_AUTODEN_OVERRIDE;
		break;

	case 0x14:
		/*
		 * Helical Scan (Exabyte) devices
		 */
		sizestr = "8mm helical scan cartridge";
		un->un_dp->type = ST_TYPE_EXABYTE;
		un->un_dp->options |= ST_AUTODEN_OVERRIDE;
		break;
	}

	/*
	 * Assume LONG ERASE, BSF and BSR
	 */

	un->un_dp->options |= (ST_LONG_ERASE|ST_UNLOADABLE|ST_BSF|
				ST_BSR|ST_KNOWS_EOD);

	/*
	 * Only if mode sense data says no buffered write, set NOBUF
	 */
	if (un->un_mspl->bufm == 0)
		un->un_dp->options |= ST_NOBUF;

	/*
	 * set up large read and write retry counts
	 */

	un->un_dp->max_rretries = un->un_dp->max_wretries = 1000;

	/*
	 * If this is a 0.50 inch reel tape, and
	 * it is *not* variable mode, try and
	 * set it to variable record length
	 * mode.
	 */
	if ((un->un_dp->options & ST_REEL) && un->un_bsize != 0 &&
	    (un->un_dp->options & ST_VARIABLE)) {
		if (st_change_block_size(dev, 0) == 0) {
			un->un_dp->bsize = 0;
			un->un_mspl->high_bl = un->un_mspl->mid_bl =
			    un->un_mspl->low_bl = 0;
		}
	}

	/*
	 * Write to console about type of device found
	 */
	ST_DEBUG6(ST_DEVINFO, st_label, CE_NOTE,
	    "Generic Drive, Vendor=%s\n\t%s", un->un_dp->name,
	    sizestr);
	if (un->un_dp->options & ST_VARIABLE) {
		scsi_log(ST_DEVINFO, st_label, CE_NOTE,
		    "!Variable record length I/O\n");
	} else {
		scsi_log(ST_DEVINFO, st_label, CE_NOTE,
		    "!Fixed record length (%d byte blocks) I/O\n",
		    un->un_dp->bsize);
	}
	ASSERT(mutex_owned(ST_MUTEX));
	return (0);
}

static int
st_determine_density(dev_t dev, int rw)
{
	int rval = 0;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_determine_density(dev = 0x%lx, rw = %s)\n",
	    dev, (rw == B_WRITE ? wr_str: rd_str));

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * If we're past BOT, density is determined already.
	 */
	if (un->un_fileno > 0 || (un->un_fileno == 0 && un->un_blkno != 0)) {
		/*
		 * XXX: put in a bitch message about attempting to
		 * XXX: change density past BOT.
		 */
		goto exit;
	}

	/*
	 * If we're going to be writing, we set the density
	 */
	if (rw == 0 || rw == B_WRITE) {
		/* un_curdens is used as an index into densities table */
		un->un_curdens = MT_DENSITY(un->un_dev);
		if (st_set_density(dev)) {
			rval = -1;
		}
		goto exit;
	}

	/*
	 * If density is known already,
	 * we don't have to get it again.(?)
	 */
	if (!un->un_density_known) {
		if (st_get_density(dev)) {
			rval = -1;
		}
	}

exit:
	ASSERT(mutex_owned(ST_MUTEX));
	return (rval);
}


/*
 * Try to determine density. We do this by attempting to read the
 * first record off the tape, cycling through the available density
 * codes as we go.
 */

static int
st_get_density(dev_t dev)
{
	int succes = 0, rval = -1, i;
	uint_t size;
	uchar_t dens, olddens;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_get_density(dev = 0x%lx)\n", dev);

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * If Auto Density override is enabled The drive has
	 * only one density and there is no point in attempting
	 * find the correct one.
	 *
	 * Since most modern drives auto detect the density
	 * and format of the recorded media before they come
	 * ready. What this function does is a legacy behavior
	 * and modern drives not only don't need it, The backup
	 * utilities that do positioning via uscsi find the un-
	 * expected rewinds problematic.
	 *
	 * The drives that need this are old reel to reel devices.
	 * I took a swag and said they must be scsi-1 or older.
	 * I don't beleave there will any of the newer devices
	 * that need this. There will be some scsi-1 devices that
	 * don't need this but I don't think they will be using the
	 * BIG aftermarket backup and restore utilitys.
	 */
	if ((un->un_dp->options & ST_AUTODEN_OVERRIDE) ||
	    (un->un_sd->sd_inq->inq_ansi > 1)) {
		un->un_density_known = 1;
		rval = 0;
		goto exit;
	}

	/*
	 * This will only work on variable record length tapes
	 * if and only if all variable record length tapes autodensity
	 * select.
	 */
	size = (unsigned)(un->un_dp->bsize ? un->un_dp->bsize : SECSIZE);
	un->un_tmpbuf = kmem_alloc(size, KM_SLEEP);

	/*
	 * Start at the specified density
	 */

	dens = olddens = un->un_curdens = MT_DENSITY(un->un_dev);

	for (i = 0; i < NDENSITIES; i++, ((un->un_curdens == NDENSITIES - 1) ?
					(un->un_curdens = 0) :
					(un->un_curdens += 1))) {
		/*
		 * If we've done this density before,
		 * don't bother to do it again.
		 */
		dens = un->un_dp->densities[un->un_curdens];
		if (i > 0 && dens == olddens)
			continue;
		olddens = dens;
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "trying density 0x%x\n", dens);
		if (st_set_density(dev)) {
			continue;
		}

		/*
		 * XXX - the creates lots of headaches and slowdowns - must
		 * fix.
		 */
		succes = (st_cmd(dev, SCMD_READ, (int)size, SYNC_CMD) == 0);
		if (st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD)) {
			break;
		}
		if (succes) {
			st_init(un);
			rval = 0;
			un->un_density_known = 1;
			break;
		}
	}
	kmem_free(un->un_tmpbuf, size);
	un->un_tmpbuf = 0;

exit:
	ASSERT(mutex_owned(ST_MUTEX));
	return (rval);
}

static int
st_set_density(dev_t dev)
{
	int rval = 0;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_set_density(dev = 0x%lx): density = 0x%x\n", dev,
	    un->un_dp->densities[un->un_curdens]);

	ASSERT(mutex_owned(ST_MUTEX));

	un->un_mspl->density = un->un_dp->densities[un->un_curdens];

	if ((un->un_dp->options & ST_AUTODEN_OVERRIDE) == 0) {
		/*
		 * If auto density override is not set, Use mode select
		 * to set density and compression.
		 */
		if (st_modeselect(un)) {
			rval = -1;
		}
	} else if ((un->un_dp->options & ST_MODE_SEL_COMP) != 0) {
		/*
		 * If auto density and mode select compression are set,
		 * This is a drive with one density code but compression
		 * can be enabled or disabled.
		 * Set compression but no need to set density.
		 */
		rval = st_set_compression(un);
		if ((rval != 0) && (rval != EALREADY)) {
			rval = -1;
		} else {
			rval = 0;
		}
	}

	/* If sucessful set density and/or compression, mark density known */
	if (rval == 0) {
		un->un_density_known = 1;
	}

	ASSERT(mutex_owned(ST_MUTEX));
	return (rval);
}

static int
st_loadtape(dev_t dev)
{
	int rval;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_loadtape(dev = 0x%lx)\n", dev);

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * 'LOAD' the tape to BOT by rewinding
	 */
	rval = st_cmd(dev, SCMD_REWIND, 1, SYNC_CMD);
	if (rval == 0) {
		st_init(un);
		un->un_density_known = 0;
	}

	ASSERT(mutex_owned(ST_MUTEX));
	return (rval);
}


/*
 * Note: QIC devices aren't so smart.  If you try to append
 * after EOM, the write can fail because the device doesn't know
 * it's at EOM.	 In that case, issue a read.  The read should fail
 * because there's no data, but the device knows it's at EOM,
 * so a subsequent write should succeed.  To further confuse matters,
 * the target returns the same error if the tape is positioned
 * such that a write would overwrite existing data.  That's why
 * we have to do the append test.  A read in the middle of
 * recorded data would succeed, thus indicating we're attempting
 * something illegal.
 */

void bp_mapin(struct buf *bp);

static void
st_test_append(struct buf *bp)
{
	dev_t dev = bp->b_edev;
	struct scsi_tape *un;
	uchar_t status;
	unsigned bcount;

	un = ddi_get_soft_state(st_state, MTUNIT(dev));

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_test_append(): fileno %d\n", un->un_fileno);

	un->un_laststate = un->un_state;
	un->un_state = ST_STATE_APPEND_TESTING;
	un->un_test_append = 0;

	/*
	 * first, map in the buffer, because we're doing a double write --
	 * first into the kernel, then onto the tape.
	 */
	bp_mapin(bp);

	/*
	 * get a copy of the data....
	 */
	un->un_tmpbuf = kmem_alloc((unsigned)bp->b_bcount, KM_SLEEP);
	bcopy(bp->b_un.b_addr, un->un_tmpbuf, (uint_t)bp->b_bcount);

	/*
	 * attempt the write..
	 */

	if (st_cmd(dev, (int)SCMD_WRITE, (int)bp->b_bcount, SYNC_CMD) == 0) {
success:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "append write succeeded\n");
		bp->b_resid = un->un_sbufp->b_resid;
		mutex_exit(ST_MUTEX);
		bcount = (unsigned)bp->b_bcount;
		biodone(bp);
		mutex_enter(ST_MUTEX);
		un->un_laststate = un->un_state;
		un->un_state = ST_STATE_OPEN;
		kmem_free(un->un_tmpbuf, bcount);
		un->un_tmpbuf = NULL;
		return;
	}

	/*
	 * The append failed. Do a short read. If that fails,  we are at EOM
	 * so we can retry the write command. If that succeeds, than we're
	 * all screwed up (the controller reported a real error).
	 *
	 * XXX: should the dummy read be > SECSIZE? should it be the device's
	 * XXX: block size?
	 *
	 */
	status = un->un_status;
	un->un_status = 0;
	(void) st_cmd(dev, SCMD_READ, SECSIZE, SYNC_CMD);
	if (un->un_status == KEY_BLANK_CHECK) {
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "append at EOM\n");
		/*
		 * Okay- the read failed. We should actually have confused
		 * the controller enough to allow writing. In any case, the
		 * i/o is on its own from here on out.
		 */
		un->un_laststate = un->un_state;
		un->un_state = ST_STATE_OPEN;
		bcopy(bp->b_un.b_addr, un->un_tmpbuf, (uint_t)bp->b_bcount);
		if (st_cmd(dev, (int)SCMD_WRITE, (int)bp->b_bcount,
		    SYNC_CMD) == 0) {
			goto success;
		}
	}

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "append write failed- not at EOM\n");
	bp->b_resid = bp->b_bcount;
	st_bioerror(bp, EIO);

	ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_test_append : EIO : append write failed - not at EOM");

	/*
	 * backspace one record to get back to where we were
	 */
	if (st_cmd(dev, SCMD_SPACE, Blk(-1), SYNC_CMD)) {
		un->un_fileno = -1;
	}

	un->un_err_resid = bp->b_resid;
	un->un_status = status;

	/*
	 * Note: biodone will do a bp_mapout()
	 */
	mutex_exit(ST_MUTEX);
	bcount = (unsigned)bp->b_bcount;
	biodone(bp);
	mutex_enter(ST_MUTEX);
	un->un_laststate = un->un_state;
	un->un_state = ST_STATE_OPEN_PENDING_IO;
	kmem_free(un->un_tmpbuf, bcount);
	un->un_tmpbuf = NULL;
}

/*
 * Special command handler
 */

/*
 * common st_cmd code. The fourth parameter states
 * whether the caller wishes to await the results
 * Note the release of the mutex during most of the function
 */
static int
st_cmd(dev_t dev, int com, int count, int wait)
{
	struct buf *bp;
	int err;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_cmd(dev = 0x%lx, com = 0x%x, count = %x, wait = %d)\n",
	    dev, com, count, wait);

	ASSERT(MUTEX_HELD(&un->un_sd->sd_mutex));
	ASSERT(mutex_owned(ST_MUTEX));

#ifdef STDEBUG
	if (st_debug)
		st_debug_cmds(un, com, count, wait);
#endif

	/* check to see if this command requires the drive to be reserved */
	err = st_check_cmd_for_need_to_reserve(un, com, count);

	if (err) {
		return (err);
	}

	while (un->un_sbuf_busy)
		cv_wait(&un->un_sbuf_cv, ST_MUTEX);
	un->un_sbuf_busy = 1;

	bp = un->un_sbufp;
	bzero(bp, sizeof (buf_t));

	bp->b_flags = (wait) ? B_BUSY : B_BUSY|B_ASYNC;

	/*
	 * Set count to the actual size of the data tranfer.
	 * For commands with no data transfer, set bp->b_bcount
	 * to the value to be used when constructing the
	 * cdb in st_make_cmd().
	 */
	switch (com) {
	case SCMD_READ:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "special read %d\n", count);
		bp->b_flags |= B_READ;
		bp->b_un.b_addr = un->un_tmpbuf;
		break;

	case SCMD_WRITE:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "special write %d\n", count);
		bp->b_un.b_addr = un->un_tmpbuf;
		break;

	case SCMD_WRITE_FILE_MARK:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "write %d file marks\n", count);
		bp->b_bcount = count;
		count = 0;
		break;

	case SCMD_REWIND:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "rewind\n");
		bp->b_bcount = 0;
		count = 0;
		break;

	case SCMD_SPACE:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "space\n");
		bp->b_bcount = count;
		count = 0;
		break;

	case SCMD_RESERVE:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "reserve");
		bp->b_bcount = 0;
		count = 0;
		break;

	case SCMD_RELEASE:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "release");
		bp->b_bcount = 0;
		count = 0;
		break;

	case SCMD_LOAD:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "%s tape\n", (count) ? "load" : "unload");
		bp->b_bcount = count;
		count = 0;
		break;

	case SCMD_ERASE:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "erase tape\n");
		bp->b_bcount = 0;
		count = 0;
		break;

	case SCMD_MODE_SENSE:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "mode sense\n");
		bp->b_flags |= B_READ;
		bp->b_un.b_addr = (caddr_t)(un->un_mspl);
		break;

	case SCMD_MODE_SELECT:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "mode select\n");
		bp->b_un.b_addr = (caddr_t)(un->un_mspl);
		break;

	case SCMD_READ_BLKLIM:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "read block limits\n");
		bp->b_flags |= B_READ;
		bp->b_un.b_addr = (caddr_t)(un->un_rbl);
		break;

	case SCMD_TEST_UNIT_READY:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "test unit ready\n");
		bp->b_bcount = 0;
		count = 0;
		break;
	default:
		ST_DEBUG(ST_DEVINFO, st_label, CE_PANIC,
		    "Unhandled scsi command 0x%x in st_cmd()\n", com);
	}

	mutex_exit(ST_MUTEX);

	if (count > 0) {
		/*
		 * We're going to do actual I/O.
		 * Set things up for physio.
		 */
		struct iovec aiov;
		struct uio auio;
		struct uio *uio = &auio;

		bzero(&auio, sizeof (struct uio));
		bzero(&aiov, sizeof (struct iovec));
		aiov.iov_base = bp->b_un.b_addr;
		aiov.iov_len = count;

		uio->uio_iov = &aiov;
		uio->uio_iovcnt = 1;
		uio->uio_resid = aiov.iov_len;
		uio->uio_segflg = UIO_SYSSPACE;

		/*
		 * Let physio do the rest...
		 */
		bp->b_forw = (struct buf *)(uintptr_t)com;
		bp->b_back = NULL;
		err = physio(st_strategy, bp, dev,
			(bp->b_flags & B_READ) ? B_READ : B_WRITE,
			st_minphys, uio);
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_cmd: physio returns %d\n", err);
	} else {
		/*
		 * Mimic physio
		 */
		bp->b_forw = (struct buf *)(uintptr_t)com;
		bp->b_back = NULL;
		bp->b_edev = dev;
		bp->b_dev = cmpdev(dev);
		bp->b_blkno = 0;
		bp->b_resid = 0;
		(void) st_strategy(bp);
		if (!wait) {
			/*
			 * This is an async command- the caller won't wait
			 * and doesn't care about errors.
			 */
			mutex_enter(ST_MUTEX);
			return (0);
		}

		/*
		 * BugTraq #4260046
		 * ----------------
		 * Restore Solaris 2.5.1 behavior, namely call biowait
		 * unconditionally. The old comment said...
		 *
		 * "if strategy was flagged with  persistent errors, we would
		 *  have an error here, and the bp would never be sent, so we
		 *  don't want to wait on a bp that was never sent...or hang"
		 *
		 * The new rationale, courtesy of Chitrank...
		 *
		 * "we should unconditionally biowait() here because
		 *  st_strategy() will do a biodone() in the persistent error
		 *  case and the following biowait() will return immediately.
		 *  If not, in the case of "errors after pkt alloc" in
		 *  st_start(), we will not biowait here which will cause the
		 *  next biowait() to return immediately which will cause
		 *  us to send out the next command. In the case where both of
		 *  these use the sbuf, when the first command completes we'll
		 *  free the packet attached to sbuf and the same pkt will
		 *  get freed again when we complete the second command.
		 *  see esc 518987.  BTW, it is necessary to do biodone() in
		 *  st_start() for the pkt alloc failure case because physio()
		 *  does biowait() and will hang if we don't do biodone()"
		 */

		err = biowait(bp);
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_cmd: biowait returns %d\n", err);
	}
	mutex_enter(ST_MUTEX);

	un->un_sbuf_busy = 0;
	cv_signal(&un->un_sbuf_cv);
	return (err);
}

static int
st_set_compression(struct scsi_tape *un)
{
	int rval;
	int turn_compression_on;
	minor_t minor;

	/*
	 * Drive either dosn't have compression or it is controlled with
	 * special density codes. Return ENOTTY so caller
	 * knows nothing was done.
	 */
	if ((un->un_dp->options & ST_MODE_SEL_COMP) == 0) {
		un->un_comp_page = 0;
		return (ENOTTY);
	}

	/* set compression based on minor node opened */
	minor = MT_DENSITY(un->un_dev);

	/*
	 * If this the compression density or
	 * the drive has two densities and uses mode select for
	 * control of compression turn on compression for MT_DENSITY2
	 * as well.
	 */
	if ((minor == ST_COMPRESSION_DENSITY) ||
	    (minor == MT_DENSITY(MT_DENSITY2)) &&
	    (un->un_dp->densities[0] == un->un_dp->densities[1]) &&
	    (un->un_dp->densities[2] == un->un_dp->densities[3]) &&
	    (un->un_dp->densities[0] != un->un_dp->densities[2])) {

		turn_compression_on = 1;
	} else {
		turn_compression_on = 0;
	}

	un->un_mspl->high_bl = (uchar_t)(un->un_bsize >> 16);
	un->un_mspl->mid_bl  = (uchar_t)(un->un_bsize >> 8);
	un->un_mspl->low_bl  = (uchar_t)(un->un_bsize);

	/*
	 * Need to determine which page does the device use for compression.
	 * First try the data compression page. If this fails try the device
	 * configuration page
	 */

	if ((un->un_comp_page & ST_DEV_DATACOMP_PAGE) == ST_DEV_DATACOMP_PAGE) {
		rval = st_set_datacomp_page(un, turn_compression_on);
		if (rval == EALREADY) {
			return (rval);
		}
		if (rval != 0) {
			if (un->un_status == KEY_ILLEGAL_REQUEST) {
				/*
				 * This device does not support data
				 * compression page
				 */
				un->un_comp_page = ST_DEV_CONFIG_PAGE;
			} else if (un->un_state >= ST_STATE_OPEN) {
				un->un_fileno = -1;
				rval = EIO;
			} else {
				rval = -1;
			}
		} else {
			un->un_comp_page = ST_DEV_DATACOMP_PAGE;
		}
	}

	if ((un->un_comp_page & ST_DEV_CONFIG_PAGE) == ST_DEV_CONFIG_PAGE) {
		rval = st_set_devconfig_page(un, turn_compression_on);
		if (rval == EALREADY) {
			return (rval);
		}
		if (rval != 0) {
			if (un->un_status == KEY_ILLEGAL_REQUEST) {
				/*
				 * This device does not support
				 * compression at all advice the
				 * user and unset ST_MODE_SEL_COMP
				 */
				un->un_dp->options &= ~ST_MODE_SEL_COMP;
				un->un_comp_page = 0;
				scsi_log(ST_DEVINFO, st_label, CE_NOTE,
				    "Device Does Not Support Compression\n");
			} else if (un->un_state >= ST_STATE_OPEN) {
				un->un_fileno = -1;
				rval = EIO;
			} else {
				rval = -1;
			}
		}
	}

	return (rval);
}

/*
 * set or unset compression thru device configuration page.
 */
static int
st_set_devconfig_page(struct scsi_tape *un, int compression_on)
{
	unsigned char cflag;
	int rval = 0;

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * Figure what to set compression flag to.
	 */
	if (compression_on) {
		/* They have selected a compression node */
		if (un->un_dp->type == ST_TYPE_FUJI) {
			cflag = 0x84;   /* use EDRC */
		} else {
			cflag = ST_DEV_CONFIG_DEF_COMP;
		}
	} else {
		cflag = ST_DEV_CONFIG_NO_COMP;
	}

	/*
	 * If compression is already set the way it was requested.
	 * And if this not the first time we has tried.
	 */
	if ((cflag == un->un_mspl->page.dev.comp_alg) &&
	    (un->un_comp_page == ST_DEV_DATACOMP_PAGE)) {
		return (EALREADY);
	}

	un->un_mspl->page.dev.comp_alg = cflag;
	/*
	 * need to send mode select even if correct compression is
	 * already set since need to set density code
	 */

#ifdef STDEBUG
	if (st_debug >= 6) {
		st_clean_print(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_set_devconfig_page: sense data for mode select",
		    (char *)un->un_mspl, sizeof (struct seq_mode));
	}
#endif
	rval = st_gen_mode_select(un, un->un_mspl, sizeof (struct seq_mode));

	return (rval);
}

/*
 * set/reset compression bit thru data compression page
 */
static int
st_set_datacomp_page(struct scsi_tape *un, int compression_on)
{
	int compression_on_already;
	int rval = 0;

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * If drive is not capable of compression (at this time)
	 * return EALREADY so caller doesn't think that this page
	 * is not supported. This check is for drives that can
	 * disable compression from the front panel or configuration.
	 * I doubt that a drive that supports this page is not really
	 * capable of compression.
	 */
	if (un->un_mspl->page.comp.dcc == 0) {
		return (EALREADY);
	}

	/* See if compression currently turned on */
	if (un->un_mspl->page.comp.dce) {
		compression_on_already = 1;
	} else {
		compression_on_already = 0;
	}

	/*
	 * If compression is already set the way it was requested.
	 * And if this not the first time we has tried.
	 */
	if ((compression_on == compression_on_already) &&
	    (un->un_comp_page == ST_DEV_DATACOMP_PAGE)) {
		return (EALREADY);
	}

	/*
	 * if we are already set to the appropriate compression
	 * mode, don't set it again
	 */
	if (compression_on) {
		/* compression selected */
		un->un_mspl->page.comp.dce = 1;
	} else {
		un->un_mspl->page.comp.dce = 0;
	}


#ifdef STDEBUG
	if (st_debug >= 6) {
		st_clean_print(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_set_datacomp_page: sense data for mode select",
		    (char *)un->un_mspl, sizeof (struct seq_mode));
	}
#endif
	rval = st_gen_mode_select(un, un->un_mspl, sizeof (struct seq_mode));

	return (rval);
}

static int
st_modesense(struct scsi_tape *un)
{
	int rval;
	uchar_t page;

	page = un->un_comp_page;

	switch (page) {
	case ST_DEV_DATACOMP_PAGE:
	case ST_DEV_CONFIG_PAGE: /* fall through */
		rval = st_gen_mode_sense(un, page, un->un_mspl,
		    sizeof (struct seq_mode));
		break;

	case ST_DEV_DATACOMP_PAGE | ST_DEV_CONFIG_PAGE:
		if (un->un_dp->options & ST_MODE_SEL_COMP) {
			page = ST_DEV_DATACOMP_PAGE;
			rval = st_gen_mode_sense(un, page, un->un_mspl,
			    sizeof (struct seq_mode));
			if (rval == 0 && un->un_mspl->page_code == page) {
				un->un_comp_page = page;
				break;
			}
			page = ST_DEV_CONFIG_PAGE;
			rval = st_gen_mode_sense(un, page, un->un_mspl,
			    sizeof (struct seq_mode));
			if (rval == 0 && un->un_mspl->page_code == page) {
				un->un_comp_page = page;
				break;
			}
			un->un_dp->options &= ~ST_MODE_SEL_COMP;
			un->un_comp_page = 0;
		} else {
			un->un_comp_page = 0;
		}

	default:	/* fall through */
		rval = st_cmd(un->un_dev, SCMD_MODE_SENSE, MSIZE, SYNC_CMD);
	}
	return (rval);
}

static int
st_modeselect(struct scsi_tape *un)
{
	int rval = 0;
	int ix;

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_modeselect(dev = 0x%lx): density = 0x%x\n",
	    un->un_dev, un->un_mspl->density);

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * The parameter list should be the same for all of the
	 * cases that follow so set them here
	 *
	 * Try mode select first if if fails set fields manually
	 */
	rval = st_modesense(un);
	if (rval != 0) {
		ST_DEBUG3(ST_DEVINFO, st_label, CE_WARN,
		    "st_modeselect: First mode sense failed\n");
		un->un_mspl->bd_len  = 8;
		un->un_mspl->high_nb = 0;
		un->un_mspl->mid_nb  = 0;
		un->un_mspl->low_nb  = 0;
	}
	un->un_mspl->high_bl = (uchar_t)(un->un_bsize >> 16);
	un->un_mspl->mid_bl  = (uchar_t)(un->un_bsize >> 8);
	un->un_mspl->low_bl  = (uchar_t)(un->un_bsize);


	/*
	 * If configured to use a specific density code for a media type.
	 * curdens is previously set by the minor node opened.
	 * If the media type doesn't match the minor node we change it so it
	 * looks like the correct one was opened.
	 */
	if (un->un_dp->options & ST_KNOWS_MEDIA) {
		uchar_t best;

		for (best = 0xff, ix = 0; ix < NDENSITIES; ix++) {
			if (un->un_mspl->media_type ==
			    un->un_dp->mediatype[ix]) {
				best = ix;
				/*
				 * It matches but it might not be the only one.
				 * Use the highest matching media type but not
				 * to exceed the density selected by the open.
				 */
				if (ix < un->un_curdens) {
					continue;
				}
				un->un_curdens = ix;
				break;
			}
		}
		/* If a match was found best will not be 0xff any more */
		if (best < NDENSITIES) {
			ST_DEBUG3(ST_DEVINFO, st_label, CE_WARN,
			    "found media 0x%X using density 0x%X\n",
			    un->un_mspl->media_type,
			    un->un_dp->densities[best]);
			un->un_mspl->density = un->un_dp->densities[best];
		} else {
			/* Otherwise set density based on minor node opened */
			un->un_mspl->density =
			    un->un_dp->densities[un->un_curdens];
		}
	} else {
		un->un_mspl->density = un->un_dp->densities[un->un_curdens];
	}

	if (un->un_dp->options & ST_NOBUF) {
		un->un_mspl->bufm = 0;
	} else {
		un->un_mspl->bufm = 1;
	}

	rval = st_set_compression(un);

	/*
	 * If st_set_compression returned invalid or already it
	 * found no need to do the mode select.
	 * So do it here.
	 */
	if ((rval == ENOTTY) || (rval == EALREADY)) {

		/* Zero non-writeable fields */
		un->un_mspl->data_len = 0;
		un->un_mspl->media_type = 0;
		un->un_mspl->wp = 0;

		/* need to set the density code */
		rval = st_cmd(un->un_dev, SCMD_MODE_SELECT, MSIZE, SYNC_CMD);
		if (rval != 0) {
			if (un->un_state >= ST_STATE_OPEN) {
				ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
				    "unable to set tape mode\n");
				un->un_fileno = -1;
				rval = EIO;
			} else {
				rval = -1;
			}
		}
	}

	/*
	 * The spec recommends to send a mode sense after a mode select
	 */
	(void) st_modesense(un);

	ASSERT(mutex_owned(ST_MUTEX));

	return (rval);
}

/*
 * st_gen_mode_sense
 *
 * generic mode sense.. it allows for any page
 */
static int
st_gen_mode_sense(struct scsi_tape *un, int page, struct seq_mode *page_data,
    int page_size)
{

	int r;
	char	cdb[CDB_GROUP0];
	struct uscsi_cmd *com;

	com = kmem_zalloc(sizeof (*com), KM_SLEEP);

	bzero(cdb, CDB_GROUP0);
	cdb[0] = SCMD_MODE_SENSE;
	cdb[2] = (char)page;
	cdb[4] = (char)page_size;

	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP0;
	com->uscsi_bufaddr = (caddr_t)page_data;
	com->uscsi_buflen = page_size;
	com->uscsi_timeout = un->un_dp->non_motion_timeout;
	com->uscsi_flags = USCSI_DIAGNOSE | USCSI_SILENT |
			    USCSI_READ | USCSI_RQENABLE;

	r = st_ioctl_cmd(un->un_dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	kmem_free(com, sizeof (*com));
	return (r);
}

/*
 * st_gen_mode_select
 *
 * generic mode select.. it allows for any page
 */
static int
st_gen_mode_select(struct scsi_tape *un, struct seq_mode *page_data,
    int page_size)
{

	int r;
	char cdb[CDB_GROUP0];
	struct uscsi_cmd *com;

	/* Zero non-writeable fields */
	page_data->data_len = 0;
	page_data->media_type = 0;
	page_data->wp = 0;

	/*
	 * If mode select has any page data, zero the ps (Page Savable) bit.
	 */
	if (page_size > MSIZE) {
		page_data->ps = 0;
	}


	com = kmem_zalloc(sizeof (*com), KM_SLEEP);

	/*
	 * then, do a mode select to set what ever info
	 */
	bzero(cdb, CDB_GROUP0);
	cdb[0] = SCMD_MODE_SELECT;
	cdb[1] = 0x10;		/* set PF bit for many third party drives */
	cdb[4] = (char)page_size;

	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP0;
	com->uscsi_bufaddr = (caddr_t)page_data;
	com->uscsi_buflen = page_size;
	com->uscsi_timeout = un->un_dp->non_motion_timeout;
	com->uscsi_flags = USCSI_DIAGNOSE | USCSI_SILENT
		| USCSI_WRITE | USCSI_RQENABLE;

	r = st_ioctl_cmd(un->un_dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);

	kmem_free(com, sizeof (*com));
	return (r);
}

/*
 * Changes devices blocksize and bsize to requested blocksize nblksz.
 * Returns returned value from first failed call or zero on success.
 */
static int
st_change_block_size(dev_t dev, uint32_t nblksz)
{
	struct seq_mode *current;
	int rval;
	uint32_t oldblksz;

	GET_SOFT_STATE(dev);

	current = kmem_zalloc(MSIZE, KM_SLEEP);

	/* Read current settings */
	rval = st_gen_mode_sense(un, 0, current, MSIZE);
	if (rval != 0) {
		scsi_log(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "mode sense for change block size failed: rval = %d", rval);
		goto finish;
	}

	/* Figure the current block size */
	oldblksz =
	    (current->high_bl << 16) |
	    (current->mid_bl << 8) |
	    (current->low_bl);

	/* If current block size is the same as requested were done */
	if (oldblksz == nblksz) {
		un->un_bsize = nblksz;
		rval = 0;
		goto finish;
	}

	/* Change to requested block size */
	current->high_bl = (uchar_t)(nblksz >> 16);
	current->mid_bl  = (uchar_t)(nblksz >> 8);
	current->low_bl  = (uchar_t)(nblksz);

	/* Attempt to change block size */
	rval = st_gen_mode_select(un, current, MSIZE);
	if (rval != 0) {
		scsi_log(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Set new block size failed: rval = %d", rval);
		goto finish;
	}

	/* Read back and verify setting */
	rval = st_modesense(un);
	if (rval == 0) {
		un->un_bsize =
		    (un->un_mspl->high_bl << 16) |
		    (un->un_mspl->mid_bl << 8) |
		    (un->un_mspl->low_bl);

		if (un->un_bsize != nblksz) {
			scsi_log(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "Blocksize set does not equal requested blocksize"
			    "(read: %u requested: %u)\n", nblksz, un->un_bsize);
			rval = EIO;
		}
	}
finish:
	kmem_free(current, MSIZE);
	return (rval);
}


static void
st_init(struct scsi_tape *un)
{
	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	"st_init(): dev = 0x%lx, will reset fileno, blkno, eof\n", un->un_dev);

	un->un_blkno = 0;
	un->un_fileno = 0;
	un->un_lastop = ST_OP_NIL;
	un->un_eof = ST_NO_EOF;
	un->un_pwr_mgmt = ST_PWR_NORMAL;
	if (st_error_level != SCSI_ERR_ALL) {
		if (DEBUGGING) {
			st_error_level = SCSI_ERR_ALL;
		} else {
			st_error_level = SCSI_ERR_RETRYABLE;
		}
	}
}


static void
st_make_cmd(struct scsi_tape *un, struct buf *bp, int (*func)(caddr_t))
{
	struct scsi_pkt *pkt;
	struct uscsi_cmd *ucmd;
	int count, tval = 0;
	int flags = 0;
	uchar_t com;
	char fixbit;

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_make_cmd(): dev = 0x%lx\n", un->un_dev);


	/*
	 * fixbit is for setting the Fixed Mode and Suppress Incorrect
	 * Length Indicator bits on read/write commands, for setting
	 * the Long bit on erase commands, and for setting the Code
	 * Field bits on space commands.
	 * XXX why do we set lastop here?
	 */

	if (bp != un->un_sbufp) {		/* regular raw I/O */
		int stat_size = (un->un_arq_enabled ?
			sizeof (struct scsi_arq_status) : 1);
		pkt = scsi_init_pkt(ROUTE, NULL, bp,
		    CDB_GROUP0, stat_size, 0, 0, func, (caddr_t)un);
		if (pkt == NULL) {
			goto exit;
		}
		SET_BP_PKT(bp, pkt);
		if (un->un_bsize == 0) {
			count = bp->b_bcount;
			fixbit = 0;
		} else {
			count = bp->b_bcount / un->un_bsize;
			fixbit = 1;
		}
		if (bp->b_flags & B_READ) {
			com = SCMD_READ;
			un->un_lastop = ST_OP_READ;
			if ((un->un_bsize == 0) && /* Not Fixed Block */
			    (un->un_dp->options & ST_READ_IGNORE_ILI)) {
				fixbit = 2;
			}
		} else {
			com = SCMD_WRITE;
			un->un_lastop = ST_OP_WRITE;
		}

		tval = un->un_dp->io_timeout;

		/*
		 * For really large xfers, increase timeout
		 */
		if (bp->b_bcount > (10 * ONE_MEG))
			tval *= bp->b_bcount/(10 * ONE_MEG);

		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "%s %ld amt 0x%lx\n", (com == SCMD_WRITE) ?
		    wr_str: rd_str, un->un_blkno, bp->b_bcount);

	} else if ((ucmd = BP_UCMD(bp)) != NULL) {
		/*
		 * uscsi - build command, allocate scsi resources
		 */
		st_make_uscsi_cmd(un, ucmd, bp, func);
		goto exit;

	} else {				/* special I/O */
		struct buf *allocbp = NULL;
		int stat_size = (un->un_arq_enabled ?
			sizeof (struct scsi_arq_status) : 1);


		com = (uchar_t)(uintptr_t)bp->b_forw;
		count = bp->b_bcount;

		switch (com) {
		case SCMD_READ:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "special read %d\n", count);
			if (un->un_bsize == 0) {
				fixbit = 2;	/* suppress SILI */
			} else {
				fixbit = 1;	/* Fixed Block Mode */
				count /= un->un_bsize;
			}
			allocbp = bp;
			un->un_lastop = ST_OP_READ;
			tval = un->un_dp->io_timeout;
			break;

		case SCMD_WRITE:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "special write %d\n", count);
			if (un->un_bsize != 0) {
				fixbit = 1;	/* Fixed Block Mode */
				count /= un->un_bsize;
			} else {
				fixbit = 0;
			}
			allocbp = bp;
			un->un_lastop = ST_OP_WRITE;
			tval = un->un_dp->io_timeout;
			break;

		case SCMD_WRITE_FILE_MARK:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "write %d file marks\n", count);
			un->un_lastop = ST_OP_WEOF;
			fixbit = 0;
			tval = un->un_dp->io_timeout;
			break;

		case SCMD_REWIND:
			if (bp->b_flags & B_ASYNC) {
				fixbit = 1;
			} else {
				fixbit = 0;
			}
			count = 0;
			un->un_lastop = ST_OP_CTL;
			tval = un->un_dp->rewind_timeout;
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "rewind\n");
			break;

		case SCMD_SPACE:
			fixbit = Isfmk(count);
			count = (int)space_cnt(count);
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "space %d %s from file %d blk %ld\n",
			    count, (fixbit) ? "filemarks" : "records",
			    un->un_fileno, un->un_blkno);
			un->un_lastop = ST_OP_CTL;
			tval = un->un_dp->space_timeout;
			break;

		case SCMD_LOAD:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "%s tape\n", (count & 1) ? "load" : "unload");
			fixbit = 0;

			/* Loading or Unloading */
			if (count & 1) {
				tval = un->un_dp->load_timeout;
			} else {
				tval = un->un_dp->unload_timeout;
			}
			/* Is Retension requested */
			if (count & 2) {
				tval += un->un_dp->rewind_timeout;
			}
			un->un_lastop = ST_OP_CTL;
			break;

		case SCMD_ERASE:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "erase tape\n");
			count = 0;
			/*
			 * We support long erase only
			 */
			fixbit = 1;
			tval = un->un_dp->erase_timeout;
			un->un_lastop = ST_OP_CTL;
			break;

		case SCMD_MODE_SENSE:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "mode sense\n");
			allocbp = bp;
			fixbit = 0;
			tval = un->un_dp->non_motion_timeout;
			break;

		case SCMD_MODE_SELECT:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "mode select\n");
			allocbp = bp;
			fixbit = 0;
			tval = un->un_dp->non_motion_timeout;
			break;

		case SCMD_RESERVE:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "reserve\n");
			fixbit = 0;
			tval = un->un_dp->non_motion_timeout;
			break;

		case SCMD_RELEASE:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "release\n");
			fixbit = 0;
			tval = un->un_dp->non_motion_timeout;
			break;

		case SCMD_READ_BLKLIM:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "read block limits\n");
			allocbp = bp;
			fixbit = count = 0;
			tval = un->un_dp->non_motion_timeout;
			break;

		case SCMD_TEST_UNIT_READY:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "test unit ready\n");
			fixbit = 0;
			tval = un->un_dp->non_motion_timeout;
			break;
		default:
			ST_DEBUG(ST_DEVINFO, st_label, CE_PANIC,
			    "Unhandled scsi command 0x%x in st_make_cmd()\n",
			    com);
		}
		pkt = scsi_init_pkt(ROUTE, NULL, allocbp,
			CDB_GROUP0, stat_size, 0, 0, func, (caddr_t)un);
		if (pkt == NULL) {
			goto exit;
		}
		if (allocbp)
			ASSERT(geterror(allocbp) == 0);

	}

	(void) scsi_setup_cdb((union scsi_cdb *)pkt->pkt_cdbp,
	    com, 0, (uint_t)count, 0);
	FILL_SCSI1_LUN(un->un_sd, pkt);
	/*
	 * Initialize the SILI/Fixed bits of the byte 1 of cdb.
	 */
	((union scsi_cdb *)(pkt->pkt_cdbp))->t_code = fixbit;
	pkt->pkt_flags = flags;

	/*
	 * If ST_SHORT_FILEMARKS bit is ON for EXABYTE
	 * device, set the Vendor Unique bit to
	 * write Short File Mark.
	 */
	if (com == SCMD_WRITE_FILE_MARK &&
		un->un_dp->options & ST_SHORT_FILEMARKS) {
		switch (un->un_dp->type) {
		case ST_TYPE_EXB8500:
		case ST_TYPE_EXABYTE:
			/*
			 * Now the Vendor Unique bit 7 in Byte 5 of CDB
			 * is set to to write Short File Mark
			 */
			((union scsi_cdb *)pkt->pkt_cdbp)->g0_vu_1 = 1;
			break;

		default:
			/*
			 * Well, if ST_SHORT_FILEMARKS is set for other
			 * tape drives, it is just ignored
			 */
			break;
		}
	}
	ASSERT(tval);
	pkt->pkt_time = tval;
	pkt->pkt_comp = st_intr;
	pkt->pkt_private = (opaque_t)bp;
	SET_BP_PKT(bp, pkt);

exit:
	ASSERT(mutex_owned(ST_MUTEX));
}


/*
 * Build a command based on a uscsi command;
 */
static void
st_make_uscsi_cmd(struct scsi_tape *un, struct uscsi_cmd *ucmd,
    struct buf *bp, int (*func)(caddr_t))
{
	struct scsi_pkt *pkt;
	caddr_t cdb;
	int	cdblen;
	int stat_size;

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_make_uscsi_cmd(): dev = 0x%lx\n", un->un_dev);

	if (ucmd->uscsi_flags & USCSI_RQENABLE) {
		stat_size = (un->un_arq_enabled ?
		    sizeof (struct scsi_arq_status) : 1);
	} else {
		stat_size = 1;
	}

	ASSERT(mutex_owned(ST_MUTEX));

	un->un_lastop = ST_OP_CTL;	/* usual */

	cdb = ucmd->uscsi_cdb;
	cdblen = ucmd->uscsi_cdblen;

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_make_uscsi_cmd: buflen=%ld bcount=%ld\n",
		ucmd->uscsi_buflen, bp->b_bcount);
	pkt = scsi_init_pkt(ROUTE, NULL,
		(bp->b_bcount > 0) ? bp : NULL,
		cdblen, stat_size, 0, 0, func, (caddr_t)un);
	if (pkt == NULL) {
		goto exit;
	}

	bcopy(cdb, pkt->pkt_cdbp, (uint_t)cdblen);

#ifdef STDEBUG
	if (st_debug >= 6) {
		st_clean_print(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "pkt_cdbp", (char *)cdb, cdblen);
	}
#endif

	if (ucmd->uscsi_flags & USCSI_SILENT) {
		pkt->pkt_flags |= FLAG_SILENT;
	}

	pkt->pkt_time = ucmd->uscsi_timeout;
	pkt->pkt_comp = st_intr;
	pkt->pkt_private = (opaque_t)bp;
	SET_BP_PKT(bp, pkt);
exit:
	ASSERT(mutex_owned(ST_MUTEX));
}


/*
 * restart cmd currently at the head of the runq
 *
 * If scsi_transport() succeeds or the retries
 * count exhausted, restore the throttle that was
 * zeroed out in st_handle_intr_busy().
 *
 */
static void
st_intr_restart(void *arg)
{
	struct scsi_tape *un = arg;
	struct buf *bp;
	int status = TRAN_ACCEPT;

	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_intr_restart(), un = 0x%p\n", (void *)un);

	un->un_hib_tid = 0;

	/*
	 * move from waitq to runq, if there is anything on the waitq
	 */
	if ((bp = un->un_quef) == NULL) {
		mutex_exit(ST_MUTEX);
		return;
	}

	/*
	 * Here we know :
	 *	throttle = 0, via st_handle_intr_busy
	 */

	if (un->un_quel == bp) {
		un->un_quel = NULL;
		un->un_quef = NULL;	/* we know it's the first one */
	} else {
		un->un_quef = bp->b_actf;
	}
	bp->b_actf = NULL;

	if (un->un_runqf) {
		/*
		 * not good, we don't want to requeue something after
		 * another.
		 */
		mutex_exit(ST_MUTEX);
		goto done_error;
	} else {
		un->un_runqf = bp;
		un->un_runql = bp;
	}

	ST_DO_KSTATS(bp, kstat_waitq_to_runq);

	mutex_exit(ST_MUTEX);

	status = scsi_transport(BP_PKT(bp));

	mutex_enter(ST_MUTEX);

	if (status != TRAN_ACCEPT) {
		ST_DO_KSTATS(bp, kstat_runq_back_to_waitq);
		mutex_exit(ST_MUTEX);

		if (status == TRAN_BUSY) {
			if (st_handle_intr_busy(un, bp,
			    ST_TRAN_BUSY_TIMEOUT) == 0)
				return;	/* timeout is setup again */
		}

	} else {
		un->un_tran_retry_ct = 0;
		if (un->un_last_throttle) {
			un->un_throttle = un->un_last_throttle;
		}
		mutex_exit(ST_MUTEX);
		return;
	}

done_error:
	ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
	    "restart transport rejected\n");
	bp->b_resid = bp->b_bcount;

#ifndef __lock_lint
	/*
	 * warlock doesn't understand this potential
	 * recursion?
	 */
	mutex_enter(ST_MUTEX);
	if (un->un_last_throttle) {
		un->un_throttle = un->un_last_throttle;
	}
	if (status != TRAN_ACCEPT)
		ST_DO_ERRSTATS(un, st_transerrs);
	ST_DO_KSTATS(bp, kstat_waitq_exit);
	SET_PE_FLAG(un);
	st_bioerror(bp, EIO);
	st_done_and_mutex_exit(un, bp);
#endif
	ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
	    "busy restart aborted\n");
}

/*
 * st_check_media():
 * Periodically check the media state using scsi_watch service;
 * this service calls back after TUR and possibly request sense
 * the callback handler (st_media_watch_cb()) decodes the request sense
 * data (if any)
 */

static int
st_check_media(dev_t dev, enum mtio_state state)
{
	int rval = 0;
	enum mtio_state	prev_state;
	opaque_t token = NULL;

	GET_SOFT_STATE(dev);

	mutex_enter(ST_MUTEX);

	ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_check_media:state=%x, mediastate=%x\n",
	    state, un->un_mediastate);

	prev_state = un->un_mediastate;

	/*
	 * is there anything to do?
	 */
retry:
	if (state == un->un_mediastate || un->un_mediastate == MTIO_NONE) {
		/*
		 * submit the request to the scsi_watch service;
		 * scsi_media_watch_cb() does the real work
		 */
		mutex_exit(ST_MUTEX);
		token = scsi_watch_request_submit(ST_SCSI_DEVP,
			st_check_media_time, SENSE_LENGTH,
			st_media_watch_cb, (caddr_t)dev);
		if (token == NULL) {
			rval = EAGAIN;
			goto done;
		}
		mutex_enter(ST_MUTEX);

		un->un_swr_token = token;
		un->un_specified_mediastate = state;

		/*
		 * now wait for media change
		 * we will not be signalled unless mediastate == state but it
		 * still better to test for this condition, since there
		 * is a 5 sec cv_broadcast delay when
		 *  mediastate == MTIO_INSERTED
		 */
		ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
			"st_check_media:waiting for media state change\n");
		while (un->un_mediastate == state) {
			if (cv_wait_sig(&un->un_state_cv, ST_MUTEX) == 0) {
				mutex_exit(ST_MUTEX);
				ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_check_media:waiting for media state "
				    "was interrupted\n");
				rval = EINTR;
				goto done;
			}
			ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_check_media:received signal, state=%x\n",
			    un->un_mediastate);
		}
	}

	/*
	 * if we transitioned to MTIO_INSERTED, media has really been
	 * inserted.  If TUR fails, it is probably a exabyte slow spin up.
	 * Reset and retry the state change.  If everything is ok, replay
	 * the open() logic.
	 */
	if ((un->un_mediastate == MTIO_INSERTED) &&
	    (un->un_state == ST_STATE_OFFLINE)) {
		ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_check_media: calling st_cmd to confirm inserted\n");

		/*
		 * set this early so that TUR will make it through strategy
		 * without triggering a st_tape_init().  We needed it set
		 * before calling st_tape_init() ourselves anyway.  If TUR
		 * fails, set it back
		 */
		un->un_state = ST_STATE_INITIALIZING;

		/*
		 * If not reserved fail as getting reservation conflict
		 * will make this hang forever.
		 */
		if ((un->un_rsvd_status &
		    (ST_RESERVE | ST_APPLICATION_RESERVATIONS)) == 0) {
			mutex_exit(ST_MUTEX);
			rval = EACCES;
			goto done;
		}
		rval = st_cmd(dev, SCMD_TEST_UNIT_READY, 0, SYNC_CMD);
		if (rval == EACCES) {
			ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_check_media: TUR got Reservation Conflict\n");
			mutex_exit(ST_MUTEX);
			goto done;
		}
		if (rval) {
			ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_check_media: TUR failed, going to retry\n");
			un->un_mediastate = prev_state;
			un->un_state = ST_STATE_OFFLINE;
			goto retry;
		}
		ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_check_media: media inserted\n");

		/* this also rewinds the tape */
		rval = st_tape_init(dev);
		if (rval != 0) {
			ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_check_media : OFFLINE init failure ");
			un->un_state = ST_STATE_OFFLINE;
			un->un_fileno = -1;
		} else {
			un->un_state = ST_STATE_OPEN_PENDING_IO;
			un->un_fileno = 0;
			un->un_blkno = 0;
		}
	} else if ((un->un_mediastate == MTIO_EJECTED) &&
		(un->un_state != ST_STATE_OFFLINE)) {
		/*
		 * supported devices must be rewound before ejection
		 * rewind resets fileno & blkno
		 */
		un->un_laststate = un->un_state;
		un->un_state = ST_STATE_OFFLINE;
	}
	mutex_exit(ST_MUTEX);
done:
	if (token) {
		(void) scsi_watch_request_terminate(token,
				SCSI_WATCH_TERMINATE_WAIT);
		mutex_enter(ST_MUTEX);
		un->un_swr_token = (opaque_t)NULL;
		mutex_exit(ST_MUTEX);
	}

	ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG, "st_check_media: done\n");

	return (rval);
}

/*
 * st_media_watch_cb() is called by scsi_watch_thread for
 * verifying the request sense data (if any)
 */
static int
st_media_watch_cb(caddr_t arg, struct scsi_watch_result *resultp)
{
	struct scsi_status *statusp = resultp->statusp;
	struct scsi_extended_sense *sensep = resultp->sensep;
	uchar_t actual_sense_length = resultp->actual_sense_length;
	struct scsi_tape *un;
	enum mtio_state state = MTIO_NONE;
	int instance;
	dev_t dev = (dev_t)arg;

	instance = MTUNIT(dev);
	if ((un = ddi_get_soft_state(st_state, instance)) == NULL) {
		return (-1);
	}

	mutex_enter(ST_MUTEX);
	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_media_watch_cb: status=%x, sensep=%p, len=%x\n",
			*((char *)statusp), (void *)sensep,
			actual_sense_length);

	/*
	 * if there was a check condition then sensep points to valid
	 * sense data
	 * if status was not a check condition but a reservation or busy
	 * status then the new state is MTIO_NONE
	 */
	if (sensep) {
		ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_media_watch_cb: KEY=%x, ASC=%x, ASCQ=%x\n",
		    sensep->es_key, sensep->es_add_code, sensep->es_qual_code);

		switch (un->un_dp->type) {
		default:
			ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_media_watch_cb: unknown drive type %d, default to ST_TYPE_HP\n",
	    un->un_dp->type);
		/* FALLTHROUGH */

		case ST_TYPE_STC3490:	/* STK 4220 1/2" cartridge */
		case ST_TYPE_FUJI:	/* 1/2" cartridge */
		case ST_TYPE_HP:	/* HP 88780 1/2" reel */
			if (un->un_dp->type == ST_TYPE_FUJI) {
				ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_media_watch_cb: ST_TYPE_FUJI\n");
			} else {
				ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_media_watch_cb: ST_TYPE_HP\n");
			}
			switch (sensep->es_key) {
			case KEY_UNIT_ATTENTION:
				/* not ready to ready transition */
				/* hp/es_qual_code == 80 on>off>on */
				/* hp/es_qual_code == 0 on>off>unld>ld>on */
				if (sensep->es_add_code == 0x28) {
					state = MTIO_INSERTED;
				}
				break;
			case KEY_NOT_READY:
				/* in process, rewinding or loading */
				if ((sensep->es_add_code == 0x04) &&
				    (sensep->es_qual_code == 0x00)) {
					state = MTIO_EJECTED;
				}
				break;
			}
			break;

		case ST_TYPE_EXB8500:	/* Exabyte 8500 */
			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_media_watch_cb: ST_TYPE_EXB8500\n");
			switch (sensep->es_key) {
			case KEY_UNIT_ATTENTION:
				/* operator medium removal request */
				if ((sensep->es_add_code == 0x5a) &&
				    (sensep->es_qual_code == 0x01)) {
					state = MTIO_EJECTED;
				/* not ready to ready transition */
				} else if ((sensep->es_add_code == 0x28) &&
				    (sensep->es_qual_code == 0x00)) {
					state = MTIO_INSERTED;
				}
				break;
			case KEY_NOT_READY:
				/* medium not present */
				if (sensep->es_add_code == 0x3a) {
					state = MTIO_EJECTED;
				}
				break;
			}
			break;
		case ST_TYPE_EXABYTE:	/* Exabyte 8200 */
			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_media_watch_cb: ST_TYPE_EXABYTE\n");
			switch (sensep->es_key) {
			case KEY_NOT_READY:
				if ((sensep->es_add_code == 0x04) &&
				    (sensep->es_qual_code == 0x00)) {
					/* volume not mounted? */
					state = MTIO_EJECTED;
				} else if (sensep->es_add_code == 0x3a) {
					state = MTIO_EJECTED;
				}
				break;
			case KEY_UNIT_ATTENTION:
				state = MTIO_EJECTED;
				break;
			}
			break;

		case ST_TYPE_DLT:		/* quantum DLT4xxx */
			switch (sensep->es_key) {
			case KEY_UNIT_ATTENTION:
				if (sensep->es_add_code == 0x28) {
					state = MTIO_INSERTED;
				}
				break;
			case KEY_NOT_READY:
				if (sensep->es_add_code == 0x04) {
					/* in transition but could be either */
					state = un->un_specified_mediastate;
				} else if ((sensep->es_add_code == 0x3a) &&
				    (sensep->es_qual_code == 0x00)) {
					state = MTIO_EJECTED;
				}
				break;
			}
			break;
		}
	} else if (*((char *)statusp) == STATUS_GOOD) {
		state = MTIO_INSERTED;
	}

	ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_media_watch_cb:state=%x, specified=%x\n",
	    state, un->un_specified_mediastate);

	/*
	 * now signal the waiting thread if this is *not* the specified state;
	 * delay the signal if the state is MTIO_INSERTED
	 * to allow the target to recover
	 */
	if (state != un->un_specified_mediastate) {
		un->un_mediastate = state;
		if (state == MTIO_INSERTED) {
			/*
			 * delay the signal to give the drive a chance
			 * to do what it apparently needs to do
			 */
			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_media_watch_cb:delayed cv_broadcast\n");
			un->un_delay_tid = timeout(st_delayed_cv_broadcast,
			    un, drv_usectohz((clock_t)MEDIA_ACCESS_DELAY));
		} else {
			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_media_watch_cb:immediate cv_broadcast\n");
			cv_broadcast(&un->un_state_cv);
		}
	}
	mutex_exit(ST_MUTEX);
	return (0);
}

/*
 * delayed cv_broadcast to allow for target to recover
 * from media insertion
 */
static void
st_delayed_cv_broadcast(void *arg)
{
	struct scsi_tape *un = arg;

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_delayed_cv_broadcast:delayed cv_broadcast\n");

	mutex_enter(ST_MUTEX);
	cv_broadcast(&un->un_state_cv);
	mutex_exit(ST_MUTEX);
}

/*
 * restart cmd currently at the start of the waitq
 */
static void
st_start_restart(void *arg)
{
	struct scsi_tape *un = arg;

	ASSERT(un != NULL);

	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_tran_restart()\n");

	if (un->un_quef) {
		st_start(un);
	}

	mutex_exit(ST_MUTEX);
}


/*
 * Command completion processing
 *
 */
static void
st_intr(struct scsi_pkt *pkt)
{
	struct scsi_tape *un;
	struct buf *last_runqf;
	struct buf *bp;
	int action = COMMAND_DONE;
	clock_t	timout;
	int	status;

	bp = (struct buf *)pkt->pkt_private;
	un = ddi_get_soft_state(st_state, MTUNIT(bp->b_edev));

	mutex_enter(ST_MUTEX);

	un->un_rqs_state &= ~(ST_RQS_ERROR);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG, "st_intr()\n");

	if (pkt->pkt_reason != CMD_CMPLT) {

		/* If device has gone away not much else to do */
		if (pkt->pkt_reason == CMD_DEV_GONE) {
			action = COMMAND_DONE_ERROR;
		} else if (un->un_state == ST_STATE_SENSING) {
			ST_DO_ERRSTATS(un, st_transerrs);
			action = COMMAND_DONE_ERROR;
		} else {
			action = st_handle_incomplete(un, bp);
		}
	/*
	 * At this point we know that the command was successfully
	 * completed. Now what?
	 */
	} else if (un->un_arq_enabled &&
	    (pkt->pkt_state & STATE_ARQ_DONE)) {
		/*
		 * the transport layer successfully completed an autorqsense
		 */
		action = st_handle_autosense(un, bp);

	} else if (un->un_state == ST_STATE_SENSING) {
		/*
		 * okay. We were running a REQUEST SENSE. Find
		 * out what to do next.
		 * some actions are based on un_state, hence
		 * restore the state st was in before ST_STATE_SENSING.
		 */
		un->un_state = un->un_laststate;
		action = st_handle_sense(un, bp);
		/*
		 * set pkt back to original packet in case we will have
		 * to requeue it
		 */
		pkt = BP_PKT(bp);
	} else  if ((SCBP(pkt)->sts_busy) || (SCBP(pkt)->sts_chk)) {
		/*
		 * Okay, we weren't running a REQUEST SENSE. Call a routine
		 * to see if the status bits we're okay. If a request sense
		 * is to be run, that will happen.
		 */
		action = st_check_error(un, pkt);
	}

	if (un->un_pwr_mgmt == ST_PWR_SUSPENDED) {
		switch (action) {
			case QUE_COMMAND:
				/*
				 * return cmd to head to the queue
				 * since we are suspending so that
				 * it gets restarted during resume
				 */
				if (un->un_runqf) {
					last_runqf = un->un_runqf;
					un->un_runqf = bp;
					bp->b_actf = last_runqf;
				} else {
					bp->b_actf = NULL;
					un->un_runqf = bp;
					un->un_runql = bp;
				}
				action = JUST_RETURN;
				break;

			case QUE_SENSE:
				action = COMMAND_DONE_ERROR;
				break;

			default:
				break;
		}
	}

	/*
	 * Restore old state if we were sensing.
	 */
	if (un->un_state == ST_STATE_SENSING && action != QUE_SENSE) {
		un->un_state = un->un_laststate;
	}

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_intr: pkt=%p, bp=%p, action=%x, status=%x\n",
	    (void *)pkt, (void *)bp, action, SCBP_C(pkt));


	switch (action) {
	case COMMAND_DONE_EACCES:
		/* this is to report a reservation conflict */
		st_bioerror(bp, EACCES);
		ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
			"Reservation Conflict \n");

		/*FALLTHROUGH*/
	case COMMAND_DONE_ERROR:
		if (un->un_eof < ST_EOT_PENDING &&
		    un->un_state >= ST_STATE_OPEN) {
			/*
			 * all errors set state of the tape to 'unknown'
			 * unless we're at EOT or are doing append testing.
			 * If sense key was illegal request, preserve state.
			 */
			if (un->un_status != KEY_ILLEGAL_REQUEST) {
				un->un_fileno = -1;
			}
		}
		un->un_err_resid = bp->b_resid = bp->b_bcount;
		/*
		 * since we have an error (COMMAND_DONE_ERROR), we want to
		 * make sure an error ocurrs, so make sure at least EIO is
		 * returned
		 */
		if (geterror(bp) == 0)
			st_bioerror(bp, EIO);

		SET_PE_FLAG(un);
		if (!(un->un_rqs_state & ST_RQS_ERROR) &&
		    (un->un_errno == EIO)) {
			un->un_rqs_state &= ~(ST_RQS_VALID);
		}
		goto done;

	case COMMAND_DONE_ERROR_RECOVERED:
		un->un_err_resid = bp->b_resid = bp->b_bcount;
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_intr(): COMMAND_DONE_ERROR_RECOVERED");
		if (geterror(bp) == 0)
			st_bioerror(bp, EIO);
		SET_PE_FLAG(un);
		if (!(un->un_rqs_state & ST_RQS_ERROR) &&
		    (un->un_errno == EIO)) {
			un->un_rqs_state &= ~(ST_RQS_VALID);
		}
		/*FALLTHROUGH*/
	case COMMAND_DONE:
		st_set_state(un);
done:
		ST_DO_KSTATS(bp, kstat_runq_exit);
		st_done_and_mutex_exit(un, bp);
		return;

	case QUE_SENSE:
		if ((un->un_ncmds > 1) && !un->un_flush_on_errors)
			goto sense_error;

		if (un->un_state != ST_STATE_SENSING) {
			un->un_laststate = un->un_state;
			un->un_state = ST_STATE_SENSING;
		}

		un->un_rqs->pkt_private = (opaque_t)bp;
		bzero(ST_RQSENSE, SENSE_LENGTH);

		if (un->un_throttle) {
			un->un_last_throttle = un->un_throttle;
			un->un_throttle = 0;
		}

		mutex_exit(ST_MUTEX);

		/*
		 * never retry this, some other command will have nuked the
		 * sense, anyway
		 */
		status = scsi_transport(un->un_rqs);

		mutex_enter(ST_MUTEX);

		if (un->un_last_throttle) {
			un->un_throttle = un->un_last_throttle;
		}

		if (status == TRAN_ACCEPT) {
			mutex_exit(ST_MUTEX);
			return;
		}
		if (status != TRAN_BUSY)
			ST_DO_ERRSTATS(un, st_transerrs);
sense_error:
		un->un_fileno = -1;
		st_bioerror(bp, EIO);
		SET_PE_FLAG(un);
		goto done;

	case QUE_BUSY_COMMAND:
		/* longish timeout */
		timout = ST_STATUS_BUSY_TIMEOUT;
		goto que_it_up;

	case QUE_COMMAND:
		/* short timeout */
		timout = ST_TRAN_BUSY_TIMEOUT;
que_it_up:
		/*
		 * let st_handle_intr_busy put this bp back on waitq and make
		 * checks to see if it is ok to requeue the command.
		 */
		ST_DO_KSTATS(bp, kstat_runq_back_to_waitq);

		/*
		 * Save the throttle before setting up the timeout
		 */
		if (un->un_throttle) {
			un->un_last_throttle = un->un_throttle;
		}
		mutex_exit(ST_MUTEX);
		if (st_handle_intr_busy(un, bp, timout) == 0)
			return;		/* timeout is setup again */

		mutex_enter(ST_MUTEX);
		un->un_fileno = -1;
		un->un_err_resid = bp->b_resid = bp->b_bcount;
		st_bioerror(bp, EIO);
		SET_PE_FLAG(un);
		goto done;

	case QUE_LAST_COMMAND:

		if ((un->un_ncmds > 1) && !un->un_flush_on_errors) {
			scsi_log(ST_DEVINFO, st_label, CE_CONT,
			    "un_ncmds: %d can't retry cmd \n", un->un_ncmds);
			goto last_command_error;
		}
		mutex_exit(ST_MUTEX);
		if (st_handle_intr_retry_lcmd(un, bp) == 0)
			return;
		mutex_enter(ST_MUTEX);
last_command_error:
		un->un_err_resid = bp->b_resid = bp->b_bcount;
		un->un_fileno = -1;
		st_bioerror(bp, EIO);
		SET_PE_FLAG(un);
		goto done;

	case JUST_RETURN:
	default:
		ST_DO_KSTATS(bp, kstat_runq_back_to_waitq);
		mutex_exit(ST_MUTEX);
		return;
	}
	/*NOTREACHED*/
}

static int
st_handle_incomplete(struct scsi_tape *un, struct buf *bp)
{
	static char *fail = "SCSI transport failed: reason '%s': %s\n";
	int rval = COMMAND_DONE_ERROR;
	struct scsi_pkt *pkt = (un->un_state == ST_STATE_SENSING) ?
			un->un_rqs : BP_PKT(bp);
	int result;

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_handle_incomplete(): dev = 0x%lx\n", un->un_dev);

	ASSERT(mutex_owned(ST_MUTEX));

	switch (pkt->pkt_reason) {
	case CMD_INCOMPLETE:	/* tran stopped with not normal state */
		/*
		 * this occurs when accessing a powered down drive, no
		 * need to complain; just fail the open
		 */
#ifdef STDEBUG
		if (st_debug >= 1) {
			st_clean_print(ST_DEVINFO, st_label, CE_WARN,
			    "Failed CDB", (char *)pkt->pkt_cdbp, CDB_SIZE);
		}

#endif
		/*
		 * if we have commands outstanding in HBA, and a command
		 * comes back incomplete, we're hosed, so reset target
		 * If we have the bus, but cmd_incomplete, we probably just
		 * have a failed selection, so don't reset the target, just
		 * requeue the command and try again
		 */
		if ((un->un_ncmds > 1) || (pkt->pkt_state != STATE_GOT_BUS)) {
			goto reset_target;
		}

		/*
		 * Retry selection a couple more times if we're
		 * open.  If opening, we only try just once to
		 * reduce probe time for nonexistant devices.
		 */
		if ((un->un_laststate > ST_STATE_OPENING) &&
		    ((int)un->un_retry_ct < st_selection_retry_count)) {
			rval = QUE_COMMAND;
		}
		ST_DO_ERRSTATS(un, st_transerrs);
		break;

	case CMD_ABORTED:
		/*
		 * most likely this is caused by flush-on-error support. If
		 * it was not there, the we're in trouble.
		 */
		if (!un->un_flush_on_errors) {
			un->un_status = SUN_KEY_FATAL;
			goto reset_target;
		}

		st_set_pe_errno(un);
		bioerror(bp, un->un_errno);
		if (un->un_errno)
			return (COMMAND_DONE_ERROR);
		else
			return (COMMAND_DONE);

	case CMD_TIMEOUT:	/* Command timed out */
		un->un_status = SUN_KEY_TIMEOUT;

		/*FALLTHROUGH*/
	default:
reset_target:
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "transport completed with %s\n",
		    scsi_rname(pkt->pkt_reason));
		ST_DO_ERRSTATS(un, st_transerrs);
		if ((pkt->pkt_state & STATE_GOT_TARGET) &&
		    ((pkt->pkt_statistics & (STAT_BUS_RESET | STAT_DEV_RESET |
			STAT_ABORTED)) == 0)) {

			/*
			 * If we haven't reserved the drive don't reset it.
			 */
			if ((un->un_rsvd_status &
			    (ST_RESERVE | ST_APPLICATION_RESERVATIONS)) == 0) {
				return (rval);
			}

			mutex_exit(ST_MUTEX);

			result = scsi_reset(ROUTE, RESET_TARGET);
			/*
			 * if target reset fails, then pull the chain
			 */
			if (result == 0) {
				result = scsi_reset(ROUTE, RESET_ALL);
			}
			mutex_enter(ST_MUTEX);

			if ((result == 0) && (un->un_state >= ST_STATE_OPEN)) {
				/* no hope left to recover */
				scsi_log(ST_DEVINFO, st_label, CE_WARN,
				    "recovery by resets failed\n");
				return (rval);
			}
		}
	}

	if ((pkt->pkt_reason == CMD_RESET) || (pkt->pkt_statistics &
		(STAT_BUS_RESET | STAT_DEV_RESET))) {
		if ((un->un_rsvd_status & ST_RESERVE)) {
			un->un_rsvd_status |= ST_LOST_RESERVE;
			ST_DEBUG3(ST_DEVINFO, st_label, CE_WARN,
				"Lost Reservation\n");
		}
	}

	if ((int)un->un_retry_ct++ < st_retry_count) {
		if (un->un_pwr_mgmt == ST_PWR_SUSPENDED) {
			rval = QUE_COMMAND;
		} else if (bp == un->un_sbufp) {
			switch ((uchar_t)(uintptr_t)bp->b_forw) {
			case SCMD_MODE_SENSE:
			case SCMD_MODE_SELECT:
			case SCMD_READ_BLKLIM:
			case SCMD_REWIND:
			case SCMD_LOAD:
			case SCMD_TEST_UNIT_READY:
				/*
				 * These commands can be rerun with impunity
				 */
				rval = QUE_COMMAND;
				break;

			default:
				break;
			}
		}
	} else {
		rval = COMMAND_DONE_ERROR;
	}

	if (un->un_state >= ST_STATE_OPEN) {
		scsi_log(ST_DEVINFO, st_label, CE_WARN,
		    fail, scsi_rname(pkt->pkt_reason),
		    (rval == COMMAND_DONE_ERROR)?
		    "giving up" : "retrying command");
	}
	return (rval);
}

/*
 * if the device is busy, then put this bp back on the waitq, on the
 * interrupt thread, where we want the head of the queue and not the
 * end
 *
 * The callers of this routine should take measures to save the
 * un_throttle in un_last_throttle which will be restored in
 * st_intr_restart(). The only exception should be st_intr_restart()
 * calling this routine for which the saving is already done.
 */
static int
st_handle_intr_busy(struct scsi_tape *un, struct buf *bp,
	clock_t timeout_interval)
{
	struct buf *last_quef;
	int rval = 0;

	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_handle_intr_busy(), un = 0x%p\n", (void *)un);

	/*
	 * Check to see if we hit the retry timeout. We check to make sure
	 * this is the first one on the runq and make sure we have not
	 * queued up any more, so this one has to be the last on the list
	 * also. If it is not, we have to fail.  If it is not the first, but
	 * is the last we are in trouble anyway, as we are in the interrupt
	 * context here.
	 */
	if (((int)un->un_tran_retry_ct++ > st_retry_count) ||
	    ((un->un_runqf != bp) && (un->un_runql != bp))) {
		rval = -1;
		goto exit;
	}

	/* put the bp back on the waitq */
	if (un->un_quef) {
		last_quef = un->un_quef;
		un->un_quef = bp;
		bp->b_actf = last_quef;
	} else  {
		bp->b_actf = NULL;
		un->un_quef = bp;
		un->un_quel = bp;
	}

	/*
	 * We know that this is the first and last on the runq at this time,
	 * so we just nullify those two queues
	 */
	un->un_runqf = NULL;
	un->un_runql = NULL;

	/*
	 * We don't want any other commands being started in the mean time.
	 * If start had just released mutex after putting something on the
	 * runq, we won't even get here.
	 */
	un->un_throttle = 0;

	/*
	 * send a marker pkt, if appropriate
	 */
	st_hba_unflush(un);

	/*
	 * all queues are aligned, we are just waiting to
	 * transport
	 */
	un->un_hib_tid = timeout(st_intr_restart, un, timeout_interval);

exit:
	mutex_exit(ST_MUTEX);
	return (rval);
}

static int
st_handle_sense(struct scsi_tape *un, struct buf *bp)
{
	struct scsi_pkt *rqpkt = un->un_rqs;
	int rval = COMMAND_DONE_ERROR;
	int amt;

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_handle_sense()\n");

	if (SCBP(rqpkt)->sts_busy) {
		ST_DEBUG4(ST_DEVINFO, st_label, CE_WARN,
		    "busy unit on request sense\n");
		if ((int)un->un_retry_ct++ < st_retry_count) {
			rval = QUE_BUSY_COMMAND;
		}
		return (rval);
	} else if (SCBP(rqpkt)->sts_chk) {
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "Check Condition on REQUEST SENSE\n");
		return (rval);
	}

	/* was there enough data? */
	amt = (int)SENSE_LENGTH - rqpkt->pkt_resid;
	if ((rqpkt->pkt_state & STATE_XFERRED_DATA) == 0 ||
	    (amt < SUN_MIN_SENSE_LENGTH)) {
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "REQUEST SENSE couldn't get sense data\n");
		return (rval);
	}
	return (st_decode_sense(un, bp, amt, SCBP(rqpkt)));
}

static int
st_handle_autosense(struct scsi_tape *un, struct buf *bp)
{
	struct scsi_pkt *pkt = BP_PKT(bp);
	struct scsi_arq_status *arqstat =
	    (struct scsi_arq_status *)pkt->pkt_scbp;
	int rval = COMMAND_DONE_ERROR;
	int amt;

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_handle_autosense()\n");

	if (arqstat->sts_rqpkt_status.sts_busy) {
		ST_DEBUG4(ST_DEVINFO, st_label, CE_WARN,
		    "busy unit on request sense\n");
		/*
		 * we return QUE_SENSE so st_intr will setup the SENSE cmd.
		 * the disadvantage is that we do not have any delay for the
		 * second retry of rqsense and we have to keep a packet around
		 */
		return (QUE_SENSE);

	} else if (arqstat->sts_rqpkt_reason != CMD_CMPLT) {
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "transport error on REQUEST SENSE\n");
		if ((arqstat->sts_rqpkt_state & STATE_GOT_TARGET) &&
		    ((arqstat->sts_rqpkt_statistics &
		    (STAT_BUS_RESET | STAT_DEV_RESET | STAT_ABORTED)) == 0)) {
			mutex_exit(ST_MUTEX);
			if (scsi_reset(ROUTE, RESET_TARGET) == 0) {
				/*
				 * if target reset fails, then pull the chain
				 */
				if (scsi_reset(ROUTE, RESET_ALL) == 0) {
					ST_DEBUG6(ST_DEVINFO, st_label,
					    CE_WARN,
					    "recovery by resets failed\n");
				}
			}
			mutex_enter(ST_MUTEX);
		}
		return (rval);

	} else if (arqstat->sts_rqpkt_status.sts_chk) {
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "Check Condition on REQUEST SENSE\n");
		return (rval);
	}


	/* was there enough data? */
	amt = (int)SENSE_LENGTH - arqstat->sts_rqpkt_resid;
	if ((arqstat->sts_rqpkt_state & STATE_XFERRED_DATA) == 0 ||
	    (amt < SUN_MIN_SENSE_LENGTH)) {
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "REQUEST SENSE couldn't get sense data\n");
		return (rval);
	}

	bcopy(&arqstat->sts_sensedata, ST_RQSENSE, SENSE_LENGTH);

	return (st_decode_sense(un, bp, amt, &arqstat->sts_rqpkt_status));
}

static int
st_decode_sense(struct scsi_tape *un, struct buf *bp,  int amt,
	struct scsi_status *statusp)
{
	struct scsi_pkt *pkt = BP_PKT(bp);
	int rval = COMMAND_DONE_ERROR;
	long resid;
	struct scsi_extended_sense *sensep = ST_RQSENSE;
	int severity;
	int get_error;

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_decode_sense()\n");

	/*
	 * For uscsi commands, squirrel away a copy of the
	 * results of the Request Sense.
	 */
	if (USCSI_CMD(bp)) {
		struct uscsi_cmd *ucmd = BP_UCMD(bp);
		ucmd->uscsi_rqstatus = *(uchar_t *)statusp;
		if (ucmd->uscsi_rqlen && un->un_srqbufp) {
			uchar_t rqlen = min((uchar_t)amt, ucmd->uscsi_rqlen);
			ucmd->uscsi_rqresid = ucmd->uscsi_rqlen - rqlen;
			bcopy(ST_RQSENSE, un->un_srqbufp, rqlen);
			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
				"st_decode_sense: stat=0x%x resid=0x%x\n",
				ucmd->uscsi_rqstatus, ucmd->uscsi_rqresid);
		}
	}

	/*
	 * If the drive is an MT-02, reposition the
	 * secondary error code into the proper place.
	 *
	 * XXX	MT-02 is non-CCS tape, so secondary error code
	 * is in byte 8.  However, in SCSI-2, tape has CCS definition
	 * so it's in byte 12.
	 */
	if (un->un_dp->type == ST_TYPE_EMULEX) {
		sensep->es_code = sensep->es_add_info[0];
	}

	/* for normal I/O check extract the resid values. */
	if (bp != un->un_sbufp) {
		if (sensep->es_valid) {
			resid = (sensep->es_info_1 << 24) |
				(sensep->es_info_2 << 16) |
				(sensep->es_info_3 << 8)  |
				(sensep->es_info_4);
			if (un->un_bsize) {
				resid *= un->un_bsize;
			}
		} else if (pkt->pkt_state & STATE_XFERRED_DATA) {
			resid = pkt->pkt_resid;
		} else {
			resid = bp->b_bcount;
		}
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_handle_sense (rw): xferred bit = %d, resid=%ld (%d), "
		    "pkt_resid=%ld\n", pkt->pkt_state & STATE_XFERRED_DATA,
		    resid,
		    (sensep->es_info_1 << 24) |
		    (sensep->es_info_2 << 16) |
		    (sensep->es_info_3 << 8)  |
		    (sensep->es_info_4),
		    pkt->pkt_resid);
		/*
		 * The problem is, what should we believe?
		 */
		if (resid && (pkt->pkt_resid == 0)) {
			pkt->pkt_resid = resid;
		}
	} else {
		/*
		 * If the command is SCMD_SPACE, we need to get the
		 * residual as returned in the sense data, to adjust
		 * our idea of current tape position correctly
		 */
		if ((CDBP(pkt)->scc_cmd == SCMD_SPACE ||
		    CDBP(pkt)->scc_cmd == SCMD_WRITE_FILE_MARK) &&
		    (sensep->es_valid)) {
			resid = (sensep->es_info_1 << 24) |
			    (sensep->es_info_2 << 16) |
			    (sensep->es_info_3 << 8)  |
			    (sensep->es_info_4);
			bp->b_resid = resid;
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_handle_sense(other):	resid=%ld\n",
			    resid);
		} else {
			/*
			 * If the special command is SCMD_READ,
			 * the correct resid will be set later.
			 */
			resid = bp->b_bcount;
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_handle_sense(special read):  resid=%ld\n",
				resid);
		}
	}

	if ((un->un_state >= ST_STATE_OPEN) &&
	    (DEBUGGING || st_error_level == SCSI_ERR_ALL)) {
		st_clean_print(ST_DEVINFO, st_label, CE_NOTE,
		    "Failed CDB", (char *)pkt->pkt_cdbp, CDB_SIZE);
		st_clean_print(ST_DEVINFO, st_label, CE_CONT,
		    "sense data", (char *)sensep, amt);
		scsi_log(ST_DEVINFO, st_label, CE_CONT,
		    "count 0x%lx resid 0x%lx pktresid 0x%lx\n",
		    bp->b_bcount, resid, pkt->pkt_resid);
	}

	switch (un->un_status = sensep->es_key) {
	case KEY_NO_SENSE:
		severity = SCSI_ERR_INFO;
		goto common;

	case KEY_RECOVERABLE_ERROR:
		severity = SCSI_ERR_RECOVERED;
		if ((sensep->es_class == CLASS_EXTENDED_SENSE) &&
		    (sensep->es_code == ST_DEFERRED_ERROR)) {
		    if (un->un_dp->options &
			ST_RETRY_ON_RECOVERED_DEFERRED_ERROR) {
			    rval = QUE_LAST_COMMAND;
			    scsi_errmsg(ST_SCSI_DEVP, pkt, st_label, severity,
				un->un_blkno, un->un_err_blkno, scsi_cmds,
				sensep);
			    scsi_log(ST_DEVINFO, st_label, CE_CONT,
				"Command will be retried\n");
			} else {
			    severity = SCSI_ERR_FATAL;
			    rval = COMMAND_DONE_ERROR_RECOVERED;
			    ST_DO_ERRSTATS(un, st_softerrs);
			    scsi_errmsg(ST_SCSI_DEVP, pkt, st_label, severity,
				un->un_blkno, un->un_err_blkno, scsi_cmds,
				sensep);
			}
			break;
		}
common:
		/*
		 * XXX only want reads to be stopped by filemarks.
		 * Don't want them to be stopped by EOT.  EOT matters
		 * only on write.
		 */
		if (sensep->es_filmk && !sensep->es_eom) {
			rval = COMMAND_DONE;
		} else if (sensep->es_eom) {
			rval = COMMAND_DONE;
		} else if (sensep->es_ili) {
			/*
			 * Fun with variable length record devices:
			 * for specifying larger blocks sizes than the
			 * actual physical record size.
			 */
			if (un->un_bsize == 0 && resid > 0) {
				/*
				 * XXX! Ugly.
				 * The requested blocksize is > tape blocksize,
				 * so this is ok, so we just return the
				 * actual size xferred.
				 */
				pkt->pkt_resid = resid;
				rval = COMMAND_DONE;
			} else if (un->un_bsize == 0 && resid < 0) {
				/*
				 * The requested blocksize is < tape blocksize,
				 * so this is not ok, so we err with ENOMEM
				 */
				rval = COMMAND_DONE_ERROR_RECOVERED;
				st_bioerror(bp, ENOMEM);
			} else {
				ST_DO_ERRSTATS(un, st_softerrs);
				severity = SCSI_ERR_FATAL;
				rval = COMMAND_DONE_ERROR;
				st_bioerror(bp, EINVAL);
			}
		} else {
			/*
			 * we hope and pray for this just being
			 * something we can ignore (ie. a
			 * truly recoverable soft error)
			 */
			rval = COMMAND_DONE;
		}
		if (sensep->es_filmk) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "filemark\n");
			un->un_status = SUN_KEY_EOF;
			un->un_eof = ST_EOF_PENDING;
			SET_PE_FLAG(un);
		}

		/*
		 * ignore eom when reading, a fmk should terminate reading
		 */
		if ((sensep->es_eom) &&
		    (CDBP(pkt)->scc_cmd != SCMD_READ)) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "eom\n");
			un->un_status = SUN_KEY_EOT;
			un->un_eof = ST_EOM;
			SET_PE_FLAG(un);
		}

		break;

	case KEY_ILLEGAL_REQUEST:

		if (un->un_laststate >= ST_STATE_OPEN) {
			ST_DO_ERRSTATS(un, st_softerrs);
			severity = SCSI_ERR_FATAL;
		} else {
			severity = SCSI_ERR_INFO;
		}
		break;

	case KEY_MEDIUM_ERROR:
		ST_DO_ERRSTATS(un, st_harderrs);
		severity = SCSI_ERR_FATAL;

		/*
		 * for (buffered) writes, a medium error must be fatal
		 */
		if (CDBP(pkt)->scc_cmd != SCMD_WRITE) {
			rval = COMMAND_DONE_ERROR_RECOVERED;
		}

check_keys:
		/*
		 * attempt to process the keys in the presence of
		 * other errors
		 */
		if (sensep->es_ili && rval != COMMAND_DONE_ERROR) {
			/*
			 * Fun with variable length record devices:
			 * for specifying larger blocks sizes than the
			 * actual physical record size.
			 */
			if (un->un_bsize == 0 && resid > 0) {
				/*
				 * XXX! Ugly
				 */
				pkt->pkt_resid = resid;
			} else if (un->un_bsize == 0 && resid < 0) {
				st_bioerror(bp, EINVAL);
			} else {
				severity = SCSI_ERR_FATAL;
				rval = COMMAND_DONE_ERROR;
				st_bioerror(bp, EINVAL);
			}
		}
		if (sensep->es_filmk) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "filemark\n");
			un->un_status = SUN_KEY_EOF;
			un->un_eof = ST_EOF_PENDING;
			SET_PE_FLAG(un);
		}

		/*
		 * ignore eom when reading, a fmk should terminate reading
		 */
		if ((sensep->es_eom) &&
		    (CDBP(pkt)->scc_cmd != SCMD_READ)) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "eom\n");
			un->un_status = SUN_KEY_EOT;
			un->un_eof = ST_EOM;
			SET_PE_FLAG(un);
		}

		break;

	case KEY_VOLUME_OVERFLOW:
		ST_DO_ERRSTATS(un, st_softerrs);
		un->un_eof = ST_EOM;
		severity = SCSI_ERR_FATAL;
		rval = COMMAND_DONE_ERROR;
		goto check_keys;

	case KEY_HARDWARE_ERROR:
		ST_DO_ERRSTATS(un, st_harderrs);
		severity = SCSI_ERR_FATAL;
		rval = COMMAND_DONE_ERROR;
		if (un->un_dp->options & ST_EJECT_ON_CHANGER_FAILURE)
			un->un_eject_tape_on_failure = st_check_asc_ascq(un);
		break;

	case KEY_BLANK_CHECK:
		ST_DO_ERRSTATS(un, st_softerrs);
		severity = SCSI_ERR_INFO;

		/*
		 * if not a special request and some data was xferred then it
		 * it is not an error yet
		 */
		if (bp != un->un_sbufp && (bp->b_flags & B_READ)) {
			/*
			 * no error for read with or without data xferred
			 */
			un->un_status = SUN_KEY_EOT;
			un->un_eof = ST_EOT;
			rval = COMMAND_DONE_ERROR;
			SET_PE_FLAG(un);
			goto check_keys;
		} else if (bp != un->un_sbufp &&
		    (pkt->pkt_state & STATE_XFERRED_DATA)) {
			rval = COMMAND_DONE;
		} else {
			rval = COMMAND_DONE_ERROR_RECOVERED;
		}

		if (un->un_laststate >= ST_STATE_OPEN) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "blank check\n");
			un->un_eof = ST_EOM;
		}
		if ((CDBP(pkt)->scc_cmd == SCMD_SPACE) &&
		    (un->un_dp->options & ST_KNOWS_EOD) &&
		    (severity = SCSI_ERR_INFO)) {
			/*
			 * we were doing a fast forward by skipping
			 * multiple fmk at the time
			 */
			st_bioerror(bp, EIO);
			severity = SCSI_ERR_RECOVERED;
			rval	 = COMMAND_DONE;
		}
		SET_PE_FLAG(un);
		goto check_keys;

	case KEY_WRITE_PROTECT:
		if (st_wrongtapetype(un)) {
			un->un_status = SUN_KEY_WRONGMEDIA;
			ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		"wrong tape for writing- use DC6150 tape (or equivalent)\n");
			severity = SCSI_ERR_UNKNOWN;
		} else {
			severity = SCSI_ERR_FATAL;
		}
		ST_DO_ERRSTATS(un, st_harderrs);
		rval = COMMAND_DONE_ERROR;
		st_bioerror(bp, EACCES);
		break;

	case KEY_UNIT_ATTENTION:
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "KEY_UNIT_ATTENTION : un_state = %d\n", un->un_state);

		/*
		 * If we have detected a Bus Reset and the tape
		 * drive has been reserved.
		 */
		if (ST_RQSENSE->es_add_code == 0x29 &&
			(un->un_rsvd_status & ST_RESERVE)) {
			un->un_rsvd_status |= ST_LOST_RESERVE;
			ST_DEBUG(ST_DEVINFO, st_label, CE_WARN,
				"st_decode_sense: Lost Reservation\n");
		}

		if (un->un_state <= ST_STATE_OPENING) {
			/*
			 * Look, the tape isn't open yet, now determine
			 * if the cause is a BUS RESET, Save the file and
			 * Block positions for the callers to recover from
			 * the loss of position.
			 */
			if ((un->un_fileno >= 0) &&
			(un->un_fileno || un->un_blkno)) {
				if (ST_RQSENSE->es_add_code == 0x29) {
					un->un_save_fileno = un->un_fileno;
					un->un_save_blkno = un->un_blkno;
					un->un_restore_pos = 1;
				}
			}

			if ((int)un->un_retry_ct++ < st_retry_count) {
				rval = QUE_COMMAND;
			} else {
				rval = COMMAND_DONE_ERROR;
			}
			severity = SCSI_ERR_INFO;

		} else {
			/*
			 * Check if it is an Unexpected Unit Attention.
			 * If state is >= ST_STATE_OPEN, we have
			 * already done the initialization .
			 * In this case it is Fatal Error
			 * since no further reading/writing
			 * can be done with fileno set to < 0.
			 */
			if (un->un_state >= ST_STATE_OPEN) {
				ST_DO_ERRSTATS(un, st_harderrs);
				severity = SCSI_ERR_FATAL;
			} else {
				severity = SCSI_ERR_INFO;
			}
			rval = COMMAND_DONE_ERROR;
		}
		un->un_fileno = -1;

		break;

	case KEY_NOT_READY:
		/*
		 * If in process of getting ready retry.
		 */
		if (sensep->es_add_code  == 0x04 &&
		    sensep->es_qual_code == 0x01 &&
		    un->un_retry_ct++ < st_retry_count) {
			rval = QUE_COMMAND;
			severity = SCSI_ERR_INFO;
		} else {
			/* give up */
			rval = COMMAND_DONE_ERROR;
			severity = SCSI_ERR_FATAL;
		}

		/*
		 * If this was an error and after device opened
		 * do error stats.
		 */
		if (rval == COMMAND_DONE_ERROR &&
		    un->un_state > ST_STATE_OPENING) {
			ST_DO_ERRSTATS(un, st_harderrs);
		}

		if (ST_RQSENSE->es_add_code == 0x3a) {
			if (st_error_level >= SCSI_ERR_FATAL)
				scsi_log(ST_DEVINFO, st_label, CE_NOTE,
				    "Tape not inserted in drive\n");
			un->un_mediastate = MTIO_EJECTED;
			cv_broadcast(&un->un_state_cv);
		}
		if ((un->un_dp->options & ST_EJECT_ON_CHANGER_FAILURE) &&
		    (rval != QUE_COMMAND))
			un->un_eject_tape_on_failure = st_check_asc_ascq(un);
		break;

	case KEY_ABORTED_COMMAND:

		/*
		 * Probably a parity error...
		 * if we retry here then this may cause data to be
		 * written twice or data skipped during reading
		 */
		ST_DO_ERRSTATS(un, st_harderrs);
		severity = SCSI_ERR_FATAL;
		rval = COMMAND_DONE_ERROR;
		goto check_keys;

	default:
		/*
		 * Undecoded sense key.	 Try retries and hope
		 * that will fix the problem.  Otherwise, we're
		 * dead.
		 */
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "Unhandled Sense Key '%s'\n",
		    sense_keys[un->un_status]);
		ST_DO_ERRSTATS(un, st_harderrs);
		severity = SCSI_ERR_FATAL;
		rval = COMMAND_DONE_ERROR;
		goto check_keys;
	}

	if ((!(pkt->pkt_flags & FLAG_SILENT) &&
	    un->un_state >= ST_STATE_OPEN) && (DEBUGGING ||
		(un->un_laststate > ST_STATE_OPENING) &&
		(severity >= st_error_level))) {

		scsi_errmsg(ST_SCSI_DEVP, pkt, st_label, severity,
		    un->un_blkno, un->un_err_blkno, scsi_cmds, sensep);
		if (sensep->es_filmk) {
			scsi_log(ST_DEVINFO, st_label, CE_CONT,
			    "File Mark Detected\n");
		}
		if (sensep->es_eom) {
			scsi_log(ST_DEVINFO, st_label, CE_CONT,
			    "End-of-Media Detected\n");
		}
		if (sensep->es_ili) {
			scsi_log(ST_DEVINFO, st_label, CE_CONT,
			    "Incorrect Length Indicator Set\n");
		}
	}
	get_error = geterror(bp);
	if (((rval == COMMAND_DONE_ERROR) ||
	    (rval == COMMAND_DONE_ERROR_RECOVERED)) &&
	    ((get_error == EIO) || (get_error == 0))) {
		un->un_rqs_state |= (ST_RQS_ERROR | ST_RQS_VALID);
		bcopy(ST_RQSENSE, un->un_uscsi_rqs_buf, SENSE_LENGTH);
		if (un->un_rqs_state & ST_RQS_READ) {
		    un->un_rqs_state &= ~(ST_RQS_READ);
		} else {
		    un->un_rqs_state |= ST_RQS_OVR;
		}
	}

	return (rval);
}


static int
st_handle_intr_retry_lcmd(struct scsi_tape *un, struct buf *bp)
{
	int status = TRAN_ACCEPT;

	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_handle_intr_rtr_lcmd(), un = 0x%p\n", (void *)un);

	/*
	 * Check to see if we hit the retry timeout. We check to make sure
	 * this is the first one on the runq and make sure we have not
	 * queued up any more, so this one has to be the last on the list
	 * also. If it is not, we have to fail.  If it is not the first, but
	 * is the last we are in trouble anyway, as we are in the interrupt
	 * context here.
	 */
	if (((int)un->un_retry_ct > st_retry_count) ||
	    ((un->un_runqf != bp) && (un->un_runql != bp))) {
	    goto exit;
	}

	if (un->un_throttle) {
		un->un_last_throttle = un->un_throttle;
		un->un_throttle = 0;
	}

	/*
	 * Here we know : bp is the first and last one on the runq
	 * it is not necessary to put it back on the head of the
	 * waitq and then move from waitq to runq. Save this queuing
	 * and call scsi_transport.
	 */

	mutex_exit(ST_MUTEX);

	status = scsi_transport(BP_PKT(bp));

	mutex_enter(ST_MUTEX);

	if (status == TRAN_ACCEPT) {
		un->un_tran_retry_ct = 0;
		if (un->un_last_throttle) {
			un->un_throttle = un->un_last_throttle;
		}
		mutex_exit(ST_MUTEX);

		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "restart transport \n");
		return (0);
	}

	ST_DO_KSTATS(bp, kstat_runq_back_to_waitq);
	mutex_exit(ST_MUTEX);

	if (status == TRAN_BUSY) {
	    if (st_handle_intr_busy(un, bp,
		ST_TRAN_BUSY_TIMEOUT) == 0)
		return (0);
	}
	ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		"restart transport rejected\n");
	mutex_enter(ST_MUTEX);
	ST_DO_ERRSTATS(un, st_transerrs);
	if (un->un_last_throttle) {
		un->un_throttle = un->un_last_throttle;
	}
exit:
	mutex_exit(ST_MUTEX);
	return (-1);
}

static int
st_wrongtapetype(struct scsi_tape *un)
{

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_wrongtapetype()\n");

	/*
	 * Hack to handle  600A, 600XTD, 6150 && 660 vs. 300XL tapes...
	 */
	if (un->un_dp && (un->un_dp->options & ST_QIC) && un->un_mspl) {
		switch (un->un_dp->type) {
		case ST_TYPE_WANGTEK:
		case ST_TYPE_ARCHIVE:
			/*
			 * If this really worked, we could go off of
			 * the density codes set in the modesense
			 * page. For this drive, 0x10 == QIC-120,
			 * 0xf == QIC-150, and 0x5 should be for
			 * both QIC-24 and, maybe, QIC-11. However,
			 * the h/w doesn't do what the manual says
			 * that it should, so we'll key off of
			 * getting a WRITE PROTECT error AND wp *not*
			 * set in the mode sense information.
			 */
			/*
			 * XXX but we already know that status is
			 * write protect, so don't check it again.
			 */

			if (un->un_status == KEY_WRITE_PROTECT &&
			    un->un_mspl->wp == 0) {
				return (1);
			}
			break;
		default:
			break;
		}
	}
	return (0);
}

static int
st_check_error(struct scsi_tape *un, struct scsi_pkt *pkt)
{
	int action;

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG, "st_check_error()\n");

	if (SCBP_C(pkt) == STATUS_RESERVATION_CONFLICT) {
		action = COMMAND_DONE_EACCES;
		un->un_rsvd_status |= ST_RESERVATION_CONFLICT;
	} else if (SCBP(pkt)->sts_busy) {
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG, "unit busy\n");
		if ((int)un->un_retry_ct++ < st_retry_count) {
			action = QUE_BUSY_COMMAND;
		} else if ((un->un_rsvd_status &
		    (ST_RESERVE | ST_APPLICATION_RESERVATIONS)) == 0) {
			/*
			 * If this is a command done before reserve is done
			 * don't reset.
			 */
			action = COMMAND_DONE_ERROR;
		} else {
			ST_DEBUG2(ST_DEVINFO, st_label, CE_WARN,
			    "unit busy too long\n");
			mutex_exit(ST_MUTEX);
			if (scsi_reset(ROUTE, RESET_TARGET) == 0) {
				(void) scsi_reset(ROUTE, RESET_ALL);
			}
			mutex_enter(ST_MUTEX);
			action = COMMAND_DONE_ERROR;
		}
	} else if (SCBP(pkt)->sts_chk) {
		/*
		 * we should only get here if the auto rqsense failed
		 * thru a uscsi cmd without autorequest sense
		 * so we just try again
		 */
		action = QUE_SENSE;
	} else {
		action = COMMAND_DONE;
	}
	return (action);
}

static void
st_calc_bnum(struct scsi_tape *un, struct buf *bp)
{
	int n;

	ASSERT(mutex_owned(ST_MUTEX));

	if (un->un_bsize == 0) {
		n = ((bp->b_bcount - bp->b_resid  == 0) ? 0 : 1);
		un->un_kbytes_xferred += (bp->b_bcount - bp->b_resid)/1000;
	} else {
		n = ((bp->b_bcount - bp->b_resid) / un->un_bsize);
	}
	un->un_blkno += n;
}

static void
st_set_state(struct scsi_tape *un)
{
	struct buf *bp = un->un_runqf;
	struct scsi_pkt *sp = BP_PKT(bp);
	struct uscsi_cmd *ucmd;

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_set_state(): un_eof=%x	fmneeded=%x  pkt_resid=0x%lx (%ld)\n",
	    un->un_eof, un->un_fmneeded, sp->pkt_resid, sp->pkt_resid);

	if (bp != un->un_sbufp) {
#ifdef STDEBUG
		if (DEBUGGING && sp->pkt_resid) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "pkt_resid %ld bcount %ld\n",
			    sp->pkt_resid, bp->b_bcount);
		}
#endif
		bp->b_resid = sp->pkt_resid;
		st_calc_bnum(un, bp);
		if (bp->b_flags & B_READ) {
			un->un_lastop = ST_OP_READ;
			un->un_fmneeded = 0;
		} else {
			un->un_lastop = ST_OP_WRITE;
			if (un->un_dp->options & ST_REEL) {
				un->un_fmneeded = 2;
			} else {
				un->un_fmneeded = 1;
			}
		}
		/*
		 * all is honky dory at this point, so let's
		 * readjust the throttle, to increase speed, if we
		 * have not throttled down.
		 */
		if (un->un_throttle)
			un->un_throttle = un->un_max_throttle;
	} else {
		char saved_lastop = un->un_lastop;
		uchar_t cmd = (uchar_t)(intptr_t)bp->b_forw;

		un->un_lastop = ST_OP_CTL;

		switch (cmd) {
		case SCMD_WRITE:
			bp->b_resid = sp->pkt_resid;
			un->un_lastop = ST_OP_WRITE;
			st_calc_bnum(un, bp);
			if (un->un_dp->options & ST_REEL) {
				un->un_fmneeded = 2;
			} else {
				un->un_fmneeded = 1;
			}
			break;
		case SCMD_READ:
			bp->b_resid = sp->pkt_resid;
			un->un_lastop = ST_OP_READ;
			st_calc_bnum(un, bp);
			un->un_fmneeded = 0;
			break;
		case SCMD_WRITE_FILE_MARK:
			if (un->un_eof != ST_EOM)
				un->un_eof = ST_NO_EOF;
			un->un_lastop = ST_OP_WEOF;
			un->un_fileno += (bp->b_bcount - bp->b_resid);
			un->un_blkno = 0;
			if (un->un_dp->options & ST_REEL) {
				un->un_fmneeded -=
					(bp->b_bcount - bp->b_resid);
				if (un->un_fmneeded < 0) {
					un->un_fmneeded = 0;
				}
			} else {
				un->un_fmneeded = 0;
			}

			break;
		case SCMD_REWIND:
			un->un_eof = ST_NO_EOF;
			un->un_fileno = 0;
			un->un_blkno = 0;
			break;

		case SCMD_SPACE:
		{
			int space_fmk, count;
			long resid;

			count = (int)space_cnt(bp->b_bcount);
			resid = (long)space_cnt(bp->b_resid);
			space_fmk = ((bp->b_bcount) & (1<<24)) ? 1 : 0;


			if (count >= 0) {
				if (space_fmk) {
					if (un->un_eof <= ST_EOF) {
						un->un_eof = ST_NO_EOF;
					}
					un->un_fileno += (count - resid);
					un->un_blkno = 0;
				} else {
					un->un_blkno += count - resid;
				}
			} else if (count < 0) {
				if (space_fmk) {
					un->un_fileno -=
					    ((-count) - resid);
					if (un->un_fileno < 0) {
						un->un_fileno = 0;
						un->un_blkno = 0;
					} else {
						un->un_blkno = INF;
					}
				} else {
					if (un->un_eof >= ST_EOF_PENDING) {
					/*
					 * we stepped back into
					 * a previous file; we are not
					 * making an effort to pretend that
					 * we are still in the current file
					 * ie. logical == physical position
					 * and leave it to st_ioctl to correct
					 */
						if (un->un_fileno > 0) {
							un->un_fileno--;
							un->un_blkno = INF;
						} else {
							un->un_blkno = 0;
						}
					} else {
						un->un_blkno -=
						    (-count) - resid;
					}
				}
			}
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "aft_space rs %ld fil %d blk %ld\n",
			    resid, un->un_fileno, un->un_blkno);
			break;
		}
		case SCMD_LOAD:
			if (bp->b_bcount & 0x1) {
				un->un_fileno = 0;
			} else {
				un->un_state = ST_STATE_OFFLINE;
				un->un_fileno = -1;
			}
			un->un_density_known = 0;
			un->un_eof = ST_NO_EOF;
			un->un_blkno = 0;
			break;
		case SCMD_ERASE:
			un->un_eof = ST_NO_EOF;
			un->un_blkno = 0;
			un->un_fileno = 0;
			break;
		case SCMD_RESERVE:
			un->un_rsvd_status |= ST_RESERVE;
			un->un_rsvd_status &=
			    ~(ST_RELEASE | ST_LOST_RESERVE |
			    ST_RESERVATION_CONFLICT);
			un->un_lastop = saved_lastop;
			break;
		case SCMD_RELEASE:
			un->un_rsvd_status |= ST_RELEASE;
			un->un_rsvd_status &=
			    ~(ST_RESERVE | ST_LOST_RESERVE |
			    ST_RESERVATION_CONFLICT);
			un->un_lastop = saved_lastop;
			break;
		case SCMD_PERSISTENT_RESERVE_IN:
			ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
			    "PGR_IN command\n");
			break;
		case SCMD_PERSISTENT_RESERVE_OUT:
			switch (sp->pkt_cdbp[1] & ST_SA_MASK) {
			case ST_SA_SCSI3_RESERVE:
			case ST_SA_SCSI3_PREEMPT:
			case ST_SA_SCSI3_PREEMPTANDABORT:
				un->un_rsvd_status |=
				    ST_APPLICATION_RESERVATIONS;
				ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
				    "PGR Reserve and set: entering"
				    " ST_APPLICATION_RESERVATIONS mode");
				break;
			case ST_SA_SCSI3_RELEASE:
			case ST_SA_SCSI3_CLEAR:
				un->un_rsvd_status &=
				    ~ST_APPLICATION_RESERVATIONS;
				ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
				    "PGR Release and reset: exiting"
				    " ST_APPLICATION_RESERVATIONS mode");
				break;
			}
			break;
		case SCMD_TEST_UNIT_READY:
		case SCMD_READ_BLKLIM:
		case SCMD_REQUEST_SENSE:
		case SCMD_INQUIRY:
		case SCMD_RECOVER_BUF:
		case SCMD_MODE_SELECT:
		case SCMD_MODE_SENSE:
		case SCMD_DOORLOCK:
		case SCMD_READ_POSITION:
		case SCMD_READ_BUFFER:
		case SCMD_REPORT_DENSITIES:
		case SCMD_LOG_SELECT_G1:
		case SCMD_LOG_SENSE_G1:
		case SCMD_REPORT_LUNS:
		case SCMD_READ_ATTRIBUTE:
			un->un_lastop = saved_lastop;
			break;
		case SCMD_LOCATE:	/* Locate makes position unknown */
		default:
			/*
			 * Unknown command, If was USCSI and USCSI_SILENT
			 * flag was not set, set position to unknown.
			 */
			if ((((ucmd = BP_UCMD(bp)) != NULL) &&
			    (ucmd->uscsi_flags & USCSI_SILENT) == 0)) {
				ST_DEBUG2(ST_DEVINFO, st_label, CE_WARN,
				    "unknown cmd 0x%X caused loss of state\n",
				    cmd);
			} else {
				break;
			}
			/* FALLTHROUGH */
		case SCMD_WRITE_BUFFER: /* Writes new firmware to device */
			un->un_fileno = -1;
			break;
		}
	}

	/*
	 * In the st driver we have a logical and physical file position.
	 * Under BSD behavior, when you get a zero read, the logical position
	 * is before the filemark but after the last record of the file.
	 * The physical position is after the filemark. MTIOCGET should always
	 * return the logical file position.
	 *
	 * The next read gives a silent skip to the next file.
	 * Under SVR4, the logical file position remains before the filemark
	 * until the file is closed or a space operation is performed.
	 * Hence set err_resid and err_file before changing fileno if case
	 * BSD Behaviour.
	 */
	un->un_err_resid = bp->b_resid;
	un->un_err_fileno = un->un_fileno;
	un->un_err_blkno = un->un_blkno;
	un->un_retry_ct = 0;


	/*
	 * If we've seen a filemark via the last read operation
	 * advance the file counter, but mark things such that
	 * the next read operation gets a zero count. We have
	 * to put this here to handle the case of sitting right
	 * at the end of a tape file having seen the file mark,
	 * but the tape is closed and then re-opened without
	 * any further i/o. That is, the position information
	 * must be updated before a close.
	 */

	if (un->un_lastop == ST_OP_READ && un->un_eof == ST_EOF_PENDING) {
		/*
		 * If we're a 1/2" tape, and we get a filemark
		 * right on block 0, *AND* we were not in the
		 * first file on the tape, and we've hit logical EOM.
		 * We'll mark the state so that later we do the
		 * right thing (in st_close(), st_strategy() or
		 * st_ioctl()).
		 *
		 */
		if ((un->un_dp->options & ST_REEL) &&
			!(un->un_dp->options & ST_READ_IGNORE_EOFS) &&
		    un->un_blkno == 0 && un->un_fileno > 0) {
			un->un_eof = ST_EOT_PENDING;
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "eot pending\n");
			un->un_fileno++;
			un->un_blkno = 0;
		} else if (BSD_BEHAVIOR) {
			/*
			 * If the read of the filemark was a side effect
			 * of reading some blocks (i.e., data was actually
			 * read), then the EOF mark is pending and the
			 * bump into the next file awaits the next read
			 * operation (which will return a zero count), or
			 * a close or a space operation, else the bump
			 * into the next file occurs now.
			 */
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "resid=%lx, bcount=%lx\n",
				bp->b_resid, bp->b_bcount);
			if (bp->b_resid != bp->b_bcount) {
				un->un_eof = ST_EOF;
			} else {
				un->un_silent_skip = 1;
				un->un_eof = ST_NO_EOF;
				un->un_fileno++;
				un->un_save_blkno = un->un_blkno;
				un->un_blkno = 0;
			}
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "eof of file %d, un_eof=%d\n",
			    un->un_fileno, un->un_eof);
		} else if (SVR4_BEHAVIOR) {
			/*
			 * If the read of the filemark was a side effect
			 * of reading some blocks (i.e., data was actually
			 * read), then the next read should return 0
			 */
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "resid=%lx, bcount=%lx\n",
			    bp->b_resid, bp->b_bcount);
			if (bp->b_resid == bp->b_bcount) {
				un->un_eof = ST_EOF;
			}
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "eof of file=%d, un_eof=%d\n",
			    un->un_fileno, un->un_eof);
		}
	}
}

/*
 * set the correct un_errno, to take corner cases into consideration
 */
static void
st_set_pe_errno(struct scsi_tape *un)
{
	ASSERT(mutex_owned(ST_MUTEX));

	/* if errno is already set, don't reset it */
	if (un->un_errno)
		return;

	/* here un_errno == 0 */
	/*
	 * if the last transfer before flushing all the
	 * waiting I/O's, was 0 (resid = count), then we
	 * want to give the user an error on all the rest,
	 * so here.  If there was a transfer, we set the
	 * resid and counts to 0, and let it drop through,
	 * giving a zero return.  the next I/O will then
	 * give an error.
	 */
	if (un->un_last_resid == un->un_last_count) {
		switch (un->un_eof) {
		case ST_EOM:
			un->un_errno = ENOMEM;
			break;
		case ST_EOT:
		case ST_EOF:
			un->un_errno = EIO;
			break;
		}
	} else {
		/*
		 * we know they did not have a zero, so make
		 * sure they get one
		 */
		un->un_last_resid = un->un_last_count = 0;
	}
}


/*
 * send in a marker pkt to terminate flushing of commands by BBA (via
 * flush-on-errors) property.  The HBA will always return TRAN_ACCEPT
 */
static void
st_hba_unflush(struct scsi_tape *un)
{
	ASSERT(mutex_owned(ST_MUTEX));

	if (!un->un_flush_on_errors)
		return;

#ifdef FLUSH_ON_ERRORS

	if (!un->un_mkr_pkt) {
		un->un_mkr_pkt = scsi_init_pkt(ROUTE, NULL, (struct buf *)NULL,
		    NULL, 0, 0, 0, SLEEP_FUNC, NULL);

		/* we slept, so it must be there */
		pkt->pkt_flags |= FLAG_FLUSH_MARKER;
	}

	mutex_exit(ST_MUTEX);
	scsi_transport(un->un_mkr_pkt);
	mutex_enter(ST_MUTEX);
#endif
}

static void
st_clean_print(dev_info_t *dev, char *label, uint_t level,
	char *title, char *data, int len)
{
	int	i;
	char	buf[256];

	(void) sprintf(buf, "%s: ", title);
	for (i = 0; i < len; i++) {
		(void) sprintf(&buf[(int)strlen(buf)], "0x%x ",
			(data[i] & 0xff));
	}
	(void) sprintf(&buf[(int)strlen(buf)], "\n");

	scsi_log(dev, label, level, "%s", buf);
}

/*
 * Conditionally enabled debugging
 */
#ifdef	STDEBUG
static void
st_debug_cmds(struct scsi_tape *un, int com, int count, int wait)
{
	char tmpbuf[64];

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "cmd=%s count=0x%x (%d)	 %ssync\n",
	    scsi_cmd_name(com, scsi_cmds, tmpbuf),
	    count, count,
	    wait == ASYNC_CMD ? "a" : "");
}

/*
 * Returns pointer to name of minor node name of device 'dev'.
 */
static char *
st_dev_name(dev_t dev)
{
	const char density[] = { 'l', 'm', 'h', 'c' };
	static char name[4];
	minor_t minor;
	int nprt = 0;

	minor = getminor(dev);
	name[nprt] = density[(minor & MT_DENSITY_MASK) >> 3];

	if (minor & MT_BSD) {
		name[++nprt] = 'b';
	}

	if (minor & MT_NOREWIND) {
		name[++nprt] = 'n';
	}

	/* NULL terminator */
	name[++nprt] = 0;

	return (name);
}
#endif	/* STDEBUG */

/*
 * Soft error reporting, so far unique to each drive
 *
 * Currently supported: exabyte and DAT soft error reporting
 */
static int
st_report_exabyte_soft_errors(dev_t dev, int flag)
{
	uchar_t *sensep;
	int amt;
	int rval = 0;
	char cdb[CDB_GROUP0], *c = cdb;
	struct uscsi_cmd *com;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_report_exabyte_soft_errors(dev = 0x%lx, flag = %d)\n",
	    dev, flag);

	ASSERT(mutex_owned(ST_MUTEX));

	com = kmem_zalloc(sizeof (*com), KM_SLEEP);
	sensep = kmem_zalloc(TAPE_SENSE_LENGTH, KM_SLEEP);

	*c++ = SCMD_REQUEST_SENSE;
	*c++ = 0;
	*c++ = 0;
	*c++ = 0;
	*c++ = TAPE_SENSE_LENGTH;
	/*
	 * set CLRCNT (byte 5, bit 7 which clears the error counts)
	 */
	*c   = (char)0x80;

	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP0;
	com->uscsi_bufaddr = (caddr_t)sensep;
	com->uscsi_buflen = TAPE_SENSE_LENGTH;
	com->uscsi_flags = USCSI_DIAGNOSE | USCSI_SILENT
		| USCSI_READ | USCSI_RQENABLE;
	com->uscsi_timeout = un->un_dp->non_motion_timeout;

	rval = st_ioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	if (rval || com->uscsi_status) {
		goto done;
	}

	/*
	 * was there enough data?
	 */
	amt = (int)TAPE_SENSE_LENGTH - com->uscsi_resid;

	if ((amt >= 19) && un->un_kbytes_xferred) {
		uint_t count, error_rate;
		uint_t rate;

		if (sensep[21] & CLN) {
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			    "Periodic head cleaning required");
		}
		if (un->un_kbytes_xferred < (EXABYTE_MIN_TRANSFER/1000))
			goto done;
		/*
		 * check if soft error reporting needs to be done.
		 */
		count = sensep[16] << 16 | sensep[17] << 8 | sensep[18];
		count &= 0xffffff;
		error_rate = (count * 100)/un->un_kbytes_xferred;

#ifdef	STDEBUG
		if (st_soft_error_report_debug) {
			scsi_log(ST_DEVINFO, st_label, CE_NOTE,
			    "Exabyte Soft Error Report:\n");
			scsi_log(ST_DEVINFO, st_label, CE_CONT,
			    "read/write error counter: %d\n", count);
			scsi_log(ST_DEVINFO, st_label, CE_CONT,
			    "number of bytes transferred: %dK\n",
				un->un_kbytes_xferred);
			scsi_log(ST_DEVINFO, st_label, CE_CONT,
			    "error_rate: %d%%\n", error_rate);

			if (amt >= 22) {
				scsi_log(ST_DEVINFO, st_label, CE_CONT,
				    "unit sense: 0x%b 0x%b 0x%b\n",
				    sensep[19], SENSE_19_BITS,
				    sensep[20], SENSE_20_BITS,
				    sensep[21], SENSE_21_BITS);
			}
			if (amt >= 27) {
				scsi_log(ST_DEVINFO, st_label, CE_CONT,
				    "tracking retry counter: %d\n",
				    sensep[26]);
				scsi_log(ST_DEVINFO, st_label, CE_CONT,
				    "read/write retry counter: %d\n",
				    sensep[27]);
			}
		}
#endif

		if (flag & FWRITE) {
			rate = EXABYTE_WRITE_ERROR_THRESHOLD;
		} else {
			rate = EXABYTE_READ_ERROR_THRESHOLD;
		}
		if (error_rate >= rate) {
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			    "Soft error rate (%d%%) during %s was too high",
			    error_rate,
			    ((flag & FWRITE) ? wrg_str : rdg_str));
			scsi_log(ST_DEVINFO, st_label, CE_CONT,
			    "Please, replace tape cartridge\n");
		}
	}

done:
	kmem_free(com, sizeof (*com));
	kmem_free(sensep, TAPE_SENSE_LENGTH);

	if (rval != 0) {
		scsi_log(ST_DEVINFO, st_label, CE_WARN,
		    "exabyte soft error reporting failed\n");
	}
	return (rval);
}

/*
 * this is very specific to Archive 4mm dat
 */
#define	ONEGIG	(1024 * 1024 * 1024)

static int
st_report_dat_soft_errors(dev_t dev, int flag)
{
	uchar_t *sensep;
	int amt, i;
	int rval = 0;
	char cdb[CDB_GROUP1], *c = cdb;
	struct uscsi_cmd *com;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_report_dat_soft_errors(dev = 0x%lx, flag = %d)\n", dev, flag);

	ASSERT(mutex_owned(ST_MUTEX));

	com = kmem_zalloc(sizeof (*com), KM_SLEEP);
	sensep = kmem_zalloc(LOG_SENSE_LENGTH, KM_SLEEP);

	*c++ = SCMD_LOG_SENSE_G1;
	*c++ = 0;
	*c++ = (flag & FWRITE) ? 0x42 : 0x43;
	*c++ = 0;
	*c++ = 0;
	*c++ = 0;
	*c++ = 2;
	*c++ = 0;
	*c++ = (char)LOG_SENSE_LENGTH;
	*c   = 0;
	com->uscsi_cdb    = cdb;
	com->uscsi_cdblen  = CDB_GROUP1;
	com->uscsi_bufaddr = (caddr_t)sensep;
	com->uscsi_buflen  = LOG_SENSE_LENGTH;
	com->uscsi_flags   =
	    USCSI_DIAGNOSE | USCSI_SILENT | USCSI_READ | USCSI_RQENABLE;
	com->uscsi_timeout = un->un_dp->non_motion_timeout;
	rval =
	    st_ioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE, UIO_SYSSPACE);
	if (rval) {
		scsi_log(ST_DEVINFO, st_label, CE_WARN,
		    "DAT soft error reporting failed\n");
	}
	if (rval || com->uscsi_status) {
		goto done;
	}

	/*
	 * was there enough data?
	 */
	amt = (int)LOG_SENSE_LENGTH - com->uscsi_resid;

	if ((amt >= MIN_LOG_SENSE_LENGTH) && un->un_kbytes_xferred) {
		int total, retries, param_code;

		total = -1;
		retries = -1;
		amt = sensep[3] + 4;


#ifdef STDEBUG
		if (st_soft_error_report_debug) {
			(void) printf("logsense:");
			for (i = 0; i < MIN_LOG_SENSE_LENGTH; i++) {
				if (i % 16 == 0) {
					(void) printf("\t\n");
				}
				(void) printf(" %x", sensep[i]);
			}
			(void) printf("\n");
		}
#endif

		/*
		 * parse the param_codes
		 */
		if (sensep[0] == 2 || sensep[0] == 3) {
			for (i = 4; i < amt; i++) {
				param_code = (sensep[i++] << 8);
				param_code += sensep[i++];
				i++; /* skip control byte */
				if (param_code == 5) {
					if (sensep[i++] == 4) {
						total = (sensep[i++] << 24);
						total += (sensep[i++] << 16);
						total += (sensep[i++] << 8);
						total += sensep[i];
					}
				} else if (param_code == 0x8007) {
					if (sensep[i++] == 2) {
						retries = sensep[i++] << 8;
						retries += sensep[i];
					}
				} else {
					i += sensep[i];
				}
			}
		}

		/*
		 * if the log sense returned valid numbers then determine
		 * the read and write error thresholds based on the amount of
		 * data transferred
		 */

		if (total > 0 && retries > 0) {
			short normal_retries = 0;
			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			"total xferred (%s) =%x, retries=%x\n",
				((flag & FWRITE) ? wrg_str : rdg_str),
				total, retries);

			if (flag & FWRITE) {
				if (total <=
					WRITE_SOFT_ERROR_WARNING_THRESHOLD) {
					normal_retries =
						DAT_SMALL_WRITE_ERROR_THRESHOLD;
				} else {
					normal_retries =
						DAT_LARGE_WRITE_ERROR_THRESHOLD;
				}
			} else {
				if (total <=
					READ_SOFT_ERROR_WARNING_THRESHOLD) {
					normal_retries =
						DAT_SMALL_READ_ERROR_THRESHOLD;
				} else {
					normal_retries =
						DAT_LARGE_READ_ERROR_THRESHOLD;
				}
			}

			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			"normal retries=%d\n", normal_retries);

			if (retries >= normal_retries) {
				scsi_log(ST_DEVINFO, st_label, CE_WARN,
				    "Soft error rate (retries = %d) during "
				    "%s was too high",  retries,
				    ((flag & FWRITE) ? wrg_str : rdg_str));
				scsi_log(ST_DEVINFO, st_label, CE_CONT,
				    "Periodic head cleaning required "
				    "and/or replace tape cartridge\n");
			}

		} else if (total == -1 || retries == -1) {
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			    "log sense parameter code does not make sense\n");
		}
	}

	/*
	 * reset all values
	 */
	c = cdb;
	*c++ = SCMD_LOG_SELECT_G1;
	*c++ = 2;	/* this resets all values */
	*c++ = (char)0xc0;
	*c++ = 0;
	*c++ = 0;
	*c++ = 0;
	*c++ = 0;
	*c++ = 0;
	*c++ = 0;
	*c   = 0;
	com->uscsi_bufaddr = NULL;
	com->uscsi_buflen  = 0;
	com->uscsi_flags   = USCSI_DIAGNOSE | USCSI_SILENT | USCSI_RQENABLE;
	rval =
	    st_ioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE, UIO_SYSSPACE);
	if (rval) {
		scsi_log(ST_DEVINFO, st_label, CE_WARN,
		    "DAT soft error reset failed\n");
	}
done:
	kmem_free(com, sizeof (*com));
	kmem_free(sensep, LOG_SENSE_LENGTH);
	return (rval);
}

static int
st_report_soft_errors(dev_t dev, int flag)
{
	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_report_soft_errors(dev = 0x%lx, flag = %d)\n", dev, flag);

	ASSERT(mutex_owned(ST_MUTEX));

	switch (un->un_dp->type) {
	case ST_TYPE_EXB8500:
	case ST_TYPE_EXABYTE:
		return (st_report_exabyte_soft_errors(dev, flag));
		/*NOTREACHED*/
	case ST_TYPE_PYTHON:
		return (st_report_dat_soft_errors(dev, flag));
		/*NOTREACHED*/
	default:
		un->un_dp->options &= ~ST_SOFT_ERROR_REPORTING;
		return (-1);
	}
}

/*
 * persistent error routines
 */

/*
 * enable persistent errors, and set the throttle appropriately, checking
 * for flush-on-errors capability
 */
static void
st_turn_pe_on(struct scsi_tape *un)
{
	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG, "st_pe_on\n");
	ASSERT(mutex_owned(ST_MUTEX));

	un->un_persistence = 1;

	/*
	 * only use flush-on-errors if auto-request-sense and untagged-qing are
	 * enabled.  This will simplify the error handling for request senses
	 */

	if (un->un_arq_enabled && un->un_untagged_qing) {
		uchar_t f_o_e;

		mutex_exit(ST_MUTEX);
		f_o_e = (scsi_ifsetcap(ROUTE, "flush-on-errors", 1, 1) == 1) ?
		    1 : 0;
		mutex_enter(ST_MUTEX);

		un->un_flush_on_errors = f_o_e;
	} else {
		un->un_flush_on_errors = 0;
	}

	if (un->un_flush_on_errors)
		un->un_max_throttle = (uchar_t)st_max_throttle;
	else
		un->un_max_throttle = 1;

	if (un->un_dp->options & ST_RETRY_ON_RECOVERED_DEFERRED_ERROR)
		un->un_max_throttle = 1;

	/* this will send a marker pkt */
	CLEAR_PE(un);
}

/*
 * This turns persistent errors permanently off
 */
static void
st_turn_pe_off(struct scsi_tape *un)
{
	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG, "st_pe_off\n");
	ASSERT(mutex_owned(ST_MUTEX));

	/* turn it off for good */
	un->un_persistence = 0;

	/* this will send a marker pkt */
	CLEAR_PE(un);

	/* turn off flush on error capability, if enabled */
	if (un->un_flush_on_errors) {
		mutex_exit(ST_MUTEX);
		(void) scsi_ifsetcap(ROUTE, "flush-on-errors", 0, 1);
		mutex_enter(ST_MUTEX);
	}


	un->un_flush_on_errors = 0;
}

/*
 * This clear persistent errors, allowing more commands through, and also
 * sending a marker packet.
 */
static void
st_clear_pe(struct scsi_tape *un)
{
	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG, "st_pe_clear\n");
	ASSERT(mutex_owned(ST_MUTEX));

	un->un_persist_errors = 0;
	un->un_throttle = un->un_last_throttle = 1;
	un->un_errno = 0;
	st_hba_unflush(un);
}

/*
 * This will flag persistent errors, shutting everything down, if the
 * application had enabled persistent errors via MTIOCPERSISTENT
 */
static void
st_set_pe_flag(struct scsi_tape *un)
{
	ASSERT(mutex_owned(ST_MUTEX));

	if (un->un_persistence) {
		ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG, "st_pe_flag\n");
		un->un_persist_errors = 1;
		un->un_throttle = un->un_last_throttle = 0;
	}
}

/*
 * List of commands that are allowed to be done while another host holds
 * the reservation.
 */
struct {
	uchar_t cmd;
	uchar_t byte;	/* byte to look for data */
	uint32_t mask;	/* bits that matter in the above data */
} rcmds[] = {
	{ SCMD_TEST_UNIT_READY, 0, 0 }, /* may fail on older drives */
	{ SCMD_REQUEST_SENSE, 0, 0 },
	{ SCMD_READ_BLKLIM, 0, 0 },
	{ SCMD_INQUIRY, 0, 0 },
	{ SCMD_RESERVE, 0, 0 },
	{ SCMD_RELEASE, 0, 0 },
	{ SCMD_DOORLOCK, 4, 3 },	/* allow (unlock) media access only */
	{ SCMD_REPORT_DENSITIES, 0, 0 },
	{ SCMD_LOG_SENSE_G1, 0, 0 },
	{ SCMD_PERSISTENT_RESERVE_IN, 0, 0 },
	{ SCMD_PERSISTENT_RESERVE_OUT, 0, 0 },
	{ SCMD_REPORT_LUNS, 0, 0 }
};

static int
st_do_reserve(struct scsi_tape *un)
{
	int rval;

	/*
	 * Issue a Throw-Away reserve command to clear the
	 * check condition.
	 * If the current behaviour of reserve/release is to
	 * hold reservation across opens , and if a Bus reset
	 * has been issued between opens then this command
	 * would set the ST_LOST_RESERVE flags in rsvd_status.
	 * In this case return an EACCES so that user knows that
	 * reservation has been lost in between opens.
	 * If this error is not returned and we continue with
	 * successful open , then user may think position of the
	 * tape is still the same but inreality we would rewind the
	 * tape and continue from BOT.
	 */
	rval = st_reserve_release(un, ST_RESERVE);
	if (rval) {
		if ((un->un_rsvd_status & ST_LOST_RESERVE_BETWEEN_OPENS) ==
		    ST_LOST_RESERVE_BETWEEN_OPENS) {
			un->un_rsvd_status &= ~(ST_LOST_RESERVE | ST_RESERVE);
			un->un_errno = EACCES;
			return (EACCES);
		}
		rval = st_reserve_release(un, ST_RESERVE);
	}
	if (rval == 0) {
		un->un_rsvd_status |= ST_INIT_RESERVE;
	}

	return (rval);
}

static int
st_check_cdb_for_need_to_reserve(struct scsi_tape *un, caddr_t cdb)
{
	int i;
	int rval = 0;

	/*
	 * If already reserved no need to do it again.
	 * Also if Reserve and Release are disabled Just return.
	 */
	if ((un->un_rsvd_status & (ST_RESERVE | ST_APPLICATION_RESERVATIONS)) ||
	    (un->un_dp->options & ST_NO_RESERVE_RELEASE)) {
		ST_DEBUG6(ST_DEVINFO, st_label, CE_NOTE,
		    "st_check_cdb_for_need_to_reserve() reserve unneeded 0x%x",
		    cdb[0]);
		return (0);
	}

	/* See if command is on the list */
	for (i = 0; i < ST_NUM_MEMBERS(rcmds); i++) {
		if ((uchar_t)cdb[0] == rcmds[i].cmd) {
			/*
			 * cmd is on list.
			 * if byte is zero always allowed.
			 */
			if (rcmds[i].byte == 0) {
				return (rval);
			}
			if (((cdb[rcmds[i].byte]) & (rcmds[i].mask)) == 0) {
				return (rval);
			}
			break;
		}
	}

	ST_DEBUG6(ST_DEVINFO, st_label, CE_NOTE,
	    "Command 0x%x requires reservation", cdb[0]);

	rval = st_do_reserve(un);

	return (rval);
}

static int
st_check_cmd_for_need_to_reserve(struct scsi_tape *un, uchar_t cmd, int cnt)
{
	int i;
	int rval = 0;

	if ((un->un_rsvd_status & (ST_RESERVE | ST_APPLICATION_RESERVATIONS)) ||
	    (un->un_dp->options & ST_NO_RESERVE_RELEASE)) {
		ST_DEBUG6(ST_DEVINFO, st_label, CE_NOTE,
		    "st_check_cmd_for_need_to_reserve() reserve unneeded 0x%x",
		    cmd);
		return (0);
	}

	/* See if command is on the list */
	for (i = 0; i < ST_NUM_MEMBERS(rcmds); i++) {
		if (cmd == rcmds[i].cmd) {
			/*
			 * cmd is on list.
			 * if byte is zero always allowed.
			 */
			if (rcmds[i].byte == 0) {
				return (rval);
			}
			if (((rcmds[i].mask) & cnt) == 0) {
				return (rval);
			}
			break;
		}
	}

	ST_DEBUG6(ST_DEVINFO, st_label, CE_NOTE,
	    "Cmd 0x%x requires reservation", cmd);

	rval = st_do_reserve(un);

	return (rval);
}

static int
st_reserve_release(struct scsi_tape *un, int cmd)
{
	struct uscsi_cmd	uscsi_cmd;
	struct uscsi_cmd	*com = &uscsi_cmd;
	int			rval;
	char			cdb[CDB_GROUP0];


	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_reserve_release: %s \n",
	    (cmd == ST_RELEASE)?  "Releasing":"Reserving");

	bzero(cdb, CDB_GROUP0);
	if (cmd == ST_RELEASE) {
		cdb[0] = SCMD_RELEASE;
	} else {
		cdb[0] = SCMD_RESERVE;
	}
	bzero(com, sizeof (struct uscsi_cmd));
	com->uscsi_flags = USCSI_WRITE;
	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP0;
	com->uscsi_timeout = un->un_dp->non_motion_timeout;

	rval = st_ioctl_cmd(un->un_dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
	    UIO_SYSSPACE);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_reserve_release: rval(1)=%d\n", rval);

	if (rval) {
		if (com->uscsi_status == STATUS_RESERVATION_CONFLICT) {
			rval = EACCES;
		}
		/*
		 * dynamically turn off reserve/release support
		 * in case of drives which do not support
		 * reserve/release command(ATAPI drives).
		 */
		if (un->un_status == KEY_ILLEGAL_REQUEST) {
			if (un->un_dp->options & ST_NO_RESERVE_RELEASE) {
				un->un_dp->options |= ST_NO_RESERVE_RELEASE;
				ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "Tape unit does not support "
				    "reserve/release \n");
			}
			rval = 0;
		}
	}
	return (rval);
}

static int
st_take_ownership(dev_t dev)
{
	int rval;

	GET_SOFT_STATE(dev);
	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_take_ownership: Entering ...\n");


	rval = st_reserve_release(un, ST_RESERVE);
	/*
	 * XXX -> Should reset be done only if we get EACCES.
	 * .
	 */
	if (rval) {
		mutex_exit(ST_MUTEX);
		if (scsi_reset(ROUTE, RESET_TARGET) == 0) {
			if (scsi_reset(ROUTE, RESET_ALL) == 0) {
				mutex_enter(ST_MUTEX);
				return (EIO);
			}
		}
		mutex_enter(ST_MUTEX);
		un->un_rsvd_status &=
			~(ST_LOST_RESERVE | ST_RESERVATION_CONFLICT);

		mutex_exit(ST_MUTEX);
		delay(drv_usectohz(ST_RESERVATION_DELAY));
		mutex_enter(ST_MUTEX);
		/*
		 * remove the check condition.
		 */
		(void) st_reserve_release(un, ST_RESERVE);
		if ((rval = st_reserve_release(un, ST_RESERVE)) != 0) {
			if ((st_reserve_release(un, ST_RESERVE)) != 0) {
				rval = (un->un_rsvd_status &
					ST_RESERVATION_CONFLICT) ? EACCES : EIO;
				return (rval);
			}
		}
		/*
		 * Set tape state to ST_STATE_OFFLINE , in case if
		 * the user wants to continue and start using
		 * the tape.
		 */
		un->un_state = ST_STATE_OFFLINE;
		un->un_rsvd_status |= ST_INIT_RESERVE;
	}
	return (rval);
}

static int
st_create_errstats(struct scsi_tape *un, int instance)
{
	char	kstatname[KSTAT_STRLEN];

	/*
	 * Create device error kstats
	 */

	if (un->un_errstats == (kstat_t *)0) {
		(void) sprintf(kstatname, "st%d,err", instance);
		un->un_errstats = kstat_create("sterr", instance, kstatname,
			"device_error", KSTAT_TYPE_NAMED,
			sizeof (struct st_errstats) / sizeof (kstat_named_t),
			KSTAT_FLAG_PERSISTENT);

		if (un->un_errstats) {
			struct st_errstats	*stp;

			stp = (struct st_errstats *)un->un_errstats->ks_data;
			kstat_named_init(&stp->st_softerrs, "Soft Errors",
				KSTAT_DATA_ULONG);
			kstat_named_init(&stp->st_harderrs, "Hard Errors",
				KSTAT_DATA_ULONG);
			kstat_named_init(&stp->st_transerrs, "Transport Errors",
				KSTAT_DATA_ULONG);
			kstat_named_init(&stp->st_vid, "Vendor",
				KSTAT_DATA_CHAR);
			kstat_named_init(&stp->st_pid, "Product",
				KSTAT_DATA_CHAR);
			kstat_named_init(&stp->st_revision, "Revision",
				KSTAT_DATA_CHAR);
			kstat_named_init(&stp->st_serial, "Serial No",
				KSTAT_DATA_CHAR);
			un->un_errstats->ks_private = un;
			un->un_errstats->ks_update = nulldev;
			kstat_install(un->un_errstats);
			/*
			 * Fill in the static data
			 */
			(void) strncpy(&stp->st_vid.value.c[0],
					ST_INQUIRY->inq_vid, 8);
			/*
			 * XXX:  Emulex MT-02 (and emulators) predates
			 *	 SCSI-1 and has no vid & pid inquiry data.
			 */
			if (ST_INQUIRY->inq_len != 0) {
				(void) strncpy(&stp->st_pid.value.c[0],
					ST_INQUIRY->inq_pid, 16);
				(void) strncpy(&stp->st_revision.value.c[0],
					ST_INQUIRY->inq_revision, 4);
				(void) strncpy(&stp->st_serial.value.c[0],
					ST_INQUIRY->inq_serial, 12);
			}
		}
	}
	return (0);
}

static int
st_validate_tapemarks(struct scsi_tape *un, int fileno, daddr_t blkno)
{
	dev_t dev;
	int rval;

	ASSERT(MUTEX_HELD(&un->un_sd->sd_mutex));
	ASSERT(mutex_owned(ST_MUTEX));

	dev = un->un_dev;

	scsi_log(ST_DEVINFO, st_label, CE_NOTE, "Restoring tape"
	" position at fileno=%x, blkno=%lx....", fileno, blkno);

	/*
	 * Rewind ? Oh yeah, Fidelity has got the STK F/W changed
	 * so as not to rewind tape on RESETS: Gee, Has life ever
	 * been simple in tape land ?
	 */
	rval = st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD);
	if (rval) {
		scsi_log(ST_DEVINFO, st_label, CE_WARN,
		"Failed to restore the last file and block position: In"
		" this state, Tape will be loaded at BOT during next open");
		un->un_fileno = -1;
		return (rval);
	}

	if (fileno) {
		rval = st_cmd(dev, SCMD_SPACE, Fmk(fileno), SYNC_CMD);
		if (rval) {
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			"Failed to restore the last file position: In this "
			" state, Tape will be loaded at BOT during next open");
			un->un_fileno = -1;
			return (rval);
		}
	}

	if (blkno) {
		rval = st_cmd(dev, SCMD_SPACE, Blk(blkno), SYNC_CMD);
		if (rval) {
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			"Failed to restore the last block position: In this"
			" state, tape will be loaded at BOT during next open");
			un->un_fileno = -1;
			return (rval);
		}
	}

	return (0);
}

/*
 * check sense key, ASC, ASCQ in order to determine if the tape needs
 * to be ejected
 */

static int
st_check_asc_ascq(struct scsi_tape *un)
{
	struct scsi_extended_sense *sensep = ST_RQSENSE;
	struct tape_failure_code   *code;

	for (code = st_tape_failure_code; code->key != 0xff; code++) {
		if ((code->key  == sensep->es_key) &&
		    (code->add_code  == sensep->es_add_code) &&
		    (code->qual_code == sensep->es_qual_code))
			return (1);
	}
	return (0);
}

/*
 * st_logpage_supported() sends a Log Sense command with
 * page code = 0 = Supported Log Pages Page to the device,
 * to see whether the page 'page' is supported.
 * Return values are:
 * -1 if the Log Sense command fails
 * 0 if page is not supported
 * 1 if page is supported
 */

static int
st_logpage_supported(dev_t dev, uchar_t page)
{
	uchar_t *sp, *sensep;
	unsigned length;
	struct uscsi_cmd *com;
	int rval;
	char cdb[CDB_GROUP1] = {
		SCMD_LOG_SENSE_G1,
		0,
		SUPPORTED_LOG_PAGES_PAGE,
		0,
		0,
		0,
		0,
		0,
		(char)LOG_SENSE_LENGTH,
		0
	};

	GET_SOFT_STATE(dev);
	ASSERT(mutex_owned(ST_MUTEX));

	com = kmem_zalloc(sizeof (struct uscsi_cmd), KM_SLEEP);
	sensep = kmem_zalloc(LOG_SENSE_LENGTH, KM_SLEEP);

	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP1;
	com->uscsi_bufaddr = (caddr_t)sensep;
	com->uscsi_buflen = LOG_SENSE_LENGTH;
	com->uscsi_flags =
		USCSI_DIAGNOSE | USCSI_SILENT | USCSI_READ | USCSI_RQENABLE;
	com->uscsi_timeout = un->un_dp->non_motion_timeout;
	rval = st_ioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE, UIO_SYSSPACE);
	if (rval || com->uscsi_status) {
		/* uscsi-command failed */
		rval = -1;
	} else {

		sp = sensep + 3;

		for (length = *sp++; length > 0; length--, sp++) {

			if (*sp == page) {
				rval = 1;
				break;
			}
		}
	}
	kmem_free(com, sizeof (struct uscsi_cmd));
	kmem_free(sensep, LOG_SENSE_LENGTH);
	return (rval);
}


/*
 * st_check_clean_bit() gets the status of the tape's cleaning bit.
 *
 * If the device does support the TapeAlert log page, then the cleaning bit
 * information will be read from this page. Otherwise we will see if one of
 * ST_CLN_TYPE_1, ST_CLN_TYPE_2 or ST_CLN_TYPE_3 is set in the properties of
 * the device, which means, that we can get the cleaning bit information via
 * a RequestSense command.
 * If both methods of getting cleaning bit information are not supported
 * st_check_clean_bit() will return with 0. Otherwise st_check_clean_bit()
 * returns with
 * - MTF_TAPE_CLN_SUPPORTED if cleaning bit is not set or
 * - MTF_TAPE_CLN_SUPPORTED | MTF_TAPE_HEAD_DIRTY if cleaning bit is set.
 * If the call to st_ioctl_cmd() to do the Log Sense or the Request Sense
 * command fails, or if the amount of Request Sense data is not enough, then
 *  st_check_clean_bit() returns with -1.
 */

static int
st_check_clean_bit(dev_t dev)
{
	int rval = 0;

	GET_SOFT_STATE(dev);

	ASSERT(mutex_owned(ST_MUTEX));

	if (un->un_HeadClean & TAPE_ALERT_NOT_SUPPORTED) {
		return (rval);
	}

	if (un->un_HeadClean == TAPE_ALERT_SUPPORT_UNKNOWN) {

		rval = st_logpage_supported(dev, TAPE_SEQUENTIAL_PAGE);
		if (rval == 1) {

			un->un_HeadClean |= TAPE_SEQUENTIAL_SUPPORTED;
		}

		rval = st_logpage_supported(dev, TAPE_ALERT_PAGE);
		if (rval == 1) {

			un->un_HeadClean |= TAPE_ALERT_SUPPORTED;
		}

		if (un->un_HeadClean == TAPE_ALERT_SUPPORT_UNKNOWN) {

			un->un_HeadClean = TAPE_ALERT_NOT_SUPPORTED;
		}
	}

	rval = 0;

	if (un->un_HeadClean & TAPE_SEQUENTIAL_SUPPORTED) {

		rval = st_check_sequential_clean_bit(dev);
	}

	if ((rval <= 0) && (un->un_HeadClean & TAPE_ALERT_SUPPORTED)) {

		rval = st_check_alert_flags(dev);
	}

	if ((rval <= 0) && (un->un_dp->options & ST_CLN_MASK)) {

		rval = st_check_sense_clean_bit(dev);
	}

	if (rval < 0) {
		return (rval);
	}

	/*
	 * If found a supported means to check need to clean.
	 */
	if (rval & MTF_TAPE_CLN_SUPPORTED) {

		/*
		 * head needs to be cleaned.
		 */
		if (rval & MTF_TAPE_HEAD_DIRTY) {

			/*
			 * Print log message only first time
			 * found needing cleaned.
			 */
			if ((un->un_HeadClean & TAPE_PREVIOUSLY_DIRTY) == 0) {

				scsi_log(ST_DEVINFO, st_label, CE_WARN,
				    "Periodic head cleaning required");

				un->un_HeadClean |= TAPE_PREVIOUSLY_DIRTY;
			}

		} else {

			un->un_HeadClean &= ~TAPE_PREVIOUSLY_DIRTY;
		}
	}

	return (rval);
}


static int
st_check_sequential_clean_bit(dev_t dev)
{
	int rval;
	int ix;
	ushort_t parameter;
	struct uscsi_cmd *cmd;
	struct log_sequential_page *sp;
	struct log_sequential_page_parameter *prm;
	char cdb[CDB_GROUP1] = {
		SCMD_LOG_SENSE_G1,
		0,
		TAPE_SEQUENTIAL_PAGE | CURRENT_CUMULATIVE_VALUES,
		0,
		0,
		0,
		0,
		(char)(sizeof (struct log_sequential_page) >> 8),
		(char)(sizeof (struct log_sequential_page)),
		0
	};

	GET_SOFT_STATE(dev);

	cmd = kmem_zalloc(sizeof (struct uscsi_cmd), KM_SLEEP);
	sp  = kmem_zalloc(sizeof (struct log_sequential_page), KM_SLEEP);

	cmd->uscsi_flags   =
		USCSI_DIAGNOSE | USCSI_SILENT | USCSI_READ | USCSI_RQENABLE;
	cmd->uscsi_timeout = un->un_dp->non_motion_timeout;
	cmd->uscsi_cdb	   = cdb;
	cmd->uscsi_cdblen  = CDB_GROUP1;
	cmd->uscsi_bufaddr = (caddr_t)sp;
	cmd->uscsi_buflen  = sizeof (struct log_sequential_page);

	rval = st_ioctl_cmd(dev, cmd, UIO_SYSSPACE, UIO_SYSSPACE, UIO_SYSSPACE);

	if (rval || cmd->uscsi_status || cmd->uscsi_resid) {

		rval = -1;

	} else if (sp->log_page.code != TAPE_SEQUENTIAL_PAGE) {

		rval = -1;
	}

	prm = &sp->param[0];

	for (ix = 0; rval == 0 && ix < TAPE_SEQUENTIAL_PAGE_PARA; ix++) {

		if (prm->log_param.length == 0) {
			break;
		}

		parameter = (((prm->log_param.pc_hi << 8) & 0xff00) +
			(prm->log_param.pc_lo & 0xff));

		if (parameter == SEQUENTIAL_NEED_CLN) {

			rval = MTF_TAPE_CLN_SUPPORTED;
			if (prm->param_value[prm->log_param.length - 1]) {

				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
					    "sequential log says head dirty\n");
				rval |= MTF_TAPE_HEAD_DIRTY;
			}
		}
		prm = (struct log_sequential_page_parameter *)
			&prm->param_value[prm->log_param.length];
	}

	kmem_free(cmd, sizeof (struct uscsi_cmd));
	kmem_free(sp,  sizeof (struct log_sequential_page));

	return (rval);
}


static int
st_check_alert_flags(dev_t dev)
{
	struct st_tape_alert *ta;
	struct uscsi_cmd *com;
	unsigned ix, length;
	int rval;
	tape_alert_flags flag;
	char cdb[CDB_GROUP1] = {
		SCMD_LOG_SENSE_G1,
		0,
		TAPE_ALERT_PAGE | CURRENT_THRESHOLD_VALUES,
		0,
		0,
		0,
		0,
		(char)(sizeof (struct st_tape_alert) >> 8),
		(char)(sizeof (struct st_tape_alert)),
		0
	};

	GET_SOFT_STATE(dev);

	com = kmem_zalloc(sizeof (struct uscsi_cmd), KM_SLEEP);
	ta  = kmem_zalloc(sizeof (struct st_tape_alert), KM_SLEEP);

	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP1;
	com->uscsi_bufaddr = (caddr_t)ta;
	com->uscsi_buflen = sizeof (struct st_tape_alert);
	com->uscsi_flags =
		USCSI_DIAGNOSE | USCSI_SILENT | USCSI_READ | USCSI_RQENABLE;
	com->uscsi_timeout = un->un_dp->non_motion_timeout;

	rval = st_ioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE, UIO_SYSSPACE);

	if (rval || com->uscsi_status || com->uscsi_resid) {

		rval = -1; /* uscsi-command failed */

	} else if (ta->log_page.code != TAPE_ALERT_PAGE) {

		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		"Not Alert Log Page returned 0x%X\n", ta->log_page.code);
		rval = -1;
	}

	length = (ta->log_page.length_hi << 8) + ta->log_page.length_lo;


	if (length != TAPE_ALERT_PARAMETER_LENGTH) {

		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "TapeAlert length %d\n", length);
	}


	for (ix = 0; ix < TAPE_ALERT_MAX_PARA; ix++) {

		/*
		 * if rval is bad before the first pass don't bother
		 */
		if (ix == 0 && rval != 0) {

			break;
		}

		flag = ((ta->param[ix].log_param.pc_hi << 8) +
			ta->param[ix].log_param.pc_lo);

		if ((ta->param[ix].param_value & 1) == 0) {
			continue;
		}
		/*
		 * check to see if current parameter is of interest.
		 * CLEAN_FOR_ERRORS is vendor specific to 9840 9940 stk's.
		 */
		if ((flag == TAF_CLEAN_NOW) ||
		    (flag == TAF_CLEAN_PERIODIC) ||
		    ((flag == CLEAN_FOR_ERRORS) &&
		    (un->un_dp->type == ST_TYPE_STK9840))) {

			rval = MTF_TAPE_CLN_SUPPORTED;


			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "alert_page drive needs clean %d\n", flag);
			un->un_HeadClean |= TAPE_ALERT_STILL_DIRTY;
			rval |= MTF_TAPE_HEAD_DIRTY;

		} else if (flag == TAF_CLEANING_MEDIA) {

			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "alert_page drive was cleaned\n");
			un->un_HeadClean &= ~TAPE_ALERT_STILL_DIRTY;
		}

	}

	/*
	 * Report it as dirty till we see it cleaned
	 */
	if (un->un_HeadClean & TAPE_ALERT_STILL_DIRTY) {

		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "alert_page still dirty\n");
		rval |= MTF_TAPE_HEAD_DIRTY;
	}

	kmem_free(com, sizeof (struct uscsi_cmd));
	kmem_free(ta,  sizeof (struct st_tape_alert));

	return (rval);
}


static int
st_check_sense_clean_bit(dev_t dev)
{
	uchar_t *sensep;
	char cdb[CDB_GROUP0];
	struct uscsi_cmd *com;
	ushort_t byte_pos;
	uchar_t bit_mask;
	unsigned length;
	int index;
	int rval;

	GET_SOFT_STATE(dev);

	/*
	 * Since this tape does not support Tape Alert,
	 * we now try to get the cleanbit status via
	 * Request Sense.
	 */

	if ((un->un_dp->options & ST_CLN_MASK) == ST_CLN_TYPE_1) {

		index = 0;

	} else if ((un->un_dp->options & ST_CLN_MASK) == ST_CLN_TYPE_2) {

		index = 1;

	} else if ((un->un_dp->options & ST_CLN_MASK) == ST_CLN_TYPE_3) {

		index = 2;

	} else {

		return (-1);
	}

	byte_pos  = st_cln_bit_position[index].cln_bit_byte;
	bit_mask  = st_cln_bit_position[index].cln_bit_mask;
	length = byte_pos + 1;

	com    = kmem_zalloc(sizeof (struct uscsi_cmd), KM_SLEEP);
	sensep = kmem_zalloc(length, KM_SLEEP);

	cdb[0] = SCMD_REQUEST_SENSE;
	cdb[1] = 0;
	cdb[2] = 0;
	cdb[3] = 0;
	cdb[4] = (char)length;
	cdb[5] = 0;

	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP0;
	com->uscsi_bufaddr = (caddr_t)sensep;
	com->uscsi_buflen = length;
	com->uscsi_flags =
		USCSI_DIAGNOSE | USCSI_SILENT | USCSI_READ | USCSI_RQENABLE;
	com->uscsi_timeout = un->un_dp->non_motion_timeout;

	rval = st_ioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
			UIO_SYSSPACE);

	if (rval || com->uscsi_status || com->uscsi_resid) {

		rval = -1;

	} else {

		rval = MTF_TAPE_CLN_SUPPORTED;
		if ((sensep[byte_pos] & bit_mask) == bit_mask) {

			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "sense data says head dirty\n");
			rval |= MTF_TAPE_HEAD_DIRTY;
		}
	}

	kmem_free(com, sizeof (struct uscsi_cmd));
	kmem_free(sensep, length);
	return (rval);
}

/*
 * st_clear_unit_attention
 *
 *  	run test unit ready's to clear out outstanding
 * 	unit attentions.
 * 	returns zero for SUCCESS or the errno from st_cmd call
 */
static int
st_clear_unit_attentions(dev_t dev_instance, int max_trys)
{
	int	i    = 0;
	int	rval;

	do {
		rval = st_cmd(dev_instance, SCMD_TEST_UNIT_READY, 0, SYNC_CMD);
	} while ((rval != 0) && (rval != ENXIO) && (++i < max_trys));
	return (rval);
}

static void
st_calculate_timeouts(struct scsi_tape *un)
{
	if (un->un_dp->non_motion_timeout == 0) {
		if (un->un_dp->options & ST_LONG_TIMEOUTS) {
			un->un_dp->non_motion_timeout =
				st_io_time * st_long_timeout_x;
		} else {
			un->un_dp->non_motion_timeout = (ushort_t)st_io_time;
		}
	}

	if (un->un_dp->io_timeout == 0) {
		if (un->un_dp->options & ST_LONG_TIMEOUTS) {
			un->un_dp->io_timeout = st_io_time * st_long_timeout_x;
		} else {
			un->un_dp->io_timeout = (ushort_t)st_io_time;
		}
	}

	if (un->un_dp->rewind_timeout == 0) {
		if (un->un_dp->options & ST_LONG_TIMEOUTS) {
			un->un_dp->rewind_timeout =
				st_space_time * st_long_timeout_x;
		} else {
			un->un_dp->rewind_timeout = (ushort_t)st_space_time;
		}
	}

	if (un->un_dp->space_timeout == 0) {
		if (un->un_dp->options & ST_LONG_TIMEOUTS) {
			un->un_dp->space_timeout =
				st_space_time * st_long_timeout_x;
		} else {
			un->un_dp->space_timeout = (ushort_t)st_space_time;
		}
	}

	if (un->un_dp->load_timeout == 0) {
		if (un->un_dp->options & ST_LONG_TIMEOUTS) {
			un->un_dp->load_timeout =
				st_space_time * st_long_timeout_x;
		} else {
			un->un_dp->load_timeout = (ushort_t)st_space_time;
		}
	}

	if (un->un_dp->unload_timeout == 0) {
		if (un->un_dp->options & ST_LONG_TIMEOUTS) {
			un->un_dp->unload_timeout =
				st_space_time * st_long_timeout_x;
		} else {
			un->un_dp->unload_timeout = (ushort_t)st_space_time;
		}
	}

	if (un->un_dp->erase_timeout == 0) {
		if (un->un_dp->options & ST_LONG_ERASE) {
			un->un_dp->erase_timeout =
				st_space_time * st_long_space_time_x;
		} else {
			un->un_dp->erase_timeout = (ushort_t)st_space_time;
		}
	}
}


/*ARGSUSED*/
static writablity
st_is_not_wormable(struct scsi_tape *un)
{
	return (RDWR);
}

static writablity
st_is_hp_lto_tape_worm(struct scsi_tape *un)
{
	writablity wrt;


	/* Mode sense should be current */
	switch (un->un_mspl->media_type) {
	case 0x00:
		switch (un->un_mspl->density) {
		case 0x40:
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "Drive has standard Gen I media loaded\n");
			break;
		case 0x42:
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "Drive has standard Gen II media loaded\n");
			break;
		case 0x44:
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "Drive has standard Gen III media loaded\n");
			break;
		default:
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "Drive has standard unknown 0x%X media loaded\n",
			    un->un_mspl->density);
		}
		wrt = RDWR;
		break;
	case 0x01:
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive has WORM medium loaded\n");
		wrt = WORM;
		break;
	case 0x80:
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive has CD-ROM emulation medium loaded\n");
		wrt = WORM;
		break;
	default:
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive has an unexpected medium type 0x%X loaded\n",
		    un->un_mspl->media_type);
		wrt = RDWR;
	}

	return (wrt);
}

#define	LTO_REQ_INQUIRY 44
static writablity
st_is_hp_lto_worm(struct scsi_tape *un)
{
	char *buf;
	int result;
	writablity wrt;

	buf = kmem_zalloc(LTO_REQ_INQUIRY, KM_SLEEP);

	result = st_get_special_inquiry(un, LTO_REQ_INQUIRY, buf, 0);

	if (result != 0) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Read Standard Inquiry for WORM support failed");
		wrt = ERROR;
	} else if ((buf[40] & 1) == 0) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive is NOT WORMable\n");
		/* This drive doesn't support it so don't check again */
		un->un_dp->options &= ~ST_WORMABLE;
		wrt = RDWR;
		un->un_wormable = st_is_not_wormable;
	} else {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive supports WORM version %d\n", buf[40] >> 1);
		un->un_wormable = st_is_hp_lto_tape_worm;
		wrt = un->un_wormable(un);
	}

	kmem_free(buf, LTO_REQ_INQUIRY);

	/*
	 * If drive doesn't support it no point in checking further.
	 */
	return (wrt);
}

static writablity
st_is_t10_worm_device(struct scsi_tape *un)
{
	writablity wrt;

	if (un->un_mspl->media_type == 0x3c) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive has WORM media loaded\n");
		wrt = WORM;
	} else {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive has non WORM media loaded\n");
		wrt = RDWR;
	}
	return (wrt);
}

#define	SEQ_CAP_PAGE	(char)0xb0
static writablity
st_is_t10_worm(struct scsi_tape *un)
{
	char *buf;
	int result;
	writablity wrt;

	buf = kmem_zalloc(6, KM_SLEEP);

	result = st_get_special_inquiry(un, 6, buf, SEQ_CAP_PAGE);

	if (result != 0) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Read Vitial Inquiry for Sequental Capability"
		    " WORM support failed %x", result);
		wrt = ERROR;
	} else if ((buf[4] & 1) == 0) {
		ASSERT(buf[1] == SEQ_CAP_PAGE);
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive is NOT WORMable\n");
		/* This drive doesn't support it so don't check again */
		un->un_dp->options &= ~ST_WORMABLE;
		wrt = RDWR;
		un->un_wormable = st_is_not_wormable;
	} else {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive supports WORM\n");
		un->un_wormable = st_is_t10_worm_device;
		wrt = un->un_wormable(un);
	}

	kmem_free(buf, 6);

	return (wrt);
}


#define	STK_REQ_SENSE 26

static writablity
st_is_stk_worm(struct scsi_tape *un)
{
	char cdb[CDB_GROUP0] = {SCMD_REQUEST_SENSE, 0, 0, 0, STK_REQ_SENSE, 0};
	struct scsi_extended_sense *sense;
	struct uscsi_cmd *cmd;
	char *buf;
	int result;
	writablity wrt;

	cmd = kmem_zalloc(sizeof (struct uscsi_cmd), KM_SLEEP);
	buf = kmem_alloc(STK_REQ_SENSE, KM_SLEEP);
	sense = (struct scsi_extended_sense *)buf;

	cmd->uscsi_flags = USCSI_READ;
	cmd->uscsi_timeout = un->un_dp->non_motion_timeout;
	cmd->uscsi_cdb = &cdb[0];
	cmd->uscsi_bufaddr = buf;
	cmd->uscsi_buflen = STK_REQ_SENSE;
	cmd->uscsi_cdblen = CDB_GROUP0;
	cmd->uscsi_rqlen = 0;
	cmd->uscsi_rqbuf = NULL;

	result = st_ioctl_cmd(un->un_dev, cmd,
	    UIO_SYSSPACE, UIO_SYSSPACE, UIO_SYSSPACE);

	if (result != 0 || cmd->uscsi_status != 0) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Request Sense for WORM failed");
		wrt = RDWR;
	} else if (sense->es_add_len + 8 < 24) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive didn't send enough sense data for WORM byte %d\n",
		    sense->es_add_len + 8);
		wrt = RDWR;
		un->un_wormable = st_is_not_wormable;
	} else if ((buf[24]) & 0x02) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive has WORM tape loaded\n");
		wrt = WORM;
		un->un_wormable = st_is_stk_worm;
	} else {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive has normal tape loaded\n");
		wrt = RDWR;
		un->un_wormable = st_is_stk_worm;
	}

	kmem_free(buf, STK_REQ_SENSE);
	kmem_free(cmd, sizeof (struct uscsi_cmd));
	return (wrt);
}

#define	DLT_INQ_SZ 44

static writablity
st_is_dlt_tape_worm(struct scsi_tape *un)
{
	caddr_t buf;
	int result;
	writablity wrt;

	buf = kmem_alloc(DLT_INQ_SZ, KM_SLEEP);

	/* Read Attribute Media Type */

	result = st_read_attributes(un, 0x0408, buf, 10);

	/*
	 * If this quantum drive is attached via an HBA that cannot
	 * support thr read attributes command return error in the
	 * hope that someday they will support the t10 method.
	 */
	if (result == EINVAL && un->un_max_cdb_sz < CDB_GROUP4) {
		scsi_log(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Read Attribute Command for WORM Media detection is not "
		    "supported on the HBA that this drive is attached to.");
		wrt = RDWR;
		un->un_wormable = st_is_not_wormable;
		goto out;
	}

	if (result != 0) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Read Attribute Command for WORM Media returned 0x%x",
		    result);
		wrt = RDWR;
		un->un_dp->options &= ~ST_WORMABLE;
		goto out;
	}

	if ((uchar_t)buf[9] == 0x80) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive media is WORM\n");
		wrt = WORM;
	} else {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive media is not WORM Media 0x%x\n", (uchar_t)buf[9]);
		wrt = RDWR;
	}

out:
	kmem_free(buf, DLT_INQ_SZ);
	return (wrt);
}

static writablity
st_is_dlt_worm(struct scsi_tape *un)
{
	caddr_t buf;
	int result;
	writablity wrt;

	buf = kmem_alloc(DLT_INQ_SZ, KM_SLEEP);

	result = st_get_special_inquiry(un, DLT_INQ_SZ, buf, 0xC0);

	if (result != 0) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Read Vendor Specific Inquiry for WORM support failed");
		wrt = RDWR;
		goto out;
	}

	if ((buf[2] & 1) == 0) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive is not WORMable\n");
		wrt = RDWR;
		un->un_dp->options &= ~ST_WORMABLE;
		un->un_wormable = st_is_not_wormable;
		goto out;
	} else {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Drive is WORMable\n");
		un->un_wormable = st_is_dlt_tape_worm;
		wrt = un->un_wormable(un);
	}
out:
	kmem_free(buf, DLT_INQ_SZ);

	return (wrt);
}

typedef struct {
	struct modeheader_seq header;
#if defined(_BIT_FIELDS_LTOH) /* X86 */
	uchar_t pagecode	:6,
				:2;
	uchar_t page_len;
	uchar_t syslogalive	:2,
		device		:1,
		abs		:1,
		ulpbot		:1,
		prth		:1,
		ponej		:1,
		ait		:1;
	uchar_t span;

	uchar_t			:6,
		worm		:1,
		mic		:1;
	uchar_t worm_cap	:1,
				:7;
	uint32_t		:32;
#else /* SPARC */
	uchar_t			:2,
		pagecode	:6;
	uchar_t page_len;
	uchar_t ait		:1,
		device		:1,
		abs		:1,
		ulpbot		:1,
		prth		:1,
		ponej		:1,
		syslogalive	:2;
	uchar_t span;
	uchar_t mic		:1,
		worm		:1,
				:6;
	uchar_t			:7,
		worm_cap	:1;
	uint32_t		:32;
#endif
}ait_dev_con;

#define	AIT_DEV_PAGE 0x31
static writablity
st_is_sony_worm(struct scsi_tape *un)
{
	int result;
	writablity wrt;
	ait_dev_con *ait_conf;

	ait_conf = kmem_zalloc(sizeof (ait_dev_con), KM_SLEEP);

	result = st_gen_mode_sense(un, AIT_DEV_PAGE,
	    (struct seq_mode *)ait_conf, sizeof (ait_dev_con));

	if (result == 0) {

		if (ait_conf->pagecode != AIT_DEV_PAGE) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "returned page 0x%x not 0x%x AIT_DEV_PAGE\n",
			    ait_conf->pagecode, AIT_DEV_PAGE);
			wrt = RDWR;
			un->un_wormable = st_is_not_wormable;

		} else if (ait_conf->worm_cap) {

			un->un_wormable = st_is_sony_worm;

			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "Drives is WORMable\n");
			if (ait_conf->worm) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "Media is WORM\n");
				wrt = WORM;
			} else {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "Media is not WORM\n");
				wrt = RDWR;
			}

		} else {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "Drives not is WORMable\n");
			wrt = RDWR;
			/* No further checking required */
			un->un_dp->options &= ~ST_WORMABLE;
		}

	} else {

		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "AIT device config mode sense page read command failed"
		    " result = %d ", result);
		wrt = ERROR;
		un->un_wormable = st_is_not_wormable;
	}

	kmem_free(ait_conf, sizeof (ait_dev_con));
	return (wrt);
}

static writablity
st_is_drive_worm(struct scsi_tape *un)
{
	writablity wrt;

	switch (un->un_dp->type) {
	case MT_ISDLT:
		wrt = st_is_dlt_worm(un);
		break;

	case MT_ISSTK9840:
		wrt = st_is_stk_worm(un);
		break;

	case MT_IS8MM:
	case MT_ISAIT:
		wrt = st_is_sony_worm(un);
		break;

	case MT_LTO:
		if (strncmp("HP ", un->un_dp->vid, 3) == 0) {
			wrt = st_is_hp_lto_worm(un);
		} else {
			wrt = st_is_t10_worm(un);
		}
		break;

	default:
		wrt = ERROR;
		break;
	}

	/*
	 * If any of the above failed try the t10 standard method.
	 */
	if (wrt == ERROR) {
		wrt = st_is_t10_worm(un);
	}

	/*
	 * Unknown method for detecting WORM media.
	 */
	if (wrt == ERROR) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "Unknown method for WORM media detection\n");
		wrt = RDWR;
		un->un_dp->options &= ~ST_WORMABLE;
	}

	return (wrt);
}

static int
st_read_attributes(struct scsi_tape *un, uint16_t attribute, caddr_t buf,
    size_t size)
{
	uchar_t cdb[CDB_GROUP4];
	struct uscsi_cmd *cmd;
	int result;

	cdb[0] = SCMD_READ_ATTRIBUTE;
	cdb[1] = 0;
	cdb[2] = 0;
	cdb[3] = 0;
	cdb[4] = 0;
	cdb[5] = 0;
	cdb[6] = 0;
	cdb[7] = 0;
	cdb[8] = (uchar_t)(attribute >> 8);
	cdb[9] = (uchar_t)(attribute);
	cdb[10] = (uchar_t)(size >> 24);
	cdb[11] = (uchar_t)(size >> 16);
	cdb[12] = (uchar_t)(size >> 8);
	cdb[13] = (uchar_t)(size);
	cdb[14] = 0;
	cdb[15] = 0;

	cmd = kmem_zalloc(sizeof (struct uscsi_cmd), KM_SLEEP);

	cmd->uscsi_flags = USCSI_READ | USCSI_DIAGNOSE;
	cmd->uscsi_timeout = un->un_dp->non_motion_timeout;
	cmd->uscsi_cdb = (caddr_t)&cdb[0];
	cmd->uscsi_bufaddr = (caddr_t)buf;
	cmd->uscsi_buflen = size;
	cmd->uscsi_cdblen = sizeof (cdb);

	result = st_ioctl_cmd(un->un_dev, cmd,
	    UIO_SYSSPACE, UIO_SYSSPACE, UIO_SYSSPACE);

	if (result != 0 || cmd->uscsi_status != 0) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_read_attribute failed: result %d status %d\n",
		    result, cmd->uscsi_status);
		if (result == 0) {
			result = EIO;
		}
	}

	kmem_free(cmd, sizeof (cdb));
	return (result);
}

static int
st_get_special_inquiry(struct scsi_tape *un, uchar_t size, caddr_t dest,
    uchar_t page)
{
	char cdb[CDB_GROUP0];
	struct scsi_extended_sense *sense;
	struct uscsi_cmd *cmd;
	int result;

	cdb[0] = SCMD_INQUIRY;
	cdb[1] = page ? 1 : 0;
	cdb[2] = page;
	cdb[3] = 0;
	cdb[4] = size;
	cdb[5] = 0;

	cmd = kmem_zalloc(sizeof (struct uscsi_cmd), KM_SLEEP);
	sense = kmem_alloc(sizeof (struct scsi_extended_sense), KM_SLEEP);

	cmd->uscsi_flags = USCSI_READ | USCSI_RQENABLE;
	cmd->uscsi_timeout = un->un_dp->non_motion_timeout;
	cmd->uscsi_cdb = &cdb[0];
	cmd->uscsi_bufaddr = dest;
	cmd->uscsi_buflen = size;
	cmd->uscsi_cdblen = CDB_GROUP0;
	cmd->uscsi_rqlen = sizeof (struct scsi_extended_sense);
	cmd->uscsi_rqbuf = (caddr_t)sense;

	result = st_ioctl_cmd(un->un_dev, cmd,
	    UIO_SYSSPACE, UIO_SYSSPACE, UIO_SYSSPACE);

	if (result != 0 || cmd->uscsi_status != 0) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_get_special_inquiry() failed for page %x", page);
		if (result == 0) {
			result = EIO;
		}
	}

	kmem_free(sense, sizeof (struct scsi_extended_sense));
	kmem_free(cmd, sizeof (struct uscsi_cmd));

	return (result);
}


#if defined(__i386) || defined(__amd64)

/*
 * release contig_mem and wake up waiting thread, if any
 */
static void
st_release_contig_mem(struct scsi_tape *un, struct contig_mem *cp)
{
	mutex_enter(ST_MUTEX);

	cp->cm_next = un->un_contig_mem;
	un->un_contig_mem = cp;
	un->un_contig_mem_available_num++;
	cv_broadcast(&un->un_contig_mem_cv);

	mutex_exit(ST_MUTEX);
}

/*
 * St_get_contig_mem will return a contig_mem if there is one available
 * in current system. Otherwise, it will try to alloc one, if the total
 * number of contig_mem is within st_max_contig_mem_num.
 * It will sleep, if allowed by caller or return NULL, if no contig_mem
 * is available for now.
 */
static struct contig_mem *
st_get_contig_mem(struct scsi_tape *un, size_t len, int alloc_flags)
{
	size_t rlen;
	struct contig_mem *cp = NULL;
	ddi_acc_handle_t acc_hdl;
	caddr_t addr;
	int big_enough = 0;
	int (*dma_alloc_cb)() = (alloc_flags == KM_SLEEP) ?
		DDI_DMA_SLEEP : DDI_DMA_DONTWAIT;

	/* Try to get one available contig_mem */
	mutex_enter(ST_MUTEX);
	if (un->un_contig_mem_available_num > 0) {
		ST_GET_CONTIG_MEM_HEAD(un, cp, len, big_enough);
	} else if (un->un_contig_mem_total_num < st_max_contig_mem_num) {
		/*
		 * we failed to get one. we're going to
		 * alloc one more contig_mem for this I/O
		 */
		mutex_exit(ST_MUTEX);
		cp = (struct contig_mem *)kmem_zalloc(
		    sizeof (struct contig_mem) + biosize(),
		    alloc_flags);
		if (cp == NULL) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "alloc contig_mem failure\n");
			return (NULL); /* cannot get one */
		}
		cp->cm_bp = (struct buf *)
		    (((caddr_t)cp) + sizeof (struct contig_mem));
		bioinit(cp->cm_bp);
		mutex_enter(ST_MUTEX);
		un->un_contig_mem_total_num++; /* one more available */
	} else {
		/*
		 * we failed to get one and we're NOT allowed to
		 * alloc more contig_mem
		 */
		if (alloc_flags == KM_SLEEP) {
			while (un->un_contig_mem_available_num <= 0) {
				cv_wait(&un->un_contig_mem_cv,
				    ST_MUTEX);
			}
			ST_GET_CONTIG_MEM_HEAD(un, cp, len, big_enough);
		} else {
			mutex_exit(ST_MUTEX);
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "alloc contig_mem failure\n");
			return (NULL); /* cannot get one */
		}
	}
	mutex_exit(ST_MUTEX);

	/* We need to check if this block of mem is big enough for this I/O */
	if (cp->cm_len < len) {
		/* not big enough, need to alloc a new one */
		if (ddi_dma_mem_alloc(un->un_contig_mem_hdl, len, &st_acc_attr,
			DDI_DMA_STREAMING, dma_alloc_cb, NULL,
			&addr, &rlen, &acc_hdl) != DDI_SUCCESS) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "alloc contig_mem failure: not enough mem\n");
			st_release_contig_mem(un, cp);
			cp = NULL;
		} else {
			if (cp->cm_addr) {
				/* release previous one before attach new one */
				ddi_dma_mem_free(&cp->cm_acc_hdl);
			}
			mutex_enter(ST_MUTEX);
			un->un_max_contig_mem_len =
			    un->un_max_contig_mem_len >= len ?
			    un->un_max_contig_mem_len : len;
			mutex_exit(ST_MUTEX);

			/* attach new mem to this cp */
			cp->cm_addr = addr;
			cp->cm_acc_hdl = acc_hdl;
			cp->cm_len = len;

			goto alloc_ok; /* get one usable cp */
		}
	} else {
		goto alloc_ok; /* get one usable cp */
	}

	/* cannot find/alloc a usable cp, when we get here */

	mutex_enter(ST_MUTEX);
	if ((un->un_max_contig_mem_len < len) ||
	    (alloc_flags != KM_SLEEP)) {
		mutex_exit(ST_MUTEX);
		return (NULL);
	}

	/*
	 * we're allowed to sleep, and there is one big enough
	 * contig mem in the system, which is currently in use,
	 * wait for it...
	 */
	big_enough = 1;
	do {
		cv_wait(&un->un_contig_mem_cv, ST_MUTEX);
		ST_GET_CONTIG_MEM_HEAD(un, cp, len, big_enough);
	} while (cp == NULL);
	mutex_exit(ST_MUTEX);

	/* we get the big enough contig mem, finally */

alloc_ok:
	/* init bp attached to this cp */
	bioreset(cp->cm_bp);
	cp->cm_bp->b_un.b_addr = cp->cm_addr;
	cp->cm_bp->b_private = (void *)cp;

	return (cp);
}

/*
 * this is the biodone func for the bp used in big block I/O
 */
static int
st_bigblk_xfer_done(struct buf *bp)
{
	struct contig_mem *cp;
	struct buf *orig_bp;
	int remapped = 0;
	int ioerr;
	struct scsi_tape *un;

	/* sanity check */
	if (bp == NULL) {
		return (DDI_FAILURE);
	}

	un = ddi_get_soft_state(st_state, MTUNIT(bp->b_edev));
	if (un == NULL) {
		return (DDI_FAILURE);
	}

	cp = (struct contig_mem *)bp->b_private;
	orig_bp = cp->cm_bp; /* get back the bp we have replaced */
	cp->cm_bp = bp;

	/* special handling for special I/O */
	if (cp->cm_use_sbuf) {
#ifndef __lock_lint
		ASSERT(un->un_sbuf_busy);
#endif
		un->un_sbufp = orig_bp;
		cp->cm_use_sbuf = 0;
	}

	orig_bp->b_resid = bp->b_resid;
	ioerr = geterror(bp);
	if (ioerr != 0) {
		bioerror(orig_bp, ioerr);
	} else if (orig_bp->b_flags & B_READ) {
		/* copy data back to original bp */
		if (orig_bp->b_flags & (B_PHYS | B_PAGEIO)) {
			bp_mapin(orig_bp);
			remapped = 1;
		}
		bcopy(bp->b_un.b_addr, orig_bp->b_un.b_addr,
			bp->b_bcount - bp->b_resid);
		if (remapped)
			bp_mapout(orig_bp);
	}

	st_release_contig_mem(un, cp);

	biodone(orig_bp);

	return (DDI_SUCCESS);
}

/*
 * We use this func to replace original bp that may not be able to do I/O
 * in big block size with one that can
 */
static struct buf *
st_get_bigblk_bp(struct buf *bp)
{
	struct contig_mem *cp;
	struct scsi_tape *un;
	struct buf *cont_bp;
	int remapped = 0;

	un = ddi_get_soft_state(st_state, MTUNIT(bp->b_edev));
	if (un == NULL) {
		return (bp);
	}

	/* try to get one contig_mem */
	cp = st_get_contig_mem(un, bp->b_bcount, KM_SLEEP);
	if (!cp) {
		scsi_log(ST_DEVINFO, st_label, CE_WARN,
			"Cannot alloc contig buf for I/O for %lu blk size",
			bp->b_bcount);
		return (bp);
	}
	cont_bp = cp->cm_bp;
	cp->cm_bp = bp;

	/* make sure that we "are" using un_sbufp for special I/O */
	if (bp == un->un_sbufp) {
#ifndef __lock_lint
		ASSERT(un->un_sbuf_busy);
#endif
		un->un_sbufp = cont_bp;
		cp->cm_use_sbuf = 1;
	}

	/* clone bp */
	cont_bp->b_bcount = bp->b_bcount;
	cont_bp->b_resid = bp->b_resid;
	cont_bp->b_iodone = st_bigblk_xfer_done;
	cont_bp->b_file = bp->b_file;
	cont_bp->b_offset = bp->b_offset;
	cont_bp->b_dip = bp->b_dip;
	cont_bp->b_error = 0;
	cont_bp->b_proc = NULL;
	cont_bp->b_flags = bp->b_flags & ~(B_PAGEIO | B_PHYS | B_SHADOW);
	cont_bp->b_shadow = NULL;
	cont_bp->b_pages = NULL;
	cont_bp->b_edev = bp->b_edev;
	cont_bp->b_dev = bp->b_dev;
	cont_bp->b_lblkno = bp->b_lblkno;
	cont_bp->b_forw = bp->b_forw;
	cont_bp->b_back = bp->b_back;
	cont_bp->av_forw = bp->av_forw;
	cont_bp->av_back = bp->av_back;
	cont_bp->b_bufsize = bp->b_bufsize;

	/* get data in original bp */
	if (bp->b_flags & B_WRITE) {
		if (bp->b_flags & (B_PHYS | B_PAGEIO)) {
			bp_mapin(bp);
			remapped = 1;
		}
		bcopy(bp->b_un.b_addr, cont_bp->b_un.b_addr, bp->b_bcount);
		if (remapped)
			bp_mapout(bp);
	}

	return (cont_bp);
}
#else
#ifdef __lock_lint
static int
st_bigblk_xfer_done(struct buf *bp)
{
	return (0);
}
#endif
#endif
