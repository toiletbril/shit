#!/bin/sh
# The test builtin string and integer operators, checked against dash. The
# string predicates judge length and equality, the integer predicates compare
# numerically, and the negation and the and-or combine results.

# String length predicates.
[ -z "" ] && echo "empty_is_zero"
[ -n "x" ] && echo "nonempty_is_n"

# String equality and inequality.
[ abc = abc ] && echo "streq"
[ abc != abd ] && echo "strneq"

# Integer comparisons across the full set of operators.
[ 5 -eq 5 ] && echo "eq"
[ 5 -ne 6 ] && echo "ne"
[ 4 -lt 5 ] && echo "lt"
[ 5 -le 5 ] && echo "le"
[ 6 -gt 5 ] && echo "gt"
[ 5 -ge 5 ] && echo "ge"

# A negation flips the result.
[ ! -z "x" ] && echo "negated_zero"

# A combined expression evaluates both halves.
[ 1 -lt 2 ] && [ 2 -lt 3 ] && echo "chained_and"

# The single-bracket form reports the comparison status through the exit code.
if [ "abc" = "xyz" ]; then
    echo "wrong"
else
    echo "else_branch"
fi

# A numeric string compares as a number, not as text.
if [ 010 -eq 10 ]; then
    echo "numeric_equal"
fi
