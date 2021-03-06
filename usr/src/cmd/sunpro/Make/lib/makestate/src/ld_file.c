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
 * Copyright 1998 Sun Microsystems, Inc. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * @(#)ld_file.c 1.7 06/12/12
 */
#pragma ident	"@(#)ld_file.c	1.7	06/12/12"

/*
 * This file contains modifications Copyright 2017 J. Schilling
 *
 * @(#)ld_file.c	1.6 17/04/25 2017 J. Schilling
 */
#include <schily/mconfig.h>
#ifndef lint
static	UConst char sccsid[] =
	"@(#)ld_file.c	1.6 17/04/25 2017 J. Schilling";
#endif

#pragma init(ld_support_init)

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#ifdef	HAVE_LIBELF_H
#include <libelf.h>
#endif
#include <sys/param.h>
#ifdef	HAVE_LINK_H
#include <link.h>
#endif

#if defined(HAVE_LIBELF_H) && defined(LD_SUP_DERIVED)
/*
 * FreeBSD has libelf.h and link.h but link.h does not
 * #define LD_SUP_DERIVED.
 */
#define	SUNPRO_DEPENDENCIES	"SUNPRO_DEPENDENCIES"

/*
 * Linked list of strings - used to keep lists of names
 * of directories or files.
 */

struct Stritem {
	char	*str;
	void	*next;
};

typedef struct Stritem 	Stritem;

static char 		*depend_file = NULL;
static Stritem		*list = NULL;

	void mk_state_init		__PR((void));
static	void prepend_str		__PR((Stritem **listpp,
						const char *str));
	void mk_state_collect_dep	__PR((const char *file));
	void mk_state_update_exit	__PR((void));
static	void ld_support_init		__PR((void));
	void ld_file			__PR((const char *file,
						const Elf_Kind ekind,
						int flags, Elf *elf));
	void ld_atexit			__PR((int exit_code));
	void ld_file64			__PR((const char *file,
						const Elf_Kind ekind,
						int flags, Elf *elf));
	void ld_atexit64		__PR((int exit_code));

void
mk_state_init()
{
	depend_file = getenv(SUNPRO_DEPENDENCIES);
} /* mk_state_init() */



static void
prepend_str(listpp, str)
	Stritem		**listpp;
	const char	*str;
{
	Stritem	*new;
	char 	*newstr;

	if (!(new = calloc(1, sizeof (Stritem)))) {
		perror("libmakestate.so");
		return;
	} /* if */

	if (!(newstr = malloc(strlen(str) + 1))) {
		perror("libmakestate.so");
		return;
	} /* if */

	new->str = strcpy(newstr, str);
	new->next = *listpp;
	*listpp = new;

} /* prepend_str() */


void
mk_state_collect_dep(file)
	const char	*file;
{
	/*
	 * SUNPRO_DEPENDENCIES wasn't set, we don't collect .make.state
	 * information.
	 */
	if (!depend_file)
		return;

	prepend_str(&list, file);

}  /* mk_state_collect_dep() */


void
mk_state_update_exit()
{
	Stritem 	*cur;
	char		lockfile[MAXPATHLEN],	*err, *space, *target;
	FILE		*ofp;
	extern char 	*file_lock(char *, char *, int);

	if (!depend_file)
		return;

	if ((space = strchr(depend_file, ' ')) == NULL)
		return;
	*space = '\0';
	target = &space[1];

	(void) sprintf(lockfile, "%s.lock", depend_file);
	if ((err = file_lock(depend_file, lockfile, 0))) {
		(void) fprintf(stderr, "%s\n", err);
		return;
	} /* if */

	if (!(ofp = fopen(depend_file, "a")))
		return;

	if (list)
		(void) fprintf(ofp, "%s: ", target);

	for (cur = list; cur; cur = cur->next)
		(void) fprintf(ofp, " %s", cur->str);

	(void) fputc('\n', ofp);

	(void) fclose(ofp);
	(void) unlink(lockfile);
	*space = ' ';

} /* mk_state_update_exit() */

static void
/* LINTED static unused */
ld_support_init()
{
	mk_state_init();

} /* ld_support_init() */

/* ARGSUSED */
void
ld_file(file, ekind, flags, elf)
	const char	*file;
	const Elf_Kind	ekind;
	int		flags;
	Elf		*elf;
{
	if (! ((flags & LD_SUP_DERIVED) && !(flags & LD_SUP_EXTRACTED)))
		return;

	mk_state_collect_dep(file);

} /* ld_file */

void
ld_atexit(exit_code)
	int	exit_code;
{
	if (exit_code)
		return;

	mk_state_update_exit();

} /* ld_atexit() */

/*
 * Supporting 64-bit objects
 */
void
ld_file64(file, ekind, flags, elf)
	const char	*file;
	const Elf_Kind	ekind;
	int		flags;
	Elf		*elf;
{
	if (! ((flags & LD_SUP_DERIVED) && !(flags & LD_SUP_EXTRACTED)))
		return;

	mk_state_collect_dep(file);

} /* ld_file64 */

void
ld_atexit64(exit_code)
	int	exit_code;
{
	if (exit_code)
		return;

	mk_state_update_exit();

} /* ld_atexit64() */
#endif	/* HAVE_LIBELF_H */
