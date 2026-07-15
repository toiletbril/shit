case "$BIN" in
/*) ;;
*) BIN=$(pwd)/$BIN ;;
esac

d=$(mktemp -d) || exit 1
trap '[ -n "$d" ] && /bin/rm -rf "$d"' EXIT

: > "$d/program"
ln -s program "$d/link"
mkdir "$d/directory"
cd "$d" || exit 1

echo '--- file ---'
"$BIN" --debug-highlight-at './program/'
echo '--- symlink to file ---'
"$BIN" --debug-highlight-at './link/'
echo '--- directory ---'
"$BIN" --debug-highlight-at './directory/'
