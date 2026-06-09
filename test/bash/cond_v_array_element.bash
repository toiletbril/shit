#!/bin/bash
declare -A cap=([france]=paris [japan]=tokyo)
[[ -v cap[france] ]] && echo "france set" || echo "france unset"
[[ -v cap[germany] ]] && echo "germany set" || echo "germany unset"
arr=(a b c)
[[ -v arr[1] ]] && echo "arr1 set" || echo "arr1 unset"
[[ -v arr[5] ]] && echo "arr5 set" || echo "arr5 unset"
[[ -v arr[@] ]] && echo "arr@ set" || echo "arr@ unset"
empty=()
[[ -v empty[@] ]] && echo "empty set" || echo "empty unset"
s=scalar
[[ -v s ]] && echo "s set" || echo "s unset"
[[ -v s[0] ]] && echo "s0 set" || echo "s0 unset"
[[ -v unsetvar ]] && echo "u set" || echo "u unset"
