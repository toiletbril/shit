#!/bin/bash
# Bash unary operators and grouping in $(( )), checked byte-for-byte against
# bash. Covers unary minus and plus, double negation, deeply nested
# parentheses, and the precedence override grouping forces.
echo "neg $(( -5 )) $(( -5 + -3 )) $(( - -5 ))"
echo "pos $(( +5 )) $(( +-5 ))"
echo "group $(( -(3 + 4) )) $(( -(2 * 3) + 1 ))"
echo "deep $(( ((((1 + 2)) * 3)) - ((4)) ))"
echo "over $(( 2 * (3 + 4) )) $(( (2 + 3) * (4 - 1) ))"
echo "divmod $(( 17 / 5 )) $(( 17 % 5 )) $(( -17 / 5 )) $(( -17 % 5 ))"
