#!/bin/bash
# Bash dynamic variables $RANDOM, $SECONDS, $EPOCHSECONDS, $BASHPID. The values
# are non-deterministic, so the script asserts their properties, which match bash
# byte-for-byte.
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
[ "$BASH_MONOSECONDS" -gt 0 ] && echo "monoseconds positive"
echo "hosttype=$HOSTTYPE"
echo "machtype=$MACHTYPE"
[ "$GROUPS" -ge 0 ] && echo "groups present"
[ -n "$SRANDOM" ] && echo "srandom present"
