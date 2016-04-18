#
# Simple profile. Do not place /usr/gnu/bin at front.
# Add /usr/X11/bin, /usr/sbin and /sbin to the end.
PATH=$PATH:/usr/sbin:/sbin
export PATH

#
# Use less(1) as the default pager for the man(1) command.
# ... but we do not have less on OpenSolaris-ON
#
#PAGER="/usr/bin/less -ins"
PAGER="/usr/bin/more -s"
export PAGER

#
# Only for ksh93...
# Define default prompt to <username>@<hostname>:<path><"($|#) ">
# and print '#' for user "root" and '$' for normal users.
#
if ( [ ! -z "${.sh.version}" ] ) 2>/dev/null ; then

	#
	# bosh also supports ${.sh.version}, but bosh does not support "[[",
	# so check whether it refers to ksh93 or to bosh.
	# If it refers to bosh, we have no "93" and "version"
	# is used while ksh93 uses "Version" in the string.
	#
	case "${.sh.version}" in

	Version*93*)
	    #
	    # This is ksh93
	    #
	    PS1='${LOGNAME}@$(/usr/bin/hostname):$(
	    [[ "${LOGNAME}" == "root" ]] && printf "%s" "${PWD/${HOME}/~}# " ||
	    printf "%s" "${PWD/${HOME}/~}\$ ")'
	    ;;

	*)	case "${.sh.shell}" in

		*Bourne*)
		    #
		    # This is the Bourne Shell
		    #
		    # We may like to insert "set -o hostprompt" to get
		    # "<username>@<hostname> > "
		    ;;
		esac
	esac

fi
