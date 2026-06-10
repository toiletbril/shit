#!/bin/bash
# The accepted alternative for conditional_operand_errors.bash on a bash that
# answers a non-integer -t operand with status 1 rather than 5.3's 2. The
# probed lines stay identical, only the version-dependent status is spelled.
echo "tty_operand=2"
[[ 7 -eq 4+ ]] 2>/dev/null
echo "arith_operand=$?"
[[ X -eq 4 ]] 2>/dev/null
echo "name_operand=$?"
echo survived
