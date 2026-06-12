# A -W word list offers its dash entries only when the token already starts
# with a dash, the empty argument token completes the plain words, and a list
# reduced to nothing by the gate falls through to the filesystem.
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
touch "$dir/datafile"
cd "$dir"
echo "== empty token, mixed list:"
"$BIN" -c 'complete -W "alpha beta -x --why" mycmd' --debug-complete-at 'mycmd ' </dev/null
echo "== dash token, mixed list:"
"$BIN" -c 'complete -W "alpha beta -x --why" mycmd' --debug-complete-at 'mycmd -' </dev/null
echo "== empty token, flags-only list falls to files:"
"$BIN" -c 'complete -W "-x --why" flagcmd' --debug-complete-at 'flagcmd ' </dev/null
