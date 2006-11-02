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

/*
 * core library for common functions across all config store types
 * and file systems to be exported. This includes legacy dfstab/sharetab
 * parsing. Need to eliminate XML where possible.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "libshare.h"
#include "libshare_impl.h"
#include "libfsmgt.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <grp.h>
#include <limits.h>
#include <sys/param.h>
#include <signal.h>
#include <libintl.h>

#include "sharetab.h"

#define	DFSTAB_NOTICE_LINES	5
static char *notice[DFSTAB_NOTICE_LINES] =	{
	"# Do not modify this file directly.\n",
	"# Use the sharemgr(1m) command for all share management\n",
	"# This file is reconstructed and only maintained for backward\n",
	"# compatibility. Configuration lines could be lost.\n",
	"#\n"
};

#define	STRNCAT(x, y, z)	(xmlChar *)strncat((char *)x, (char *)y, z)

/* will be much smaller, but this handles bad syntax in the file */
#define	MAXARGSFORSHARE	256

/* used internally only */
typedef
struct sharelist {
    struct sharelist *next;
    int   persist;
    char *path;
    char *resource;
    char *fstype;
    char *options;
    char *description;
    char *group;
    char *origline;
    int lineno;
} xfs_sharelist_t;
static void parse_dfstab(char *, xmlNodePtr);
extern char *get_token(char *);
static void dfs_free_list(xfs_sharelist_t *);
/* prototypes */
void getlegacyconfig(char *, xmlNodePtr *);
extern sa_share_t _sa_add_share(sa_group_t, char *, int, int *);
extern xmlNodePtr _sa_get_next_error(xmlNodePtr);
extern sa_group_t _sa_create_group(char *);
static void outdfstab(FILE *, xfs_sharelist_t *);
extern int _sa_remove_optionset(sa_optionset_t);
extern int set_node_share(void *, char *, char *);
extern void set_node_attr(void *, char *, char *);

/*
 * alloc_sharelist()
 *
 * allocator function to return an zfs_sharelist_t
 */

static xfs_sharelist_t *
alloc_sharelist()
{
	xfs_sharelist_t *item;

	item = (xfs_sharelist_t *)malloc(sizeof (xfs_sharelist_t));
	if (item != NULL)
	    (void) memset(item, '\0', sizeof (xfs_sharelist_t));
	return (item);
}

/*
 * fix_notice(list)
 *
 * Look at the beginning of the current /etc/dfs/dfstab file and add
 * the do not modify notice if it doesn't exist.
 */

static xfs_sharelist_t *
fix_notice(xfs_sharelist_t *list)
{
	xfs_sharelist_t *item, *prev;
	int i;

	if (list->path == NULL && list->description != NULL &&
	    strcmp(list->description, notice[0]) != 0) {
	    for (prev = NULL, i = 0; i < DFSTAB_NOTICE_LINES; i++) {
		item = alloc_sharelist();
		if (item != NULL) {
		    item->description = strdup(notice[i]);
		    if (prev == NULL) {
			item->next = list;
			prev = item;
			list = item;
		    } else {
			item->next = prev->next;
			prev->next = item;
			prev = item;
		    }
		}
	    }
	}
	return (list);
}

/*
 * getdfstab(dfs)
 *
 * Returns an zfs_sharelist_t list of lines from the dfstab file
 * pointed to by the FILE pointer dfs. Each entry is parsed and the
 * original line is also preserved. Used in parsing and updating the
 * dfstab file.
 */

static xfs_sharelist_t *
getdfstab(FILE *dfs)
{
	char buff[_POSIX_ARG_MAX]; /* reasonable size given syntax of share */
	char *bp;
	char *token;
	char *args[MAXARGSFORSHARE];
	int argc;
	int c;
	static int line = 0;
	xfs_sharelist_t *item, *first, *last;

	if (dfs != NULL) {
	    first = NULL;
	    line = 0;
	    while (fgets(buff, sizeof (buff), dfs) != NULL) {
		line++;
		bp = buff;
		if (buff[0] == '#') {
		    item = alloc_sharelist();
		    if (item != NULL) {
			/* if no path, then comment */
			item->lineno = line;
			item->description = strdup(buff);
			if (first == NULL) {
			    first = item;
			    last = item;
			} else {
			    last->next = item;
			    last = item;
			}
		    } else {
			break;
		    }
		    continue;
		} else if (buff[0] == '\n') {
		    continue;
		}
		optind = 1;
		item = alloc_sharelist();
		if (item == NULL) {
		    break;
		} else if (first == NULL) {
		    first = item;
		    last = item;
		} else {
		    last->next = item;
		    last = item;
		}
		item->lineno = line;
		item->origline = strdup(buff);
		(void) get_token(NULL); /* reset to new pointers */
		argc = 0;
		while ((token = get_token(bp)) != NULL) {
		    if (argc < MAXARGSFORSHARE) {
			args[argc++] = token;
		    }
		}
		while ((c = getopt(argc, args, "F:o:d:pg:")) != -1) {
		    switch (c) {
		    case 'p':
			item->persist = 1;
			break;
		    case 'F':
			item->fstype = strdup(optarg);
			break;
		    case 'o':
			item->options = strdup(optarg);
			break;
		    case 'd':
			item->description = strdup(optarg);
			break;
		    case 'g':
			item->group = strdup(optarg);
			break;
		    default:
			break;
		    }
		}
		if (optind < argc) {
		    item->path = strdup(args[optind]);
		    optind++;
		    if (optind < argc) {
			char *resource;
			char *optgroup;
			/* resource and/or groupname */
			resource = args[optind];
			optgroup = strchr(resource, '@');
			if (optgroup != NULL) {
			    *optgroup++ = '\0';
			}
			if (optgroup != NULL)
			    item->group = strdup(optgroup);
			if (resource != NULL && strlen(resource) > 0)
			    item->resource = strdup(resource);
		    }
		}
	    }
	    if (item->fstype == NULL)
		item->fstype = strdup("nfs"); /* this is the default */
	}
	first = fix_notice(first);
	return (first);
}

/*
 * finddfsentry(list, path)
 *
 * Look for path in the zfs_sharelist_t list and return the tnry if it
 * exists.
 */

static xfs_sharelist_t *
finddfsentry(xfs_sharelist_t *list, char *path)
{
	xfs_sharelist_t *item;

	for (item = list; item != NULL; item = item->next) {
	    if (item->path != NULL && strcmp(item->path, path) == 0)
		return (item);
	}
	return (NULL);
}

/*
 * remdfsentry(list, path, proto)
 *
 * Remove the specified path (with protocol) from the list. This will
 * remove it from dfstab when the file is rewritten.
 */

static xfs_sharelist_t *
remdfsentry(xfs_sharelist_t *list, char *path, char *proto)
{
	xfs_sharelist_t *item, *prev = NULL;


	for (item = prev = list; item != NULL; item = item->next) {
	    /* skip comment entry but don't lose it */
	    if (item->path == NULL) {
		prev = item;
		continue;
	    }
	    /* if proto is NULL, remove all protocols */
	    if (proto == NULL || (strcmp(item->path, path) == 0 &&
		(item->fstype != NULL && strcmp(item->fstype, proto) == 0)))
		break;
	    if (item->fstype == NULL &&
		(proto == NULL || strcmp(proto, "nfs") == 0))
		break;
	    prev = item;
	}
	if (item != NULL) {
	    if (item == prev) {
		list = item->next; /* this must be the first one */
	    } else {
		prev->next = item->next;
	    }
	    item->next = NULL;
	    dfs_free_list(item);
	}
	return (list);
}

/*
 * remdfsline(list, line)
 *
 * Remove the line specified from the list.
 */

static xfs_sharelist_t *
remdfsline(xfs_sharelist_t *list, char *line)
{
	xfs_sharelist_t *item, *prev = NULL;

	for (item = prev = list; item != NULL; item = item->next) {
	    /* skip comment entry but don't lose it */
	    if (item->path == NULL) {
		prev = item;
		continue;
	    }
	    if (strcmp(item->origline, line) == 0) {
		break;
	    }
	    prev = item;
	}
	if (item != NULL) {
	    if (item == prev) {
		list = item->next; /* this must be the first one */
	    } else {
		prev->next = item->next;
	    }
	    item->next = NULL;
	    dfs_free_list(item);
	}
	return (list);
}

/*
 * adddfsentry(list, share, proto)
 *
 * Add an entry to the dfstab list for share (relative to proto). This
 * is used to update dfstab for legacy purposes.
 */

static xfs_sharelist_t *
adddfsentry(xfs_sharelist_t *list, sa_share_t share, char *proto)
{
	xfs_sharelist_t *item, *tmp;
	sa_group_t parent;
	char *groupname;

	item = alloc_sharelist();
	if (item != NULL) {
	    parent = sa_get_parent_group(share);
	    groupname = sa_get_group_attr(parent, "name");
	    if (strcmp(groupname, "default") == 0) {
		sa_free_attr_string(groupname);
		groupname = NULL;
	    }
	    item->path = sa_get_share_attr(share, "path");
	    item->resource = sa_get_share_attr(share, "resource");
	    item->group = groupname;
	    item->fstype = strdup(proto);
	    item->options = sa_proto_legacy_format(proto, share, 1);
	    if (item->options != NULL && strlen(item->options) == 0) {
		free(item->options);
		item->options = NULL;
	    }
	    item->description = sa_get_share_description(share);
	    if (item->description != NULL && strlen(item->description) == 0) {
		sa_free_share_description(item->description);
		item->description = NULL;
	    }
	    if (list == NULL) {
		list = item;
	    } else {
		for (tmp = list; tmp->next != NULL; tmp = tmp->next)
		    /* do nothing */;
		tmp->next = item;
	    }
	}
	return (list);
}

/*
 * outdfstab(dfstab, list)
 *
 * Output the list to dfstab making sure the file is truncated.
 * Comments and errors are preserved.
 */

static void
outdfstab(FILE *dfstab, xfs_sharelist_t *list)
{
	xfs_sharelist_t *item;

	(void) ftruncate(fileno(dfstab), 0);

	for (item = list; item != NULL; item = item->next) {
	    if (item->path != NULL) {
		if (*item->path == '/')
		    (void) fprintf(dfstab, "share %s%s%s%s%s%s%s %s%s%s%s%s\n",
			    (item->fstype != NULL) ? "-F " : "",
			    (item->fstype != NULL) ? item->fstype : "",
			    (item->options != NULL) ? " -o " : "",
			    (item->options != NULL) ? item->options : "",
			    (item->description != NULL) ? " -d \"" : "",
			    (item->description != NULL) ?
				item->description : "",
			    (item->description != NULL) ? "\"" : "",
			    item->path,
			    ((item->resource != NULL) ||
				(item->group != NULL)) ? " " : "",
			    (item->resource != NULL) ? item->resource : "",
			    item->group != NULL ? "@" : "",
			    item->group != NULL ? item->group : "");
		else
		    (void) fprintf(dfstab, "%s", item->origline);
	    } else {
		if (item->description != NULL) {
		    (void) fprintf(dfstab, "%s", item->description);
		} else {
		    (void) fprintf(dfstab, "%s", item->origline);
		}
	    }
	}
}

/*
 * open_dfstab(file)
 *
 * Open the specified dfstab file. If the owner/group/perms are wrong,
 * fix them.
 */

static FILE *
open_dfstab(char *file)
{
	struct group *grp;
	struct group group;
	char *buff;
	int grsize;
	FILE *dfstab;

	dfstab = fopen(file, "r+");
	if (dfstab == NULL) {
	    dfstab = fopen(file, "w+");
	}
	if (dfstab != NULL) {
		grsize = sysconf(_SC_GETGR_R_SIZE_MAX);
		buff = malloc(grsize);
		if (buff != NULL)
		    grp = getgrnam_r(SA_DEFAULT_FILE_GRP, &group, buff, grsize);
		else
		    grp = getgrnam(SA_DEFAULT_FILE_GRP); /* take the risk */
		(void) fchmod(fileno(dfstab), 0644);
		(void) fchown(fileno(dfstab), 0,
				grp != NULL ? grp->gr_gid : 3);
		if (buff != NULL)
		    free(buff);
		rewind(dfstab);
	}
	return (dfstab);
}

/*
 * sa_comment_line(line, err)
 *
 * Add a comment to the dfstab file with err as a prefix to the
 * original line.
 */

static void
sa_comment_line(char *line, char *err)
{
	FILE *dfstab;
	xfs_sharelist_t *list;
	sigset_t old, new;

	dfstab = open_dfstab(SA_LEGACY_DFSTAB);
	if (dfstab != NULL) {
		(void) setvbuf(dfstab, NULL, _IOLBF, BUFSIZ * 8);
		(void) sigprocmask(SIG_BLOCK, NULL, &new);
		(void) sigaddset(&new, SIGHUP);
		(void) sigaddset(&new, SIGINT);
		(void) sigaddset(&new, SIGQUIT);
		(void) sigaddset(&new, SIGTSTP);
		(void) sigprocmask(SIG_SETMASK, &new, &old);
		(void) lockf(fileno(dfstab), F_LOCK, 0);
		list = getdfstab(dfstab);
		rewind(dfstab);
		(void) remdfsline(list, line);
		outdfstab(dfstab, list);
		(void) fprintf(dfstab, "# Error: %s: %s", err, line);
		(void) fsync(fileno(dfstab));
		(void) lockf(fileno(dfstab), F_ULOCK, 0);
		(void) fclose(dfstab);
		(void) sigprocmask(SIG_SETMASK, &old, NULL);
		if (list != NULL)
		    dfs_free_list(list);
	}
}

/*
 * sa_delete_legacy(share)
 *
 * Delete the specified share from the legacy config file.
 */

int
sa_delete_legacy(sa_share_t share)
{
	FILE *dfstab;
	int err;
	int ret = SA_OK;
	xfs_sharelist_t *list;
	char *path;
	sa_optionset_t optionset;
	sa_group_t parent;
	sigset_t old, new;

	dfstab = open_dfstab(SA_LEGACY_DFSTAB);
	if (dfstab != NULL) {
		(void) setvbuf(dfstab, NULL, _IOLBF, BUFSIZ * 8);
		(void) sigprocmask(SIG_BLOCK, NULL, &new);
		(void) sigaddset(&new, SIGHUP);
		(void) sigaddset(&new, SIGINT);
		(void) sigaddset(&new, SIGQUIT);
		(void) sigaddset(&new, SIGTSTP);
		(void) sigprocmask(SIG_SETMASK, &new, &old);
		path = sa_get_share_attr(share, "path");
		parent = sa_get_parent_group(share);
		if (parent != NULL) {
		    (void) lockf(fileno(dfstab), F_LOCK, 0);
		    list = getdfstab(dfstab);
		    rewind(dfstab);
		    for (optionset = sa_get_optionset(parent, NULL);
			optionset != NULL;
			optionset = sa_get_next_optionset(optionset)) {
			char *proto = sa_get_optionset_attr(optionset, "type");
			if (list != NULL && proto != NULL)
			    (void) remdfsentry(list, path, proto);
			if (proto == NULL)
			    ret = SA_NO_MEMORY;
			/*
			 * may want to only do the dfstab if this call
			 * returns NOT IMPLEMENTED but it shouldn't
			 * hurt.
			 */
			if (ret == SA_OK) {
			    err = sa_proto_delete_legacy(proto, share);
			    if (err != SA_NOT_IMPLEMENTED)
				ret = err;
			}
			if (proto != NULL)
			    sa_free_attr_string(proto);
		    }
		    outdfstab(dfstab, list);
		    if (list != NULL)
			dfs_free_list(list);
		    (void) fflush(dfstab);
		    (void) lockf(fileno(dfstab), F_ULOCK, 0);
		}
		(void) fsync(fileno(dfstab));
		(void) sigprocmask(SIG_SETMASK, &old, NULL);
		(void) fclose(dfstab);
		sa_free_attr_string(path);
	} else {
		if (errno == EACCES || errno == EPERM) {
		    ret = SA_NO_PERMISSION;
		} else {
		    ret = SA_CONFIG_ERR;
		}
	}
	return (ret);
}

/*
 * sa_update_legacy(share, proto)
 *
 * There is an assumption that dfstab will be the most common form of
 * legacy configuration file for shares, but not the only one. Because
 * of that, dfstab handling is done in the main code with calls to
 * this function and protocol specific calls to deal with formating
 * options into dfstab/share compatible syntax. Since not everything
 * will be dfstab, there is a provision for calling a protocol
 * specific plugin interface that allows the protocol plugin to do its
 * own legacy files and skip the dfstab update.
 */

int
sa_update_legacy(sa_share_t share, char *proto)
{
	FILE *dfstab;
	int ret = SA_OK;
	xfs_sharelist_t *list;
	char *path;
	sigset_t old, new;
	char *persist;

	ret = sa_proto_update_legacy(proto, share);
	if (ret != SA_NOT_IMPLEMENTED)
	    return (ret);
	/* do the dfstab format */
	persist = sa_get_share_attr(share, "type");
	/*
	 * only update if the share is not transient -- no share type
	 * set or the type is not "transient".
	 */
	if (persist == NULL || strcmp(persist, "transient") != 0) {
	    dfstab = open_dfstab(SA_LEGACY_DFSTAB);
	    if (dfstab != NULL) {
		(void) setvbuf(dfstab, NULL, _IOLBF, BUFSIZ * 8);
		(void) sigprocmask(SIG_BLOCK, NULL, &new);
		(void) sigaddset(&new, SIGHUP);
		(void) sigaddset(&new, SIGINT);
		(void) sigaddset(&new, SIGQUIT);
		(void) sigaddset(&new, SIGTSTP);
		(void) sigprocmask(SIG_SETMASK, &new, &old);
		path = sa_get_share_attr(share, "path");
		(void) lockf(fileno(dfstab), F_LOCK, 0);
		list = getdfstab(dfstab);
		rewind(dfstab);
		if (list != NULL)
		    list = remdfsentry(list, path, proto);
		list = adddfsentry(list, share, proto);
		outdfstab(dfstab, list);
		(void) fflush(dfstab);
		(void) lockf(fileno(dfstab), F_ULOCK, 0);
		(void) fsync(fileno(dfstab));
		(void) sigprocmask(SIG_SETMASK, &old, NULL);
		(void) fclose(dfstab);
		sa_free_attr_string(path);
		if (list != NULL)
		    dfs_free_list(list);
	    } else {
		if (errno == EACCES || errno == EPERM) {
		    ret = SA_NO_PERMISSION;
		} else {
		    ret = SA_CONFIG_ERR;
		}
	    }
	}
	if (persist != NULL)
	    sa_free_attr_string(persist);
	return (ret);
}

/*
 * sa_is_security(optname, proto)
 *
 * Check to see if optname is a security (named optionset) specific
 * property for the specified protocol.
 */

int
sa_is_security(char *optname, char *proto)
{
	int ret = 0;
	if (proto != NULL)
	    ret = sa_proto_security_prop(proto, optname);
	return (ret);
}

/*
 * add_syntax_comment(root, line, err, todfstab)
 *
 * add a comment to the document indicating a syntax error. If
 * todfstab is set, write it back to the dfstab file as well.
 */

static void
add_syntax_comment(xmlNodePtr root, char *line, char *err, int todfstab)
{
	xmlNodePtr node;

	node = xmlNewChild(root, NULL, (xmlChar *)"error", (xmlChar *)line);
	if (node != NULL) {
	    xmlSetProp(node, (xmlChar *)"type", (xmlChar *)err);
	}
	if (todfstab)
	    sa_comment_line(line, err);
}

/*
 * sa_is_share(object)
 *
 * returns true of the object is of type "share".
 */

int
sa_is_share(void *object)
{
	if (object != NULL) {
	    if (strcmp((char *)((xmlNodePtr)object)->name, "share") == 0)
		return (1);
	}
	return (0);
}

/*
 * _sa_remove_property(property)
 *
 * remove a property only from the document.
 */

static void
_sa_remove_property(sa_property_t property)
{
	xmlUnlinkNode((xmlNodePtr)property);
	xmlFreeNode((xmlNodePtr)property);
}

/*
 * sa_parse_legacy_options(group, options, proto)
 *
 * In order to support legacy configurations, we allow the protocol
 * specific plugin to parse legacy syntax options (like those in
 * /etc/dfs/dfstab). This adds a new optionset to the group (or
 * share).
 *
 * Once the optionset has been created, we then get the derived
 * optionset of the parent (options from the optionset of the parent
 * and any parent it might have) and remove those from the created
 * optionset. This avoids duplication of options.
 */

int
sa_parse_legacy_options(sa_group_t group, char *options, char *proto)
{
	int ret = SA_INVALID_PROTOCOL;
	sa_group_t parent;
	parent = sa_get_parent_group(group);

	if (proto != NULL)
	    ret = sa_proto_legacy_opts(proto, group, options);
	/*
	 * if in a group, remove the inherited options and security
	 */
	if (ret == SA_OK) {
	    if (parent != NULL) {
		sa_optionset_t optionset;
		sa_property_t popt, prop;
		sa_optionset_t localoptions;
		/* find parent options to remove from child */
		optionset = sa_get_derived_optionset(parent, proto, 1);
		localoptions = sa_get_optionset(group, proto);
		if (optionset != NULL) {
		    for (popt = sa_get_property(optionset, NULL);
			popt != NULL;
			popt = sa_get_next_property(popt)) {
			char *tag;
			char *value1;
			char *value2;

			tag = sa_get_property_attr(popt, "type");
			if (tag != NULL) {
			    prop = sa_get_property(localoptions, tag);
			    if (prop != NULL) {
				value1 = sa_get_property_attr(popt, "value");
				value2 = sa_get_property_attr(prop, "value");
				if (value1 != NULL && value2 != NULL &&
				    strcmp(value1, value2) == 0) {
				    /* remove the property from the child */
				    (void) _sa_remove_property(prop);
				}
				if (value1 != NULL)
				    sa_free_attr_string(value1);
				if (value2 != NULL)
				    sa_free_attr_string(value2);
			    }
			    sa_free_attr_string(tag);
			}
		    }
		    prop = sa_get_property(localoptions, NULL);
		    if (prop == NULL && sa_is_share(group)) {
			/*
			 * all properties removed so remove the
			 * optionset if it is on a share
			 */
			(void) _sa_remove_optionset(localoptions);
		    }
		    sa_free_derived_optionset(optionset);
		}
		/*
		 * need to remove security here. If there are no
		 * security options on the local group/share, don't
		 * bother since those are the only ones that would be
		 * affected.
		 */
		localoptions = sa_get_all_security_types(group, proto, 0);
		if (localoptions != NULL) {
		    for (prop = sa_get_property(localoptions, NULL);
			prop != NULL; prop = sa_get_next_property(prop)) {
			char *tag;
			sa_security_t security;
			tag = sa_get_property_attr(prop, "type");
			if (tag != NULL) {
			    security = sa_get_security(group, tag, proto);
			    sa_free_attr_string(tag);
			    for (popt = sa_get_property(security, NULL);
				popt != NULL;
				popt = sa_get_next_property(popt)) {
				char *value1;
				char *value2;

				/* remove duplicates from this level */
				value1 = sa_get_property_attr(popt, "value");
				value2 = sa_get_property_attr(prop, "value");
				if (value1 != NULL && value2 != NULL &&
				    strcmp(value1, value2) == 0) {
				    /* remove the property from the child */
				    (void) _sa_remove_property(prop);
				}
				if (value1 != NULL)
				    sa_free_attr_string(value1);
				if (value2 != NULL)
				    sa_free_attr_string(value2);
			    }
			}
		    }
		    (void) sa_destroy_optionset(localoptions);
		}
	    }
	}
	return (ret);
}

/*
 * dfs_free_list(list)
 *
 * Free the data in each list entry of the list as well as freeing the
 * entries themselves. We need to avoid memory leaks and don't want to
 * dereference any NULL members.
 */

static void
dfs_free_list(xfs_sharelist_t *list)
{
	xfs_sharelist_t *entry;
	for (entry = list; entry != NULL; entry = list) {
	    if (entry->path != NULL)
		free(entry->path);
	    if (entry->resource != NULL)
		free(entry->resource);
	    if (entry->fstype != NULL)
		free(entry->fstype);
	    if (entry->options != NULL)
		free(entry->options);
	    if (entry->description != NULL)
		free(entry->description);
	    if (entry->origline != NULL)
		free(entry->origline);
	    if (entry->group != NULL)
		free(entry->group);
	    list = list->next;
	    free(entry);
	}
}

/*
 * parse_dfstab(dfstab, root)
 *
 * Open and read the existing dfstab, parsing each line and adding it
 * to the internal configuration. Make sure syntax errors, etc are
 * preserved as comments.
 */

static void
parse_dfstab(char *dfstab, xmlNodePtr root)
{
	sa_share_t share;
	sa_group_t group;
	sa_group_t sgroup = NULL;
	sa_group_t defgroup;
	xfs_sharelist_t *head, *list;
	int err;
	int defined_group;
	FILE *dfs;
	char *oldprops;

	/* read the dfstab format file and fill in the doc tree */

	dfs = fopen(dfstab, "r");
	if (dfs == NULL) {
	    return;
	}

	defgroup = sa_get_group("default");

	for (head = list = getdfstab(dfs);
		list != NULL;
		list = list->next) {
	    share = NULL;
	    group = NULL;
	    defined_group = 0;
	    err = 0;

	    if (list->origline == NULL) {
		/*
		 * Comment line that we will likely skip.
		 * If the line has the syntax:
		 *	# error: string: string
		 * It should be preserved until manually deleted.
		 */
		if (list->description != NULL &&
		    strncmp(list->description, "# Error: ", 9) == 0) {
		    char *line;
		    char *error;
		    char *cmd;
		    line = strdup(list->description);
		    if (line != NULL) {
			error = line + 9;
			cmd = strchr(error, ':');
			if (cmd != NULL) {
			    int len;
			    *cmd = '\0';
			    cmd += 2;
			    len = strlen(cmd);
			    cmd[len - 1] = '\0';
			    add_syntax_comment(root, cmd, error, 0);
			}
			free(line);
		    }
		}
		continue;
	    }
	    if (list->path != NULL && strlen(list->path) > 0 &&
		*list->path == '/') {
		share = sa_find_share(list->path);
		if (share != NULL)
		    sgroup = sa_get_parent_group(share);
		else
		    sgroup = NULL;
	    } else {
		(void) printf(gettext("No share specified in dfstab: "
					"line %d: %s\n"),
			list->lineno, list->origline);
		add_syntax_comment(root, list->origline,
				    gettext("No share specified"),
				    1);
		continue;
	    }
	    if (list->group != NULL && strlen(list->group) > 0) {
		group = sa_get_group(list->group);
		defined_group = 1;
	    } else {
		group = defgroup;
	    }
	    if (defined_group && group == NULL) {
		(void) printf(gettext("Unknown group used in dfstab: "
					"line %d: %s\n"),
			list->lineno, list->origline);
		add_syntax_comment(root, list->origline,
				    gettext("Unknown group specified"), 1);
		continue;
	    }
	    if (group != NULL) {
		if (share == NULL) {
		    if (!defined_group && group == defgroup) {
			/* this is an OK add for legacy */
			share = sa_add_share(defgroup, list->path,
					SA_SHARE_PERMANENT | SA_SHARE_PARSER,
					&err);
			if (share != NULL) {
			    if (list->description != NULL &&
				strlen(list->description) > 0)
				(void) sa_set_share_description(share,
							    list->description);
			    if (list->options != NULL &&
				strlen(list->options) > 0) {
				(void) sa_parse_legacy_options(share,
								list->options,
								list->fstype);
			    }
			    if (list->resource != NULL)
				(void) sa_set_share_attr(share, "resource",
						    list->resource);
			} else {
			    (void) printf(gettext("Error in dfstab: "
					    "line %d: %s\n"),
				    list->lineno, list->origline);
			    if (err != SA_BAD_PATH)
				add_syntax_comment(root, list->origline,
						gettext("Syntax"), 1);
			    else
				add_syntax_comment(root, list->origline,
						gettext("Path"), 1);
			    continue;
			}
		    }
		} else {
		    if (group != sgroup) {
			(void) printf(gettext("Attempt to change"
						"configuration in"
						"dfstab: line %d: %s\n"),
				list->lineno, list->origline);
			add_syntax_comment(root, list->origline,
				gettext("Attempt to change configuration"), 1);
			continue;
		    }
		    /* its the same group but could have changed options */
		    oldprops = sa_proto_legacy_format(list->fstype, share, 0);
		    if (oldprops != NULL) {
			if (list->options != NULL &&
				strcmp(oldprops, list->options) != 0) {
			    sa_optionset_t opts;
			    sa_security_t secs;
			    /* possibly different values */
			    opts = sa_get_optionset((sa_group_t)share,
							list->fstype);
			    (void) sa_destroy_optionset(opts);
			    for (secs = sa_get_security((sa_group_t)share,
							NULL, list->fstype);
				secs != NULL;
				secs = sa_get_security((sa_group_t)share,
							NULL, list->fstype)) {
				(void) sa_destroy_security(secs);
			    }
			    (void) sa_parse_legacy_options(share,
							    list->options,
							    list->fstype);
			}
		    }
		}
	    } else {
		/* shouldn't happen */
		err = SA_CONFIG_ERR;
	    }

	}
	dfs_free_list(head);
}

/*
 * legacy_removes(group, file)
 *
 * Find any shares that are "missing" from the legacy file. These
 * should be removed from the configuration since they are likely from
 * a legacy app or the admin modified the dfstab file directly. We
 * have to support this even if it is not the recommended way to do
 * things.
 */

static void
legacy_removes(sa_group_t group, char *file)
{
	sa_share_t share;
	char *path;
	xfs_sharelist_t *list, *item;
	FILE *dfstab;

	dfstab = fopen(file, "r");
	if (dfstab != NULL) {
	    list = getdfstab(dfstab);
	    (void) fclose(dfstab);
	    for (share = sa_get_share(group, NULL); share != NULL;
		share = sa_get_next_share(share)) {
		/* now see if the share is in the dfstab file */
		path = sa_get_share_attr(share, "path");
		if (path != NULL) {
		    item = finddfsentry(list, path);
		    if (item == NULL) {
			/* the share was removed this way */
			(void) sa_remove_share(share);
			/* start over since the list was broken */
			share = sa_get_share(group, NULL);
		    }
		    sa_free_attr_string(path);
		}
	    }
	    if (list != NULL)
		dfs_free_list(list);
	}
}

/*
 * getlegacyconfig(path, root)
 *
 * Parse dfstab and build the legacy configuration. This only gets
 * called when a change was detected.
 */

void
getlegacyconfig(char *path, xmlNodePtr *root)
{
	sa_group_t defgroup;

	if (root != NULL) {
	    if (*root == NULL)
		*root = xmlNewNode(NULL, (xmlChar *)"sharecfg");
	    if (*root != NULL) {
		if (strcmp(path, SA_LEGACY_DFSTAB) == 0) {
			/*
			 * walk the default shares and find anything
			 * missing.  we do this first to make sure it
			 * is cleaned up since there may be legacy
			 * code add/del via dfstab and we need to
			 * cleanup SMF.
			 */
		    defgroup = sa_get_group("default");
		    if (defgroup != NULL) {
			legacy_removes(defgroup, path);
		    }
		    /* parse the dfstab and add anything new */
		    parse_dfstab(path, *root);
		}
	    }
	}
}

/*
 * parse_sharetab(void)
 *
 * Read the /etc/dfs/sharetab file via libfsmgt and see which entries
 * don't exist in the repository. These shares are marked transient.
 * We also need to see if they are ZFS shares since ZFS bypasses the
 * SMF repository.
 */

int
parse_sharetab(void)
{
	fs_sharelist_t *list, *tmplist;
	int err = 0;
	sa_share_t share;
	sa_group_t group;
	sa_group_t lgroup;
	char *groupname;
	int legacy = 0;

	list = fs_get_share_list(&err);
	if (list == NULL)
	    return (legacy);

	lgroup = sa_get_group("default");

	for (tmplist = list; tmplist != NULL; tmplist = tmplist->next) {
	    group = NULL;
	    share = sa_find_share(tmplist->path);
	    if (share == NULL) {
		/*
		 * this share is transient so needs to be
		 * added. Initially, this will be under
		 * default(legacy) unless it is a ZFS
		 * share. If zfs, we need a zfs group.
		 */
		if (tmplist->resource != NULL &&
		    (groupname = strchr(tmplist->resource, '@')) != NULL) {
		    /* there is a defined group */
		    *groupname++ = '\0';
		    group = sa_get_group(groupname);
		    if (group != NULL) {
			share = _sa_add_share(group, tmplist->path,
						SA_SHARE_TRANSIENT, &err);
		    } else {
			(void) printf(gettext("Group for temporary share"
						"not found: %s\n"),
					tmplist->path);
			share = _sa_add_share(lgroup, tmplist->path,
						SA_SHARE_TRANSIENT, &err);
		    }
		} else {
		    if (sa_zfs_is_shared(tmplist->path)) {
			group = sa_get_group("zfs");
			if (group == NULL) {
			    group = sa_create_group("zfs", &err);
			    if (group == NULL && err == SA_NO_PERMISSION) {
				group = _sa_create_group("zfs");
			    }
			    if (group != NULL) {
				(void) sa_create_optionset(group,
							    tmplist->fstype);
				(void) sa_set_group_attr(group, "zfs", "true");
			    }
			}
			if (group != NULL) {
			    share = _sa_add_share(group, tmplist->path,
						    SA_SHARE_TRANSIENT, &err);
			}
		    } else {
			share = _sa_add_share(lgroup, tmplist->path,
						SA_SHARE_TRANSIENT, &err);
		    }
		}
		if (share == NULL)
		    (void) printf(gettext("Problem with transient: %s\n"),
				    sa_errorstr(err));
		if (share != NULL)
		    set_node_attr(share, "shared", "true");

		if (err == SA_OK) {
		    if (tmplist->options != NULL &&
			strlen(tmplist->options) > 0) {
			(void) sa_parse_legacy_options(share,
							tmplist->options,
							tmplist->fstype);
		    }
		    if (tmplist->resource != NULL &&
			strcmp(tmplist->resource, "-") != 0)
			set_node_attr(share, "resource", tmplist->resource);
		    if (tmplist->description != NULL) {
			xmlNodePtr node;
			node = xmlNewChild((xmlNodePtr)share, NULL,
						(xmlChar *)"description", NULL);
			xmlNodeSetContent(node,
					    (xmlChar *)tmplist->description);
		    }
		    legacy = 1;
		}
	    } else {
		/*
		 * if this is a legacy share, mark as shared so we
		 * only update sharetab appropriately.
		 */
		set_node_attr(share, "shared", "true");
	    }
	}
	fs_free_share_list(list);
	return (legacy);
}

/*
 * get the transient shares from the sharetab (or other) file.  since
 * these are transient, they only appear in the working file and not
 * in a repository.
 */
int
gettransients(xmlNodePtr *root)
{
	int legacy = 0;

	if (root != NULL) {
	    if (*root == NULL)
		*root = xmlNewNode(NULL, (xmlChar *)"sharecfg");
	    if (*root != NULL) {
		legacy = parse_sharetab();
	    }
	}
	return (legacy);
}

/*
 * sa_has_prop(optionset, prop)
 *
 * Is the specified property a member of the optionset?
 */

int
sa_has_prop(sa_optionset_t optionset, sa_property_t prop)
{
	char *name;
	sa_property_t otherprop;
	int result = 0;

	if (optionset != NULL) {
	    name = sa_get_property_attr(prop, "type");
	    if (name != NULL) {
		otherprop = sa_get_property(optionset, name);
		if (otherprop != NULL)
		    result = 1;
		sa_free_attr_string(name);
	    }
	}
	return (result);
}

/*
 * Update legacy files
 *
 * Provides functions to add/remove/modify individual entries
 * in dfstab and sharetab
 */

void
update_legacy_config(void)
{
	/*
	 * no longer used -- this is a placeholder in case we need to
	 * add it back later.
	 */
}

/*
 * sa_valid_property(object, proto, property)
 *
 * check to see if the specified property is valid relative to the
 * specified protocol. The protocol plugin is called to do the work.
 */

int
sa_valid_property(void *object, char *proto, sa_property_t property)
{
	int ret = SA_OK;

	if (proto != NULL && property != NULL) {
	    ret = sa_proto_valid_prop(proto, property, object);
	}

	return (ret);
}

/*
 * sa_fstype(path)
 *
 * Given path, return the string representing the path's file system
 * type. This is used to discover ZFS shares.
 */

char *
sa_fstype(char *path)
{
	int err;
	struct stat st;

	err = stat(path, &st);
	if (err < 0) {
	    err = SA_NO_SUCH_PATH;
	} else {
	    err = SA_OK;
	}
	if (err == SA_OK) {
		/* have a valid path at this point */
	    return (strdup(st.st_fstype));
	}
	return (NULL);
}

void
sa_free_fstype(char *type)
{
	free(type);
}

/*
 * sa_get_derived_optionset(object, proto, hier)
 *
 *	Work backward to the top of the share object tree and start
 *	copying protocol specific optionsets into a newly created
 *	optionset that doesn't have a parent (it will be freed
 *	later). This provides for the property inheritence model. That
 *	is, properties closer to the share take precedence over group
 *	level. This also provides for groups of groups in the future.
 */

sa_optionset_t
sa_get_derived_optionset(void *object, char *proto, int hier)
{
	sa_optionset_t newoptionset;
	sa_optionset_t optionset;
	sa_group_t group;

	if (hier &&
	    (group = sa_get_parent_group((sa_share_t)object)) != NULL) {
	    newoptionset = sa_get_derived_optionset((void *)group, proto, hier);
	} else {
	    newoptionset = (sa_optionset_t)xmlNewNode(NULL,
						    (xmlChar *)"optionset");
	    if (newoptionset != NULL) {
		sa_set_optionset_attr(newoptionset, "type", proto);
	    }
	}
	/* dont' do anything if memory wasn't allocated */
	if (newoptionset == NULL)
	    return (NULL);

	/* found the top so working back down the stack */
	optionset = sa_get_optionset((sa_optionset_t)object, proto);
	if (optionset != NULL) {
	    sa_property_t prop;
	    /* add optionset to the newoptionset */
	    for (prop = sa_get_property(optionset, NULL);
		prop != NULL; prop = sa_get_next_property(prop)) {
		sa_property_t newprop;
		char *name;
		char *value;
		name = sa_get_property_attr(prop, "type");
		value = sa_get_property_attr(prop, "value");
		if (name != NULL) {
		    newprop = sa_get_property(newoptionset, name);
		    /* replace the value with the new value */
		    if (newprop != NULL) {
			/*
			 * only set if value is non NULL, old value ok
			 * if it is NULL.
			 */
			if (value != NULL)
			    set_node_attr(newprop, "value", value);
		    } else {
			/* an entirely new property */
			if (value != NULL) {
			    newprop = sa_create_property(name, value);
			    if (newprop != NULL) {
				newprop = (sa_property_t)
				    xmlAddChild((xmlNodePtr)newoptionset,
						(xmlNodePtr)newprop);
			    }
			}
		    }
		    sa_free_attr_string(name);
		}
		if (value != NULL)
		    sa_free_attr_string(value);
	    }
	}
	return (newoptionset);
}

void
sa_free_derived_optionset(sa_optionset_t optionset)
{
	/* while it shouldn't be linked, it doesn't hurt */
	if (optionset != NULL) {
	    xmlUnlinkNode((xmlNodePtr) optionset);
	    xmlFreeNode((xmlNodePtr) optionset);
	}
}

/*
 *  sa_get_all_security_types(object, proto, hier)
 *
 *	find all the security types set for this object.  This is
 *	preliminary to getting a derived security set. The return value is an
 *	optionset containg properties which are the sectype values found by
 *	walking up the XML document struture. The returned optionset
 *	is a derived optionset.
 *
 *	If hier is 0, only look at object. If non-zero, walk up the tree.
 */
sa_optionset_t
sa_get_all_security_types(void *object, char *proto, int hier)
{
	sa_optionset_t options;
	sa_security_t security;
	sa_group_t group;
	sa_property_t prop;

	options = NULL;

	if (hier &&
	    (group = sa_get_parent_group((sa_share_t)object)) != NULL) {
	    options = sa_get_all_security_types((void *)group, proto, hier);
	} else {
	    options = (sa_optionset_t)xmlNewNode(NULL,
						    (xmlChar *)"optionset");
	}
	/* hit the top so collect the security types working back */
	if (options != NULL) {
	    for (security = sa_get_security((sa_group_t)object, NULL, NULL);
		security != NULL; security = sa_get_next_security(security)) {
		char *type;
		char *sectype;

		type = sa_get_security_attr(security, "type");
		if (type != NULL) {
		    if (strcmp(type, proto) != 0) {
			sa_free_attr_string(type);
			continue;
		    }
		    sectype = sa_get_security_attr(security, "sectype");
		    if (sectype != NULL) {
			/*
			 * have a security type, check to see if
			 * already present in optionset and add if it
			 * isn't.
			 */
			if (sa_get_property(options, sectype) == NULL) {
			    prop = sa_create_property(sectype, "true");
			    if (prop != NULL)
				prop = (sa_property_t)
				    xmlAddChild((xmlNodePtr)options,
						(xmlNodePtr)prop);
			}
			sa_free_attr_string(sectype);
		    }
		    sa_free_attr_string(type);
		}
	    }
	}
	return (options);
}

/*
 * sa_get_derived_security(object, sectype, proto, hier)
 *
 * Get the derived security(named optionset) for the object given the
 * sectype and proto. If hier is non-zero, walk up the tree to get all
 * properties defined for this object, otherwise just those on the
 * object.
 */

sa_security_t
sa_get_derived_security(void *object, char *sectype, char *proto, int hier)
{
	sa_security_t newsecurity;
	sa_security_t security;
	sa_group_t group;

	if (hier &&
	    (group = sa_get_parent_group((sa_share_t)object)) != NULL) {
	    newsecurity = sa_get_derived_security((void *)group,
						    sectype, proto, hier);
	} else {
	    newsecurity = (sa_security_t)xmlNewNode(NULL,
						    (xmlChar *)"security");
	    if (newsecurity != NULL) {
		sa_set_security_attr(newsecurity, "type", proto);
		sa_set_security_attr(newsecurity, "sectype", sectype);
	    }
	}
	/* dont' do anything if memory wasn't allocated */
	if (newsecurity == NULL)
	    return (NULL);

	/* found the top so working back down the stack */
	security = sa_get_security((sa_security_t)object, sectype, proto);
	if (security != NULL) {
	    sa_property_t prop;
	    /* add security to the newsecurity */
	    for (prop = sa_get_property(security, NULL);
		prop != NULL; prop = sa_get_next_property(prop)) {
		sa_property_t newprop;
		char *name;
		char *value;
		name = sa_get_property_attr(prop, "type");
		value = sa_get_property_attr(prop, "value");
		if (name != NULL) {
		    newprop = sa_get_property(newsecurity, name);
		    /* replace the value with the new value */
		    if (newprop != NULL) {
			/*
			 * only set if value is non NULL, old value ok
			 * if it is NULL.
			 */
			if (value != NULL)
			    set_node_attr(newprop, name, value);
		    } else {
			/* an entirely new property */
			if (value != NULL) {
			    newprop = sa_create_property(name, value);
			    newprop = (sa_property_t)
				xmlAddChild((xmlNodePtr)newsecurity,
					    (xmlNodePtr)newprop);
			}
		    }
		    sa_free_attr_string(name);
		}
		if (value != NULL)
		    sa_free_attr_string(value);
	    }
	}
	return (newsecurity);
}

void
sa_free_derived_security(sa_security_t security)
{
	/* while it shouldn't be linked, it doesn't hurt */
	if (security != NULL) {
	    xmlUnlinkNode((xmlNodePtr)security);
	    xmlFreeNode((xmlNodePtr)security);
	}
}

/*
 * sharetab utility functions
 *
 * makes use of the original sharetab.c from fs.d/nfs/lib
 */

/*
 * fillshare(share, proto, sh)
 *
 * Fill the struct share with values obtained from the share object.
 */
static void
fillshare(sa_share_t share, char *proto, struct share *sh)
{
	char *groupname = NULL;
	char *value;
	sa_group_t group;
	char *buff;
	char *zfs;

	group = sa_get_parent_group(share);
	if (group != NULL) {
	    zfs = sa_get_group_attr(group, "zfs");
	    groupname = sa_get_group_attr(group, "name");

	    if (groupname != NULL &&
		(strcmp(groupname, "default") == 0 || zfs != NULL)) {
		/*
		 * since the groupname is either "default" or the
		 * group is a ZFS group, we don't want to keep
		 * groupname. We do want it if it is any other type of
		 * group.
		 */
		sa_free_attr_string(groupname);
		groupname = NULL;
	    }
	    if (zfs != NULL)
		sa_free_attr_string(zfs);
	}

	value = sa_get_share_attr(share, "path");
	if (value != NULL) {
	    sh->sh_path = strdup(value);
	    sa_free_attr_string(value);
	}

	value = sa_get_share_attr(share, "resource");
	if (value != NULL || groupname != NULL) {
	    int len = 0;

	    if (value != NULL)
		len += strlen(value);
	    if (groupname != NULL)
		len += strlen(groupname);
	    len += 3; /* worst case */
	    buff = malloc(len);
	    (void) snprintf(buff, len, "%s%s%s",
		    (value != NULL && strlen(value) > 0) ? value : "-",
		    groupname != NULL ? "@" : "",
		    groupname != NULL ? groupname : "");
	    sh->sh_res = buff;
	    if (value != NULL)
		sa_free_attr_string(value);
	    if (groupname != NULL) {
		sa_free_attr_string(groupname);
		groupname = NULL;
	    }
	} else {
	    sh->sh_res = strdup("-");
	}

	sh->sh_fstype = strdup(proto);
	value = sa_proto_legacy_format(proto, share, 1);
	if (value != NULL) {
	    if (strlen(value) > 0)
		sh->sh_opts = strdup(value);
	    else
		sh->sh_opts = strdup("rw");
	    free(value);
	} else
	    sh->sh_opts = strdup("rw");

	value = sa_get_share_description(share);
	if (value != NULL) {
	    sh->sh_descr = strdup(value);
	    sa_free_share_description(value);
	} else
	    sh->sh_descr = strdup("");
}

/*
 * emptyshare(sh)
 *
 * Free the strings in the non-NULL members of sh.
 */

static void
emptyshare(struct share *sh)
{
	if (sh->sh_path != NULL)
	    free(sh->sh_path);
	sh->sh_path = NULL;
	if (sh->sh_res != NULL)
	    free(sh->sh_res);
	sh->sh_res = NULL;
	if (sh->sh_fstype != NULL)
	    free(sh->sh_fstype);
	sh->sh_fstype = NULL;
	if (sh->sh_opts != NULL)
	    free(sh->sh_opts);
	sh->sh_opts = NULL;
	if (sh->sh_descr != NULL)
	    free(sh->sh_descr);
	sh->sh_descr = NULL;
}

/*
 * sa_update_sharetab(share, proto)
 *
 * Update the sharetab file with info from the specified share.
 * This could be an update or add.
 */

int
sa_update_sharetab(sa_share_t share, char *proto)
{
	int ret = SA_OK;
	struct share shtab;
	char *path;
	int logging = 0;
	FILE *sharetab;
	sigset_t old, new;

	path = sa_get_share_attr(share, "path");
	if (path != NULL) {
	    (void) memset(&shtab, '\0', sizeof (shtab));
	    sharetab = fopen(SA_LEGACY_SHARETAB, "r+");
	    if (sharetab == NULL) {
		sharetab = fopen(SA_LEGACY_SHARETAB, "w+");
	    }
	    if (sharetab != NULL) {
		(void) setvbuf(sharetab, NULL, _IOLBF, BUFSIZ * 8);
		(void) sigprocmask(SIG_BLOCK, NULL, &new);
		(void) sigaddset(&new, SIGHUP);
		(void) sigaddset(&new, SIGINT);
		(void) sigaddset(&new, SIGQUIT);
		(void) sigaddset(&new, SIGTSTP);
		(void) sigprocmask(SIG_SETMASK, &new, &old);
		(void) lockf(fileno(sharetab), F_LOCK, 0);
		(void) remshare(sharetab, path, &logging);
		/* fill in share structure and write it out */
		(void) fillshare(share, proto, &shtab);
		(void) putshare(sharetab, &shtab);
		emptyshare(&shtab);
		(void) fflush(sharetab);
		(void) lockf(fileno(sharetab), F_ULOCK, 0);
		(void) fsync(fileno(sharetab));
		(void) sigprocmask(SIG_SETMASK, &old, NULL);
		(void) fclose(sharetab);
	    } else {
		if (errno == EACCES || errno == EPERM) {
		    ret = SA_NO_PERMISSION;
		} else {
		    ret = SA_CONFIG_ERR;
		}
	    }
	    sa_free_attr_string(path);
	}
	return (ret);
}

/*
 * sa_delete_sharetab(path, proto)
 *
 * remove the specified share from sharetab.
 */

int
sa_delete_sharetab(char *path, char *proto)
{
	int ret = SA_OK;
	int logging = 0;
	FILE *sharetab;
	sigset_t old, new;
#ifdef lint
	proto = proto;
#endif

	if (path != NULL) {
	    sharetab = fopen(SA_LEGACY_SHARETAB, "r+");
	    if (sharetab == NULL) {
		sharetab = fopen(SA_LEGACY_SHARETAB, "w+");
	    }
	    if (sharetab != NULL) {
		/* should block keyboard level signals around the lock */
		(void) sigprocmask(SIG_BLOCK, NULL, &new);
		(void) sigaddset(&new, SIGHUP);
		(void) sigaddset(&new, SIGINT);
		(void) sigaddset(&new, SIGQUIT);
		(void) sigaddset(&new, SIGTSTP);
		(void) sigprocmask(SIG_SETMASK, &new, &old);
		(void) lockf(fileno(sharetab), F_LOCK, 0);
		ret = remshare(sharetab, path, &logging);
		(void) fflush(sharetab);
		(void) lockf(fileno(sharetab), F_ULOCK, 0);
		(void) fsync(fileno(sharetab));
		(void) sigprocmask(SIG_SETMASK, &old, NULL);
		(void) fclose(sharetab);
	    } else {
		if (errno == EACCES || errno == EPERM) {
		    ret = SA_NO_PERMISSION;
		} else {
		    ret = SA_CONFIG_ERR;
		}
	    }
	}
	return (ret);
}
