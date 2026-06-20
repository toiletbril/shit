#!/bin/bash
# return outside a function and outside a sourced file is rejected and the shell
# continues, while return inside a function ends it with the status. The dot
# command passes its trailing operands to the sourced file as positional
# parameters and restores the caller's parameters afterward.
return 5
echo "after=$?"
f() { return 7; }
f
echo "fn=$?"
file=$(mktemp)
printf 'echo "in=$1 $2"\n' > "$file"
set -- keep1 keep2
. "$file" arg1 arg2
echo "out=$1 $2"
rm -f "$file"
