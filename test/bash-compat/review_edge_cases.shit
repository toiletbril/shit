#!/bin/bash
# Edge cases found by code review, checked byte-for-byte against bash. The [[ ]]
# operators short-circuit so a bad dead branch does not error, an empty (( ))
# yields status 1, an empty $'' keeps one field, and an empty replacement pattern
# is a no-op while the anchored forms still splice.
[[ 0 -eq 0 || x -eq y ]] && echo or-shortcircuit
[[ 1 -eq 2 && x -eq y ]] || echo and-shortcircuit
[[ -n "" && bad =~ "(" ]] || echo regex-dead-branch
(( )) ; echo "empty arith rc=$?"
(( 1 + 1 )) ; echo "true arith rc=$?"
(( 0 )) ; echo "false arith rc=$?"
f() { echo "fields=$#"; } ; f $''
v=ab ; e= ; printf 'replace-all=[%s]\n' "${v//$e/-}"
printf 'replace-one=[%s]\n' "${v/$e/-}"
w=hi ; printf 'prefix=[%s]\n' "${w/#/X}"
printf 'suffix=[%s]\n' "${w/%/Z}"
printf 'normal=[%s]\n' "${v/a/Q}"
echo {1..99999999999999999999}
