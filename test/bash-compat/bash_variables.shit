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
