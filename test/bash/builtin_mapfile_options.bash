#!/bin/bash
# mapfile -d sets the line delimiter, -s skips leading lines, and -O assigns into
# the existing array from the given index, keeping the elements outside the
# written range.
printf 'a:b:c:' | { mapfile -t -d : arr; echo "d=${arr[2]}-${#arr[@]}"; }
printf 'l1\nl2\nl3\nl4\n' | { mapfile -t -s 2 arr; echo "s=${arr[0]}-${#arr[@]}"; }
printf 'x\ny\n' | { arr=(K0 K1 K2); mapfile -t -O 1 arr; echo "O=${arr[0]}-${arr[1]}-${arr[2]}"; }
