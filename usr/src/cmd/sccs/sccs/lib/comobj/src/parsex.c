/*
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * See the file CDDL.Schily.txt in this distribution for details.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file CDDL.Schily.txt from this distribution.
 */
/*
 * @(#)parsex.c	1.4 19/11/11 Copyright 2018 J. Schilling
 */
#if defined(sun)
#pragma ident "@(#)parsex.c	1.4 19/11/11 Copyright 2018 J. Schilling"
#endif

#if defined(sun)
#pragma ident	"@(#)parsex.c"
#pragma ident	"@(#)sccs:lib/comobj/parsex.c"
#endif
#include	<defines.h>
#include	<i18n.h>

int
parseX(X)
	Xparms	*X;
{
	char	*opts = X->x_parm;
	char	*ep;
	char	*np;
	int	optlen;
	long	optflags = X->x_opts;
	unsigned flags = X->x_flags;
	BOOL	not = FALSE;

	while (*opts) {
		if ((ep = strchr(opts, '=')) != NULL) {
			Intptr_t	pdiff = ep - opts;

			optlen = (int)pdiff;
			if (optlen != pdiff)	/* lint paranoia */
				return (FALSE);
			optlen++;
			if ((np = strchr(opts, ',')) == NULL) {
				np = &opts[strlen(opts)];
			} else {
				np++;
			}
		} else if ((ep = strchr(opts, ',')) != NULL) {
			Intptr_t	pdiff = ep - opts;

			optlen = (int)pdiff;
			if (optlen != pdiff)	/* lint paranoia */
				return (FALSE);
			np = &ep[1];
		} else {
			optlen = strlen(opts);
			np = &opts[optlen];
		}
		if (opts[0] == '!') {
			opts++;
			optlen--;
			not = TRUE;
		}
		if (strncmp(opts, "not", optlen) == 0 ||
				strncmp(opts, "!", optlen) == 0) {
			not = TRUE;
		} else if ((flags & XO_INIT_PATH) &&
		    strncmp(opts, "Gp=", optlen) == 0) {
			size_t	l = np - &opts[3];

			if (optlen != 3)
				goto bad;
			if (opts[3]) {
				if (*np == '\0')
					l++;
				X->x_init_path = xmalloc(l);
				strlcpy(X->x_init_path, &opts[3], l);
			}
			optflags |= XO_INIT_PATH;

		} else if ((flags & XO_URAND) &&
		    strncmp(opts, "Gr=", optlen) == 0) {
			size_t	l = np - &opts[3];
			char	buf[33];

			if (*np == '\0')
				l++;
			if (optlen != 3 || l > sizeof (buf))
				goto bad;
			if (opts[3]) {
				strlcpy(buf, &opts[3], l);
				if (*urand_ab(buf, &X->x_rand))
					goto bad;
			}
			optflags |= XO_URAND;

		} else if ((flags & XO_MAIL) &&
		    strncmp(opts, "mail=", optlen) == 0) {
			size_t	l = np - &opts[5];

			if (optlen != 5)
				goto bad;
			if (opts[5]) {
				if (*np == '\0')
					l++;
				X->x_mail = xmalloc(l);
				strlcpy(X->x_mail, &opts[5], l);
			}
			optflags |= XO_MAIL;

		} else if ((flags & XO_PREPEND_FILE) &&
		    strncmp(opts, "prepend", optlen) == 0) {
			optflags |= XO_PREPEND_FILE;
		} else if ((flags & XO_UNLINK) &&
		    strncmp(opts, "unlink", optlen) == 0) {
			optflags |= XO_UNLINK;
		} else if (strncmp(opts, "help", optlen) == 0) {
			sccshelp(stdout, "Xopts");
			exit(0);
		} else {
		bad:
			Fflags &= ~FTLEXIT;
			fatal(gettext("illegal Xopt (co41)"));
			sccshelp(stdout, "Xopts");
			exit(1);
		}
		opts = np;
	}
	if (not)
		optflags = ~optflags;

	X->x_opts = optflags;

	return (TRUE);
}
