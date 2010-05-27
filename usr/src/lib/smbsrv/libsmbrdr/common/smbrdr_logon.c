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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <pthread.h>
#include <string.h>
#include <strings.h>
#include <synch.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <smbsrv/wintypes.h>
#include <smbsrv/libsmbrdr.h>
#include <smbsrv/smb.h>
#include <smbrdr.h>

#define	SMBRDR_ANON_USER	"IPC$"

static int smbrdr_session_setupx(struct sdb_logon *);
static struct sdb_logon *smbrdr_logon_init(struct sdb_session *, char *,
    uint8_t *);
static int smbrdr_authenticate(char *, char *, uint8_t *);

/*
 * If the username is SMBRDR_ANON_USER, an anonymous session will be
 * established. Otherwise, an authenticated session will be established
 * based on the specified credentials.
 */
int
smbrdr_logon(char *domain_controller, char *domain, char *username)
{
	uint8_t pwd_hash[SMBAUTH_HASH_SZ];
	int rc;

	if (username == NULL || *username == 0) {
		smb_log(smbrdr_log_hdl, LOG_DEBUG, "smbrdr_logon: no username");
		return (-1);
	}

	bzero(pwd_hash, SMBAUTH_HASH_SZ);
	if (smb_strcasecmp(username, SMBRDR_ANON_USER, 0) != 0) {
		smb_ipc_get_passwd(pwd_hash, SMBAUTH_HASH_SZ);
		if (*pwd_hash == 0) {
			smb_log(smbrdr_log_hdl, LOG_DEBUG,
			    "smbrdr_logon: no password");
			return (-1);
		}
	}

	if (smbrdr_negotiate(domain_controller, domain) != 0) {
		smb_log(smbrdr_log_hdl, LOG_DEBUG,
		    "smbrdr_logon: negotiate failed");
		return (-1);
	}

	rc = smbrdr_authenticate(domain_controller, username, pwd_hash);
	return ((rc == AUTH_USER_GRANT) ? 0 : -1);

}

/*
 * Get the user session key from an already open named pipe.
 * The RPC library needs this.  See ndr_rpc_get_ssnkey()
 *
 * Returns zero (success) or an errno.
 */
int
smbrdr_get_ssnkey(int fid, unsigned char *ssn_key, size_t key_len)
{
	struct sdb_logon *logon;
	struct sdb_session *session;
	struct sdb_netuse *netuse;
	struct sdb_ofile *ofile;

	if (ssn_key == NULL || key_len < SMBAUTH_SESSION_KEY_SZ)
		return (EINVAL);

	ofile = smbrdr_ofile_get(fid);
	if (ofile == NULL)
		return (EBADF);

	netuse = ofile->netuse;
	session = netuse->session;
	logon = &session->logon;

	if (key_len > SMBAUTH_SESSION_KEY_SZ)
		bzero(ssn_key, key_len);
	bcopy(logon->ssn_key, ssn_key,
	    SMBAUTH_SESSION_KEY_SZ);

	smbrdr_ofile_put(ofile);
	return (0);
}

/*
 * smbrdr_authenticate
 *
 * This is the entry point for logging  a user onto the domain. The
 * session structure should have been obtained via a successful call
 * to smbrdr_smb_connect. We allocate a logon structure to hold the
 * user details and attempt to logon using smbrdr_session_setupx.
 * Note that we expect the password fields to have been encrypted
 * before this call.
 *
 * On success, the logon structure will be returned. Otherwise a null
 * pointer will be returned.
 */
static int
smbrdr_authenticate(char *server, char *username, uint8_t *pwd)
{
	struct sdb_session *session;
	struct sdb_logon *logon;
	struct sdb_logon old_logon;
	int ret;

	session = smbrdr_session_lock(server, SDB_SLCK_WRITE);
	if (session == NULL) {
		smb_log(smbrdr_log_hdl, LOG_DEBUG,
		    "smbrdr_authenticate: %s: no session with %s",
		    username, server);
		return (-1);
	}

	bzero(&old_logon, sizeof (struct sdb_logon));

	logon = &session->logon;
	if (logon->type != SDB_LOGON_NONE) {
		if (strcasecmp(logon->username, username) == 0) {
			/* The requested user has already been logged in */
			smbrdr_session_unlock(session);
			return ((logon->type == SDB_LOGON_GUEST)
			    ? AUTH_GUEST_GRANT : AUTH_USER_GRANT);
		}

		old_logon = *logon;
	}

	logon = smbrdr_logon_init(session, username, pwd);

	if (logon == NULL) {
		smb_log(smbrdr_log_hdl, LOG_DEBUG,
		    "smbrdr_authenticate: %s: %s", username, strerror(errno));
		smbrdr_session_unlock(session);
		return (-1);
	}

	if (smbrdr_session_setupx(logon) < 0) {
		free(logon);
		smbrdr_session_unlock(session);
		return (-1);
	}


	ret = (logon->type == SDB_LOGON_GUEST)
	    ? AUTH_GUEST_GRANT : AUTH_USER_GRANT;

	session->logon = *logon;
	free(logon);

	if (old_logon.type != SDB_LOGON_NONE)
		(void) smbrdr_logoffx(&old_logon);

	smbrdr_session_unlock(session);
	return (ret);
}


/*
 * smbrdr_session_setupx
 *
 * Build and send an SMB session setup command. This is used to log a
 * user onto the domain. See CIFS section 4.1.2.
 *
 * Returns 0 on success. Otherwise returns a -ve error code.
 */
static int
smbrdr_session_setupx(struct sdb_logon *logon)
{
	struct sdb_session *session;
	smb_hdr_t smb_hdr;
	smbrdr_handle_t srh;
	smb_msgbuf_t *mb;
	char *native_os;
	char *native_lanman;
	unsigned short data_bytes;
	unsigned short guest;
	unsigned long capabilities;
	unsigned short null_size;
	size_t (*strlen_fn)(const char *s);
	DWORD status;
	int rc;
	int64_t lmlevel;

	/*
	 * Paranoia check - we should never get this
	 * far without a valid session structure.
	 */
	if ((session = logon->session) == NULL)
		return (-1);

	if (session->remote_caps & CAP_UNICODE) {
		strlen_fn = smb_wcequiv_strlen;
		null_size = sizeof (smb_wchar_t);
		session->smb_flags2 |= SMB_FLAGS2_UNICODE;
	} else {
		strlen_fn = strlen;
		null_size = sizeof (char);
	}

	if (smbrdr_sign_init(session, logon) < 0) {
		smb_log(smbrdr_log_hdl, LOG_DEBUG,
		    "smbrdr_session_setupx: smbrdr_sign_init failed");
		return (-1);
	}

	status = smbrdr_request_init(&srh, SMB_COM_SESSION_SETUP_ANDX,
	    session, 0, 0);

	if (status != NT_STATUS_SUCCESS) {
		smbrdr_sign_fini(session);
		smb_log(smbrdr_log_hdl, LOG_DEBUG, "smbrdr_session_setupx: %s",
		    xlate_nt_status(status));
		return (-1);
	}
	mb = &srh.srh_mbuf;

	/*
	 * Regardless of the server's capabilities or what's
	 * reported in smb_flags2, we should report our full
	 * capabilities.
	 */
	capabilities = CAP_UNICODE | CAP_NT_SMBS | CAP_STATUS32;

	/*
	 * Compute the BCC for unicode or ASCII strings.
	 */
	data_bytes  = logon->auth.ci_len + logon->auth.cs_len + null_size;
	data_bytes += strlen_fn(session->native_os) + null_size;
	data_bytes += strlen_fn(session->native_lanman) + null_size;

	if (logon->type == SDB_LOGON_ANONYMOUS) {
		/*
		 * Anonymous logon: no username or domain name.
		 * We still need to include two null characters.
		 */
		data_bytes += (2 * null_size);

		rc = smb_msgbuf_encode(mb, "bb.wwwwlwwllwlu.u.",
		    13,				/* smb_wct */
		    0xff,			/* AndXCommand (none) */
		    32 + 26 + 3 + data_bytes,	/* AndXOffset */
		    SMBRDR_REQ_BUFSZ,		/* MaxBufferSize */
		    1,				/* MaxMpxCount */
		    0,				/* VcNumber */
		    0,				/* SessionKey */
		    1,				/* CaseInsensitivePassLength */
		    0,				/* CaseSensitivePassLength */
		    0,				/* Reserved */
		    capabilities,		/* Capabilities */
		    data_bytes,			/* smb_bcc */
		    0,				/* No user or domain */
		    session->native_os,		/* NativeOS */
		    session->native_lanman);	/* NativeLanMan */
	} else {
		data_bytes += strlen_fn(logon->username) + null_size;
		data_bytes += strlen_fn(session->domain) + null_size;

		rc = smb_msgbuf_encode(mb, "bb.wwwwlwwllw#c#cuuu.u.",
		    13,				/* smb_wct */
		    0xff,			/* AndXCommand (none) */
		    32 + 26 + 3 + data_bytes,	/* AndXOffset */
		    SMBRDR_REQ_BUFSZ,		/* MaxBufferSize */
		    1,				/* MaxMpxCount */
		    session->vc,		/* VcNumber */
		    session->sesskey,		/* SessionKey */
		    logon->auth.ci_len,		/* CaseInsensitivePassLength */
		    logon->auth.cs_len,		/* CaseSensitivePassLength */
		    0,				/* Reserved */
		    capabilities,		/* Capabilities */
		    data_bytes,			/* smb_bcc */
		    logon->auth.ci_len,		/* ci length spec */
		    logon->auth.ci,		/* CaseInsensitivePassword */
		    logon->auth.cs_len,		/* cs length spec */
		    logon->auth.cs,		/* CaseSensitivePassword */
		    logon->username,		/* AccountName */
		    session->domain,		/* PrimaryDomain */
		    session->native_os,		/* NativeOS */
		    session->native_lanman);	/* NativeLanMan */
	}

	if (rc <= 0) {
		smb_log(smbrdr_log_hdl, LOG_DEBUG,
		    "smbrdr_session_setupx: encode failed");
		smbrdr_handle_free(&srh);
		smbrdr_sign_fini(session);
		return (-1);
	}

	status = smbrdr_exchange(&srh, &smb_hdr, 0);

	if (status != NT_STATUS_SUCCESS) {
		smb_log(smbrdr_log_hdl, LOG_DEBUG, "smbrdr_session_setupx: %s",
		    xlate_nt_status(status));

		if (status == NT_STATUS_INVALID_PARAMETER) {
			rc = smb_config_getnum(SMB_CI_LM_LEVEL, &lmlevel);
			if (rc != SMBD_SMF_OK || lmlevel > 2)
				smb_log(smbrdr_log_hdl, LOG_DEBUG,
				    "If the DC is running Windows Server 2008: "
				    "apply hotfix KB 957441");
				smb_log(smbrdr_log_hdl, LOG_DEBUG,
				    "If the DC is running Windows Server 2008 "
				    "R2: do not apply the hotfix but update "
				    "the registry as described in KB 957441");
		}

		smbrdr_handle_free(&srh);
		smbrdr_sign_fini(session);
		return (-1);
	}

	rc = smb_msgbuf_decode(mb, "5.w2.u", &guest, &native_os);

	/*
	 * There was a problem in decoding response from
	 * a Samba 2.x PDC. This server sends strings in ASCII
	 * format and there is one byte with value 0 between
	 * native_os and native_lm:
	 *
	 *				 FF 53 4D 42 73 00		.SMBs.
	 * 00 00 00 88 01 00 00 00 00 00 00 00 00 00 00 00 ................
	 * 00 00 00 00 BB 00 64 00 00 00 03 FF 00 00 00 01 ......d.........
	 * 00 1C 00 55 6E 69 78 00 53 61 6D 62 61 20 32 2E ...Unix.Samba.2.
	 * 32 2E 38 61 00 53 41 4D 42 41 5F 44 4F 4D 00    2.8a.SAMBA_DOM.
	 *
	 * The byte doesn't seem to be padding because when change in
	 * native OS from Unix to Unix1 the 0 byte is still there:
	 *
	 *				 FF 53 4D 42 73 00		.SMBs.
	 * 00 00 00 88 01 00 00 00 00 00 00 00 00 00 00 00 ................
	 * 00 00 00 00 BB 00 64 00 00 00 03 FF 00 00 00 00 ......d.........
	 * 00 1D 00 55 6E 69 78 31 00 53 61 6D 62 61 20 32 ...Unix1.Samba.2
	 * 2E 32 2E 38 61 00 53 41 4D 42 41 5F 44 4F 4D 00 .2.8a.SAMBA_DOM.
	 */
	if (rc > 0) {
		if (session->remote_caps & CAP_UNICODE)
			rc = smb_msgbuf_decode(mb, "u", &native_lanman);
		else
			rc = smb_msgbuf_decode(mb, ".u", &native_lanman);
	}

	if (rc <= 0) {
		smb_log(smbrdr_log_hdl, LOG_DEBUG,
		    "smbrdr_session_setupx: decode failed");
		smbrdr_handle_free(&srh);
		smbrdr_sign_fini(session);
		return (-1);
	}

	session->remote_os = smbnative_os_value(native_os);
	session->remote_lm = smbnative_lm_value(native_lanman);
	session->pdc_type  = smbnative_pdc_value(native_lanman);

	logon->uid = smb_hdr.uid;
	if (guest)
		logon->type = SDB_LOGON_GUEST;

	smbrdr_handle_free(&srh);
	smbrdr_sign_unset_key(session);

	logon->state = SDB_LSTATE_SETUP;

	return (0);
}

/*
 * smbrdr_logoffx
 *
 * Build and send an SMB session logoff (SMB_COM_LOGOFF_ANDX) command.
 * This is the inverse of an SMB_COM_SESSION_SETUP_ANDX. See CIFS
 * section 4.1.3. The logon structure should have been obtained from a
 * successful call to smbrdr_authenticate.
 *
 * Returns 0 on success. Otherwise returns a -ve error code.
 */
int
smbrdr_logoffx(struct sdb_logon *logon)
{
	struct sdb_session *session;
	smbrdr_handle_t srh;
	smb_hdr_t smb_hdr;
	DWORD status;
	int rc;

	if (logon->state != SDB_LSTATE_SETUP) {
		/* No user to logoff */
		bzero(logon, sizeof (struct sdb_logon));
		return (0);
	}

	if ((session = logon->session) == 0) {
		bzero(logon, sizeof (struct sdb_logon));
		return (0);
	}

	logon->state = SDB_LSTATE_LOGGING_OFF;
	smbrdr_netuse_logoff(logon->uid);

	if ((session->state != SDB_SSTATE_NEGOTIATED) &&
	    (session->state != SDB_SSTATE_DISCONNECTING)) {
		bzero(logon, sizeof (struct sdb_logon));
		return (0);
	}

	status = smbrdr_request_init(&srh, SMB_COM_LOGOFF_ANDX,
	    session, logon, 0);

	if (status != NT_STATUS_SUCCESS) {
		logon->state = SDB_LSTATE_SETUP;
		smb_log(smbrdr_log_hdl, LOG_DEBUG, "smbrdr_logoffx: %s: %s",
		    logon->username, xlate_nt_status(status));
		return (-1);
	}

	rc = smb_msgbuf_encode(&srh.srh_mbuf, "bbbww", 2, 0xff, 0, 0, 0);
	if (rc < 0) {
		logon->state = SDB_LSTATE_SETUP;
		smbrdr_handle_free(&srh);
		smb_log(smbrdr_log_hdl, LOG_DEBUG,
		    "smbrdr_logoffx: %s: encode failed", logon->username);
		return (rc);
	}

	status = smbrdr_exchange(&srh, &smb_hdr, 0);
	if (status != NT_STATUS_SUCCESS) {
		smb_log(smbrdr_log_hdl, LOG_DEBUG, "smbrdr_logoffx: %s: %s",
		    logon->username, xlate_nt_status(status));
		rc = -1;
	} else {
		rc = 0;
	}

	bzero(logon, sizeof (struct sdb_logon));
	smbrdr_handle_free(&srh);
	return (rc);
}


/*
 * smbrdr_logon_init
 *
 * Find a slot for account logon information. The account information
 * is associated with a session so we need a valid session slot before
 * calling this function. If we already have a record of the specified
 * account, a pointer to that record is returned. Otherwise we attempt
 * to allocate a new one.
 */
static struct sdb_logon *
smbrdr_logon_init(struct sdb_session *session, char *username, uint8_t *pwd)
{
	struct sdb_logon *logon;
	int64_t smbrdr_lmcompl;
	int rc;

	logon = (struct sdb_logon *)malloc(sizeof (sdb_logon_t));
	if (logon == 0)
		return (0);

	bzero(logon, sizeof (struct sdb_logon));
	logon->session = session;

	(void) smb_config_getnum(SMB_CI_LM_LEVEL, &smbrdr_lmcompl);

	if (strcmp(username, "IPC$") == 0) {
		logon->type = SDB_LOGON_ANONYMOUS;
		logon->auth.ci_len = 1;
		*(logon->auth.ci) = 0;
		logon->auth.cs_len = 0;
	} else {
		logon->type = SDB_LOGON_USER;
		rc = smb_auth_set_info(username, 0, pwd,
		    session->domain, session->challenge_key,
		    session->challenge_len, smbrdr_lmcompl, &logon->auth);

		/* Generate (and save) the session key. */
		if (rc == 0) {
			rc = smb_auth_gen_session_key(&logon->auth,
			    logon->ssn_key);
		}

		if (rc != 0) {
			free(logon);
			return (0);
		}
	}

	(void) strlcpy(logon->username, username, MAX_ACCOUNT_NAME);
	logon->state = SDB_LSTATE_INIT;
	return (logon);
}
