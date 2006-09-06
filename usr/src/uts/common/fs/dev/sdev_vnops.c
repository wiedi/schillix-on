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
 * vnode ops for the /dev filesystem
 *
 * - VDIR, VCHR, CBLK, and VLNK are considered must supported files
 * - VREG and VDOOR are used for some internal implementations in
 *    the global zone, e.g. devname and devfsadm communication
 * - other file types are unusual in this namespace and
 *    not supported for now
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/user.h>
#include <sys/time.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/flock.h>
#include <sys/kmem.h>
#include <sys/uio.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/cred.h>
#include <sys/cred_impl.h>
#include <sys/dirent.h>
#include <sys/pathname.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/policy.h>
#include <vm/hat.h>
#include <vm/seg_vn.h>
#include <vm/seg_map.h>
#include <vm/seg.h>
#include <vm/as.h>
#include <vm/page.h>
#include <sys/proc.h>
#include <sys/mode.h>
#include <sys/sunndi.h>
#include <sys/ptms.h>
#include <fs/fs_subr.h>
#include <sys/fs/dv_node.h>
#include <sys/fs/sdev_impl.h>
#include <sys/fs/sdev_node.h>

/*ARGSUSED*/
static int
sdev_open(struct vnode **vpp, int flag, struct cred *cred)
{
	struct sdev_node *dv = VTOSDEV(*vpp);
	struct sdev_node *ddv = dv->sdev_dotdot;
	int error = 0;

	if ((*vpp)->v_type == VDIR)
		return (0);

	if (!SDEV_IS_GLOBAL(dv))
		return (ENOTSUP);

	ASSERT((*vpp)->v_type == VREG);
	if ((*vpp)->v_type != VREG)
		return (ENOTSUP);

	ASSERT(ddv);
	rw_enter(&ddv->sdev_contents, RW_READER);
	if (dv->sdev_attrvp == NULL) {
		rw_exit(&ddv->sdev_contents);
		return (ENOENT);
	}
	error = VOP_OPEN(&(dv->sdev_attrvp), flag, cred);
	rw_exit(&ddv->sdev_contents);
	return (error);
}

/*ARGSUSED1*/
static int
sdev_close(struct vnode *vp, int flag, int count,
    offset_t offset, struct cred *cred)
{
	struct sdev_node *dv = VTOSDEV(vp);

	if (vp->v_type == VDIR) {
		cleanlocks(vp, ttoproc(curthread)->p_pid, 0);
		cleanshares(vp, ttoproc(curthread)->p_pid);
		return (0);
	}

	if (!SDEV_IS_GLOBAL(dv))
		return (ENOTSUP);

	ASSERT(vp->v_type == VREG);
	if (vp->v_type != VREG)
		return (ENOTSUP);

	ASSERT(dv->sdev_attrvp);
	return (VOP_CLOSE(dv->sdev_attrvp, flag, count, offset, cred));
}

/*ARGSUSED*/
static int
sdev_read(struct vnode *vp, struct uio *uio, int ioflag, struct cred *cred,
	struct caller_context *ct)
{
	struct sdev_node *dv = (struct sdev_node *)VTOSDEV(vp);
	int	error;

	if (!SDEV_IS_GLOBAL(dv))
		return (EINVAL);

	if (vp->v_type == VDIR)
		return (EISDIR);

	/* only supporting regular files in /dev */
	ASSERT(vp->v_type == VREG);
	if (vp->v_type != VREG)
		return (EINVAL);

	ASSERT(RW_READ_HELD(&VTOSDEV(vp)->sdev_contents));
	ASSERT(dv->sdev_attrvp);
	(void) VOP_RWLOCK(dv->sdev_attrvp, 0, NULL);
	error = VOP_READ(dv->sdev_attrvp, uio, ioflag, cred, ct);
	VOP_RWUNLOCK(dv->sdev_attrvp, 0, NULL);
	return (error);
}

/*ARGSUSED*/
static int
sdev_write(struct vnode *vp, struct uio *uio, int ioflag, struct cred *cred,
	struct caller_context *ct)
{
	struct sdev_node *dv = VTOSDEV(vp);
	int	error = 0;

	if (!SDEV_IS_GLOBAL(dv))
		return (EINVAL);

	if (vp->v_type == VDIR)
		return (EISDIR);

	/* only supporting regular files in /dev */
	ASSERT(vp->v_type == VREG);
	if (vp->v_type != VREG)
		return (EINVAL);

	ASSERT(dv->sdev_attrvp);

	(void) VOP_RWLOCK(dv->sdev_attrvp, 1, NULL);
	error = VOP_WRITE(dv->sdev_attrvp, uio, ioflag, cred, ct);
	VOP_RWUNLOCK(dv->sdev_attrvp, 1, NULL);
	if (error == 0) {
		sdev_update_timestamps(dv->sdev_attrvp, kcred,
		    AT_MTIME);
	}
	return (error);
}

/*ARGSUSED*/
static int
sdev_ioctl(struct vnode *vp, int cmd, intptr_t arg, int flag,
    struct cred *cred, int *rvalp)
{
	struct sdev_node *dv = VTOSDEV(vp);

	if (!SDEV_IS_GLOBAL(dv) || (vp->v_type == VDIR))
		return (ENOTTY);

	ASSERT(vp->v_type == VREG);
	if (vp->v_type != VREG)
		return (EINVAL);

	ASSERT(dv->sdev_attrvp);
	return (VOP_IOCTL(dv->sdev_attrvp, cmd, arg, flag, cred, rvalp));
}

static int
sdev_getattr(struct vnode *vp, struct vattr *vap, int flags, struct cred *cr)
{
	int			error = 0;
	struct sdev_node	*dv = VTOSDEV(vp);
	struct sdev_node	*parent = dv->sdev_dotdot;
	struct devname_nsmap *map = NULL;
	struct devname_ops	*dirops = NULL;
	int (*fn)(devname_handle_t *, struct vattr *, struct cred *);

	ASSERT(parent);

	rw_enter(&parent->sdev_contents, RW_READER);
	ASSERT(dv->sdev_attr || dv->sdev_attrvp);
	if (SDEV_IS_GLOBAL(dv) && (dv->sdev_state != SDEV_ZOMBIE)) {
		map = sdev_get_map(parent, 0);
		dirops = map ? map->dir_ops : NULL;
	}

	/*
	 * search order:
	 * 	- for persistent nodes (SDEV_PERSIST): backstore
	 *	- for non-persistent nodes: module ops if global, then memory
	 */
	if (dv->sdev_attrvp) {
		rw_exit(&parent->sdev_contents);
		error = VOP_GETATTR(dv->sdev_attrvp, vap, flags, cr);
		sdev_vattr_merge(dv, vap);
	} else if (dirops && (fn = dirops->devnops_getattr)) {
		sdev_vattr_merge(dv, vap);
		rw_exit(&parent->sdev_contents);
		error = (*fn)(&(dv->sdev_handle), vap, cr);
	} else {
		ASSERT(dv->sdev_attr);
		*vap = *dv->sdev_attr;
		sdev_vattr_merge(dv, vap);
		rw_exit(&parent->sdev_contents);
	}

	return (error);
}

static int
sdev_setattr(struct vnode *vp, struct vattr *vap, int flags, struct cred *cred)
{
	return (devname_setattr_func(vp, vap, flags, cred, NULL, 0));
}

static int
sdev_getsecattr(struct vnode *vp, struct vsecattr *vsap, int flags,
    struct cred *cr)
{
	int	error;
	struct sdev_node *dv = VTOSDEV(vp);
	struct vnode *avp = dv->sdev_attrvp;

	if (avp == NULL) {
		/* return fs_fab_acl() if flavor matches, else do nothing */
		if ((SDEV_ACL_FLAVOR(vp) == _ACL_ACLENT_ENABLED &&
		    (vsap->vsa_mask & (VSA_ACLCNT | VSA_DFACLCNT))) ||
		    (SDEV_ACL_FLAVOR(vp) == _ACL_ACE_ENABLED &&
		    (vsap->vsa_mask & (VSA_ACECNT | VSA_ACE))))
			return (fs_fab_acl(vp, vsap, flags, cr));

		return (ENOSYS);
	}

	(void) VOP_RWLOCK(avp, 1, NULL);
	error = VOP_GETSECATTR(avp, vsap, flags, cr);
	VOP_RWUNLOCK(avp, 1, NULL);
	return (error);
}

static int
sdev_setsecattr(struct vnode *vp, struct vsecattr *vsap, int flags,
    struct cred *cr)
{
	int	error;
	struct sdev_node *dv = VTOSDEV(vp);
	struct vnode *avp = dv->sdev_attrvp;

	if (dv->sdev_state == SDEV_ZOMBIE)
		return (0);

	if (avp == NULL) {
		if (SDEV_IS_GLOBAL(dv) && !SDEV_IS_PERSIST(dv))
			return (fs_nosys());

		ASSERT(dv->sdev_attr);
		/*
		 * if coming in directly, the acl system call will
		 * have held the read-write lock via VOP_RWLOCK()
		 * If coming in via specfs, specfs will have
		 * held the rw lock on the realvp i.e. us.
		 */
		ASSERT(RW_WRITE_HELD(&dv->sdev_contents));
		sdev_vattr_merge(dv, dv->sdev_attr);
		error =  sdev_shadow_node(dv, cr);
		if (error) {
			return (fs_nosys());
		}

		ASSERT(dv->sdev_attrvp);
		/* clean out the memory copy if any */
		if (dv->sdev_attr) {
			kmem_free(dv->sdev_attr, sizeof (struct vattr));
			dv->sdev_attr = NULL;
		}
		avp = dv->sdev_attrvp;
	}
	ASSERT(avp);

	(void) VOP_RWLOCK(avp, V_WRITELOCK_TRUE, NULL);
	error = VOP_SETSECATTR(avp, vsap, flags, cr);
	VOP_RWUNLOCK(avp, V_WRITELOCK_TRUE, NULL);
	return (error);
}

int
sdev_unlocked_access(void *vdv, int mode, struct cred *cr)
{
	struct sdev_node	*dv = vdv;
	int			shift = 0;
	uid_t			owner = dv->sdev_attr->va_uid;

	if (crgetuid(cr) != owner) {
		shift += 3;
		if (groupmember(dv->sdev_attr->va_gid, cr) == 0)
			shift += 3;
	}

	mode &= ~(dv->sdev_attr->va_mode << shift);
	if (mode == 0)
		return (0);

	return (secpolicy_vnode_access(cr, SDEVTOV(dv), owner, mode));
}

static int
sdev_access(struct vnode *vp, int mode, int flags, struct cred *cr)
{
	struct sdev_node	*dv = VTOSDEV(vp);
	int ret = 0;

	ASSERT(dv->sdev_attr || dv->sdev_attrvp);

	if (dv->sdev_attrvp) {
		ret = VOP_ACCESS(dv->sdev_attrvp, mode, flags, cr);
	} else if (dv->sdev_attr) {
		rw_enter(&dv->sdev_contents, RW_READER);
		ret = sdev_unlocked_access(dv, mode, cr);
		if (ret)
			ret = EACCES;
		rw_exit(&dv->sdev_contents);
	}

	return (ret);
}

/*
 * Lookup
 */
/*ARGSUSED3*/
static int
sdev_lookup(struct vnode *dvp, char *nm, struct vnode **vpp,
    struct pathname *pnp, int flags, struct vnode *rdir, struct cred *cred)
{
	struct sdev_node	*parent;

	parent = VTOSDEV(dvp);
	ASSERT(parent);

	if (!SDEV_IS_GLOBAL(parent))
		return (prof_lookup(dvp, nm, vpp, cred));
	return (devname_lookup_func(parent, nm, vpp, cred, NULL, 0));
}

/*ARGSUSED2*/
static int
sdev_create(struct vnode *dvp, char *nm, struct vattr *vap, vcexcl_t excl,
    int mode, struct vnode **vpp, struct cred *cred, int flag)
{
	struct vnode		*vp = NULL;
	struct vnode		*avp;
	struct sdev_node	*parent;
	struct sdev_node	*self = NULL;
	int			error = 0;
	vtype_t			type = vap->va_type;

	ASSERT(vap->va_type != VNON &&
	    vap->va_type != VBAD);

	if ((type == VFIFO) || (type == VSOCK) ||
	    (type == VPROC) || (type == VPORT))
		return (ENOTSUP);

	parent = VTOSDEV(dvp);
	ASSERT(parent);

	rw_enter(&parent->sdev_dotdot->sdev_contents, RW_READER);
	if (parent->sdev_state == SDEV_ZOMBIE) {
		rw_exit(&parent->sdev_dotdot->sdev_contents);
		return (ENOENT);
	}

	/* non-global do not allow pure node creation */
	if (!SDEV_IS_GLOBAL(parent)) {
		rw_exit(&parent->sdev_dotdot->sdev_contents);
		return (prof_lookup(dvp, nm, vpp, cred));
	}
	rw_exit(&parent->sdev_dotdot->sdev_contents);

again:
	/* check existing name */
	error = VOP_LOOKUP(dvp, nm, &vp, NULL, 0, NULL, cred);

	/* name found */
	if (error == 0) {
		ASSERT(vp);
		if (excl == EXCL) {
			error = EEXIST;
		} else if ((vp->v_type == VDIR) && (mode & VWRITE)) {
			/* allowing create/read-only an existing directory */
			error = EISDIR;
		} else {
			error = VOP_ACCESS(vp, mode, flag, cred);
		}

		if (error) {
			VN_RELE(vp);
			return (error);
		}

		/* truncation first */
		if ((vp->v_type == VREG) && (vap->va_mask & AT_SIZE) &&
		    (vap->va_size == 0)) {
			ASSERT(parent->sdev_attrvp);
			ASSERT(VTOSDEV(vp)->sdev_attrvp);
			error = VOP_CREATE(parent->sdev_attrvp,
			    nm, vap, excl, mode, &avp, cred, flag);

			if (error) {
				VN_RELE(vp);
				return (error);
			}
		}

		sdev_update_timestamps(vp, kcred,
		    AT_CTIME|AT_MTIME|AT_ATIME);
		*vpp = vp;
		return (0);
	}

	/* bail out early */
	if (error != ENOENT)
		return (error);

	/*
	 * For memory-based (ROFS) directory:
	 * 	- either disallow node creation;
	 *	- or implement VOP_CREATE of its own
	 */
	rw_enter(&parent->sdev_contents, RW_WRITER);
	if (!SDEV_IS_PERSIST(parent)) {
		rw_exit(&parent->sdev_contents);
		return (ENOTSUP);
	}
	ASSERT(parent->sdev_attrvp);
	error = sdev_mknode(parent, nm, &self, vap, NULL, NULL,
	    cred, SDEV_READY);
	if (error) {
		rw_exit(&parent->sdev_contents);
		if (self)
			SDEV_RELE(self);
		return (error);
	}
	rw_exit(&parent->sdev_contents);

	ASSERT(self);
	/* take care the timestamps for the node and its parent */
	sdev_update_timestamps(SDEVTOV(self), kcred,
	    AT_CTIME|AT_MTIME|AT_ATIME);
	sdev_update_timestamps(dvp, kcred, AT_MTIME|AT_ATIME);
	if (SDEV_IS_GLOBAL(parent))
		atomic_inc_ulong(&parent->sdev_gdir_gen);

	/* wake up other threads blocked on looking up this node */
	mutex_enter(&self->sdev_lookup_lock);
	SDEV_UNBLOCK_OTHERS(self, SDEV_LOOKUP);
	mutex_exit(&self->sdev_lookup_lock);
	error = sdev_to_vp(self, vpp);
	return (error);
}

static int
sdev_remove(struct vnode *dvp, char *nm, struct cred *cred)
{
	int	error;
	struct sdev_node *parent = (struct sdev_node *)VTOSDEV(dvp);
	struct vnode *vp = NULL;
	struct sdev_node *dv = NULL;
	struct devname_nsmap *map = NULL;
	struct devname_ops *dirops = NULL;
	int (*fn)(devname_handle_t *);
	int len;
	int bkstore = 0;

	/* bail out early */
	len = strlen(nm);
	if (nm[0] == '.') {
		if (len == 1) {
			return (EINVAL);
		} else if (len == 2 && nm[1] == '.') {
			return (EEXIST);
		}
	}

	ASSERT(parent);
	rw_enter(&parent->sdev_contents, RW_READER);
	if (!SDEV_IS_GLOBAL(parent)) {
		rw_exit(&parent->sdev_contents);
		return (ENOTSUP);
	}

	/* check existence first */
	dv = sdev_cache_lookup(parent, nm);
	if (dv == NULL) {
		rw_exit(&parent->sdev_contents);
		return (ENOENT);
	}

	vp = SDEVTOV(dv);
	if ((dv->sdev_state == SDEV_INIT) ||
	    (dv->sdev_state == SDEV_ZOMBIE)) {
		rw_exit(&parent->sdev_contents);
		VN_RELE(vp);
		return (ENOENT);
	}

	/* the module may record/reject removing a device node */
	map = sdev_get_map(parent, 0);
	dirops = map ? map->dir_ops : NULL;
	if (dirops && ((fn = dirops->devnops_remove) != NULL)) {
		error = (*fn)(&(dv->sdev_handle));
		if (error) {
			rw_exit(&parent->sdev_contents);
			VN_RELE(vp);
			return (error);
		}
	}

	/*
	 * sdev_dirdelete does the real job of:
	 *  - make sure no open ref count
	 *  - destroying the sdev_node
	 *  - releasing the hold on attrvp
	 */
	bkstore = SDEV_IS_PERSIST(dv) ? 1 : 0;
	if (!rw_tryupgrade(&parent->sdev_contents)) {
		rw_exit(&parent->sdev_contents);
		rw_enter(&parent->sdev_contents, RW_WRITER);
	}
	error = sdev_cache_update(parent, &dv, nm, SDEV_CACHE_DELETE);
	rw_exit(&parent->sdev_contents);

	sdcmn_err2(("sdev_remove: cache_update error %d\n", error));
	if (error && (error != EBUSY)) {
		/* report errors other than EBUSY */
		VN_RELE(vp);
	} else {
		sdcmn_err2(("sdev_remove: cleaning node %s from cache "
		    " with error %d\n", nm, error));

		/*
		 * best efforts clean up the backing store
		 */
		if (bkstore) {
			ASSERT(parent->sdev_attrvp);
			error = VOP_REMOVE(parent->sdev_attrvp, nm, cred);
			/*
			 * do not report BUSY error
			 * because the backing store ref count is released
			 * when the last ref count on the sdev_node is
			 * released.
			 */
			if (error == EBUSY) {
				sdcmn_err2(("sdev_remove: device %s is still on"
				    "disk %s\n", nm, parent->sdev_path));
				error = 0;
			}
		}

		if (error == EBUSY)
			error = 0;
	}

	return (error);
}

/*
 * Some restrictions for this file system:
 *  - both oldnm and newnm are in the scope of /dev file system,
 *    to simply the namespace management model.
 */
static int
sdev_rename(struct vnode *odvp, char *onm, struct vnode *ndvp, char *nnm,
    struct cred *cred)
{
	struct sdev_node	*fromparent = NULL;
	struct vattr		vattr;
	struct sdev_node	*toparent;
	struct sdev_node	*fromdv = NULL;	/* source node */
	struct vnode 		*ovp;	/* source vnode */
	struct sdev_node	*todv = NULL;	/* destination node */
	struct vnode 		*nvp;		/* destination vnode */
	int			samedir = 0;	/* set if odvp == ndvp */
	struct vnode		*realvp;
	int			len;
	char			nnm_path[MAXPATHLEN];
	struct devname_nsmap 	*omap = NULL;
	struct devname_ops	*odirops = NULL;
	int (*fn)(devname_handle_t *, char *);
	int (*rmfn)(devname_handle_t *);
	int error = 0;
	dev_t fsid;
	int bkstore = 0;

	/* prevent modifying "." and ".." */
	if ((onm[0] == '.' &&
	    (onm[1] == '\0' || (onm[1] == '.' && onm[2] == '\0')))) {
		return (EINVAL);
	}

	fromparent = VTOSDEV(odvp);
	toparent = VTOSDEV(ndvp);

	/* ZOMBIE parent doesn't allow new node creation */
	rw_enter(&fromparent->sdev_dotdot->sdev_contents, RW_READER);
	if (fromparent->sdev_state == SDEV_ZOMBIE) {
		rw_exit(&fromparent->sdev_dotdot->sdev_contents);
		return (ENOENT);
	}

	/* renaming only supported for global device nodes */
	if (!SDEV_IS_GLOBAL(fromparent)) {
		rw_exit(&fromparent->sdev_dotdot->sdev_contents);
		return (ENOTSUP);
	}
	rw_exit(&fromparent->sdev_dotdot->sdev_contents);

	rw_enter(&toparent->sdev_dotdot->sdev_contents, RW_READER);
	if (toparent->sdev_state == SDEV_ZOMBIE) {
		rw_exit(&toparent->sdev_dotdot->sdev_contents);
		return (ENOENT);
	}
	rw_exit(&toparent->sdev_dotdot->sdev_contents);

	/* check existence of the source node */
	error = VOP_LOOKUP(odvp, onm, &ovp, NULL, 0, NULL, cred);
	if (error) {
		sdcmn_err2(("sdev_rename: the source node %s exists\n",
		    onm));
		return (error);
	}

	if (VOP_REALVP(ovp, &realvp) == 0) {
		VN_HOLD(realvp);
		VN_RELE(ovp);
		ovp = realvp;
	}

	/* check existence of destination */
	error = VOP_LOOKUP(ndvp, nnm, &nvp, NULL, 0, NULL, cred);
	if (error && (error != ENOENT)) {
		VN_RELE(ovp);
		return (error);
	}

	if (nvp && (VOP_REALVP(nvp, &realvp) == 0)) {
		VN_HOLD(realvp);
		VN_RELE(nvp);
		nvp = realvp;
	}

	/*
	 * For now, if both exist, they must be the same type.
	 * Changing the type of a node probably needs some special
	 * handling.
	 */
	if (ovp && nvp) {
		if (ovp->v_type != nvp->v_type) {
			VN_RELE(ovp);
			VN_RELE(nvp);
			return (EINVAL);
		}
	}

	/* make sure the source and the destination are in /dev */
	if (odvp != ndvp) {
		vattr.va_mask = AT_FSID;
		if (error = VOP_GETATTR(odvp, &vattr, 0, cred)) {
			VN_RELE(ovp);
			return (error);
		}
		fsid = vattr.va_fsid;
		vattr.va_mask = AT_FSID;
		if (error = VOP_GETATTR(ndvp, &vattr, 0, cred)) {
			VN_RELE(ovp);
			return (error);
		}
		if (fsid != vattr.va_fsid) {
			VN_RELE(ovp);
			return (EXDEV);
		}
	}

	/* make sure the old entry can be deleted */
	error = VOP_ACCESS(odvp, VWRITE, 0, cred);
	if (error) {
		VN_RELE(ovp);
		return (error);
	}

	/* make sure the destination allows creation */
	samedir = (fromparent == toparent);
	if (!samedir) {
		error = VOP_ACCESS(ndvp, VEXEC|VWRITE, 0, cred);
		if (error) {
			VN_RELE(ovp);
			return (error);
		}
	}

	fromdv = VTOSDEV(ovp);
	ASSERT(fromdv);

	/* check with the plug-in modules for the source directory */
	rw_enter(&fromparent->sdev_contents, RW_READER);
	omap = sdev_get_map(fromparent, 0);
	rw_exit(&fromparent->sdev_contents);
	odirops = omap ? omap->dir_ops : NULL;
	if (odirops && ((fn = odirops->devnops_rename) != NULL)) {
		if (samedir) {
			error = (*fn)(&(fromdv->sdev_handle), nnm);
		} else {
			len = strlen(nnm) + strlen(toparent->sdev_name) + 2;
			(void) snprintf(nnm_path, len, "%s/%s",
			    toparent->sdev_name, nnm);
			error = (*fn)(&(fromdv->sdev_handle), nnm);
		}

		if (error) {
			VN_RELE(ovp);
			return (error);
		}
	}

	/*
	 * Remove the destination if exist
	 * On failure, we should attempt to restore the current state
	 * before returning error.
	 */
	if (nvp) {
		switch (nvp->v_type) {
		case VDIR:
			error = VOP_RMDIR(ndvp, nnm, ndvp, cred);
			break;
		default:
			error = VOP_REMOVE(ndvp, nnm, cred);
			break;
		}

		if (error) {
			sdcmn_err2(("sdev_rename: removing existing destination"
			    " %s failed, error %d\n", nnm, error));
			VN_RELE(ovp);
			VN_RELE(nvp);
			return (error);
		}
	}

	/*
	 * link source to new target in the memory
	 */
	error = VOP_LOOKUP(ndvp, nnm, &nvp, NULL, 0, NULL, cred);
	if (error && (error != ENOENT)) {
		VN_RELE(ovp);
		return (error);
	} else if (error == ENOENT) {
		/* make a new node from the old node */
		error = sdev_rnmnode(fromparent, fromdv, toparent, &todv,
		    nnm, cred);
	} else {
		ASSERT(nvp);
		if (VOP_REALVP(nvp, &realvp) == 0) {
			VN_HOLD(realvp);
			VN_RELE(nvp);
			nvp = realvp;
		}

		/* destination file exists */
		todv = VTOSDEV(nvp);
		ASSERT(todv);
		error = sdev_rnmnode(fromparent, fromdv, toparent, &todv,
		    nnm, cred);
		if (error) {
			sdcmn_err2(("sdev_rename: renaming %s to %s failed "
			    " with existing destination error %d\n",
			    onm, nnm, error));
			VN_RELE(nvp);
			VN_RELE(ovp);
			return (error);
		}
	}

	/* unlink from source */
	if (error == 0) {
		/*
		 * check with the plug-in module whether source can be
		 * re-used or not
		 */
		if (odirops && ((rmfn = odirops->devnops_remove) != NULL)) {
			error = (*rmfn)(&(fromdv->sdev_handle));
		}

		if (error == 0) {
			bkstore = SDEV_IS_PERSIST(fromdv) ? 1 : 0;
			rw_enter(&fromparent->sdev_contents, RW_WRITER);
			error = sdev_cache_update(fromparent, &fromdv, onm,
			    SDEV_CACHE_DELETE);
			rw_exit(&fromparent->sdev_contents);

			/* best effforts clean up the backing store */
			if (bkstore) {
				ASSERT(fromparent->sdev_attrvp);
				error = VOP_REMOVE(fromparent->sdev_attrvp,
				    onm, kcred);
				if (error) {
					sdcmn_err2(("sdev_rename: device %s is "
					    "still on disk %s\n", onm,
					    fromparent->sdev_path));
					error = 0;
				}
			}

			if (error == EBUSY) {
				error = 0;
			}
		}
	}

	/* book keeping the ovp v_count */
	if (error) {
		sdcmn_err2(("sdev_rename: renaming %s to %s failed "
		    " with error %d\n", onm, nnm, error));
		VN_RELE(ovp);
	}

	return (error);
}

/*
 * dev-fs version of "ln -s path dev-name"
 *	tnm - path, e.g. /devices/... or /dev/...
 *	lnm - dev_name
 */
static int
sdev_symlink(struct vnode *dvp, char *lnm, struct vattr *tva,
    char *tnm, struct cred *cred)
{
	int error;
	struct vnode *vp = NULL;
	struct sdev_node *parent = (struct sdev_node *)VTOSDEV(dvp);
	struct sdev_node *self = (struct sdev_node *)NULL;

	ASSERT(parent);
	rw_enter(&parent->sdev_dotdot->sdev_contents, RW_READER);
	if (parent->sdev_state == SDEV_ZOMBIE) {
		rw_exit(&parent->sdev_dotdot->sdev_contents);
		sdcmn_err2(("sdev_symlink: parent %s is ZOMBIED \n",
		    parent->sdev_name));
		return (ENOENT);
	}

	if (!SDEV_IS_GLOBAL(parent)) {
		rw_exit(&parent->sdev_dotdot->sdev_contents);
		return (ENOTSUP);
	}
	rw_exit(&parent->sdev_dotdot->sdev_contents);

	/* find existing name */
	error = VOP_LOOKUP(dvp, lnm, &vp, NULL, 0, NULL, cred);
	if (error == 0) {
		ASSERT(vp);
		VN_RELE(vp);
		sdcmn_err2(("sdev_symlink: node %s already exists\n", lnm));
		return (EEXIST);
	}

	if (error != ENOENT) {
		return (error);
	}

	/* put it into memory cache */
	rw_enter(&parent->sdev_contents, RW_WRITER);
	error = sdev_mknode(parent, lnm, &self, tva, NULL, (void *)tnm,
	    cred, SDEV_READY);
	if (error) {
		rw_exit(&parent->sdev_contents);
		sdcmn_err2(("sdev_symlink: node %s creation failed\n", lnm));
		if (self)
			SDEV_RELE(self);

		return (error);
	}
	ASSERT(self && (self->sdev_state == SDEV_READY));
	rw_exit(&parent->sdev_contents);

	/* take care the timestamps for the node and its parent */
	sdev_update_timestamps(SDEVTOV(self), kcred,
	    AT_CTIME|AT_MTIME|AT_ATIME);
	sdev_update_timestamps(dvp, kcred, AT_MTIME|AT_ATIME);
	if (SDEV_IS_GLOBAL(parent))
		atomic_inc_ulong(&parent->sdev_gdir_gen);

	/* wake up other threads blocked on looking up this node */
	mutex_enter(&self->sdev_lookup_lock);
	SDEV_UNBLOCK_OTHERS(self, SDEV_LOOKUP);
	mutex_exit(&self->sdev_lookup_lock);
	SDEV_RELE(self);	/* don't return with vnode held */
	return (0);
}

static int
sdev_mkdir(struct vnode *dvp, char *nm, struct vattr *va, struct vnode **vpp,
    struct cred *cred)
{
	int error;
	struct sdev_node *parent = (struct sdev_node *)VTOSDEV(dvp);
	struct sdev_node *self = NULL;
	struct vnode	*vp = NULL;

	ASSERT(parent && parent->sdev_dotdot);
	rw_enter(&parent->sdev_dotdot->sdev_contents, RW_READER);
	if (parent->sdev_state == SDEV_ZOMBIE) {
		rw_exit(&parent->sdev_dotdot->sdev_contents);
		return (ENOENT);
	}

	/* non-global do not allow pure directory creation */
	if (!SDEV_IS_GLOBAL(parent)) {
		rw_exit(&parent->sdev_dotdot->sdev_contents);
		return (prof_lookup(dvp, nm, vpp, cred));
	}
	rw_exit(&parent->sdev_dotdot->sdev_contents);

	/* find existing name */
	error = VOP_LOOKUP(dvp, nm, &vp, NULL, 0, NULL, cred);
	if (error == 0) {
		VN_RELE(vp);
		return (EEXIST);
	}

	if (error != ENOENT)
		return (error);

	/* put it into memory */
	rw_enter(&parent->sdev_contents, RW_WRITER);
	error = sdev_mknode(parent, nm, &self,
	    va, NULL, NULL, cred, SDEV_READY);
	if (error) {
		rw_exit(&parent->sdev_contents);
		if (self)
			SDEV_RELE(self);
		return (error);
	}
	ASSERT(self && (self->sdev_state == SDEV_READY));
	rw_exit(&parent->sdev_contents);

	/* take care the timestamps for the node and its parent */
	sdev_update_timestamps(SDEVTOV(self), kcred,
	    AT_CTIME|AT_MTIME|AT_ATIME);
	sdev_update_timestamps(dvp, kcred, AT_MTIME|AT_ATIME);
	if (SDEV_IS_GLOBAL(parent))
		atomic_inc_ulong(&parent->sdev_gdir_gen);

	/* wake up other threads blocked on looking up this node */
	mutex_enter(&self->sdev_lookup_lock);
	SDEV_UNBLOCK_OTHERS(self, SDEV_LOOKUP);
	mutex_exit(&self->sdev_lookup_lock);
	*vpp = SDEVTOV(self);
	return (0);
}

/*
 * allowing removing an empty directory under /dev
 */
/*ARGSUSED*/
static int
sdev_rmdir(struct vnode *dvp, char *nm, struct vnode *cdir, struct cred *cred)
{
	int error = 0;
	struct sdev_node *parent = (struct sdev_node *)VTOSDEV(dvp);
	struct sdev_node *self = NULL;
	struct vnode *vp = NULL;

	/* bail out early */
	if (strcmp(nm, ".") == 0)
		return (EINVAL);
	if (strcmp(nm, "..") == 0)
		return (EEXIST); /* should be ENOTEMPTY */

	/* no destruction of non-global node */
	ASSERT(parent && parent->sdev_dotdot);
	rw_enter(&parent->sdev_dotdot->sdev_contents, RW_READER);
	if (!SDEV_IS_GLOBAL(parent)) {
		rw_exit(&parent->sdev_dotdot->sdev_contents);
		return (ENOTSUP);
	}
	rw_exit(&parent->sdev_dotdot->sdev_contents);

	/* check existing name */
	rw_enter(&parent->sdev_contents, RW_WRITER);
	self = sdev_cache_lookup(parent, nm);
	if (self == NULL) {
		rw_exit(&parent->sdev_contents);
		return (ENOENT);
	}

	vp = SDEVTOV(self);
	if ((self->sdev_state == SDEV_INIT) ||
	    (self->sdev_state == SDEV_ZOMBIE)) {
		rw_exit(&parent->sdev_contents);
		VN_RELE(vp);
		return (ENOENT);
	}

	/* some sanity checks */
	if (vp == dvp || vp == cdir) {
		rw_exit(&parent->sdev_contents);
		VN_RELE(vp);
		return (EINVAL);
	}

	if (vp->v_type != VDIR) {
		rw_exit(&parent->sdev_contents);
		VN_RELE(vp);
		return (ENOTDIR);
	}

	if (vn_vfswlock(vp)) {
		rw_exit(&parent->sdev_contents);
		VN_RELE(vp);
		return (EBUSY);
	}

	if (vn_mountedvfs(vp) != NULL) {
		rw_exit(&parent->sdev_contents);
		vn_vfsunlock(vp);
		VN_RELE(vp);
		return (EBUSY);
	}

	self = VTOSDEV(vp);
	/* bail out on a non-empty directory */
	rw_enter(&self->sdev_contents, RW_READER);
	if (self->sdev_nlink > 2) {
		rw_exit(&self->sdev_contents);
		rw_exit(&parent->sdev_contents);
		vn_vfsunlock(vp);
		VN_RELE(vp);
		return (ENOTEMPTY);
	}
	rw_exit(&self->sdev_contents);

	/* unlink it from the directory cache */
	error = sdev_cache_update(parent, &self, nm, SDEV_CACHE_DELETE);
	rw_exit(&parent->sdev_contents);
	vn_vfsunlock(vp);

	if (error && (error != EBUSY)) {
		VN_RELE(vp);
	} else {
		sdcmn_err2(("sdev_rmdir: cleaning node %s from directory "
		    " cache with error %d\n", nm, error));

		/* best effort to clean up the backing store */
		if (SDEV_IS_PERSIST(parent)) {
			ASSERT(parent->sdev_attrvp);
			error = VOP_RMDIR(parent->sdev_attrvp, nm,
			    parent->sdev_attrvp, kcred);
			sdcmn_err2(("sdev_rmdir: cleaning device %s is on"
			    " disk error %d\n", parent->sdev_path, error));
		}

		if (error == EBUSY)
			error = 0;
	}

	return (error);
}

/*
 * read the contents of a symbolic link
 */
static int
sdev_readlink(struct vnode *vp, struct uio *uiop, struct cred *cred)
{
	struct sdev_node *dv;
	int	error = 0;

	ASSERT(vp->v_type == VLNK);

	dv = VTOSDEV(vp);

	if (dv->sdev_attrvp) {
		/* non-NULL attrvp implys a persisted node at READY state */
		return (VOP_READLINK(dv->sdev_attrvp, uiop, cred));
	} else if (dv->sdev_symlink != NULL) {
		/* memory nodes, e.g. local nodes */
		rw_enter(&dv->sdev_contents, RW_READER);
		sdcmn_err2(("sdev_readlink link is %s\n", dv->sdev_symlink));
		error = uiomove(dv->sdev_symlink, strlen(dv->sdev_symlink),
		    UIO_READ, uiop);
		rw_exit(&dv->sdev_contents);
		return (error);
	}

	return (ENOENT);
}

static int
sdev_readdir(struct vnode *dvp, struct uio *uiop, struct cred *cred, int *eofp)
{
	struct sdev_node *parent = VTOSDEV(dvp);

	ASSERT(parent);
	if (!SDEV_IS_GLOBAL(parent))
		prof_filldir(parent);
	return (devname_readdir_func(dvp, uiop, cred, eofp, SDEV_BROWSE));
}

/*ARGSUSED1*/
static void
sdev_inactive(struct vnode *vp, struct cred *cred)
{
	int clean;
	struct sdev_node *dv = VTOSDEV(vp);
	struct sdev_node *ddv = dv->sdev_dotdot;
	struct sdev_node *idv;
	struct sdev_node *prev = NULL;
	int state;
	struct devname_nsmap *map = NULL;
	struct devname_ops	*dirops = NULL;
	void (*fn)(devname_handle_t *, struct cred *) = NULL;

	rw_enter(&ddv->sdev_contents, RW_WRITER);
	state = dv->sdev_state;

	mutex_enter(&vp->v_lock);
	ASSERT(vp->v_count >= 1);

	clean = (vp->v_count == 1) && (state == SDEV_ZOMBIE);

	/*
	 * last ref count on the ZOMBIE node is released.
	 * clean up the sdev_node, and
	 * release the hold on the backing store node so that
	 * the ZOMBIE backing stores also cleaned out.
	 */
	if (clean) {
		ASSERT(ddv);
		if (SDEV_IS_GLOBAL(dv)) {
			map = ddv->sdev_mapinfo;
			dirops = map ? map->dir_ops : NULL;
			if (dirops && (fn = dirops->devnops_inactive))
				(*fn)(&(dv->sdev_handle), cred);
		}

		ddv->sdev_nlink--;
		if (vp->v_type == VDIR) {
			dv->sdev_nlink--;
		}
		for (idv = ddv->sdev_dot; idv && idv != dv;
		    prev = idv, idv = idv->sdev_next);
		ASSERT(idv == dv);
		if (prev == NULL)
			ddv->sdev_dot = dv->sdev_next;
		else
			prev->sdev_next = dv->sdev_next;
		dv->sdev_next = NULL;
		dv->sdev_nlink--;
		--vp->v_count;
		mutex_exit(&vp->v_lock);
		sdev_nodedestroy(dv, 0);
	} else {
		--vp->v_count;
		mutex_exit(&vp->v_lock);
	}
	rw_exit(&ddv->sdev_contents);
}

static int
sdev_fid(struct vnode *vp, struct fid *fidp)
{
	struct sdev_node	*dv = VTOSDEV(vp);
	struct sdev_fid	*sdev_fid;

	if (fidp->fid_len < (sizeof (struct sdev_fid) - sizeof (ushort_t))) {
		fidp->fid_len = sizeof (struct sdev_fid) - sizeof (ushort_t);
		return (ENOSPC);
	}

	sdev_fid = (struct sdev_fid *)fidp;
	bzero(sdev_fid, sizeof (struct sdev_fid));
	sdev_fid->sdevfid_len =
	    (int)sizeof (struct sdev_fid) - sizeof (ushort_t);
	sdev_fid->sdevfid_ino = dv->sdev_ino;

	return (0);
}

/*
 * This pair of routines bracket all VOP_READ, VOP_WRITE
 * and VOP_READDIR requests.  The contents lock stops things
 * moving around while we're looking at them.
 */
static void
sdev_rwlock(struct vnode *vp, int write_flag)
{
	rw_enter(&VTOSDEV(vp)->sdev_contents, write_flag ? RW_WRITER :
		RW_READER);
}

/*ARGSUSED1*/
static void
sdev_rwunlock(struct vnode *vp, int write_flag)
{
	rw_exit(&VTOSDEV(vp)->sdev_contents);
}

/*ARGSUSED1*/
static int
sdev_seek(struct vnode *vp, offset_t ooff, offset_t *noffp)
{
	struct vnode *attrvp = VTOSDEV(vp)->sdev_attrvp;

	ASSERT(vp->v_type != VCHR &&
	    vp->v_type != VBLK && vp->v_type != VLNK);

	if (vp->v_type == VDIR)
		return (fs_seek(vp, ooff, noffp));

	ASSERT(attrvp);
	return (VOP_SEEK(attrvp, ooff, noffp));
}

/*ARGSUSED1*/
static int
sdev_frlock(struct vnode *vp, int cmd, struct flock64 *bfp, int flag,
    offset_t offset, struct flk_callback *flk_cbp, struct cred *cr)
{
	int error;
	struct sdev_node *dv = VTOSDEV(vp);

	ASSERT(dv);
	ASSERT(dv->sdev_attrvp);
	error = VOP_FRLOCK(dv->sdev_attrvp, cmd, bfp, flag, offset,
	    flk_cbp, cr);

	return (error);
}

static int
sdev_setfl(struct vnode *vp, int oflags, int nflags, cred_t *cr)
{
	struct sdev_node *dv = VTOSDEV(vp);
	ASSERT(dv);
	ASSERT(dv->sdev_attrvp);

	return (VOP_SETFL(dv->sdev_attrvp, oflags, nflags, cr));
}

static int
sdev_pathconf(vnode_t *vp, int cmd, ulong_t *valp, cred_t *cr)
{
	switch (cmd) {
	case _PC_ACL_ENABLED:
		*valp = SDEV_ACL_FLAVOR(vp);
		return (0);
	}

	return (fs_pathconf(vp, cmd, valp, cr));
}

vnodeops_t *sdev_vnodeops;

const fs_operation_def_t sdev_vnodeops_tbl[] = {
	VOPNAME_OPEN, sdev_open,
	VOPNAME_CLOSE, sdev_close,
	VOPNAME_READ, sdev_read,
	VOPNAME_WRITE, sdev_write,
	VOPNAME_IOCTL, sdev_ioctl,
	VOPNAME_GETATTR, sdev_getattr,
	VOPNAME_SETATTR, sdev_setattr,
	VOPNAME_ACCESS, sdev_access,
	VOPNAME_LOOKUP, sdev_lookup,
	VOPNAME_CREATE, sdev_create,
	VOPNAME_RENAME, sdev_rename,
	VOPNAME_REMOVE, sdev_remove,
	VOPNAME_MKDIR, sdev_mkdir,
	VOPNAME_RMDIR, sdev_rmdir,
	VOPNAME_READDIR, sdev_readdir,
	VOPNAME_SYMLINK, sdev_symlink,
	VOPNAME_READLINK, sdev_readlink, /* readlink */
	VOPNAME_FSYNC, (fs_generic_func_p) fs_sync,
	VOPNAME_INACTIVE, (fs_generic_func_p)sdev_inactive,
	VOPNAME_FID, sdev_fid,
	VOPNAME_RWLOCK, (fs_generic_func_p)sdev_rwlock,
	VOPNAME_RWUNLOCK, (fs_generic_func_p)sdev_rwunlock,
	VOPNAME_SEEK, sdev_seek,
	VOPNAME_FRLOCK, sdev_frlock,
	VOPNAME_PATHCONF, sdev_pathconf,
	VOPNAME_SETFL, sdev_setfl,
	VOPNAME_SETSECATTR, sdev_setsecattr,	/* setsecattr */
	VOPNAME_GETSECATTR, sdev_getsecattr,	/* getsecattr */
	NULL, NULL
};

int sdev_vnodeops_tbl_size = sizeof (sdev_vnodeops_tbl);
