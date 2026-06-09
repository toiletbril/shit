#!/bin/bash
# A bash shebang makes shit mimic bash, so bashisms work the way bash runs them.
[ a == a ] && echo double-equals-ok
test foo == foo && echo test-double-equals-ok
declare -A m; m[key]=value; echo "assoc=${m[key]}"
a=(one two three); echo "array=${a[@]}"; echo "count=${#a[@]}"
s=HELLO; echo "lower=${s,,}"
for i in 1 2 3; do printf '%s' "$i"; done; echo
