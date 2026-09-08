#!/bin/bash

trap 'echo ERR' ERR

echo guarded-loop
for item in one two; do
  false && echo unreachable
done
echo "status=$?"

echo plain-loop
for item in one two; do
  false
done
echo "status=$?"

echo brace
{ false; }
echo "status=$?"

echo function
fail_function() {
  false
}
fail_function
echo "status=$?"

echo subshell
(false)
echo "status=$?"

echo conditional
[[ no = yes ]]
echo "status=$?"

echo arithmetic
((0))
echo "status=$?"

echo errtrace-function
set -E
fail_function
echo "status=$?"

echo errtrace-subshell
(false)
echo "status=$?"

echo return-plain
trap 'echo RETURN' RETURN
return_function() {
  true
}
return_function
echo return-functrace
set -T
return_function
set +T
trap - RETURN

echo lineno-plain
trap 'echo "at $LINENO"' ERR
false
echo lineno-function
lineno_function() {
  false
}
lineno_function
echo lineno-subshell
(false)
echo lineno-source
. bash/goldens/err_trap_lineno_inner.bash
trap - ERR
echo lineno-done

echo err-command-text
trap 'echo "E-[$BASH_COMMAND]"' ERR
false
command_text_function() {
  false
}
command_text_function
! true
[ 1 -eq 2 ]
(( 0 ))
[[ -n "" ]]
false | true
grep -q missing < /dev/null
trap - ERR
echo err-command-text-done
