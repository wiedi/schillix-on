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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

#ifndef	_UCBLIB_SYNONYMS_H
#define	_UCBLIB_SYNONYMS_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__STDC__)

/* external data */
#define	altzone		_altzone
#define	daylight	_daylight
#define	lone		_lone
#define	lten		_lten
#define	lzero		_lzero
#define	timezone	_timezone
#define	tzname		_tzname

/* functions */
#define	Msgdb		_Msgdb
#define	a64l		_a64l
#define	access		_access
#define	acct		_acct
#define	addseverity	_addseverity
#define	adjtime		_adjtime
#define	alarm		_alarm
#define	ascftime	_ascftime
#define	brk		_brk
#define	brkbase		_brkbase
#define	catclose	_catclose
#define	catgets		_catgets
#define	catopen		_catopen
#define	cfgetispeed	_cfgetispeed
#define	cfgetospeed	_cfgetospeed
#define	cfree		_cfree
#define	cfsetispeed	_cfsetispeed
#define	cfsetospeed	_cfsetospeed
#define	cftime		_cftime
#define	chdir		_chdir
#define	chmod		_chmod
#define	chown		_chown
#define	chroot		_chroot
#define	close		_close
#define	closedir	_closedir
#define	closelog	_closelog
#define	countbase	_countbase
#define	creat		_creat
#define	crypt		_crypt
#define	ctermid		_ctermid
#define	cuserid		_cuserid
#define	dial		_dial
#define	drand48		_drand48
#define	dup		_dup
#define	dup2		_dup2
#define	ecvt		_ecvt
#define	edata		_edata
#define	encrypt		_encrypt
#define	end		_end
#define	endgrent	_endgrent
#define	endpwent	_endpwent
#define	endspent	_endspent
#define	endutent	_endutent
#define	endutxent	_endutxent
#define	environ		_environ
#define	erand48		_erand48
#define	etext		_etext
#define	execl		_execl
#define	execle		_execle
#define	execlp		_execlp
#define	execv		_execv
#define	execve		_execve
#define	execvp		_execvp
#define	exportfs	_exportfs
#define	fattach		_fattach
#define	fchdir		_fchdir
#define	fchmod		_fchmod
#define	fchown		_fchown
#define	fcntl		_fcntl
#define	fcvt		_fcvt
#define	fdetach		_fdetach
#define	fdopen		_fdopen
#define	ffs		_ffs
#define	fgetgrent	_fgetgrent
#define	fgetpwent	_fgetpwent
#define	fgetspent	_fgetspent
#define	finite		_finite
#define	fmount		_fmount
#define	fmtmsg		_fmtmsg
#define	fork		_fork
#define	fpathconf	_fpathconf
#define	fpclass		_fpclass
#define	fpgetmask	_fpgetmask
#define	fpgetround	_fpgetround
#define	fpgetsticky	_fpgetsticky
#define	fpsetmask	_fpsetmask
#define	fpsetround	_fpsetround
#define	fpsetsticky	_fpsetsticky
#define	fptrap		_fptrap
#define	fstat		_fstat
#define	fstatvfs	_fstatvfs
#define	fsync		_fsync
#define	ftok		_ftok
#define	ftruncate	_ftruncate
#define	ftw		_ftw
#define	funmount	_funmount
#define	gcvt		_gcvt
#define	getcontext	_getcontext
#define	getcwd		_getcwd
#define	getdate		_getdate
#define	getdate_err	_getdate_err
#define	getdents	_getdents
#define	getegid		_getegid
#define	geteuid		_geteuid
#define	getgid		_getgid
#define	getgrent	_getgrent
#define	getgrgid	_getgrgid
#define	getgrnam	_getgrnam
#define	getgroups	_getgroups
#define	gethz		_gethz
#define	getitimer	_getitimer
#define	getlogin	_getlogin
#define	getmntany	_getmntany
#define	getmntent	_getmntent
#define	getmsg		_getmsg
#define	getopt		_getopt
#define	getpass		_getpass
#define	getpgid		_getpgid
#define	getpgrp		_getpgrp
#define	getpid		_getpid
#define	getpmsg		_getpmsg
#define	getppid		_getppid
#define	getpw		_getpw
#define	getpwent	_getpwent
#define	getpwnam	_getpwnam
#define	getpwuid	_getpwuid
#define	getrlimit	_getrlimit
#define	getsid		_getsid
#define	getspent	_getspent
#define	getspnam	_getspnam
#define	getsubopt	_getsubopt
#define	gettimeofday	_gettimeofday
#define	gettxt		_gettxt
#define	getuid		_getuid
#define	getutent	_getutent
#define	getutid		_getutid
#define	getutline	_getutline
#define	getutmp		_getutmp
#define	getutmpx	_getutmpx
#define	getutxent	_getutxent
#define	getutxid	_getutxid
#define	getutxline	_getutxline
#define	getvfsany	_getvfsany
#define	getvfsent	_getvfsent
#define	getvfsfile	_getvfsfile
#define	getvfsspec	_getvfsspec
#define	getw		_getw
#define	grantpt		_grantpt
#define	gsignal		_gsignal
#define	gtty		_gtty
#define	hcreate		_hcreate
#define	hdestroy	_hdestroy
#define	hsearch		_hsearch
#define	id2str		_id2str
#define	initgroups	_initgroups
#define	insque		_insque
#define	ioctl		_ioctl
#define	isastream	_isastream
#define	isatty		_isatty
#define	jrand48		_jrand48
#define	kill		_kill
#define	l3tol		_l3tol
#define	l64a		_l64a
#define	ladd		_ladd
#define	lchown		_lchown
#define	lckpwdf		_lckpwdf
#define	lcong48		_lcong48
#define	ldivide		_ldivide
#define	lexp10		_lexp10
#define	lfind		_lfind
#define	link		_link
#define	llog10		_llog10
#define	lmul		_lmul
#define	lockf		_lockf
#define	lrand48		_lrand48
#define	lsearch		_lsearch
#define	lseek		_lseek
#define	lshiftl		_lshiftl
#define	lsign		_lsign
#define	lstat		_lstat
#define	lsub		_lsub
#define	ltol3		_ltol3
#define	makecontext	_makecontext
#define	makeut		_makeut
#define	makeutx		_makeutx
#define	memalign	_memalign
#define	memccpy		_memccpy
#define	memcntl		_memcntl
#define	mincore		_mincore
#define	mkdir		_mkdir
#define	mkfifo		_mkfifo
#define	mktemp		_mktemp
#define	mlock		_mlock
#define	mlockall	_mlockall
#define	mmap		_mmap
#define	modut		_modut
#define	modutx		_modutx
#define	monitor		_monitor
#define	mount		_mount
#define	mprotect	_mprotect
#define	mrand48		_mrand48
#define	msgctl		_msgctl
#define	msgget		_msgget
#define	msgrcv		_msgrcv
#define	msgsnd		_msgsnd
#define	msync		_msync
#define	munlock		_munlock
#define	munlockall	_munlockall
#define	munmap		_munmap
#define	nfs_getfh	_nfs_getfh
#define	nfssvc		_nfssvc
#define	nftw		_nftw
#define	nice		_nice
#define	nl_langinfo	_nl_langinfo
#define	nrand48		_nrand48
#define	nuname		_nuname
#define	open		_open
#define	opendir		_opendir
#define	openlog		_openlog
#define	pathconf	_pathconf
#define	pause		_pause
#define	pclose		_pclose
#define	pipe		_pipe
#define	plock		_plock
#define	poll		_poll
#define	popen		_popen
#define	profil		_profil
#define	psiginfo	_psiginfo
#define	psignal		_psignal
#define	ptrace		_ptrace
#define	ptsname		_ptsname
#define	putenv		_putenv
#define	putmsg		_putmsg
#define	putpmsg		_putpmsg
#define	putpwent	_putpwent
#define	putspent	_putspent
#define	pututline	_pututline
#define	pututxline	_pututxline
#define	putw		_putw
#define	read		_read
#define	readdir		_readdir
#define	readlink	_readlink
#define	readv		_readv
#define	realpath	_realpath
#define	remque		_remque
#define	rmdir		_rmdir
#define	sbrk		_sbrk
#define	seed48		_seed48
#define	seekdir		_seekdir
#define	select		_select
#define	semctl		_semctl
#define	semget		_semget
#define	semop		_semop
#define	setchrclass	_setchrclass
#define	setcontext	_setcontext
#define	setegid		_setegid
#define	seteuid		_seteuid
#define	setgid		_setgid
#define	setgrent	_setgrent
#define	setgroups	_setgroups
#define	setitimer	_setitimer
#define	setkey		_setkey
#define	setlogmask	_setlogmask
#define	setpgid		_setpgid
#define	setpgrp		_setpgrp
#define	setpwent	_setpwent
#define	setrlimit	_setrlimit
#define	setsid		_setsid
#define	setspent	_setspent
#define	settimeofday	_settimeofday
#define	setuid		_setuid
#define	setutent	_setutent
#define	setutxent	_setutxent
#define	shmat		_shmat
#define	shmctl		_shmctl
#define	shmdt		_shmdt
#define	shmget		_shmget
#define	sig2str		_sig2str
#define	sigaction	_sigaction
#define	sigaddset	_sigaddset
#define	sigaltstack	_sigaltstack
#define	sigdelset	_sigdelset
#define	sigemptyset	_sigemptyset
#define	sigfillset	_sigfillset
#define	sigflag		_sigflag
#define	sighold		_sighold
#define	sigignore	_sigignore
#define	sigismember	_sigismember
#define	siglongjmp	_siglongjmp
#define	sigpause	_sigpause
#define	sigpending	_sigpending
#define	sigprocmask	_sigprocmask
#define	sigrelse	_sigrelse
#define	sigsend		_sigsend
#define	sigsendset	_sigsendset
#define	sigset		_sigset
#define	sigsetjmp	_sigsetjmp
#define	sigsuspend	_sigsuspend
#define	sleep		_sleep
#define	srand48		_srand48
#define	ssignal		_ssignal
#define	stat		_stat
#define	statfs		_statfs
#define	statvfs		_statvfs
#define	stime		_stime
#define	str2id		_str2id
#define	str2sig		_str2sig
#define	strdup		_strdup
#define	stty		_stty
#define	swab		_swab
#define	swapcontext	_swapcontext
#define	swapctl		_swapctl
#define	symlink		_symlink
#define	sync		_sync
#define	synchutmp	_synchutmp
#define	sys3b		_sys3b
#define	sysi86		_sysi86
#define	sysconf		_sysconf
#define	sysfs		_sysfs
#define	sysinfo		_sysinfo
#define	syslog		_syslog
#define	tcdrain		_tcdrain
#define	tcflow		_tcflow
#define	tcflush		_tcflush
#define	tcgetattr	_tcgetattr
#define	tcgetpgrp	_tcgetpgrp
#define	tcgetsid	_tcgetsid
#define	tcsendbreak	_tcsendbreak
#define	tcsetattr	_tcsetattr
#define	tcsetpgrp	_tcsetpgrp
#define	tdelete		_tdelete
#define	tell		_tell
#define	telldir		_telldir
#define	tempnam		_tempnam
#define	tfind		_tfind
#define	truncate	_truncate
#define	tsearch		_tsearch
#define	ttyname		_ttyname
#define	ttyslot		_ttyslot
#define	twalk		_twalk
#define	tzname		_tzname
#define	tzset		_tzset
#define	uadmin		_uadmin
#define	ulckpwdf	_ulckpwdf
#define	ulimit		_ulimit
#define	umask		_umask
#define	umount		_umount
#define	uname		_uname
#define	undial		_undial
#define	unlink		_unlink
#define	unlockpt	_unlockpt
#define	unordered	_unordered
#define	updutfile	_updutfile
#define	updutxfile	_updutxfile
#define	updutmp		_updutmp
#define	updutmpx	_updutmpx
#define	updwtmp		_updwtmp
#define	updwtmpx	_updwtmpx
#define	utime		_utime
#define	utmpname	_utmpname
#define	utmpxname	_utmpxname
#define	utssys		_utssys
#define	valloc		_valloc
#define	vfork		_vfork
#define	vsyslog		_vsyslog
#define	wait		_wait
#define	waitid		_waitid
#define	waitpid		_waitpid
#define	write		_write
#define	writev		_writev
#define	_assert		__assert
#define	_ctype		__ctype
#define	_filbuf		__filbuf
#define	_flsbuf		__flsbuf
#define	_iob		__iob

#else  /* not __STDC__ */

#define	const

#ifndef _SIZE_T
#define	_SIZE_T
typedef unsigned int	size_t;
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif	/* _UCBLIB_SYNONYMS_H */
