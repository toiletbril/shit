#!/bin/bash

# The last command of an EXIT action runs as an ordinary child and leaves the
# shell in place to finish exiting. The status the shell ends with comes from
# the exit that triggered the action, and an exit the action runs replaces it.
# The action runs once, so an exit inside it reaches no second fire.

echo top-external
( trap 'echo action-head; /bin/echo action-external' EXIT
  echo body
  exit 3 )
echo "top-external-status=$?"

echo subshell-external
( trap 'echo sub-head; /bin/echo sub-external' EXIT
  exit 4 )
echo "subshell-external-status=$?"

echo action-exits
( trap 'echo action-exit-head; exit 5' EXIT
  exit 6 )
echo "action-exits-status=$?"

echo action-exits-bare
( trap 'echo bare-head; exit' EXIT
  exit 7 )
echo "action-exits-bare-status=$?"

echo action-reentry
( trap 'echo reentry-head; exit 8; echo unreachable' EXIT
  echo reentry-body
  exit 9 )
echo "action-reentry-status=$?"

echo action-only-external
( trap '/bin/echo only-external' EXIT
  exit 11 )
echo "action-only-external-status=$?"

echo action-external-fails
( trap '/bin/sh -c "exit 12"' EXIT
  exit 13 )
echo "action-external-fails-status=$?"

echo nested-exit-trap
( trap 'echo outer-action' EXIT
  ( trap 'echo inner-action; /bin/echo inner-external' EXIT
    exit 14 )
  echo "inner-status=$?"
  exit 15 )
echo "nested-exit-trap-status=$?"

echo exit-trap-last-command-done

# The top level action ends the script. The external command it finishes with
# leaves the shell alive to take the status the action exits with.
trap 'echo top-action; /bin/echo top-external; exit 22' EXIT
echo before-top-exit
exit 23
