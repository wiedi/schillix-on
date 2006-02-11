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

#include <assert.h>
#include <alloca.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/pci.h>
#include <sys/pcie.h>
#include <sys/fm/protocol.h>
#include <fm/topo_mod.h>
#include <libdevinfo.h>
#include <topo_error.h>

#include "hostbridge.h"
#include "pcibus.h"
#include "did.h"
#include "did_props.h"

static int ASRU_set(tnode_t *, did_t *,
    const char *, const char *, const char *);
static int FRU_set(tnode_t *, did_t *,
    const char *, const char *, const char *);
static int DEVprop_set(tnode_t *, did_t *,
    const char *, const char *, const char *);
static int DRIVERprop_set(tnode_t *, did_t *,
    const char *, const char *, const char *);
static int EXCAP_set(tnode_t *, did_t *,
    const char *, const char *, const char *);
static int label_set(tnode_t *, did_t *,
    const char *, const char *, const char *);
static int maybe_di_chars_copy(tnode_t *, did_t *,
    const char *, const char *, const char *);
static int maybe_di_uint_to_str(tnode_t *, did_t *,
    const char *, const char *, const char *);

/*
 * Arrays of "property translation routines" to set the properties a
 * given type of topology node should have.
 *
 * Note that the label_set translation *MUST COME BEFORE* the FRU
 * translation.  For the near term we're setting the FRU fmri to
 * be a legacy-hc style FMRI based on the label, so the label needs
 * to have been set before we do the FRU translation.
 *
 */

txprop_t Fn_common_props[] = {
	{ NULL, TOPO_PGROUP_IO, TOPO_PROP_DEV,
	    TOPO_STABILITY_PRIVATE, DEVprop_set },
	{ DI_DEVTYPPROP, TOPO_PGROUP_IO, TOPO_PROP_DEVTYPE,
	    TOPO_STABILITY_PRIVATE, maybe_di_chars_copy },
	{ DI_DEVIDPROP, TOPO_PGROUP_PCI, TOPO_PROP_DEVID,
	    TOPO_STABILITY_PRIVATE, maybe_di_uint_to_str },
	{ NULL, TOPO_PGROUP_IO, TOPO_PROP_DRIVER,
	    TOPO_STABILITY_PRIVATE, DRIVERprop_set },
	{ NULL, TOPO_PGROUP_PCI, TOPO_PROP_EXCAP,
	    TOPO_STABILITY_PRIVATE, EXCAP_set },
	{ DI_VENDIDPROP, TOPO_PGROUP_PCI, TOPO_PROP_VENDID,
	    TOPO_STABILITY_PRIVATE, maybe_di_uint_to_str },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_LABEL,
	    TOPO_STABILITY_PRIVATE, label_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_FRU,
	    TOPO_STABILITY_PRIVATE, FRU_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_ASRU,
	    TOPO_STABILITY_PRIVATE, ASRU_set }
};

txprop_t Dev_common_props[] = {
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_LABEL,
	    TOPO_STABILITY_PRIVATE, label_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_FRU,
	    TOPO_STABILITY_PRIVATE, FRU_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_ASRU,
	    TOPO_STABILITY_PRIVATE, ASRU_set }
};

txprop_t Bus_common_props[] = {
	{ DI_DEVTYPPROP, TOPO_PGROUP_IO, TOPO_PROP_DEVTYPE,
	    TOPO_STABILITY_PRIVATE, maybe_di_chars_copy },
	{ NULL, TOPO_PGROUP_IO, TOPO_PROP_DRIVER,
	    TOPO_STABILITY_PRIVATE, DRIVERprop_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_LABEL,
	    TOPO_STABILITY_PRIVATE, label_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_FRU,
	    TOPO_STABILITY_PRIVATE, FRU_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_ASRU,
	    TOPO_STABILITY_PRIVATE, ASRU_set }
};

txprop_t RC_common_props[] = {
	{ NULL, TOPO_PGROUP_IO, TOPO_PROP_DEV,
	    TOPO_STABILITY_PRIVATE, DEVprop_set },
	{ DI_DEVTYPPROP, TOPO_PGROUP_IO, TOPO_PROP_DEVTYPE,
	    TOPO_STABILITY_PRIVATE, maybe_di_chars_copy },
	{ NULL, TOPO_PGROUP_IO, TOPO_PROP_DRIVER,
	    TOPO_STABILITY_PRIVATE, DRIVERprop_set },
	{ NULL, TOPO_PGROUP_PCI, TOPO_PROP_EXCAP,
	    TOPO_STABILITY_PRIVATE, EXCAP_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_LABEL,
	    TOPO_STABILITY_PRIVATE, label_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_FRU,
	    TOPO_STABILITY_PRIVATE, FRU_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_ASRU,
	    TOPO_STABILITY_PRIVATE, ASRU_set }
};

txprop_t ExHB_common_props[] = {
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_LABEL,
	    TOPO_STABILITY_PRIVATE, label_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_FRU,
	    TOPO_STABILITY_PRIVATE, FRU_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_ASRU,
	    TOPO_STABILITY_PRIVATE, ASRU_set }
};

txprop_t IOB_common_props[] = {
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_LABEL,
	    TOPO_STABILITY_PRIVATE, label_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_FRU,
	    TOPO_STABILITY_PRIVATE, FRU_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_ASRU,
	    TOPO_STABILITY_PRIVATE, ASRU_set }
};

txprop_t HB_common_props[] = {
	{ NULL, TOPO_PGROUP_IO, TOPO_PROP_DEV,
	    TOPO_STABILITY_PRIVATE, DEVprop_set },
	{ NULL, TOPO_PGROUP_IO, TOPO_PROP_DRIVER,
	    TOPO_STABILITY_PRIVATE, DRIVERprop_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_LABEL,
	    TOPO_STABILITY_PRIVATE, label_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_FRU,
	    TOPO_STABILITY_PRIVATE, FRU_set },
	{ NULL, TOPO_PGROUP_PROTOCOL, TOPO_PROP_ASRU,
	    TOPO_STABILITY_PRIVATE, ASRU_set }
};

int Bus_propcnt = sizeof (Bus_common_props) / sizeof (txprop_t);
int Dev_propcnt = sizeof (Dev_common_props) / sizeof (txprop_t);
int ExHB_propcnt = sizeof (ExHB_common_props) / sizeof (txprop_t);
int HB_propcnt = sizeof (HB_common_props) / sizeof (txprop_t);
int IOB_propcnt = sizeof (IOB_common_props) / sizeof (txprop_t);
int RC_propcnt = sizeof (RC_common_props) / sizeof (txprop_t);
int Fn_propcnt = sizeof (Fn_common_props) / sizeof (txprop_t);

/*
 * If this devinfo node came originally from OBP data, we'll have prom
 * properties associated with the node where we can find properties of
 * interest.  We ignore anything after the the first four bytes of the
 * property, and interpet those first four bytes as our unsigned
 * integer.  If we don't find the property or it's not large enough,
 * 'val' will remained unchanged and we'll return -1.  Otherwise 'val'
 * gets updated with the property value and we return 0.
 */
static int
promprop2uint(di_node_t n, const char *propnm, uint_t *val)
{
	di_prom_prop_t pp = DI_PROM_PROP_NIL;
	uchar_t *buf;

	while ((pp = di_prom_prop_next(Promtree, n, pp)) != DI_PROM_PROP_NIL) {
		if (strcmp(di_prom_prop_name(pp), propnm) == 0) {
			if (di_prom_prop_data(pp, &buf) < sizeof (uint_t))
				continue;
			bcopy(buf, val, sizeof (uint_t));
			return (0);
		}
	}
	return (-1);
}

/*
 * If this devinfo node was added by the PCI hotplug framework it
 * doesn't have the PROM properties, but hopefully has the properties
 * we're looking for attached directly to the devinfo node.  We only
 * care about the first four bytes of the property, which we read as
 * our unsigned integer.  The remaining bytes are ignored.  If we
 * don't find the property we're looking for, or can't get its value,
 * 'val' remains unchanged and we return -1.  Otherwise 'val' gets the
 * property value and we return 0.
 */
static int
hwprop2uint(di_node_t n, const char *propnm, uint_t *val)
{
	di_prop_t hp = DI_PROP_NIL;
	uchar_t *buf;

	while ((hp = di_prop_next(n, hp)) != DI_PROP_NIL) {
		if (strcmp(di_prop_name(hp), propnm) == 0) {
			if (di_prop_bytes(hp, &buf) < sizeof (uint_t))
				continue;
			bcopy(buf, val, sizeof (uint_t));
			return (0);
		}
	}
	return (-1);
}

int
di_uintprop_get(di_node_t n, const char *pnm, uint_t *pv)
{
	if (hwprop2uint(n, pnm, pv) < 0)
		if (promprop2uint(n, pnm, pv) < 0)
			return (-1);
	return (0);
}

int
di_bytes_get(di_node_t n, const char *pnm, int *sz, uchar_t **db)
{
	di_prom_prop_t pp = DI_PROM_PROP_NIL;
	di_prop_t hp = DI_PROP_NIL;

	*sz = -1;
	while ((hp = di_prop_next(n, hp)) != DI_PROP_NIL) {
		if (strcmp(di_prop_name(hp), pnm) == 0) {
			if ((*sz = di_prop_bytes(hp, db)) < 0)
				continue;
			break;
		}
	}
	if (*sz < 0) {
		while ((pp = di_prom_prop_next(Promtree, n, pp)) !=
		    DI_PROM_PROP_NIL) {
			if (strcmp(di_prom_prop_name(pp), pnm) == 0) {
				*sz = di_prom_prop_data(pp, db);
				if (*sz < 0)
					continue;
				break;
			}
		}
	}
	if (*sz < 0)
		return (-1);
	return (0);
}

/*
 * fix_dev_prop -- sometimes di_devfs_path() doesn't tell the whole
 * story, leaving off the device and function number.  Chances are if
 * devfs doesn't put these on then we'll never see this device as an
 * error detector called out in an ereport.  Unfortunately, there are
 * races and we sometimes do get ereports from devices that devfs
 * decides aren't there.  For example, the error injector card seems
 * to bounce in and out of existence according to devfs.  We tack on
 * the missing dev and fn here so that the DEV property used to look
 * up the topology node is correct.
 */
static char *
dev_path_fix(topo_mod_t *mp, char *path, int devno, int fnno)
{
	char *lastslash;
	char *newpath;
	int need;

	/*
	 * We only care about the last component of the dev path. If
	 * we don't find a slash, something is weird.
	 */
	lastslash = strrchr(path, '/');
	assert(lastslash != NULL);

	/*
	 * If an @ sign is present in the last component, the
	 * di_devfs_path() result had the device,fn unit-address.
	 * In that case there's nothing we need do.
	 */
	if (strchr(lastslash, '@') != NULL)
		return (path);

	if (fnno == 0)
		need = snprintf(NULL, 0, "%s@%x", path, devno);
	else
		need = snprintf(NULL, 0, "%s@%x,%x", path, devno, fnno);
	need++;

	if ((newpath = topo_mod_alloc(mp, need)) == NULL) {
		topo_mod_strfree(mp, path);
		return (NULL);
	}

	if (fnno == 0)
		(void) snprintf(newpath, need, "%s@%x", path, devno);
	else
		(void) snprintf(newpath, need, "%s@%x,%x", path, devno, fnno);

	topo_mod_strfree(mp, path);
	return (newpath);
}

/*
 * dev_for_hostbridge() -- For hostbridges we truncate the devfs path
 * after the first element in the bus address.
 */
static char *
dev_for_hostbridge(topo_mod_t *mp, char *path)
{
	char *lastslash;
	char *newpath;
	char *comma;

	/*
	 * We only care about the last component of the dev path. If
	 * we don't find a slash, something is weird.
	 */
	lastslash = strrchr(path, '/');
	assert(lastslash != NULL);

	/*
	 * Find the comma in the last component component@x,y, and
	 * truncate the comma and any following number.
	 */
	comma = strchr(lastslash, ',');
	assert(comma != NULL);

	*comma = '\0';
	if ((newpath = topo_mod_strdup(mp, path)) == NULL)
		return (path);
	*comma = ',';
	topo_mod_strfree(mp, path);
	return (newpath);
}

/*ARGSUSED*/
static int
ASRU_set(tnode_t *tn, did_t *pd,
    const char *dpnm, const char *tpgrp, const char *tpnm)
{
	topo_mod_t *mp;
	nvlist_t *fmri;
	char *nm;
	int e;

	/*
	 * If this topology node represents a device, and that device
	 * implements a slot, set the ASRU to be the resource describing
	 * this topology node.  Otherwise, inherit our parent's ASRU value.
	 */
	mp = did_mod(pd);
	nm = topo_node_name(tn);
	if (strcmp(nm, PCI_DEVICE) == 0 || strcmp(nm, PCIEX_DEVICE) == 0) {
		if (did_label(pd, topo_node_instance(tn)) != NULL) {
			if (topo_prop_get_fmri(tn, TOPO_PGROUP_PROTOCOL,
			    TOPO_PROP_RESOURCE, &fmri, &e) < 0)
				return (topo_mod_seterrno(mp, e));
			if (topo_node_asru_set(tn, fmri, 0, &e) < 0) {
				nvlist_free(fmri);
				return (topo_mod_seterrno(mp, e));
			}
			nvlist_free(fmri);
			return (0);
		}
	}
	if (topo_node_asru_set(tn, NULL, 0, &e) < 0)
		if (e != ETOPO_PROP_NOENT)
			return (topo_mod_seterrno(mp, e));
	return (0);
}

/*
 * Hopefully this hack routine goes away when fmdump can print the labels.
 */
static int
FRU_fmri_hack(topo_mod_t *mp, tnode_t *tn, const char *label)
{
	topo_hdl_t *hp;
	char buf[PATH_MAX];
	nvlist_t *fmri;
	int err, e;

	hp = topo_mod_handle(mp);

	(void) snprintf(buf, PATH_MAX, "hc:///component=%s", label);
	if (topo_fmri_str2nvl(hp, buf, &fmri, &err) < 0)
		return (topo_mod_seterrno(mp, err));

	e = topo_node_fru_set(tn, fmri, 0, &err);
	nvlist_free(fmri);
	if (e < 0)
		return (topo_mod_seterrno(mp, err));
	return (0);
}

/*ARGSUSED*/
static int
FRU_set(tnode_t *tn, did_t *pd,
    const char *dpnm, const char *tpgrp, const char *tpnm)
{
	topo_mod_t *mp;
	char *label, *nm;
	int e;

	nm = topo_node_name(tn);
	mp = did_mod(pd);

	/*
	 * If this topology node represents something other than an
	 * ioboard or a device that implements a slot, inherit the
	 * parent's FRU value.  If there is no label, inherit our
	 * parent's FRU value.  Otherwise, munge up an fmri based on
	 * the label.
	 */
	if (strcmp(nm, "ioboard") != 0 && strcmp(nm, PCI_DEVICE) != 0 &&
	    strcmp(nm, PCIEX_DEVICE) != 0) {
		if (topo_node_fru_set(tn, NULL, 0, &e) < 0) {
			if (e != ETOPO_PROP_NOENT)
				return (topo_mod_seterrno(mp, e));
		}
		return (0);
	}

	if (topo_prop_get_string(tn,
	    TOPO_PGROUP_PROTOCOL, TOPO_PROP_LABEL, &label, &e) < 0) {
		if (e != ETOPO_PROP_NOENT)
			return (topo_mod_seterrno(mp, e));
		if (topo_node_fru_set(tn, NULL, 0, &e) < 0) {
			if (e != ETOPO_PROP_NOENT)
				return (topo_mod_seterrno(mp, e));
		}
		return (0);
	}
	e = FRU_fmri_hack(mp, tn, label);
	topo_mod_strfree(mp, label);
	return (e);
}

/*ARGSUSED*/
static int
label_set(tnode_t *tn, did_t *pd,
    const char *dpnm, const char *tpgrp, const char *tpnm)
{
	topo_mod_t *mp;
	nvlist_t *in, *out;
	char *label;
	int err;

	mp = did_mod(pd);
	if (topo_mod_nvalloc(mp, &in, NV_UNIQUE_NAME) != 0)
		return (topo_mod_seterrno(mp, EMOD_FMRI_NVL));
	if (nvlist_add_uint64(in, TOPO_METH_LABEL_ARG_NVL, (uint64_t)pd) != 0) {
		nvlist_free(in);
		return (topo_mod_seterrno(mp, EMOD_NOMEM));
	}
	if (topo_method_invoke(tn,
	    TOPO_METH_LABEL, TOPO_METH_LABEL_VERSION, in, &out, &err) != 0) {
		nvlist_free(in);
		return (topo_mod_seterrno(mp, err));
	}
	nvlist_free(in);
	if (out != NULL &&
	    nvlist_lookup_string(out, TOPO_METH_LABEL_RET_STR, &label) == 0) {
		if (topo_prop_set_string(tn, TOPO_PGROUP_PROTOCOL,
		    TOPO_PROP_LABEL, TOPO_PROP_SET_ONCE, label, &err) != 0) {
			nvlist_free(out);
			return (topo_mod_seterrno(mp, err));
		}
		nvlist_free(out);
	}
	return (0);
}

/*ARGSUSED*/
static int
EXCAP_set(tnode_t *tn, did_t *pd,
    const char *dpnm, const char *tpgrp, const char *tpnm)
{
	int excap;
	int err;
	int e = 0;

	if ((excap = did_excap(pd)) <= 0)
		return (0);

	switch (excap & PCIE_PCIECAP_DEV_TYPE_MASK) {
	case PCIE_PCIECAP_DEV_TYPE_ROOT:
		e = topo_prop_set_string(tn, TOPO_PGROUP_PCI,
		    TOPO_PROP_EXCAP, TOPO_PROP_SET_ONCE, PCIEX_ROOT, &err);
		break;
	case PCIE_PCIECAP_DEV_TYPE_UP:
		e = topo_prop_set_string(tn, TOPO_PGROUP_PCI,
		    TOPO_PROP_EXCAP, TOPO_PROP_SET_ONCE, PCIEX_SWUP, &err);
		break;
	case PCIE_PCIECAP_DEV_TYPE_DOWN:
		e = topo_prop_set_string(tn, TOPO_PGROUP_PCI,
		    TOPO_PROP_EXCAP, TOPO_PROP_SET_ONCE, PCIEX_SWDWN, &err);
		break;
	case PCIE_PCIECAP_DEV_TYPE_PCI2PCIE:
		e = topo_prop_set_string(tn, TOPO_PGROUP_PCI,
		    TOPO_PROP_EXCAP, TOPO_PROP_SET_ONCE, PCIEX_BUS, &err);
		break;
	case PCIE_PCIECAP_DEV_TYPE_PCIE2PCI:
		e = topo_prop_set_string(tn, TOPO_PGROUP_PCI,
		    TOPO_PROP_EXCAP, TOPO_PROP_SET_ONCE, PCI_BUS, &err);
		break;
	case PCIE_PCIECAP_DEV_TYPE_PCIE_DEV:
		e = topo_prop_set_string(tn, TOPO_PGROUP_PCI,
		    TOPO_PROP_EXCAP, TOPO_PROP_SET_ONCE, PCIEX_DEVICE, &err);
		break;
	}
	if (e != 0)
		return (topo_mod_seterrno(did_mod(pd), err));
	return (0);
}

/*ARGSUSED*/
static int
DEVprop_set(tnode_t *tn, did_t *pd,
    const char *dpnm, const char *tpgrp, const char *tpnm)
{
	topo_mod_t *mp;
	char *dnpath;
	char *path, *fpath;
	int d, f;
	int err, e;

	mp = did_mod(pd);
	if ((dnpath = di_devfs_path(did_dinode(pd))) == NULL) {
		topo_mod_dprintf(mp, "NULL di_devfs_path.\n");
		return (topo_mod_seterrno(mp, ETOPO_PROP_NOENT));
	}
	if ((path = topo_mod_strdup(mp, dnpath)) == NULL) {
		di_devfs_path_free(dnpath);
		return (-1);
	}
	di_devfs_path_free(dnpath);

	/* The DEV path is modified for hostbridges */
	if (strcmp(topo_node_name(tn), HOSTBRIDGE) == 0) {
		fpath = dev_for_hostbridge(did_mod(pd), path);
	} else {
		did_BDF(pd, NULL, &d, &f);
		fpath = dev_path_fix(mp, path, d, f);
	}
	if (fpath == NULL)
		return (-1);
	e = topo_prop_set_string(tn,
	    tpgrp, tpnm, TOPO_PROP_SET_ONCE, fpath, &err);
	topo_mod_strfree(mp, fpath);
	if (e != 0)
		return (topo_mod_seterrno(mp, err));
	return (0);
}

/*ARGSUSED*/
static int
DRIVERprop_set(tnode_t *tn, did_t *pd,
    const char *dpnm, const char *tpgrp, const char *tpnm)
{
	char *dnm;
	int err;

	if ((dnm = di_driver_name(did_dinode(pd))) == NULL)
		return (0);
	if (topo_prop_set_string(tn,
	    tpgrp, tpnm, TOPO_PROP_SET_ONCE, dnm, &err) < 0)
		return (topo_mod_seterrno(did_mod(pd), err));

	return (0);
}

/*ARGSUSED*/
static int
maybe_di_chars_copy(tnode_t *tn, did_t *pd,
    const char *dpnm, const char *tpgrp, const char *tpnm)
{
	topo_mod_t *mp;
	uchar_t *typbuf;
	char *tmpbuf;
	int sz = -1;
	int err, e;

	if (di_bytes_get(did_dinode(pd), dpnm, &sz, &typbuf) < 0)
		return (0);
	mp = did_mod(pd);
	tmpbuf = topo_mod_alloc(mp, sz + 1);
	bcopy(typbuf, tmpbuf, sz);
	tmpbuf[sz] = 0;
	e = topo_prop_set_string(tn,
	    tpgrp, tpnm, TOPO_PROP_SET_ONCE, tmpbuf, &err);
	topo_mod_free(mp, tmpbuf, sz + 1);
	if (e != 0)
		return (topo_mod_seterrno(mp, err));
	return (0);
}

static int
uint_to_strprop(topo_mod_t *mp, uint_t v, tnode_t *tn,
    const char *tpgrp, const char *tpnm)
{
	char str[21]; /* sizeof (UINT64_MAX) + '\0' */
	int e;

	(void) snprintf(str, 21, "%x", v);
	if (topo_prop_set_string(tn,
	    tpgrp, tpnm, TOPO_PROP_SET_ONCE, str, &e) < 0)
		return (topo_mod_seterrno(mp, e));
	return (0);
}

static int
maybe_di_uint_to_str(tnode_t *tn, did_t *pd,
    const char *dpnm, const char *tpgrp, const char *tpnm)
{
	uint_t v;

	if (di_uintprop_get(did_dinode(pd), dpnm, &v) < 0)
		return (0);

	return (uint_to_strprop(did_mod(pd), v, tn, tpgrp, tpnm));
}

int
did_props_set(tnode_t *tn, did_t *pd, txprop_t txarray[], int txnum)
{
	topo_mod_t *mp;
	const char *ppgroup = NULL;
	int i, r, e;

	mp = did_mod(pd);
	for (i = 0; i < txnum; i++) {
		/*
		 * Ensure the property group has been created.
		 */
		if (ppgroup == NULL ||
		    strcmp(txarray[i].tx_tpgroup, ppgroup) != 0) {
			if (topo_pgroup_create(tn, txarray[i].tx_tpgroup,
			    txarray[i].tx_pgstab, &e) < 0) {
				if (e != ETOPO_PROP_DEFD)
					return (topo_mod_seterrno(mp, e));
			}
		}

		topo_mod_dprintf(mp,
		    "Setting property %s in group %s.\n",
		    txarray[i].tx_tprop, txarray[i].tx_tpgroup);
		r = txarray[i].tx_xlate(tn, pd,
		    txarray[i].tx_diprop, txarray[i].tx_tpgroup,
		    txarray[i].tx_tprop);
		if (r != 0) {
			topo_mod_dprintf(mp, "failed.\n");
			topo_mod_dprintf(mp, "Error was %s.\n",
			    topo_strerror(topo_mod_errno(mp)));
			return (-1);
		}
		topo_mod_dprintf(mp, "succeeded.\n");
	}
	return (0);
}
