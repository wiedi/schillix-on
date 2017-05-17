/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.opensource.org/licenses/cddl1.txt
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
 * Miscellaneous support subroutines for High Sierra filesystem
 *
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Copyright 2006-2007,2017 J. Schilling, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/cmn_err.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/uio.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/seg_map.h>
#include <sys/swap.h>
#include <vm/seg_kmem.h>

#include <sys/fs/hsfs_spec.h>
#include <sys/fs/hsfs_node.h>
#include <sys/fs/hsfs_impl.h>

extern int hsfs_lostpage;

#ifdef __STDC__
static time_t hs_date_to_gmtime(int year, int mon, int day, int gmtoff);
#else
static time_t hs_date_to_gmtime();
#endif

/*
 * Table used in logging non-fatal errors which should be recorded
 * once per mount.  Indexed by HSFS_ERR values (defined in hsfs_node.h).
 */
struct hsfs_error {
	char	*hdr_text;	/* msg prefix: general error type */
				/* must contain %s for mnt pt */
	char 	*err_text;	/* specific error message */
	uchar_t	multiple;	/* > 1 such error per fs possible? */
	uchar_t	n_printf_args;	/* if err_text printf-like, # addtl args */
} hsfs_error[] = {
	/* HSFS_ERR_TRAILING_JUNK */
	"hsfs: Warning: the file system mounted on %s "
		"does not conform to the ISO-9660 specification:",
	"trailing blanks or null characters in file or directory name.\n",
	1, 0,
	/* HSFS_ERR_LOWER_CASE_NM */
	"hsfs: Warning: the file system mounted on %s "
		"does not conform to the ISO-9660 specification:",
	"lower case characters in file or directory name.\n",
	1, 0,
	/* HSFS_ERR_BAD_ROOT_DIR */
	"hsfs: Warning: the file system mounted on %s "
		"does not conform to the ISO-9660 specification:",
	"invalid root directory.\n",
	0,  0,
	/* HSFS_ERR_UNSUP_TYPE */
	"hsfs: Warning: the file system mounted on %s "
		"contains a file or directory with an unsupported type:",
	" 0x%x.\n",
	1, 1,
	/* HSFS_ERR_BAD_FILE_LEN */
	"hsfs: Warning: file system mounted on %s "
		"does not conform to the ISO-9660 specification:",
	"file name length greater than max allowed\n",
	1, 0,
	/* HSFS_ERR_BAD_JOLIET_FILE_LEN */
	"hsfs: Warning: file system mounted on %s "
		"does not conform to the Joliet specification:",
	"file name length greater than max allowed\n",
	1, 0,
	/* HSFS_ERR_TRUNC_JOLIET_FILE_LEN */
	"hsfs: Warning: file system mounted on %s "
		"does not conform to the Joliet specification:",
	"file name length greater than MAXNAMELEN (truncated)\n",
	1, 0,
	/* HSFS_ERR_BAD_DIR_ENTRY */
	"hsfs: Warning: file system mounted on %s "
		"has inconsistent data:",
	"invalid directory or file name length (ignored)\n",
	1, 0,
	/* HSFS_ERR_NEG_SUA_LEN */
	"hsfs: Warning: file system mounted on %s "
		"has inconsistent Rock Ridge data:",
	"negative SUA len\n",
	1, 0,
	/* HSFS_ERR_BAD_SUA_LEN */
	"hsfs: Warning: file system mounted on %s "
		"has inconsistent Rock Ridge data:",
	"SUA len too big\n",
	1, 0,
};

/*
 * Local datatype for defining tables of (Offset, Name) pairs for
 * kstats.
 */
typedef struct {
	offset_t	index;
	char		*name;
} hsfs_ksindex_t;

static const hsfs_ksindex_t hsfs_kstats[] = {
	{ 0,		"mountpoint"		},
	{ 1,		"pages_lost"		},
	{ 2,		"physical_read_pages"	},
	{ 3,		"cache_read_pages"	},
	{ 4,		"readahead_pages"	},
	{ 5,		"coalesced_pages"	},
	{ 6,		"total_pages_requested"	},
	{-1,		NULL 			}
};

/*
 * hs_parse_dirdate
 *
 * Parse the short 'directory-format' date into a Unix timeval.
 * This is the date format used in Directory Entries.
 *
 * If the date is not representable, make something up.
 */
void
hs_parse_dirdate(dp, tvp)
	uchar_t *dp;
	struct timeval *tvp;
{
	int year, month, day, hour, minute, sec, gmtoff;

	year = HDE_DATE_YEAR(dp);
	month = HDE_DATE_MONTH(dp);
	day = HDE_DATE_DAY(dp);
	hour = HDE_DATE_HOUR(dp);
	minute = HDE_DATE_MIN(dp);
	sec = HDE_DATE_SEC(dp);
	gmtoff = HDE_DATE_GMTOFF(dp);

	tvp->tv_usec = 0;

	if ((month < 1) || (month > 12) ||
	    (day < 1) || (day > 31)) {
		tvp->tv_sec = 0;
	} else {
		tvp->tv_sec = hs_date_to_gmtime(year, month, day, gmtoff);
		tvp->tv_sec += ((hour * 60) + minute) * 60 + sec;
	}
}

/*
 * hs_parse_longdate
 *
 * Parse the long 'user-oriented' date into a Unix timeval.
 * This is the date format used in the Volume Descriptor.
 *
 * If the date is not representable, make something up.
 */
void
hs_parse_longdate(dp, tvp)
	uchar_t *dp;
	struct timeval *tvp;
{
	int year, month, day, hour, minute, sec, gmtoff;

	year = HSV_DATE_YEAR(dp);
	month = HSV_DATE_MONTH(dp);
	day = HSV_DATE_DAY(dp);
	hour = HSV_DATE_HOUR(dp);
	minute = HSV_DATE_MIN(dp);
	sec = HSV_DATE_SEC(dp);
	gmtoff = HSV_DATE_GMTOFF(dp);

	if ((month < 1) || (month > 12) ||
	    (day < 1) || (day > 31)) {
		tvp->tv_sec = 0;
		tvp->tv_usec = 0;
	} else {
		tvp->tv_sec = hs_date_to_gmtime(year, month, day, gmtoff);
		tvp->tv_sec += ((hour * 60) + minute) * 60 + sec;
		tvp->tv_usec = HSV_DATE_HSEC(dp) * 10000;
	}
}

/* cumulative number of seconds per month,  non-leap and leap-year versions */
static time_t cum_sec[] = {
	0x0, 0x28de80, 0x4dc880, 0x76a700, 0x9e3400, 0xc71280,
	0xee9f80, 0x1177e00, 0x1405c80, 0x167e980, 0x190c800, 0x1b85500
};
static time_t cum_sec_leap[] = {
	0x0, 0x28de80, 0x4f1a00, 0x77f880, 0x9f8580, 0xc86400,
	0xeff100, 0x118cf80, 0x141ae00, 0x1693b00, 0x1921980, 0x1b9a680
};

#define	SEC_PER_DAY	0x15180
#define	SEC_PER_YEAR	0x1e13380L

/*
 * The Gregorian leap year formula
 */
#define	isleap(A) (((A)%4)? 0   : (((A)%100) == 0 && ((A)%400)) ? 0   : 1)
/*
 * Return the number of leap years since 0 AD assuming that the Gregorian
 * calendar applies to all years.
 */
#define	LEAPS(Y) 	((Y) / 4 - (Y) / 100 + (Y) / 400)
/*
 * Return the number of days since 0 AD
 */
#define	YRDAYS(Y)	(((Y) * 365L) + LEAPS(Y))
/*
 * Return the number of days between Januar 1 1970 and the end of the year
 * before the the year used as argument.
 */
#define	DAYS_SINCE_70(Y) (YRDAYS((Y)-1) - YRDAYS(1970-1))

/*
 * hs_date_to_gmtime
 *
 * Convert year(0-9999)/month(1-12)/day(1-31) to seconds-since-1970/1/1.
 */
static time_t
hs_date_to_gmtime(year, mon, day, gmtoff)
	int year;
	int mon;
	int day;
	int gmtoff;
{
	time_t sum;
	time_t *cp;

	/*
	 * Figure seconds until this year and correct for leap years.
	 */
	sum = DAYS_SINCE_70(year);
	sum *= SEC_PER_DAY;

	/*
	 * Point to the correct table for this year and
	 * add in seconds until this month.
	 */
	cp = (mon >= 3 && isleap(year)) ? cum_sec_leap : cum_sec;
	sum += cp[mon - 1];
	/*
	 * Add in seconds until 0:00 of this day.
	 * (days-per-month validation is not done here)
	 */
	sum += (day - 1) * SEC_PER_DAY;
	sum -= (gmtoff * 15 * 60);
	return (sum);
}

/*
 * Indicate whether the directory is valid.
 */

int
hsfs_valid_dir(hd)
	struct hs_direntry *hd;
{
	/*
	 * check to see if this directory is not marked as a directory.
	 * check to see if data length is zero.
	 */

	if (hd->ext_size == 0)
		return (0);

	if (hd->type != VDIR)
		return (0);

	return (1);
}



/*
 * If we haven't complained about this error type yet, do.
 */
void
hs_log_bogus_disk_warning(fsp, errtype, data)
	struct hsfs	*fsp;
	int 		errtype;
	uint_t		data;
{

	if (fsp->hsfs_err_flags & (1 << errtype))
		return;		/* already complained */

	cmn_err(CE_NOTE, hsfs_error[errtype].hdr_text,
		fsp->hsfs_fsmnt);

	switch (hsfs_error[errtype].n_printf_args) {
	case 0:
		cmn_err(CE_CONT, hsfs_error[errtype].err_text);
		break;
	case 1:
		cmn_err(CE_CONT, hsfs_error[errtype].err_text, data);
		break;
	default:
		/* don't currently handle more than 1 arg */
		cmn_err(CE_CONT, "unknown problem; internal error.\n");
	}
	cmn_err(CE_CONT,
"Due to this error, the file system may not be correctly interpreted.\n");
	if (hsfs_error[errtype].multiple)
		cmn_err(CE_CONT,
"Other such errors in this file system will be silently ignored.\n\n");
	else
		cmn_err(CE_CONT, "\n");

	fsp->hsfs_err_flags |= (1 << errtype);
}

/*
 * Callback from kstat framework. Grab a snapshot of the current hsfs
 * counters and populate the kstats.
 */
static int
hsfs_kstats_update(kstat_t *ksp, int flag)
{
	struct hsfs *fsp;
	kstat_named_t *knp;
	uint64_t pages_lost;
	uint64_t physical_read_bytes;
	uint64_t cache_read_pages;
	uint64_t readahead_bytes;
	uint64_t coalesced_bytes;
	uint64_t total_pages_requested;

	if (flag != KSTAT_READ)
		return (EACCES);

	fsp = ksp->ks_private;
	knp = ksp->ks_data;

	mutex_enter(&(fsp->hqueue->strategy_lock));
	mutex_enter(&(fsp->hqueue->hsfs_queue_lock));

	cache_read_pages = fsp->cache_read_pages;
	pages_lost = hsfs_lostpage;
	physical_read_bytes = fsp->physical_read_bytes;
	readahead_bytes =  fsp->readahead_bytes;
	coalesced_bytes = fsp->coalesced_bytes;
	total_pages_requested = fsp->total_pages_requested;

	mutex_exit(&(fsp->hqueue->strategy_lock));
	mutex_exit(&(fsp->hqueue->hsfs_queue_lock));

	knp++;
	(knp++)->value.ui64 = pages_lost;
	(knp++)->value.ui64 = howmany(physical_read_bytes, PAGESIZE);
	(knp++)->value.ui64 = cache_read_pages;
	(knp++)->value.ui64 = howmany(readahead_bytes, PAGESIZE);
	(knp++)->value.ui64 = howmany(coalesced_bytes, PAGESIZE);
	(knp++)->value.ui64 = total_pages_requested;

	return (0);
}

/*
 * Initialize hsfs kstats, which are all name value pairs with
 * values being various counters.
 */
static kstat_t *
hsfs_setup_named_kstats(struct hsfs *fsp, int fsid, char *name,
    const hsfs_ksindex_t *ksip, int (*update)(kstat_t *, int))
{
	kstat_t *ksp;
	kstat_named_t *knp;
	char *np;
	char *mntpt = fsp->hsfs_fsmnt;
	size_t size;

	size = (sizeof (hsfs_kstats)) / (sizeof (hsfs_ksindex_t));
	ksp = kstat_create("hsfs_fs", fsid, name, "hsfs",
	    KSTAT_TYPE_NAMED, size-1, KSTAT_FLAG_VIRTUAL);
	if (ksp == NULL)
		return (NULL);

	ksp->ks_data = kmem_alloc(sizeof (kstat_named_t) * size, KM_SLEEP);
	ksp->ks_private = fsp;
	ksp->ks_update = update;
	ksp->ks_data_size += strlen(mntpt) + 1;
	knp = ksp->ks_data;
	kstat_named_init(knp, ksip->name, KSTAT_DATA_STRING);
	kstat_named_setstr(knp, mntpt);
	knp++;
	ksip++;

	for (; (np = ksip->name) != NULL; ++knp, ++ksip) {
		kstat_named_init(knp, np, KSTAT_DATA_UINT64);
	}
	kstat_install(ksp);

	return (ksp);
}

void
hsfs_init_kstats(struct hsfs *fsp, int fsid)
{
	fsp->hsfs_kstats = hsfs_setup_named_kstats(fsp, fsid, "hsfs_read_stats",
	    hsfs_kstats, hsfs_kstats_update);
}

void
hsfs_fini_kstats(struct hsfs *fsp)
{
	void *data;

	if (fsp->hsfs_kstats != NULL) {
		data = fsp->hsfs_kstats->ks_data;
		kstat_delete(fsp->hsfs_kstats);
		kmem_free(data, sizeof (kstat_named_t) *
		    (sizeof (hsfs_kstats)) / (sizeof (hsfs_ksindex_t)));
	}
	fsp->hsfs_kstats = NULL;
}
