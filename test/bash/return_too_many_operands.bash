#!/bin/bash

# A return with more than one operand reports an error and leaves the function
# with status two, checked against bash. The script carries on to its end and
# the EXIT action reads the status of the last command.

echo before_trap

trap 'echo "action_saw=$?"; echo action_tail' EXIT

too_many() {
  return 1 2
  echo unreachable
}
too_many
echo "function_status=$?"
echo continued
