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
 *
 * Common code and structures used by name-service-switch "compat" backends.
 *
 * Most of the code in the "compat" backend is a perverted form of code from
 * the "files" backend;  this file is no exception.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <bsm/libbsm.h>
#include <user_attr.h>
#include "compat_common.h"
#include "../../../libnsl/include/nsl_stdio_prv.h"

/*
 * This should be in a header.
 */

extern int yp_get_default_domain(char **domain);

/*
 * Routines to manage list of "-" users for get{pw, sp, gr}ent().  Current
 *   implementation is completely moronic; we use a linked list.  But then
 *   that's what it's always done in 4.x...
 */

struct setofstrings {
	char			*name;
	struct setofstrings	*next;
	/*
	 * === Should get smart and malloc the string and pointer as one
	 *	object rather than two.
	 */
};
typedef struct setofstrings	*strset_t;

static void
strset_free(ssp)
	strset_t	*ssp;
{
	strset_t	cur, nxt;

	for (cur = *ssp;  cur != 0;  cur = nxt) {
		nxt = cur->next;
		free(cur->name);
		free(cur);
	}
	*ssp = 0;
}

static boolean_t
strset_add(ssp, nam)
	strset_t	*ssp;
	const char	*nam;
{
	strset_t	new;

	if (0 == (new = (strset_t)malloc(sizeof (*new)))) {
		return (B_FALSE);
	}
	if (0 == (new->name = malloc(strlen(nam) + 1))) {
		free(new);
		return (B_FALSE);
	}
	strcpy(new->name, nam);
	new->next = *ssp;
	*ssp = new;
	return (B_TRUE);
}

static boolean_t
strset_in(ssp, nam)
	const strset_t	*ssp;
	const char	*nam;
{
	strset_t	cur;

	for (cur = *ssp;  cur != 0;  cur = cur->next) {
		if (strcmp(cur->name, nam) == 0) {
			return (B_TRUE);
		}
	}
	return (B_FALSE);
}


struct compat_backend {
	compat_backend_op_t	*ops;
	int			n_ops;
	const char		*filename;
	__NSL_FILE		*f;
	int			minbuf;
	char			*buf;
	int			linelen;	/* <== Explain use, lifetime */

	nss_db_initf_t		db_initf;
	nss_db_root_t		*db_rootp;	/* Shared between instances */
	nss_getent_t		db_context;	/* Per-instance enumeration */

	compat_get_name		getnamef;
	compat_merge_func	mergef;

	/* We wouldn't need all this hokey state stuff if we */
	/*   used another thread to implement a coroutine... */
	enum {
		GETENT_FILE,
		GETENT_NETGROUP,
		GETENT_ATTRDB,
		GETENT_ALL,
		GETENT_DONE
	}			state;
	strset_t		minuses;

	int			permit_netgroups;
	const char		*yp_domain;
	nss_backend_t		*getnetgrent_backend;
	char			*netgr_buffer;
};


/*
 * Lookup and enumeration routines for +@group and -@group.
 *
 * This code knows a lot more about lib/libc/port/gen/getnetgrent.c than
 *   is really healthy.  The set/get/end routines below duplicate code
 *   from that file, but keep the state information per-backend-instance
 *   instead of just per-process.
 */

extern void _nss_initf_netgroup(nss_db_params_t *);
/*
 * Should really share the db_root in getnetgrent.c in order to get the
 *   resource-management quotas right, but this will have to do.
 */
static DEFINE_NSS_DB_ROOT(netgr_db_root);

static boolean_t
netgr_in(compat_backend_ptr_t be, const char *group, const char *user)
{
	if (be->yp_domain == 0) {
		if (yp_get_default_domain((char **)&be->yp_domain) != 0) {
			return (B_FALSE);
		}
	}
	return (innetgr(group, 0, user, be->yp_domain));
}

static boolean_t
netgr_all_in(compat_backend_ptr_t be, const char *group)
{
	/*
	 * 4.x does this;  ours not to reason why...
	 */
	return (netgr_in(be, group, "*"));
}

static void
netgr_set(be, netgroup)
	compat_backend_ptr_t	be;
	const char		*netgroup;
{
	/*
	 * ===> Need comment to explain that this first "if" is optimizing
	 *	for the same-netgroup-as-last-time case
	 */
	if (be->getnetgrent_backend != 0 &&
	    NSS_INVOKE_DBOP(be->getnetgrent_backend,
			    NSS_DBOP_SETENT,
			    (void *) netgroup) != NSS_SUCCESS) {
		NSS_INVOKE_DBOP(be->getnetgrent_backend, NSS_DBOP_DESTRUCTOR,
				0);
		be->getnetgrent_backend = 0;
	}
	if (be->getnetgrent_backend == 0) {
		struct nss_setnetgrent_args	args;

		args.netgroup	= netgroup;
		args.iterator	= 0;
		nss_search(&netgr_db_root, _nss_initf_netgroup,
			NSS_DBOP_NETGROUP_SET, &args);
		be->getnetgrent_backend = args.iterator;
	}
}

static boolean_t
netgr_next_u(be, up)
	compat_backend_ptr_t	be;
	char			**up;
{
	if (be->netgr_buffer == 0 &&
	    (be->netgr_buffer = malloc(NSS_BUFLEN_NETGROUP)) == 0) {
		/* Out of memory */
		return (B_FALSE);
	}

	do {
		struct nss_getnetgrent_args	args;

		args.buffer	= be->netgr_buffer;
		args.buflen	= NSS_BUFLEN_NETGROUP;
		args.status	= NSS_NETGR_NO;

		if (be->getnetgrent_backend != 0) {
			NSS_INVOKE_DBOP(be->getnetgrent_backend,
					NSS_DBOP_GETENT, &args);
		}

		if (args.status == NSS_NETGR_FOUND) {
			*up	  = args.retp[NSS_NETGR_USER];
		} else {
			return (B_FALSE);
		}
	} while (*up == 0);
	return (B_TRUE);
}

static void
netgr_end(be)
	compat_backend_ptr_t	be;
{
	if (be->getnetgrent_backend != 0) {
		NSS_INVOKE_DBOP(be->getnetgrent_backend,
				NSS_DBOP_DESTRUCTOR, 0);
		be->getnetgrent_backend = 0;
	}
	if (be->netgr_buffer != 0) {
		free(be->netgr_buffer);
		be->netgr_buffer = 0;
	}
}


#define	MAXFIELDS 9	/* Sufficient for passwd (7), shadow (9), group (4) */

static nss_status_t
do_merge(be, args, instr, linelen)
	compat_backend_ptr_t	be;
	nss_XbyY_args_t		*args;
	const char		*instr;
	int			linelen;
{
	char			*fields[MAXFIELDS];
	int			i;
	int			overrides;
	const char		*p;
	const char		*end = instr + linelen;
	nss_status_t		res;

	/*
	 * Potential optimization:  only perform the field-splitting nonsense
	 *   once per input line (at present, "+" and "+@netgroup" entries
	 *   will cause us to do this multiple times in getent() requests).
	 */

	for (i = 0;  i < MAXFIELDS;  i++) {
		fields[i] = 0;
	}
	for (p = instr, overrides = 0, i = 0; /* no test */; i++) {
		const char	*q = memchr(p, ':', end - p);
		const char	*r = (q == 0) ? end : q;
		ssize_t		len = r - p;

		if (len > 0) {
			char	*s = malloc(len + 1);
			if (s == 0) {
				overrides = -1;	/* Indicates "you lose" */
				break;
			}
			memcpy(s, p, len);
			s[len] = '\0';
			fields[i] = s;
			overrides++;
		}
		if (q == 0) {
			/* End of line */
			break;
		} else {
			/* Skip the colon at (*q) */
			p = q + 1;
		}
	}
	if (overrides == 1) {
		/* No real overrides, return (*args) intact */
		res = NSS_SUCCESS;
	} else if (overrides > 1) {
		/*
		 * The zero'th field is always nonempty (+/-...), but at least
		 *   one other field was also nonempty, i.e. wants to override
		 */
		switch ((*be->mergef)(be, args, (const char **)fields)) {
		    case NSS_STR_PARSE_SUCCESS:
			args->returnval	= args->buf.result;
			args->erange	= 0;
			res = NSS_SUCCESS;
			break;
		    case NSS_STR_PARSE_ERANGE:
			args->returnval	= 0;
			args->erange	= 1;
			res = NSS_NOTFOUND;
			break;
		    case NSS_STR_PARSE_PARSE:
			args->returnval	= 0;
			args->erange	= 0;
/* ===> Very likely the wrong thing to do... */
			res = NSS_NOTFOUND;
			break;
		}
	} else {
		args->returnval	= 0;
		args->erange	= 0;
		res = NSS_UNAVAIL;	/* ==> Right? */
	}

	for (i = 0;  i < MAXFIELDS;  i++) {
		if (fields[i] != 0) {
			free(fields[i]);
		}
	}

	return (res);
}

/*ARGSUSED*/
nss_status_t
_nss_compat_setent(be, dummy)
	compat_backend_ptr_t	be;
	void			*dummy;
{
	if (be->f == 0) {
		if (be->filename == 0) {
			/* Backend isn't initialized properly? */
			return (NSS_UNAVAIL);
		}
		if ((be->f = __nsl_fopen(be->filename, "r")) == 0) {
			return (NSS_UNAVAIL);
		}
	} else {
		__nsl_rewind(be->f);
	}
	strset_free(&be->minuses);
	/* ===> ??? nss_endent(be->db_rootp, be->db_initf, &be->db_context); */

	if ((strcmp(be->filename, USERATTR_FILENAME) == 0) ||
	    (strcmp(be->filename, AUDITUSER_FILENAME) == 0))
		be->state = GETENT_ATTRDB;
	else
		be->state = GETENT_FILE;

	/* ===> ??  netgroup stuff? */
	return (NSS_SUCCESS);
}

/*ARGSUSED*/
nss_status_t
_nss_compat_endent(be, dummy)
	compat_backend_ptr_t	be;
	void			*dummy;
{
	if (be->f != 0) {
		__nsl_fclose(be->f);
		be->f = 0;
	}
	if (be->buf != 0) {
		free(be->buf);
		be->buf = 0;
	}
	nss_endent(be->db_rootp, be->db_initf, &be->db_context);

	be->state = GETENT_FILE; /* Probably superfluous but comforting */
	strset_free(&be->minuses);
	netgr_end(be);

	/*
	 * Question: from the point of view of resource-freeing vs. time to
	 *   start up again, how much should we do in endent() and how much
	 *   in the destructor?
	 */
	return (NSS_SUCCESS);
}

/*ARGSUSED*/
nss_status_t
_nss_compat_destr(be, dummy)
	compat_backend_ptr_t	be;
	void			*dummy;
{
	if (be != 0) {
		if (be->f != 0) {
			_nss_compat_endent(be, 0);
		}
		nss_delete(be->db_rootp);
		nss_delete(&netgr_db_root);
		free(be);
	}
	return (NSS_SUCCESS);	/* In case anyone is dumb enough to check */
}

static int
read_line(f, buffer, buflen)
	__NSL_FILE		*f;
	char			*buffer;
	int			buflen;
{
	/*CONSTCOND*/
	while (1) {
		int	linelen;

		if (__nsl_fgets(buffer, buflen, f) == 0) {
			/* End of file */
			return (-1);
		}
		linelen = strlen(buffer);
		/* linelen >= 1 (since fgets didn't return 0) */

		if (buffer[linelen - 1] == '\n') {
			/*
			 * ===> The code below that calls read_line() doesn't
			 *	play by the rules;  it assumes in places that
			 *	the line is null-terminated.  For now we'll
			 *	humour it.
			 */
			buffer[--linelen] = '\0';
			return (linelen);
		}
		if (__nsl_feof(f)) {
			/* Line is last line in file, and has no newline */
			return (linelen);
		}
		/* Line too long for buffer;  toss it and loop for next line */
		/* ===== should syslog() in cases where previous code did */
		while (__nsl_fgets(buffer, buflen, f) != 0 &&
		    buffer[strlen(buffer) - 1] != '\n') {
			;
		}
	}
}

static int
is_nss_lookup_by_name(int attrdb, nss_dbop_t op)
{
	int result = 0;

	if ((attrdb != 0) &&
	    ((op == NSS_DBOP_AUDITUSER_BYNAME) ||
	    (op == NSS_DBOP_USERATTR_BYNAME))) {
		result = 1;
	} else if ((attrdb == 0) &&
	    ((op == NSS_DBOP_GROUP_BYNAME) ||
	    (op == NSS_DBOP_PASSWD_BYNAME) ||
	    (op == NSS_DBOP_SHADOW_BYNAME))) {
		result = 1;
	}

	return (result);
}

/*ARGSUSED*/
nss_status_t
_attrdb_compat_XY_all(be, argp, netdb, check, op_num)
    compat_backend_ptr_t be;
    nss_XbyY_args_t *argp;
    int netdb;
    compat_XY_check_func check;
    nss_dbop_t op_num;
{
	int		parsestat;
	int		(*func)();
	const char	*filter = argp->key.name;
	nss_status_t	res;

#ifdef	DEBUG
	(void) fprintf(stdout, "\n[compat_common.c: _attrdb_compat_XY_all]\n");
#endif	/* DEBUG */

	if (be->buf == 0 &&
	    (be->buf = malloc(be->minbuf)) == 0) {
		return (NSS_UNAVAIL);
	}
	if ((res = _nss_compat_setent(be, 0)) != NSS_SUCCESS) {
		return (res);
	}
	res = NSS_NOTFOUND;

	/*CONSTCOND*/
	while (1) {
		int	linelen;
		char	*instr	= be->buf;

		if ((linelen = read_line(be->f, instr, be->minbuf)) < 0) {
			/* End of file */
			argp->returnval = 0;
			argp->erange    = 0;
			break;
		}
		if (filter != 0 && strstr(instr, filter) == 0) {
			/*
			 * Optimization:  if the entry doesn't contain the
			 * filter string then it can't be the entry we want,
			 * so don't bother looking more closely at it.
			 */
			continue;
		}
		if (netdb) {
			char	*first;
			char	*last;

			if ((last = strchr(instr, '#')) == 0) {
				last = instr + linelen;
			}
			*last-- = '\0';		/* Nuke '\n' or #comment */

			/*
			 * Skip leading whitespace.  Normally there isn't
			 * any, so it's not worth calling strspn().
			 */
			for (first = instr;  isspace(*first);  first++) {
				;
			}
			if (*first == '\0') {
				continue;
			}
			/*
			 * Found something non-blank on the line.  Skip back
			 * over any trailing whitespace;  since we know
			 * there's non-whitespace earlier in the line,
			 * checking for termination is easy.
			 */
			while (isspace(*last)) {
				--last;
			}
			linelen = last - first + 1;
			if (first != instr) {
				instr = first;
			}
		}
		argp->returnval = 0;
		func = argp->str2ent;
		parsestat = (*func)(instr, linelen, argp->buf.result,
					argp->buf.buffer, argp->buf.buflen);
		if (parsestat == NSS_STR_PARSE_SUCCESS) {
			argp->returnval = argp->buf.result;
			if (check == 0 || (*check)(argp)) {
				res = NSS_SUCCESS;
				break;
			}
		} else if (parsestat == NSS_STR_PARSE_ERANGE) {
			argp->erange = 1;
			break;
		}
	}
	/*
	 * stayopen is set to 0 by default in order to close the opened
	 * file.  Some applications may break if it is set to 1.
	 */
	if (check != 0 && !argp->stayopen) {
		(void) _nss_compat_endent(be, 0);
	}

	if (res != NSS_SUCCESS) {
		if ((op_num == NSS_DBOP_USERATTR_BYNAME) ||
		    (op_num == NSS_DBOP_AUDITUSER_BYNAME)) {
			res = nss_search(be->db_rootp,
			    be->db_initf,
			    op_num,
			    argp);
		} else {
			res = nss_getent(be->db_rootp,
			    be->db_initf, &be->db_context, argp);
		}
		if (res != NSS_SUCCESS) {
			argp->returnval	= 0;
			argp->erange	= 0;
		}
	}

	return (res);
}

nss_status_t
_nss_compat_XY_all(be, args, check, op_num)
	compat_backend_ptr_t	be;
	nss_XbyY_args_t		*args;
	compat_XY_check_func	check;
	nss_dbop_t		op_num;
{
	nss_status_t		res;
	int			parsestat;

	if (be->buf == 0 &&
	    (be->buf = malloc(be->minbuf)) == 0) {
		return (NSS_UNAVAIL); /* really panic, malloc failed */
	}

	if ((res = _nss_compat_setent(be, 0)) != NSS_SUCCESS) {
		return (res);
	}

	res = NSS_NOTFOUND;

	/*CONSTCOND*/
	while (1) {
		int		linelen;
		char		*instr	= be->buf;
		char		*colon;

		linelen = read_line(be->f, instr, be->minbuf);
		if (linelen < 0) {
			/* End of file */
			args->returnval = 0;
			args->erange    = 0;
			break;
		}

		args->returnval = 0;	/* reset for both types of entries */

		if (instr[0] != '+' && instr[0] != '-') {
			/* Simple, wholesome, God-fearing entry */
			parsestat = (*args->str2ent)(instr, linelen,
						    args->buf.result,
						    args->buf.buffer,
						    args->buf.buflen);
			if (parsestat == NSS_STR_PARSE_SUCCESS) {
				args->returnval = args->buf.result;
				if ((*check)(args) != 0) {
					res = NSS_SUCCESS;
					break;
				}

/* ===> Check the Dani logic here... */

			} else if (parsestat == NSS_STR_PARSE_ERANGE) {
				args->erange = 1;
				res = NSS_NOTFOUND;
				break;
				/* should we just skip this one long line ? */
			} /* else if (parsestat == NSS_STR_PARSE_PARSE) */
				/* don't care ! */

/* ==> ?? */		continue;
		}

		/*
		 * Process "+", "+name", "+@netgroup", "-name" or "-@netgroup"
		 *
		 * This code is optimized for lookups by name.
		 *
		 * For lookups by identifier search key cannot be matched with
		 * the name of the "+" or "-" entry. So nss_search() is to be
		 * called before extracting the name i.e. via (*be->getnamef)().
		 *
		 * But for lookups by name, search key is compared with the name
		 * of the "+" or "-" entry to acquire a match and thus
		 * unnesessary calls to nss_search() is eliminated. Also for
		 * matching "-" entries, calls to nss_search() is eliminated.
		 */

		if ((colon = strchr(instr, ':')) != 0) {
			*colon = '\0';	/* terminate field to extract name */
		}

		if (instr[1] == '@') {
			/*
			 * Case 1:
			 * The entry is of the form "+@netgroup" or
			 * "-@netgroup".  If we're performing a lookup by name,
			 * we can simply extract the name from the search key
			 * (i.e. args->key.name).  If not, then we must call
			 * nss_search() before extracting the name via the
			 * get_XXname() function. i.e. (*be->getnamef)(args).
			 */
			if (is_nss_lookup_by_name(0, op_num) != 0) {
				/* compare then search */
				if (!be->permit_netgroups ||
				    !netgr_in(be, instr + 2, args->key.name))
					continue;
				if (instr[0] == '+') {
					/* need to search for "+" entry */
					nss_search(be->db_rootp, be->db_initf,
					    op_num, args);
					if (args->returnval == 0)
						continue;
				}
			} else {
				/* search then compare */
				nss_search(be->db_rootp, be->db_initf, op_num,
				    args);
				if (args->returnval == 0)
					continue;
				if (!be->permit_netgroups ||
				    !netgr_in(be, instr + 2,
				    (*be->getnamef)(args)))
					continue;
			}
		} else if (instr[1] == '\0') {
			/*
			 * Case 2:
			 * The entry is of the form "+" or "-".  The former
			 * allows all entries from name services.  The latter
			 * is illegal and ought to be ignored.
			 */
			if (instr[0] == '-')
				continue;
			/* need to search for "+" entry */
			nss_search(be->db_rootp, be->db_initf, op_num, args);
			if (args->returnval == 0)
				continue;
		} else {
			/*
			 * Case 3:
			 * The entry is of the form "+name" or "-name".
			 * If we're performing a lookup by name, we can simply
			 * extract the name from the search key
			 * (i.e. args->key.name).  If not, then we must call
			 * nss_search() before extracting the name via the
			 * get_XXname() function. i.e. (*be->getnamef)(args).
			 */
			if (is_nss_lookup_by_name(0, op_num) != 0) {
				/* compare then search */
				if (strcmp(instr + 1, args->key.name) != 0)
					continue;
				if (instr[0] == '+') {
					/* need to search for "+" entry */
					nss_search(be->db_rootp, be->db_initf,
					    op_num, args);
					if (args->returnval == 0)
						continue;
				}
			} else {
				/* search then compare */
				nss_search(be->db_rootp, be->db_initf, op_num,
				    args);
				if (args->returnval == 0)
					continue;
				if (strcmp(instr + 1, (*be->getnamef)(args))
				    != 0)
					continue;
			}
		}
		if (instr[0] == '-') {
			/* no need to search for "-" entry */
			args->returnval = 0;
			args->erange = 0;
			res = NSS_NOTFOUND;
		} else {
			if (colon != 0)
				*colon = ':';	/* restoration */
			res = do_merge(be, args, instr, linelen);
		}
		break;
	}

	/*
	 * stayopen is set to 0 by default in order to close the opened
	 * file.  Some applications may break if it is set to 1.
	 */
	if (!args->stayopen) {
		(void) _nss_compat_endent(be, 0);
	}

	return (res);
}

nss_status_t
_nss_compat_getent(be, a)
	compat_backend_ptr_t	be;
	void			*a;
{
	nss_XbyY_args_t		*args = (nss_XbyY_args_t *)a;
	nss_status_t		res;
	char			*colon = 0; /* <=== need comment re lifetime */

	if (be->f == 0) {
		if ((res = _nss_compat_setent(be, 0)) != NSS_SUCCESS) {
			return (res);
		}
	}

	if (be->buf == 0 &&
	    (be->buf = malloc(be->minbuf)) == 0) {
		return (NSS_UNAVAIL); /* really panic, malloc failed */
	}

	/*CONSTCOND*/
	while (1) {
		char		*instr	= be->buf;
		int		linelen;
		char		*name;	/* === Need more distinctive label */
		const char	*savename;

		/*
		 * In the code below...
		 *    break	means "I found one, I think" (i.e. goto the
		 *		code after the end of the switch statement),
		 *    continue	means "Next candidate"
		 *		(i.e. loop around to the switch statement),
		 *    return	means "I'm quite sure" (either Yes or No).
		 */
		switch (be->state) {

		    case GETENT_DONE:
			args->returnval	= 0;
			args->erange	= 0;
			return (NSS_NOTFOUND);

		    case GETENT_ATTRDB:
			res = _attrdb_compat_XY_all(be,
			    args, 1, (compat_XY_check_func)NULL, 0);
			return (res);

		    case GETENT_FILE:
			linelen = read_line(be->f, instr, be->minbuf);
			if (linelen < 0) {
				/* End of file */
				be->state = GETENT_DONE;
				continue;
			}
			if ((colon = strchr(instr, ':')) != 0) {
				*colon = '\0';
			}
			if (instr[0] == '-') {
				if (instr[1] != '@') {
					strset_add(&be->minuses, instr + 1);
				} else if (be->permit_netgroups) {
					netgr_set(be, instr + 2);
					while (netgr_next_u(be, &name)) {
						strset_add(&be->minuses,
							name);
					}
					netgr_end(be);
				} /* Else (silently) ignore the entry */
				continue;
			} else if (instr[0] != '+') {
				int	parsestat;
				/*
				 * Normal entry, no +/- nonsense
				 */
				if (colon != 0) {
					*colon = ':';
				}
				args->returnval = 0;
				parsestat = (*args->str2ent)(instr, linelen,
							args->buf.result,
							args->buf.buffer,
							args->buf.buflen);
				if (parsestat == NSS_STR_PARSE_SUCCESS) {
					args->returnval = args->buf.result;
					return (NSS_SUCCESS);
				}
				/* ==> ?? Treat ERANGE differently ?? */
				if (parsestat == NSS_STR_PARSE_ERANGE) {
					args->returnval = 0;
					args->erange = 1;
					return (NSS_NOTFOUND);
				}
				/* Skip the offending entry, get next */
				continue;
			} else if (instr[1] == '\0') {
				/* Plain "+" */
				nss_setent(be->db_rootp, be->db_initf,
					&be->db_context);
				be->state = GETENT_ALL;
				be->linelen = linelen;
				continue;
			} else if (instr[1] == '@') {
				/* "+@netgroup" */
				netgr_set(be, instr + 2);
				be->state = GETENT_NETGROUP;
				be->linelen = linelen;
				continue;
			} else {
				/* "+name" */
				name = instr + 1;
				break;
			}
			/* NOTREACHED */

		    case GETENT_ALL:
			linelen = be->linelen;
			args->returnval = 0;
			nss_getent(be->db_rootp, be->db_initf,
				&be->db_context, args);
			if (args->returnval == 0) {
				/* ==> ?? Treat ERANGE differently ?? */
				nss_endent(be->db_rootp, be->db_initf,
					&be->db_context);
				be->state = GETENT_FILE;
				continue;
			}
			if (strset_in(&be->minuses, (*be->getnamef)(args))) {
				continue;
			}
			name = 0; /* tell code below we've done the lookup */
			break;

		    case GETENT_NETGROUP:
			linelen = be->linelen;
			if (!netgr_next_u(be, &name)) {
				netgr_end(be);
				be->state = GETENT_FILE;
				continue;
			}
			/* pass "name" variable to code below... */
			break;
		}

		if (name != 0) {
			if (strset_in(&be->minuses, name)) {
				continue;
			}
			/*
			 * Do a getXXXnam(name).  If we were being pure,
			 *   we'd introduce yet another function-pointer
			 *   that the database-specific code had to supply
			 *   to us.  Instead we'll be grotty and hard-code
			 *   the knowledge that
			 *	(a) The username is always passwd in key.name,
			 *	(b) NSS_DBOP_PASSWD_BYNAME ==
			 *		NSS_DBOP_SHADOW_BYNAME ==
			 *		NSS_DBOP_next_iter.
			 */
			savename = args->key.name;
			args->key.name	= name;
			args->returnval	= 0;
			nss_search(be->db_rootp, be->db_initf,
				NSS_DBOP_next_iter, args);
			args->key.name = savename;  /* In case anyone cares */
		}
		/*
		 * Found one via "+", "+name" or "@netgroup".
		 * Override some fields if the /etc file says to do so.
		 */
		if (args->returnval == 0) {
			/* ==> ?? Should treat erange differently? */
			continue;
		}
		/* 'colon' was set umpteen iterations ago in GETENT_FILE */
		if (colon != 0) {
			*colon = ':';
			colon = 0;
		}
		return (do_merge(be, args, instr, linelen));
	}
}

/* We don't use this directly;  we just copy the bits when we want to	 */
/* initialize the variable (in the compat_backend struct) that we do use */
static DEFINE_NSS_GETENT(context_initval);

nss_backend_t *
_nss_compat_constr(ops, n_ops, filename, min_bufsize, rootp, initf, netgroups,
		getname_func, merge_func)
	compat_backend_op_t	ops[];
	int			n_ops;
	const char		*filename;
	int			min_bufsize;
	nss_db_root_t		*rootp;
	nss_db_initf_t		initf;
	int			netgroups;
	compat_get_name		getname_func;
	compat_merge_func	merge_func;
{
	compat_backend_ptr_t	be;

	if ((be = (compat_backend_ptr_t)malloc(sizeof (*be))) == 0) {
		return (0);
	}
	be->ops		= ops;
	be->n_ops	= n_ops;
	be->filename	= filename;
	be->f		= 0;
	be->minbuf	= min_bufsize;
	be->buf		= 0;

	be->db_rootp	= rootp;
	be->db_initf	= initf;
	be->db_context	= context_initval;

	be->getnamef	= getname_func;
	be->mergef	= merge_func;

	if ((strcmp(be->filename, USERATTR_FILENAME) == 0) ||
	    (strcmp(be->filename, AUDITUSER_FILENAME) == 0))
		be->state = GETENT_ATTRDB;
	else
		be->state = GETENT_FILE;    /* i.e. do Automatic setent(); */

	be->minuses	= 0;

	be->permit_netgroups = netgroups;
	be->yp_domain	= 0;
	be->getnetgrent_backend	= 0;
	be->netgr_buffer = 0;

	return ((nss_backend_t *)be);
}
