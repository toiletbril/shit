unset SHIT_FLAGS
# The optimizer prepass runs in the default mood, so --debug-optimizer traces
# what each pass folds. Each case is a separate process, so its stderr trace
# prints before its stdout result. The goldens assert the behavior of constant
# folding, arithmetic memoization through constant propagation, dead-branch
# elimination, and loop elimination, plus the cases that must not fold.

echo "=== constant arithmetic fold ==="
"$BIN" --debug-optimizer -c 'echo $((1 + 2 * 3))'

echo "=== constant propagation into arithmetic ==="
"$BIN" --debug-optimizer -c 'x=2; y=3; echo $((x + y))'

echo "=== dead branch, condition is true ==="
"$BIN" --debug-optimizer -c 'if true; then echo a; else echo b; fi'

echo "=== dead branch, all false folds to else ==="
"$BIN" --debug-optimizer -c 'if false; then echo a; else echo b; fi'

echo "=== while false is eliminated ==="
"$BIN" --debug-optimizer -c 'while false; do echo never; done; echo after'

echo "=== until true is eliminated ==="
"$BIN" --debug-optimizer -c 'until true; do echo never; done; echo after'

echo "=== runtime variable does not fold ==="
printf '' | "$BIN" --debug-optimizer -c 'read n; echo $((n + 1))'

echo "=== undecidable condition does not fold ==="
"$BIN" --debug-optimizer -c 'if [ -f /nonexistent_optimizer_probe ]; then echo a; fi; echo done'
