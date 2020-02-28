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
 * Copyright 2020 J. Schilling.  All rights reserved.
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <stdio.h>
#include <shadow.h>
#include <stdlib.h>
#include <errno.h>
#include "getent.h"

/*
 * getspnam - get entries from shadow database
 */
int
dogetsp(const char **list)
{
	struct spwd *spp;
	int rc = EXC_SUCCESS;


	if (list == NULL || *list == NULL) {
		while ((spp = getspent()) != NULL)
			(void) putspent(spp, stdout);
	} else {
		for (; *list != NULL; list++) {
			errno = 0;

			spp = getspnam(*list);

			if (spp == NULL)
				rc = EXC_NAME_NOT_FOUND;
			else
				(void) putspent(spp, stdout);
		}
	}

	return (rc);
}
