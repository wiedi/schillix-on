/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LOCALIZED_STRINGS_H
#define	_LOCALIZED_STRINGS_H


#ifdef __cplusplus
extern "C" {
#endif

#include <libintl.h>

/*
 * Strings to be localized
 */
#define	WSREG_OUT_OF_MEMORY dgettext(TEXT_DOMAIN, "out of memory")
#define	WSREG_SYSTEM_SOFTWARE dgettext(TEXT_DOMAIN, "%s %s System Software")

#ifdef	__cplusplus
}
#endif

#endif /* _LOCALIZED_STRINGS_H */
