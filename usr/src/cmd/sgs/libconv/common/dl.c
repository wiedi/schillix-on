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

#include	<string.h>
#include	"_conv.h"
#include	"dl_msg.h"

#define	MODESZ	MSG_GBL_OSQBRKT_SIZE + \
		MSG_RTLD_LAZY_SIZE + \
		MSG_RTLD_GLOBAL_SIZE + \
		MSG_RTLD_NOLOAD_SIZE + \
		MSG_RTLD_PARENT_SIZE + \
		MSG_RTLD_GROUP_SIZE + \
		MSG_RTLD_WORLD_SIZE + \
		MSG_RTLD_NODELETE_SIZE + \
		MSG_RTLD_FIRST_SIZE + \
		MSG_RTLD_CONFGEN_SIZE + \
		CONV_INV_STRSIZE + MSG_GBL_CSQBRKT_SIZE


/*
 * String conversion routine for dlopen() attributes.
 */
const char *
conv_dl_mode(int mode, int fabricate)
{
	static	char	string[MODESZ];
	static Val_desc vda[] = {
		{ RTLD_NOLOAD,		MSG_ORIG(MSG_RTLD_NOLOAD) },
		{ RTLD_PARENT,		MSG_ORIG(MSG_RTLD_PARENT) },
		{ RTLD_GROUP,		MSG_ORIG(MSG_RTLD_GROUP) },
		{ RTLD_WORLD,		MSG_ORIG(MSG_RTLD_WORLD) },
		{ RTLD_NODELETE,	MSG_ORIG(MSG_RTLD_NODELETE) },
		{ RTLD_FIRST,		MSG_ORIG(MSG_RTLD_FIRST) },
		{ RTLD_CONFGEN,		MSG_ORIG(MSG_RTLD_CONFGEN) },
		{ 0,			0 }
	};
	int		_mode = mode;

	(void) strcpy(string, MSG_ORIG(MSG_GBL_OSQBRKT));

	if (mode & RTLD_NOW) {
	    if (strlcat(string, MSG_ORIG(MSG_RTLD_NOW), MODESZ) >= MODESZ)
		return (conv_invalid_val(string, MODESZ, mode, 0));
	} else if (fabricate) {
	    if (strlcat(string, MSG_ORIG(MSG_RTLD_LAZY), MODESZ) >= MODESZ)
		return (conv_invalid_val(string, MODESZ, mode, 0));
	}
	if (mode & RTLD_GLOBAL) {
	    if (strlcat(string, MSG_ORIG(MSG_RTLD_GLOBAL), MODESZ) >= MODESZ)
		return (conv_invalid_val(string, MODESZ, mode, 0));
	} else if (fabricate) {
	    if (strlcat(string, MSG_ORIG(MSG_RTLD_LOCAL), MODESZ) >= MODESZ)
		return (conv_invalid_val(string, MODESZ, mode, 0));
	}
	_mode &= ~(RTLD_LAZY | RTLD_NOW | RTLD_GLOBAL);

	if (conv_expn_field(string, MODESZ, vda, mode, _mode, 0, 0))
		(void) strlcat(string, MSG_ORIG(MSG_GBL_CSQBRKT), MODESZ);

	return ((const char *)string);
}

#define	FLAGSZ	MSG_GBL_OSQBRKT_SIZE + \
		MSG_RTLD_REL_RELATIVE_SIZE +	MSG_GBL_SEP_SIZE + \
		MSG_RTLD_REL_EXEC_SIZE +	MSG_GBL_SEP_SIZE + \
		MSG_RTLD_REL_DEPENDS_SIZE +	MSG_GBL_SEP_SIZE + \
		MSG_RTLD_REL_PRELOAD_SIZE +	MSG_GBL_SEP_SIZE + \
		MSG_RTLD_REL_SELF_SIZE +	MSG_GBL_SEP_SIZE + \
		MSG_RTLD_REL_WEAK_SIZE +	MSG_GBL_SEP_SIZE + \
		MSG_RTLD_MEMORY_SIZE +		MSG_GBL_SEP_SIZE + \
		MSG_RTLD_STRIP_SIZE +		MSG_GBL_SEP_SIZE + \
		MSG_RTLD_NOHEAP_SIZE +		MSG_GBL_SEP_SIZE + \
		MSG_RTLD_CONFSET_SIZE + \
		CONV_INV_STRSIZE + MSG_GBL_CSQBRKT_SIZE

/*
 * String conversion routine for dldump() flags.
 * crle(1) uses this routine to generate update information, and in this case
 * we build a "|" separated string.
 */
const char *
conv_dl_flag(int flags, int separator)
{
	static	char	string[FLAGSZ];
	static Val_desc vda[] = {
		{ RTLD_REL_RELATIVE,	MSG_ORIG(MSG_RTLD_REL_RELATIVE) },
		{ RTLD_REL_EXEC,	MSG_ORIG(MSG_RTLD_REL_EXEC) },
		{ RTLD_REL_DEPENDS,	MSG_ORIG(MSG_RTLD_REL_DEPENDS) },
		{ RTLD_REL_PRELOAD,	MSG_ORIG(MSG_RTLD_REL_PRELOAD) },
		{ RTLD_REL_SELF,	MSG_ORIG(MSG_RTLD_REL_SELF) },
		{ RTLD_REL_WEAK,	MSG_ORIG(MSG_RTLD_REL_WEAK) },
		{ RTLD_MEMORY,		MSG_ORIG(MSG_RTLD_MEMORY) },
		{ RTLD_STRIP,		MSG_ORIG(MSG_RTLD_STRIP) },
		{ RTLD_NOHEAP,		MSG_ORIG(MSG_RTLD_NOHEAP) },
		{ RTLD_CONFSET,		MSG_ORIG(MSG_RTLD_CONFSET) },
		{ 0,			0 }
	};
	int		_flags = flags, element = 0;

	if (flags == 0)
		return (MSG_ORIG(MSG_GBL_ZERO));

	if (separator)
		(void) strlcpy(string, MSG_ORIG(MSG_GBL_QUOTE), FLAGSZ);
	else
		(void) strlcpy(string, MSG_ORIG(MSG_GBL_OSQBRKT), FLAGSZ);

	if ((flags & RTLD_REL_ALL) == RTLD_REL_ALL) {
	    if (strlcat(string, MSG_ORIG(MSG_RTLD_REL_ALL), FLAGSZ) >= FLAGSZ)
		return (conv_invalid_val(string, FLAGSZ, flags, 0));
	    element++;
	    flags = _flags &= ~RTLD_REL_ALL;
	}

	if (conv_expn_field(string, FLAGSZ, vda, flags, _flags,
	    (separator ? MSG_ORIG(MSG_GBL_SEP) : 0), element)) {
		if (separator)
		    (void) strlcat(string, MSG_ORIG(MSG_GBL_QUOTE), FLAGSZ);
		else
		    (void) strlcat(string, MSG_ORIG(MSG_GBL_CSQBRKT), FLAGSZ);
	}

	return ((const char *)string);
}
