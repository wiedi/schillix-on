/* @(#)stdio.h	1.12 16/09/11 Copyright 2009-2016 J. Schilling */
/*
 *	Abstraction from stdio.h
 *
 *	Copyright (c) 2009-2016 J. Schilling
 */
/*@@C@@*/

#ifndef _SCHILY_STDIO_H
#define	_SCHILY_STDIO_H
#ifndef NO_SCHILY_STDIO_H	/* We #undef _SCHILY_STDIO_H later because */
				/* of the ill designed "hdrchk" program    */

#ifndef	_SCHILY_MCONFIG_H
#include <schily/mconfig.h>
#endif

#ifdef	INCL_MYSTDIO
#ifndef _INCL_MYSTDIO_H
#include <mystdio.h>
#define	_INCL_MYSTDIO_H
#endif

#else	/* INCL_MYSTDIO */

#ifndef _INCL_STDIO_H
#include <stdio.h>
#define	_INCL_STDIO_H
#endif
#endif	/* INCL_MYSTDIO */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef	HAVE_LARGEFILES
/*
 * If HAVE_LARGEFILES is defined, it is guaranteed that fseeko()/ftello()
 * both are available.
 */
#define	fseek	fseeko
#define	ftell	ftello
#else	/* !HAVE_LARGEFILES */

/*
 * If HAVE_LARGEFILES is not defined, we depend on specific tests for
 * fseeko()/ftello() which must have been done before the tests for
 * Large File support have been done.
 * Note that this only works if the tests used below are really done before
 * the Large File autoconf test is run. This is because autoconf does no
 * clean testing but instead cumulatively modifes the envivonment used for
 * testing.
 */
#ifdef	HAVE_FSEEKO
#	define	fseek	fseeko
#endif
#ifdef	HAVE_FTELLO
#	define	ftell	ftello
#endif
#endif

#if	!defined(HAVE_POPEN) && defined(HAVE__POPEN)
#define	popen(c, m)	_popen((c), (m))
#endif

#if	!defined(HAVE_PCLOSE) && defined(HAVE__PCLOSE)
#define	pclose(fp)	_pclose(fp)
#endif

#ifdef	FAST_GETC_PUTC
/*
 * The following code partially allows libschily to access FILE * as fast as
 * from inside libc on Solaris.
 * This makes it possible to implement js_printf() from libschily aprox.
 * 33% faster than printf() from libc on Solaris. To do this, we
 * partially unhide the FILE structure in a 64 bit environment on Solaris
 * to allow to run putc_unlocked() as a marcro.
 *
 * If you believe you can do this on onther platforms, send a note.
 */
#if	defined(__SVR4) && defined(__sun) && defined(_LP64)

/*
 * This is how the 64 bit FILE * begins on Solaris.
 */
struct SCHILY__FILE_TAG {
	unsigned char	*_ptr;	/* next character from/to here in buffer */
	unsigned char	*_base;	/* the buffer */
	unsigned char	*_end;	/* the end of the buffer */
	ssize_t		_cnt;	/* number of available characters in buffer */
};

#define	__putc_unlocked(x, p)	(--(p)->_cnt < 0 \
					? __flsbuf((x), (FILE *)(p)) \
					: (int)(*(p)->_ptr++ = \
					(unsigned char) (x)))

#define	putc_unlocked(x, p)	__putc_unlocked(x, (struct SCHILY__FILE_TAG *)p)

extern int	__flsbuf __PR((int, FILE *));

#endif	/* defined(__SVR4) && defined(__sun) && defined(_LP64) */
#endif	/* FAST_GETC_PUTC */

#ifdef __cplusplus
}
#endif

#else	/* !NO_SCHILY_STDIO_H */
#undef	_SCHILY_STDIO_H			/* #undef here to pass "hdrchk" */
#endif	/* NO_SCHILY_STDIO_H */
#endif	/* _SCHILY_STDIO_H */
