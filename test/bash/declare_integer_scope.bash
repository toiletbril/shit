#!/bin/bash
# The integer mark is scoped with a local binding, applies to array elements,
# and declare -p prints it for arrays and for a name that has no value yet.
declare -i g=5
f() { local g; g=2+2; echo "plain_local_suppresses=[$g]"; }
f
echo "outer_keeps_mark=$g"
h() { local -i x=5; x+=3; echo "local_i_adds=$x"; }
h
x=1+1
echo "mark_gone_after_return=[$x]"
m() { local -ia nums; nums[0]=2; nums[0]+=3; echo "local_int_array=${nums[0]}"; }
m
declare -ia arr
arr[0]=5
arr[0]+=3
echo "indexed_element_adds=${arr[0]}"
declare -iA map
map[k]=5
map[k]+=3
echo "assoc_element_adds=${map[k]}"
declare -ia plain
plain[0]="2+3"
echo "plain_element_evaluates=${plain[0]}"
declare -i b[0]=7
b[0]+=1
echo "subscripted_declare=${b[0]}"
declare -p b
declare -i z
declare -p z
declare -iA m3
m3[q]=5
declare -p m3
declare -ix q2=5
q2+=3 env > /tmp/shit_test_q2env 2>/dev/null
grep "^q2=" /tmp/shit_test_q2env
echo "prefix_restores=$q2"
rm -f /tmp/shit_test_q2env
