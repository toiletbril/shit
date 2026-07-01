# The cd builtin completes only directories, never files, while another command
# still completes both. A hermetic temp directory keeps the candidates stable.
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir "$dir/alpha_dir" "$dir/beta_dir"
: > "$dir/alpha_file"
: > "$dir/beta_file"
cd "$dir"
echo "== cd offers only directories:"
"$BIN" --debug-complete-at 'cd alpha' </dev/null
echo "== cd with no prefix lists the directories:"
"$BIN" --debug-complete-at 'cd ' </dev/null
echo "== ls still offers files and directories:"
"$BIN" --debug-complete-at 'ls alpha' </dev/null
