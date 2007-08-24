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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdio.h>
#include <libzfs.h>
#include <string.h>
#include <strings.h>
#include <libshare.h>
#include "libshare_impl.h"
#include <libintl.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>

extern sa_share_t _sa_add_share(sa_group_t, char *, int, int *);
extern sa_group_t _sa_create_zfs_group(sa_group_t, char *);
extern char *sa_fstype(char *);
extern void set_node_attr(void *, char *, char *);
extern int sa_is_share(void *);

/*
 * File system specific code for ZFS. The original code was stolen
 * from the "zfs" command and modified to better suit this library's
 * usage.
 */

typedef struct get_all_cbdata {
	zfs_handle_t	**cb_handles;
	size_t		cb_alloc;
	size_t		cb_used;
	uint_t		cb_types;
} get_all_cbdata_t;

/*
 * sa_zfs_init(impl_handle)
 *
 * Initialize an access handle into libzfs.  The handle needs to stay
 * around until sa_zfs_fini() in order to maintain the cache of
 * mounts.
 */

int
sa_zfs_init(sa_handle_impl_t impl_handle)
{
	impl_handle->zfs_libhandle = libzfs_init();
	if (impl_handle->zfs_libhandle != NULL) {
		libzfs_print_on_error(impl_handle->zfs_libhandle, B_TRUE);
		return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * sa_zfs_fini(impl_handle)
 *
 * cleanup data structures and the libzfs handle used for accessing
 * zfs file share info.
 */

void
sa_zfs_fini(sa_handle_impl_t impl_handle)
{
	if (impl_handle->zfs_libhandle != NULL) {
		if (impl_handle->zfs_list != NULL) {
			zfs_handle_t **zhp = impl_handle->zfs_list;
			size_t i;

			/*
			 * Contents of zfs_list need to be freed so we
			 * don't lose ZFS handles.
			 */
			for (i = 0; i < impl_handle->zfs_list_count; i++) {
				zfs_close(zhp[i]);
			}
			free(impl_handle->zfs_list);
			impl_handle->zfs_list = NULL;
			impl_handle->zfs_list_count = 0;
		}

		libzfs_fini(impl_handle->zfs_libhandle);
		impl_handle->zfs_libhandle = NULL;
	}
}

/*
 * get_one_filesystem(zfs_handle_t, data)
 *
 * an interator function called while iterating through the ZFS
 * root. It accumulates into an array of file system handles that can
 * be used to derive info about those file systems.
 *
 * Note that as this function is called, we close all zhp handles that
 * are not going to be places into the cp_handles list. We don't want
 * to close the ones we are keeping, but all others would be leaked if
 * not closed here.
 */

static int
get_one_filesystem(zfs_handle_t *zhp, void *data)
{
	get_all_cbdata_t *cbp = data;
	zfs_type_t type = zfs_get_type(zhp);

	/*
	 * Interate over any nested datasets.
	 */
	if (type == ZFS_TYPE_FILESYSTEM &&
	    zfs_iter_filesystems(zhp, get_one_filesystem, data) != 0) {
		zfs_close(zhp);
		return (1);
	}

	/*
	 * Skip any datasets whose type does not match.
	 */
	if ((type & cbp->cb_types) == 0) {
		zfs_close(zhp);
		return (0);
	}

	if (cbp->cb_alloc == cbp->cb_used) {
		zfs_handle_t **handles;

		if (cbp->cb_alloc == 0)
			cbp->cb_alloc = 64;
		else
			cbp->cb_alloc *= 2;

		handles = (zfs_handle_t **)calloc(1,
		    cbp->cb_alloc * sizeof (void *));

		if (handles == NULL) {
			zfs_close(zhp);
			return (0);
		}
		if (cbp->cb_handles) {
			bcopy(cbp->cb_handles, handles,
			    cbp->cb_used * sizeof (void *));
			free(cbp->cb_handles);
		}

		cbp->cb_handles = handles;
	}

	cbp->cb_handles[cbp->cb_used++] = zhp;

	return (0);
}

/*
 * get_all_filesystems(zfs_handle_t ***fslist, size_t *count)
 *
 * iterate through all ZFS file systems starting at the root. Returns
 * a count and an array of handle pointers. Allocating is only done
 * once. The caller does not need to free since it will be done at
 * sa_zfs_fini() time.
 */

static void
get_all_filesystems(sa_handle_impl_t impl_handle,
			zfs_handle_t ***fslist, size_t *count)
{
	get_all_cbdata_t cb = { 0 };
	cb.cb_types = ZFS_TYPE_FILESYSTEM;

	if (impl_handle->zfs_list != NULL) {
		*fslist = impl_handle->zfs_list;
		*count = impl_handle->zfs_list_count;
		return;
	}

	(void) zfs_iter_root(impl_handle->zfs_libhandle,
	    get_one_filesystem, &cb);

	impl_handle->zfs_list = *fslist = cb.cb_handles;
	impl_handle->zfs_list_count = *count = cb.cb_used;
}

/*
 * mountpoint_compare(a, b)
 *
 * compares the mountpoint on two zfs file systems handles.
 * returns values following strcmp() model.
 */

static int
mountpoint_compare(const void *a, const void *b)
{
	zfs_handle_t **za = (zfs_handle_t **)a;
	zfs_handle_t **zb = (zfs_handle_t **)b;
	char mounta[MAXPATHLEN];
	char mountb[MAXPATHLEN];

	verify(zfs_prop_get(*za, ZFS_PROP_MOUNTPOINT, mounta,
	    sizeof (mounta), NULL, NULL, 0, B_FALSE) == 0);
	verify(zfs_prop_get(*zb, ZFS_PROP_MOUNTPOINT, mountb,
	    sizeof (mountb), NULL, NULL, 0, B_FALSE) == 0);

	return (strcmp(mounta, mountb));
}

/*
 * return legacy mountpoint.  Caller provides space for mountpoint.
 */
int
get_legacy_mountpoint(char *path, char *mountpoint, size_t len)
{
	FILE *fp;
	struct mnttab entry;

	if ((fp = fopen(MNTTAB, "r")) == NULL) {
		return (1);
	}

	while (getmntent(fp, &entry) == 0) {

		if (entry.mnt_fstype == NULL ||
		    strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0)
			continue;

		if (strcmp(entry.mnt_mountp, path) == 0) {
			(void) strlcpy(mountpoint, entry.mnt_special, len);
			(void) fclose(fp);
			return (0);
		}
	}
	(void) fclose(fp);
	return (1);
}

/*
 * get_zfs_dataset(impl_handle, path)
 *
 * get the name of the ZFS dataset the path is equivalent to.  The
 * dataset name is used for get/set of ZFS properties since libzfs
 * requires a dataset to do a zfs_open().
 */

static char *
get_zfs_dataset(sa_handle_impl_t impl_handle, char *path,
    boolean_t search_mnttab)
{
	size_t i, count = 0;
	char *dataset = NULL;
	zfs_handle_t **zlist;
	char mountpoint[ZFS_MAXPROPLEN];
	char canmount[ZFS_MAXPROPLEN];

	get_all_filesystems(impl_handle, &zlist, &count);
	qsort(zlist, count, sizeof (void *), mountpoint_compare);
	for (i = 0; i < count; i++) {
		/* must have a mountpoint */
		if (zfs_prop_get(zlist[i], ZFS_PROP_MOUNTPOINT, mountpoint,
		    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) != 0) {
			/* no mountpoint */
			continue;
		}

		/* mountpoint must be a path */
		if (strcmp(mountpoint, ZFS_MOUNTPOINT_NONE) == 0 ||
		    strcmp(mountpoint, ZFS_MOUNTPOINT_LEGACY) == 0) {
			/*
			 * Search mmttab for mountpoint
			 */

			if (search_mnttab == B_TRUE &&
			    get_legacy_mountpoint(path, mountpoint,
			    sizeof (mountpoint)) == 0) {
				dataset = mountpoint;
				break;
			}
			continue;
		}

		/* canmount must be set */
		canmount[0] = '\0';
		if (zfs_prop_get(zlist[i], ZFS_PROP_CANMOUNT, canmount,
		    sizeof (canmount), NULL, NULL, 0, B_FALSE) != 0 ||
		    strcmp(canmount, "off") == 0)
			continue;

		/*
		 * have a mountable handle but want to skip those marked none
		 * and legacy
		 */
		if (strcmp(mountpoint, path) == 0) {
			dataset = (char *)zfs_get_name(zlist[i]);
			break;
		}

	}

	if (dataset != NULL)
		dataset = strdup(dataset);

	return (dataset);
}

/*
 * get_zfs_property(dataset, property)
 *
 * Get the file system property specified from the ZFS dataset.
 */

static char *
get_zfs_property(char *dataset, zfs_prop_t property)
{
	zfs_handle_t *handle = NULL;
	char shareopts[ZFS_MAXPROPLEN];
	libzfs_handle_t *libhandle;

	libhandle = libzfs_init();
	if (libhandle != NULL) {
		handle = zfs_open(libhandle, dataset, ZFS_TYPE_FILESYSTEM);
		if (handle != NULL) {
			if (zfs_prop_get(handle, property, shareopts,
			    sizeof (shareopts), NULL, NULL, 0,
			    B_FALSE) == 0) {
				zfs_close(handle);
				libzfs_fini(libhandle);
				return (strdup(shareopts));
			}
			zfs_close(handle);
		}
		libzfs_fini(libhandle);
	}
	return (NULL);
}

/*
 * sa_zfs_is_shared(handle, path)
 *
 * Check to see if the ZFS path provided has the sharenfs option set
 * or not.
 */

int
sa_zfs_is_shared(sa_handle_t sahandle, char *path)
{
	int ret = 0;
	char *dataset;
	zfs_handle_t *handle = NULL;
	char shareopts[ZFS_MAXPROPLEN];
	libzfs_handle_t *libhandle;

	dataset = get_zfs_dataset((sa_handle_t)sahandle, path, B_FALSE);
	if (dataset != NULL) {
		libhandle = libzfs_init();
		if (libhandle != NULL) {
			handle = zfs_open(libhandle, dataset,
			    ZFS_TYPE_FILESYSTEM);
			if (handle != NULL) {
				if (zfs_prop_get(handle, ZFS_PROP_SHARENFS,
				    shareopts, sizeof (shareopts), NULL, NULL,
				    0, B_FALSE) == 0 &&
				    strcmp(shareopts, "off") != 0) {
					ret = 1; /* it is shared */
				}
				zfs_close(handle);
			}
			libzfs_fini(libhandle);
		}
		free(dataset);
	}
	return (ret);
}

/*
 * find_or_create_group(groupname, proto, *err)
 *
 * While walking the ZFS tree, we need to add shares to a defined
 * group. If the group doesn't exist, create it first, making sure it
 * is marked as a ZFS group.
 *
 * Note that all ZFS shares are in a subgroup of the top level group
 * called "zfs".
 */

static sa_group_t
find_or_create_group(sa_handle_t handle, char *groupname, char *proto, int *err)
{
	sa_group_t group;
	sa_optionset_t optionset;
	int ret = SA_OK;

	/*
	 * we check to see if the "zfs" group exists. Since this
	 * should be the top level group, we don't want the
	 * parent. This is to make sure the zfs group has been created
	 * and to created if it hasn't been.
	 */
	group = sa_get_group(handle, groupname);
	if (group == NULL) {
		group = sa_create_group(handle, groupname, &ret);

		/* make sure this is flagged as a ZFS group */
		if (group != NULL)
			ret = sa_set_group_attr(group, "zfs", "true");
	}
	if (group != NULL) {
		if (proto != NULL) {
			optionset = sa_get_optionset(group, proto);
			if (optionset == NULL) {
				optionset = sa_create_optionset(group, proto);
			} else {
				char **protolist;
				int numprotos, i;
				numprotos = sa_get_protocols(&protolist);
				for (i = 0; i < numprotos; i++) {
					optionset = sa_create_optionset(group,
					    protolist[i]);
				}
				if (protolist != NULL)
					free(protolist);
			}
		}
	}
	if (err != NULL)
		*err = ret;
	return (group);
}

/*
 * find_or_create_zfs_subgroup(groupname, optstring, *err)
 *
 * ZFS shares will be in a subgroup of the "zfs" master group.  This
 * function looks to see if the groupname exists and returns it if it
 * does or else creates a new one with the specified name and returns
 * that.  The "zfs" group will exist before we get here, but we make
 * sure just in case.
 *
 * err must be a valid pointer.
 */

static sa_group_t
find_or_create_zfs_subgroup(sa_handle_t handle, char *groupname,
				char *optstring, int *err)
{
	sa_group_t group = NULL;
	sa_group_t zfs;
	char *name;
	char *options;

	/* start with the top-level "zfs" group */
	zfs = sa_get_group(handle, "zfs");
	*err = SA_OK;
	if (zfs != NULL) {
		for (group = sa_get_sub_group(zfs); group != NULL;
		    group = sa_get_next_group(group)) {
			name = sa_get_group_attr(group, "name");
			if (name != NULL && strcmp(name, groupname) == 0) {
				/* have the group so break out of here */
				sa_free_attr_string(name);
				break;
			}
			if (name != NULL)
				sa_free_attr_string(name);
		}

		if (group == NULL) {
			/*
			 * need to create the sub-group since it doesn't exist
			 */
			group = _sa_create_zfs_group(zfs, groupname);
			if (group != NULL)
				set_node_attr(group, "zfs", "true");
			if (strcmp(optstring, "on") == 0)
				optstring = "rw";
			if (group != NULL) {
				options = strdup(optstring);
				if (options != NULL) {
					*err = sa_parse_legacy_options(group,
					    options, "nfs");
					free(options);
				} else {
					*err = SA_NO_MEMORY;
				}
			}
		}
	}
	return (group);
}

/*
 * zfs_inherited(handle, source, sourcestr)
 *
 * handle case of inherited sharenfs. Pulled out of sa_get_zfs_shares
 * for readability.
 */
static int
zfs_inherited(sa_handle_t handle, sa_share_t share, char *sourcestr,
    char *shareopts, char *mountpoint)
{
	int doshopt = 0;
	int err = SA_OK;
	sa_group_t group;

	/*
	 * Need to find the "real" parent sub-group. It may not be
	 * mounted, but it was identified in the "sourcestr"
	 * variable. The real parent not mounted can occur if
	 * "canmount=off and sharenfs=on".
	 */
	group = find_or_create_zfs_subgroup(handle, sourcestr, shareopts,
	    &doshopt);
	if (group != NULL) {
		share = _sa_add_share(group, mountpoint, SA_SHARE_TRANSIENT,
		    &err);
		/*
		 * some options may only be on shares. If the opt
		 * string contains one of those, we put it just on the
		 * share.
		 */
		if (share != NULL && doshopt == SA_PROP_SHARE_ONLY) {
			char *options;
			options = strdup(shareopts);
			if (options != NULL) {
				err = sa_parse_legacy_options(share, options,
				    "nfs");
				free(options);
			}
		}
	} else {
		err = SA_NO_MEMORY;
	}
	return (err);
}

/*
 * zfs_notinherited()
 *
 * handle case where this is the top of a sub-group in ZFS. Pulled out
 * of sa_get_zfs_shares for readability.
 */
static int
zfs_notinherited(sa_group_t group, char *mountpoint, char *shareopts)
{
	int err = SA_OK;
	sa_share_t share;

	set_node_attr(group, "zfs", "true");
	share = _sa_add_share(group, mountpoint, SA_SHARE_TRANSIENT, &err);
	if (err == SA_OK) {
		if (strcmp(shareopts, "on") != 0) {
			char *options;
			options = strdup(shareopts);
			if (options != NULL) {
				err = sa_parse_legacy_options(group, options,
				    "nfs");
				free(options);
			}
			if (err == SA_PROP_SHARE_ONLY) {
				/*
				 * Same as above, some properties may
				 * only be on shares, but due to the
				 * ZFS sub-groups being artificial, we
				 * sometimes get this and have to deal
				 * with it. We do it by attempting to
				 * put it on the share.
				 */
				options = strdup(shareopts);
				if (options != NULL) {
					err = sa_parse_legacy_options(share,
					    options, "nfs");
					free(options);
				}
			}
			/* unmark the share's changed state */
			set_node_attr(share, "changed", NULL);
		}
	}
	return (err);
}

/*
 * zfs_grp_error(err)
 *
 * Print group create error, but only once. If err is 0 do the
 * print else don't.
 */

static void
zfs_grp_error(int err)
{
	if (err == 0) {
		/* only print error once */
		(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
		    "Cannot create ZFS subgroup during initialization:"
		    " %s\n"), sa_errorstr(SA_SYSTEM_ERR));
	}
}

/*
 * sa_get_zfs_shares(handle, groupname)
 *
 * Walk the mnttab for all zfs mounts and determine which are
 * shared. Find or create the appropriate group/sub-group to contain
 * the shares.
 *
 * All shares are in a sub-group that will hold the properties. This
 * allows representing the inherited property model.
 */

int
sa_get_zfs_shares(sa_handle_t handle, char *groupname)
{
	sa_group_t group;
	sa_group_t zfsgroup;
	int legacy = 0;
	int err;
	zfs_handle_t **zlist;
	char shareopts[ZFS_MAXPROPLEN];
	sa_share_t share;
	zfs_source_t source;
	char sourcestr[ZFS_MAXPROPLEN];
	char mountpoint[ZFS_MAXPROPLEN];
	size_t count = 0, i;
	libzfs_handle_t *zfs_libhandle;

	/*
	 * If we can't access libzfs, don't bother doing anything.
	 */
	zfs_libhandle = ((sa_handle_impl_t)handle)->zfs_libhandle;
	if (zfs_libhandle == NULL)
		return (SA_SYSTEM_ERR);

	zfsgroup = find_or_create_group(handle, groupname, "nfs", &err);
	if (zfsgroup == NULL)
		return (legacy);

	/*
	 * need to walk the mounted ZFS pools and datasets to
	 * find shares that are possible.
	 */
	get_all_filesystems((sa_handle_impl_t)handle, &zlist, &count);
	qsort(zlist, count, sizeof (void *), mountpoint_compare);

	group = zfsgroup;
	for (i = 0; i < count; i++) {
		char *dataset;

		source = ZFS_SRC_ALL;
		/* If no mountpoint, skip. */
		if (zfs_prop_get(zlist[i], ZFS_PROP_MOUNTPOINT,
		    mountpoint, sizeof (mountpoint), NULL, NULL, 0,
		    B_FALSE) != 0)
			continue;

		/*
		 * zfs_get_name value must not be freed. It is just a
		 * pointer to a value in the handle.
		 */
		if ((dataset = (char *)zfs_get_name(zlist[i])) == NULL)
			continue;

		/*
		 * only deal with "mounted" file systems since
		 * unmounted file systems can't actually be shared.
		 */

		if (!zfs_is_mounted(zlist[i], NULL))
			continue;

		if (zfs_prop_get(zlist[i], ZFS_PROP_SHARENFS, shareopts,
		    sizeof (shareopts), &source, sourcestr,
		    ZFS_MAXPROPLEN, B_FALSE) == 0 &&
		    strcmp(shareopts, "off") != 0) {
			/* it is shared so add to list */
			share = sa_find_share(handle, mountpoint);
			err = SA_OK;
			if (share != NULL) {
				/*
				 * A zfs file system had been shared
				 * through traditional methods
				 * (share/dfstab or added to a non-zfs
				 * group.  Now it has been added to a
				 * ZFS group via the zfs
				 * command. Remove from previous
				 * config and setup with current
				 * options.
				 */
				err = sa_remove_share(share);
				share = NULL;
			}
			if (err == SA_OK) {
				if (source & ZFS_SRC_INHERITED) {
					err = zfs_inherited(handle,
					    share, sourcestr,
					    shareopts, mountpoint);
				} else {
					group = _sa_create_zfs_group(
					    zfsgroup, dataset);
					if (group == NULL) {
						static int err = 0;
						/*
						 * there is a problem,
						 * but we can't do
						 * anything about it
						 * at this point so we
						 * issue a warning an
						 * move on.
						 */
						zfs_grp_error(err);
						err = 1;
						continue;
					}
					set_node_attr(group, "zfs",
					    "true");
					/*
					 * Add share with local opts via
					 * zfs_notinherited.
					 */
					err = zfs_notinherited(group,
					    mountpoint, shareopts);
				}
			}
		}
	}
	/*
	 * Don't need to free the "zlist" variable since it is only a
	 * pointer to a cached value that will be freed when
	 * sa_fini() is called.
	 */
	return (legacy);
}

#define	COMMAND		"/usr/sbin/zfs"

/*
 * sa_zfs_set_sharenfs(group, path, on)
 *
 * Update the "sharenfs" property on the path. If on is true, then set
 * to the properties on the group or "on" if no properties are
 * defined. Set to "off" if on is false.
 */

int
sa_zfs_set_sharenfs(sa_group_t group, char *path, int on)
{
	int ret = SA_NOT_IMPLEMENTED;
	char *command;

	command = malloc(ZFS_MAXPROPLEN * 2);
	if (command != NULL) {
		char *opts = NULL;
		char *dataset = NULL;
		FILE *pfile;
		sa_handle_impl_t impl_handle;
		/* for now, NFS is always available for "zfs" */
		if (on) {
			opts = sa_proto_legacy_format("nfs", group, 1);
			if (opts != NULL && strlen(opts) == 0) {
				free(opts);
				opts = strdup("on");
			}
		}

		impl_handle = (sa_handle_impl_t)sa_find_group_handle(group);
		assert(impl_handle != NULL);
		if (impl_handle != NULL)
			dataset = get_zfs_dataset(impl_handle, path, B_FALSE);
		else
			ret = SA_SYSTEM_ERR;

		if (dataset != NULL) {
			(void) snprintf(command, ZFS_MAXPROPLEN * 2,
			    "%s set sharenfs=\"%s\" %s", COMMAND,
			    opts != NULL ? opts : "off", dataset);
			pfile = popen(command, "r");
			if (pfile != NULL) {
				ret = pclose(pfile);
				if (ret != 0)
					ret = SA_SYSTEM_ERR;
			}
		}
		if (opts != NULL)
			free(opts);
		if (dataset != NULL)
			free(dataset);
		free(command);
	}
	return (ret);
}

/*
 * sa_zfs_update(group)
 *
 * call back to ZFS to update the share if necessary.
 * Don't do it if it isn't a real change.
 */
int
sa_zfs_update(sa_group_t group)
{
	sa_optionset_t protopt;
	sa_group_t parent;
	char *command;
	char *optstring;
	int ret = SA_OK;
	int doupdate = 0;
	FILE *pfile;

	if (sa_is_share(group))
		parent = sa_get_parent_group(group);
	else
		parent = group;

	if (parent != NULL) {
		command = malloc(ZFS_MAXPROPLEN * 2);
		if (command == NULL)
			return (SA_NO_MEMORY);

		*command = '\0';
		for (protopt = sa_get_optionset(parent, NULL); protopt != NULL;
		    protopt = sa_get_next_optionset(protopt)) {

			char *proto = sa_get_optionset_attr(protopt, "type");
			char *path;
			char *dataset = NULL;
			char *zfsopts = NULL;

			if (sa_is_share(group)) {
				path = sa_get_share_attr((sa_share_t)group,
				    "path");
				if (path != NULL) {
					sa_handle_impl_t impl_handle;

					impl_handle = sa_find_group_handle(
					    group);
					if (impl_handle != NULL)
						dataset = get_zfs_dataset(
						    impl_handle, path, B_FALSE);
					else
						ret = SA_SYSTEM_ERR;

					sa_free_attr_string(path);
				}
			} else {
				dataset = sa_get_group_attr(group, "name");
			}
			/* update only when there is an optstring found */
			doupdate = 0;
			if (proto != NULL && dataset != NULL) {
				optstring = sa_proto_legacy_format(proto,
				    group, 1);
				zfsopts = get_zfs_property(dataset,
				    ZFS_PROP_SHARENFS);

				if (optstring != NULL && zfsopts != NULL) {
					if (strcmp(optstring, zfsopts) != 0)
						doupdate++;
				}

				if (doupdate) {
					if (optstring != NULL &&
					    strlen(optstring) > 0) {
						(void) snprintf(command,
						    ZFS_MAXPROPLEN * 2,
						    "%s set sharenfs=%s %s",
						    COMMAND,
						    optstring, dataset);
					} else {
						(void) snprintf(command,
						    ZFS_MAXPROPLEN * 2,
						    "%s set sharenfs=on %s",
						    COMMAND,
						    dataset);
					}
					pfile = popen(command, "r");
					if (pfile != NULL)
						ret = pclose(pfile);
					switch (ret) {
					default:
					case 1:
						ret = SA_SYSTEM_ERR;
						break;
					case 2:
						ret = SA_SYNTAX_ERR;
						break;
					case 0:
						break;
					}
				}
				if (optstring != NULL)
					free(optstring);
				if (zfsopts != NULL)
					free(zfsopts);
			}
			if (proto != NULL)
				sa_free_attr_string(proto);
			if (dataset != NULL)
				free(dataset);
		}
		free(command);
	}
	return (ret);
}

/*
 * sa_group_is_zfs(group)
 *
 * Given the group, determine if the zfs attribute is set.
 */

int
sa_group_is_zfs(sa_group_t group)
{
	char *zfs;
	int ret = 0;

	zfs = sa_get_group_attr(group, "zfs");
	if (zfs != NULL) {
		ret = 1;
		sa_free_attr_string(zfs);
	}
	return (ret);
}

/*
 * sa_path_is_zfs(path)
 *
 * Check to see if the file system path represents is of type "zfs".
 */

int
sa_path_is_zfs(char *path)
{
	char *fstype;
	int ret = 0;

	fstype = sa_fstype(path);
	if (fstype != NULL && strcmp(fstype, "zfs") == 0)
		ret = 1;
	if (fstype != NULL)
		sa_free_fstype(fstype);
	return (ret);
}

int
sa_sharetab_fill_zfs(sa_share_t share, share_t *sh, char *proto)
{
	char *path;

	/* Make sure path is valid */

	path = sa_get_share_attr(share, "path");
	if (path != NULL) {
		(void) memset(sh, 0, sizeof (sh));
		(void) sa_fillshare(share, proto, sh);
		sa_free_attr_string(path);
		return (0);
	} else
		return (1);
}

#define	SMAX(i, j)	\
	if ((j) > (i)) { \
		(i) = (j); \
	}

int
sa_share_zfs(sa_share_t share, char *path, share_t *sh,
    void *exportdata, boolean_t on)
{
	libzfs_handle_t *libhandle;
	sa_group_t group;
	sa_handle_t sahandle;
	char *dataset;
	int err = EINVAL;
	int i, j;
	char newpath[MAXPATHLEN];
	char *pathp;

	/*
	 * First find the dataset name
	 */
	if ((group = sa_get_parent_group(share)) == NULL)  {
		return (SA_SYSTEM_ERR);
	}
	if ((sahandle = sa_find_group_handle(group)) == NULL) {
		return (SA_SYSTEM_ERR);
	}

	/*
	 * If get_zfs_dataset fails, see if it is a subdirectory
	 */

	pathp = path;
	while ((dataset = get_zfs_dataset(sahandle, pathp, B_TRUE)) == NULL) {
		char *p;

		if (pathp == path) {
			(void) strlcpy(newpath, path, sizeof (newpath));
			pathp = newpath;
		}

		/*
		 * chop off part of path, but if we are at root then
		 * make sure path is a /
		 */
		if ((strlen(pathp) > 1) && (p = strrchr(pathp, '/'))) {
			if (pathp == p) {
				*(p + 1) = '\0';  /* skip over /, root case */
			} else {
				*p = '\0';
			}
		} else {
			return (SA_SYSTEM_ERR);
		}
	}

	libhandle = libzfs_init();
	if (libhandle != NULL) {

		i = (sh->sh_path ? strlen(sh->sh_path) : 0);
		sh->sh_size = i;

		j = (sh->sh_res ? strlen(sh->sh_res) : 0);
		sh->sh_size += j;
		SMAX(i, j);

		j = (sh->sh_fstype ? strlen(sh->sh_fstype) : 0);
		sh->sh_size += j;
		SMAX(i, j);

		j = (sh->sh_opts ? strlen(sh->sh_opts) : 0);
		sh->sh_size += j;
		SMAX(i, j);

		j = (sh->sh_descr ? strlen(sh->sh_descr) : 0);
		sh->sh_size += j;
		SMAX(i, j);
		err = zfs_deleg_share_nfs(libhandle, dataset, path,
		    exportdata, sh, i, on);
		libzfs_fini(libhandle);
	}
	free(dataset);
	return (err);
}
