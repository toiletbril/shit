# bash empties COMPREPLY before each completion, so a function that appends with
# COMPREPLY+=() does not inherit the entries of the previous completion on the
# same command. A stale COMPREPLY set before the run stands in for that previous
# completion, and it must not leak into the candidates. Without the reset, a
# bare nix tab that lists every subcommand poisons the next nix sear tab.
echo "== a stale COMPREPLY does not leak into an appending spec:"
"$BIN" -c "COMPREPLY=(stale_one stale_two); _f(){ COMPREPLY+=(search); }; complete -F _f gn" --debug-complete-at 'gn sea' </dev/null
