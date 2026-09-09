#!/bin/bash

# A continue with a count that is not a number ends a non-interactive shell
# with status two, checked against bash. The EXIT action reads that status.

echo before_trap

trap 'echo "action_saw=$?"; echo action_tail' EXIT

echo last_body
while true; do
  continue bogus
done
echo unreachable
