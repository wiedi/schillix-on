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
 *
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */


#ifndef _CSI_MSG_
#define	_CSI_MSG_
typedef enum {
	MSG_FIRST = 0,
	MSG_UNMAPPED_RPCSERVICE,
	MSG_RPCTCP_SVCCREATE_FAILED,
	MSG_RPCTCP_SVCREGISTER_FAILED,
	MSG_RPCUDP_SVCCREATE_FAILED,
	MSG_RPCUDP_SVCREGISTER_FAILED,
	MSG_INITIATION_STARTED,
	MSG_INITIATION_COMPLETED,
	MSG_INITIATION_FAILURE,
	MSG_CREATE_CONNECTQ_FAILURE,
	MSG_CREATE_NI_OUTQ_FAILURE,
	MSG_LOCATE_QMEMBER_FAILURE,
	MSG_DELETE_QMEMBER_FAILURE,
	MSG_SYSTEM_ERROR,
	MSG_UNEXPECTED_SIGNAL,
	MSG_RPC_INVALID_PROCEDURE,
	MSG_RPC_INVALID_PROGRAM,
	MSG_RPC_CANT_REPLY,
	MSG_RPCTCP_CLNTCREATE,
	MSG_RPCUDP_CLNTCREATE,
	MSG_INVALID_PROTO,
	MSG_QUEUE_CREATE_FAILURE,
	MSG_QUEUE_STATUS_FAILURE,
	MSG_QUEUE_MEMBADD_FAILURE,
	MSG_QUEUE_CLEANING,
	MSG_UNDEF_MSG,
	MSG_UNDEF_MSG_TRUNC,
	MSG_UNDEF_MODULE_TYPE,
	MSG_UNDEF_CLIENT,
	MSG_MESSAGE_SIZE,
	MSG_MESSAGE_SIZE_TRUNC,
	MSG_IPC_SEND_FAILURE,
	MSG_READ_FAILURE,
	MSG_SEND_NI_FAILURE,
	MSG_SEND_ACSSA_FAILURE,
	MSG_INVALID_COMM_SERVICE,
	MSG_XDR_XLATE_FAILURE,
	MSG_RPC_CANT_FREEARGS,
	MSG_QUEUE_ENTRY_DROP,
	MSG_UNDEF_HOST,
	MSG_INVALID_HOST,
	MSG_TERMINATION_STARTED,
	MSG_TERMINATION_COMPLETED,
	MSG_DUPLICATE_ACSLM_PACKET,
	MSG_INVALID_NI_TIMEOUT,
	MSG_DUPLICATE_NI_PACKET,
	MSG_NI_TIMEDOUT,
	MSG_UNEXPECTED_FAILURE,
	MSG_INVALID_COMMAND,
	MSG_INVALID_TYPE,
	MSG_INVALID_CONNECTQ_AGETIME,
	MSG_INVALID_LOCATION_TYPE,
	MSG_NONE_SPECIFIED,
	MSG_INVALID_VERSION_NUMBER,
	MSG_ADIOPEN_FAILURE,
	MSG_ADIREAD_FAILURE,
	MSG_ADIWRITE_FAILURE,
	MSG_ADI_SIGN_ON_FAILURE,
	MSG_ADI_SIGN_OFF_FAILURE,
	MSG_ADICLOSE_FAILURE,
	MSG_SEND_ADI_NI_FAILURE,
	MSG_QUEUE_ADI_ENTRY_DROP,
	MSG_DUPLICATE_ADI_NI_PACKET,
	MSG_PROC_WARNING,
	MSG_SYNTAX_WARNING,
	MSG_PROTOCOL_WARNING,
	MSG_VERSION_WARNING,
	MSG_HANDLE_PROC_WARNING,
	MSG_RPC_INVALID_VERSION,
	MSG_INVALID_ENVIRONMENT_VAR,
	MSG_LAST
} CSI_MSGNO;


#endif /* _CSI_MSG_ */
