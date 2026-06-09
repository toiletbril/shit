#!/bin/bash
# Bash mapfile and readarray reading lines from a here-string, checked
# byte-for-byte against bash. The element count and individual elements print so
# the comparison stays deterministic.
mapfile -t lines <<< $'alpha\nbeta\ngamma'
echo "count=${#lines[@]}"
echo "first=${lines[0]} last=${lines[2]}"
readarray -t arr <<< $'1\n2\n3\n4'
echo "n=${#arr[@]} edge=$((arr[0]+arr[3]))"
mapfile plain <<< $'p\nq'
echo "plain n=${#plain[@]}"
i=0
while [ $i -lt ${#lines[@]} ]; do
  echo "line $i is ${lines[i]}"
  i=$((i+1))
done
