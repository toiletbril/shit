# A command separator inside quotes is not a command boundary, so the command
# segment holding the cursor stays whole and the argument still completes against
# the filesystem. Before the quote-aware scan the segment slice cut at the quoted
# ';' or '|', so the argument saw a broken command word and offered nothing. PATH
# is pinned to an empty directory so only the marker files join the candidates.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
: > "$dir/ZQmarker_file"
mkdir "$dir/ZQdir_marker"
export PATH="$dir"
cd "$dir"
echo "== plain argument lists both markers:"
"$BIN" --debug-complete-at 'echo ZQ' </dev/null
echo "== a quoted semicolon does not split the segment:"
"$BIN" --debug-complete-at 'echo "a;b" ZQ' </dev/null
echo "== a quoted pipe does not split the segment:"
"$BIN" --debug-complete-at "echo 'a|b' ZQ" </dev/null
echo "== a quoted ampersand does not split the segment:"
"$BIN" --debug-complete-at 'echo "a&b" ZQ' </dev/null
echo "== a real separator still starts a new command segment:"
"$BIN" --debug-complete-at 'true; echo ZQ' </dev/null
