/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.opensource.org/licenses/cddl1.txt
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
 * Copyright 2020 J. Schilling
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <libgen.h>	/* basename() */

/*
 * TEXTDOMAIN should be defined in Makefile
 * in case it isn't, define it here
 */
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif

static char *
expand_metas(char *in)	/* walk thru string interpreting \n etc. */
{
	register char *out, *cp;

	for (cp = out = in; *in != NULL; out++, in++) {
		if (*in == '\\') {
			switch (*++in) {
			case 'b' :
				*out = '\b';
				break;
			case 'f' :
				*out = '\f';
				break;
			case 'n' :
				*out = '\n';
				break;
			case 'r' :
				*out = '\r';
				break;
			case 't' :
				*out = '\t';
				break;
			case 'v' :
				*out = '\v';
				break;
			default:
				*out = *in;
				break;
			}
		} else
			*out = *in;
	}
	*out = NULL;
	return (cp);
}

#define	ERR_USAGE \
	"Usage: gettext [-d domainname | --domain=domainname ]  " \
		"[domain] \"msgid\"\n" \
	"       gettext -s [-d domainname | --domain=domainname] [-e] [-n] "\
		"\"msgid\" ...\n"

#define	ERR_NUSAGE \
	"Usage: ngettext [-d domainname | --domain=domainname ]  " \
		"[domain] \"msgid\" \"msgid-plural\" count\n"

static void
usage(int is_ngettext) {
	if (is_ngettext)
		(void) fprintf(stderr, gettext(ERR_NUSAGE));
	else
		(void) fprintf(stderr, gettext(ERR_USAGE));
	exit(-1);
}

int
main(int argc, char *argv[])	/* shell script equivalent of gettext(3) */
{
	char	*domainpath, *msgid;
	char	*domain = NULL;
	char	c, *arg;
	int	exp_flag = 0;
	int	no_newline = 0;
	int	echo_flag = 0;
	int	is_ngettext = 0;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	arg = basename(argv[0]);
	if (*arg == 'n')
		is_ngettext = 1;
	argv++;
	while (--argc > 1) {
		arg = *argv;
		if (*arg == '-') {
			if (!*(arg + 1)) {
				/* not an option */
				break;
			}
loop:
			if ((c = *++arg) == '\0') {
				/* next argument */
				argv++;
				continue;
			} else if (c != '-') {
				switch (c) {
				case 'd':
					/* domainname */
					if (*(arg + 1)) {
						/*
						 * no spaces between -d and
						 * optarg
						 */
						domain = ++arg;
						argv++;
						continue;
					}
					if (--argc > 1) {
						domain = *++argv;
						argv++;
						continue;
					}
					/* not enough args */
					usage(is_ngettext);
					/* NOTREACHED */
					break;
				case 'e':
					/* enable escape sequence expansion */
					exp_flag = 1;
					goto loop;
					/* NOTREACHED */
				case 'n':
					/* suppress tailing newline */
					no_newline = 1;
					if (is_ngettext == 0)
						goto loop;
					/* FALLTHROUGH */
				case 's':
					echo_flag = 1;
					if (is_ngettext == 0)
						goto loop;
					/* FALLTHROUGH */
				default:
					/* illegal option */
					usage(is_ngettext);
					/* NOTREACHED */
					break;
				}
				/* NOTREACHED */
			}
			/* c == '-' */
			if (*(arg + 1) == '\0') {
				/* "--" found, option end */
				argv++;
				argc--;
				break;
			}

			/* long option */
			arg++;
			if (strncmp(arg, "domain=", 7) == 0) {
				/* domainname */
				arg += 7;
				if (*arg == '\0') {
					/* illegal option */
					usage(is_ngettext);
					/* NOTREACHED */
				}
				domain = arg;
				argv++;
				continue;
			}
			/* illegal option */
			usage(is_ngettext);
			/* NOTREACHED */
		}
		break;
	}
	if ((argc == 0) ||
	    (is_ngettext && argc < 3) ||
	    (!is_ngettext && argc > 2)) {
		usage(is_ngettext);
		/* NOTREACHED */
	}

	domainpath = getenv("TEXTDOMAINDIR");
	if (!echo_flag) {
		/* traditional mode */
		if ((!is_ngettext && argc == 2) ||
		   (is_ngettext && argc == 4)) {
			/*
			 * textdomain is specified by the argument.
			 */
			domain = *argv++;
		} else if (!domain) {
			/*
			 * textdomain is not specified by the argument.
			 * TEXTDOMAIN will be used.
			 */
			domain = getenv("TEXTDOMAIN");
			if (!domain) {
				/*
				 * no domain specified
				 * Just print the argument given.
				 */
				if (is_ngettext && atol(argv[2]) != 1) {
					(void) printf("%s",
							expand_metas(argv[1]));
					exit(1);
				}
				(void) printf("%s", expand_metas(*argv));
				exit(1);
			}
		}
		if (domainpath) {
			(void) bindtextdomain(domain, domainpath);
		}
		msgid = expand_metas(*argv);
		if (is_ngettext) {
			char		*msgid2 = expand_metas(argv[1]);
			unsigned long	n = atol(argv[2]);

			(void) fputs(dngettext(domain, msgid, msgid2, n),
					stdout);
		} else {
			(void) fputs(dgettext(domain, msgid), stdout);
		}
		exit(*domain == NULL);
	}
	/* echo mode */
	if (!domain) {
		domain = getenv("TEXTDOMAIN");
	}
	if (domainpath && domain) {
		(void) bindtextdomain(domain, domainpath);
	}
	while (argc-- > 0) {
		if (exp_flag)
			msgid = expand_metas(*argv++);
		else
			msgid = *argv++;
		(void) fputs(domain ? dgettext(domain, msgid) : msgid,
			stdout);

		if (argc > 0)
			(void) fputc(' ', stdout);
	}
	if (!no_newline)
		(void) fputc('\n', stdout);

	return ((domain == NULL) || (*domain == NULL));
}
