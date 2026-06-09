#!/bin/bash
# Bash (( )) arithmetic command exit status, checked byte-for-byte against bash.
# The command succeeds when the value is non-zero and fails when it is zero, so a
# negative value also succeeds. Covers the status in $?, in an if, and through
# the && and || connectives.
(( 5 )); echo "five rc=$?"
(( 0 )); echo "zero rc=$?"
(( -3 )); echo "neg rc=$?"
(( 2 - 2 )); echo "cancel rc=$?"
if (( 2 + 2 == 4 )); then echo "if true"; fi
if (( 1 == 2 )); then echo nope; else echo "if false"; fi
(( 3 > 1 )) && echo "and true"
(( 3 < 1 )) || echo "or false"
total=0
i=0
while (( i < 4 )); do
  (( total += i ))
  (( i++ ))
done
echo "total $total"
