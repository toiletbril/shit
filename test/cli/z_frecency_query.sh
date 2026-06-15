unset SHIT_FLAGS
# z jumps to a directory ranked by the frecency store named by
# SHIT_DIRECTORY_HISTORY, prints the path it picks, errors when nothing matches,
# and skips a stale entry whose directory was removed. The store is seeded by
# hand, path then rank then last-access tab separated, so the ranking is fixed.
d=$(mktemp -d); store=$(mktemp)
printf '%s\t5\t9999999999\n' "$d" > "$store"
echo "== a query matches the seeded directory:"
SHIT_DIRECTORY_HISTORY="$store" "$BIN" -c "z $(basename "$d")" | sed "s#$d#TMPDIR#"
echo "== a query with no match errors:"
SHIT_DIRECTORY_HISTORY="$store" "$BIN" -c 'z no_such_dir_xyz'; echo "rc=$?"
echo "== a higher-ranked but removed entry is skipped:"
printf '%s\t9\t9999999999\n%s\t5\t9999999999\n' "$d/removed" "$d" > "$store"
SHIT_DIRECTORY_HISTORY="$store" "$BIN" -c "z $(basename "$d")" | sed "s#$d#TMPDIR#"
rm -rf "$d" "$store"
