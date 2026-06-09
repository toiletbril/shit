#!/bin/bash
# Bash whole-array copy and merge, checked byte-for-byte against bash. A quoted
# at copy duplicates every element so a later write to the copy leaves the
# original alone, a command substitution that field splits its lines builds a new
# array, and a merge of a copy with an extra element grows the count by one.
a=(one "two three" four five)
b=("${a[@]}")
echo "copy-count: ${#b[@]}"
printf '<%s>' "${b[@]}"; echo
b[0]=ONE
echo "orig-unchanged: ${a[0]}"
echo "copy-changed: ${b[0]}"
nums=(3 1 2)
sorted=($(printf '%s\n' "${nums[@]}" | sort))
echo "sorted: ${sorted[@]}"
merged=("${a[@]}" extra)
echo "merged-count: ${#merged[@]} last=${merged[-1]}"
