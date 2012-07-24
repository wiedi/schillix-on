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

PS1='${LOGNAME}@$(/usr/bin/hostname):$(
    [[ "${LOGNAME}" == "root" ]] && printf "%s" "${PWD/${HOME}/~}# " ||
    printf "%s" "${PWD/${HOME}/~}\$ ")'

fi
