#!/bin/bash
f() { visited[$1]=1; }
g() { local -A visited=(); f keyA; f keyB; echo "dyn: ${visited[keyA]}${visited[keyB]} n=${#visited[@]}"; }
g
declare -A m=([x]=1 [y]=2)
echo "literal: ${m[x]}${m[y]}"
declare -A e=()
e[path/to]=here
echo "empty-then-set: ${e[path/to]}"
