unset SHIT_FLAGS
# rm refuses a . or .. operand even under -f, so a recursive remove cannot delete
# the working or the parent directory entry. A hermetic temp directory keeps it
# stable, and it is left in place so the test never runs rm on a real tree.
dir=$(mktemp -d)
: > "$dir/keep"
echo "== rm -rf . is refused (count of the refusal):"
"$BIN" -c "cd '$dir'; shitbox rm -rf ." </dev/null 2>&1 | grep -c "refusing to remove"
echo "== rm -rf .. is refused (count):"
"$BIN" -c "cd '$dir'; shitbox rm -rf .." </dev/null 2>&1 | grep -c "refusing to remove"
echo "== the directory contents survive:"
"$BIN" -c "[ -e '$dir/keep' ] && echo keep-survives || echo keep-gone" </dev/null
