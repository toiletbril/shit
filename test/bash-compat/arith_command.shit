#!/bin/bash
# Bash (( expr )) arithmetic command, checked byte-for-byte against bash. The
# status is success when the value is non-zero. Covers comparison, assignment,
# compound assignment, pre and post increment, and nested parentheses.
(( 1 + 1 )) && echo a
(( 0 )) || echo b
(( 5 > 3 )) && echo c
(( 2 * (3 + 4) == 14 )) && echo d
x=5
(( x++ ))
echo "$x"
(( ++x ))
echo "$x"
y=10
(( y -= 4 ))
echo "$y"
z=0
(( z = 3 * 3 ))
echo "$z"
n=10
(( n % 3 )) && echo e
(( 1 == 1 && 2 == 2 )) && echo f
(( 1 == 2 || 3 == 3 )) && echo g
echo "$(( 6 / 2 + 1 ))"
i=5
echo "$(( i++ ))"
echo "$i"
echo "$(( ++i ))"
count=0
while (( count < 3 )); do
  echo "loop $count"
  (( count++ ))
done
