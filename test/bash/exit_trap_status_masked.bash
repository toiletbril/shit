#!/bin/bash

# An exit operand outside one byte range is masked before the EXIT action reads
# it, checked against bash. A bare exit inside the action reports that same
# masked status again.

echo before_trap

trap 'echo "action_saw=$?"; echo action_tail; exit' EXIT

echo last_body
exit 300
