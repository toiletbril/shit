#!/bin/sh
# The EXIT trap runs once when the shell finishes and sees the final state,
# checked against dash. A later trap on EXIT replaces the earlier action, and the
# action runs after the last command of the script.

# The action set last is the one that runs at exit.
trap 'echo first_action' EXIT
trap 'echo final_action' EXIT

echo body_one
echo body_two

# A function called before exit does not trigger the EXIT trap itself.
finish() {
    echo in_function
}
finish

# Removing and re-adding the trap leaves the latest action in place.
trap - EXIT
trap 'echo reinstalled' EXIT
echo last_body
