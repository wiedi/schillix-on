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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * SMB: lock_byte_range
 *
 * The lock record message is sent to lock the given byte range.  More than
 * one non-overlapping byte range may be locked in a given file.  Locks
 * prevent attempts to lock, read or write the locked portion of the file
 * by other clients or Pids.  Overlapping locks are not allowed. Offsets
 * beyond the current end of file may be locked.  Such locks will not cause
 * allocation of file space.
 *
 * Since Offset is a 32 bit quantity, this request is inappropriate for
 * general locking within a very large file.
 *
 * Client Request                     Description
 * ================================== =================================
 *
 * UCHAR WordCount;                   Count of parameter words = 5
 * USHORT Fid;                        File handle
 * ULONG Count;                       Count of bytes to lock
 * ULONG Offset;                      Offset from start of file
 * USHORT ByteCount;                  Count of data bytes = 0
 *
 * Locks may only be unlocked by the Pid that performed the lock.
 *
 * Server Response                    Description
 * ================================== =================================
 *
 * UCHAR WordCount;                   Count of parameter words = 0
 * USHORT ByteCount;                  Count of data bytes = 0
 *
 * This client request does not wait for the lock to be granted.  If the
 * lock can not be immediately granted (within 200-300 ms), the server
 * should return failure to the client
 */

#include <smbsrv/smb_incl.h>

int
smb_com_lock_byte_range(struct smb_request *sr)
{
	uint32_t	count;
	uint32_t	off;
	DWORD		result;

	if (smbsr_decode_vwv(sr, "wll", &sr->smb_fid, &count, &off) != 0) {
		smbsr_decode_error(sr);
		/* NOTREACHED */
	}

	sr->fid_ofile = smb_ofile_lookup_by_fid(sr->tid_tree, sr->smb_fid);
	if (sr->fid_ofile == NULL) {
		smbsr_error(sr, NT_STATUS_INVALID_HANDLE,
		    ERRDOS, ERRbadfid);
		/* NOTREACHED */
	}

	/*
	 * The last parameter is lock type. This is dependent on
	 * lock flag (3rd parameter). Since the lock flag is
	 * set to be exclusive, lock type is passed as
	 * normal lock (write lock).
	 */
	result = smb_lock_range(sr, sr->fid_ofile,
	    (u_offset_t)off, (uint64_t)count,  0, SMB_LOCK_TYPE_READWRITE);
	if (result != NT_STATUS_SUCCESS) {
		smb_lock_range_error(sr, result);
		/* NOT REACHED */
	}

	smbsr_encode_empty_result(sr);

	return (SDRC_NORMAL_REPLY);
}
