#!/bin/bash
# A CHLD action that spawns its own child, checked against bash. The child the
# action reaps is counted, and its fire is held back until the next child
# arrives.
chld_count=0
trap 'chld_count=$((chld_count+1)); /bin/echo inner > /dev/null' CHLD

/bin/echo one > /dev/null
echo "after_one=$chld_count"
:
echo "after_builtin=$chld_count"
{ :; }
echo "after_group=$chld_count"
for v in 1; do :; done
echo "after_loop=$chld_count"
/bin/echo two > /dev/null
echo "after_two=$chld_count"
/bin/echo three > /dev/null
echo "after_three=$chld_count"

trap - CHLD
chld_count=0
/bin/echo settle > /dev/null
echo "after_removal=$chld_count"

deep_count=0
trap 'deep_count=$((deep_count+1)); /bin/echo a > /dev/null; /bin/echo b > /dev/null' CHLD
/bin/echo start > /dev/null
echo "two_children=$deep_count"

trap - CHLD
status_count=0
trap 'status_count=$((status_count+1)); /bin/echo s > /dev/null' CHLD
false | true | false
echo "pipe_status=${PIPESTATUS[*]}"
/bin/cat /nonexistent-kosh-chld-probe 2> /dev/null
echo "failed_status=$? fires=$status_count"

trap - CHLD
echo "done"
