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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/cmn_err.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/modctl.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/tiuser.h>
#include <sys/kmem.h>
#include <sys/pathname.h>
#include <sys/zone.h>
#include <sys/tsol/label.h>
#include <sys/tsol/tnet.h>
#include <sys/fs/lofs_node.h>
#include <inet/ip6.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <nfs/nfs.h>
#include <nfs/nfs4.h>
#include <nfs/nfs_clnt.h>
#include <sys/dnlc.h>


int sys_labeling = -1;			/* initially unset */

static kmem_cache_t *tslabel_cache;
ts_label_t *l_admin_low;
ts_label_t *l_admin_high;

uint32_t default_doi = DEFAULT_DOI;

/*
 * Initialize labels infrastructure.
 * This is called during startup() time (before vfs_mntroot) by thread_init().
 * It has to be called early so that the is_system_labeled() function returns
 * the right value when called by the networking code on a diskless boot.
 */
void
label_init(void)
{
	bslabel_t label;

	/*
	 * Use the value of "label_services" within the edition module.
	 * If for some reason label_services is not found, this will
	 * result in the appropriate default -- "off."
	 */
	if (modgetsymvalue("label_services", B_FALSE) != 0)
		sys_labeling = 1;
	else
		sys_labeling = 0;

	tslabel_cache = kmem_cache_create("tslabel_cache", sizeof (ts_label_t),
	    0, NULL, NULL, NULL, NULL, NULL, 0);
	bsllow(&label);
	l_admin_low = labelalloc(&label, default_doi, KM_SLEEP);
	bslhigh(&label);
	l_admin_high = labelalloc(&label, default_doi, KM_SLEEP);
}

/*
 * Allocate new ts_label_t.
 */
ts_label_t *
labelalloc(const bslabel_t *val, uint32_t doi, int flag)
{
	ts_label_t *lab = kmem_cache_alloc(tslabel_cache, flag);

	if (lab != NULL) {
		lab->tsl_ref = 1;
		lab->tsl_doi = doi;
		if (val == NULL)
			bzero(&lab->tsl_label, sizeof (bslabel_t));
		else
			bcopy(val, &lab->tsl_label,  sizeof (bslabel_t));
	}
	return (lab);
}

/*
 * Put a hold on a label structure.
 */
void
label_hold(ts_label_t *lab)
{
	atomic_add_32(&lab->tsl_ref, 1);
}

/*
 * Release previous hold on a label structure.  Free it if refcnt == 0.
 */
void
label_rele(ts_label_t *lab)
{
	if (atomic_add_32_nv(&lab->tsl_ref, -1) == 0)
		kmem_cache_free(tslabel_cache, lab);
}

bslabel_t *
label2bslabel(ts_label_t *lab)
{
	return (&lab->tsl_label);
}


uint32_t
label2doi(ts_label_t *lab)
{
	return (lab->tsl_doi);
}

/*
 * Compare labels. Return 1 if equal, 0 otherwise.
 */
boolean_t
label_equal(const ts_label_t *l1, const ts_label_t *l2)
{
	return ((l1->tsl_doi == l2->tsl_doi) &&
	    blequal(&l1->tsl_label, &l2->tsl_label));
}

/*
 * There's no protocol today to obtain the label from the server.
 * So we rely on conventions: zones, zone names, and zone paths
 * must match across TX servers and their TX clients.  Now use
 * the exported name to find the equivalent local zone and its
 * label.  Caller is responsible for doing a label_rele of the
 * returned ts_label.
 */
ts_label_t *
getflabel_cipso(vfs_t *vfsp)
{
	zone_t	*reszone;
	zone_t	*new_reszone;
	char	*nfspath, *respath;
	refstr_t	*resource_ref;
	boolean_t	treat_abs = B_FALSE;

	if (vfsp->vfs_resource == NULL)
		return (NULL);			/* error */
	resource_ref = vfs_getresource(vfsp);

	nfspath = (char *)refstr_value(resource_ref);
	respath = strchr(nfspath, ':');		/* skip server name */
	if (respath)
		respath++;			/* skip over ":" */
	if (*respath != '/') {
		/* treat path as absolute but it doesn't have leading '/' */
		treat_abs = B_TRUE;
	}

	reszone = zone_find_by_any_path(respath, treat_abs);
	if (reszone == global_zone) {
		refstr_rele(resource_ref);
		label_hold(l_admin_low);
		zone_rele(reszone);
		return (l_admin_low);
	}

	/*
	 * Skip over zonepath (not including "root"), e.g. /zone/internal
	 */
	respath += reszone->zone_rootpathlen - 7;
	if (treat_abs)
		respath--;			/* no leading '/' to skip */
	if (strncmp(respath, "/root/", 6) == 0) {
		/* Check if we now have something like "/zone/public/" */

		respath += 5;			/* skip "/root" first */
		new_reszone = zone_find_by_any_path(respath, B_FALSE);
		if (new_reszone != global_zone) {
			zone_rele(reszone);
			reszone = new_reszone;
		} else {
			zone_rele(new_reszone);
		}
	}

	refstr_rele(resource_ref);
	label_hold(reszone->zone_slabel);
	zone_rele(reszone);

	return (reszone->zone_slabel);
}

static ts_label_t *
getflabel_nfs(vfs_t *vfsp)
{
	bslabel_t	*server_sl;
	ts_label_t	*srv_label;
	tsol_tpc_t	*tp;
	int		addr_type;
	void		*ipaddr;
	struct servinfo *svp;
	struct netbuf	*addr;
	struct knetconfig *knconf;
	mntinfo_t	*mi;

	mi = VFTOMI(vfsp);
	svp = mi->mi_curr_serv;
	addr = &svp->sv_addr;
	knconf = svp->sv_knconf;

	if (strcmp(knconf->knc_protofmly, NC_INET) == 0) {
		addr_type = IPV4_VERSION;
		/* LINTED: following cast to ipaddr is OK */
		ipaddr = &((struct sockaddr_in *)addr->buf)->sin_addr;
	} else if (strcmp(knconf->knc_protofmly, NC_INET6) == 0) {
		addr_type = IPV6_VERSION;
		/* LINTED: following cast to ipaddr is OK */
		ipaddr = &((struct sockaddr_in6 *)addr->buf)->sin6_addr;
	} else {
		goto errout;
	}

	tp = find_tpc(ipaddr, addr_type, B_FALSE);
	if (tp == NULL)
		goto errout;

	if (tp->tpc_tp.host_type == SUN_CIPSO) {
		TPC_RELE(tp);
		return (getflabel_cipso(vfsp));
	}

	if (tp->tpc_tp.host_type != UNLABELED)
		goto errout;

	server_sl = &tp->tpc_tp.tp_def_label;
	srv_label = labelalloc(server_sl, default_doi, KM_SLEEP);

	TPC_RELE(tp);

	return (srv_label);

errout:
	return (NULL);
}

/*
 * getflabel -
 *
 * Return pointer to the ts_label associated with the specified file,
 * or returns NULL if error occurs.  Caller is responsible for doing
 * a label_rele of the ts_label.
 */
ts_label_t *
getflabel(vnode_t *vp)
{
	vfs_t		*vfsp, *rvfsp;
	vnode_t		*rvp, *rvp2;
	zone_t		*zone;
	ts_label_t	*zl;
	boolean_t	vfs_is_held = B_FALSE;
	char		vpath[MAXPATHLEN];

	ASSERT(vp);
	vfsp = vp->v_vfsp;
	if (vfsp == NULL)
		return (NULL);

	rvp = vp;

	/*
	 * Traverse lofs mounts and fattach'es to get the real vnode
	 */
	if (VOP_REALVP(rvp, &rvp2) == 0)
		rvp = rvp2;

	rvfsp = rvp->v_vfsp;

	/* rvp/rvfsp now represent the real vnode/vfs we will be using */

	/* Go elsewhere to handle all nfs files. */
	if (strncmp(vfssw[rvfsp->vfs_fstype].vsw_name, "nfs", 3) == 0)
		return (getflabel_nfs(rvfsp));

	/*
	 * Fast path, for objects in a labeled zone: everything except
	 * for lofs/nfs will be just the label of that zone.
	 */
	if ((rvfsp->vfs_zone != NULL) && (rvfsp->vfs_zone != global_zone)) {
		if ((strcmp(vfssw[rvfsp->vfs_fstype].vsw_name,
		    "lofs") != 0)) {
			zone = rvfsp->vfs_zone;
			zone_hold(zone);
			goto zone_out;		/* return this label */
		}
	}

	if (vnodetopath(rootdir, rvp, vpath, sizeof (vpath), kcred) != 0) {
		return (NULL);
	}

	/*
	 * Sanity check - vpath may be weird for some cases, like devices.
	 */
	if (*vpath != '/') {
		zone = curproc->p_zone;
		zone_hold(zone);
		goto zone_out;
	}

	VFS_HOLD(vfsp);
	vfs_is_held = B_TRUE;

	zone = zone_find_by_any_path(vpath, B_FALSE);

	/*
	 * If the vnode source zone is properly set to a non-global zone, or
	 * any zone if the mount is R/W, then use the label of that zone.
	 */
	if ((zone != global_zone) || ((vfsp->vfs_flag & VFS_RDONLY) != 0))
		goto zone_out;		/* return this label */

	/*
	 * Otherwise, if we're not in the global zone, use the label of
	 * our zone.
	 */
	if ((zone = curproc->p_zone) != global_zone) {
		zone_hold(zone);
		goto zone_out;		/* return this label */
	}

	/*
	 * We're in the global zone and the mount is R/W ... so the file
	 * may actually be in the global zone -- or in the root of any zone.
	 * Always build our own path for the file, to be sure it's simplified
	 * (i.e., no ".", "..", "//", and so on).
	 */

	zone_rele(zone);
	zone = zone_find_by_any_path(vpath, B_FALSE);

zone_out:
	if ((curproc->p_zone == global_zone) && (zone == global_zone)) {
		vfs_t		*nvfs;
		boolean_t	exported = B_FALSE;
		refstr_t	*mntpt_ref;
		char		*mntpt;

		/*
		 * File is in the global zone - check whether it's admin_high.
		 * If it's in a filesys that was exported from the global zone,
		 * it's admin_low by definition.  Otherwise, if it's in a
		 * filesys that's NOT exported to any zone, it's admin_high.
		 *
		 * And for these files if there wasn't a valid mount resource,
		 * the file must be admin_high (not exported, probably a global
		 * zone device).
		 */
		if (!vfs_is_held)
			goto out_high;

		mntpt_ref = vfs_getmntpoint(vfsp);
		mntpt = (char *)refstr_value(mntpt_ref);

		if ((mntpt != NULL) && (*mntpt == '/')) {
			zone_t	*to_zone;

			to_zone = zone_find_by_any_path(mntpt, B_FALSE);
			zone_rele(to_zone);
			if (to_zone != global_zone) {
				/* force admin_low */
				exported = B_TRUE;
			}
		}
		if (mntpt_ref)
			refstr_rele(mntpt_ref);

		if (!exported) {
			size_t	plen = strlen(vpath);

			vfs_list_read_lock();
			nvfs = vfsp->vfs_next;
			while (nvfs != vfsp) {
				const char	*rstr;
				size_t		rlen = 0;

				rstr = refstr_value(nvfs->vfs_resource);
				if (rstr != NULL)
					rlen = strlen(rstr);

				/*
				 * Check for a match: does this vfs correspond
				 * to our global zone file path?  I.e., check
				 * if the resource string of this vfs is a
				 * prefix of our path.
				 */
				if ((rlen > 0) && (rlen <= plen) &&
				    (strncmp(rstr, vpath, rlen) == 0) &&
				    (vpath[rlen] == '/' ||
				    vpath[rlen] == '\0')) {
					/* force admin_low */
					exported = B_TRUE;
					break;
				}
				nvfs = nvfs->vfs_next;
			}
			vfs_list_unlock();
		}

		if (!exported)
			goto out_high;
	}

	if (vfs_is_held)
		VFS_RELE(vfsp);

	/*
	 * Now that we have the "home" zone for the file, return the slabel
	 * of that zone.
	 */
	zl = zone->zone_slabel;
	label_hold(zl);
	zone_rele(zone);
	return (zl);

out_high:
	if (vfs_is_held)
		VFS_RELE(vfsp);

	label_hold(l_admin_high);
	zone_rele(zone);
	return (l_admin_high);
}

static int
cgetlabel(bslabel_t *label_p, vnode_t *vp)
{
	ts_label_t	*tsl;
	int		error = 0;

	if ((tsl = getflabel(vp)) == NULL)
		return (EIO);

	if (copyout((caddr_t)label2bslabel(tsl), (caddr_t)label_p,
	    sizeof (*(label_p))) != 0)
		error = EFAULT;

	label_rele(tsl);
	return (error);
}

/*
 * fgetlabel(2TSOL) - get file label
 * getlabel(2TSOL) - get file label
 */
int
getlabel(const char *path, bslabel_t *label_p)
{
	struct		vnode	*vp;
	char		*spath;
	int		error;

	/* Sanity check arguments */
	if (path == NULL)
		return (set_errno(EINVAL));

	spath = kmem_zalloc(MAXPATHLEN, KM_SLEEP);
	if ((error = copyinstr(path, spath, MAXPATHLEN, NULL)) != 0) {
		kmem_free(spath, MAXPATHLEN);
		return (set_errno(error));
	}

	if (error = lookupname(spath, UIO_SYSSPACE, FOLLOW, NULLVPP, &vp)) {
		kmem_free(spath, MAXPATHLEN);
		return (set_errno(error));
	}
	kmem_free(spath, MAXPATHLEN);

	error = cgetlabel(label_p, vp);

	VN_RELE(vp);
	if (error != 0)
		return (set_errno(error));
	else
		return (0);
}

int
fgetlabel(int fd, bslabel_t *label_p)
{
	file_t		*fp;
	int		error;

	if ((fp = getf(fd)) == NULL)
		return (set_errno(EBADF));

	error = cgetlabel(label_p, fp->f_vnode);
	releasef(fd);

	if (error != 0)
		return (set_errno(error));
	else
		return (0);
}

/*
 * Used by NFSv4 to query label of a pathname
 * component during lookup/access ops.
 */
ts_label_t *
nfs4_getflabel(vnode_t *vp)
{
	zone_t *zone;
	ts_label_t *zone_label;
	char path[MAXNAMELEN];
	vnode_t *pvp, *tvp;

	mutex_enter(&vp->v_lock);
	/*
	 * mount traverse has been done by caller
	 * before calling this routine.
	 */
	ASSERT(!vn_ismntpt(vp));
	if (vp->v_path != NULL) {
		zone = zone_find_by_any_path(vp->v_path, B_FALSE);
		mutex_exit(&vp->v_lock);
	} else {
		/*
		 * v_path not cached. Since we rely on path
		 * of an obj to get its label, we need to get
		 * path corresponding to the parent vnode.
		 */
		tvp = vp;
		do {
			mutex_exit(&tvp->v_lock);
			if ((pvp = dnlc_reverse_lookup(tvp, path,
			    sizeof (path))) == NULL)
				return (NULL);
			mutex_enter(&pvp->v_lock);
			tvp = pvp;
		} while (pvp->v_path == NULL);
		zone = zone_find_by_any_path(pvp->v_path, B_FALSE);
		mutex_exit(&pvp->v_lock);
	}
	/*
	 * Caller has verified that the file is either
	 * exported or visible. So if the path falls in
	 * global zone, admin_low is returned; otherwise
	 * the zone's label is returned.
	 */
	zone_label = zone->zone_slabel;
	label_hold(zone_label);
	zone_rele(zone);
	return (zone_label);
}
