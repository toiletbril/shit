#!/bin/bash
# A [[ ]] operand error fails the conditional with status 2 for a non-integer
# -t and continues, and an arithmetic syntax error in a comparison fails the
# command without aborting, the lines cond.tests runs.
[[ -t X ]] 2>/dev/null
echo "tty_operand=$?"
[[ 7 -eq 4+ ]] 2>/dev/null
echo "arith_operand=$?"
[[ X -eq 4 ]] 2>/dev/null
echo "name_operand=$?"
echo survived
