/* @(#)sccslog.c	1.44 18/12/17 Copyright 1997-2018 J. Schilling */
#include <schily/mconfig.h>
#ifndef lint
static	UConst char sccsid[] =
	"@(#)sccslog.c	1.44 18/12/17 Copyright 1997-2018 J. Schilling";
#endif
/*
 *	Copyright (c) 1997-2018 J. Schilling
 */
/*
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * See the file CDDL.Schily.txt in this distribution for details.
 * A copy of the CDDL is also available via the Internet at
 * http://www.opensource.org/licenses/cddl1.txt
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file CDDL.Schily.txt from this distribution.
 */

#include <defines.h>
#include <schily/stdio.h>
#include <schily/stdlib.h>
#include <schily/unistd.h>
#include <schily/standard.h>
#include <schily/string.h>
#include <schily/time.h>
#include <schily/utypes.h>
#include <schily/stat.h>
#include <schily/dirent.h>
#include <schily/maxpath.h>
#include <schily/schily.h>
#include <version.h>
#include <i18n.h>

#define	streql(s1, s2)	(strcmp((s1), (s2)) == 0)
#undef	fgetline			/* May be #defined by schily.h */
#define	fgetline	log_fgetline

struct xx {
	time_t	time;
	Llong	ltime;
	int	gmtoff;
	struct tm tm;
	char	*user;
	char	*file;
	char	*vers;
	char	*comment;
	int	flags;
};

#define	PRINTED	0x01

LOCAL	struct	xx	*list;
LOCAL	int		listmax;
LOCAL	int		listsize;

LOCAL	char		*Cwd;
LOCAL	char		*SccsPath = "";
LOCAL	BOOL		extended = FALSE;
LOCAL	Nparms		N;			/* Keep -N parameters		*/

LOCAL	int	xxcmp		__PR((const void *vp1, const void *vp2));
LOCAL	char *	mapuser		__PR((char *name));
LOCAL	void	usage		__PR((int exitcode));
EXPORT	int	main		__PR((int ac, char *av[]));
LOCAL	void	dodir		__PR((char *name));
LOCAL	void	dofile		__PR((char *name));
LOCAL	int	fgetline	__PR((FILE *, char *, int));
LOCAL	int	getN		__PR((const char *, void *));

/*
 * XXX With SCCS v6 local time + GMT off, we should not compare struct tm
 * XXX but time_t or better Llong ltime.
 */
LOCAL int
xxcmp(vp1, vp2)
	const void	*vp1;
	const void	*vp2;
{
	const struct xx *p1 = vp1;
	const struct xx *p2 = vp2;
	const struct tm	*tm1;
	const struct tm	*tm2;

	tm1 = &(p1)->tm;
	tm2 = &(p2)->tm;

	if (tm1->tm_year < tm2->tm_year)
		return (1);
	else if (tm1->tm_year > tm2->tm_year)
		return (-1);
	else if (tm1->tm_mon < tm2->tm_mon)
		return (1);
	else if (tm1->tm_mon > tm2->tm_mon)
		return (-1);
	else if (tm1->tm_mday < tm2->tm_mday)
		return (1);
	else if (tm1->tm_mday > tm2->tm_mday)
		return (-1);
	else if (tm1->tm_hour < tm2->tm_hour)
		return (1);
	else if (tm1->tm_hour > tm2->tm_hour)
		return (-1);
	else if (tm1->tm_min < tm2->tm_min)
		return (1);
	else if (tm1->tm_min > tm2->tm_min)
		return (-1);
	else if (tm1->tm_sec < tm2->tm_sec)
		return (1);
	else if (tm1->tm_sec > tm2->tm_sec)
		return (-1);
	return (0);

#ifdef	OLD
	if ((p1)->time < (p2)->time)
		return (1);
	if ((p1)->time > (p2)->time)
		return (-1);
	return (0);
#endif
}

LOCAL char *
mapuser(name)
	char	*name;
{
static	char	nbuf[1024];
static	FILE	*f = NULL;
static	int	cannot = 0;
static	char	*lastname = NULL;
static	char	*lastuser = NULL;
	int	len;

	if (cannot)
		return (name);

	if (f == NULL) {
		char	*home = getenv("HOME");

		if (home == NULL)
			home = ".";
		js_snprintf(nbuf, sizeof (nbuf), "%s/.sccs/usermap", home);
		f = fopen(nbuf, "r");
		if (f == NULL) {
			cannot = 1;
			return (name);
		}
		lastname = lastuser = NULL;
	}
	if (lastname && streql(lastname, name))
		return (lastuser);
	rewind(f);
	while ((len = fgetline(f, nbuf, sizeof (nbuf))) >= 0) {
		char	*p;

		if (len == 0)
			continue;
		p = strchr(nbuf, '\t');
		if (p == NULL)
			p = strchr(nbuf, ' ');
		if (p == NULL || p == nbuf)
			continue;
		*p++ = '\0';
		if (!streql(nbuf, name))
			continue;
		while (*p == ' ' || *p =='\t')
			p++;
		lastname = name;
		lastuser = p;
		return (p);
	}
	lastname = lastuser = NULL;
	return (name);
}

LOCAL void
usage(exitcode)
	int	exitcode;
{
	fprintf(stderr, _("Usage: sccslog [options] s.file1 .. s.filen\n"));
	fprintf(stderr, _("	-help	Print this help.\n"));
	fprintf(stderr, _("	-version Print version number.\n"));
	fprintf(stderr, _("	-a	Print all deltas with times differing > 60s separately.\n"));
	fprintf(stderr, _("	-aa	Print all deltas with different times separately.\n"));
	fprintf(stderr, _("	-Cdir	Base dir for printed filenames.\n"));
	fprintf(stderr, _("	-p subdir	Define SCCS subdir.\n"));
	fprintf(stderr, _("	-x	Include all comment, even SCCSv6 metadata.\n"));
	fprintf(stderr, _("	-Nbulk-spec Processes a bulk of SCCS history files.\n"));
	exit(exitcode);
}

EXPORT int
main(ac, av)
	int	ac;
	char	*av[];
{
	int	cac;
	char	* const *cav;
	char	*opts = "help,V,version,a+,x,C*,p*,N&_";
	BOOL	help = FALSE;
	BOOL	pversion = FALSE;
	int	nopooling = 0;
	int	i;
	int	j;

	save_args(ac, av);

	/*
	 * Set locale for all categories.
	 */
	setlocale(LC_ALL, "");

	sccs_setinsbase(INS_BASE);

	/*
	 * Set directory to search for general l10n SCCS messages.
	 */
#ifdef	PROTOTYPES
	(void) bindtextdomain(NOGETTEXT("SUNW_SPRO_SCCS"),
	    NOGETTEXT(INS_BASE "/" SCCS_BIN_PRE "lib/locale/"));
#else
	(void) bindtextdomain(NOGETTEXT("SUNW_SPRO_SCCS"),
	    NOGETTEXT("/usr/ccs/lib/locale/"));
#endif

	(void) textdomain(NOGETTEXT("SUNW_SPRO_SCCS"));

	Fflags = FTLEXIT | FTLMSG | FTLCLN;
#ifdef	SCCS_FATALHELP
	Fflags |= FTLFUNC;
	Ffunc = sccsfatalhelp;
#endif
	cac = --ac;
	cav = ++av;

	if (getallargs(&cac, &cav, opts,
			&help, &pversion, &pversion,
			&nopooling, &extended,
			&Cwd, &SccsPath,
			getN, &N) < 0) {
		errmsgno(EX_BAD, "Bad flag: %s.\n", cav[0]);
		usage(EX_BAD);
	}
	if (help)
		usage(0);
	if (pversion) {
		printf(_(
		"sccslog %s-SCCS version %s %s (%s-%s-%s) Copyright (C) 1997-2018 J?rg Schilling\n"),
			PROVIDER,
			VERSION,
			VDATE,
			HOST_CPU, HOST_VENDOR, HOST_OS);
		exit(0);
	}
	if (N.n_parm) {					/* Parse -N args  */
		parseN(&N);
	}

	xsethome(NULL);
	if (N.n_parm && N.n_sdot && (sethomestat & SETHOME_OFFTREE))
		fatal(gettext("-Ns. not supported in off-tree project mode"));
	Fflags &= ~FTLEXIT;
	Fflags |= FTLJMP;

	cac = ac;
	cav = av;

	i = 0;
	while (getfiles(&cac, &cav, opts) > 0) {
		struct stat	sb;

		if (stat(cav[0], &sb) >= 0 && S_ISDIR(sb.st_mode))
			dodir(cav[0]);
		else
			dofile(cav[0]);
		i++;
		cac--;
		cav++;
	}
	/*
	 * Make sure that "sccs -R log" results in useful output.
	 */
	if (i == 0 && *SccsPath)
		dodir(SccsPath);

	qsort(list, listsize, sizeof (struct xx), xxcmp);

#ifdef	SCCSLOG_DEBUG
	printf("%d Eintr?ge\n", listsize);
#endif
	for (i = 0; i < listsize; i++) {
		if (list[i].flags & PRINTED)
			continue;

		/*
		 * XXX Should we implement a variant with local time +GMT off?
		 */
		printf("%.20s%d %s\n",
			ctime(&list[i].time), list[i].tm.tm_year + 1900,
			mapuser(list[i].user));
		printf("	* %s %s\n",
			list[i].file,
			list[i].vers);
		for (j = i+1; j < listsize; j++) {
			if (nopooling &&
			    list[i].time - list[j].time > 60)
				break;
			if (nopooling > 1 &&
			    list[i].time != list[j].time)
				break;
			if (list[i].time - list[j].time > 24*60*60)
				break;
			if (list[i].comment == NULL || list[j].comment == NULL)
				continue;
			if (streql(list[i].comment, list[j].comment)) {
				printf("	* %s %s\n",
					list[j].file,
					list[j].vers);
				list[j].flags |= PRINTED;
			}
		}

		printf("	  %s\n\n",
			list[i].comment);
	}
	return (0);
}

LOCAL void
dodir(name)
	char	*name;
{
	DIR		*dp = opendir(name);
	struct dirent	*d;
	char		*np;
	char		fname[MAXPATHNAME+1];
	char		*base;
	int		len;

	if (dp == NULL) {
		errmsg("Cannot open directory '%s'\n", name);
		return;
	}
	strlcpy(fname, name, sizeof (fname));
	base = &fname[strlen(fname)-1];
	if (*base != '/')
		*++base = '/';
	base++;
	len = sizeof (fname) - strlen(fname);
	while ((d = readdir(dp)) != NULL) {
		char * oparm = N.n_parm;

		np = d->d_name;

		if (np[0] != 's' || np[1] != '.' || np[2] == '\0')
			continue;

		strlcpy(base, np, len);
		N.n_parm = NULL;
		dofile(fname);
		N.n_parm = oparm;
	}
	closedir(dp);
}

LOCAL void
dofile(name)
	char	*name;
{
	FILE	*f;
	char	buf[8192];
	int	len;
	BOOL	firstline = TRUE;
	struct tm tm;
	char	*bname;
	char	*pname;

	if (setjmp(Fjmp))
		return;
	if (N.n_parm) {
		char	*ofile = name;

		name = bulkprepare(&N, name);
		if (name == NULL) {
			if (N.n_ifile)
				ofile = N.n_ifile;
			fatal(gettext("directory specified as s-file (cm14)"));
		}
	}

	f = fopen(name, "rb");
	if (f == NULL) {
		errmsg("Cannot open '%s'.\n", name);
		return;
	}
	if (list == NULL) {
		listmax += 128;
		list = malloc(listmax*sizeof (*list));
		if (list == NULL)
			comerr("No memory.\n");
	}

	bname = pname = name;
	if ((pname = strrchr(pname, '/')) == 0)
		pname = name;
	else
		bname = ++pname;
	if (pname[0] == 's' && pname[1] == '.')
		pname += 2;
	if (*SccsPath && (pname != &name[2])) {
		char	*p = malloc(strlen(name) + 2);
		
		if (p) {
			char	*sp;

			sp = strstr(name, SccsPath);
			if (sp == NULL)
				len = bname - name;
			else
				len = sp - name;
			sprintf(p, "%.*s%s", len, name, pname);
		}
		pname = p;
	} else if (Cwd) {
		char	*p = malloc(strlen(Cwd) + strlen(pname) + 1);

		if (p)
			sprintf(p, "%s%s", Cwd, pname);
		pname = p;
	} else {
		pname = strdup(pname);
	}
	if (pname == NULL)
		comerr("No memory.\n");

	while ((len = fgetline(f, buf, sizeof (buf))) >= 0) {
		if (firstline) {
			firstline = FALSE;
			if (buf[0] != 1 || buf[1] != 'h') {
				fclose(f);
				return;
			}
		}
		if (len == 0)
			continue;
		if (buf[0] != 1)
			continue;

/*		if (buf[1] == 'd' || buf[1] == 'c')*/
/*			error("%s\n", &buf[1]);*/

		if (buf[1] == 'd') {
			char	vers[256];
			char	user[256];
			time_t	t;
			Llong	lt;
			int	nsecs;
			int	gmtoffs;
			char	*p = &buf[4];

			len = sscanf(p, "%s %d/%d/%d %d:%d:%d.%d%d %s",
				vers,
				&tm.tm_year, &tm.tm_mon, &tm.tm_mday,
				&tm.tm_hour, &tm.tm_min, &tm.tm_sec,
				&nsecs,
				&gmtoffs,
				user);
			if (len == 10) {
				int hours = gmtoffs / 100;
				int mins  = gmtoffs % 100;

				gmtoffs = hours * 3600 + mins * 60;
			} else {
				gmtoffs = 1;
			}
			if (len < 10)
				len = sscanf(p, "%s %d/%d/%d %d:%d:%d%d %s",
				vers,
				&tm.tm_year, &tm.tm_mon, &tm.tm_mday,
				&tm.tm_hour, &tm.tm_min, &tm.tm_sec,
				&gmtoffs,
				user);
			if (len == 9) {
				int hours = gmtoffs / 100;
				int mins  = gmtoffs % 100;

				gmtoffs = hours * 3600 + mins * 60;
			} else {
				/*
				 * XXX GMT offset aus localtime bestimmen?
				 * XXX Nein, wir nehmen mktime() bei len >= 9.
				 */
				gmtoffs = 1;
			}
			if (len < 9)
				len = sscanf(p, "%s %d/%d/%d %d:%d:%d %s",				vers,
				&tm.tm_year, &tm.tm_mon, &tm.tm_mday,
				&tm.tm_hour, &tm.tm_min, &tm.tm_sec,
				user);
			if (len < 8) {
				errmsgno(EX_BAD,
					"Cannot scan date '%s' from '%s'.\n",
				p, name);
			}

			if (tm.tm_year >= 100)
				tm.tm_year -= 1900;
			else if (tm.tm_year >= 0 && tm.tm_year < 69)
				tm.tm_year += 100;
			tm.tm_isdst = -1;		/* let mktime() do it */
			tm.tm_mon -= 1;
			seterrno(0);
			if (tm.tm_year >= 138 &&	/* 2038 */
			    sizeof (t) < sizeof (lt)) {

				tm.tm_year -= 56;	/* 2 * 4 * 7 */
				if (len >= 9) {
					lt = t = mkgmtime(&tm);
					lt -= gmtoffs;
					t -= gmtoffs;
				} else {
#undef	mktime						/* Don't use xmktime */
					lt = t = mktime(&tm);
				}
				tm.tm_year += 56;
				lt += 1767225600;	/* 56 years */
			} else {
				if (len >= 9) {
					lt = t = mkgmtime(&tm);
					lt -= gmtoffs;
					t -= gmtoffs;
				} else {
					lt = t = mktime(&tm);
				}
			}
			if (geterrno() != 0) {
				comerr("Cannot convert date '%s' from '%s'.\n",
				p, name);
			}

/*#define	XXX*/
#ifdef	XXX
			error("len: %d '%s' %d/%d/%d%n",
				len, vers,
				tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
				&len);
			error("%*s %2.2d:%2.2d:%2.2d %s %.20s%d %lld %d\n",
				23 - len, "",
				tm.tm_hour, tm.tm_min, tm.tm_sec,
				user, ctime(&t), tm.tm_year+1900,
				lt, geterrno());
#endif

			if (listsize >= listmax) {
				listmax += 128;
				list = realloc(list, listmax*sizeof (*list));
			}
			if (list == NULL)
				comerr("No memory.\n");
			list[listsize].time = t;
			list[listsize].ltime = lt;
			list[listsize].gmtoff = gmtoffs;
			list[listsize].tm   = tm;
			list[listsize].user = strdup(user);
			list[listsize].vers = strdup(vers);
			list[listsize].comment = NULL;
			list[listsize].flags = 0;
			list[listsize].file = pname;
		}
		if (buf[1] == 'c') {
			if (buf[2] == '_' && !extended)
				continue;
			if (list[listsize].comment == NULL) {
				list[listsize].comment = strdup(&buf[3]);
			} else {
				/*
				 * multi line comments
				 */
				int lastlen = strlen(list[listsize].comment);

				list[listsize].comment = realloc(list[listsize].comment,
							lastlen + (4-3) + len + 1);
				if (list[listsize].comment == NULL)
					comerr("No memory.\n");
							    /* 4 bytes */
				strcat(list[listsize].comment, "\n\t  ");
				strcat(list[listsize].comment, &buf[3]);
			}
		}
		if (buf[1] == 'e') {
			if (list[listsize].user == NULL) {
				errmsgno(EX_BAD, "Corrupt file '%s'.\n", name);
				continue;
			}
			/*
			 * Check for very old SCCS history files that may have
			 * no comment at all in special for Release 1.1.
			 */
			if (list[listsize].comment == NULL)
				list[listsize].comment = strdup("");
			listsize++;
		}
	}
	fclose(f);
}

LOCAL int
fgetline(f, buf, len)
	FILE	*f;
	char	*buf;
	int	len;
{
	if (fgets(buf, len, f) == NULL) {
		if (feof(f) || ferror(f))
			return (EOF);
	}
	len = strlen(buf);
	if (len > 0 && buf[len-1] == '\n')
		buf[--len] = '\0';
	return (len);
}

LOCAL int
getN(argp, valp)
	const char	*argp;
	void		*valp;
{
	initN(&N);
	N.n_parm = (char *)argp;
	return (TRUE);
}
