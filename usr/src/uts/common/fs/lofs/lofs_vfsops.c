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

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/pathname.h>
#include <sys/kmem.h>
#include <sys/cred.h>
#include <sys/statvfs.h>
#include <sys/fs/lofs_info.h>
#include <sys/fs/lofs_node.h>
#include <sys/mount.h>
#include <sys/mntent.h>
#include <sys/mkdev.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>
#include <sys/policy.h>
#include "fs/fs_subr.h"

/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>

static mntopts_t lofs_mntopts;

static int lofsinit(int, char *);

static vfsdef_t vfw = {
	VFSDEF_VERSION,
	"lofs",
	lofsinit,
	VSW_HASPROTO|VSW_STATS,
	&lofs_mntopts
};

/*
 * Stuff needed to support "zonedevfs" mode.
 */
static major_t lofs_major;
static minor_t lofs_minor;
static kmutex_t lofs_minor_lock;

/*
 * LOFS mount options table
 */
static char *xattr_cancel[] = { MNTOPT_NOXATTR, NULL };
static char *noxattr_cancel[] = { MNTOPT_XATTR, NULL };
static char *zonedevfs_cancel[] = { MNTOPT_LOFS_NOZONEDEVFS, NULL };
static char *nozonedevfs_cancel[] = { MNTOPT_LOFS_ZONEDEVFS, NULL };
static char *sub_cancel[] = { MNTOPT_LOFS_NOSUB, NULL };
static char *nosub_cancel[] = { MNTOPT_LOFS_SUB, NULL };

static mntopt_t mntopts[] = {
/*
 *	option name		cancel option	default arg	flags
 *		private data
 */
	{ MNTOPT_XATTR,		xattr_cancel,	NULL,		0,
		(void *)0 },
	{ MNTOPT_NOXATTR,	noxattr_cancel,	NULL,		0,
		(void *)0 },
	{ MNTOPT_LOFS_ZONEDEVFS,	zonedevfs_cancel,	NULL,	0,
		(void *)0 },
	{ MNTOPT_LOFS_NOZONEDEVFS,	nozonedevfs_cancel,	NULL,	0,
		(void *)0 },
	{ MNTOPT_LOFS_SUB,	sub_cancel,	NULL,		0,
		(void *)0 },
	{ MNTOPT_LOFS_NOSUB,	nosub_cancel,	NULL,		0,
		(void *)0 },
};

static mntopts_t lofs_mntopts = {
	sizeof (mntopts) / sizeof (mntopt_t),
	mntopts
};

/*
 * Module linkage information for the kernel.
 */

static struct modlfs modlfs = {
	&mod_fsops, "filesystem for lofs", &vfw
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlfs, NULL
};

/*
 * This is the module initialization routine.
 */
int
_init()
{
	int status;

	lofs_subrinit();
	status = mod_install(&modlinkage);
	if (status != 0) {
		/*
		 * Cleanup previously initialized work.
		 */
		lofs_subrfini();
	}

	return (status);
}

/*
 * Don't allow the lofs module to be unloaded for now.
 * There is a memory leak if it gets unloaded.
 */
int
_fini()
{
	return (EBUSY);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


static int lofsfstype;
vfsops_t *lo_vfsops;

/*
 * lo mount vfsop
 * Set up mount info record and attach it to vfs struct.
 */
/*ARGSUSED*/
static int
lo_mount(struct vfs *vfsp,
	struct vnode *vp,
	struct mounta *uap,
	struct cred *cr)
{
	int error;
	struct vnode *srootvp = NULL;	/* the server's root */
	struct vnode *realrootvp;
	struct loinfo *li;
	int is_zonedevfs = 0;
	int nodev;

	nodev = vfs_optionisset(vfsp, MNTOPT_NODEVICES, NULL);

	if ((error = secpolicy_fs_mount(cr, vp, vfsp)) != 0)
		return (EPERM);

	/*
	 * Loopback devices which get "nodevices" added can be done without
	 * "nodevices" set because we cannot import devices into a zone
	 * with loopback.  Note that we have all zone privileges when
	 * this happens; if not, we'd have gotten "nosuid".
	 */
	if (!nodev && vfs_optionisset(vfsp, MNTOPT_NODEVICES, NULL))
		vfs_setmntopt(vfsp, MNTOPT_DEVICES, NULL, VFS_NODISPLAY);

	/*
	 * We must ensure that only the global zone applies the 'zonedevfs'
	 * option; we don't want non-global zones to be able to establish
	 * lofs mounts using the special dev_t we use to ensure that the
	 * contents of a zone's /dev cannot be victim to link(2) or rename(2).
	 * See below, where we set all of this up.
	 *
	 * Since this is more like a privilege check, we use crgetzoneid(cr)
	 * instead of getzoneid().
	 */
	is_zonedevfs = vfs_optionisset(vfsp, MNTOPT_LOFS_ZONEDEVFS, NULL);
	if (crgetzoneid(cr) != GLOBAL_ZONEID && is_zonedevfs)
		return (EPERM);

	mutex_enter(&vp->v_lock);
	if (!(uap->flags & MS_OVERLAY) &&
		(vp->v_count != 1 || (vp->v_flag & VROOT))) {
		mutex_exit(&vp->v_lock);
		return (EBUSY);
	}
	mutex_exit(&vp->v_lock);

	/*
	 * Find real root, and make vfs point to real vfs
	 */
	if (error = lookupname(uap->spec, (uap->flags & MS_SYSSPACE) ?
		UIO_SYSSPACE : UIO_USERSPACE, FOLLOW, NULLVPP,
	    &realrootvp))
		return (error);

	/*
	 * realrootvp may be an AUTOFS node, in which case we
	 * perform a VOP_ACCESS() to trigger the mount of the
	 * intended filesystem, so we loopback mount the intended
	 * filesystem instead of the AUTOFS filesystem.
	 */
	(void) VOP_ACCESS(realrootvp, 0, 0, cr);

	/*
	 * We're interested in the top most filesystem.
	 * This is specially important when uap->spec is a trigger
	 * AUTOFS node, since we're really interested in mounting the
	 * filesystem AUTOFS mounted as result of the VOP_ACCESS()
	 * call not the AUTOFS node itself.
	 */
	if (vn_mountedvfs(realrootvp) != NULL) {
		if (error = traverse(&realrootvp)) {
			VN_RELE(realrootvp);
			return (error);
		}
	}

	/*
	 * Allocate a vfs info struct and attach it
	 */
	li = kmem_zalloc(sizeof (struct loinfo), KM_SLEEP);
	li->li_realvfs = realrootvp->v_vfsp;
	li->li_mountvfs = vfsp;

	/*
	 * Set mount flags to be inherited by loopback vfs's
	 */
	if (vfs_optionisset(vfsp, MNTOPT_RO, NULL)) {
		li->li_mflag |= VFS_RDONLY;
	}
	if (vfs_optionisset(vfsp, MNTOPT_NOSUID, NULL)) {
		li->li_mflag |= (VFS_NOSETUID|VFS_NODEVICES);
	}
	if (vfs_optionisset(vfsp, MNTOPT_NODEVICES, NULL)) {
		li->li_mflag |= VFS_NODEVICES;
	}
	if (vfs_optionisset(vfsp, MNTOPT_NOSETUID, NULL)) {
		li->li_mflag |= VFS_NOSETUID;
	}
	/*
	 * Permissive flags are added to the "deny" bitmap.
	 */
	if (vfs_optionisset(vfsp, MNTOPT_NOXATTR, NULL)) {
		li->li_dflag |= VFS_XATTR;
	}
	if (vfs_optionisset(vfsp, MNTOPT_NONBMAND, NULL)) {
		li->li_dflag |= VFS_NBMAND;
	}

	/*
	 * Propagate inheritable mount flags from the real vfs.
	 */
	if ((li->li_realvfs->vfs_flag & VFS_RDONLY) &&
	    !vfs_optionisset(vfsp, MNTOPT_RO, NULL))
		vfs_setmntopt(vfsp, MNTOPT_RO, NULL,
		    VFS_NODISPLAY);
	if ((li->li_realvfs->vfs_flag & VFS_NOSETUID) &&
	    !vfs_optionisset(vfsp, MNTOPT_NOSETUID, NULL))
		vfs_setmntopt(vfsp, MNTOPT_NOSETUID, NULL,
		    VFS_NODISPLAY);
	if ((li->li_realvfs->vfs_flag & VFS_NODEVICES) &&
	    !vfs_optionisset(vfsp, MNTOPT_NODEVICES, NULL))
		vfs_setmntopt(vfsp, MNTOPT_NODEVICES, NULL,
		    VFS_NODISPLAY);
	/*
	 * Permissive flags such as VFS_XATTR, as opposed to restrictive flags
	 * such as VFS_RDONLY, are handled differently.  An explicit
	 * MNTOPT_NOXATTR should override the underlying filesystem's VFS_XATTR.
	 */
	if ((li->li_realvfs->vfs_flag & VFS_XATTR) &&
	    !vfs_optionisset(vfsp, MNTOPT_NOXATTR, NULL) &&
	    !vfs_optionisset(vfsp, MNTOPT_XATTR, NULL))
		vfs_setmntopt(vfsp, MNTOPT_XATTR, NULL,
		    VFS_NODISPLAY);
	if ((li->li_realvfs->vfs_flag & VFS_NBMAND) &&
	    !vfs_optionisset(vfsp, MNTOPT_NBMAND, NULL) &&
	    !vfs_optionisset(vfsp, MNTOPT_NONBMAND, NULL))
		vfs_setmntopt(vfsp, MNTOPT_NBMAND, NULL,
		    VFS_NODISPLAY);

	li->li_refct = 0;
	vfsp->vfs_data = (caddr_t)li;
	vfsp->vfs_bcount = 0;
	vfsp->vfs_fstype = lofsfstype;
	vfsp->vfs_bsize = li->li_realvfs->vfs_bsize;

	/*
	 * Test to see if we need to be in "zone /dev" mode.  In zonedevfs
	 * mode, we pull a nasty trick; we make sure that the lofs dev_t does
	 * *not* reflect the underlying device, so that no renames or links
	 * can occur to or from the /dev hierarchy.
	 */
	if (is_zonedevfs) {
		dev_t dev;

		mutex_enter(&lofs_minor_lock);
		do {
			lofs_minor = (lofs_minor + 1) & MAXMIN32;
			dev = makedevice(lofs_major, lofs_minor);
		} while (vfs_devismounted(dev));
		mutex_exit(&lofs_minor_lock);

		vfsp->vfs_dev = dev;
		vfs_make_fsid(&vfsp->vfs_fsid, dev, lofsfstype);

		li->li_flag |= LO_ZONEDEVFS;
	} else {
		vfsp->vfs_dev = li->li_realvfs->vfs_dev;
		vfsp->vfs_fsid.val[0] = li->li_realvfs->vfs_fsid.val[0];
		vfsp->vfs_fsid.val[1] = li->li_realvfs->vfs_fsid.val[1];
	}

	if (vfs_optionisset(vfsp, MNTOPT_LOFS_NOSUB, NULL)) {
		li->li_flag |= LO_NOSUB;
	}

	/*
	 * Setup the hashtable. If the root of this mount isn't a directory,
	 * there's no point in allocating a large hashtable. A table with one
	 * bucket is sufficient.
	 */
	if (realrootvp->v_type != VDIR)
		lsetup(li, 1);
	else
		lsetup(li, 0);

	/*
	 * Make the root vnode
	 */
	srootvp = makelonode(realrootvp, li, 0);
	srootvp->v_flag |= VROOT;
	li->li_rootvp = srootvp;

#ifdef LODEBUG
	lo_dprint(4, "lo_mount: vfs %p realvfs %p root %p realroot %p li %p\n",
	    vfsp, li->li_realvfs, srootvp, realrootvp, li);
#endif
	return (0);
}

/*
 * Undo loopback mount
 */
static int
lo_unmount(struct vfs *vfsp, int flag, struct cred *cr)
{
	struct loinfo *li;

	if (secpolicy_fs_unmount(cr, vfsp) != 0)
		return (EPERM);

	/*
	 * Forced unmount is not supported by this file system
	 * and thus, ENOTSUP, is being returned.
	 */
	if (flag & MS_FORCE)
		return (ENOTSUP);

	li = vtoli(vfsp);
#ifdef LODEBUG
	lo_dprint(4, "lo_unmount(%p) li %p\n", vfsp, li);
#endif
	if (li->li_refct != 1 || li->li_rootvp->v_count != 1) {
#ifdef LODEBUG
		lo_dprint(4, "refct %d v_ct %d\n", li->li_refct,
		    li->li_rootvp->v_count);
#endif
		return (EBUSY);
	}
	VN_RELE(li->li_rootvp);
	return (0);
}

/*
 * Find root of lofs mount.
 */
static int
lo_root(struct vfs *vfsp, struct vnode **vpp)
{
	*vpp = vtoli(vfsp)->li_rootvp;
#ifdef LODEBUG
	lo_dprint(4, "lo_root(0x%p) = %p\n", vfsp, *vpp);
#endif
	/*
	 * If the root of the filesystem is a special file, return the specvp
	 * version of the vnode. We don't save the specvp vnode in our
	 * hashtable since that's exclusively for lnodes.
	 */
	if (IS_DEVVP(*vpp)) {
		struct vnode *svp;

		svp = specvp(*vpp, (*vpp)->v_rdev, (*vpp)->v_type, kcred);
		if (svp == NULL)
			return (ENOSYS);
		*vpp = svp;
	} else {
		VN_HOLD(*vpp);
	}

	return (0);
}

/*
 * Get file system statistics.
 */
static int
lo_statvfs(register struct vfs *vfsp, struct statvfs64 *sbp)
{
	vnode_t *realrootvp;

#ifdef LODEBUG
	lo_dprint(4, "lostatvfs %p\n", vfsp);
#endif
	/*
	 * Using realrootvp->v_vfsp (instead of the realvfsp that was
	 * cached) is necessary to make lofs work woth forced UFS unmounts.
	 * In the case of a forced unmount, UFS stores a set of dummy vfsops
	 * in all the (i)vnodes in the filesystem. The dummy ops simply
	 * returns back EIO.
	 */
	(void) lo_realvfs(vfsp, &realrootvp);
	if (realrootvp != NULL)
		return (VFS_STATVFS(realrootvp->v_vfsp, sbp));
	else
		return (EIO);
}

/*
 * LOFS doesn't have any data or metadata to flush, pending I/O on the
 * underlying filesystem will be flushed when such filesystem is synched.
 */
/* ARGSUSED */
static int
lo_sync(struct vfs *vfsp,
	short flag,
	struct cred *cr)
{
#ifdef LODEBUG
	lo_dprint(4, "lo_sync: %p\n", vfsp);
#endif
	return (0);
}

/*
 * Obtain the vnode from the underlying filesystem.
 */
static int
lo_vget(struct vfs *vfsp, struct vnode **vpp, struct fid *fidp)
{
	vnode_t *realrootvp;

#ifdef LODEBUG
	lo_dprint(4, "lo_vget: %p\n", vfsp);
#endif
	(void) lo_realvfs(vfsp, &realrootvp);
	if (realrootvp != NULL)
		return (VFS_VGET(realrootvp->v_vfsp, vpp, fidp));
	else
		return (EIO);
}

/*
 * Free mount-specific data.
 */
static void
lo_freevfs(struct vfs *vfsp)
{
	struct loinfo *li = vtoli(vfsp);

	ldestroy(li);
	kmem_free(li, sizeof (struct loinfo));
}

static int
lofsinit(int fstyp, char *name)
{
	static const fs_operation_def_t lo_vfsops_template[] = {
		VFSNAME_MOUNT, lo_mount,
		VFSNAME_UNMOUNT, lo_unmount,
		VFSNAME_ROOT, lo_root,
		VFSNAME_STATVFS, lo_statvfs,
		VFSNAME_SYNC, (fs_generic_func_p) lo_sync,
		VFSNAME_VGET, lo_vget,
		VFSNAME_FREEVFS, (fs_generic_func_p) lo_freevfs,
		NULL, NULL
	};
	int error;

	error = vfs_setfsops(fstyp, lo_vfsops_template, &lo_vfsops);
	if (error != 0) {
		cmn_err(CE_WARN, "lofsinit: bad vfs ops template");
		return (error);
	}

	error = vn_make_ops(name, lo_vnodeops_template, &lo_vnodeops);
	if (error != 0) {
		(void) vfs_freevfsops_by_type(fstyp);
		cmn_err(CE_WARN, "lofsinit: bad vnode ops template");
		return (error);
	}

	lofsfstype = fstyp;

	if ((lofs_major = getudev()) == (major_t)-1) {
		(void) vfs_freevfsops_by_type(fstyp);
		cmn_err(CE_WARN, "lofsinit: Can't get unique device number.");
		return (ENXIO);
	}

	lofs_minor = 0;
	mutex_init(&lofs_minor_lock, NULL, MUTEX_DEFAULT, NULL);

	return (0);
}
