unset SHIT_FLAGS
# rm refuses a bare root operand even under -f, so a recursive remove cannot walk
# the whole filesystem from /, matching GNU rm under its default preserve-root. A
# hermetic temp directory confirms a normal file still removes, and it is left in
# place so the test never runs rm on a real tree.
dir=$(mktemp -d)
: > "$dir/keep"
echo "== rm -rf / is refused (count of the refusal):"
"$BIN" -c "shitbox rm -rf /" </dev/null 2>&1 | grep -c "refusing to remove the root"
echo "== rm -rf // is refused (count):"
"$BIN" -c "shitbox rm -rf //" </dev/null 2>&1 | grep -c "refusing to remove the root"
echo "== a normal file still removes:"
"$BIN" -c "shitbox rm '$dir/keep'" </dev/null
"$BIN" -c "[ -e '$dir/keep' ] && echo keep-survives || echo keep-removed" </dev/null
