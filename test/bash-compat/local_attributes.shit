#!/bin/bash
# Bash local with attribute flags, checked byte-for-byte against bash. -a makes a
# local indexed array, -A an associative one, and -i, -r, -x are accepted. The
# inline array-literal form local -a a=(...) is not supported, only the separate
# assignment.
f() { local -a arr; arr=(x y z); echo "${arr[1]} count=${#arr[@]}"; }
f
g() { local -i n=5; echo "n is $n"; }
g
h() { local -A m; m[key]=value; echo "${m[key]}"; }
h
j() { local x=hi; echo "scalar $x"; }
j
m() { local -r c=constant; echo "readonly $c"; }
m
n() { local y; y=assigned; echo "$y"; }
n
indexed() { local -a inner; inner=(a b c); echo "first ${inner[0]} last ${inner[2]} size ${#inner[@]}"; }
indexed
