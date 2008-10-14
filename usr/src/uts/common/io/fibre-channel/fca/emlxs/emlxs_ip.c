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
 * Copyright 2008 Emulex.  All rights reserved.
 * Use is subject to License terms.
 */


#include "emlxs.h"

/* Required for EMLXS_CONTEXT in EMLXS_MSGF calls */
EMLXS_MSG_DEF(EMLXS_IP_C);


extern int32_t
emlxs_ip_handle_event(emlxs_hba_t *hba, RING *rp, IOCBQ *iocbq)
{
	emlxs_port_t *port = &PPORT;
	IOCB *cmd;
	emlxs_buf_t *sbp;
	NODELIST *ndlp;

	cmd = &iocbq->iocb;

	HBASTATS.IpEvent++;

	sbp = (emlxs_buf_t *)iocbq->sbp;

	if (!sbp) {
		HBASTATS.IpStray++;

		EMLXS_MSGF(EMLXS_CONTEXT, &emlxs_stray_ip_completion_msg,
		    "cmd=0x%x iotag=0x%x status=0x%x perr=0x%x",
		    (uint32_t)cmd->ulpCommand, (uint32_t)cmd->ulpIoTag,
		    cmd->ulpStatus, cmd->un.ulpWord[4]);

		return (EIO);
	}
	if (rp->ringno != FC_IP_RING) {
		HBASTATS.IpStray++;

		return (0);
	}
	port = sbp->iocbq.port;

	switch (cmd->ulpCommand) {
		/*
		 *  Error:  Abnormal BCAST (Local error)
		 */
	case CMD_XMIT_BCAST_CN:
	case CMD_XMIT_BCAST64_CN:

		HBASTATS.IpBcastCompleted++;
		HBASTATS.IpBcastError++;

		EMLXS_MSGF(EMLXS_CONTEXT, &emlxs_ip_detail_msg,
		    "XMIT BCAST completion error cmd=0x%x status=0x%x "
		    "[%08x,%08x]", cmd->ulpCommand, cmd->ulpStatus,
		    cmd->un.ulpWord[4], cmd->un.ulpWord[5]);

		emlxs_pkt_complete(sbp, cmd->ulpStatus,
		    cmd->un.grsp.perr.statLocalError, 1);

		break;

		/*
		 *  Error:  Abnormal XMIT SEQUENCE (Local error)
		 */
	case CMD_XMIT_SEQUENCE_CR:
	case CMD_XMIT_SEQUENCE64_CR:

		HBASTATS.IpSeqCompleted++;
		HBASTATS.IpSeqError++;

		EMLXS_MSGF(EMLXS_CONTEXT, &emlxs_ip_detail_msg,
		    "XMIT SEQUENCE CR completion error: "
		    "cmd=%x status=0x%x [%08x,%08x]", cmd->ulpCommand,
		    cmd->ulpStatus, cmd->un.ulpWord[4], cmd->un.ulpWord[5]);

		emlxs_pkt_complete(sbp, cmd->ulpStatus,
		    cmd->un.grsp.perr.statLocalError, 1);

		break;

		/*
		 * Normal BCAST completion
		 */
	case CMD_XMIT_BCAST_CX:
	case CMD_XMIT_BCAST64_CX:

		HBASTATS.IpBcastCompleted++;
		HBASTATS.IpBcastGood++;

		EMLXS_MSGF(EMLXS_CONTEXT, &emlxs_ip_detail_msg,
		    "XMIT BCAST CN completion: cmd=%x status=0x%x [%08x,%08x]",
		    cmd->ulpCommand, cmd->ulpStatus, cmd->un.ulpWord[4],
		    cmd->un.ulpWord[5]);

		emlxs_pkt_complete(sbp, cmd->ulpStatus,
		    cmd->un.grsp.perr.statLocalError, 1);

		break;

		/*
		 * Normal XMIT SEQUENCE completion
		 */
	case CMD_XMIT_SEQUENCE_CX:
	case CMD_XMIT_SEQUENCE64_CX:

		HBASTATS.IpSeqCompleted++;

		EMLXS_MSGF(EMLXS_CONTEXT, &emlxs_ip_detail_msg,
		    "XMIT SEQUENCE CR completion: cmd=%x status=0x%x "
		    "[%08x,%08x]", cmd->ulpCommand, cmd->ulpStatus,
		    cmd->un.ulpWord[4], cmd->un.ulpWord[5]);

		if (cmd->ulpStatus) {
			HBASTATS.IpSeqError++;

			if ((cmd->ulpStatus == IOSTAT_LOCAL_REJECT) &&
			    ((cmd->un.ulpWord[4] & 0xff) == IOERR_NO_XRI)) {
				ndlp = (NODELIST *) sbp->node;
				if ((cmd->ulpContext == ndlp->nlp_Xri) &&
				    !(ndlp->nlp_flag[FC_IP_RING] &
				    NLP_RPI_XRI)) {
					ndlp->nlp_Xri = 0;
					(void) emlxs_create_xri(port, rp, ndlp);
				}
			}
		} else {
			HBASTATS.IpSeqGood++;
		}

		emlxs_pkt_complete(sbp, cmd->ulpStatus,
		    cmd->un.grsp.perr.statLocalError, 1);

		break;

	default:

		HBASTATS.IpStray++;

		EMLXS_MSGF(EMLXS_CONTEXT, &emlxs_invalid_ip_msg,
		    "Invalid iocb: cmd=0x%x", cmd->ulpCommand);

		break;

	}	/* switch(cmd->ulpCommand) */


	return (0);

} /* emlxs_ip_handle_event() */


extern int32_t
emlxs_ip_handle_unsol_req(emlxs_port_t *port, RING *rp, IOCBQ *iocbq,
    MATCHMAP *mp, uint32_t size)
{
	emlxs_hba_t *hba = HBA;
	fc_unsol_buf_t *ubp;
	IOCB *cmd;
	NETHDR *nd;
	NODELIST *ndlp;
	uint8_t *mac;
	emlxs_ub_priv_t *ub_priv;
	uint32_t sid;
	uint32_t i;
	uint32_t IpDropped = 1;
	uint32_t IpBcastReceived = 0;
	uint32_t IpSeqReceived = 0;

	cmd = &iocbq->iocb;
	ubp = NULL;

	for (i = 0; i < MAX_VPORTS; i++) {
		port = &VPORT(i);

		if (!(port->flag & EMLXS_PORT_BOUND) ||
		    !(port->flag & EMLXS_PORT_IP_UP)) {
			continue;
		}
		ubp = (fc_unsol_buf_t *)
		    emlxs_ub_get(port, size, FC_TYPE_IS8802_SNAP, 0);

		if (!ubp) {

			/* Theoretically we should never get here. */
			/*
			 * There should be one DMA buffer for every ub
			 * buffer.  If we are out of ub buffers
			 */
			/* then some how this matching has been corrupted */

			EMLXS_MSGF(EMLXS_CONTEXT, &emlxs_unsol_ip_dropped_msg,
			    "Buffer not found. paddr=%lx",
			    getPaddr(cmd->un.cont64[0].addrHigh,
			    cmd->un.cont64[0].addrLow));

			continue;
		}
		bcopy(mp->virt, ubp->ub_buffer, size);

		ub_priv = ubp->ub_fca_private;
		nd = (NETHDR *) ubp->ub_buffer;
		mac = nd->fc_srcname.IEEE;
		ndlp = emlxs_node_find_mac(port, mac);

		if (ndlp) {
			sid = ndlp->nlp_DID;

			if ((ndlp->nlp_Xri == 0) &&
			    !(ndlp->nlp_flag[FC_IP_RING] & NLP_RPI_XRI)) {
				(void) emlxs_create_xri(port, rp, ndlp);
			}
		}
		/*
		 * If no node is found, then check if this is a broadcast
		 * frame
		 */
		else if (cmd->un.xrseq.w5.hcsw.Fctl & BC) {
			sid = cmd->un.ulpWord[4] & 0x00ffffff;
		} else {
			/*
			 * We have to drop this frame because we do not have
			 * the S_ID of the request
			 */
			EMLXS_MSGF(EMLXS_CONTEXT, &emlxs_unsol_ip_dropped_msg,
			    "Node not found. mac=%02x%02x%02x%02x%02x%02x",
			    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

			(void) emlxs_ub_release((opaque_t)port, 1,
			    &ubp->ub_token);

			continue;
		}

		if (cmd->un.xrseq.w5.hcsw.Fctl & BC) {
			IpBcastReceived++;
		} else {
			IpSeqReceived++;
		}

		/*
		 * Setup frame header
		 */
		ubp->ub_frame.r_ctl = cmd->un.xrseq.w5.hcsw.Rctl;
		ubp->ub_frame.type = cmd->un.xrseq.w5.hcsw.Type;
		ubp->ub_frame.s_id = sid;
		ubp->ub_frame.ox_id = ub_priv->token;
		ubp->ub_frame.rx_id = cmd->ulpContext;
		ubp->ub_class = FC_TRAN_CLASS3;

		emlxs_ub_callback(port, ubp);
		IpDropped = 0;
	}
	port = &PPORT;

out:

	if (IpDropped) {
		HBASTATS.IpDropped++;
	}
	if (IpBcastReceived) {
		HBASTATS.IpBcastReceived++;
	}
	if (IpSeqReceived) {
		HBASTATS.IpSeqReceived++;
	}
	return (0);

} /* emlxs_ip_handle_unsol_req() */


extern int32_t
emlxs_ip_handle_rcv_seq_list(emlxs_hba_t *hba, RING *rp, IOCBQ *iocbq)
{
	emlxs_port_t *port = &PPORT;
	IOCB *cmd;
	uint64_t bdeAddr;
	MATCHMAP *mp = NULL;
#ifdef SLI3_SUPPORT
	HBQE_t *hbqE;
	uint32_t hbq_id;
	uint32_t hbqe_tag;
#endif	/* SLI3_SUPPORT */

	/*
	 * No action required for now.
	 */
	cmd = &iocbq->iocb;

	HBASTATS.IpRcvEvent++;

	EMLXS_MSGF(EMLXS_CONTEXT, &emlxs_ip_detail_msg,
	    "Receive sequence list: cmd=0x%x iotag=0x%x status=0x%x "
	    "w4=0x%x ringno=0x%x", cmd->ulpCommand, cmd->ulpIoTag,
	    cmd->ulpStatus, cmd->un.ulpWord[4], rp->ringno);

	if (cmd->ulpStatus) {
		goto out;
	}
#ifdef SLI3_SUPPORT
	hbqE = (HBQE_t *)&iocbq->iocb;
	hbq_id = hbqE->unt.ext.HBQ_tag;
	hbqe_tag = hbqE->unt.ext.HBQE_tag;

	if (hba->flag & FC_HBQ_ENABLED) {
		HBQ_INIT_t *hbq;

		hbq = &hba->hbq_table[hbq_id];

		HBASTATS.IpUbPosted--;

		if (hbqe_tag >= hbq->HBQ_numEntries) {
			mp = NULL;
		} else {
			mp = hba->hbq_table[hbq_id].HBQ_PostBufs[hbqe_tag];
		}
	} else
#endif	/* SLI3_SUPPORT */
	{
		/* Check for valid buffer */
		if (!(cmd->un.cont64[0].tus.f.bdeFlags & BUFF_TYPE_INVALID)) {
			bdeAddr = getPaddr(cmd->un.cont64[0].addrHigh,
			    cmd->un.cont64[0].addrLow);
			mp = emlxs_mem_get_vaddr(hba, rp, bdeAddr);
		}
	}

out:

#ifdef SLI3_SUPPORT
	if (hba->flag & FC_HBQ_ENABLED) {
		emlxs_update_HBQ_index(hba, hbq_id);
	} else
#endif	/* SLI3_SUPPORT */
	{
		if (mp) {
			(void) emlxs_mem_put(hba, MEM_IPBUF, (uint8_t *)mp);
		}
		(void) emlxs_post_buffer(hba, rp, 1);
	}

	HBASTATS.IpDropped++;

	return (0);

} /* emlxs_ip_handle_rcv_seq_list() */



/*
 * Process a create_xri command completion.
 */
extern int32_t
emlxs_handle_create_xri(emlxs_hba_t *hba, RING *rp, IOCBQ *iocbq)
{
	emlxs_port_t *port = &PPORT;
	IOCB *cmd;
	NODELIST *ndlp;
	fc_packet_t *pkt;
	emlxs_buf_t *sbp;

	cmd = &iocbq->iocb;

	sbp = (emlxs_buf_t *)iocbq->sbp;

	if (!sbp) {
		EMLXS_MSGF(EMLXS_CONTEXT, &emlxs_stray_ip_completion_msg,
		    "create_xri: cmd=0x%x iotag=0x%x status=0x%x w4=0x%x",
		    cmd->ulpCommand, cmd->ulpIoTag, cmd->ulpStatus,
		    cmd->un.ulpWord[4]);

		return (EIO);
	}
	/* check for first xmit completion in sequence */
	ndlp = (NODELIST *)sbp->node;

	if (cmd->ulpStatus) {
		EMLXS_MSGF(EMLXS_CONTEXT, &emlxs_bad_ip_completion_msg,
		    "create_xri: cmd=0x%x iotag=0x%x status=0x%x w4=0x%x",
		    cmd->ulpCommand, cmd->ulpIoTag, cmd->ulpStatus,
		    cmd->un.ulpWord[4]);

		mutex_enter(&EMLXS_RINGTX_LOCK);
		ndlp->nlp_flag[rp->ringno] &= ~NLP_RPI_XRI;
		mutex_exit(&EMLXS_RINGTX_LOCK);

		return (EIO);
	}
	mutex_enter(&EMLXS_RINGTX_LOCK);
	ndlp->nlp_Xri = cmd->ulpContext;
	ndlp->nlp_flag[rp->ringno] &= ~NLP_RPI_XRI;
	mutex_exit(&EMLXS_RINGTX_LOCK);

	EMLXS_MSGF(EMLXS_CONTEXT, &emlxs_ip_detail_msg,
	    "create_xri completed: DID=0x%x Xri=0x%x iotag=0x%x",
	    ndlp->nlp_DID, ndlp->nlp_Xri, cmd->ulpIoTag);

	pkt = sbp->pkt;
	emlxs_pkt_free(pkt);

	return (0);

} /* emlxs_handle_create_xri()  */


/*
 * Issue an iocb command to create an exchange with
 * the remote Nport specified by the NODELIST entry.
 */
extern int32_t
emlxs_create_xri(emlxs_port_t *port, RING *rp, NODELIST *ndlp)
{
	emlxs_hba_t *hba = HBA;
	IOCB *icmd;
	IOCBQ *iocbq;
	fc_packet_t *pkt;
	emlxs_buf_t *sbp;
	uint16_t iotag;

	/* Check if an XRI has already been requested */
	mutex_enter(&EMLXS_RINGTX_LOCK);
	if (ndlp->nlp_Xri != 0 || (ndlp->nlp_flag[rp->ringno] & NLP_RPI_XRI)) {
		mutex_exit(&EMLXS_RINGTX_LOCK);
		return (0);
	}
	ndlp->nlp_flag[rp->ringno] |= NLP_RPI_XRI;
	mutex_exit(&EMLXS_RINGTX_LOCK);

	if (!(pkt = emlxs_pkt_alloc(port, 0, 0, 0, KM_NOSLEEP))) {
		EMLXS_MSGF(EMLXS_CONTEXT, &emlxs_ip_detail_msg,
		    "create_xri failed: Unable to allocate pkt. did=0x%x",
		    ndlp->nlp_DID);

		goto fail;
	}
	sbp = (emlxs_buf_t *)pkt->pkt_fca_private;
	iocbq = &sbp->iocbq;

	/* Get the iotag by registering the packet */
	iotag = emlxs_register_pkt(rp, sbp);

	if (!iotag) {
		/*
		 * No more command slots available, retry later
		 */
		emlxs_pkt_free(pkt);

		EMLXS_MSGF(EMLXS_CONTEXT, &emlxs_ip_detail_msg,
		    "create_xri failed: Unable to allocate IOTAG. did=0x%x",
		    ndlp->nlp_DID);

		goto fail;
	}
	icmd = &iocbq->iocb;
	icmd->ulpIoTag = iotag;
	icmd->ulpContext = ndlp->nlp_Rpi;
	icmd->ulpLe = 1;
	icmd->ulpCommand = CMD_CREATE_XRI_CR;
	icmd->ulpOwner = OWN_CHIP;

	/* Initalize iocbq */
	iocbq->port = (void *)port;
	iocbq->node = (void *)ndlp;
	iocbq->ring = (void *)rp;

	mutex_enter(&sbp->mtx);
	sbp->node = (void *)ndlp;
	sbp->ring = rp;
	mutex_exit(&sbp->mtx);

	EMLXS_MSGF(EMLXS_CONTEXT, &emlxs_ip_detail_msg,
	    "create_xri sent: DID=0x%x Xri=0x%x iotag=0x%x",
	    ndlp->nlp_DID, ndlp->nlp_Xri, iotag);

	emlxs_issue_iocb_cmd(hba, rp, iocbq);

	return (0);

fail:

	/* Clear the XRI flag */
	mutex_enter(&EMLXS_RINGTX_LOCK);
	ndlp->nlp_flag[rp->ringno] &= ~NLP_RPI_XRI;
	mutex_exit(&EMLXS_RINGTX_LOCK);

	return (1);

} /* emlxs_create_xri() */
