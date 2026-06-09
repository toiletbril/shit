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
