/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 *
 *	Openvision retains the copyright to derivative works of
 *	this source code.	Do *NOT* create a derivative of this
 *	source code before consulting with your legal department.
 *	Do *NOT* integrate *ANY* of this source code into another
 *	product before consulting with your legal department.
 *
 *	For further information, read the top-level Openvision
 *	copyright which is contained in the top-level MIT Kerberos
 *	copyright.
 *
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 *
 */


/*
 * Copyright 1993 OpenVision Technologies, Inc., All Rights Reserved
 */

/*
 * SUNWresync121 XXX
 * Beware future resyncers, this file is much diff from MIT (1.0...)
 */

#include <stdio.h>
#include <stdio_ext.h>
#include <signal.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>	/* inet_ntoa */
#include <netdb.h>
#include <gssapi/gssapi.h>
#include <rpc/rpc.h>
#include <kadm5/admin.h>
#include <kadm5/kadm_rpc.h>
#include <kadm5/server_internal.h>
#include <server_acl.h>
#include <krb5/adm_proto.h>
#include <string.h>
#include <gssapi_krb5.h>
#include <libintl.h>
#include <locale.h>
#include <sys/resource.h>
#include <kdb/kdb_log.h>

#include <rpc/rpcsec_gss.h>

#ifndef	FD_SETSIZE
#define	FD_SETSIZE	256
#endif

#ifndef MAX
#define	MAX(a, b)	(((a) > (b)) ? (a) : (b))
#endif

static int signal_request_exit = 0;
static int schpw;
kadm5_config_params chgpw_params;
void kadm_svc_run(void);
void setup_signal_handlers(iprop_role iproprole);
void sig_exit(int);
void sig_pipe(int);
krb5_error_code log_kt_error(char*, char*);

#ifdef POSIX_SIGNALS
static struct sigaction s_action;
#endif /* POSIX_SIGNALS */

#define	TIMEOUT	15

typedef struct _auth_gssapi_name {
	char *name;
	gss_OID type;
} auth_gssapi_name;

gss_name_t gss_changepw_name = NULL, gss_oldchangepw_name = NULL;
void *global_server_handle;

/*
 * This is a kludge, but the server needs these constants to be
 * compatible with old clients.	They are defined in <kadm5/admin.h>,
 * but only if USE_KADM5_API_VERSION == 1.
 */
#define	OVSEC_KADM_ADMIN_SERVICE_P	"ovsec_adm@admin"
#define	OVSEC_KADM_CHANGEPW_SERVICE_P	"ovsec_adm@changepw"

/*
 * This enables us to set the keytab that gss_acquire_cred uses, but
 * it also restricts us to linking against the Kv5 GSS-API library.
 * Since this is *k*admind, that shouldn't be a problem.
 */
extern char *krb5_overridekeyname;

extern void krb5_iprop_prog_1();
extern kadm5_ret_t kiprop_get_adm_host_srv_name(
	krb5_context,
	const char *,
	char **);

static krb5_context context;  /* XXX yuck.  the signal handlers need this */

in_port_t l_port = 0;	/* global local port num, for BSM audits */

int nofork = 0; /* global; don't fork (debug mode) */


/*
 * Function: usage
 *
 * Purpose: print out the server usage message
 *
 * Arguments:
 * Requires:
 * Effects:
 * Modifies:
 */

void
usage()
{
	fprintf(stderr, gettext("Usage: kadmind [-r realm] [-m] [-d] "
	    "[-p port-number]\n"));
	exit(1);
}

/*
 * Function: display_status
 *
 * Purpose: displays GSS-API messages
 *
 * Arguments:
 *
 * 	msg		a string to be displayed with the message
 * 	maj_stat	the GSS-API major status code
 * 	min_stat	the GSS-API minor status code
 *
 * Effects:
 *
 * The GSS-API messages associated with maj_stat and min_stat are
 * displayed on stderr, each preceeded by "GSS-API error <msg>: " and
 * followed by a newline.
 */
static void display_status_1();

void display_status(msg, maj_stat, min_stat)
     char *msg;
     OM_uint32 maj_stat;
     OM_uint32 min_stat;
{
     display_status_1(msg, maj_stat, GSS_C_GSS_CODE);
     display_status_1(msg, min_stat, GSS_C_MECH_CODE);
}

static void display_status_1(m, code, type)
     char *m;
     OM_uint32 code;
     int type;
{
	OM_uint32 maj_stat, min_stat;
	gss_buffer_desc msg;
	OM_uint32 msg_ctx;
     
	msg_ctx = 0;
	while (1) {
		maj_stat = gss_display_status(&min_stat, code,
					      type, GSS_C_NULL_OID,
					      &msg_ctx, &msg);
		fprintf(stderr, "GSS-API error %s: %s\n", m,
			(char *)msg.value); 
		(void) gss_release_buffer(&min_stat, &msg);
	  
		if (!msg_ctx)
			break;
	}
}


/*
 * Solaris Kerberos: the following prototypes are needed because these are 
 * private interfaces that do not have prototypes in any .h 
 */

extern struct hostent   *res_getipnodebyaddr(const void *, size_t, int, int *); 
extern void             res_freehostent(struct hostent *); 

static void
freedomnames(char **npp)
{
	char **tpp;

	if (npp) {
		tpp = npp;
		while (*tpp++) {
			free(*(tpp-1));
		}
		free(npp);
	}
}

/*
 * Construct a list of uniq FQDNs of all the net interfaces (except
 * krb5.conf master dups) and return it in arg 'dnames'.
 * 
 * On successful return (0), caller must call freedomnames()
 * to free memory.
 */
static int
getdomnames(krb5_context ctx, char *realm, char ***dnames)
{
	krb5_address **addresses = NULL;
	krb5_address *a = NULL;
	struct hostent *hp = NULL;
	int ret, i, result=0, error;
	char **npp = NULL, **tpp=NULL;
	int dup=0, n = 0;
	char *cfhost = NULL; /* krb5 conf file master hostname */

	if (ret = kadm5_get_master(ctx, realm, &cfhost)) {
		return (ret);
	}

	ret = krb5_os_localaddr(ctx, &addresses); 
	if (ret != 0) { 
		if (nofork)
			(void) fprintf(stderr,
				    "kadmind: get localaddrs failed: %s",
				    error_message(ret));
		result = ret;
		goto err;
	} 


	for (i=0; addresses[i]; i++) {
		a = addresses[i];
		hp = res_getipnodebyaddr(a->contents, a->length, 
					a->addrtype == ADDRTYPE_INET
					? AF_INET : AF_INET6,
					&error);
		if (hp != NULL) {

			/* skip master host in krb5.conf */
			if (strcasecmp(cfhost, hp->h_name) == 0) {
				res_freehostent(hp); 
				hp = NULL; 
				continue;
			}
				
			dup = 0;
			tpp = npp;
			/* skip if hostname already exists in list */
			while (tpp && *tpp++) {
				if (strcasecmp(*(tpp-1), hp->h_name) == 0) {
					dup++;
					break;
				}
			}

			if (dup) {
				res_freehostent(hp); 
				hp = NULL; 
				continue;
			}

			npp = realloc(npp, sizeof(char *) * (n + 2));
			if (!npp) {
				result = ENOMEM;
				goto err;
			}
			npp[n] = strdup(hp->h_name);
			if (!npp[n]) {
				result = ENOMEM;
				goto err;
			}
			npp[n+1] = NULL;
			n++;

			res_freehostent(hp); 
			hp = NULL; 
			result = 0;
		}
			
	}

#ifdef DEBUG
	printf("getdomnames: n=%d, i=%d, npp=%p\n", n, i, npp);
	tpp = npp;
	while (tpp && *tpp++) {
		printf("tpp=%s\n", *(tpp-1));
	}
#endif

	goto out;

err:
	if (npp) {
		freedomnames(npp);
		npp = NULL;
	}

	if (hp) {
		res_freehostent(hp); 
		hp = NULL;
	}

out:
	if (cfhost) {
		free (cfhost);
		cfhost = NULL;
	}
	if (addresses) {
		krb5_free_addresses(ctx, addresses);
		addresses = NULL;
	}

	if (result == 0)
		*dnames = npp;

	return (result);
}

/*
 * Set the rpcsec_gss svc names for all net interfaces.
 */
static void
set_svc_domnames(char *svcname, char **dnames,
		u_int program, u_int version)
{
	bool_t ret;
	char **tpp = dnames;

	if (!tpp)
		return;

	while (*tpp++) {
		/* MAX_NAME_LEN from rpc/rpcsec_gss.h */
		char name[MAXHOSTNAMELEN+MAX_NAME_LEN+2] = {0};
		(void) snprintf(name, sizeof(name), "%s@%s",
				svcname, *(tpp-1)); 
		ret = rpc_gss_set_svc_name(name,
					"kerberos_v5", 0,
					program, version);
		if (nofork && ret)
			(void) fprintf(stderr,
				    "rpc_gss_set_svc_name success: %s\n",
				    name);
	}
}




int
main(int argc, char *argv[])
{
	void kadm_1(struct svc_req *, SVCXPRT *);
	SVCXPRT *transp;
	extern char *optarg;
	extern int optind, opterr;
	int ret, rlen, oldnames = 0;
	OM_uint32 OMret, major_status, minor_status;
	char *whoami;
	FILE *acl_file;
	gss_buffer_desc in_buf;
	struct servent *srv;
	struct sockaddr_in addr;
	struct sockaddr_in *sin;
	int s;
	int optchar;
	struct netconfig *nconf;
	void *handlep;
	int fd;
	struct t_info tinfo;
	struct t_bind tbindstr, *tres;

	struct t_optmgmt req, resp;
	struct opthdr *opt;
	char reqbuf[128];
	struct rlimit rl;

	char *kiprop_name = NULL; /* IProp svc name */
	kdb_log_context *log_ctx;
	kadm5_server_handle_t handle;
	krb5_context ctx;

	kadm5_config_params params;
	auth_gssapi_name names[6];
     	gss_buffer_desc gssbuf;
     	gss_OID nt_krb5_name_oid;

	char **dnames = NULL;
	int retdn;

	/* This is OID value the Krb5_Name NameType */
     	gssbuf.value = "{1 2 840 113554 1 2 2 1}";
     	gssbuf.length = strlen(gssbuf.value);
     	major_status = gss_str_to_oid(&minor_status, &gssbuf,
				    &nt_krb5_name_oid);
     	if (major_status != GSS_S_COMPLETE) {
		fprintf(stderr,
			gettext("Couldn't create KRB5 Name NameType OID\n"));
		display_status("str_to_oid", major_status, minor_status);
		exit(1);
     	}

	names[0].name = names[1].name = names[2].name = 
		names[3].name = names[4].name  = names[5].name =NULL;
	names[0].type = names[1].type = names[2].type = 
		names[3].type = names[4].type = names[5].type =
		(gss_OID) nt_krb5_name_oid;

	whoami = (strrchr(argv[0], '/') ? strrchr(argv[0], '/') + 1 : argv[0]);

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)  /* Should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it weren't */
#endif

	(void) textdomain(TEXT_DOMAIN);

	nofork = 0;

	memset((char *) &params, 0, sizeof (params));

	while ((optchar = getopt(argc, argv, "r:mdp:")) != EOF) {
		switch (optchar) {
		case 'r':
			if (!optarg)
				usage();
			params.realm = optarg;
			params.mask |= KADM5_CONFIG_REALM;
			break;
		case 'm':
			params.mkey_from_kbd = 1;
			params.mask |= KADM5_CONFIG_MKEY_FROM_KBD;
			break;
		case 'd':
			nofork = 1;
			break;
		case 'p':
			if (!optarg)
				usage();
			params.kadmind_port = atoi(optarg);
			params.mask |= KADM5_CONFIG_KADMIND_PORT;
			break;
		case '?':
		default:
			usage();
		}
	}


	if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
		rl.rlim_cur = rl.rlim_max = MAX(rl.rlim_max, FD_SETSIZE);
		(void) setrlimit(RLIMIT_NOFILE, &rl);
		(void) enable_extended_FILE_stdio(-1, -1);
	}

	if (!nofork && (ret = daemon(0, 0))) {
		ret = errno;
		krb5_klog_syslog(LOG_ERR,
		    gettext("Cannot detach from tty: %s"),
		    error_message(ret));
		fprintf(stderr, gettext("%s: Cannot detach from tty: %s\n"),
		    whoami, error_message(ret));
		krb5_klog_close(context);
		exit(1);
	}

	if (ret = krb5_init_context(&context)) {
		fprintf(stderr,
		    gettext("%s: %s while initializing context, aborting\n"),
		    whoami, error_message(ret));
		exit(1);
	}

	krb5_klog_init(context, "admin_server", whoami, 1);


	/*
	 * When using the Horowitz/IETF protocol for
	 * password changing, the default port is 464
	 * (officially recognized by IANA)
	 *
	 * DEFAULT_KPASSWD_PORT -> 464
	 */
	chgpw_params.kpasswd_port = DEFAULT_KPASSWD_PORT;
	chgpw_params.mask |= KADM5_CONFIG_KPASSWD_PORT;
	chgpw_params.kpasswd_protocol = KRB5_CHGPWD_CHANGEPW_V2;
	chgpw_params.mask |= KADM5_CONFIG_KPASSWD_PROTOCOL;

	if (ret = kadm5_get_config_params(context, NULL, NULL, &chgpw_params,
		&chgpw_params)) {
		krb5_klog_syslog(LOG_ERR, gettext("%s: %s while initializing,"
				" aborting"), whoami, error_message(ret));
		fprintf(stderr,
		    gettext("%s: %s while initializing, aborting\n"),
		    whoami, error_message(ret));
		krb5_klog_close(context);
		exit(1);
	}

	/*
	 * We now setup the socket and bind() to port 464, so that
	 * kadmind can now listen to and process change-pwd requests
	 * from non-Solaris Kerberos V5 clients such as Microsoft,
	 * MIT, AIX, HP etc
	 */
	if ((schpw = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		krb5_klog_syslog(LOG_ERR, gettext( "cannot create simple "
				"chpw socket: %s"), error_message(errno));
		fprintf(stderr, gettext("Cannot create simple chpw "
					"socket: %s"), error_message(errno));
		krb5_klog_close(context);
		exit(1);
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(chgpw_params.kpasswd_port);

	if (bind(schpw, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		char portbuf[32];
		int oerrno = errno;
		fprintf(stderr, gettext("%s: Cannot bind socket.\n"), whoami);
		fprintf(stderr, gettext("bind: %s\n"), error_message(oerrno));
		errno = oerrno;
		(void) snprintf(portbuf, sizeof (portbuf), "%d",
				ntohs(addr.sin_port));
		krb5_klog_syslog(LOG_ERR, gettext("cannot bind simple "
				"chpw socket: %s"), error_message(oerrno));
		if(oerrno == EADDRINUSE) {
			char *w = strrchr(whoami, '/');
			if (w) {
				w++;
			}
			else {
				w = whoami;
			}
			fprintf(stderr, gettext(
				"This probably means that another %s process\n"
				"is already running, or that another program\n"
				"is using the server port (number %d).\n"
				"If another %s is already running, you should\n"
				"kill it before restarting the server.\n"),
				w, ntohs(addr.sin_port), w);
		}
		krb5_klog_close(context);
		exit(1);
	}

	if (ret = kadm5_get_config_params(context, NULL, NULL, &params,
		&params)) {
		krb5_klog_syslog(LOG_ERR, gettext("%s: %s while initializing,"
				" aborting"), whoami, error_message(ret));
		fprintf(stderr,
		    gettext("%s: %s while initializing, aborting\n"),
		    whoami, error_message(ret));
		krb5_klog_close(context);
		exit(1);
	}
#define	REQUIRED_PARAMS (KADM5_CONFIG_REALM | KADM5_CONFIG_ACL_FILE | \
			KADM5_CONFIG_ADMIN_KEYTAB)

	if ((params.mask & REQUIRED_PARAMS) != REQUIRED_PARAMS) {
		krb5_klog_syslog(LOG_ERR,
		    gettext("%s: Missing required configuration values "
			"while initializing, aborting"), whoami,
		    (params.mask & REQUIRED_PARAMS) ^ REQUIRED_PARAMS);
		fprintf(stderr,
		    gettext("%s: Missing required configuration values "
			"(%x) while initializing, aborting\n"), whoami,
		    (params.mask & REQUIRED_PARAMS) ^ REQUIRED_PARAMS);
		krb5_klog_close(context);
		exit(1);
	}
	memset((char *) &addr, 0, sizeof (struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	l_port = addr.sin_port = htons(params.kadmind_port);
	sin = &addr;

	if ((handlep = setnetconfig()) == (void *) NULL) {
		(void) krb5_klog_syslog(LOG_ERR,
		    gettext("cannot get any transport information"));
		krb5_klog_close(context);
		exit(1);
	}
	while (nconf = getnetconfig(handlep)) {
		if ((nconf->nc_semantics == NC_TPI_COTS_ORD) &&
		    (strcmp(nconf->nc_protofmly, NC_INET) == 0) &&
		    (strcmp(nconf->nc_proto, NC_TCP) == 0))
			break;
	}

	if (nconf == (struct netconfig *) NULL) {
		(void) endnetconfig(handlep);
		krb5_klog_close(context);
		exit(1);
	}
	fd = t_open(nconf->nc_device, O_RDWR, &tinfo);
	if (fd == -1) {
		krb5_klog_syslog(LOG_ERR,
		    gettext("unable to open connection for ADMIN server"));
		krb5_klog_close(context);
		exit(1);
	}
	/* LINTED */
	opt = (struct opthdr *) reqbuf;
	opt->level = SOL_SOCKET;
	opt->name = SO_REUSEADDR;
	opt->len = sizeof (int);

	/*
	 * The option value is "1".  This will allow the server to restart
	 * whilst the previous process is cleaning up after itself in a
	 * FIN_WAIT_2 or TIME_WAIT state.  If another process is started
	 * outside of smf(5) then bind will fail anyway, which is what we want.
	 */
	reqbuf[sizeof (struct opthdr)] = 1;

	req.flags = T_NEGOTIATE;
	req.opt.len = sizeof (struct opthdr) + opt->len;
	req.opt.buf = (char *) opt;

	resp.flags = 0;
	resp.opt.buf = reqbuf;
	resp.opt.maxlen = sizeof (reqbuf);

	if (t_optmgmt(fd, &req, &resp) < 0 || resp.flags != T_SUCCESS) {
		t_error("t_optmgmt");
		exit(1);
	}
	/* Transform addr to netbuf */

	tres = (struct t_bind *) t_alloc(fd, T_BIND, T_ADDR);
	if (tres == NULL) {
		(void) t_close(fd);
		(void) krb5_klog_syslog(LOG_ERR,
					gettext("cannot allocate netbuf"));
		krb5_klog_close(context);
		exit(1);
	}
	tbindstr.qlen = 8;
	tbindstr.addr.buf = (char *) sin;
	tbindstr.addr.len = tbindstr.addr.maxlen = __rpc_get_a_size(tinfo.addr);
	sin = (struct sockaddr_in *) tbindstr.addr.buf;
	/* SUNWresync121 XXX (void) memset(&addr, 0, sizeof(addr)); */

	if (t_bind(fd, &tbindstr, tres) < 0) {
		int oerrno = errno;
		fprintf(stderr, gettext("%s: Cannot bind socket.\n"), whoami);
		fprintf(stderr, gettext("bind: %s\n"), error_message(oerrno));
		errno = oerrno;
		krb5_klog_syslog(LOG_ERR, gettext("Cannot bind socket: %s"),
		    error_message(errno));
		if (oerrno == EADDRINUSE) {
			char *w = strrchr(whoami, '/');

			if (w) {
				w++;
			} else {
				w = whoami;
			}
			fprintf(stderr, gettext(
				"This probably means that another %s "
				"process is already\n"
				"running, or that another program is using "
				"the server port (number %d)\n"
				"after being assigned it by the RPC "
				"portmap deamon.	If another\n"
				"%s is already running, you should kill "
				"it before\n"
				"restarting the server. If, on the other hand, "
				"another program is\n"
				"using the server port, you should kill it "
				"before running\n"
				"%s, and ensure that the conflict does "
				"not occur in the\n"
				"future by making sure that %s is started "
				"on reboot\n"
				"before portmap.\n"),
			    w, ntohs(addr.sin_port), w, w, w);
			krb5_klog_syslog(LOG_ERR,
			    gettext("Check for already-running %s or for "
				"another process using port %d"), w,
			    htons(addr.sin_port));
		}
		krb5_klog_close(context);
		exit(1);
	}
	transp = svc_tli_create(fd, nconf, NULL, 0, 0);
	(void) t_free((char *) tres, T_BIND);
	if (transp == NULL) {
		fprintf(stderr, gettext("%s: Cannot create RPC service.\n"),
			whoami);
		krb5_klog_syslog(LOG_ERR, gettext("Cannot create RPC service: %m"));
		krb5_klog_close(context);
		exit(1);
	}
	if (!svc_register(transp, KADM, KADMVERS, kadm_1, 0)) {
		fprintf(stderr,
		    gettext("%s: Cannot register RPC service.\n"), whoami);
		krb5_klog_syslog(LOG_ERR,
		    gettext("Cannot register RPC service, failing."));
		krb5_klog_close(context);
		exit(1);
	}

	/*
	 * XXX krb5_defkeyname is an internal library global and should go
	 * away
	 */
	krb5_overridekeyname = params.admin_keytab;

	/* Solaris Kerberos:
	 * The only service principals which matter here are
	 *  -> names[0].name (kadmin/<fqdn>)
	 *  -> names[1].name (changepw/<fqdn>)
	 * KADM5_ADMIN_SERVICE_P, KADM5_CHANGEPW_SERVICE_P,
	 * OVSEC_KADM_ADMIN_SERVICE_P, OVSEC_KADM_CHANGEPW_SERVICE_P
	 * are all legacy service princs and calls to rpc_gss_set_svc_name()
	 * using these principals will always fail as they are not host
	 * based principals.
	 */

	(void) kadm5_get_adm_host_srv_name(context,
					   params.realm, &names[0].name);
	(void) kadm5_get_cpw_host_srv_name(context,
					   params.realm, &names[1].name);
	names[2].name = KADM5_ADMIN_SERVICE_P;
	names[3].name = KADM5_CHANGEPW_SERVICE_P;
	names[4].name = OVSEC_KADM_ADMIN_SERVICE_P;
	names[5].name = OVSEC_KADM_CHANGEPW_SERVICE_P;

	if (names[0].name == NULL || names[1].name == NULL ||
	    names[2].name == NULL || names[3].name == NULL ||
	    names[4].name == NULL || names[5].name == NULL) {
		krb5_klog_syslog(LOG_ERR,
		    gettext("Cannot initialize GSS-API authentication, "
			"failing."));
		fprintf(stderr,
		    gettext("%s: Cannot initialize "
			"GSS-API authentication.\n"),
		    whoami);
		krb5_klog_close(context);
		exit(1);
	}


	/*
	 * Try to acquire creds for the old OV services as well as the new
	 * names, but if that fails just fall back on the new names.
	 */
	
	if (rpc_gss_set_svc_name(names[5].name,
				"kerberos_v5", 0, KADM, KADMVERS) &&
	    rpc_gss_set_svc_name(names[4].name,
				"kerberos_v5", 0, KADM, KADMVERS))
		oldnames++;
	if (rpc_gss_set_svc_name(names[3].name,
				"kerberos_v5", 0, KADM, KADMVERS))
		oldnames++;
	if (rpc_gss_set_svc_name(names[2].name,
				"kerberos_v5", 0, KADM, KADMVERS))
		oldnames++;

    /* If rpc_gss_set_svc_name() fails for either kadmin/<fqdn> or
     * for changepw/<fqdn> then try to determine if this is caused
     * by a missing keytab file or entry. If so, log it and continue.
     */
	if (rpc_gss_set_svc_name(names[0].name,
				"kerberos_v5", 0, KADM, KADMVERS))
		oldnames++;
	else
		log_kt_error(names[0].name, whoami);
	if (rpc_gss_set_svc_name(names[1].name,
				"kerberos_v5", 0, KADM, KADMVERS))
		oldnames++;
	else
		log_kt_error(names[1].name, whoami);

	retdn = getdomnames(context, params.realm, &dnames);
	if (retdn == 0 && dnames) {
		/*
		 * Multi-homed KDCs sometimes may need to set svc names
		 * for multiple net interfaces so we set them for
		 * all interfaces just in case.
		 */
		set_svc_domnames(KADM5_ADMIN_HOST_SERVICE,
				dnames, KADM, KADMVERS);
		set_svc_domnames(KADM5_CHANGEPW_HOST_SERVICE,
				dnames, KADM, KADMVERS);
	}

	/* if set_names succeeded, this will too */
	in_buf.value = names[1].name;
	in_buf.length = strlen(names[1].name) + 1;
	(void) gss_import_name(&OMret, &in_buf, (gss_OID) nt_krb5_name_oid,
	    &gss_changepw_name);
	if (oldnames) {
		in_buf.value = names[3].name;
		in_buf.length = strlen(names[3].name) + 1;
		(void) gss_import_name(&OMret, &in_buf,
					(gss_OID) nt_krb5_name_oid,
					&gss_oldchangepw_name);
	}
	if (ret = acl_init(context, 0, params.acl_file)) {
		krb5_klog_syslog(LOG_ERR, gettext("Cannot initialize acl file: %s"),
		    error_message(ret));
		fprintf(stderr, gettext("%s: Cannot initialize acl file: %s\n"),
		    whoami, error_message(ret));
		krb5_klog_close(context);
		exit(1);
	}
	if ((ret = kadm5_init("kadmind", NULL,
		    NULL, &params,
		    KADM5_STRUCT_VERSION,
		    KADM5_API_VERSION_2,
		    &global_server_handle)) != KADM5_OK) {
		krb5_klog_syslog(LOG_ERR,
		    gettext("%s while initializing, aborting"),
		    error_message(ret));
		fprintf(stderr,
		    gettext("%s: %s while initializing, aborting\n"),
		    whoami, error_message(ret));
		krb5_klog_close(context);
		exit(1);
	}

	handle = global_server_handle;
	ctx = handle->context;
	if (params.iprop_enabled == TRUE)
		ulog_set_role(ctx, IPROP_MASTER);
	else
		ulog_set_role(ctx, IPROP_NULL);

	log_ctx = ctx->kdblog_context;

	if (log_ctx && (log_ctx->iproprole == IPROP_MASTER)) {
		/*
		 * IProp is enabled, so let's map in the update log
		 * and setup the service.
		 */
		if (ret = ulog_map(ctx, &params, FKADMIND)) {
			fprintf(stderr,
				gettext("%s: %s while mapping update log "
				"(`%s.ulog')\n"), whoami, error_message(ret),
				params.dbname);
			krb5_klog_syslog(LOG_ERR,
				gettext("%s while mapping update log "
				"(`%s.ulog')"), error_message(ret),
				params.dbname);
			krb5_klog_close(ctx);
			exit(1);
		}


		if (nofork)
			fprintf(stderr,
				"%s: create IPROP svc (PROG=%d, VERS=%d)\n",
				whoami, KRB5_IPROP_PROG, KRB5_IPROP_VERS);

		if (!svc_create(krb5_iprop_prog_1,
				KRB5_IPROP_PROG, KRB5_IPROP_VERS,
				"circuit_v")) {
			fprintf(stderr,
    gettext("%s: Cannot create IProp RPC service (PROG=%d, VERS=%d)\n"),
				whoami,
				KRB5_IPROP_PROG, KRB5_IPROP_VERS);
			krb5_klog_syslog(LOG_ERR,
    gettext("Cannot create IProp RPC service (PROG=%d, VERS=%d), failing."),
					KRB5_IPROP_PROG, KRB5_IPROP_VERS);
			krb5_klog_close(ctx);
			exit(1);
		}

		if (ret = kiprop_get_adm_host_srv_name(ctx,
							params.realm,
							&kiprop_name)) {
			krb5_klog_syslog(LOG_ERR,
			gettext("%s while getting IProp svc name, failing"),
					error_message(ret));
			fprintf(stderr,
		gettext("%s: %s while getting IProp svc name, failing\n"),
				whoami, error_message(ret));
			krb5_klog_close(ctx);
			exit(1);
		}

		if (!rpc_gss_set_svc_name(kiprop_name, "kerberos_v5", 0,
					KRB5_IPROP_PROG, KRB5_IPROP_VERS)) {
			rpc_gss_error_t err;
			(void) rpc_gss_get_error(&err);

		/* Try to determine if the error was caused by a missing keytab or
		 * missing keytab entries (and log it).
		 */
			log_kt_error(kiprop_name, whoami);
			krb5_klog_syslog(LOG_ERR,
    gettext("Unable to set RPCSEC_GSS service name (`%s'), failing."),
					kiprop_name ? kiprop_name : "<null>");
			fprintf(stderr,
    gettext("%s: Unable to set RPCSEC_GSS service name (`%s'), failing.\n"),
				whoami,
				kiprop_name ? kiprop_name : "<null>");

			if (nofork) {
				fprintf(stderr,
			"%s: set svc name (rpcsec err=%d, sys err=%d)\n",
					whoami,
					err.rpc_gss_error,
					err.system_error);
			}

			exit(1);
		}
		free(kiprop_name);

		if (retdn == 0 && dnames) {
			set_svc_domnames(KADM5_KIPROP_HOST_SERVICE,
					dnames,
					KRB5_IPROP_PROG, KRB5_IPROP_VERS);
		}

	} else {
		if (!oldnames) {
		/* rpc_gss_set_svc_name failed for both kadmin/<fqdn> and
		 * changepw/<fqdn>.
		 */
			krb5_klog_syslog(LOG_ERR,
					gettext("Unable to set RPCSEC_GSS service names "
						"('%s, %s')"),
					names[0].name, names[1].name);
			fprintf(stderr,
					gettext("%s: Unable to set RPCSEC_GSS service names "
						"('%s, %s')\n"),
					whoami,
					names[0].name, names[1].name);
			krb5_klog_close(context);
			exit(1);
		}
	}

	if (dnames)
		freedomnames(dnames);

	setup_signal_handlers(log_ctx->iproprole);
	krb5_klog_syslog(LOG_INFO, gettext("starting"));
	if (nofork)
		fprintf(stderr, "%s: starting...\n", whoami);


	/*
	 * We now call our own customized async event processing
	 * function kadm_svc_run(), as opposed to svc_run() earlier,
	 * since this enables kadmind to also listen-to/process
	 * non-RPCSEC_GSS based change-pwd requests apart from the
	 * regular, RPCSEC_GSS kpasswd requests from Solaris Krb5 clients.
	 */
	kadm_svc_run();

	krb5_klog_syslog(LOG_INFO, gettext("finished, exiting"));
	kadm5_destroy(global_server_handle);
	t_close(fd);
	krb5_klog_close(context);
	exit(0);
}


/*
 * Function: kadm_svc_run
 * 
 * Purpose: modified version of sunrpc svc_run.
 *	    which closes the database every TIMEOUT seconds.
 *
 * Arguments:
 * Requires:
 * Effects:
 * Modifies:
 */
void
kadm_svc_run(void)
{
	struct pollfd	*rfd = 0;
	struct	timeval	    timeout;
	int pollret;
	int nfds = 0;
	int i;

	while(signal_request_exit == 0) {
		timeout.tv_sec = TIMEOUT;
		timeout.tv_usec = 0;

		if (nfds != svc_max_pollfd) {
			rfd = realloc(rfd, sizeof (pollfd_t) * svc_max_pollfd);
			nfds = svc_max_pollfd;
		}

		(void) memcpy(rfd, svc_pollfd,
			sizeof (pollfd_t) * svc_max_pollfd);

		for (i = 0; i < nfds; i++) {
			if (rfd[i].fd == -1) {
				rfd[i].fd = schpw;
				rfd[i].events = POLLIN;
				break;
			}
		}

		switch(pollret = poll(rfd, nfds,
				__rpc_timeval_to_msec(&timeout))) {
		case -1:
			if(errno == EINTR)
				continue;
			perror("poll");
			return;
		case 0:
			continue;
		default:
			for (i = 0; i < nfds; i++) {
				if (rfd[i].revents & POLLIN) {
					if (rfd[i].fd == schpw)
						handle_chpw(context, schpw,
							global_server_handle,
							&chgpw_params);
					else
						svc_getreq_poll(rfd, pollret);
					break;
				} else {
					if (i == (nfds - 1))
						perror("poll");
				}
			}
			break;
		}
	}
}


/*
 * Function: setup_signal_handlers
 *
 * Purpose: Setup signal handling functions with System V's signal().
 */
void setup_signal_handlers(iprop_role iproprole) {
	signal(SIGINT, sig_exit);
	signal(SIGTERM, sig_exit);
	signal(SIGQUIT, sig_exit);
	signal(SIGPIPE, sig_pipe);

	/*
	 * IProp will fork for a full-resync, we don't want to
	 * wait on it and we don't want the living dead procs either.
	 */
	if (iproprole == IPROP_MASTER)
		(void) signal(SIGCHLD, SIG_IGN);

	return;
}


/*
 * Function: sig_exit
 *
 * Purpose: sets flags saying the server got a signal and that it
 *          should exit when convenient.
 *
 * Effects:
 *      Modifies signal_request_exit which ideally makes the server exit
 *      at some point.
 *
 * Modifies:
 *      Signal_request_exit
 */
void sig_exit(int signum)
{
	krb5_klog_syslog(LOG_NOTICE, gettext("Got signal to request exit"));
	signal_request_exit = 1;
	return;
}


/*
 * Function: sig_pipe
 *
 * Purpose: SIGPIPE handler
 *
 * Effects: krb5_klog_syslog a message that a SIGPIPE occurred and returns,
 * thus causing the read() or write() to fail and, presumable, the RPC
 * to recover.	Otherwise, the process aborts.
 */
void
sig_pipe(int unused)
{
	krb5_klog_syslog(LOG_NOTICE, gettext("Warning: Received a SIGPIPE; "
		"probably a client aborted.  Continuing."));
}


/*
 * Given a service name (s_name) determine if the keytab file exists
 * and if the keytab entry is present. Log missing keytab
 * at LOG_ERR and log missing keytab entries at LOG_WARNING.
 * If any of krb5_* (or strdup) fail it will return the failure. 
 */
krb5_error_code log_kt_error(char *s_name, char *whoami) {
	krb5_keytab kt;
	krb5_principal princ;
	krb5_keytab_entry entry;
	krb5_error_code	code = 0;
	char kt_name[MAX_KEYTAB_NAME_LEN];
	char *service;
	char *host;

	service = strdup(s_name);
	if(!service)
		return ENOMEM;

	host = strchr(service, '@');
	*host++ = '\0';
	if (code = krb5_sname_to_principal(context, host,
				service, KRB5_NT_SRV_HST, &princ)) {
		krb5_klog_syslog(LOG_ERR,
				gettext("krb5_sname_to_principal failed: %s"),
				error_message(code));
		fprintf(stderr,
				gettext("%s: krb5_sname_to_principal failed: %s"),
				whoami, error_message(code));
		free(service);
		return code;
	} 

	if (code = krb5_kt_default_name(context, kt_name, sizeof (kt_name))) {
		krb5_klog_syslog(LOG_ERR,
				gettext("krb5_kt_default_name failed: %s"),
				error_message(code));
		fprintf(stderr,
				gettext("%s: krb5_kt_default_name failed: %s"),
				whoami, error_message(code));
		krb5_free_principal(context, princ);
		free(service);
		return code;
	}

	if (code = krb5_kt_default(context, &kt)) {
		krb5_klog_syslog(LOG_ERR,
				gettext("krb5_kt_default failed: %s"),
				error_message(code));
		fprintf(stderr,
				gettext("%s: krb5_kt_default failed: %s"),
				whoami, error_message(code));
		krb5_free_principal(context, princ);
		free(service);
		return code;
	} 

	code = krb5_kt_get_entry(context, kt, princ, 0, 0, &entry);

	switch (code) {
		case 0:
			krb5_kt_free_entry(context, &entry);
			break;
		case KRB5_KT_NOTFOUND:
			krb5_klog_syslog(LOG_WARNING,
					gettext("Keytab entry \"%s/%s\" is missing from \"%s\""),
					service, host,
					kt_name);
			fprintf(stderr,
					gettext("%s: Keytab entry \"%s/%s\" is missing from \"%s\".\n"),
					whoami,
					service, host,
					kt_name);
			break;
		case ENOENT:
			krb5_klog_syslog(LOG_ERR,
					gettext("Keytab file \"%s\" does not exist"),
					kt_name);
			fprintf(stderr,
					gettext("%s: Keytab file \"%s\" does not exist.\n"),
					whoami,
					kt_name);
			break;
	}
	krb5_kt_close(context,kt);
	krb5_free_principal(context, princ);
	free(service);
	return code;
}

