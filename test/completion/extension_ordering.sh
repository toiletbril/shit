# For a known utility, an empty argument suggests only the file types it opens
# plus directories, a hard filter that keeps the listing short. A typed prefix
# keeps every match, floating the matching extension to the front while hiding
# nothing. A program with no table entry is unaffected.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
: > "$dir/archive.zip"
: > "$dir/ARCHIVE.ZIP"
: > "$dir/backup.tar.gz"
: > "$dir/notes.txt"
: > "$dir/data.csv"
mkdir "$dir/sub"
export PATH="$dir"
cd "$dir"
echo "== unzip with an empty argument shows only .zip files and directories:"
"$BIN" --debug-complete-at 'unzip ' </dev/null
echo "== tar with an empty argument shows only its archive types:"
"$BIN" --debug-complete-at 'tar ' </dev/null
echo "== a program with no table entry keeps plain alphabetical order:"
"$BIN" --debug-complete-at 'cat ' </dev/null
