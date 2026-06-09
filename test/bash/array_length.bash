#!/bin/bash
# Bash array length forms, checked byte-for-byte against bash. The at and star
# counts report the element total, an element length reports its string length,
# and append grows the count across single and multiple element appends starting
# from a populated array and from an empty one.
a=(one two three four)
echo "at-len: ${#a[@]}"
echo "star-len: ${#a[*]}"
echo "e0: ${#a[0]}"
echo "e2: ${#a[2]}"
a+=(five)
echo "after-append: ${#a[@]} last=${a[4]}"
a+=(six seven)
echo "multi-append: ${#a[@]} ${a[@]}"
b=()
echo "empty-len: ${#b[@]}"
b+=(z)
echo "grown: ${#b[@]} ${b[0]}"
c=(x)
c+=(y z)
echo "c: ${c[@]} count=${#c[@]}"
