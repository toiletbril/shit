unset SHIT_FLAGS
# touch sets the modification time of an existing file to the current time rather
# than passing it over. A hermetic temp directory keeps it stable, and it is left
# in place so the test never runs rm.
dir=$(mktemp -d)
: > "$dir/f"
before=$(stat -c '%Y' "$dir/f")
sleep 1.1
"$BIN" -c "shitbox touch '$dir/f'" </dev/null
after=$(stat -c '%Y' "$dir/f")
echo "== touch advances the modification time of an existing file:"
[ "$after" -gt "$before" ] && echo advanced || echo unchanged
echo "== touch -c still does not create a missing file:"
"$BIN" -c "shitbox touch -c '$dir/ghost'" </dev/null
"$BIN" -c "[ -e '$dir/ghost' ] && echo ghost-exists || echo ghost-missing" </dev/null
