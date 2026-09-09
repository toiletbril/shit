#!/bin/bash
# The EXIT trap against a shell that errexit ends, checked against bash. The
# action runs, sees the failing status, and its own commands run to the end.
trap 'echo "action_saw=$?"; echo action_tail' EXIT

set -e
echo last_body
false
echo unreachable
