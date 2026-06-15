#!/bin/bash
# A readonly or a declare -i made inside a subshell dies with the child, so the
# parent can still reassign the name afterward, the way a forked subshell
# isolates its option and attribute changes.
x=1
(readonly x)
x=2
echo "x=$x"
n=5
(declare -i n)
n=abc
echo "n=$n"
arr=(a b)
(readonly arr)
arr=(c d)
echo "arr=${arr[1]}"
