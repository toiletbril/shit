#!/bin/bash
# shellcheck disable=SC2034,SC2086,SC2249

echo source-return
( trap 'echo R-source' RETURN
  . bash/goldens/return_trap_inner.bash
  echo after-source=$? )
echo after-source-return=$?

echo source-plain
( trap 'echo R-plain' RETURN
  . bash/goldens/return_trap_plain.bash
  echo after-plain=$? )
echo after-source-plain=$?

echo source-repeated
( trap 'echo R-repeated' RETURN
  . bash/goldens/return_trap_plain.bash
  . bash/goldens/return_trap_plain.bash
  echo after-repeated=$? )
echo after-source-repeated=$?

echo source-installs-trap
( . bash/goldens/return_trap_setter.bash
  echo after-setter=$? )
echo after-source-installs-trap=$?

echo source-action-status
( trap 'false' RETURN
  . bash/goldens/return_trap_inner.bash
  echo after-action-status=$? )
echo after-source-action-status=$?

echo source-positional
( trap 'echo R-one=$1' RETURN
  set -- outer
  . bash/goldens/return_trap_plain.bash sourced-arg
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
  . bash/goldens/return_trap_plain.bash
  echo after-posix=$? )
echo after-posix-source=$?

echo return-trap-done
