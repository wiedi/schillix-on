
/* : : generated by proto : : */
/* : : generated from /home/gisburn/ksh93/ast_ksh_20081104/build_sparc_64bit/src/lib/libast/features/sig.sh by iffe version 2008-01-31 : : */
#ifndef _def_sig_ast
#if !defined(__PROTO__)
#  if defined(__STDC__) || defined(__cplusplus) || defined(_proto) || defined(c_plusplus)
#    if defined(__cplusplus)
#      define __LINKAGE__	"C"
#    else
#      define __LINKAGE__
#    endif
#    define __STDARG__
#    define __PROTO__(x)	x
#    define __OTORP__(x)
#    define __PARAM__(n,o)	n
#    if !defined(__STDC__) && !defined(__cplusplus)
#      if !defined(c_plusplus)
#      	define const
#      endif
#      define signed
#      define void		int
#      define volatile
#      define __V_		char
#    else
#      define __V_		void
#    endif
#  else
#    define __PROTO__(x)	()
#    define __OTORP__(x)	x
#    define __PARAM__(n,o)	o
#    define __LINKAGE__
#    define __V_		char
#    define const
#    define signed
#    define void		int
#    define volatile
#  endif
#  define __MANGLE__	__LINKAGE__
#  if defined(__cplusplus) || defined(c_plusplus)
#    define __VARARG__	...
#  else
#    define __VARARG__
#  endif
#  if defined(__STDARG__)
#    define __VA_START__(p,a)	va_start(p,a)
#  else
#    define __VA_START__(p,a)	va_start(p)
#  endif
#  if !defined(__INLINE__)
#    if defined(__cplusplus)
#      define __INLINE__	extern __MANGLE__ inline
#    else
#      if defined(_WIN32) && !defined(__GNUC__)
#      	define __INLINE__	__inline
#      endif
#    endif
#  endif
#endif
#if !defined(__LINKAGE__)
#define __LINKAGE__		/* 2004-08-11 transition */
#endif

#define _def_sig_ast	1
#define _sys_types	1	/* #include <sys/types.h> ok */
                  
#define sig_info	_sig_info_

#if defined(__STDPP__directive) && defined(__STDPP__hide)
__STDPP__directive pragma pp:hide kill killpg
#else
#define kill	______kill
#define killpg	______killpg
#endif
#include <signal.h>
#if defined(__STDPP__directive) && defined(__STDPP__hide)
__STDPP__directive pragma pp:nohide kill killpg
#else
#undef	kill
#undef	killpg
#endif
#ifndef sigmask
#define sigmask(s)	(1<<((s)-1))
#endif
typedef void (*Sig_handler_t) __PROTO__((int));


#define Handler_t		Sig_handler_t

#define SIG_REG_PENDING		(-1)
#define SIG_REG_POP		0
#define SIG_REG_EXEC		00001
#define SIG_REG_PROC		00002
#define SIG_REG_TERM		00004
#define SIG_REG_ALL		00777
#define SIG_REG_SET		01000

typedef struct
{
	char**		name;
	char**		text;
	int		sigmax;
} Sig_info_t;

extern __MANGLE__ int		kill __PROTO__((pid_t, int));
extern __MANGLE__ int		killpg __PROTO__((pid_t, int));

#if _BLD_ast && defined(__EXPORT__)
#undef __MANGLE__
#define __MANGLE__ __LINKAGE__ __EXPORT__
#endif
#if !_BLD_ast && defined(__IMPORT__)
#undef __MANGLE__
#define __MANGLE__ __LINKAGE__ __IMPORT__
#endif

extern __MANGLE__ Sig_info_t	sig_info;

#undef __MANGLE__
#define __MANGLE__ __LINKAGE__

#if _BLD_ast && defined(__EXPORT__)
#undef __MANGLE__
#define __MANGLE__ __LINKAGE__		__EXPORT__
#endif

extern __MANGLE__ int		sigcritical __PROTO__((int));
extern __MANGLE__ int		sigunblock __PROTO__((int));

#undef __MANGLE__
#define __MANGLE__ __LINKAGE__
#endif
