#!/bin/bash
# Bash array quoting forms, checked byte-for-byte against bash. A quoted at view
# keeps each element a separate word, a quoted star view joins on the first IFS
# byte, an unquoted at view field splits, and a single-element star copy lands as
# one element while a quoted at copy keeps the element count.
a=(one "two three" four)
printf '[%s]' "${a[@]}"; echo
printf '[%s]' "${a[*]}"; echo
printf '[%s]' ${a[@]}; echo
IFS=-
printf '[%s]' "${a[*]}"; echo
unset IFS
b=("${a[@]}")
printf '<%s>' "${b[@]}"; echo
echo "count=${#b[@]}"
c=("${a[*]}")
echo "starcopy=${#c[@]} first=[${c[0]}]"
