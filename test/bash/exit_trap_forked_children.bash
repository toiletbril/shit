#!/bin/bash
# The EXIT trap against forked children, checked against bash. An inherited
# action stays with the parent, and an action a child installs for itself runs
# when that child leaves.
trap 'echo outer' EXIT

{ echo stage; } | /bin/cat
{ echo async; } &
wait

( trap 'echo inner_stage' EXIT; echo sub ) | /bin/cat
( trap 'echo inner_async' EXIT; echo asy ) &
wait

coproc CO { trap 'echo co_action' EXIT; echo hi; }
read -r line <&"${CO[0]}"
echo "got=$line"
wait "$COPROC_PID" 2> /dev/null
echo "co_status=$?"

echo tail
