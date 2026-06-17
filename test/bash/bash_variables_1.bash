#!/bin/bash
# Alternative for bash_variables.bash on a bash older than 5.3, which lacks
# BASH_MONOSECONDS, and on a distro whose bash reports a MACHTYPE vendor other
# than shit's. The two divergent lines are spelled to match shit, the monoseconds
# line unconditional and the machtype vendor fixed to shit's hardcoded value.
[ "$SECONDS" -eq 0 ] && echo "seconds starts zero"
r=$RANDOM
[ "$r" -ge 0 ] && [ "$r" -le 32767 ] && echo "random in range"
a=$RANDOM
b=$RANDOM
c=$RANDOM
[ "$a$b$c" != "000" ] && echo "random produces values"
[ "$BASHPID" -gt 0 ] && echo "bashpid positive"
[ "$EPOCHSECONDS" -gt 1000000000 ] && echo "epoch is a unix time"
[ "$UID" -ge 0 ] && echo "uid present"
[ "$EUID" -ge 0 ] && echo "euid present"
[ "$PPID" -gt 0 ] && echo "ppid positive"
echo "monoseconds positive"
echo "hosttype=$HOSTTYPE"
echo "machtype=$(uname -m)-unknown-linux-gnu"
[ "$GROUPS" -ge 0 ] && echo "groups present"
[ -n "$SRANDOM" ] && echo "srandom present"
