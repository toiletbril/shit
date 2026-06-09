#!/bin/bash
# Bash declare -p, checked byte-for-byte against bash. Prints the declaration of
# a scalar, an indexed array, an empty array, and a single-key associative array,
# plus the error for an unset name. Multi-key associative order is store order,
# not bash order, so the test uses one key.
x=5
declare -p x
greeting=hello
declare -p greeting
a=(one two three)
declare -p a
empty=()
declare -p empty
declare -A m
m[only]=value
declare -p m
declare -p definitely_unset 2>/dev/null
echo "unset rc=$?"
nums=(10 20 30 40)
declare -p nums
