%{
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
 *
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma	ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/acl.h>
#include <aclutils.h>

extern int yyinteractive;
extern acl_t *yyacl;
%}


%union {
	char *str;
	int val;
	struct acl_perm_type acl_perm;
	ace_t ace;
	aclent_t aclent;
	acl_t *acl;
}


%token USER_TOK GROUP_TOK MASK_TOK OTHER_TOK OWNERAT_TOK 
%token GROUPAT_TOK EVERYONEAT_TOK DEFAULT_USER_TOK DEFAULT_GROUP_TOK
%token DEFAULT_MASK_TOK DEFAULT_OTHER_TOK COLON COMMA NL SLASH 
%token <str> IDNAME PERM_TOK INHERIT_TOK
%token <val> ID ERROR ACE_PERM ACE_INHERIT ENTRY_TYPE ACCESS_TYPE

%type <str> idname
%type <acl_perm> perms perm aclent_perm ace_perms
%type <acl> acl_entry
%type <ace> ace 
%type <aclent> aclent
%type <val> iflags verbose_iflag compact_iflag access_type id entry_type

%left ERROR COLON

%%

acl:	acl_entry NL 
	{ 
		yyacl = $1;
		return (0);
	} 

	/* This seems illegal, but the old aclfromtext() allows it */
	| acl_entry COMMA NL	
	{
		yyacl = $1;
		return (0);
	}
	| acl_entry COMMA acl 
	{ 
		yyacl = $1;
		return (0);
	}
	
acl_entry: ace 
	{
		ace_t *acep;

		if (yyacl == NULL) {
			yyacl = acl_alloc(ACE_T);
			if (yyacl == NULL) 
				return (EACL_MEM_ERROR);
		} 

		$$ = yyacl;
		if ($$->acl_type == ACLENT_T) {
			acl_error(gettext("Cannot have POSIX draft ACL entries"
			     " with NFSv4/ZFS ACL entries.\n"));
			acl_free(yyacl);
			yyacl = NULL;
			return (EACL_DIFF_TYPE);
		}
			
		$$->acl_aclp = realloc($$->acl_aclp,
		    ($$->acl_entry_size * ($$->acl_cnt + 1)));
		if ($$->acl_aclp == NULL) {
			free (yyacl);
			return (EACL_MEM_ERROR);	
		}
		acep = $$->acl_aclp;
		acep[$$->acl_cnt] = $1;
		$$->acl_cnt++;
	}
	| aclent
	{
		aclent_t *aclent;

		if (yyacl == NULL) {
			yyacl = acl_alloc(ACLENT_T);
			if (yyacl == NULL) 
				return (EACL_MEM_ERROR);
		} 

		$$ = yyacl;
		if ($$->acl_type == ACE_T) {
			acl_error(gettext("Cannot have NFSv4/ZFS ACL entries"
			     " with POSIX draft ACL entries.\n"));
			acl_free(yyacl);
			yyacl = NULL;
			return (EACL_DIFF_TYPE);
		}

		$$->acl_aclp = realloc($$->acl_aclp,
		    ($$->acl_entry_size  * ($$->acl_cnt +1)));
		if ($$->acl_aclp == NULL) {
			free (yyacl);
			return (EACL_MEM_ERROR);	
		}
		aclent = $$->acl_aclp;
		aclent[$$->acl_cnt] = $1;
		$$->acl_cnt++;
	}

ace:	entry_type idname ace_perms access_type
	{
		int error;
		int id;
		int mask;

		error = get_id($1, $2, &id);
		if (error) {
			acl_error(gettext("Invalid user %s specified.\n"), $2);
			free($2);
			return (EACL_INVALID_USER_GROUP);
		}
			
		$$.a_who = id;
		$$.a_flags = ace_entry_type($1);
		free($2);
		error = ace_perm_mask(&$3, &$$.a_access_mask);
		if (error)
			return (error);
		$$.a_type = $4;

	}
	| entry_type idname ace_perms access_type COLON id
	{
		int error;
		int id;

		if (yyinteractive) {
			acl_error(gettext("Extra fields on the end of "
			    "ACL specification.\n"));
			return (EACL_UNKNOWN_DATA);
		}
		error = get_id($1, $2, &id);
		if (error) {
			$$.a_who = $6;
		} else {
			$$.a_who = id;
		}
		$$.a_flags = ace_entry_type($1);
		free($2);
		error = ace_perm_mask(&$3, &$$.a_access_mask);
		if (error)
			return (error);
		$$.a_type = $4;
	}
	| entry_type idname ace_perms iflags access_type 
	{
		int error;
		int id;

		error = get_id($1, $2, &id);
		if (error) {
			acl_error(gettext("Invalid user %s specified.\n"), $2);
			free($2);
			return (EACL_INVALID_USER_GROUP);
		}
		
		$$.a_who = id;
		$$.a_flags = ace_entry_type($1);
		free($2);
		error = ace_perm_mask(&$3, &$$.a_access_mask);
		if (error)
			return (error);
		$$.a_type = $5;
		$$.a_flags |= $4;
	}
	| entry_type idname ace_perms iflags access_type COLON id
	{
		int error;
		int  id;

		if (yyinteractive) {
			acl_error(gettext("Extra fields on the end of "
			    "ACL specification.\n"));
			return (EACL_UNKNOWN_DATA);
		}
		error = get_id($1, $2, &id);
		if (error) {
			$$.a_who = $7;
		} else {
			$$.a_who = id;
		}

		$$.a_flags = ace_entry_type($1);
		free($2);
		error = ace_perm_mask(&$3, &$$.a_access_mask);
		if (error)
			return (error);

		$$.a_type = $5;
		$$.a_flags |= $4;
	}
	| entry_type ace_perms access_type
	{ 
		int error;

		$$.a_who = -1;
		$$.a_flags = ace_entry_type($1);
		error = ace_perm_mask(&$2, &$$.a_access_mask);
		if (error) {
			return (error);
		}
		$$.a_type = $3;
	} 
	| entry_type ace_perms access_type COLON id
	{
		if (yyinteractive) {
			acl_error(gettext("Extra fields on the end of "
			    "ACL specification.\n"));
			return (EACL_UNKNOWN_DATA);
		}

		return (EACL_ENTRY_ERROR);
	}
	| entry_type ace_perms iflags access_type 
	{
		int error;

		$$.a_who = -1;
		$$.a_flags = ace_entry_type($1);
		error = ace_perm_mask(&$2, &$$.a_access_mask);
		if (error)
			return (error);
		$$.a_type = $4;
		$$.a_flags |= $3;

	}
	| entry_type ace_perms iflags access_type COLON id
	{
		if (yyinteractive) {
			acl_error(gettext("Extra fields on the end of "
			    "ACL specification.\n"));
			return (EACL_UNKNOWN_DATA);
		}
		return (EACL_ENTRY_ERROR);
	}

aclent: entry_type idname aclent_perm	/* user or group */
	{
		int error;
		int id;

		error = get_id($1, $2, &id);
		if (error) {
			acl_error(gettext("Invalid user '%s' specified.\n"),
			    $2);
			free($2);
			return (EACL_INVALID_USER_GROUP);
		}

		error = compute_aclent_perms($3.perm_str, &$$.a_perm);
		if (error) {
			free($2);
			acl_error(gettext(
			    "Invalid permission(s) '%s' specified.\n"),
			    $3.perm_str);
			return (error);
		}
		$$.a_id = id;
		error = aclent_entry_type($1, 0, &$$.a_type);
		free($2);
		if (error) {
			acl_error(
			    gettext("Invalid ACL entry type '%s' "
			    "specified.\n"), $1);
			return (error);
		}
	}
	| entry_type COLON aclent_perm		/* owner group other */
	{
		int error;

		error = compute_aclent_perms($3.perm_str, &$$.a_perm);
		if (error) {
			acl_error(gettext(
			    "Invalid permission(s) '%s' specified.\n"),
			    $3.perm_str);
			return (error);
		}
		$$.a_id = -1;
		error = aclent_entry_type($1, 1, &$$.a_type);
		if (error) {
			acl_error(
			    gettext("Invalid ACL entry type '%s' specified.\n"),
			    $1);
			return (error);
		}
	}
	| entry_type COLON aclent_perm COLON id
	{ 
		if (yyinteractive) {
			acl_error(gettext("Extra fields on the end of "
			    "ACL specification.\n"));
			return (EACL_UNKNOWN_DATA);
		}
		return (EACL_ENTRY_ERROR);
	}
	| entry_type idname aclent_perm COLON id 	/* user or group */
	{	
		int error;
		int id;

		if (yyinteractive) {
			acl_error(gettext("Extra fields on the end of "
			    "ACL specification.\n"));
			return (EACL_UNKNOWN_DATA);
		}
		error = compute_aclent_perms($3.perm_str, &$$.a_perm);
		if (error) {
			free($2);
			acl_error(gettext(
			    "Invalid permission(s) '%s' specified.\n"),
			    $3.perm_str);
			return (error);
		}
		error = get_id($1, $2, &id);
		if (error)
			$$.a_id = $5;	
		else 
			$$.a_id = id;

		error = aclent_entry_type($1, 0, &$$.a_type);
		free($2);
		if (error) {
			acl_error(
			    gettext("Invalid ACL entry type '%s' specified.\n"),
			    $1);
			return (error);
		}
	}
	| entry_type aclent_perm  /* mask entry */
	{
		int error;

		error = compute_aclent_perms($2.perm_str, &$$.a_perm);
		if (error) {
			acl_error(gettext(
			    "Invalid permission(s) '%s' specified.\n"),
			    $2.perm_str);
			return (error);
		}
		$$.a_id = -1;
		error = aclent_entry_type($1, 0, &$$.a_type);
		if (error) {
			acl_error(
			    gettext("Invalid ACL entry type specified %d.\n"),
			    error);
			return (error);
		}
	}
	| entry_type aclent_perm COLON id
	{
		if (yyinteractive) {
			acl_error(gettext("Extra fields on the end of "
			    "ACL specification.\n"));
			return (EACL_UNKNOWN_DATA);
		}
		return (EACL_ENTRY_ERROR);
	}

iflags: compact_iflag COLON {$$ = $1;}
	| verbose_iflag COLON {$$ = $1;}
	| COLON {$$ = 0;}

compact_iflag : INHERIT_TOK
	{
		int error;
		uint32_t iflags;

		error = compute_ace_inherit($1, &iflags);
		if (error) {
			acl_error(gettext("Invalid inheritance flags "
			    "'%s' specified.\n"), $1);
			free($1);
			return (error);
		}
		$$ = iflags;
	}
	| INHERIT_TOK SLASH verbose_iflag
	{
		acl_error(gettext("Can't mix compact inherit flags with"
		    " verbose inheritance flags.\n"));
		return (EACL_INHERIT_ERROR);
	}

verbose_iflag: ACE_INHERIT	{$$ |= $1;}
	| ACE_INHERIT SLASH verbose_iflag {$$ = $1 | $3;}
	| ACE_INHERIT SLASH compact_iflag
	{
		acl_error(gettext("Can't mix verbose inherit flags with"
		    " compact inheritance flags.\n"));
		return (EACL_INHERIT_ERROR);
	}
	| ACE_INHERIT SLASH ACCESS_TYPE
	{
		acl_error(gettext("Inheritance flags can't be mixed with"
		    " access type.\n"));
		return (EACL_INHERIT_ERROR);
	}
	| ACE_INHERIT SLASH ERROR {return ($3);}
	
aclent_perm: PERM_TOK
	{
		$$.perm_style = PERM_TYPE_UNKNOWN;
		$$.perm_str = $1;
		$$.perm_val = 0;
	}
	| PERM_TOK ERROR 
	{
		acl_error(gettext("ACL entry permissions are incorrectly "
		    "specified.\n"));
		return ($2);
	}

access_type: ACCESS_TYPE {$$ = $1;}	
	| ERROR {return ($1);}

id: ID {$$ = $1;}
  	| COLON
	{
		acl_error(gettext("Invalid uid/gid specified.\nThe field"
		    " should be a numeric value.\n")); 
		return (EACL_UNKNOWN_DATA);
	}
	| ERROR {return ($1);}

ace_perms: perm {$$ = $1;}
	| aclent_perm COLON {$$ = $1;}
	| ERROR {return ($1);}

perm: perms COLON {$$ = $1;}
    	| COLON {$$.perm_style = PERM_TYPE_EMPTY;}

perms: ACE_PERM 
     	{
		$$.perm_style = PERM_TYPE_ACE;
		$$.perm_val |= $1;
	}
	| ACE_PERM SLASH perms
	{
		$$.perm_style = PERM_TYPE_ACE;
		$$.perm_val = $1 | $3.perm_val;
	}
	| ACE_PERM SLASH aclent_perm
	{

		acl_error(gettext("Can't mix verbose permissions with"
		    " compact permission.\n"));
		return (EACL_PERM_MASK_ERROR);

	}
	| ACE_PERM SLASH ERROR {return ($3);}
		

idname: IDNAME {$$ = $1;}

entry_type: ENTRY_TYPE {$$ = $1;}
	| ERROR {return ($1);}
