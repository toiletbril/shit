#!/bin/bash
# Bash ternary and comma arithmetic operators inside $(( )), checked
# byte-for-byte against bash. Covers a plain ternary, a nested ternary, the
# comma operator yielding its last value, and a comma sequence with side
# effects.
echo "tern $(( 5 > 3 ? 10 : 20 )) $(( 5 < 3 ? 10 : 20 ))"
echo "nest $(( 1 ? 2 ? 30 : 40 : 50 )) $(( 0 ? 2 : 3 ? 60 : 70 ))"
echo "comma $(( 1 + 1, 2 + 2, 3 + 3 ))"
a=0; b=0
echo "seq $(( a = 5, b = a * 2, a + b ))"
echo "after $a $b"
n=2
echo "ternassign $(( n > 0 ? (n = 100) : (n = -100) ))"
echo "afterternary $n"
