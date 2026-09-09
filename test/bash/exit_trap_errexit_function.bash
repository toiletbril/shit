#!/bin/bash
# The EXIT trap against a function body that errexit ends, checked against bash.
trap 'echo "action_saw=$?"' EXIT

failing_function()
{
  echo in_body
  /bin/cat /nonexistent-kosh-errexit-probe 2> /dev/null
  echo unreachable_body
}

set -e
failing_function
echo unreachable
