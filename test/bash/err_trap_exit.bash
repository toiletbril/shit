#!/bin/bash

# An ERR action that exits leaves the frame it fires in. Each section runs
# inside its own subshell, and the exit stops that section alone.

echo action-exit-in-subshell
(
  set -o errtrace
  trap 'echo caught; exit 9' ERR
  ( false; echo unreachable; echo also-unreachable )
  echo unreachable-end
)
echo "s=$?"

echo action-exit-at-top
(
  trap 'echo caught-top; exit 7' ERR
  false
  echo unreachable
)
echo "s=$?"

echo action-exit-in-function
(
  set -o errtrace
  trap 'echo caught-function; exit 5' ERR
  exits_from_action() { false; echo unreachable; }
  exits_from_action
  echo unreachable-end
)
echo "s=$?"

echo action-exit-in-substitution
(
  set -o errtrace
  trap 'echo caught-substitution; exit 4' ERR
  echo "captured=$(false; echo substitution-tail)"
  echo outer-tail
)
echo "s=$?"

echo action-exit-under-errexit
(
  set -e
  trap 'echo caught-errexit; exit 3' ERR
  false
  echo unreachable
)
echo "s=$?"

# The option decides the exit as it stands once the action has returned. An
# action that clears errexit lets the list carry on. An action that sets it
# stops a list that ran without it.
echo action-clears-errexit
(
  set -e
  trap 'echo cleared; set +e' ERR
  false
  echo first-tail
  false
  echo second-tail
)
echo "s=$?"

echo action-sets-errexit
(
  set +e
  trap 'echo raised; set -e' ERR
  false
  echo unreachable
)
echo "s=$?"

echo err-trap-exit-done
