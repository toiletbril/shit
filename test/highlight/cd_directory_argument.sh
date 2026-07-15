# A cd argument that names a directory colors like a valid path, while one that
# names a file colors red, the way fish marks a bad cd target. Another command
# colors an existing file as a valid path. A hermetic temp directory keeps the
# spans stable.
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir "$dir/adir"
: > "$dir/afile"
cd "$dir"
echo "== cd into a directory colors it as a valid path:"
"$BIN" --debug-highlight-at 'cd adir'
echo "== cd into a file colors it red:"
"$BIN" --debug-highlight-at 'cd afile'
echo "== ls of the same file stays a valid path:"
"$BIN" --debug-highlight-at 'ls afile'

if [ "${OS-}" = Windows_NT ]; then
    path_separator='\'
else
    path_separator='/'
fi
mkdir -p "$dir/native/subdir"
native_path="native${path_separator}subdir"
native_result=$("$BIN" --debug-highlight-at "echo $native_path")
case "$native_result" in
    *"$native_path"*) ;;
    *) exit 1 ;;
esac
printf '%s\n' "$native_result" | grep -q '\\e\[96m' || exit 1
echo "== native path separators highlight directories:"
