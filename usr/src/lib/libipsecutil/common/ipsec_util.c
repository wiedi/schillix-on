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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sysconf.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/pfkeyv2.h>
#include <net/pfpolicy.h>
#include <libintl.h>
#include <setjmp.h>
#include <libgen.h>

#include "ipsec_util.h"
#include "ikedoor.h"

/*
 * This file contains support functions that are shared by the ipsec
 * utilities including ipseckey(1m) and ikeadm(1m).
 */

/* Set standard default/initial values for globals... */
boolean_t pflag = B_FALSE;	/* paranoid w.r.t. printing keying material */
boolean_t nflag = B_FALSE;	/* avoid nameservice? */
boolean_t interactive = B_FALSE;	/* util not running on cmdline */
boolean_t readfile = B_FALSE;	/* cmds are being read from a file */
uint_t	lineno = 0;		/* track location if reading cmds from file */
jmp_buf	env;		/* for error recovery in interactive/readfile modes */

/*
 * Print errno and exit if cmdline or readfile, reset state if interactive
 */
void
bail(char *what)
{
	if (errno != 0)
		warn(what);
	else
		warnx(gettext("Error: %s"), what);
	if (readfile) {
		warnx(gettext("System error on line %u."), lineno);
	}
	if (interactive && !readfile)
		longjmp(env, 2);
	exit(1);
}

/*
 * Print caller-supplied variable-arg error msg, then exit if cmdline or
 * readfile, or reset state if interactive.
 */
/*PRINTFLIKE1*/
void
bail_msg(char *fmt, ...)
{
	va_list	ap;
	char	msgbuf[BUFSIZ];

	va_start(ap, fmt);
	(void) vsnprintf(msgbuf, BUFSIZ, fmt, ap);
	va_end(ap);
	if (readfile)
		warnx(gettext("ERROR on line %u:\n%s\n"), lineno,  msgbuf);
	else
		warnx(gettext("ERROR: %s\n"), msgbuf);

	if (interactive && !readfile)
		longjmp(env, 1);

	exit(1);
}


/*
 * dump_XXX functions produce ASCII output from various structures.
 *
 * Because certain errors need to do this to stderr, dump_XXX functions
 * take a FILE pointer.
 *
 * If an error occured while writing to the specified file, these
 * functions return -1, zero otherwise.
 */

int
dump_sockaddr(struct sockaddr *sa, uint8_t prefixlen, boolean_t addr_only,
    FILE *where)
{
	struct sockaddr_in	*sin;
	struct sockaddr_in6	*sin6;
	char			*printable_addr, *protocol;
	uint8_t			*addrptr;
	/* Add 4 chars to hold '/nnn' for prefixes. */
	char			storage[INET6_ADDRSTRLEN + 4];
	uint16_t		port;
	boolean_t		unspec;
	struct hostent		*hp;
	int			getipnode_errno, addrlen;

	switch (sa->sa_family) {
	case AF_INET:
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		sin = (struct sockaddr_in *)sa;
		addrptr = (uint8_t *)&sin->sin_addr;
		port = sin->sin_port;
		protocol = "AF_INET";
		unspec = (sin->sin_addr.s_addr == 0);
		addrlen = sizeof (sin->sin_addr);
		break;
	case AF_INET6:
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		sin6 = (struct sockaddr_in6 *)sa;
		addrptr = (uint8_t *)&sin6->sin6_addr;
		port = sin6->sin6_port;
		protocol = "AF_INET6";
		unspec = IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr);
		addrlen = sizeof (sin6->sin6_addr);
		break;
	default:
		return (0);
	}

	if (inet_ntop(sa->sa_family, addrptr, storage, INET6_ADDRSTRLEN) ==
	    NULL) {
		printable_addr = gettext("<inet_ntop() failed>");
	} else {
		char prefix[5];	/* "/nnn" with terminator. */

		(void) snprintf(prefix, sizeof (prefix), "/%d", prefixlen);
		printable_addr = storage;
		if (prefixlen != 0) {
			(void) strlcat(printable_addr, prefix,
			    sizeof (storage));
		}
	}
	if (addr_only) {
		if (fprintf(where, "%s", printable_addr) < 0)
			return (-1);
	} else {
		if (fprintf(where, gettext("%s: port %d, %s"), protocol,
		    ntohs(port), printable_addr) < 0)
			return (-1);
		if (!nflag) {
			/*
			 * Do AF_independent reverse hostname lookup here.
			 */
			if (unspec) {
				if (fprintf(where,
				    gettext(" <unspecified>")) < 0)
					return (-1);
			} else {
				hp = getipnodebyaddr((char *)addrptr, addrlen,
				    sa->sa_family, &getipnode_errno);
				if (hp != NULL) {
					if (fprintf(where,
					    " (%s)", hp->h_name) < 0)
						return (-1);
					freehostent(hp);
				} else {
					if (fprintf(where,
					    gettext(" <unknown>")) < 0)
						return (-1);
				}
			}
		}
		if (fputs(".\n", where) == EOF)
			return (-1);
	}
	return (0);
}

/*
 * Dump a key and bitlen
 */
int
dump_key(uint8_t *keyp, uint_t bitlen, FILE *where)
{
	int	numbytes;

	numbytes = SADB_1TO8(bitlen);
	/* The & 0x7 is to check for leftover bits. */
	if ((bitlen & 0x7) != 0)
		numbytes++;
	while (numbytes-- != 0) {
		if (pflag) {
			/* Print no keys if paranoid */
			if (fprintf(where, "XX") < 0)
				return (-1);
		} else {
			if (fprintf(where, "%02x", *keyp++) < 0)
				return (-1);
		}
	}
	if (fprintf(where, "/%u", bitlen) < 0)
		return (-1);
	return (0);
}

/*
 * Print an authentication or encryption algorithm
 */
static int
dump_generic_alg(uint8_t alg_num, int proto_num, FILE *where)
{
	struct ipsecalgent *alg;

	alg = getipsecalgbynum(alg_num, proto_num, NULL);
	if (alg == NULL) {
		if (fprintf(where, gettext("<unknown %u>"), alg_num) < 0)
			return (-1);
		return (0);
	}

	/*
	 * Special-case <none> for backward output compat.
	 * Assume that SADB_AALG_NONE == SADB_EALG_NONE.
	 */
	if (alg_num == SADB_AALG_NONE) {
		if (fputs(gettext("<none>"), where) == EOF)
			return (-1);
	} else {
		if (fputs(alg->a_names[0], where) == EOF)
			return (-1);
	}

	freeipsecalgent(alg);
	return (0);
}

int
dump_aalg(uint8_t aalg, FILE *where)
{
	return (dump_generic_alg(aalg, IPSEC_PROTO_AH, where));
}

int
dump_ealg(uint8_t ealg, FILE *where)
{
	return (dump_generic_alg(ealg, IPSEC_PROTO_ESP, where));
}

/*
 * Print an SADB_IDENTTYPE string
 *
 * Also return TRUE if the actual ident may be printed, FALSE if not.
 *
 * If rc is not NULL, set its value to -1 if an error occured while writing
 * to the specified file, zero otherwise.
 */
boolean_t
dump_sadb_idtype(uint8_t idtype, FILE *where, int *rc)
{
	boolean_t canprint = B_TRUE;
	int rc_val = 0;

	switch (idtype) {
	case SADB_IDENTTYPE_PREFIX:
		if (fputs(gettext("prefix"), where) == EOF)
			rc_val = -1;
		break;
	case SADB_IDENTTYPE_FQDN:
		if (fputs(gettext("FQDN"), where) == EOF)
			rc_val = -1;
		break;
	case SADB_IDENTTYPE_USER_FQDN:
		if (fputs(gettext("user-FQDN (mbox)"), where) == EOF)
			rc_val = -1;
		break;
	case SADB_X_IDENTTYPE_DN:
		if (fputs(gettext("ASN.1 DER Distinguished Name"),
		    where) == EOF)
			rc_val = -1;
		canprint = B_FALSE;
		break;
	case SADB_X_IDENTTYPE_GN:
		if (fputs(gettext("ASN.1 DER Generic Name"), where) == EOF)
			rc_val = -1;
		canprint = B_FALSE;
		break;
	case SADB_X_IDENTTYPE_KEY_ID:
		if (fputs(gettext("Generic key id"), where) == EOF)
			rc_val = -1;
		break;
	case SADB_X_IDENTTYPE_ADDR_RANGE:
		if (fputs(gettext("Address range"), where) == EOF)
			rc_val = -1;
		break;
	default:
		if (fprintf(where, gettext("<unknown %u>"), idtype) < 0)
			rc_val = -1;
		break;
	}

	if (rc != NULL)
		*rc = rc_val;

	return (canprint);
}

/*
 * Slice an argv/argc vector from an interactive line or a read-file line.
 */
static int
create_argv(char *ibuf, int *newargc, char ***thisargv)
{
	unsigned int argvlen = START_ARG;
	char **current;
	boolean_t firstchar = B_TRUE;
	boolean_t inquotes = B_FALSE;

	*thisargv = malloc(sizeof (char *) * argvlen);
	if ((*thisargv) == NULL)
		return (MEMORY_ALLOCATION);
	current = *thisargv;
	*current = NULL;

	for (; *ibuf != '\0'; ibuf++) {
		if (isspace(*ibuf)) {
			if (inquotes) {
				continue;
			}
			if (*current != NULL) {
				*ibuf = '\0';
				current++;
				if (*thisargv + argvlen == current) {
					/* Regrow ***thisargv. */
					if (argvlen == TOO_MANY_ARGS) {
						free(*thisargv);
						return (TOO_MANY_TOKENS);
					}
					/* Double the allocation. */
					current = realloc(*thisargv,
					    sizeof (char *) * (argvlen << 1));
					if (current == NULL) {
						free(*thisargv);
						return (MEMORY_ALLOCATION);
					}
					*thisargv = current;
					current += argvlen;
					argvlen <<= 1;	/* Double the size. */
				}
				*current = NULL;
			}
		} else {
			if (firstchar) {
				firstchar = B_FALSE;
				if (*ibuf == COMMENT_CHAR) {
					free(*thisargv);
					return (COMMENT_LINE);
				}
			}
			if (*ibuf == QUOTE_CHAR) {
				if (inquotes) {
					inquotes = B_FALSE;
					*ibuf = '\0';
				} else {
					inquotes = B_TRUE;
				}
				continue;
			}
			if (*current == NULL) {
				*current = ibuf;
				(*newargc)++;
			}
		}
	}

	/*
	 * Tricky corner case...
	 * I've parsed _exactly_ the amount of args as I have space.  It
	 * won't return NULL-terminated, and bad things will happen to
	 * the caller.
	 */
	if (argvlen == *newargc) {
		current = realloc(*thisargv, sizeof (char *) * (argvlen + 1));
		if (current == NULL) {
			free(*thisargv);
			return (MEMORY_ALLOCATION);
		}
		*thisargv = current;
		current[argvlen] = NULL;
	}

	return (SUCCESS);
}

/*
 * Enter a mode where commands are read from a file.  Treat stdin special.
 */
void
do_interactive(FILE *infile, char *promptstring, parse_cmdln_fn parseit)
{
	char		ibuf[IBUF_SIZE], holder[IBUF_SIZE];
	char		*hptr, **thisargv;
	int		thisargc;
	boolean_t	continue_in_progress = B_FALSE;

	(void) setjmp(env);

	interactive = B_TRUE;
	bzero(ibuf, IBUF_SIZE);

	if (infile == stdin) {
		(void) printf("%s", promptstring);
		(void) fflush(stdout);
	} else {
		readfile = B_TRUE;
	}

	while (fgets(ibuf, IBUF_SIZE, infile) != NULL) {
		if (readfile)
			lineno++;
		thisargc = 0;
		thisargv = NULL;

		/*
		 * Check byte IBUF_SIZE - 2, because byte IBUF_SIZE - 1 will
		 * be null-terminated because of fgets().
		 */
		if (ibuf[IBUF_SIZE - 2] != '\0') {
			(void) fprintf(stderr,
			    gettext("Line %d too big.\n"), lineno);
			exit(1);
		}

		if (!continue_in_progress) {
			/* Use -2 because of \n from fgets. */
			if (ibuf[strlen(ibuf) - 2] == CONT_CHAR) {
				/*
				 * Can use strcpy here, I've checked the
				 * length already.
				 */
				(void) strcpy(holder, ibuf);
				hptr = &(holder[strlen(holder)]);

				/* Remove the CONT_CHAR from the string. */
				hptr[-2] = ' ';

				continue_in_progress = B_TRUE;
				bzero(ibuf, IBUF_SIZE);
				continue;
			}
		} else {
			/* Handle continuations... */
			(void) strncpy(hptr, ibuf,
			    (size_t)(&(holder[IBUF_SIZE]) - hptr));
			if (holder[IBUF_SIZE - 1] != '\0') {
				(void) fprintf(stderr,
				    gettext("Command buffer overrun.\n"));
				exit(1);
			}
			/* Use - 2 because of \n from fgets. */
			if (hptr[strlen(hptr) - 2] == CONT_CHAR) {
				bzero(ibuf, IBUF_SIZE);
				hptr += strlen(hptr);

				/* Remove the CONT_CHAR from the string. */
				hptr[-2] = ' ';

				continue;
			} else {
				continue_in_progress = B_FALSE;
				/*
				 * I've already checked the length...
				 */
				(void) strcpy(ibuf, holder);
			}
		}

		switch (create_argv(ibuf, &thisargc, &thisargv)) {
		case TOO_MANY_TOKENS:
			(void) fprintf(stderr,
			    gettext("Too many input tokens.\n"));
			exit(1);
			break;
		case MEMORY_ALLOCATION:
			(void) fprintf(stderr,
			    gettext("Memory allocation error.\n"));
			exit(1);
			break;
		case COMMENT_LINE:
			/* Comment line. */
			break;
		default:
			parseit(thisargc, thisargv);
			free(thisargv);
			if (infile == stdin) {
				(void) printf("%s", promptstring);
				(void) fflush(stdout);
			}
			break;
		}
		bzero(ibuf, IBUF_SIZE);
	}
	if (!readfile) {
		(void) putchar('\n');
		(void) fflush(stdout);
	}
	exit(0);
}

/*
 * Functions to parse strings that represent a debug or privilege level.
 * These functions are copied from main.c and door.c in usr.lib/in.iked/common.
 * If this file evolves into a common library that may be used by in.iked
 * as well as the usr.sbin utilities, those duplicate functions should be
 * deleted.
 *
 * A privilege level may be represented by a simple keyword, corresponding
 * to one of the possible levels.  A debug level may be represented by a
 * series of keywords, separated by '+' or '-', indicating categories to
 * be added or removed from the set of categories in the debug level.
 * For example, +all-op corresponds to level 0xfffffffb (all flags except
 * for D_OP set); while p1+p2+pfkey corresponds to level 0x38.  Note that
 * the leading '+' is implicit; the first keyword in the list must be for
 * a category that is to be added.
 *
 * These parsing functions make use of a local version of strtok, strtok_d,
 * which includes an additional parameter, char *delim.  This param is filled
 * in with the character which ends the returned token.  In other words,
 * this version of strtok, in addition to returning the token, also returns
 * the single character delimiter from the original string which marked the
 * end of the token.
 */
static char *
strtok_d(char *string, const char *sepset, char *delim)
{
	static char	*lasts;
	char		*q, *r;

	/* first or subsequent call */
	if (string == NULL)
		string = lasts;

	if (string == 0)		/* return if no tokens remaining */
		return (NULL);

	q = string + strspn(string, sepset);	/* skip leading separators */

	if (*q == '\0')			/* return if no tokens remaining */
		return (NULL);

	if ((r = strpbrk(q, sepset)) == NULL) {		/* move past token */
		lasts = 0;	/* indicate that this is last token */
	} else {
		*delim = *r;	/* save delimitor */
		*r = '\0';
		lasts = r + 1;
	}
	return (q);
}

static keywdtab_t	privtab[] = {
	{ IKE_PRIV_MINIMUM,	"base" },
	{ IKE_PRIV_MODKEYS,	"modkeys" },
	{ IKE_PRIV_KEYMAT,	"keymat" },
	{ IKE_PRIV_MINIMUM,	"0" },
};

int
privstr2num(char *str)
{
	keywdtab_t	*pp;
	char		*endp;
	int		 priv;

	for (pp = privtab; pp < A_END(privtab); pp++) {
		if (strcasecmp(str, pp->kw_str) == 0)
			return (pp->kw_tag);
	}

	priv = strtol(str, &endp, 0);
	if (*endp == '\0')
		return (priv);

	return (-1);
}

static keywdtab_t	dbgtab[] = {
	{ D_CERT,	"cert" },
	{ D_KEY,	"key" },
	{ D_OP,		"op" },
	{ D_P1,		"p1" },
	{ D_P1,		"phase1" },
	{ D_P2,		"p2" },
	{ D_P2,		"phase2" },
	{ D_PFKEY,	"pfkey" },
	{ D_POL,	"pol" },
	{ D_POL,	"policy" },
	{ D_PROP,	"prop" },
	{ D_DOOR,	"door" },
	{ D_CONFIG,	"config" },
	{ D_ALL,	"all" },
	{ 0,		"0" },
};

int
dbgstr2num(char *str)
{
	keywdtab_t	*dp;

	for (dp = dbgtab; dp < A_END(dbgtab); dp++) {
		if (strcasecmp(str, dp->kw_str) == 0)
			return (dp->kw_tag);
	}
	return (D_INVALID);
}

int
parsedbgopts(char *optarg)
{
	char	*argp, *endp, op, nextop;
	int	mask = 0, new;

	mask = strtol(optarg, &endp, 0);
	if (*endp == '\0')
		return (mask);

	op = optarg[0];
	if (op != '-')
		op = '+';
	argp = strtok_d(optarg, "+-", &nextop);
	do {
		new = dbgstr2num(argp);
		if (new == D_INVALID) {
			/* we encountered an invalid keywd */
			return (new);
		}
		if (op == '+') {
			mask |= new;
		} else {
			mask &= ~new;
		}
		op = nextop;
	} while ((argp = strtok_d(NULL, "+-", &nextop)) != NULL);

	return (mask);
}


/*
 * functions to manipulate the kmcookie-label mapping file
 */

/*
 * Open, lockf, fdopen the given file, returning a FILE * on success,
 * or NULL on failure.
 */
FILE *
kmc_open_and_lock(char *name)
{
	int	fd, rtnerr;
	FILE	*fp;

	if ((fd = open(name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
		return (NULL);
	}
	if (lockf(fd, F_LOCK, 0) < 0) {
		return (NULL);
	}
	if ((fp = fdopen(fd, "a+")) == NULL) {
		return (NULL);
	}
	if (fseek(fp, 0, SEEK_SET) < 0) {
		/* save errno in case fclose changes it */
		rtnerr = errno;
		(void) fclose(fp);
		errno = rtnerr;
		return (NULL);
	}
	return (fp);
}

/*
 * Extract an integer cookie and string label from a line from the
 * kmcookie-label file.  Return -1 on failure, 0 on success.
 */
int
kmc_parse_line(char *line, int *cookie, char **label)
{
	char	*cookiestr;

	*cookie = 0;
	*label = NULL;

	cookiestr = strtok(line, " \t\n");
	if (cookiestr == NULL) {
		return (-1);
	}

	/* Everything that follows, up to the newline, is the label. */
	*label = strtok(NULL, "\n");
	if (*label == NULL) {
		return (-1);
	}

	*cookie = atoi(cookiestr);
	return (0);
}

/*
 * Insert a mapping into the file (if it's not already there), given the
 * new label.  Return the assigned cookie, or -1 on error.
 */
int
kmc_insert_mapping(char *label)
{
	FILE	*map;
	char	linebuf[MAXLINESIZE];
	char	*cur_label;
	int	max_cookie = 0, cur_cookie, rtn_cookie;
	int	rtnerr = 0;
	boolean_t	found = B_FALSE;

	/* open and lock the file; will sleep until lock is available */
	if ((map = kmc_open_and_lock(KMCFILE)) == NULL) {
		/* kmc_open_and_lock() sets errno appropriately */
		return (-1);
	}

	while (fgets(linebuf, sizeof (linebuf), map) != NULL) {

		if (kmc_parse_line(linebuf, &cur_cookie, &cur_label) < 0) {
			rtnerr = EINVAL;
			goto error;
		}

		if (cur_cookie > max_cookie)
			max_cookie = cur_cookie;

		if ((!found) && (strcmp(cur_label, label) == 0)) {
			found = B_TRUE;
			rtn_cookie = cur_cookie;
		}
	}

	if (!found) {
		rtn_cookie = ++max_cookie;
		if ((fprintf(map, "%u\t%s\n", rtn_cookie, label) < 0) ||
		    (fflush(map) < 0)) {
			rtnerr = errno;
			goto error;
		}
	}
	(void) fclose(map);

	return (rtn_cookie);

error:
	(void) fclose(map);
	errno = rtnerr;
	return (-1);
}

/*
 * Lookup the given cookie and return its corresponding label.  Return
 * a pointer to the label on success, NULL on error (or if the label is
 * not found).  Note that the returned label pointer points to a static
 * string, so the label will be overwritten by a subsequent call to the
 * function; the function is also not thread-safe as a result.
 */
char *
kmc_lookup_by_cookie(int cookie)
{
	FILE		*map;
	static char	linebuf[MAXLINESIZE];
	char		*cur_label;
	int		cur_cookie;

	if ((map = kmc_open_and_lock(KMCFILE)) == NULL) {
		return (NULL);
	}

	while (fgets(linebuf, sizeof (linebuf), map) != NULL) {

		if (kmc_parse_line(linebuf, &cur_cookie, &cur_label) < 0) {
			(void) fclose(map);
			return (NULL);
		}

		if (cookie == cur_cookie) {
			(void) fclose(map);
			return (cur_label);
		}
	}
	(void) fclose(map);

	return (NULL);
}

/*
 * Parse basic extension headers and return in the passed-in pointer vector.
 * Return values include:
 *
 *	KGE_OK	Everything's nice and parsed out.
 *		If there are no extensions, place NULL in extv[0].
 *	KGE_DUP	There is a duplicate extension.
 *		First instance in appropriate bin.  First duplicate in
 *		extv[0].
 *	KGE_UNK	Unknown extension type encountered.  extv[0] contains
 *		unknown header.
 *	KGE_LEN	Extension length error.
 *	KGE_CHK	High-level reality check failed on specific extension.
 *
 * My apologies for some of the pointer arithmetic in here.  I'm thinking
 * like an assembly programmer, yet trying to make the compiler happy.
 */
int
spdsock_get_ext(spd_ext_t *extv[], spd_msg_t *basehdr, uint_t msgsize,
    char *diag_buf, uint_t diag_buf_len)
{
	int i;

	if (diag_buf != NULL)
		diag_buf[0] = '\0';

	for (i = 1; i <= SPD_EXT_MAX; i++)
		extv[i] = NULL;

	i = 0;
	/* Use extv[0] as the "current working pointer". */

	extv[0] = (spd_ext_t *)(basehdr + 1);
	msgsize = SPD_64TO8(msgsize);

	while ((char *)extv[0] < ((char *)basehdr + msgsize)) {
		/* Check for unknown headers. */
		i++;

		if (extv[0]->spd_ext_type == 0 ||
		    extv[0]->spd_ext_type > SPD_EXT_MAX) {
			if (diag_buf != NULL) {
				(void) snprintf(diag_buf, diag_buf_len,
				    "spdsock ext 0x%X unknown: 0x%X",
				    i, extv[0]->spd_ext_type);
			}
			return (KGE_UNK);
		}

		/*
		 * Check length.  Use uint64_t because extlen is in units
		 * of 64-bit words.  If length goes beyond the msgsize,
		 * return an error.  (Zero length also qualifies here.)
		 */
		if (extv[0]->spd_ext_len == 0 ||
		    (uint8_t *)((uint64_t *)extv[0] + extv[0]->spd_ext_len) >
		    (uint8_t *)((uint8_t *)basehdr + msgsize))
			return (KGE_LEN);

		/* Check for redundant headers. */
		if (extv[extv[0]->spd_ext_type] != NULL)
			return (KGE_DUP);

		/* If I make it here, assign the appropriate bin. */
		extv[extv[0]->spd_ext_type] = extv[0];

		/* Advance pointer (See above for uint64_t ptr reasoning.) */
		extv[0] = (spd_ext_t *)
		    ((uint64_t *)extv[0] + extv[0]->spd_ext_len);
	}

	/* Everything's cool. */

	/*
	 * If extv[0] == NULL, then there are no extension headers in this
	 * message.  Ensure that this is the case.
	 */
	if (extv[0] == (spd_ext_t *)(basehdr + 1))
		extv[0] = NULL;

	return (KGE_OK);
}

const char *
spdsock_diag(int diagnostic)
{
	switch (diagnostic) {
	case SPD_DIAGNOSTIC_NONE:
		return (gettext("no error"));
	case SPD_DIAGNOSTIC_UNKNOWN_EXT:
		return (gettext("unknown extension"));
	case SPD_DIAGNOSTIC_BAD_EXTLEN:
		return (gettext("bad extension length"));
	case SPD_DIAGNOSTIC_NO_RULE_EXT:
		return (gettext("no rule extension"));
	case SPD_DIAGNOSTIC_BAD_ADDR_LEN:
		return (gettext("bad address len"));
	case SPD_DIAGNOSTIC_MIXED_AF:
		return (gettext("mixed address family"));
	case SPD_DIAGNOSTIC_ADD_NO_MEM:
		return (gettext("add: no memory"));
	case SPD_DIAGNOSTIC_ADD_WRONG_ACT_COUNT:
		return (gettext("add: wrong action count"));
	case SPD_DIAGNOSTIC_ADD_BAD_TYPE:
		return (gettext("add: bad type"));
	case SPD_DIAGNOSTIC_ADD_BAD_FLAGS:
		return (gettext("add: bad flags"));
	case SPD_DIAGNOSTIC_ADD_INCON_FLAGS:
		return (gettext("add: inconsistent flags"));
	case SPD_DIAGNOSTIC_MALFORMED_LCLPORT:
		return (gettext("malformed local port"));
	case SPD_DIAGNOSTIC_DUPLICATE_LCLPORT:
		return (gettext("duplicate local port"));
	case SPD_DIAGNOSTIC_MALFORMED_REMPORT:
		return (gettext("malformed remote port"));
	case SPD_DIAGNOSTIC_DUPLICATE_REMPORT:
		return (gettext("duplicate remote port"));
	case SPD_DIAGNOSTIC_MALFORMED_PROTO:
		return (gettext("malformed proto"));
	case SPD_DIAGNOSTIC_DUPLICATE_PROTO:
		return (gettext("duplicate proto"));
	case SPD_DIAGNOSTIC_MALFORMED_LCLADDR:
		return (gettext("malformed local address"));
	case SPD_DIAGNOSTIC_DUPLICATE_LCLADDR:
		return (gettext("duplicate local address"));
	case SPD_DIAGNOSTIC_MALFORMED_REMADDR:
		return (gettext("malformed remote address"));
	case SPD_DIAGNOSTIC_DUPLICATE_REMADDR:
		return (gettext("duplicate remote address"));
	case SPD_DIAGNOSTIC_MALFORMED_ACTION:
		return (gettext("malformed action"));
	case SPD_DIAGNOSTIC_DUPLICATE_ACTION:
		return (gettext("duplicate action"));
	case SPD_DIAGNOSTIC_MALFORMED_RULE:
		return (gettext("malformed rule"));
	case SPD_DIAGNOSTIC_DUPLICATE_RULE:
		return (gettext("duplicate rule"));
	case SPD_DIAGNOSTIC_MALFORMED_RULESET:
		return (gettext("malformed ruleset"));
	case SPD_DIAGNOSTIC_DUPLICATE_RULESET:
		return (gettext("duplicate ruleset"));
	case SPD_DIAGNOSTIC_INVALID_RULE_INDEX:
		return (gettext("invalid rule index"));
	case SPD_DIAGNOSTIC_BAD_SPDID:
		return (gettext("bad spdid"));
	case SPD_DIAGNOSTIC_BAD_MSG_TYPE:
		return (gettext("bad message type"));
	case SPD_DIAGNOSTIC_UNSUPP_AH_ALG:
		return (gettext("unsupported AH algorithm"));
	case SPD_DIAGNOSTIC_UNSUPP_ESP_ENCR_ALG:
		return (gettext("unsupported ESP encryption algorithm"));
	case SPD_DIAGNOSTIC_UNSUPP_ESP_AUTH_ALG:
		return (gettext("unsupported ESP authentication algorithm"));
	case SPD_DIAGNOSTIC_UNSUPP_AH_KEYSIZE:
		return (gettext("unsupported AH key size"));
	case SPD_DIAGNOSTIC_UNSUPP_ESP_ENCR_KEYSIZE:
		return (gettext("unsupported ESP encryption key size"));
	case SPD_DIAGNOSTIC_UNSUPP_ESP_AUTH_KEYSIZE:
		return (gettext("unsupported ESP authentication key size"));
	case SPD_DIAGNOSTIC_NO_ACTION_EXT:
		return (gettext("No ACTION extension"));
	case SPD_DIAGNOSTIC_ALG_ID_RANGE:
		return (gettext("invalid algorithm identifer"));
	case SPD_DIAGNOSTIC_ALG_NUM_KEY_SIZES:
		return (gettext("number of key sizes inconsistent"));
	case SPD_DIAGNOSTIC_ALG_NUM_BLOCK_SIZES:
		return (gettext("number of block sizes inconsistent"));
	case SPD_DIAGNOSTIC_ALG_MECH_NAME_LEN:
		return (gettext("invalid mechanism name length"));
	case SPD_DIAGNOSTIC_NOT_GLOBAL_OP:
		return (gettext("operation not applicable to all policies"));
	case SPD_DIAGNOSTIC_NO_TUNNEL_SELECTORS:
		return (gettext("using selectors on a transport-mode tunnel"));
	default:
		return (gettext("unknown diagnostic"));
	}
}

/*
 * PF_KEY Diagnostic table.
 *
 * PF_KEY NOTE:  If you change pfkeyv2.h's SADB_X_DIAGNOSTIC_* space, this is
 * where you need to add new messages.
 */

const char *
keysock_diag(int diagnostic)
{
	switch (diagnostic) {
	case SADB_X_DIAGNOSTIC_NONE:
		return (gettext("No diagnostic"));
	case SADB_X_DIAGNOSTIC_UNKNOWN_MSG:
		return (gettext("Unknown message type"));
	case SADB_X_DIAGNOSTIC_UNKNOWN_EXT:
		return (gettext("Unknown extension type"));
	case SADB_X_DIAGNOSTIC_BAD_EXTLEN:
		return (gettext("Bad extension length"));
	case SADB_X_DIAGNOSTIC_UNKNOWN_SATYPE:
		return (gettext("Unknown Security Association type"));
	case SADB_X_DIAGNOSTIC_SATYPE_NEEDED:
		return (gettext("Specific Security Association type needed"));
	case SADB_X_DIAGNOSTIC_NO_SADBS:
		return (gettext("No Security Association Databases present"));
	case SADB_X_DIAGNOSTIC_NO_EXT:
		return (gettext("No extensions needed for message"));
	case SADB_X_DIAGNOSTIC_BAD_SRC_AF:
		return (gettext("Bad source address family"));
	case SADB_X_DIAGNOSTIC_BAD_DST_AF:
		return (gettext("Bad destination address family"));
	case SADB_X_DIAGNOSTIC_BAD_PROXY_AF:
		return (gettext("Bad inner-source address family"));
	case SADB_X_DIAGNOSTIC_AF_MISMATCH:
		return (gettext("Source/destination address family mismatch"));
	case SADB_X_DIAGNOSTIC_BAD_SRC:
		return (gettext("Bad source address value"));
	case SADB_X_DIAGNOSTIC_BAD_DST:
		return (gettext("Bad destination address value"));
	case SADB_X_DIAGNOSTIC_ALLOC_HSERR:
		return (gettext("Soft allocations limit more than hard limit"));
	case SADB_X_DIAGNOSTIC_BYTES_HSERR:
		return (gettext("Soft bytes limit more than hard limit"));
	case SADB_X_DIAGNOSTIC_ADDTIME_HSERR:
		return (gettext("Soft add expiration time later "
		    "than hard expiration time"));
	case SADB_X_DIAGNOSTIC_USETIME_HSERR:
		return (gettext("Soft use expiration time later "
		    "than hard expiration time"));
	case SADB_X_DIAGNOSTIC_MISSING_SRC:
		return (gettext("Missing source address"));
	case SADB_X_DIAGNOSTIC_MISSING_DST:
		return (gettext("Missing destination address"));
	case SADB_X_DIAGNOSTIC_MISSING_SA:
		return (gettext("Missing SA extension"));
	case SADB_X_DIAGNOSTIC_MISSING_EKEY:
		return (gettext("Missing encryption key"));
	case SADB_X_DIAGNOSTIC_MISSING_AKEY:
		return (gettext("Missing authentication key"));
	case SADB_X_DIAGNOSTIC_MISSING_RANGE:
		return (gettext("Missing SPI range"));
	case SADB_X_DIAGNOSTIC_DUPLICATE_SRC:
		return (gettext("Duplicate source address"));
	case SADB_X_DIAGNOSTIC_DUPLICATE_DST:
		return (gettext("Duplicate destination address"));
	case SADB_X_DIAGNOSTIC_DUPLICATE_SA:
		return (gettext("Duplicate SA extension"));
	case SADB_X_DIAGNOSTIC_DUPLICATE_EKEY:
		return (gettext("Duplicate encryption key"));
	case SADB_X_DIAGNOSTIC_DUPLICATE_AKEY:
		return (gettext("Duplicate authentication key"));
	case SADB_X_DIAGNOSTIC_DUPLICATE_RANGE:
		return (gettext("Duplicate SPI range"));
	case SADB_X_DIAGNOSTIC_MALFORMED_SRC:
		return (gettext("Malformed source address"));
	case SADB_X_DIAGNOSTIC_MALFORMED_DST:
		return (gettext("Malformed destination address"));
	case SADB_X_DIAGNOSTIC_MALFORMED_SA:
		return (gettext("Malformed SA extension"));
	case SADB_X_DIAGNOSTIC_MALFORMED_EKEY:
		return (gettext("Malformed encryption key"));
	case SADB_X_DIAGNOSTIC_MALFORMED_AKEY:
		return (gettext("Malformed authentication key"));
	case SADB_X_DIAGNOSTIC_MALFORMED_RANGE:
		return (gettext("Malformed SPI range"));
	case SADB_X_DIAGNOSTIC_AKEY_PRESENT:
		return (gettext("Authentication key not needed"));
	case SADB_X_DIAGNOSTIC_EKEY_PRESENT:
		return (gettext("Encryption key not needed"));
	case SADB_X_DIAGNOSTIC_PROP_PRESENT:
		return (gettext("Proposal extension not needed"));
	case SADB_X_DIAGNOSTIC_SUPP_PRESENT:
		return (gettext("Supported algorithms extension not needed"));
	case SADB_X_DIAGNOSTIC_BAD_AALG:
		return (gettext("Unsupported authentication algorithm"));
	case SADB_X_DIAGNOSTIC_BAD_EALG:
		return (gettext("Unsupported encryption algorithm"));
	case SADB_X_DIAGNOSTIC_BAD_SAFLAGS:
		return (gettext("Invalid SA flags"));
	case SADB_X_DIAGNOSTIC_BAD_SASTATE:
		return (gettext("Invalid SA state"));
	case SADB_X_DIAGNOSTIC_BAD_AKEYBITS:
		return (gettext("Bad number of authentication bits"));
	case SADB_X_DIAGNOSTIC_BAD_EKEYBITS:
		return (gettext("Bad number of encryption bits"));
	case SADB_X_DIAGNOSTIC_ENCR_NOTSUPP:
		return (gettext("Encryption not supported for this SA type"));
	case SADB_X_DIAGNOSTIC_WEAK_EKEY:
		return (gettext("Weak encryption key"));
	case SADB_X_DIAGNOSTIC_WEAK_AKEY:
		return (gettext("Weak authentication key"));
	case SADB_X_DIAGNOSTIC_DUPLICATE_KMP:
		return (gettext("Duplicate key management protocol"));
	case SADB_X_DIAGNOSTIC_DUPLICATE_KMC:
		return (gettext("Duplicate key management cookie"));
	case SADB_X_DIAGNOSTIC_MISSING_NATT_LOC:
		return (gettext("Missing NAT-T local address"));
	case SADB_X_DIAGNOSTIC_MISSING_NATT_REM:
		return (gettext("Missing NAT-T remote address"));
	case SADB_X_DIAGNOSTIC_DUPLICATE_NATT_LOC:
		return (gettext("Duplicate NAT-T local address"));
	case SADB_X_DIAGNOSTIC_DUPLICATE_NATT_REM:
		return (gettext("Duplicate NAT-T remote address"));
	case SADB_X_DIAGNOSTIC_MALFORMED_NATT_LOC:
		return (gettext("Malformed NAT-T local address"));
	case SADB_X_DIAGNOSTIC_MALFORMED_NATT_REM:
		return (gettext("Malformed NAT-T remote address"));
	case SADB_X_DIAGNOSTIC_DUPLICATE_NATT_PORTS:
		return (gettext("Duplicate NAT-T ports"));
	case SADB_X_DIAGNOSTIC_MISSING_INNER_SRC:
		return (gettext("Missing inner source address"));
	case SADB_X_DIAGNOSTIC_MISSING_INNER_DST:
		return (gettext("Missing inner destination address"));
	case SADB_X_DIAGNOSTIC_DUPLICATE_INNER_SRC:
		return (gettext("Duplicate inner source address"));
	case SADB_X_DIAGNOSTIC_DUPLICATE_INNER_DST:
		return (gettext("Duplicate inner destination address"));
	case SADB_X_DIAGNOSTIC_MALFORMED_INNER_SRC:
		return (gettext("Malformed inner source address"));
	case SADB_X_DIAGNOSTIC_MALFORMED_INNER_DST:
		return (gettext("Malformed inner destination address"));
	case SADB_X_DIAGNOSTIC_PREFIX_INNER_SRC:
		return (gettext("Invalid inner-source prefix length "));
	case SADB_X_DIAGNOSTIC_PREFIX_INNER_DST:
		return (gettext("Invalid inner-destination prefix length"));
	case SADB_X_DIAGNOSTIC_BAD_INNER_DST_AF:
		return (gettext("Bad inner-destination address family"));
	case SADB_X_DIAGNOSTIC_INNER_AF_MISMATCH:
		return (gettext(
		    "Inner source/destination address family mismatch"));
	case SADB_X_DIAGNOSTIC_BAD_NATT_REM_AF:
		return (gettext("Bad NAT-T remote address family"));
	case SADB_X_DIAGNOSTIC_BAD_NATT_LOC_AF:
		return (gettext("Bad NAT-T local address family"));
	case SADB_X_DIAGNOSTIC_PROTO_MISMATCH:
		return (gettext("Source/desination protocol mismatch"));
	case SADB_X_DIAGNOSTIC_INNER_PROTO_MISMATCH:
		return (gettext("Inner source/desination protocol mismatch"));
	case SADB_X_DIAGNOSTIC_DUAL_PORT_SETS:
		return (gettext("Both inner ports and outer ports are set"));
	default:
		return (gettext("Unknown diagnostic code"));
	}
}

/*
 * Convert an IPv6 mask to a prefix len.  I assume all IPv6 masks are
 * contiguous, so I stop at the first zero bit!
 */
int
in_masktoprefix(uint8_t *mask, boolean_t is_v4mapped)
{
	int rc = 0;
	uint8_t last;
	int limit = IPV6_ABITS;

	if (is_v4mapped) {
		mask += ((IPV6_ABITS - IP_ABITS)/8);
		limit = IP_ABITS;
	}

	while (*mask == 0xff) {
		rc += 8;
		if (rc == limit)
			return (limit);
		mask++;
	}

	last = *mask;
	while (last != 0) {
		rc++;
		last = (last << 1) & 0xff;
	}

	return (rc);
}

/*
 * Expand the diagnostic code into a message.
 */
void
print_diagnostic(FILE *file, uint16_t diagnostic)
{
	/* Use two spaces so above strings can fit on the line. */
	(void) fprintf(file, gettext("  Diagnostic code %u:  %s.\n"),
	    diagnostic, keysock_diag(diagnostic));
}

/*
 * Prints the base PF_KEY message.
 */
void
print_sadb_msg(struct sadb_msg *samsg, time_t wallclock, boolean_t vflag)
{
	if (wallclock != 0)
		printsatime(wallclock, gettext("%sTimestamp: %s\n"), "", NULL,
		    vflag);

	(void) printf(gettext("Base message (version %u) type "),
	    samsg->sadb_msg_version);
	switch (samsg->sadb_msg_type) {
	case SADB_RESERVED:
		(void) printf(gettext("RESERVED (warning: set to 0)"));
		break;
	case SADB_GETSPI:
		(void) printf("GETSPI");
		break;
	case SADB_UPDATE:
		(void) printf("UPDATE");
		break;
	case SADB_ADD:
		(void) printf("ADD");
		break;
	case SADB_DELETE:
		(void) printf("DELETE");
		break;
	case SADB_GET:
		(void) printf("GET");
		break;
	case SADB_ACQUIRE:
		(void) printf("ACQUIRE");
		break;
	case SADB_REGISTER:
		(void) printf("REGISTER");
		break;
	case SADB_EXPIRE:
		(void) printf("EXPIRE");
		break;
	case SADB_FLUSH:
		(void) printf("FLUSH");
		break;
	case SADB_DUMP:
		(void) printf("DUMP");
		break;
	case SADB_X_PROMISC:
		(void) printf("X_PROMISC");
		break;
	case SADB_X_INVERSE_ACQUIRE:
		(void) printf("X_INVERSE_ACQUIRE");
		break;
	default:
		(void) printf(gettext("Unknown (%u)"), samsg->sadb_msg_type);
		break;
	}
	(void) printf(gettext(", SA type "));

	switch (samsg->sadb_msg_satype) {
	case SADB_SATYPE_UNSPEC:
		(void) printf(gettext("<unspecified/all>"));
		break;
	case SADB_SATYPE_AH:
		(void) printf("AH");
		break;
	case SADB_SATYPE_ESP:
		(void) printf("ESP");
		break;
	case SADB_SATYPE_RSVP:
		(void) printf("RSVP");
		break;
	case SADB_SATYPE_OSPFV2:
		(void) printf("OSPFv2");
		break;
	case SADB_SATYPE_RIPV2:
		(void) printf("RIPv2");
		break;
	case SADB_SATYPE_MIP:
		(void) printf(gettext("Mobile IP"));
		break;
	default:
		(void) printf(gettext("<unknown %u>"), samsg->sadb_msg_satype);
		break;
	}

	(void) printf(".\n");

	if (samsg->sadb_msg_errno != 0) {
		(void) printf(gettext("Error %s from PF_KEY.\n"),
		    strerror(samsg->sadb_msg_errno));
		print_diagnostic(stdout, samsg->sadb_x_msg_diagnostic);
	}

	(void) printf(gettext("Message length %u bytes, seq=%u, pid=%u.\n"),
	    SADB_64TO8(samsg->sadb_msg_len), samsg->sadb_msg_seq,
	    samsg->sadb_msg_pid);
}

/*
 * Print the SA extension for PF_KEY.
 */
void
print_sa(char *prefix, struct sadb_sa *assoc)
{
	if (assoc->sadb_sa_len != SADB_8TO64(sizeof (*assoc))) {
		warnx(gettext("WARNING: SA info extension length (%u) is bad."),
		    SADB_64TO8(assoc->sadb_sa_len));
	}

	(void) printf(gettext("%sSADB_ASSOC spi=0x%x, replay=%u, state="),
	    prefix, ntohl(assoc->sadb_sa_spi), assoc->sadb_sa_replay);
	switch (assoc->sadb_sa_state) {
	case SADB_SASTATE_LARVAL:
		(void) printf(gettext("LARVAL"));
		break;
	case SADB_SASTATE_MATURE:
		(void) printf(gettext("MATURE"));
		break;
	case SADB_SASTATE_DYING:
		(void) printf(gettext("DYING"));
		break;
	case SADB_SASTATE_DEAD:
		(void) printf(gettext("DEAD"));
		break;
	default:
		(void) printf(gettext("<unknown %u>"), assoc->sadb_sa_state);
	}

	if (assoc->sadb_sa_auth != SADB_AALG_NONE) {
		(void) printf(gettext("\n%sAuthentication algorithm = "),
		    prefix);
		(void) dump_aalg(assoc->sadb_sa_auth, stdout);
	}

	if (assoc->sadb_sa_encrypt != SADB_EALG_NONE) {
		(void) printf(gettext("\n%sEncryption algorithm = "), prefix);
		(void) dump_ealg(assoc->sadb_sa_encrypt, stdout);
	}

	(void) printf(gettext("\n%sflags=0x%x < "), prefix,
	    assoc->sadb_sa_flags);
	if (assoc->sadb_sa_flags & SADB_SAFLAGS_PFS)
		(void) printf("PFS ");
	if (assoc->sadb_sa_flags & SADB_SAFLAGS_NOREPLAY)
		(void) printf("NOREPLAY ");

	/* BEGIN Solaris-specific flags. */
	if (assoc->sadb_sa_flags & SADB_X_SAFLAGS_USED)
		(void) printf("X_USED ");
	if (assoc->sadb_sa_flags & SADB_X_SAFLAGS_UNIQUE)
		(void) printf("X_UNIQUE ");
	if (assoc->sadb_sa_flags & SADB_X_SAFLAGS_AALG1)
		(void) printf("X_AALG1 ");
	if (assoc->sadb_sa_flags & SADB_X_SAFLAGS_AALG2)
		(void) printf("X_AALG2 ");
	if (assoc->sadb_sa_flags & SADB_X_SAFLAGS_EALG1)
		(void) printf("X_EALG1 ");
	if (assoc->sadb_sa_flags & SADB_X_SAFLAGS_EALG2)
		(void) printf("X_EALG2 ");
	if (assoc->sadb_sa_flags & SADB_X_SAFLAGS_NATT_LOC)
		(void) printf("X_NATT_LOC ");
	if (assoc->sadb_sa_flags & SADB_X_SAFLAGS_NATT_REM)
		(void) printf("X_NATT_REM ");
	if (assoc->sadb_sa_flags & SADB_X_SAFLAGS_TUNNEL)
		(void) printf("X_TUNNEL ");
	/* END Solaris-specific flags. */

	(void) printf(">\n");
}

void
printsatime(int64_t lt, const char *msg, const char *pfx, const char *pfx2,
    boolean_t vflag)
{
	char tbuf[TBUF_SIZE]; /* For strftime() call. */
	const char *tp = tbuf;
	time_t t = lt;
	struct tm res;

	if (t != lt) {
		if (lt > 0)
			t = LONG_MAX;
		else
			t = LONG_MIN;
	}

	if (strftime(tbuf, TBUF_SIZE, NULL, localtime_r(&t, &res)) == 0)
		tp = gettext("<time conversion failed>");
	(void) printf(msg, pfx, tp);
	if (vflag && (pfx2 != NULL))
		(void) printf(gettext("%s\t(raw time value %llu)\n"), pfx2, lt);
}

/*
 * Print the SA lifetime information.  (An SADB_EXT_LIFETIME_* extension.)
 */
void
print_lifetimes(time_t wallclock, struct sadb_lifetime *current,
    struct sadb_lifetime *hard, struct sadb_lifetime *soft, boolean_t vflag)
{
	int64_t scratch;
	char *soft_prefix = gettext("SLT: ");
	char *hard_prefix = gettext("HLT: ");
	char *current_prefix = gettext("CLT: ");

	if (current != NULL &&
	    current->sadb_lifetime_len != SADB_8TO64(sizeof (*current))) {
		warnx(gettext("WARNING: CURRENT lifetime extension length "
			"(%u) is bad."),
		    SADB_64TO8(current->sadb_lifetime_len));
	}

	if (hard != NULL &&
	    hard->sadb_lifetime_len != SADB_8TO64(sizeof (*hard))) {
		warnx(gettext("WARNING: HARD lifetime "
			"extension length (%u) is bad."),
		    SADB_64TO8(hard->sadb_lifetime_len));
	}

	if (soft != NULL &&
	    soft->sadb_lifetime_len != SADB_8TO64(sizeof (*soft))) {
		warnx(gettext("WARNING: SOFT lifetime "
		    "extension length (%u) is bad."),
		    SADB_64TO8(soft->sadb_lifetime_len));
	}

	(void) printf(" LT: Lifetime information\n");

	if (current != NULL) {
		/* Express values as current values. */
		(void) printf(gettext(
		    "%s%llu bytes protected, %u allocations used.\n"),
		    current_prefix, current->sadb_lifetime_bytes,
		    current->sadb_lifetime_allocations);
		printsatime(current->sadb_lifetime_addtime,
		    gettext("%sSA added at time %s\n"),
		    current_prefix, current_prefix, vflag);
		if (current->sadb_lifetime_usetime != 0) {
			printsatime(current->sadb_lifetime_usetime,
			    gettext("%sSA first used at time %s\n"),
			    current_prefix, current_prefix, vflag);
		}
		printsatime(wallclock, gettext("%sTime now is %s\n"),
		    current_prefix, current_prefix, vflag);
	}

	if (soft != NULL) {
		(void) printf(gettext("%sSoft lifetime information:  "),
		    soft_prefix);
		(void) printf(gettext("%llu bytes of lifetime, %u "
		    "allocations.\n"), soft->sadb_lifetime_bytes,
		    soft->sadb_lifetime_allocations);
		(void) printf(gettext("%s%llu seconds of post-add lifetime.\n"),
		    soft_prefix, soft->sadb_lifetime_addtime);
		(void) printf(gettext("%s%llu seconds of post-use lifetime.\n"),
		    soft_prefix, soft->sadb_lifetime_usetime);
		/* If possible, express values as time remaining. */
		if (current != NULL) {
			if (soft->sadb_lifetime_bytes != 0)
				(void) printf(gettext(
				    "%s%llu more bytes can be protected.\n"),
				    soft_prefix,
				    (soft->sadb_lifetime_bytes >
					current->sadb_lifetime_bytes) ?
				    (soft->sadb_lifetime_bytes -
					current->sadb_lifetime_bytes) : (0));
			if (soft->sadb_lifetime_addtime != 0 ||
			    (soft->sadb_lifetime_usetime != 0 &&
				current->sadb_lifetime_usetime != 0)) {
				int64_t adddelta, usedelta;

				if (soft->sadb_lifetime_addtime != 0) {
					adddelta =
					    current->sadb_lifetime_addtime +
					    soft->sadb_lifetime_addtime -
					    wallclock;
				} else {
					adddelta = TIME_MAX;
				}

				if (soft->sadb_lifetime_usetime != 0 &&
				    current->sadb_lifetime_usetime != 0) {
					usedelta =
					    current->sadb_lifetime_usetime +
					    soft->sadb_lifetime_usetime -
					    wallclock;
				} else {
					usedelta = TIME_MAX;
				}
				(void) printf("%s", soft_prefix);
				scratch = MIN(adddelta, usedelta);
				if (scratch >= 0) {
					(void) printf(gettext("Soft expiration "
					    "occurs in %lld seconds, "),
					    scratch);
				} else {
					(void) printf(gettext(
					    "Soft expiration occurred "));
				}
				scratch += wallclock;
				printsatime(scratch, gettext("%sat %s.\n"), "",
				    soft_prefix, vflag);
			}
		}
	}

	if (hard != NULL) {
		(void) printf(gettext("%sHard lifetime information:  "),
		    hard_prefix);
		(void) printf(gettext("%llu bytes of lifetime, "
		    "%u allocations.\n"), hard->sadb_lifetime_bytes,
		    hard->sadb_lifetime_allocations);
		(void) printf(gettext("%s%llu seconds of post-add lifetime.\n"),
		    hard_prefix, hard->sadb_lifetime_addtime);
		(void) printf(gettext("%s%llu seconds of post-use lifetime.\n"),
		    hard_prefix, hard->sadb_lifetime_usetime);
		/* If possible, express values as time remaining. */
		if (current != NULL) {
			if (hard->sadb_lifetime_bytes != 0)
				(void) printf(gettext(
				    "%s%llu more bytes can be protected.\n"),
				    hard_prefix,
				    (hard->sadb_lifetime_bytes >
					current->sadb_lifetime_bytes) ?
				    (hard->sadb_lifetime_bytes -
					current->sadb_lifetime_bytes) : (0));
			if (hard->sadb_lifetime_addtime != 0 ||
			    (hard->sadb_lifetime_usetime != 0 &&
				current->sadb_lifetime_usetime != 0)) {
				int64_t adddelta, usedelta;

				if (hard->sadb_lifetime_addtime != 0) {
					adddelta =
					    current->sadb_lifetime_addtime +
					    hard->sadb_lifetime_addtime -
					    wallclock;
				} else {
					adddelta = TIME_MAX;
				}

				if (hard->sadb_lifetime_usetime != 0 &&
				    current->sadb_lifetime_usetime != 0) {
					usedelta =
					    current->sadb_lifetime_usetime +
					    hard->sadb_lifetime_usetime -
					    wallclock;
				} else {
					usedelta = TIME_MAX;
				}
				(void) printf("%s", hard_prefix);
				scratch = MIN(adddelta, usedelta);
				if (scratch >= 0) {
					(void) printf(gettext("Hard expiration "
					    "occurs in %lld seconds, "),
					    scratch);
				} else {
					(void) printf(gettext(
					    "Hard expiration occured "));
				}
				scratch += wallclock;
				printsatime(scratch, gettext("%sat %s.\n"), "",
				    hard_prefix, vflag);
			}
		}
	}
}

/*
 * Print an SADB_EXT_ADDRESS_* extension.
 */
void
print_address(char *prefix, struct sadb_address *addr)
{
	struct protoent *pe;

	(void) printf("%s", prefix);
	switch (addr->sadb_address_exttype) {
	case SADB_EXT_ADDRESS_SRC:
		(void) printf(gettext("Source address "));
		break;
	case SADB_X_EXT_ADDRESS_INNER_SRC:
		(void) printf(gettext("Inner source address "));
		break;
	case SADB_EXT_ADDRESS_DST:
		(void) printf(gettext("Destination address "));
		break;
	case SADB_X_EXT_ADDRESS_INNER_DST:
		(void) printf(gettext("Inner destination address "));
		break;
	case SADB_X_EXT_ADDRESS_NATT_LOC:
		(void) printf(gettext("NATT local address "));
		break;
	case SADB_X_EXT_ADDRESS_NATT_REM:
		(void) printf(gettext("NATT remote address "));
		break;
	}

	(void) printf(gettext("(proto=%d"), addr->sadb_address_proto);
	if (!nflag) {
		if (addr->sadb_address_proto == 0) {
			(void) printf(gettext("/<unspecified>"));
		} else if ((pe = getprotobynumber(addr->sadb_address_proto))
		    != NULL) {
			(void) printf("/%s", pe->p_name);
		} else {
			(void) printf(gettext("/<unknown>"));
		}
	}
	(void) printf(gettext(")\n%s"), prefix);
	(void) dump_sockaddr((struct sockaddr *)(addr + 1),
	    addr->sadb_address_prefixlen, B_FALSE, stdout);
}

/*
 * Print an SADB_EXT_KEY extension.
 */
void
print_key(char *prefix, struct sadb_key *key)
{
	(void) printf("%s", prefix);

	switch (key->sadb_key_exttype) {
	case SADB_EXT_KEY_AUTH:
		(void) printf(gettext("Authentication"));
		break;
	case SADB_EXT_KEY_ENCRYPT:
		(void) printf(gettext("Encryption"));
		break;
	}

	(void) printf(gettext(" key.\n%s"), prefix);
	(void) dump_key((uint8_t *)(key + 1), key->sadb_key_bits, stdout);
	(void) putchar('\n');
}

/*
 * Print an SADB_EXT_IDENTITY_* extension.
 */
void
print_ident(char *prefix, struct sadb_ident *id)
{
	boolean_t canprint = B_TRUE;

	(void) printf("%s", prefix);
	switch (id->sadb_ident_exttype) {
	case SADB_EXT_IDENTITY_SRC:
		(void) printf(gettext("Source"));
		break;
	case SADB_EXT_IDENTITY_DST:
		(void) printf(gettext("Destination"));
		break;
	}

	(void) printf(gettext(" identity, uid=%d, type "), id->sadb_ident_id);
	canprint = dump_sadb_idtype(id->sadb_ident_type, stdout, NULL);
	(void) printf("\n%s", prefix);
	if (canprint)
		(void) printf("%s\n", (char *)(id + 1));
	else
		(void) printf(gettext("<cannot print>\n"));
}

/*
 * Print an SADB_SENSITIVITY extension.
 */
void
print_sens(char *prefix, struct sadb_sens *sens)
{
	uint64_t *bitmap = (uint64_t *)(sens + 1);
	int i;

	(void) printf(
	    gettext("%sSensitivity DPD %d, sens level=%d, integ level=%d\n"),
	    prefix, sens->sadb_sens_dpd, sens->sadb_sens_sens_level,
	    sens->sadb_sens_integ_level);
	for (i = 0; sens->sadb_sens_sens_len-- > 0; i++, bitmap++)
		(void) printf(
		    gettext("%s Sensitivity BM extended word %d 0x%llx\n"),
		    i, *bitmap);
	for (i = 0; sens->sadb_sens_integ_len-- > 0; i++, bitmap++)
		(void) printf(
		    gettext("%s Integrity BM extended word %d 0x%llx\n"),
		    i, *bitmap);
}

/*
 * Print an SADB_EXT_PROPOSAL extension.
 */
void
print_prop(char *prefix, struct sadb_prop *prop)
{
	struct sadb_comb *combs;
	int i, numcombs;

	(void) printf(gettext("%sProposal, replay counter = %u.\n"), prefix,
	    prop->sadb_prop_replay);

	numcombs = prop->sadb_prop_len - SADB_8TO64(sizeof (*prop));
	numcombs /= SADB_8TO64(sizeof (*combs));

	combs = (struct sadb_comb *)(prop + 1);

	for (i = 0; i < numcombs; i++) {
		(void) printf(gettext("%s Combination #%u "), prefix, i + 1);
		if (combs[i].sadb_comb_auth != SADB_AALG_NONE) {
			(void) printf(gettext("Authentication = "));
			(void) dump_aalg(combs[i].sadb_comb_auth, stdout);
			(void) printf(gettext("  minbits=%u, maxbits=%u.\n%s "),
			    combs[i].sadb_comb_auth_minbits,
			    combs[i].sadb_comb_auth_maxbits, prefix);
		}

		if (combs[i].sadb_comb_encrypt != SADB_EALG_NONE) {
			(void) printf(gettext("Encryption = "));
			(void) dump_ealg(combs[i].sadb_comb_encrypt, stdout);
			(void) printf(gettext("  minbits=%u, maxbits=%u.\n%s "),
			    combs[i].sadb_comb_encrypt_minbits,
			    combs[i].sadb_comb_encrypt_maxbits, prefix);
		}

		(void) printf(gettext("HARD: "));
		if (combs[i].sadb_comb_hard_allocations)
			(void) printf(gettext("alloc=%u "),
			    combs[i].sadb_comb_hard_allocations);
		if (combs[i].sadb_comb_hard_bytes)
			(void) printf(gettext("bytes=%llu "),
			    combs[i].sadb_comb_hard_bytes);
		if (combs[i].sadb_comb_hard_addtime)
			(void) printf(gettext("post-add secs=%llu "),
			    combs[i].sadb_comb_hard_addtime);
		if (combs[i].sadb_comb_hard_usetime)
			(void) printf(gettext("post-use secs=%llu"),
			    combs[i].sadb_comb_hard_usetime);

		(void) printf(gettext("\n%s SOFT: "), prefix);
		if (combs[i].sadb_comb_soft_allocations)
			(void) printf(gettext("alloc=%u "),
			    combs[i].sadb_comb_soft_allocations);
		if (combs[i].sadb_comb_soft_bytes)
			(void) printf(gettext("bytes=%llu "),
			    combs[i].sadb_comb_soft_bytes);
		if (combs[i].sadb_comb_soft_addtime)
			(void) printf(gettext("post-add secs=%llu "),
			    combs[i].sadb_comb_soft_addtime);
		if (combs[i].sadb_comb_soft_usetime)
			(void) printf(gettext("post-use secs=%llu"),
			    combs[i].sadb_comb_soft_usetime);
		(void) putchar('\n');
	}
}

/*
 * Print an extended proposal (SADB_X_EXT_EPROP).
 */
void
print_eprop(char *prefix, struct sadb_prop *eprop)
{
	uint64_t *sofar;
	struct sadb_x_ecomb *ecomb;
	struct sadb_x_algdesc *algdesc;
	int i, j;

	(void) printf(gettext("%sExtended Proposal, replay counter = %u, "),
	    prefix, eprop->sadb_prop_replay);
	(void) printf(gettext("number of combinations = %u.\n"),
	    eprop->sadb_x_prop_numecombs);

	sofar = (uint64_t *)(eprop + 1);
	ecomb = (struct sadb_x_ecomb *)sofar;

	for (i = 0; i < eprop->sadb_x_prop_numecombs; ) {
		(void) printf(gettext("%s Extended combination #%u:\n"),
		    prefix, ++i);

		(void) printf(gettext("%s HARD: "), prefix);
		(void) printf(gettext("alloc=%u, "),
		    ecomb->sadb_x_ecomb_hard_allocations);
		(void) printf(gettext("bytes=%llu, "),
		    ecomb->sadb_x_ecomb_hard_bytes);
		(void) printf(gettext("post-add secs=%llu, "),
		    ecomb->sadb_x_ecomb_hard_addtime);
		(void) printf(gettext("post-use secs=%llu\n"),
		    ecomb->sadb_x_ecomb_hard_usetime);

		(void) printf(gettext("%s SOFT: "), prefix);
		(void) printf(gettext("alloc=%u, "),
		    ecomb->sadb_x_ecomb_soft_allocations);
		(void) printf(gettext("bytes=%llu, "),
		    ecomb->sadb_x_ecomb_soft_bytes);
		(void) printf(gettext("post-add secs=%llu, "),
		    ecomb->sadb_x_ecomb_soft_addtime);
		(void) printf(gettext("post-use secs=%llu\n"),
		    ecomb->sadb_x_ecomb_soft_usetime);

		sofar = (uint64_t *)(ecomb + 1);
		algdesc = (struct sadb_x_algdesc *)sofar;

		for (j = 0; j < ecomb->sadb_x_ecomb_numalgs; ) {
			(void) printf(gettext("%s Alg #%u "), prefix, ++j);
			switch (algdesc->sadb_x_algdesc_satype) {
			case SADB_SATYPE_ESP:
				(void) printf(gettext("for ESP "));
				break;
			case SADB_SATYPE_AH:
				(void) printf(gettext("for AH "));
				break;
			default:
				(void) printf(gettext("for satype=%d "),
				    algdesc->sadb_x_algdesc_satype);
			}
			switch (algdesc->sadb_x_algdesc_algtype) {
			case SADB_X_ALGTYPE_CRYPT:
				(void) printf(gettext("Encryption = "));
				(void) dump_ealg(algdesc->sadb_x_algdesc_alg,
				    stdout);
				break;
			case SADB_X_ALGTYPE_AUTH:
				(void) printf(gettext("Authentication = "));
				(void) dump_aalg(algdesc->sadb_x_algdesc_alg,
				    stdout);
				break;
			default:
				(void) printf(gettext("algtype(%d) = alg(%d)"),
				    algdesc->sadb_x_algdesc_algtype,
				    algdesc->sadb_x_algdesc_alg);
				break;
			}

			(void) printf(gettext("  minbits=%u, maxbits=%u.\n"),
			    algdesc->sadb_x_algdesc_minbits,
			    algdesc->sadb_x_algdesc_maxbits);

			sofar = (uint64_t *)(++algdesc);
		}
		ecomb = (struct sadb_x_ecomb *)sofar;
	}
}

/*
 * Print an SADB_EXT_SUPPORTED extension.
 */
void
print_supp(char *prefix, struct sadb_supported *supp)
{
	struct sadb_alg *algs;
	int i, numalgs;

	(void) printf(gettext("%sSupported "), prefix);
	switch (supp->sadb_supported_exttype) {
	case SADB_EXT_SUPPORTED_AUTH:
		(void) printf(gettext("authentication"));
		break;
	case SADB_EXT_SUPPORTED_ENCRYPT:
		(void) printf(gettext("encryption"));
		break;
	}
	(void) printf(gettext(" algorithms.\n"));

	algs = (struct sadb_alg *)(supp + 1);
	numalgs = supp->sadb_supported_len - SADB_8TO64(sizeof (*supp));
	numalgs /= SADB_8TO64(sizeof (*algs));
	for (i = 0; i < numalgs; i++) {
		(void) printf("%s", prefix);
		switch (supp->sadb_supported_exttype) {
		case SADB_EXT_SUPPORTED_AUTH:
			(void) dump_aalg(algs[i].sadb_alg_id, stdout);
			break;
		case SADB_EXT_SUPPORTED_ENCRYPT:
			(void) dump_ealg(algs[i].sadb_alg_id, stdout);
			break;
		}
		(void) printf(gettext(" minbits=%u, maxbits=%u, ivlen=%u.\n"),
		    algs[i].sadb_alg_minbits, algs[i].sadb_alg_maxbits,
		    algs[i].sadb_alg_ivlen);
	}
}

/*
 * Print an SADB_EXT_SPIRANGE extension.
 */
void
print_spirange(char *prefix, struct sadb_spirange *range)
{
	(void) printf(gettext("%sSPI Range, min=0x%x, max=0x%x\n"), prefix,
	    htonl(range->sadb_spirange_min),
	    htonl(range->sadb_spirange_max));
}

/*
 * Print an SADB_X_EXT_KM_COOKIE extension.
 */

void
print_kmc(char *prefix, struct sadb_x_kmc *kmc)
{
	char *cookie_label;

	if ((cookie_label = kmc_lookup_by_cookie(kmc->sadb_x_kmc_cookie)) ==
	    NULL)
		cookie_label = gettext("<Label not found.>");

	(void) printf(gettext("%sProtocol %u, cookie=\"%s\" (%u)\n"), prefix,
	    kmc->sadb_x_kmc_proto, cookie_label, kmc->sadb_x_kmc_cookie);
}

/*
 * Take a PF_KEY message pointed to buffer and print it.  Useful for DUMP
 * and GET.
 */
void
print_samsg(uint64_t *buffer, boolean_t want_timestamp, boolean_t vflag)
{
	uint64_t *current;
	struct sadb_msg *samsg = (struct sadb_msg *)buffer;
	struct sadb_ext *ext;
	struct sadb_lifetime *currentlt = NULL, *hardlt = NULL, *softlt = NULL;
	int i;
	time_t wallclock;

	(void) time(&wallclock);

	print_sadb_msg(samsg, want_timestamp ? wallclock : 0, vflag);
	current = (uint64_t *)(samsg + 1);
	while (current - buffer < samsg->sadb_msg_len) {
		int lenbytes;

		ext = (struct sadb_ext *)current;
		lenbytes = SADB_64TO8(ext->sadb_ext_len);
		switch (ext->sadb_ext_type) {
		case SADB_EXT_SA:
			print_sa(gettext("SA: "), (struct sadb_sa *)current);
			break;
		/*
		 * Pluck out lifetimes and print them at the end.  This is
		 * to show relative lifetimes.
		 */
		case SADB_EXT_LIFETIME_CURRENT:
			currentlt = (struct sadb_lifetime *)current;
			break;
		case SADB_EXT_LIFETIME_HARD:
			hardlt = (struct sadb_lifetime *)current;
			break;
		case SADB_EXT_LIFETIME_SOFT:
			softlt = (struct sadb_lifetime *)current;
			break;

		case SADB_EXT_ADDRESS_SRC:
			print_address(gettext("SRC: "),
			    (struct sadb_address *)current);
			break;
		case SADB_X_EXT_ADDRESS_INNER_SRC:
			print_address(gettext("INS: "),
			    (struct sadb_address *)current);
			break;
		case SADB_EXT_ADDRESS_DST:
			print_address(gettext("DST: "),
			    (struct sadb_address *)current);
			break;
		case SADB_X_EXT_ADDRESS_INNER_DST:
			print_address(gettext("IND: "),
			    (struct sadb_address *)current);
			break;
		case SADB_EXT_KEY_AUTH:
			print_key(gettext("AKY: "), (struct sadb_key *)current);
			break;
		case SADB_EXT_KEY_ENCRYPT:
			print_key(gettext("EKY: "), (struct sadb_key *)current);
			break;
		case SADB_EXT_IDENTITY_SRC:
			print_ident(gettext("SID: "),
			    (struct sadb_ident *)current);
			break;
		case SADB_EXT_IDENTITY_DST:
			print_ident(gettext("DID: "),
			    (struct sadb_ident *)current);
			break;
		case SADB_EXT_SENSITIVITY:
			print_sens(gettext("SNS: "),
			    (struct sadb_sens *)current);
			break;
		case SADB_EXT_PROPOSAL:
			print_prop(gettext("PRP: "),
			    (struct sadb_prop *)current);
			break;
		case SADB_EXT_SUPPORTED_AUTH:
			print_supp(gettext("SUA: "),
			    (struct sadb_supported *)current);
			break;
		case SADB_EXT_SUPPORTED_ENCRYPT:
			print_supp(gettext("SUE: "),
			    (struct sadb_supported *)current);
			break;
		case SADB_EXT_SPIRANGE:
			print_spirange(gettext("SPR: "),
			    (struct sadb_spirange *)current);
			break;
		case SADB_X_EXT_EPROP:
			print_eprop(gettext("EPR: "),
			    (struct sadb_prop *)current);
			break;
		case SADB_X_EXT_KM_COOKIE:
			print_kmc(gettext("KMC: "),
			    (struct sadb_x_kmc *)current);
			break;
		case SADB_X_EXT_ADDRESS_NATT_REM:
			print_address(gettext("NRM: "),
			    (struct sadb_address *)current);
			break;
		case SADB_X_EXT_ADDRESS_NATT_LOC:
			print_address(gettext("NLC: "),
			    (struct sadb_address *)current);
			break;
		default:
			(void) printf(gettext(
			    "UNK: Unknown ext. %d, len %d.\n"),
			    ext->sadb_ext_type, lenbytes);
			for (i = 0; i < ext->sadb_ext_len; i++)
				(void) printf(gettext("UNK: 0x%llx\n"),
				    ((uint64_t *)ext)[i]);
			break;
		}
		current += (lenbytes == 0) ?
		    SADB_8TO64(sizeof (struct sadb_ext)) : ext->sadb_ext_len;
	}
	/*
	 * Print lifetimes NOW.
	 */
	if (currentlt != NULL || hardlt != NULL || softlt != NULL)
		print_lifetimes(wallclock, currentlt, hardlt, softlt, vflag);

	if (current - buffer != samsg->sadb_msg_len) {
		warnx(gettext("WARNING: insufficient buffer "
			"space or corrupt message."));
	}

	(void) fflush(stdout);	/* Make sure our message is out there. */
}

/*
 * save_XXX functions are used when "saving" the SA tables to either a
 * file or standard output.  They use the dump_XXX functions where needed,
 * but mostly they use the rparseXXX functions.
 */

/*
 * Print save information for a lifetime extension.
 *
 * NOTE : It saves the lifetime in absolute terms.  For example, if you
 * had a hard_usetime of 60 seconds, you'll save it as 60 seconds, even though
 * there may have been 59 seconds burned off the clock.
 */
boolean_t
save_lifetime(struct sadb_lifetime *lifetime, FILE *ofile)
{
	char *prefix;

	prefix = (lifetime->sadb_lifetime_exttype == SADB_EXT_LIFETIME_SOFT) ?
	    "soft" : "hard";

	if (putc('\t', ofile) == EOF)
		return (B_FALSE);

	if (lifetime->sadb_lifetime_allocations != 0 && fprintf(ofile,
	    "%s_alloc %u ", prefix, lifetime->sadb_lifetime_allocations) < 0)
		return (B_FALSE);

	if (lifetime->sadb_lifetime_bytes != 0 && fprintf(ofile,
	    "%s_bytes %llu ", prefix, lifetime->sadb_lifetime_bytes) < 0)
		return (B_FALSE);

	if (lifetime->sadb_lifetime_addtime != 0 && fprintf(ofile,
	    "%s_addtime %llu ", prefix, lifetime->sadb_lifetime_addtime) < 0)
		return (B_FALSE);

	if (lifetime->sadb_lifetime_usetime != 0 && fprintf(ofile,
	    "%s_usetime %llu ", prefix, lifetime->sadb_lifetime_usetime) < 0)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Print save information for an address extension.
 */
boolean_t
save_address(struct sadb_address *addr, FILE *ofile)
{
	char *printable_addr, buf[INET6_ADDRSTRLEN];
	const char *prefix, *pprefix;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)(addr + 1);
	struct sockaddr_in *sin = (struct sockaddr_in *)sin6;
	int af = sin->sin_family;

	/*
	 * Address-family reality check.
	 */
	if (af != AF_INET6 && af != AF_INET)
		return (B_FALSE);

	switch (addr->sadb_address_exttype) {
	case SADB_EXT_ADDRESS_SRC:
		prefix = "src";
		pprefix = "sport";
		break;
	case SADB_X_EXT_ADDRESS_INNER_SRC:
		prefix = "isrc";
		pprefix = "isport";
		break;
	case SADB_EXT_ADDRESS_DST:
		prefix = "dst";
		pprefix = "dport";
		break;
	case SADB_X_EXT_ADDRESS_INNER_DST:
		prefix = "idst";
		pprefix = "idport";
		break;
	case SADB_X_EXT_ADDRESS_NATT_LOC:
		prefix = "nat_loc ";
		pprefix = "nat_lport";
		break;
	case SADB_X_EXT_ADDRESS_NATT_REM:
		prefix = "nat_rem ";
		pprefix = "nat_rport";
		break;
	}

	if (fprintf(ofile, "    %s ", prefix) < 0)
		return (B_FALSE);

	/*
	 * Do not do address-to-name translation, given that we live in
	 * an age of names that explode into many addresses.
	 */
	printable_addr = (char *)inet_ntop(af,
	    (af == AF_INET) ? (char *)&sin->sin_addr : (char *)&sin6->sin6_addr,
	    buf, sizeof (buf));
	if (printable_addr == NULL)
		printable_addr = "<inet_ntop() failed>";
	if (fprintf(ofile, "%s", printable_addr) < 0)
		return (B_FALSE);
	if (addr->sadb_address_prefixlen != 0 &&
	    !((addr->sadb_address_prefixlen == 32 && af == AF_INET) ||
		(addr->sadb_address_prefixlen == 128 && af == AF_INET6))) {
		if (fprintf(ofile, "/%d", addr->sadb_address_prefixlen) < 0)
			return (B_FALSE);
	}

	/*
	 * The port is in the same position for struct sockaddr_in and
	 * struct sockaddr_in6.  We exploit that property here.
	 */
	if ((pprefix != NULL) && (sin->sin_port != 0))
		(void) fprintf(ofile, " %s %d", pprefix, ntohs(sin->sin_port));

	return (B_TRUE);
}

/*
 * Print save information for a key extension. Returns whether writing
 * to the specified output file was successful or not.
 */
boolean_t
save_key(struct sadb_key *key, FILE *ofile)
{
	char *prefix;

	if (putc('\t', ofile) == EOF)
		return (B_FALSE);

	prefix = (key->sadb_key_exttype == SADB_EXT_KEY_AUTH) ? "auth" : "encr";

	if (fprintf(ofile, "%skey ", prefix) < 0)
		return (B_FALSE);

	if (dump_key((uint8_t *)(key + 1), key->sadb_key_bits, ofile) == -1)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Print save information for an identity extension.
 */
boolean_t
save_ident(struct sadb_ident *ident, FILE *ofile)
{
	char *prefix;

	if (putc('\t', ofile) == EOF)
		return (B_FALSE);

	prefix = (ident->sadb_ident_exttype == SADB_EXT_IDENTITY_SRC) ? "src" :
	    "dst";

	if (fprintf(ofile, "%sidtype %s ", prefix,
	    rparseidtype(ident->sadb_ident_type)) < 0)
		return (B_FALSE);

	if (ident->sadb_ident_type == SADB_X_IDENTTYPE_DN ||
	    ident->sadb_ident_type == SADB_X_IDENTTYPE_GN) {
		if (fprintf(ofile, gettext("<can-not-print>")) < 0)
			return (B_FALSE);
	} else {
		if (fprintf(ofile, "%s", (char *)(ident + 1)) < 0)
			return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * "Save" a security association to an output file.
 *
 * NOTE the lack of calls to gettext() because I'm outputting parseable stuff.
 * ALSO NOTE that if you change keywords (see parsecmd()), you'll have to
 * change them here as well.
 */
void
save_assoc(uint64_t *buffer, FILE *ofile)
{
	int seen_proto = 0;
	uint64_t *current;
	struct sadb_address *addr;
	struct sadb_msg *samsg = (struct sadb_msg *)buffer;
	struct sadb_ext *ext;
#define	bail2(s)	do { \
				int t = errno; \
				(void) fclose(ofile); \
				errno = t; \
				interactive = B_FALSE;	/* Guarantees exit. */ \
				Bail(s); \
			} while (B_FALSE)	/* How do I lint-clean this? */

#define	savenl() if (fputs(" \\\n", ofile) == EOF) { bail2("savenl"); }

	if (fputs("# begin assoc\n", ofile) == EOF)
		Bail("save_assoc: Opening comment of SA");
	if (fprintf(ofile, "add %s ", rparsesatype(samsg->sadb_msg_satype)) < 0)
		Bail("save_assoc: First line of SA");
	/* LINTED E_CONST_COND */
	savenl();

	current = (uint64_t *)(samsg + 1);
	while (current - buffer < samsg->sadb_msg_len) {
		struct sadb_sa *assoc;

		ext = (struct sadb_ext *)current;
		switch (ext->sadb_ext_type) {
		case SADB_EXT_SA:
			assoc = (struct sadb_sa *)ext;
			if (assoc->sadb_sa_state != SADB_SASTATE_MATURE) {
				if (fprintf(ofile, "# WARNING: SA was dying "
				    "or dead.\n") < 0) {
					/* LINTED E_CONST_COND */
					bail2("save_assoc: fprintf not mature");
				}
			}
			if (fprintf(ofile, "    spi 0x%x ",
			    ntohl(assoc->sadb_sa_spi)) < 0)
				/* LINTED E_CONST_COND */
				bail2("save_assoc: fprintf spi");
			if (assoc->sadb_sa_encrypt != SADB_EALG_NONE) {
				if (fprintf(ofile, "encr_alg %s ",
				    rparsealg(assoc->sadb_sa_encrypt,
					IPSEC_PROTO_ESP)) < 0)
					/* LINTED E_CONST_COND */
					bail2("save_assoc: fprintf encrypt");
			}
			if (assoc->sadb_sa_auth != SADB_AALG_NONE) {
				if (fprintf(ofile, "auth_alg %s ",
				    rparsealg(assoc->sadb_sa_auth,
					IPSEC_PROTO_AH)) < 0)
					/* LINTED E_CONST_COND */
					bail2("save_assoc: fprintf auth");
			}
			if (fprintf(ofile, "replay %d ",
			    assoc->sadb_sa_replay) < 0)
				/* LINTED E_CONST_COND */
				bail2("save_assoc: fprintf replay");
			if (assoc->sadb_sa_flags & (SADB_X_SAFLAGS_NATT_LOC |
			    SADB_X_SAFLAGS_NATT_REM)) {
				if (fprintf(ofile, "encap udp") < 0)
					/* LINTED E_CONST_COND */
					bail2("save_assoc: fprintf encap");
			}
			/* LINTED E_CONST_COND */
			savenl();
			break;
		case SADB_EXT_LIFETIME_HARD:
		case SADB_EXT_LIFETIME_SOFT:
			if (!save_lifetime((struct sadb_lifetime *)ext, ofile))
				/* LINTED E_CONST_COND */
				bail2("save_lifetime");
			/* LINTED E_CONST_COND */
			savenl();
			break;
		case SADB_EXT_ADDRESS_SRC:
		case SADB_EXT_ADDRESS_DST:
		case SADB_X_EXT_ADDRESS_INNER_SRC:
		case SADB_X_EXT_ADDRESS_INNER_DST:
		case SADB_X_EXT_ADDRESS_NATT_REM:
		case SADB_X_EXT_ADDRESS_NATT_LOC:
			addr = (struct sadb_address *)ext;
			if (!seen_proto && addr->sadb_address_proto) {
				(void) fprintf(ofile, "    proto %d",
				    addr->sadb_address_proto);
				/* LINTED E_CONST_COND */
				savenl();
				seen_proto = 1;
			}
			if (!save_address(addr, ofile))
				/* LINTED E_CONST_COND */
				bail2("save_address");
			/* LINTED E_CONST_COND */
			savenl();
			break;
		case SADB_EXT_KEY_AUTH:
		case SADB_EXT_KEY_ENCRYPT:
			if (!save_key((struct sadb_key *)ext, ofile))
				/* LINTED E_CONST_COND */
				bail2("save_address");
			/* LINTED E_CONST_COND */
			savenl();
			break;
		case SADB_EXT_IDENTITY_SRC:
		case SADB_EXT_IDENTITY_DST:
			if (!save_ident((struct sadb_ident *)ext, ofile))
				/* LINTED E_CONST_COND */
				bail2("save_address");
			/* LINTED E_CONST_COND */
			savenl();
			break;
		case SADB_EXT_SENSITIVITY:
		default:
			/* Skip over irrelevant extensions. */
			break;
		}
		current += ext->sadb_ext_len;
	}

	if (fputs(gettext("\n# end assoc\n\n"), ofile) == EOF)
		/* LINTED E_CONST_COND */
		bail2("save_assoc: last fputs");
}

/*
 * Open the output file for the "save" command.
 */
FILE *
opensavefile(char *filename)
{
	int fd;
	FILE *retval;
	struct stat buf;

	/*
	 * If the user specifies "-" or doesn't give a filename, then
	 * dump to stdout.  Make sure to document the dangers of files
	 * that are NFS, directing your output to strange places, etc.
	 */
	if (filename == NULL || strcmp("-", filename) == 0)
		return (stdout);

	/*
	 * open the file with the create bits set.  Since I check for
	 * real UID == root in main(), I won't worry about the ownership
	 * problem.
	 */
	fd = open(filename, O_WRONLY | O_EXCL | O_CREAT | O_TRUNC, S_IRUSR);
	if (fd == -1) {
		if (errno != EEXIST)
			bail_msg("%s %s: %s", filename, gettext("open error"),
			    strerror(errno));
		fd = open(filename, O_WRONLY | O_TRUNC, 0);
		if (fd == -1)
			bail_msg("%s %s: %s", filename, gettext("open error"),
			    strerror(errno));
		if (fstat(fd, &buf) == -1) {
			(void) close(fd);
			bail_msg("%s fstat: %s", filename, strerror(errno));
		}
		if (S_ISREG(buf.st_mode) &&
		    ((buf.st_mode & S_IAMB) != S_IRUSR)) {
			warnx(gettext("WARNING: Save file already exists with "
				"permission %o."), buf.st_mode & S_IAMB);
			warnx(gettext("Normal users may be able to read IPsec "
				"keying material."));
		}
	}

	/* Okay, we have an FD.  Assign it to a stdio FILE pointer. */
	retval = fdopen(fd, "w");
	if (retval == NULL) {
		(void) close(fd);
		bail_msg("%s %s: %s", filename, gettext("fdopen error"),
		    strerror(errno));
	}
	return (retval);
}

const char *
do_inet_ntop(const void *addr, char *cp, size_t size)
{
	boolean_t isv4;
	struct in6_addr *inaddr6 = (struct in6_addr *)addr;
	struct in_addr inaddr;

	if ((isv4 = IN6_IS_ADDR_V4MAPPED(inaddr6)) == B_TRUE) {
		IN6_V4MAPPED_TO_INADDR(inaddr6, &inaddr);
	}

	return (inet_ntop(isv4 ? AF_INET : AF_INET6,
	    isv4 ? (void *)&inaddr : inaddr6, cp, size));
}

char numprint[NBUF_SIZE];

/*
 * Parse and reverse parse a specific SA type (AH, ESP, etc.).
 */
static struct typetable {
	char *type;
	int token;
} type_table[] = {
	{"all", SADB_SATYPE_UNSPEC},
	{"ah",  SADB_SATYPE_AH},
	{"esp", SADB_SATYPE_ESP},
	/* PF_KEY NOTE:  More to come if net/pfkeyv2.h gets updated. */
	{NULL, 0}	/* Token value is irrelevant for this entry. */
};

char *
rparsesatype(int type)
{
	struct typetable *tt = type_table;

	while (tt->type != NULL && type != tt->token)
		tt++;

	if (tt->type == NULL) {
		(void) snprintf(numprint, NBUF_SIZE, "%d", type);
	} else {
		return (tt->type);
	}

	return (numprint);
}


/*
 * Return a string containing the name of the specified numerical algorithm
 * identifier.
 */
char *
rparsealg(uint8_t alg, int proto_num)
{
	static struct ipsecalgent *holder = NULL; /* we're single-threaded */

	if (holder != NULL)
		freeipsecalgent(holder);

	holder = getipsecalgbynum(alg, proto_num, NULL);
	if (holder == NULL) {
		(void) snprintf(numprint, NBUF_SIZE, "%d", alg);
		return (numprint);
	}

	return (*(holder->a_names));
}

/*
 * Parse and reverse parse out a source/destination ID type.
 */
static struct idtypes {
	char *idtype;
	uint8_t retval;
} idtypes[] = {
	{"prefix",	SADB_IDENTTYPE_PREFIX},
	{"fqdn",	SADB_IDENTTYPE_FQDN},
	{"domain",	SADB_IDENTTYPE_FQDN},
	{"domainname",	SADB_IDENTTYPE_FQDN},
	{"user_fqdn",	SADB_IDENTTYPE_USER_FQDN},
	{"mailbox",	SADB_IDENTTYPE_USER_FQDN},
	{"der_dn",	SADB_X_IDENTTYPE_DN},
	{"der_gn",	SADB_X_IDENTTYPE_GN},
	{NULL,		0}
};

char *
rparseidtype(uint16_t type)
{
	struct idtypes *idp;

	for (idp = idtypes; idp->idtype != NULL; idp++) {
		if (type == idp->retval)
			return (idp->idtype);
	}

	(void) snprintf(numprint, NBUF_SIZE, "%d", type);
	return (numprint);
}
