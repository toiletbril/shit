case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
cd "$dir"

echo "== URLs and SSH remote paths share the URL highlight:"
"$BIN" --debug-highlight-at 'echo https://example.com/a host:path user@host:/dir/file'
echo "== an unknown command keeps its own error color:"
"$BIN" --debug-highlight-at 'definitely_missing_command host:path'
echo "== a local colon path remains a local path:"
"$BIN" --debug-highlight-at 'echo ./missing:part'
echo "== a Windows drive path remains a local path:"
"$BIN" --debug-highlight-at 'echo C:/missing'
