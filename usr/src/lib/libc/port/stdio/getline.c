/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2016 J. Schilling
 */

#include "lint.h"
#include "file64.h"
#include "mtlib.h"
#include <stdio.h>
#include <errno.h>
#include <thread.h>
#include <synch.h>
#include <unistd.h>
#include <limits.h>
#include <malloc.h>
#include <sys/types.h>
#include "stdiom.h"

#define	LINESZ	128	/* initial guess for a NULL *lineptr */

ssize_t
getdelim(char **_RESTRICT_KYWD lineptr, size_t *_RESTRICT_KYWD n,
    int delimiter, FILE *_RESTRICT_KYWD iop)
{
	rmutex_t *lk;
	char *ptr;
	Uchar *bufend;
	char *p;
	size_t size;
	size_t cnt;

	if (lineptr == NULL || n == NULL ||
	    delimiter < 0 || delimiter > UCHAR_MAX) {
		errno = EINVAL;
		return (-1);
	}

	if (*lineptr == NULL || *n < LINESZ) {	/* initial allocation */
		if ((*lineptr = realloc(*lineptr, LINESZ)) == NULL) {
			errno = ENOMEM;
			return (-1);
		}
		*n = LINESZ;
	}
	ptr = *lineptr;
	size = *n;
	cnt = 0;

	FLOCKFILE(lk, iop);

	_SET_ORIENTATION_BYTE(iop);

	if (!(iop->_flag & (_IOREAD | _IORW))) {
		errno = EBADF;
		FUNLOCKFILE(lk);
		return (-1);
	}

	if (iop->_base == NULL) {
		if ((bufend = _findbuf(iop)) == NULL) {
			FUNLOCKFILE(lk);
			return (-1);
		}
	} else {
		bufend = _bufend(iop);
	}

	size--;					/* room for '\0' */
	do {
		size_t	nn;

		if (iop->_cnt <= 0) {		/* empty buffer */
			if (__filbuf(iop) != EOF) {
				iop->_ptr--;	/* put back the character */
				iop->_cnt++;
			} else {
				break;		/* nothing left to read */
			}
		}
		if (cnt >= size) {		/* must reallocate */
			size++;
			if ((ptr = realloc(*lineptr, 2 * size)) == NULL) {
				FUNLOCKFILE(lk);
				ptr = *lineptr + size - 1;
				*ptr = '\0';
				errno = ENOMEM;
				return (-1);
			}
			*lineptr = ptr;
			ptr += size - 1;
			size = (*n = 2 * size) - 1;
		}

		nn = size - cnt;
		nn = (nn < iop->_cnt ? nn : iop->_cnt);
		if ((p = memccpy(ptr, iop->_ptr, delimiter, nn)) != NULL)
			nn = (p - ptr);
		cnt += nn;
		ptr += nn;
		iop->_cnt -= nn;
		iop->_ptr += nn;
		if (_needsync(iop, bufend))
			_bufsync(iop, bufend);
	} while (p == NULL);

	*ptr = '\0';

	FUNLOCKFILE(lk);
	if (cnt > SSIZE_MAX) {
		errno = EOVERFLOW;
		return (-1);
	}
	return (cnt ? cnt : -1);
}

ssize_t
getline(char **_RESTRICT_KYWD lineptr, size_t *_RESTRICT_KYWD n,
    FILE *_RESTRICT_KYWD iop)
{
	return (getdelim(lineptr, n, '\n', iop));
}
