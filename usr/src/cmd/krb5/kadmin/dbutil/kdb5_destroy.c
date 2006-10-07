/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 *
 *	Openvision retains the copyright to derivative works of
 *	this source code.  Do *NOT* create a derivative of this
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
 * admin/destroy/kdb5_destroy.c
 *
 * Copyright 1990 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 * 
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 * 
 *
 * kdb_dest(roy): destroy the named database.
 *
 * This version knows about DBM format databases.
 */

#define KDB5_DISPATCH
#define KRB5_KDB5_DBM__
#include <k5-int.h>
/* #define these to avoid an indirection function; for future implementations,
   these may be redirected from a dispatch table/routine */
#define krb5_dbm_db_set_name krb5_db_set_name
#define krb5_dbm_db_set_nonblocking krb5_db_set_nonblocking
#define krb5_dbm_db_init krb5_db_init
#define krb5_dbm_db_get_age krb5_db_get_age
#define krb5_dbm_db_create krb5_db_create
#define krb5_dbm_db_rename krb5_db_rename
#define krb5_dbm_db_get_principal krb5_db_get_principal
#define krb5_dbm_db_free_principal krb5_db_free_principal
#define krb5_dbm_db_put_principal krb5_db_put_principal
#define krb5_dbm_db_delete_principal krb5_db_delete_principal
#define krb5_dbm_db_lock krb5_db_lock
#define krb5_dbm_db_unlock krb5_db_unlock
#define krb5_dbm_db_set_lockmode krb5_db_set_lockmode
#define krb5_dbm_db_close_database krb5_db_close_database
#define krb5_dbm_db_open_database krb5_db_open_database

#include <stdio.h>
#include "com_err.h"
#include <kadm5/admin.h>
#include <kadm5/adb.h>
#include <libintl.h>
#include "kdb5_util.h"

extern int exit_status;
extern krb5_boolean dbactive;
extern kadm5_config_params global_params;


void
kdb5_destroy(argc, argv)
    int argc;
    char *argv[];
{
    extern char *optarg;
    extern int optind;
    int optchar;
    char *dbname;
    char buf[5];
    krb5_error_code retval, retval1, retval2;
    krb5_context context;
    int force = 0;
    char ufilename[MAX_FILENAME];

    krb5_init_context(&context);

    if (strrchr(argv[0], '/'))
	argv[0] = strrchr(argv[0], '/')+1;

    dbname = global_params.dbname;

    optind = 1;
    while ((optchar = getopt(argc, argv, "f")) != -1) {
	switch(optchar) {
	case 'f':
	    force++;
	    break;
	case '?':
	default:
	    usage();
	    return;
	    /*NOTREACHED*/
	}
    }
    if (!force) {
	printf(gettext("Deleting KDC database stored in '%s', "
		"are you sure?\n"), dbname);
	printf(gettext("(type 'yes' or 'y' to confirm)? "));
	if (fgets(buf, sizeof(buf), stdin) == NULL) {
	    exit_status++; return;
        }
	if ((strncmp(buf, gettext("yes\n"),
	 	strlen(gettext("yes\n"))) != 0) && 
	(strncmp(buf, gettext("y\n"),
		strlen(gettext("y\n"))) != 0)) {
	printf(gettext("database not deleted !! '%s'...\n"),
		dbname);

	    exit_status++; return;
        }
	printf(gettext("OK, deleting database '%s'...\n"), dbname);
    }

    retval = krb5_db_set_name(context, dbname);
    if (retval) {
	com_err(argv[0], retval, "'%s'",dbname);
	exit_status++; return;
    }
    retval1 = krb5_db_destroy(context, dbname);

    /* check for a stash file and delete it if necessary */
    if (global_params.stash_file == NULL) {
	char stash[MAXPATHLEN+1];
	extern krb5_principal master_princ;
	krb5_data *realm = krb5_princ_realm(context, master_princ);
	(void) strlcpy(stash, DEFAULT_KEYFILE_STUB, sizeof (stash)); 
	/*
	 * realm->data is not necessarily NULL terminated so be
	 * careful how much data is copied here.  Don't overrun
	 * the "stash" buffer and dont overrun the realm->data buffer,
	 * copy the smaller of the 2 lengths.
	 */
	(void) strncat(stash, realm->data,
		(realm->length < (MAXPATHLEN-strlen(stash)) ? realm->length :
		MAXPATHLEN-strlen(stash)));
	global_params.stash_file = (char *)strdup(stash);
    }
    if (!access(global_params.stash_file, F_OK))
	(void)unlink(global_params.stash_file);

    retval2 = osa_adb_destroy_policy_db(&global_params);
    if (retval1) {
		com_err(argv[0], retval1,
			gettext("deleting database '%s'"), dbname);
	exit_status++; return;
    }
    if (retval2) {
		com_err(argv[0], retval2,
			gettext("destroying policy database"));
	 exit_status++; return;
    }

    if (global_params.iprop_enabled) {
	if (strlcpy(ufilename, dbname, MAX_FILENAME) >= MAX_FILENAME) {
		exit_status++;
		return;
	}
	if (strlcat(ufilename, ".ulog", MAX_FILENAME) >= MAX_FILENAME) {
		exit_status++;
		return;
	}

	(void) unlink(ufilename);
    }

    dbactive = FALSE;
    printf(gettext("** Database '%s' destroyed.\n"), dbname);
    return;
}
