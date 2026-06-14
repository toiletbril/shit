# Completion re-roots to the command line of the substitution the cursor sits
# in, so a token inside $(...), inside backticks, or inside the substitution of
# a compound head completes the inner command rather than the outer one. A
# registered spec and a controlled directory keep the candidates stable across
# machines.
echo "== inside \$( ):"
"$BIN" -c 'complete -W "alpha beta gamma" probecmd' --debug-complete-at 'echo $(probecmd a' </dev/null
echo "== inside backticks:"
"$BIN" -c 'complete -W "alpha beta gamma" probecmd' --debug-complete-at 'echo `probecmd b' </dev/null
echo "== inside a for-head substitution:"
"$BIN" -c 'complete -W "alpha beta gamma" probecmd' --debug-complete-at 'for x in $(probecmd g' </dev/null
echo "== filesystem still completes inside a substitution:"
dir=/tmp/shit_cis_dir
rm -rf "$dir"
mkdir -p "$dir"
: > "$dir/onlyfile"
"$BIN" --debug-complete-at "echo \$(cat $dir/only" </dev/null
rm -rf "$dir"
echo "== arithmetic is not a command body:"
"$BIN" -c 'complete -W "alpha beta gamma" probecmd' --debug-complete-at 'echo $((probecmd a' </dev/null
