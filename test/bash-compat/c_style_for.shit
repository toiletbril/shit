#!/bin/bash
# Bash C-style for loop, for (( init; cond; step )), checked byte-for-byte
# against bash. Covers counting up and down, an accumulator, an empty header with
# break, continue and break inside, the comma operator, and a variable bound.
for (( i=0; i<3; i++ )); do echo "up $i"; done
for (( i=3; i>0; i-- )); do echo "down $i"; done
sum=0
for (( i=1; i<=10; i++ )); do sum=$((sum + i)); done
echo "sum $sum"
for (( ; ; )); do echo once; break; done
for (( i=0; i<5; i++ )); do
  if (( i == 2 )); then continue; fi
  echo "skip2 $i"
done
for (( i=0; i<10; i++ )); do
  if (( i == 3 )); then break; fi
  echo "stop3 $i"
done
for (( i=0, j=6; i<j; i++, j-- )); do echo "pair $i $j"; done
limit=4
for (( k=0; k<limit; k++ )); do echo "var $k"; done
