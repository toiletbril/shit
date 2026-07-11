unset SHIT_FLAGS
# help with no operand lists every builtin name one per line, a pattern that
# is a builtin renders that builtin's help the way --help renders it, and a
# name that is not a builtin is a located error pointing at the operand.
echo "== help with no operand lists builtins:"
"$BIN" -c 'help' | head -5
echo "== help cd renders the cd builtin help:"
"$BIN" -c 'help cd' | head -3
echo "== help nope is a located error:"
"$BIN" -c 'help nope' 2>&1; echo "rc=$?"
