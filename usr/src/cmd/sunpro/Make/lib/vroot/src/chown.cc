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
 * Copyright 1993 Sun Microsystems, Inc. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * @(#)chown.cc 1.4 06/12/12
 */

#pragma	ident	"@(#)chown.cc	1.4	06/12/12"

/*
 * This file contains modifications Copyright 2017 J. Schilling
 *
 * @(#)chown.cc	1.2 17/05/01 2017 J. Schilling
 */
#include <schily/mconfig.h>
#ifndef lint
static	UConst char sccsid[] =
	"@(#)chown.cc	1.2 17/05/01 2017 J. Schilling";
#endif

#include <vroot/vroot.h>
#include <vroot/args.h>

extern int chown(const char *path, uid_t owner, gid_t group);


static int	chown_thunk(char *path)
{
	vroot_result= chown(path, vroot_args.chown.user, vroot_args.chown.group);
	return(vroot_result == 0);
}

int	chown_vroot(char *path, int user, int group, pathpt vroot_path, pathpt vroot_vroot)
{
	vroot_args.chown.user= user;
	vroot_args.chown.group= group;
	translate_with_thunk(path, chown_thunk, vroot_path, vroot_vroot, rw_read);
	return(vroot_result);
}
