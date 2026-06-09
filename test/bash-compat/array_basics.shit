#!/bin/bash
# Bash indexed arrays, assignment a=(x y z) and element access, checked
# byte-for-byte against bash. Covers numeric index, @ and *, the scalar read of
# element zero, a negative index, an arithmetic subscript, out of range, building
# one array from another, and iteration.
a=(x y z)
echo "${a[0]}"
echo "${a[1]}"
echo "${a[2]}"
echo "${a[@]}"
echo "${a[*]}"
echo "$a"
echo "${a[-1]}"
i=1
echo "${a[i]}"
echo "${a[1+1]}"
echo "[${a[9]}]"
b=("${a[@]}" w)
echo "${b[@]}"
fruits=(apple banana cherry)
for f in ${fruits[@]}; do echo "fruit $f"; done
nums=(5 10 15 20)
echo "${nums[3]}"
empty=()
echo "[${empty[@]}]"
mixed=(one "two three" four)
echo "${mixed[1]}"
echo "${mixed[@]}"
