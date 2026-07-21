dir=$(mktemp -d)
trap '[ -n "$dir" ] && /bin/rm -rf "$dir"' EXIT
/bin/cp "$BIN" "$dir/path_probe"

empty_result=$(PATH="$dir" "$BIN" --debug-complete-at '' </dev/null)
case "$empty_result" in
    *path_probe*) ;;
    *) exit 1 ;;
esac
echo "== empty command includes PATH programs"

segment_result=$(PATH="$dir" "$BIN" --debug-complete-at 'true; ' </dev/null)
case "$segment_result" in
    *path_probe*) ;;
    *) exit 1 ;;
esac
echo "== empty command segment includes PATH programs"
