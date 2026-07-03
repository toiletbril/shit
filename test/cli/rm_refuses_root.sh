unset SHIT_FLAGS
# rm refuses a bare root operand even under -f, so a recursive remove cannot walk
# the whole filesystem from /, matching GNU rm under its default preserve-root.
# POLICY: every rm here uses --dry-run, so the test never deletes a real file. A
# normal operand is reported for removal under the dry run while the file
# survives.
dir=$(mktemp -d)
: > "$dir/keep"
echo "== rm -rf / is refused (count of the refusal):"
"$BIN" -c "shitbox rm -rf --dry-run /" </dev/null 2>&1 | grep -c "refusing to remove the root"
echo "== rm -rf // is refused (count):"
"$BIN" -c "shitbox rm -rf --dry-run //" </dev/null 2>&1 | grep -c "refusing to remove the root"
echo "== a normal file is reported for removal under --dry-run, and survives:"
"$BIN" -c "cd '$dir'; shitbox rm --dry-run keep" </dev/null 2>&1
"$BIN" -c "[ -e '$dir/keep' ] && echo keep-survives || echo keep-removed" </dev/null
[ -n "$dir" ] && rm -rf "$dir"
