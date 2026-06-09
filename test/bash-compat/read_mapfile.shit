#!/bin/bash
# Bash read -a and mapfile/readarray, checked byte-for-byte against bash. Input
# arrives by pipe rather than a here-string, and elements are read by index since
# a quoted array view does not yet split per element.
echo "a b c d" | { read -a w; echo "${w[0]} ${w[3]} ${#w[@]}"; }
echo "10 20 30 40 50" | { read -a nums; echo "${nums[2]} ${#nums[@]}"; }
printf 'l1\nl2\nl3\n' | { mapfile lines; echo "${#lines[@]}"; }
printf 'x\ny\nz\n' | { mapfile -t lines; echo "${lines[0]}-${lines[2]} ${#lines[@]}"; }
printf 'apple\nbanana\ncherry\n' | { readarray -t fruit; echo "${fruit[1]} ${#fruit[@]}"; }
printf 'one\ntwo\nthree\n' | { mapfile -t arr; i=0; while [ $i -lt ${#arr[@]} ]; do echo "line $i is ${arr[i]}"; i=$((i+1)); done; }
echo "single" | { read -a one; echo "${#one[@]} ${one[0]}"; }
