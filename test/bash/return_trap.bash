#!/bin/bash
# shellcheck disable=SC2034,SC2086,SC2249

echo source-return
( trap 'echo R-source' RETURN
  . "${BASH_SOURCE[0]%/*}/goldens/return_trap_inner.bash"
  echo after-source=$? )
echo after-source-return=$?

echo source-plain
( trap 'echo R-plain' RETURN
  . "${BASH_SOURCE[0]%/*}/goldens/return_trap_plain.bash"
  echo after-plain=$? )
echo after-source-plain=$?

echo source-repeated
( trap 'echo R-repeated' RETURN
  . "${BASH_SOURCE[0]%/*}/goldens/return_trap_plain.bash"
  . "${BASH_SOURCE[0]%/*}/goldens/return_trap_plain.bash"
  echo after-repeated=$? )
echo after-source-repeated=$?

echo source-installs-trap
( . "${BASH_SOURCE[0]%/*}/goldens/return_trap_setter.bash"
  echo after-setter=$? )
echo after-source-installs-trap=$?

echo source-action-status
( trap 'false' RETURN
  . "${BASH_SOURCE[0]%/*}/goldens/return_trap_inner.bash"
  echo after-action-status=$? )
echo after-source-action-status=$?

echo source-positional
( trap 'echo R-one=$1' RETURN
  set -- outer
  . "${BASH_SOURCE[0]%/*}/goldens/return_trap_plain.bash" sourced-arg
  echo after-positional=$1 )
echo after-source-positional=$?

echo function-without-functrace
( trap 'echo R-function' RETURN
  quiet_function() { echo quiet-body; }
  quiet_function
  echo after-quiet=$? )
echo after-function-without-functrace=$?

echo function-with-functrace
( set -T
  trap 'echo R-function' RETURN
  traced_function() { echo traced-body; return 5; }
  traced_function
  echo after-traced=$? )
echo after-function-with-functrace=$?

echo nested-function-with-functrace
( set -T
  trap 'echo R-depth=${#FUNCNAME[@]}' RETURN
  inner_function() { echo inner-body; }
  outer_function() { inner_function; echo outer-body; }
  outer_function
  echo after-nested=$? )
echo after-nested-function-with-functrace=$?

echo posix-source
( set -o posix
  trap 'echo R-posix' RETURN 2>/dev/null
  . "${BASH_SOURCE[0]%/*}/goldens/return_trap_plain.bash"
  echo after-posix=$? )
echo after-posix-source=$?

echo function-status-in-action
( set -T
  trap 'echo "R-seen=$?"' RETURN
  false_then_zero() { false; return 0; }
  true_then_five() { true; return 5; }
  falls_through() { false; }
  bare_return() { false; return; }
  false_then_zero
  echo "after-false-then-zero=$?"
  true_then_five
  echo "after-true-then-five=$?"
  falls_through
  echo "after-falls-through=$?"
  bare_return
  echo "after-bare-return=$?" )
echo after-function-status-in-action=$?

echo source-status-in-action
( trap 'echo "R-seen=$?"' RETURN
  . "${BASH_SOURCE[0]%/*}/goldens/return_trap_status.bash"
  echo after-status=$? )
echo after-source-status-in-action=$?

echo source-keyword-spelling
( trap 'echo R-keyword' RETURN
  source "${BASH_SOURCE[0]%/*}/goldens/return_trap_inner.bash"
  echo after-keyword=$? )
echo after-source-keyword-spelling=$?

echo action-removes-trap
( set -T
  trap 'echo R-once; trap - RETURN' RETURN
  first_call() { echo first-body; }
  second_call() { echo second-body; }
  first_call
  second_call
  echo after-removal=$? )
echo after-action-removes-trap=$?

echo traced-function-pipeline
( set -T
  trap 'echo R-stage' RETURN
  staged_function() { echo staged-body; return 3; }
  staged_function | cat
  echo after-pipeline=$? )
echo after-traced-function-pipeline=$?

echo functrace-long-form
( set -o functrace
  trap 'echo R-long' RETURN
  long_form_function() { echo long-body; }
  long_form_function
  echo after-long-form=$? )
echo after-functrace-long-form=$?

echo function-action-failure
( set -T
  trap 'false' RETURN
  failing_action_function() { echo action-body; return 4; }
  failing_action_function
  echo after-action-failure=$? )
echo after-function-action-failure=$?

echo sourced-action-command
( trap 'echo "R-[$BASH_COMMAND]"' RETURN
  . "${BASH_SOURCE[0]%/*}/goldens/return_trap_plain.bash"
  echo after-untraced=$? )
echo after-sourced-action-command=$?

echo sourced-action-command-traced
( set -T
  trap 'echo "R-[$BASH_COMMAND]"' RETURN
  . "${BASH_SOURCE[0]%/*}/goldens/return_trap_plain.bash"
  echo after-traced=$? )
echo after-sourced-action-command-traced=$?

echo sourced-action-command-early-return
( trap 'echo "R-[$BASH_COMMAND]"' RETURN
  . "${BASH_SOURCE[0]%/*}/goldens/return_trap_inner.bash"
  echo after-early-return=$? )
echo after-sourced-action-command-early-return=$?

echo function-installs-own-trap
( installs_own() {
    trap 'echo "R-own=$?"' RETURN
    return 4
  }
  installs_own
  echo after-own=$?
  plain_after() { echo plain-body; }
  plain_after
  echo after-plain-following=$?
  trap -p RETURN
  echo after-listing=$? )
echo after-function-installs-own-trap=$?

echo nested-body-installs-trap
( set -T
  trap 'echo R-outer' RETURN
  replaces_inherited() {
    trap 'echo R-inner' RETURN
    echo replacing-body
  }
  replaces_inherited
  echo after-replacement=$?
  trap -p RETURN )
echo after-nested-body-installs-trap=$?

echo function-exit-in-body
( set -T
  trap 'echo R-exit' RETURN
  exits_body() { echo exit-body; exit 4; }
  ( exits_body )
  echo after-function-exit=$? )
echo after-function-exit-in-body=$?

echo sourced-exit-in-body
( trap 'echo R-src-exit' RETURN
  ( . "${BASH_SOURCE[0]%/*}/goldens/return_trap_exit.bash" )
  echo after-sourced-exit=$? )
echo after-sourced-exit-in-body=$?

echo return-trap-done
