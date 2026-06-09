#!/bin/bash
# Bash let return status, checked byte-for-byte against bash. A let whose last
# expression is zero returns status one, a nonzero result returns status zero,
# and a relational expression returns the negation of its truth value.
let 'a = 1'; echo "rc=$?"
let 'b = 0'; echo "rc=$?"
let 'c = 5 - 5'; echo "rc=$?"
let 'd = 3 > 2'; echo "rc=$?"
let 'e = 2 > 3'; echo "rc=$?"
( let 'x = 0' ); echo "subshell rc=$?"
