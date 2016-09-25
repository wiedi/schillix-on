#!/sbin/sh
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.opensource.org/licenses/cddl1.txt
#
# CDDL HEADER END
#

#
# Copyright 2015-2016 Jörg Schilling.  All rights reserved.
#

#
# Match strings, call: "smf_strmatch <str1> <op> <str2>"
#
# op	may be "=" to check for equal strings or "!=" for non-equal strings
# str1	may be any string that is to be checked
# str2	may be any glob to check against <str1>
#
# This function may be used to avoid RE string compare with the [[ ]] ksh-ism.
# It is safe as it is based on the "case" feature of the shell and a test call
# with three parameters and a binary operator in the middle.
#
smf_strmatch() {
	case "$1" in

	$3)	[ "$2" = '=' ] && return 0; return 1;;
	*)	[ "$2" = '=' ] && return 1; return 0;;
	esac
}
