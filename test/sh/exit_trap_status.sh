#!/bin/sh
# The EXIT trap reads the status the shell is ending with, checked against
# dash. The operand of the exit builtin reaches the action through $?.

echo before_trap

returns_seven() {
    return 7
}
returns_seven
echo "function_status=$?"

trap 'echo "action_saw=$?"; echo action_tail' EXIT

echo last_body
exit 42
