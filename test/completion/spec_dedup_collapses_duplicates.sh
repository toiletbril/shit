# A completion spec may return the same value many times, the way a wrapped
# bash-completion helper does when it cannot narrow the current word. The
# duplicates are collapsed to one candidate, so a single distinct match inserts
# rather than listing the same name twice. The driver prints the candidates
# alone, so a single line here proves the collapse ran.
echo "== a repeated value collapses to one candidate:"
"$BIN" -c "_f(){ COMPREPLY=(search search search search); }; complete -F _f gn" --debug-complete-at 'gn sea' </dev/null
echo "== duplicates among distinct values are each collapsed:"
"$BIN" -c "_f(){ COMPREPLY=(build search profile search flake profile search); }; complete -F _f gn" --debug-complete-at 'gn ' </dev/null
