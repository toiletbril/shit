#!/bin/bash
# Bash arithmetic assignment operators inside (( )), checked byte-for-byte
# against bash. Covers plain assignment and every compound form, the value the
# assignment yields, and chained assignment.
n=5
(( n = 100 ))
echo "set $n"
(( n += 7 )); echo "add $n"
(( n -= 12 )); echo "sub $n"
(( n *= 3 )); echo "mul $n"
(( n /= 4 )); echo "div $n"
(( n %= 50 )); echo "mod $n"
(( n <<= 4 )); echo "shl $n"
(( n >>= 2 )); echo "shr $n"
(( n &= 30 )); echo "and $n"
(( n |= 1 )); echo "or $n"
(( n ^= 6 )); echo "xor $n"
echo "yield $(( n += 10 ))"
a=1; b=2; c=3
(( a = b = c = 9 ))
echo "chain $a $b $c"
