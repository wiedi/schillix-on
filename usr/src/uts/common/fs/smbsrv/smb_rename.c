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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <smbsrv/nterror.h>
#include <sys/synch.h>
#include <smbsrv/smb_incl.h>
#include <smbsrv/smb_fsops.h>
#include <sys/nbmlock.h>

static int smb_do_rename(smb_request_t *, smb_fqi_t *, smb_fqi_t *);

/*
 * smb_com_rename
 *
 * Rename a file. Files OldFileName must exist and NewFileName must not.
 * Both pathnames must be relative to the Tid specified in the request.
 * Open files may be renamed.
 *
 * Multiple files may be renamed in response to a single request as Rename
 * File supports wildcards in the file name (last component of the path).
 * NOTE: we don't support rename with wildcards.
 *
 * SearchAttributes indicates the attributes that the target file(s) must
 * have. If SearchAttributes is zero then only normal files are renamed.
 * If the system file or hidden attributes are specified then the rename
 * is inclusive - both the specified type(s) of files and normal files are
 * renamed. The encoding of SearchAttributes is described in section 3.10
 * - File Attribute Encoding.
 */
smb_sdrc_t
smb_pre_rename(smb_request_t *sr)
{
	smb_fqi_t *src_fqi = &sr->arg.dirop.fqi;
	smb_fqi_t *dst_fqi = &sr->arg.dirop.dst_fqi;
	int rc;

	if ((rc = smbsr_decode_vwv(sr, "w", &src_fqi->fq_sattr)) == 0) {
		rc = smbsr_decode_data(sr, "%SS", sr, &src_fqi->fq_path.pn_path,
		    &dst_fqi->fq_path.pn_path);

		dst_fqi->fq_sattr = 0;
	}

	DTRACE_SMB_2(op__Rename__start, smb_request_t *, sr,
	    struct dirop *, &sr->arg.dirop);

	return ((rc == 0) ? SDRC_SUCCESS : SDRC_ERROR);
}

void
smb_post_rename(smb_request_t *sr)
{
	DTRACE_SMB_1(op__Rename__done, smb_request_t *, sr);
}

smb_sdrc_t
smb_com_rename(smb_request_t *sr)
{
	static kmutex_t mutex;
	smb_fqi_t *src_fqi = &sr->arg.dirop.fqi;
	smb_fqi_t *dst_fqi = &sr->arg.dirop.dst_fqi;
	struct smb_node *dst_node;
	int rc;

	if (!STYPE_ISDSK(sr->tid_tree->t_res_type)) {
		smbsr_error(sr, NT_STATUS_ACCESS_DENIED,
		    ERRDOS, ERROR_ACCESS_DENIED);
		return (SDRC_ERROR);
	}

	mutex_enter(&mutex);
	rc = smb_do_rename(sr, src_fqi, dst_fqi);
	mutex_exit(&mutex);

	if (rc != 0) {
		/*
		 * The following values are based on observed WFWG,
		 * Windows 9x, NT and Windows 2000 behaviour.
		 * ERROR_FILE_EXISTS doesn't work for Windows 98 clients.
		 * Windows 95 clients don't see the problem because the
		 * target is deleted before the rename request.
		 */
		switch (rc) {
		case EEXIST:
			smbsr_error(sr, NT_STATUS_OBJECT_NAME_COLLISION,
			    ERRDOS, ERROR_ALREADY_EXISTS);
			break;
		case EPIPE:
			smbsr_error(sr, NT_STATUS_SHARING_VIOLATION,
			    ERRDOS, ERROR_SHARING_VIOLATION);
			break;
		case ENOENT:
			smbsr_error(sr, NT_STATUS_OBJECT_NAME_NOT_FOUND,
			    ERRDOS, ERROR_FILE_NOT_FOUND);
			break;
		default:
			smbsr_errno(sr, rc);
			break;
		}

		return (SDRC_ERROR);
	}

	if (src_fqi->fq_dnode)
		smb_node_release(src_fqi->fq_dnode);

	dst_node = dst_fqi->fq_dnode;
	if (dst_node) {
		if (dst_node->flags & NODE_FLAGS_NOTIFY_CHANGE) {
			dst_node->flags |= NODE_FLAGS_CHANGED;
			smb_process_node_notify_change_queue(dst_node);
		}
		smb_node_release(dst_node);
	}

	SMB_NULL_FQI_NODES(*src_fqi);
	SMB_NULL_FQI_NODES(*dst_fqi);

	rc = smbsr_encode_empty_result(sr);
	return ((rc == 0) ? SDRC_SUCCESS : SDRC_ERROR);
}

/*
 * smb_do_rename
 *
 * Backend to smb_com_rename to ensure that the rename operation is atomic.
 * This function should be called within a mutual exclusion region. If the
 * source and destination are identical, we don't actually do a rename, we
 * just check that the conditions are right. If the source and destination
 * files differ only in case, we a case-sensitive rename. Otherwise, we do
 * a full case-insensitive rename.
 *
 * This function should always return errno values.
 *
 * Upon success, the last_snode's and dir_snode's of both src_fqi and dst_fqi
 * are not released in this routine but in smb_com_rename().
 */
static int
smb_do_rename(
    smb_request_t *sr,
    smb_fqi_t *src_fqi,
    smb_fqi_t *dst_fqi)
{
	smb_node_t *src_node;
	char *dstname;
	DWORD status;
	int rc;
	int count;

	if ((rc = smbd_fs_query(sr, src_fqi, FQM_PATH_MUST_EXIST)) != 0) {
		return (rc);
	}

	src_node = src_fqi->fq_fnode;

	/*
	 * Break the oplock before access checks. If a client
	 * has a file open, this will force a flush or close,
	 * which may affect the outcome of any share checking.
	 */
	(void) smb_oplock_break(src_node, sr->session, B_FALSE);

	for (count = 0; count <= 3; count++) {
		if (count) {
			smb_node_end_crit(src_node);
			delay(MSEC_TO_TICK(400));
		}

		smb_node_start_crit(src_node, RW_READER);

		status = smb_node_rename_check(src_node);

		if (status != NT_STATUS_SHARING_VIOLATION)
			break;
	}

	if (status == NT_STATUS_SHARING_VIOLATION) {
		smb_node_end_crit(src_node);

		smb_node_release(src_node);
		smb_node_release(src_fqi->fq_dnode);

		SMB_NULL_FQI_NODES(*src_fqi);
		SMB_NULL_FQI_NODES(*dst_fqi);
		return (EPIPE); /* = ERRbadshare */
	}

	status = smb_range_check(sr, src_node, 0, UINT64_MAX, B_TRUE);

	if (status != NT_STATUS_SUCCESS) {
		smb_node_end_crit(src_node);

		smb_node_release(src_node);
		smb_node_release(src_fqi->fq_dnode);

		SMB_NULL_FQI_NODES(*src_fqi);
		SMB_NULL_FQI_NODES(*dst_fqi);
		return (EACCES);
	}

	if (utf8_strcasecmp(src_fqi->fq_path.pn_path,
	    dst_fqi->fq_path.pn_path) == 0) {
		if ((rc = smbd_fs_query(sr, dst_fqi, 0)) != 0) {
			smb_node_end_crit(src_node);

			smb_node_release(src_node);
			smb_node_release(src_fqi->fq_dnode);

			SMB_NULL_FQI_NODES(*src_fqi);
			SMB_NULL_FQI_NODES(*dst_fqi);
			return (rc);
		}

		/*
		 * Because the fqm parameter to smbd_fs_query() was 0,
		 * a successful return value means that dst_fqi->fq_fnode
		 * may be NULL.
		 */
		if (dst_fqi->fq_fnode)
			smb_node_release(dst_fqi->fq_fnode);

		rc = strcmp(src_fqi->fq_od_name, dst_fqi->fq_last_comp);
		if (rc == 0) {
			smb_node_end_crit(src_node);

			smb_node_release(src_node);
			smb_node_release(src_fqi->fq_dnode);
			smb_node_release(dst_fqi->fq_dnode);

			SMB_NULL_FQI_NODES(*src_fqi);
			SMB_NULL_FQI_NODES(*dst_fqi);
			return (0);
		}

		rc = smb_fsop_rename(sr, sr->user_cr,
		    src_fqi->fq_dnode,
		    src_fqi->fq_od_name,
		    dst_fqi->fq_dnode,
		    dst_fqi->fq_last_comp);

		if (rc != 0) {
			smb_node_release(src_fqi->fq_dnode);
			smb_node_release(dst_fqi->fq_dnode);

			SMB_NULL_FQI_NODES(*src_fqi);
			SMB_NULL_FQI_NODES(*dst_fqi);
		}

		smb_node_end_crit(src_node);

		smb_node_release(src_node);
		return (rc);
	}

	rc = smbd_fs_query(sr, dst_fqi, FQM_PATH_MUST_NOT_EXIST);
	if (rc != 0) {
		smb_node_end_crit(src_node);

		smb_node_release(src_node);
		smb_node_release(src_fqi->fq_dnode);

		SMB_NULL_FQI_NODES(*src_fqi);
		SMB_NULL_FQI_NODES(*dst_fqi);
		return (rc);
	}

	/*
	 * Because of FQM_PATH_MUST_NOT_EXIST and the successful return
	 * value, only dst_fqi->fq_dnode is valid (dst_fqi->fq_fnode
	 * is NULL).
	 */

	/*
	 * Use the unmangled form of the destination name if the
	 * source and destination names are the same and the source
	 * name is mangled.  (We are taking a chance here, assuming
	 * that this is what the user wants.)
	 */

	if ((smb_maybe_mangled_name(src_fqi->fq_last_comp)) &&
	    (strcmp(src_fqi->fq_last_comp, dst_fqi->fq_last_comp) == 0)) {
		dstname = src_fqi->fq_od_name;
	} else {
		dstname = dst_fqi->fq_last_comp;
	}

	rc = smb_fsop_rename(sr, sr->user_cr,
	    src_fqi->fq_dnode,
	    src_fqi->fq_od_name,
	    dst_fqi->fq_dnode,
	    dstname);

	if (rc != 0) {
		smb_node_release(src_fqi->fq_dnode);
		smb_node_release(dst_fqi->fq_dnode);

		SMB_NULL_FQI_NODES(*src_fqi);
		SMB_NULL_FQI_NODES(*dst_fqi);
	}

	smb_node_end_crit(src_node);

	smb_node_release(src_node);

	return (rc);
}
