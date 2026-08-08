#!/bin/bash
# Bash associative array operations beyond the basics, checked byte-for-byte
# against bash. A declare -A with an inline element initializes the map, append
# concatenates onto an existing value, a missing key reads empty with a zero
# count, and a for loop reads a value back by its key.
declare -A cap
cap[france]=paris
echo "${cap[france]}"
echo "${#cap[@]}"
echo "${!cap[@]}"
echo "${cap[@]}"
cap[france]+=" city"
echo "${cap[france]}"
declare -A nums=([x]=1)
echo "init: ${nums[x]} count=${#nums[@]}"
declare -A e
echo "missing=[${e[nope]}] count=${#e[@]}"
for k in france; do echo "iter $k=${cap[$k]}"; done


# Bash associative arrays via declare -A, checked byte-for-byte against bash with
# literal keys (the multi-key element order is store-defined, so single-element
# views are used for the listing forms).
declare -A m
m[foo]=bar
echo "${m[foo]}"
m[key]=value
echo "${m[key]}"
echo "${#m[@]}"
declare -A colors
colors[red]=ff0000
echo "${colors[red]}"
echo "${!colors[@]}"
echo "${colors[@]}"
declare -A counts
counts[apples]=5
counts[apples]=8
echo "${counts[apples]}"
declare -A app
app[x]=foo
app[x]+=bar
echo "${app[x]}"
typeset -A t
t[k]=v
echo "${t[k]}"
declare -A empty
echo "[${empty[missing]}] count=${#empty[@]}"

f() { visited[$1]=1; }
g() { local -A visited=(); f keyA; f keyB; echo "dyn: ${visited[keyA]}${visited[keyB]} n=${#visited[@]}"; }
g
declare -A m=([x]=1 [y]=2)
echo "literal: ${m[x]}${m[y]}"
declare -A e=()
e[path/to]=here
echo "empty-then-set: ${e[path/to]}"

i=1 j=2
a[i|j]=ored
echo "or: ${a[3]}"
b[2&3]=anded
echo "and: ${b[2]}"
c[1<<4]=shifted
echo "shift: ${c[16]}"
key[63|8]=1
echo "mask: ${key[55]}-${key[8]}"
declare -A m
m[plain]=v
echo "assoc: ${m[plain]}"
