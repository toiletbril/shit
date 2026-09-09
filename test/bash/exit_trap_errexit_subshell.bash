#!/bin/bash
# The EXIT trap against a subshell status that errexit ends, checked against
# bash. The action reports the status the subshell exited with.
trap 'echo "action_saw=$?"' EXIT

set -e
echo before
( exit 3 )
echo unreachable
