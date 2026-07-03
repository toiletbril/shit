unset SHIT_FLAGS
# rm refuses a . or .. operand even under -f, so a recursive remove cannot delete
# the working or the parent directory entry. POLICY: every rm here uses
# --dry-run, so the test never deletes a real file. The refusal fires before any
# removal, so the count is unchanged under a dry run.
dir=$(mktemp -d)
: > "$dir/keep"
echo "== rm -rf . is refused (count of the refusal):"
"$BIN" -c "cd '$dir'; shitbox rm -rf --dry-run ." </dev/null 2>&1 | grep -c "refusing to remove"
echo "== rm -rf .. is refused (count):"
"$BIN" -c "cd '$dir'; shitbox rm -rf --dry-run .." </dev/null 2>&1 | grep -c "refusing to remove"
echo "== the directory contents survive:"
"$BIN" -c "[ -e '$dir/keep' ] && echo keep-survives || echo keep-gone" </dev/null
[ -n "$dir" ] && rm -rf "$dir"
