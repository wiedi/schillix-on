set -o globalaliases	# Enable persistent aliases from $HOME/.globals
set -o localaliases	# Enable persistent aliases from ./.locals (dir local)

set -o fdpipe		# Enable 2| for pipe on stderr
set -o hostprompt	# Enable "<hostname> <logname>> " as default prompt

set -o monitor		# Enable job-control

TF='%6:E real %6U user %6S sys %P%% cpu %I+%Oio %Fpf+%Ww'
TIMEFORMAT=$TF

SAVEHISTORY=on		# Save history in $HOME/.history

set -o hashcmds		# Enable hash-commands to edit raw aliases
			# Warning: every shell comment in this script now
			# needs a space after the '#' character.

set -o time		# Enable timing for all commands, must be last
