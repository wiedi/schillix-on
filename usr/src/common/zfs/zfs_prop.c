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
 * Master property table.
 *
 * This table keeps track of all the properties supported by ZFS, and their
 * various attributes.  Not all of these are needed by the kernel, and several
 * are only used by a single libzfs client.  But having them here centralizes
 * all property information in one location.
 *
 * 	name		The human-readable string representing this property
 * 	proptype	Basic type (string, boolean, number)
 * 	default		Default value for the property.  Sadly, C only allows
 * 			you to initialize the first member of a union, so we
 * 			have two default members for each property.
 * 	attr		Attributes (readonly, inheritable) for the property
 * 	types		Valid dataset types to which this applies
 * 	values		String describing acceptable values for the property
 * 	colname		The column header for 'zfs list'
 *	colfmt		The column formatting for 'zfs list'
 *
 * This table must match the order of property types in libzfs.h.
 */

#include <sys/zio.h>
#include <sys/spa.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_ioctl.h>

#include "zfs_prop.h"

#if defined(_KERNEL)
#include <sys/systm.h>
#else
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#endif

typedef enum {
	prop_default,
	prop_readonly,
	prop_inherit
} prop_attr_t;

typedef struct {
	const char	*pd_name;
	zfs_proptype_t	pd_proptype;
	uint64_t	pd_numdefault;
	const char	*pd_strdefault;
	prop_attr_t	pd_attr;
	int		pd_types;
	const char	*pd_values;
	const char	*pd_colname;
	boolean_t	pd_rightalign;
} prop_desc_t;

static prop_desc_t zfs_prop_table[ZFS_NPROP_ALL] = {
	{ "type",	prop_type_string,	0,	NULL,	prop_readonly,
	    ZFS_TYPE_ANY, "filesystem | volume | snapshot", "TYPE", B_TRUE },
	{ "creation",	prop_type_number,	0,	NULL,	prop_readonly,
	    ZFS_TYPE_ANY, "<date>", "CREATION", B_FALSE },
	{ "used",	prop_type_number,	0,	NULL,	prop_readonly,
	    ZFS_TYPE_ANY, "<size>",	"USED", B_TRUE },
	{ "available",	prop_type_number,	0,	NULL,	prop_readonly,
	    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME, "<size>", "AVAIL", B_TRUE },
	{ "referenced",	prop_type_number,	0,	NULL,	prop_readonly,
	    ZFS_TYPE_ANY,
	    "<size>", "REFER", B_TRUE },
	{ "compressratio", prop_type_number,	0,	NULL,	prop_readonly,
	    ZFS_TYPE_ANY, "<1.00x or higher if compressed>", "RATIO", B_TRUE },
	{ "mounted",	prop_type_boolean,	0,	NULL,	prop_readonly,
	    ZFS_TYPE_FILESYSTEM, "yes | no | -", "MOUNTED", B_TRUE },
	{ "origin",	prop_type_string,	0,	NULL,	prop_readonly,
	    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME, "<snapshot>", "ORIGIN",
	    B_FALSE },
	{ "quota",	prop_type_number,	0,	NULL,	prop_default,
	    ZFS_TYPE_FILESYSTEM, "<size> | none", "QUOTA", B_TRUE },
	{ "reservation", prop_type_number,	0,	NULL,	prop_default,
	    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME,
	    "<size> | none", "RESERV", B_TRUE },
	{ "volsize",	prop_type_number,	0,	NULL,	prop_default,
	    ZFS_TYPE_VOLUME, "<size>", "VOLSIZE", B_TRUE },
	{ "volblocksize", prop_type_number,	8192,	NULL,	prop_readonly,
	    ZFS_TYPE_VOLUME, "512 to 128k, power of 2",	"VOLBLOCK", B_TRUE },
	{ "recordsize",	prop_type_number,	SPA_MAXBLOCKSIZE,	NULL,
	    prop_inherit,
	    ZFS_TYPE_FILESYSTEM,
	    "512 to 128k, power of 2", "RECSIZE", B_TRUE },
	{ "mountpoint",	prop_type_string,	0,	"/",	prop_inherit,
	    ZFS_TYPE_FILESYSTEM,
	    "<path> | legacy | none", "MOUNTPOINT", B_FALSE },
	{ "sharenfs",	prop_type_string,	0,	"off",	prop_inherit,
	    ZFS_TYPE_FILESYSTEM,
	    "on | off | share(1M) options", "SHARENFS", B_FALSE },
	{ "checksum",	prop_type_index,	ZIO_CHECKSUM_DEFAULT,	"on",
	    prop_inherit,	ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME,
	    "on | off | fletcher2 | fletcher4 | sha256", "CHECKSUM", B_TRUE },
	{ "compression", prop_type_index,	ZIO_COMPRESS_DEFAULT,	"off",
	    prop_inherit,	ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME,
	    "on | off | lzjb", "COMPRESS", B_TRUE },
	{ "atime",	prop_type_boolean,	1,	NULL,	prop_inherit,
	    ZFS_TYPE_FILESYSTEM,
	    "on | off", "ATIME", B_TRUE },
	{ "devices",	prop_type_boolean,	1,	NULL,	prop_inherit,
	    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_SNAPSHOT,
	    "on | off", "DEVICES", B_TRUE },
	{ "exec",	prop_type_boolean,	1,	NULL,	prop_inherit,
	    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_SNAPSHOT,
	    "on | off", "EXEC", B_TRUE },
	{ "setuid",	prop_type_boolean,	1,	NULL,	prop_inherit,
	    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_SNAPSHOT, "on | off", "SETUID",
	    B_TRUE },
	{ "readonly",	prop_type_boolean,	0,	NULL,	prop_inherit,
	    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME,
	    "on | off", "RDONLY", B_TRUE },
	{ "zoned",	prop_type_boolean,	0,	NULL,	prop_inherit,
	    ZFS_TYPE_FILESYSTEM,
	    "on | off", "ZONED", B_TRUE },
	{ "snapdir",	prop_type_index,	ZFS_SNAPDIR_HIDDEN, "hidden",
	    prop_inherit,
	    ZFS_TYPE_FILESYSTEM,
	    "hidden | visible", "SNAPDIR", B_TRUE },
	{ "aclmode", prop_type_index,	ZFS_ACL_GROUPMASK, "groupmask",
	    prop_inherit, ZFS_TYPE_FILESYSTEM,
	    "discard | groupmask | passthrough", "ACLMODE", B_TRUE },
	{ "aclinherit", prop_type_index,	ZFS_ACL_SECURE,	"secure",
	    prop_inherit, ZFS_TYPE_FILESYSTEM,
	    "discard | noallow | secure | passthrough", "ACLINHERIT", B_TRUE },
	{ "canmount",	prop_type_boolean,	1,	NULL,	prop_default,
	    ZFS_TYPE_FILESYSTEM,
	    "on | off", "CANMOUNT", B_TRUE },
	{ "createtxg",	prop_type_number,	0,	NULL,	prop_readonly,
	    ZFS_TYPE_ANY, NULL, NULL, NULL},
	{ "name",	prop_type_string,	0,	NULL,	prop_readonly,
	    ZFS_TYPE_ANY,
	    NULL, "NAME", B_FALSE },
};

zfs_proptype_t
zfs_prop_get_type(zfs_prop_t prop)
{
	return (zfs_prop_table[prop].pd_proptype);
}

static boolean_t
propname_match(const char *p, zfs_prop_t prop, size_t len)
{
	const char *propname = zfs_prop_table[prop].pd_name;
#ifndef _KERNEL
	const char *colname = zfs_prop_table[prop].pd_colname;
	int c;
#endif

#ifndef _KERNEL
	if (colname == NULL)
		return (B_FALSE);
#endif

	if (len == strlen(propname) &&
	    strncmp(p, propname, len) == 0)
		return (B_TRUE);

#ifndef _KERNEL
	if (len != strlen(colname))
		return (B_FALSE);

	for (c = 0; c < len; c++)
		if (p[c] != tolower(colname[c]))
			break;

	return (colname[c] == '\0');
#else
	return (B_FALSE);
#endif
}


/*
 * Given a property name, returns the corresponding property ID.
 */
zfs_prop_t
zfs_name_to_prop(const char *propname)
{
	int i;

	for (i = 0; i < ZFS_NPROP_ALL; i++) {
		if (propname_match(propname, i, strlen(propname)))
			return (i);
	}

	return (ZFS_PROP_INVAL);
}

/*
 * For user property names, we allow all lowercase alphanumeric characters, plus
 * a few useful punctuation characters.
 */
static int
valid_char(char c)
{
	return ((c >= 'a' && c <= 'z') ||
	    (c >= '0' && c <= '9') ||
	    c == '-' || c == '_' || c == '.' || c == ':');
}

/*
 * Returns true if this is a valid user-defined property (one with a ':').
 */
boolean_t
zfs_prop_user(const char *name)
{
	int i;
	char c;
	boolean_t foundsep = B_FALSE;

	for (i = 0; i < strlen(name); i++) {
		c = name[i];
		if (!valid_char(c))
			return (B_FALSE);
		if (c == ':')
			foundsep = B_TRUE;
	}

	if (!foundsep)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Return the default value for the given property.
 */
const char *
zfs_prop_default_string(zfs_prop_t prop)
{
	return (zfs_prop_table[prop].pd_strdefault);
}

uint64_t
zfs_prop_default_numeric(zfs_prop_t prop)
{
	return (zfs_prop_table[prop].pd_numdefault);
}

/*
 * Returns TRUE if the property is readonly.
 */
int
zfs_prop_readonly(zfs_prop_t prop)
{
	return (zfs_prop_table[prop].pd_attr == prop_readonly);
}

/*
 * Given a property ID, returns the corresponding name.
 */
const char *
zfs_prop_to_name(zfs_prop_t prop)
{
	return (zfs_prop_table[prop].pd_name);
}

/*
 * Returns TRUE if the property is inheritable.
 */
int
zfs_prop_inheritable(zfs_prop_t prop)
{
	return (zfs_prop_table[prop].pd_attr == prop_inherit);
}

#ifndef _KERNEL

typedef struct zfs_index {
	const char *name;
	uint64_t index;
} zfs_index_t;

static zfs_index_t checksum_table[] = {
	{ "on",		ZIO_CHECKSUM_ON },
	{ "off",	ZIO_CHECKSUM_OFF },
	{ "fletcher2",	ZIO_CHECKSUM_FLETCHER_2 },
	{ "fletcher4",	ZIO_CHECKSUM_FLETCHER_4 },
	{ "sha256",	ZIO_CHECKSUM_SHA256 },
	{ NULL }
};

static zfs_index_t compress_table[] = {
	{ "on",		ZIO_COMPRESS_ON },
	{ "off",	ZIO_COMPRESS_OFF },
	{ "lzjb",	ZIO_COMPRESS_LZJB },
	{ NULL }
};

static zfs_index_t snapdir_table[] = {
	{ "hidden",	ZFS_SNAPDIR_HIDDEN },
	{ "visible",	ZFS_SNAPDIR_VISIBLE },
	{ NULL }
};

static zfs_index_t acl_mode_table[] = {
	{ "discard",	ZFS_ACL_DISCARD },
	{ "groupmask",	ZFS_ACL_GROUPMASK },
	{ "passthrough", ZFS_ACL_PASSTHROUGH },
	{ NULL }
};

static zfs_index_t acl_inherit_table[] = {
	{ "discard",	ZFS_ACL_DISCARD },
	{ "noallow",	ZFS_ACL_NOALLOW },
	{ "secure",	ZFS_ACL_SECURE },
	{ "passthrough", ZFS_ACL_PASSTHROUGH },
	{ NULL }
};

static zfs_index_t *
zfs_prop_index_table(zfs_prop_t prop)
{
	switch (prop) {
	case ZFS_PROP_CHECKSUM:
		return (checksum_table);
		break;
	case ZFS_PROP_COMPRESSION:
		return (compress_table);
		break;
	case ZFS_PROP_SNAPDIR:
		return (snapdir_table);
		break;
	case ZFS_PROP_ACLMODE:
		return (acl_mode_table);
		break;
	case ZFS_PROP_ACLINHERIT:
		return (acl_inherit_table);
		break;
	default:
		return (NULL);
	}
}


/*
 * Tables of index types, plus functions to convert between the user view
 * (strings) and internal representation (uint64_t).
 */
int
zfs_prop_string_to_index(zfs_prop_t prop, const char *string, uint64_t *index)
{
	zfs_index_t *table;
	int i;

	if ((table = zfs_prop_index_table(prop)) == NULL)
		return (-1);

	for (i = 0; table[i].name != NULL; i++) {
		if (strcmp(string, table[i].name) == 0) {
			*index = table[i].index;
			return (0);
		}
	}

	return (-1);
}

int
zfs_prop_index_to_string(zfs_prop_t prop, uint64_t index, const char **string)
{
	zfs_index_t *table;
	int i;

	if ((table = zfs_prop_index_table(prop)) == NULL)
		return (-1);

	for (i = 0; table[i].name != NULL; i++) {
		if (table[i].index == index) {
			*string = table[i].name;
			return (0);
		}
	}

	return (-1);
}

/*
 * Returns TRUE if the property applies to the given dataset types.
 */
int
zfs_prop_valid_for_type(zfs_prop_t prop, int types)
{
	return ((zfs_prop_table[prop].pd_types & types) != 0);
}

/*
 * Returns a string describing the set of acceptable values for the given
 * property, or NULL if it cannot be set.
 */
const char *
zfs_prop_values(zfs_prop_t prop)
{
	return (zfs_prop_table[prop].pd_values);
}

/*
 * Returns TRUE if this property is a string type.  Note that index types
 * (compression, checksum) are treated as strings in userland, even though they
 * are stored numerically on disk.
 */
int
zfs_prop_is_string(zfs_prop_t prop)
{
	return (zfs_prop_table[prop].pd_proptype == prop_type_string ||
	    zfs_prop_table[prop].pd_proptype == prop_type_index);
}

/*
 * Returns the column header for the given property.  Used only in
 * 'zfs list -o', but centralized here with the other property information.
 */
const char *
zfs_prop_column_name(zfs_prop_t prop)
{
	return (zfs_prop_table[prop].pd_colname);
}

/*
 * Returns whether the given property should be displayed right-justified for
 * 'zfs list'.
 */
boolean_t
zfs_prop_align_right(zfs_prop_t prop)
{
	return (zfs_prop_table[prop].pd_rightalign);
}

/*
 * Determines the minimum width for the column, and indicates whether it's fixed
 * or not.  Only string columns are non-fixed.
 */
size_t
zfs_prop_width(zfs_prop_t prop, boolean_t *fixed)
{
	prop_desc_t *pd = &zfs_prop_table[prop];
	zfs_index_t *idx;
	size_t ret;
	int i;

	*fixed = B_TRUE;

	/*
	 * Start with the width of the column name.
	 */
	ret = strlen(pd->pd_colname);

	/*
	 * For fixed-width values, make sure the width is large enough to hold
	 * any possible value.
	 */
	switch (pd->pd_proptype) {
	case prop_type_number:
		/*
		 * The maximum length of a human-readable number is 5 characters
		 * ("20.4M", for example).
		 */
		if (ret < 5)
			ret = 5;
		/*
		 * 'creation' is handled specially because it's a number
		 * internally, but displayed as a date string.
		 */
		if (prop == ZFS_PROP_CREATION)
			*fixed = B_FALSE;
		break;
	case prop_type_boolean:
		/*
		 * The maximum length of a boolean value is 3 characters, for
		 * "off".
		 */
		if (ret < 3)
			ret = 3;
		break;
	case prop_type_index:
		idx = zfs_prop_index_table(prop);
		for (i = 0; idx[i].name != NULL; i++) {
			if (strlen(idx[i].name) > ret)
				ret = strlen(idx[i].name);
		}
		break;

	case prop_type_string:
		*fixed = B_FALSE;
		break;
	}

	return (ret);
}

#endif
