#!/bin/sh
# dash takes the first operand of a return and says nothing about the rest.
# The function stops with that status and the script carries on to its end.

echo before_trap

trap 'echo "action_saw=$?"; echo action_tail' EXIT

too_many() {
    return 9 2 3
    echo unreachable
}
too_many
echo "function_status=$?"
echo continued
