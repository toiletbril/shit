#!/bin/bash
# A (( that closes with a lone parenthesis is a subshell whose first child is
# a subshell, while a true arithmetic command keeps the (( reading, the
# disambiguation arith.tests runs at its line 191.
((echo abc; echo def;); echo ghi)
echo "subshell=$?"
((1+2)); echo "arith_true=$?"
((0)); echo "arith_false=$?"
(( (3>2) && (2>1) )); echo "nested=$?"
x=$(( (1+2) * 2 )); echo "x=$x"
