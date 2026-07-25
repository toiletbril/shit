unset SHIT_FLAGS
# history -r <file> reads a named file into the list. The list is backed by its
# file, so the named file is merged into the backing history and the whole list
# is listed back.
d=$(mktemp -d)
printf 'existing one\nexisting two\n' > "$d/hist"
printf 'merged alpha\nmerged beta\n' > "$d/extra"
echo "== history -r <file> merges the named file into the list:"
SHIT_HISTORY="$d/hist" "$BIN" -c "history -r $d/extra; history"
echo "== history -r on a missing file errors:"
SHIT_HISTORY="$d/hist" "$BIN" -c \
    'history -r "$TEST_TEMP_DIRECTORY/no-such-history"; echo "rc=$?"' \
    2>/dev/null
[ -n "$d" ] && rm -rf "$d"
