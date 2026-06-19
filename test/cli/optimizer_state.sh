unset SHIT_FLAGS
# The --show-optimizer-state flag prints the optimizer state and a located line
# for every node the analysis stage eliminated, then a final summary, all to
# standard error. Each case is a separate process, so its stderr dump prints
# before its stdout result. The goldens assert the located elimination lines and
# the state summary across compound-body elimination, C-style for folding, and
# the empty for loop.

echo "=== if with no reachable body is eliminated ==="
"$BIN" --show-optimizer-state -c 'if false; then echo a; fi; echo done'

echo "=== for over an empty list is eliminated ==="
"$BIN" --show-optimizer-state -c 'for x in; do echo a; done; echo done'

echo "=== c-style for with a blank init and a zero condition is eliminated ==="
"$BIN" --show-optimizer-state -c 'for ((; 0; i++)); do echo a; done; echo done'

echo "=== c-style for with a non-blank init folds but keeps the init ==="
"$BIN" --show-optimizer-state -c 'for ((i=5; 0; i++)); do echo a; done; echo done'

echo "=== while false is eliminated ==="
"$BIN" --show-optimizer-state -c 'while false; do echo a; done; echo done'
