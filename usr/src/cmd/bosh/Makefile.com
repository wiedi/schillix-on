#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License, Version 1.0 only
# (the "License").  You may not use this file except in compliance
# with the License.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.opensource.org/licenses/cddl1.txt
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright 2013-2016 J. Schilling
# Use is subject to license terms.
#

PROG =		bosh

OBJS=	abbrev.o alias.o args.o bltin.o cmd.o ctype.o defs.o \
	echo.o error.o expand.o fault.o func.o gmatch.o hash.o hashserv.o \
	io.o jobs.o macro.o main.o msg.o name.o print.o pwd.o \
	service.o sh_policy.o signames.o stak.o string.o \
	strexpr.o test.o ulimit.o umask.o word.o xec.o \
	builtin.o find.o \
	hashcmd.o hashtab.o \
	optget.o printf.o 

SRCS=	$(OBJS:%.o=../%.c)

#
# The files: abbrev.c, abbrev.h, signames.c
# have been borrowed from "bsh"
#


include ../../Makefile.cmd

#
# for message catalogs
#
POFILE= ../bosh.po
POFILES= $(SRCS:%.c=%.po)
XGETFLAGS += -a -x ../bosh.xcl

C99MODE=	$(C99_ENABLE)

#
# Code that was in ../Makefile.cmd:
#
i386_SPFLAG=	-D_iBCS2
sparc_SPFLAG=
iBCS2FLAG =	$($(MACH)_SPFLAG)

#
# Enhanced/New #defines:
#
CPPFLAGS +=	-DVSHNAME='"bosh"'	# Shell name variant (bosh/osh/pbosh)

CPPFLAGS +=	-DSCHILY_INCLUDES	# Tell the code to use schily include files
CPPFLAGS +=	-DBOURNE_SHELL		# Tell the code that we compile for sh/bosh
CPPFLAGS +=	-DUSE_LARGEFILES	# Allow Large Files (> 2 GB)
CPPFLAGS +=	-DUSE_NLS		# Enable NLS support in include/schily/*.h
CPPFLAGS +=	-DUSE_JS_BOOL		# Allow to use schily/dbgmalloc.h
#CPPFLAGS +=	-DNO_LOCALE		# Don't use setlocale()
#CPPFLAGS +=	-DNO_WCHAR		# Don't use wide chars
#CPPFLAGS +=	-DNO_VFORK		# Don't use vfork()
#CPPFLAGS +=	-DNO_WAITID		# Don't use waitid()
CPPFLAGS +=	-DDO_SPLIT_ROOT		# No setlocale() without localedir

CPPFLAGS +=	-DDO_SHRCFILES		# Enable rcfiles "/etc/sh.shrc" "$HOME/.shrc"
CPPFLAGS +=	-DDO_HASHCMDS		# Include hash cmds (line starts with #)
CPPFLAGS +=	-DDO_HOSTPROMPT		# Set PS1 to "hostname uname> "
CPPFLAGS +=	-DDO_SYSALIAS		# Include alias/unalias builtin
CPPFLAGS +=	-DDO_GLOBALALIASES	# Include persistent aliases in ~/.globals 
CPPFLAGS +=	-DDO_LOCALALIASES	# Include persistent aliases in .locals 
CPPFLAGS +=	-DDO_SYSALLOC		# Include the "alloc" debug builtin
CPPFLAGS +=	-DDO_SYSREPEAT		# Include the "repeat" builtin
CPPFLAGS +=	-DDO_SYSDOSH		# Include the "dosh" builtin
CPPFLAGS +=	-DDO_SYSPUSHD		# Include pushd / popd / dirs builtin && cd -
CPPFLAGS +=	-DDO_SYSTRUE		# Include true / false builtin
CPPFLAGS +=	-DDO_ECHO_A		# Include support for echo \a
CPPFLAGS +=	-DDO_EXEC_AC		# Include support for exec -c -a
CPPFLAGS +=	-DDO_READ_R		# Include support for read -r
CPPFLAGS +=	-DDO_SET_O		# Include support for set -o
CPPFLAGS +=	-DDO_MULTI_OPT		# Include support for sh -v -x / set -v -x
CPPFLAGS +=	-DDO_MONITOR_SCRIPT	# Allow to set jobconrol in shell scripts
CPPFLAGS +=	-DDO_UMASK_S		# Include support for umask -S
CPPFLAGS +=	-DDO_CHECKBINARY	# Check scripts for binary (\0 before \n)
CPPFLAGS +=	-DDO_GETOPT_LONGONLY	# Include support for getopts "?900?(lo)"
CPPFLAGS +=	-DDO_GETOPT_POSIX	# Fail: $OPTARG has optopt for optstr[0] = ':'
CPPFLAGS +=	-DDO_GETOPT_PLUS	# Support +o if optstr[0] = '+'
CPPFLAGS +=	-DDO_GETOPT_UTILS	# Include support for -- in all builtins
CPPFLAGS +=	-DDO_POSIX_FOR		# Support for i; do .... with semicolon
CPPFLAGS +=	-DDO_POSIX_CASE		# Support for POSIX case with _(_ pat )
CPPFLAGS +=	-DDO_FALLTHR_CASE	# Support for fallthrough case with ;&
CPPFLAGS +=	-DDO_POSIX_GMATCH	# Support for POSIX [:alpha:] ...
#CPPFLAGS +=	-DGMATCH_CLERR_NORM	# Handle glob class error as normal char
CPPFLAGS +=	-DDO_POSIX_TYPE		# Report keywords as well
CPPFLAGS +=	-DDO_PIPE_SEMI_SYNTAX_E	# Report a syntax error for "echo foo |;"
CPPFLAGS +=	-DDO_PIPE_SYNTAX_E	# Report a syntax error for "echo foo |"
CPPFLAGS +=	-DDO_EMPTY_SEMI		# Permit ";" and ";echo" as valid commands
CPPFLAGS +=	-DDO_PIPE_PARENT	# Optimized pipes: Shell always parent
CPPFLAGS +=	-DDO_SETIO_NOFORK	# Avoid to fork w. redir. IO in compound cmd
CPPFLAGS +=	-DDO_ALLEXPORT		# Bugfix for set -a; read VAR / getopts
CPPFLAGS +=	-DDO_O_APPEND		# Support O_APPEND instead of lseek() for >>
CPPFLAGS +=	-DDO_EXPAND_DIRSLASH	# Expand dir*/ to dir/
CPPFLAGS +=	-DDO_GLOBSKIPDOT	# Implement set -o globskipdot, skip . ..
CPPFLAGS +=	-DDO_GLOBSKIPDOT_DEF	# Implement set -o globskipdot as default
CPPFLAGS +=	-DDO_SIGNED_EXIT	# Allow negative exit(1) parameters
CPPFLAGS +=	-DDO_DOL_SLASH		# Include support for $/
CPPFLAGS +=	-DDO_DOT_SH_PARAMS	# Include support for ${.sh.xxx} parameters
CPPFLAGS +=	-DDO_U_DOLAT_NOFAIL	# set -u; echo "$@" does not fail
CPPFLAGS +=	-DDO_DUP_FAIL		# call failed() when dup() fails
CPPFLAGS +=	-DDO_SUBSTRING		# Include support for substring operations
CPPFLAGS +=	-DDO_POSIX_SPEC_BLTIN	# Only special builtins keep var assignment
CPPFLAGS +=	-DDO_POSIX_FAILURE	# Only special builtins exit() on errors
CPPFLAGS +=	-DDO_POSIX_CD		# cd/pwd/... implement POSIX -L/-P
CPPFLAGS +=	-DDO_CHDIR_LONG		# chdir() and pwd() support more than PATH_MAX
CPPFLAGS +=	-DDO_EXPAND_LONG	# expand() support more than PATH_MAX
CPPFLAGS +=	-DDO_POSIX_RETURN	# Allow "return" inside "dot" scripts
CPPFLAGS +=	-DDO_POSIX_EXIT		# Use POSIX exit codes 126/127
CPPFLAGS +=	-DDO_POSIX_E		# Use POSIX rules for set -e, e.g. cmd subst
CPPFLAGS +=	-DDO_POSIX_EXPORT	# Support export/readonly -p name=value
					# and export prefix vars to "exec"
CPPFLAGS +=	-DDO_POSIX_EXPORT_ENV	# Autoexport imported environ w. -o posix
#CPPFLAGS +=	-DDO_EXPORT_ENV		# Always autoexport imported environ
CPPFLAGS +=	-DDO_POSIX_UNSET	# Support unset -f / -v
CPPFLAGS +=	-DDO_POSIX_SET		# Let set -- clear the arguments
CPPFLAGS +=	-DDO_POSIX_M		# Imply "set -m" for interactive shells
CPPFLAGS +=	-DDO_POSIX_TEST		# Implement POSIX test -e & text -S
CPPFLAGS +=	-DDO_POSIX_TRAP		# Implement POSIX trap -- for output
CPPFLAGS +=	-DDO_TRAP_EXIT		# Fork for (trap cmd EXIT; /usr/bin/true)
CPPFLAGS +=	-DDO_TRAP_FROM_WAITID	# With jobcontrol get signal from waitid()
CPPFLAGS +=	-DDO_POSIX_READ		# Implement POSIX read, mult. IFS -> ""
CPPFLAGS +=	-DDO_POSIX_PARAM	# Implement support for ${10}
CPPFLAGS +=	-DDO_POSIX_HERE		# Clear "quote" before expanding here document
#CPPFLAGS +=	-DDO_ALWAYS_POSIX_SH	# set -o posix by default
#CPPFLAGS +=	-DDO_POSIX_SH		# set -o posix if basename(argv[0]) == "sh"
					# This has precedence before DO_POSIX_PATH
					# Define DO_POSIX_SH if you like to use
					# bosh as /bin/sh on Linux.
CPPFLAGS +=	-DDO_POSIX_PATH		# Implement set -o posix from PATH
					# call smake 'CPPOPTX=-DPOSIX_BOSH_PATH=\"/bin/sh\"'
					# or add CPPFLAGS += -DPOSIX_BOSH_PATH=\"/bin/sh\"
					# to make a PATH auto set -o posix
CPPFLAGS +=	-DDO_POSIX_REDIRECT	# Redirect all error messages
CPPFLAGS +=	-DDO_POSIX_FIELD_SPLIT	# IFS=: var=a::b echo $var -> a '' b

CPPFLAGS +=	-DDO_NOTSYM		# Implement POSIX NOT symbol (!)
CPPFLAGS +=	-DDO_SELECT		# Implement ksh "select" feature
CPPFLAGS +=	-DDO_IFS_HACK		# Implement an IFS hack similar to ksh88
CPPFLAGS +=	-DDO_DOL_PAREN		# Implement POSIX $(...) command subst
CPPFLAGS +=	-DDO_EXT_TEST		# Implement extended test features
CPPFLAGS +=	-DDO_TILDE		# Include support for tilde expansion
CPPFLAGS +=	-DDO_BGNICE		# Include support for set -o bgnice
CPPFLAGS +=	-DDO_TIME		# Include support for set -o time
CPPFLAGS +=	-DDO_FULLEXCODE		# Include support for set -o fullexitcode
#					# The next feature is currently not POSIX
#CPPOPTX +=	-DDO_EXIT_MODFIX	# Prevent $? == 0 whith exitcode & 0xFF
CPPFLAGS +=	-DDO_CONT_BRK_FIX	# Fix the SYS III (1981) continue/break bug
CPPFLAGS +=	-DDO_CONT_BRK_POSIX	# Exit 1 with break 0 / continue 0
CPPFLAGS +=	-DDO_IOSTRIP_FIX	# Fix the SYS III (1981) cat 0<<-EOF bug
CPPFLAGS +=	-DDO_NOCLOBBER		# Include support for set -o noclobber
CPPFLAGS +=	-DDO_NOTIFY		# Include support for set -o notify
CPPFLAGS +=	-DDO_FDPIPE		# Include support for 2| for stderr pipe
CPPFLAGS +=	-DDO_KILL_L_SIG		# Include support for kill -l signo
CPPFLAGS +=	-DDO_IFS_SEP		# First char in IFS is "$*" separator
CPPFLAGS +=	-DDO_PS34		# Include support for PS3 and PS4
CPPFLAGS +=	-DDO_PPID		# Include support for POSIX PPID
CPPFLAGS +=	-DDO_LINENO		# Include support for POSIX LINENO
CPPFLAGS +=	-DDO_ULIMIT_OPTS	# Add options to the ulimit(1) output
CPPFLAGS +=	-DDO_STOI_PICKY		# Be more picky when parsing numbers
CPPFLAGS +=	-DDO_SYSBUILTIN		# Include the "builtin" builtin
CPPFLAGS +=	-DDO_SYSCOMMAND		# Include the "command" builtin
#CPPFLAGS +=	-DDO_SYSATEXPR		# Include the "@" builtin
CPPFLAGS +=	-DDO_SYSSYNC		# Include the "sync" builtin
CPPFLAGS +=	-DDO_SYSPGRP		# Include the "pgrp" builtin
CPPFLAGS +=	-DDO_SYSKILLPG		# Include the "killpg" builtin
CPPFLAGS +=	-DDO_SYSERRSTR		# Include the "errstr" builtin
CPPFLAGS +=	-DDO_SYSFIND		# Include the "find" builtin
CPPFLAGS +=	-DDO_SYSPRINTF		# Include the "printf" builtin
CPPFLAGS +=	-DDO_SYSPRINTF_FLOAT	# Include float support in "printf" builtin
CPPFLAGS +=	-DDO_SYSLOCAL		# Include the "local" builtin
CPPFLAGS +=	-DDO_SYSFC		# Include the "fc" builtin
CPPFLAGS +=	-DDO_SYSLIMIT		# Include the "limit" builtin
CPPFLAGS +=	-DDO_QS_CONVERT		# Convert quoted "\a\b\c" to "'abc'"
LIB_FIND +=	-lfind			# Add libfind

#CPPFLAGS +=	-DPARSE_DEBUG		# Include debug code/messages for parser
#CPPFLAGS +=	-DSTAK_DEBUG		# Include debug code for stak.c
CPPFLAGS +=	-DTOSSGROWING_MACRO	# Make stak.c::tossgrowing() a macro
#CPPFLAGS +=	-DTOSSCHECK		# Debug / verify magic in tossgrowing()
#CPPFLAGS +=	-DARGS_RIGHT_TO_LEFT	# Evaluate var2=val2 var1=val1 left to right
#CPPFLAGS +=	-DMY_GMATCH		# Enforce to use our local gmatch()
					# instead if the gmatch() from -lgen

#CPPFLAGS +=	-DSUN_EXPORT_BUG	# Export local readoly vars to scripts

CPPFLAGS +=	-DINTERACTIVE		# Include command line history editor
CPPFLAGS +=	-DINT_DOLMINUS		# Auto set -i for interactive shell

#CPPFLAGS +=	-DNO_SIGSEGV		# Do not install a SIGSEGV handler for debug

#
# Standard UNIX #defines for the Bourne Shell:
#
CPPFLAGS +=	-D_iBCS2 		# SCO echo compat support (unconditional)
#CPPFLAGS +=	$(iBCS2FLAG)		# SCO echo compat support (x86 only)
CPPFLAGS +=	-DACCT			# Include support for Shell Accounting
#CPPFLAGS +=	RES			# "Research" include "login", disable others

MAPFILE.INT = ../mapfile-intf
MAPFILES = $(MAPFILE.INT) $(MAPFILE.NGB)
LDFLAGS += $(MAPFILES:%=-M%)

#
# Use lazy loading for all libraries that are not needed when interpreting
# shell scripts.
#
# Note that libxtermcap is an indirect dependency via libshedit.
# Note that libschily is an indirect dependency via libshedit.
# These dependencies are loaded when libshedit is loaded because we are in
# interactive mode.
#
# Since we use -DDO_POSIX_GMATCH, we no longer need -lgen in LAZYLIBS.
#
LAZYLIBS = $(ZLAZYLOAD) -lsecdb -lshedit -lfind -lschily $(ZNOLAZYLOAD)
lint := LAZYLIBS = -lsecdb -lshedit -lfind -lschily
LDLIBS += $(LAZYLIBS)

.KEEP_STATE:

.PARALLEL:	$(OBJS)

%.o:	../%.c
	$(COMPILE.c) $<

all: $(PROG)

$(PROG): $(OBJS) $(MAPFILES)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(POFILE): $(POFILES)
	$(RM) $@
	$(CAT)     $(POFILES) | $(SED) -f ../poxcl.sed > $@

clean:	
	$(RM) $(OBJS)

lint: lint_SRCS

include ../../Makefile.targ
