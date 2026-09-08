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

# A failing subshell reports the parenthesized command and the line the closing
# parenthesis is written on. The reprint lays out one blank inside each
# parenthesis, separates the commands of a multiple line body with a semicolon
# and a blank, and keeps the redirections written after the closing
# parenthesis.
echo err-subshell-text
trap 'echo "E-$LINENO-[$BASH_COMMAND]"' ERR
(false)
(   false   )
( echo subshell-a
  false )
( false ) > /dev/null
( true ) && ( false )
{ false; }
trap - ERR
echo err-subshell-text-done

# A subshell nested inside another carries the same layout, and a doubled
# parenthesis opens an arithmetic command that keeps the way it is written. A
# substitution whose body opens a subshell takes a blank after the dollar sign,
# because the two parentheses would otherwise read as arithmetic. Errtrace is
# cleared so that each statement raises one fire and the reprint stands alone.
echo err-nested-subshell-text
set +E
trap 'echo "E-[$BASH_COMMAND]"' ERR
( ( false ) )
( (false) )
( ( false
  ) )
( ( echo nested-a
    false ) )
( ( ( false ) ) )
( ((0)) )
( { false; } )
( echo "a  b" ; ( false ) )
( true && ( false ) )
( ( false ) > /dev/null )
false $( ( echo nested-b ) )
false $( (echo nested-c) )
false $( ((1)) )
trap - ERR
set -E
echo err-nested-subshell-text-done
