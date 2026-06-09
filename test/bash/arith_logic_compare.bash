#!/bin/bash
# Bash logical and comparison arithmetic operators inside $(( )), checked
# byte-for-byte against bash. Covers the six comparisons, logical and, or, not,
# and short-circuit evaluation that leaves a side effect unrun.
echo "lt $(( 3 < 5 )) $(( 5 < 5 ))"
echo "le $(( 5 <= 5 )) $(( 6 <= 5 ))"
echo "gt $(( 6 > 2 )) $(( 2 > 6 ))"
echo "ge $(( 2 >= 9 )) $(( 9 >= 9 ))"
echo "eq $(( 4 == 4 )) $(( 4 == 5 ))"
echo "ne $(( 4 != 4 )) $(( 4 != 5 ))"
echo "and $(( 1 && 1 )) $(( 1 && 0 )) $(( 0 && 1 ))"
echo "or $(( 0 || 1 )) $(( 0 || 0 )) $(( 1 || 0 ))"
echo "not $(( !0 )) $(( !7 )) $(( !!7 ))"
x=5
(( 0 && (x = 99) ))
echo "shortand $x"
(( 1 || (x = 77) ))
echo "shortor $x"
