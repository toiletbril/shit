#!/bin/bash
# An array's setness is element zero's, and the test, assign, and substring
# modifiers run against one element with its own setness.
a=(); echo "[${a+y}]"
b=(x); echo "[${b+y}]"
declare -a c=([1]=q); echo "[${c+y}]" "[${c[1]+y}]" "[${c[0]+y}]"
e=(abcdef ghij)
echo "[${e[1]:1}]" "[${e[1]:1:2}]" "[${e[9]:-fb}]" "[${e[0]:+plus}]"
echo "[${e[9]=zz}]" "[${e[9]}]"
declare -A m=([k]=v)
echo "[${m[k]+y}]" "[${m[no]+y}]" "[${m[no]:-fb2}]"
