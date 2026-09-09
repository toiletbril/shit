#!/bin/bash
# A signal trap does not fire inside a subshell, checked against bash. The
# inherited action stays in the listing there until a trap command changes
# something, and that command discards every inherited action at once.
chld_count=0
trap 'chld_count=$((chld_count+1))' CHLD

/bin/echo warm > /dev/null
echo "before_sub=$chld_count"

(
  echo "sub_entry=$chld_count"
  /bin/echo one > /dev/null
  echo "sub_one=$chld_count"
  /bin/echo two > /dev/null
  echo "sub_two=$chld_count"
)
echo "after_sub=$chld_count"

/bin/echo three > /dev/null
echo "after_three=$chld_count"
trap - CHLD

trap 'echo fired_hup' HUP
trap 'echo fired_term' TERM

echo "--- a bare subshell lists the inherited actions"
( trap -p )

echo "--- an install discards them"
( trap 'echo fired_usr1' USR1; trap -p )

echo "--- a reset discards them"
( trap - HUP; trap -p )

echo "--- an ignore keeps only itself"
( trap '' USR2; trap -p )

echo "--- the parent keeps its own"
trap -p
trap - HUP TERM

echo "--- a forked pipeline stage"
stage_count=0
trap 'stage_count=$((stage_count+1))' CHLD
/bin/echo settle > /dev/null
(
  /bin/echo four > /dev/null
  /bin/echo five > /dev/null
  echo "stage_count=$stage_count"
) | /bin/cat
trap - CHLD

echo done
