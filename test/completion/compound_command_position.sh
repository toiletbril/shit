# The compound keywords are transparent to command position, so the body of
# an if, while, until, then, else, elif, do, or brace group completes
# commands, and their argument stages ride through to the inner command. A
# case pattern's unmatched closing paren opens the arm's body the same way,
# while a matched paren closes a substitution and stays an argument.
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
echo "== case arm body:"
"$BIN" --debug-complete-at 'case $x in a) expor' </dev/null
echo "== case arm with alternates:"
"$BIN" --debug-complete-at 'case $x in a|b) expor' </dev/null
echo "== arguments through the case arm:"
"$BIN" --debug-complete-at 'case $x in a) set -o no' </dev/null
echo "== matched paren stays an argument:"
"$BIN" --debug-complete-at 'echo $(ls) expor' </dev/null
