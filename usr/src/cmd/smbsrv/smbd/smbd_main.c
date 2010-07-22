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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioccom.h>
#include <sys/corectl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <wait.h>
#include <signal.h>
#include <atomic.h>
#include <libscf.h>
#include <limits.h>
#include <priv_utils.h>
#include <door.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <libscf.h>
#include <zone.h>
#include <libgen.h>
#include <pwd.h>
#include <grp.h>
#include <cups/cups.h>

#include <smbsrv/smb_door.h>
#include <smbsrv/smb_ioctl.h>
#include <smbsrv/string.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/libsmbns.h>
#include <smbsrv/libmlsvc.h>
#include "smbd.h"

#define	SMB_CUPS_DOCNAME "generic_doc"
#define	DRV_DEVICE_PATH	"/devices/pseudo/smbsrv@0:smbsrv"
#define	SMB_DBDIR "/var/smb"
#define	SMB_SPOOL_WAIT 2

static void *smbd_nbt_listener(void *);
static void *smbd_tcp_listener(void *);
static void *smbd_nbt_receiver(void *);
static void *smbd_tcp_receiver(void *);

static int smbd_daemonize_init(void);
static void smbd_daemonize_fini(int, int);
static int smb_init_daemon_priv(int, uid_t, gid_t);

static int smbd_kernel_bind(void);
static void smbd_kernel_unbind(void);
static int smbd_already_running(void);

static int smbd_service_init(void);
static void smbd_service_fini(void);

static int smbd_setup_options(int argc, char *argv[]);
static void smbd_usage(FILE *fp);
static void smbd_report(const char *fmt, ...);

static void smbd_sig_handler(int sig);

static int32_t smbd_gmtoff(void);
static int smbd_localtime_init(void);
static void *smbd_localtime_monitor(void *arg);

static pthread_t localtime_thr;

static int smbd_spool_init(void);
static void *smbd_spool_monitor(void *arg);
static pthread_t smbd_spool_thr;

static int smbd_refresh_init(void);
static void smbd_refresh_fini(void);
static void *smbd_refresh_monitor(void *);
static void smbd_refresh_dc(void);

static void *smbd_nbt_receiver(void *);
static void *smbd_nbt_listener(void *);

static void *smbd_tcp_receiver(void *);
static void *smbd_tcp_listener(void *);

static int smbd_start_listeners(void);
static void smbd_stop_listeners(void);
static int smbd_kernel_start(void);

static void smbd_fatal_error(const char *);

static pthread_t refresh_thr;
static pthread_cond_t refresh_cond;
static pthread_mutex_t refresh_mutex;

static cond_t listener_cv;
static mutex_t listener_mutex;

/*
 * Mutex to ensure that smbd_service_fini() and smbd_service_init()
 * are atomic w.r.t. one another.  Otherwise, if a shutdown begins
 * before initialization is complete, resources can get deallocated
 * while initialization threads are still using them.
 */
static mutex_t smbd_service_mutex;
static cond_t smbd_service_cv;

smbd_t smbd;

/*
 * Use SMF error codes only on return or exit.
 */
int
main(int argc, char *argv[])
{
	struct sigaction	act;
	sigset_t		set;
	uid_t			uid;
	int			pfd = -1;
	uint_t			sigval;
	struct rlimit		rl;
	int			orig_limit;

	smbd.s_pname = basename(argv[0]);
	openlog(smbd.s_pname, LOG_PID | LOG_NOWAIT, LOG_DAEMON);

	if (smbd_setup_options(argc, argv) != 0)
		return (SMF_EXIT_ERR_FATAL);

	if ((uid = getuid()) != smbd.s_uid) {
		smbd_report("user %d: %s", uid, strerror(EPERM));
		return (SMF_EXIT_ERR_FATAL);
	}

	if (getzoneid() != GLOBAL_ZONEID) {
		smbd_report("non-global zones are not supported");
		return (SMF_EXIT_ERR_FATAL);
	}

	if (is_system_labeled()) {
		smbd_report("Trusted Extensions not supported");
		return (SMF_EXIT_ERR_FATAL);
	}

	if (smbd_already_running())
		return (SMF_EXIT_OK);

	/*
	 * Raise the file descriptor limit to accommodate simultaneous user
	 * authentications/file access.
	 */
	if ((getrlimit(RLIMIT_NOFILE, &rl) == 0) &&
	    (rl.rlim_cur < rl.rlim_max)) {
		orig_limit = rl.rlim_cur;
		rl.rlim_cur = rl.rlim_max;
		if (setrlimit(RLIMIT_NOFILE, &rl) != 0)
			smbd_report("Failed to raise file descriptor limit"
			    " from %d to %d", orig_limit, rl.rlim_cur);
	}

	(void) sigfillset(&set);
	(void) sigdelset(&set, SIGABRT);

	(void) sigfillset(&act.sa_mask);
	act.sa_handler = smbd_sig_handler;
	act.sa_flags = 0;

	(void) sigaction(SIGABRT, &act, NULL);
	(void) sigaction(SIGTERM, &act, NULL);
	(void) sigaction(SIGHUP, &act, NULL);
	(void) sigaction(SIGINT, &act, NULL);
	(void) sigaction(SIGPIPE, &act, NULL);
	(void) sigaction(SIGUSR1, &act, NULL);

	(void) sigdelset(&set, SIGTERM);
	(void) sigdelset(&set, SIGHUP);
	(void) sigdelset(&set, SIGINT);
	(void) sigdelset(&set, SIGPIPE);
	(void) sigdelset(&set, SIGUSR1);

	if (smbd.s_fg) {
		(void) sigdelset(&set, SIGTSTP);
		(void) sigdelset(&set, SIGTTIN);
		(void) sigdelset(&set, SIGTTOU);

		if (smbd_service_init() != 0) {
			smbd_report("service initialization failed");
			exit(SMF_EXIT_ERR_FATAL);
		}
	} else {
		/*
		 * "pfd" is a pipe descriptor -- any fatal errors
		 * during subsequent initialization of the child
		 * process should be written to this pipe and the
		 * parent will report this error as the exit status.
		 */
		pfd = smbd_daemonize_init();

		if (smbd_service_init() != 0) {
			smbd_report("daemon initialization failed");
			exit(SMF_EXIT_ERR_FATAL);
		}

		smbd_daemonize_fini(pfd, SMF_EXIT_OK);
	}

	(void) atexit(smb_kmod_stop);

	while (!smbd.s_shutting_down) {
		if (smbd.s_sigval == 0 && smbd.s_refreshes == 0)
			(void) sigsuspend(&set);

		sigval = atomic_swap_uint(&smbd.s_sigval, 0);

		switch (sigval) {
		case 0:
		case SIGPIPE:
		case SIGABRT:
			break;

		case SIGHUP:
			syslog(LOG_DEBUG, "refresh requested");
			(void) pthread_cond_signal(&refresh_cond);
			break;

		case SIGUSR1:
			smb_log_dumpall();
			break;

		default:
			/*
			 * Typically SIGINT or SIGTERM.
			 */
			smbd.s_shutting_down = B_TRUE;
			break;
		}
	}

	smbd_service_fini();
	closelog();
	return ((smbd.s_fatal_error) ? SMF_EXIT_ERR_FATAL : SMF_EXIT_OK);
}

/*
 * This function will fork off a child process,
 * from which only the child will return.
 *
 * Use SMF error codes only on exit.
 */
static int
smbd_daemonize_init(void)
{
	int status, pfds[2];
	sigset_t set, oset;
	pid_t pid;
	int rc;

	/*
	 * Reset privileges to the minimum set required. We continue
	 * to run as root to create and access files in /var.
	 */
	rc = smb_init_daemon_priv(PU_RESETGROUPS, smbd.s_uid, smbd.s_gid);

	if (rc != 0) {
		smbd_report("insufficient privileges");
		exit(SMF_EXIT_ERR_FATAL);
	}

	/*
	 * Block all signals prior to the fork and leave them blocked in the
	 * parent so we don't get in a situation where the parent gets SIGINT
	 * and returns non-zero exit status and the child is actually running.
	 * In the child, restore the signal mask once we've done our setsid().
	 */
	(void) sigfillset(&set);
	(void) sigdelset(&set, SIGABRT);
	(void) sigprocmask(SIG_BLOCK, &set, &oset);

	if (pipe(pfds) == -1) {
		smbd_report("unable to create pipe");
		exit(SMF_EXIT_ERR_FATAL);
	}

	closelog();

	if ((pid = fork()) == -1) {
		openlog(smbd.s_pname, LOG_PID | LOG_NOWAIT, LOG_DAEMON);
		smbd_report("unable to fork");
		closelog();
		exit(SMF_EXIT_ERR_FATAL);
	}

	/*
	 * If we're the parent process, wait for either the child to send us
	 * the appropriate exit status over the pipe or for the read to fail
	 * (presumably with 0 for EOF if our child terminated abnormally).
	 * If the read fails, exit with either the child's exit status if it
	 * exited or with SMF_EXIT_ERR_FATAL if it died from a fatal signal.
	 */
	if (pid != 0) {
		(void) close(pfds[1]);

		if (read(pfds[0], &status, sizeof (status)) == sizeof (status))
			_exit(status);

		if (waitpid(pid, &status, 0) == pid && WIFEXITED(status))
			_exit(WEXITSTATUS(status));

		_exit(SMF_EXIT_ERR_FATAL);
	}

	openlog(smbd.s_pname, LOG_PID | LOG_NOWAIT, LOG_DAEMON);
	(void) setsid();
	(void) sigprocmask(SIG_SETMASK, &oset, NULL);
	(void) chdir("/");
	(void) umask(022);
	(void) close(pfds[0]);

	return (pfds[1]);
}

/*
 * This function is based on __init_daemon_priv() and replaces
 * __init_daemon_priv() since we want smbd to have all privileges so that it
 * can execute map/unmap commands with all privileges during share
 * connection/disconnection.  Unused privileges are disabled until command
 * execution.  The permitted and the limit set contains all privileges.  The
 * inheritable set contains no privileges.
 */

static const char root_cp[] = "/core.%f.%t";
static const char daemon_cp[] = "/var/tmp/core.%f.%t";

static int
smb_init_daemon_priv(int flags, uid_t uid, gid_t gid)
{
	priv_set_t *perm = NULL;
	int ret = -1;
	char buf[1024];

	/*
	 * This is not a significant failure: it allows us to start programs
	 * with sufficient privileges and with the proper uid.   We don't
	 * care enough about the extra groups in that case.
	 */
	if (flags & PU_RESETGROUPS)
		(void) setgroups(0, NULL);

	if (gid != (gid_t)-1 && setgid(gid) != 0)
		goto end;

	perm = priv_allocset();
	if (perm == NULL)
		goto end;

	/* E = P */
	(void) getppriv(PRIV_PERMITTED, perm);
	(void) setppriv(PRIV_SET, PRIV_EFFECTIVE, perm);

	/* Now reset suid and euid */
	if (uid != (uid_t)-1 && setreuid(uid, uid) != 0)
		goto end;

	/* I = 0 */
	priv_emptyset(perm);
	ret = setppriv(PRIV_SET, PRIV_INHERITABLE, perm);
end:
	priv_freeset(perm);

	if (core_get_process_path(buf, sizeof (buf), getpid()) == 0 &&
	    strcmp(buf, "core") == 0) {

		if ((uid == (uid_t)-1 ? geteuid() : uid) == 0) {
			(void) core_set_process_path(root_cp, sizeof (root_cp),
			    getpid());
		} else {
			(void) core_set_process_path(daemon_cp,
			    sizeof (daemon_cp), getpid());
		}
	}
	(void) setpflags(__PROC_PROTECT, 0);

	return (ret);
}

/*
 * Most privileges, except the ones that are required for smbd, are turn off
 * in the effective set.  They will be turn on when needed for command
 * execution during share connection/disconnection.
 */
static void
smbd_daemonize_fini(int fd, int exit_status)
{
	priv_set_t *pset;

	/*
	 * Now that we're running, if a pipe fd was specified, write an exit
	 * status to it to indicate that our parent process can safely detach.
	 * Then proceed to loading the remaining non-built-in modules.
	 */
	if (fd >= 0)
		(void) write(fd, &exit_status, sizeof (exit_status));

	(void) close(fd);

	pset = priv_allocset();
	if (pset == NULL)
		return;

	priv_basicset(pset);

	/* list of privileges for smbd */
	(void) priv_addset(pset, PRIV_NET_MAC_AWARE);
	(void) priv_addset(pset, PRIV_NET_PRIVADDR);
	(void) priv_addset(pset, PRIV_PROC_AUDIT);
	(void) priv_addset(pset, PRIV_SYS_DEVICES);
	(void) priv_addset(pset, PRIV_SYS_SMB);
	(void) priv_addset(pset, PRIV_SYS_MOUNT);

	priv_inverse(pset);

	/* turn off unneeded privileges */
	(void) setppriv(PRIV_OFF, PRIV_EFFECTIVE, pset);

	priv_freeset(pset);

	/* reenable core dumps */
	__fini_daemon_priv(NULL);
}

/*
 * smbd_service_init
 */
static int
smbd_service_init(void)
{
	static struct dir {
		char	*name;
		int	perm;
	} dir[] = {
		{ SMB_DBDIR,	0700 },
		{ SMB_CVOL,	0755 },
		{ SMB_SYSROOT,	0755 },
		{ SMB_SYSTEM32,	0755 },
		{ SMB_VSS,	0755 }
	};
	int	rc, i;

	(void) mutex_lock(&smbd_service_mutex);

	smbd.s_pid = getpid();
	for (i = 0; i < sizeof (dir)/sizeof (dir[0]); ++i) {
		if ((mkdir(dir[i].name, dir[i].perm) < 0) &&
		    (errno != EEXIST)) {
			smbd_report("mkdir %s: %s", dir[i].name,
			    strerror(errno));
			(void) mutex_unlock(&smbd_service_mutex);
			return (-1);
		}
	}

	if ((rc = smb_ccache_init(SMB_VARRUN_DIR, SMB_CCACHE_FILE)) != 0) {
		if (rc == -1)
			smbd_report("mkdir %s: %s", SMB_VARRUN_DIR,
			    strerror(errno));
		else
			smbd_report("unable to set KRB5CCNAME");
		(void) mutex_unlock(&smbd_service_mutex);
		return (-1);
	}

	smbd.s_loghd = smb_log_create(SMBD_LOGSIZE, SMBD_LOGNAME);
	smb_codepage_init();

	if (smbd_nicmon_start(SMBD_DEFAULT_INSTANCE_FMRI) != 0)
		smbd_report("NIC monitor failed to start");

	(void) dyndns_start();
	smb_ipc_init();

	if (smb_netbios_start() != 0)
		smbd_report("NetBIOS services failed to start");
	else
		smbd_report("NetBIOS services started");

	smbd.s_secmode = smb_config_get_secmode();
	if ((rc = smb_domain_init(smbd.s_secmode)) != 0) {
		if (rc == SMB_DOMAIN_NOMACHINE_SID) {
			smbd_report(
			    "no machine SID: check idmap configuration");
			(void) mutex_unlock(&smbd_service_mutex);
			return (-1);
		}
	}

	smb_ads_init();
	if (mlsvc_init() != 0) {
		smbd_report("msrpc initialization failed");
		(void) mutex_unlock(&smbd_service_mutex);
		return (-1);
	}

	if (smbd.s_secmode == SMB_SECMODE_DOMAIN)
		if (smbd_locate_dc_start() != 0)
			smbd_report("dc discovery failed %s", strerror(errno));

	smbd.s_door_srv = smbd_door_start();
	smbd.s_door_opipe = smbd_opipe_start();
	if (smbd.s_door_srv < 0 || smbd.s_door_opipe < 0) {
		smbd_report("door initialization failed %s", strerror(errno));
		(void) mutex_unlock(&smbd_service_mutex);
		return (-1);
	}

	if (smbd_refresh_init() != 0) {
		(void) mutex_unlock(&smbd_service_mutex);
		return (-1);
	}

	dyndns_update_zones();
	(void) smbd_localtime_init();
	(void) smb_lgrp_start();
	smb_pwd_init(B_TRUE);

	if (smb_shr_start() != 0) {
		smbd_report("share initialization failed: %s", strerror(errno));
		(void) mutex_unlock(&smbd_service_mutex);
		return (-1);
	}

	smbd.s_door_lmshr = smbd_share_start();
	if (smbd.s_door_lmshr < 0)
		smbd_report("share initialization failed");

	if (smbd_kernel_bind() != 0) {
		(void) mutex_unlock(&smbd_service_mutex);
		return (-1);
	}

	if (smb_shr_load() != 0) {
		smbd_report("failed to start loading shares: %s",
		    strerror(errno));
		(void) mutex_unlock(&smbd_service_mutex);
		return (-1);
	}

	if (smbd_spool_init() != 0) {
		smbd_report("failed to start print monitor: %s",
		    strerror(errno));
		(void) mutex_unlock(&smbd_service_mutex);
		return (-1);
	}

	smbd.s_initialized = B_TRUE;
	smbd_report("service initialized");
	(void) cond_signal(&smbd_service_cv);
	(void) mutex_unlock(&smbd_service_mutex);
	return (0);
}

/*
 * Shutdown smbd and smbsrv kernel services.
 *
 * Shutdown will not begin until initialization has completed.
 * Only one thread is allowed to perform the shutdown.  Other
 * threads will be blocked on fini_in_progress until the process
 * has exited.
 */
static void
smbd_service_fini(void)
{
	static uint_t	fini_in_progress;

	(void) mutex_lock(&smbd_service_mutex);

	while (!smbd.s_initialized)
		(void) cond_wait(&smbd_service_cv, &smbd_service_mutex);

	if (atomic_swap_uint(&fini_in_progress, 1) != 0) {
		while (fini_in_progress)
			(void) cond_wait(&smbd_service_cv, &smbd_service_mutex);
		/*NOTREACHED*/
	}

	smbd.s_shutting_down = B_TRUE;
	smbd_report("service shutting down");

	smb_kmod_stop();
	smb_logon_abort();
	smb_lgrp_stop();
	smbd_opipe_stop();
	smbd_door_stop();
	smbd_refresh_fini();
	smbd_kernel_unbind();
	smbd_share_stop();
	smb_shr_stop();
	dyndns_stop();
	smbd_nicmon_stop();
	smb_ccache_remove(SMB_CCACHE_PATH);
	smb_pwd_fini();
	smb_domain_fini();
	mlsvc_fini();
	smb_ads_fini();
	smb_netbios_stop();

	smbd.s_initialized = B_FALSE;
	smbd_report("service terminated");
	(void) mutex_unlock(&smbd_service_mutex);
	exit((smbd.s_fatal_error) ? SMF_EXIT_ERR_FATAL : SMF_EXIT_OK);
}

/*
 * smbd_refresh_init()
 *
 * SMB service refresh thread initialization.  This thread waits for a
 * refresh event and updates the daemon's view of the configuration
 * before going back to sleep.
 */
static int
smbd_refresh_init()
{
	pthread_attr_t		tattr;
	pthread_condattr_t	cattr;
	int			rc;

	(void) pthread_condattr_init(&cattr);
	(void) pthread_cond_init(&refresh_cond, &cattr);
	(void) pthread_condattr_destroy(&cattr);

	(void) pthread_mutex_init(&refresh_mutex, NULL);

	(void) pthread_attr_init(&tattr);
	(void) pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
	rc = pthread_create(&refresh_thr, &tattr, smbd_refresh_monitor, 0);
	(void) pthread_attr_destroy(&tattr);

	return (rc);
}

/*
 * smbd_refresh_fini()
 *
 * Stop the refresh thread.
 */
static void
smbd_refresh_fini()
{
	if (pthread_self() != refresh_thr) {
		(void) pthread_cancel(refresh_thr);
		(void) pthread_cond_destroy(&refresh_cond);
		(void) pthread_mutex_destroy(&refresh_mutex);
	}
}

/*
 * smbd_refresh_monitor()
 *
 * Wait for a refresh event. When this thread wakes up, update the
 * smbd configuration from the SMF config information then go back to
 * wait for the next refresh.
 */
/*ARGSUSED*/
static void *
smbd_refresh_monitor(void *arg)
{
	smb_kmod_cfg_t	cfg;
	int		error;

	while (!smbd.s_shutting_down) {
		(void) pthread_mutex_lock(&refresh_mutex);
		while ((atomic_swap_uint(&smbd.s_refreshes, 0) == 0) &&
		    (!smbd.s_shutting_down))
			(void) pthread_cond_wait(&refresh_cond, &refresh_mutex);
		(void) pthread_mutex_unlock(&refresh_mutex);

		if (smbd.s_shutting_down) {
			smbd_service_fini();
			/*NOTREACHED*/
		}

		(void) mutex_lock(&smbd_service_mutex);

		/*
		 * We've been woken up by a refresh event so go do
		 * what is necessary.
		 */
		smb_ads_refresh();
		smb_ccache_remove(SMB_CCACHE_PATH);

		/*
		 * Start the dyndns thread, if required.
		 * Clear the DNS zones for the existing interfaces
		 * before updating the NIC interface list.
		 */
		(void) dyndns_start();
		dyndns_clear_zones();

		if (smbd_nicmon_refresh() != 0)
			smbd_report("NIC monitor refresh failed");
		smb_netbios_name_reconfig();
		smb_browser_reconfig();
		smbd_refresh_dc();
		dyndns_update_zones();

		(void) mutex_unlock(&smbd_service_mutex);

		if (smbd_set_netlogon_cred()) {
			/*
			 * Restart required because the domain changed
			 * or the credential chain setup failed.
			 */
			if (smb_smf_restart_service() != 0) {
				syslog(LOG_ERR,
				    "unable to restart smb/server. "
				    "Run 'svcs -xv smb/server' for more "
				    "information.");
				smbd_service_fini();
				/*NOTREACHED*/
			}

			break;
		}

		if (!smbd.s_kbound) {
			if ((error = smbd_kernel_bind()) == 0)
				(void) smb_shr_load();

			continue;
		}

		(void) smb_shr_load();

		smb_load_kconfig(&cfg);
		error = smb_kmod_setcfg(&cfg);
		if (error < 0)
			smbd_report("configuration update failed: %s",
			    strerror(error));
	}

	return (NULL);
}

/*
 * Update DC information on a refresh.
 */
static void
smbd_refresh_dc(void)
{
	char fqdomain[MAXHOSTNAMELEN];
	if (smb_config_get_secmode() != SMB_SECMODE_DOMAIN)
		return;

	if (smb_getfqdomainname(fqdomain, MAXHOSTNAMELEN))
		return;

	if (!smb_locate_dc(fqdomain, "", NULL))
		smbd_report("DC refresh failed");
}

void
smbd_set_secmode(int secmode)
{
	switch (secmode) {
	case SMB_SECMODE_WORKGRP:
	case SMB_SECMODE_DOMAIN:
		(void) smb_config_set_secmode(secmode);
		smbd.s_secmode = secmode;
		break;

	default:
		syslog(LOG_ERR, "invalid security mode: %d", secmode);
		syslog(LOG_ERR, "entering maintenance mode");
		(void) smb_smf_maintenance_mode();
	}
}

/*
 * The service is online if initialization is complete and shutdown
 * has not begun.
 */
boolean_t
smbd_online(void)
{
	return (smbd.s_initialized && !smbd.s_shutting_down);
}

/*
 * If the door has already been opened by another process (non-zero pid
 * in target), we assume that another smbd is already running.  If there
 * is a race here, it will be caught later when smbsrv is opened because
 * only one process is allowed to open the device at a time.
 */
static int
smbd_already_running(void)
{
	door_info_t info;
	int door;

	if ((door = open(SMBD_DOOR_NAME, O_RDONLY)) < 0)
		return (0);

	if (door_info(door, &info) < 0)
		return (0);

	if (info.di_target > 0) {
		smbd_report("already running: pid %ld\n", info.di_target);
		(void) close(door);
		return (1);
	}

	(void) close(door);
	return (0);
}

/*
 * smbd_kernel_bind
 *
 * This function open the smbsrv device and start the kernel service.
 */
static int
smbd_kernel_bind(void)
{
	int rc;

	smbd_kernel_unbind();

	if ((rc = smb_kmod_bind()) == 0) {
		rc = smbd_kernel_start();
		if (rc != 0)
			smb_kmod_unbind();
		else
			smbd.s_kbound = B_TRUE;
	}

	if (rc != 0)
		smbd_report("kernel bind error: %s", strerror(errno));
	return (rc);
}

static int
smbd_kernel_start(void)
{
	smb_kmod_cfg_t	cfg;
	int		rc;

	smb_load_kconfig(&cfg);
	rc = smb_kmod_setcfg(&cfg);
	if (rc != 0)
		return (rc);

	rc = smb_kmod_setgmtoff(smbd_gmtoff());
	if (rc != 0)
		return (rc);

	rc = smb_kmod_start(smbd.s_door_opipe, smbd.s_door_lmshr,
	    smbd.s_door_srv);
	if (rc != 0)
		return (rc);

	rc = smbd_start_listeners();
	if (rc != 0)
		return (rc);

	return (0);
}

/*
 * smbd_kernel_unbind
 */
static void
smbd_kernel_unbind(void)
{
	smbd_stop_listeners();
	smb_kmod_unbind();
	smbd.s_kbound = B_FALSE;
}

/*
 * Initialization of the spool thread.
 * Returns 0 on success, an error number if thread creation fails.
 */

static int
smbd_spool_init(void)
{
	pthread_attr_t tattr;
	int rc;

	(void) pthread_attr_init(&tattr);
	(void) pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
	rc = pthread_create(&smbd_spool_thr, &tattr, smbd_spool_monitor, 0);
	(void) pthread_attr_destroy(&tattr);

	return (rc);
}

/*
 * This user thread blocks waiting for close print file
 * in the kernel. It then uses the data returned from the ioctl
 * to copy the spool file into the cups spooler.
 * This function is really only used by Vista and Win7 clients,
 * other versions of windows create only a zero size file and this
 * be removed by spoolss_copy_spool_file function.
 */

/*ARGSUSED*/
static void *
smbd_spool_monitor(void *arg)
{
	uint32_t spool_num;
	char username[MAXNAMELEN];
	char path[MAXPATHLEN];
	smb_inaddr_t ipaddr;
	int error_retry_cnt = 5;

	while (!smbd.s_shutting_down && (error_retry_cnt > 0)) {
		errno = 0;

		if (smb_kmod_get_spool_doc(&spool_num, username,
		    path, &ipaddr) == 0) {
			spoolss_copy_spool_file(&ipaddr,
			    username, path, SMB_CUPS_DOCNAME);
			error_retry_cnt = 5;
		} else {
			if (errno == ECANCELED)
				break;

			(void) sleep(SMB_SPOOL_WAIT);
			error_retry_cnt--;
		}
	}
	return (NULL);
}

/*
 * Initialization of the localtime thread.
 * Returns 0 on success, an error number if thread creation fails.
 */

int
smbd_localtime_init(void)
{
	pthread_attr_t tattr;
	int rc;

	(void) pthread_attr_init(&tattr);
	(void) pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
	rc = pthread_create(&localtime_thr, &tattr, smbd_localtime_monitor, 0);
	(void) pthread_attr_destroy(&tattr);
	return (rc);
}

/*
 * Local time thread to kernel land.
 * Send local gmtoff to kernel module one time at startup
 * and each time it changes (up to twice a year).
 * Local gmtoff is checked once every 15 minutes and
 * since some timezones are aligned on half and qtr hour boundaries,
 * once an hour would likely suffice.
 */

/*ARGSUSED*/
static void *
smbd_localtime_monitor(void *arg)
{
	struct tm local_tm;
	time_t secs;
	int32_t gmtoff, last_gmtoff = -1;
	int timeout;
	int error;

	for (;;) {
		gmtoff = smbd_gmtoff();

		if ((last_gmtoff != gmtoff) && smbd.s_kbound) {
			error = smb_kmod_setgmtoff(gmtoff);
			if (error != 0)
				smbd_report("localtime set failed: %s",
				    strerror(error));
		}

		/*
		 * Align the next iteration on a fifteen minute boundary.
		 */
		secs = time(0);
		(void) localtime_r(&secs, &local_tm);
		timeout = ((15 - (local_tm.tm_min % 15)) * SECSPERMIN);
		(void) sleep(timeout);

		last_gmtoff = gmtoff;
	}

	/*NOTREACHED*/
	return (NULL);
}

/*
 * smbd_gmtoff
 *
 * Determine offset from GMT. If daylight saving time use altzone,
 * otherwise use timezone.
 */
static int32_t
smbd_gmtoff(void)
{
	time_t clock_val;
	struct tm *atm;
	int32_t gmtoff;

	(void) time(&clock_val);
	atm = localtime(&clock_val);

	gmtoff = (atm->tm_isdst) ? altzone : timezone;

	return (gmtoff);
}

static void
smbd_sig_handler(int sigval)
{
	if (smbd.s_sigval == 0)
		(void) atomic_swap_uint(&smbd.s_sigval, sigval);

	if (sigval == SIGHUP) {
		atomic_inc_uint(&smbd.s_refreshes);
		(void) pthread_cond_signal(&refresh_cond);
	}

	if (sigval == SIGINT || sigval == SIGTERM) {
		smbd.s_shutting_down = B_TRUE;
		(void) pthread_cond_signal(&refresh_cond);
	}
}

/*
 * Set up configuration options and parse the command line.
 * This function will determine if we will run as a daemon
 * or in the foreground.
 *
 * Failure to find a uid or gid results in using the default (0).
 */
static int
smbd_setup_options(int argc, char *argv[])
{
	struct passwd *pwd;
	struct group *grp;
	int c;

	if ((pwd = getpwnam("root")) != NULL)
		smbd.s_uid = pwd->pw_uid;

	if ((grp = getgrnam("sys")) != NULL)
		smbd.s_gid = grp->gr_gid;

	smbd.s_fg = smb_config_get_fg_flag();

	while ((c = getopt(argc, argv, ":f")) != -1) {
		switch (c) {
		case 'f':
			smbd.s_fg = 1;
			break;

		case ':':
		case '?':
		default:
			smbd_usage(stderr);
			return (-1);
		}
	}

	return (0);
}

static void
smbd_usage(FILE *fp)
{
	static char *help[] = {
		"-f  run program in foreground"
	};

	int i;

	(void) fprintf(fp, "Usage: %s [-f]\n", smbd.s_pname);

	for (i = 0; i < sizeof (help)/sizeof (help[0]); ++i)
		(void) fprintf(fp, "    %s\n", help[i]);
}

static void
smbd_report(const char *fmt, ...)
{
	char buf[128];
	va_list ap;

	if (fmt == NULL)
		return;

	va_start(ap, fmt);
	(void) vsnprintf(buf, 128, fmt, ap);
	va_end(ap);

	(void) fprintf(stderr, "smbd: %s\n", buf);
}

static int
smbd_start_listeners(void)
{
	int		rc1;
	int		rc2;
	pthread_attr_t	tattr;

	(void) pthread_attr_init(&tattr);

	if (!smbd.s_nbt_listener_running) {
		rc1 = pthread_create(&smbd.s_nbt_listener_id, &tattr,
		    smbd_nbt_listener, NULL);
		if (rc1 != 0)
			smbd_report("unable to start NBT service");
		else
			smbd.s_nbt_listener_running = B_TRUE;
	}

	if (!smbd.s_tcp_listener_running) {
		rc2 = pthread_create(&smbd.s_tcp_listener_id, &tattr,
		    smbd_tcp_listener, NULL);
		if (rc2 != 0)
			smbd_report("unable to start TCP service");
		else
			smbd.s_tcp_listener_running = B_TRUE;
	}

	(void) pthread_attr_destroy(&tattr);

	if (rc1 != 0)
		return (rc1);
	return (rc2);
}

/*
 * Stop the listener threads.  In an attempt to ensure that the listener
 * threads get the signal, we use the timed wait loop to harass the
 * threads into terminating.  Then, if they are still running, we make
 * one final attempt to deliver the signal before calling thread join
 * to wait for them.  Note: if these threads don't terminate, smbd will
 * hang here and SMF will probably end up killing the contract.
 */
static void
smbd_stop_listeners(void)
{
	void		*status;
	timestruc_t	delay;
	int		rc = 0;

	(void) mutex_lock(&listener_mutex);

	while ((smbd.s_nbt_listener_running || smbd.s_tcp_listener_running) &&
	    (rc != ETIME)) {
		if (smbd.s_nbt_listener_running)
			(void) pthread_kill(smbd.s_nbt_listener_id, SIGTERM);

		if (smbd.s_tcp_listener_running)
			(void) pthread_kill(smbd.s_tcp_listener_id, SIGTERM);

		delay.tv_sec = 3;
		delay.tv_nsec = 0;
		rc = cond_reltimedwait(&listener_cv, &listener_mutex, &delay);
	}

	(void) mutex_unlock(&listener_mutex);

	if (smbd.s_nbt_listener_running) {
		syslog(LOG_WARNING, "NBT listener still running");
		(void) pthread_kill(smbd.s_nbt_listener_id, SIGTERM);
		(void) pthread_join(smbd.s_nbt_listener_id, &status);
		smbd.s_nbt_listener_running = B_FALSE;
	}

	if (smbd.s_tcp_listener_running) {
		syslog(LOG_WARNING, "TCP listener still running");
		(void) pthread_kill(smbd.s_tcp_listener_id, SIGTERM);
		(void) pthread_join(smbd.s_tcp_listener_id, &status);
		smbd.s_tcp_listener_running = B_FALSE;
	}
}

/*
 * Perform fatal error exit.
 */
static void
smbd_fatal_error(const char *msg)
{
	if (msg == NULL)
		msg = "Fatal error";

	smbd_report("%s", msg);
	smbd.s_fatal_error = B_TRUE;
	(void) kill(smbd.s_pid, SIGTERM);
}

/*ARGSUSED*/
static void *
smbd_nbt_receiver(void *arg)
{
	(void) smb_kmod_nbtreceive();
	return (NULL);
}

/*ARGSUSED*/
static void *
smbd_nbt_listener(void *arg)
{
	pthread_attr_t	tattr;
	sigset_t	set;
	sigset_t	oset;
	pthread_t	tid;
	int		error = 0;

	(void) sigfillset(&set);
	(void) sigdelset(&set, SIGTERM);
	(void) sigdelset(&set, SIGINT);
	(void) pthread_sigmask(SIG_SETMASK, &set, &oset);
	(void) pthread_attr_init(&tattr);
	(void) pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);

	while (smb_kmod_nbtlisten(error) == 0)
		error = pthread_create(&tid, &tattr, smbd_nbt_receiver, NULL);

	(void) pthread_attr_destroy(&tattr);

	if (!smbd.s_shutting_down)
		smbd_fatal_error("NBT listener thread terminated unexpectedly");

	(void) mutex_lock(&listener_mutex);
	smbd.s_nbt_listener_running = B_FALSE;
	(void) cond_broadcast(&listener_cv);
	(void) mutex_unlock(&listener_mutex);
	return (NULL);
}

/*ARGSUSED*/
static void *
smbd_tcp_receiver(void *arg)
{
	(void) smb_kmod_tcpreceive();
	return (NULL);
}

/*ARGSUSED*/
static void *
smbd_tcp_listener(void *arg)
{
	pthread_attr_t	tattr;
	sigset_t	set;
	sigset_t	oset;
	pthread_t	tid;
	int		error = 0;

	(void) sigfillset(&set);
	(void) sigdelset(&set, SIGTERM);
	(void) sigdelset(&set, SIGINT);
	(void) pthread_sigmask(SIG_SETMASK, &set, &oset);
	(void) pthread_attr_init(&tattr);
	(void) pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);

	while (smb_kmod_tcplisten(error) == 0)
		error = pthread_create(&tid, &tattr, smbd_tcp_receiver, NULL);

	(void) pthread_attr_destroy(&tattr);

	if (!smbd.s_shutting_down)
		smbd_fatal_error("TCP listener thread terminated unexpectedly");

	(void) mutex_lock(&listener_mutex);
	smbd.s_tcp_listener_running = B_FALSE;
	(void) cond_broadcast(&listener_cv);
	(void) mutex_unlock(&listener_mutex);
	return (NULL);
}

/*
 * Enable libumem debugging by default on DEBUG builds.
 */
#ifdef DEBUG
const char *
_umem_debug_init(void)
{
	return ("default,verbose"); /* $UMEM_DEBUG setting */
}

const char *
_umem_logging_init(void)
{
	return ("fail,contents"); /* $UMEM_LOGGING setting */
}
#endif
