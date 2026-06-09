#!/bin/bash
# Bash bitwise arithmetic operators inside $(( )), checked byte-for-byte against
# bash. Covers complement, and, or, xor, and the two shifts, with negative
# operands and a precedence mix.
echo "not $(( ~0 )) $(( ~5 )) $(( ~-1 ))"
echo "and $(( 12 & 10 )) $(( 255 & 15 ))"
echo "or $(( 12 | 1 )) $(( 8 | 4 | 2 | 1 ))"
echo "xor $(( 12 ^ 6 )) $(( 5 ^ 5 ))"
echo "shl $(( 1 << 8 )) $(( 3 << 4 ))"
echo "shr $(( 256 >> 3 )) $(( -8 >> 1 ))"
echo "prec $(( 1 << 4 | 2 & 3 ^ 5 ))"
echo "mask $(( (1 << 5) - 1 ))"
