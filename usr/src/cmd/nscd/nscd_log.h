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

#ifndef	_NSCD_LOG_H
#define	_NSCD_LOG_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include "nscd_common.h"

/*
 * nscd logging options
 */
/*
 * components: select more than one by OR'ing
 */
#define	NSCD_LOG_NONE		0x0000
#define	NSCD_LOG_ACCESS_INFO	0x0001
#define	NSCD_LOG_INT_ADDR	0x0002
#define	NSCD_LOG_NSW_STATE	0x0004
#define	NSCD_LOG_GETENT_CTX	0x0008
#define	NSCD_LOG_SWITCH_ENGINE	0x0010
#define	NSCD_LOG_CONFIG		0x0020
#define	NSCD_LOG_FRONT_END	0x0040
#define	NSCD_LOG_CACHE		0x0080
#define	NSCD_LOG_SMF_MONITOR	0x0100
#define	NSCD_LOG_ADMIN		0x0200
#define	NSCD_LOG_SELF_CRED	0x0400
#define	NSCD_LOG_ALL		0x07ff

/*
 * debug level: select more than one by OR'ing
 */
#define	NSCD_LOG_LEVEL_NONE		0x0000
#define	NSCD_LOG_LEVEL_CANT_FOUND	0x0001
#define	NSCD_LOG_LEVEL_ALERT		0x0002
#define	NSCD_LOG_LEVEL_CRIT		0x0004
#define	NSCD_LOG_LEVEL_ERROR		0x0008
#define	NSCD_LOG_LEVEL_WARNING		0x0010
#define	NSCD_LOG_LEVEL_NOTICE		0x0020
#define	NSCD_LOG_LEVEL_INFO		0x0040
#define	NSCD_LOG_LEVEL_DEBUG		0x0080
#define	NSCD_LOG_LEVEL_ALL		0x00ff

/*
 * _nscd_log_comp and _nscd_log_level defined in nscd_log.c
 */
extern int _nscd_log_comp;
extern int _nscd_log_level;

#define	_NSCD_LOG(comp, lvl)	if ((_nscd_log_comp & (comp)) && \
					(_nscd_log_level & (lvl))) \
				_nscd_logit

#define	_NSCD_LOG_IF(comp, lvl)	if ((_nscd_log_comp & (comp)) && \
					(_nscd_log_level & (lvl)))


/*
 * prototypes
 */
void		_nscd_logit(char *funcname, char *format, ...);
nscd_rc_t	_nscd_set_debug_level(int level);
nscd_rc_t	_nscd_set_log_file(char *name);

#ifdef	__cplusplus
}
#endif

#endif	/* _NSCD_LOG_H */
