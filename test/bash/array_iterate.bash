#!/bin/bash
# Bash iteration over an array, checked byte-for-byte against bash. A quoted at
# view yields one loop word per element so an element with a space stays whole,
# an unquoted at view field splits each element, and arithmetic over the elements
# accumulates the running total.
fruits=(apple banana cherry)
for f in "${fruits[@]}"; do echo "fruit: $f"; done
n=(5 10 15 20)
total=0
for v in "${n[@]}"; do total=$((total+v)); done
echo "sum: $total"
words=("hello world" foo bar)
for w in "${words[@]}"; do echo "word=[$w]"; done
count=0
for w in ${words[@]}; do count=$((count+1)); done
echo "unquoted-split-count: $count"
for i in ${!fruits[@]}; do echo "$i -> ${fruits[$i]}"; done
