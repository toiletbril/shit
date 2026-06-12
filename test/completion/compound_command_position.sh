# The compound keywords are transparent to command position, so the body of
# an if, while, until, then, else, elif, do, or brace group completes
# commands, and their argument stages ride through to the inner command.
echo "== if body:"
"$BIN" --debug-complete-at 'if expor' </dev/null
echo "== then body:"
"$BIN" --debug-complete-at 'if [ -f x ]; then expor' </dev/null
echo "== while body:"
"$BIN" --debug-complete-at 'while expor' </dev/null
echo "== do body:"
"$BIN" --debug-complete-at 'for f in a b; do expor' </dev/null
echo "== brace group:"
"$BIN" --debug-complete-at '{ expor' </dev/null
echo "== arguments through the keyword:"
"$BIN" --debug-complete-at 'while set -o no' </dev/null
