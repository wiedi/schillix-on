/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include	"msg.h"
#include	"_debug.h"
#include	"libld.h"

void
Dbg_unused_sec(Is_desc *isp)
{
	const char	*str;

	if (DBG_NOTCLASS(DBG_UNUSED))
		return;
	if (DBG_NOTDETAIL())
		return;

	/*
	 * If the file from which this section originates hasn't been referenced
	 * at all, skip this diagnostic, as it would have been covered under
	 * Dbg_unused_file() called from ignore_section_processing().
	 */
	if (isp->is_file &&
	    ((isp->is_file->ifl_flags & FLG_IF_FILEREF) == 0))
		return;

	if (isp->is_flags & FLG_IS_DISCARD)
		str = MSG_INTL(MSG_USD_SECDISCARD);
	else
		str = MSG_ORIG(MSG_STR_EMPTY);

	dbg_print(MSG_INTL(MSG_USD_SEC), isp->is_basename,
	    EC_XWORD(isp->is_shdr->sh_size), isp->is_file->ifl_name, str);
}

/*
 * There are no ELF32/ELF64 data structures in these functions - only define
 * one copy in liblddbg.
 */
#if	!defined(_ELF64)

void
Dbg_unused_file(const char *name, int needstr, int cycle)
{
	if (DBG_NOTCLASS(DBG_UNUSED))
		return;

	if (needstr)
		dbg_print(MSG_INTL(MSG_USD_NEEDSTR), name);
	else if (cycle)
		dbg_print(MSG_INTL(MSG_USD_FILECYCLIC), name, cycle);
	else
		dbg_print(MSG_INTL(MSG_USD_FILE), name);
}

void
Dbg_unused_unref(const char *caller, const char *depend)
{
	if (DBG_NOTCLASS(DBG_UNUSED))
		return;
	if (DBG_NOTDETAIL())
		return;

	dbg_print(MSG_INTL(MSG_USD_UNREF), caller, depend);
}

void
Dbg_unused_rtldinfo(const char *fname1, const char *fname2)
{
	if (DBG_NOTCLASS(DBG_UNUSED))
		return;

	dbg_print(MSG_INTL(MSG_USD_RTLDINFO), fname1, fname2);
}
#endif
